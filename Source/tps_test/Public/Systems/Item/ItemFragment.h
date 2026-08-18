// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "ItemFragment.generated.h"

/**
 * Base class for per-instance item state.
 *
 * THE SPLIT THIS ENFORCES
 *   Row (DataTable)  = IMMUTABLE data shared by every copy of an item: mesh, projectile
 *                      class, muzzle flash, ammo type, icon.
 *   Fragment         = MUTABLE state that can differ between two copies of the SAME item:
 *                      rounds in the mag, stats altered by attachments, durability.
 *
 * The test for any new field is: can two copies of this item ever disagree about it?
 * Yes -> fragment. No -> row.
 *
 * ADDING A FRAGMENT TYPE
 *   1. Derive a USTRUCT from FItemFragment, IN THE MODULE THAT OWNS THAT DOMAIN. Weapon
 *      state belongs in Systems/Weapon, armour state in Systems/Armor. Putting them all
 *      in one shared header would force Systems/Item to include every domain, which is
 *      precisely the coupling fragments exist to remove.
 *   2. Add it to the item's ItemDetails.DefaultFragments in its DataTable row and author
 *      the starting values. New instances are born with a copy.
 *   3. Read it with Instance.GetFragment<T>() / GetMutableFragment<T>().
 * No registration, no factory, no inventory changes.
 *
 * CROSS-INSTANCE LINKS
 * A fragment may reference OTHER item instances by Guid (a weapon's fitted attachments
 * are peer items in the same store). Override GatherReferencedItemIds and
 * RemapReferencedItemIds so the inventory can move and duplicate those links without
 * knowing what your fragment means.
 */
USTRUCT(BlueprintType)
struct TPS_TEST_API FItemFragment
{
	GENERATED_BODY()

	virtual ~FItemFragment() = default;

	/** Append the Guids of any other item instances this fragment references. */
	virtual void GatherReferencedItemIds(TArray<FGuid>& OutIds) const {}

	/** Rewrite referenced Guids through the given old -> new mapping (used when an item
	    is duplicated and its referenced instances are copied too). */
	virtual void RemapReferencedItemIds(const TMap<FGuid, FGuid>& OldToNew) {}
};

/**
 * Durability for items that wear out. Items without this fragment have no durability at
 * all, which is why the inventory treats a missing fragment as the -1 sentinel rather
 * than as zero.
 *
 * Generic enough to live here beside the base rather than in a domain module: weapons,
 * armour and tools all use the same notion.
 */
USTRUCT(BlueprintType)
struct TPS_TEST_API FDurabilityFragment : public FItemFragment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Durability = 100.f;
};

/* Item becomes stackable */
USTRUCT(BlueprintType)
struct TPS_TEST_API FStackableFragment : public FItemFragment
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int MaxStackSize = 64;
};


