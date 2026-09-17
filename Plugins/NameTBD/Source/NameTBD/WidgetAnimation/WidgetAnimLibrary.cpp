// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

float UWidgetAnimLibrary::EaseOutBack(float Alpha, float Overshoot)
{
	const float C1 = Overshoot;
	const float C3 = C1 + 1.f;
	const float T = Alpha - 1.f;
	return 1.f + C3 * FMath::Pow(T, 3.f) + C1 * FMath::Pow(T, 2.f);
}

float UWidgetAnimLibrary::EvaluateEasedAlpha(const FWidgetAnimSettings& Settings, float LinearAlpha)
{
	LinearAlpha = FMath::Clamp(LinearAlpha, 0.f, 1.f);

	switch (Settings.Easing)
	{
	case EWidgetAnimEasing::Linear:
		return LinearAlpha;
	case EWidgetAnimEasing::EaseIn:
		return FMath::InterpEaseIn(0.f, 1.f, LinearAlpha, 2.f);
	case EWidgetAnimEasing::EaseOut:
		return FMath::InterpEaseOut(0.f, 1.f, LinearAlpha, 2.f);
	case EWidgetAnimEasing::EaseInOut:
		return FMath::InterpEaseInOut(0.f, 1.f, LinearAlpha, 2.f);
	case EWidgetAnimEasing::Custom:
		return Settings.CustomCurve ? Settings.CustomCurve->GetFloatValue(LinearAlpha) : LinearAlpha;
	}

	return LinearAlpha;
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

void UWidgetAnimLibrary::ApplyAnimationContribution(const FWidgetAnimSettings& Settings, float DirectedAlpha, FWidgetAnimResult& OutResult)
{
	switch (Settings.AnimType)
	{
	case EWidgetAnimType::Fade:
	{
		const float EasedAlpha = EvaluateEasedAlpha(Settings, DirectedAlpha);
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget);
		Transform.Opacity *= FMath::Lerp(Settings.StartOpacity, Settings.EndOpacity, EasedAlpha);
		break;
	}

	case EWidgetAnimType::Scale:
	{
		const float ScaleAlpha = Settings.Overshoot > 0.f
			? EaseOutBack(DirectedAlpha, Settings.Overshoot)
			: EvaluateEasedAlpha(Settings, DirectedAlpha);
		const float ScaleValue = FMath::Lerp(Settings.StartScale, Settings.EndScale, ScaleAlpha);
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget);
		Transform.Scale *= FVector2D(ScaleValue, ScaleValue);
		break;
	}

	case EWidgetAnimType::Slide:
	{
		const float SlideAlpha = Settings.SlideOvershoot > 0.f
			? EaseOutBack(DirectedAlpha, Settings.SlideOvershoot)
			: EvaluateEasedAlpha(Settings, DirectedAlpha);
		const FVector2D StartOffset = GetSlideDirectionOffset(Settings.SlideFromDirection) * Settings.SlideDistance;
		FWidgetAnimTargetedTransform& Transform = FindOrAddTargetedTransform(OutResult, Settings.TargetWidget);
		Transform.Translation += FMath::Lerp(StartOffset, FVector2D::ZeroVector, SlideAlpha);
		break;
	}

	case EWidgetAnimType::Rotate:
	{
		const float EasedAlpha = EvaluateEasedAlpha(Settings, DirectedAlpha);
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
			const float EasedAlpha = EvaluateEasedAlpha(Settings, DirectedAlpha);
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
			const float EasedAlpha = EvaluateEasedAlpha(Settings, DirectedAlpha);
			const FLinearColor Value = FMath::Lerp(Settings.StartVectorValue, Settings.EndVectorValue, EasedAlpha);
			FWidgetAnimVectorTarget& Target = OutResult.VectorParameters.FindOrAdd(Settings.VectorParameterName);
			Target.TargetImage = Settings.MaterialTargetImage;
			Target.Value = Value;
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

	static FLayerSpan GetLayerSpan(const FWidgetAnimSettings& Settings)
	{
		// Speed scales the layer's whole placement, delay included, so a stagger keeps its proportions.
		const float Speed = FMath::Max(Settings.PlaybackSpeed, 0.01f);

		FLayerSpan Span;
		Span.Start = FMath::Max(Settings.Delay, 0.f) / Speed;
		Span.Length = FMath::Max(Settings.Duration, KINDA_SMALL_NUMBER) / Speed;
		return Span;
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

bool UWidgetAnimLibrary::TickAnimationsInto(const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation, FWidgetAnimResult& OutResult, bool* bOutAllComplete)
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

		// Before its start the layer holds its start pose, after its end its end pose. Direction needs no
		// special case: a return leg is the timeline running backwards, so the alpha falls on its own.
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

		const bool bInvert = GetEffectiveInvert(Settings, bIsOutAnimation);
		ApplyAnimationContribution(Settings, bInvert ? 1.f - LinearAlpha : LinearAlpha, OutResult);

		if (!States[Index].bFinished)
		{
			bAllComplete = false;

			// An endless loop never ends by itself, so it must not hold the whole animation open forever.
			if (!IsEndlessLoop(Settings))
			{
				bAllSettled = false;
			}
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
			Target->SetRenderTransformAngle(Baseline.Angle + Transform.Angle);
		}
		else
		{
			Target->SetRenderOpacity(Transform.Opacity);
			Target->SetRenderScale(Transform.Scale);
			Target->SetRenderTranslation(Transform.Translation);
			Target->SetRenderTransformAngle(Transform.Angle);
		}
	}
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
	const FWidgetAnimResult Result = TickWidgetAnimations(Animations, States, DeltaTime, bIsOutAnimation);
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

	FWidgetAnimResult StartResult;
	for (const FWidgetAnimSettings& Settings : Animations)
	{
		ApplyAnimationContribution(Settings, GetEffectiveInvert(Settings, /*bIsOutAnimation=*/false) ? 1.f : 0.f, StartResult);
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
		if (!States[Index].bFinished && !IsEndlessLoop(Animations[Index]))
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
	Baseline.Angle = Transform.Angle;
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

	bool bNeedsFallback = false;

	auto Gather = [&Baselines, &bNeedsFallback](const TArray<FWidgetAnimSettings>& Animations)
	{
		for (const FWidgetAnimSettings& Settings : Animations)
		{
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
		Widget->SetRenderTransformAngle(Baseline.Angle);
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
	Widget->SetRenderTransformAngle(0.f);
}

// =====================================================================================================
// Posing and preview
// =====================================================================================================

void UWidgetAnimLibrary::ApplyWidgetAnimationsAtAlpha(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, float Alpha, bool bIsOutAnimation, const FWidgetAnimBaselineSet& Baselines)
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.f, 1.f);

	// Every entry is posed at the SAME alpha rather than at its own point on a shared clock. Delay and
	// looping are deliberately ignored: this answers "what does the stack look like N% of the way
	// through", which is what a scrub slider and a held start/end pose both want. Watching the stagger
	// between two delayed entries is what the playing modes are for.
	FWidgetAnimResult Result;
	Result.bAllFinished = true;

	for (const FWidgetAnimSettings& Settings : Animations)
	{
		const bool bInvert = GetEffectiveInvert(Settings, bIsOutAnimation);
		ApplyAnimationContribution(Settings, bInvert ? 1.f - ClampedAlpha : ClampedAlpha, Result);
	}

	ApplyResultInternal(FallbackWidget, Result, Baselines.bCaptured ? &Baselines : nullptr);
	ApplyMaterialAnimResult(Result);
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

// =====================================================================================================
// Layers
// =====================================================================================================

FWidgetAnimSettings FWidgetAnimLayer::ToSettings() const
{
	FWidgetAnimSettings Out;

	Out.AnimType = AnimType;
	Out.Duration = Duration;
	Out.Delay = Delay;
	Out.Easing = Easing;
	Out.CustomCurve = CustomCurve;

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

	// bLoop / bPingPong / LoopCount / bInvertAnimation / bMirrorInOut / the two targets are all decisions
	// about ONE USE of this layer, so they are deliberately left at their defaults here and filled in by
	// the entry that references the animation.
	return Out;
}

FWidgetAnimLayer FWidgetAnimLayer::FromSettings(const FWidgetAnimSettings& Settings)
{
	FWidgetAnimLayer Out;

	Out.AnimType = Settings.AnimType;
	Out.Duration = Settings.Duration;
	Out.Delay = Settings.Delay;
	Out.Easing = Settings.Easing;
	Out.CustomCurve = Settings.CustomCurve;

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
				TickAnimationsInto(Track.Settings, Track.States, 0.f, Track.bIsOut, Result);
			}
			continue;
		}

		// Finished and settled are separate questions. A track is only finished once EVERY layer has
		// stopped - an endless loop never does until something ends it. Marking a track finished as
		// soon as its non-looping layers were done (which, for a stack made only of loops, was on its
		// very first frame) is what stopped loops from ever advancing: finished tracks are only
		// re-posed at zero delta.
		bool bAllComplete = false;
		Track.bSettled = TickAnimationsInto(Track.Settings, Track.States, DeltaTime * Track.Speed, Track.bIsOut, Result, &bAllComplete);

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