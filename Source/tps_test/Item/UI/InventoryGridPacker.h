#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InventoryGridPacker.generated.h"

USTRUCT(BlueprintType)
struct FGridPlacement
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 ItemIndex = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 Row = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 Column = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 ColumnSpan = 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 RowSpan = 1;
};

UCLASS()
class TPS_TEST_API UInventoryGridPacker : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * One-call packer: pass the slot-size identifier for each inventory item (in order),
	 * get back exactly where each one goes. Internally resolves each identifier's
	 * footprint via UInventorySlotSizeSettings, so no separate lookup loop is needed in BP.
	 *
	 * @param SlotSizeIds  One identifier per inventory item, same order as your inventory array.
	 * @param NumColumns   Fixed grid width.
	 * @return             One FGridPlacement per input item, same order/index as SlotSizeIds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory Grid")
	static TArray<FGridPlacement> PackInventoryGrid(const TArray<FName>& SlotSizeIds, int32 NumColumns);
};