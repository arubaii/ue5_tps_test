// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/WidgetAnimation/WidgetAnimInput.h"

#include "FlowListInput.generated.h"

class UFlowList;

/** What a Flow List input reads off its list. */
UENUM(BlueprintType)
enum class EFlowListMeasure : uint8
{
	SelectedIndex UMETA(DisplayName = "Selected Index", ToolTip = "Which item is selected, counting from 0. -1 with none. An Integer or Choice input driven by it picks a state per item: a different character for each."),
	SelectedValue UMETA(DisplayName = "Selected Value", ToolTip = "The selected item's Item Value (set on its Flow List Slot), or its index when it has none. Unlike the index, it stays with the item when items are added, removed or moved."),
	ScrollPosition UMETA(DisplayName = "Scroll Position", ToolTip = "Where the list is, in items: 2.5 is halfway between the third and fourth. Follows the list as it moves, between items too."),
	ScrollProgress UMETA(DisplayName = "Scroll Progress", ToolTip = "0 at the first item, 1 at the last. Round a looping list, how far round it is."),
	ScrollSpeed UMETA(DisplayName = "Scroll Speed", ToolTip = "How fast the list is moving, in items per second: positive toward the end, 0 at rest."),
	IsScrolling UMETA(DisplayName = "Is Scrolling", ToolTip = "1 while the list is dragged, coasting or springing, 0 once it rests."),
	ItemCount UMETA(DisplayName = "Item Count", ToolTip = "How many items the list has."),
	ItemIndex UMETA(DisplayName = "Item Index", ToolTip = "Measured on an item, or something inside one: that item's index."),
	ItemDistance UMETA(DisplayName = "Item Distance", ToolTip = "Measured on an item, or something inside one: how far it is from the focus, in items - 0 focused, 1 the next, -1 the one before, fractions as the list moves."),
	ItemFocus UMETA(DisplayName = "Item Focus", ToolTip = "Measured on an item, or something inside one: 1 when it is at the focus, falling to 0 one item away - a selected look that blends in as the list moves, rather than switching."),
	IsItemSelected UMETA(DisplayName = "Is Item Selected", ToolTip = "Measured on an item, or something inside one: 1 while it is the selected item.")
};

/**
 * A Flow List's state, for a value track or a state machine input: which item is selected, where the list is, how fast it
 * moves - or, measured on an item, how far that item is from the focus. A state machine on a character portrait with an
 * Integer input Driven By a Flow List's Selected Index shows a different character for each item with no code; an item's
 * own animation Driven By Item Focus blends its selected look in as it nears the focus.
 *
 * Which list: for the Item measures, the list the widget measured is an item of. For the others, the one named List Name,
 * or with none the nearest: the list the widget is in, the widget itself, the first inside it, then outward through the
 * widgets around it (see UFlowList::FindNearestFlowList).
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Flow List"))
struct NAMETBD_API FWidgetAnimInput_FlowList : public FWidgetAnimInputSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EFlowListMeasure Measure = EFlowListMeasure::SelectedIndex;

	/** The Flow List to read, by its name in the Widget Blueprint. None: the nearest. Not used by the Item measures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "List Name"))
	FName ListName;

	virtual bool Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue) override;
	virtual FString GetSummary() const override;

private:
	/** The list read, found once for the widget measured on. */
	TWeakObjectPtr<UFlowList> List;
	TWeakObjectPtr<UWidget> SearchedFrom;
	FName SearchedName;
	bool bSearched = false;

	/** The scroll last frame, for Scroll Speed. */
	float LastScroll = 0.f;
	bool bHasLastScroll = false;
};
