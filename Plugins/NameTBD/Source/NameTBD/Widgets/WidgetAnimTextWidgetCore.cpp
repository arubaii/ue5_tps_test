// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTextWidgetCore.h"

#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/DataTable.h"
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextMarkup.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "SWidgetAnimText.h"
#include "SWidgetAnimTextGlow.h"
#include "Widgets/SOverlay.h"

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
			Entry.bPlayOnTextChange = Default.bPlayOnTextChange;
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
		.Effects(Effects)
		.OnTextChanged([this]() { HandleTextChanged(); });

	// The material effects of the text's tags - <glow> under it, and whatever a tag's own material makes, under or over
	// it. Every text widget has both sides, so a tag is all it takes; each does nothing until the text asks for it.
	auto MakeEffects = [this](bool bOver)
	{
		return SNew(SWidgetAnimTextGlow)
			.Visibility(EVisibility::HitTestInvisible)
			.Text(Slate)
			.Over(bOver)
			.StyleSource(StyleSource)
			.Effects(Effects);
	};
	EffectsUnder = MakeEffects(/*bOver=*/false);
	EffectsOver = MakeEffects(/*bOver=*/true);

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			EffectsUnder.ToSharedRef()
		]
		+ SOverlay::Slot()
		[
			Slate.ToSharedRef()
		]
		+ SOverlay::Slot()
		[
			EffectsOver.ToSharedRef()
		];
}

void FWidgetAnimTextWidgetCore::ForEachEffectText(TFunctionRef<void(SWidgetAnimText&)> Visit) const
{
	for (const TSharedPtr<SWidgetAnimTextGlow>& Side : { EffectsUnder, EffectsOver })
	{
		if (Side.IsValid())
		{
			Side->ForEachLayerText(Visit);
		}
	}
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
	EffectsUnder.Reset();
	EffectsOver.Reset();
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
		ForEachEffectText([&Style](SWidgetAnimText& Copy) { Copy.SetStyle(Style); });
	}

	// A new text is a new reveal: skipping the last one to its end doesn't skip this one.
	bRevealFinished = false;
}

void FWidgetAnimTextWidgetCore::WriteFromTrack(TFunctionRef<void()> Write)
{
	TGuardValue<bool> Guard(bWritingFromTrack, true);
	Write();
}

void FWidgetAnimTextWidgetCore::HandleTextChanged()
{
	// A track writing its own text - a count ticking over, a reveal putting the text back - is not a new line.
	if (bWritingFromTrack)
	{
		return;
	}

	UWidget* Widget = Owner.Get();
	if (!Widget || Widget->IsDesignTime())
	{
		return;
	}

	// However the text was set - Set Text, a binding, a View Model, a dialogue system - what plays on a new text plays.
	if (UWidgetAnimationComponent* Component = GetComponent())
	{
		Component->PlayForTextChange();
	}
}

float FWidgetAnimTextWidgetCore::GetRevealSpeed() const
{
	return RevealSpeed * WidgetAnimTextMarkup::GetGlobalRevealSpeed();
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
	ForEachEffectText([Poses](SWidgetAnimText& Copy) { Copy.SetPartPoses(Poses); });
}

void FWidgetAnimTextWidgetCore::ClearPartPoses()
{
	if (Slate.IsValid())
	{
		Slate->ClearPartPoses();
	}
	ForEachEffectText([](SWidgetAnimText& Copy) { Copy.ClearPartPoses(); });
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
		if (Raw > Before || (Raw >= Total && Before < Total))
		{
			FireMarkers(Fx.RevealUnit, Before, Raw, Total, DisplayString);
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
	ForEachEffectText([](SWidgetAnimText& Copy) { Copy.NotifyEffectsChanged(); });
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

void FWidgetAnimTextWidgetCore::GetPartBegins(FName Unit, const FString& DisplayString, TArray<int32>& OutBegins) const
{
	OutBegins.Reset();
	if (Unit == WidgetAnimPartUnits::Line)
	{
		const int32 NumLines = Slate.IsValid() ? Slate->GetLineCount() : 0;
		for (int32 LineIndex = 0; LineIndex < NumLines; ++LineIndex)
		{
			int32 Begin = 0;
			int32 End = 0;
			if (Slate->GetLineRange(LineIndex, Begin, End))
			{
				OutBegins.Add(Begin);
			}
		}
		return;
	}

	TArray<WidgetAnimTextParts::FRange> LetterRanges;
	WidgetAnimTextParts::GetLetters(DisplayString, LetterRanges);
	if (Unit == WidgetAnimPartUnits::Word)
	{
		TArray<WidgetAnimTextParts::FRange> WordRanges;
		WidgetAnimTextParts::GetWords(LetterRanges, WordRanges);
		for (const WidgetAnimTextParts::FRange& Range : WordRanges)
		{
			OutBegins.Add(Range.Begin);
		}
		return;
	}
	for (const WidgetAnimTextParts::FRange& Range : LetterRanges)
	{
		OutBegins.Add(Range.Begin);
	}
}

void FWidgetAnimTextWidgetCore::FireMarkers(FName Unit, int32 Before, int32 Now, int32 Total, const FString& DisplayString)
{
	const TSharedPtr<const FWidgetAnimTextMarkup> Markup = CachedMarkup;
	if (!Markup.IsValid() || Markup->Markers.Num() == 0 || !Markup->Display.Equals(DisplayString, ESearchCase::CaseSensitive))
	{
		return;
	}

	TArray<int32> Begins;
	GetPartBegins(Unit, DisplayString, Begins);

	// Each marker happens as the reveal reaches the first part at or after it - or, past the last part, as it finishes.
	TArray<TPair<FName, int32>> Events;
	TArray<FName> Plays;
	for (const FWidgetAnimTextMarker& Marker : Markup->Markers)
	{
		if (Marker.Kind == EWidgetAnimTextMarkerKind::Pause)
		{
			continue;
		}

		int32 Part = Total;
		for (int32 PartIndex = 0; PartIndex < Begins.Num(); ++PartIndex)
		{
			if (Begins[PartIndex] >= Marker.At)
			{
				Part = PartIndex;
				break;
			}
		}

		const bool bReached = Part < Total ? (Part >= Before && Part < Now) : (Now >= Total && Before < Total);
		if (!bReached)
		{
			continue;
		}
		if (Marker.Kind == EWidgetAnimTextMarkerKind::Event)
		{
			Events.Emplace(Marker.Name, Part);
		}
		else
		{
			Plays.AddUnique(Marker.Name);
		}
	}

	if (Events.Num() == 0 && Plays.Num() == 0)
	{
		return;
	}

	// A frame later, rather than in the middle of the animation drawing the reveal: an event may set a new text or play
	// an animation, which must not happen while the animation that reached it is being applied.
	TWeakObjectPtr<UWidget> WeakOwner = Owner;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, WeakOwner, Events, Plays](float)
	{
		UWidget* Widget = WeakOwner.Get();
		if (!Widget)
		{
			return false;
		}
		for (const TPair<FName, int32>& Event : Events)
		{
			if (OnTextEvent)
			{
				OnTextEvent(Event.Key, Event.Value);
			}
		}
		if (Plays.Num() > 0 && !Widget->IsDesignTime())
		{
			if (UWidgetAnimationComponent* Component = GetComponent())
			{
				Component->PlayWidgetAnimationsById(Plays, /*bReverse=*/false, /*PlaybackSpeed=*/1.f, /*bHoldFinalPose=*/false, /*bRestartIfPlaying=*/true);
			}
		}
		return false;
	}));
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

TSharedRef<const FWidgetAnimTextMarkup> FWidgetAnimTextWidgetCore::ReadMarkup(const FString& Source, bool bRich, const UDataTable* StyleSet, uint8 TransformPolicy) const
{
	const bool bTags = !TagsEnabled || TagsEnabled();
	const uint32 TagsVersion = WidgetAnimTextMarkup::GetTagsVersion();
	if (CachedMarkup.IsValid() && bCachedRich == bRich && bCachedTags == bTags && CachedTagsVersion == TagsVersion
		&& CachedTransformPolicy == TransformPolicy && CachedStyleSet.Get() == StyleSet && CachedSource.Equals(Source, ESearchCase::CaseSensitive))
	{
		return CachedMarkup.ToSharedRef();
	}

	auto IsStyle = [StyleSet](FName Name)
	{
		return StyleSet && StyleSet->FindRow<FRichTextStyleRow>(Name, TEXT("Animated Rich Text"), /*bWarnIfRowMissing=*/false) != nullptr;
	};

	const TSharedRef<FWidgetAnimTextMarkup> Markup = MakeShared<FWidgetAnimTextMarkup>();
	WidgetAnimTextMarkup::Parse(Source, bRich, bTags, IsStyle, *Markup);

	// Upper or lower case, as a Text Block draws it - applied to what is drawn, so no tag is changed by it. A language
	// whose casing changes a text's length (German's sharp s) is read again after casing instead, so every tag still
	// stands where it should.
	if (TransformPolicy != static_cast<uint8>(ETextTransformPolicy::None))
	{
		const FString Cased = ApplyTransformPolicy(FText::FromString(Markup->Display), TransformPolicy);
		if (Cased.Len() == Markup->Display.Len())
		{
			Markup->Display = Cased;
		}
		else
		{
			WidgetAnimTextMarkup::Parse(ApplyTransformPolicy(FText::FromString(Source), TransformPolicy), bRich, bTags, IsStyle, *Markup);
		}
	}

	CachedMarkup = Markup;
	CachedSource = Source;
	CachedStyleSet = StyleSet;
	CachedTagsVersion = TagsVersion;
	CachedTransformPolicy = TransformPolicy;
	bCachedRich = bRich;
	bCachedTags = bTags;
	return Markup;
}

TSharedRef<const FWidgetAnimTextMarkup> FWidgetAnimTextWidgetCore::ReadTextBlockMarkup(const FText& Text, uint8 TransformPolicy) const
{
	return ReadMarkup(Text.ToString(), /*bRich=*/false, /*StyleSet=*/nullptr, TransformPolicy);
}

TSharedRef<const FWidgetAnimTextMarkup> FWidgetAnimTextWidgetCore::ReadRichTextMarkup(const FText& Text, const UDataTable* StyleSet) const
{
	return ReadMarkup(Text.ToString(), /*bRich=*/true, StyleSet, static_cast<uint8>(ETextTransformPolicy::None));
}

void FWidgetAnimTextWidgetCore::GatherTextBlockStyle(const UTextBlock& TextBlock, FWidgetAnimTextStyle& OutStyle) const
{
	const TSharedRef<const FWidgetAnimTextMarkup> Markup = ReadTextBlockMarkup(TextBlock.GetText(), static_cast<uint8>(TextBlock.GetTextTransformPolicy()));
	OutStyle.Text = Markup->Display;
	OutStyle.Markup = Markup;
	OutStyle.Runs.Reset();
	OutStyle.Default.Font = TextBlock.GetFont();
	OutStyle.Default.Color = TextBlock.GetColorAndOpacity();
	OutStyle.Default.ShadowOffset = TextBlock.GetShadowOffset();
	OutStyle.Default.ShadowColor = TextBlock.GetShadowColorAndOpacity();
	OutStyle.MinDesiredWidth = TextBlock.GetMinDesiredWidth();
	OutStyle.bAutoWrap = TextBlock.GetAutoWrapText();
	OutStyle.WrapTextAt = TextBlock.GetWrapTextAt();
}

void FWidgetAnimTextWidgetCore::GatherRichTextStyle(const URichTextBlock& RichText, const FTextBlockStyle& DefaultStyle, const UDataTable* StyleSet, FWidgetAnimTextStyle& OutStyle) const
{
	// Styles and text tags read together, so each styled run and each tag lands on the same letters.
	const TSharedRef<const FWidgetAnimTextMarkup> Markup = ReadRichTextMarkup(RichText.GetText(), StyleSet);
	OutStyle.Text = Markup->Display;
	OutStyle.Markup = Markup;
	const TArray<WidgetAnimRichText::FRun>& Runs = Markup->StyleRuns;

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
