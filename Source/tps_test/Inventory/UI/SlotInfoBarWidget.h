#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/Structs/Instance.h"

#include "SlotInfoBarWidget.generated.h"

UCLASS()
class USlotInfoBarWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintImplementableEvent)
	void SetSlotDetails(FItemInstance SlotItemInstance);
	
	UFUNCTION(BlueprintImplementableEvent)
	void SetEmptySlotDetails();
};
