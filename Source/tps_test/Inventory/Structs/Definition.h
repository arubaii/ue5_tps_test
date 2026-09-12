// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataTable.h"
#include "Curves/CurveFloat.h"                 // FRuntimeFloatCurve
#include "Templates/SubclassOf.h"

#include "Inventory/Structs/InventoryEnums.h"  

#include "StructUtils/InstancedStruct.h"

#include "Definition.generated.h"


class UTexture2D;
class UStaticMesh;
class USkeletalMesh;


/**
 * Shared item definition data embedded in every item row (Generic row IS this struct;
 * typed rows like FWeaponRow embed it as ItemDetails).
 * Replaces Blueprint struct S_ItemStructure.
 */
/**
 * Shared item definition data embedded in every item row (Generic row IS this struct;
 * typed rows like FWeaponRow embed it as ItemDetails).
 * Replaces Blueprint struct S_ItemStructure.
 */
USTRUCT(BlueprintType, meta = (UseCategoryGroups))
struct FItemDefinition : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName DisplayName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EItemType ItemType = EItemType::Consumable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText ItemText;
	
	/* 
	 * Slot footprint within grid.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Slot | Design")
	FIntPoint SlotSize = FIntPoint(1, 1);
	
	/* The first element key should match SlotSize. 
	 * 
	 * Set different item icon by the slot footprint. So if an item with a 1x1 footprint is dragged into a 3x1
	 * slot, the 3x1 icon will be used. If the footprint image is not specified, the first element will be used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FIntPoint, TSoftObjectPtr<UTexture2D>> IconByFootprint;
	

	// The item's base actor class ("Inherited from BP Item Master" in the BP struct).
	// Adjust the base type if BP_ItemBase has a native parent you'd prefer here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<AActor> Class;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Inventory Slot")
	TArray<ESlotType> CompatibleSlotTypes;
	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Slot | Design")
	bool HasInfoBar = true;
	
	/**
	 * Fragments copied onto every new instance of this item, with their starting values
	 * authored here. A weapon row holds a FWeaponItemFragment carrying its stats and
	 * recoil; anything that wears out holds a FDurabilityFragment; a plain potion holds
	 * none.
	 *
	 * This is what lets BuildItemInstance populate domain state without knowing what any
	 * domain means — see FItemFragment.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (BaseStruct = "/Script/tps_test.ItemFragment"))
	TArray<FInstancedStruct> DefaultFragments;

	/** The authored default fragment of type T from this definition, or null. Used for
	    reads that must work before the live instance has replicated in. */
	template<typename T>
	const T* GetDefaultFragment() const
	{
		for (const FInstancedStruct& Fragment : DefaultFragments)
		{
			if (const T* Typed = Fragment.GetPtr<T>())
			{
				return Typed;
			}
		}
		return nullptr;
	}
	
};

/**
 * Ammo carried by a weapon instance.
 * Replaces Blueprint struct S_AmmoCount. Field parity preserved.
 */
USTRUCT(BlueprintType)
struct FAmmoCount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 AmmoInMag = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 AmmoTotal = 0;
};

/**
 * A single stat/effect modifier payload.
 * Replaces Blueprint struct S_ModifierPayload.
 * NOTE: despite the name "ModifierRowName", the Blueprint type is a GameplayTag.
 */
USTRUCT(BlueprintType, meta = (UseCategoryGroups))
struct FModifierPayload
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag ModifierRowName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Value = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FLinearColor Color = FLinearColor::White;
};


