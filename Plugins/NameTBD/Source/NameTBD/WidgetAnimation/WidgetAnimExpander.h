// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "WidgetAnimEasing.h" // EWidgetAnimEasing
#include "WidgetAnimParts.h"  // FWidgetAnimPartKey

#include "WidgetAnimExpander.generated.h"

class UCurveFloat;
class UWidget;

// =====================================================================================================
// Staggers (expanders)
// =====================================================================================================
//
// A track moves one widget. A Stagger makes it move many, one after another: the children of a list, the letters of a
// title. It is set on the track in the Widget Animations editor, next to its timing, and says two things - what the
// many are (each child, each letter, word or line) and how their starts are spread (so far apart, in what order).
//
// It fans out at play time, not when the animation is authored: every time the track starts, its stagger looks at the
// track's widget as it is right then - however many children a list was filled with, whatever string the label shows
// in the player's language - and the track becomes one ordinary copy per item, each aimed at its item and started its
// share of the spread later. From there on nothing knows there was a stagger: the copies loop, ping-pong, spring, hold
// and let go exactly as tracks do, and any track type works on them - a Fade on each child, a Count on the label
// inside each child.
//
// Letters, words and lines are not widgets. They are parts of a part host (see WidgetAnimParts.h), and only the tracks
// that move a widget's render transform and opacity - Fade, Scale, Slide, Rotate, Pulse and Transform - can move them.
// On any other type a text stagger does nothing, and the track plays on the whole widget as usual.
//
// Its place on the timeline: the track as authored is its first item. The items after it start later, so the track's
// span on the timeline runs on to where its last item ends, and a track after it set to After Previous waits for that
// last item. With Each, how far that is depends on how many items there turn out to be, so the Widget Animations editor
// counts them on the widget it previews on.
//
// Until its turn, an item holds its start pose (Hold Start Pose Until Its Turn): a letter fading in from nothing is
// invisible until its fade begins, rather than showing and then vanishing. That is unlike an unstaggered track, which
// leaves its widget alone until it starts.
//
// A game or licensee module adds a stagger of its own - each cell of a grid, each segment of a bar - by deriving a
// USTRUCT from FWidgetAnimExpander and overriding GatherItems. It is offered on every track, with no registration.

/** How a stagger spaces the starts of its items. */
UENUM(BlueprintType)
enum class EWidgetAnimStaggerSpacing : uint8
{
	Each UMETA(DisplayName = "Seconds Apart", ToolTip = "Each item starts a set time after the one before it, however many there are - a typewriter's steady pace. A longer text, or a fuller list, takes longer; a track after this one set to After Previous waits for the last item."),

	Across UMETA(DisplayName = "Spread Over", ToolTip = "The items' starts are spread over a set time, however many there are: two items start that far apart, twenty start much closer. The track always takes the same time on the timeline.")
};

/** Which item of a stagger goes first. */
UENUM(BlueprintType)
enum class EWidgetAnimStaggerOrder : uint8
{
	FirstToLast UMETA(DisplayName = "First To Last", ToolTip = "In order: the first child, the first letter, first."),

	LastToFirst UMETA(DisplayName = "Last To First", ToolTip = "In reverse order: the last item first."),

	FromCenter UMETA(DisplayName = "From The Middle", ToolTip = "The middle item first, spreading out to both ends; items the same distance from the middle start together."),

	FromEdges UMETA(DisplayName = "From Both Ends", ToolTip = "Both ends first, closing in on the middle."),

	Random UMETA(ToolTip = "In a shuffled order - the same shuffle every time for the same Seed and number of items.")
};

/** One item a stagger fans out to: a widget, or one part of it. */
struct NAMETBD_API FWidgetAnimExpandedItem
{
	UWidget* Widget = nullptr;

	/** Unset for a whole widget. See WidgetAnimParts.h. */
	FWidgetAnimPartKey Part;
};

/**
 * What a track's Stagger fans it out into, and how its items' starts are spread. The base is never used itself: pick
 * one of the staggers that derive from it. See the note at the top of WidgetAnimExpander.h.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimExpander
{
	GENERATED_BODY()

	virtual ~FWidgetAnimExpander() = default;

	/** Whether the items' starts are a set time apart, or spread over a set time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger", meta = (DisplayName = "Spacing"))
	EWidgetAnimStaggerSpacing Spacing = EWidgetAnimStaggerSpacing::Each;

	/** Seconds between one item's start and the next one's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger", meta = (DisplayName = "Seconds Apart", ClampMin = "0.0", UIMax = "0.5", EditCondition = "Spacing == EWidgetAnimStaggerSpacing::Each", EditConditionHides))
	float Interval = 0.05f;

	/** Seconds from the first item's start to the last one's, however many items there are. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger", meta = (DisplayName = "Spread Over", ClampMin = "0.0", UIMax = "3.0", EditCondition = "Spacing == EWidgetAnimStaggerSpacing::Across", EditConditionHides))
	float Spread = 0.4f;

	/** Which item goes first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger")
	EWidgetAnimStaggerOrder Order = EWidgetAnimStaggerOrder::FirstToLast;

	/** Picks the shuffle. Change it for another order; the same Seed always gives the same order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger", meta = (EditCondition = "Order == EWidgetAnimStaggerOrder::Random", EditConditionHides))
	int32 Seed = 0;

	/**
	 * How the starts are spread along the way from the first item to the last. Linear keeps them evenly apart. Ease In
	 * starts them slowly and bunches the last ones together; Ease Out bunches the first ones and lets the last trail.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger", meta = (DisplayName = "Spacing Curve"))
	EWidgetAnimEasing SpacingEasing = EWidgetAnimEasing::Linear;

	/** A Curve asset from 0 to 1 that spreads the starts, for a Spacing Curve of Custom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger", meta = (DisplayName = "Custom Spacing Curve", EditCondition = "SpacingEasing == EWidgetAnimEasing::Custom", EditConditionHides))
	TObjectPtr<UCurveFloat> SpacingCurve = nullptr;

	/**
	 * Each item shows the track's start pose until its turn comes: a letter fading in from nothing stays invisible until
	 * its fade begins. Off, an item is left as it is until its turn, as a track without a stagger is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger", meta = (DisplayName = "Hold Start Pose Until Its Turn"))
	bool bHoldStartPose = true;

	/**
	 * The items to fan out to on Target - the widget the track moves: its Layer Group's target, or the widget the
	 * animation plays on. Read every time the track starts, so it sees the widget as it is then.
	 */
	virtual void GatherItems(UWidget* Target, TArray<FWidgetAnimExpandedItem>& OutItems) const {}

	/**
	 * Whether GatherItems can be trusted on Target yet. False only while what it counts isn't known yet - the lines of a
	 * wrapping text that has never been given a width. The track waits up to a few frames for it before fanning out; the
	 * widget has never been drawn in that time, so nothing on screen waits.
	 */
	virtual bool IsReadyToExpand(UWidget* Target) const { return true; }

	/**
	 * Whether its items are parts of a widget rather than widgets. Only tracks that move a render transform and opacity
	 * can move a part; on other types such a stagger is left out and the track plays on the whole widget.
	 */
	virtual bool GathersParts() const { return false; }

	/** A few words naming what it fans out to, where the track is listed - "Each Child", "Each Letter". */
	virtual FString GetSummary() const { return FString(); }

	/**
	 * Seconds after the track's own start that each of NumItems items starts, by its place in the list GatherItems
	 * gave. The first to go is at 0.
	 */
	void ComputeOffsets(int32 NumItems, TArray<float>& OutOffsets) const;

	/** Seconds from the first item's start to the last one's, for NumItems items. 0 for one item or none. */
	float GetFanSeconds(int32 NumItems) const;

	/** How the starts are spaced, in words - "0.05 s apart, from the middle". */
	FString DescribeSpacing() const;
};

/** One copy of the track for each child of its widget: a list's rows, a grid's tiles, a menu's buttons. */
USTRUCT(BlueprintType, meta = (DisplayName = "Each Child"))
struct NAMETBD_API FWidgetAnimExpander_EachChild : public FWidgetAnimExpander
{
	GENERATED_BODY()

	/**
	 * Leaves out children that are Collapsed or Hidden, so a list's unused slots don't take a turn. Off, every child
	 * takes its turn, shown or not.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger", meta = (DisplayName = "Skip Hidden Children"))
	bool bSkipHidden = true;

	/**
	 * The children of the track's widget: of a panel, its children; of a User Widget, the children of the panel at its
	 * root; of a List View or Tile View, the entry widgets it is showing right now. A widget with none - a plain Image -
	 * has none, and the track then plays on nothing.
	 */
	virtual void GatherItems(UWidget* Target, TArray<FWidgetAnimExpandedItem>& OutItems) const override;
	virtual FString GetSummary() const override { return TEXT("Each Child"); }
};

/** Which parts of a text a text stagger fans out to. */
UENUM(BlueprintType)
enum class EWidgetAnimTextUnit : uint8
{
	Letter UMETA(ToolTip = "Every letter - white space is never a letter, so the gaps between words take no turn."),

	Word UMETA(ToolTip = "Every word: a run of letters with no white space in it, punctuation included."),

	Line UMETA(ToolTip = "Every line, as the text is laid out - wrapping decides where one ends.")
};

/**
 * One copy of the track for each letter, word or line of an Animated Text Block: a title whose letters drop in one by
 * one, a paragraph revealed a line at a time. Needs an Animated Text Block - a plain Text Block draws its text as one
 * piece and has no letters to move; see UWidgetAnimTextBlock.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Each Letter, Word or Line"))
struct NAMETBD_API FWidgetAnimExpander_EachTextPart : public FWidgetAnimExpander
{
	GENERATED_BODY()

	FWidgetAnimExpander_EachTextPart()
	{
		// A letter is small: a list's pace would make a title crawl.
		Interval = 0.03f;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stagger")
	EWidgetAnimTextUnit Unit = EWidgetAnimTextUnit::Letter;

	/**
	 * The parts of the track's widget when it is an Animated Text Block, or else of the first one inside it, depth
	 * first - so a track aimed at a whole button staggers the letters of its label.
	 */
	virtual void GatherItems(UWidget* Target, TArray<FWidgetAnimExpandedItem>& OutItems) const override;
	virtual bool IsReadyToExpand(UWidget* Target) const override;
	virtual bool GathersParts() const override { return true; }
	virtual FString GetSummary() const override;

	/** The part unit the Animated Text Block knows Unit by. See WidgetAnimPartUnits. */
	FName GetPartUnit() const;
};

namespace WidgetAnimExpand
{
	/**
	 * The part host a text stagger aimed at Target uses: Target itself when it is one, or else the first one inside it,
	 * depth first, through panels and into User Widgets. Null when there is none.
	 */
	NAMETBD_API UWidget* FindPartHost(UWidget* Target);
}
