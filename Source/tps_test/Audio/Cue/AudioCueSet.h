#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AudioCueSet.generated.h"

class USoundBase;

/**
 * A tag -> sound lookup, authored entirely in the Content Browser rather than in any
 * graph or code. Adding a sound for a new action ("drinking a potion") is: add a row,
 * pick the tag, drag in a sound — no node placement, no recompiling.
 *
 * One asset can back the whole game, or you can have several (e.g. a "metallic" set for
 * an armored inventory vs a "cloth" set for a rogue's) and swap which one an inventory
 * points at, since nothing here is inventory-specific.
 */
UCLASS(BlueprintType)
class TPS_TEST_API UAudioCueSet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Tag -> sound. Leave a tag out entirely to fall through to DefaultSound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TMap<FGameplayTag, TObjectPtr<USoundBase>> Cues;

	/**
	 * Played when a requested tag has no entry above. Optional — leave null if you'd
	 * rather have unmapped actions stay silent than play a generic click.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> DefaultSound;

	/** Resolve a tag to the sound that should play for it, or nullptr if truly silent. */
	USoundBase* Resolve(const FGameplayTag& CueTag) const
	{
		if (const TObjectPtr<USoundBase>* found = Cues.Find(CueTag))
			return *found;

		return DefaultSound;
	}
};