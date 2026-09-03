// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "StructUtils/InstancedStruct.h"

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

