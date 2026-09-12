#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Components/Border.h"
#include "Components/GridPanel.h"
#include "Styling/SlateBrush.h"
#include "NameTBD/WidgetAnimation/UserWidget_Animatable.h" 
#include "Inventory/Components/InventoryComponent.h"    
#include "NameTBD/Search/W_SearchBarBase.h" 
#include "NameTBD/Cards/InfoCardSpecDataAsset.h"
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

	// =================================================================================
	//  Designer preview
	//
	//  The component is handed over at runtime, so SlotGridPanel is an empty rectangle
	//  in the UMG designer and there is nothing to lay a background, padding or a slot
	//  multiplier out against. These properties describe the SHAPE of the inventory this
	//  WBP is meant to display, and PreConstruct fills PreviewGridPanel with empty slot
	//  widgets built from them.
	//
	//  It is a drawing, not a state: nothing here is read at runtime, no inventory is
	//  created from it, and the whole panel is torn down the moment the game starts. It
	//  is on the author to keep these numbers in step with the component they intend to
	//  point at this widget — nothing can check that for them from inside a WBP.
	// =================================================================================

	/** Master switch. Off leaves PreviewGridPanel collapsed and empty in the designer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Preview")
	bool bShowDesignerPreview = true;

	/** Cells wide, mirroring UInventoryComponent::GridWidth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Preview", meta = (ClampMin = "0", EditCondition = "bShowDesignerPreview"))
	int32 PreviewGridWidth = 6;

	/** Cells tall, mirroring UInventoryComponent::GridHeight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Preview", meta = (ClampMin = "0", EditCondition = "bShowDesignerPreview"))
	int32 PreviewGridHeight = 4;

	/** The type every preview cell gets, mirroring UInventoryComponent::SlotType. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Preview", meta = (EditCondition = "bShowDesignerPreview"))
	ESlotType PreviewSlotType = ESlotType::Generic;

	/**
	 * Per-index overrides, exactly the struct the component takes: Index addresses the
	 * flat row-major preview grid, and Type / BackgroundImage / SlotFootprint mean what
	 * they mean there. Overrides that run off the grid or collide with another are
	 * skipped with a warning and drawn as ordinary 1x1 cells, which is the same call
	 * InitializeInventory makes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Preview", meta = (EditCondition = "bShowDesignerPreview"))
	TArray<FSlotOverrideData> PreviewSlotOverrides;

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

	/**
	 * Where the design-time preview is drawn. Deliberately its own panel rather than
	 * SlotGridPanel: the runtime pool owns that one, and the two would fight over its
	 * children the first time a real refresh landed on top of a preview.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Widgets")
	TObjectPtr<UGridPanel> PreviewGridPanel;

	UPROPERTY(BlueprintReadWrite, Category = "Inventory|Widgets")
	TArray<UW_InventorySlotBase*> InventorySlotWidgets;

	/**
	 * The design-time preview pool, one widget per cell in the same order BuildPreviewGrid
	 * lays them into PreviewGridPanel (row-major, swallowed cells included). Exposed
	 * read-only so a Blueprint graph can iterate the actual preview widgets directly —
	 * each one already carries its own resolved SlotType / SlotBackgroundImage / footprint
	 * — instead of walking PreviewGridPanel's children and re-casting them by hand.
	 *
	 * Kept separate from InventorySlotWidgets on purpose — that array IS the runtime
	 * pool, and its index is a slot index (HandleInventoryChanged's O(1) path is built
	 * on that identity). Preview cells are never slots, so they never go in it.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Inventory|Widgets")
	TArray<TObjectPtr<UW_InventorySlotBase>> PreviewSlotWidgets;

	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void RefreshGrid();

	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void RefreshSubView();

	/** Rebuilds PreviewGridPanel from the Preview* properties. Design time only. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Preview")
	void BuildPreviewGrid();

	/** Destroys every preview cell and collapses the panel. Safe to call repeatedly. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Preview")
	void ClearPreviewGrid();

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