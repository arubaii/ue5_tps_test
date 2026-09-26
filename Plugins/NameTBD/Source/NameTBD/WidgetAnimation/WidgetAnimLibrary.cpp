// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "NameTBD/Widgets/ShapeBox.h"
#include "WidgetAnimTransform.h"
#include "WidgetAnimSpring.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

// The easing maths moved to WidgetAnimEasing.h so the Transform family - and the spring and sequencing
// families after it - share one implementation instead of each growing a copy. These two stay as thin
// forwarders because plenty of call sites in this file use them, and both are declared in the header.

float UWidgetAnimLibrary::EaseOutBack(float Alpha, float Overshoot)
{
	return WidgetAnimEasing::EaseOutBack(Alpha, Overshoot);
}

float UWidgetAnimLibrary::EvaluateEasedAlpha(const FWidgetAnimSettings& Settings, float LinearAlpha)
{
	if (Settings.IsSpringTimed())
	{
		return UWidgetAnimSpringLibrary::EvaluateDirectedProgress(Settings.Spring, LinearAlpha, /*bTowardStart=*/false);
	}

	return WidgetAnimEasing::Evaluate(Settings.Easing, Settings.CustomCurve, LinearAlpha);
}

float UWidgetAnimLibrary::EvaluateLayerAlpha(const FWidgetAnimSettings& Settings, float DirectedAlpha, bool bTowardStart)
{
	// Only a spring cares which way it is heading. A tween's curve is the same curve rewound, which is
	// exactly what running a tween backwards means; a spring rewound would not look like a spring at all.
	if (Settings.IsSpringTimed())
	{
		return UWidgetAnimSpringLibrary::EvaluateDirectedProgress(Settings.Spring, DirectedAlpha, bTowardStart);
	}

	return WidgetAnimEasing::Evaluate(Settings.Easing, Settings.CustomCurve, DirectedAlpha);
}

FVector2D UWidgetAnimLibrary::GetSlideDirectionOffset(EWidgetSlideDirection Direction)
{
	switch (Direction)
	{
	case EWidgetSlideDirection::Up:    return FVector2D(0.f, -1.f);
	case EWidgetSlideDirection::Down:  return FVector2D(0.f, 1.f);
	case EWidgetSlideDirection::Left:  return FVector2D(-1.f, 0.f);
	case EWidgetSlideDirection::Right: return FVector2D(1.f, 0.f);
	}

	return FVector2D::ZeroVector;
}

FWidgetAnimTargetedTransform& UWidgetAnimLibrary::FindOrAddTargetedTransform(FWidgetAnimResult& Result, UWidget* TargetWidget)
{
	for (FWidgetAnimTargetedTransform& Existing : Result.TargetedTransforms)
	{
		if (Existing.TargetWidget == TargetWidget)
		{
			return Existing;
		}
	}

	FWidgetAnimTargetedTransform& NewEntry = Result.TargetedTransforms.AddDefaulted_GetRef();
	NewEntry.TargetWidget = TargetWidget;
	return NewEntry;
}

namespace
{
	/**
	 * The slot holding TargetBox's outline for this frame, created if this is the first layer to write it.
	 *
	 * Unlike the transform slot, whatever the caller assigns here REPLACES what was there: shapes do not
	 * add or multiply, so the last layer to write a box wins.
	 */
	FWidgetAnimShapeTarget& FindOrAddShapeTarget(FWidgetAnimResult& Result, UShapeBox* TargetBox)
	{
		for (FWidgetAnimShapeTarget& Existing : Result.ShapeTargets)
		{
			if (Existing.TargetBox == TargetBox)
			{
				return Existing;
			}
		}

		FWidgetAnimShapeTarget& NewEntry = Result.ShapeTargets.AddDefaulted_GetRef();
		NewEntry.TargetBox = TargetBox;
		return NewEntry;
	}
}

void UWidgetAnimLibrary::ApplyAnimationContribution(const FWidgetAnimSettings& Settings, float DirectedAlpha, FWidgetAnimResult& OutResult, const FWidgetAnimBaselineSet* Baselines, bool bTowardStart)
{
	// Scale and Slide's Overshoot is the tween's way of getting a pop, and replaces the Easing when set. A
	// Spring layer already overshoots by its own physics, so there Overshoot is hidden and ignored - two
	// different pops stacked on one value would be neither.
	const bool bSpring = Settings.IsSpringTimed();

	switch (Settings.AnimType)
	{
	case EWidgetAnimType::Fade:
	{
		const float EasedAlpha = EvaluateLayerAlpha(Settings, DirectedAlpha, bTowardStart);
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget);

		// Held inside what opacity can mean. A spring (or a custom curve) overshooting a fade to 1 would
		// otherwise ask for 1.1, and one overshooting a fade to 0 for less than nothing; neither is a look,
		// only a glitch. The end values themselves are left alone, so an opacity authored above 1 still reaches it.
		const float Opacity = FMath::Lerp(Settings.StartOpacity, Settings.EndOpacity, EasedAlpha);
		Transform.Opacity *= FMath::Clamp(Opacity, 0.f, FMath::Max3(1.f, Settings.StartOpacity, Settings.EndOpacity));
		break;
	}

	case EWidgetAnimType::Scale:
	{
		const float ScaleAlpha = (Settings.Overshoot > 0.f && !bSpring)
			? EaseOutBack(DirectedAlpha, Settings.Overshoot)
			: EvaluateLayerAlpha(Settings, DirectedAlpha, bTowardStart);
		const float ScaleValue = FMath::Lerp(Settings.StartScale, Settings.EndScale, ScaleAlpha);
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget);
		Transform.Scale *= FVector2D(ScaleValue, ScaleValue);
		break;
	}

	case EWidgetAnimType::Slide:
	{
		const float SlideAlpha = (Settings.SlideOvershoot > 0.f && !bSpring)
			? EaseOutBack(DirectedAlpha, Settings.SlideOvershoot)
			: EvaluateLayerAlpha(Settings, DirectedAlpha, bTowardStart);
		const FVector2D StartOffset = GetSlideDirectionOffset(Settings.SlideFromDirection) * Settings.SlideDistance;
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget);
		Transform.Translation += FMath::Lerp(StartOffset, FVector2D::ZeroVector, SlideAlpha);
		break;
	}

	case EWidgetAnimType::Rotate:
	{
		const float EasedAlpha = EvaluateLayerAlpha(Settings, DirectedAlpha, bTowardStart);
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget);
		Transform.Angle += FMath::Lerp(Settings.StartAngle, Settings.EndAngle, EasedAlpha);
		break;
	}

	case EWidgetAnimType::Pulse:
	{
		const float Phase = DirectedAlpha * 2.f * PI;
		const float ScaleMultiplier = 1.f + Settings.PulseScaleAmount * FMath::Sin(Phase);
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget);
		Transform.Scale *= FVector2D(ScaleMultiplier, ScaleMultiplier);
		break;
	}

	case EWidgetAnimType::MaterialScalar:
	{
		if (!Settings.ScalarParameterName.IsNone())
		{
			const float EasedAlpha = EvaluateLayerAlpha(Settings, DirectedAlpha, bTowardStart);
			const float Value = FMath::Lerp(Settings.StartScalarValue, Settings.EndScalarValue, EasedAlpha);
			FWidgetAnimScalarTarget& Target = OutResult.ScalarParameters.FindOrAdd(Settings.ScalarParameterName);
			Target.TargetImage = Settings.MaterialTargetImage;
			Target.Value = Value;
		}
		break;
	}

	case EWidgetAnimType::MaterialVector:
	{
		if (!Settings.VectorParameterName.IsNone())
		{
			const float EasedAlpha = EvaluateLayerAlpha(Settings, DirectedAlpha, bTowardStart);
			const FLinearColor Value = FMath::Lerp(Settings.StartVectorValue, Settings.EndVectorValue, EasedAlpha);
			FWidgetAnimVectorTarget& Target = OutResult.VectorParameters.FindOrAdd(Settings.VectorParameterName);
			Target.TargetImage = Settings.MaterialTargetImage;
			Target.Value = Value;
		}
		break;
	}

	case EWidgetAnimType::Transform:
	{
		// Eased like every other layer: the layer's Easing bends time across the whole run of keys, and the
		// keys are met linearly in that eased time. The last key is the layer's length. The layer's Delay was
		// already consumed by the clock before DirectedAlpha reached us, and inversion is folded into
		// DirectedAlpha, so a reversed layer just walks this time backwards.
		const float EasedAlpha = EvaluateEasedAlpha(Settings, FMath::Clamp(DirectedAlpha, 0.f, 1.f));
		const float LocalTime = EasedAlpha * Settings.Transform.GetDuration();

		// Null unless a baseline captured this Shape Box's authored outline, which is exactly when a key's
		// Use Authored Shape has something to mean.
		const FShapeBoxShape* AuthoredShape = Baselines ? Baselines->FindShapeBaseline(Settings.ShapeTargetBox) : nullptr;

		const FWidgetTransformAnimResult Contribution =
			UWidgetAnimTransformLibrary::Evaluate(Settings.Transform, LocalTime, AuthoredShape);

		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget);
		Transform.Translation += Contribution.Translation;
		Transform.Scale       *= Contribution.Scale;
		Transform.Shear       += Contribution.Shear;
		Transform.Angle       += Contribution.Angle;

		// Assigned, not accumulated - see FindOrAddShapeTarget. A null box is kept rather than dropped: it
		// means "no target named", and is resolved against the fallback widget when the result is applied,
		// exactly as a transform with no target is. Dropping it here is what made Animate Shape do nothing on
		// every entry left pointing at its own widget.
		if (Contribution.bHasShape)
		{
			FindOrAddShapeTarget(OutResult, Settings.ShapeTargetBox).Shape = Contribution.Shape;
		}
		break;
	}
	}
}

bool UWidgetAnimLibrary::GetEffectiveInvert(const FWidgetAnimSettings& Settings, bool bIsOutAnimation)
{
	return Settings.bInvertAnimation != bIsOutAnimation;
}

namespace WidgetAnimLoop
{
	/** Where one layer sits on its animation's timeline, in seconds of track time. Speed already applied. */
	struct FLayerSpan
	{
		float Start = 0.f;
		float Length = KINDA_SMALL_NUMBER;
	};

	/**
	 * How long a layer runs. A Transform layer's is its curve's last key, a Spring layer's is its spring's
	 * settle time; for both, the Duration field is hidden and ignored.
	 *
	 * A spring's settle time is worked out from its parameters on every call rather than cached - a handful
	 * of logs and square roots, and only a critically damped spring searches - so there is no stored copy to
	 * go stale when a spring is edited mid-preview.
	 */
	static float GetEffectiveLayerDuration(const FWidgetAnimSettings& Settings)
	{
		if (Settings.AnimType == EWidgetAnimType::Transform)
		{
			return Settings.Transform.GetDuration();
		}

		if (Settings.IsSpringTimed())
		{
			return UWidgetAnimSpringLibrary::GetSettleTime(Settings.Spring);
		}

		return Settings.Duration;
	}

	static FLayerSpan GetLayerSpan(const FWidgetAnimSettings& Settings)
	{
		// Speed scales the layer's whole placement, delay included, so a stagger keeps its proportions.
		const float Speed = FMath::Max(Settings.PlaybackSpeed, 0.01f);

		FLayerSpan Span;
		Span.Start = FMath::Max(Settings.Delay, 0.f) / Speed;
		// A Transform layer's length comes from its curve, a Spring layer's from its spring, rather than Duration.
		Span.Length = FMath::Max(GetEffectiveLayerDuration(Settings), KINDA_SMALL_NUMBER) / Speed;
		return Span;
	}

	/**
	 * The same leader/group assignment TickAnimationsInto computes inline, factored out for the duration
	 * helpers below so the two can't drift apart. TickAnimationsInto keeps its own inline copy rather than
	 * calling this: it runs every frame, and its TInlineAllocator arrays are worth leaving untouched.
	 */
	static void ComputeGroupSpans(
		const TArray<FWidgetAnimSettings>& Animations,
		TArray<int32, TInlineAllocator<16>>& OutLeaderOf,
		TArray<FLayerSpan, TInlineAllocator<16>>& OutSpans,
		TArray<float, TInlineAllocator<16>>& OutGroupLead,
		TArray<float, TInlineAllocator<16>>& OutGroupEnd)
	{
		const int32 NumLayers = Animations.Num();
		OutLeaderOf.SetNum(NumLayers);
		OutSpans.SetNum(NumLayers);
		OutGroupLead.Init(TNumericLimits<float>::Max(), NumLayers);
		OutGroupEnd.Init(0.f, NumLayers);

		for (int32 Index = 0; Index < NumLayers; ++Index)
		{
			OutLeaderOf[Index] = Index;

			const int32 Group = Animations[Index].LoopGroup;
			if (Group != INDEX_NONE)
			{
				for (int32 Earlier = 0; Earlier < Index; ++Earlier)
				{
					if (Animations[Earlier].LoopGroup == Group)
					{
						OutLeaderOf[Index] = OutLeaderOf[Earlier];
						break;
					}
				}
			}

			OutSpans[Index] = GetLayerSpan(Animations[Index]);

			const int32 Leader = OutLeaderOf[Index];
			OutGroupLead[Leader] = FMath::Min(OutGroupLead[Leader], OutSpans[Index].Start);
			OutGroupEnd[Leader] = FMath::Max(OutGroupEnd[Leader], OutSpans[Index].Start + OutSpans[Index].Length);
		}
	}

	/**
	 * Analytic version of what AdvanceTimeline walks toward frame by frame: how long a group's clock takes
	 * to come to rest, in the same track-timeline seconds AdvanceTimeline measures CycleTime in. -1 if it
	 * never rests on its own (IsEndlessLoop).
	 *
	 * The first leg always runs the group's whole 0..End span, lead-in included; every following leg is a
	 * shorter Lead..End trip, and with Ping Pong a full extra "play" after the first is a forward-back
	 * pair of those trips - mirrors ShouldStartAnotherLeg exactly.
	 */
	static float GetGroupDuration(const FWidgetAnimSettings& Settings, float Lead, float End)
	{
		if (!Settings.bLoop)
		{
			return End;
		}

		if (UWidgetAnimLibrary::IsEndlessLoop(Settings))
		{
			return -1.f;
		}

		const int32 ExtraPlays = FMath::Max(Settings.LoopCount - 1, 0);
		const float ReturnLegLength = End - Lead;

		return Settings.bPingPong
			? End + ExtraPlays * 2.f * ReturnLegLength
			: End + ExtraPlays * ReturnLegLength;
	}

	/**
	 * Cumulative seconds a leader's clock has actually played, across every completed leg and the one in
	 * progress - unlike Clock.CycleTime itself, which AdvanceTimeline resets to Lead (or bounces between
	 * Lead and End with Ping Pong) at the start of every leg after the first, so read alone it never grows
	 * past one leg's length no matter how many legs have played.
	 */
	static float GetGroupElapsed(const FWidgetAnimSettings& Settings, const FWidgetAnimState& Clock, float Lead, float End)
	{
		if (!Settings.bLoop)
		{
			return Clock.CycleTime;
		}

		if (Clock.bFinished)
		{
			return GetGroupDuration(Settings, Lead, End);
		}

		if (Clock.LoopsCompleted <= 0)
		{
			// Still in the first leg, which runs the whole 0..End span - CycleTime already is cumulative.
			return Clock.CycleTime;
		}

		// Every leg after the first is a Lead..End trip, whichever direction it runs, so each one adds
		// exactly (End - Lead) seconds once it completes.
		const float ReturnLegLength = End - Lead;
		const float PriorCumulative = End + (Clock.LoopsCompleted - 1) * ReturnLegLength;
		const float CurrentLegProgress = Clock.bPlayingForward ? (Clock.CycleTime - Lead) : (End - Clock.CycleTime);

		return PriorCumulative + FMath::Max(CurrentLegProgress, 0.f);
	}

	/**
	 * The leader whose GetGroupDuration is the largest - or, if any group is endless, the first such group
	 * found - matching how GetStackDuration reduces a whole stack down to one number. OutDuration carries
	 * that leader's own duration back out (-1 for endless). INDEX_NONE, with OutDuration left at 0, for an
	 * empty stack.
	 */
	static int32 FindDrivingLeader(
		const TArray<FWidgetAnimSettings>& Animations,
		const TArray<int32, TInlineAllocator<16>>& LeaderOf,
		const TArray<float, TInlineAllocator<16>>& GroupLead,
		const TArray<float, TInlineAllocator<16>>& GroupEnd,
		float& OutDuration)
	{
		OutDuration = 0.f;
		int32 Driving = INDEX_NONE;

		for (int32 Index = 0; Index < Animations.Num(); ++Index)
		{
			if (LeaderOf[Index] != Index)
			{
				continue;
			}

			const float GroupDuration = GetGroupDuration(Animations[Index], GroupLead[Index], GroupEnd[Index]);
			if (GroupDuration < 0.f)
			{
				OutDuration = -1.f;
				return Index;
			}

			if (Driving == INDEX_NONE || GroupDuration > OutDuration)
			{
				OutDuration = GroupDuration;
				Driving = Index;
			}
		}

		return Driving;
	}

	/**
	 * Whether the leg that just ended is followed by another.
	 *
	 * Loop Count is how many times the animation plays through. With Ping Pong it plays back between
	 * those plays, so N plays is 2N-1 legs - forward, back, forward, ... - and it always ends on a
	 * forward leg, on its end pose. Counting legs instead is what made an even count settle on the
	 * start pose.
	 */
	static bool ShouldStartAnotherLeg(const FWidgetAnimSettings& Settings, const FWidgetAnimState& Clock, bool bEndedForward)
	{
		if (!Settings.bLoop)
		{
			return false;
		}

		// A return leg is never the last one: whatever ends the loop, it ends on the end pose.
		if (!bEndedForward)
		{
			return true;
		}

		if (Clock.bSettleRequested)
		{
			return false;
		}

		if (Settings.LoopCount <= 0)
		{
			return true;
		}

		// LoopsCompleted counts legs, including the one that just ended.
		const int32 PlaysCompleted = Settings.bPingPong ? (Clock.LoopsCompleted + 1) / 2 : Clock.LoopsCompleted;
		return PlaysCompleted < Settings.LoopCount;
	}

	/**
	 * Advances one animation's shared clock.
	 *
	 * The clock is a position on the animation's timeline, 0 .. End, where End is the latest point any
	 * of its layers finishes. The first play runs the whole timeline. Every later leg runs between Lead
	 * (the earliest any layer starts) and End: the stagger between layers repeats every loop, but a
	 * lead-in shared by all of them is only waited out once, as a single delayed layer always did.
	 *
	 * A ping-pong return leg runs the same span backwards, so the layers unwind in reverse order.
	 */
	static void AdvanceTimeline(const FWidgetAnimSettings& Settings, FWidgetAnimState& Clock, float Lead, float End, float DeltaTime)
	{
		if (Clock.bFinished)
		{
			return;
		}

		Lead = FMath::Clamp(Lead, 0.f, End);
		float Remaining = FMath::Max(DeltaTime, 0.f);

		// Bounded so a long hitch on a very short loop can't spin here. Hitting the bound only drops time.
		for (int32 Guard = 0; Guard < 1024; ++Guard)
		{
			if (Clock.bPlayingForward)
			{
				Clock.CycleTime += Remaining;
				if (Clock.CycleTime < End)
				{
					return;
				}
				Remaining = Clock.CycleTime - End;
				Clock.CycleTime = End;
			}
			else
			{
				Clock.CycleTime -= Remaining;
				if (Clock.CycleTime > Lead)
				{
					return;
				}
				Remaining = Lead - Clock.CycleTime;
				Clock.CycleTime = Lead;
			}

			const bool bEndedForward = Clock.bPlayingForward;
			++Clock.LoopsCompleted;

			if (!ShouldStartAnotherLeg(Settings, Clock, bEndedForward))
			{
				// Only ever reached at the end of a forward leg, so this is the end pose.
				Clock.CycleTime = End;
				Clock.bPlayingForward = true;
				Clock.bFinished = true;
				Clock.bSettleRequested = false;
				return;
			}

			if (Settings.bPingPong)
			{
				Clock.bPlayingForward = !bEndedForward;
			}
			else
			{
				Clock.CycleTime = Lead;
			}

			if (End - Lead <= KINDA_SMALL_NUMBER)
			{
				return;
			}
		}
	}
}

bool UWidgetAnimLibrary::IsEndlessLoop(const FWidgetAnimSettings& Settings)
{
	return Settings.bLoop && Settings.LoopCount <= 0;
}

bool UWidgetAnimLibrary::HasRunningLayers(const TArray<FWidgetAnimSettings>& Animations, const TArray<FWidgetAnimState>& States)
{
	if (States.Num() != Animations.Num())
	{
		return false;
	}

	for (const FWidgetAnimState& State : States)
	{
		if (!State.bFinished)
		{
			return true;
		}
	}

	return false;
}

bool UWidgetAnimLibrary::BlocksSettle(const FWidgetAnimSettings& Settings, const FWidgetAnimState& State)
{
	if (State.bFinished)
	{
		return false;
	}

	if (IsEndlessLoop(Settings) && State.LoopsCompleted >= 1)
	{
		// It has played through its span at least once, so what's left is ongoing flourish, not arrival.
		return false;
	}

	return true;
}

bool UWidgetAnimLibrary::TickAnimationsInto(const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation, FWidgetAnimResult& OutResult, bool* bOutAllComplete, const FWidgetAnimBaselineSet* Baselines)
{
	const int32 NumLayers = Animations.Num();

	if (States.Num() != NumLayers)
	{
		const int32 OldNum = States.Num();
		States.SetNum(NumLayers);
		for (int32 Index = OldNum; Index < NumLayers; ++Index)
		{
			States[Index].DelayRemaining = FMath::Max(Animations[Index].Delay, 0.f);
		}
	}

	// Layers loop as the animation they belong to, not one by one. Looped separately, layers of different
	// lengths drift apart after the first cycle and every delay is lost after the first play, so a
	// layered animation never looked like itself again. Each group's clock lives on its first layer.
	TArray<int32, TInlineAllocator<16>> LeaderOf;
	TArray<WidgetAnimLoop::FLayerSpan, TInlineAllocator<16>> Spans;
	TArray<float, TInlineAllocator<16>> GroupLead;
	TArray<float, TInlineAllocator<16>> GroupEnd;
	LeaderOf.SetNum(NumLayers);
	Spans.SetNum(NumLayers);
	GroupLead.Init(TNumericLimits<float>::Max(), NumLayers);
	GroupEnd.Init(0.f, NumLayers);

	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		LeaderOf[Index] = Index;

		const int32 Group = Animations[Index].LoopGroup;
		if (Group != INDEX_NONE)
		{
			for (int32 Earlier = 0; Earlier < Index; ++Earlier)
			{
				if (Animations[Earlier].LoopGroup == Group)
				{
					LeaderOf[Index] = LeaderOf[Earlier];
					break;
				}
			}
		}

		Spans[Index] = WidgetAnimLoop::GetLayerSpan(Animations[Index]);

		const int32 Leader = LeaderOf[Index];
		GroupLead[Leader] = FMath::Min(GroupLead[Leader], Spans[Index].Start);
		GroupEnd[Leader] = FMath::Max(GroupEnd[Leader], Spans[Index].Start + Spans[Index].Length);
	}

	// One advance per group, on its leader.
	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		if (LeaderOf[Index] == Index)
		{
			WidgetAnimLoop::AdvanceTimeline(Animations[Index], States[Index], GroupLead[Index], GroupEnd[Index], DeltaTime);
		}
	}

	bool bAllSettled = true;
	bool bAllComplete = true;

	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		const FWidgetAnimSettings& Settings = Animations[Index];
		const int32 Leader = LeaderOf[Index];
		const FWidgetAnimState& Clock = States[Leader];
		const WidgetAnimLoop::FLayerSpan& Span = Spans[Index];

		const float TimelineTime = Clock.bFinished ? GroupEnd[Leader] : Clock.CycleTime;
		const float LocalTime = TimelineTime - Span.Start;

		// After its end the layer holds its end pose. Before its start it does nothing at all - the widget is
		// left exactly as it is until the layer actually begins - unless it plays inverted, where what comes
		// before its start is its end pose: a Play Out has to hold the pose the entrance left the widget in
		// until each layer begins undoing it. Direction needs no special case: a return leg is the timeline
		// running backwards, so the alpha falls on its own.
		const bool bInvert = GetEffectiveInvert(Settings, bIsOutAnimation);
		const bool bBeforeStart = (LocalTime < 0.f && !bInvert) || Settings.bMuted;
		const float LinearAlpha = FMath::Clamp(LocalTime / Span.Length, 0.f, 1.f);

		if (Index != Leader)
		{
			// Members mirror the group's progress so anything reading a single layer's state sees the truth.
			FWidgetAnimState& State = States[Index];
			State.bFinished = Clock.bFinished;
			State.bPlayingForward = Clock.bPlayingForward;
			State.LoopsCompleted = Clock.LoopsCompleted;
			State.bSettleRequested = Clock.bSettleRequested;
			State.DelayRemaining = FMath::Max(-LocalTime, 0.f);
			State.CycleTime = FMath::Clamp(LocalTime, 0.f, Span.Length);
		}

		if (!bBeforeStart)
		{
			// Heading back toward the start values: inverted on a forward leg, or not inverted on a ping-pong's
			// return leg. A finished clock always rests at the end of a forward leg. Only springs read it - see
			// ApplyAnimationContribution.
			const bool bReturnLeg = !Clock.bFinished && !Clock.bPlayingForward;
			const bool bTowardStart = bInvert != bReturnLeg;

			ApplyAnimationContribution(Settings, bInvert ? 1.f - LinearAlpha : LinearAlpha, OutResult, Baselines, bTowardStart);
		}

		if (!States[Index].bFinished)
		{
			bAllComplete = false;
		}

		if (BlocksSettle(Settings, States[Index]))
		{
			bAllSettled = false;
		}
	}

	if (bOutAllComplete)
	{
		*bOutAllComplete = bAllComplete;
	}

	return bAllSettled;
}

FWidgetAnimResult UWidgetAnimLibrary::TickWidgetAnimations(const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation)
{
	FWidgetAnimResult Result;
	Result.bAllFinished = TickAnimationsInto(Animations, States, DeltaTime, bIsOutAnimation, Result);
	return Result;
}

void UWidgetAnimLibrary::ApplyResultInternal(UWidget* FallbackWidget, const FWidgetAnimResult& Result, const FWidgetAnimBaselineSet* Baselines)
{
	for (const FWidgetAnimTargetedTransform& Transform : Result.TargetedTransforms)
	{
		UWidget* Target = Transform.TargetWidget ? Transform.TargetWidget : FallbackWidget;
		if (!Target)
		{
			continue;
		}

		if (Baselines)
		{
			// Composed: the animation says "half as bright, eight pixels up", not "opacity 0.5, y = -8".
			const FWidgetAnimBaseline Baseline = FindBaselineFor(*Baselines, Target);
			Target->SetRenderOpacity(Baseline.Opacity * Transform.Opacity);
			Target->SetRenderScale(Baseline.Scale * Transform.Scale);
			Target->SetRenderTranslation(Baseline.Translation + Transform.Translation);
			Target->SetRenderShear(Baseline.Shear + Transform.Shear);
			Target->SetRenderTransformAngle(Baseline.Angle + Transform.Angle);
		}
		else
		{
			Target->SetRenderOpacity(Transform.Opacity);
			Target->SetRenderScale(Transform.Scale);
			Target->SetRenderTranslation(Transform.Translation);
			Target->SetRenderShear(Transform.Shear);
			Target->SetRenderTransformAngle(Transform.Angle);
		}
	}

	// Shapes are not part of the render transform and are never composed over a baseline - the animation
	// says what the outline IS, and the baseline's job is only to put it back afterwards.
	ApplyShapeAnimResult(Result, FallbackWidget);
}

void UWidgetAnimLibrary::ApplyShapeAnimResult(const FWidgetAnimResult& Result, UWidget* FallbackWidget)
{
	for (const FWidgetAnimShapeTarget& Target : Result.ShapeTargets)
	{
		// No box named: the layer animates the widget it was played on, so the outline goes to that widget's
		// Shape Box - the widget itself, or the Shape Box it sits in.
		UShapeBox* Box = Target.TargetBox ? Target.TargetBox.Get() : ResolveShapeBox(FallbackWidget);
		if (Box)
		{
			ApplyShapeToBox(Box, Target.Shape);
		}
	}
}

void UWidgetAnimLibrary::ApplyShapeToBox(UShapeBox* Box, const FShapeBoxShape& Shape)
{
	if (!Box || Box->GetShape().Equals(Shape))
	{
		return;
	}

	Box->SetShape(Shape);

	// SetShape writes the UObject's copy. The Slate widget that actually draws the outline keeps its own,
	// handed to it in SynchronizeProperties - which is why the Shape Box reacts to a Details panel edit (that
	// path always synchronizes) but an animation writing the same field changed nothing on screen, and why
	// only invalidating the Slate widget was not enough: it repainted the old outline. Synchronizing pushes
	// the new shape across the same way the Details panel does, whatever UShapeBox::SetShape does or doesn't
	// forward on its own.
	//
	// Layout as well as paint, because the shape decides where the box's content is arranged, not just how
	// it is drawn. Only when the shape actually changed, so a held pose costs nothing, and only once the
	// Slate widget exists - before that, building it synchronizes the UObject's shape by itself.
	if (const TSharedPtr<SWidget> SlateBox = Box->GetCachedWidget())
	{
		// Through UWidget, where SynchronizeProperties is public, in case UShapeBox re-declares it protected.
		static_cast<UWidget*>(Box)->SynchronizeProperties();
		SlateBox->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

UShapeBox* UWidgetAnimLibrary::ResolveShapeBox(UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}

	if (UShapeBox* Direct = Cast<UShapeBox>(Widget))
	{
		return Direct;
	}

	// A Shape Box holds exactly one child, so a Shape Box parent always means "this widget alone inside it".
	return Cast<UShapeBox>(Widget->GetParent());
}

void UWidgetAnimLibrary::ApplyWidgetAnimResult(UWidget* FallbackWidget, const FWidgetAnimResult& Result)
{
	ApplyResultInternal(FallbackWidget, Result, nullptr);
}

void UWidgetAnimLibrary::ApplyWidgetAnimResultOver(UWidget* FallbackWidget, const FWidgetAnimResult& Result, const FWidgetAnimBaselineSet& Baselines)
{
	ApplyResultInternal(FallbackWidget, Result, &Baselines);
}

void UWidgetAnimLibrary::ApplyMaterialAnimResult(const FWidgetAnimResult& Result)
{
	for (const TPair<FName, FWidgetAnimScalarTarget>& Pair : Result.ScalarParameters)
	{
		UImage* TargetImage = Pair.Value.TargetImage;
		if (!TargetImage)
		{
			continue;
		}

		if (UMaterialInstanceDynamic* MID = TargetImage->GetDynamicMaterial())
		{
			MID->SetScalarParameterValue(Pair.Key, Pair.Value.Value);
		}
	}

	for (const TPair<FName, FWidgetAnimVectorTarget>& Pair : Result.VectorParameters)
	{
		UImage* TargetImage = Pair.Value.TargetImage;
		if (!TargetImage)
		{
			continue;
		}

		if (UMaterialInstanceDynamic* MID = TargetImage->GetDynamicMaterial())
		{
			MID->SetVectorParameterValue(Pair.Key, Pair.Value.Value);
		}
	}
}

void UWidgetAnimLibrary::PlayInternal(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation, const FWidgetAnimBaselineSet* Baselines)
{
	// Ticked through the internal entry point rather than TickWidgetAnimations, so a Transform layer's
	// keys can resolve Use Authored Shape against the captured baseline.
	FWidgetAnimResult Result;
	Result.bAllFinished = TickAnimationsInto(Animations, States, DeltaTime, bIsOutAnimation, Result, nullptr, Baselines);

	ApplyResultInternal(FallbackWidget, Result, Baselines);
	ApplyMaterialAnimResult(Result);
}

void UWidgetAnimLibrary::PlayWidgetAnimations(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation)
{
	PlayInternal(FallbackWidget, Animations, States, DeltaTime, bIsOutAnimation, nullptr);
}

void UWidgetAnimLibrary::PlayWidgetAnimationsOver(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation, const FWidgetAnimBaselineSet& Baselines)
{
	PlayInternal(FallbackWidget, Animations, States, DeltaTime, bIsOutAnimation, &Baselines);
}

void UWidgetAnimLibrary::InitializeInternal(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, const FWidgetAnimBaselineSet* Baselines)
{
	ResetWidgetAnimations(Animations, States);

	// The pose at time 0: every layer that starts right away, at its start. A layer with a delay does nothing
	// until it starts - see TickAnimationsInto - so it leaves the widget alone here too.
	FWidgetAnimResult StartResult;
	for (const FWidgetAnimSettings& Settings : Animations)
	{
		const bool bInvert = GetEffectiveInvert(Settings, /*bIsOutAnimation=*/false);
		if (Settings.bMuted || (!bInvert && WidgetAnimLoop::GetLayerSpan(Settings).Start > 0.f))
		{
			continue;
		}
		ApplyAnimationContribution(Settings, bInvert ? 1.f : 0.f, StartResult, Baselines, /*bTowardStart=*/bInvert);
	}

	ApplyResultInternal(FallbackWidget, StartResult, Baselines);
	ApplyMaterialAnimResult(StartResult);
}

void UWidgetAnimLibrary::InitializeWidgetAnimations(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States)
{
	InitializeInternal(FallbackWidget, Animations, States, nullptr);
}

void UWidgetAnimLibrary::InitializeWidgetAnimationsOver(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, const FWidgetAnimBaselineSet& Baselines)
{
	InitializeInternal(FallbackWidget, Animations, States, &Baselines);
}

void UWidgetAnimLibrary::ResetWidgetAnimations(const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States)
{
	States.SetNum(Animations.Num());
	for (int32 Index = 0; Index < Animations.Num(); ++Index)
	{
		States[Index] = FWidgetAnimState();
		States[Index].DelayRemaining = FMath::Max(Animations[Index].Delay, 0.f);
	}
}

bool UWidgetAnimLibrary::AreWidgetAnimationsFinished(const TArray<FWidgetAnimSettings>& Animations, const TArray<FWidgetAnimState>& States)
{
	for (int32 Index = 0; Index < Animations.Num() && Index < States.Num(); ++Index)
	{
		if (BlocksSettle(Animations[Index], States[Index]))
		{
			return false;
		}
	}

	return true;
}


void UWidgetAnimLibrary::PlayWidgetIn(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, FWidgetAnimPlaybackState& PlaybackState)
{
	PlaybackState.OutStates.Reset();

	InitializeWidgetAnimations(FallbackWidget, InAnimations, PlaybackState.InStates);
	PlaybackState.Phase = EWidgetAnimPhase::PlayingIn;
}

void UWidgetAnimLibrary::PlayWidgetOut(FWidgetAnimPlaybackState& PlaybackState, const TArray<FWidgetAnimSettings>& OutAnimations)
{
	ResetWidgetAnimations(OutAnimations, PlaybackState.OutStates);
	PlaybackState.Phase = EWidgetAnimPhase::PlayingOut;
}

bool UWidgetAnimLibrary::TickPlaybackInternal(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, FWidgetAnimPlaybackState& PlaybackState, float DeltaTime, const FWidgetAnimBaselineSet* Baselines)
{
	switch (PlaybackState.Phase)
	{
	case EWidgetAnimPhase::PlayingIn:
	{
		PlayInternal(FallbackWidget, InAnimations, PlaybackState.InStates, DeltaTime, /*bIsOutAnimation=*/false, Baselines);
		if (AreWidgetAnimationsFinished(InAnimations, PlaybackState.InStates))
		{
			PlaybackState.Phase = EWidgetAnimPhase::Shown;
		}
		return false;
	}

	case EWidgetAnimPhase::PlayingOut:
	{
		PlayInternal(FallbackWidget, OutAnimations, PlaybackState.OutStates, DeltaTime, /*bIsOutAnimation=*/true, Baselines);
		if (AreWidgetAnimationsFinished(OutAnimations, PlaybackState.OutStates))
		{
			PlaybackState.Phase = EWidgetAnimPhase::Hidden;
			return true; 
		}
		return false;
	}

	case EWidgetAnimPhase::Shown:
	{
		// The entrance has landed, but endless loops in it keep running. This used to return without
		// ticking, which is what froze every looping layer the moment the rest of the stack finished.
		// Skipped when nothing is moving, so a settled widget is not re-posed every frame for nothing.
		if (HasRunningLayers(InAnimations, PlaybackState.InStates))
		{
			PlayInternal(FallbackWidget, InAnimations, PlaybackState.InStates, DeltaTime, /*bIsOutAnimation=*/false, Baselines);
		}
		return false;
	}

	case EWidgetAnimPhase::Hidden:
	{
		if (HasRunningLayers(OutAnimations, PlaybackState.OutStates))
		{
			PlayInternal(FallbackWidget, OutAnimations, PlaybackState.OutStates, DeltaTime, /*bIsOutAnimation=*/true, Baselines);
		}
		return false;
	}

	case EWidgetAnimPhase::Idle:
	default:
		return false;
	}
}

bool UWidgetAnimLibrary::TickWidgetAnimPlayback(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, FWidgetAnimPlaybackState& PlaybackState, float DeltaTime)
{
	return TickPlaybackInternal(FallbackWidget, InAnimations, OutAnimations, PlaybackState, DeltaTime, nullptr);
}

bool UWidgetAnimLibrary::TickWidgetAnimPlaybackOver(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, FWidgetAnimPlaybackState& PlaybackState, float DeltaTime, const FWidgetAnimBaselineSet& Baselines)
{
	return TickPlaybackInternal(FallbackWidget, InAnimations, OutAnimations, PlaybackState, DeltaTime, &Baselines);
}

// =====================================================================================================
// Baselines
// =====================================================================================================

FWidgetAnimBaseline UWidgetAnimLibrary::FindBaselineFor(const FWidgetAnimBaselineSet& Baselines, UWidget* Widget)
{
	for (const FWidgetAnimBaseline& Baseline : Baselines.Baselines)
	{
		if (Baseline.Widget == Widget)
		{
			return Baseline;
		}
	}

	// Identity: a widget nobody captured is animated exactly the way it always was.
	return FWidgetAnimBaseline();
}

void UWidgetAnimLibrary::CaptureOne(FWidgetAnimBaselineSet& Baselines, UWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	for (const FWidgetAnimBaseline& Existing : Baselines.Baselines)
	{
		if (Existing.Widget == Widget)
		{
			return;
		}
	}

	const FWidgetTransform& Transform = Widget->GetRenderTransform();

	FWidgetAnimBaseline& Baseline = Baselines.Baselines.AddDefaulted_GetRef();
	Baseline.Widget = Widget;
	Baseline.Opacity = Widget->GetRenderOpacity();
	Baseline.Scale = Transform.Scale;
	Baseline.Translation = Transform.Translation;
	Baseline.Shear = Transform.Shear;
	Baseline.Angle = Transform.Angle;
}

void UWidgetAnimLibrary::CaptureOneShape(FWidgetAnimBaselineSet& Baselines, UShapeBox* Box, bool bIsFallback)
{
	if (!Box)
	{
		return;
	}

	for (FWidgetAnimShapeBaseline& Existing : Baselines.ShapeBaselines)
	{
		if (Existing.Box == Box)
		{
			// Already captured - under a name, or as the fallback. The same box can be both, and must not be
			// captured twice or the second read would record a pose the animation had already written.
			Existing.bIsFallback |= bIsFallback;
			return;
		}
	}

	FWidgetAnimShapeBaseline& Baseline = Baselines.ShapeBaselines.AddDefaulted_GetRef();
	Baseline.Box = Box;
	Baseline.Shape = Box->GetShape();
	Baseline.bIsFallback = bIsFallback;
}

void UWidgetAnimLibrary::CaptureWidgetAnimBaselines(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, FWidgetAnimBaselineSet& Baselines)
{
	// Capturing twice would read back a pose the animation itself wrote, and every capture after the first
	// would drift a little further from what was actually authored. Call ResetBaselines first if the set
	// really does need rebuilding - after restoring it, not before.
	if (Baselines.bCaptured)
	{
		return;
	}

	Baselines.bCaptured = true;
	Baselines.Baselines.Reset();
	Baselines.ShapeBaselines.Reset();

	bool bNeedsFallback = false;
	bool bNeedsShapeFallback = false;

	auto Gather = [&Baselines, &bNeedsFallback, &bNeedsShapeFallback](const TArray<FWidgetAnimSettings>& Animations)
	{
		for (const FWidgetAnimSettings& Settings : Animations)
		{
			// A Transform layer that animates a Shape Box needs the box's authored outline kept, both to
			// restore it afterwards and because a key's Use Authored Shape resolves against it.
			if (Settings.AnimType == EWidgetAnimType::Transform && Settings.Transform.IsAnimatingShape())
			{
				if (Settings.ShapeTargetBox)
				{
					CaptureOneShape(Baselines, Settings.ShapeTargetBox);
				}
				else
				{
					bNeedsShapeFallback = true;
				}
			}

			if (Settings.AnimType == EWidgetAnimType::MaterialScalar || Settings.AnimType == EWidgetAnimType::MaterialVector)
			{
				// Material entries write parameters, not a transform, so there is no pose to restore.
				continue;
			}

			if (Settings.TargetWidget)
			{
				CaptureOne(Baselines, Settings.TargetWidget);
			}
			else
			{
				bNeedsFallback = true;
			}
		}
	};

	Gather(InAnimations);
	Gather(OutAnimations);

	if (bNeedsFallback)
	{
		CaptureOne(Baselines, FallbackWidget);
	}

	if (bNeedsShapeFallback)
	{
		CaptureOneShape(Baselines, ResolveShapeBox(FallbackWidget), /*bIsFallback=*/true);
	}
}

void UWidgetAnimLibrary::RestoreWidgetAnimBaselines(const FWidgetAnimBaselineSet& Baselines)
{
	for (const FWidgetAnimBaseline& Baseline : Baselines.Baselines)
	{
		UWidget* Widget = Baseline.Widget;
		if (!Widget)
		{
			continue;
		}

		Widget->SetRenderOpacity(Baseline.Opacity);
		Widget->SetRenderScale(Baseline.Scale);
		Widget->SetRenderTranslation(Baseline.Translation);
		Widget->SetRenderShear(Baseline.Shear);
		Widget->SetRenderTransformAngle(Baseline.Angle);
	}

	// Outlines go back whole. Nothing partial to undo: Animate Shape replaced the shape rather than
	// composing with it, so the authored one is the only thing there is to restore.
	for (const FWidgetAnimShapeBaseline& ShapeBaseline : Baselines.ShapeBaselines)
	{
		ApplyShapeToBox(ShapeBaseline.Box, ShapeBaseline.Shape);
	}
}

void UWidgetAnimLibrary::ResetWidgetRenderState(UWidget* Widget)
{
	if (!Widget)
	{
		return;
	}

	Widget->SetRenderOpacity(1.f);
	Widget->SetRenderScale(FVector2D(1.f, 1.f));
	Widget->SetRenderTranslation(FVector2D::ZeroVector);
	Widget->SetRenderShear(FVector2D::ZeroVector);
	Widget->SetRenderTransformAngle(0.f);
}

// =====================================================================================================
// Posing and preview
// =====================================================================================================

void UWidgetAnimLibrary::ApplyWidgetAnimationsAtAlpha(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, float Alpha, bool bIsOutAnimation, const FWidgetAnimBaselineSet& Baselines)
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.f, 1.f);

	// Alpha is a point in TIME along one pass of the stack, and every layer is posed where it actually is
	// at that time - its own Delay included, the same way TickAnimationsInto places it on a playing clock.
	// Posing every layer at the same alpha instead is what made a scrub show a staggered animation as if
	// every layer started together. A layer that hasn't started yet leaves the widget alone, exactly as it
	// does on a playing clock - an inverted one excepted, which holds its end pose until it starts.
	const float StackTime = ClampedAlpha * GetStackPassDuration(Animations);

	FWidgetAnimResult Result;
	Result.bAllFinished = true;

	for (const FWidgetAnimSettings& Settings : Animations)
	{
		const WidgetAnimLoop::FLayerSpan Span = WidgetAnimLoop::GetLayerSpan(Settings);
		const float LayerAlpha = FMath::Clamp((StackTime - Span.Start) / Span.Length, 0.f, 1.f);

		const bool bInvert = GetEffectiveInvert(Settings, bIsOutAnimation);
		if (Settings.bMuted || (!bInvert && StackTime < Span.Start))
		{
			continue;
		}
		// One pass has no return legs, so the only thing that turns a layer toward its start here is inversion.
		ApplyAnimationContribution(Settings, bInvert ? 1.f - LayerAlpha : LayerAlpha, Result, Baselines.bCaptured ? &Baselines : nullptr, /*bTowardStart=*/bInvert);
	}

	ApplyResultInternal(FallbackWidget, Result, Baselines.bCaptured ? &Baselines : nullptr);
	ApplyMaterialAnimResult(Result);
}

float FWidgetAnimLayer::GetRunLength() const
{
	if (AnimType == EWidgetAnimType::Transform)
	{
		return Transform.GetDuration();
	}

	// The same number GetEffectiveLayerDuration gives the settings built from this layer, so a layer is
	// placed after the one above it by exactly the length it then plays for.
	if (IsSpringTimed())
	{
		return UWidgetAnimSpringLibrary::GetSettleTime(Spring);
	}

	return FMath::Max(Duration, KINDA_SMALL_NUMBER);
}

FString FWidgetAnimLayerGroup::GetDisplayName(int32 GroupIndex) const
{
	return GroupName.TrimStartAndEnd().IsEmpty() ? MakeDefaultName(GroupIndex) : GroupName;
}

FString FWidgetAnimLayerGroup::MakeDefaultName(int32 GroupIndex)
{
	return FString::Printf(TEXT("Layer Group %d"), GroupIndex + 1);
}

bool UWidgetAnimLibrary::NormalizeLayerGroups(TArray<FWidgetAnimLayerGroup>& Groups, TArray<FWidgetAnimLayer>& Layers)
{
	bool bChanged = false;

	// Layers held inside their groups: into the single list, group by group, each remembering its group.
	for (int32 GroupIndex = 0; GroupIndex < Groups.Num(); ++GroupIndex)
	{
		TArray<FWidgetAnimLayer>& Held = Groups[GroupIndex].Layers;
		if (Held.Num() == 0)
		{
			continue;
		}

		for (FWidgetAnimLayer& Layer : Held)
		{
			Layer.LayerGroup = GroupIndex;
			Layers.Add(MoveTemp(Layer));
		}
		Held.Reset();
		bChanged = true;
	}

	// Layers from before groups belong to the one group they were always played on.
	if (Layers.Num() > 0 && Groups.Num() == 0)
	{
		Groups.AddDefaulted();
		bChanged = true;
	}

	for (FWidgetAnimLayer& Layer : Layers)
	{
		const int32 InRange = GetLayerGroupIndex(Layer, Groups.Num());
		if (InRange != INDEX_NONE && InRange != Layer.LayerGroup)
		{
			Layer.LayerGroup = InRange;
			bChanged = true;
		}
	}

	return bChanged;
}

int32 UWidgetAnimLibrary::GetLayerGroupIndex(const FWidgetAnimLayer& Layer, int32 NumGroups)
{
	return NumGroups > 0 ? FMath::Clamp(Layer.LayerGroup, 0, NumGroups - 1) : INDEX_NONE;
}

void UWidgetAnimLibrary::BuildLayerSettings(const TArray<FWidgetAnimLayer>& Layers, TArray<FWidgetAnimSettings>& OutSettings)
{
	OutSettings.Reserve(OutSettings.Num() + Layers.Num());

	// Where the layer above started and finished, in seconds from the start of the animation. Both begin at
	// 0, so the first layer counts from the start whichever way it is set to start.
	float PreviousStart = 0.f;
	float PreviousEnd = 0.f;

	for (const FWidgetAnimLayer& Layer : Layers)
	{
		const float Anchor = (Layer.Start == EWidgetAnimLayerStart::AfterPrevious) ? PreviousEnd : PreviousStart;
		// A negative Delay overlaps the layer above - see FWidgetAnimLayer::Delay - but never reaches back before
		// the animation starts.
		const float LayerStart = FMath::Max(Anchor + Layer.Delay, 0.f);

		FWidgetAnimSettings Settings = Layer.ToSettings();

		// From here on Delay means seconds from the start of the animation, which is what the clock, the
		// scrub and every duration helper read it as. Speed is applied to it later, uniformly, so the
		// sequence keeps its proportions at any playback speed.
		Settings.Delay = LayerStart;
		OutSettings.Add(MoveTemp(Settings));

		PreviousStart = LayerStart;
		PreviousEnd = LayerStart + Layer.GetRunLength();
	}
}

#if WITH_EDITOR
void UWidgetAnimLibrary::MuteHiddenLayers(const TArray<FWidgetAnimLayer>& Layers, TArray<FWidgetAnimSettings>& Settings, int32 FirstSetting)
{
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		const int32 SettingIndex = FirstSetting + LayerIndex;
		if (Settings.IsValidIndex(SettingIndex) && Layers[LayerIndex].bHiddenInEditor)
		{
			Settings[SettingIndex].bMuted = true;
		}
	}
}
#endif

float UWidgetAnimLibrary::GetStackPassDuration(const TArray<FWidgetAnimSettings>& Settings)
{
	float End = 0.f;
	for (const FWidgetAnimSettings& Layer : Settings)
	{
		const WidgetAnimLoop::FLayerSpan Span = WidgetAnimLoop::GetLayerSpan(Layer);
		End = FMath::Max(End, Span.Start + Span.Length);
	}

	return FMath::Max(End, KINDA_SMALL_NUMBER);
}

float UWidgetAnimLibrary::GetTransformKeyStackTime(const FWidgetAnimSettings& Layer, int32 KeyIndex)
{
	if (Layer.AnimType != EWidgetAnimType::Transform)
	{
		return -1.f;
	}

	const FRichCurve* Curve = Layer.Transform.GetActiveCurve();
	if (!Curve || !Curve->GetConstRefOfKeys().IsValidIndex(KeyIndex))
	{
		return -1.f;
	}

	// Same placement GetLayerSpan gives the layer - delay and key time both scaled by speed - so the result
	// is a point on the very clock ApplyWidgetAnimationsAtAlpha and the playing modes run on.
	const float Speed = FMath::Max(Layer.PlaybackSpeed, 0.01f);
	const float KeyTime = FMath::Max(Curve->GetConstRefOfKeys()[KeyIndex].Time, 0.f);
	return (FMath::Max(Layer.Delay, 0.f) + KeyTime) / Speed;
}

void UWidgetAnimLibrary::RestartWidgetAnimPreview(FWidgetAnimPreviewState& State)
{
	State.bStarted = false;
	State.HoldRemaining = 0.f;
	State.Playback = FWidgetAnimPlaybackState();

	// Or a restart mid-scrub would leave the widget posed while the state still claimed that pose was
	// current, and the next frame at the same alpha would be skipped as "unchanged".
	State.LastScrubAlpha = -1.f;
	State.bLastScrubWasOut = false;
}

bool UWidgetAnimLibrary::TickWidgetAnimPreview(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, FWidgetAnimPreviewState& State, const FWidgetAnimPreviewSettings& Settings, const FWidgetAnimBaselineSet& Baselines, float DeltaTime)
{
	const FWidgetAnimBaselineSet* BaselinePtr = Baselines.bCaptured ? &Baselines : nullptr;

	// Changing mode starts the harness over rather than continuing from wherever the previous one stopped,
	// so flipping Loop In and Out -> Play In Once doesn't leave half an exit animation on screen.
	if (State.LastMode != Settings.Mode)
	{
		State.LastMode = Settings.Mode;
		RestartWidgetAnimPreview(State);

		if (Settings.Mode == EWidgetAnimPreviewMode::Off)
		{
			RestoreWidgetAnimBaselines(Baselines);
			return false;
		}
	}

	switch (Settings.Mode)
	{
	case EWidgetAnimPreviewMode::Off:
		return false;

	case EWidgetAnimPreviewMode::StartPose:
		ApplyWidgetAnimationsAtAlpha(FallbackWidget, InAnimations, 0.f, /*bIsOutAnimation=*/false, Baselines);
		return false;

	case EWidgetAnimPreviewMode::EndPose:
		ApplyWidgetAnimationsAtAlpha(FallbackWidget, InAnimations, 1.f, /*bIsOutAnimation=*/false, Baselines);
		return false;

	case EWidgetAnimPreviewMode::Scrub:
	{
		const float ClampedAlpha = FMath::Clamp(Settings.ScrubAlpha, 0.f, 1.f);

		if (State.bLastScrubWasOut != Settings.bScrubOutAnimation
			|| !FMath::IsNearlyEqual(State.LastScrubAlpha, ClampedAlpha, KINDA_SMALL_NUMBER))
		{
			ApplyWidgetAnimationsAtAlpha(
				FallbackWidget,
				Settings.bScrubOutAnimation ? OutAnimations : InAnimations,
				ClampedAlpha,
				Settings.bScrubOutAnimation,
				Baselines);

			State.LastScrubAlpha = ClampedAlpha;
			State.bLastScrubWasOut = Settings.bScrubOutAnimation;
		}

		// True, where the other two held poses return false. They are finished the moment they have
		// drawn; a scrub is not finished until the mode changes. Returning false here stopped the active
		// timer after a single frame, and the active timer is the thing that forces the designer viewport
		// to repaint - which is why dragging the slider only ever showed the value the mouse was released
		// on. Staying registered is what makes it continuous.
		return true;
	}

	default:
		break;
	}

	const bool bLooping = (Settings.Mode == EWidgetAnimPreviewMode::LoopIn || Settings.Mode == EWidgetAnimPreviewMode::LoopInOut);

	if (!State.bStarted)
	{
		State.bStarted = true;
		State.HoldRemaining = 0.f;

		if (Settings.Mode == EWidgetAnimPreviewMode::PlayOut)
		{
			// Sit on the settled pose first, or the exit appears to start from nowhere.
			ApplyWidgetAnimationsAtAlpha(FallbackWidget, InAnimations, 1.f, /*bIsOutAnimation=*/false, Baselines);
			ResetWidgetAnimations(OutAnimations, State.Playback.OutStates);
			State.Playback.Phase = EWidgetAnimPhase::PlayingOut;
		}
		else
		{
			InitializeInternal(FallbackWidget, InAnimations, State.Playback.InStates, BaselinePtr);
			State.Playback.OutStates.Reset();
			State.Playback.Phase = EWidgetAnimPhase::PlayingIn;
		}
	}

	const float ScaledDelta = DeltaTime * FMath::Max(Settings.PlaybackSpeed, 0.01f);

	// Holds run on real time, not scaled time: they're dead air between animations, not part of them.
	if (State.HoldRemaining > 0.f)
	{
		State.HoldRemaining -= DeltaTime;
		if (State.HoldRemaining > 0.f)
		{
			// Only Shown / Hidden are ever held, and ticking those just keeps any looping layers moving
			// through the hold instead of freezing them for its length.
			TickPlaybackInternal(FallbackWidget, InAnimations, OutAnimations, State.Playback, ScaledDelta, BaselinePtr);
			return true;
		}
		State.HoldRemaining = 0.f;
	}

	// Reached with no hold left to run, either because it just expired or because it was zero to begin
	// with - which is why the hold is armed after the tick below rather than at the transition itself.
	if (State.Playback.Phase == EWidgetAnimPhase::Shown && bLooping)
	{
		if (Settings.Mode == EWidgetAnimPreviewMode::LoopInOut)
		{
			ResetWidgetAnimations(OutAnimations, State.Playback.OutStates);
			State.Playback.Phase = EWidgetAnimPhase::PlayingOut;
		}
		else
		{
			InitializeInternal(FallbackWidget, InAnimations, State.Playback.InStates, BaselinePtr);
			State.Playback.Phase = EWidgetAnimPhase::PlayingIn;
		}
	}
	else if (State.Playback.Phase == EWidgetAnimPhase::Hidden && Settings.Mode == EWidgetAnimPreviewMode::LoopInOut)
	{
		InitializeInternal(FallbackWidget, InAnimations, State.Playback.InStates, BaselinePtr);
		State.Playback.Phase = EWidgetAnimPhase::PlayingIn;
	}

	const EWidgetAnimPhase PhaseBefore = State.Playback.Phase;
	TickPlaybackInternal(FallbackWidget, InAnimations, OutAnimations, State.Playback, ScaledDelta, BaselinePtr);

	if (State.Playback.Phase != PhaseBefore && bLooping)
	{
		if (State.Playback.Phase == EWidgetAnimPhase::Shown)
		{
			State.HoldRemaining = FMath::Max(Settings.HoldShown, 0.f);
		}
		else if (State.Playback.Phase == EWidgetAnimPhase::Hidden)
		{
			State.HoldRemaining = FMath::Max(Settings.HoldHidden, 0.f);
		}
	}

	const bool bIsPlaying = State.Playback.Phase == EWidgetAnimPhase::PlayingIn || State.Playback.Phase == EWidgetAnimPhase::PlayingOut;

	// Play In Once / Play Out Once: the stack has landed, but a looping layer in it is still moving and
	// needs frames. Returning false here stopped the active timer, so the loop never visibly ran.
	const bool bShowingOut = State.Playback.Phase == EWidgetAnimPhase::PlayingOut || State.Playback.Phase == EWidgetAnimPhase::Hidden;
	const bool bLayersStillRunning = bShowingOut
		? HasRunningLayers(OutAnimations, State.Playback.OutStates)
		: HasRunningLayers(InAnimations, State.Playback.InStates);

	return bIsPlaying || State.HoldRemaining > 0.f || bLooping || bLayersStillRunning;
}

// =====================================================================================================
// Authoring helpers
// =====================================================================================================

FWidgetAnimSettings UWidgetAnimLibrary::RetimeWidgetAnimation(const FWidgetAnimSettings& Settings, float Speed, float ExtraDelay)
{
	FWidgetAnimSettings Result = Settings;

	const float SafeSpeed = FMath::Max(Speed, 0.01f);
	Result.Duration = FMath::Max(Settings.Duration / SafeSpeed, 0.01f);
	Result.Delay = FMath::Max(Settings.Delay / SafeSpeed + ExtraDelay, 0.f);

	// A spring's length is its own, so Duration alone would retime nothing. The same spring played Speed times
	// faster is the same shape in less time: its frequency scaled by Speed with its damping ratio unchanged -
	// which is Visual Duration divided by it, or Stiffness by its square and Damping by it - and its initial
	// velocity, in travels per second, scaled with it.
	if (Settings.IsSpringTimed())
	{
		Result.Spring.VisualDuration = FMath::Max(Settings.Spring.VisualDuration / SafeSpeed, 0.02f);
		Result.Spring.Stiffness = Settings.Spring.Stiffness * SafeSpeed * SafeSpeed;
		Result.Spring.Damping = Settings.Spring.Damping * SafeSpeed;
		Result.Spring.InitialVelocity = Settings.Spring.InitialVelocity * SafeSpeed;
	}

	return Result;
}

void UWidgetAnimLibrary::AppendWithMirroring(const TArray<FWidgetAnimSettings>& Source, TArray<FWidgetAnimSettings>& Destination, TArray<FWidgetAnimSettings>& MirrorDestination)
{
	Destination.Reserve(Destination.Num() + Source.Num());

	for (const FWidgetAnimSettings& Settings : Source)
	{
		Destination.Add(Settings);

		// Direction is decided at play time by bIsOutAnimation, so the mirrored copy is a straight copy:
		// the same entry simply runs backwards when it plays as the other half of the pair.
		if (Settings.bMirrorInOut)
		{
			MirrorDestination.Add(Settings);
		}
	}
}

float UWidgetAnimLibrary::GetStackDuration(const TArray<FWidgetAnimSettings>& Settings)
{
	if (Settings.Num() == 0)
	{
		return 0.f;
	}

	TArray<int32, TInlineAllocator<16>> LeaderOf;
	TArray<WidgetAnimLoop::FLayerSpan, TInlineAllocator<16>> Spans;
	TArray<float, TInlineAllocator<16>> GroupLead;
	TArray<float, TInlineAllocator<16>> GroupEnd;
	WidgetAnimLoop::ComputeGroupSpans(Settings, LeaderOf, Spans, GroupLead, GroupEnd);

	float Duration = 0.f;
	WidgetAnimLoop::FindDrivingLeader(Settings, LeaderOf, GroupLead, GroupEnd, Duration);
	return Duration;
}

float UWidgetAnimLibrary::GetStackElapsed(const TArray<FWidgetAnimSettings>& Settings, const TArray<FWidgetAnimState>& States)
{
	const int32 NumLayers = Settings.Num();
	if (NumLayers == 0 || States.Num() != NumLayers)
	{
		// Not yet initialized for this stack - nothing has played, so nothing has elapsed.
		return 0.f;
	}

	TArray<int32, TInlineAllocator<16>> LeaderOf;
	TArray<WidgetAnimLoop::FLayerSpan, TInlineAllocator<16>> Spans;
	TArray<float, TInlineAllocator<16>> GroupLead;
	TArray<float, TInlineAllocator<16>> GroupEnd;
	WidgetAnimLoop::ComputeGroupSpans(Settings, LeaderOf, Spans, GroupLead, GroupEnd);

	// Only a leader's clock sits on the group's absolute timeline; a member's CycleTime is local to its
	// own span instead (see TickAnimationsInto), so only leaders are read here.
	float Elapsed = 0.f;
	for (int32 Index = 0; Index < NumLayers; ++Index)
	{
		if (LeaderOf[Index] != Index)
		{
			continue;
		}

		const float GroupElapsed = WidgetAnimLoop::GetGroupElapsed(Settings[Index], States[Index], GroupLead[Index], GroupEnd[Index]);
		Elapsed = FMath::Max(Elapsed, GroupElapsed);
	}

	return Elapsed;
}

bool UWidgetAnimLibrary::GetStackDrivingLoopInfo(const TArray<FWidgetAnimSettings>& Settings, bool& OutPingPong, float& OutLoopSeconds)
{
	OutPingPong = false;
	OutLoopSeconds = 0.f;

	if (Settings.Num() == 0)
	{
		return false;
	}

	TArray<int32, TInlineAllocator<16>> LeaderOf;
	TArray<WidgetAnimLoop::FLayerSpan, TInlineAllocator<16>> Spans;
	TArray<float, TInlineAllocator<16>> GroupLead;
	TArray<float, TInlineAllocator<16>> GroupEnd;
	WidgetAnimLoop::ComputeGroupSpans(Settings, LeaderOf, Spans, GroupLead, GroupEnd);

	float Duration = 0.f;
	const int32 Driving = WidgetAnimLoop::FindDrivingLeader(Settings, LeaderOf, GroupLead, GroupEnd, Duration);

	if (Driving == INDEX_NONE || !Settings[Driving].bLoop)
	{
		return false;
	}

	const float ReturnLegLength = GroupEnd[Driving] - GroupLead[Driving];
	OutPingPong = Settings[Driving].bPingPong;

	// A round trip is there-and-back, so what a Ping Pong loop visibly repeats is double the one-way span.
	OutLoopSeconds = OutPingPong ? ReturnLegLength * 2.f : ReturnLegLength;
	return true;
}

// =====================================================================================================
// Layers
// =====================================================================================================

FWidgetAnimSettings FWidgetAnimLayer::ToSettings() const
{
	FWidgetAnimSettings Out;

	Out.AnimType = AnimType;
	Out.Timing = Timing;
	Out.Duration = Duration;
	Out.Delay = Delay;
	Out.Easing = Easing;
	Out.CustomCurve = CustomCurve;
	Out.Spring = Spring;

	Out.StartOpacity = StartOpacity;
	Out.EndOpacity = EndOpacity;

	Out.StartScale = StartScale;
	Out.EndScale = EndScale;
	Out.Overshoot = Overshoot;

	Out.SlideFromDirection = SlideFromDirection;
	Out.SlideDistance = SlideDistance;
	Out.SlideOvershoot = SlideOvershoot;

	Out.StartAngle = StartAngle;
	Out.EndAngle = EndAngle;

	Out.PulseScaleAmount = PulseScaleAmount;

	Out.ScalarParameterName = ScalarParameterName;
	Out.StartScalarValue = StartScalarValue;
	Out.EndScalarValue = EndScalarValue;

	Out.VectorParameterName = VectorParameterName;
	Out.StartVectorValue = StartVectorValue;
	Out.EndVectorValue = EndVectorValue;

	// One line rather than a dozen, which is the whole point of the nested payload: the next family costs
	// one line here too instead of a copy of every one of its fields.
	Out.Transform = Transform;

	// bLoop / bPingPong / LoopCount / bInvertAnimation / bMirrorInOut / the two targets are all decisions
	// about ONE USE of this layer, so they are deliberately left at their defaults here and filled in by
	// the entry that references the animation.
	return Out;
}

FWidgetAnimLayer FWidgetAnimLayer::FromSettings(const FWidgetAnimSettings& Settings)
{
	FWidgetAnimLayer Out;

	Out.AnimType = Settings.AnimType;
	Out.Timing = Settings.Timing;
	Out.Duration = Settings.Duration;
	Out.Delay = Settings.Delay;
	Out.Easing = Settings.Easing;
	Out.CustomCurve = Settings.CustomCurve;
	Out.Spring = Settings.Spring;

	Out.StartOpacity = Settings.StartOpacity;
	Out.EndOpacity = Settings.EndOpacity;

	Out.StartScale = Settings.StartScale;
	Out.EndScale = Settings.EndScale;
	Out.Overshoot = Settings.Overshoot;

	Out.SlideFromDirection = Settings.SlideFromDirection;
	Out.SlideDistance = Settings.SlideDistance;
	Out.SlideOvershoot = Settings.SlideOvershoot;

	Out.StartAngle = Settings.StartAngle;
	Out.EndAngle = Settings.EndAngle;

	Out.PulseScaleAmount = Settings.PulseScaleAmount;

	Out.ScalarParameterName = Settings.ScalarParameterName;
	Out.StartScalarValue = Settings.StartScalarValue;
	Out.EndScalarValue = Settings.EndScalarValue;

	Out.VectorParameterName = Settings.VectorParameterName;
	Out.StartVectorValue = Settings.StartVectorValue;
	Out.EndVectorValue = Settings.EndVectorValue;

	Out.Transform = Settings.Transform;

	return Out;
}

// =====================================================================================================
// Track player
// =====================================================================================================

void UWidgetAnimLibrary::StartWidgetAnimTrack(FWidgetAnimPlayer& Player, FName AnimId, int32 SourceIndex, const TArray<FWidgetAnimSettings>& Settings, bool bIsOut, float Speed, bool bRestartIfPlaying, bool bHoldWhenFinished)
{
	if (Settings.Num() == 0)
	{
		return;
	}

	// Matched on all three: an entry with no Anim ID is still a distinct animation, and the same entry
	// played forwards and backwards are two different things that may legitimately overlap.
	FWidgetAnimTrack* Existing = Player.Tracks.FindByPredicate(
		[AnimId, SourceIndex, bIsOut](const FWidgetAnimTrack& Track)
		{
			return Track.AnimId == AnimId && Track.SourceIndex == SourceIndex && Track.bIsOut == bIsOut;
		});

	if (Existing && !bRestartIfPlaying && !Existing->bFinished)
	{
		return;
	}

	FWidgetAnimTrack& Track = Existing ? *Existing : Player.Tracks.AddDefaulted_GetRef();

	Track.AnimId = AnimId;
	Track.SourceIndex = SourceIndex;
	Track.bIsOut = bIsOut;
	Track.bFinished = false;
	Track.bSettled = false;
	Track.bHoldWhenFinished = bHoldWhenFinished;
	Track.Speed = FMath::Max(Speed, 0.01f);
	Track.Settings = Settings;

	ResetWidgetAnimations(Track.Settings, Track.States);
}

void UWidgetAnimLibrary::StopWidgetAnimTracks(FWidgetAnimPlayer& Player, const TArray<FName>& AnimIds, bool bRemoveTracks)
{
	const bool bAll = AnimIds.Num() == 0;

	for (int32 Index = Player.Tracks.Num() - 1; Index >= 0; --Index)
	{
		if (!bAll && !AnimIds.Contains(Player.Tracks[Index].AnimId))
		{
			continue;
		}

		if (bRemoveTracks)
		{
			Player.Tracks.RemoveAt(Index);
		}
		else
		{
			// Left in place and marked done, so a track asked to stop holds where it got to rather than
			// snapping back to the baseline mid-motion.
			Player.Tracks[Index].bFinished = true;
			Player.Tracks[Index].bSettled = true;
		}
	}
}

bool UWidgetAnimLibrary::EndWidgetAnimTrackLoops(FWidgetAnimPlayer& Player, FName AnimId, bool bSoftSettle)
{
	if (AnimId.IsNone())
	{
		return false;
	}

	bool bAnyAffected = false;

	for (FWidgetAnimTrack& Track : Player.Tracks)
	{
		if (Track.bFinished || Track.AnimId != AnimId)
		{
			continue;
		}

		// Normally already in step; guarded so a track that has not been ticked yet cannot index past the end.
		if (Track.States.Num() != Track.Settings.Num())
		{
			ResetWidgetAnimations(Track.Settings, Track.States);
		}

		for (int32 Index = 0; Index < Track.Settings.Num(); ++Index)
		{
			const FWidgetAnimSettings& Settings = Track.Settings[Index];
			FWidgetAnimState& State = Track.States[Index];
			if (State.bFinished)
			{
				continue;
			}

			if (bSoftSettle)
			{
				// Non-looping layers already run to their end without being told.
				if (Settings.bLoop)
				{
					State.bSettleRequested = true;
				}
			}
			else
			{
				// Straight to the end pose. The next tick sees every layer finished, marks the track
				// finished and reports it, and applies this pose (or drops the track if it doesn't hold).
				// A finished clock is always evaluated at the end of its timeline, whatever CycleTime says.
				State.DelayRemaining = 0.f;
				State.bPlayingForward = true;
				State.bFinished = true;
				State.bSettleRequested = false;
			}
		}

		bAnyAffected = true;
	}

	return bAnyAffected;
}

bool UWidgetAnimLibrary::AreWidgetAnimTracksPlaying(const FWidgetAnimPlayer& Player, const TArray<FName>& AnimIds)
{
	const bool bAll = AnimIds.Num() == 0;

	for (const FWidgetAnimTrack& Track : Player.Tracks)
	{
		if (Track.bFinished)
		{
			continue;
		}

		if (bAll || AnimIds.Contains(Track.AnimId))
		{
			return true;
		}
	}

	return false;
}

bool UWidgetAnimLibrary::TickWidgetAnimPlayer(UWidget* FallbackWidget, FWidgetAnimPlayer& Player, const FWidgetAnimBaselineSet& Baselines, float DeltaTime, TArray<FName>& OutJustFinished)
{
	OutJustFinished.Reset();

	FWidgetAnimResult Result;
	bool bAnyPlaying = false;

	for (FWidgetAnimTrack& Track : Player.Tracks)
	{
		if (Track.bFinished)
		{
			// Zero delta: re-evaluates the resting pose so a finished entrance keeps contributing rather
			// than vanishing from the combined result and letting the widget snap back.
			if (Track.bHoldWhenFinished)
			{
				TickAnimationsInto(Track.Settings, Track.States, 0.f, Track.bIsOut, Result, nullptr, &Baselines);
			}
			continue;
		}

		// Finished and settled are separate questions. A track is only finished once EVERY layer has
		// stopped - an endless loop never does until something ends it. Marking a track finished as
		// soon as its non-looping layers were done (which, for a stack made only of loops, was on its
		// very first frame) is what stopped loops from ever advancing: finished tracks are only
		// re-posed at zero delta.
		bool bAllComplete = false;
		Track.bSettled = TickAnimationsInto(Track.Settings, Track.States, DeltaTime * Track.Speed, Track.bIsOut, Result, &bAllComplete, &Baselines);

		if (bAllComplete)
		{
			Track.bFinished = true;
			Track.bSettled = true;
			OutJustFinished.AddUnique(Track.AnimId);
		}
		else
		{
			bAnyPlaying = true;
		}
	}

	// One apply for the whole player. Two animations touching the same widget therefore combine - opacity
	// and scale multiply, translation and angle add - instead of the later one erasing the earlier one.
	ApplyResultInternal(FallbackWidget, Result, Baselines.bCaptured ? &Baselines : nullptr);
	ApplyMaterialAnimResult(Result);

	Player.Tracks.RemoveAll([](const FWidgetAnimTrack& Track) { return Track.bFinished && !Track.bHoldWhenFinished; });

	return bAnyPlaying;
}

// =====================================================================================================
// Widget targeting
// =====================================================================================================

namespace WidgetAnimTree
{
	/**
	 * Depth-first walk of Widget and everything beneath it, regardless of Is Variable.
	 *
	 * Widgets in the same WidgetTree are identified by their plain name, since names are unique per tree.
	 * Widgets inside a nested User Widget become "NestedUserWidgetName/InnerWidgetName", because there the
	 * plain name is only unique within the nested widget's own tree.
	 */
	static void Gather(UWidget* Widget, const FString& TreePrefix, TArray<TPair<FString, UWidget*>>& Out, int32 Depth = 0)
	{
		static constexpr int32 MaxDepth = 128;
		if (!Widget || Depth > MaxDepth)
		{
			return;
		}

		const FString Path = TreePrefix.IsEmpty() ? Widget->GetName() : (TreePrefix + TEXT("/") + Widget->GetName());
		Out.Emplace(Path, Widget);

		// UContentWidget (Button, Border, SizeBox, ...) derives from UPanelWidget, so this covers both.
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				Gather(Panel->GetChildAt(Index), TreePrefix, Out, Depth + 1);
			}
		}
		else if (const UUserWidget* NestedUserWidget = Cast<UUserWidget>(Widget))
		{
			if (NestedUserWidget->WidgetTree)
			{
				Gather(NestedUserWidget->WidgetTree->RootWidget, Path, Out, Depth + 1);
			}
		}
	}
}

void UWidgetAnimLibrary::GatherTargetableWidgetPaths(UWidget* Root, TArray<FString>& OutPaths)
{
	TArray<TPair<FString, UWidget*>> All;
	WidgetAnimTree::Gather(Root, FString(), All);

	OutPaths.Reset(All.Num());
	for (const TPair<FString, UWidget*>& Entry : All)
	{
		OutPaths.AddUnique(Entry.Key);
	}
}

UWidget* UWidgetAnimLibrary::FindWidgetByPath(UWidget* Root, FName Path)
{
	if (Path.IsNone() || !Root)
	{
		return nullptr;
	}

	TArray<TPair<FString, UWidget*>> All;
	WidgetAnimTree::Gather(Root, FString(), All);

	const FString PathString = Path.ToString();
	for (const TPair<FString, UWidget*>& Entry : All)
	{
		if (Entry.Key.Equals(PathString, ESearchCase::IgnoreCase))
		{
			return Entry.Value;
		}
	}

	return nullptr;
}

void UWidgetAnimLibrary::GatherTargetableWidgetPathsInTree(UUserWidget* Owner, TArray<FString>& OutPaths)
{
	OutPaths.Reset();

	if (Owner && Owner->WidgetTree)
	{
		GatherTargetableWidgetPaths(Owner->WidgetTree->RootWidget, OutPaths);
	}
}

UWidget* UWidgetAnimLibrary::FindWidgetByPathInTree(UUserWidget* Owner, FName Path)
{
	if (!Owner || !Owner->WidgetTree)
	{
		return nullptr;
	}

	return FindWidgetByPath(Owner->WidgetTree->RootWidget, Path);
}

// =====================================================================================================
// Designer interactive-edit sync
// =====================================================================================================

#if WITH_EDITOR

namespace WidgetAnimDesignerSync
{
	static TArray<TWeakObjectPtr<UObject>>& GetRegistry()
	{
		static TArray<TWeakObjectPtr<UObject>> Registry;
		return Registry;
	}

	void RegisterPreview(UObject* PreviewWidget)
	{
		if (!PreviewWidget)
		{
			return;
		}

		TArray<TWeakObjectPtr<UObject>>& Registry = GetRegistry();

		// Compacted on the way past. Design-time widgets are thrown away and rebuilt constantly, and
		// nothing else would ever clear the stale entries.
		Registry.RemoveAll([](const TWeakObjectPtr<UObject>& Entry) { return !Entry.IsValid(); });
		Registry.AddUnique(PreviewWidget);
	}

	void UnregisterPreview(UObject* PreviewWidget)
	{
		GetRegistry().RemoveAll([PreviewWidget](const TWeakObjectPtr<UObject>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == PreviewWidget;
		});
	}

	void ForEachPreviewOf(const UObject* Template, TFunctionRef<void(UObject*)> Update)
	{
		if (!Template)
		{
			return;
		}

		for (const TWeakObjectPtr<UObject>& Entry : GetRegistry())
		{
			UObject* Preview = Entry.Get();
			if (!Preview || Preview == Template)
			{
				continue;
			}

			// Class and name together. Names are unique within a WidgetTree, and a preview is built from
			// the template as its archetype, so it carries the same name - which makes this exact rather
			// than a guess. Anything that somehow isn't a match simply doesn't get updated, and the
			// ordinary commit-time rebuild still puts it right.
			if (Preview->GetClass() != Template->GetClass())
			{
				continue;
			}

			// Editing Class Defaults on the widget being edited goes through that class's CDO, whose name
			// is "Default__WBP_Thing_C" and matches nothing. Class alone is the right test there: a CDO
			// stands for every instance of its class by definition.
			const bool bTemplateIsClassDefault = Template->HasAnyFlags(RF_ClassDefaultObject);
			if (bTemplateIsClassDefault || Preview->GetFName() == Template->GetFName())
			{
				Update(Preview);
			}
		}
	}

	bool IsInteractiveChange(const FPropertyChangedEvent& PropertyChangedEvent)
	{
		// Interactive is the mid-drag case this exists for. ValueSet is here too because a spin box sends
		// exactly one of those as the drag ENDS, and the preview rebuild that would otherwise pick that
		// value up is asynchronous - so without it the final value of a drag is the one frame that never
		// reaches the preview. Pushing a pose twice is free; missing the last one is what you notice.
		return PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive
			|| PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet;
	}
}

#endif // WITH_EDITOR