// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetAnimSpring.generated.h"

// =====================================================================================================
// Why a spring is a timing, not a track type
// =====================================================================================================
//
// Every production system that ships springs treats them as HOW a value travels, not WHAT travels. Motion
// has `type: "spring"` next to `type: "tween"` on any property, SwiftUI has `.spring` as an Animation you
// hand any change to, Compose has `spring()` as an AnimationSpec. The property being animated never
// knows. So here a spring is a Timing on a track, beside Tween (Duration + Easing), and every track that
// travels from a start value to an end value - Fade, Scale, Slide, Rotate, Material Scalar, Material
// Vector - can use it without a line of per-type code. Their lerps already extrapolate, so an overshoot
// past 1 carries the widget past its end value and back, exactly as the spring says.
//
// Transform and Pulse are the exceptions. A Transform track's keys set its own timing - and a spring is a
// motion between two values, which is why Motion only lets a spring animate between two keyframes too.
// Pulse is a sine with no start and end to travel between.
//
// The solver is the closed-form solution of a damped harmonic oscillator, not an integrator: position at
// any time is one formula, so a spring track scrubs, loops, ping-pongs and inverts exactly like every
// other track, frame-rate independent, and gives the same answer in the designer as in a packaged game.

/** How a track travels from its start values to its end values. */
UENUM(BlueprintType)
enum class EWidgetAnimTiming : uint8
{
	Tween UMETA(ToolTip = "Authored: Duration says how long it takes and Easing says how it moves. The same motion every time."),

	Spring UMETA(ToolTip = "Physical: a spring pulls the value to its end, with weight, overshoot and settle. How long the track runs comes from the spring - Duration and Easing are not used."),

	Value UMETA(ToolTip = "Driven from gameplay: Set Widget Animation Value with this track's Value Name says where between its start and end it is, and Value Smoothing says how it travels there. The rest of the animation still plays on its clock. On the timeline - and in every preview - it sweeps once across its Duration.")
};

/** Which two ways of describing the same spring a track is authored in. */
UENUM(BlueprintType)
enum class EWidgetSpringMode : uint8
{
	DurationAndBounce UMETA(DisplayName = "Duration and Bounce", ToolTip = "Visual Duration and Bounce - the way Motion, SwiftUI and Figma describe a spring for UI. Change the bounce and the pace stays the same."),

	Physical UMETA(DisplayName = "Stiffness, Damping and Mass", ToolTip = "The physics directly. Stiffness pulls harder, Damping resists, Mass makes it heavier. Everything else is derived from these.")
};

/**
 * One spring, as authored on a track.
 *
 * The track travels 0 -> 1 through its values - start to end - the way every tween does. The spring decides
 * where along that it is at each moment, and that can be past 1 (overshoot) and back.
 *
 * The track runs until the spring has SETTLED - come within Rest Threshold of its end and stayed there -
 * and that settle time, not a Duration, is the track's length on the timeline. It is longer than Visual
 * Duration: the bulk of the move happens before Visual Duration, and the small settle after it.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetSpringParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (DisplayName = "Spring Mode"))
	EWidgetSpringMode Mode = EWidgetSpringMode::DurationAndBounce;

	/**
	 * Roughly how long, in seconds, the value takes to reach its end - the part of the move the eye reads as
	 * "the animation". Whatever overshoot and settle there is happens mostly after this.
	 *
	 * The same number Motion calls visualDuration, with the same maths behind it, so a spring tuned on the web
	 * with visualDuration 0.4 and bounce 0.25 moves identically here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (DisplayName = "Visual Duration", Units = "s", ClampMin = "0.02", UIMin = "0.1", UIMax = "2.0", EditCondition = "Mode == EWidgetSpringMode::DurationAndBounce", EditConditionHides))
	float VisualDuration = 0.4f;

	/**
	 * How much it overshoots and wobbles. 0 arrives as fast as possible with no overshoot at all (critically
	 * damped); 0.2 - 0.3 is a tasteful pop; 0.5 and up is visibly springy.
	 *
	 * Negative values, as in SwiftUI, damp it past critical: an even softer arrival that eases in over a
	 * longer tail. Capped at 0.95, since a spring that never loses energy never settles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "-0.9", ClampMax = "0.95", UIMin = "-0.5", UIMax = "0.9", EditCondition = "Mode == EWidgetSpringMode::DurationAndBounce", EditConditionHides))
	float Bounce = 0.2f;

	/** How hard the spring pulls toward the end value. Higher is snappier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0.01", UIMin = "10", UIMax = "1000", EditCondition = "Mode == EWidgetSpringMode::Physical", EditConditionHides))
	float Stiffness = 100.f;

	/**
	 * How strongly the motion is resisted. Lower bounces more. At 0 the spring never loses energy - it is
	 * stopped, and snapped to its end, at the 10 second limit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0.0", UIMin = "0", UIMax = "100", EditCondition = "Mode == EWidgetSpringMode::Physical", EditConditionHides))
	float Damping = 10.f;

	/** How heavy the thing on the spring is. Higher is slower and more lethargic, and swings further. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (ClampMin = "0.01", UIMin = "0.1", UIMax = "10", EditCondition = "Mode == EWidgetSpringMode::Physical", EditConditionHides))
	float Mass = 1.f;

	/**
	 * How fast it is already moving when the track starts, in whole travels per second toward the end value.
	 * 0 starts from rest. 5 starts as if flicked - it covers ground faster and overshoots more even with no
	 * bounce. Negative starts it moving the wrong way first, a wind-up.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (DisplayName = "Initial Velocity", UIMin = "-10", UIMax = "10"))
	float InitialVelocity = 0.f;

	/**
	 * How close to its end value, as a fraction of the whole travel, the spring must stay before it counts
	 * as settled and the track ends. 0.001 is a tenth of a pixel on a 100 pixel slide. Larger ends the track
	 * sooner, with a small snap at the end; smaller runs a longer, invisible tail.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", AdvancedDisplay, meta = (DisplayName = "Rest Threshold", ClampMin = "0.00001", ClampMax = "0.1", UIMin = "0.0001", UIMax = "0.01"))
	float RestThreshold = 0.001f;
};

/**
 * A spring's position and speed. What a live spring - one driven from code toward a target that moves -
 * carries from frame to frame. See UWidgetAnimSpringLibrary::AdvanceSpring.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetSpringState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float Value = 0.f;

	/** Units of Value per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float Velocity = 0.f;
};

// =====================================================================================================
// Solver
// =====================================================================================================

/** A spring reduced to the two numbers the closed-form solution needs. Mass is folded into both. */
struct NAMETBD_API FWidgetSpringCoefficients
{
	/** Undamped angular frequency, in radians per second: sqrt(stiffness / mass). */
	double NaturalFrequency = 1.0;

	/** Damping as a fraction of critical: below 1 overshoots, 1 is the fastest arrival that doesn't, above 1 is slower. */
	double DampingRatio = 1.0;
};

/**
 * The maths, as free functions: called per track per frame from the library's evaluators, and never wired
 * up by hand. Everything is in double precision - an exponential envelope over a long tail is exactly where
 * float starts to drift.
 */
namespace WidgetAnimSpring
{
	/** No spring runs longer than this. A spring that has not settled by then is snapped to its end. The same cap Motion uses. */
	constexpr float MaxSettleSeconds = 10.f;

	/** The shortest a spring track can be, so a timeline never holds a zero-length section. */
	constexpr float MinSettleSeconds = 0.01f;

	/** The spring's frequency and damping ratio, from whichever mode it is authored in. */
	NAMETBD_API FWidgetSpringCoefficients ResolveCoefficients(const FWidgetSpringParams& Params);

	/**
	 * The exact state of a spring Time seconds after it started Displacement away from its rest point and
	 * moving at Velocity. Displacement is position minus rest, so a spring starting behind its target has a
	 * negative one. Underdamped, critically damped and overdamped each have their own closed form.
	 */
	NAMETBD_API void Evaluate(const FWidgetSpringCoefficients& Coefficients, double Displacement, double Velocity, double Time, double& OutDisplacement, double& OutVelocity);

	/**
	 * How long until the spring stays within Tolerance of rest for good, from an upper bound on its motion -
	 * so it can only be slightly long, never short. Clamped to MaxSettleSeconds.
	 */
	NAMETBD_API double ComputeSettleTime(const FWidgetSpringCoefficients& Coefficients, double Displacement, double Velocity, double Tolerance);
}

UCLASS()
class NAMETBD_API UWidgetAnimSpringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * How long a spring track runs, in seconds: until it has settled to within its Rest Threshold. What the
	 * timeline draws as the track's length and what After Previous sequences from.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Spring")
	static float GetSettleTime(const FWidgetSpringParams& Params);

	/**
	 * The spring's pace as a Visual Duration, whichever mode it is authored in: the time the value takes to
	 * visually reach its end. What a Physical spring would be written as in Duration and Bounce.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Spring")
	static float GetVisualDuration(const FWidgetSpringParams& Params);

	/** Damping as a fraction of critical: below 1 overshoots, 1 is the fastest arrival that doesn't, above 1 is slower still. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Spring")
	static float GetDampingRatio(const FWidgetSpringParams& Params);

	/**
	 * How far along its travel the spring is, Seconds after it starts: 0 at the start, 1 at the end, and past
	 * 1 while it overshoots. Exactly 1 once it has settled.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Spring")
	static float EvaluateProgress(const FWidgetSpringParams& Params, float Seconds);

	/**
	 * The spring's progress at Alpha of its track - 0 its start, 1 its settle time - which is what a track's
	 * value is lerped by.
	 *
	 * bTowardStart is how a spring plays backwards. A tween played backwards is its motion rewound, which is
	 * what reversing a curve means. A spring rewound would start with the wobble and end with a sudden launch
	 * from rest, which no spring does, so instead it runs forward in time toward its START value: the value is
	 * 1 - progress(1 - Alpha). Alpha still runs 1 -> 0 on the way back, so the rest of the library needs no
	 * special case. Used for inverted tracks and Play Out - a ping-pong's return leg rewinds the track instead.
	 */
	static float EvaluateDirectedProgress(const FWidgetSpringParams& Params, float Alpha, bool bTowardStart);

	/**
	 * Moves a live spring DeltaTime seconds toward Target, exactly - one closed-form step, the same at any
	 * frame rate, however long the frame. True once it has come to rest, when it is also snapped exactly onto
	 * Target.
	 *
	 * For values driven from code rather than authored on a timeline: a bar following a health value, a
	 * panel following the cursor. Change Target whenever you like - the spring keeps its velocity and bends
	 * toward the new one, which is what makes spring-driven UI feel responsive rather than replayed.
	 *
	 * Rest Distance and Rest Speed are in Value's own units, since a live spring has no fixed travel to be
	 * a fraction of - half a pixel suits positions, 0.001 suits opacity or scale. Initial Velocity and Rest
	 * Threshold on Params are not used; State carries the velocity.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Spring", meta = (AdvancedDisplay = "RestDistance,RestSpeed"))
	static bool AdvanceSpring(const FWidgetSpringParams& Params, float Target, UPARAM(ref) FWidgetSpringState& State, float DeltaTime, float RestDistance = 0.01f, float RestSpeed = 0.1f);
};