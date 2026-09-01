// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"                         // FTableRowBase
#include "Inventory/Structs/Definition.h"			  //  FWeaponStats, FWeaponRecoil
#include "Inventory/Structs/InventoryEnums.h"       // ESlotType, EModifierValueType
#include "Rows.generated.h"

class USkeletalMesh;



/**
 * Row for the modifier table.
 * Replaces the Blueprint struct S_ModifierRow. Field parity preserved.
 */
USTRUCT(BlueprintType)
struct FModifierRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName DisplayName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EModifierValueType ValueType = EModifierValueType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Value = 0.f;
};

/**
 * Row for the attachment table.
 * Replaces the Blueprint struct S_AttachmentRow. Field parity preserved.
 * Embeds the shared FItemStructure as ItemDetails (matches UItemLibrary's extraction).
 */
USTRUCT(BlueprintType)
struct FAttachmentRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FItemDefinition ItemDetails;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ESlotType AttachmentSlot = ESlotType::Attachment_Grip;
};

/**
 * Row for the weapon table: the IMMUTABLE half of a weapon.
 *
 mutable half - ammo, stats, recoil, fitted attachments - lives in a FWeaponItemFragment
 * authored in ItemDetails.DefaultFragments and copied onto each instance.
 */
USTRUCT(BlueprintType)
struct FWeaponRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FItemDefinition ItemDetails;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

    /** Presentation & spawn design (projectile class, muzzle flash, future FX). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FWeaponDesign Design;
    
    /** Which ammo item this weapon consumes by row name into the ammo table (e.g. "AMU_Light"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName AmmoType = NAME_None;

    /** Which attachment slots this weapon offers. Immutable per weapon type; what is
        FITTED in each slot is per-instance and lives in FWeaponItemFragment::Attachments. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TMap<ESlotType, FGuid> AttachmentSlots;
};

USTRUCT(BlueprintType)
struct FAmmoRow : public FTableRowBase
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FProjectileImpact ProjectileImpact;
};



