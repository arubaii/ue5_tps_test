// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataTable.h"
#include "Curves/CurveFloat.h"                 // FRuntimeFloatCurve
#include "Public/ItemSystem/InventoryEnums.h"  // ESlotType, EItemType, EFireMode, EModifierValueType
#include "Private/InteractableClass.h"
#include "Definition.generated.h"


// Forward-declare the CrystalRecoil data asset so we don't force every includer
// to pull the plugin header. The pointer is all we store.
class UCRRecoilPattern;
class UTexture2D;
class UStaticMesh;
class USkeletalMesh;

class AInteractable;

/**
 * Ammo carried by a weapon instance.
 * Replaces Blueprint struct S_AmmoCount. Field parity preserved.
 */
USTRUCT(BlueprintType)
struct FAmmoCount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo")
	int32 AmmoInMag = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo")
	int32 AmmoTotal = 0;
};

/**
 * A single stat/effect modifier payload.
 * Replaces Blueprint struct S_ModifierPayload.
 * NOTE: despite the name "ModifierRowName", the Blueprint type is a GameplayTag.
 */
USTRUCT(BlueprintType)
struct FModifierPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier")
	FGameplayTag ModifierRowName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier")
	float Value = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier")
	FLinearColor Color = FLinearColor::White;
};

/**
 * Static weapon stats (definition-side; read from the weapon row).
 * Replaces Blueprint struct S_WeaponStats.
 *
 * NOTE: You mentioned you'll add per-instance jitter tunables here yourself
 * (HorizontalJitter / VerticalJitter). Add them below when ready.
 */
USTRUCT(BlueprintType)
struct FWeaponStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Stats")
	float Damage = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Stats")
	int32 MagSize = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Stats", meta = (ToolTip = "In RPM."))
	float FireRate_RPM = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Stats")
	EFireMode FireMode = EFireMode::SemiAuto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Stats")
	int32 NumBurstBullets = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Stats")
	float TimeBetweenEachBurstBullet_Seconds = 0.f;

	// --- Add your per-instance jitter tunables here, e.g.:
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Stats")
	// float HorizontalJitter = 0.f;
	// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Stats")
	// float VerticalJitter = 0.f;
};

/**
 * Static weapon recoil definition. All fields are static-per-weapon (NOT runtime
 * per-instance state), so nothing here needs to replicate; it is read from the
 * weapon definition row.
 * Replaces Blueprint struct S_WeaponRecoil.
 */
USTRUCT(BlueprintType)
struct FWeaponRecoil
{
	GENERATED_BODY()

	// CrystalRecoil pattern data asset (UCRRecoilPattern). Object reference only —
	// the asset exists identically on all machines, so this is safe/cheap if ever copied.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	TObjectPtr<UCRRecoilPattern> RecoilPattern = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	bool bRecoveryEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	float RecoilMulti_ADS = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	float RecoilMulti_Crouched = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	float RecoilMulti_Moving = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	float RecoilMulti_InAir = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil",
		meta = (ToolTip = "Heat added per shot based on current heat; controls how quickly accuracy degrades: (Current heat, Heat added by shot)"))
	FRuntimeFloatCurve ShotToHeatCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil",
		meta = (ToolTip = "Spread angle corresponding to the current heat; controls the strength of heat with continued fire: (Current heat, Spread angle)"))
	FRuntimeFloatCurve HeatToSpreadAngleCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil",
		meta = (ToolTip = "Heat lost per second based on current heat. Applied each tick after RecoilHeatCooldownDelay since the last shot; controls accuracy recovery: (Current heat, heat lost per second)"))
	FRuntimeFloatCurve HeatToCooldownPerSecondCurve;
};

/**
 * Shared item definition data embedded in every item row (Generic row IS this struct;
 * typed rows like FWeaponRow embed it as ItemDetails).
 * Replaces Blueprint struct S_ItemStructure.
 */
USTRUCT(BlueprintType)
struct FItemStructure : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FName DisplayName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	EItemType ItemType = EItemType::Consumable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FText ItemText;

	// Soft reference to the icon texture (matches the Blueprint's Texture2D soft ref).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TSoftObjectPtr<UTexture2D> Icon;

	// The item's base actor class ("Inherited from BP Item Master" in the BP struct).
	// Adjust the base type if BP_ItemBase has a native parent you'd prefer here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TSubclassOf<AInteractable> Class;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 MaxStackSize = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	bool HasDurability = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	bool HasQuantity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TArray<ESlotType> CompatibleSlotTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	TArray<FModifierPayload> DefaultModifiers;
};