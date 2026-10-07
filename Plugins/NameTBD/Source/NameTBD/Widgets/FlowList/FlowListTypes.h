// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Types/SlateEnums.h"

#include "NameTBD/WidgetAnimation/WidgetAnimEasing.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h" // FWidgetAnimEntryBase
#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"

#include "FlowListTypes.generated.h"

// =====================================================================================================
// How a Flow List thinks
// =====================================================================================================
//
// A native Scroll Box moves a stack of widgets by an offset in pixels. The lists in stylized games - Persona's
// dials and slanted menus, a weather strip that tapers and curls, a cover flow, an iOS date picker, a ring of
// characters to pick from - are built on a different idea, the one Flutter's ListWheelScrollView, Rive's Scroll
// constraint with Carousel mode and every console menu share:
//
//   1. The scroll position is in ITEMS, not pixels. 2.4 means "two fifths of the way from item 2 to item 3".
//      Snapping is rounding, looping is wrapping, Scroll To Index is a number, and a list of 5 and a list of 500
//      feel identical under the thumb.
//   2. Every item has a signed DISTANCE from the focus: 0 is the focused item, 1 the next, -1 the one before, 0.5
//      halfway between. Everything about how an item looks - its size, fade, tint, turn, how far it juts out - is a
//      function of that one number (the Effects channels below). That is what makes a list taper, curl or wheel.
//   3. Items are laid along a PATH by how far they are from the focus. A line, an arc, a drum, a ring or a hand
//      drawn spline - the same items, scrolled the same way, read completely differently.
//   4. Motion is physical. Drags follow the finger with a rubber band past the ends, flings coast and pick the item
//      they would have stopped near, and every arrival is a spring that keeps the velocity it was given - the trick
//      that makes iOS paging feel like one continuous motion instead of a flick followed by an animation.
//   5. Items arriving, leaving and changing places animate. Removing the front item of a push/pop list lets it
//      leave while the rest slide up into its place, each growing into its new distance from the focus.

/** The shape items are laid along. */
UENUM(BlueprintType)
enum class EFlowListPathType : uint8
{
	Line UMETA(ToolTip = "A straight line through the focus, along the Orientation, tilted by Angle. With Stop At Content End it is a scroll box."),
	Arc UMETA(ToolTip = "A circle touching the line at its apex: the list curls away toward one side. Persona-style dials and curling lists."),
	Wheel UMETA(ToolTip = "A drum seen from the front, like a date picker: items squash and shrink as they turn away, and vanish over the top."),
	Ring UMETA(ToolTip = "A ring seen from slightly above, like a character select carousel: items go round behind the focus, smaller, fainter and higher."),
	Spline UMETA(ToolTip = "A curve through Spline Points, drawn as fractions of the list's size. Any path a designer can sketch.")
};

/** How an Effects channel turns an item's distance from the focus into a value. */
UENUM(BlueprintType)
enum class EFlowListChannelMode : uint8
{
	Off UMETA(ToolTip = "The channel does nothing."),
	Falloff UMETA(ToolTip = "At Focus on the focused item, easing to At Edge by Distance items away."),
	Curve UMETA(ToolTip = "Read straight from Curve: X is the signed distance from the focus in items (negative before it), Y the value.")
};

/** What a Falloff channel does on the items before the focus. */
UENUM(BlueprintType)
enum class EFlowListMirror : uint8
{
	Same UMETA(ToolTip = "The same as the items after it: a symmetric falloff."),
	Opposite UMETA(ToolTip = "The other way round: what pushes items after the focus one way pushes the items before it the other. For Spread, Rotation and Cross Offset."),
	Separate UMETA(ToolTip = "Its own At Edge and Distance - a list whose passed items fade out fast while the coming ones only dim.")
};

/** Which items draw on top where they overlap. */
UENUM(BlueprintType)
enum class EFlowListZOrder : uint8
{
	FocusOnTop UMETA(DisplayName = "Focus On Top", ToolTip = "The nearer an item is to the focus, the higher it draws. Cover flows, stacks, carousels."),
	FirstOnTop UMETA(DisplayName = "First On Top", ToolTip = "Earlier items draw over later ones."),
	LastOnTop UMETA(DisplayName = "Last On Top", ToolTip = "Later items draw over earlier ones, like a plain list."),
	Depth UMETA(ToolTip = "Nearer the viewer draws on top - for the Wheel and Ring paths. Falls back to Focus On Top on flat paths.")
};

/** What happens when a drag or fling runs past either end. */
UENUM(BlueprintType)
enum class EFlowListOverscroll : uint8
{
	Elastic UMETA(ToolTip = "Rubber-bands past the end with growing resistance and springs back, as on iOS."),
	Clamp UMETA(ToolTip = "Stops dead at the end.")
};

/** The gamepad stick that Stick Scrolls reads. */
UENUM(BlueprintType)
enum class EFlowListStick : uint8
{
	LeftStick UMETA(DisplayName = "Left Stick"),
	RightStick UMETA(DisplayName = "Right Stick")
};

/** Starting points for the look of a Flow List. Picking one overwrites Layout, Effects, Scrolling and Transitions. */
UENUM(BlueprintType)
enum class EFlowListStylePreset : uint8
{
	Custom UMETA(ToolTip = "Leaves everything as it is."),
	ScrollList UMETA(DisplayName = "Scroll List", ToolTip = "A vertical scroll box with physics: free scrolling, coasting flings, rubber-band ends, stops at the end of its content."),
	Carousel UMETA(ToolTip = "A horizontal carousel snapping to each item, neighbours smaller and dimmer."),
	CoverFlow UMETA(DisplayName = "Cover Flow", ToolTip = "Overlapping covers squashed to the sides, a gap opened around the focused one."),
	PickerWheel UMETA(DisplayName = "Picker Wheel", ToolTip = "A vertical drum like a date picker."),
	Dial UMETA(ToolTip = "A looping arc that turns like a TV dial - Persona's command menus. The focused item juts out."),
	Cascade UMETA(ToolTip = "Items taper and curl down away from the focus and fade out once passed - a push/pop list like a weather strip."),
	Ring UMETA(ToolTip = "A looping ring of items going round behind the focus."),
	Slanted UMETA(ToolTip = "A tilted menu whose focused item juts out, larger and brighter - Persona 5 style."),
	Stack UMETA(ToolTip = "A deck: the focused item in front, the next ones peeking out behind it, passed ones flying off.")
};

// =====================================================================================================
// Effects
// =====================================================================================================

/**
 * One value an item takes from its distance to the focus: its scale, its opacity, how far it juts out.
 *
 * Distances are in items and signed: 0 is the focused item, 1 the next, -1 the one before, and everything in
 * between while the list moves. A Falloff eases from At Focus to At Edge over Distance items and holds there; a
 * Curve is read straight from its keys.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FFlowListChannel
{
	GENERATED_BODY()

	FFlowListChannel() = default;
	FFlowListChannel(float InAtFocus, float InAtEdge, float InDistance)
		: AtFocus(InAtFocus), AtEdge(InAtEdge), Distance(InDistance), BeforeAtEdge(InAtEdge), BeforeDistance(InDistance)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel")
	EFlowListChannelMode Mode = EFlowListChannelMode::Off;

	/** The value on the focused item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (DisplayName = "At Focus", EditCondition = "Mode == EFlowListChannelMode::Falloff", EditConditionHides))
	float AtFocus = 0.f;

	/** The value Distance items from the focus, and beyond (unless Extrapolate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (DisplayName = "At Edge", EditCondition = "Mode == EFlowListChannelMode::Falloff", EditConditionHides))
	float AtEdge = 0.f;

	/** How many items from the focus the value takes to get from At Focus to At Edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (ClampMin = "0.01", UIMin = "0.25", UIMax = "8", EditCondition = "Mode == EFlowListChannelMode::Falloff", EditConditionHides))
	float Distance = 3.f;

	/** How it gets there. Ease Out changes fast next to the focus and settles toward the edge - the usual taper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (EditCondition = "Mode == EFlowListChannelMode::Falloff", EditConditionHides))
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;

	/** The 0..1 shape a Custom easing follows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (EditCondition = "Mode == EFlowListChannelMode::Falloff && Easing == EWidgetAnimEasing::Custom", EditConditionHides))
	TObjectPtr<UCurveFloat> EasingCurve = nullptr;

	/** What happens on the items before the focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (EditCondition = "Mode == EFlowListChannelMode::Falloff", EditConditionHides))
	EFlowListMirror Mirror = EFlowListMirror::Same;

	/** At Edge for the items before the focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (DisplayName = "Before: At Edge", EditCondition = "Mode == EFlowListChannelMode::Falloff && Mirror == EFlowListMirror::Separate", EditConditionHides))
	float BeforeAtEdge = 0.f;

	/** Distance for the items before the focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (DisplayName = "Before: Distance", ClampMin = "0.01", UIMin = "0.25", UIMax = "8", EditCondition = "Mode == EFlowListChannelMode::Falloff && Mirror == EFlowListMirror::Separate", EditConditionHides))
	float BeforeDistance = 3.f;

	/**
	 * Keeps going past Distance at the rate it had on average, instead of holding At Edge: a Cross Offset of 0 to 20 over
	 * one item puts the fifth item 100 out, so a list leans further the further it goes. Ignores Easing past Distance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (EditCondition = "Mode == EFlowListChannelMode::Falloff", EditConditionHides))
	bool bExtrapolate = false;

	/** X: signed distance from the focus in items. Y: the value. Anything outside the keys holds the nearest key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (EditCondition = "Mode == EFlowListChannelMode::Curve", EditConditionHides, XAxisName = "Distance (items)", YAxisName = "Value"))
	FRuntimeFloatCurve Curve;

	bool IsActive() const { return Mode != EFlowListChannelMode::Off; }

	/** The value at SignedDistance items from the focus - Default when the channel is off. */
	float Evaluate(float SignedDistance, float Default) const;
};

/** A color an item takes from its distance to the focus - multiplied into everything it draws. */
USTRUCT(BlueprintType)
struct NAMETBD_API FFlowListColorChannel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (DisplayName = "Enabled"))
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (DisplayName = "At Focus", EditCondition = "bEnabled"))
	FLinearColor AtFocus = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (DisplayName = "At Edge", EditCondition = "bEnabled"))
	FLinearColor AtEdge = FLinearColor(0.45f, 0.45f, 0.5f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (ClampMin = "0.01", UIMin = "0.25", UIMax = "8", EditCondition = "bEnabled"))
	float Distance = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (EditCondition = "bEnabled"))
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;

	/** The 0..1 shape a Custom easing follows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel", meta = (EditCondition = "bEnabled && Easing == EWidgetAnimEasing::Custom", EditConditionHides))
	TObjectPtr<UCurveFloat> EasingCurve = nullptr;

	FLinearColor Evaluate(float SignedDistance) const;
};

/**
 * A lens that follows the pointer, as a dock's icons swell under the cursor: items near the pointer grow, and - with Spacing
 * Follows Scale - push their neighbours aside to make room. It eases in as the pointer comes over the list and out as it
 * leaves. Distances along the list are measured through the path's direction at the focus, so it is exact on a Line and
 * close on a gentle Arc.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FFlowListMagnify
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnify", meta = (DisplayName = "Enabled"))
	bool bEnabled = false;

	/** The scale of an item right under the pointer, multiplied with everything else that scales it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnify", meta = (ClampMin = "0.1", UIMin = "1", UIMax = "2.5", EditCondition = "bEnabled"))
	float Scale = 1.4f;

	/** How far from the pointer the lens reaches, in Slate units along the list. Items further away aren't touched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnify", meta = (ClampMin = "1", UIMin = "40", UIMax = "600", EditCondition = "bEnabled"))
	float Radius = 160.f;

	/** The lens's profile from the pointer out to Radius. Ease In Out is the round, dock-like swell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnify", meta = (EditCondition = "bEnabled"))
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseInOut;

	/** How long it takes to swell as the pointer arrives and settle as it leaves, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnify", meta = (ClampMin = "0", UIMin = "0", UIMax = "0.5", Units = "s", EditCondition = "bEnabled"))
	float FadeTime = 0.15f;

	/** Only while the list isn't being dragged or flung: the lens holds still under a moving list instead of sliding along it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnify", meta = (DisplayName = "Pause While Scrolling", EditCondition = "bEnabled"))
	bool bPauseWhileScrolling = true;
};

/**
 * Everything about an item that follows from where it is relative to the focus. Each channel is off until given a
 * Mode, so a list starts as plain as a scroll box and gets its character one channel at a time.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FFlowListEffects
{
	GENERATED_BODY()

	/** Uniform scale. Text is laid out at the scaled size, so it stays crisp as items taper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	FFlowListChannel Scale = FFlowListChannel(1.f, 0.8f, 2.f);

	/**
	 * Scale along the list only - an item squashed side-on, the cheap and convincing half of a cover flow's 3D turn.
	 * Multiplies with Scale.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	FFlowListChannel Squash = FFlowListChannel(1.f, 0.6f, 1.f);

	/** 0 to 1, multiplied with the item's own opacity. An item at 0 is not drawn and can't be clicked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	FFlowListChannel Opacity = FFlowListChannel(1.f, 0.4f, 3.f);

	/** Degrees, clockwise, about the item's pivot. Added to Align To Path's turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	FFlowListChannel Rotation = FFlowListChannel(0.f, 0.f, 3.f);

	/**
	 * Slate units across the path: right of a vertical list, below a horizontal one. On a curve it turns with the path, so
	 * on an Arc it points away from the circle's centre when Arc Radius is negative and toward it when positive. A
	 * focused item that juts out, or a list that leans.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (DisplayName = "Cross Offset"))
	FFlowListChannel CrossOffset = FFlowListChannel(0.f, 0.f, 1.f);

	/**
	 * Slate units along the path, added after spacing. With Mirror set to Opposite it opens a gap around the focused item
	 * (positive) or gathers its neighbours in (negative) - a cover flow's breathing room.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	FFlowListChannel Spread = FFlowListChannel(0.f, 0.f, 1.f);

	/** A color multiplied into each item: dim the ones away from the focus, or warm the focused one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	FFlowListColorChannel Tint;

	/** Items swell under the pointer, as a dock's do. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (DisplayName = "Hover Magnify"))
	FFlowListMagnify Magnify;

	/**
	 * Whether the gaps between items shrink as they do. On, a tapering list stays packed - each item sits against the
	 * last at its own size, which is what makes a taper read as a curve. Off, items shrink in place on an even pitch.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	bool bSpacingFollowsScale = true;

	/** Fades items out over this many Slate units as they near the list's ends along its length. 0 for a hard edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (ClampMin = "0", UIMin = "0", UIMax = "200"))
	float EdgeFade = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (DisplayName = "Z Order"))
	EFlowListZOrder ZOrder = EFlowListZOrder::FocusOnTop;

	/** Items further than this from the focus are not drawn at all, in items. 0 draws as many as fit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", AdvancedDisplay, meta = (ClampMin = "0", UIMin = "0", UIMax = "20"))
	float CullDistance = 0.f;
};

// =====================================================================================================
// Layout
// =====================================================================================================

/** Where the focus is, which way the list runs, and the path its items follow. */
USTRUCT(BlueprintType)
struct NAMETBD_API FFlowListLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (DisplayName = "Path"))
	EFlowListPathType Type = EFlowListPathType::Line;

	/** Which way the list runs, and which of an item's sides is its length. Keys and sticks navigate along it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	TEnumAsByte<EOrientation> Orientation = Orient_Horizontal;

	/** Tilts the whole path, in degrees clockwise - a slanted menu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (UIMin = "-90", UIMax = "90", ForceUnits = "Degrees"))
	float Angle = 0.f;

	/** Where the focused item sits, as a fraction of the list's size. 0.5, 0.5 is the middle; 0.5, 0 the top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (UIMin = "0", UIMax = "1"))
	FVector2D FocusAnchor = FVector2D(0.5, 0.5);

	/** Moves the focus point by this many Slate units after Focus Anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	FVector2D FocusOffset = FVector2D::ZeroVector;

	/** The point of each item that sits on the path, as a fraction of its size - and what it scales and turns about. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (UIMin = "0", UIMax = "1"))
	FVector2D ItemPivot = FVector2D(0.5, 0.5);

	/** Turns each item to follow the path's direction where it is: items on an arc tilt like spokes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (DisplayName = "Align To Path"))
	bool bAlignToPath = false;

	/**
	 * The circle's radius, in Slate units. Positive puts the circle's centre on the cross side - below a horizontal list
	 * (an arch) or right of a vertical one - so the list's ends curl that way. Negative curls them the other way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Arc", meta = (UIMin = "-2000", UIMax = "2000", EditCondition = "Type == EFlowListPathType::Arc", EditConditionHides))
	float ArcRadius = 500.f;

	/**
	 * Where along the path the arc is straightest, in Slate units from the focus. 0 curls both ends away from the focus
	 * evenly; 200 lets the list run on past the focus for a while before it turns back, so the focused item sits on the
	 * shoulder of the curve rather than its apex.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Arc", meta = (UIMin = "-1000", UIMax = "1000", EditCondition = "Type == EFlowListPathType::Arc", EditConditionHides))
	float ArcApex = 0.f;

	/** The drum's radius, in Slate units. Items further than a quarter turn from the focus are behind it and not drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Wheel", meta = (ClampMin = "1", UIMin = "50", UIMax = "800", EditCondition = "Type == EFlowListPathType::Wheel", EditConditionHides))
	float WheelRadius = 180.f;

	/** The ring's width (X) and how much higher its far side sits than its near side (Y), in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Ring", meta = (EditCondition = "Type == EFlowListPathType::Ring", EditConditionHides))
	FVector2D RingRadius = FVector2D(320.0, 60.0);

	/** Spaces the items evenly round the whole ring, however many there are. Off, they are spaced by their size and Spacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Ring", meta = (DisplayName = "Fit Ring To Items", EditCondition = "Type == EFlowListPathType::Ring", EditConditionHides))
	bool bFitRingToItems = true;

	/** The opacity of an item at the far side of the ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Ring", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "Type == EFlowListPathType::Ring", EditConditionHides))
	float RingBackOpacity = 0.35f;

	/** How much smaller items get as they turn away, per Slate unit of depth. Wheel and Ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path", meta = (ClampMin = "0", UIMin = "0", UIMax = "0.01", EditCondition = "Type == EFlowListPathType::Wheel || Type == EFlowListPathType::Ring", EditConditionHides))
	float Perspective = 0.0015f;

	/**
	 * The curve's points, as fractions of the list's size (0,0 its top left, 1,1 its bottom right). It passes through each.
	 * Easiest edited on the designer's canvas: select the list and click Edit Path above it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Spline", meta = (EditCondition = "Type == EFlowListPathType::Spline", EditConditionHides))
	TArray<FVector2D> SplinePoints = { FVector2D(0.1, 0.85), FVector2D(0.35, 0.45), FVector2D(0.65, 0.35), FVector2D(0.9, 0.1) };

	/** Where along the curve the focus is, as a fraction of its length. Replaces Focus Anchor on a spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Spline", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "Type == EFlowListPathType::Spline", EditConditionHides))
	float SplineFocus = 0.5f;

	/** The gap between neighbouring items, in Slate units. Negative overlaps them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing", meta = (UIMin = "-200", UIMax = "200"))
	float Spacing = 16.f;

	/**
	 * Every item's size. 0 on an axis uses each item's own desired size there. Uniform items make the most even
	 * carousels; a slot's Size Override still wins.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing")
	FVector2D ItemSize = FVector2D::ZeroVector;

	/** The distance from one item's pivot to the next, replacing sizes and Spacing. 0 works it out from them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing", meta = (DisplayName = "Pitch Override", ClampMin = "0", UIMin = "0", UIMax = "400"))
	float Pitch = 0.f;

	/** The size the list asks for when its slot sizes it to its content. 0 on an axis measures the items end to end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spacing", AdvancedDisplay)
	FVector2D DesiredSize = FVector2D::ZeroVector;
};

// =====================================================================================================
// Scrolling
// =====================================================================================================

/** How the list moves: by drag, wheel, keys, sticks and code. */
USTRUCT(BlueprintType)
struct NAMETBD_API FFlowListScrolling
{
	GENERATED_BODY()

	FFlowListScrolling();

	/** Always comes to rest on an item. Off, it stops wherever a fling runs out, like a scroll box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling", meta = (DisplayName = "Snap To Items"))
	bool bSnapToItems = true;

	/** Goes round forever: after the last item comes the first. Needs at least two items. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling", meta = (DisplayName = "Loop"))
	bool bLoop = false;

	/**
	 * Line path only: stops scrolling once the last item's far edge reaches the far edge of the list, rather than when
	 * the last item reaches the focus - what a scroll box does. A list whose items all fit doesn't scroll at all. Put the
	 * focus at the start (Focus Anchor and Item Pivot 0 along the list) for a top-aligned list.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling", meta = (DisplayName = "Stop At Content End"))
	bool bStopAtContentEnd = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Drag", meta = (DisplayName = "Drag To Scroll"))
	bool bDragToScroll = true;

	/** Items moved per item's length dragged. Above 1 a drag covers more ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Drag", meta = (ClampMin = "0.05", UIMin = "0.25", UIMax = "4", EditCondition = "bDragToScroll"))
	float DragSensitivity = 1.f;

	/**
	 * How far a fling carries, as the seconds its speed would keep it going. A fling's resting place is its release
	 * speed times this - 0.35 is a brisk native feel, 0.6 slides a long way, 0.1 barely coasts. With Snap To Items the
	 * list springs to the item nearest that resting place, so a hard fling skips many items and a flick moves one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Drag", meta = (ClampMin = "0", UIMin = "0", UIMax = "1", Units = "s", EditCondition = "bDragToScroll"))
	float FlingCoast = 0.35f;

	/** The most items one fling may move. 1 pages through one item per swipe, like a story viewer. 0 for no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Drag", meta = (ClampMin = "0", UIMin = "0", UIMax = "10", EditCondition = "bDragToScroll && bSnapToItems"))
	int32 MaxItemsPerFling = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Drag")
	EFlowListOverscroll Overscroll = EFlowListOverscroll::Elastic;

	/** How readily an elastic end gives: higher stretches further for the same drag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Drag", meta = (ClampMin = "0.05", ClampMax = "1", EditCondition = "Overscroll == EFlowListOverscroll::Elastic", EditConditionHides))
	float Elasticity = 0.55f;

	/**
	 * How the list arrives wherever it is sent - a snap after a drag, a wheel notch, a key press, Scroll To Index. It
	 * keeps the speed it already had, so a fling flows straight into its snap and a held key glides.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling")
	FWidgetSpringParams ScrollSpring;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Wheel", meta = (DisplayName = "Wheel Scroll"))
	bool bWheelScroll = true;

	/** Items per wheel notch. Fractions add up across notches, so a trackpad scrolls smoothly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Wheel", meta = (ClampMin = "0.05", UIMin = "0.25", UIMax = "5", EditCondition = "bWheelScroll"))
	float WheelStep = 1.f;

	/**
	 * Keys and sticks along the list move one item - a stick scrolls instead with Stick Scrolls. At either end, navigation
	 * leaves the list in that direction, so a button below a vertical list is one press past its last item.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Navigation", meta = (DisplayName = "Navigation"))
	bool bNavigation = true;

	/** At either end, navigation jumps to the other end instead of leaving the list. Looping lists always go round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Navigation", meta = (DisplayName = "Wrap Navigation", EditCondition = "bNavigation"))
	bool bWrapNavigation = false;

	/**
	 * While the list holds keyboard or gamepad focus, focus moves to whichever item the list settles on or passes, so the
	 * pad and the mouse never disagree about which item is current.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Navigation", meta = (DisplayName = "Focus Follows Selection"))
	bool bFocusFollowsSelection = true;

	/**
	 * The stick along the list scrolls it smoothly, faster the further it is pushed and - with Stick Acceleration Time -
	 * the longer it is held, instead of stepping an item at a time as navigation. Letting go settles on the item it was
	 * passing or the next one ahead (Snap To Items), or glides to a stop like a fling. Only while the list or one of its
	 * items has focus, and only the axis along the list: the other one still navigates out of it. While it scrolls, that
	 * axis of that stick goes to the list alone - nothing behind the UI sees it. Whether it selects as it goes is Select
	 * While Scrolling. The D-pad and arrow keys still step.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Stick", meta = (DisplayName = "Stick Scrolls"))
	bool bStickScrolls = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Stick", meta = (EditCondition = "bStickScrolls"))
	EFlowListStick Stick = EFlowListStick::LeftStick;

	/** Items a second with the stick pushed all the way, before any acceleration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Stick", meta = (ClampMin = "0.1", UIMin = "1", UIMax = "20", EditCondition = "bStickScrolls"))
	float StickSpeed = 5.f;

	/** Items a second once it has been held long enough to reach full speed - see Stick Acceleration Time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Stick", meta = (ClampMin = "0.1", UIMin = "1", UIMax = "60", EditCondition = "bStickScrolls"))
	float StickMaxSpeed = 20.f;

	/**
	 * Seconds held all the way over to climb from Stick Speed to Stick Max Speed, easing in - so a short push moves
	 * carefully and a long hold races through a long list. Pushed part way it climbs more slowly; reversing or letting go
	 * starts it over. 0: no acceleration, always Stick Speed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Stick", meta = (ClampMin = "0", UIMin = "0", UIMax = "3", Units = "s", EditCondition = "bStickScrolls"))
	float StickAccelerationTime = 1.5f;

	/** How far the stick must be pushed, 0 to 1, before it scrolls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Stick", meta = (ClampMin = "0", ClampMax = "0.9", EditCondition = "bStickScrolls"))
	float StickDeadZone = 0.25f;

	/** The push-to-speed curve: 1 is straight, 2 and up give finer control near the middle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Stick", meta = (ClampMin = "0.5", ClampMax = "4", EditCondition = "bStickScrolls"))
	float StickResponse = 2.f;

	/** Pushing up (or right, along a horizontal list) goes the other way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling|Stick", meta = (DisplayName = "Invert Stick", EditCondition = "bStickScrolls"))
	bool bInvertStick = false;

	/**
	 * Scrolling selects: while the list is dragged, flung, wheeled, moved with a stick or by a scroll bar, the selection
	 * follows whichever item passes the focus - a dial clicking past each item, its select sound, selection events and Play
	 * On Selection animations with it. Off: scrolling only ever moves the list - it never selects, not even once it rests.
	 * An item is selected by clicking it, or by stepping to it with the D-pad or the arrow keys (from the item at the focus,
	 * if the selection has been scrolled away); a stick scrolls - an item a notch, or smoothly with Stick Scrolls. Scroll To
	 * Index and Scroll By from Blueprint still select.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling", meta = (DisplayName = "Select While Scrolling"))
	bool bSelectWhileScrolling = true;

	/** Clicking or tapping an item that isn't the focused one scrolls it to the focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scrolling", meta = (DisplayName = "Click To Focus"))
	bool bClickToFocus = true;
};

// =====================================================================================================
// Transitions
// =====================================================================================================

/**
 * How an item arrives or leaves: the state it comes from (Enter) or goes to (Exit), and how long it takes. Offsets
 * are in the path's own frame - X along the path (positive further down the list), Y across it - so an item
 * leaving along a curve follows the curve.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FFlowListTransition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (ClampMin = "0", UIMin = "0", UIMax = "1.5", Units = "s"))
	float Duration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (ClampMin = "0", UIMin = "0", UIMax = "1", Units = "s"))
	float Delay = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (EditCondition = "Easing == EWidgetAnimEasing::Custom", EditConditionHides))
	TObjectPtr<UCurveFloat> EasingCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (UIMin = "0", UIMax = "2"))
	float Scale = 0.6f;

	/** X along the path, Y across it, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	FVector2D Offset = FVector2D::ZeroVector;

	/** Degrees, clockwise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (UIMin = "-180", UIMax = "180", ForceUnits = "Degrees"))
	float Rotation = 0.f;
};

/**
 * The intro: every item playing its entrance when the list is first shown, the focused one first and the rest spreading
 * out from it.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FFlowListIntro
{
	GENERATED_BODY()

	/**
	 * Plays every item's entrance - Enter, or Anim Enter Item without Use Basic Animations - when the list is first shown,
	 * the focused item first and spreading out. Items added together in one frame (a list filled in Construct) enter one
	 * after another too.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro", meta = (DisplayName = "Play Intro"))
	bool bPlayIntro = false;

	/** Seconds between the starts of items entering one after another. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Intro", meta = (EditCondition = "bPlayIntro", ClampMin = "0", UIMin = "0", UIMax = "0.2", Units = "s"))
	float Stagger = 0.04f;
};

/** How items arrive, leave and make room for each other. */
USTRUCT(BlueprintType)
struct NAMETBD_API FFlowListTransitions
{
	GENERATED_BODY()

	FFlowListTransitions();

	/** Animates items added, removed, moved and shown at runtime. Off, the list changes in a single frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (DisplayName = "Animate Changes"))
	bool bAnimateChanges = true;

	/**
	 * Items arrive and leave by Enter and Exit: a fade, a scale, an offset and a turn, set here. Off, each item plays
	 * Widget Animations instead - Anim Enter Item and Anim Exit Item - as a Widget Animation Component's Anim In and Anim
	 * Out play on the widget it is on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (DisplayName = "Use Basic Animations"))
	bool bUseBasicAnimations = true;

	/** The state a new item grows from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (EditCondition = "bUseBasicAnimations", EditConditionHides))
	FFlowListTransition Enter;

	/** The state a removed item fades to before it is let go. It can't be clicked while it leaves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (EditCondition = "bUseBasicAnimations", EditConditionHides))
	FFlowListTransition Exit;

	/**
	 * Widget Animations an item plays on itself as it becomes the selected item (Play On Selection), and as it arrives: added
	 * or shown at runtime, in the intro (Play Intro), or scrolling into view (Play As Items Scroll In And Out). As a Widget
	 * Animation Component's Anim In plays on the widget it is on. An entry set to Mirror In Out also plays, backwards, as the
	 * item is deselected or leaves. The designer plays it whenever a transition is edited, and from Enter above the selected
	 * list or item.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (DisplayName = "Anim Enter Item", EditCondition = "!bUseBasicAnimations", EditConditionHides))
	TArray<FWidgetAnimEntryBase> AnimEnterItem;

	/**
	 * Widget Animations an item plays on itself as it stops being the selected item (Play On Selection), and as it leaves:
	 * removed, or scrolling out of view (Play As Items Scroll In And Out). As a Widget Animation Component's Anim Out does. A
	 * removed item stays drawn - and can't be clicked - until the longest of them ends. Empty: nothing plays then, and the
	 * item keeps the pose its entrance left it in. Previewed in the designer from Exit above the selected list or item.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (DisplayName = "Anim Exit Item", EditCondition = "!bUseBasicAnimations", EditConditionHides))
	TArray<FWidgetAnimEntryBase> AnimExitItem;

	/**
	 * The item that becomes selected plays Anim Enter Item, and the one that stops being selected plays Anim Exit Item - a
	 * highlight made of Widget Animations, following the selection as the list scrolls. With no Anim Exit Item nothing plays
	 * as an item is deselected: it keeps where its entrance left it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (DisplayName = "Play On Selection", EditCondition = "!bUseBasicAnimations", EditConditionHides))
	bool bPlayOnSelection = true;

	/**
	 * Items also play Anim Enter Item as they scroll into view, and Anim Exit Item as they scroll out of it - a list whose
	 * items animate in as they arrive on screen. Items in view when the list is first shown don't (Play Intro plays theirs).
	 * With no Anim Exit Item nothing plays as an item scrolls out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (DisplayName = "Play As Items Scroll In And Out", EditCondition = "!bUseBasicAnimations", EditConditionHides))
	bool bPlayOnScrollIntoView = false;

	/** How items slide to their new places when others arrive, leave or move. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions")
	FWidgetSpringParams ReorderSpring;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (DisplayName = "Intro"))
	FFlowListIntro Intro;

	/**
	 * When items are added or removed before the focused item, the list shifts with them so the same item stays
	 * focused. Removing the focused item itself always lets the next one slide into its place - a push/pop list.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transitions", meta = (DisplayName = "Keep Focused Item"))
	bool bKeepFocusedItem = true;

	/**
	 * Saved before Play Intro and Stagger moved into Intro: read once on load, into Intro, and never saved again. Stagger
	 * is -1 when nothing was saved for it.
	 */
	UPROPERTY()
	bool bPlayIntro = false;

	UPROPERTY()
	float Stagger = -1.f;

	void PostSerialize(const FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FFlowListTransitions> : public TStructOpsTypeTraitsBase2<FFlowListTransitions>
{
	enum
	{
		WithPostSerialize = true,
	};
};

/**
 * The state an item is in while arriving or leaving - multiplied into, and added onto, whatever its distance from the
 * focus gives it.
 */
struct FFlowListTransitionState
{
	float Opacity = 1.f;
	float Scale = 1.f;
	float Rotation = 0.f;
	FVector2f Offset = FVector2f::ZeroVector;
};

namespace FlowListTypes
{
	/** A spring in Duration and Bounce terms. */
	NAMETBD_API FWidgetSpringParams MakeSpring(float VisualDuration, float Bounce);

	/** Where an item is Alpha (0..1, unshaped) of the way through Transition. bEnter runs from the transition's state to rest. */
	NAMETBD_API FFlowListTransitionState EvaluateTransition(const FFlowListTransition& Transition, float Alpha, bool bEnter);

	/**
	 * A Spline path's curve, flattened, in the list's local space for a list of Size: Catmull-Rom through each of Points
	 * (fractions of Size), its ends held - or a line across the middle with fewer than two. Focus Offset not added.
	 */
	NAMETBD_API void BuildSplineCurve(const TArray<FVector2D>& Points, const FVector2f& Size, TArray<FVector2f>& OutCurve);

	/** How many curve points BuildSplineCurve makes between two of Points. */
	inline constexpr int32 SplineStepsPerSpan = 16;

	/** Overwrites the four groups with a preset's look. Custom leaves them as they are. */
	NAMETBD_API void ApplyPreset(EFlowListStylePreset Preset, FFlowListLayout& Layout, FFlowListEffects& Effects, FFlowListScrolling& Scrolling, FFlowListTransitions& Transitions);
}
