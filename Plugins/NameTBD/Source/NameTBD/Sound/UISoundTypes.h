// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "Sound/SoundBase.h"

#include "UISoundTypes.generated.h"

/**
 * One sound, plus how it should be played. Leaving Sound empty makes the slot silent, which is how a sound
 * set covers only the events you actually care about without needing a per-slot on/off toggle.
 *
 * Deliberately knows nothing about buttons, cards, or states. Whatever is playing it decides which slot goes
 * with which event; this only describes the sound itself.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FUISoundSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TObjectPtr<USoundBase> Sound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (EditCondition = "Sound != nullptr", ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (EditCondition = "Sound != nullptr", ClampMin = "0.1", UIMin = "0.1", UIMax = "4.0"))
	float PitchMultiplier = 1.0f;

	/**
	 * Randomly shifts the pitch by up to this much either way on each play, so a sound fired repeatedly (a
	 * hover swept along a row of buttons) doesn't sound like the same file stuttering. 0 for no variation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (EditCondition = "Sound != nullptr", ClampMin = "0.0", UIMin = "0.0", UIMax = "0.5"))
	float PitchVariation = 0.0f;

	/** Seconds to wait before playing. The wait is dropped if the same set fires again first, so a sound never lands after the thing it was announcing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (EditCondition = "Sound != nullptr", ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", ForceUnits = "s"))
	float Delay = 0.0f;

	bool IsValid() const { return Sound != nullptr; }
};

/**
 * The settings that belong to a whole sound set rather than to one slot in it. Kept separate so every UI
 * sound set - buttons, cards, whatever comes next - shares one definition of "how often may this fire" and
 * "should the state it starts in be announced", instead of each re-inventing them slightly differently.
 *
 * Inline this into a set with meta = (ShowOnlyInnerProperties) so it reads as part of the set in the Details
 * panel rather than as a nested group.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FUISoundSetSettings
{
	GENERATED_BODY()

	/** Label only, so an owner with several sets is readable in the array. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	FName SetName = TEXT("Sounds");

	/**
	 * Ignores any play from this set within this many seconds of the last one. Stops a cursor jittering on a
	 * boundary from machine-gunning the same sound. 0 to allow every play.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0", ForceUnits = "s"))
	float MinTimeBetweenPlays = 0.05f;

	/**
	 * When on, the very first event this set sees is recorded without being played. That's what stops a
	 * screen full of widgets all sounding at once the moment they appear, while still letting every change
	 * after that be heard. Turn it off when the first event is the interesting one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	bool bSilentOnFirstEvent = true;

	/**
	 * Whether re-entering the event this set is already on plays the sound again.
	 *
	 * On (the default): every time the owner asserts a state, it sounds - setting a button enabled while it
	 * is already enabled is still a thing the player did, and staying silent makes the UI feel like it
	 * missed the input. The repeat limit above is what stops this becoming a machine gun.
	 *
	 * Off: strictly discrete. The sound fires on the transition into an event and then not again until the
	 * event has changed and come back - or until the owner is hidden and shown again, which resets the
	 * latch outright (see FUISoundPlayer::ResetEventLatches). Use it for ambient state that gets re-asserted
	 * constantly and would otherwise chatter.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (DisplayName = "Reassert On Repeat"))
	bool bReassertOnRepeat = true;
};

/**
 * Runtime bookkeeping for one sound set. Not a USTRUCT: nothing here is worth serializing, and it holds a
 * timer handle. Keep one of these per set, in an array that mirrors the set array.
 */
struct NAMETBD_API FUISoundRuntime
{
	/** The event this set last saw, so an event only sounds on the transition into it rather than every frame it persists. */
	int32 LastEventKey = INDEX_NONE;

	bool bHasSeenEvent = false;

	/** World seconds of the last play REQUEST, not of the sound being heard - see FUISoundPlayer::Play. */
	double LastPlayTime = -1.0e9;

	FTimerHandle DelayHandle;
};

/**
 * The one place UI sounds are actually played. Everything that owns sound sets goes through here, so the
 * repeat limit, the pitch jitter and the delay behave identically wherever they're used.
 */
struct NAMETBD_API FUISoundPlayer
{
	/**
	 * Plays a slot through one set's runtime, applying the set's repeat limit and the slot's delay.
	 *
	 * The repeat limit is charged when the play is REQUESTED, not when it's heard: charging it on the far
	 * side of a delay would let a jittering input stack up a queue that all fires at once.
	 *
	 * A set has one delay timer, deliberately. A set that fires again during its own wait replaces the
	 * pending sound rather than doubling up, which is what stops a delayed hover sound landing after the
	 * unhover that should have cancelled it.
	 */
	static void Play(const UObject* WorldContext, FUISoundRuntime& Runtime, const FUISoundSetSettings& Settings, const FUISoundSlot& Slot);

	/**
	 * Plays Slot only if EventKey differs from the last one this runtime saw, then records it either way.
	 * bSuppressPlayback records the event without playing it, for a state that's being seeded rather than
	 * entered. Honors FUISoundSetSettings::bSilentOnFirstEvent.
	 */
	static void PlayOnEventChange(const UObject* WorldContext, FUISoundRuntime& Runtime, const FUISoundSetSettings& Settings, const FUISoundSlot& Slot, int32 EventKey, bool bSuppressPlayback);

	/**
	 * Forgets which event these runtimes last saw, so the next one counts as a transition again - and, with
	 * bSilentOnFirstEvent, is recorded silently the way the owner's very first event was.
	 *
	 * For an owner that goes away and comes back: a menu collapsing and reopening is a fresh start, and a
	 * discrete set that stayed latched on "Hovered" from last time would swallow the first hover of the new
	 * one. Sets with Reassert On Repeat on don't need this, but resetting them is harmless.
	 */
	static void ResetEventLatches(TArray<FUISoundRuntime>& Runtimes);

	/** Plays a sound outright, with no set, no repeat limit and no delay. For one-offs pushed in from Blueprint. */
	static void PlayOneShot(const UObject* WorldContext, USoundBase* Sound, float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f);

	/** Drops a pending delayed sound. Call before the owner goes away, or the timer fires into a dead object. */
	static void Cancel(const UObject* WorldContext, FUISoundRuntime& Runtime);

	static void CancelAll(const UObject* WorldContext, TArray<FUISoundRuntime>& Runtimes);

	/** Grows or shrinks Runtimes to match a set array, cancelling anything pending first so no handle is orphaned. */
	static void ResizeRuntimes(const UObject* WorldContext, TArray<FUISoundRuntime>& Runtimes, int32 SetCount);
};