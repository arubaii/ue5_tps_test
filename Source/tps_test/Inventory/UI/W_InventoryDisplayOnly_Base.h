// W_InventoryDisplayOnlyBase.h
#pragma once

#include "CoreMinimal.h"
#include "Components/GridPanel.h"
#include "UI/WidgetAnimation/UserWidget_Animatable.h"
#include "W_InventorySlot_Base.h"
#include "W_InventoryDisplayOnly_Base.generated.h"

class UInventoryComponent;
struct FItemInstance;

/**
 * Lean, read-only slot display. No InventoryComponent reference is held between
 * calls, no change-delegate binding, no search/subview. Feed it a component (or
 * call RefreshFromComponent again later) and it renders pooled, read-only slot
 * widgets — icon/quantity only, no drag, no click-to-move. Intended for info
 * cards, tooltips, recipe/attachment previews, anywhere a full UW_InventoryBase
 * would be overkill.
 *
 * Slot widgets are handed fully RESOLVED FItemInstance data (via
 * UW_InventorySlotBase::InitSlotReadOnly), not raw FInventorySlot/Guid — the
 * Guid -> FItemInstance lookup only ever happens here, briefly, during a
 * refresh, using the component passed in. Nothing about the component is
 * retained afterward.
 */
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
	FVector2D BaseSlotSize = FVector2D(64.0f, 64.0f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Config")
	bool ShowSlotInfoBar = false; 

	/**
	 * Resolves Component's current slots into item instances and renders them.
	 * Call this any time you want the display to reflect the component's
	 * current state (initial show, or after something changes) — nothing is
	 * retained afterward beyond the pooled slot widgets themselves.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory Display")
	void RefreshFromComponent(UInventoryComponent* Component);

	/** Hides every pooled slot widget without destroying them. */
	UFUNCTION(BlueprintCallable, Category = "Inventory Display")
	void ClearDisplay();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Display|Config")
	int32 GridWidth = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Display|Config")
	int32 GridHeight = 4;

	/**
	 * Renders a custom array of slots, managing its own grid configuration.
	 * Uses the provided Component to resolve the slots' ItemIDs into FItemInstances.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory Display")
	void RefreshFromItems(UInventoryComponent* InventoryComponentRef, const TArray<FInventorySlot>& InventorySlots);
	
protected:

	UPROPERTY(BlueprintReadWrite, Category = "Inventory Display")
	TArray<TObjectPtr<UW_InventorySlotBase>> InventorySlotWidgets;

	UW_InventorySlotBase* GetOrCreateSlotWidget(int32 Index);
};