#pragma once

#include "CoreMinimal.h"
#include "NameTBD/Cards/InfoCardTypes.h"

#include "Inventory/Structs/Instance.h"


#include "InventoryInfoCardPayloads.generated.h"

UCLASS(DisplayName = "Info Card Data (Item Instance)")
class TPS_TEST_API UInfoCardPayload_ItemInstance : public UInfoCardPayload
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FItemInstance ItemInstance;
};

UCLASS(DisplayName = "Info Card Data (Item Definition)")
class TPS_TEST_API UInfoCardPayload_ItemDefinition : public UInfoCardPayload
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FItemInstance ItemInstance;
};
