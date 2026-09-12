#pragma once

#include "CoreMinimal.h"
#include "Components/GridPanel.h"
#include "NameTBD/WidgetAnimation/UserWidget_Animatable.h"
#include "W_InventorySlot_Base.h"
#include "W_InventoryDisplayOnly_Base.generated.h"

class UInventoryComponent;
struct FItemInstance;

UCLASS()
class TPS_TEST_API UW_InventoryDisplayOnlyBase : public UUserWidget_Animatable
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly, Category = "Inventory Display")
	TSubclassOf<UW_InventorySlotBase> InventorySlotClass;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory Display")
	TObjectPtr<UGridPanel> SlotGridPanel;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	FVector2D SlotSizeMultiplier = FVector2D(0.25f, 0.25f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	bool ShowSlotInfoBar = false; 

	UFUNCTION(BlueprintCallable, Category = "Inventory Display")
	void RefreshFromComponent(UInventoryComponent* Component);

	UFUNCTION(BlueprintCallable, Category = "Inventory Display")
	void ClearDisplay();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Display|Config")
	int32 GridWidth = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Display|Config")
	int32 GridHeight = 4;

	UFUNCTION(BlueprintCallable, Category = "Inventory Display")
	void RefreshFromItems(UInventoryComponent* InventoryComponentRef, const TArray<FInventorySlot>& InventorySlots);
	
protected:

	UPROPERTY(BlueprintReadWrite, Category = "Inventory Display")
	TArray<TObjectPtr<UW_InventorySlotBase>> InventorySlotWidgets;

	UW_InventorySlotBase* GetOrCreateSlotWidget(int32 Index);
};