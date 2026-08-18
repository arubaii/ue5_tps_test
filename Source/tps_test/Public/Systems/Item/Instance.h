// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Public/Systems/Item/Definition.h"          // FModifierPayload
#include "Public/Systems/Item/InventoryEnums.h"      // ESlotType
#include "Public/Systems/Item/ItemFragment.h"        // FItemFragment
#include "StructUtils/InstancedStruct.h"
#include "Instance.generated.h"


/**
 * A single runtime item instance — the mutable, per-copy state of an item.
 *
 * The item's STATIC definition is looked up separately via UItemLibrary::GetItemStruct(RowName).
 *
 * Domain-specific state lives in Fragments rather than as fields here, so a potion does
 * not carry an ammo count and a recoil profile. Adding armour or quest state touches no
 * inventory code: see FItemFragment.
 *
 * Fragments may reference OTHER FItemInstances by Guid (a weapon's fitted attachments are
 * peer items in the same store), so moving a weapon means moving those instances too —
 * the transitive closure the inventory walks via GatherReferencedItemIds.
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
	
	/** True while this instance is represented by a world actor rather than held in an inventory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance")
	bool IsWorldItem = false;

	/**
	 * Per-instance domain state, copied from the definition's DefaultFragments when the
	 * item is created and then mutated in place. Weapons carry a FWeaponItemFragment,
	 * anything that wears out carries a FDurabilityFragment, and so on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item Instance",
		meta = (BaseStruct = "/Script/tps_test.ItemFragment"))
	TArray<FInstancedStruct> Fragments;

	/** First fragment of type T, or null if this item has none. */
	template<typename T>
	const T* GetFragment() const
	{
		for (const FInstancedStruct& Fragment : Fragments)
		{
			if (const T* Typed = Fragment.GetPtr<T>())
			{
				return Typed;
			}
		}
		return nullptr;
	}

	/**
	 * Mutable access. Mutate a COPY of the instance and write it back through the
	 * inventory (SetItemInstance) so the change replicates — never edit a instance
	 * fetched by value and expect it to stick.
	 */
	template<typename T>
	T* GetMutableFragment()
	{
		for (FInstancedStruct& Fragment : Fragments)
		{
			if (T* Typed = Fragment.GetMutablePtr<T>())
			{
				return Typed;
			}
		}
		return nullptr;
	}

	/** True if this item carries a fragment of type T (i.e. "is this a weapon?"). */
	template<typename T>
	bool HasFragment() const
	{
		return GetFragment<T>() != nullptr;
	}
	

	/** Guids of every other instance referenced by this item's fragments. */
	void GatherReferencedItemIds(TArray<FGuid>& OutIds) const
	{
		for (const FInstancedStruct& Fragment : Fragments)
		{
			if (const FItemFragment* Typed = Fragment.GetPtr<FItemFragment>())
			{
				Typed->GatherReferencedItemIds(OutIds);
			}
		}
	}

	/** Rewrite every fragment's cross-instance links through the given mapping. */
	void RemapReferencedItemIds(const TMap<FGuid, FGuid>& OldToNew)
	{
		for (FInstancedStruct& Fragment : Fragments)
		{
			if (FItemFragment* Typed = Fragment.GetMutablePtr<FItemFragment>())
			{
				Typed->RemapReferencedItemIds(OldToNew);
			}
		}
	}

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