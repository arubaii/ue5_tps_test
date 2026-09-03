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

// EFragmentFilterMode and FFragmentFilterOption now live in InventoryComponent.h (pulled
// in above) so FSubInventoryQuery::FragmentFilter can use the same type this widget's
// FragmentFilterOptions dropdown does — a resolved dropdown entry hands straight into a
// query with no conversion step.

/**
 * Wraps the label text so the Details panel's map-value column reads "Value (Display
 * Name)" instead of "Value (Text)". Unreal derives that parenthetical from the value's
 * own type name, not from any per-property display metadata — a bare FText is hardcoded
 * to show as "Text" with no way to relabel it, so it needs a named struct of its own
 * (the same reason the key column already reads "Fragment Filter Option").
 */
USTRUCT(BlueprintType)
struct FDisplayName
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View|Search")
	FText Text;
};

/**
 * C++ base for W_Inventory
 */
UCLASS()
class TPS_TEST_API UW_InventoryBase : public UUserWidget_Animatable
{
	GENERATED_BODY()
 
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	FVector2D BaseSlotSize = FVector2D(128.0f, 128.0f);
	
	/** Setting this will set the inventory slot to Not Hit-Testable (Self & All Children), removing all pointer events
	 *  from the slots themselves.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	bool bAreSlotsInteractable = true;
	
	/* Useful to creating hub inventories like shops or crafting stations */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config", meta = (EditCondition = "bAreSlotsInteractable"))
	bool bAllowSlotDragging = true;
	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config", meta = (EditCondition = "bAreSlotsInteractable"))
	TObjectPtr<UInfoCardDataAsset> SlotInfoCardsOverride; 
	
	/**
	 * A view containing only the entries whose display name fuzzy-matches SearchText.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Sub View|Search")
	static FSubInventoryView FilterSubInventoryBySearch(const FSubInventoryView& View, const FString& SearchText);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View|Search")
	bool bShowSearchBar = false;
	
	/* The first sorting pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View|Search", meta = (EditCondition = "bShowSearchBar"))
	ESubInventorySort FirstLayerSort  = ESubInventorySort::None;
	
	/** The second sorting pass, which sorts items of the first pass when multiple share the same level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View|Search", meta = (EditCondition = "bShowSearchBar"))
	ESubInventorySort SecondLayerSort  = ESubInventorySort::None;
	
	/**
	 * Filter by a group of fragment types (see FFragmentFilterOption for how the group
	 * is combined). Filter dropdown will be set to collapsed if this map is empty.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View|Search",
		meta = (EditCondition = "bShowSearchBar"))
	TMap<FFragmentFilterOption, FText> FragmentFilterOptions;
	
			
	/**
	 * Resolve a search bar's ActiveFilter (opaque FName) back to the fragment group it
	 * stands for. Returns an empty FFragmentFilterOption (no Fragments) for the "All"
	 * selection or an unrecognized key — both cases GetSubInventoryByFragment already
	 * treats as "everything", so callers can feed the result straight in with no branching.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Sub View|Search")
	FFragmentFilterOption ResolveFilterKey(FName Key) const;

	/**
	 * Pads the sub-view out with empty slots.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Sub View")
	bool bSubViewShowsEmptySlots = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Style")
	bool bShowScrollBar = false;

	
	/* On the inventory border */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Style")
	FLinearColor BackgroundColor = FLinearColor(0.0,0.0,0.0,0.0);
	
	/* On the inventory border */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Style")
	FVector4 CornerRadius = FVector4(0.f, 0.f, 0.f, 0.f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Slot")
	FMargin SlotPadding = FMargin{0.0, 0.0, 0.0, 0.0};

	// --- C++ Grid Generation & Refresh Logic ---
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

	/** The panel this widget tints and rounds. Optional: no-op if unbound. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Style")
	TObjectPtr<UBorder> SlotBorder;

	/**
	 * The search bar embedded in this widget's tree. Optional: a WBP_Inventory that
	 * doesn't include search/filter UI simply won't have this bound, and everything
	 * below degrades to a no-op rather than an error.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Sub View|Search")
	TObjectPtr<UW_SearchBarBase> InventorySearchBar;

	
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	bool bInSubView = false; 

	// The native event handler
	UFUNCTION(BlueprintCallable, Category = "Inventory|UI")
	void HandleInventoryChanged(int32 Index);
	
	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	FString SearchQuery;
	
private:
	/**
	 * Turns FragmentFilterOptions into InventorySearchBar's dropdown contents and the reverse
	 * lookup ResolveFilterKey reads. Called once from NativeConstruct; call again
	 * yourself if FragmentFilterOptions is ever changed after construction (e.g. a
	 * vendor's available categories update at runtime).
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Sub View|Search")
	void RefreshFilterOptionsFromMap();

	/**
	 * Each fragment group's opaque dropdown key, derived from its fragment names and
	 * filter mode since the group itself (an array + an enum) can't double as its own
	 * stable identifier the way a single fragment type's own name could.
	 */
	UPROPERTY()
	TMap<FName, FFragmentFilterOption> FilterKeyToFragment;
	
	bool bSubViewNeedsRefresh = false;

	UFUNCTION()
	void ExecuteDeferredSubViewRefresh();
};