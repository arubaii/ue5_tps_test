#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Inventory/Structs/Instance.h"
#include "Inventory/Structs/Definition.h"
#include "Inventory/Structs/InventoryEnums.h"
#include "Layout/Margin.h"
#include "W_InventorySlot_Base.generated.h"

class UInventoryComponent;
class UW_InventoryBase;
class UW_InventoryDisplayOnlyBase;
class USizeBox;
class UBorder;
class UInfoCardAnchor;
class USlotInfoBarWidget;
class UTexture2D;

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

	/**
	 * The footprint this widget DRAWS at — what its SizeBox and grid span are built from.
	 *
	 * Normally the item's own footprint, but the slot wins when the slot is wider than
	 * the item: a 1x1 pistol in a 3x1 holster draws 3x1, and an EMPTY 3x1 holster still
	 * draws 3x1 rather than collapsing to a single cell the way an empty slot used to.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	FIntPoint SlotFootprint = FIntPoint(1, 1);

	/**
	 * The footprint the SLOT itself was authored with, via FSlotOverrideData::SlotFootprint.
	 * (1,1) for an ordinary slot and for every read-only/preview slot, which has no
	 * inventory behind it to ask.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	FIntPoint SlotOwnFootprint = FIntPoint(1, 1);

	/** The ITEM's own authored footprint, independent of the slot it happens to be in. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	FIntPoint ItemFootprint = FIntPoint(1, 1);

	/**
	 * True when this cell was swallowed by an oversized slot anchored elsewhere. Such a
	 * widget collapses itself — the same thing a shadow cell of a multi-cell ITEM does,
	 * just triggered by the slot rather than by its contents.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	bool bIsSlotShadowCell = false;

	/**
	 * What KIND of slot this is. At runtime it's the inventory's SlotType, replaced by
	 * FSlotOverrideData::Type where an override covers this index; at design time it's
	 * whatever the owning widget's preview grid authored.
	 *
	 * This is the key the developer settings map to a background image, so the WBP can
	 * pick its empty-slot artwork off it in OnRefreshVisuals without caring whether the
	 * slot came from a live component or from the designer preview.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	ESlotType SlotType = ESlotType::Generic;

	/**
	 * A per-slot background authored on the override (FSlotOverrideData::BackgroundImage),
	 * which beats the SlotType -> image mapping when it's set. Null means "no override —
	 * fall back to the developer settings entry for SlotType".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	TSoftObjectPtr<UTexture2D> SlotBackgroundImage;
	

	/**
	 * True only for cells built by W_InventoryBase's designer preview. There is no
	 * inventory component behind these and there never will be — they exist so the grid
	 * isn't an empty rectangle in the UMG designer.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory|Slot")
	bool bIsPreviewSlot = false;

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

	/**
	 * Design-time entry point: an always-empty cell that knows its own footprint and type
	 * but has no component behind it.
	 *
	 * Deliberately NOT routed through RefreshSlot — that function's first move is to ask
	 * the component what's at CurrentSlotIndex, and there is nothing to ask. Everything
	 * downstream of the lookup (footprint resolution, span, sizing, info bar, visuals) is
	 * the same code the runtime path uses, so a preview cell measures identically to the
	 * real thing.
	 *
	 * bInIsShadowCell is passed in rather than derived, because the "which cells does an
	 * oversized slot swallow" pass belongs to whoever owns the whole grid — a single cell
	 * can't see its neighbours.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Inventory|Slot")
	void InitSlotPreview(FIntPoint InSlotFootprint, ESlotType InSlotType,
	                     const TSoftObjectPtr<UTexture2D>& InBackgroundImage, bool bInIsShadowCell);

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

	/**
	 * The icon to draw for whatever is in this slot, picked out of the definition's
	 * IconByFootprint by the footprint the item is DRAWN at — which is the slot's when
	 * the slot is the wider of the two. That map is the whole reason slot footprints
	 * needed UI plumbing: a 1x1 pistol dropped in a 3x1 holster is supposed to swap to
	 * the artwork drawn for a 3x1 hole, not stretch its square icon across three cells.
	 *
	 * Falls back to the first authored entry when the exact footprint has no artwork, and
	 * returns null for an empty slot.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory|Slot|Internal")
	TSoftObjectPtr<UTexture2D> GetSlotIcon() const;

	/** Recomputes SlotFootprint from SlotOwnFootprint and ItemFootprint. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Slot|Internal")
	virtual void RefreshDisplayFootprint();
	
protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory|Slot")
	void OnRefreshVisuals(bool bIsEmpty);

	/** Pulls SlotType/SlotBackgroundImage off the component (and its override, if any). */
	virtual void RefreshSlotType();
	
private:
	bool bDelayedConstructRan = false;
};