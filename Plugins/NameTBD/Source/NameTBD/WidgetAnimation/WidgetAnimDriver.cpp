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
	 * whole track. A two-thousandth of a 500 pixel slide is a quarter of a pixel; and a quantized track (a Count, a
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

		// A tween starts a fresh ease from wherever it has gotten to; a spring needs nothing, since its displacement is
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

		case EWidgetAnimValueSmoothing::Steady:
		{
			// Each channel at its own steady pace, measured in the value's own units: 100 a second on a 0 - 1000 count is
			// a tenth of the track a second. Faster only as far as Catch Up Within needs, so a big jump still lands in
			// time and eases into the steady pace as it closes in.
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				double AtStart = 0.0;
				double AtEnd = 1.0;
				Options.GetChannelRange(Channel, AtStart, AtEnd);
				const double Range = FMath::Abs(AtEnd - AtStart);
				const float Remaining = State.Target[Channel] - State.Progress[Channel];
				const float Distance = FMath::Abs(Remaining);

				float Speed = Range > UE_DOUBLE_SMALL_NUMBER ? static_cast<float>(FMath::Max(Motion.SteadySpeed, 0.0) / Range) : 0.f;
				if (Motion.SteadyCatchUpTime > KINDA_SMALL_NUMBER)
				{
					Speed = FMath::Max(Speed, Distance / Motion.SteadyCatchUpTime);
				}
				if (Speed <= 0.f)
				{
					// No speed at all and no time to catch up in: there is no pace to keep, so it is shown at once.
					Speed = TNumericLimits<float>::Max();
				}

				const float Step = Speed * DeltaTime;
				State.Progress[Channel] = Step >= Distance - SpringRestDistance ? State.Target[Channel] : State.Progress[Channel] + FMath::Sign(Remaining) * Step;
				State.Velocity[Channel] = 0.f;
				bArrived = bArrived && State.Progress[Channel] == State.Target[Channel];
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

// =====================================================================================================
// Transform value targets
// =====================================================================================================

namespace
{
	struct FTransformTargetRow
	{
		EWidgetAnimTransformValueTarget Target;
		EWidgetAnimTransformPart Part;
		bool bX;
		bool bY;
	};

	// Every target, the field it moves and its axes. A field with one value is X only.
	const FTransformTargetRow TransformTargetRows[] =
	{
		{ EWidgetAnimTransformValueTarget::WholePath, EWidgetAnimTransformPart::WholePath, true, true },
		{ EWidgetAnimTransformValueTarget::Angle, EWidgetAnimTransformPart::Angle, true, false },
		{ EWidgetAnimTransformValueTarget::Scale, EWidgetAnimTransformPart::Scale, true, true },
		{ EWidgetAnimTransformValueTarget::ScaleX, EWidgetAnimTransformPart::Scale, true, false },
		{ EWidgetAnimTransformValueTarget::ScaleY, EWidgetAnimTransformPart::Scale, false, true },
		{ EWidgetAnimTransformValueTarget::ShapeAngle, EWidgetAnimTransformPart::ShapeAngle, true, true },
		{ EWidgetAnimTransformValueTarget::ShapeAngleX, EWidgetAnimTransformPart::ShapeAngle, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeAngleY, EWidgetAnimTransformPart::ShapeAngle, false, true },
		{ EWidgetAnimTransformValueTarget::ShapeBend, EWidgetAnimTransformPart::ShapeBend, true, true },
		{ EWidgetAnimTransformValueTarget::ShapeBendX, EWidgetAnimTransformPart::ShapeBend, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeBendY, EWidgetAnimTransformPart::ShapeBend, false, true },
		{ EWidgetAnimTransformValueTarget::ShapeCornerBottomLeft, EWidgetAnimTransformPart::ShapeCornerBottomLeft, true, true },
		{ EWidgetAnimTransformValueTarget::ShapeCornerBottomLeftX, EWidgetAnimTransformPart::ShapeCornerBottomLeft, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeCornerBottomLeftY, EWidgetAnimTransformPart::ShapeCornerBottomLeft, false, true },
		{ EWidgetAnimTransformValueTarget::ShapeCornerBottomRight, EWidgetAnimTransformPart::ShapeCornerBottomRight, true, true },
		{ EWidgetAnimTransformValueTarget::ShapeCornerBottomRightX, EWidgetAnimTransformPart::ShapeCornerBottomRight, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeCornerBottomRightY, EWidgetAnimTransformPart::ShapeCornerBottomRight, false, true },
		{ EWidgetAnimTransformValueTarget::ShapeCornerTopLeft, EWidgetAnimTransformPart::ShapeCornerTopLeft, true, true },
		{ EWidgetAnimTransformValueTarget::ShapeCornerTopLeftX, EWidgetAnimTransformPart::ShapeCornerTopLeft, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeCornerTopLeftY, EWidgetAnimTransformPart::ShapeCornerTopLeft, false, true },
		{ EWidgetAnimTransformValueTarget::ShapeCornerTopRight, EWidgetAnimTransformPart::ShapeCornerTopRight, true, true },
		{ EWidgetAnimTransformValueTarget::ShapeCornerTopRightX, EWidgetAnimTransformPart::ShapeCornerTopRight, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeCornerTopRightY, EWidgetAnimTransformPart::ShapeCornerTopRight, false, true },
		{ EWidgetAnimTransformValueTarget::ShapeEdgeBowBottom, EWidgetAnimTransformPart::ShapeEdgeBowBottom, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeEdgeBowLeft, EWidgetAnimTransformPart::ShapeEdgeBowLeft, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeEdgeBowRight, EWidgetAnimTransformPart::ShapeEdgeBowRight, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeEdgeBowTop, EWidgetAnimTransformPart::ShapeEdgeBowTop, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeExtrude, EWidgetAnimTransformPart::ShapeExtrude, true, true },
		{ EWidgetAnimTransformValueTarget::ShapeExtrudeX, EWidgetAnimTransformPart::ShapeExtrude, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeExtrudeY, EWidgetAnimTransformPart::ShapeExtrude, false, true },
		{ EWidgetAnimTransformValueTarget::ShapeInset, EWidgetAnimTransformPart::ShapeInset, true, true },
		{ EWidgetAnimTransformValueTarget::ShapeInsetX, EWidgetAnimTransformPart::ShapeInset, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeInsetY, EWidgetAnimTransformPart::ShapeInset, false, true },
		{ EWidgetAnimTransformValueTarget::ShapePerspective, EWidgetAnimTransformPart::ShapePerspective, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeRotation, EWidgetAnimTransformPart::ShapeRotation, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeWrap, EWidgetAnimTransformPart::ShapeWrap, true, true },
		{ EWidgetAnimTransformValueTarget::ShapeWrapX, EWidgetAnimTransformPart::ShapeWrap, true, false },
		{ EWidgetAnimTransformValueTarget::ShapeWrapY, EWidgetAnimTransformPart::ShapeWrap, false, true },
		{ EWidgetAnimTransformValueTarget::Shear, EWidgetAnimTransformPart::Shear, true, true },
		{ EWidgetAnimTransformValueTarget::ShearX, EWidgetAnimTransformPart::Shear, true, false },
		{ EWidgetAnimTransformValueTarget::ShearY, EWidgetAnimTransformPart::Shear, false, true },
		{ EWidgetAnimTransformValueTarget::Translation, EWidgetAnimTransformPart::Translation, true, true },
		{ EWidgetAnimTransformValueTarget::TranslationX, EWidgetAnimTransformPart::Translation, true, false },
		{ EWidgetAnimTransformValueTarget::TranslationY, EWidgetAnimTransformPart::Translation, false, true },
	};
}

EWidgetAnimTransformPart WidgetAnimValue::GetTransformPart(EWidgetAnimTransformValueTarget Target, bool& bOutX, bool& bOutY)
{
	for (const FTransformTargetRow& Row : TransformTargetRows)
	{
		if (Row.Target == Target)
		{
			bOutX = Row.bX;
			bOutY = Row.bY;
			return Row.Part;
		}
	}

	bOutX = true;
	bOutY = true;
	return EWidgetAnimTransformPart::WholePath;
}

bool WidgetAnimValue::TransformTargetTakesTwoAxes(EWidgetAnimTransformValueTarget Target)
{
	bool bX = true;
	bool bY = true;
	GetTransformPart(Target, bX, bY);
	return bX && bY;
}

bool WidgetAnimValue::IsShapePart(EWidgetAnimTransformPart Part)
{
	return Part != EWidgetAnimTransformPart::WholePath
		&& Part != EWidgetAnimTransformPart::Translation
		&& Part != EWidgetAnimTransformPart::Scale
		&& Part != EWidgetAnimTransformPart::Shear
		&& Part != EWidgetAnimTransformPart::Angle;
}
