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

	int32 VisibleCount = 0;

	for (int32 i = 0; i < InventorySlots.Num(); ++i)
	{
		if (!InventorySlots[i].bIsAnchor)
		{
			continue; // shadow cell of a multi-cell item — never gets its own widget
		}

		FItemInstance ResolvedItem;
		InventoryComponentRef->GetItem(InventorySlots[i].ItemID, ResolvedItem);

		const bool bHasItem = ResolvedItem.UUID.IsValid() || !ResolvedItem.RowName.IsNone();
		if (!bHasItem)
		{
			continue; // empty slot — nothing to preview
		}

		UW_InventorySlotBase* SlotWidget = GetOrCreateSlotWidget(VisibleCount);

		if (UGridSlot* GridSlot = Cast<UGridSlot>(SlotWidget->Slot))
		{
			GridSlot->SetRow(VisibleCount / SafeGridWidth);
			GridSlot->SetColumn(VisibleCount % SafeGridWidth);
		}

		SlotWidget->GetDisplayWidgetRef(this);
		SlotWidget->InitSlotReadOnly(ResolvedItem, InventorySlots[i].bIsAnchor);

		++VisibleCount;
	}

	for (int32 i = VisibleCount; i < InventorySlotWidgets.Num(); ++i)
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