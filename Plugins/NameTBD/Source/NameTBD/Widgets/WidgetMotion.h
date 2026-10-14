// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"

#include "WidgetMotion.generated.h"

/**
 * How an effect widget moves from one state to the next - a dropdown opening, a card turning, a bar filling, a panel lighting
 * on hover. The same vocabulary as the ready-made buttons' Motion. Each widget follows it with an Enter Duration (the way in:
 * opening, turning to the back, rising, hovered) and an Exit Duration (the way back); 0 is the motion's own pace.
 */
UENUM(BlueprintType)
enum class EWidgetMotion : uint8
{
	Smooth UMETA(ToolTip = "Leaves quickly and eases into place (ease out). About a quarter of a second."),
	Snappy UMETA(ToolTip = "Quick and crisp, eased out hard - for dense menus. About a seventh of a second."),
	Bouncy UMETA(ToolTip = "A spring: overshoots a little and settles. Keeps its speed if it is turned round midway."),
	Gentle UMETA(ToolTip = "Slow away, slow in (ease in and out) - calm, cinematic. Close to half a second."),
	Linear UMETA(ToolTip = "One steady speed, start to end."),
	Instant UMETA(ToolTip = "No animation: the new state shows at once.")
};

/**
 * One animated value driven by an EWidgetMotion: set a target, advance it every frame. A tween restarts from where the value is
 * when the target changes; Bouncy is a live spring (UWidgetAnimSpringLibrary::AdvanceSpring), so it keeps its velocity and
 * bends toward a new target. Plain data, no UObject; game thread.
 */
struct NAMETBD_API FWidgetMotionValue
{
	/** Where it is now. */
	float Value = 0.f;

	/** Where it is going. */
	float Target = 0.f;

	/** Starts at Target, not moving. */
	void Snap(float InTarget);

	/**
	 * Heads for InTarget over Duration seconds (0: Motion's own pace) as Motion says. Instant, or bInstant, snaps. Setting the
	 * target it already heads for changes nothing.
	 */
	void SetTarget(float InTarget, EWidgetMotion Motion, float Duration, bool bInstant = false);

	/** Moves DeltaTime on. True while still moving. */
	bool Advance(float DeltaTime);

	bool IsMoving() const { return bMoving; }

	/** The pace Motion has when no duration is given, in seconds. */
	static float GetDefaultDuration(EWidgetMotion Motion);

	/** Motion's curve at Alpha (0..1) for the tweens; Bouncy and Instant read as Smooth. */
	static float Ease(EWidgetMotion Motion, float Alpha);

private:
	EWidgetMotion CurrentMotion = EWidgetMotion::Smooth;
	float From = 0.f;
	float Elapsed = 0.f;
	float CurrentDuration = 0.25f;
	FWidgetSpringState Spring;
	FWidgetSpringParams SpringParams;
	bool bMoving = false;
};
