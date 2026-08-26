#include "InventoryGridPacker.h"
#include "InventorySlotSizeSettings.h"

TArray<FGridPlacement> UInventoryGridPacker::PackInventoryGrid(const TArray<FName>& SlotSizeIds, int32 NumColumns)
{
	TArray<FGridPlacement> Placements;
	Placements.Reserve(SlotSizeIds.Num());

	if (NumColumns <= 0)
	{
		return Placements;
	}

	TArray<TArray<bool>> Occupied;

	auto EnsureRowExists = [&](int32 Row)
	{
		while (Occupied.Num() <= Row)
		{
			TArray<bool> NewRow;
			NewRow.Init(false, NumColumns);
			Occupied.Add(NewRow);
		}
	};

	auto DoesFootprintFit = [&](int32 StartRow, int32 StartCol, int32 ColSpan, int32 RowSpan) -> bool
	{
		if (StartCol + ColSpan > NumColumns)
		{
			return false;
		}
		EnsureRowExists(StartRow + RowSpan - 1);
		for (int32 r = StartRow; r < StartRow + RowSpan; ++r)
		{
			for (int32 c = StartCol; c < StartCol + ColSpan; ++c)
			{
				if (Occupied[r][c])
				{
					return false;
				}
			}
		}
		return true;
	};

	auto MarkFootprintOccupied = [&](int32 StartRow, int32 StartCol, int32 ColSpan, int32 RowSpan)
	{
		EnsureRowExists(StartRow + RowSpan - 1);
		for (int32 r = StartRow; r < StartRow + RowSpan; ++r)
		{
			for (int32 c = StartCol; c < StartCol + ColSpan; ++c)
			{
				Occupied[r][c] = true;
			}
		}
	};

	for (int32 ItemIndex = 0; ItemIndex < SlotSizeIds.Num(); ++ItemIndex)
	{
		bool bFound = false;
		const FVector2D SizePixels = UInventorySlotSizeSettings::GetSlotSize(SlotSizeIds[ItemIndex], bFound);

		// Convert pixel size to cell span, assuming a base cell = Default slot size (128x128).
		// Adjust the divisor if your base cell size differs.
		const float BaseCellSize = 128.f;
		const int32 ColSpan = bFound ? FMath::Max(1, FMath::RoundToInt(SizePixels.X / BaseCellSize)) : 1;
		const int32 RowSpan = bFound ? FMath::Max(1, FMath::RoundToInt(SizePixels.Y / BaseCellSize)) : 1;

		// Safety cap: never search more rows than there are items, prevents an infinite
		// loop if ColSpan > NumColumns (misconfigured data).
		const int32 MaxRowSearch = SlotSizeIds.Num() + 1;

		bool bPlaced = false;
		int32 Row = 0;

		while (!bPlaced && Row < MaxRowSearch)
		{
			for (int32 Col = 0; Col <= NumColumns - ColSpan; ++Col)
			{
				if (DoesFootprintFit(Row, Col, ColSpan, RowSpan))
				{
					MarkFootprintOccupied(Row, Col, ColSpan, RowSpan);

					FGridPlacement Placement;
					Placement.ItemIndex = ItemIndex;
					Placement.Row = Row;
					Placement.Column = Col;
					Placement.ColumnSpan = ColSpan;
					Placement.RowSpan = RowSpan;
					Placements.Add(Placement);

					bPlaced = true;
					break;
				}
			}
			if (!bPlaced)
			{
				++Row;
			}
		}

		if (!bPlaced)
		{
			// Couldn't fit (e.g. ColSpan > NumColumns) — place at (0,0) as a fallback
			// rather than silently dropping the item.
			FGridPlacement Fallback;
			Fallback.ItemIndex = ItemIndex;
			Fallback.Row = 0;
			Fallback.Column = 0;
			Fallback.ColumnSpan = FMath::Min(ColSpan, NumColumns);
			Fallback.RowSpan = RowSpan;
			Placements.Add(Fallback);
		}
	}

	return Placements;
}