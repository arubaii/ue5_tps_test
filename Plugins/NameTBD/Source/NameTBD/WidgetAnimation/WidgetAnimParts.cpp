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

void FWidgetAnimTextTarget::FindLetterRanges(const FString& Display, TArray<FIntPoint>& OutRanges) const
{
	OutRanges.Reset();
	if (Text.IsEmpty() || Display.IsEmpty())
	{
		return;
	}

	// Every place it is, in characters, one after another and never overlapping - "aa" in "aaaa" is two places, not three.
	const ESearchCase::Type SearchCase = bMatchCase ? ESearchCase::CaseSensitive : ESearchCase::IgnoreCase;
	TArray<FIntPoint, TInlineAllocator<4>> Places;
	for (int32 From = 0; From <= Display.Len() - Text.Len();)
	{
		const int32 Found = Display.Find(Text, SearchCase, ESearchDir::FromStart, From);
		if (Found == INDEX_NONE)
		{
			break;
		}
		const int32 End = Found + Text.Len();

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
		return;
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
			return;
		}
		FirstPlace = LastPlace = Number - 1;
		break;
	default: break;
	}

	// Each as the letters it covers - white space is never a letter, so "Hello World" is ten of them in one run.
	TArray<WidgetAnimTextParts::FRange> Letters;
	WidgetAnimTextParts::GetLetters(Display, Letters);
	for (int32 Place = FirstPlace; Place <= LastPlace; ++Place)
	{
		const FIntPoint& Chars = Places[Place];
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
