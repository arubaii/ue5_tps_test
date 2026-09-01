#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "AudioCueLibrary.generated.h"

class UAudioCueSet;

UCLASS()
class TPS_TEST_API UAudioCueLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Look up CueTag in CueSet and play it 2D (UI-style, no falloff). Silently does
	 * nothing if CueSet is null or the tag resolves to no sound — callers don't need to
	 * branch on "is there a sound configured for this."
	 *
	 * PitchVariance/VolumeVariance apply a random +/- offset each play so a cue heard
	 * repeatedly (swapping items rapidly, merging a full stack) doesn't sound like the
	 * exact same recording looping — the defaults are small enough to read as "alive"
	 * rather than as pitch-shifted or uneven. Set either to 0 for an exact, unvaried
	 * playback if a particular cue needs to sound identical every time.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject"))
	static void PlayCue2D(const UObject* WorldContextObject, UAudioCueSet* CueSet, FGameplayTag CueTag,
						  float PitchVariance = 0.05f, float VolumeVariance = 0.0f);

	/** Same as PlayCue2D but spatialized at a world location — for pickups, drops, etc. */
	UFUNCTION(BlueprintCallable, Category = "Audio", meta = (WorldContext = "WorldContextObject"))
	static void PlayCueAtLocation(const UObject* WorldContextObject, UAudioCueSet* CueSet,
								  FGameplayTag CueTag, FVector Location,
								  float PitchVariance = 0.05f, float VolumeVariance = 0.0f);
};