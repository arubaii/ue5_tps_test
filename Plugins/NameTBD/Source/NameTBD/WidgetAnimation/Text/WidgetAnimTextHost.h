// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEasing.h" // EWidgetAnimEasing

#include "WidgetAnimTextHost.generated.h"

class UWidget;
struct FWidgetAnimTextMarkup;

// =====================================================================================================
// Text hosts and text effects
// =====================================================================================================
//
// The text tracks - Text Reveal, Count, Scramble, Text Color, Shimmer, Wave, Shake, Letter Spacing - each animate one
// thing about a text. On a plain Text Block (or Rich Text Block) the few that can work through its text do: a reveal
// or a scramble writes shorter or garbled text, a count writes numbers, a color sets its color. On an animated text
// widget - Animated Text Block, Animated Rich Text Block and their Common UI counterparts - every one of them works,
// letter by letter, because such a widget is a text host: it draws each letter itself and keeps one set of text
// effects (FWidgetAnimTextEffects) that the tracks write their share of and it draws from.
//
// Each track owns its own fields of the effects - the reveal's, the wave's - and writes them every frame it plays, as
// its channel (see FWidgetAnimEvaluator::GetChannel). Putting a track's baseline back clears its fields. So any
// number of text tracks, of different kinds, play on one text at once and combine: a line types out with each letter
// rising in, while a shimmer sweeps across and the whole thing gently waves.
//
// Effects are drawn on top of the text's layout, never by laying it out again: a letter that is not revealed yet
// keeps its place, a wave or a shake moves letters about their places, Letter Spacing pushes letters apart from the
// middle of their line without re-wrapping it. Only Scramble changes which glyph a letter shows, and it draws the
// stand-in in the letter's own box.
//
// A text host also reads tags in its own text - <shake>this</>, <speed=2>, <pause=0.5> - and draws their effects on the
// words they tag, with no track at all (see WidgetAnimTextMarkup.h). That is how a line of dialogue carries its own
// emphasis: whatever dialogue system sets the text, the tags come with it.

// Appearing
//
// How a part of a text appears as a Text Reveal reaches it - or as a text tag's own reveal look reaches the letters it
// tags. Designers pick a Style (Typewriter, Fade, Slide, Pop...) and tune a few words' worth of settings; each style
// is then turned into an FWidgetAnimTextAppear - where the part starts from and how it gets to its place - which is
// what the text widget draws. Custom hands over an FWidgetAnimTextAppear as authored.

/** How each part of a text appears once a reveal reaches it. */
UENUM(BlueprintType)
enum class EWidgetAnimTextRevealStyle : uint8
{
	Typewriter UMETA(ToolTip = "Each part pops on at once, in place - the classic dialogue box."),

	Fade UMETA(ToolTip = "Each part fades in, in place - a soft, calm line."),

	Slide UMETA(ToolTip = "Each part slides into place from a side (Slide From), fading in as it comes or not (Fade In As It Appears)."),

	Pop UMETA(ToolTip = "Each part grows from nothing, overshoots a little and settles - lively, cartoon-like."),

	Stomp UMETA(ToolTip = "Each part lands from much larger, as if stamped down - an impact, a shout, a title card."),

	Drop UMETA(ToolTip = "Each part drops in from above and bounces into place."),

	Spin UMETA(ToolTip = "Each part spins into place about its middle as it grows."),

	Flip UMETA(ToolTip = "Each part flips up into place, opening from a flat line - a flap display."),

	Stretch UMETA(ToolTip = "Each part stretches out sideways from its middle."),

	Blur UMETA(ToolTip = "Each part comes into focus from a soft blur - Distance sets how soft."),

	Wipe UMETA(ToolTip = "Each part is wiped on from left to right, as if written by hand."),

	Scatter UMETA(ToolTip = "Each part flies in from a direction of its own and gathers into place."),

	Custom UMETA(ToolTip = "Set where each part starts from - its opacity, offset, scale, angle, stretch and blur - yourself, under Custom Appear.")
};

/** Which side a Slide reveal brings each part in from. */
UENUM(BlueprintType)
enum class EWidgetAnimTextSlideFrom : uint8
{
	Below,
	Above,
	Left,
	Right
};

/** How a letter (or word, or line) of a Text Reveal looks as it appears: where it comes from, over how long. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimTextAppear
{
	GENERATED_BODY()

	/**
	 * Seconds each part takes to appear once it is reached. 0 pops each one on at once, typewriter style. Longer
	 * overlaps them - several letters are always part way in - which reads as a soft wipe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear", meta = (DisplayName = "Appear Time", ClampMin = "0.0", UIMax = "1.0", Units = "s"))
	float Time = 0.12f;

	/** How see-through a part is as it starts to appear. 0 fades it in; 1 doesn't fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear", meta = (DisplayName = "From Opacity", ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.f;

	/** Where a part starts from, relative to its place - (0, 8) rises in from below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear", meta = (DisplayName = "From Offset"))
	FVector2D Offset = FVector2D(0.0, 6.0);

	/** How big a part starts: 1 as it is, 0 grows in from nothing, 1.5 lands from larger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear", meta = (DisplayName = "From Scale", ClampMin = "0.0", UIMax = "3.0"))
	float Scale = 1.f;

	/** How far turned a part starts, in degrees about its middle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear", meta = (DisplayName = "From Angle", UIMin = "-180.0", UIMax = "180.0", Units = "deg"))
	float Angle = 0.f;

	/**
	 * How squashed a part starts on each axis, on top of From Scale: (1, 0) opens up from a flat line, (0, 1) stretches
	 * out sideways from its middle. (1, 1) is neither.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear", meta = (DisplayName = "From Stretch", UIMin = "0.0", UIMax = "3.0"))
	FVector2D Stretch = FVector2D(1.0, 1.0);

	/** How blurred a part starts, in Slate units - it comes into focus as it appears. 0 is sharp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear", meta = (DisplayName = "From Blur", ClampMin = "0.0", UIMax = "12.0"))
	float Blur = 0.f;

	/**
	 * How far each part starts in a direction of its own, in Slate units - on top of From Offset. The parts gather in
	 * from all around.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear", meta = (DisplayName = "Scatter", ClampMin = "0.0", UIMax = "80.0"))
	float Scatter = 0.f;

	/** Each part is uncovered from left to right as it appears, rather than all of it at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear", meta = (DisplayName = "Wipe On"))
	bool bWipe = false;

	/** How each part moves from where it starts to its place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear")
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;

	/**
	 * How far past its place a part travels before it settles back - 0 not at all, 1.7 a lively bounce. Replaces
	 * Easing while above 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear", meta = (ClampMin = "0.0", UIMax = "3.0"))
	float Overshoot = 0.f;

	/** How far along its way in a part is drawn when it has appeared Alpha (0 to 1) of the way. May pass 1 with Overshoot. */
	float Ease(float Alpha) const;
};

/**
 * A reveal look as a designer picks it: a Style and the few settings it takes. What a Text Reveal track and a text tag
 * (see WidgetAnimTextMarkup.h) are both given, turned into an FWidgetAnimTextAppear by ToAppear.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimTextRevealLook
{
	GENERATED_BODY()

	/** How each part appears once it is reached. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reveal")
	EWidgetAnimTextRevealStyle Style = EWidgetAnimTextRevealStyle::Typewriter;

	/** Seconds each part takes to appear. Longer overlaps them, several parts always on their way in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reveal", meta = (DisplayName = "Appear Time", ClampMin = "0.0", UIMax = "1.0", Units = "s", EditCondition = "Style != EWidgetAnimTextRevealStyle::Typewriter && Style != EWidgetAnimTextRevealStyle::Custom", EditConditionHides))
	float AppearTime = 0.25f;

	/** Which side each part slides in from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reveal", meta = (DisplayName = "Slide From", EditCondition = "Style == EWidgetAnimTextRevealStyle::Slide", EditConditionHides))
	EWidgetAnimTextSlideFrom SlideFrom = EWidgetAnimTextSlideFrom::Below;

	/** How far each part travels - or, for Blur, how soft it starts - in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reveal", meta = (ClampMin = "0.0", UIMax = "60.0", EditCondition = "Style == EWidgetAnimTextRevealStyle::Slide || Style == EWidgetAnimTextRevealStyle::Drop || Style == EWidgetAnimTextRevealStyle::Blur || Style == EWidgetAnimTextRevealStyle::Scatter", EditConditionHides))
	float Distance = 12.f;

	/** Each part fades in as it moves into place. Off, it is fully there from its first frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reveal", meta = (DisplayName = "Fade In As It Appears", EditCondition = "Style != EWidgetAnimTextRevealStyle::Typewriter && Style != EWidgetAnimTextRevealStyle::Fade && Style != EWidgetAnimTextRevealStyle::Blur && Style != EWidgetAnimTextRevealStyle::Wipe && Style != EWidgetAnimTextRevealStyle::Custom", EditConditionHides))
	bool bFadeIn = true;

	/** Every setting of the appearance, for a Style of Custom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reveal", meta = (DisplayName = "Custom Appear", EditCondition = "Style == EWidgetAnimTextRevealStyle::Custom", EditConditionHides))
	FWidgetAnimTextAppear Custom;

	/** The appearance this look stands for. */
	FWidgetAnimTextAppear ToAppear() const { return MakeAppear(Style, AppearTime, SlideFrom, Distance, bFadeIn, Custom); }

	/** The appearance a Style stands for with these settings - shared by every place that offers the styles. */
	static FWidgetAnimTextAppear MakeAppear(EWidgetAnimTextRevealStyle InStyle, float InAppearTime, EWidgetAnimTextSlideFrom InSlideFrom,
		float InDistance, bool bInFadeIn, const FWidgetAnimTextAppear& InCustom);
};

/** What a Wave moves on each letter. */
UENUM(BlueprintType)
enum class EWidgetAnimTextWaveMotion : uint8
{
	Bob UMETA(ToolTip = "Up and down: Amplitude in Slate units."),

	Sway UMETA(ToolTip = "Side to side: Amplitude in Slate units."),

	Rotate UMETA(ToolTip = "Tilting back and forth about each letter's middle: Amplitude in degrees."),

	Scale UMETA(ToolTip = "Growing and shrinking about each letter's middle: Amplitude is how much bigger at the crest - 0.2 is 20% larger."),

	Hop UMETA(ToolTip = "Hopping up from its place and landing again, never below it - a bounce. Amplitude in Slate units.")
};

// Changing

/** How a text's changed characters make way for the new ones - a Count's number ticking over, say. */
UENUM(BlueprintType)
enum class EWidgetAnimTextChangeStyle : uint8
{
	Instant UMETA(ToolTip = "The new text simply replaces the old."),

	SlotMachine UMETA(DisplayName = "Slot Machine", ToolTip = "Each changed digit rolls up or down to the next, as on a slot machine's reel or an odometer - through the digits in between."),

	Roulette UMETA(ToolTip = "Each changed digit rolls in sideways, from the right or the left, as on a roulette strip."),

	Fade UMETA(ToolTip = "Each changed digit fades out as the new one fades in, in place."),

	Blend UMETA(ToolTip = "Each changed digit melts into the next: the old one softens and shrinks away as the new one comes into focus.")
};

/** What moves when a text changes. */
UENUM(BlueprintType)
enum class EWidgetAnimTextChangeScope : uint8
{
	ChangedDigits UMETA(DisplayName = "Changed Digits", ToolTip = "Only the characters that changed - 1,250 to 1,260 moves only the 5. Characters that stay slide to their new places if the text got wider or narrower."),

	WholeNumber UMETA(DisplayName = "Whole Number", ToolTip = "The whole number moves as one, whatever changed in it. Text around it - a label, a unit - stays put.")
};

// =====================================================================================================
// Effects
// =====================================================================================================

/**
 * Everything the text tracks have set on one text host this frame. Plain data, drawn by the host. Each track writes
 * only its own fields; see the note at the top of WidgetAnimTextHost.h.
 */
struct NAMETBD_API FWidgetAnimTextEffects
{
	// Text Reveal ----------------------------------------------------------------------------------

	/** A reveal is in force: parts not shown yet are not drawn. */
	bool bReveal = false;

	/** What the reveal counts in - WidgetAnimPartUnits::Letter, Word or Line. */
	FName RevealUnit;

	/**
	 * How far each part of RevealUnit has appeared, in order: 0 not yet, 1 fully. Parts past the end of the array have
	 * not appeared. A part between 0 and 1 is drawn part way in from its appearance - Appear, or a text tag's own.
	 */
	TArray<float> RevealAlphas;

	FWidgetAnimTextAppear Appear;

	/** Drawn just after the last part that has started to appear, while the text isn't all shown. Empty: none. */
	FString Cursor;

	/** The cursor blinks, twice a second. */
	bool bCursorBlink = true;

	/**
	 * The text still to come is shown faintly, at GhostOpacity, and each part fills in as the reveal reaches it - a
	 * preview of the whole line. Off, parts not reached yet aren't drawn at all.
	 */
	bool bRevealGhost = false;
	float GhostOpacity = 0.3f;

	/** The text still to come in this color (at GhostOpacity) rather than its own. */
	bool bGhostColor = false;
	FLinearColor GhostColor = FLinearColor::White;

	// Scramble -------------------------------------------------------------------------------------

	bool bScramble = false;

	/** Letters from the start that show as themselves. */
	int32 ScrambleSettled = 0;

	/** Letters after those that show a random stand-in; any after that are not drawn. Negative: every one is scrambled. */
	int32 ScrambleAhead = -1;

	/** What the stand-ins are picked from. */
	FString ScrambleCharset;

	/** Picks the stand-ins: a new seed picks new ones. */
	uint32 ScrambleSeed = 0;

	/** How many times a second the stand-ins change on their own, the host keeping time. 0: only when the seed does. */
	float ScrambleRate = 0.f;

	// Text Color -----------------------------------------------------------------------------------

	/** Colors the whole text: multiplies its own colors, or with bTintReplaces takes their place (their opacity kept). */
	bool bTint = false;
	FLinearColor Tint = FLinearColor::White;
	bool bTintReplaces = false;

	/**
	 * The letters each of these effects is limited to - a track's Part of the Text (see FWidgetAnimTextTarget), each a first
	 * letter (X) and how many (Y); see WidgetAnimTextParts::Covers. Empty: every letter, as always.
	 */
	TArray<FIntPoint> TintLetters;
	TArray<FIntPoint> ShimmerLetters;
	TArray<FIntPoint> WaveLetters;
	TArray<FIntPoint> ShakeLetters;

	// Shimmer --------------------------------------------------------------------------------------

	bool bShimmer = false;

	/** Where the band's middle is, in letters from the first; its half width, and how many letters its edges fade over. */
	float ShimmerCenter = 0.f;
	float ShimmerWidth = 2.f;
	float ShimmerSoftness = 2.f;

	/** The band's color, and how fully it replaces the text's own color at its middle. */
	FLinearColor ShimmerColor = FLinearColor::White;
	float ShimmerStrength = 1.f;

	// Wave -----------------------------------------------------------------------------------------

	bool bWave = false;
	EWidgetAnimTextWaveMotion WaveMotion = EWidgetAnimTextWaveMotion::Bob;

	/** How far along its cycle the wave is, in cycles; its size; and how many letters one wave spans. */
	float WavePhase = 0.f;
	float WaveAmplitude = 4.f;
	float WaveLength = 8.f;

	/** Cycles a second the wave travels on its own, the host keeping time - on top of WavePhase. */
	float WaveSpeed = 0.f;

	// Shake ----------------------------------------------------------------------------------------

	bool bShake = false;

	/** How far each letter may be thrown, in Slate units, and how far turned, in degrees. */
	float ShakeAmount = 0.f;
	float ShakeAngle = 0.f;

	/** Picks the throws: a new seed throws every letter somewhere new. */
	uint32 ShakeSeed = 0;

	/** How many times a second every letter is thrown somewhere new on its own, the host keeping time. */
	float ShakeRate = 0.f;

	// Letter Spacing -------------------------------------------------------------------------------

	/** Slate units added between each two letters of a line, pushing them apart from the line's middle. May be negative. */
	float ExtraSpacing = 0.f;

	// Count: how the text changes ------------------------------------------------------------------

	/**
	 * How the text's characters make way when the text changes while this is set: a Count ticking over, drawn rolling,
	 * fading or blending rather than replaced. The host keeps the time each change takes. Instant: not set.
	 */
	EWidgetAnimTextChangeStyle ChangeStyle = EWidgetAnimTextChangeStyle::Instant;
	EWidgetAnimTextChangeScope ChangeScope = EWidgetAnimTextChangeScope::ChangedDigits;

	/** Seconds each change takes, and how it moves. */
	float ChangeTime = 0.3f;
	EWidgetAnimEasing ChangeEasing = EWidgetAnimEasing::EaseOut;

	/**
	 * Which way rolling changes come in: +1 the new characters from below (Slot Machine) or the right (Roulette), -1
	 * from above or the left, 0 whichever way the number went - in from below or the right when it went up.
	 */
	int32 ChangeFrom = 0;

	/** Which way the number last went: +1 up, -1 down, 0 not known - then the host compares the numbers itself. */
	int32 ChangeTrend = 0;

	/** A rolling digit passes through every digit between its old and new one, as a reel does. Off, it rolls straight to the new one. */
	bool bChangeThroughDigits = true;

	/** Whether anything at all is set - with nothing, the host can draw its text whole. */
	bool IsEmpty() const
	{
		return !bReveal && !bScramble && !bTint && !bShimmer && !bWave && !bShake && ExtraSpacing == 0.f
			&& ChangeStyle == EWidgetAnimTextChangeStyle::Instant;
	}

	/** Whether anything set moves on its own, so the host must be drawn every frame to show it. */
	bool IsLive() const
	{
		return (bWave && WaveSpeed != 0.f) || (bShake && ShakeRate > 0.f) || (bScramble && ScrambleRate > 0.f)
			|| (bReveal && bCursorBlink && !Cursor.IsEmpty() && !IsRevealComplete());
	}

	/** Whether a reveal has every part fully shown - or isn't set, which hides nothing. */
	bool IsRevealComplete() const
	{
		if (!bReveal)
		{
			return true;
		}
		for (const float Alpha : RevealAlphas)
		{
			if (Alpha < 1.f)
			{
				return false;
			}
		}
		return true;
	}
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UWidgetAnimTextHost : public UInterface
{
	GENERATED_BODY()
};

/**
 * A text widget the text tracks can animate letter by letter: it draws its letters itself, from its text and the
 * effects the tracks set. Implemented by the Animated Text Block, the Animated Rich Text Block and their Common UI
 * counterparts. C++ only.
 */
class NAMETBD_API IWidgetAnimTextHost
{
	GENERATED_BODY()

public:

	/** The effects every text track playing on it writes into. Call OnWidgetAnimTextEffectsChanged after a change. */
	virtual FWidgetAnimTextEffects& GetWidgetAnimTextEffects() = 0;

	/** Draws the new effects, and tells anything listening which parts have just been revealed. */
	virtual void OnWidgetAnimTextEffectsChanged() = 0;

	/** Its text as authored - with a rich text's style tags. */
	virtual FText GetWidgetAnimText() const = 0;

	/** Replaces its text. */
	virtual void SetWidgetAnimText(const FText& InText) = 0;

	/** Its text as drawn: style tags taken out, upper or lower case applied. What its letters are counted in. */
	virtual FString GetWidgetAnimDisplayString() const = 0;

	/**
	 * A numeric text's number - Common Animated Numeric Text keeps one and formats it itself. False for any other text,
	 * which a Count writes as text instead.
	 */
	virtual bool GetWidgetAnimNumber(double& OutValue) const { return false; }

	/** Sets a numeric text's number, for it to format its own way. False, doing nothing, on any other text. */
	virtual bool SetWidgetAnimNumber(double Value) { return false; }

	/**
	 * Its text's tags, as read from the text it shows (see WidgetAnimTextMarkup.h): the effects on tagged words, and
	 * where a reveal speeds up, slows down, pauses and fires events. Null when its text has none.
	 */
	virtual TSharedPtr<const FWidgetAnimTextMarkup> GetWidgetAnimTextMarkup() const { return nullptr; }

	/**
	 * How much faster than authored a Text Reveal paced by its Typing Speed plays on it - the widget's own Text Reveal
	 * Speed times the game's (see UWidgetAnimTextLibrary::SetGlobalTextRevealSpeed). 1 as authored.
	 */
	virtual float GetWidgetAnimRevealSpeed() const { return 1.f; }
};

/**
 * Text, wherever it is kept: on a text host, a Text Block or a Rich Text Block. What the text tracks read and write
 * through, so each works on all three.
 */
namespace WidgetAnimText
{
	/**
	 * The text a text track aimed at Widget acts on: Widget itself when it is a text host, a Text Block or a Rich Text
	 * Block, or else the first of those inside it, depth first, into User Widgets - a text host preferred over a plain
	 * text anywhere inside. Null when there is none.
	 */
	NAMETBD_API UObject* ResolveTarget(UWidget* Widget);

	/** Whether Object is something ResolveTarget can return. */
	NAMETBD_API bool IsText(const UObject* Object);

	/** Object as a text host, or null for a plain Text Block or Rich Text Block. */
	NAMETBD_API IWidgetAnimTextHost* AsHost(UObject* Object);

	/** Its text as authored - markup included on rich text. */
	NAMETBD_API FText GetText(const UObject* Object);

	/** Replaces its text. */
	NAMETBD_API void SetText(UObject* Object, const FText& InText);

	/** Its text as drawn, style tags taken out: what letters and words are counted in. */
	NAMETBD_API FString GetDisplayString(const UObject* Object);

	/**
	 * The number it shows: a numeric text's own number, or else the first number in its text as drawn - "Gold: 1,250"
	 * reads 1250, grouping and decimals as the player's culture writes them. False when there is none.
	 */
	NAMETBD_API bool GetNumber(const UObject* Object, double& OutValue);

	/** The first number in Text, as GetNumber reads it. */
	NAMETBD_API bool ParseNumber(const FString& Text, double& OutValue);

	/** Its color, and setting it - a Text Block's Color and Opacity; a text host's own color is left alone (see bTint). */
	NAMETBD_API bool GetColor(const UObject* Object, FLinearColor& OutColor);
	NAMETBD_API void SetColor(UObject* Object, const FLinearColor& InColor);
}

/**
 * Rich text markup, the way a Rich Text Block reads it: <Style>text</> runs, &lt; &gt; &amp; &quot; &apos; escapes.
 * A self-closing tag - a decorator such as an inline image, <img id="Coin"/> - is left out; the animated rich text
 * widgets draw styles only.
 */
namespace WidgetAnimRichText
{
	/** One stretch of the display string in one style. Style None: the text's default style. */
	struct FRun
	{
		int32 Begin = 0;
		int32 End = 0;
		FName Style;
	};

	/** Markup as the string it draws, and the styled stretches of that string, covering all of it, in order. */
	NAMETBD_API void Parse(const FString& Markup, FString& OutDisplay, TArray<FRun>& OutRuns);
}
