// Copyright AR GameSystems 2026. All rights reserved.

#include "FlowListTypes.h"

// =====================================================================================================
// Channels
// =====================================================================================================

float FFlowListChannel::Evaluate(float SignedDistance, float Default) const
{
	switch (Mode)
	{
	case EFlowListChannelMode::Off:
		return Default;

	case EFlowListChannelMode::Curve:
	{
		const FRichCurve* Rich = Curve.GetRichCurveConst();
		return Rich && Rich->GetNumKeys() > 0 ? Rich->Eval(SignedDistance, Default) : Default;
	}

	case EFlowListChannelMode::Falloff:
	{
		const bool bBefore = SignedDistance < 0.f;
		const bool bSeparate = bBefore && Mirror == EFlowListMirror::Separate;
		const float Edge = bSeparate ? BeforeAtEdge : AtEdge;
		const float Span = FMath::Max(bSeparate ? BeforeDistance : Distance, 0.01f);

		const float Alpha = FMath::Abs(SignedDistance) / Span;
		const float Shaped = (bExtrapolate && Alpha > 1.f) ? Alpha : WidgetAnimEasing::Evaluate(Easing, EasingCurve, Alpha);
		const float Value = AtFocus + (Edge - AtFocus) * Shaped;

		// Opposite: the same change from At Focus, the other way - what leans the items after the focus right leans the
		// ones before it left.
		return (bBefore && Mirror == EFlowListMirror::Opposite) ? AtFocus - (Value - AtFocus) : Value;
	}
	}

	return Default;
}

FLinearColor FFlowListColorChannel::Evaluate(float SignedDistance) const
{
	if (!bEnabled)
	{
		return FLinearColor::White;
	}
	const float Alpha = WidgetAnimEasing::Evaluate(Easing, EasingCurve, FMath::Abs(SignedDistance) / FMath::Max(Distance, 0.01f));
	return FMath::Lerp(AtFocus, AtEdge, Alpha);
}

// =====================================================================================================
// Defaults
// =====================================================================================================

FFlowListScrolling::FFlowListScrolling()
{
	ScrollSpring = FlowListTypes::MakeSpring(0.32f, 0.1f);
}

FFlowListTransitions::FFlowListTransitions()
{
	Enter.Duration = 0.35f;
	Enter.Easing = EWidgetAnimEasing::EaseOut;
	Enter.Opacity = 0.f;
	Enter.Scale = 0.6f;
	Enter.Offset = FVector2D(40.0, 0.0);

	Exit.Duration = 0.25f;
	Exit.Easing = EWidgetAnimEasing::EaseIn;
	Exit.Opacity = 0.f;
	Exit.Scale = 0.75f;
	Exit.Offset = FVector2D::ZeroVector;

	ReorderSpring = FlowListTypes::MakeSpring(0.4f, 0.12f);
}

namespace FlowListTypes
{
	FWidgetSpringParams MakeSpring(float VisualDuration, float Bounce)
	{
		FWidgetSpringParams Spring;
		Spring.Mode = EWidgetSpringMode::DurationAndBounce;
		Spring.VisualDuration = VisualDuration;
		Spring.Bounce = Bounce;
		return Spring;
	}

	FFlowListTransitionState EvaluateTransition(const FFlowListTransition& Transition, float Alpha, bool bEnter)
	{
		const float Eased = WidgetAnimEasing::Evaluate(Transition.Easing, Transition.EasingCurve, Alpha);

		// Entering runs from the transition's state to rest, leaving from rest to it: the same lerp, from the other end.
		const float Toward = bEnter ? 1.f - Eased : Eased;

		FFlowListTransitionState State;
		State.Opacity = FMath::Lerp(1.f, Transition.Opacity, Toward);
		State.Scale = FMath::Lerp(1.f, Transition.Scale, Toward);
		State.Rotation = Transition.Rotation * Toward;
		State.Offset = FVector2f(Transition.Offset) * Toward;
		return State;
	}

	namespace
	{
		FFlowListChannel Falloff(float AtFocus, float AtEdge, float Distance, EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut,
			EFlowListMirror Mirror = EFlowListMirror::Same)
		{
			FFlowListChannel Channel(AtFocus, AtEdge, Distance);
			Channel.Mode = EFlowListChannelMode::Falloff;
			Channel.Easing = Easing;
			Channel.Mirror = Mirror;
			return Channel;
		}

		FFlowListChannel FalloffSplit(float AtFocus, float AtEdge, float Distance, float BeforeAtEdge, float BeforeDistance,
			EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut)
		{
			FFlowListChannel Channel = Falloff(AtFocus, AtEdge, Distance, Easing, EFlowListMirror::Separate);
			Channel.BeforeAtEdge = BeforeAtEdge;
			Channel.BeforeDistance = BeforeDistance;
			return Channel;
		}

		FFlowListColorChannel Dim(const FLinearColor& AtEdge, float Distance)
		{
			FFlowListColorChannel Channel;
			Channel.bEnabled = true;
			Channel.AtEdge = AtEdge;
			Channel.Distance = Distance;
			return Channel;
		}
	}

	void ApplyPreset(EFlowListStylePreset Preset, FFlowListLayout& Layout, FFlowListEffects& Effects, FFlowListScrolling& Scrolling, FFlowListTransitions& Transitions)
	{
		if (Preset == EFlowListStylePreset::Custom)
		{
			return;
		}

		// Every preset starts from the defaults, so picking one after another never leaves the first one's settings behind.
		// Whether changes animate and whether the intro plays are about the screen, not the look, so they are kept.
		const bool bAnimateChanges = Transitions.bAnimateChanges;
		const bool bPlayIntro = Transitions.bPlayIntro;
		Layout = FFlowListLayout();
		Effects = FFlowListEffects();
		Scrolling = FFlowListScrolling();
		Transitions = FFlowListTransitions();
		Transitions.bAnimateChanges = bAnimateChanges;
		Transitions.bPlayIntro = bPlayIntro;

		switch (Preset)
		{
		case EFlowListStylePreset::ScrollList:
			Layout.Orientation = Orient_Vertical;
			Layout.FocusAnchor = FVector2D(0.5, 0.0);
			Layout.ItemPivot = FVector2D(0.5, 0.0);
			Layout.Spacing = 8.f;
			Scrolling.bSnapToItems = false;
			Scrolling.bStopAtContentEnd = true;
			Scrolling.FlingCoast = 0.45f;
			Scrolling.bClickToFocus = false;
			Transitions.Enter.Offset = FVector2D(0.0, 24.0);
			Transitions.Enter.Scale = 1.f;
			Transitions.Exit.Scale = 1.f;
			Transitions.Exit.Offset = FVector2D(0.0, -24.0);
			break;

		case EFlowListStylePreset::Carousel:
			Layout.Orientation = Orient_Horizontal;
			Layout.Spacing = 24.f;
			Effects.Scale = Falloff(1.f, 0.82f, 1.f);
			Effects.Opacity = Falloff(1.f, 0.35f, 3.f, EWidgetAnimEasing::Linear);
			Transitions.Enter.Offset = FVector2D(0.0, 30.0);
			break;

		case EFlowListStylePreset::CoverFlow:
			Layout.Orientation = Orient_Horizontal;
			Layout.Spacing = -90.f;
			Effects.Scale = Falloff(1.f, 0.85f, 1.f);
			Effects.Squash = Falloff(1.f, 0.55f, 1.f);
			Effects.Spread = Falloff(0.f, 70.f, 1.f, EWidgetAnimEasing::EaseOut, EFlowListMirror::Opposite);
			Effects.Opacity = Falloff(1.f, 0.2f, 5.f, EWidgetAnimEasing::Linear);
			Effects.Tint = Dim(FLinearColor(0.55f, 0.55f, 0.6f, 1.f), 2.f);
			Effects.ZOrder = EFlowListZOrder::FocusOnTop;
			Scrolling.FlingCoast = 0.45f;
			break;

		case EFlowListStylePreset::PickerWheel:
			Layout.Type = EFlowListPathType::Wheel;
			Layout.Orientation = Orient_Vertical;
			Layout.WheelRadius = 150.f;
			Layout.Perspective = 0.001f;
			Layout.Spacing = 4.f;
			Effects.Opacity = Falloff(1.f, 0.3f, 3.f, EWidgetAnimEasing::Linear);
			Effects.Tint = Dim(FLinearColor(0.6f, 0.6f, 0.65f, 1.f), 1.f);
			Effects.ZOrder = EFlowListZOrder::Depth;
			Scrolling.FlingCoast = 0.5f;
			Transitions.Enter.Offset = FVector2D::ZeroVector;
			break;

		case EFlowListStylePreset::Dial:
			Layout.Type = EFlowListPathType::Arc;
			Layout.Orientation = Orient_Vertical;
			Layout.FocusAnchor = FVector2D(0.4, 0.5);
			Layout.ArcRadius = -320.f;
			Layout.bAlignToPath = true;
			Layout.Spacing = 10.f;
			Effects.Scale = Falloff(1.15f, 0.8f, 2.f);
			Effects.Opacity = Falloff(1.f, 0.3f, 3.f, EWidgetAnimEasing::Linear);
			Effects.CrossOffset = Falloff(28.f, 0.f, 1.f);
			Effects.Tint = Dim(FLinearColor(0.5f, 0.5f, 0.55f, 1.f), 1.f);
			Scrolling.bLoop = true;
			break;

		case EFlowListStylePreset::Cascade:
			// The weather strip: the focus near the top, each item after it smaller and further round a curve that dips
			// away and comes back, and the ones scrolled past fading out above it. Pop Front plays it as a push/pop list.
			Layout.Type = EFlowListPathType::Arc;
			Layout.Orientation = Orient_Vertical;
			Layout.FocusAnchor = FVector2D(0.55, 0.18);
			Layout.ArcRadius = 360.f;
			Layout.ArcApex = 170.f;
			Layout.Spacing = 6.f;
			Effects.Scale = FalloffSplit(1.f, 0.5f, 4.f, 1.15f, 1.f);
			Effects.Opacity = FalloffSplit(1.f, 0.85f, 4.f, 0.f, 1.f, EWidgetAnimEasing::Linear);
			Effects.ZOrder = EFlowListZOrder::FirstOnTop;
			Transitions.Enter.Offset = FVector2D(60.0, 0.0);
			Transitions.Enter.Scale = 0.4f;
			Transitions.Enter.Duration = 0.4f;
			Transitions.Exit.Offset = FVector2D(-40.0, 0.0);
			Transitions.Exit.Scale = 1.2f;
			Transitions.Exit.Duration = 0.3f;
			Transitions.ReorderSpring = MakeSpring(0.45f, 0.15f);
			break;

		case EFlowListStylePreset::Ring:
			Layout.Type = EFlowListPathType::Ring;
			Layout.Orientation = Orient_Horizontal;
			Layout.FocusAnchor = FVector2D(0.5, 0.65);
			Layout.Spacing = 40.f;
			Layout.Perspective = 0.0016f;
			Effects.ZOrder = EFlowListZOrder::Depth;
			Scrolling.bLoop = true;
			Transitions.Enter.Offset = FVector2D(0.0, -40.0);
			break;

		case EFlowListStylePreset::Slanted:
			Layout.Orientation = Orient_Vertical;
			Layout.Angle = 12.f;
			Layout.FocusAnchor = FVector2D(0.45, 0.4);
			Layout.Spacing = 2.f;
			Effects.Rotation = Falloff(-4.f, -7.f, 2.f);
			Effects.CrossOffset = Falloff(40.f, 0.f, 1.f);
			Effects.Scale = Falloff(1.12f, 1.f, 1.f);
			Effects.Tint = Dim(FLinearColor(0.55f, 0.55f, 0.6f, 1.f), 1.f);
			Scrolling.ScrollSpring = MakeSpring(0.22f, 0.2f);
			Transitions.Enter.Offset = FVector2D(0.0, -80.0);
			Transitions.Enter.Scale = 1.f;
			break;

		case EFlowListStylePreset::Stack:
			Layout.Orientation = Orient_Vertical;
			// Pivoted on their bottom edge, so each card behind shrinks toward the same baseline and peeks out a Pitch lower.
			Layout.FocusAnchor = FVector2D(0.5, 0.7);
			Layout.ItemPivot = FVector2D(0.5, 1.0);
			Layout.Pitch = 16.f;
			Effects.bSpacingFollowsScale = false;
			Effects.Scale = FalloffSplit(1.f, 0.7f, 3.f, 1.1f, 1.f, EWidgetAnimEasing::Linear);
			Effects.Opacity = FalloffSplit(1.f, 0.f, 4.f, 0.f, 0.6f, EWidgetAnimEasing::EaseIn);
			Effects.Spread = FalloffSplit(0.f, 0.f, 1.f, -120.f, 1.f, EWidgetAnimEasing::EaseIn);
			Effects.Tint = Dim(FLinearColor(0.6f, 0.6f, 0.65f, 1.f), 3.f);
			Effects.ZOrder = EFlowListZOrder::FocusOnTop;
			Scrolling.MaxItemsPerFling = 1;
			Transitions.Enter.Offset = FVector2D(30.0, 0.0);
			Transitions.Exit.Offset = FVector2D(-120.0, 0.0);
			Transitions.Exit.Scale = 1.1f;
			break;

		default:
			break;
		}
	}
}
