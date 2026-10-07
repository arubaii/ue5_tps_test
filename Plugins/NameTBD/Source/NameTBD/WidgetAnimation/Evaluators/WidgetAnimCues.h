// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/WidgetAnimation/WidgetAnimCue.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h" // EWidgetAnimPlayDirection
#include "WidgetAnimCues.generated.h"

class USoundBase;

// =====================================================================================================
// The first-party cues
// =====================================================================================================
//
// Listed under Cues in the Add Track menu: Event and Sound. Play Animation is no longer offered - an existing animation is
// inserted as a track of its own instead (Insert Existing Widget Animation) - but still plays wherever a track of it was made. How and when every cue fires - its points,
// loops, ping-pong, inverted play, scrubbing - is in WidgetAnimCue.h; these only say what happens when one does. A
// particle, a camera shake or anything else the game owns is an Event handled there, or a Blueprint cue type (see
// UWidgetAnimBlueprintCue): UMG has no particle widget of its own to fire at.

// -----------------------------------------------------------------------------------------------------
// Event
// -----------------------------------------------------------------------------------------------------

/**
 * Event: tells the game a moment has come - "Landed", "Revealed", "Beat" - by name, for Blueprint or C++ to act on. Each
 * name is an event of its own in a Widget Blueprint's graph, named by the animation it is in - On Card Event Track Landed
 * Fired for WA_Card, added from the + in the Events of the Widget Animation Component's Details - called on the nearest
 * User Widget around the widget playing it that has it. An Event Dispatcher named On + the name (OnLanded), declared by
 * any User Widget around it, is broadcast too, so a widget holding this one can listen. Also heard by the component's On Animation Event, by the nearest User Widget with the
 * Widget Animation Event Listener interface, and by WidgetAnimCues::OnEvent in C++.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Event"))
struct NAMETBD_API FWidgetAnimEventCueParams : public FWidgetAnimCueParams
{
	GENERATED_BODY()

	/**
	 * What the event is called. An event of this name appears in the Events of the Widget Animation Component playing the
	 * animation - in its Details, named after the animation and this: On Card Event Track Landed Fired for Landed in WA_Card -
	 * where its + adds it to the widget's graph to handle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event", meta = (DisplayName = "Event Name"))
	FName EventName = TEXT("Event");

	/** A number handed along with the event at the track's start and end. Each key carries its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Event")
	float Value = 0.f;
};

class NAMETBD_API FWidgetAnimEventCueEvaluator : public TWidgetAnimCueEvaluator<FWidgetAnimEventCueParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetTrackDescription(const FInstancedStruct& Params) const override;
	virtual FText GetTrackSummary(const FInstancedStruct& Params) const override;
	virtual void Fire(const FWidgetAnimCueContext& Context) const override;

protected:

	virtual double GetPointValue(const FInstancedStruct& Params) const override;
};

// -----------------------------------------------------------------------------------------------------
// Sound
// -----------------------------------------------------------------------------------------------------

/** How a Sound cue's volume moves while it fades in or out - the engine's audio fader curves, in their order. */
UENUM(BlueprintType)
enum class EWidgetAnimSoundFadeCurve : uint8
{
	Linear UMETA(ToolTip = "Evenly, from start to finish."),
	Logarithmic UMETA(ToolTip = "As loudness is heard: evenly to the ear."),
	SCurve UMETA(DisplayName = "S-Curve", ToolTip = "Gently at first and last, quickest in the middle."),
	Sine UMETA(ToolTip = "Along a quarter sine: quickly at first, easing into its end.")
};

/**
 * Sound: plays a sound, in 2D, as a UI sound does - a whoosh as a panel slides in, a thud as a card lands. Staggered over
 * a title's letters, a blip per letter. Heard in the editor's previews too (Editor Preferences > Plugins > Widget
 * Animations Editor > Play Cue Sounds In Previews), where only one cue sound plays at a time.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Sound"))
struct NAMETBD_API FWidgetAnimSoundCueParams : public FWidgetAnimCueParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TObjectPtr<USoundBase> Sound = nullptr;

	/**
	 * The track is as long as the sound - at its Pitch - rather than its Duration, so Fire At End and a track After
	 * Previous line up with the sound ending. Off, or for a looping sound, which has no end, its Duration is used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (DisplayName = "Length From Sound"))
	bool bLengthFromSound = true;

	/** How loud, at the track's start and end. Each key plays at its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float Volume = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = "0.1", UIMin = "0.1", UIMax = "4.0"))
	float Pitch = 1.f;

	/**
	 * Shifts the pitch by up to this much either way each time it plays, so a sound fired again and again - once per
	 * letter - doesn't sound like one file stuttering. 0 for none.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (DisplayName = "Pitch Variation", ClampMin = "0.0", UIMax = "0.5"))
	float PitchVariation = 0.f;

	/**
	 * Seconds the sound takes to rise from silence as it starts, along its Fade Curve. 0 starts it at full volume. Eases the
	 * start of its Volume curve in the Widget Animations panel's Graph Editor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (DisplayName = "Fade In", ClampMin = "0.0", UIMax = "5.0", Units = "s"))
	float FadeIn = 0.f;

	/**
	 * Seconds the sound takes to fall to silence, finishing as the track's run does - as the sound ends, with Length From
	 * Sound. Eases the end of its Volume curve in the Graph Editor, along its Fade Curve. 0 lets it play out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (DisplayName = "Fade Out", ClampMin = "0.0", UIMax = "5.0", Units = "s"))
	float FadeOut = 0.f;

	/** How its volume moves while it fades in and out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (DisplayName = "Fade Curve", EditCondition = "FadeIn > 0 || FadeOut > 0"))
	EWidgetAnimSoundFadeCurve FadeCurve = EWidgetAnimSoundFadeCurve::Linear;
};

class NAMETBD_API FWidgetAnimSoundCueEvaluator : public TWidgetAnimCueEvaluator<FWidgetAnimSoundCueParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FText GetTrackSummary(const FInstancedStruct& Params) const override;
	virtual bool FiresAtDesignTime(const FInstancedStruct& Params) const override { return true; }
	virtual void Fire(const FWidgetAnimCueContext& Context) const override;

	// Length From Sound: the track's length is worked out as it starts, from the sound, and it is drawn striped.
	virtual bool NeedsPlayPreparation(const FInstancedStruct& Params) const override;
	virtual float PrepareForPlay(UObject* Target, FInstancedStruct& Params, float AuthoredRun) const override;
	virtual bool HasDerivedLength(const FInstancedStruct& Params) const override;
	virtual FText GetDerivedLengthLabel(const FInstancedStruct& Params) const override;

	/** Seconds the sound plays for at the track's Pitch; negative when it has no end (loops) or no sound is set. */
	static float GetSoundLength(const FWidgetAnimSoundCueParams& Params);

protected:

	virtual double GetPointValue(const FInstancedStruct& Params) const override;
};

// -----------------------------------------------------------------------------------------------------
// Play Animation
// -----------------------------------------------------------------------------------------------------

/** Which widget's animations a Play Animation cue plays. */
UENUM(BlueprintType)
enum class EWidgetAnimPlayCueOn : uint8
{
	PlayingWidget UMETA(DisplayName = "Widget Playing It", ToolTip = "The widget playing this animation: its Widget Animation Component, button or card - or, if it has none, the nearest one around it."),

	TrackWidget UMETA(DisplayName = "Track's Widget", ToolTip = "The widget this track is aimed at - its Layer Group's target, or each item of a stagger - and its own animations, or the nearest ones around it.")
};

/**
 * Play Animation - no longer offered in the Add Track menu or the Type Params picker: inserting an existing animation as a
 * track of its own does what it was for. Kept so tracks made with it still load and play.
 *
 * Plays other animations by Anim ID at a moment of this one - a glow once a card has landed, a shake as a
 * counter reaches its total, each child's own entrance as a list's stagger reaches it. Played the way Play Widget
 * Animation plays them.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Play Animation", Hidden))
struct NAMETBD_API FWidgetAnimPlayCueParams : public FWidgetAnimCueParams
{
	GENERATED_BODY()

	/** The Anim IDs to play. Empty plays every entry in the Play Direction below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play Animation", meta = (DisplayName = "Anim IDs"))
	TArray<FName> AnimIds;

	/** Whose animations: the widget playing this animation, or the widget this track is aimed at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play Animation", meta = (DisplayName = "Play On"))
	EWidgetAnimPlayCueOn PlayOn = EWidgetAnimPlayCueOn::PlayingWidget;

	/** Forward plays their Anim In entries, Reverse their Anim Out entries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play Animation", meta = (DisplayName = "Play Direction"))
	EWidgetAnimPlayDirection PlayDirection = EWidgetAnimPlayDirection::Forward;

	/** How fast they play, at the track's start and end. Each key plays them at its own speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play Animation", meta = (DisplayName = "Playback Speed", ClampMin = "0.01", UIMin = "0.1", UIMax = "4.0"))
	float PlaybackSpeed = 1.f;

	/** Off, they give their widgets back when they end, as a one-shot flourish should. On, their end pose stays. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play Animation", meta = (DisplayName = "Hold Final Pose"))
	bool bHoldFinalPose = false;

	/** Starts them from the beginning if they are already playing. Off, one already playing carries on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play Animation", meta = (DisplayName = "Restart If Playing"))
	bool bRestartIfPlaying = true;
};

class NAMETBD_API FWidgetAnimPlayCueEvaluator : public TWidgetAnimCueEvaluator<FWidgetAnimPlayCueParams>
{
public:

	/** Not offered any more - see FWidgetAnimPlayCueParams. */
	virtual bool IsListedInAddMenu() const override { return false; }

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FText GetTrackSummary(const FInstancedStruct& Params) const override;
	virtual void Fire(const FWidgetAnimCueContext& Context) const override;

protected:

	virtual double GetPointValue(const FInstancedStruct& Params) const override;
};
