// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "WidgetAnimEasing.generated.h"

/**
 * How a normalized 0..1 alpha is shaped before it drives a value.
 *
 * Lived on WidgetAnimLibrary.h until the Transform family needed it too. It is here, in a header that
 * depends on nothing else in the plugin, so every animation family can share one easing implementation
 * rather than each one growing its own copy. WidgetAnimLibrary.h includes this transitively, so nothing
 * that used to say EWidgetAnimEasing needs its includes changed.
 */
UENUM(BlueprintType)
enum class EWidgetAnimEasing : uint8
{
	Linear,
	EaseIn,
	EaseOut,
	EaseInOut,
	Custom
};

/**
 * The one place alpha shaping happens.
 *
 * Free functions rather than a UBlueprintFunctionLibrary: these are called per layer per frame, from
 * inside evaluators, and are not something a designer ever wires up by hand.
 */
namespace WidgetAnimEasing
{
	/**
	 * Overshoot curve - travels past 1 and settles back. Overshoot is how far past, 0.4 being a noticeable
	 * but still tasteful pop. This is what Scale's Overshoot and Slide's Slide Overshoot have always used.
	 */
	NAMETBD_API float EaseOutBack(float Alpha, float Overshoot);

	/** Shapes Alpha by Easing, using Curve when Easing is Custom. Alpha is clamped to 0..1 first. */
	NAMETBD_API float Evaluate(EWidgetAnimEasing Easing, const UCurveFloat* Curve, float Alpha);

	/**
	 * Evaluate, except that a positive Overshoot replaces the easing with EaseOutBack.
	 *
	 * The existing Scale and Slide types already behave this way - Overshoot, when set, wins over the
	 * authored Easing rather than combining with it - so this keeps one rule for every family instead of
	 * each evaluator re-deciding what overshoot means.
	 */
	NAMETBD_API float EvaluateWithOvershoot(EWidgetAnimEasing Easing, const UCurveFloat* Curve, float Alpha, float Overshoot);

	/**
	 * A cubic Bezier from (0, 0) to (1, 1) through the handles Leave and Arrive, read at X - the same curve CSS's
	 * cubic-bezier(x1, y1, x2, y2) draws, and what a graph editor's two tangent handles on a segment describe. The handles'
	 * X are kept within 0..1 so the curve never turns back on itself in time; their Y may go past either end, which is an
	 * overshoot or an anticipation. Exactly 0 at X 0 and 1 at X 1.
	 */
	NAMETBD_API float EvaluateBezier(const FVector2D& Leave, const FVector2D& Arrive, float X);

	/**
	 * The handles a segment's preset easing is drawn with as a Bezier - Linear, Ease In, Ease Out and Ease In Out - so a
	 * graph editor can show handles on any segment, and dragging one starts from the curve already there. Exact for Linear,
	 * Ease In and Ease Out (squares, as Evaluate eases them); Ease In Out is its closest Bezier. False for anything with no
	 * such curve - Hold, a Curve asset.
	 */
	NAMETBD_API bool GetBezierHandles(EWidgetAnimEasing Easing, FVector2D& OutLeave, FVector2D& OutArrive);
}

/**
 * The curve one stretch between two keys follows - a segment - when it should move differently from the rest
 * of its track.
 *
 * Track Easing, the default, means "no opinion": the segment moves by the track's own Easing, from its key to the
 * next. Anything else takes that one segment over, the way picking an interpolation between two keys does in any
 * keyframe editor.
 *
 * A separate enum rather than EWidgetAnimEasing plus a flag, so Track Easing reads as the first choice in the
 * same dropdown, and Hold - which only means something between two keys - has somewhere to live.
 */
UENUM(BlueprintType)
enum class EWidgetAnimSegmentEasing : uint8
{
	Track     UMETA(DisplayName = "Track Easing", ToolTip = "Moves by the track's own Easing, as every segment does until it is given a curve of its own."),
	Linear    UMETA(ToolTip = "A constant speed from this key to the next."),
	EaseIn    UMETA(DisplayName = "Ease In", ToolTip = "Leaves this key slowly and arrives at the next at speed."),
	EaseOut   UMETA(DisplayName = "Ease Out", ToolTip = "Leaves this key at speed and settles into the next."),
	EaseInOut UMETA(DisplayName = "Ease In Out", ToolTip = "Slow away from this key, slow into the next."),
	Hold      UMETA(ToolTip = "Stays on this key's value, then jumps to the next key's the moment it arrives."),
	Custom    UMETA(ToolTip = "Follows a Curve asset from 0 to 1 across the segment."),

	Bezier    UMETA(ToolTip = "Shaped by two handles, dragged in the Graph Editor: how fast it leaves this key and how it arrives at the next - past either end for an overshoot or a wind-up.")
};

/** How one segment - from a key to the next - moves. See EWidgetAnimSegmentEasing. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimSegmentCurve
{
	GENERATED_BODY()

	/** How the value travels from this key to the next one. Track Easing leaves it to the track's Easing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curve", meta = (DisplayName = "Curve To Next Key"))
	EWidgetAnimSegmentEasing Easing = EWidgetAnimSegmentEasing::Track;

	/** The curve a Custom segment follows, read from 0 to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curve", meta = (DisplayName = "Custom Segment Curve", EditCondition = "Easing == EWidgetAnimSegmentEasing::Custom", EditConditionHides))
	TObjectPtr<UCurveFloat> CustomCurve = nullptr;

	/**
	 * A Bezier segment's handle leaving this key: X how far along the segment in time (0 to 1), Y how far along the way to
	 * the next key's value it pulls (0 its own value, 1 the next key's). Dragged in the Graph Editor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curve", meta = (DisplayName = "Leave Handle", EditCondition = "Easing == EWidgetAnimSegmentEasing::Bezier", EditConditionHides))
	FVector2D LeaveHandle = FVector2D(0.42, 0.0);

	/** A Bezier segment's handle arriving at the next key, in the same terms as Leave Handle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curve", meta = (DisplayName = "Arrive Handle", EditCondition = "Easing == EWidgetAnimSegmentEasing::Bezier", EditConditionHides))
	FVector2D ArriveHandle = FVector2D(0.58, 1.0);

	/** Whether this segment leaves its motion to the track - the default, and what every segment of older data is. */
	bool IsTrackEasing() const { return Easing == EWidgetAnimSegmentEasing::Track; }

	/** Whether this segment is shaped by its own two handles - see LeaveHandle and ArriveHandle. */
	bool IsBezier() const { return Easing == EWidgetAnimSegmentEasing::Bezier; }

	// The handles only mean anything on a Bezier segment, so what is left in them on any other is not a difference.
	bool operator==(const FWidgetAnimSegmentCurve& Other) const
	{
		return Easing == Other.Easing && CustomCurve == Other.CustomCurve
			&& (!IsBezier() || (LeaveHandle == Other.LeaveHandle && ArriveHandle == Other.ArriveHandle));
	}
	bool operator!=(const FWidgetAnimSegmentCurve& Other) const { return !(*this == Other); }
};

namespace WidgetAnimEasing
{
	/**
	 * Alpha (0..1 across one segment) shaped by that segment's curve. TrackEase is what Track Easing defers to -
	 * the track's own Easing, or its overshoot - so the caller, which knows the track, decides what that means.
	 *
	 * Inline, and built only on Evaluate above: nothing here needs a definition of its own.
	 */
	inline float EvaluateSegment(const FWidgetAnimSegmentCurve& Curve, float Alpha, TFunctionRef<float(float)> TrackEase)
	{
		switch (Curve.Easing)
		{
		case EWidgetAnimSegmentEasing::Linear:    return Evaluate(EWidgetAnimEasing::Linear, nullptr, Alpha);
		case EWidgetAnimSegmentEasing::EaseIn:    return Evaluate(EWidgetAnimEasing::EaseIn, nullptr, Alpha);
		case EWidgetAnimSegmentEasing::EaseOut:   return Evaluate(EWidgetAnimEasing::EaseOut, nullptr, Alpha);
		case EWidgetAnimSegmentEasing::EaseInOut: return Evaluate(EWidgetAnimEasing::EaseInOut, nullptr, Alpha);

		// Only arriving counts as arriving: anything short of the next key is still this one.
		case EWidgetAnimSegmentEasing::Hold:      return Alpha >= 1.f ? 1.f : 0.f;

		// No curve picked yet reads as linear rather than as nothing moving at all.
		case EWidgetAnimSegmentEasing::Custom:
			return Curve.CustomCurve ? Evaluate(EWidgetAnimEasing::Custom, Curve.CustomCurve, Alpha) : Evaluate(EWidgetAnimEasing::Linear, nullptr, Alpha);

		case EWidgetAnimSegmentEasing::Bezier:    return EvaluateBezier(Curve.LeaveHandle, Curve.ArriveHandle, Alpha);

		case EWidgetAnimSegmentEasing::Track:
		default:
			return TrackEase(Alpha);
		}
	}
}