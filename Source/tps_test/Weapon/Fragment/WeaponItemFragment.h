// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Item/Fragment/ItemFragment.h"
#include "Item/Structs/InventoryEnums.h"
#include "Item/Structs/Definition.h"
#include "WeaponItemFragment.generated.h"

/**
 * Per-instance weapon state. Only weapon items carry this fragment.
 *
 * Everything here can differ between two copies of the same weapon: one pistol has a full
 * mag and a suppressor fitted, another is empty and bare. Data that is identical for every
 * copy - the mesh, the projectile class, the muzzle flash, the ammo type it consumes -
 * stays on FWeaponRow.
 *
 * WHERE THE STARTING VALUES COME FROM
 * The weapon's DataTable row holds one of these in ItemDetails.DefaultFragments with the
 * designer-authored defaults. BuildItemInstance copies that array onto every new instance,
 * so a freshly created weapon already has its stats and recoil. There is no separate
 * stamping step to forget.
 *
 * The "Modified" prefix these fields used to carry on FItemInstance is gone: now that the
 * row holds no stats, there is nothing to disambiguate against.
 */
USTRUCT(BlueprintType)
struct TPS_TEST_API FWeaponItemFragment : public FItemFragment
{
	GENERATED_BODY()

	/** Rounds currently in the magazine. AmmoTotal mirrors the backpack ammo pool for UI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FAmmoCount AmmoCount;

	/** Live fire stats. Starts from the row's default fragment, then attachments and
	    modifiers mutate this copy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FWeaponStats Stats;

	/** Live recoil profile. Same lifecycle as Stats - attachments tune the copy, and the
	    weapon component pushes it into the recoil component via ApplyRecoilProfile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FWeaponRecoil Recoil;

	/** Attachment slot -> the Guid of the attachment instance fitted in it. The attachment
	    instances live in the same inventory store and travel with the weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<ESlotType, FGuid> Attachments;
};