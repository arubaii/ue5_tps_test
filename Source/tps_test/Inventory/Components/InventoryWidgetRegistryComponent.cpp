#include "InventoryWidgetRegistryComponent.h"

void UInventoryWidgetRegistryComponent::RegisterWidgetForInventory(UInventoryComponent* InventoryComponent,
																	UUserWidget* InventoryWidget)
{
	if (!InventoryComponent)
		return;

	InventoryWidgets.Add(InventoryComponent, InventoryWidget);
}

void UInventoryWidgetRegistryComponent::DeregisterInventoryWidget(UInventoryComponent* InventoryComponent)
{
	if (InventoryComponent)
		InventoryWidgets.Remove(InventoryComponent);
}

UUserWidget* UInventoryWidgetRegistryComponent::ResolveWidgetForInventory(UInventoryComponent* InventoryComponent,
																		  bool& OutFound) const
{
	OutFound = false;

	if (!InventoryComponent)
		return nullptr;

	if (const TObjectPtr<UUserWidget>* found = InventoryWidgets.Find(InventoryComponent))
	{
		OutFound = true;
		return *found;
	}

	return nullptr;
}