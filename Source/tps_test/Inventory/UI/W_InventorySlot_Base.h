#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/Structs/Instance.h"
#include "Inventory/Structs/Definition.h"
#include "Layout/Margin.h"
#include "W_InventorySlot_Base.generated.h"

class UInventoryComponent;
class UW_InventoryBase;
class UW_InventoryDisplayOnlyBase;
class USizeBox;
class UBorder;
class UInfoCardAnchor;
class USlotInfoBarWidget;

UCLASS(Abstract)
class TPS_TEST_API UW_InventorySlotBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	TObjectPtr<UInventoryComponent> InventoryComponentRef;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	TObjectPtr<UW_InventoryBase> InventoryWidgetRef;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	TObjectPtr<UW_InventoryDisplayOnlyBase> InventoryDisplayWidgetRef;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	int32 CurrentSlotIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	FInventorySlot CurrentSlot;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	FItemInstance CurrentSlotInstance;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	FItemDefinition CurrentSlotDefinition;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	FName CurrentSlotRowName;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	FIntPoint SlotFootprint = FIntPoint(1, 1);

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	FVector2D SlotSize = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	bool bHasInfoBar = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Slot")
	FLinearColor EmptySlotColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Slot")
	FVector2D FallbackSlotSizeMultiplier = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory|Slot")
	FName HoverCardId = FName("ItemHover");

	// --- Bound Widgets ---
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Widgets")
	TObjectPtr<USizeBox> SlotSizeBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Widgets")
	TObjectPtr<USizeBox> SlotVisualRoot;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Widgets")
	TObjectPtr<UBorder> SlotPadding;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Widgets")
	TObjectPtr<USlotInfoBarWidget> SlotInfoBar;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Widgets")
	TObjectPtr<UInfoCardAnchor> InfoCardAnchor;

	// --- Native Entry Points ---
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Inventory|Slot")
	void InitSlot(UInventoryComponent* InInventoryComponent, int32 Index);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Inventory|Slot")
	void InitSlotReadOnly(const FItemInstance& ItemInstance, bool bIsAnchor);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Inventory|Slot")
	void GetDisplayWidgetRef(UW_InventoryDisplayOnlyBase* DisplayWidget);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Inventory|Slot")
	void AddPadding(FMargin InPadding);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Inventory|Slot")
	void GetInventoryWidgetRef(UW_InventoryBase* InventoryWidget);

	UFUNCTION(BlueprintNativeEvent, Category = "Inventory|Slot|Internal", meta = (DisplayName = "Delayed Construct"))
	void DelayedConstruct();
	
	// --- Internal Native Logic ---
	UFUNCTION(BlueprintCallable, Category = "Inventory|Slot|Internal")
	virtual void RefreshSlot();

	UFUNCTION(BlueprintCallable, Category = "Inventory|Slot|Internal")
	virtual void RefreshSlotSize();

	UFUNCTION(BlueprintCallable, Category = "Inventory|Slot|Internal")
	virtual void RefreshSlotDimensions();

	UFUNCTION(BlueprintCallable, Category = "Inventory|Slot|Internal")
	virtual void SetupInfoCardPayload();

	UFUNCTION(BlueprintPure, Category = "Inventory|Slot|Internal")
	FVector2D GetBaseSlotSize() const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Slot|Internal")
	FVector2D GetSlotMultiplier() const;
	
protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory|Slot")
	void OnRefreshVisuals(bool bIsEmpty);
	
private:
	bool bDelayedConstructRan = false;
};