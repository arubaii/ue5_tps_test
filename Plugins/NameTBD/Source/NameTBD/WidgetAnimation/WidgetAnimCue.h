// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/WeakObjectPtr.h"
#include "StructUtils/InstancedStruct.h"
#include "WidgetAnimEvaluator.h"
#include "WidgetAnimParts.h" // FWidgetAnimPartKey

#include "WidgetAnimCue.generated.h"

class UWidget;
class USoundBase;
struct FWidgetAnimSettings;
struct FWidgetAnimValueKeys;

// =====================================================================================================
// Cues
// =====================================================================================================
//
// A cue is a track that makes something happen at moments on the timeline rather than moving anything: an event the
// game handles, a sound, another animation started. It is a registered type whose composition is Cue (see
// EWidgetAnimComposition), so it is authored, placed, keyed, staggered and aimed by its Layer Group exactly as any
// track is, and it loops, ping-pongs and inverts with the animation it is in. It writes nothing, so it has no pose, no
// baseline and nothing to put back.
//
// Its moments are its points: its start (Fire At Start, on by default), every key between its start and its end, and
// its end (Fire At End, off by default). A cue that should happen once is a track with nothing but its start; one that
// should happen three times is a track with keys. Each point carries a value - a key's own, or the type's for the start
// and end - which the type reads as it likes: an event passes it on, a sound plays at it as a volume.
//
// When a cue fires - the rules every cue type gets for free:
//
//   - As the playhead passes a point while the animation plays on its clock. A point exactly where the animation
//     starts fires on its very first frame.
//   - Once per pass of a loop. A lead-in only played on the first pass - a delay shared by every track - is passed once,
//     like the tracks in it.
//   - Direction is the playhead's: Forward (the default) fires only while playing forward, Backward only on the way
//     back of a Ping Pong loop, Both either way. A point at a turnaround - where the playhead reverses - fires once, not
//     twice, as long as either direction fires it.
//   - Played inverted - Invert Animation, or an exit, which plays its animation backward - a point is where its motion
//     is, so a cue at the start of an entrance fires at the end of the same animation played as an exit. Direction still
//     follows the playhead. Fire When Inverted off keeps it silent there.
//   - Never while scrubbing, snapping to a pose (Initialize, Reset To Start, a hard End Looping Animation), or stopping.
//     Jumping is not passing.
//   - At most once per point per frame: a hitch that swallows several loops fires each point once, not once per loop.
//   - On a value-driven track (Timing = Value), as the value crosses a point: rising past it is forward, falling past it
//     backward. The value the track starts at crosses nothing. Movements under a hundredth of the range are waited out,
//     so a Spring settling on a point doesn't chatter; one that bounces back past a point fires it again.
//   - A hidden track (in the editor) never fires, and a staggered one fires once per item - each child, or each letter
//     of an Animated Text Block: a click per letter as a title types out.
//
// Firing is deferred to the next frame, never in the middle of an animation being applied: a cue's handler may play,
// stop or destroy anything, the animation it came from included. The editor's previews fire only what is meant to be
// heard there (FWidgetAnimEvaluator::FiresAtDesignTime) - a sound does, an event doesn't.
//
// Event cues reach Blueprint as events, one per animation and event name: a Custom Event named "On Card Event Track
// Landed Fired" for a Landed track in WA_Card (WidgetAnimCues::GetEventFunctionName) with an Info pin, added from the +
// beside the name in the Events of the Widget Animation Component's Details, where every Event track its animations have
// is listed - those of animations inserted into them as tracks too, named by the animation they are in - as a button
// lists On Clicked. The Event cue calls it on the nearest User Widget around the widget playing it that has one. A
// dispatcher of the right name is broadcast too, for a parent.
//
// A cue type is a USTRUCT deriving from FWidgetAnimCueParams and an evaluator deriving from TWidgetAnimCueEvaluator,
// registered as any type is. In Blueprint, subclass UWidgetAnimBlueprintCue.

/** Which way the playhead has to be going for a cue to fire. */
UENUM(BlueprintType)
enum class EWidgetAnimCueDirection : uint8
{
	Forward UMETA(ToolTip = "Only while the animation plays forward - not on the way back of a Ping Pong loop. On a value-driven track, as the value rises past a point."),

	Backward UMETA(ToolTip = "Only on the way back of a Ping Pong loop. On a value-driven track, as the value falls past a point."),

	Both UMETA(DisplayName = "Both Ways", ToolTip = "Whichever way the playhead passes. A point where it turns round fires once.")
};

/**
 * What every cue type's settings share: which of its points fire, and when. Derive a cue type's settings from this.
 * See the note at the top of WidgetAnimCue.h.
 */
USTRUCT(BlueprintType, meta = (Hidden))
struct NAMETBD_API FWidgetAnimCueParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** Fires as the track starts. Off, only its keys (and its end, if Fire At End) fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue", meta = (DisplayName = "Fire At Start"))
	bool bFireAtStart = true;

	/** Fires as the track ends, too - its length on the timeline then says when. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue", meta = (DisplayName = "Fire At End"))
	bool bFireAtEnd = false;

	/** Which way the playhead has to be going: forward, on the way back of a Ping Pong loop, or both. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue")
	EWidgetAnimCueDirection Direction = EWidgetAnimCueDirection::Forward;

	/**
	 * Fires when the animation plays inverted too - Invert Animation, or as an exit, which plays its animation backward
	 * (an Anim Out entry, or an Anim In entry mirrored into the exit). Off, it is silent there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cue", meta = (DisplayName = "Fire When Inverted"))
	bool bFireWhenInverted = true;

	/** Whether point Point - 0 the start, the last the end, the keys between - is one this track fires at. */
	bool FiresAtPoint(int32 Point, int32 NumPoints) const
	{
		if (Point <= 0)
		{
			return bFireAtStart;
		}
		return Point >= NumPoints - 1 ? bFireAtEnd : true;
	}

	bool AllowsForward() const { return Direction != EWidgetAnimCueDirection::Backward; }
	bool AllowsBackward() const { return Direction != EWidgetAnimCueDirection::Forward; }
};

/** Everything about one firing of a cue, handed to whatever handles it. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimCueInfo
{
	GENERATED_BODY()

	/** The widget the cue's track is aimed at: its Layer Group's target, a staggered item, or the widget playing it. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UWidget> Target = nullptr;

	/** The widget playing the animation - the one its Widget Animation Component, button or card animates. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UWidget> Host = nullptr;

	/** The Widget Animation the cue is in. None for a component's additive tracks. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UObject> Animation = nullptr;

	/** The Anim ID of the entry that played it, when it was played by the track player. None otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FName AnimId;

	/** Which point fired: 0 the track's start, then its keys in order, the last its end. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	int32 Point = 0;

	/** The value at that point - the key's own, or the cue's for its start and end. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float Value = 0.f;

	/** For a track staggered over the letters, words or lines of a text: which one. -1 otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	int32 PartIndex = INDEX_NONE;

	/** Passed on the way back: a Ping Pong loop returning, or a value falling. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bPlayingBackward = false;

	/**
	 * How long until the track's run is over from this point, in seconds as it plays and the way the playhead is going: the
	 * whole run from its start, nothing at its end. What a Sound cue's Fade Out is timed by. -1 on a value-driven track,
	 * whose value decides when it is over.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float SecondsToTrackEnd = -1.f;

	/** Played inverted: Invert Animation, or as an exit. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bInverted = false;

	/** Fired by an editor preview rather than a game. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bDesignTime = false;
};

UINTERFACE(BlueprintType, MinimalAPI, meta = (DisplayName = "Widget Animation Event Listener"))
class UWidgetAnimEventListener : public UInterface
{
	GENERATED_BODY()
};

/**
 * Receives the Event cues of the animations played inside it. Add it to a User Widget (Class Settings > Interfaces) and
 * implement On Widget Animation Event: every Event cue an animation fires - on a Widget Animation Component anywhere in
 * the widget, on a button or card in it - reaches the nearest User Widget around the widget playing it that has this
 * interface. The same event also goes to that component's On Animation Event.
 */
class NAMETBD_API IWidgetAnimEventListener
{
	GENERATED_BODY()

public:

	/**
	 * An Event cue has fired: its name, and where and how. Called a frame after it was passed, so anything may be done
	 * here. Implemented in Blueprint; C++ listens with WidgetAnimCues::OnEvent or the component's On Animation Event.
	 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Widget Animation")
	void OnWidgetAnimationEvent(FName EventName, const FWidgetAnimCueInfo& Info);
};

/**
 * The shape of an Event track's event - what its function takes - for code that wants to bind a function of that shape.
 */
DECLARE_DYNAMIC_DELEGATE_OneParam(FWidgetAnimationTrackEventSignature, const FWidgetAnimCueInfo&, Info);

/** What a cue type's Fire is handed. */
struct NAMETBD_API FWidgetAnimCueContext
{
	/** What the type's ResolveTarget found on Info.Target. Null when nothing; the type decides whether it cares. */
	UObject* Target = nullptr;

	/** The track's settings - the type's own struct. */
	const FInstancedStruct* Params = nullptr;

	FWidgetAnimCueInfo Info;

	/** Something to find a world through: the target, else the host. Null when both are gone. */
	UObject* GetWorldContext() const;
};

/**
 * One point passed this frame, in FWidgetAnimResult. Not reflected: like an authoritative write, it points at the
 * settings it came from until something keeps it past the frame - see WidgetAnimCues::Queue, which copies them.
 */
struct NAMETBD_API FWidgetAnimCueFire
{
	const FInstancedStruct* Params = nullptr;

	/** The widget the track was played on. Null: the fallback widget. */
	UWidget* TargetWidget = nullptr;

	/** The animation the track came from (FWidgetAnimSettings::ValueScope). */
	UObject* Animation = nullptr;

	FWidgetAnimPartKey Part;
	int32 Point = 0;
	double Value = 0.0;
	bool bBackward = false;
	bool bInverted = false;

	/** Seconds from this point to where the playhead leaves the track - see FWidgetAnimCueInfo::SecondsToTrackEnd. */
	float SecondsToEnd = -1.f;

	/** Which layer of its stack, and where along the frame's sweep - for firing in order, and once per point. */
	int32 LayerIndex = INDEX_NONE;
	float Order = 0.f;
};

/**
 * Cue settings with the identity filled in, as TWidgetAnimEvaluator does, and everything a cue doesn't use answered:
 * listed under Cues, aimed at any widget, no value written, nothing to capture or put back. A cue type overrides Fire,
 * GetDisplayName and, for a value its points carry, GetPointValue.
 */
class NAMETBD_API FWidgetAnimCueEvaluatorBase : public FWidgetAnimEvaluator
{
public:

	virtual FText GetCategory() const override;
	virtual EWidgetAnimComposition GetComposition() const override { return EWidgetAnimComposition::Cue; }

	/** Its moments are points in time; there is no pace to ease. */
	virtual bool PrefersLinearEasing() const override { return true; }

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override { return GetPointValue(Params); }
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override {}

protected:

	/** The value the start and end carry, which a new key starts from too. 0 by default. */
	virtual double GetPointValue(const FInstancedStruct& Params) const { return 0.0; }
};

template <typename TParams>
class TWidgetAnimCueEvaluator : public FWidgetAnimCueEvaluatorBase
{
public:

	virtual const UScriptStruct* GetParamsStruct() const override { return TParams::StaticStruct(); }

protected:

	static const TParams* GetParams(const FInstancedStruct& Params) { return Params.GetPtr<TParams>(); }
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FWidgetAnimEventNative, FName /*EventName*/, const FWidgetAnimCueInfo& /*Info*/);

namespace WidgetAnimCues
{
	/**
	 * Hands this frame's fires to the next frame, where each is fired - in order, a copy of its settings kept, so the
	 * animation they came from may be gone by then. Host is the widget the animation was played on (the fallback for a
	 * fire with no target); AnimId the entry's, when there is one. bPreview treats them as an editor preview's, whatever
	 * the widget: only cues that fire at design time do.
	 */
	NAMETBD_API void Queue(TConstArrayView<FWidgetAnimCueFire> Fires, UWidget* Host, FName AnimId, bool bPreview = false);

	/** Drops every fire still waiting for the next frame - for the module shutting down. */
	NAMETBD_API void DiscardPending();

	/**
	 * The Event Dispatcher an Event cue named EventName is broadcast on: On + its name, with anything that can't be in a
	 * Blueprint name taken out - "Landed" is OnLanded, "Card Landed" OnCardLanded, "OnReveal" stays OnReveal. Any User
	 * Widget around the widget playing the animation that declares a dispatcher of that name gets it broadcast; the Widget
	 * Animation Component's Details add it with one click (Animation Events). None for an event with no name.
	 */
	NAMETBD_API FName GetEventDispatcherName(FName EventName);

	/**
	 * What an Event track's event names its animation by: the Widget Animation's asset name without its WA_ prefix
	 * (WA_Card is Card), or Additive for a component's Additive Anim Layers, which are in no animation.
	 */
	NAMETBD_API FString GetEventAnimationName(const UObject* Animation);

	/**
	 * The name of an Event track's event - the Custom Event the component's Details add, and the function it compiles to:
	 * "On Card Event Track Landed Fired" for a Landed track in WA_Card. Animation is the Widget Animation the track is in -
	 * the one inserted as a track, for a track of an animation inserted into another - or null for additive tracks. The
	 * Event cue calls it on the nearest User Widget around the widget playing it that has one. None for an event with no
	 * name.
	 */
	NAMETBD_API FName GetEventFunctionName(const UObject* Animation, FName EventName);

	/** Every Event cue fired anywhere, for C++ that wants them all - analytics, an audio system. After the listeners. */
	NAMETBD_API FWidgetAnimEventNative& OnEvent();

	/**
	 * How a sound fades in and out: FadeIn seconds up from silence as it starts; FadeOut seconds down to silence, starting
	 * FadeOutAt seconds after it starts (none when negative). Curve is an EWidgetAnimSoundFadeCurve.
	 */
	struct FSoundFade
	{
		float FadeIn = 0.f;
		float FadeOut = 0.f;
		float FadeOutAt = -1.f;
		uint8 Curve = 0;

		bool HasFade() const { return FadeIn > 0.f || (FadeOut > 0.f && FadeOutAt >= 0.f); }
	};

	/**
	 * How an editor preview plays a sound, set by the editor module - the runtime can't reach the editor's audio. Null
	 * (the default, and in a game) plays nothing at design time.
	 */
	using FPreviewSoundPlayer = TFunction<void(USoundBase* /*Sound*/, float /*Volume*/, float /*Pitch*/, const FSoundFade& /*Fade*/)>;
	NAMETBD_API void SetPreviewSoundPlayer(FPreviewSoundPlayer Player);

	/** Plays Sound through the editor's preview player. False when there is none. */
	NAMETBD_API bool PlayPreviewSound(USoundBase* Sound, float Volume, float Pitch, const FSoundFade& Fade = FSoundFade());

	/**
	 * The points a cue track fires at, as (point, where along its run 0 - 1) - its start, its keys, its end, as its
	 * settings say. Empty for a track that isn't a cue.
	 */
	NAMETBD_API void GetFiringPoints(const FInstancedStruct& TypeParams, const FWidgetAnimValueKeys& Keys, TArray<TPair<int32, float>, TInlineAllocator<8>>& OutPoints);

	/** The value point Point of a cue track carries: a key's own, or the type's for its start and end. */
	NAMETBD_API double GetPointValue(const FInstancedStruct& TypeParams, const FWidgetAnimValueKeys& Keys, int32 Point);

	/** The cue settings every cue type's derive from, or null when TypeParams aren't a cue's. */
	inline const FWidgetAnimCueParams* GetCueParams(const FInstancedStruct& TypeParams) { return TypeParams.GetPtr<FWidgetAnimCueParams>(); }
}
