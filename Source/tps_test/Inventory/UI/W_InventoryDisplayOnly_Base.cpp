#include "W_InventoryDisplayOnly_Base.h"
#include "Components/GridSlot.h"
#include "Inventory/Components/InventoryComponent.h"

UW_InventorySlotBase* UW_InventoryDisplayOnlyBase::GetOrCreateSlotWidget(int32 Index)
{
	if (InventorySlotWidgets.IsValidIndex(Index))
	{
		UW_InventorySlotBase* SlotWidget = InventorySlotWidgets[Index];
		SlotWidget->SetVisibility(ESlateVisibility::Visible);
		if (SlotWidget->GetParent() != SlotGridPanel)
		{
			SlotWidget->RemoveFromParent();
			SlotGridPanel->AddChildToGrid(SlotWidget);
		}
		return SlotWidget;
	}

	UW_InventorySlotBase* SlotWidget = CreateWidget<UW_InventorySlotBase>(this, InventorySlotClass);
	SlotGridPanel->AddChildToGrid(SlotWidget);
	InventorySlotWidgets.Add(SlotWidget);
	return SlotWidget;
}

void UW_InventoryDisplayOnlyBase::RefreshFromItems(UInventoryComponent* InventoryComponentRef, const TArray<FInventorySlot>& InventorySlots)
{
	if (!SlotGridPanel || !InventorySlotClass) return;

	if (!InventoryComponentRef || InventorySlots.IsEmpty())
	{
		ClearDisplay();
		return;
	}

	const int32 SafeGridWidth = FMath::Max(1, GridWidth);

	for (int32 i = 0; i < InventorySlots.Num(); ++i)
	{
		UW_InventorySlotBase* SlotWidget = GetOrCreateSlotWidget(i);

		if (UGridSlot* GridSlot = Cast<UGridSlot>(SlotWidget->Slot))
		{
			GridSlot->SetRow(i / SafeGridWidth);
			GridSlot->SetColumn(i % SafeGridWidth);
		}

		FItemInstance ResolvedItem;
		InventoryComponentRef->GetItem(InventorySlots[i].ItemID, ResolvedItem);

		SlotWidget->GetDisplayWidgetRef(this);
		SlotWidget->InitSlotReadOnly(ResolvedItem, InventorySlots[i].bIsAnchor);
	}

	for (int32 i = InventorySlots.Num(); i < InventorySlotWidgets.Num(); ++i)
	{
		InventorySlotWidgets[i]->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UW_InventoryDisplayOnlyBase::RefreshFromComponent(UInventoryComponent* Component)
{
	if (!Component)
	{
		ClearDisplay();
		return;
	}

	RefreshFromItems(Component, Component->GetSlots());
}



void UW_InventoryDisplayOnlyBase::ClearDisplay()
{
	for (UW_InventorySlotBase* SlotWidget : InventorySlotWidgets)
	{
		SlotWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}