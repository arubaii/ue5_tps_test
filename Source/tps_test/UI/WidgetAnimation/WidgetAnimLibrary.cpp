// WidgetAnimLibrary.cpp

#include "WidgetAnimLibrary.h"

float UWidgetAnimLibrary::EaseOutBack(float Alpha, float Overshoot)
{
	// Standard "ease out back" formula: overshoots past 1.0 then settles.
	// Overshoot controls how far past 1.0 it punches (0 = no punch).
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
	// Returns the unit direction the widget starts displaced toward, before
	// scaling by SlideDistance. E.g. "From Up" starts above rest position,
	// which is a smaller (more negative) screen-space Y.
	switch (Direction)
	{
	case EWidgetSlideDirection::Up:    return FVector2D(0.f, -1.f);
	case EWidgetSlideDirection::Down:  return FVector2D(0.f, 1.f);
	case EWidgetSlideDirection::Left:  return FVector2D(-1.f, 0.f);
	case EWidgetSlideDirection::Right: return FVector2D(1.f, 0.f);
	}

	return FVector2D::ZeroVector;
}

void UWidgetAnimLibrary::ApplyAnimationContribution(const FWidgetAnimSettings& Settings, float DirectedAlpha, FWidgetAnimResult& OutResult)
{
	switch (Settings.AnimType)
	{
	case EWidgetAnimType::Fade:
	{
		const float EasedAlpha = EvaluateEasedAlpha(Settings, DirectedAlpha);
		OutResult.Opacity *= FMath::Lerp(Settings.StartOpacity, Settings.EndOpacity, EasedAlpha);
		break;
	}

	case EWidgetAnimType::Scale:
	{
		const float ScaleAlpha = Settings.Overshoot > 0.f
			? EaseOutBack(DirectedAlpha, Settings.Overshoot)
			: EvaluateEasedAlpha(Settings, DirectedAlpha);
		const float ScaleValue = FMath::Lerp(Settings.StartScale, Settings.EndScale, ScaleAlpha);
		OutResult.Scale *= FVector2D(ScaleValue, ScaleValue);
		break;
	}

	case EWidgetAnimType::Slide:
	{
		const float SlideAlpha = Settings.SlideOvershoot > 0.f
			? EaseOutBack(DirectedAlpha, Settings.SlideOvershoot)
			: EvaluateEasedAlpha(Settings, DirectedAlpha);
		const FVector2D StartOffset = GetSlideDirectionOffset(Settings.SlideFromDirection) * Settings.SlideDistance;
		OutResult.Translation += FMath::Lerp(StartOffset, FVector2D::ZeroVector, SlideAlpha);
		break;
	}

	case EWidgetAnimType::Rotate:
	{
		const float EasedAlpha = EvaluateEasedAlpha(Settings, DirectedAlpha);
		OutResult.Angle += FMath::Lerp(Settings.StartAngle, Settings.EndAngle, EasedAlpha);
		break;
	}

	case EWidgetAnimType::Pulse:
	{
		// Continuous sine wave over one Duration-length cycle; DirectedAlpha
		// is the phase within the current cycle (0-1 -> a full sine wave).
		const float Phase = DirectedAlpha * 2.f * PI;
		const float ScaleMultiplier = 1.f + Settings.PulseScaleAmount * FMath::Sin(Phase);
		OutResult.Scale *= FVector2D(ScaleMultiplier, ScaleMultiplier);
		break;
	}
	}
}

void UWidgetAnimLibrary::EvaluateSingleAnimation(const FWidgetAnimSettings& Settings, FWidgetAnimState& State, float DeltaTime, FWidgetAnimResult& OutResult)
{
	if (State.bFinished)
	{
		// Already reached its end - keep contributing the resting value
		// directly rather than re-deriving it each tick.
		const float RestingAlpha = State.bPlayingForward ? 1.f : 0.f;
		ApplyAnimationContribution(Settings, RestingAlpha, OutResult);
		return;
	}

	// Count down any remaining start delay first.
	if (State.DelayRemaining > 0.f)
	{
		State.DelayRemaining -= DeltaTime;
		if (State.DelayRemaining > 0.f)
		{
			return; // still waiting - identity contribution
		}

		// Carry over whatever part of this tick's DeltaTime occurred after
		// the delay actually ended, so we don't lose/duplicate time.
		DeltaTime = -State.DelayRemaining;
		State.DelayRemaining = 0.f;
	}

	const float Duration = FMath::Max(Settings.Duration, KINDA_SMALL_NUMBER);

	// CycleTime is bounded to [0, Duration] and wrapped in place below, so
	// looping only ever advances once per actually-completed cycle - unlike
	// recomputing from an ever-growing total elapsed time every tick.
	State.CycleTime += DeltaTime;

	while (State.CycleTime >= Duration)
	{
		if (Settings.bLoop && (Settings.LoopCount <= 0 || State.LoopsCompleted < Settings.LoopCount - 1))
		{
			State.CycleTime -= Duration;
			State.LoopsCompleted++;
			if (Settings.bPingPong)
			{
				State.bPlayingForward = !State.bPlayingForward;
			}
		}
		else
		{
			State.CycleTime = Duration;
			State.bFinished = true;
			// Always settle at the End pose when finishing, regardless of
			// which bounce direction ping-pong happened to be facing - the
			// alternative is that an even LoopCount lands back at Start
			// purely by parity, which reads as broken rather than intentional.
			State.bPlayingForward = true;
			break;
		}
	}

	const float LinearAlpha = FMath::Clamp(State.CycleTime / Duration, 0.f, 1.f);
	const float DirectedAlpha = State.bFinished
		? (State.bPlayingForward ? 1.f : 0.f)
		: (State.bPlayingForward ? LinearAlpha : (1.f - LinearAlpha));

	ApplyAnimationContribution(Settings, DirectedAlpha, OutResult);
}

FWidgetAnimResult UWidgetAnimLibrary::TickWidgetAnimations(const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime)
{
	if (States.Num() != Animations.Num())
	{
		const int32 OldNum = States.Num();
		States.SetNum(Animations.Num());
		for (int32 Index = OldNum; Index < Animations.Num(); ++Index)
		{
			States[Index].DelayRemaining = FMath::Max(Animations[Index].Delay, 0.f);
		}
	}

	FWidgetAnimResult Result;
	Result.bAllFinished = true;

	for (int32 Index = 0; Index < Animations.Num(); ++Index)
	{
		const FWidgetAnimSettings& Settings = Animations[Index];
		EvaluateSingleAnimation(Settings, States[Index], DeltaTime, Result);

		const bool bCountsTowardFinished = !Settings.bLoop;
		if (bCountsTowardFinished && !States[Index].bFinished)
		{
			Result.bAllFinished = false;
		}
	}

	return Result;
}

void UWidgetAnimLibrary::ApplyWidgetAnimResult(UWidget* Widget, const FWidgetAnimResult& Result)
{
	if (!Widget)
	{
		return;
	}

	Widget->SetRenderOpacity(Result.Opacity);
	Widget->SetRenderScale(Result.Scale);
	Widget->SetRenderTranslation(Result.Translation);
	Widget->SetRenderTransformAngle(Result.Angle);
}

void UWidgetAnimLibrary::PlayWidgetAnimations(UWidget* Widget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime)
{
	const FWidgetAnimResult Result = TickWidgetAnimations(Animations, States, DeltaTime);
	ApplyWidgetAnimResult(Widget, Result);
}

void UWidgetAnimLibrary::InitializeWidgetAnimations(UWidget* Widget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States)
{
	ResetWidgetAnimations(Animations, States);

	// Compute every entry's contribution at DirectedAlpha 0 (i.e. its Start
	// value), ignoring Delay entirely, so the widget is placed in the exact
	// pre-animation pose regardless of how long each entry waits to start.
	FWidgetAnimResult StartResult;
	for (const FWidgetAnimSettings& Settings : Animations)
	{
		ApplyAnimationContribution(Settings, 0.f, StartResult);
	}

	ApplyWidgetAnimResult(Widget, StartResult);
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
		const FWidgetAnimSettings& Settings = Animations[Index];
		const bool bCountsTowardFinished = !Settings.bLoop;
		if (bCountsTowardFinished && !States[Index].bFinished)
		{
			return false;
		}
	}

	return true;
}