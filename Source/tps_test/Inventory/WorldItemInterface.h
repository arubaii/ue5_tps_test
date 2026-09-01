// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Inventory/Structs/Instance.h"
#include "WorldItemInterface.generated.h"

/**
 * Contract between UInventoryComponent and world item actors (BP_ItemBase and,
 * later, a native item actor).
 *
 * WHY THIS EXISTS: in the old design, world items registered their instance in the
 * BP_GameInstance database and the component looked them up by Guid. In the new
 * per-component, replicated design the world actor OWNS its instance while the item
 * is on the ground, and pickup is a TRANSFER from the actor into the component's
 * store. C++ cannot read/write Blueprint variables directly, so this interface is
 * how the component pulls the payload off the actor and pushes results back.
 *
 * BP_ItemBase implements these as interface events/functions (a few nodes each):
 *  - GetWorldItemInstance        -> return the actor's ItemInstance variable
 *  - GetWorldItemAttachedInstances -> return attachment instances travelling with it
 *                                     (empty for non-weapons)
 *  - SetWorldItemInstance        -> set ItemInstance (+ IsRuntimeSpawn = true) and
 *                                   refresh visuals. Called on spawn (drop) and when
 *                                   a partial pickup leaves a remainder.
 *  - OnWorldItemFullyPickedUp    -> default implementation should DestroyActor.
 *
 * All Set/On calls are made by the SERVER only; the actor replicates the result down.
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UWorldItemInterface : public UInterface
{
	GENERATED_BODY()
};

class TPS_TEST_API IWorldItemInterface
{
	GENERATED_BODY()

public:
	/** The item instance this world actor represents. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "World Item")
	FItemInstance GetWorldItemInstance();

	/**
	 * Attachment instances that travel with the main instance (the Guid closure of
	 * WeaponAttachments). Empty for items without attachments.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "World Item")
	TArray<FItemInstance> GetWorldItemAttachedInstances();

	/** Server: replace the actor's instance (spawn payload, or remainder after partial pickup). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "World Item")
	void SetWorldItemInstance(const FItemInstance& NewInstance);

	/** Server: same as SetWorldItemInstance but also delivers the attachment closure. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "World Item")
	void SetWorldItemPayload(const FItemInstance& NewInstance, const TArray<FItemInstance>& AttachedInstances);

	/** Server: the whole stack was taken. Default BP implementation: DestroyActor. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "World Item")
	void OnWorldItemFullyPickedUp();
};