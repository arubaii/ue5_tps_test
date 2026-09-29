// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "WidgetAnimTextHost.h"

#include "WidgetAnimTextMarkup.generated.h"

class UMaterialInterface;

// =====================================================================================================
// Text tags
// =====================================================================================================
//
// A line of dialogue says how it should be shown in the line itself, with tags - the way a Rich Text Block's styles
// are written:
//
//     Well <shake>that</> was <wave>unexpected</>...<pause=0.6> <speed=3><angry>GET OUT!</></>
//
// The animated text widgets read these tags out of whatever text they are given - by a dialogue system, a Data Table,
// a String Table, a Blueprint, a View Model binding - and draw the tagged words with their effects, with no track and
// no setup on the widget. So they work with any dialogue tool: the tool only has to put the line in the text. Tags
// stay in the text through localization, as rich text styles do, so a translator places them where the language
// needs them.
//
// Two kinds of tag:
//
//  - Effect tags, from the project's Text Tags (Project Settings > Plugins > Widget Animations > Text Tags): each names
//    a set of effects - colors, a rainbow, a looping shimmer, a size, a wave, a shake, a pulse, a blink - and, for a
//    Text Reveal, how its words appear, how fast they type out and an animation to play when they are reached. The
//    project starts with a library of them (shake, wave, bounce, swing, pulse, blink, rainbow, shimmer, shout, whisper,
//    angry, pop, fast, slow) that designers edit and add to. <shake a=2 s=0.5> doubles a tag's size and halves its
//    speed; <shake=2> is the same as a=2.
//
//  - Built-in tags, always there: <speed=2>...</> types the words it tags twice as fast (0.5 half as fast; unclosed,
//    to the end of the line); <instant>...</> shows them all at once; <pause=0.5> waits half a second before the rest;
//    <event=Name> fires On Text Event with Name as the reveal reaches it (a sound, a portrait, a camera shake);
//    <play=AnimId> plays that Anim ID on the text widget as the reveal reaches it; <color=#FF4020> or <color=red>
//    colors the words it tags; <size=1.3> draws them larger; <glow>...</> gives them a soft glow (from Glow Defaults) -
//    <glow=cyan> or <glow=#40C0FF> in that color, r=6 its radius, a=1.5 its strength, p=0.4 a pulse and s=2 its speed.
//
// Glow and material effects: any effect tag with Glow ticked draws its letters, in its glow color, into a layer of their
// own, and a material turns that layer into the effect - drawn under the text (a glow) or over it (Glow Over Text). The
// material is the tag's Glow Material, or the project's Text Glow Material. So a new look is only a material and a tag:
// a User Interface material reading "Texture", given "Radius" (pixels) and "TagStrength", and any parameters the tag
// lists. In the text, r= a= p= s= set its radius, strength, pulse and speed, and any other attribute sets a material
// parameter of that name for that use: <magic Swirl=3 Tint=cyan>. The text widget builds the layers itself, one per
// material, side and set of parameters its text uses; a text with none costs nothing.
//
// </> closes the tag opened last and </shake> the last shake. Tags nest. On an Animated Rich Text Block, a tag its Text
// Style Set has a row for stays a style; everything else works as above. A name that is neither a style nor a tag is
// left in the text as written on a plain animated text, and taken out, as a Rich Text Block does, on a rich one.

/**
 * One effect tag: its name as written in the text, and what it does to the words it tags. Every setting is off until
 * ticked, so a tag is exactly the effects it names. See the note at the top of WidgetAnimTextMarkup.h.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimTextTag
{
	GENERATED_BODY()

	FWidgetAnimTextTag()
	{
		// A warm gold, so a tag with Color ticked shows something before a color is picked.
		Colors.Add(FLinearColor(1.f, 0.8f, 0.25f, 1.f));
	}

	/** What is written in the text: a Tag of shake is used as <shake>these words</>. Not case sensitive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag")
	FName Tag;

	// Look ---------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (InlineEditConditionToggle))
	bool bColor = false;

	/** The tagged letters' color. Several make a gradient across them, first letter to last. Their opacity is kept. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Color", EditCondition = "bColor"))
	TArray<FLinearColor> Colors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (InlineEditConditionToggle))
	bool bRainbow = false;

	/** Colors the tagged letters with a rainbow that flows along them - this many times round a second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Rainbow", EditCondition = "bRainbow", ClampMin = "0.0", UIMax = "3.0", Units = "Hz"))
	float RainbowSpeed = 0.5f;

	/** How many letters one full rainbow spans. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Rainbow Length", EditCondition = "bRainbow == true", EditConditionHides, ClampMin = "1.0", UIMax = "40.0"))
	float RainbowLength = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (InlineEditConditionToggle))
	bool bShimmer = false;

	/** A band of this color sweeping across the tagged words, over and over - a gleam on a reward's name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Shimmer", EditCondition = "bShimmer"))
	FLinearColor ShimmerColor = FLinearColor::White;

	/** Seconds from one sweep to the next. Each sweep takes the first half of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Shimmer Every", EditCondition = "bShimmer == true", EditConditionHides, ClampMin = "0.1", UIMax = "5.0", Units = "s"))
	float ShimmerEvery = 1.6f;

	/** How many letters either side of the band's middle are fully in it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Shimmer Width", EditCondition = "bShimmer == true", EditConditionHides, ClampMin = "0.0", UIMax = "10.0"))
	float ShimmerWidth = 1.f;

	/** How fully the band takes the letters' own color at its middle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Shimmer Strength", EditCondition = "bShimmer == true", EditConditionHides, ClampMin = "0.0", ClampMax = "1.0"))
	float ShimmerStrength = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (InlineEditConditionToggle))
	bool bSize = false;

	/** Draws the tagged letters this much larger, each about its own middle: 1.3 is 30% larger. The line isn't laid out again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Size", EditCondition = "bSize", ClampMin = "0.1", UIMax = "3.0"))
	float Size = 1.3f;

	// Glow and material effects -----------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (InlineEditConditionToggle))
	bool bGlow = false;

	/**
	 * A material effect on the tagged letters - a soft glow by default. The letters are drawn in this color into a
	 * layer of their own, and a material turns that layer into the effect: Glow Material, or the project's Text Glow
	 * Material when that is empty. Its opacity scales the effect's. See the note at the top of WidgetAnimTextMarkup.h.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Glow", EditCondition = "bGlow"))
	FLinearColor GlowColor = FLinearColor(1.f, 0.8f, 0.35f, 1.f);

	/** Draw the letters into the effect's layer in their own color (after any other color this tag gives them) instead of Glow's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Glow In Text Color", EditCondition = "bGlow == true", EditConditionHides))
	bool bGlowInTextColor = false;

	/**
	 * The material that makes the effect - any User Interface material reading the Texture Object Parameter "Texture"
	 * (the tagged letters, drawn in their glow color) and outputting a color and an opacity. It is given "Radius" (in
	 * pixels) and "TagStrength", plus the parameters below. Empty: the project's Text Glow Material.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Glow Material", EditCondition = "bGlow == true", EditConditionHides))
	TSoftObjectPtr<UMaterialInterface> GlowMaterial;

	/** Draw the effect over the letters rather than under them - a shine, scanlines, a dissolve - where a glow goes under. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Glow Over Text", EditCondition = "bGlow == true", EditConditionHides))
	bool bGlowOverText = false;

	/**
	 * How far the effect reaches past the letters' edges, in Slate units - the material's "Radius", and the room its
	 * layer is given. One layer has one radius: the largest of the tags drawn into it. <tag r=10> sets it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Glow Radius", EditCondition = "bGlow == true", EditConditionHides, ClampMin = "0.5", UIMax = "24.0"))
	float GlowRadius = 6.f;

	/** How strong the effect is - the material's "TagStrength": 0.5 half as strong, 2 twice as strong. <tag a=2> doubles it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Glow Strength", EditCondition = "bGlow == true", EditConditionHides, ClampMin = "0.0", UIMax = "3.0"))
	float GlowStrength = 1.f;

	/** How far the effect's strength dips and returns, over and over: 0 holds it steady, 1 fades it right out. <tag p=0.5> sets it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Glow Pulse", EditCondition = "bGlow == true", EditConditionHides, ClampMin = "0.0", ClampMax = "1.0"))
	float GlowPulse = 0.f;

	/** Pulses a second. <tag s=2> doubles it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Glow Pulse Speed", EditCondition = "bGlow == true", EditConditionHides, ClampMin = "0.0", UIMax = "6.0", Units = "Hz"))
	float GlowPulseSpeed = 1.f;

	/**
	 * Scalar parameters set on the material, by name. Written in the text, <tag Speed=3> sets (or adds) Speed for that
	 * use - any attribute besides a, s, r and p that is a number.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Glow Scalar Parameters", EditCondition = "bGlow == true", EditConditionHides))
	TMap<FName, float> GlowScalarParameters;

	/** Vector parameters set on the material, by name. <tag Tint=#FF8000> or <tag Tint=cyan> sets one for that use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Glow Vector Parameters", EditCondition = "bGlow == true", EditConditionHides))
	TMap<FName, FLinearColor> GlowVectorParameters;

	// Motion -------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (InlineEditConditionToggle))
	bool bWave = false;

	/** A wave along the tagged letters, looping: each one bobbing, swaying, tilting, swelling or hopping in turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Wave", EditCondition = "bWave"))
	EWidgetAnimTextWaveMotion WaveMotion = EWidgetAnimTextWaveMotion::Bob;

	/** How far: Slate units for Bob, Sway and Hop, degrees for Rotate, and for Scale how much bigger at the crest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Wave Amplitude", EditCondition = "bWave == true", EditConditionHides, UIMin = "0.0", UIMax = "20.0"))
	float WaveAmplitude = 3.f;

	/** How many letters one wave spans, crest to crest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Wavelength", EditCondition = "bWave == true", EditConditionHides, ClampMin = "0.5", UIMax = "30.0"))
	float WaveLength = 7.f;

	/** How many waves pass each letter a second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Wave Speed", EditCondition = "bWave == true", EditConditionHides, UIMin = "-4.0", UIMax = "4.0", Units = "Hz"))
	float WaveSpeed = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (InlineEditConditionToggle))
	bool bShake = false;

	/** Every tagged letter jittering on its own, this far in Slate units - anger, fear, cold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Shake", EditCondition = "bShake", ClampMin = "0.0", UIMax = "10.0"))
	float ShakeDistance = 1.5f;

	/** How far each letter may turn as it shakes, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Shake Angle", EditCondition = "bShake == true", EditConditionHides, ClampMin = "0.0", UIMax = "30.0", Units = "deg"))
	float ShakeAngle = 3.f;

	/** How many times a second each letter is thrown somewhere new. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Shakes Per Second", EditCondition = "bShake == true", EditConditionHides, ClampMin = "1.0", UIMax = "60.0"))
	float ShakeRate = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (InlineEditConditionToggle))
	bool bPulse = false;

	/** The tagged letters swelling and shrinking in turn, this much larger at their largest: 0.15 is 15%. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Pulse", EditCondition = "bPulse", ClampMin = "0.0", UIMax = "1.0"))
	float PulseAmount = 0.15f;

	/** Pulses a second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Pulse Speed", EditCondition = "bPulse == true", EditConditionHides, ClampMin = "0.0", UIMax = "6.0", Units = "Hz"))
	float PulseSpeed = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (InlineEditConditionToggle))
	bool bBlink = false;

	/** The tagged words fading down to this opacity and back, over and over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Blink", EditCondition = "bBlink", ClampMin = "0.0", ClampMax = "1.0"))
	float BlinkOpacity = 0.25f;

	/** Blinks a second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Blink Speed", EditCondition = "bBlink == true", EditConditionHides, ClampMin = "0.0", UIMax = "6.0", Units = "Hz"))
	float BlinkSpeed = 1.f;

	// Reveal -------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (InlineEditConditionToggle))
	bool bRevealSpeed = false;

	/** A Text Reveal types the tagged words this much faster: 2 twice as fast, 0.5 half as fast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Reveal Speed", EditCondition = "bRevealSpeed", ClampMin = "0.05", UIMax = "10.0"))
	float RevealSpeed = 2.f;

	/** A Text Reveal shows the tagged words all at once, as it reaches them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Reveal All At Once"))
	bool bRevealInstant = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (InlineEditConditionToggle))
	bool bRevealLook = false;

	/** How the tagged words appear as a Text Reveal reaches them, in place of the reveal's own look - a shout that stomps in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Reveal Look", EditCondition = "bRevealLook"))
	FWidgetAnimTextRevealLook RevealLook;

	/**
	 * An Anim ID played on the text widget as a Text Reveal reaches the tagged words - an entry of its Widget Animation,
	 * say a shake of the whole dialogue box. None plays nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tag", meta = (DisplayName = "Play Anim ID"))
	FName PlayAnimId;

	/** Whether it draws anything on its letters - a color, a size or a motion - rather than only changing a reveal. */
	bool DrawsEffect() const { return bColor || bRainbow || bShimmer || bSize || bGlow || bWave || bShake || bPulse || bBlink; }

	/** Whether what it draws moves on its own, so its text must be drawn every frame. */
	bool IsLive() const
	{
		return (bRainbow && RainbowSpeed != 0.f) || bShimmer || (bWave && WaveSpeed != 0.f) || bShake || (bPulse && PulseSpeed != 0.f) || (bBlink && BlinkSpeed != 0.f)
			|| (bGlow && GlowPulse > 0.f && GlowPulseSpeed != 0.f);
	}

	/** Scales its sizes by Amount and its speeds by Speed - what <tag a=2 s=0.5> asks for. */
	void Scale(float Amount, float Speed);
};

/**
 * What plain <glow> starts from - Project Settings > Plugins > Widget Animations > Text Tags > Glow Defaults. Its
 * attributes change it per use: <glow=cyan r=10 a=2 p=0.4 s=2>.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimTextGlowDefaults
{
	GENERATED_BODY()

	/** The glow in each letter's own color. Off: in Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (DisplayName = "In Text Color"))
	bool bInTextColor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (EditCondition = "!bInTextColor"))
	FLinearColor Color = FLinearColor(1.f, 0.8f, 0.35f, 1.f);

	/** How far the glow reaches past the letters, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (ClampMin = "0.5", UIMax = "24.0"))
	float Radius = 6.f;

	/** On top of the material's own Strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (ClampMin = "0.0", UIMax = "3.0"))
	float Strength = 1.f;

	/** How far it dips and returns, over and over: 0 steady. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Pulse = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (ClampMin = "0.0", UIMax = "6.0", Units = "Hz"))
	float PulseSpeed = 1.f;
};

/** One stretch of a text's drawn string an effect tag covers, and the tag as it applies there. */
struct NAMETBD_API FWidgetAnimTextSpan
{
	/** Where it is in the drawn string, [Begin, End). */
	int32 Begin = 0;
	int32 End = 0;

	FWidgetAnimTextTag Tag;

	/** Tag.RevealLook as it is drawn, when the tag has one. */
	FWidgetAnimTextAppear Appear;

	/**
	 * For a tag with Glow: which effect layer its letters are drawn into - one per material, side and set of material
	 * parameters, so tags that share all three share a layer. 0 for none (no Glow, or no material to draw it with).
	 */
	uint32 EffectKey = 0;

	/** The material its layer uses: the tag's Glow Material, or the project's Text Glow Material. */
	FSoftObjectPath EffectMaterial;

	bool Covers(int32 Index) const { return Index >= Begin && Index < End; }
};

/** What happens at one point of a reveal. */
enum class EWidgetAnimTextMarkerKind : uint8
{
	/** The reveal waits Seconds before going on. */
	Pause,

	/** On Text Event fires with Name. */
	Event,

	/** The text widget plays the Anim ID Name. */
	Play
};

/** Something that happens as a reveal reaches a point of the drawn string - a <pause>, an <event>, a <play>. */
struct NAMETBD_API FWidgetAnimTextMarker
{
	/** Where it is in the drawn string: it happens as the reveal reaches the first part at or after it. */
	int32 At = 0;

	EWidgetAnimTextMarkerKind Kind = EWidgetAnimTextMarkerKind::Pause;
	float Seconds = 0.f;
	FName Name;
};

/**
 * A text's tags, read: the string it draws, its rich text style runs, the stretches its effect tags cover and the
 * points its markers stand at. Made by WidgetAnimTextMarkup::Parse and shared, never changed after.
 */
struct NAMETBD_API FWidgetAnimTextMarkup
{
	/** The text as drawn - every tag taken out. */
	FString Display;

	/** Rich text style runs of Display, covering it in order, for a rich text. Empty for a plain one. */
	TArray<WidgetAnimRichText::FRun> StyleRuns;

	/** Every effect tag's stretch, in the order they were opened - an inner tag after the one around it. */
	TArray<FWidgetAnimTextSpan> Spans;

	/** Every marker, in order along the string. */
	TArray<FWidgetAnimTextMarker> Markers;

	/** Whether any span draws an effect, and whether any of those move on their own. */
	bool bDrawsEffects = false;
	bool bLiveEffects = false;

	/** Whether anything changes how a reveal plays - a speed, an instant stretch, a look of its own, a pause. */
	bool bShapesReveal = false;

	/** How much faster than its pace a reveal types the letter at Index: every speed tag around it, multiplied. */
	float GetRevealSpeedAt(int32 Index) const;

	/** Whether a reveal shows the letter at Index all at once with the letters around it. */
	bool IsInstantAt(int32 Index) const;

	/** The innermost tag with a reveal look of its own around Index, or null. */
	const FWidgetAnimTextSpan* FindRevealLookAt(int32 Index) const;

	/** Seconds of pause at markers after After and up to Upto - (After, Upto] in the drawn string. */
	float GetPauseBetween(int32 After, int32 Upto) const;

	/** Whether there is anything at all: no spans, no markers. */
	bool IsEmpty() const { return Spans.Num() == 0 && Markers.Num() == 0; }
};

namespace WidgetAnimTextMarkup
{
	/**
	 * Reads Source's tags. bRich: Source is a Rich Text Block's markup - its escapes read and every tag IsStyle names a
	 * style run (a tag that is neither a style nor a text tag too, as a Rich Text Block takes it). bTags: text tags and
	 * built-in tags are read; off, only styles are.
	 */
	NAMETBD_API void Parse(const FString& Source, bool bRich, bool bTags, TFunctionRef<bool(FName)> IsStyle, FWidgetAnimTextMarkup& Out);

	/** The project's text tag called Name, or null. */
	NAMETBD_API const FWidgetAnimTextTag* FindTag(FName Name);

	/** Bumped whenever the project's text tags change, so every text reads its tags again. */
	NAMETBD_API uint32 GetTagsVersion();
	NAMETBD_API void NotifyTagsChanged();

	/** The text tags a new project starts with - the library the project settings list until they are edited. */
	NAMETBD_API void GetDefaultTags(TArray<FWidgetAnimTextTag>& OutTags);

	/** The game's text reveal speed - see UWidgetAnimTextLibrary::SetGlobalTextRevealSpeed. */
	NAMETBD_API float GetGlobalRevealSpeed();
}

/** Text tags and text reveals, from Blueprint. */
UCLASS()
class NAMETBD_API UWidgetAnimTextLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * How much faster every Text Reveal paced by its Typing Speed plays, in every text widget - a player's Text Speed
	 * setting. 1 as authored, 2 twice as fast. Multiplies each widget's own Text Reveal Speed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Text", meta = (DisplayName = "Set Global Text Reveal Speed"))
	static void SetGlobalTextRevealSpeed(float Speed);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Text", meta = (DisplayName = "Get Global Text Reveal Speed"))
	static float GetGlobalTextRevealSpeed();

	/**
	 * Text with its text tags and built-in tags taken out - for a subtitle log, a history panel, a text-to-speech
	 * reader. Rich text style tags are kept.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Text", meta = (DisplayName = "Remove Text Tags"))
	static FText RemoveTextTags(const FText& Text);
};
