// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimParts.h"

#include "Internationalization/BreakIterator.h"

const FName WidgetAnimPartUnits::Letter(TEXT("Letter"));
const FName WidgetAnimPartUnits::Word(TEXT("Word"));
const FName WidgetAnimPartUnits::Line(TEXT("Line"));

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
