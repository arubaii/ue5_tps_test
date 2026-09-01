#include "AudioCueLibrary.h"

#include "AudioCueSet.h"
#include "Kismet/GameplayStatics.h"

void UAudioCueLibrary::PlayCue2D(const UObject* WorldContextObject, UAudioCueSet* CueSet, FGameplayTag CueTag,
								 float PitchVariance, float VolumeVariance)
{
	if (!CueSet)
		return;

	if (USoundBase* sound = CueSet->Resolve(CueTag))
	{
		const float pitch = 1.f + FMath::FRandRange(-PitchVariance, PitchVariance);

		// Clamped above zero — a negative or zero volume multiplier isn't "quieter",
		// it's either silent or (worse) an inverted waveform, neither of which is what
		// "a bit of variance" is asking for.
		const float volume = FMath::Max(0.01f, 1.f + FMath::FRandRange(-VolumeVariance, VolumeVariance));

		UGameplayStatics::PlaySound2D(WorldContextObject, sound, volume, pitch);
	}
}

void UAudioCueLibrary::PlayCueAtLocation(const UObject* WorldContextObject, UAudioCueSet* CueSet,
										 FGameplayTag CueTag, FVector Location,
										 float PitchVariance, float VolumeVariance)
{
	if (!CueSet)
		return;

	if (USoundBase* sound = CueSet->Resolve(CueTag))
	{
		const float pitch = 1.f + FMath::FRandRange(-PitchVariance, PitchVariance);
		const float volume = FMath::Max(0.01f, 1.f + FMath::FRandRange(-VolumeVariance, VolumeVariance));

		UGameplayStatics::PlaySoundAtLocation(WorldContextObject, sound, Location, volume, pitch);
	}
}