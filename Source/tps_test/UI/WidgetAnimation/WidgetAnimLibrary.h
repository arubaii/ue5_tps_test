#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/Widget.h"
#include "Components/Image.h"
#include "Curves/CurveFloat.h"
#include "WidgetAnimLibrary.generated.h"

class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EWidgetAnimType : uint8
{
	Fade,
	Scale,
	Slide,
	Rotate,
	Pulse,
	MaterialScalar,
	MaterialVector
};

UENUM(BlueprintType)
enum class EWidgetAnimEasing : uint8
{
	Linear,
	EaseIn,
	EaseOut,
	EaseInOut,
	Custom
};

UENUM(BlueprintType)
enum class EWidgetSlideDirection : uint8
{
	Up,
	Down,
	Left,
	Right
};

USTRUCT(BlueprintType)
struct TPS_TEST_API FWidgetAnimSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	EWidgetAnimType AnimType = EWidgetAnimType::Fade;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.01"))
	float Duration = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.0"))
	float Delay = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "Easing == EWidgetAnimEasing::Custom", EditConditionHides))
	UCurveFloat* CustomCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bLoop = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "bLoop", EditConditionHides))
	bool bPingPong = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "bLoop", EditConditionHides, ClampMin = "0"))
	int32 LoopCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bInvertAnimation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bMirrorInOut = false;

	/** Reverted to native UWidget pointer for Designer dropdowns */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "AnimType != EWidgetAnimType::MaterialScalar && AnimType != EWidgetAnimType::MaterialVector", EditConditionHides))
	TObjectPtr<UWidget> TargetWidget = nullptr;

	// ------------------------------------------------------------------
	// Fade
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (EditCondition = "AnimType == EWidgetAnimType::Fade", EditConditionHides))
	float StartOpacity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (EditCondition = "AnimType == EWidgetAnimType::Fade", EditConditionHides))
	float EndOpacity = 1.f;

	// ------------------------------------------------------------------
	// Scale (pop-in)
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EWidgetAnimType::Scale", EditConditionHides))
	float StartScale = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EWidgetAnimType::Scale", EditConditionHides))
	float EndScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EWidgetAnimType::Scale", EditConditionHides, ClampMin = "0.0"))
	float Overshoot = 0.4f;

	// ------------------------------------------------------------------
	// Slide
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", EditConditionHides))
	EWidgetSlideDirection SlideFromDirection = EWidgetSlideDirection::Down;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", EditConditionHides, ClampMin = "0.0"))
	float SlideDistance = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", EditConditionHides, ClampMin = "0.0"))
	float SlideOvershoot = 0.f;

	// ------------------------------------------------------------------
	// Rotate (spin-in)
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotate", meta = (EditCondition = "AnimType == EWidgetAnimType::Rotate", EditConditionHides))
	float StartAngle = -180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotate", meta = (EditCondition = "AnimType == EWidgetAnimType::Rotate", EditConditionHides))
	float EndAngle = 0.f;

	// ------------------------------------------------------------------
	// Pulse
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pulse", meta = (EditCondition = "AnimType == EWidgetAnimType::Pulse", EditConditionHides, ClampMin = "0.0"))
	float PulseScaleAmount = 0.05f;

	// ------------------------------------------------------------------
	// Material Scalar / Vector
	// ------------------------------------------------------------------

	/** Reverted to native UImage pointer for Designer dropdowns */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (EditCondition = "AnimType == EWidgetAnimType::MaterialScalar || AnimType == EWidgetAnimType::MaterialVector", EditConditionHides))
	TObjectPtr<UImage> MaterialTargetImage = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (EditCondition = "AnimType == EWidgetAnimType::MaterialScalar", EditConditionHides))
	FName ScalarParameterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (EditCondition = "AnimType == EWidgetAnimType::MaterialScalar", EditConditionHides))
	float StartScalarValue = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (EditCondition = "AnimType == EWidgetAnimType::MaterialScalar", EditConditionHides))
	float EndScalarValue = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (EditCondition = "AnimType == EWidgetAnimType::MaterialVector", EditConditionHides))
	FName VectorParameterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (EditCondition = "AnimType == EWidgetAnimType::MaterialVector", EditConditionHides))
	FLinearColor StartVectorValue = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (EditCondition = "AnimType == EWidgetAnimType::MaterialVector", EditConditionHides))
	FLinearColor EndVectorValue = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct TPS_TEST_API FWidgetAnimState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float DelayRemaining = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float CycleTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bFinished = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	int32 LoopsCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bPlayingForward = true;
};

USTRUCT(BlueprintType)
struct TPS_TEST_API FWidgetAnimTargetedTransform
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	UWidget* TargetWidget = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float Opacity = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FVector2D Scale = FVector2D(1.f, 1.f);

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FVector2D Translation = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float Angle = 0.f;
};

USTRUCT(BlueprintType)
struct TPS_TEST_API FWidgetAnimScalarTarget
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	UImage* TargetImage = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float Value = 0.f;
};

USTRUCT(BlueprintType)
struct TPS_TEST_API FWidgetAnimVectorTarget
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	UImage* TargetImage = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FLinearColor Value = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct TPS_TEST_API FWidgetAnimResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimTargetedTransform> TargetedTransforms;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TMap<FName, FWidgetAnimScalarTarget> ScalarParameters;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TMap<FName, FWidgetAnimVectorTarget> VectorParameters;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bAllFinished = false;
};

UENUM(BlueprintType)
enum class EWidgetAnimPhase : uint8
{
	Idle,
	PlayingIn,
	Shown,
	PlayingOut,
	Hidden
};

USTRUCT(BlueprintType)
struct TPS_TEST_API FWidgetAnimPlaybackState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	EWidgetAnimPhase Phase = EWidgetAnimPhase::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimState> InStates;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimState> OutStates;
};

UCLASS()
class TPS_TEST_API UWidgetAnimLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static FWidgetAnimResult TickWidgetAnimations(const TArray<FWidgetAnimSettings>& Animations, UPARAM(ref) TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation = false);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void ApplyWidgetAnimResult(UWidget* FallbackWidget, const FWidgetAnimResult& Result);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void ApplyMaterialAnimResult(const FWidgetAnimResult& Result);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void PlayWidgetAnimations(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, UPARAM(ref) TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation = false);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void InitializeWidgetAnimations(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, UPARAM(ref) TArray<FWidgetAnimState>& States);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void ResetWidgetAnimations(const TArray<FWidgetAnimSettings>& Animations, UPARAM(ref) TArray<FWidgetAnimState>& States);

	UFUNCTION(BlueprintPure, Category = "Widget Animation")
	static bool AreWidgetAnimationsFinished(const TArray<FWidgetAnimSettings>& Animations, const TArray<FWidgetAnimState>& States);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void PlayWidgetIn(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, UPARAM(ref) FWidgetAnimPlaybackState& PlaybackState);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void PlayWidgetOut(UPARAM(ref) FWidgetAnimPlaybackState& PlaybackState, const TArray<FWidgetAnimSettings>& OutAnimations);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static bool TickWidgetAnimPlayback(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, UPARAM(ref) FWidgetAnimPlaybackState& PlaybackState, float DeltaTime);

private:

	static float EvaluateEasedAlpha(const FWidgetAnimSettings& Settings, float LinearAlpha);
	static float EaseOutBack(float Alpha, float Overshoot);
	static FVector2D GetSlideDirectionOffset(EWidgetSlideDirection Direction);

	static FWidgetAnimTargetedTransform& FindOrAddTargetedTransform(FWidgetAnimResult& Result, UWidget* TargetWidget);

	static void ApplyAnimationContribution(const FWidgetAnimSettings& Settings, float DirectedAlpha, FWidgetAnimResult& OutResult);
	static void EvaluateSingleAnimation(const FWidgetAnimSettings& Settings, FWidgetAnimState& State, float DeltaTime, FWidgetAnimResult& OutResult, bool bIsOutAnimation);

	static bool GetEffectiveInvert(const FWidgetAnimSettings& Settings, bool bIsOutAnimation);
};