#include "SearchLibrary.h"
#include "NameTBD/Search/SearchableItemInterface.h"


namespace
{
	// Scoring weights. Tuned so that "matched the very start" and "matched a whole run of
	// characters together" both beat "matched the same letters scattered across the string",
	// which is the difference between a result the player expected and one that merely
	// technically qualifies.
	constexpr int32 ScoreConsecutive   = 15; // each char immediately after a previous match
	constexpr int32 ScoreWordStart     = 10; // char begins a word (after space/punct/camelCase)
	constexpr int32 ScoreStringStart   = 20; // char is the candidate's very first
	constexpr int32 ScoreBase          = 1;  // any match at all
	constexpr int32 PenaltyPerSkipped  = -1; // unmatched char passed over while searching

	bool IsWordBoundaryAt(const FString& Candidate, int32 Index)
	{
		if (Index <= 0)
			return true;

		const TCHAR previous = Candidate[Index - 1];
		const TCHAR current = Candidate[Index];

		// A space or punctuation before us starts a new word, and so does a lowercase
		// character before an uppercase one — otherwise "hp" would miss the second word
		// in names written as "HealthPotion" rather than "Health Potion".
		if (FChar::IsWhitespace(previous) || FChar::IsPunct(previous) || previous == TEXT('_'))
			return true;

		return FChar::IsLower(previous) && FChar::IsUpper(current);
	}
}

bool USearchLibrary::MatchInternal(const FString& Query, const FString& Candidate, int32& OutScore)
{
	OutScore = 0;

	// No query means no filtering. Returning true here is what lets an empty search box
	// show everything instead of nothing.
	if (Query.IsEmpty())
		return true;

	if (Candidate.IsEmpty())
		return false;

	int32 queryIndex = 0;
	int32 previousMatchIndex = INDEX_NONE;

	for (int32 candidateIndex = 0; candidateIndex < Candidate.Len() && queryIndex < Query.Len(); ++candidateIndex)
	{
		const TCHAR queryChar = FChar::ToLower(Query[queryIndex]);
		const TCHAR candidateChar = FChar::ToLower(Candidate[candidateIndex]);

		if (queryChar != candidateChar)
		{
			OutScore += PenaltyPerSkipped;
			continue;
		}

		int32 charScore = ScoreBase;

		if (candidateIndex == 0)
			charScore += ScoreStringStart;
		else if (IsWordBoundaryAt(Candidate, candidateIndex))
			charScore += ScoreWordStart;

		if (previousMatchIndex != INDEX_NONE && candidateIndex == previousMatchIndex + 1)
			charScore += ScoreConsecutive;

		OutScore += charScore;
		previousMatchIndex = candidateIndex;
		++queryIndex;
	}

	// Ran out of candidate before consuming the whole query — some query character never
	// appeared (in order), so this isn't a match at all and the score is meaningless.
	if (queryIndex < Query.Len())
	{
		OutScore = 0;
		return false;
	}

	// Shorter candidates are better matches for the same query: typing "potion" should
	// rank "Potion" above "Potion of Extraordinary Regeneration".
	OutScore += FMath::Max(0, 30 - Candidate.Len());

	return true;
}

bool USearchLibrary::FuzzyMatch(const FString& Query, const FString& Candidate)
{
	int32 unusedScore = 0;
	return MatchInternal(Query, Candidate, unusedScore);
}

bool USearchLibrary::FuzzyMatchWithScore(const FString& Query, const FString& Candidate, int32& OutScore)
{
	return MatchInternal(Query, Candidate, OutScore);
}

void USearchLibrary::FuzzyFilterRanked(const FString& Query, const TArray<FString>& Candidates,
                                       TArray<int32>& OutMatchingIndices)
{
	OutMatchingIndices.Reset();

	TArray<TPair<int32, int32>> scored; // (index, score)
	scored.Reserve(Candidates.Num());

	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		int32 score = 0;
		if (MatchInternal(Query, Candidates[i], score))
			scored.Emplace(i, score);
	}

	// StableSort so that equal-scoring results keep the caller's original ordering —
	// which for an inventory means whatever sort layers the view was already built with,
	// rather than an arbitrary reshuffle every keystroke.
	scored.StableSort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
	{
		return A.Value > B.Value;
	});

	OutMatchingIndices.Reserve(scored.Num());
	for (const TPair<int32, int32>& entry : scored)
		OutMatchingIndices.Add(entry.Key);
}

void USearchLibrary::FilterSearchableObjects(const FString& Query, FName ActiveFilter, const TArray<UObject*>& UnfilteredObjects, TArray<UObject*>& OutFilteredObjects)
{
	OutFilteredObjects.Reset();

	TArray<FString> ExtractedStrings;
	TArray<UObject*> PreFiltered;

	// 1. Pre-filter by dropdown and extract the strings automatically
	for (UObject* Obj : UnfilteredObjects)
	{
		if (Obj && Obj->Implements<USearchableItemInterface>())
		{
			// Check the dropdown filter
			FName Category = ISearchableItemInterface::Execute_GetSearchFilterCategory(Obj);
			if (ActiveFilter != NAME_None && ActiveFilter != Category)
			{
				continue;
			}

			PreFiltered.Add(Obj);
			ExtractedStrings.Add(ISearchableItemInterface::Execute_GetSearchText(Obj));
		}
	}

	// 2. Perform the fuzzy search math
	TArray<int32> MatchedIndices;
	FuzzyFilterRanked(Query, ExtractedStrings, MatchedIndices);

	// 3. Rebuild the final array of UObjects in the correct sorted order
	OutFilteredObjects.Reserve(MatchedIndices.Num());
	for (int32 Index : MatchedIndices)
	{
		OutFilteredObjects.Add(PreFiltered[Index]);
	}
}


// =====================================================================================================
// Matching with options
// =====================================================================================================

namespace SearchLibraryOptionsPrivate
{
	/** U+00C0..U+00FF and U+0100..U+017F folded to their plain letters, one for one; '_' keeps the character (x and the division sign). */
	const TCHAR* const Latin1Folds = TEXT("AAAAAAACEEEEIIIIDNOOOOO_OUUUUYTsaaaaaaaceeeeiiiidnooooo_ouuuuyty");
	const TCHAR* const LatinExtendedAFolds = TEXT("AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs");

	TCHAR FoldAccent(TCHAR Char)
	{
		const uint32 Code = static_cast<uint32>(Char);
		if (Code >= 0xC0 && Code <= 0xFF)
		{
			const TCHAR Folded = Latin1Folds[Code - 0xC0];
			return Folded == TEXT('_') ? Char : Folded;
		}
		if (Code >= 0x100 && Code <= 0x17F)
		{
			return LatinExtendedAFolds[Code - 0x100];
		}
		return Char;
	}

	/** Case kept, accents folded (when asked): what word boundaries are read from. */
	FString FoldOnly(const FString& Text, const FSearchMatchOptions& Options)
	{
		if (!Options.bIgnoreAccents)
		{
			return Text;
		}
		FString Out = Text;
		for (TCHAR& Char : Out)
		{
			Char = FoldAccent(Char);
		}
		return Out;
	}

	bool IsBoundary(const FString& Original, int32 Index)
	{
		if (Index <= 0)
		{
			return true;
		}
		const TCHAR Previous = Original[Index - 1];
		const TCHAR Current = Original[Index];
		if (FChar::IsWhitespace(Previous) || FChar::IsPunct(Previous) || Previous == TEXT('_'))
		{
			return true;
		}
		// CamelCase and digits after letters (Potion2) start words too.
		return (FChar::IsLower(Previous) && FChar::IsUpper(Current)) || (FChar::IsAlpha(Previous) && FChar::IsDigit(Current));
	}

	/** Runs of matched characters, sorted and merged where they touch or overlap. */
	void MergeRanges(TArray<FIntPoint>& Ranges)
	{
		Ranges.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X < B.X; });
		TArray<FIntPoint> Merged;
		for (const FIntPoint& Range : Ranges)
		{
			if (Merged.Num() > 0 && Range.X <= Merged.Last().X + Merged.Last().Y)
			{
				FIntPoint& Last = Merged.Last();
				Last.Y = FMath::Max(Last.X + Last.Y, Range.X + Range.Y) - Last.X;
			}
			else
			{
				Merged.Add(Range);
			}
		}
		Ranges = MoveTemp(Merged);
	}

	/** Every '<' and '&' made literal for a Rich Text Block. */
	FString EscapeRich(const FString& Text)
	{
		FString Out;
		Out.Reserve(Text.Len());
		for (const TCHAR Char : Text)
		{
			if (Char == TEXT('&'))
			{
				Out += TEXT("&amp;");
			}
			else if (Char == TEXT('<'))
			{
				Out += TEXT("&lt;");
			}
			else if (Char == TEXT('>'))
			{
				Out += TEXT("&gt;");
			}
			else
			{
				Out.AppendChar(Char);
			}
		}
		return Out;
	}
}

FString USearchLibrary::NormalizeForSearch(const FString& Text, const FSearchMatchOptions& Options)
{
	FString Out = SearchLibraryOptionsPrivate::FoldOnly(Text, Options);
	if (!Options.bCaseSensitive)
	{
		Out.ToLowerInline();
	}
	return Out;
}

bool USearchLibrary::MatchTerm(const FString& Term, const FString& Normalized, const FString& Original, ESearchMatchMode Mode, int32& OutScore, TArray<FIntPoint>* OutRanges)
{
	using namespace SearchLibraryOptionsPrivate;

	OutScore = 0;
	const int32 TermLength = Term.Len();
	const int32 Length = Normalized.Len();
	if (TermLength == 0)
	{
		return true;
	}
	if (Length < TermLength)
	{
		return false;
	}

	switch (Mode)
	{
	case ESearchMatchMode::Exact:
		if (Normalized != Term)
		{
			return false;
		}
		OutScore = 100;
		if (OutRanges)
		{
			OutRanges->Add(FIntPoint(0, Length));
		}
		return true;

	case ESearchMatchMode::StartsWith:
		if (!Normalized.StartsWith(Term, ESearchCase::CaseSensitive))
		{
			return false;
		}
		OutScore = 60 + TermLength * 2;
		if (OutRanges)
		{
			OutRanges->Add(FIntPoint(0, TermLength));
		}
		return true;

	case ESearchMatchMode::Contains:
	case ESearchMatchMode::WordStarts:
	{
		// The best place it occurs: the very start, then the start of a word, then the earliest.
		int32 Best = INDEX_NONE;
		int32 BestScore = TNumericLimits<int32>::Lowest();
		for (int32 From = Normalized.Find(Term, ESearchCase::CaseSensitive); From != INDEX_NONE;
			From = Normalized.Find(Term, ESearchCase::CaseSensitive, ESearchDir::FromStart, From + 1))
		{
			const bool bBoundary = IsBoundary(Original, From);
			if (Mode == ESearchMatchMode::WordStarts && !bBoundary)
			{
				continue;
			}
			const int32 Score = (From == 0 ? 40 : bBoundary ? 25 : 10) + TermLength * 2 - FMath::Min(From, 20) / 4;
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = From;
			}
		}
		if (Best == INDEX_NONE)
		{
			return false;
		}
		OutScore = BestScore;
		if (OutRanges)
		{
			OutRanges->Add(FIntPoint(Best, TermLength));
		}
		return true;
	}

	case ESearchMatchMode::Fuzzy:
	default:
	{
		// The subsequence match of MatchInternal, tried from every place the term's first letter occurs and the best kept -
		// so "pot" in "Spot Potion" lands on the word Potion, not on the letters of Spot.
		constexpr int32 Consecutive = 15;
		constexpr int32 WordStart = 10;
		constexpr int32 StringStart = 20;
		constexpr int32 Base = 1;
		constexpr int32 Skipped = -1;

		int32 BestScore = TNumericLimits<int32>::Lowest();
		TArray<int32> BestPicks;
		TArray<int32> Picks;
		for (int32 Start = 0; Start <= Length - TermLength; ++Start)
		{
			if (Normalized[Start] != Term[0])
			{
				continue;
			}
			Picks.Reset();
			int32 Score = 0;
			int32 TermIndex = 0;
			int32 Previous = INDEX_NONE;
			for (int32 Index = Start; Index < Length && TermIndex < TermLength; ++Index)
			{
				if (Normalized[Index] != Term[TermIndex])
				{
					// Letters passed over only count between matched ones: where the match starts isn't a penalty.
					Score += Previous != INDEX_NONE ? Skipped : 0;
					continue;
				}
				int32 CharScore = Base;
				CharScore += Index == 0 ? StringStart : IsBoundary(Original, Index) ? WordStart : 0;
				CharScore += (Previous != INDEX_NONE && Index == Previous + 1) ? Consecutive : 0;
				Score += CharScore;
				Picks.Add(Index);
				Previous = Index;
				++TermIndex;
			}
			if (TermIndex < TermLength)
			{
				// No later start can finish either: it has even less of the candidate left.
				break;
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				BestPicks = Picks;
			}
		}
		if (BestPicks.Num() == 0)
		{
			return false;
		}
		OutScore = BestScore;
		if (OutRanges)
		{
			for (const int32 Pick : BestPicks)
			{
				OutRanges->Add(FIntPoint(Pick, 1));
			}
		}
		return true;
	}
	}
}

bool USearchLibrary::MatchQuery(const FString& Query, const FString& Candidate, const FSearchMatchOptions& Options, int32& OutScore, TArray<FIntPoint>* OutRanges)
{
	OutScore = 0;
	const FString Trimmed = Query.TrimStartAndEnd();
	FString Compact = Trimmed;
	Compact.ReplaceInline(TEXT(" "), TEXT(""));
	if (Trimmed.IsEmpty() || Compact.Len() < Options.MinQueryLength)
	{
		// Nothing (or too little) typed: everything shows, nothing is highlighted.
		return true;
	}
	if (Candidate.IsEmpty())
	{
		return false;
	}

	const FString Original = SearchLibraryOptionsPrivate::FoldOnly(Candidate, Options);
	const FString Normalized = NormalizeForSearch(Candidate, Options);
	const FString NormalizedQuery = NormalizeForSearch(Trimmed, Options);

	TArray<FString> Terms;
	if (Options.bMatchEveryWord && Options.MatchMode != ESearchMatchMode::Exact)
	{
		NormalizedQuery.ParseIntoArrayWS(Terms);
	}
	else
	{
		Terms.Add(NormalizedQuery);
	}

	const FString CompareAs = Options.MatchMode == ESearchMatchMode::Exact ? Normalized.TrimStartAndEnd() : Normalized;
	for (const FString& Term : Terms)
	{
		int32 TermScore = 0;
		if (!MatchTerm(Term, CompareAs, Original, Options.MatchMode, TermScore, OutRanges))
		{
			OutScore = 0;
			if (OutRanges)
			{
				OutRanges->Reset();
			}
			return false;
		}
		OutScore += TermScore;
	}

	// Shorter candidates are the better match for the same query, as in MatchInternal.
	OutScore += FMath::Max(0, 30 - Candidate.Len());
	if (OutRanges)
	{
		SearchLibraryOptionsPrivate::MergeRanges(*OutRanges);
	}
	return true;
}

bool USearchLibrary::MatchWithOptions(const FString& Query, const FString& Candidate, const FSearchMatchOptions& Options, int32& OutScore)
{
	return MatchQuery(Query, Candidate, Options, OutScore, nullptr);
}

bool USearchLibrary::MatchAny(const FString& Query, TConstArrayView<FString> Texts, const FSearchMatchOptions& Options, int32& OutScore)
{
	OutScore = 0;
	bool bAny = false;
	int32 Best = TNumericLimits<int32>::Lowest();
	for (int32 Index = 0; Index < Texts.Num(); ++Index)
	{
		int32 Score = 0;
		if (MatchQuery(Query, Texts[Index], Options, Score, nullptr))
		{
			// A keyword match ranks under the same match on the label itself.
			Score -= Index > 0 ? 5 : 0;
			Best = FMath::Max(Best, Score);
			bAny = true;
		}
	}
	OutScore = bAny ? Best : 0;
	return bAny;
}

void USearchLibrary::FilterRankedWithOptions(const FString& Query, const TArray<FString>& Candidates, const FSearchMatchOptions& Options, bool bBestFirst,
	TArray<int32>& OutMatchingIndices)
{
	OutMatchingIndices.Reset();
	TArray<TPair<int32, int32>> Scored;
	Scored.Reserve(Candidates.Num());
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		int32 Score = 0;
		if (MatchQuery(Query, Candidates[Index], Options, Score, nullptr))
		{
			Scored.Emplace(Index, Score);
		}
	}
	if (bBestFirst)
	{
		Scored.StableSort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B) { return A.Value > B.Value; });
	}
	OutMatchingIndices.Reserve(Scored.Num());
	for (const TPair<int32, int32>& Entry : Scored)
	{
		OutMatchingIndices.Add(Entry.Key);
	}
}

TArray<FIntPoint> USearchLibrary::GetMatchRanges(const FString& Query, const FString& Candidate, const FSearchMatchOptions& Options)
{
	TArray<FIntPoint> Ranges;
	int32 Score = 0;
	if (!MatchQuery(Query, Candidate, Options, Score, &Ranges))
	{
		Ranges.Reset();
	}
	return Ranges;
}

FString USearchLibrary::HighlightMatches(const FString& Query, const FString& Candidate, const FSearchMatchOptions& Options, const FString& StyleName)
{
	using namespace SearchLibraryOptionsPrivate;

	const TArray<FIntPoint> Ranges = GetMatchRanges(Query, Candidate, Options);
	FString Out;
	int32 Cursor = 0;
	for (const FIntPoint& Range : Ranges)
	{
		const int32 Start = FMath::Clamp(Range.X, Cursor, Candidate.Len());
		const int32 End = FMath::Clamp(Range.X + Range.Y, Start, Candidate.Len());
		Out += EscapeRich(Candidate.Mid(Cursor, Start - Cursor));
		if (End > Start)
		{
			Out += FString::Printf(TEXT("<%s>%s</>"), *StyleName, *EscapeRich(Candidate.Mid(Start, End - Start)));
		}
		Cursor = End;
	}
	Out += EscapeRich(Candidate.Mid(Cursor));
	return Out;
}

void USearchLibrary::FilterSearchableObjectsWithOptions(const FString& Query, FName ActiveFilter, const TArray<UObject*>& UnfilteredObjects,
	const FSearchMatchOptions& Options, bool bBestFirst, TArray<UObject*>& OutFilteredObjects)
{
	OutFilteredObjects.Reset();
	TArray<TPair<UObject*, int32>> Scored;
	TArray<FString> Texts;
	for (UObject* Object : UnfilteredObjects)
	{
		if (!Object || !Object->Implements<USearchableItemInterface>())
		{
			continue;
		}
		if (ActiveFilter != NAME_None && ActiveFilter != ISearchableItemInterface::Execute_GetSearchFilterCategory(Object))
		{
			continue;
		}
		Texts.Reset();
		Texts.Add(ISearchableItemInterface::Execute_GetSearchText(Object));
		if (Options.bSearchKeywords)
		{
			Texts.Append(ISearchableItemInterface::Execute_GetSearchKeywords(Object));
		}
		int32 Score = 0;
		if (MatchAny(Query, Texts, Options, Score))
		{
			Scored.Emplace(Object, Score);
		}
	}
	if (bBestFirst)
	{
		Scored.StableSort([](const TPair<UObject*, int32>& A, const TPair<UObject*, int32>& B) { return A.Value > B.Value; });
	}
	OutFilteredObjects.Reserve(Scored.Num());
	for (const TPair<UObject*, int32>& Entry : Scored)
	{
		OutFilteredObjects.Add(Entry.Key);
	}
}
