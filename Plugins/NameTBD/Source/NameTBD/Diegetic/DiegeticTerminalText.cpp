// Copyright AR GameSystems 2026. All rights reserved.

#include "DiegeticTerminalText.h"

#include "DiegeticScreenComponent.h"
#include "NameTBD/Sound/UISoundTypes.h"
#include "Sound/SoundBase.h"

#define LOCTEXT_NAMESPACE "DiegeticTerminalText"

namespace DiegeticTerminalTextPrivate
{
	/** A little pitch spread on the key clicks, so a long line doesn't buzz. */
	constexpr float ClickPitchSpread = 0.06f;
}

UDiegeticTerminalText::UDiegeticTerminalText(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Phosphor green until set otherwise.
	SetColorAndOpacity(FSlateColor(FLinearColor(0.3f, 1.f, 0.5f)));
}

#if WITH_EDITOR
const FText UDiegeticTerminalText::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}
#endif

void UDiegeticTerminalText::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	// Its own Text typed out the first time it is shown in a game: a boot sequence written in the designer.
	if (!IsDesignTime() && bTypeTextWhenShown && !bTypedAuthoredText)
	{
		bTypedAuthoredText = true;
		const FString Authored = GetText().ToString();
		ShownLines.Reset();
		Queue.Reset();
		TypedCharacters = 0;
		Enqueue(Authored, /*bInstant=*/false);
		Refresh();
	}
	EnsureTicking();
}

void UDiegeticTerminalText::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

void UDiegeticTerminalText::Enqueue(const FString& Line, bool bInstant)
{
	TArray<FString> Parts;
	Line.ParseIntoArrayLines(Parts, /*InCullEmpty=*/false);
	if (Parts.Num() == 0)
	{
		Parts.Add(FString());
	}
	for (FString& Part : Parts)
	{
		FQueuedLine& Queued = Queue.AddDefaulted_GetRef();
		Queued.Line = MoveTemp(Part);
		Queued.bInstant = bInstant;
	}
}

void UDiegeticTerminalText::TypeLine(const FText& Line)
{
	Enqueue(Line.ToString(), /*bInstant=*/false);
	EnsureTicking();
}

void UDiegeticTerminalText::TypeLines(const TArray<FText>& Lines)
{
	for (const FText& Line : Lines)
	{
		Enqueue(Line.ToString(), /*bInstant=*/false);
	}
	EnsureTicking();
}

void UDiegeticTerminalText::PrintLine(const FText& Line)
{
	Enqueue(Line.ToString(), /*bInstant=*/true);
	EnsureTicking();
}

void UDiegeticTerminalText::FinishTyping()
{
	if (Queue.Num() == 0)
	{
		return;
	}
	TArray<FQueuedLine> Remaining = MoveTemp(Queue);
	Queue.Reset();
	TypedCharacters = 0;
	for (const FQueuedLine& Queued : Remaining)
	{
		CommitLine(Queued.Line);
	}
	PauseLeft = 0.f;
	Refresh();
	OnTypingFinished.Broadcast();
}

void UDiegeticTerminalText::ClearTerminal()
{
	ShownLines.Reset();
	Queue.Reset();
	TypedCharacters = 0;
	LinesTyped = 0;
	PauseLeft = 0.f;
	Refresh();
}

void UDiegeticTerminalText::CommitLine(const FString& Line)
{
	ShownLines.Add(Line);
	if (MaxLines > 0 && ShownLines.Num() > MaxLines)
	{
		ShownLines.RemoveAt(0, ShownLines.Num() - MaxLines);
	}
	OnLineTyped.Broadcast(LinesTyped++);
}

void UDiegeticTerminalText::EnsureTicking()
{
	const bool bNeeded = Queue.Num() > 0 || (bShowCaret && CaretBlinkRate > 0.f);
	if (bNeeded && !TickerHandle.IsValid() && GetCachedWidget().IsValid() && !IsDesignTime())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float DeltaTime)
		{
			return TickTerminal(DeltaTime);
		}));
	}
}

bool UDiegeticTerminalText::TickTerminal(float DeltaTime)
{
	bool bChanged = false;

	// Typing: a pause between lines, then characters as the budget allows; a line typed is kept and the next begins.
	if (Queue.Num() > 0)
	{
		bCaretLit = true;
		CaretClock = 0.f;
		if (PauseLeft > 0.f)
		{
			PauseLeft -= DeltaTime;
		}
		else
		{
			CharacterBudget += CharactersPerSecond > 0.f ? DeltaTime * CharactersPerSecond : TNumericLimits<double>::Max();
			while (Queue.Num() > 0 && PauseLeft <= 0.f)
			{
				FQueuedLine& Current = Queue[0];
				const int32 Length = Current.Line.Len();
				if (!Current.bInstant && TypedCharacters < Length)
				{
					const int32 Step = static_cast<int32>(FMath::Min(CharacterBudget, static_cast<double>(Length - TypedCharacters)));
					if (Step <= 0)
					{
						break;
					}
					for (int32 Index = TypedCharacters; Index < TypedCharacters + Step; ++Index)
					{
						if (TypeSound && !FChar::IsWhitespace(Current.Line[Index]) && ++CharactersSinceSound >= FMath::Max(SoundInterval, 1))
						{
							CharactersSinceSound = 0;
							const float Pitch = 1.f + FMath::FRandRange(-DiegeticTerminalTextPrivate::ClickPitchSpread, DiegeticTerminalTextPrivate::ClickPitchSpread);
							FUISoundPlayer::PlayOneShot(this, TypeSound, SoundVolume, Pitch);
						}
					}
					TypedCharacters += Step;
					CharacterBudget -= Step;
					bChanged = true;
					if (TypedCharacters < Length)
					{
						break;
					}
				}
				// Off the queue before anyone hears of it: a handler may queue more, or clear the terminal.
				const FString Done = MoveTemp(Current.Line);
				Queue.RemoveAt(0);
				TypedCharacters = 0;
				PauseLeft = Queue.Num() > 0 ? LinePause : 0.f;
				bChanged = true;
				CommitLine(Done);
				if (Queue.Num() == 0)
				{
					CharacterBudget = 0.0;
					OnTypingFinished.Broadcast();
				}
			}
		}
	}
	else if (bShowCaret && CaretBlinkRate > 0.f)
	{
		// Resting: the caret blinks.
		CaretClock += DeltaTime;
		const bool bLit = FMath::Frac(CaretClock * CaretBlinkRate) < 0.5f;
		if (bLit != bCaretLit)
		{
			bCaretLit = bLit;
			bChanged = true;
		}
	}

	if (bChanged)
	{
		Refresh();
	}

	const bool bKeep = Queue.Num() > 0 || (bShowCaret && CaretBlinkRate > 0.f);
	if (!bKeep)
	{
		TickerHandle.Reset();
	}
	return bKeep;
}

void UDiegeticTerminalText::Refresh()
{
	FString Composed = FString::Join(ShownLines, TEXT("\n"));
	FString Typing;
	if (Queue.Num() > 0 && !Queue[0].bInstant)
	{
		Typing = Queue[0].Line.Left(TypedCharacters);
	}
	const bool bTypingLine = Queue.Num() > 0;
	if (bTypingLine)
	{
		if (!Composed.IsEmpty())
		{
			Composed += TEXT("\n");
		}
		Composed += Typing;
	}
	if (bShowCaret)
	{
		// At the end of the last line - or on a line of its own, below the last one, when nothing is being typed.
		if (!bTypingLine && !Composed.IsEmpty())
		{
			Composed += TEXT("\n");
		}
		Composed += bCaretLit ? Caret : FString::ChrN(Caret.Len(), TEXT(' '));
	}
	SetText(FText::FromString(Composed));

	if (UDiegeticScreenComponent* Screen = UDiegeticScreenComponent::FindScreenForWidget(this))
	{
		Screen->MarkContentChanged();
	}
}

#undef LOCTEXT_NAMESPACE
