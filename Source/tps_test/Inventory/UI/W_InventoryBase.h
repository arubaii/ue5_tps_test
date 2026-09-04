#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Components/Border.h"
#include "Components/GridPanel.h"
#include "Styling/SlateBrush.h"
#include "UI/WidgetAnimation/UserWidget_Animatable.h" 
#include "Inventory/Components/InventoryComponent.h"    
#include "UI/Search/W_SearchBarBase.h" 
#include "UI/Cards/InfoCardSpecDataAsset.h"
#include "W_InventorySlot_Base.h"
#include "W_InventoryBase.generated.h"

USTRUCT(BlueprintType)
struct FDisplayName
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View|Search")
	FText Text;
};

UCLASS()
class TPS_TEST_API UW_InventoryBase : public UUserWidget_Animatable
{
	GENERATED_BODY()
 
public:
	/** Scales InventorySettings->BaseSlotSize for this widget's slots. 1.0 = project default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	FVector2D SlotSizeMultiplier = FVector2D(1.0f, 1.0f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	bool bAreSlotsInteractable = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config", meta = (EditCondition = "bAreSlotsInteractable"))
	bool bAllowSlotDragging = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config", meta = (EditCondition = "bAreSlotsInteractable"))
	TObjectPtr<UInfoCardDataAsset> SlotInfoCardsOverride; 
	
	UFUNCTION(BlueprintPure, Category = "Inventory|Sub View|Search")
	static FSubInventoryView FilterSubInventoryBySearch(const FSubInventoryView& View, const FString& SearchText);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View|Search")
	bool bShowSearchBar = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View|Search", meta = (EditCondition = "bShowSearchBar"))
	ESubInventorySort FirstLayerSort  = ESubInventorySort::None;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View|Search", meta = (EditCondition = "bShowSearchBar"))
	ESubInventorySort SecondLayerSort  = ESubInventorySort::None;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View|Search", meta = (EditCondition = "bShowSearchBar"))
	TMap<FFragmentFilterOption, FText> FragmentFilterOptions;
	
	UFUNCTION(BlueprintPure, Category = "Inventory|Sub View|Search")
	FFragmentFilterOption ResolveFilterKey(FName Key) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View")
	bool bSubViewShowsEmptySlots = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Style")
	bool bShowScrollBar = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Style")
	FLinearColor BackgroundColor = FLinearColor(0.0,0.0,0.0,0.0);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Style")
	FVector4 CornerRadius = FVector4(0.f, 0.f, 0.f, 0.f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Slot")
	FMargin SlotPadding = FMargin{0.0, 0.0, 0.0, 0.0};

	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	TObjectPtr<UInventoryComponent> InventoryComponent;

	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	FSubInventoryQuery SubViewQuery;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory|Widgets")
	TSubclassOf<UW_InventorySlotBase> InventorySlotClass;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Widgets")
	TObjectPtr<UGridPanel> SlotGridPanel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Widgets")
	TObjectPtr<UGridPanel> SubViewPanel;

	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Widgets")
	TArray<UW_InventorySlotBase*> InventorySlotWidgets;

	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void RefreshGrid();

	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void RefreshSubView();

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Style")
	TObjectPtr<UBorder> SlotBorder;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Sub View|Search")
	TObjectPtr<UW_SearchBarBase> InventorySearchBar;
	
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	bool bInSubView = false; 

	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void HandleInventoryChanged(int32 Index);
	
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	FString SearchQuery;
	
private:
	UFUNCTION(BlueprintCallable, Category = "Inventory|Sub View|Search")
	void RefreshFilterOptionsFromMap();

	UPROPERTY()
	TMap<FName, FFragmentFilterOption> FilterKeyToFragment;
	
	bool bSubViewNeedsRefresh = false;

	UFUNCTION()
	void ExecuteDeferredSubViewRefresh();
};