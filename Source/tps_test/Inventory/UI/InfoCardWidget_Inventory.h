#pragma once

#include "CoreMinimal.h"
#include "NameTBD/Cards/InfoCardWidget.h"
#include "NameTBD/Cards/InfoCardTypes.h" // Needed for UInfoCardPayload
#include "InfoCardWidget_Inventory.generated.h"

class UInventoryComponent;
class UW_InventoryDisplayOnlyBase;

/**
 * Payload data for inventory displays.
 * Moved from InfoCardTypes.h to keep the core UI module free of Inventory dependencies.
 */
UCLASS(DisplayName = "Info Card Data (Inventory Display)")
class TPS_TEST_API UInfoCardPayload_InventoryDisplay : public UNameTBD_Payload
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Display")
	TObjectPtr<UInventoryComponent> SourceComponent = nullptr;
};

/**
 * Inventory-specific Info Card subclass.
 * Re-parent any InfoCard WBP that needs to display an inventory grid to this class.
 */
UCLASS()
class TPS_TEST_API UInfoCardWidget_Inventory : public UInfoCardWidget
{
	GENERATED_BODY()

public:

	/**
	 * Optional. Any WBP built on this class can include a widget named
	 * "InventoryDisplay" of this class to get automatic component-preview
	 * behaviour for free.
	 */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "InfoCard")
	TObjectPtr<UW_InventoryDisplayOnlyBase> InventoryDisplay;

protected:

	UPROPERTY(Transient)
	TWeakObjectPtr<UInventoryComponent> BoundInventoryComponent;

	virtual void ApplyPayloads_Implementation(const TArray<UNameTBD_Payload*>& InPayloads) override;
	virtual void NativeDestruct() override;

	/**
	 * Binds InventoryDisplay to Component's OnInventoryChanged and refreshes it
	 * immediately. Unbinds any previously-bound component first.
	 */
	void BindInventoryDisplay(UInventoryComponent* Component);

	/** Unbinds from whatever's currently in BoundInventoryComponent, if anything. */
	void UnbindInventoryDisplay();

	UFUNCTION()
	void HandleBoundInventoryChanged(int32 InventoryIndex);
};