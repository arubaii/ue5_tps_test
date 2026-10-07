// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimParts.h"

#include "Internationalization/BreakIterator.h"

const FName WidgetAnimPartUnits::Letter(TEXT("Letter"));
const FName WidgetAnimPartUnits::Word(TEXT("Word"));
const FName WidgetAnimPartUnits::Line(TEXT("Line"));
const FName WidgetAnimPartUnits::Span(TEXT("Span"));

void WidgetAnimTextParts::GetLetters(const FString& Text, TArray<FRange>& OutLetters)
{
	OutLetters.Reset();
	if (Text.IsEmpty())
	{
		return;
	}

	// Grapheme clusters rather than characters: an accented letter written as a letter plus a combining mark, or an
	// emoji made of several code points, is one letter to whoever reads it, and must fade or pop as one. One iterator,
	// kept: creating one builds an ICU break iterator, and this runs whenever a text changes. Game thread only, like
	// everything that touches widgets.
	static TSharedPtr<IBreakIterator> Iterator;
	if (!Iterator.IsValid())
	{
		Iterator = FBreakIterator::CreateCharacterBoundaryIterator();
	}

	Iterator->SetString(FString(Text));
	int32 Begin = Iterator->ResetToBeginning();
	for (int32 End = Iterator->MoveToNext(); End != INDEX_NONE && End > Begin; Begin = End, End = Iterator->MoveToNext())
	{
		// White space is never a letter: a space has nothing to fade in, and a stagger counting it would pause on
		// every gap between words.
		bool bVisible = false;
		for (int32 Index = Begin; Index < End && Index < Text.Len(); ++Index)
		{
			if (!FChar::IsWhitespace(Text[Index]))
			{
				bVisible = true;
				break;
			}
		}

		if (bVisible)
		{
			OutLetters.Add({ Begin, End });
		}
	}

	// Nothing stays pointing at the caller's text.
	Iterator->ClearString();
}

void WidgetAnimTextParts::GetWords(const TArray<FRange>& Letters, TArray<FRange>& OutWords, TArray<int32>* OutWordOfLetter)
{
	OutWords.Reset();
	if (OutWordOfLetter)
	{
		OutWordOfLetter->Reset(Letters.Num());
	}

	for (int32 LetterIndex = 0; LetterIndex < Letters.Num(); ++LetterIndex)
	{
		// Anything between two letters is white space - only white space is left out of the letters - so a gap
		// between them is where one word ends and the next begins.
		const bool bContinues = LetterIndex > 0 && Letters[LetterIndex].Begin == Letters[LetterIndex - 1].End;
		if (bContinues && OutWords.Num() > 0)
		{
			OutWords.Last().End = Letters[LetterIndex].End;
		}
		else
		{
			OutWords.Add(Letters[LetterIndex]);
		}

		if (OutWordOfLetter)
		{
			OutWordOfLetter->Add(OutWords.Num() - 1);
		}
	}
}

void FWidgetAnimTextTarget::GetParts(TArray<FString>& OutParts) const
{
	OutParts.Reset();
	TArray<FString> Pieces;
	Text.ParseIntoArray(Pieces, PartSeparator, /*bCullEmpty=*/false);
	for (FString& Piece : Pieces)
	{
		Piece.TrimStartAndEndInline();
		if (!Piece.IsEmpty())
		{
			OutParts.Add(MoveTemp(Piece));
		}
	}
}

void FWidgetAnimTextTarget::FindLetterRanges(const FString& Display, TArray<FIntPoint>& OutRanges) const
{
	OutRanges.Reset();
	if (Text.IsEmpty() || Display.IsEmpty())
	{
		return;
	}

	// Each part on its own - "Hello && Big World" is "Hello", then "Big World" - with every rule applied to each.
	TArray<FString> Parts;
	GetParts(Parts);

	const ESearchCase::Type SearchCase = bMatchCase ? ESearchCase::CaseSensitive : ESearchCase::IgnoreCase;
	TArray<FIntPoint, TInlineAllocator<8>> Chosen;
	for (const FString& Part : Parts)
	{
		// Every place it is, in characters, one after another and never overlapping - "aa" in "aaaa" is two places, not three.
		TArray<FIntPoint, TInlineAllocator<4>> Places;
		for (int32 From = 0; From <= Display.Len() - Part.Len();)
		{
			const int32 Found = Display.Find(Part, SearchCase, ESearchDir::FromStart, From);
			if (Found == INDEX_NONE)
			{
				break;
			}
			const int32 End = Found + Part.Len();

			// A whole word has no letter or digit right before it or right after it.
			const bool bWholeWord = (Found == 0 || !FChar::IsAlnum(Display[Found - 1])) && (End >= Display.Len() || !FChar::IsAlnum(Display[End]));
			if (!bWholeWords || bWholeWord)
			{
				Places.Add(FIntPoint(Found, End));
				From = End;
			}
			else
			{
				From = Found + 1;
			}
		}
		if (Places.Num() == 0)
		{
			continue;
		}

		// The ones asked for.
		int32 FirstPlace = 0;
		int32 LastPlace = Places.Num() - 1;
		switch (Occurrence)
		{
		case EWidgetAnimTextOccurrence::First:  LastPlace = 0; break;
		case EWidgetAnimTextOccurrence::Last:   FirstPlace = LastPlace; break;
		case EWidgetAnimTextOccurrence::Number:
			if (!Places.IsValidIndex(Number - 1))
			{
				continue;
			}
			FirstPlace = LastPlace = Number - 1;
			break;
		default: break;
		}
		for (int32 Place = FirstPlace; Place <= LastPlace; ++Place)
		{
			Chosen.Add(Places[Place]);
		}
	}
	if (Chosen.Num() == 0)
	{
		return;
	}

	// In the order they are in the text, and two parts that overlap or touch are one place - each place moves as a piece.
	Chosen.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X < B.X; });
	TArray<FIntPoint, TInlineAllocator<8>> Merged;
	for (const FIntPoint& Place : Chosen)
	{
		if (Merged.Num() > 0 && Place.X <= Merged.Last().Y)
		{
			Merged.Last().Y = FMath::Max(Merged.Last().Y, Place.Y);
		}
		else
		{
			Merged.Add(Place);
		}
	}

	// Each as the letters it covers - white space is never a letter, so "Hello World" is ten of them in one run.
	TArray<WidgetAnimTextParts::FRange> Letters;
	WidgetAnimTextParts::GetLetters(Display, Letters);
	for (const FIntPoint& Chars : Merged)
	{
		int32 First = INDEX_NONE;
		int32 Count = 0;
		for (int32 LetterIndex = 0; LetterIndex < Letters.Num(); ++LetterIndex)
		{
			if (Letters[LetterIndex].Begin < Chars.Y && Letters[LetterIndex].End > Chars.X)
			{
				First = First == INDEX_NONE ? LetterIndex : First;
				++Count;
			}
		}
		if (Count > 0)
		{
			OutRanges.Add(FIntPoint(First, Count));
		}
	}
}
