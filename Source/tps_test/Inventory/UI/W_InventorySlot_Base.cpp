#include "W_InventorySlot_Base.h"
#include "Inventory/Components/InventoryComponent.h"
#include "Inventory/ItemLibrary.h" 
#include "DeveloperSettings/InventorySettings.h"
#include "W_InventoryBase.h"
#include "SlotInfoBarWidget.h"
#include "W_InventoryDisplayOnly_Base.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/GridSlot.h"
#include "UI/Cards/InfoCardAnchor.h"
#include "UI/Cards/InfoCardTypes.h"

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

	if (InventoryComponentRef)
	{
		bHasItem = InventoryComponentRef->GetItemByIndex(CurrentSlotIndex, CurrentSlotInstance);
		CurrentSlot = InventoryComponentRef->GetSlot(CurrentSlotIndex);
	}

	const bool bIsShadowCell = bHasItem && !CurrentSlot.bIsAnchor;

	if (bHasItem && !bIsShadowCell)
	{
		CurrentSlotRowName = CurrentSlotInstance.RowName;
		UItemLibrary::GetItemDefinition(CurrentSlotRowName, CurrentSlotDefinition);
		SlotFootprint = CurrentSlotDefinition.SlotSize;
		bHasInfoBar = CurrentSlotDefinition.HasInfoBar;
	}
	else
	{
		CurrentSlotInstance = FItemInstance();
		CurrentSlotDefinition = FItemDefinition();
		CurrentSlotRowName = NAME_None;
		SlotFootprint = FIntPoint(1, 1);
		bHasInfoBar = false;
	}

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

void UW_InventorySlotBase::InitSlotReadOnly_Implementation(const FItemInstance& ItemInstance, bool bIsAnchor)
{
	CurrentSlotInstance = ItemInstance;
	CurrentSlotRowName = ItemInstance.RowName;
	
	bool bHasItem = CurrentSlotInstance.UUID.IsValid() || !CurrentSlotRowName.IsNone();
	const bool bIsShadowCell = bHasItem && !bIsAnchor;

	if (bHasItem && !bIsShadowCell)
	{
		UItemLibrary::GetItemDefinition(CurrentSlotRowName, CurrentSlotDefinition);
		SlotFootprint = CurrentSlotDefinition.SlotSize;
		bHasInfoBar = CurrentSlotDefinition.HasInfoBar;
	}
	else
	{
		CurrentSlotDefinition = FItemDefinition();
		SlotFootprint = FIntPoint(1, 1);
		bHasInfoBar = false;
	}

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
	return UInventorySettings::GetInventorySettings()->BaseSlotSize;
}

FVector2D UW_InventorySlotBase::GetSlotMultiplier() const
{
	if (InventoryWidgetRef) return InventoryWidgetRef->SlotSizeMultiplier;
	if (InventoryDisplayWidgetRef) return InventoryDisplayWidgetRef->SlotSizeMultiplier;
	return FallbackSlotSizeMultiplier;
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
	
	InfoCardAnchor->SetCardEnabledById(HoverCardId, bHasItem);
}