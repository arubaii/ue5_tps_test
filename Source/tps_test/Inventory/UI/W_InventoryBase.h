#pragma once

#include "CoreMinimal.h"
#include "UI/WidgetAnimation/UserWidget_Animatable.h" // adjust path if this lives elsewhere in your module
#include "Inventory/Components/InventoryComponent.h"    // ESubInventorySort, EInventorySortDirection
#include "UI/Search/W_SearchBarBase.h" // FSearchFilterOption, UW_SearchBarBase — adjust path if this lives elsewhere
#include "Components/Border.h"
#include "Styling/SlateBrush.h"
#include "W_InventoryBase.generated.h"

/**
 * C++ base for W_Inventory
 */
UCLASS()
class TPS_TEST_API UW_InventoryBase : public UUserWidget_Animatable
{
	GENERATED_BODY()
 
public:

	/**
	 * A view containing only the entries whose display name fuzzy-matches SearchText,
	 * re-compacted so ViewIndex stays gap-free.
	 *
	 * Takes a view rather than repeating GetSubInventoryByFragment's parameters, so it
	 * composes: build the view however you like, then narrow it. An empty SearchText
	 * returns the input unchanged, so callers don't branch on "has the player typed yet".
	 *
	 * The re-index is the part that matters. Entries carry a ViewIndex that the layout
	 * and the slot widgets both address by position; dropping entries without reassigning
	 * it would leave the packer allocating cells for items that are no longer drawn,
	 * which shows up as holes in the grid rather than as an error.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Search")
	static FSubInventoryView FilterSubInventoryBySearch(const FSubInventoryView& View, const FString& SearchText);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Search")
	bool bShowSearchBar = false;
	
	/* The first sorting pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Search", meta = (EditCondition = "bShowSearchBar"))
	ESubInventorySort FirstLayerSort  = ESubInventorySort::None;
	
	/** The second sorting pass, which sorts items of the first pass when multiple share the same level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Search", meta = (EditCondition = "bShowSearchBar"))
	ESubInventorySort SecondLayerSort  = ESubInventorySort::None;
	
	/**
	 * Filter by fragment type.
	 * Filter dropdown will be set to collapsed if this map is empty. 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Search",
		meta = (EditCondition = "bShowSearchBar", MetaStruct = "/Script/tps_test.ItemFragment"))
	TMap<TObjectPtr<UScriptStruct>, FText> FragmentFilterOptions;
	
			
	/**
	 * Resolve a search bar's ActiveFilter (opaque FName) back to the fragment type it
	 * stands for. Returns nullptr for the "All" selection or an unrecognized key — both
	 * cases GetSubInventoryByFragment already treats as "everything", so callers can
	 * feed this straight into FSubInventoryQuery::FragmentType with no branching.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Search")
	UScriptStruct* ResolveFilterKey(FName Key) const;

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
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Inventory|Search")
	TObjectPtr<UW_SearchBarBase> InventorySearchBar;

private:
	/**
	 * Turns FragmentFilterOptions into InventorySearchBar's dropdown contents and the reverse
	 * lookup ResolveFilterKey reads. Called once from NativeConstruct; call again
	 * yourself if FragmentFilterOptions is ever changed after construction (e.g. a
	 * vendor's available categories update at runtime).
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Search")
	void RefreshFilterOptionsFromMap();

	/** Fragment type's own name IS the key — no separate identifier to keep in sync. */
	UPROPERTY()
	TMap<FName, TObjectPtr<UScriptStruct>> FilterKeyToFragment;
};