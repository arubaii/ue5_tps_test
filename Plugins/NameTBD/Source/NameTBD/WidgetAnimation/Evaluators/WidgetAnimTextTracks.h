// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEvaluator.h"
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextHost.h"
#include "WidgetAnimTextTracks.generated.h"

// =====================================================================================================
// The text tracks
// =====================================================================================================
//
// Every track in the Add Track menu's Text section: Text Reveal and Count (their own files), and the effects here -
// Scramble, Text Color, Shimmer, Wave, Shake and Letter Spacing. Each is its own track rather than one master text
// track with every setting on it, so a text can have any mix of them, each timed, keyed and staggered on its own, and
// each stays small enough to read at a glance. On an animated text widget (see WidgetAnimTextHost.h) each writes its own
// share of the widget's text effects and they combine; on a plain Text Block, those that can work through its text or
// color do, and the rest do nothing.

/** What a text track's target held before the track touched it, and so what is put back after. */
USTRUCT()
struct NAMETBD_API FWidgetAnimTextTrackBaseline
{
	GENERATED_BODY()

	UPROPERTY()
	FText Text;

	UPROPERTY()
	bool bHasText = false;

	UPROPERTY()
	FLinearColor Color = FLinearColor::White;

	UPROPERTY()
	bool bHasColor = false;

	/** A numeric text's own number, put back as a number so it keeps formatting itself. */
	UPROPERTY()
	double Number = 0.0;

	UPROPERTY()
	bool bHasNumber = false;
};

/**
 * What every text track shares: listed under Text, aimed at the text a widget holds (WidgetAnimText::ResolveTarget), and
 * a baseline that - on a text host - clears the track's own effects. A track that also works on plain text overrides
 * CaptureBaseline and RestoreBaseline to put its text or color back too.
 */
class NAMETBD_API FWidgetAnimTextTrackEvaluatorBase : public FWidgetAnimEvaluator
{
public:

	virtual FText GetCategory() const override;
	virtual UClass* GetTargetClass() const override;
	virtual UObject* ResolveTarget(UWidget* Widget, const FInstancedStruct& Params) const override;

	virtual bool CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const override;
	virtual void RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const override;

protected:

	/**
	 * Takes this track's effects off Effects - back to how FWidgetAnimTextEffects starts. True if anything was set, so
	 * the host is only told of a change there was.
	 */
	virtual bool ClearEffects(FWidgetAnimTextEffects& Effects) const = 0;

	/** Clears this track's effects on Target, if it is a text host with any set. */
	void ClearEffectsOn(UObject* Target) const;

	/** How many letters Target's text has as drawn. */
	static int32 CountLetters(const UObject* Target);
};

/**
 * The text a track is rewriting on a plain Text Block or Rich Text Block - which, having no effects of its own, can
 * only be shown a reveal or a scramble by having its text rewritten. The evaluators hold no state, so this lives here,
 * keyed by target and channel, like the quantized writes the library remembers.
 */
namespace WidgetAnimPlainText
{
	/**
	 * The text to rewrite from: Override when it isn't empty; else the text Target showed before the track first wrote
	 * it - or, once something else has set a new text since (the text no longer reads as the track last wrote it), that
	 * new text.
	 */
	NAMETBD_API FText GetSource(UObject* Target, FName Channel, const FText& Override);

	/** What the track has just written, to know its own writing from a new text next time. */
	NAMETBD_API void NoteWritten(UObject* Target, FName Channel, const FString& Written);

	/** The track has let the text go. */
	NAMETBD_API void Forget(UObject* Target, FName Channel);

	/** Text as drawn on Target: a rich text's markup taken out. */
	NAMETBD_API FString GetDisplayString(const UObject* Target, const FText& Text);

	/** Writes Text to Target if it doesn't already read so. */
	NAMETBD_API void Write(UObject* Target, const FText& Text);
}

/** A number from 0 up to (not including) Count, picked by Index and Seed: the same two always pick the same. */
NAMETBD_API int32 WidgetAnimTextPick(int32 Index, uint32 Seed, int32 Count);

/** FWidgetAnimTextTrackEvaluatorBase with the identity filled in for one settings struct, as TWidgetAnimEvaluator does. */
template <typename TParams>
class TWidgetAnimTextTrackEvaluator : public FWidgetAnimTextTrackEvaluatorBase
{
public:

	virtual const UScriptStruct* GetParamsStruct() const override { return TParams::StaticStruct(); }

protected:

	static const TParams* GetParams(const FInstancedStruct& Params) { return Params.GetPtr<TParams>(); }
};

// =====================================================================================================
// Scramble
// =====================================================================================================

/** What a Scramble's stand-in characters are picked from. */
UENUM(BlueprintType)
enum class EWidgetAnimScrambleCharset : uint8
{
	Letters UMETA(ToolTip = "A to Z, upper and lower case."),
	UpperCase UMETA(DisplayName = "Upper Case", ToolTip = "A to Z."),
	Digits UMETA(ToolTip = "0 to 9."),
	LettersAndDigits UMETA(DisplayName = "Letters And Digits", ToolTip = "A to Z, upper and lower case, and 0 to 9."),
	Symbols UMETA(ToolTip = "Punctuation and symbols - a glitch."),
	Binary UMETA(ToolTip = "0 and 1."),
	Hex UMETA(ToolTip = "0 to 9 and A to F - a terminal decoding."),
	Custom UMETA(ToolTip = "The characters in Custom Characters.")
};

/**
 * Scramble: a text decoding into place - each letter a flicker of random characters until it settles on itself, one
 * after another. A title unscrambling, a code being cracked, a glitch.
 *
 * Its value is how much of the text has settled, 0 to 1. Timed and keyed like any track: a Start of 1 and End of 0
 * scrambles a text away. With Scramble Ahead, only the next few letters flicker and the rest wait, unseen - a decode
 * sweeping in. Drawn letter by letter in each letter's own place on an animated text widget; on a plain Text Block its
 * text is rewritten, which may re-wrap as characters change width.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Scramble"))
struct NAMETBD_API FWidgetAnimScrambleParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** How much of the text has settled when the track starts: 0 none of it, 1 all of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scramble", meta = (DisplayName = "Start Settled", ClampMin = "0.0", ClampMax = "1.0"))
	double StartValue = 0.0;

	/** How much of the text has settled when the track ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scramble", meta = (DisplayName = "End Settled", ClampMin = "0.0", ClampMax = "1.0"))
	double EndValue = 1.0;

	/** What the flickering characters are picked from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scramble", meta = (DisplayName = "Characters"))
	EWidgetAnimScrambleCharset Charset = EWidgetAnimScrambleCharset::LettersAndDigits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scramble", meta = (DisplayName = "Custom Characters", EditCondition = "Charset == EWidgetAnimScrambleCharset::Custom", EditConditionHides))
	FString CustomCharset = TEXT("#%&*+=?@");

	/**
	 * Only this many letters past the settled ones flicker; the rest are not shown yet. Off: every letter not yet settled
	 * flickers, so the whole text is there from the start, scrambled.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scramble", meta = (DisplayName = "Scramble Ahead", InlineEditConditionToggle))
	bool bLimitAhead = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scramble", meta = (DisplayName = "Scramble Ahead", ClampMin = "1", UIMax = "20", EditCondition = "bLimitAhead"))
	int32 AheadCount = 6;

	/** How many times a second the flickering characters change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scramble", meta = (DisplayName = "Changes Per Second", ClampMin = "0.0", UIMax = "60.0"))
	float ChangesPerSecond = 16.f;

	/** The characters Charset stands for. */
	FString GetCharacters() const;
};

class NAMETBD_API FWidgetAnimScrambleEvaluator : public TWidgetAnimTextTrackEvaluator<FWidgetAnimScrambleParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override;

	// Prepared only so that, waiting for its turn, the text shows scrambled rather than already settled.
	virtual bool NeedsPlayPreparation(const FInstancedStruct& Params) const override { return true; }
	virtual bool HoldsStartPose(const FInstancedStruct& Params) const override { return true; }

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override;
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;

	virtual bool CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const override;
	virtual void RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const override;

protected:

	virtual bool ClearEffects(FWidgetAnimTextEffects& Effects) const override;
};

// =====================================================================================================
// Text Color
// =====================================================================================================

/** How a Text Color's color meets the text's own. */
UENUM(BlueprintType)
enum class EWidgetAnimTextColorMode : uint8
{
	Multiply UMETA(ToolTip = "Multiplies the text's own colors - white leaves them as they are, so a rich text's styles keep their differences. For fading or darkening."),
	Replace UMETA(ToolTip = "Takes the place of the text's own colors, keeping their opacity - every style the one color. For a flash or a highlight.")
};

/**
 * Text Color: a text changing color - a flash of red on damage, a highlight settling back, a label graying out.
 *
 * Its value blends from Start Color (0) to End Color (1); keys in between blend part way, so a key of 1 with the end at
 * 0 flashes and returns. On a plain Text Block it sets its Color and Opacity; on an animated text widget it colors
 * the text over its own colors, the whole text at once.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Text Color"))
struct NAMETBD_API FWidgetAnimTextColorParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Color")
	FLinearColor StartColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Color")
	FLinearColor EndColor = FLinearColor(1.f, 0.25f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Color")
	EWidgetAnimTextColorMode Mode = EWidgetAnimTextColorMode::Multiply;
};

class NAMETBD_API FWidgetAnimTextColorEvaluator : public TWidgetAnimTextTrackEvaluator<FWidgetAnimTextColorParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override;

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override { return bStart ? 0.0 : 1.0; }
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;

	virtual bool CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const override;
	virtual void RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const override;

protected:

	virtual bool ClearEffects(FWidgetAnimTextEffects& Effects) const override;
};

// =====================================================================================================
// Shimmer
// =====================================================================================================

/**
 * Shimmer: a band of light sweeping across a text, letter by letter - a gleam on a reward, a "new" label, loading text.
 *
 * Its value is where the band is: 0 just before the first letter, 1 just past the last, so each run of the track is one
 * sweep - loop it for a gleam every so often, with a Delay between. Animated text widgets only.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Shimmer"))
struct NAMETBD_API FWidgetAnimShimmerParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** The band's color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shimmer")
	FLinearColor Color = FLinearColor::White;

	/** How fully the band takes the place of the letters' own color at its middle, 0 to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shimmer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 0.8f;

	/** How many letters either side of its middle are fully in the band. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shimmer", meta = (DisplayName = "Width", ClampMin = "0.0", UIMax = "10.0"))
	float Width = 1.f;

	/** How many letters its edges fade out over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shimmer", meta = (ClampMin = "0.0", UIMax = "10.0"))
	float Softness = 2.5f;
};

class NAMETBD_API FWidgetAnimShimmerEvaluator : public TWidgetAnimTextTrackEvaluator<FWidgetAnimShimmerParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override;
	virtual bool PrefersLinearEasing() const override { return true; }

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override { return bStart ? 0.0 : 1.0; }
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;

protected:

	virtual bool ClearEffects(FWidgetAnimTextEffects& Effects) const override;
};

// =====================================================================================================
// Wave
// =====================================================================================================

/**
 * Wave: letters bobbing, swaying, tilting or swelling one after another in a wave that travels along the text - happy
 * dialogue, a floating title, a "press any key".
 *
 * Its value is the wave's strength: 0 still, 1 at full Amplitude - so a track from 0 to 1 eases a wave in, and one
 * held at 1 (the default) waves for as long as it plays. It keeps moving on its own while its strength holds, with a
 * held final pose too. Animated text widgets only.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Wave"))
struct NAMETBD_API FWidgetAnimWaveParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave", meta = (DisplayName = "Start Strength", ClampMin = "0.0", UIMax = "1.0"))
	double StartValue = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave", meta = (DisplayName = "End Strength", ClampMin = "0.0", UIMax = "1.0"))
	double EndValue = 1.0;

	/** What the wave moves on each letter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave")
	EWidgetAnimTextWaveMotion Motion = EWidgetAnimTextWaveMotion::Bob;

	/** How far at full strength: Slate units for Bob and Sway, degrees for Rotate, and for Scale how much bigger at the crest (0.2 is 20%). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave", meta = (UIMin = "0.0", UIMax = "20.0"))
	float Amplitude = 4.f;

	/** How many letters one wave spans, crest to crest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave", meta = (DisplayName = "Wavelength", ClampMin = "0.5", UIMax = "30.0"))
	float Wavelength = 8.f;

	/** How many waves pass each letter a second. Negative travels the other way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wave", meta = (UIMin = "-4.0", UIMax = "4.0", Units = "Hz"))
	float Speed = 1.f;
};

class NAMETBD_API FWidgetAnimWaveEvaluator : public TWidgetAnimTextTrackEvaluator<FWidgetAnimWaveParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override;

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override;
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;

protected:

	virtual bool ClearEffects(FWidgetAnimTextEffects& Effects) const override;
};

// =====================================================================================================
// Shake
// =====================================================================================================

/**
 * Shake: every letter jittering on its own - anger, fear, an impact, a warning.
 *
 * Its value is the shake's strength: by default 1 falling to 0, a jolt that dies away; hold it at 1 for a steady
 * tremble. Animated text widgets only.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Shake"))
struct NAMETBD_API FWidgetAnimShakeParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (DisplayName = "Start Strength", ClampMin = "0.0", UIMax = "1.0"))
	double StartValue = 1.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (DisplayName = "End Strength", ClampMin = "0.0", UIMax = "1.0"))
	double EndValue = 0.0;

	/** How far each letter may be thrown at full strength, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (ClampMin = "0.0", UIMax = "10.0"))
	float Distance = 2.f;

	/** How far each letter may be turned at full strength, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (ClampMin = "0.0", UIMax = "30.0", Units = "deg"))
	float Angle = 4.f;

	/** How many times a second every letter is thrown somewhere new. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (DisplayName = "Shakes Per Second", ClampMin = "1.0", UIMax = "60.0"))
	float Rate = 24.f;
};

class NAMETBD_API FWidgetAnimShakeEvaluator : public TWidgetAnimTextTrackEvaluator<FWidgetAnimShakeParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override;

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override;
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;

protected:

	virtual bool ClearEffects(FWidgetAnimTextEffects& Effects) const override;
};

// =====================================================================================================
// Letter Spacing
// =====================================================================================================

/**
 * Letter Spacing: letters drawing together or spreading apart from the middle of each line - a title tracking in to
 * settle, a word breathing out.
 *
 * Its value is the space added between each two letters, in Slate units - negative draws them closer. The text is not
 * laid out again: each line spreads about its middle, so wrapping never changes as it moves. Animated text widgets only.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Letter Spacing"))
struct NAMETBD_API FWidgetAnimLetterSpacingParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** Space between letters when the track starts, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Letter Spacing", meta = (DisplayName = "Start Spacing", UIMin = "-10.0", UIMax = "40.0"))
	double StartValue = 12.0;

	/** Space between letters when it ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Letter Spacing", meta = (DisplayName = "End Spacing", UIMin = "-10.0", UIMax = "40.0"))
	double EndValue = 0.0;
};

class NAMETBD_API FWidgetAnimLetterSpacingEvaluator : public TWidgetAnimTextTrackEvaluator<FWidgetAnimLetterSpacingParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override;

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override;
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;

protected:

	virtual bool ClearEffects(FWidgetAnimTextEffects& Effects) const override;
};
