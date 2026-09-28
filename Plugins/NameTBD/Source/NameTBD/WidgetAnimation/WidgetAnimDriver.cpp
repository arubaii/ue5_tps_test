// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimDriver.h"

// =====================================================================================================
// FWidgetAnimValueOptions
// =====================================================================================================

int32 FWidgetAnimValueOptions::GetNumChannels() const
{
	switch (Kind)
	{
	case EWidgetAnimValueKind::Vector2D: return 2;
	case EWidgetAnimValueKind::Color:    return 4;
	default:                             return 1;
	}
}

void FWidgetAnimValueOptions::GetChannelRange(int32 Channel, double& OutAtStart, double& OutAtEnd) const
{
	switch (Kind)
	{
	case EWidgetAnimValueKind::Vector2D:
		OutAtStart = Channel == 0 ? ValueAtStart2D.X : ValueAtStart2D.Y;
		OutAtEnd = Channel == 0 ? ValueAtEnd2D.X : ValueAtEnd2D.Y;
		return;

	case EWidgetAnimValueKind::Color:
		OutAtStart = ValueAtStartColor.Component(FMath::Clamp(Channel, 0, 3));
		OutAtEnd = ValueAtEndColor.Component(FMath::Clamp(Channel, 0, 3));
		return;

	default:
		OutAtStart = ValueAtStart;
		OutAtEnd = ValueAtEnd;
		return;
	}
}

FVector4f FWidgetAnimValueOptions::ToProgress(const FVector4& Value) const
{
	FVector4f Progress(0.f, 0.f, 0.f, 0.f);
	for (int32 Channel = 0; Channel < GetNumChannels(); ++Channel)
	{
		double AtStart = 0.0;
		double AtEnd = 1.0;
		GetChannelRange(Channel, AtStart, AtEnd);

		// A range of no width has no inside: at or past its one value is the end.
		const double Span = AtEnd - AtStart;
		Progress[Channel] = FMath::IsNearlyZero(Span)
			? (Value[Channel] >= AtEnd ? 1.f : 0.f)
			: static_cast<float>(FMath::Clamp((Value[Channel] - AtStart) / Span, 0.0, 1.0));
	}
	return Progress;
}

FVector4 FWidgetAnimValueOptions::FromProgress(const FVector4f& Progress) const
{
	FVector4 Value(0.0, 0.0, 0.0, 0.0);
	for (int32 Channel = 0; Channel < GetNumChannels(); ++Channel)
	{
		double AtStart = 0.0;
		double AtEnd = 1.0;
		GetChannelRange(Channel, AtStart, AtEnd);
		Value[Channel] = FMath::Lerp(AtStart, AtEnd, static_cast<double>(Progress[Channel]));
	}

	// One number reads the same on every channel, as it was given.
	if (GetNumChannels() == 1)
	{
		Value = WidgetAnimValue::FromFloat(Value.X);
	}
	return Value;
}

FVector4 FWidgetAnimValueOptions::GetStartValue() const
{
	switch (Kind)
	{
	case EWidgetAnimValueKind::Vector2D: return WidgetAnimValue::FromVector2D(StartValue2D);
	case EWidgetAnimValueKind::Color:    return WidgetAnimValue::FromColor(StartValueColor);
	default:                             return WidgetAnimValue::FromFloat(StartValue);
	}
}

// =====================================================================================================
// WidgetAnimValue
// =====================================================================================================

namespace WidgetAnimValue
{
	/**
	 * How close to its target, and how slow, a spring has to be to count as arrived - in progress, a fraction of the
	 * whole track. A two-thousandth of a 500 pixel slide is a quarter of a pixel; and a quantized track (a Count Up, a
	 * Blueprint type with a Quantize Step) still lands exactly, because arriving snaps onto the target.
	 */
	constexpr float SpringRestDistance = 0.0005f;
	constexpr float SpringRestSpeed = 0.005f;

	void Snap(FWidgetAnimValueState& State, const FVector4f& Progress)
	{
		State.Progress = Progress;
		State.Target = Progress;
		State.Velocity = FVector4f(0.f, 0.f, 0.f, 0.f);
		State.TweenFrom = Progress;
		State.TweenElapsed = 0.f;
	}

	bool SetTarget(const FWidgetAnimValueOptions& Options, const FWidgetAnimValueMotion& Motion, FWidgetAnimValueState& State, const FVector4f& NewTarget, bool bSnap)
	{
		const int32 NumChannels = FMath::Clamp(Motion.NumChannels, 1, 4);

		// Measured against the last target rather than where the track is shown, so a bar still gliding up to 80%
		// ignores a late 60% instead of turning round for it.
		FVector4f Target = State.Target;
		bool bChanged = false;
		for (int32 Channel = 0; Channel < NumChannels; ++Channel)
		{
			const float Wanted = FMath::Clamp(NewTarget[Channel], 0.f, 1.f);
			if (!bSnap && Options.bNeverGoBackwards && Wanted < State.Target[Channel])
			{
				continue;
			}
			if (Wanted != Target[Channel])
			{
				Target[Channel] = Wanted;
				bChanged = true;
			}
		}

		if (bSnap || Motion.Smoothing == EWidgetAnimValueSmoothing::Immediate)
		{
			if (!bChanged && !State.IsMoving())
			{
				return false;
			}
			Snap(State, Target);
			return true;
		}

		if (!bChanged)
		{
			return false;
		}

		// A tween starts a fresh ease from wherever it has got to; a spring needs nothing, since its displacement is
		// measured from the target each step and its velocity carries straight over.
		State.Target = Target;
		State.TweenFrom = State.Progress;
		State.TweenElapsed = 0.f;
		return true;
	}

	bool Advance(const FWidgetAnimValueOptions& Options, const FWidgetAnimValueMotion& Motion, FWidgetAnimValueState& State, float DeltaTime)
	{
		if (!State.IsMoving())
		{
			return true;
		}

		const int32 NumChannels = FMath::Clamp(Motion.NumChannels, 1, 4);
		DeltaTime = FMath::Max(DeltaTime, 0.f);
		const FVector4f Before = State.Progress;
		bool bArrived = true;

		switch (Motion.Smoothing)
		{
		case EWidgetAnimValueSmoothing::Tween:
		{
			State.TweenElapsed += DeltaTime;
			const float Alpha = Motion.TweenDuration > KINDA_SMALL_NUMBER ? FMath::Clamp(State.TweenElapsed / Motion.TweenDuration, 0.f, 1.f) : 1.f;
			const float Eased = WidgetAnimEasing::Evaluate(Motion.TweenEasing, Motion.TweenCurve, Alpha);
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				State.Progress[Channel] = FMath::Lerp(State.TweenFrom[Channel], State.Target[Channel], Eased);
				State.Velocity[Channel] = 0.f;
			}
			bArrived = Alpha >= 1.f;
			break;
		}

		case EWidgetAnimValueSmoothing::Spring:
		{
			// Each channel its own spring: a 2D value's X and Y each settle at their own pace.
			const FWidgetSpringParams DefaultSpring;
			const FWidgetSpringParams& Spring = Motion.Spring ? *Motion.Spring : DefaultSpring;
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				FWidgetSpringState ChannelState;
				ChannelState.Value = State.Progress[Channel];
				ChannelState.Velocity = State.Velocity[Channel];
				const bool bChannelArrived = UWidgetAnimSpringLibrary::AdvanceSpring(Spring, State.Target[Channel], ChannelState, DeltaTime, SpringRestDistance, SpringRestSpeed);
				State.Progress[Channel] = ChannelState.Value;
				State.Velocity[Channel] = ChannelState.Velocity;
				bArrived = bArrived && bChannelArrived;
			}
			break;
		}

		default:
			// Immediate has nothing to travel: a value set is shown at once.
			break;
		}

		// No faster than the whole track in Minimum Time To Fill. A spring held back keeps moving at that speed, so it
		// neither builds up energy it would spend overshooting later nor stops dead once it is let go.
		if (Options.MinimumTimeToFill > KINDA_SMALL_NUMBER && Motion.Smoothing != EWidgetAnimValueSmoothing::Immediate)
		{
			const float MaxStep = DeltaTime / Options.MinimumTimeToFill;
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				const float Step = State.Progress[Channel] - Before[Channel];
				if (FMath::Abs(Step) > MaxStep)
				{
					const float Direction = Step > 0.f ? 1.f : -1.f;
					State.Progress[Channel] = Before[Channel] + Direction * MaxStep;
					if (Motion.Smoothing == EWidgetAnimValueSmoothing::Spring)
					{
						State.Velocity[Channel] = Direction / Options.MinimumTimeToFill;
					}
					bArrived = false;
				}
			}
		}

		if (bArrived)
		{
			// Exactly onto the target, whatever the last step's arithmetic left, so a track's end value is its end value.
			Snap(State, State.Target);
		}

		return !State.IsMoving();
	}
}