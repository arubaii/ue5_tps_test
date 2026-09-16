// Copyright AR Game Systems 2026. All rights reserved.

#include "UISoundTypes.h"

#include "Engine/World.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

namespace UISoundTypesLocal
{
	/**
	 * Nothing plays without a live game world. A UMG designer preview has a world of its own, so the check is
	 * for a real one rather than merely non-null: previewing a widget shouldn't make noise.
	 */
	UWorld* ResolvePlayableWorld(const UObject* WorldContext)
	{
		if (!WorldContext)
		{
			return nullptr;
		}

		UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		if (!World)
		{
			return nullptr;
		}

		return World->IsGameWorld() ? World : nullptr;
	}
}

void FUISoundPlayer::Play(const UObject* WorldContext, FUISoundRuntime& Runtime, const FUISoundSetSettings& Settings, const FUISoundSlot& Slot)
{
	if (!Slot.IsValid())
	{
		return;
	}

	UWorld* World = UISoundTypesLocal::ResolvePlayableWorld(WorldContext);
	if (!World)
	{
		return;
	}

	// Charged at request time, not at play time. See the comment on this function's declaration.
	const double Now = World->GetTimeSeconds();
	if (Settings.MinTimeBetweenPlays > 0.0f && (Now - Runtime.LastPlayTime) < Settings.MinTimeBetweenPlays)
	{
		return;
	}
	Runtime.LastPlayTime = Now;

	USoundBase* Sound = Slot.Sound;
	const float Volume = FMath::Max(0.0f, Slot.VolumeMultiplier);
	const float Jitter = Slot.PitchVariation > 0.0f ? FMath::FRandRange(-Slot.PitchVariation, Slot.PitchVariation) : 0.0f;
	const float Pitch = FMath::Max(0.01f, Slot.PitchMultiplier + Jitter);

	if (Slot.Delay <= 0.0f)
	{
		UGameplayStatics::PlaySound2D(World, Sound, Volume, Pitch);
		return;
	}

	// One timer per set: a second fire during the wait replaces the pending sound instead of stacking.
	FTimerHandle& Handle = Runtime.DelayHandle;
	World->GetTimerManager().ClearTimer(Handle);

	TWeakObjectPtr<UWorld> WeakWorld(World);
	World->GetTimerManager().SetTimer(
		Handle,
		FTimerDelegate::CreateLambda([WeakWorld, Sound, Volume, Pitch]()
		{
			if (UWorld* TimerWorld = WeakWorld.Get())
			{
				UGameplayStatics::PlaySound2D(TimerWorld, Sound, Volume, Pitch);
			}
		}),
		Slot.Delay,
		false);
}

void FUISoundPlayer::PlayOnEventChange(const UObject* WorldContext, FUISoundRuntime& Runtime, const FUISoundSetSettings& Settings, const FUISoundSlot& Slot, int32 EventKey, bool bSuppressPlayback)
{
	const bool bIsFirst = !Runtime.bHasSeenEvent;
	const bool bIsSameEvent = !bIsFirst && Runtime.LastEventKey == EventKey;

	// Discrete only when asked for. Re-asserting a state is still something that happened, and the caller
	// is usually announcing an input rather than describing a state it happens to be sitting in; the repeat
	// limit in Play is what keeps that from chattering.
	if (bIsSameEvent && !Settings.bReassertOnRepeat)
	{
		return;
	}

	Runtime.LastEventKey = EventKey;
	Runtime.bHasSeenEvent = true;

	if (bSuppressPlayback || (bIsFirst && Settings.bSilentOnFirstEvent))
	{
		return;
	}

	Play(WorldContext, Runtime, Settings, Slot);
}

void FUISoundPlayer::ResetEventLatches(TArray<FUISoundRuntime>& Runtimes)
{
	for (FUISoundRuntime& Runtime : Runtimes)
	{
		Runtime.LastEventKey = INDEX_NONE;
		Runtime.bHasSeenEvent = false;
	}

	// LastPlayTime is deliberately left alone: the repeat limit exists to stop sounds treading on each
	// other in real time, and that is just as true across a hide/show as within one.
}

void FUISoundPlayer::PlayOneShot(const UObject* WorldContext, USoundBase* Sound, float VolumeMultiplier, float PitchMultiplier)
{
	if (!Sound)
	{
		return;
	}

	if (UWorld* World = UISoundTypesLocal::ResolvePlayableWorld(WorldContext))
	{
		UGameplayStatics::PlaySound2D(World, Sound, FMath::Max(0.0f, VolumeMultiplier), FMath::Max(0.01f, PitchMultiplier));
	}
}

void FUISoundPlayer::Cancel(const UObject* WorldContext, FUISoundRuntime& Runtime)
{
	if (!Runtime.DelayHandle.IsValid())
	{
		return;
	}

	// Not ResolvePlayableWorld: cancelling has to work while the world is tearing down, which is exactly
	// when the "is this a game world" question starts answering unhelpfully.
	if (const UObject* Context = WorldContext)
	{
		if (UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr)
		{
			World->GetTimerManager().ClearTimer(Runtime.DelayHandle);
		}
	}

	Runtime.DelayHandle.Invalidate();
}

void FUISoundPlayer::CancelAll(const UObject* WorldContext, TArray<FUISoundRuntime>& Runtimes)
{
	for (FUISoundRuntime& Runtime : Runtimes)
	{
		Cancel(WorldContext, Runtime);
	}
}

void FUISoundPlayer::ResizeRuntimes(const UObject* WorldContext, TArray<FUISoundRuntime>& Runtimes, int32 SetCount)
{
	if (Runtimes.Num() == SetCount)
	{
		return;
	}

	// Shrinking would drop timer handles on the floor and leave them to fire into nothing, so everything
	// pending is canceled first and the sets simply start over.
	CancelAll(WorldContext, Runtimes);
	Runtimes.Reset();
	Runtimes.SetNum(FMath::Max(0, SetCount));
}