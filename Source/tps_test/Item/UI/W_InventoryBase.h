#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryGridPacker.h"
#include "W_InventoryBase.generated.h"

class UInventoryComponent;
class UCanvasPanel;
class UScrollBox;
class UBorder;
class UTexture2D;

UCLASS()
class TPS_TEST_API UW_InventoryBase : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	// ---------------------------------------------------------------------------------
	// Components
	// ---------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Inventory")
	TObjectPtr<UInventoryComponent> InventoryComponent;

	// ---------------------------------------------------------------------------------
	// Widgets — names must match the Designer hierarchy exactly for BindWidget to resolve
	// ---------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> SlotBorder;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UScrollBox> SlotScrollBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UCanvasPanel> SlotGrid;

	// ---------------------------------------------------------------------------------
	// Design (parity with your existing variables)
	// ---------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Design")
	FLinearColor BackgroundColor = FLinearColor::White;

	/** Base cell size in pixels — Default slot size. Drives all Canvas positioning/sizing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Design")
	FVector2D SlotSize = FVector2D(128.f, 128.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Design")
	TObjectPtr<UTexture2D> SlotBackgroundImage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Design")
	bool SlotHasInfoBar = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Design")
	bool HasScrollBar = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Design")
	int32 NumberOfColumns = 5;

	/** BP-only slot widget class (e.g. W_InventorySlot_C). Must implement IInventorySlotInterface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Design")
	TSubclassOf<UUserWidget> InventorySlotWidgetClass;

	// ---------------------------------------------------------------------------------
	// State
	// ---------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	bool IsInventoryFull = false;

	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	bool IsInventoryWidgetInitialized = false;

	UPROPERTY(BlueprintReadWrite, Category = "Inventory|State")
	TArray<TObjectPtr<UUserWidget>> InventorySlotWidgets;

	// ---------------------------------------------------------------------------------
	// Core grid logic
	// ---------------------------------------------------------------------------------

	/** Rebuilds every slot widget and repositions them on the Canvas from a fresh grid pack. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Grid")
	void RefreshGrid();

	UFUNCTION(BlueprintCallable, Category = "Inventory|Grid")
	void InitializeInventory(UInventoryComponent* InInventoryComponentRef);

protected:
	/** Index -> item instance -> definition -> SlotSizeId. Falls back to "Default" if the slot is empty or the lookup fails. */
	FName GetSlotSizeIdForIndex(int32 Index) const;
};