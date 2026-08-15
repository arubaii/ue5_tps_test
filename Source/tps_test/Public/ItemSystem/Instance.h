// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Public/ItemSystem/Definition.h"   // FAmmoCount, FModifierPayload, FWeaponStats, FWeaponRecoil
#include "Public/ItemSystem/InventoryEnums.h"      // ESlotType
#include "Instance.generated.h"

/**
 * A single runtime item instance — the mutable, per-copy state of an item.
 * Replaces the Blueprint struct S_ItemInstance. Field parity preserved.
 *
 * The item's STATIC definition is looked up separately via UItemLibrary::GetItemStruct(RowName).
 * WeaponAttachments holds Guids that reference OTHER FItemInstances (attachments are peer
 * items living in the same inventory store), so moving a weapon means moving its attachment
 * instances too (the transitive closure).
 */
USTRUCT(BlueprintType)
struct FItemInstance
{
	GENERATED_BODY()

	/** Stable unique identity for this instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance")
	FGuid UUID;

	/** Row name into the item tables; selects the definition (prefix picks the table). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance")
	FName RowName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance")
	int32 Quantity = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance")
	float Durability = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance")
	FAmmoCount AmmoCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance")
	TArray<FModifierPayload> Modifiers;

	/** Cached/derived weapon stats for this instance (definition + modifiers). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance")
	FWeaponStats ModifiedWeaponStats;

	/** Cached/derived weapon recoil for this instance (static-per-weapon in practice). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance")
	FWeaponRecoil ModifiedWeaponRecoil;

	/** Attachment slot -> Guid of the attachment's own FItemInstance (peer item). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance")
	TMap<ESlotType, FGuid> WeaponAttachments;

	/** True while this instance is represented by a world actor rather than held in an inventory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance")
	bool IsWorldItem = false;

	/** Convenience: does this instance refer to a real item (valid UUID)? */
	bool IsValid() const { return UUID.IsValid(); }

	friend bool operator==(const FItemInstance& A, const FItemInstance& B)
	{
		return A.UUID == B.UUID;
	}
};

/**
 * A slot in an inventory: where an item sits and what kind of slot it is.
 * Replaces the Blueprint struct S_InventorySlot. Field parity preserved.
 *
 * ItemID references an FItemInstance by Guid (empty Guid = empty slot).
 * SlotType is the slot's OWN type; compatibility is "item's CompatibleSlotTypes contains SlotType".
 */
USTRUCT(BlueprintType)
struct FInventorySlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Slot")
	int32 SlotIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Slot")
	ESlotType SlotType = ESlotType::Generic;

	/** Guid of the FItemInstance occupying this slot; invalid/empty when the slot is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Slot")
	FGuid ItemID;

	/** True when no item occupies this slot. */
	bool IsEmpty() const { return !ItemID.IsValid(); }
};