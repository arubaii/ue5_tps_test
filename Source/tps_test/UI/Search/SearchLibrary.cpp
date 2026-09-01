#include "SearchLibrary.h"

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
