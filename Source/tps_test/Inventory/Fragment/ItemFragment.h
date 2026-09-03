// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "StructUtils/InstancedStruct.h"
#include "Interactable/Types/InteractionTypes.h"
#include "ItemFragment.generated.h"


class UItemAction;

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

USTRUCT(BlueprintType)
struct TPS_TEST_API FUsableFragment : public FItemFragment
{
	GENERATED_BODY()
 
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<TSubclassOf<UItemAction>> Actions;
 
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float UseCooldown = 1.f;
 
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	bool ConsumeOnUse = false;
 
	/**
	 * How the player commits to picking this up.
	 *
	 *   Press     - tap the key. Ammo, notes, anything cheap.
	 *   Hold      - hold for HoldDuration. Use for anything the player would regret
	 *               taking by accident: a dropped weapon next to the one they are
	 *               carrying, a quest item, a container they meant to walk past.
	 *   Automatic - no input at all; picked up the moment it is focused. Currency,
	 *               scrap, ammo in a shooter that expects you to hoover it up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	EInteractionMethod InteractionMethod = EInteractionMethod::Press;
 
	/**
	 * Seconds to hold. Zero falls back to the interactable component's authored default,
	 * which is what you want for "this item is a Hold item, but the actor knows the
	 * house style for how long a hold takes".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction",
		meta = (EditCondition = "InteractionMethod == EInteractionMethod::Hold", ClampMin = "0.0"))
	float HoldDuration = 0.f;
};

USTRUCT(BlueprintType)
struct FCraftingCost
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FDataTableRowHandle Item;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	int32 Quantity = 1;
};

USTRUCT(BlueprintType)
struct TPS_TEST_API FCraftableFragment : public FItemFragment
{
	GENERATED_BODY()
	
	/* Specify the item, and the quantity required to craft */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	TArray<FCraftingCost> Recipe;
};

