// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "NameTBD/WidgetAnimation/WidgetAnimParts.h"
#include "NameTBD/WidgetAnimation/WidgetAnimRuntimeSettings.h" // EWidgetAnimTextWidgetKind
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextHost.h"

#include "WidgetAnimTextWidgetCore.generated.h"

class SWidget;
class SWidgetAnimText;
class UWidget;
class UTextBlock;
class URichTextBlock;
class UDataTable;
class UWidgetAnimationComponent;
struct FTextBlockStyle;
struct FWidgetAnimEntry;
struct FWidgetAnimTextStyle;
struct FWidgetAnimValueId;

// Dummy struct to force UHT to parse the delegates
USTRUCT()
struct FWidgetAnimTextDummyStruct
{
	GENERATED_BODY()
};

/** A part of a text has just been revealed: its index among its reveal's parts, and its text. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWidgetAnimTextPartRevealed, int32, PartIndex, const FString&, PartText);

/** Every part of a text's reveal has been revealed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWidgetAnimTextRevealFinished);

/**
 * Everything the animated text widgets share - Animated Text Block, Animated Rich Text Block and their Common UI
 * counterparts - which derive from different engine classes, so share it by holding one of these rather than by a base
 * class: their Slate widget, their text effects, the reveal events, and their built-in Widget Animation Component.
 *
 * The WIDGETANIM_TEXT_WIDGET_* macros below declare and define each widget's one-line forwards to it.
 */
class NAMETBD_API FWidgetAnimTextWidgetCore
{
public:

	FWidgetAnimTextWidgetCore();

	/**
	 * Sets it up, from the widget's constructor: the widget, how to reach its component, which project defaults it
	 * plays, and how to read its text and style.
	 */
	void Setup(UWidget* InOwner, TFunction<UWidgetAnimationComponent*()> InGetComponent, EWidgetAnimTextWidgetKind InKind,
		TFunction<void(FWidgetAnimTextStyle&)> InStyleSource, TFunction<bool()> InUsesProjectDefaults);

	// ---------------------------------------------------------------------------------------------
	// Lifecycle - called from the widget's overrides
	// ---------------------------------------------------------------------------------------------

	/** Makes the Slate widget that draws the text. */
	TSharedRef<SWidget> BuildSlate();

	/** The Slate widget is gone. At runtime, the widget's animations end with it. */
	void ReleaseSlate();

	/** The Slate widget was just made: at runtime, the widget is constructed and plays its Anim In. */
	void NotifyRebuilt();

	/** The widget's settings were applied - a designer edit: the text redrawn, the designer preview told. */
	void NotifySynchronized();

	/** The text or a style changed now: drawn with it this frame, and counted anew. */
	void NotifyTextChanged();

	/** Routes a hide through Anim Out first. Returns false when the widget should set it straight away. */
	bool RouteVisibility(ESlateVisibility InVisibility);

	// ---------------------------------------------------------------------------------------------
	// IWidgetAnimHost, IWidgetAnimPartHost and IWidgetAnimTextHost, forwarded
	// ---------------------------------------------------------------------------------------------

	/** The built-in component, bound to the widget. Null on a template, which never animates. */
	UWidgetAnimationComponent* GetComponent() const;

	int32 GetPartCount(FName Unit, const FString& DisplayString) const;
	bool ArePartsReady(FName Unit, bool bAutoWrap, float WrapTextAt) const;
	void SetPartPoses(TConstArrayView<FWidgetAnimPartPose> Poses);
	void ClearPartPoses();

	FWidgetAnimTextEffects& GetEffects() { return *Effects; }

	/** Draws the effects, and fires the reveal events for any parts just revealed. */
	void NotifyEffectsChanged(const FString& DisplayString);

	// ---------------------------------------------------------------------------------------------
	// Reveals
	// ---------------------------------------------------------------------------------------------

	/** Whether a reveal is under way and hasn't shown everything yet. */
	bool IsRevealing() const;

	/** Shows the rest of a reveal under way at once - a dialogue line skipped to its end. Its events fire. */
	void FinishReveal(const FString& DisplayString);

	/** Told each part as it is revealed, and when all are: the widget broadcasts them as Blueprint events. */
	TFunction<void(int32, const FString&)> OnPartRevealed;
	TFunction<void()> OnRevealFinished;

	// ---------------------------------------------------------------------------------------------
	// Shared helpers for the widgets' own code
	// ---------------------------------------------------------------------------------------------

	/** Text in a Text Block's Transform Policy - upper or lower case - as it is drawn. */
	static FString ApplyTransformPolicy(const FText& Text, uint8 TransformPolicy);

	/** A Text Block's text and look, into Style: everything but the Text Layout Widget settings the widget fills itself. */
	static void GatherTextBlockStyle(const UTextBlock& TextBlock, FWidgetAnimTextStyle& OutStyle);

	/**
	 * A Rich Text Block's markup and Text Style Set, into Style as its string and styled runs: DefaultStyle for text in
	 * no style, and StyleSet's rows for the rest. The widget passes its own, which only it can read.
	 */
	static void GatherRichTextStyle(const URichTextBlock& RichText, const FTextBlockStyle& DefaultStyle, const UDataTable* StyleSet, FWidgetAnimTextStyle& OutStyle);

	/** A Rich Text Block's string as drawn, its style tags taken out. */
	static FString GetRichDisplayString(const URichTextBlock& RichText);

private:

	/** Fills in the project's default entries for this kind - see UWidgetAnimationComponent::SetDefaultEntriesProvider. */
	bool ProvideDefaultEntries(TArray<FWidgetAnimEntry>& OutAnimIn, TArray<FWidgetAnimEntry>& OutAnimOut) const;

	/** Part PartIndex of the reveal's unit as text, for its event. */
	FString GetPartText(FName Unit, int32 PartIndex, const FString& DisplayString) const;

	TWeakObjectPtr<UWidget> Owner;
	TFunction<UWidgetAnimationComponent*()> GetComponentFunction;
	TFunction<void(FWidgetAnimTextStyle&)> StyleSource;
	TFunction<bool()> UsesProjectDefaults;
	EWidgetAnimTextWidgetKind Kind = EWidgetAnimTextWidgetKind::Text;

	TSharedPtr<SWidgetAnimText> Slate;
	TSharedRef<FWidgetAnimTextEffects> Effects;

	/** How many parts of the reveal had appeared at the last change - what the events are fired against. */
	int32 RevealedCount = 0;
	int32 RevealTotal = 0;

	/** Finish Reveal in force: every part shown until the reveal starts again - its own count going back. */
	bool bRevealFinished = false;

	/** How many parts the reveal track itself had started last time, before any Finish Reveal - how going back is seen. */
	int32 LastRawCount = 0;

	/** Letter and word counts of the last string counted. */
	mutable FString CountedString;
	mutable int32 NumLetters = 0;
	mutable int32 NumWords = 0;
};

// =====================================================================================================
// Declarations and definitions every animated text widget shares
// =====================================================================================================
//
// In the class body, after its UPROPERTYs and UFUNCTIONs and in a public section: WIDGETANIM_TEXT_WIDGET_BODY().
// In its .cpp: WIDGETANIM_TEXT_WIDGET_IMPL(Class) and one of WIDGETANIM_TEXT_WIDGET_TEXTBLOCK_IMPL(Class) (a Text Block
// underneath, with WIDGETANIM_TEXT_WIDGET_SETTEXT_IMPL(Class) when it overrides Set Text) or
// WIDGETANIM_TEXT_WIDGET_RICHTEXT_IMPL(Class) (a Rich Text Block underneath). The class holds an
// FWidgetAnimTextWidgetCore named TextCore, and its constructor calls SetupTextCore(Kind). See UWidgetAnimTextBlock for
// the properties, events and functions each declares itself - UHT needs them written out in the class.
//
// Macros rather than a base class because the widgets derive from four different engine classes - Text Block, Rich
// Text Block, and Common UI's own Text, Rich Text, Numeric Text and Date Time Text blocks - and each must stay one of
// those, for everything that looks for one.

#define WIDGETANIM_TEXT_WIDGET_BODY() \
	/* IWidgetAnimHost */ \
	virtual void PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying) override; \
	virtual void StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart) override; \
	virtual bool AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const override; \
	virtual bool SetWidgetAnimationValues(const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying) override; \
	virtual FVector4 GetWidgetAnimationValues(const FWidgetAnimValueId& Id, bool bTargetValue) const override; \
	/* IWidgetAnimPartHost */ \
	virtual int32 GetWidgetAnimPartCount(FName Unit) const override; \
	virtual bool AreWidgetAnimPartsReady(FName Unit) const override; \
	virtual void SetWidgetAnimPartPoses(TConstArrayView<FWidgetAnimPartPose> Poses) override; \
	virtual void ClearWidgetAnimPartPoses() override; \
	/* IWidgetAnimTextHost */ \
	virtual FWidgetAnimTextEffects& GetWidgetAnimTextEffects() override; \
	virtual void OnWidgetAnimTextEffectsChanged() override; \
	virtual FText GetWidgetAnimText() const override; \
	virtual void SetWidgetAnimText(const FText& InText) override; \
	virtual FString GetWidgetAnimDisplayString() const override; \
	/* UWidget */ \
	virtual void SetVisibility(ESlateVisibility InVisibility) override; \
	virtual void SynchronizeProperties() override; \
	virtual void ReleaseSlateResources(bool bReleaseChildren) override; \
	virtual TSharedRef<SWidget> RebuildWidget() override; \
	virtual void OnWidgetRebuilt() override; \
	/* Shared */ \
	void GatherWidgetAnimTextStyle(FWidgetAnimTextStyle& OutStyle) const; \
	void SetupTextCore(EWidgetAnimTextWidgetKind InKind);

/** Everything but the text itself and its style. */
#define WIDGETANIM_TEXT_WIDGET_IMPL(Class) \
	void Class::SetupTextCore(EWidgetAnimTextWidgetKind InKind) \
	{ \
		WidgetAnimationComponent = CreateDefaultSubobject<UWidgetAnimationComponent>(TEXT("WidgetAnimationComponent")); \
		TextCore.Setup(this, \
			[this]() { return WidgetAnimationComponent.Get(); }, \
			InKind, \
			[this](FWidgetAnimTextStyle& OutStyle) { GatherWidgetAnimTextStyle(OutStyle); }, \
			[this]() { return bUseProjectDefaultAnimations; }); \
		TextCore.OnPartRevealed = [this](int32 PartIndex, const FString& PartText) { OnTextPartRevealed.Broadcast(PartIndex, PartText); }; \
		TextCore.OnRevealFinished = [this]() { OnTextRevealFinished.Broadcast(); }; \
	} \
	void Class::PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying) \
	{ \
		if (UWidgetAnimationComponent* Component = TextCore.GetComponent()) \
		{ \
			Component->PlayWidgetAnimationsById(AnimIds, bReverse, PlaybackSpeed, bHoldFinalPose, bRestartIfPlaying); \
		} \
	} \
	void Class::StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart) \
	{ \
		if (UWidgetAnimationComponent* Component = TextCore.GetComponent()) \
		{ \
			Component->StopWidgetAnimationsById(AnimIds, bResetToStart); \
		} \
	} \
	bool Class::AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const \
	{ \
		const UWidgetAnimationComponent* Component = TextCore.GetComponent(); \
		return Component && Component->AreWidgetAnimationsPlayingById(AnimIds); \
	} \
	bool Class::SetWidgetAnimationValues(const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying) \
	{ \
		UWidgetAnimationComponent* Component = TextCore.GetComponent(); \
		return Component && Component->SetWidgetAnimationValues(Id, Value, bSnap, bStartIfNotPlaying); \
	} \
	FVector4 Class::GetWidgetAnimationValues(const FWidgetAnimValueId& Id, bool bTargetValue) const \
	{ \
		const UWidgetAnimationComponent* Component = TextCore.GetComponent(); \
		return Component ? Component->GetWidgetAnimationValues(Id, bTargetValue) : FVector4(0.0, 0.0, 0.0, 0.0); \
	} \
	int32 Class::GetWidgetAnimPartCount(FName Unit) const \
	{ \
		return TextCore.GetPartCount(Unit, GetWidgetAnimDisplayString()); \
	} \
	bool Class::AreWidgetAnimPartsReady(FName Unit) const \
	{ \
		FWidgetAnimTextStyle GatheredStyle; \
		GatherWidgetAnimTextStyle(GatheredStyle); \
		return TextCore.ArePartsReady(Unit, GatheredStyle.bAutoWrap, GatheredStyle.WrapTextAt); \
	} \
	void Class::SetWidgetAnimPartPoses(TConstArrayView<FWidgetAnimPartPose> Poses) \
	{ \
		TextCore.SetPartPoses(Poses); \
	} \
	void Class::ClearWidgetAnimPartPoses() \
	{ \
		TextCore.ClearPartPoses(); \
	} \
	FWidgetAnimTextEffects& Class::GetWidgetAnimTextEffects() \
	{ \
		return TextCore.GetEffects(); \
	} \
	void Class::OnWidgetAnimTextEffectsChanged() \
	{ \
		TextCore.NotifyEffectsChanged(GetWidgetAnimDisplayString()); \
	} \
	void Class::FinishTextReveal() \
	{ \
		TextCore.FinishReveal(GetWidgetAnimDisplayString()); \
	} \
	bool Class::IsTextRevealing() const \
	{ \
		return TextCore.IsRevealing(); \
	} \
	void Class::SetVisibility(ESlateVisibility InVisibility) \
	{ \
		if (!TextCore.RouteVisibility(InVisibility)) \
		{ \
			Super::SetVisibility(InVisibility); \
		} \
	} \
	void Class::SynchronizeProperties() \
	{ \
		Super::SynchronizeProperties(); \
		TextCore.NotifySynchronized(); \
	} \
	void Class::ReleaseSlateResources(bool bReleaseChildren) \
	{ \
		TextCore.ReleaseSlate(); \
		Super::ReleaseSlateResources(bReleaseChildren); \
	} \
	TSharedRef<SWidget> Class::RebuildWidget() \
	{ \
		/* The engine class's own Slate widget is made and kept - everything it does with it keeps working - but never shown. */ \
		Super::RebuildWidget(); \
		return TextCore.BuildSlate(); \
	} \
	void Class::OnWidgetRebuilt() \
	{ \
		Super::OnWidgetRebuilt(); \
		TextCore.NotifyRebuilt(); \
	}

/** Text and style for a widget with a Text Block underneath - Animated Text Block, and Common Text, Numeric and Date Time. */
#define WIDGETANIM_TEXT_WIDGET_TEXTBLOCK_IMPL(Class) \
	void Class::GatherWidgetAnimTextStyle(FWidgetAnimTextStyle& OutStyle) const \
	{ \
		FWidgetAnimTextWidgetCore::GatherTextBlockStyle(*this, OutStyle); \
		OutStyle.Justification = Justification; \
		OutStyle.Margin = Margin; \
		OutStyle.LineHeight = LineHeightPercentage; \
	} \
	FText Class::GetWidgetAnimText() const \
	{ \
		return GetText(); \
	} \
	void Class::SetWidgetAnimText(const FText& InText) \
	{ \
		/* Through the Text Block, as any caller would set it: Common UI's Numeric and Date Time blocks hide their own. */ \
		static_cast<UTextBlock*>(this)->SetText(InText); \
		TextCore.NotifyTextChanged(); \
	} \
	FString Class::GetWidgetAnimDisplayString() const \
	{ \
		return FWidgetAnimTextWidgetCore::ApplyTransformPolicy(GetText(), static_cast<uint8>(GetTextTransformPolicy())); \
	}

/**
 * Set Text, redrawn at once - for a widget with a Text Block underneath that declares
 * "virtual void SetText(FText InText) override;". Common Animated Numeric and Date Time Text don't: their text is made
 * by the widget itself, and is picked up the frame it changes.
 */
#define WIDGETANIM_TEXT_WIDGET_SETTEXT_IMPL(Class) \
	void Class::SetText(FText InText) \
	{ \
		Super::SetText(InText); \
		TextCore.NotifyTextChanged(); \
	}

/** Text and style for a widget with a Rich Text Block underneath - Animated Rich Text Block and Common Rich Text. */
#define WIDGETANIM_TEXT_WIDGET_RICHTEXT_IMPL(Class) \
	void Class::GatherWidgetAnimTextStyle(FWidgetAnimTextStyle& OutStyle) const \
	{ \
		FWidgetAnimTextWidgetCore::GatherRichTextStyle(*this, GetCurrentDefaultTextStyle(), GetTextStyleSet(), OutStyle); \
		OutStyle.Justification = Justification; \
		OutStyle.Margin = Margin; \
		OutStyle.LineHeight = LineHeightPercentage; \
		OutStyle.bAutoWrap = GetAutoWrapText(); \
		OutStyle.WrapTextAt = GetWrapTextAt(); \
	} \
	FText Class::GetWidgetAnimText() const \
	{ \
		return GetText(); \
	} \
	void Class::SetWidgetAnimText(const FText& InText) \
	{ \
		SetText(InText); \
	} \
	FString Class::GetWidgetAnimDisplayString() const \
	{ \
		return FWidgetAnimTextWidgetCore::GetRichDisplayString(*this); \
	} \
	/* The class declares "virtual void SetText(const FText& InText) override;". */ \
	void Class::SetText(const FText& InText) \
	{ \
		Super::SetText(InText); \
		TextCore.NotifyTextChanged(); \
	}
