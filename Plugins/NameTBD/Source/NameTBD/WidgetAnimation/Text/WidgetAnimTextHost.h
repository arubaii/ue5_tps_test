// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEasing.h" // EWidgetAnimEasing

#include "WidgetAnimTextHost.generated.h"

class UWidget;

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

	/** How each part moves from where it starts to its place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appear")
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;
};

/** What a Wave moves on each letter. */
UENUM(BlueprintType)
enum class EWidgetAnimTextWaveMotion : uint8
{
	Bob UMETA(ToolTip = "Up and down: Amplitude in Slate units."),

	Sway UMETA(ToolTip = "Side to side: Amplitude in Slate units."),

	Rotate UMETA(ToolTip = "Tilting back and forth about each letter's middle: Amplitude in degrees."),

	Scale UMETA(ToolTip = "Growing and shrinking about each letter's middle: Amplitude is how much bigger at the crest - 0.2 is 20% larger.")
};

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
	 * not appeared. A part between 0 and 1 is drawn part way in from Appear.
	 */
	TArray<float> RevealAlphas;

	FWidgetAnimTextAppear Appear;

	/** Drawn just after the last part that has started to appear, while the text isn't all shown. Empty: none. */
	FString Cursor;

	/** The cursor blinks, twice a second. */
	bool bCursorBlink = true;

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

	/** Whether anything at all is set - with nothing, the host can draw its text whole. */
	bool IsEmpty() const
	{
		return !bReveal && !bScramble && !bTint && !bShimmer && !bWave && !bShake && ExtraSpacing == 0.f;
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
