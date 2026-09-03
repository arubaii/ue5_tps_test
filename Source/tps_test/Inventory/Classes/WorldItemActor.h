// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interactable/InteractableActor.h"

#include "Inventory/Structs/Instance.h"
#include "Inventory/WorldItemInterface.h"

#include "WorldItemActor.generated.h"

/**
 * A world item lying on the ground — the item system's bridge into the Interaction
 * plugin.
 *
 * ========================= HOW THE TWO SYSTEMS MEET =========================
 * The Interaction plugin knows nothing about items. It asks its questions through
 * IInteractableInterface, and this class answers them by reading FUsableFragment off
 * its own item instance.
 *
 * Those overrides ARE the integration. Adding another item-driven axis (auto-pickup vs.
 * interact-to-pickup, a per-rarity prompt colour, a durability gate) means adding a
 * field to the fragment and reading it in the matching override here. Nothing in the
 * plugin changes, and nothing in the inventory system learns that interaction exists.
 *
 * Everything inventory-shaped lives in this class precisely so the plugin's own
 * AInteractableActor can ship without it.
 * ===========================================================================
 */
UCLASS(Blueprintable, BlueprintType)
class TPS_TEST_API AWorldItemActor : public AInteractableActor, public IWorldItemInterface
{
	GENERATED_BODY()

public:
	AWorldItemActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	// ---------------------------------------------------------------- item payload

	/**
	 * The item this actor represents while it is on the ground.
	 *
	 * Replicated, because a client needs the row name and quantity to draw a sensible
	 * prompt before it ever picks anything up. Server-owned: only the inventory system
	 * writes it, through IWorldItemInterface.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ItemInstance, EditAnywhere, BlueprintReadWrite, Category = "Item")
	FItemInstance ItemInstance;

	/**
	 * Attachment instances travelling with the main item (a rifle's fitted scope). Empty
	 * for everything else.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Item")
	TArray<FItemInstance> AttachedInstances;

	/**
	 * Level-authored contents. Ignored once the actor has been given a real instance by
	 * a drop, which is what bRuntimeSpawned tracks.
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Item|Level Editor")
	FDataTableRowHandle ItemRow;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Item|Level Editor", meta = (ClampMin = "1"))
	int32 Quantity = 1;

	/** True once a runtime system has handed this actor its instance. Do not author. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Item")
	bool bRuntimeSpawned = false;

	// ---------------------------------------------------------------- IInteractableInterface

	/**
	 * Read the commit style off the item's FUsableFragment, falling back to the
	 * component's authored default when the item has no usable fragment.
	 */
	virtual EInteractionMethod GetInteractionMethod_Implementation() const override;
	virtual float GetHoldDuration_Implementation() const override;
	virtual UObject* GetInteractionWidgetPayload_Implementation() const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;

	/**
	 * AUTHORITY ONLY. Committed. Routes the pickup into the interactor's inventory.
	 *
	 * The old Blueprint resolved the inventory through GetPlayerState(0), which returns
	 * the FIRST player state in the game state's array — so on a listening server every
	 * client's pickup went into the host's bag. Here it resolves through the actual
	 * Interactor.
	 */
	virtual void OnInteract_Implementation(AActor* Interactor) override;

	// ---------------------------------------------------------------- IWorldItemInterface

	virtual FItemInstance GetWorldItemInstance_Implementation() override;
	virtual TArray<FItemInstance> GetWorldItemAttachedInstances_Implementation() override;
	virtual void SetWorldItemInstance_Implementation(const FItemInstance& NewInstance) override;
	virtual void SetWorldItemPayload_Implementation(const FItemInstance& NewInstance, const TArray<FItemInstance>& InAttachedInstances) override;
	virtual void OnWorldItemFullyPickedUp_Implementation() override;

	// ---------------------------------------------------------------- hooks

	/**
	 * Refresh visuals from the current instance — swap the mesh, retint, whatever. Called
	 * on both ends whenever ItemInstance changes.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Item")
	void OnItemInstanceChanged();
	virtual void OnItemInstanceChanged_Implementation() {}

protected:
	UFUNCTION()
	void OnRep_ItemInstance();

	/** Level-placed actors mint their own instance from ItemRow on the server. */
	void InitializeFromLevelData();
};