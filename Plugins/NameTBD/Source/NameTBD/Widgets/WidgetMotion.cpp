// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetMotion.h"

void FWidgetMotionValue::Snap(float InTarget)
{
	Value = InTarget;
	Target = InTarget;
	From = InTarget;
	Elapsed = 0.f;
	Spring.Value = InTarget;
	Spring.Velocity = 0.f;
	bMoving = false;
}

void FWidgetMotionValue::SetTarget(float InTarget, EWidgetMotion Motion, float Duration, bool bInstant)
{
	if (bInstant || Motion == EWidgetMotion::Instant)
	{
		Snap(InTarget);
		return;
	}
	if (bMoving && FMath::IsNearlyEqual(InTarget, Target) && Motion == CurrentMotion)
	{
		return;
	}
	if (!bMoving && FMath::IsNearlyEqual(InTarget, Value))
	{
		Snap(InTarget);
		return;
	}

	const float Seconds = Duration > KINDA_SMALL_NUMBER ? Duration : GetDefaultDuration(Motion);
	if (Motion == EWidgetMotion::Bouncy)
	{
		// The spring carries on from where the value is, at the speed it already has.
		if (CurrentMotion != EWidgetMotion::Bouncy || !bMoving)
		{
			Spring.Value = Value;
			Spring.Velocity = bMoving && CurrentDuration > KINDA_SMALL_NUMBER ? (Target - From) / CurrentDuration : 0.f;
		}
		SpringParams.Mode = EWidgetSpringMode::DurationAndBounce;
		SpringParams.VisualDuration = FMath::Max(Seconds, 0.05f);
		SpringParams.Bounce = 0.3f;
	}

	CurrentMotion = Motion;
	From = Value;
	Target = InTarget;
	Elapsed = 0.f;
	CurrentDuration = FMath::Max(Seconds, 0.01f);
	bMoving = true;
}

bool FWidgetMotionValue::Advance(float DeltaTime)
{
	if (!bMoving)
	{
		return false;
	}

	if (CurrentMotion == EWidgetMotion::Bouncy)
	{
		// Rests within a thousandth of the travel (these drive alphas, angles, fractions - all small numbers).
		const float Travel = FMath::Max(FMath::Abs(Target - From), 0.001f);
		const bool bRested = UWidgetAnimSpringLibrary::AdvanceSpring(SpringParams, Target, Spring, DeltaTime, Travel * 0.001f, Travel * 0.01f);
		Value = Spring.Value;
		bMoving = !bRested;
		if (bRested)
		{
			Value = Target;
		}
		return bMoving;
	}

	Elapsed += DeltaTime;
	const float Alpha = FMath::Clamp(Elapsed / CurrentDuration, 0.f, 1.f);
	Value = FMath::Lerp(From, Target, Ease(CurrentMotion, Alpha));
	if (Alpha >= 1.f)
	{
		Snap(Target);
	}
	return bMoving;
}

float FWidgetMotionValue::GetDefaultDuration(EWidgetMotion Motion)
{
	switch (Motion)
	{
	case EWidgetMotion::Snappy: return 0.14f;
	case EWidgetMotion::Bouncy: return 0.4f;
	case EWidgetMotion::Gentle: return 0.45f;
	case EWidgetMotion::Linear: return 0.25f;
	case EWidgetMotion::Instant: return 0.f;
	case EWidgetMotion::Smooth:
	default: return 0.25f;
	}
}

float FWidgetMotionValue::Ease(EWidgetMotion Motion, float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	switch (Motion)
	{
	case EWidgetMotion::Linear:
		return T;
	case EWidgetMotion::Snappy:
		return 1.f - FMath::Pow(1.f - T, 4.f);
	case EWidgetMotion::Gentle:
		return T < 0.5f ? 4.f * T * T * T : 1.f - FMath::Pow(-2.f * T + 2.f, 3.f) * 0.5f;
	case EWidgetMotion::Smooth:
	case EWidgetMotion::Bouncy:
	case EWidgetMotion::Instant:
	default:
		return 1.f - FMath::Pow(1.f - T, 3.f);
	}
}
