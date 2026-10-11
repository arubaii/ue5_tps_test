// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimKeyValues.h"

#include "NameTBDColors.h"
#include "NameTBDTooltips.h"
#include "WidgetAnimKeyValues.h"
#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "DetailLayoutBuilder.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SWidgetAnimKeyValues"

namespace WidgetAnimKeyValueCells
{
	namespace Palette = NameTBD::Colors::WidgetAnimEditor;
	namespace Tips = NameTBD::Tooltips::WidgetAnimEditor;

	/** Sizes of the table: the key column, a value column, a value box in a one-key list, and the most a table grows to. */
	constexpr float KeyColumnWidth = 92.f;
	constexpr float ValueColumnWidth = 78.f;
	constexpr float SingleValueWidth = 120.f;
	constexpr float MaxTableHeight = 320.f;

	/** What was typed into a value box, kept by its number interface so the commit can work it out for every box picked with it. */
	struct FTyped
	{
		FString Expression;
		bool bFresh = false;
	};

	/**
	 * A value box's numbers: shown as numbers, typed as math (see WidgetAnimKeyValues::Evaluate) worked out against the box's own
	 * number. What was typed is kept in Typed - only when it was really typed: text left as it was shown (a click in and out)
	 * is no edit, and must not set every box picked with it to this one's number.
	 */
	class FInterface : public TDefaultNumericTypeInterface<double>
	{
	public:
		FInterface(TSharedRef<FTyped> InTyped, bool bInInteger)
			: Typed(InTyped)
			, bWhole(bInInteger)
		{
		}

		virtual FString ToString(const double& Number) const override
		{
			return bWhole ? FString::Printf(TEXT("%lld"), static_cast<long long>(FMath::RoundHalfFromZero(Number))) : TDefaultNumericTypeInterface<double>::ToString(Number);
		}

		virtual TOptional<double> FromString(const FString& InString, const double& InExistingValue) override
		{
			const bool bUnchanged = InString.TrimStartAndEnd() == ToString(InExistingValue).TrimStartAndEnd();
			Typed->Expression = InString;
			Typed->bFresh = !bUnchanged;
			return bUnchanged ? TOptional<double>(InExistingValue) : WidgetAnimKeyValues::Evaluate(InString, InExistingValue);
		}

		virtual bool IsCharacterValid(TCHAR InChar) const override
		{
			return FChar::IsDigit(InChar) || FCString::Strchr(TEXT(".,+-*/%^()=xXvVeE "), InChar) != nullptr;
		}

	private:
		TSharedRef<FTyped> Typed;
		bool bWhole = false;
	};

	const FSlateBrush* SelectedOutline()
	{
		static const FSlateRoundedBoxBrush Brush(Palette::KeyValueCellSelectedFill, 3.f, Palette::KeyValueCellSelected, 1.5f);
		return &Brush;
	}
}

/**
 * One value box of the list: a key's number of one channel. Ctrl+click picks it with others (Shift+click a run down its column);
 * typed into or dragged, it edits every box picked with it. See SWidgetAnimKeyValues.
 */
class SWidgetAnimValueCell : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimValueCell)
		: _Width(80.f)
		, _Dimmed(false)
	{}
		SLATE_ARGUMENT(float, Width)

		/** Shown though the key doesn't alter it (a key's list, or a track's with Show All Values): dimmed. */
		SLATE_ARGUMENT(bool, Dimmed)

		/** Every cell of this one's column in its table, top to bottom - what Shift+click picks a run of. */
		SLATE_ARGUMENT(TArray<SWidgetAnimTimeline::FValueCell>, Column)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<SWidgetAnimTimeline> InPanel, const SWidgetAnimTimeline::FValueCell& InCell)
	{
		using namespace WidgetAnimKeyValueCells;

		Panel = InPanel;
		Cell = InCell;
		Column = InArgs._Column;
		Typed = MakeShared<FTyped>();

		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		const TArray<WidgetAnimKeyValues::FChannel>* Channels = Owner ? &Owner->GetKeyChannels(Cell.Layer) : nullptr;
		const WidgetAnimKeyValues::FChannel* Channel = Channels && Channels->IsValidIndex(Cell.Channel) ? &(*Channels)[Cell.Channel] : nullptr;
		const double Step = Channel ? static_cast<double>(Channel->DragStep) : 0.1;
		const bool bWhole = Channel && Channel->bInteger;
		const FText Label = Channel ? Channel->Label : FText::GetEmpty();
		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Panel;
		const SWidgetAnimTimeline::FValueCell Shown = Cell;

		ChildSlot
		[
			SNew(SBox)
			.WidthOverride(InArgs._Width)
			[
				SNew(SOverlay)

				+ SOverlay::Slot()
				[
					SNew(SBox)
					.RenderOpacity(InArgs._Dimmed ? Palette::KeyValueAuthoredOpacity : 1.f)
					[
						SNew(SNumericEntryBox<double>)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.AllowSpin(true)
						.Delta(Step)
						.UndeterminedString(LOCTEXT("NoValue", "-"))
						.TypeInterface(MakeShared<FInterface>(Typed.ToSharedRef(), bWhole))
						.Value_Lambda([WeakOwner, Shown]() -> TOptional<double>
						{
							const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
							return Pinned ? Pinned->GetKeyValue(Shown) : TOptional<double>();
						})
						.IsEnabled_Lambda([WeakOwner, Shown]()
						{
							const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
							return Pinned && Pinned->CanSetKeyValue(Shown);
						})
						.ToolTipText_Lambda([WeakOwner, Shown, Label]()
						{
							const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
							FText WhyNot;
							if (Pinned && !Pinned->CanSetKeyValue(Shown, &WhyNot) && !WhyNot.IsEmpty())
							{
								return FText::Format(LOCTEXT("CellWhyNotFormat", "{0}\n{1}"), Label, WhyNot);
							}
							return FText::Format(Tips::KeyValueCellTooltip(), Label);
						})
						.OnValueCommitted(this, &SWidgetAnimValueCell::OnCommitted)
						.OnBeginSliderMovement(this, &SWidgetAnimValueCell::OnBeginDrag)
						.OnValueChanged(this, &SWidgetAnimValueCell::OnDragged)
						.OnEndSliderMovement(this, &SWidgetAnimValueCell::OnEndDrag)
					]
				]

				// Picked with others: framed, over a wash of the selection color. Never in the mouse's way.
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(SelectedOutline())
					.Visibility_Lambda([WeakOwner, Shown]()
					{
						const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
						return Pinned && Pinned->IsValueCellSelected(Shown) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
				]
			]
		];
	}

	/**
	 * Before the box sees the press: Ctrl / Cmd picks this box or puts it back, Shift picks the run down its column from the last
	 * box picked in it - and the box itself is left alone. A plain press on a box not picked picks it alone, then goes on to the
	 * box; on one of several picked, they stay picked, so typing or dragging edits them all.
	 */
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (!Owner || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}

		TArray<SWidgetAnimTimeline::FValueCell> Picked = Owner->GetSelectedValueCells();
		if (MouseEvent.IsControlDown() || MouseEvent.IsCommandDown())
		{
			if (Picked.Contains(Cell))
			{
				Picked.Remove(Cell);
			}
			else
			{
				// The one the box under the mouse holds now, first: a plain press picked it alone, Ctrl adds to that.
				Picked.Add(Cell);
			}
			Owner->SetSelectedValueCells(Picked);
			return FReply::Handled();
		}

		if (MouseEvent.IsShiftDown())
		{
			// From the last box picked in this column to this one.
			int32 From = INDEX_NONE;
			for (int32 Index = Picked.Num() - 1; Index >= 0 && From == INDEX_NONE; --Index)
			{
				From = Column.IndexOfByKey(Picked[Index]);
			}
			const int32 To = Column.IndexOfByKey(Cell);
			if (From == INDEX_NONE || To == INDEX_NONE)
			{
				Picked.AddUnique(Cell);
			}
			else
			{
				for (int32 Index = FMath::Min(From, To); Index <= FMath::Max(From, To); ++Index)
				{
					Picked.AddUnique(Column[Index]);
				}
			}
			Owner->SetSelectedValueCells(Picked);
			return FReply::Handled();
		}

		if (!Picked.Contains(Cell))
		{
			Owner->SetSelectedValueCells({ Cell });
		}
		return FReply::Unhandled();
	}

private:

	/** What an edit of this box edits: every box picked with it, when it is one of several picked - else itself alone. */
	TArray<SWidgetAnimTimeline::FValueCell> GetEditedCells() const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (Owner && Owner->IsValueCellSelected(Cell) && Owner->GetSelectedValueCells().Num() > 1)
		{
			return Owner->GetSelectedValueCells();
		}
		return { Cell };
	}

	/** Typed: what was typed worked out for each box edited, against its own number - "+8" adds 8 to each, "12" sets each to 12. */
	void OnCommitted(double Value, ETextCommit::Type CommitType)
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		const bool bTyped = Typed.IsValid() && Typed->bFresh;
		if (Typed.IsValid())
		{
			Typed->bFresh = false;
		}
		// The end of a drag commits too - it was set as it went - and a box left as it was is no edit.
		if (!Owner || !bTyped || CommitType == ETextCommit::OnCleared || Owner->IsDraggingKeyValues())
		{
			return;
		}

		const TArray<SWidgetAnimTimeline::FValueCell> Cells = GetEditedCells();
		TArray<SWidgetAnimTimeline::FValueCell> Set;
		TArray<double> Values;
		for (const SWidgetAnimTimeline::FValueCell& Edited : Cells)
		{
			if (Edited == Cell)
			{
				Set.Add(Edited);
				Values.Add(Value);
				continue;
			}
			const TOptional<double> Current = Owner->GetKeyValue(Edited);
			const TOptional<double> Worked = Current.IsSet() ? WidgetAnimKeyValues::Evaluate(Typed->Expression, Current.GetValue()) : TOptional<double>();
			if (Worked.IsSet())
			{
				Set.Add(Edited);
				Values.Add(Worked.GetValue());
			}
		}
		Owner->SetKeyValues(Set, Values);
	}

	/** Dragged: every box edited moves by what this one has moved since the press. One undo step for the drag. */
	void OnBeginDrag()
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (!Owner)
		{
			return;
		}
		Owner->BeginKeyValueDrag();
		DragCells = GetEditedCells();
		DragStarts.Reset();
		for (const SWidgetAnimTimeline::FValueCell& Edited : DragCells)
		{
			const TOptional<double> Current = Owner->GetKeyValue(Edited);
			DragStarts.Add(Current.Get(0.0));
		}
		DragSelf = DragCells.IndexOfByKey(Cell);
	}

	void OnDragged(double Value)
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (!Owner || !Owner->IsDraggingKeyValues() || !DragStarts.IsValidIndex(DragSelf))
		{
			return;
		}
		const double Moved = Value - DragStarts[DragSelf];
		TArray<double> Values;
		Values.Reserve(DragCells.Num());
		for (int32 Index = 0; Index < DragCells.Num(); ++Index)
		{
			Values.Add(Index == DragSelf ? Value : DragStarts[Index] + Moved);
		}
		Owner->SetKeyValues(DragCells, Values);
	}

	void OnEndDrag(double Value)
	{
		OnDragged(Value);
		if (const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin())
		{
			Owner->EndKeyValueDrag();
		}
		DragCells.Reset();
		DragStarts.Reset();
		DragSelf = INDEX_NONE;
	}

	TWeakPtr<SWidgetAnimTimeline> Panel;
	SWidgetAnimTimeline::FValueCell Cell;
	TArray<SWidgetAnimTimeline::FValueCell> Column;
	TSharedPtr<WidgetAnimKeyValueCells::FTyped> Typed;

	TArray<SWidgetAnimTimeline::FValueCell> DragCells;
	TArray<double> DragStarts;
	int32 DragSelf = INDEX_NONE;
};

// =====================================================================================================
// The list
// =====================================================================================================

void SWidgetAnimKeyValues::Construct(const FArguments& InArgs, TWeakPtr<SWidgetAnimTimeline> InPanel)
{
	using namespace WidgetAnimKeyValueCells;

	Panel = InPanel;
	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	const TArray<FWidgetAnimLayer>* Layers = Owner ? Owner->GetLayers() : nullptr;
	if (!Owner || !Layers)
	{
		ChildSlot[SNullWidget::NullWidget];
		return;
	}
	const bool bForTracks = InArgs._ForTracks;
	bListAll = !bForTracks || Owner->IsShowingAllKeyValues();
	const bool bShowAll = bListAll;

	// The keys by track, tracks in the order first given, keys in order along each.
	for (const SWidgetAnimTimeline::FKeyRef& Ref : InArgs._Keys)
	{
		if (!Layers->IsValidIndex(Ref.Layer) || Ref.Key < 0 || Ref.Key >= WidgetAnimKeyValues::GetNumKeys((*Layers)[Ref.Layer]))
		{
			continue;
		}
		FTrackPart* Part = Parts.FindByPredicate([&Ref](const FTrackPart& Candidate) { return Candidate.Layer == Ref.Layer; });
		if (!Part)
		{
			if (Parts.Num() >= MaxTracks)
			{
				++NumTracksLeftOut;
				continue;
			}
			Part = &Parts.AddDefaulted_GetRef();
			Part->Layer = Ref.Layer;
		}
		Part->Keys.AddUnique(Ref.Key);
	}

	// Which channels each track's list shows: those any of its keys alters (every one, with Show All Values), in the track's order.
	for (FTrackPart& Part : Parts)
	{
		Part.Keys.Sort();
		const FWidgetAnimLayer& Layer = (*Layers)[Part.Layer];
		const TArray<WidgetAnimKeyValues::FChannel>& Channels = Owner->GetKeyChannels(Part.Layer);

		TArray<TArray<bool>> Altered;
		Altered.SetNum(Part.Keys.Num());
		TArray<bool> AnyAltered;
		AnyAltered.Init(false, Channels.Num());
		for (int32 Row = 0; Row < Part.Keys.Num(); ++Row)
		{
			Altered[Row].Init(false, Channels.Num());
			for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
			{
				const bool bAltered = WidgetAnimKeyValues::IsAltered(Layer, Part.Keys[Row], Channels[ChannelIndex]);
				Altered[Row][ChannelIndex] = bAltered;
				AnyAltered[ChannelIndex] |= bAltered;
			}
		}
		for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
		{
			const bool bHolds = WidgetAnimKeyValues::Read(Layer, Part.Keys.Num() > 0 ? Part.Keys[0] : 0, Channels[ChannelIndex]).IsSet();
			if (AnyAltered[ChannelIndex] || (bShowAll && bHolds))
			{
				Part.Columns.Add(ChannelIndex);
			}
		}
		Part.Shown.SetNum(Part.Keys.Num());
		for (int32 Row = 0; Row < Part.Keys.Num(); ++Row)
		{
			for (const int32 ChannelIndex : Part.Columns)
			{
				Part.Shown[Row].Add(Altered[Row][ChannelIndex]);
			}
		}
	}

	// Rows for each table, made before any list points at them.
	PartRows.SetNum(Parts.Num());
	for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
	{
		for (int32 Row = 0; Row < Parts[PartIndex].Keys.Num(); ++Row)
		{
			const TSharedPtr<FRowItem> Item = MakeShared<FRowItem>();
			Item->Part = PartIndex;
			Item->Row = Row;
			PartRows[PartIndex].Add(Item);
		}
	}

	const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Panel;
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// A track's list: Show All Values. Keys picked list every value already.
	if (bForTracks)
	{
		Box->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 2.f, 0.f, 6.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.ToolTipText(Tips::ShowAllKeyValuesTooltip())
				.IsChecked_Lambda([WeakOwner]()
				{
					const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
					return Pinned && Pinned->IsShowingAllKeyValues() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakOwner](ECheckBoxState State)
				{
					if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
					{
						Pinned->SetShowAllKeyValues(State == ECheckBoxState::Checked);
					}
				})
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(LOCTEXT("ShowAllValues", "Show All Values"))
				]
			]
		];
	}

	for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
	{
		Box->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, PartIndex > 0 ? 10.f : 0.f, 0.f, 0.f))
		[
			MakePart(PartIndex)
		];
	}

	if (NumTracksLeftOut > 0)
	{
		Box->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(FText::Format(Tips::KeyValuesMoreTracksNote(), FText::AsNumber(NumTracksLeftOut), FText::AsNumber(MaxTracks)))
		];
	}

	ChildSlot
	[
		Box
	];
}

FText SWidgetAnimKeyValues::GetKeyName(int32 LayerIndex, int32 Key) const
{
	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	const TArray<FWidgetAnimLayer>* Layers = Owner ? Owner->GetLayers() : nullptr;
	if (!Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return FText::GetEmpty();
	}

	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
	const int32 NumKeys = WidgetAnimKeyValues::GetNumKeys(Layer);
	FText Name;
	if (Owner->IsDopeSheet())
	{
		// A Dope Sheet's keys are keys, counted from 1 - never a start and an end.
		Name = FText::Format(LOCTEXT("DopeKeyNumber", "Key {0}"), FText::AsNumber(Key + 1));
	}
	else if (!WidgetAnimTypeIsKeyframed(Layer.AnimType) && Key == 0)
	{
		Name = LOCTEXT("StartKey", "Start");
	}
	else if (!WidgetAnimTypeIsKeyframed(Layer.AnimType) && Key == NumKeys - 1)
	{
		Name = LOCTEXT("EndKey", "End");
	}
	else
	{
		// Counted from 1 as they are read: a keyframed track's first key is Key 1; a start-to-end track's first between is too.
		Name = FText::Format(LOCTEXT("KeyNumber", "Key {0}"), FText::AsNumber(WidgetAnimTypeIsKeyframed(Layer.AnimType) ? Key + 1 : Key));
	}

	TArray<float> KeyTimes;
	Owner->GetKeyTimes(LayerIndex, KeyTimes);
	return KeyTimes.IsValidIndex(Key)
		? FText::Format(LOCTEXT("KeyNameTime", "{0}  {1}"), Name, FText::FromString(Owner->FormatTime(KeyTimes[Key], 2)))
		: Name;
}

TSharedRef<SWidget> SWidgetAnimKeyValues::MakePart(int32 PartIndex)
{
	const FTrackPart& Part = Parts[PartIndex];
	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	const TArray<FWidgetAnimLayer>* Layers = Owner ? Owner->GetLayers() : nullptr;
	if (!Layers || !Layers->IsValidIndex(Part.Layer))
	{
		return SNullWidget::NullWidget;
	}

	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// Several tracks: each named over its keys.
	if (Parts.Num() > 1)
	{
		Box->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, 3.f))
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
			.Text(SWidgetAnimTimeline::GetLayerDisplayName((*Layers)[Part.Layer]))
		];
	}

	if (Part.Columns.Num() == 0)
	{
		Box->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(WidgetAnimKeyValues::IsKeyAuthored((*Layers)[Part.Layer], Part.Keys.Num() > 0 ? Part.Keys[0] : 0) && Part.Keys.Num() == 1
				? NameTBD::Tooltips::WidgetAnimEditor::KeyValuesAuthoredNote()
				: NameTBD::Tooltips::WidgetAnimEditor::KeyValuesNoneNote())
		];
		return Box;
	}

	Box->AddSlot()
	.AutoHeight()
	[
		Part.Keys.Num() == 1 && Parts.Num() == 1 ? MakeSingleKey(Part) : MakeTable(PartIndex)
	];
	return Box;
}

TSharedRef<SWidget> SWidgetAnimKeyValues::MakeSingleKey(const FTrackPart& Part)
{
	using namespace WidgetAnimKeyValueCells;

	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	const TArray<WidgetAnimKeyValues::FChannel>& Channels = Owner->GetKeyChannels(Part.Layer);
	const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	for (int32 Column = 0; Column < Part.Columns.Num(); ++Column)
	{
		const int32 ChannelIndex = Part.Columns[Column];
		const bool bShown = Part.Shown[0][Column];
		Box->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 1.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(bShown ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground())
				.Text(Channels.IsValidIndex(ChannelIndex) ? Channels[ChannelIndex].Label : FText::GetEmpty())
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeCell(Part, 0, Column, SingleValueWidth)
			]
		];
	}
	return Box;
}

TSharedRef<SWidget> SWidgetAnimKeyValues::MakeKeyLabel(const FTrackPart& Part, int32 Row) const
{
	using namespace WidgetAnimKeyValueCells;

	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	const TArray<FWidgetAnimLayer>* Layers = Owner ? Owner->GetLayers() : nullptr;
	const int32 LayerIndex = Part.Layer;
	const int32 Key = Part.Keys[Row];
	const bool bAuthored = Layers && Layers->IsValidIndex(LayerIndex) && WidgetAnimKeyValues::IsKeyAuthored((*Layers)[LayerIndex], Key);
	const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Panel;

	// Pressed, the playhead goes to the key - the preview shows it, as clicking the key on its track does.
	return SNew(SBox)
		.WidthOverride(KeyColumnWidth)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2.f, 1.f))
			.ToolTipText(bAuthored ? Tips::KeyValuesAuthoredNote() : Tips::KeyValuesKeyTooltip())
			.OnClicked_Lambda([WeakOwner, LayerIndex, Key]()
			{
				const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
				TArray<float> KeyTimes;
				if (Pinned)
				{
					Pinned->GetKeyTimes(LayerIndex, KeyTimes);
				}
				const TOptional<float> Start = Pinned ? Pinned->GetLayersStartTime({ LayerIndex }) : TOptional<float>();
				if (Pinned && Start.IsSet() && KeyTimes.IsValidIndex(Key))
				{
					Pinned->ScrubToSeconds(Start.GetValue() + KeyTimes[Key]);
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(bAuthored ? IDetailLayoutBuilder::GetDetailFontItalic() : IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(bAuthored ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground())
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Text(GetKeyName(LayerIndex, Key))
			]
		];
}

TSharedRef<SWidget> SWidgetAnimKeyValues::MakeCell(const FTrackPart& Part, int32 Row, int32 Column, float Width) const
{
	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	const bool bAltered = Part.Shown.IsValidIndex(Row) && Part.Shown[Row].IsValidIndex(Column) && Part.Shown[Row][Column];
	if (!Owner || (!bAltered && !bListAll))
	{
		// Not a value this key alters: nothing in its place, so each key reads as what it changes.
		return SNew(SBox).WidthOverride(Width);
	}

	SWidgetAnimTimeline::FValueCell Cell;
	Cell.Layer = Part.Layer;
	Cell.Key = Part.Keys[Row];
	Cell.Channel = Part.Columns[Column];

	// Its column, for Shift+click: the boxes shown in it, top to bottom.
	TArray<SWidgetAnimTimeline::FValueCell> ColumnCells;
	for (int32 Other = 0; Other < Part.Keys.Num(); ++Other)
	{
		if (bListAll || (Part.Shown.IsValidIndex(Other) && Part.Shown[Other].IsValidIndex(Column) && Part.Shown[Other][Column]))
		{
			SWidgetAnimTimeline::FValueCell& InColumn = ColumnCells.AddDefaulted_GetRef();
			InColumn.Layer = Part.Layer;
			InColumn.Key = Part.Keys[Other];
			InColumn.Channel = Cell.Channel;
		}
	}

	return SNew(SWidgetAnimValueCell, Panel, Cell)
		.Width(Width)
		.Dimmed(!bAltered)
		.Column(ColumnCells);
}

TSharedRef<SWidget> SWidgetAnimKeyValues::MakeTable(int32 PartIndex)
{
	using namespace WidgetAnimKeyValueCells;

	const FTrackPart& Part = Parts[PartIndex];
	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	const TArray<WidgetAnimKeyValues::FChannel>& Channels = Owner->GetKeyChannels(Part.Layer);
	const float TableWidth = KeyColumnWidth + ValueColumnWidth * Part.Columns.Num() + 4.f;

	// The header: each value's name over its column.
	const TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(KeyColumnWidth)
			.Padding(FMargin(4.f, 0.f))
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("KeyColumn", "Key"))
			]
		];
	for (const int32 ChannelIndex : Part.Columns)
	{
		const FText Label = Channels.IsValidIndex(ChannelIndex) ? Channels[ChannelIndex].Label : FText::GetEmpty();
		Header->AddSlot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(ValueColumnWidth)
			.Padding(FMargin(2.f, 0.f))
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ToolTipText(Label)
				.Text(Label)
			]
		];
	}

	// The rows - a list, so a track of hundreds of keys only makes the rows in view.
	const TSharedRef<SListView<TSharedPtr<FRowItem>>> List = SNew(SListView<TSharedPtr<FRowItem>>)
		.ListItemsSource(&PartRows[PartIndex])
		.SelectionMode(ESelectionMode::None)
		.OnGenerateRow_Lambda([this](TSharedPtr<FRowItem> Item, const TSharedRef<STableViewBase>& OwnerTable) -> TSharedRef<ITableRow>
		{
			const TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox);
			if (Item.IsValid() && Parts.IsValidIndex(Item->Part))
			{
				const FTrackPart& RowPart = Parts[Item->Part];
				RowBox->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeKeyLabel(RowPart, Item->Row)
				];
				for (int32 Column = 0; Column < RowPart.Columns.Num(); ++Column)
				{
					RowBox->AddSlot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(1.f))
					[
						MakeCell(RowPart, Item->Row, Column, ValueColumnWidth - 2.f)
					];
				}
			}
			return SNew(STableRow<TSharedPtr<FRowItem>>, OwnerTable)
				.Padding(FMargin(0.f, 1.f))
				[
					RowBox
				];
		});

	return SNew(SScrollBox)
		.Orientation(Orient_Horizontal)
		+ SScrollBox::Slot()
		[
			SNew(SBox)
			.WidthOverride(TableWidth)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 0.f, 0.f, 2.f))
				[
					Header
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.MaxDesiredHeight(MaxTableHeight)
					[
						List
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
