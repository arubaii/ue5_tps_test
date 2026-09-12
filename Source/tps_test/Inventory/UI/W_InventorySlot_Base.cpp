#include "W_InventorySlot_Base.h"

#include "InventoryInfoCardPayloads.h"
#include "Inventory/Components/InventoryComponent.h"
#include "Inventory/ItemLibrary.h" 
#include "DeveloperSettings/InventorySettings.h"
#include "W_InventoryBase.h"
#include "SlotInfoBarWidget.h"
#include "W_InventoryDisplayOnly_Base.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/GridSlot.h"
#include "NameTBD/Cards/InfoCardAnchor.h"
#include "NameTBD/Cards/InfoCardTypes.h"
#include "Engine/Texture2D.h"

void UW_InventorySlotBase::InitSlot_Implementation(UInventoryComponent* InInventoryComponent, int32 Index)
{
	InventoryComponentRef = InInventoryComponent;
	CurrentSlotIndex = Index;

	if (InventoryComponentRef)
	{
		CurrentSlot = InventoryComponentRef->GetSlot(Index);
	}

	RefreshSlot();
}

void UW_InventorySlotBase::RefreshSlot()
{
	bool bHasItem = false;

	// A pooled widget can have been a preview cell a moment ago (PreConstruct built it,
	// then the component arrived): clear that before anything else so the live data is
	// never mixed with authored design-time data.
	bIsPreviewSlot = false;

	// Reset before the lookup: a pooled widget being re-pointed at a different index must
	// not keep the previous slot's footprint if the new index has no component behind it.
	SlotOwnFootprint = FIntPoint(1, 1);
	bIsSlotShadowCell = false;

	if (InventoryComponentRef)
	{
		bHasItem = InventoryComponentRef->GetItemByIndex(CurrentSlotIndex, CurrentSlotInstance);
		CurrentSlot = InventoryComponentRef->GetSlot(CurrentSlotIndex);

		SlotOwnFootprint  = InventoryComponentRef->GetSlotFootprintSize(CurrentSlotIndex);
		bIsSlotShadowCell = InventoryComponentRef->IsSlotShadowCell(CurrentSlotIndex);
	}

	RefreshSlotType();

	// Two independent reasons a cell draws nothing, and either one is enough: the cell is
	// under a multi-cell ITEM anchored elsewhere, or it was swallowed by an oversized
	// SLOT. The second one collapses whether or not anything is in it — the three cells
	// behind a 3x1 holster are never their own slots, empty or not.
	const bool bIsItemShadowCell = bHasItem && !CurrentSlot.bIsAnchor;
	const bool bIsShadowCell = bIsItemShadowCell || bIsSlotShadowCell;

	if (bHasItem && !bIsShadowCell)
	{
		CurrentSlotRowName = CurrentSlotInstance.RowName;
		UItemLibrary::GetItemDefinition(CurrentSlotRowName, CurrentSlotDefinition);
		ItemFootprint = CurrentSlotDefinition.SlotSize;
		bHasInfoBar = CurrentSlotDefinition.HasInfoBar;
	}
	else
	{
		CurrentSlotInstance = FItemInstance();
		CurrentSlotDefinition = FItemDefinition();
		CurrentSlotRowName = NAME_None;
		ItemFootprint = FIntPoint(1, 1);
		bHasInfoBar = false;
	}

	if (bIsSlotShadowCell)
		SlotOwnFootprint = FIntPoint(1, 1); // it draws nothing; never let it span cells

	RefreshDisplayFootprint();

	SetVisibility(bIsShadowCell ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);

	RefreshSlotSize();
	RefreshSlotDimensions();
	SetupInfoCardPayload();
	
	if (SlotInfoBar)
	{
		if (bHasItem && !bIsShadowCell)
		{
			SlotInfoBar->SetSlotDetails(CurrentSlotInstance);
		}
		else
		{
			SlotInfoBar->SetEmptySlotDetails();
		}
	}
	
	OnRefreshVisuals(!bHasItem || bIsShadowCell);
	
	if (!bDelayedConstructRan)
	{
		DelayedConstruct();
		bDelayedConstructRan = true;
	}
}

void UW_InventorySlotBase::RefreshSlotType()
{
	// Default state, so a pooled widget re-pointed at a component-less index doesn't keep
	// the previous slot's type or background.
	SlotType = ESlotType::Generic;
	SlotBackgroundImage = nullptr;

	if (!InventoryComponentRef)
		return;

	// The inventory-wide type first; an override at this exact index then replaces it,
	// which mirrors what InitializeInventory stamps onto the slots themselves.
	SlotType = InventoryComponentRef->SlotType;

	if (CurrentSlotIndex < 0)
		return;

	FSlotOverrideData OverrideData;
	bool bFound = false;
	InventoryComponentRef->GetSlotOverrideByIndex(CurrentSlotIndex, OverrideData, bFound);

	if (bFound)
	{
		SlotType = OverrideData.Type;
		SlotBackgroundImage = OverrideData.BackgroundImage;
	}
}

void UW_InventorySlotBase::InitSlotReadOnly_Implementation(const FItemInstance& ItemInstance, bool bIsAnchor)
{
	bIsPreviewSlot = false;

	CurrentSlotInstance = ItemInstance;
	CurrentSlotRowName = ItemInstance.RowName;
	
	bool bHasItem = CurrentSlotInstance.UUID.IsValid() || !CurrentSlotRowName.IsNone();
	const bool bIsShadowCell = bHasItem && !bIsAnchor;

	// A read-only preview has no inventory behind it, so there is no slot to override
	// anything: these draw at the item's own footprint in the preview's own grid.
	SlotOwnFootprint = FIntPoint(1, 1);
	bIsSlotShadowCell = false;

	// Same reasoning for the type: nothing to ask, so leave it Generic rather than
	// keeping whatever this widget was showing before it was pooled into this role.
	SlotType = ESlotType::Generic;
	SlotBackgroundImage = nullptr;

	if (bHasItem && !bIsShadowCell)
	{
		UItemLibrary::GetItemDefinition(CurrentSlotRowName, CurrentSlotDefinition);
		ItemFootprint = CurrentSlotDefinition.SlotSize;
		bHasInfoBar = CurrentSlotDefinition.HasInfoBar;
	}
	else
	{
		CurrentSlotDefinition = FItemDefinition();
		ItemFootprint = FIntPoint(1, 1);
		bHasInfoBar = false;
	}

	RefreshDisplayFootprint();

	SetVisibility(bIsShadowCell ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);

	RefreshSlotSize();
	RefreshSlotDimensions();
	SetupInfoCardPayload();
	
	if (SlotInfoBar)
	{
		if (bHasItem && !bIsShadowCell)
		{
			SlotInfoBar->SetSlotDetails(CurrentSlotInstance);
		}
		else
		{
			SlotInfoBar->SetEmptySlotDetails();
		}
	}
	
	OnRefreshVisuals(!bHasItem || bIsShadowCell);
}

void UW_InventorySlotBase::InitSlotPreview_Implementation(FIntPoint InSlotFootprint, ESlotType InSlotType,
						 const TSoftObjectPtr<UTexture2D>& InBackgroundImage, bool bInIsShadowCell)
{
	bIsPreviewSlot = true;

	// There is no component and no index behind a preview cell. Say so explicitly rather
	// than leaving a stale pointer: anything that later asks this widget what it holds
	// gets "nothing", which is the truth.
	InventoryComponentRef = nullptr;
	CurrentSlotIndex = INDEX_NONE;
	CurrentSlot = FInventorySlot();

	CurrentSlotInstance = FItemInstance();
	CurrentSlotDefinition = FItemDefinition();
	CurrentSlotRowName = NAME_None;
	ItemFootprint = FIntPoint(1, 1);
	bHasInfoBar = false;

	SlotType = InSlotType;
	SlotBackgroundImage = InBackgroundImage;

	bIsSlotShadowCell = bInIsShadowCell;

	// A swallowed cell draws nothing, so it must never span — same rule the runtime path
	// applies after asking the component.
	SlotOwnFootprint = bInIsShadowCell
		? FIntPoint(1, 1)
		: FIntPoint(FMath::Max(1, InSlotFootprint.X), FMath::Max(1, InSlotFootprint.Y));

	RefreshDisplayFootprint();

	SetVisibility(bIsSlotShadowCell ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);

	RefreshSlotSize();
	RefreshSlotDimensions();
	SetupInfoCardPayload();

	if (SlotInfoBar)
	{
		SlotInfoBar->SetEmptySlotDetails();
	}

	// Always the empty visual: a preview cell exists to show the SHAPE of the grid, and
	// faking contents in the designer would only lie about what the WBP looks like in a
	// fresh inventory.
	OnRefreshVisuals(true);
}

void UW_InventorySlotBase::DelayedConstruct_Implementation() {}

void UW_InventorySlotBase::GetDisplayWidgetRef_Implementation(UW_InventoryDisplayOnlyBase* DisplayWidget)
{
	InventoryDisplayWidgetRef = DisplayWidget;
}

void UW_InventorySlotBase::GetInventoryWidgetRef_Implementation(UW_InventoryBase* InventoryWidget)
{
	InventoryWidgetRef = InventoryWidget;
}

void UW_InventorySlotBase::AddPadding_Implementation(FMargin InPadding)
{
	if (SlotPadding)
	{
		SlotPadding->SetPadding(InPadding);
	}
}

FVector2D UW_InventorySlotBase::GetBaseSlotSize() const
{
	if (const UInventorySettings* Settings = UInventorySettings::GetInventorySettings())
	{
		return Settings->BaseSlotSize;
	}

	// Design time can reach this before the settings object is resolvable; a zero-sized
	// preview grid would look like a bug in the WBP rather than a missing setting.
	return FVector2D(64.0, 64.0);
}

FVector2D UW_InventorySlotBase::GetSlotMultiplier() const
{
	if (InventoryWidgetRef) return InventoryWidgetRef->SlotSizeMultiplier;
	if (InventoryDisplayWidgetRef) return InventoryDisplayWidgetRef->SlotSizeMultiplier;
	return FallbackSlotSizeMultiplier;
}

void UW_InventorySlotBase::RefreshDisplayFootprint()
{
	// Per axis rather than "slot if oversized, else item": the two can disagree on
	// different axes (a 1x2 item in a 3x1 slot is refused by the component, but a
	// mis-authored definition or a stale InitSlotReadOnly call can still land here), and
	// taking the larger on each axis means the widget never draws smaller than the thing
	// it is standing in — a slot cropped to less than its own footprint would leave a
	// visible hole in the grid with no cell to fill it.
	SlotFootprint.X = FMath::Max(1, FMath::Max(SlotOwnFootprint.X, ItemFootprint.X));
	SlotFootprint.Y = FMath::Max(1, FMath::Max(SlotOwnFootprint.Y, ItemFootprint.Y));
}

TSoftObjectPtr<UTexture2D> UW_InventorySlotBase::GetSlotIcon() const
{
	if (CurrentSlotDefinition.IconByFootprint.Num() == 0)
		return nullptr;

	const bool bHasItem = CurrentSlotInstance.UUID.IsValid() || !CurrentSlotRowName.IsNone();
	if (!bHasItem)
		return nullptr; // an empty slot draws its background, not an item icon

	// Keyed on the DRAWN footprint, which is the point of the whole map: the same pistol
	// resolves to different artwork depending on the hole it is sitting in.
	if (const TSoftObjectPtr<UTexture2D>* exact = CurrentSlotDefinition.IconByFootprint.Find(SlotFootprint))
	{
		if (!exact->IsNull())
			return *exact;
	}

	// The definition's own comment makes the first element the fallback, and it is keyed
	// to the item's authored SlotSize — so try that explicitly before falling back to
	// "whatever iterates first", which in a TMap is not a stable choice.
	if (const TSoftObjectPtr<UTexture2D>* authored = CurrentSlotDefinition.IconByFootprint.Find(CurrentSlotDefinition.SlotSize))
	{
		if (!authored->IsNull())
			return *authored;
	}

	for (const TPair<FIntPoint, TSoftObjectPtr<UTexture2D>>& pair : CurrentSlotDefinition.IconByFootprint)
	{
		if (!pair.Value.IsNull())
			return pair.Value;
	}

	return nullptr;
}

void UW_InventorySlotBase::RefreshSlotSize()
{
	FVector2D BaseSize = GetBaseSlotSize();
	FVector2D Multiplier = GetSlotMultiplier();

	SlotSize.X = BaseSize.X * SlotFootprint.X * Multiplier.X;
	SlotSize.Y = BaseSize.Y * SlotFootprint.Y * Multiplier.Y;

	if (SlotSizeBox)
	{
		SlotSizeBox->SetWidthOverride(SlotSize.X);
		SlotSizeBox->SetHeightOverride(SlotSize.Y);
	}

	if (SlotVisualRoot)
	{
		SlotVisualRoot->SetWidthOverride(BaseSize.X * SlotFootprint.X);
		SlotVisualRoot->SetHeightOverride(BaseSize.Y * SlotFootprint.Y);
	}

	if (SlotInfoBar)
	{
		bool bShouldShowBar = bHasInfoBar;
		
		// Override visibility if this slot belongs to a DisplayOnly widget with the flag disabled
		if (InventoryDisplayWidgetRef && !InventoryDisplayWidgetRef->ShowSlotInfoBar)
		{
			bShouldShowBar = false;
		}
		
		SlotInfoBar->SetVisibility(bShouldShowBar ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (InventoryWidgetRef)
	{
		AddPadding(InventoryWidgetRef->SlotPadding);
	}
}

void UW_InventorySlotBase::RefreshSlotDimensions()
{
	if (UGridSlot* GridSlot = Cast<UGridSlot>(Slot))
	{
		GridSlot->SetColumnSpan(SlotFootprint.X);
		GridSlot->SetRowSpan(SlotFootprint.Y);
	}
}

void UW_InventorySlotBase::SetupInfoCardPayload()
{
	if (!InfoCardAnchor) return;

	const bool bHasItem = CurrentSlotInstance.UUID.IsValid() || !CurrentSlotRowName.IsNone();
	
	if (bHasItem)
	{
		bool bIsPayloadValid = false;
		if (UInfoCardPayload_ItemInstance* Payload = Cast<UInfoCardPayload_ItemInstance>(
			InfoCardAnchor->GetOrAddPayload(HoverCardId, UInfoCardPayload_ItemInstance::StaticClass(), bIsPayloadValid,HoverCardId )))
		{
			Payload->ItemInstance = CurrentSlotInstance;
			InfoCardAnchor->RefreshCard(HoverCardId);
		}
	}
	
	InfoCardAnchor->SetCardEnabledById(HoverCardId, bHasItem, false);
}