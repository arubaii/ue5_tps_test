#include "UI/WidgetAnimation/WidgetAnimLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

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

void UWidgetAnimLibrary::EvaluateSingleAnimation(const FWidgetAnimSettings& Settings, FWidgetAnimState& State, float DeltaTime, FWidgetAnimResult& OutResult, bool bIsOutAnimation)
{
	const bool bInvert = GetEffectiveInvert(Settings, bIsOutAnimation);

	if (State.bFinished)
	{
		const float RestingAlpha = State.bPlayingForward ? 1.f : 0.f;
		ApplyAnimationContribution(Settings, bInvert ? 1.f - RestingAlpha : RestingAlpha, OutResult);
		return;
	}

	if (State.DelayRemaining > 0.f)
	{
		State.DelayRemaining -= DeltaTime;
		if (State.DelayRemaining > 0.f)
		{
			return; 
		}

		DeltaTime = -State.DelayRemaining;
		State.DelayRemaining = 0.f;
	}

	const float Duration = FMath::Max(Settings.Duration, KINDA_SMALL_NUMBER);
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
			State.bPlayingForward = true;
			break;
		}
	}

	const float LinearAlpha = FMath::Clamp(State.CycleTime / Duration, 0.f, 1.f);
	const float DirectedAlpha = State.bFinished
		? (State.bPlayingForward ? 1.f : 0.f)
		: (State.bPlayingForward ? LinearAlpha : (1.f - LinearAlpha));

	ApplyAnimationContribution(Settings, bInvert ? 1.f - DirectedAlpha : DirectedAlpha, OutResult);
}

FWidgetAnimResult UWidgetAnimLibrary::TickWidgetAnimations(const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation)
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
		EvaluateSingleAnimation(Settings, States[Index], DeltaTime, Result, bIsOutAnimation);

		const bool bCountsTowardFinished = !Settings.bLoop;
		if (bCountsTowardFinished && !States[Index].bFinished)
		{
			Result.bAllFinished = false;
		}
	}

	return Result;
}

void UWidgetAnimLibrary::ApplyWidgetAnimResult(UWidget* FallbackWidget, const FWidgetAnimResult& Result)
{
	for (const FWidgetAnimTargetedTransform& Transform : Result.TargetedTransforms)
	{
		UWidget* Target = Transform.TargetWidget ? Transform.TargetWidget : FallbackWidget;
		if (!Target)
		{
			continue;
		}

		Target->SetRenderOpacity(Transform.Opacity);
		Target->SetRenderScale(Transform.Scale);
		Target->SetRenderTranslation(Transform.Translation);
		Target->SetRenderTransformAngle(Transform.Angle);
	}
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

void UWidgetAnimLibrary::PlayWidgetAnimations(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation)
{
	const FWidgetAnimResult Result = TickWidgetAnimations(Animations, States, DeltaTime, bIsOutAnimation);
	ApplyWidgetAnimResult(FallbackWidget, Result);
	ApplyMaterialAnimResult(Result);
}

void UWidgetAnimLibrary::InitializeWidgetAnimations(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States)
{
	ResetWidgetAnimations(Animations, States);

	FWidgetAnimResult StartResult;
	for (const FWidgetAnimSettings& Settings : Animations)
	{
		ApplyAnimationContribution(Settings, GetEffectiveInvert(Settings, /*bIsOutAnimation=*/false) ? 1.f : 0.f, StartResult);
	}

	ApplyWidgetAnimResult(FallbackWidget, StartResult);
	ApplyMaterialAnimResult(StartResult);
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

bool UWidgetAnimLibrary::TickWidgetAnimPlayback(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, FWidgetAnimPlaybackState& PlaybackState, float DeltaTime)
{
	switch (PlaybackState.Phase)
	{
	case EWidgetAnimPhase::PlayingIn:
	{
		PlayWidgetAnimations(FallbackWidget, InAnimations, PlaybackState.InStates, DeltaTime, /*bIsOutAnimation=*/false);
		if (AreWidgetAnimationsFinished(InAnimations, PlaybackState.InStates))
		{
			PlaybackState.Phase = EWidgetAnimPhase::Shown;
		}
		return false;
	}

	case EWidgetAnimPhase::PlayingOut:
	{
		PlayWidgetAnimations(FallbackWidget, OutAnimations, PlaybackState.OutStates, DeltaTime, /*bIsOutAnimation=*/true);
		if (AreWidgetAnimationsFinished(OutAnimations, PlaybackState.OutStates))
		{
			PlaybackState.Phase = EWidgetAnimPhase::Hidden;
			return true; 
		}
		return false;
	}

	case EWidgetAnimPhase::Idle:
	case EWidgetAnimPhase::Shown:
	case EWidgetAnimPhase::Hidden:
	default:
		return false;
	}
}