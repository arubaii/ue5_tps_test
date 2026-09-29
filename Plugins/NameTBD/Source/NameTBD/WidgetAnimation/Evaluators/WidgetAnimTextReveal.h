// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/WidgetAnimation/WidgetAnimExpander.h" // EWidgetAnimTextUnit
#include "WidgetAnimTextTracks.h"
#include "WidgetAnimTextReveal.generated.h"

/**
 * When each part of a Text Reveal starts to appear, worked out for one text: what a Text Reveal track plays by. Kept
 * on the track's own playing copy of its settings; see FWidgetAnimTextRevealParams.
 */
struct NAMETBD_API FWidgetAnimTextRevealPlan
{
	/** The string as drawn it was worked out for, and the unit it counts in. */
	FString ForString;
	FName Unit;
	int32 NumParts = 0;

	/** The text's tags it was worked out with - their speeds, pauses and looks - by identity. Null for none. */
	const void* ForMarkup = nullptr;

	/** Where along the reveal each part starts to appear, 0 to 1. */
	TArray<float> Starts;

	/**
	 * How much of the reveal each part takes to appear - its tag's look's, or the reveal's own. Empty: every part takes
	 * AppearFraction.
	 */
	TArray<float> Appears;

	/** How much of the reveal one part takes to appear. 0 pops each one on at once. */
	float AppearFraction = 0.f;

	/** How many seconds the reveal it was worked out for runs. 0 until worked out on a widget. */
	float RunSeconds = 0.f;

	bool IsFor(const FString& String, FName InUnit, int32 InNumParts, const void* InMarkup) const
	{
		return RunSeconds > 0.f && Unit == InUnit && NumParts == InNumParts && ForMarkup == InMarkup && ForString.Equals(String, ESearchCase::CaseSensitive);
	}

	/** How far part PartIndex has appeared when the reveal is Revealed (0 to 1) of the way along. */
	float GetPartAlpha(int32 PartIndex, double Revealed) const;
};

/**
 * Text Reveal: a text typed out, a letter (or word, or line) at a time - a dialogue line, a quest objective, a
 * terminal readout - each part fading, rising or popping in as it appears, and a cursor running ahead if you like.
 *
 * By default its pace is its Typing Speed: so many letters a second, with a pause after each sentence and a shorter one
 * after each comma. The track is then as long as its text takes to type - a line of dialogue twice as long runs twice
 * as long - worked out on its widget as it starts to play, and anything placed after it moves with it. The Widget
 * Animations editor draws such a track striped, as it does a value track, since its Duration means nothing. With Typing
 * Speed off, its Duration sets the pace instead, and Easing and keys shape it like any track: a key of 0.5 with a Hold
 * curve pauses halfway, a Start of 1 and End of 0 un-types it.
 *
 * With Value timing, the reveal follows a value set from gameplay, 0 to 1 of the way through it - a line kept in step
 * with its voice-over by the fraction of the audio played. On Text Part Revealed, on the text widget, fires as each part
 * appears, however it is timed: a blip per letter, as a dialogue box chatters.
 *
 * How each part appears is its Style: Typewriter (the default) pops it on in place; Fade, Slide, Pop, Stomp, Drop,
 * Spin, Flip, Stretch, Blur, Wipe and Scatter bring it in over its Appear Time; Custom takes every setting by hand.
 * Preview Text To Come shows the whole text faintly from the start, each part filling in as it is reached.
 *
 * On an animated text widget its text's tags pace it too (see WidgetAnimTextMarkup.h): <speed=2> types faster,
 * <instant> shows words at once, <pause=0.5> waits, a tag's own Reveal Look brings its words in its own way, and
 * <event> and <play> fire as the reveal reaches them. The widget's Text Reveal Speed and the game's scale a typed pace.
 *
 * On an animated text widget every part keeps its place from the start and those not reached yet are simply not drawn,
 * so nothing re-wraps as it types. On a plain Text Block or Rich Text Block its text is rewritten to the part shown so
 * far, which may re-wrap as it grows and drops a rich text's styles until it is all shown; its Style, tags and preview
 * need an animated text widget.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Text Reveal"))
struct NAMETBD_API FWidgetAnimTextRevealParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/**
	 * What is revealed at a time. Line is a line as an animated text widget lays it out; on a plain Text Block, the
	 * text's own line breaks.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Reveal By"))
	EWidgetAnimTextUnit Unit = EWidgetAnimTextUnit::Letter;

	/**
	 * Paces the reveal by its text: so many letters (or words, or lines) a second, the track as long as its text takes.
	 * Off: the track's Duration sets the pace, the text spread across it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Use Typing Speed"))
	bool bUseTypingSpeed = true;

	/** Letters (or words, or lines) a second. 30 letters a second is a brisk dialogue box; 60 is fast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Typing Speed", ClampMin = "0.1", UIMin = "1.0", UIMax = "120.0", EditCondition = "bUseTypingSpeed", EditConditionHides))
	float TypingSpeed = 30.f;

	/** A pause after each part ending a sentence - . ! ? and the like - before the next part appears. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Pause After Sentence", ClampMin = "0.0", UIMax = "1.0", Units = "s"))
	float SentencePause = 0.25f;

	/** A pause after each part ending with a comma, colon, semicolon or dash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Pause After Comma", ClampMin = "0.0", UIMax = "1.0", Units = "s"))
	float CommaPause = 0.08f;

	/**
	 * How each part appears once it is reached. Typewriter pops it on at once; the others bring it in over Appear Time.
	 * Animated text widgets only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Style"))
	EWidgetAnimTextRevealStyle Style = EWidgetAnimTextRevealStyle::Typewriter;

	/** Seconds each part takes to appear. Longer overlaps them, several parts always on their way in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Appear Time", ClampMin = "0.0", UIMax = "1.0", Units = "s", EditCondition = "Style != EWidgetAnimTextRevealStyle::Typewriter && Style != EWidgetAnimTextRevealStyle::Custom", EditConditionHides))
	float AppearTime = 0.25f;

	/** Which side each part slides in from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Slide From", EditCondition = "Style == EWidgetAnimTextRevealStyle::Slide", EditConditionHides))
	EWidgetAnimTextSlideFrom SlideFrom = EWidgetAnimTextSlideFrom::Below;

	/** How far each part travels - or, for Blur, how soft it starts - in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (ClampMin = "0.0", UIMax = "60.0", EditCondition = "Style == EWidgetAnimTextRevealStyle::Slide || Style == EWidgetAnimTextRevealStyle::Drop || Style == EWidgetAnimTextRevealStyle::Blur || Style == EWidgetAnimTextRevealStyle::Scatter", EditConditionHides))
	float Distance = 12.f;

	/** Each part fades in as it moves into place. Off, it is fully there from its first frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Fade In As It Appears", EditCondition = "Style != EWidgetAnimTextRevealStyle::Typewriter && Style != EWidgetAnimTextRevealStyle::Fade && Style != EWidgetAnimTextRevealStyle::Blur && Style != EWidgetAnimTextRevealStyle::Wipe && Style != EWidgetAnimTextRevealStyle::Custom", EditConditionHides))
	bool bFadeIn = true;

	/** Every setting of how each part appears, for a Style of Custom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Custom Appear", EditCondition = "Style == EWidgetAnimTextRevealStyle::Custom", EditConditionHides))
	FWidgetAnimTextAppear Appear;

	/**
	 * Shows the whole text from the start, faintly, each part filling in as the reveal reaches it - so a reader sees the
	 * shape of the line before it is read out. Animated text widgets only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Preview Text To Come"))
	bool bPreviewTextToCome = false;

	/** How opaque the text still to come is drawn: 0.3 is a faint outline of the line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Preview Opacity", ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bPreviewTextToCome", EditConditionHides))
	float PreviewOpacity = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (InlineEditConditionToggle))
	bool bPreviewColor = false;

	/** The text still to come in this color, rather than its own - gray, say. Its opacity is Preview Opacity's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Preview Color", EditCondition = "bPreviewColor"))
	FLinearColor PreviewColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.f);

	/**
	 * Drawn just after the last part shown, until the whole text is: "_" or "|", say. Empty: no cursor. Animated text
	 * widgets only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal")
	FString Cursor;

	/** The cursor blinks, twice a second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Cursor Blinks"))
	bool bCursorBlink = true;

	/** How much of the text shows when the track starts: 0 none of it, 1 all of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Start Revealed", ClampMin = "0.0", ClampMax = "1.0"))
	double StartValue = 0.0;

	/** How much of the text shows when the track ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "End Revealed", ClampMin = "0.0", ClampMax = "1.0"))
	double EndValue = 1.0;

	/**
	 * The text to type out. Empty types out whatever the text widget shows when the track starts - so the same
	 * animation reveals any line of dialogue the game puts in it: set its text, then play.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Reveal", meta = (DisplayName = "Text"))
	FText Text;

	/** Worked out as the track starts playing; never saved. Mutable: a text changed mid-reveal is worked out again. */
	mutable FWidgetAnimTextRevealPlan Plan;

	/** How each part appears, as its Style and settings say. */
	FWidgetAnimTextAppear GetAppear() const
	{
		return FWidgetAnimTextRevealLook::MakeAppear(Style, AppearTime, SlideFrom, Distance, bFadeIn, Appear);
	}
};

/** The Text Reveal type's evaluator. Registered by the NameTBD module; see FWidgetAnimEvaluator. */
class NAMETBD_API FWidgetAnimTextRevealEvaluator : public TWidgetAnimTextTrackEvaluator<FWidgetAnimTextRevealParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override;
	virtual bool PrefersLinearEasing() const override { return true; }

	virtual bool NeedsPlayPreparation(const FInstancedStruct& Params) const override { return true; }
	virtual float PrepareForPlay(UObject* Target, FInstancedStruct& Params, float AuthoredRun) const override;
	virtual bool IsReadyToPrepare(UObject* Target, const FInstancedStruct& Params) const override;
	virtual bool HoldsStartPose(const FInstancedStruct& Params) const override { return true; }
	virtual bool HasDerivedLength(const FInstancedStruct& Params) const override;
	virtual FText GetDerivedLengthLabel(const FInstancedStruct& Params) const override;

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override;
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;

	virtual bool CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const override;
	virtual void RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const override;

	/** The channel it writes: its own on a text widget, so a Count or a Scramble can play on the same text alongside it. */
	static const FName RevealChannel;

protected:

	virtual bool ClearEffects(FWidgetAnimTextEffects& Effects) const override;
};
