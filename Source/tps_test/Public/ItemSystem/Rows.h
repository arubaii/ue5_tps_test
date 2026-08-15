// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"                       // FTableRowBase
#include "Public/ItemSystem/Definition.h"			//  FWeaponStats, FWeaponRecoil
#include "Public/ItemSystem/InventoryEnums.h"       // ESlotType, EModifierValueType
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier Row")
	FName DisplayName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier Row")
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier Row")
	EModifierValueType ValueType = EModifierValueType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier Row")
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Row")
	FItemStructure ItemDetails;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Row")
	ESlotType AttachmentSlot = ESlotType::Attachment_Grip;
};

/**
 * Row for the weapon table.
 * Replaces the Blueprint struct S_WeaponRow. Field parity preserved.
 * Embeds the shared FItemStructure as ItemDetails (matches UItemLibrary's extraction).
 */
USTRUCT(BlueprintType)
struct FWeaponRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Row")
	FItemStructure ItemDetails;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Row")
	TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;

	/** Attachment slot -> Guid (as authored in the row). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Row")
	TMap<ESlotType, FGuid> AttachmentSlots;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Row")
	FWeaponStats WeaponStats;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Row")
	FWeaponRecoil WeaponRecoil;
};