// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTextWidgetCore.h"

#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "SWidgetAnimText.h"

FWidgetAnimTextWidgetCore::FWidgetAnimTextWidgetCore()
	: Effects(MakeShared<FWidgetAnimTextEffects>())
{
}

void FWidgetAnimTextWidgetCore::Setup(UWidget* InOwner, TFunction<UWidgetAnimationComponent*()> InGetComponent, EWidgetAnimTextWidgetKind InKind,
	TFunction<void(FWidgetAnimTextStyle&)> InStyleSource, TFunction<bool()> InUsesProjectDefaults)
{
	Owner = InOwner;
	GetComponentFunction = MoveTemp(InGetComponent);
	Kind = InKind;
	StyleSource = MoveTemp(InStyleSource);
	UsesProjectDefaults = MoveTemp(InUsesProjectDefaults);
}

// =====================================================================================================
// Lifecycle
// =====================================================================================================

UWidgetAnimationComponent* FWidgetAnimTextWidgetCore::GetComponent() const
{
	UWidget* Widget = Owner.Get();
	UWidgetAnimationComponent* Component = GetComponentFunction ? GetComponentFunction() : nullptr;
	if (!Widget || !Component || Widget->IsTemplate())
	{
		return nullptr;
	}

	// Bound lazily, as UUserWidget_Animatable binds its own: a template must never be bound. The project's defaults are
	// offered each time, so they follow the widget's Use Project Default Animations whenever the entries are built.
	if (Component->GetAnimatedWidget() != Widget)
	{
		Component->BindToWidget(Widget);

		TWeakObjectPtr<UWidget> WeakOwner = Owner;
		Component->SetDefaultEntriesProvider([this, WeakOwner](TArray<FWidgetAnimEntry>& OutAnimIn, TArray<FWidgetAnimEntry>& OutAnimOut)
		{
			return WeakOwner.IsValid() && ProvideDefaultEntries(OutAnimIn, OutAnimOut);
		});
	}
	return Component;
}

bool FWidgetAnimTextWidgetCore::ProvideDefaultEntries(TArray<FWidgetAnimEntry>& OutAnimIn, TArray<FWidgetAnimEntry>& OutAnimOut) const
{
	if (UsesProjectDefaults && !UsesProjectDefaults())
	{
		return false;
	}

	const FWidgetAnimTextWidgetDefaults& Defaults = UWidgetAnimRuntimeSettings::Get().GetTextWidgetDefaults(Kind);
	auto Convert = [](const TArray<FWidgetAnimTextDefaultEntry>& From, TArray<FWidgetAnimEntry>& To)
	{
		for (const FWidgetAnimTextDefaultEntry& Default : From)
		{
			UWidgetAnimPresetAsset* Animation = Default.Animation.LoadSynchronous();
			if (!Animation)
			{
				continue;
			}

			// Every group on the widget itself: a text widget has nothing inside it to aim a group at.
			FWidgetAnimEntry& Entry = To.AddDefaulted_GetRef();
			Entry.Animation = Animation;
			Entry.AnimId = Default.AnimId;
			Entry.bLoop = Default.bLoop;
			Entry.bPingPong = Default.bPingPong;
			Entry.LoopCount = Default.LoopCount;
			Entry.bInvertAnimation = Default.bInvertAnimation;
			Entry.bMirrorInOut = Default.bMirrorInOut;
			Entry.PlaybackSpeed = Default.PlaybackSpeed;
		}
	};
	Convert(Defaults.AnimIn, OutAnimIn);
	Convert(Defaults.AnimOut, OutAnimOut);
	return OutAnimIn.Num() > 0 || OutAnimOut.Num() > 0;
}

TSharedRef<SWidget> FWidgetAnimTextWidgetCore::BuildSlate()
{
	Slate = SNew(SWidgetAnimText)
		.StyleSource(StyleSource)
		.Effects(Effects);
	return Slate.ToSharedRef();
}

void FWidgetAnimTextWidgetCore::ReleaseSlate()
{
	UWidget* Widget = Owner.Get();
	if (Widget && !Widget->IsDesignTime())
	{
		if (UWidgetAnimationComponent* Component = GetComponent())
		{
			Component->NotifyOwnerDestructed();
		}
	}
	Slate.Reset();
}

void FWidgetAnimTextWidgetCore::NotifyRebuilt()
{
	UWidget* Widget = Owner.Get();
	UWidgetAnimationComponent* Component = GetComponent();
	if (!Widget || !Component)
	{
		return;
	}

	// A text widget has no Construct of its own: its Slate widget being made is when it is put on screen.
	if (Widget->IsDesignTime())
	{
		Component->EnsureDesignerPreviewTimer();
	}
	else
	{
		Component->NotifyOwnerConstructed();
	}
}

void FWidgetAnimTextWidgetCore::NotifySynchronized()
{
	NotifyTextChanged();

	UWidget* Widget = Owner.Get();
	if (Widget && Widget->IsDesignTime())
	{
		if (UWidgetAnimationComponent* Component = GetComponent())
		{
			Component->NotifyOwnerDesignTimeRefresh();
		}
	}
}

void FWidgetAnimTextWidgetCore::NotifyTextChanged()
{
	if (Slate.IsValid() && StyleSource)
	{
		FWidgetAnimTextStyle Style;
		StyleSource(Style);
		Slate->SetStyle(Style);
	}

	// A new text is a new reveal: skipping the last one to its end doesn't skip this one.
	bRevealFinished = false;
}

bool FWidgetAnimTextWidgetCore::RouteVisibility(ESlateVisibility InVisibility)
{
	UWidget* Widget = Owner.Get();
	UWidgetAnimationComponent* Component = GetComponent();
	if (!Widget || !Component || Component->IsApplyingVisibility())
	{
		return false;
	}
	Component->SetVisibilityAnimated(InVisibility);
	return true;
}

// =====================================================================================================
// Parts and effects
// =====================================================================================================

int32 FWidgetAnimTextWidgetCore::GetPartCount(FName Unit, const FString& DisplayString) const
{
	if (Unit == WidgetAnimPartUnits::Letter || Unit == WidgetAnimPartUnits::Word)
	{
		if (!DisplayString.Equals(CountedString, ESearchCase::CaseSensitive) || (NumLetters == 0 && !DisplayString.IsEmpty()))
		{
			// Counted the same way the Slate widget and the text tracks count them, so a stagger's letters are exactly its letters.
			TArray<WidgetAnimTextParts::FRange> LetterRanges;
			TArray<WidgetAnimTextParts::FRange> WordRanges;
			WidgetAnimTextParts::GetLetters(DisplayString, LetterRanges);
			WidgetAnimTextParts::GetWords(LetterRanges, WordRanges);
			CountedString = DisplayString;
			NumLetters = LetterRanges.Num();
			NumWords = WordRanges.Num();
		}
		return Unit == WidgetAnimPartUnits::Letter ? NumLetters : NumWords;
	}

	if (Unit == WidgetAnimPartUnits::Line)
	{
		// Lines depend on the width it wraps at, so only its Slate widget knows them; before it has one, a text with
		// anything in it is taken as one line.
		if (Slate.IsValid())
		{
			return Slate->GetLineCount();
		}
		return DisplayString.IsEmpty() ? 0 : 1;
	}

	return 0;
}

bool FWidgetAnimTextWidgetCore::ArePartsReady(FName Unit, bool bAutoWrap, float WrapTextAt) const
{
	// Only lines wait, and only when they wrap to a width that isn't known until the widget has been arranged.
	if (Unit != WidgetAnimPartUnits::Line || WrapTextAt > 0.f || !bAutoWrap)
	{
		return true;
	}
	return Slate.IsValid() && Slate->HasBeenArranged();
}

void FWidgetAnimTextWidgetCore::SetPartPoses(TConstArrayView<FWidgetAnimPartPose> Poses)
{
	if (Slate.IsValid())
	{
		Slate->SetPartPoses(Poses);
	}
}

void FWidgetAnimTextWidgetCore::ClearPartPoses()
{
	if (Slate.IsValid())
	{
		Slate->ClearPartPoses();
	}
}

void FWidgetAnimTextWidgetCore::NotifyEffectsChanged(const FString& DisplayString)
{
	FWidgetAnimTextEffects& Fx = *Effects;

	if (Fx.bReveal)
	{
		// How many parts have started to appear, as the reveal track wrote them.
		int32 Raw = 0;
		while (Raw < Fx.RevealAlphas.Num() && Fx.RevealAlphas[Raw] > 0.f)
		{
			++Raw;
		}
		const int32 Total = GetPartCount(Fx.RevealUnit, DisplayString);

		// Finished early by Finish Text Reveal: everything shown, until the reveal starts over - its own count going back.
		const bool bWentBack = Raw < LastRawCount;
		LastRawCount = Raw;
		if (bRevealFinished)
		{
			if (bWentBack)
			{
				bRevealFinished = false;
			}
			else
			{
				Fx.RevealAlphas.Init(1.f, Total);
				Raw = Total;
			}
		}

		// A reveal gone backwards - played again, or a new text - starts its events over.
		if (Raw < RevealedCount || Total != RevealTotal)
		{
			RevealedCount = FMath::Min(RevealedCount, Raw);
		}

		const int32 Before = RevealedCount;
		RevealedCount = Raw;
		RevealTotal = Total;

		if (OnPartRevealed)
		{
			for (int32 PartIndex = Before; PartIndex < Raw; ++PartIndex)
			{
				OnPartRevealed(PartIndex, GetPartText(Fx.RevealUnit, PartIndex, DisplayString));
			}
		}
		if (OnRevealFinished && Raw >= Total && Before < Total)
		{
			OnRevealFinished();
		}
	}
	else
	{
		// No reveal set any more: a skip to its end is over with it, so a replay of the same text types out again.
		RevealedCount = 0;
		RevealTotal = 0;
		LastRawCount = 0;
		bRevealFinished = false;
	}

	if (Slate.IsValid())
	{
		Slate->NotifyEffectsChanged();
	}
}

bool FWidgetAnimTextWidgetCore::IsRevealing() const
{
	return Effects->bReveal && RevealedCount < RevealTotal;
}

void FWidgetAnimTextWidgetCore::FinishReveal(const FString& DisplayString)
{
	if (!IsRevealing())
	{
		return;
	}

	bRevealFinished = true;
	NotifyEffectsChanged(DisplayString);
}

FString FWidgetAnimTextWidgetCore::GetPartText(FName Unit, int32 PartIndex, const FString& DisplayString) const
{
	if (Unit == WidgetAnimPartUnits::Line)
	{
		int32 Begin = 0;
		int32 End = 0;
		return Slate.IsValid() && Slate->GetLineRange(PartIndex, Begin, End) ? DisplayString.Mid(Begin, End - Begin) : FString();
	}

	TArray<WidgetAnimTextParts::FRange> LetterRanges;
	WidgetAnimTextParts::GetLetters(DisplayString, LetterRanges);
	if (Unit == WidgetAnimPartUnits::Word)
	{
		TArray<WidgetAnimTextParts::FRange> WordRanges;
		WidgetAnimTextParts::GetWords(LetterRanges, WordRanges);
		return WordRanges.IsValidIndex(PartIndex) ? DisplayString.Mid(WordRanges[PartIndex].Begin, WordRanges[PartIndex].End - WordRanges[PartIndex].Begin) : FString();
	}
	return LetterRanges.IsValidIndex(PartIndex) ? DisplayString.Mid(LetterRanges[PartIndex].Begin, LetterRanges[PartIndex].End - LetterRanges[PartIndex].Begin) : FString();
}

// =====================================================================================================
// Shared helpers
// =====================================================================================================

FString FWidgetAnimTextWidgetCore::ApplyTransformPolicy(const FText& Text, uint8 TransformPolicy)
{
	switch (static_cast<ETextTransformPolicy>(TransformPolicy))
	{
	case ETextTransformPolicy::ToUpper: return Text.ToUpper().ToString();
	case ETextTransformPolicy::ToLower: return Text.ToLower().ToString();
	default:                            return Text.ToString();
	}
}

void FWidgetAnimTextWidgetCore::GatherTextBlockStyle(const UTextBlock& TextBlock, FWidgetAnimTextStyle& OutStyle)
{
	OutStyle.Text = ApplyTransformPolicy(TextBlock.GetText(), static_cast<uint8>(TextBlock.GetTextTransformPolicy()));
	OutStyle.Runs.Reset();
	OutStyle.Default.Font = TextBlock.GetFont();
	OutStyle.Default.Color = TextBlock.GetColorAndOpacity();
	OutStyle.Default.ShadowOffset = TextBlock.GetShadowOffset();
	OutStyle.Default.ShadowColor = TextBlock.GetShadowColorAndOpacity();
	OutStyle.MinDesiredWidth = TextBlock.GetMinDesiredWidth();
	OutStyle.bAutoWrap = TextBlock.GetAutoWrapText();
	OutStyle.WrapTextAt = TextBlock.GetWrapTextAt();
}

void FWidgetAnimTextWidgetCore::GatherRichTextStyle(const URichTextBlock& RichText, const FTextBlockStyle& DefaultStyle, const UDataTable* StyleSet, FWidgetAnimTextStyle& OutStyle)
{
	TArray<WidgetAnimRichText::FRun> Runs;
	WidgetAnimRichText::Parse(RichText.GetText().ToString(), OutStyle.Text, Runs);

	// The default style is the Rich Text Block's own - its Text Style Set's "Default" row, or its override.
	auto ToRunStyle = [](const FTextBlockStyle& TextStyle, FWidgetAnimTextRunStyle& OutRun)
	{
		OutRun.Font = TextStyle.Font;
		OutRun.Color = TextStyle.ColorAndOpacity;
		OutRun.ShadowOffset = FVector2D(TextStyle.ShadowOffset);
		OutRun.ShadowColor = TextStyle.ShadowColorAndOpacity;
	};
	ToRunStyle(DefaultStyle, OutStyle.Default);

	// Each tagged run in its row of the Text Style Set; a tag it has no row for, in the default style - as a Rich Text
	// Block draws it.
	OutStyle.Runs.Reset(Runs.Num());
	for (const WidgetAnimRichText::FRun& Run : Runs)
	{
		FWidgetAnimTextRunStyle& Styled = OutStyle.Runs.Add_GetRef(OutStyle.Default);
		Styled.Begin = Run.Begin;
		Styled.End = Run.End;

		const FRichTextStyleRow* Row = StyleSet && !Run.Style.IsNone()
			? StyleSet->FindRow<FRichTextStyleRow>(Run.Style, TEXT("Animated Rich Text"), /*bWarnIfRowMissing=*/false)
			: nullptr;
		if (Row)
		{
			ToRunStyle(Row->TextStyle, Styled);
		}
	}
}

FString FWidgetAnimTextWidgetCore::GetRichDisplayString(const URichTextBlock& RichText)
{
	FString Display;
	TArray<WidgetAnimRichText::FRun> Runs;
	WidgetAnimRichText::Parse(RichText.GetText().ToString(), Display, Runs);
	return Display;
}
