#include "W_InventoryBase.h"

#include "Inventory/ItemLibrary.h"
#include "UI/Search/SearchLibrary.h" // adjust to wherever USearchLibrary lives in your module

FSubInventoryView UW_InventoryBase::FilterSubInventoryBySearch(const FSubInventoryView& View,
                                                               const FString& SearchText)
{
	if (SearchText.IsEmpty())
		return View;

	FSubInventoryView filtered;
	filtered.SourceInventory = View.SourceInventory;
	filtered.Query = View.Query;
	filtered.bAllowIntraInventorySwap = View.bAllowIntraInventorySwap;
	filtered.Entries.Reserve(View.Entries.Num());

	for (const FSubInventoryEntry& entry : View.Entries)
	{
		// Match the authored display name, falling back to the row name only when the
		// definition can't be resolved — the player is searching for what the tooltip
		// says, not for an internal data table key.
		FItemDefinition definition;
		const FString label = UItemLibrary::GetItemDefinition(entry.Item.RowName, definition)
			? definition.DisplayName.ToString()
			: entry.Item.RowName.ToString();

		if (!USearchLibrary::FuzzyMatch(SearchText, label))
			continue;

		FSubInventoryEntry kept = entry;

		// Positions are handed out fresh. SourceSlotIndex is left alone: it addresses the
		// real inventory and the swap paths depend on it, so it must survive filtering
		// untouched even though ViewIndex does not.
		kept.ViewIndex = filtered.Entries.Num();
		filtered.Entries.Add(MoveTemp(kept));
	}

	return filtered;
}

void UW_InventoryBase::NativePreConstruct()
{
	Super::NativePreConstruct();

	// Applied unconditionally, design-time included — this is a style property, and a
	// style property that only shows up at runtime defeats the point of previewing it
	// in the UMG designer.
	if (SlotBorder)
	{
		// UBorder::GetBrush() doesn't exist yet in 5.7 — Background is the same
		// underlying FSlateBrush (it's what Blueprint's own "Brush" pin reads/writes),
		// just without an accessor wrapping it. Read and write it directly instead.
		FSlateBrush brush = SlotBorder->Background;
		brush.TintColor = FSlateColor(BackgroundColor);

		// CornerRadii is only honored when DrawAs is RoundedBox; on a plain Box brush
		// it's silently ignored, which is why leaving CornerRadius at its zero default
		// still needs this switched in order to fall back to square corners correctly.
		brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		brush.OutlineSettings.CornerRadii = CornerRadius;
		brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;

		// One SetBrush call carrying both changes, rather than SetBrushColor followed by
		// a separate rounding call — the two would fight over the same underlying brush
		// struct, with whichever ran second silently discarding the other's change.
		SlotBorder->SetBrush(brush);
	}
}

void UW_InventoryBase::NativeConstruct()
{
	Super::NativeConstruct();

	RefreshFilterOptionsFromMap();
}

void UW_InventoryBase::RefreshFilterOptionsFromMap()
{
	FilterKeyToFragment.Reset();

	if (!InventorySearchBar)
		return; // this WBP has no search UI — nothing to populate

	if (!bShowSearchBar)
	{
		// Collapsed and left with no options: even if something re-shows the widget
		// later without going through this function again, there's nothing stale in
		// its dropdown to be wrong.
		InventorySearchBar->SetVisibility(ESlateVisibility::Collapsed);
		InventorySearchBar->SetFilterOptions(TArray<FSearchFilterOption>{});
		return;
	}

	InventorySearchBar->SetVisibility(ESlateVisibility::Visible);

	if (FragmentFilterOptions.Num() == 0)
	{
		InventorySearchBar->SetFilterOptions(TArray<FSearchFilterOption>{});
		return;
	}

	TArray<FSearchFilterOption> options;
	options.Reserve(FragmentFilterOptions.Num());

	for (const TPair<TObjectPtr<UScriptStruct>, FText>& pair : FragmentFilterOptions)
	{
		if (!pair.Key)
			continue; // an empty row in the Details panel — skip rather than crash on it

		// The fragment struct's own name is stable and unique within the project, so it
		// doubles as the search bar's opaque Key with nothing extra to author or keep
		// in sync — the one thing the designer fills in is the display label.
		const FName key = pair.Key->GetFName();

		FilterKeyToFragment.Add(key, pair.Key);
		options.Add(FSearchFilterOption{ key, pair.Value });
	}

	InventorySearchBar->SetFilterOptions(options);
}

UScriptStruct* UW_InventoryBase::ResolveFilterKey(FName Key) const
{
	if (const TObjectPtr<UScriptStruct>* found = FilterKeyToFragment.Find(Key))
		return *found;

	return nullptr;
}