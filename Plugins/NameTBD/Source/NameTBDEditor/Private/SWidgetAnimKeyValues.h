// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "SWidgetAnimTimeline.h"

class SVerticalBox;

/**
 * The Selection panel's Key Values: the numbers keys hold, each key showing only those it alters (see WidgetAnimKeyValues) -
 * a Transform key that only scales shows Scale X and Y, not its whole Transform.
 *
 * One key: a row per value, name and box. Several keys (a track picked - every key of it - or several keys picked): a table
 * per track, a row per key and a column per value. Keys picked list every value they hold. A track picked lists only the values
 * its keys alter, a box only where that key alters it. No box is dimmed: one that can be set looks it - only one that can't
 * (a Slide's end) is greyed, being disabled. A table's rows are laid out as they are while they fit MaxTableHeight (no space
 * left under them); past that, a list of that height.
 *
 * Every box is the track's own number, read live: typed in - plain numbers or math ("+8", "*2", "x * 2 + 1", see
 * WidgetAnimKeyValues::Evaluate) - or dragged. Ctrl+click picks several boxes (Shift+click a run down a column): typing into
 * one sets them all, each worked out against its own number, and dragging one moves them all by the same amount. One undo step
 * per edit. See SWidgetAnimTimeline::SetKeyValues.
 */
class SWidgetAnimKeyValues : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimKeyValues)
		: _ForTracks(false)
	{}
		/** The keys to list, as the Track Editor counts them. Several tracks' are listed track by track, in the order given. */
		SLATE_ARGUMENT(TArray<SWidgetAnimTimeline::FKeyRef>, Keys)

		/** Listed for tracks picked - all their keys - rather than keys picked: only the values altered. */
		SLATE_ARGUMENT(bool, ForTracks)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<SWidgetAnimTimeline> InPanel);

	/** The most tracks listed at once; past it, how many more there are. A big selection is edited a few tracks at a time. */
	static constexpr int32 MaxTracks = 12;

private:

	/** One track's part of the list: its keys, and which of its channels are shown - as columns - and where. */
	struct FTrackPart
	{
		int32 Layer = INDEX_NONE;
		TArray<int32> Keys;
		TArray<int32> Columns;

		/** Per key (as Keys), per column: whether that key alters it - shown as a box. */
		TArray<TArray<bool>> Shown;
	};

	/** One row of a track's table: a key. */
	struct FRowItem
	{
		int32 Part = INDEX_NONE;
		int32 Row = INDEX_NONE;
	};

	TSharedRef<SWidget> MakePart(int32 PartIndex);
	TSharedRef<SWidget> MakeSingleKey(const FTrackPart& Part);
	TSharedRef<SWidget> MakeTable(int32 PartIndex);
	TSharedRef<SWidget> MakeKeyLabel(const FTrackPart& Part, int32 Row) const;
	TSharedRef<SWidget> MakeCell(const FTrackPart& Part, int32 Row, int32 Column, float Width) const;

	/** A key's name in the list: Start and End for a track's start and end values, else Key N - and its time. */
	FText GetKeyName(int32 LayerIndex, int32 Key) const;

	TWeakPtr<SWidgetAnimTimeline> Panel;
	TArray<FTrackPart> Parts;
	TArray<TArray<TSharedPtr<FRowItem>>> PartRows;
	int32 NumTracksLeftOut = 0;

	/** Every value the keys hold is listed, not only those altered: keys picked (a track's list shows only those altered). */
	bool bListAll = false;
};
