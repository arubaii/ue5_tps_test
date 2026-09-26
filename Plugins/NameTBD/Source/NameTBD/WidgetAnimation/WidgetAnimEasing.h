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
}