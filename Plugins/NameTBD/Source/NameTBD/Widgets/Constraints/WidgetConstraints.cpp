// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetConstraints.h"

#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "NameTBD/Widgets/WidgetEffectsDraw.h"
#include "NameTBD/Widgets/WidgetTreeLookup.h"
#include "NameTBD/Widgets/Paths/WidgetPathLayer.h"
#include "NameTBD/Widgets/Shadow/DropShadow.h"
#include "NameTBD/Widgets/Shapes/ShapeWidget.h"
#include "NameTBD/Widgets/Shapes/ShapeWidgets.h"

// =====================================================================================================
// Math
// =====================================================================================================

namespace WidgetConstraintMath
{
	FWidgetConstraintPose PoseFromTransform(const FSlateRenderTransform& LocalToHost, const FVector2f& Size, const FVector2f& Anchor)
	{
		FWidgetConstraintPose Pose;
		const FVector2f Point = LocalToHost.TransformPoint(Size * Anchor);
		const FVector2f AxisX = LocalToHost.TransformVector(FVector2f(1.f, 0.f));
		const FVector2f AxisY = LocalToHost.TransformVector(FVector2f(0.f, 1.f));
		Pose.Position = FVector2D(Point.X, Point.Y);
		Pose.Angle = FMath::RadiansToDegrees(FMath::Atan2(AxisX.Y, AxisX.X));
		const float Handedness = FVector2f::CrossProduct(AxisX, AxisY) < 0.f ? -1.f : 1.f;
		Pose.Scale = FVector2D(AxisX.Size(), AxisY.Size() * Handedness);
		return Pose;
	}

	float AngleDelta(float A, float B)
	{
		return FMath::UnwindDegrees(B - A);
	}

	namespace
	{
		/** The polyline's points with the closing point added for a closed one. */
		const TArray<FVector2f>& Walk(const TArray<FVector2f>& Points, bool bClosed, TArray<FVector2f>& Scratch)
		{
			if (!bClosed || Points.Num() < 2)
			{
				return Points;
			}
			Scratch = Points;
			Scratch.Add(Points[0]);
			return Scratch;
		}
	}

	float ClosestDistance(const TArray<FVector2f>& Points, bool bClosed, const FVector2f& Point)
	{
		TArray<FVector2f> Scratch;
		const TArray<FVector2f>& Line = Walk(Points, bClosed, Scratch);
		float Best = TNumericLimits<float>::Max();
		float BestDistance = 0.f;
		float Walked = 0.f;
		for (int32 Index = 1; Index < Line.Num(); ++Index)
		{
			const FVector2f From = Line[Index - 1];
			const FVector2f Segment = Line[Index] - From;
			const float Length = Segment.Size();
			if (Length <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const float Along = FMath::Clamp(FVector2f::DotProduct(Point - From, Segment) / (Length * Length), 0.f, 1.f);
			const float DistanceSquared = FVector2f::DistSquared(From + Segment * Along, Point);
			if (DistanceSquared < Best)
			{
				Best = DistanceSquared;
				BestDistance = Walked + Along * Length;
			}
			Walked += Length;
		}
		return BestDistance;
	}

	bool Sample(const TArray<FVector2f>& Points, bool bClosed, float Distance, FVector2f& OutPoint, FVector2f& OutDirection)
	{
		TArray<FVector2f> Scratch;
		const TArray<FVector2f>& Line = Walk(Points, bClosed, Scratch);
		if (Line.Num() < 2)
		{
			return false;
		}
		float Walked = 0.f;
		for (int32 Index = 1; Index < Line.Num(); ++Index)
		{
			const FVector2f From = Line[Index - 1];
			const FVector2f To = Line[Index];
			const float Length = FVector2f::Distance(From, To);
			if (Length <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			if (Walked + Length >= Distance || Index == Line.Num() - 1)
			{
				const float Alpha = FMath::Clamp((Distance - Walked) / Length, 0.f, 1.f);
				OutPoint = FMath::Lerp(From, To, Alpha);
				OutDirection = (To - From) / Length;
				return true;
			}
			Walked += Length;
		}
		OutPoint = Line.Last();
		OutDirection = FVector2f(1.f, 0.f);
		return true;
	}

	void OffsetLine(TArray<FVector2f>& Points, bool bClosed, float Offset)
	{
		const int32 Count = Points.Num();
		if (Count < 2 || FMath::IsNearlyZero(Offset))
		{
			return;
		}
		// Outward for a closed line: the side away from what it encloses (signed area says which way it winds).
		float Side = 1.f;
		if (bClosed)
		{
			Side = WidgetEffectsDraw::SignedArea(Points) > 0.f ? -1.f : 1.f;
		}
		TArray<FVector2f> Moved;
		Moved.SetNumUninitialized(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const bool bHasPrevious = bClosed || Index > 0;
			const bool bHasNext = bClosed || Index < Count - 1;
			const FVector2f Previous = Points[(Index - 1 + Count) % Count];
			const FVector2f Next = Points[(Index + 1) % Count];
			FVector2f Tangent = FVector2f::ZeroVector;
			if (bHasPrevious)
			{
				Tangent += (Points[Index] - Previous).GetSafeNormal();
			}
			if (bHasNext)
			{
				Tangent += (Next - Points[Index]).GetSafeNormal();
			}
			Tangent = Tangent.GetSafeNormal();
			const FVector2f Normal(Tangent.Y, -Tangent.X);
			Moved[Index] = Points[Index] + Normal * (Offset * Side);
		}
		Points = MoveTemp(Moved);
	}
}

FWidgetConstraintPose FWidgetConstraintPose::Lerp(const FWidgetConstraintPose& A, const FWidgetConstraintPose& B, float Alpha)
{
	FWidgetConstraintPose Out;
	Out.Position = FMath::Lerp(A.Position, B.Position, static_cast<double>(Alpha));
	Out.Angle = A.Angle + WidgetConstraintMath::AngleDelta(A.Angle, B.Angle) * Alpha;
	Out.Scale = FMath::Lerp(A.Scale, B.Scale, static_cast<double>(Alpha));
	Out.Opacity = FMath::Lerp(A.Opacity, B.Opacity, Alpha);
	return Out;
}

// =====================================================================================================
// Context
// =====================================================================================================

UWidget* FWidgetConstraintContext::FindWidget(FName Name) const
{
	return WidgetTreeLookup::FindWidget(Owner, Name);
}

bool FWidgetConstraintContext::GetWidgetFrame(const UWidget* Widget, FSlateRenderTransform& OutLocalToHost, FVector2f& OutSize) const
{
	return Widget && ResolveWidget && ResolveWidget(Widget, OutLocalToHost, OutSize);
}

FBox2f FWidgetConstraintContext::GetRestBox() const
{
	const FVector2f Min = FVector2f(static_cast<float>(Rest.Position.X), static_cast<float>(Rest.Position.Y)) - PivotPoint;
	return FBox2f(Min, Min + Size);
}

namespace WidgetConstraintsPrivate
{
	FVector2f ToFloat(const FVector2D& Value)
	{
		return FVector2f(static_cast<float>(Value.X), static_cast<float>(Value.Y));
	}

	FVector2D ToDouble(const FVector2f& Value)
	{
		return FVector2D(Value.X, Value.Y);
	}

	float DistanceToBox(const FBox2f& Box, const FVector2f& Point)
	{
		const float DX = FMath::Max3(Box.Min.X - Point.X, 0.f, Point.X - Box.Max.X);
		const float DY = FMath::Max3(Box.Min.Y - Point.Y, 0.f, Point.Y - Box.Max.Y);
		return FMath::Sqrt(DX * DX + DY * DY);
	}

	/** A ring of segments round Center, for the designer's lines. */
	void AddCircle(TArray<FVector2f>& OutSegments, const FVector2f& Center, const FVector2f& Radii)
	{
		constexpr int32 Steps = 48;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float A = UE_TWO_PI * Step / Steps;
			const float B = UE_TWO_PI * (Step + 1) / Steps;
			OutSegments.Add(Center + FVector2f(FMath::Cos(A) * Radii.X, FMath::Sin(A) * Radii.Y));
			OutSegments.Add(Center + FVector2f(FMath::Cos(B) * Radii.X, FMath::Sin(B) * Radii.Y));
		}
	}

	/** An ellipse filling Size, from its top, clockwise. */
	void MakeEllipse(const FVector2f& Center, const FVector2f& Radii, float StartDegrees, TArray<FVector2f>& Out)
	{
		constexpr int32 Steps = 96;
		Out.Reset();
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float Angle = FMath::DegreesToRadians(StartDegrees) + UE_TWO_PI * Step / Steps;
			Out.Add(Center + FVector2f(FMath::Sin(Angle) * Radii.X, -FMath::Cos(Angle) * Radii.Y));
		}
	}

	/** Whether a widget is drawn as an ellipse, and otherwise its rounded corners. */
	bool ReadRound(const UWidget* Widget, FVector4f& OutRadii, bool& bOutPill)
	{
		if (Widget && Widget->IsA<UEllipseShape>())
		{
			return true;
		}
		const FDropShadowCaster Caster = UDropShadowComponent::ReadCaster(Widget);
		OutRadii = Caster.Radii;
		bOutPill = Caster.bPill;
		return false;
	}

	/** The outermost User Widget a widget is in - the screen it is part of. */
	const UWidget* FindScreenRoot(const UWidget* Widget)
	{
		const UUserWidget* Top = Widget ? Widget->GetTypedOuter<UUserWidget>() : nullptr;
		while (Top)
		{
			const UUserWidget* Next = Top->GetTypedOuter<UUserWidget>();
			if (!Next)
			{
				break;
			}
			Top = Next;
		}
		return Top;
	}
}

// =====================================================================================================
// UWidgetConstraint
// =====================================================================================================

UWidgetConstraint::UWidgetConstraint()
{
	Smoothing.VisualDuration = 0.35f;
	Smoothing.Bounce = 0.15f;
}

TArray<FString> UWidgetConstraint::GetWidgetOptions() const
{
	TArray<FString> Names;
	WidgetTreeLookup::GetWidgetNames(this, Names);
	return Names;
}

void UWidgetConstraint::ResetMotion()
{
	bActivationStarted = false;
	bSmoothStarted = false;
	bHasLastPointer = false;
}

bool UWidgetConstraint::SetChannel(EWidgetConstraintChannel Channel, float Value)
{
	if (Channel == EWidgetConstraintChannel::Strength)
	{
		SetStrength(Value);
		return true;
	}
	return false;
}

bool UWidgetConstraint::GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const
{
	if (Channel == EWidgetConstraintChannel::Strength)
	{
		OutValue = Strength;
		return true;
	}
	return false;
}

UWidget* UWidgetConstraint::ResolveTargetWidget(const FWidgetConstraintContext& Context, const FWidgetConstraintTarget& Target) const
{
	UWidget* Found = nullptr;
	switch (Target.Kind)
	{
	case EWidgetConstraintTargetKind::Widget:
		Found = TargetOverride.IsValid() ? TargetOverride.Get() : Context.FindWidget(Target.Widget);
		break;
	case EWidgetConstraintTargetKind::Parent:
		Found = Context.Owner ? Context.Owner->GetParent() : nullptr;
		break;
	default:
		break;
	}
	return Found != Context.Owner ? Found : nullptr;
}

bool UWidgetConstraint::GetTargetPose(const FWidgetConstraintContext& Context, const FWidgetConstraintTarget& Target, FWidgetConstraintPose& OutPose)
{
	using namespace WidgetConstraintsPrivate;

	OutPose = FWidgetConstraintPose();
	switch (Target.Kind)
	{
	case EWidgetConstraintTargetKind::Widget:
	case EWidgetConstraintTargetKind::Parent:
	{
		const UWidget* Widget = ResolveTargetWidget(Context, Target);
		FSlateRenderTransform Frame;
		FVector2f Size;
		if (!Context.GetWidgetFrame(Widget, Frame, Size))
		{
			return false;
		}
		OutPose = WidgetConstraintMath::PoseFromTransform(Frame, Size, ToFloat(Target.Anchor));
		OutPose.Opacity = Widget->GetRenderOpacity();
		return true;
	}
	case EWidgetConstraintTargetKind::Pointer:
		if (Context.bHasPointer)
		{
			LastPointer = Context.Pointer;
			bHasLastPointer = true;
			OutPose.Position = ToDouble(Context.Pointer);
			return true;
		}
		if (Target.WhenPointerAway == EWidgetConstraintPointerAway::HoldLast && bHasLastPointer)
		{
			OutPose.Position = ToDouble(LastPointer);
			return true;
		}
		return false;
	case EWidgetConstraintTargetKind::Point:
		OutPose.Position = Context.Rest.Position + Target.Point;
		return true;
	default:
		return false;
	}
}

float UWidgetConstraint::ComputeActivation(const FWidgetConstraintContext& Context) const
{
	switch (Activation)
	{
	case EWidgetConstraintActivation::WhileHovered:
		return Context.bPointerOver ? 1.f : 0.f;
	case EWidgetConstraintActivation::WhilePressed:
		return Context.bPressed ? 1.f : 0.f;
	case EWidgetConstraintActivation::NearPointer:
	{
		if (!Context.bHasPointer)
		{
			return 0.f;
		}
		const float Distance = WidgetConstraintsPrivate::DistanceToBox(Context.GetRestBox(), Context.Pointer);
		const float Near = 1.f - FMath::Clamp(Distance / FMath::Max(ActivationRadius, 1.f), 0.f, 1.f);
		return FMath::SmoothStep(0.f, 1.f, Near);
	}
	default:
		return 1.f;
	}
}

FWidgetConstraintPose UWidgetConstraint::Apply(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, float MasterStrength)
{
	FWidgetConstraintPose Solved = Pose;
	const bool bCanAct = bEnabled && Solve(Context, Pose, Solved);
	const float Wanted = bCanAct ? ComputeActivation(Context) : 0.f;

	if (!bActivationStarted)
	{
		ActivationValue.Snap(Wanted);
		bActivationStarted = true;
	}
	else
	{
		ActivationValue.SetTarget(Wanted, EWidgetMotion::Smooth, ActivationFade, ActivationFade <= 0.f);
	}
	ActivationValue.Advance(Context.DeltaTime);
	const float Blend = FMath::Clamp(Strength * MasterStrength * FMath::Clamp(ActivationValue.Value, 0.f, 1.f), 0.f, 1.f);

	// What the constraint adds to the pose: a move, a turn, a scale and an opacity ratio. While it can't act it keeps the
	// last change it made, fading out with its activation.
	float Wished[6];
	if (bCanAct)
	{
		Wished[0] = static_cast<float>(Solved.Position.X - Pose.Position.X);
		Wished[1] = static_cast<float>(Solved.Position.Y - Pose.Position.Y);
		Wished[2] = WidgetConstraintMath::AngleDelta(Pose.Angle, Solved.Angle);
		Wished[3] = FMath::Abs(Pose.Scale.X) > KINDA_SMALL_NUMBER ? static_cast<float>(Solved.Scale.X / Pose.Scale.X) : 1.f;
		Wished[4] = FMath::Abs(Pose.Scale.Y) > KINDA_SMALL_NUMBER ? static_cast<float>(Solved.Scale.Y / Pose.Scale.Y) : 1.f;
		Wished[5] = Pose.Opacity > KINDA_SMALL_NUMBER ? Solved.Opacity / Pose.Opacity : 1.f;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			LastChange[Index] = Wished[Index];
		}
	}
	else
	{
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Wished[Index] = LastChange[Index];
		}
	}

	float Change[6];
	Change[0] = Wished[0] * Blend;
	Change[1] = Wished[1] * Blend;
	Change[2] = Wished[2] * Blend;
	Change[3] = FMath::Lerp(1.f, Wished[3], Blend);
	Change[4] = FMath::Lerp(1.f, Wished[4], Blend);
	Change[5] = FMath::Lerp(1.f, Wished[5], Blend);

	if (bSmooth)
	{
		static const float RestDistances[6] = { 0.05f, 0.05f, 0.02f, 0.0005f, 0.0005f, 0.0005f };
		if (!bSmoothStarted)
		{
			for (int32 Index = 0; Index < 6; ++Index)
			{
				SmoothState[Index].Value = Change[Index];
				SmoothState[Index].Velocity = 0.f;
			}
			bSmoothStarted = true;
		}
		else
		{
			// An angle springs the short way toward where it is going.
			Change[2] = SmoothState[2].Value + WidgetConstraintMath::AngleDelta(SmoothState[2].Value, Change[2]);
			for (int32 Index = 0; Index < 6; ++Index)
			{
				UWidgetAnimSpringLibrary::AdvanceSpring(Smoothing, Change[Index], SmoothState[Index], Context.DeltaTime, RestDistances[Index], RestDistances[Index] * 2.f);
			}
		}
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Change[Index] = SmoothState[Index].Value;
		}
	}

	FWidgetConstraintPose Out = Pose;
	Out.Position += FVector2D(Change[0], Change[1]);
	Out.Angle += Change[2];
	Out.Scale *= FVector2D(Change[3], Change[4]);
	Out.Opacity *= Change[5];
	return Out;
}

void UWidgetConstraint::GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const
{
	if (bHasLastTarget)
	{
		OutSegments.Add(WidgetConstraintsPrivate::ToFloat(Pose.Position));
		OutSegments.Add(LastTarget);
	}
}

void UWidgetConstraint::NoteTarget(const FWidgetConstraintPose& TargetPose)
{
	LastTarget = WidgetConstraintsPrivate::ToFloat(TargetPose.Position);
	bHasLastTarget = true;
}

// =====================================================================================================
// Copy Position
// =====================================================================================================

bool UWidgetCopyPositionConstraint::Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose)
{
	using namespace WidgetConstraintsPrivate;

	OutPose = Pose;
	FVector2f Desired = ToFloat(Pose.Position);
	if (SourceSpace == EWidgetConstraintSpace::Local)
	{
		const UWidget* Widget = ResolveTargetWidget(Context, Target);
		if (!Widget)
		{
			return false;
		}
		const FVector2f Moved = ToFloat(Widget->GetRenderTransform().Translation);
		Desired.X += bCopyX ? Moved.X * static_cast<float>(CopyAmount.X) : 0.f;
		Desired.Y += bCopyY ? Moved.Y * static_cast<float>(CopyAmount.Y) : 0.f;
	}
	else
	{
		FWidgetConstraintPose TargetPose;
		if (!GetTargetPose(Context, Target, TargetPose))
		{
			return false;
		}
		FVector2f TargetPoint = ToFloat(TargetPose.Position);
		if (bKeepStartingOffset)
		{
			if (!bHasStartingOffset)
			{
				StartingOffset = ToFloat(Context.Rest.Position) - TargetPoint;
				bHasStartingOffset = true;
			}
			TargetPoint += StartingOffset;
		}
		NoteTarget(TargetPose);
		Desired.X = bCopyX ? FMath::Lerp(Desired.X, TargetPoint.X, static_cast<float>(CopyAmount.X)) : Desired.X;
		Desired.Y = bCopyY ? FMath::Lerp(Desired.Y, TargetPoint.Y, static_cast<float>(CopyAmount.Y)) : Desired.Y;
	}
	Desired += ToFloat(Offset);

	if (bLimit)
	{
		const FVector2f Rest = ToFloat(Context.Rest.Position);
		Desired.X = Rest.X + FMath::Clamp(Desired.X - Rest.X, static_cast<float>(FMath::Min(Min.X, Max.X)), static_cast<float>(FMath::Max(Min.X, Max.X)));
		Desired.Y = Rest.Y + FMath::Clamp(Desired.Y - Rest.Y, static_cast<float>(FMath::Min(Min.Y, Max.Y)), static_cast<float>(FMath::Max(Min.Y, Max.Y)));
	}
	OutPose.Position = ToDouble(Desired);
	return true;
}

void UWidgetCopyPositionConstraint::GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const
{
	OutWidgets.Add(ResolveTargetWidget(Context, Target));
}

void UWidgetCopyPositionConstraint::GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const
{
	Super::GetDebugLines(Context, Pose, OutSegments);
	if (bLimit)
	{
		const FVector2f Rest = WidgetConstraintsPrivate::ToFloat(Context.Rest.Position);
		const FVector2f A = Rest + WidgetConstraintsPrivate::ToFloat(Min);
		const FVector2f B = Rest + WidgetConstraintsPrivate::ToFloat(Max);
		const FVector2f Corners[4] = { A, FVector2f(B.X, A.Y), B, FVector2f(A.X, B.Y) };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			OutSegments.Add(Corners[Index]);
			OutSegments.Add(Corners[(Index + 1) % 4]);
		}
	}
}

bool UWidgetCopyPositionConstraint::SetChannel(EWidgetConstraintChannel Channel, float Value)
{
	switch (Channel)
	{
	case EWidgetConstraintChannel::OffsetX: Offset.X = Value; return true;
	case EWidgetConstraintChannel::OffsetY: Offset.Y = Value; return true;
	default: return Super::SetChannel(Channel, Value);
	}
}

bool UWidgetCopyPositionConstraint::GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const
{
	switch (Channel)
	{
	case EWidgetConstraintChannel::OffsetX: OutValue = static_cast<float>(Offset.X); return true;
	case EWidgetConstraintChannel::OffsetY: OutValue = static_cast<float>(Offset.Y); return true;
	default: return Super::GetChannel(Channel, OutValue);
	}
}

void UWidgetCopyPositionConstraint::ResetMotion()
{
	Super::ResetMotion();
	bHasStartingOffset = false;
}

// =====================================================================================================
// Copy Rotation / Scale / Transform
// =====================================================================================================

bool UWidgetCopyRotationConstraint::Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose)
{
	OutPose = Pose;
	float Desired = 0.f;
	if (SourceSpace == EWidgetConstraintSpace::Local)
	{
		const UWidget* Widget = ResolveTargetWidget(Context, Target);
		if (!Widget)
		{
			return false;
		}
		Desired = Pose.Angle + Widget->GetRenderTransform().Angle * CopyAmount + AngleOffset;
	}
	else
	{
		FWidgetConstraintPose TargetPose;
		if (!GetTargetPose(Context, Target, TargetPose))
		{
			return false;
		}
		NoteTarget(TargetPose);
		Desired = TargetPose.Angle * CopyAmount + AngleOffset;
	}
	if (bLimit)
	{
		const float FromRest = WidgetConstraintMath::AngleDelta(Context.Rest.Angle, Desired);
		Desired = Context.Rest.Angle + FMath::Clamp(FromRest, FMath::Min(MinAngle, MaxAngle), FMath::Max(MinAngle, MaxAngle));
	}
	OutPose.Angle = Desired;
	return true;
}

void UWidgetCopyRotationConstraint::GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const
{
	OutWidgets.Add(ResolveTargetWidget(Context, Target));
}

bool UWidgetCopyRotationConstraint::SetChannel(EWidgetConstraintChannel Channel, float Value)
{
	if (Channel == EWidgetConstraintChannel::AngleOffset)
	{
		AngleOffset = Value;
		return true;
	}
	return Super::SetChannel(Channel, Value);
}

bool UWidgetCopyRotationConstraint::GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const
{
	if (Channel == EWidgetConstraintChannel::AngleOffset)
	{
		OutValue = AngleOffset;
		return true;
	}
	return Super::GetChannel(Channel, OutValue);
}

bool UWidgetCopyScaleConstraint::Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose)
{
	OutPose = Pose;
	FVector2D Source(1.0, 1.0);
	if (SourceSpace == EWidgetConstraintSpace::Local)
	{
		const UWidget* Widget = ResolveTargetWidget(Context, Target);
		if (!Widget)
		{
			return false;
		}
		Source = Widget->GetRenderTransform().Scale;
	}
	else
	{
		FWidgetConstraintPose TargetPose;
		if (!GetTargetPose(Context, Target, TargetPose))
		{
			return false;
		}
		NoteTarget(TargetPose);
		Source = TargetPose.Scale;
	}

	const FVector2D Taken = FVector2D(1.0, 1.0) + (Source - FVector2D(1.0, 1.0)) * CopyAmount;
	FVector2D Desired = Pose.Scale;
	if (SourceSpace == EWidgetConstraintSpace::Local)
	{
		Desired.X *= bCopyX ? Taken.X : 1.0;
		Desired.Y *= bCopyY ? Taken.Y : 1.0;
	}
	else
	{
		Desired.X = bCopyX ? Taken.X : Desired.X;
		Desired.Y = bCopyY ? Taken.Y : Desired.Y;
	}
	if (bLimit)
	{
		Desired.X = FMath::Clamp(Desired.X, FMath::Min(MinScale.X, MaxScale.X), FMath::Max(MinScale.X, MaxScale.X));
		Desired.Y = FMath::Clamp(Desired.Y, FMath::Min(MinScale.Y, MaxScale.Y), FMath::Max(MinScale.Y, MaxScale.Y));
	}
	OutPose.Scale = Desired;
	return true;
}

void UWidgetCopyScaleConstraint::GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const
{
	OutWidgets.Add(ResolveTargetWidget(Context, Target));
}

bool UWidgetCopyTransformConstraint::Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose)
{
	using namespace WidgetConstraintsPrivate;

	OutPose = Pose;
	const UWidget* Widget = ResolveTargetWidget(Context, Target);
	FSlateRenderTransform Frame;
	FVector2f Size;
	if (!Widget || !Context.GetWidgetFrame(Widget, Frame, Size))
	{
		// A pointer or a point has no turn or size: only its place is copied.
		FWidgetConstraintPose TargetPose;
		if (!bCopyPosition || !GetTargetPose(Context, Target, TargetPose))
		{
			return false;
		}
		NoteTarget(TargetPose);
		OutPose.Position = TargetPose.Position + Offset;
		return true;
	}

	const FWidgetConstraintPose TargetPose = WidgetConstraintMath::PoseFromTransform(Frame, Size, ToFloat(Target.Anchor));
	NoteTarget(TargetPose);
	const FVector2f AnchorPoint = Size * ToFloat(Target.Anchor);
	if (bKeepStartingOffset && !bHasStarting)
	{
		StartingLocal = Frame.Inverse().TransformPoint(ToFloat(Context.Rest.Position)) - AnchorPoint;
		StartingAngle = WidgetConstraintMath::AngleDelta(TargetPose.Angle, Context.Rest.Angle);
		bHasStarting = true;
	}
	const FVector2f Local = bKeepStartingOffset ? StartingLocal : ToFloat(Offset);
	if (bCopyPosition)
	{
		OutPose.Position = ToDouble(Frame.TransformPoint(AnchorPoint + Local));
	}
	if (bCopyRotation)
	{
		OutPose.Angle = TargetPose.Angle + AngleOffset + (bKeepStartingOffset ? StartingAngle : 0.f);
	}
	if (bCopyScale)
	{
		OutPose.Scale = TargetPose.Scale;
	}
	return true;
}

void UWidgetCopyTransformConstraint::GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const
{
	OutWidgets.Add(ResolveTargetWidget(Context, Target));
}

void UWidgetCopyTransformConstraint::GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const
{
	Super::GetDebugLines(Context, Pose, OutSegments);
}

bool UWidgetCopyTransformConstraint::SetChannel(EWidgetConstraintChannel Channel, float Value)
{
	switch (Channel)
	{
	case EWidgetConstraintChannel::OffsetX: Offset.X = Value; return true;
	case EWidgetConstraintChannel::OffsetY: Offset.Y = Value; return true;
	case EWidgetConstraintChannel::AngleOffset: AngleOffset = Value; return true;
	default: return Super::SetChannel(Channel, Value);
	}
}

bool UWidgetCopyTransformConstraint::GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const
{
	switch (Channel)
	{
	case EWidgetConstraintChannel::OffsetX: OutValue = static_cast<float>(Offset.X); return true;
	case EWidgetConstraintChannel::OffsetY: OutValue = static_cast<float>(Offset.Y); return true;
	case EWidgetConstraintChannel::AngleOffset: OutValue = AngleOffset; return true;
	default: return Super::GetChannel(Channel, OutValue);
	}
}

void UWidgetCopyTransformConstraint::ResetMotion()
{
	Super::ResetMotion();
	bHasStarting = false;
}

// =====================================================================================================
// Distance
// =====================================================================================================

bool UWidgetDistanceConstraint::Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose)
{
	using namespace WidgetConstraintsPrivate;

	OutPose = Pose;
	FWidgetConstraintPose TargetPose;
	if (!GetTargetPose(Context, Target, TargetPose))
	{
		return false;
	}
	NoteTarget(TargetPose);
	const FVector2f Center = ToFloat(TargetPose.Position);
	const FVector2f From = ToFloat(Pose.Position) - Center;
	const float Current = From.Size();
	const FVector2f Direction = Current > KINDA_SMALL_NUMBER ? From / Current : FVector2f(1.f, 0.f);
	const bool bMove = Mode == EWidgetConstraintDistanceMode::Exact
		|| (Mode == EWidgetConstraintDistanceMode::Within && Current > Distance)
		|| (Mode == EWidgetConstraintDistanceMode::Outside && Current < Distance);
	if (bMove)
	{
		OutPose.Position = ToDouble(Center + Direction * Distance);
	}
	return true;
}

void UWidgetDistanceConstraint::GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const
{
	OutWidgets.Add(ResolveTargetWidget(Context, Target));
}

void UWidgetDistanceConstraint::GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const
{
	Super::GetDebugLines(Context, Pose, OutSegments);
	if (bHasLastTarget)
	{
		WidgetConstraintsPrivate::AddCircle(OutSegments, LastTarget, FVector2f(Distance, Distance));
	}
}

bool UWidgetDistanceConstraint::SetChannel(EWidgetConstraintChannel Channel, float Value)
{
	if (Channel == EWidgetConstraintChannel::Distance)
	{
		Distance = FMath::Max(Value, 0.f);
		return true;
	}
	return Super::SetChannel(Channel, Value);
}

bool UWidgetDistanceConstraint::GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const
{
	if (Channel == EWidgetConstraintChannel::Distance)
	{
		OutValue = Distance;
		return true;
	}
	return Super::GetChannel(Channel, OutValue);
}

// =====================================================================================================
// Follow Path
// =====================================================================================================

bool UWidgetFollowPathConstraint::BuildPath(const FWidgetConstraintContext& Context, TArray<FVector2f>& OutPoints, bool& bOutClosed)
{
	using namespace WidgetConstraintsPrivate;

	OutPoints.Reset();
	bOutClosed = true;
	switch (PathSource)
	{
	case EWidgetConstraintPathSource::Circle:
	{
		FWidgetConstraintPose Center;
		if (!GetTargetPose(Context, Target, Center))
		{
			return false;
		}
		MakeEllipse(ToFloat(Center.Position), ToFloat(Radius), StartAngle, OutPoints);
		break;
	}

	case EWidgetConstraintPathSource::WidgetPathConnection:
	{
		const UWidgetPathLayer* Layer = Cast<UWidgetPathLayer>(Context.FindWidget(PathWidget));
		TArray<FVector2D> Desktop;
		if (!Layer || !Layer->GetConnectionPath(Context.FindWidget(ConnectionFrom), Context.FindWidget(ConnectionTo), Desktop))
		{
			return false;
		}
		bOutClosed = false;
		OutPoints.Reserve(Desktop.Num());
		for (const FVector2D& Point : Desktop)
		{
			OutPoints.Add(Context.DesktopToHost.TransformPoint(ToFloat(Point)));
		}
		break;
	}

	case EWidgetConstraintPathSource::ShapeOutline:
	case EWidgetConstraintPathSource::WidgetBox:
	{
		const UWidget* Widget = Context.FindWidget(PathWidget);
		FSlateRenderTransform Frame;
		FVector2f Size;
		if (!Widget || Widget == Context.Owner || !Context.GetWidgetFrame(Widget, Frame, Size))
		{
			return false;
		}

		TArray<FVector2f> Local;
		const UShapeWidget* Shape = Cast<UShapeWidget>(Widget);
		if (PathSource == EWidgetConstraintPathSource::ShapeOutline && Shape)
		{
			// The outline as the shape draws it: its Fit, Shape Scale and Shape Rotation. The longest contour (a ring's outer edge).
			FWidgetShapeOutline Outline;
			Shape->BuildOutline(Shape->MakeFrame(Size), 0.25f, Outline);
			const FWidgetShapeLine* Longest = nullptr;
			float LongestLength = 0.f;
			for (const FWidgetShapeLine& Line : Outline.Lines)
			{
				const float Length = WidgetShapeGeometry::GetLength(Line.Points, Line.bClosed);
				if (Length > LongestLength)
				{
					LongestLength = Length;
					Longest = &Line;
				}
			}
			if (!Longest)
			{
				return false;
			}
			Local = Longest->Points;
			bOutClosed = Longest->bClosed;
		}
		else
		{
			FVector4f Radii(0.f, 0.f, 0.f, 0.f);
			bool bPill = false;
			bool bEllipse = EdgeShape == EWidgetConstraintEdgeShape::Ellipse;
			if (EdgeShape == EWidgetConstraintEdgeShape::Auto)
			{
				bEllipse = ReadRound(Widget, Radii, bPill);
			}
			if (bEllipse)
			{
				MakeEllipse(Size * 0.5f, Size * 0.5f, 0.f, Local);
			}
			else
			{
				if (bPill)
				{
					const float Half = FMath::Min(Size.X, Size.Y) * 0.5f;
					Radii = FVector4f(Half, Half, Half, Half);
				}
				FWidgetShapeOutline Outline;
				WidgetEffectsDraw::AppendRoundedRect(Outline, FVector2f::ZeroVector, Size, WidgetEffectsDraw::FitRadii(Radii, Size), 0.25f);
				if (Outline.Lines.Num() == 0)
				{
					return false;
				}
				Local = Outline.Lines[0].Points;
			}
		}
		OutPoints.Reserve(Local.Num());
		for (const FVector2f& Point : Local)
		{
			OutPoints.Add(Frame.TransformPoint(Point));
		}
		break;
	}
	}

	WidgetConstraintMath::OffsetLine(OutPoints, bOutClosed, PathOffset);
	return OutPoints.Num() >= 2;
}

bool UWidgetFollowPathConstraint::Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose)
{
	using namespace WidgetConstraintsPrivate;

	OutPose = Pose;
	TArray<FVector2f> Points;
	bool bClosed = true;
	if (!BuildPath(Context, Points, bClosed))
	{
		DebugPath.Reset();
		return false;
	}
	DebugPath = Points;
	bDebugClosed = bClosed;

	const float Length = WidgetShapeGeometry::GetLength(Points, bClosed);
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const bool bWraps = bClosed || bLoop;

	float Distance = 0.f;
	if (Mode == EWidgetConstraintPathMode::ClosestToTarget)
	{
		FWidgetConstraintPose TargetPose;
		if (!GetTargetPose(Context, Target, TargetPose))
		{
			return false;
		}
		NoteTarget(TargetPose);
		Distance = WidgetConstraintMath::ClosestDistance(Points, bClosed, ToFloat(TargetPose.Position));
	}
	else
	{
		Travelled += Speed * Context.DeltaTime;
		if (bWraps)
		{
			// Kept small: whole turns don't change where it is.
			const float Turn = bPositionInPixels ? Length : 1.f;
			Travelled = FMath::Fmod(Travelled, Turn);
		}
		const float Position = PathPosition + Travelled;
		Distance = bPositionInPixels ? Position : Position * Length;
	}

	if (bWraps)
	{
		Distance = FMath::Fmod(Distance, Length);
		if (Distance < 0.f)
		{
			Distance += Length;
		}
	}
	else
	{
		Distance = FMath::Clamp(Distance, 0.f, Length);
	}
	CurrentFraction = Distance / Length;

	FVector2f Point;
	FVector2f Direction;
	if (!WidgetConstraintMath::Sample(Points, bClosed, Distance, Point, Direction))
	{
		return false;
	}
	OutPose.Position = ToDouble(Point);
	if (bOrient)
	{
		const float PathAngle = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X)) + OrientOffset;
		OutPose.Angle = Pose.Angle + WidgetConstraintMath::AngleDelta(Pose.Angle, PathAngle);
	}
	return true;
}

void UWidgetFollowPathConstraint::GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const
{
	OutWidgets.Add(Context.FindWidget(PathWidget));
	OutWidgets.Add(ResolveTargetWidget(Context, Target));
}

void UWidgetFollowPathConstraint::GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const
{
	Super::GetDebugLines(Context, Pose, OutSegments);
	const int32 Count = DebugPath.Num();
	for (int32 Index = 1; Index < Count; ++Index)
	{
		OutSegments.Add(DebugPath[Index - 1]);
		OutSegments.Add(DebugPath[Index]);
	}
	if (bDebugClosed && Count > 2)
	{
		OutSegments.Add(DebugPath.Last());
		OutSegments.Add(DebugPath[0]);
	}
}

bool UWidgetFollowPathConstraint::SetChannel(EWidgetConstraintChannel Channel, float Value)
{
	switch (Channel)
	{
	case EWidgetConstraintChannel::PathPosition: PathPosition = Value; return true;
	case EWidgetConstraintChannel::AngleOffset: OrientOffset = Value; return true;
	default: return Super::SetChannel(Channel, Value);
	}
}

bool UWidgetFollowPathConstraint::GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const
{
	switch (Channel)
	{
	case EWidgetConstraintChannel::PathPosition: OutValue = PathPosition; return true;
	case EWidgetConstraintChannel::AngleOffset: OutValue = OrientOffset; return true;
	default: return Super::GetChannel(Channel, OutValue);
	}
}

void UWidgetFollowPathConstraint::ResetMotion()
{
	Super::ResetMotion();
	Travelled = 0.f;
}

// =====================================================================================================
// Look At
// =====================================================================================================

bool UWidgetLookAtConstraint::Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose)
{
	using namespace WidgetConstraintsPrivate;

	OutPose = Pose;
	FWidgetConstraintPose TargetPose;
	if (!GetTargetPose(Context, Target, TargetPose))
	{
		return false;
	}
	NoteTarget(TargetPose);
	const FVector2f Toward = ToFloat(TargetPose.Position) - ToFloat(Pose.Position);
	if (Toward.SizeSquared() < 0.25f)
	{
		// On top of it: no way to face, so it stays as it is.
		return true;
	}

	const float Heading = FMath::RadiansToDegrees(FMath::Atan2(Toward.Y, Toward.X));
	float Desired = Heading - ForwardAngle + AngleOffset;
	if (bLimit)
	{
		const float FromRest = WidgetConstraintMath::AngleDelta(Context.Rest.Angle, Desired);
		Desired = Context.Rest.Angle + FMath::Clamp(FromRest, FMath::Min(MinAngle, MaxAngle), FMath::Max(MinAngle, MaxAngle));
	}
	OutPose.Angle = Pose.Angle + WidgetConstraintMath::AngleDelta(Pose.Angle, Desired);
	if (bKeepUpright)
	{
		const bool bFacingLeft = FMath::Abs(FMath::UnwindDegrees(OutPose.Angle + ForwardAngle)) > 90.f;
		OutPose.Scale.Y = FMath::Abs(Pose.Scale.Y) * (bFacingLeft ? -1.0 : 1.0);
	}
	return true;
}

void UWidgetLookAtConstraint::GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const
{
	OutWidgets.Add(ResolveTargetWidget(Context, Target));
}

void UWidgetLookAtConstraint::GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const
{
	Super::GetDebugLines(Context, Pose, OutSegments);
}

bool UWidgetLookAtConstraint::SetChannel(EWidgetConstraintChannel Channel, float Value)
{
	if (Channel == EWidgetConstraintChannel::AngleOffset)
	{
		AngleOffset = Value;
		return true;
	}
	return Super::SetChannel(Channel, Value);
}

bool UWidgetLookAtConstraint::GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const
{
	if (Channel == EWidgetConstraintChannel::AngleOffset)
	{
		OutValue = AngleOffset;
		return true;
	}
	return Super::GetChannel(Channel, OutValue);
}

// =====================================================================================================
// Stay Inside
// =====================================================================================================

const UWidget* UWidgetStayInsideConstraint::GetAreaWidget(const FWidgetConstraintContext& Context) const
{
	const UWidget* Found = Area == EWidgetConstraintArea::Parent ? (Context.Owner ? Context.Owner->GetParent() : nullptr) : Context.FindWidget(AreaWidget);
	return Found != Context.Owner ? Found : nullptr;
}

bool UWidgetStayInsideConstraint::Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose)
{
	using namespace WidgetConstraintsPrivate;

	OutPose = Pose;
	const UWidget* AreaOf = GetAreaWidget(Context);
	FSlateRenderTransform Frame;
	FVector2f AreaSize;
	if (!AreaOf || !Context.GetWidgetFrame(AreaOf, Frame, AreaSize))
	{
		return false;
	}

	// Worked out in the area's own space, where it is a box from (0, 0) to its size.
	const FSlateRenderTransform ToArea = Frame.Inverse();
	const FVector2f Local = ToArea.TransformPoint(ToFloat(Pose.Position));
	const FVector2f AreaScale(FMath::Max(Frame.TransformVector(FVector2f(1.f, 0.f)).Size(), KINDA_SMALL_NUMBER), FMath::Max(Frame.TransformVector(FVector2f(0.f, 1.f)).Size(), KINDA_SMALL_NUMBER));

	// How far the widget reaches from its pivot each way, in the area's units.
	FVector2f Before = FVector2f::ZeroVector;
	FVector2f After = FVector2f::ZeroVector;
	if (bKeepWholeWidgetInside)
	{
		const FVector2f Scale(FMath::Abs(static_cast<float>(Pose.Scale.X)), FMath::Abs(static_cast<float>(Pose.Scale.Y)));
		Before = Context.PivotPoint * Scale / AreaScale;
		After = (Context.Size - Context.PivotPoint) * Scale / AreaScale;
	}

	bool bEllipse = AreaShape == EWidgetConstraintEdgeShape::Ellipse;
	if (AreaShape == EWidgetConstraintEdgeShape::Auto)
	{
		FVector4f Radii;
		bool bPill = false;
		bEllipse = ReadRound(AreaOf, Radii, bPill);
	}

	FVector2f Clamped = Local;
	if (bEllipse)
	{
		const FVector2f Center = AreaSize * 0.5f;
		const FVector2f Reach((FMath::Max(Before.X, After.X)), FMath::Max(Before.Y, After.Y));
		const FVector2f Radii(FMath::Max(AreaSize.X * 0.5f - Margin - Reach.X, 0.f), FMath::Max(AreaSize.Y * 0.5f - Margin - Reach.Y, 0.f));
		const FVector2f Offset = Local - Center;
		const FVector2f Scaled(Radii.X > KINDA_SMALL_NUMBER ? Offset.X / Radii.X : 0.f, Radii.Y > KINDA_SMALL_NUMBER ? Offset.Y / Radii.Y : 0.f);
		const float Reached = Scaled.Size();
		if (Reached > 1.f)
		{
			Clamped = Center + FVector2f(Scaled.X / Reached * Radii.X, Scaled.Y / Reached * Radii.Y);
		}
	}
	else
	{
		const FVector2f Min(Margin + Before.X, Margin + Before.Y);
		const FVector2f Max(AreaSize.X - Margin - After.X, AreaSize.Y - Margin - After.Y);
		Clamped.X = Min.X <= Max.X ? FMath::Clamp(Local.X, Min.X, Max.X) : (Min.X + Max.X) * 0.5f;
		Clamped.Y = Min.Y <= Max.Y ? FMath::Clamp(Local.Y, Min.Y, Max.Y) : (Min.Y + Max.Y) * 0.5f;
	}

	// A rubber-band edge lets part of the overshoot through.
	const FVector2f Result = Clamped + (Local - Clamped) * Elasticity;
	OutPose.Position = ToDouble(Frame.TransformPoint(Result));
	return true;
}

void UWidgetStayInsideConstraint::GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const
{
	OutWidgets.Add(GetAreaWidget(Context));
}

// =====================================================================================================
// Remap
// =====================================================================================================

bool UWidgetRemapConstraint::Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose)
{
	using namespace WidgetConstraintsPrivate;

	OutPose = Pose;
	FWidgetConstraintPose TargetPose;
	if (!GetTargetPose(Context, Target, TargetPose))
	{
		return false;
	}
	NoteTarget(TargetPose);

	// Measured from where the widget rests, so what it drives doesn't feed back into what it measures.
	const FVector2f Rest = ToFloat(Context.Rest.Position);
	const FVector2f TargetPoint = ToFloat(TargetPose.Position);
	const UWidget* Widget = ResolveTargetWidget(Context, Target);
	float Measured = 0.f;
	switch (Measure)
	{
	case EWidgetConstraintMeasure::OffsetX: Measured = TargetPoint.X - Rest.X; break;
	case EWidgetConstraintMeasure::OffsetY: Measured = TargetPoint.Y - Rest.Y; break;
	case EWidgetConstraintMeasure::Distance:
		Measured = bMeasureFromEdges ? DistanceToBox(Context.GetRestBox(), TargetPoint) : FVector2f::Distance(Rest, TargetPoint);
		break;
	case EWidgetConstraintMeasure::Direction:
		Measured = FMath::RadiansToDegrees(FMath::Atan2(TargetPoint.Y - Rest.Y, TargetPoint.X - Rest.X));
		break;
	case EWidgetConstraintMeasure::TargetAngle:
		Measured = Widget ? Widget->GetRenderTransform().Angle : 0.f;
		break;
	case EWidgetConstraintMeasure::TargetScale:
		Measured = Widget ? static_cast<float>(Widget->GetRenderTransform().Scale.X) : 1.f;
		break;
	case EWidgetConstraintMeasure::TargetOpacity:
		Measured = Widget ? Widget->GetRenderOpacity() : 1.f;
		break;
	}

	const float Span = static_cast<float>(FromRange.Y - FromRange.X);
	float Alpha = FMath::IsNearlyZero(Span) ? 0.f : (Measured - static_cast<float>(FromRange.X)) / Span;
	if (bClamp)
	{
		Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
	}
	if (bUseCurve)
	{
		if (const FRichCurve* Shape = Curve.GetRichCurveConst())
		{
			if (Shape->GetNumKeys() > 0)
			{
				Alpha = Shape->Eval(Alpha);
			}
		}
	}
	const float Value = FMath::Lerp(static_cast<float>(ToRange.X), static_cast<float>(ToRange.Y), Alpha);

	switch (Drive)
	{
	case EWidgetConstraintDrive::OffsetX: OutPose.Position.X += Value; break;
	case EWidgetConstraintDrive::OffsetY: OutPose.Position.Y += Value; break;
	case EWidgetConstraintDrive::Angle: OutPose.Angle += Value; break;
	case EWidgetConstraintDrive::Scale: OutPose.Scale *= Value; break;
	case EWidgetConstraintDrive::ScaleX: OutPose.Scale.X *= Value; break;
	case EWidgetConstraintDrive::ScaleY: OutPose.Scale.Y *= Value; break;
	case EWidgetConstraintDrive::Opacity: OutPose.Opacity *= FMath::Max(Value, 0.f); break;
	}
	return true;
}

void UWidgetRemapConstraint::GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const
{
	OutWidgets.Add(ResolveTargetWidget(Context, Target));
}

// =====================================================================================================
// Pointer Parallax
// =====================================================================================================

const UWidget* UWidgetPointerParallaxConstraint::GetAreaWidget(const FWidgetConstraintContext& Context) const
{
	if (Area == EWidgetConstraintParallaxArea::Widget)
	{
		const UWidget* Found = Context.FindWidget(AreaWidget);
		if (Found)
		{
			return Found;
		}
	}
	return WidgetConstraintsPrivate::FindScreenRoot(Context.Owner);
}

bool UWidgetPointerParallaxConstraint::Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose)
{
	OutPose = Pose;
	if (!Context.bHasPointer)
	{
		return false;
	}
	const UWidget* AreaOf = GetAreaWidget(Context);
	FSlateRenderTransform Frame;
	FVector2f AreaSize;
	if (!AreaOf || !Context.GetWidgetFrame(AreaOf, Frame, AreaSize) || AreaSize.X <= 0.f || AreaSize.Y <= 0.f)
	{
		return false;
	}

	// -1 at the area's left / top edge, 1 at its right / bottom.
	const FVector2f Local = Frame.Inverse().TransformPoint(Context.Pointer);
	const FVector2f Centered(FMath::Clamp(Local.X / AreaSize.X * 2.f - 1.f, -1.f, 1.f), FMath::Clamp(Local.Y / AreaSize.Y * 2.f - 1.f, -1.f, 1.f));
	OutPose.Position += FVector2D(Centered.X * Depth.X, Centered.Y * Depth.Y);
	OutPose.Angle += Centered.X * Tilt;
	return true;
}

void UWidgetPointerParallaxConstraint::GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const
{
	if (Area == EWidgetConstraintParallaxArea::Widget)
	{
		OutWidgets.Add(Context.FindWidget(AreaWidget));
	}
}

bool UWidgetPointerParallaxConstraint::SetChannel(EWidgetConstraintChannel Channel, float Value)
{
	if (Channel == EWidgetConstraintChannel::Distance)
	{
		Depth = FVector2D(Value, Value);
		return true;
	}
	return Super::SetChannel(Channel, Value);
}

bool UWidgetPointerParallaxConstraint::GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const
{
	if (Channel == EWidgetConstraintChannel::Distance)
	{
		OutValue = static_cast<float>(Depth.X);
		return true;
	}
	return Super::GetChannel(Channel, OutValue);
}
