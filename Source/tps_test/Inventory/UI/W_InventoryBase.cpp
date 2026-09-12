#include "W_InventoryBase.h"

#include "Inventory/ItemLibrary.h"
#include "NameTBD/Search/SearchLibrary.h" 
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

	// PreConstruct also runs once at runtime, on the real widget. BuildPreviewGrid's own
	// design-time check turns that call into a teardown, so a preview left over from the
	// designer can never survive into a live session.
	BuildPreviewGrid();
}

void UW_InventoryBase::NativeConstruct()
{
	Super::NativeConstruct();

	// Belt and braces with the PreConstruct teardown: Construct is the last point before
	// anything gameplay-driven touches this widget, and a stray preview cell here would
	// draw on top of the real grid.
	ClearPreviewGrid();

	RefreshFilterOptionsFromMap();
}

void UW_InventoryBase::ClearPreviewGrid()
{
	for (UW_InventorySlotBase* previewWidget : PreviewSlotWidgets)
	{
		if (previewWidget)
			previewWidget->RemoveFromParent();
	}
	PreviewSlotWidgets.Reset();

	if (PreviewGridPanel)
	{
		// ClearChildren as well as the loop above: a previous PreConstruct in this editor
		// session may have left children behind that this instance's array no longer
		// knows about (the array is Transient and the panel's children are not).
		PreviewGridPanel->ClearChildren();
		PreviewGridPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UW_InventoryBase::BuildPreviewGrid()
{
	// Always start from nothing. PreConstruct re-runs on every property edit in the
	// designer, and rebuilding on top of the previous pass would stack cells.
	ClearPreviewGrid();

	if (!PreviewGridPanel)
		return; // this WBP has no preview panel — nothing to do, at either time

	const bool bDesignTime =
#if WITH_EDITOR
		IsDesignTime();
#else
		false;
#endif

	if (!bDesignTime || !bShowDesignerPreview || !InventorySlotClass)
		return; // ClearPreviewGrid already collapsed the panel

	const int32 gridW = FMath::Max(0, PreviewGridWidth);
	const int32 gridH = FMath::Max(0, PreviewGridHeight);
	const int32 totalCells = gridW * gridH;

	if (totalCells <= 0)
		return;

	// Resolve the authored overrides into a per-cell picture first, then draw it. The
	// two passes can't be merged: whether a cell is a slot of its own depends on an
	// override that may be authored AFTER it in the array.
	TArray<FIntPoint> footprints;
	footprints.Init(FIntPoint(1, 1), totalCells);

	TArray<int32> owningIndex;                 // INDEX_NONE == the cell owns itself
	owningIndex.Init(INDEX_NONE, totalCells);

	TArray<ESlotType> types;
	types.Init(PreviewSlotType, totalCells);

	TArray<TSoftObjectPtr<UTexture2D>> backgrounds;
	backgrounds.SetNum(totalCells);

	for (const FSlotOverrideData& slotOverride : PreviewSlotOverrides)
	{
		const int32 anchor = slotOverride.Index;

		if (!footprints.IsValidIndex(anchor))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: preview slot override index %d is outside a %dx%d grid — skipped."),
				*GetName(), anchor, gridW, gridH);
			continue;
		}

		// Type and background land on the anchor whether or not the footprint is valid:
		// those two are independent of geometry and a rejected footprint shouldn't also
		// throw away a perfectly good slot type.
		types[anchor] = slotOverride.Type;
		backgrounds[anchor] = slotOverride.BackgroundImage;

		const FIntPoint footprint(
			FMath::Max(1, slotOverride.SlotFootprint.X),
			FMath::Max(1, slotOverride.SlotFootprint.Y));

		if (footprint == FIntPoint(1, 1))
			continue; // an ordinary cell; nothing to swallow

		const int32 anchorX = anchor % gridW;
		const int32 anchorY = anchor / gridW;

		if (anchorX + footprint.X > gridW || anchorY + footprint.Y > gridH)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: preview slot override at %d (%dx%d) runs off the grid — drawn as 1x1."),
				*GetName(), anchor, footprint.X, footprint.Y);
			continue;
		}

		// Reject on collision rather than letting the later override win: two oversized
		// slots overlapping is an authoring mistake, and silently resolving it in the
		// designer would hide a mistake the component itself rejects at init.
		bool bRegionClear = true;
		for (int32 y = 0; y < footprint.Y && bRegionClear; ++y)
		{
			for (int32 x = 0; x < footprint.X && bRegionClear; ++x)
			{
				const int32 cell = (anchorY + y) * gridW + (anchorX + x);
				if (owningIndex[cell] != INDEX_NONE || footprints[cell] != FIntPoint(1, 1))
					bRegionClear = false;
			}
		}

		if (!bRegionClear)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("%s: preview slot override at %d collides with another — drawn as 1x1."),
				*GetName(), anchor);
			continue;
		}

		footprints[anchor] = footprint;

		for (int32 y = 0; y < footprint.Y; ++y)
		{
			for (int32 x = 0; x < footprint.X; ++x)
			{
				if (x == 0 && y == 0)
					continue; // the anchor keeps owning itself

				owningIndex[(anchorY + y) * gridW + (anchorX + x)] = anchor;
			}
		}
	}

	PreviewGridPanel->SetVisibility(ESlateVisibility::Visible);
	PreviewSlotWidgets.Reserve(totalCells);

	// One widget per CELL, swallowed cells included — the same shape the runtime grid
	// builds, so what the designer shows is what RefreshGrid will produce.
	for (int32 i = 0; i < totalCells; ++i)
	{
		UW_InventorySlotBase* slotWidget = CreateWidget<UW_InventorySlotBase>(this, InventorySlotClass);
		if (!slotWidget)
			continue;

		PreviewGridPanel->AddChildToGrid(slotWidget);
		PreviewSlotWidgets.Add(slotWidget);

		if (UGridSlot* gridSlot = Cast<UGridSlot>(slotWidget->Slot))
		{
			gridSlot->SetRow(i / gridW);
			gridSlot->SetColumn(i % gridW);
		}

		// Before InitSlotPreview, not after: the slot reads SlotSizeMultiplier and
		// SlotPadding off this reference while it sizes itself.
		slotWidget->GetInventoryWidgetRef(this);

		const bool bIsShadowCell = owningIndex[i] != INDEX_NONE;

		slotWidget->InitSlotPreview(
			footprints[i],
			types[i],
			backgrounds[i],
			bIsShadowCell);

		slotWidget->AddPadding(SlotPadding);
	}
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

	// The real thing is about to be drawn, so the drawing of it goes away. This is the
	// path that actually matters in PIE: PreConstruct ran before the component arrived.
	ClearPreviewGrid();

	// 1. TOGGLE VISIBILITY & STATE
	bInSubView = false;
	SlotGridPanel->SetVisibility(ESlateVisibility::Visible);
	if (SubViewPanel) SubViewPanel->SetVisibility(ESlateVisibility::Collapsed);

	const TArray<FInventorySlot> Slots = InventoryComponent->GetSlots();
	const int32 GridWidth = FMath::Max(1, InventoryComponent->GridWidth);

	// One widget per CELL, including the cells an oversized slot (or a multi-cell item)
	// swallows — those collapse themselves inside RefreshSlot rather than being skipped
	// here. Skipping them would be tempting and wrong: it breaks the widget-index ==
	// slot-index identity that HandleInventoryChanged's O(1) path depends on, and a
	// collapsed widget costs nothing in a grid panel anyway. The span each visible widget
	// claims is set from its own footprint in RefreshSlotDimensions.
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

	ClearPreviewGrid();

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
	const int32 TotalCells = InventoryComponent->IsGridEnabled() ? (GridW * GridH) : 0;

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