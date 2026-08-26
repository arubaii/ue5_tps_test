#include "W_InventoryBase.h"
#include "Item/Component/InventoryComponent.h"
#include "IInventorySlotInterface.h"
#include "Item/ItemLibrary.h"
#include "Item/Structs/Definition.h"
#include "Item/Structs/Instance.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/Border.h"

void UW_InventoryBase::NativeConstruct()
{
	Super::NativeConstruct();

	if (SlotScrollBox && HasScrollBar)
	{
		SlotScrollBox->SetOrientation(Orient_Vertical);
	}
}

void UW_InventoryBase::InitializeInventory(UInventoryComponent* InInventoryComponentRef)
{
	if (!IsValid(InInventoryComponentRef))
	{
		return;
	}

	InventoryComponent = InInventoryComponentRef;
	IsInventoryWidgetInitialized = true;
	RefreshGrid();
}

FName UW_InventoryBase::GetSlotSizeIdForIndex(int32 Index) const
{
	if (!IsValid(InventoryComponent))
	{
		return FName("Default");
	}

	FItemInstance ItemInstance;
	if (!InventoryComponent->GetItemByIndex(Index, ItemInstance))
	{
		return FName("Default");
	}

	FItemDefinition ItemDefinition;
	if (!UItemLibrary::GetItemDefinition(ItemInstance.RowName, ItemDefinition))
	{
		return FName("Default");
	}

	
	return FName("STUB");
	// return ItemDefinition.SlotSizeId.IsNone() ? FName("Default") : ItemDefinition.SlotSizeId;
}

void UW_InventoryBase::RefreshGrid()
{
	if (!IsValid(InventoryComponent) || !SlotGrid)
	{
		return;
	}

	SlotGrid->ClearChildren();
	InventorySlotWidgets.Empty();

	const int32 SlotCount = InventoryComponent->MaxInventorySlots;
	if (SlotCount <= 0 || NumberOfColumns <= 0)
	{
		return;
	}

	TArray<FName> SlotSizeIds;
	SlotSizeIds.Reserve(SlotCount);
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		SlotSizeIds.Add(GetSlotSizeIdForIndex(Index));
	}

	const TArray<FGridPlacement> Placements = UInventoryGridPacker::PackInventoryGrid(SlotSizeIds, NumberOfColumns);

	for (const FGridPlacement& Placement : Placements)
	{
		if (!InventorySlotWidgetClass)
		{
			break;
		}

		UUserWidget* NewSlot = CreateWidget<UUserWidget>(GetOwningPlayer(), InventorySlotWidgetClass);
		if (!NewSlot)
		{
			continue;
		}

		if (NewSlot->Implements<UInventorySlotInterface>())
		{
			IInventorySlotInterface::Execute_InitSlot(NewSlot, InventoryComponent, Placement.ItemIndex,
				SlotSize, SlotBackgroundImage, SlotHasInfoBar);
		}

		UPanelSlot* AddedSlot = SlotGrid->AddChild(NewSlot);
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(AddedSlot))
		{
			CanvasSlot->SetPosition(FVector2D(Placement.Column * SlotSize.X, Placement.Row * SlotSize.Y));
			CanvasSlot->SetSize(FVector2D(Placement.ColumnSpan * SlotSize.X, Placement.RowSpan * SlotSize.Y));
			CanvasSlot->SetAnchors(FAnchors(0.f, 0.f));
			CanvasSlot->SetAlignment(FVector2D(0.f, 0.f));
		}

		InventorySlotWidgets.Add(NewSlot);
	}

	IsInventoryFull = (InventorySlotWidgets.Num() >= SlotCount);
}