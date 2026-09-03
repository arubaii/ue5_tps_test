#include "W_InventoryBase.h"

#include "Inventory/ItemLibrary.h"
#include "UI/Search/SearchLibrary.h" // adjust to wherever USearchLibrary lives in your module
#include "Components/GridSlot.h"

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

	for (const TPair<FFragmentFilterOption, FText>& pair : FragmentFilterOptions)
	{
		if (pair.Key.Fragments.Num() == 0)
			continue; // an empty row in the Details panel — skip rather than crash on it

		// A single fragment struct no longer doubles as its own key now that an entry can
		// name several — build one from the group instead: every fragment's name plus the
		// combinator, so two entries with the same fragments but different FilterMode
		// (or vice versa) still resolve to distinct keys.
		FString keyString;
		for (const TObjectPtr<UScriptStruct>& fragment : pair.Key.Fragments)
		{
			if (fragment)
				keyString += fragment->GetName() + TEXT("|");
		}
		keyString += (pair.Key.FilterMode == EFragmentFilterMode::WidensFilter)
			? TEXT("Widens")
			: TEXT("Narrows");

		const FName key(*keyString);

		FilterKeyToFragment.Add(key, pair.Key);
		options.Add(FSearchFilterOption{ key, pair.Value });
	}

	InventorySearchBar->SetFilterOptions(options);
}

FFragmentFilterOption UW_InventoryBase::ResolveFilterKey(FName Key) const
{
	if (const FFragmentFilterOption* found = FilterKeyToFragment.Find(Key))
		return *found;

	return FFragmentFilterOption();
}

void UW_InventoryBase::RefreshGrid()
{
	if (!InventoryComponent || !SlotGridPanel || !InventorySlotClass) return;

	// 1. TOGGLE VISIBILITY & STATE
	bInSubView = false;
	SlotGridPanel->SetVisibility(ESlateVisibility::Visible);
	if (SubViewPanel) SubViewPanel->SetVisibility(ESlateVisibility::Collapsed);

	const TArray<FInventorySlot> Slots = InventoryComponent->GetSlots();
	const int32 GridWidth = FMath::Max(1, InventoryComponent->GridWidth);

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		UW_InventorySlotBase* SlotWidget = nullptr;

		// 2. REUSE OR CREATE WIDGET (Pooling to fix lag)
		if (i < InventorySlotWidgets.Num())
		{
			SlotWidget = InventorySlotWidgets[i];
			SlotWidget->SetVisibility(ESlateVisibility::Visible);
			
			if (SlotWidget->GetParent() != SlotGridPanel)
			{
				SlotWidget->RemoveFromParent();
				SlotGridPanel->AddChildToGrid(SlotWidget);
			}
		}
		else
		{
			SlotWidget = CreateWidget<UW_InventorySlotBase>(this, InventorySlotClass);
			SlotGridPanel->AddChildToGrid(SlotWidget);
			InventorySlotWidgets.Add(SlotWidget);
		}

		// 3. UPDATE DATA
		if (UGridSlot* GridSlot = Cast<UGridSlot>(SlotWidget->Slot))
		{
			GridSlot->SetRow(i / GridWidth);
			GridSlot->SetColumn(i % GridWidth);
		}

		SlotWidget->GetInventoryWidgetRef(this);
		SlotWidget->InitSlot(InventoryComponent, i);
	}

	// 4. HIDE UNUSED WIDGETS
	for (int32 i = Slots.Num(); i < InventorySlotWidgets.Num(); ++i)
	{
		InventorySlotWidgets[i]->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UW_InventoryBase::RefreshSubView()
{
	if (!InventoryComponent || !SubViewPanel || !InventorySlotClass) return;

	// 1. TOGGLE VISIBILITY & STATE
	bInSubView = true;
	SubViewPanel->SetVisibility(ESlateVisibility::Visible);
	if (SlotGridPanel) SlotGridPanel->SetVisibility(ESlateVisibility::Collapsed);

	FSubInventoryView View = InventoryComponent->GetSubInventoryByFragment(SubViewQuery);

	if (!SearchQuery.IsEmpty())
	{
		View = FilterSubInventoryBySearch(View, SearchQuery);
	}

	TArray<FSubInventoryPlacement> Placements;
	// Ask for the layout of the ITEMS ONLY (bShowEmptySlots = false). We will generate the empty slots ourselves.
	UInventoryComponent::GetSubInventoryLayout(View, InventoryComponent->GridWidth, true, false, Placements);

	const int32 GridW = FMath::Max(1, InventoryComponent->GridWidth);
	const int32 GridH = FMath::Max(1, InventoryComponent->GridHeight);
	const int32 TotalCells = InventoryComponent->IsGridEnabled() ? (GridW * GridH) : InventoryComponent->MaxInventorySlots;

	// Track which grid coordinates are occupied so we know where to put filler slots
	TArray<bool> OccupiedCells;
	OccupiedCells.Init(false, TotalCells);

	int32 WidgetIndex = 0;

	// Helper lambda to cleanly reuse or spawn widgets
	auto GetOrCreateSlotWidget = [&](int32 CurrentWidgetIndex) -> UW_InventorySlotBase*
	{
		UW_InventorySlotBase* SlotWidget = nullptr;
		if (CurrentWidgetIndex < InventorySlotWidgets.Num())
		{
			SlotWidget = InventorySlotWidgets[CurrentWidgetIndex];
			SlotWidget->SetVisibility(ESlateVisibility::Visible);
			if (SlotWidget->GetParent() != SubViewPanel)
			{
				SlotWidget->RemoveFromParent();
				SubViewPanel->AddChildToGrid(SlotWidget);
			}
		}
		else
		{
			SlotWidget = CreateWidget<UW_InventorySlotBase>(this, InventorySlotClass);
			SubViewPanel->AddChildToGrid(SlotWidget);
			InventorySlotWidgets.Add(SlotWidget);
		}
		return SlotWidget;
	};

	// --- STEP 1: PLACE THE ACTUAL SEARCHED/FILTERED ITEMS ---
	for (const FSubInventoryPlacement& Placement : Placements)
	{
		// FIX: Retrieve the TRUE SourceSlotIndex from the view entry!
		int32 TrueSourceIndex = Placement.SourceSlotIndex;
		if (Placement.ViewIndex != INDEX_NONE && View.Entries.IsValidIndex(Placement.ViewIndex))
		{
			TrueSourceIndex = View.Entries[Placement.ViewIndex].SourceSlotIndex;
		}

		// Mark these cells as occupied
		for (int32 r = 0; r < Placement.RowSpan; ++r)
		{
			for (int32 c = 0; c < Placement.ColumnSpan; ++c)
			{
				int32 CellIndex = (Placement.Row + r) * GridW + (Placement.Column + c);
				if (OccupiedCells.IsValidIndex(CellIndex))
				{
					OccupiedCells[CellIndex] = true;
				}
			}
		}

		UW_InventorySlotBase* SlotWidget = GetOrCreateSlotWidget(WidgetIndex);

		if (UGridSlot* GridSlot = Cast<UGridSlot>(SlotWidget->Slot))
		{
			GridSlot->SetRow(Placement.Row);
			GridSlot->SetColumn(Placement.Column);
			GridSlot->SetRowSpan(Placement.RowSpan);
			GridSlot->SetColumnSpan(Placement.ColumnSpan);
		}

		SlotWidget->InitSlot(InventoryComponent, TrueSourceIndex);
		SlotWidget->AddPadding(SlotPadding);
		SlotWidget->GetInventoryWidgetRef(this);

		WidgetIndex++;
	}

	// --- STEP 2: FILL THE REST OF THE INVENTORY WITH EMPTY SLOTS ---
	if (bSubViewShowsEmptySlots)
	{
		for (int32 i = 0; i < TotalCells; ++i)
		{
			if (OccupiedCells.IsValidIndex(i) && !OccupiedCells[i])
			{
				UW_InventorySlotBase* SlotWidget = GetOrCreateSlotWidget(WidgetIndex);

				if (UGridSlot* GridSlot = Cast<UGridSlot>(SlotWidget->Slot))
				{
					GridSlot->SetRow(i / GridW);
					GridSlot->SetColumn(i % GridW);
					GridSlot->SetRowSpan(1);
					GridSlot->SetColumnSpan(1);
				}

				// Passing INDEX_NONE (-1) forces the Blueprint to draw a genuinely empty slot
				SlotWidget->InitSlot(InventoryComponent, INDEX_NONE);
				SlotWidget->AddPadding(SlotPadding);
				SlotWidget->GetInventoryWidgetRef(this);

				WidgetIndex++;
			}
		}
	}

	// --- STEP 3: HIDE UNUSED WIDGETS IN THE POOL ---
	for (int32 i = WidgetIndex; i < InventorySlotWidgets.Num(); ++i)
	{
		InventorySlotWidgets[i]->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UW_InventoryBase::HandleInventoryChanged(int32 Index)
{
	if (!InventoryComponent) return;

	if (bInSubView)
	{
		// Debounce the heavy packing algorithm:
		// If this fires multiple times in a single frame, we only queue the refresh ONCE.
		if (!bSubViewNeedsRefresh)
		{
			bSubViewNeedsRefresh = true;
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimerForNextTick(this, &UW_InventoryBase::ExecuteDeferredSubViewRefresh);
			}
		}
	}
	else
	{
		// Safety check: If the widget array hasn't been populated yet, do a full rebuild
		if (InventorySlotWidgets.Num() != InventoryComponent->GetSlots().Num())
		{
			RefreshGrid();
		}
		// OPTIMIZATION: Just update the exact slot that changed (O(1) operation)
		else if (InventorySlotWidgets.IsValidIndex(Index))
		{
			InventorySlotWidgets[Index]->InitSlot(InventoryComponent, Index);
		}
	}
}

void UW_InventoryBase::ExecuteDeferredSubViewRefresh()
{
	bSubViewNeedsRefresh = false;
	RefreshSubView();
}