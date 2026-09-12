#include "InfoCardWidget_Inventory.h"
#include "Inventory/Components/InventoryComponent.h"
#include "Inventory/UI/W_InventoryDisplayOnly_Base.h" 

void UInfoCardWidget_Inventory::ApplyPayloads_Implementation(const TArray<UInfoCardPayload*>& InPayloads)
{
	Super::ApplyPayloads_Implementation(InPayloads);

	if (!InventoryDisplay)
	{
		return;
	}

	const UInfoCardPayload_InventoryDisplay* DisplayPayload = nullptr;
	for (const UInfoCardPayload* Payload : InPayloads)
	{
		DisplayPayload = Cast<UInfoCardPayload_InventoryDisplay>(Payload);
		if (DisplayPayload)
		{
			break;
		}
	}

	BindInventoryDisplay(DisplayPayload ? DisplayPayload->SourceComponent : nullptr);
}

void UInfoCardWidget_Inventory::NativeDestruct()
{
	UnbindInventoryDisplay();
	Super::NativeDestruct();
}

void UInfoCardWidget_Inventory::BindInventoryDisplay(UInventoryComponent* Component)
{
	UnbindInventoryDisplay();

	if (!InventoryDisplay)
	{
		return;
	}

	if (!Component)
	{
		InventoryDisplay->ClearDisplay();
		return;
	}

	BoundInventoryComponent = Component;
	Component->OnInventoryChanged.AddDynamic(this, &UInfoCardWidget_Inventory::HandleBoundInventoryChanged);
	InventoryDisplay->RefreshFromComponent(Component);
}

void UInfoCardWidget_Inventory::UnbindInventoryDisplay()
{
	if (UInventoryComponent* Component = BoundInventoryComponent.Get())
	{
		Component->OnInventoryChanged.RemoveDynamic(this, &UInfoCardWidget_Inventory::HandleBoundInventoryChanged);
	}
	BoundInventoryComponent = nullptr;
}

void UInfoCardWidget_Inventory::HandleBoundInventoryChanged(int32 InventoryIndex)
{
	// Closing rather than risking a stale or wrong display is the safe default; 
	// the next hover/right-click rebuilds the card fresh.
	CloseCard();
}