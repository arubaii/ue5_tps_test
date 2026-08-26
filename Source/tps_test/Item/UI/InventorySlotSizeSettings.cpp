#include "InventorySlotSizeSettings.h"

UInventorySlotSizeSettings::UInventorySlotSizeSettings()
{
	CategoryName = TEXT("Game");
	SectionName  = TEXT("Inventory Slot Sizes");

	// Defaults only apply the very first time the ini has no entries yet.
	SlotSizes.Add({ FName("Default"), FVector2D(128.f, 128.f) });
	SlotSizes.Add({ FName("Wide"),    FVector2D(256.f, 128.f) });
	SlotSizes.Add({ FName("Tall"),    FVector2D(128.f, 256.f) });
}

const TArray<FInventorySlotSizeEntry>& UInventorySlotSizeSettings::GetSlotSizes()
{
	return GetDefault<UInventorySlotSizeSettings>()->SlotSizes;
}

FVector2D UInventorySlotSizeSettings::GetSlotSize(FName Identifier, bool& bFound)
{
	const TArray<FInventorySlotSizeEntry>& Sizes = GetSlotSizes();
	for (const FInventorySlotSizeEntry& Entry : Sizes)
	{
		if (Entry.Identifier == Identifier)
		{
			bFound = true;
			return Entry.Size;
		}
	}
	bFound = false;
	return FVector2D::ZeroVector;
}

TArray<FName> UInventorySlotSizeSettings::GetSlotSizeIdentifiers()
{
	TArray<FName> Ids;
	for (const FInventorySlotSizeEntry& Entry : GetSlotSizes())
	{
		Ids.Add(Entry.Identifier);
	}
	return Ids;
}