#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IInventorySlotInterface.generated.h"

class UInventoryComponent;
class UTexture2D;

UINTERFACE(BlueprintType)
class TPS_TEST_API UInventorySlotInterface : public UInterface
{
	GENERATED_BODY()
};

/** Implement this on your W_InventorySlot Blueprint so W_InventoryBase can init it without a C++ base class. */
class TPS_TEST_API IInventorySlotInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "Inventory")
	void InitSlot(UInventoryComponent* InventoryComponentRef, int32 Index, FVector2D SlotSize,
				  UTexture2D* SlotBackgroundImage, bool HasInfoBar);
};