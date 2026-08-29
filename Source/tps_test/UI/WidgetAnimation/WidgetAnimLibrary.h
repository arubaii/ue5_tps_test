// WidgetAnimLibrary.h
//
// Lightweight, code-driven UMG widget animation system (no Widget Animations
// / Sequencer required). Designed so a widget can hold an array of
// FWidgetAnimSettings (edited per-element in the Details panel) and a
// matching array of FWidgetAnimState (runtime-only), then drive them from
// Tick via UWidgetAnimLibrary::PlayWidgetAnimations(). Multiple entries can
// run at once and their effects combine (opacity multiplies, scale
// multiplies, translation sums, angle sums) instead of overwriting each
// other, so e.g. Fade + Scale + Slide can all play together.
//
// TYPICAL USAGE (on a UUserWidget subclass):
//
//   UPROPERTY(EditAnywhere, Category = "Animation")
//   TArray<FWidgetAnimSettings> Animations;
//
//   UPROPERTY()
//   TArray<FWidgetAnimState> AnimStates;
//
//   // In NativeTick / your Tick event:
//   UWidgetAnimLibrary::PlayWidgetAnimations(this, Animations, AnimStates, InDeltaTime);
//
//   // To replay from the start (e.g. on Construct / on show):
//   UWidgetAnimLibrary::ResetWidgetAnimations(Animations, AnimStates);

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/Widget.h"
#include "Curves/CurveFloat.h"
#include "WidgetAnimLibrary.generated.h"

/** Which effect a given animation entry drives. */
UENUM(BlueprintType)
enum class EWidgetAnimType : uint8
{
	Fade,
	Scale,
	Slide,
	Rotate,
	Pulse
};

/** Easing shape applied over an entry's Duration. */
UENUM(BlueprintType)
enum class EWidgetAnimEasing : uint8
{
	Linear,
	EaseIn,
	EaseOut,
	EaseInOut,
	/** Uses CustomCurve (evaluated 0-1) instead of a built-in easing shape. */
	Custom
};

/** Direction a Slide entry enters from. */
UENUM(BlueprintType)
enum class EWidgetSlideDirection : uint8
{
	Up,
	Down,
	Left,
	Right
};

/**
 * One animation entry. Add several of these to an array on your widget to
 * layer effects (e.g. one Fade entry + one Scale entry + one Slide entry
 * all playing together).
 */
USTRUCT(BlueprintType)
struct FWidgetAnimSettings
{
	GENERATED_BODY()

	/** Which effect this entry drives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	EWidgetAnimType AnimType = EWidgetAnimType::Fade;

	/** How long one play-through takes, in seconds. For Pulse, this is the length of one pulse cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.01"))
	float Duration = 0.3f;

	/** Seconds to wait before this entry starts contributing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.0"))
	float Delay = 0.f;

	/** Easing shape used for the timing of this entry (ignored where an Overshoot value is set). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;

	/** Used when Easing is Custom. Evaluated across Time 0-1, expected to output roughly 0-1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "Easing == EWidgetAnimEasing::Custom"))
	TObjectPtr<UCurveFloat> CustomCurve = nullptr;

	/** If true, this entry repeats instead of stopping after one play-through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bLoop = false;

	/** If true, alternate loops play in reverse (there-and-back) instead of always restarting from Start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "bLoop"))
	bool bPingPong = true;

	/** Number of loops to play before stopping. 0 = loop forever. Ignored if bLoop is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "bLoop", ClampMin = "0"))
	int32 LoopCount = 0;

	// ------------------------------------------------------------------
	// Fade
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (EditCondition = "AnimType == EWidgetAnimType::Fade"))
	float StartOpacity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (EditCondition = "AnimType == EWidgetAnimType::Fade"))
	float EndOpacity = 1.f;

	// ------------------------------------------------------------------
	// Scale (pop-in)
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EWidgetAnimType::Scale"))
	float StartScale = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EWidgetAnimType::Scale"))
	float EndScale = 1.f;

	/**
	 * 0 = no overshoot (uses Easing above). Above 0 = the scale punches past
	 * EndScale before settling back to it (classic "pop"); larger values
	 * overshoot further. Typical range 0.2 - 1.0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EWidgetAnimType::Scale", ClampMin = "0.0"))
	float Overshoot = 0.4f;

	// ------------------------------------------------------------------
	// Slide
	// ------------------------------------------------------------------

	/** Direction the widget slides in FROM (e.g. Down = starts below rest position and slides up into place). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide"))
	EWidgetSlideDirection SlideFromDirection = EWidgetSlideDirection::Down;

	/** How far offscreen/away the widget starts, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", ClampMin = "0.0"))
	float SlideDistance = 100.f;

	/** Same idea as Scale's Overshoot, applied to the slide-in motion (0 = off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", ClampMin = "0.0"))
	float SlideOvershoot = 0.f;

	// ------------------------------------------------------------------
	// Rotate (spin-in)
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotate", meta = (EditCondition = "AnimType == EWidgetAnimType::Rotate"))
	float StartAngle = -180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotate", meta = (EditCondition = "AnimType == EWidgetAnimType::Rotate"))
	float EndAngle = 0.f;

	// ------------------------------------------------------------------
	// Pulse (continuous breathing loop while active)
	// ------------------------------------------------------------------

	/** How much bigger/smaller the widget gets at the peak of each pulse, e.g. 0.05 = +/-5%. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pulse", meta = (EditCondition = "AnimType == EWidgetAnimType::Pulse", ClampMin = "0.0"))
	float PulseScaleAmount = 0.05f;
};

/**
 * Runtime-only playback state for one FWidgetAnimSettings entry. Keep an
 * array of these index-parallel with your Animations array; don't edit it
 * by hand, it's advanced by UWidgetAnimLibrary each tick.
 */
USTRUCT(BlueprintType)
struct FWidgetAnimState
{
	GENERATED_BODY()

	/** Seconds still left before this entry's Delay elapses and it starts contributing. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float DelayRemaining = 0.f;

	/** Time elapsed within the current play-through, wrapped to [0, Duration] each time it loops. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float CycleTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bFinished = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	int32 LoopsCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bPlayingForward = true;
};

/** Combined output of every active animation entry for one tick. */
USTRUCT(BlueprintType)
struct FWidgetAnimResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float Opacity = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FVector2D Scale = FVector2D(1.f, 1.f);

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FVector2D Translation = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float Angle = 0.f;

	/** True if every non-looping entry has finished playing (looping entries are ignored for this check). */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bAllFinished = false;
};

UCLASS()
class TPS_TEST_API UWidgetAnimLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Advances every entry's state by DeltaTime and returns the combined
	 * result. Does NOT touch the widget itself - call ApplyWidgetAnimResult
	 * (or use PlayWidgetAnimations to do both in one call).
	 *
	 * States is auto-resized to match Animations if their lengths differ,
	 * so you don't need to manage its size manually.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static FWidgetAnimResult TickWidgetAnimations(const TArray<FWidgetAnimSettings>& Animations, UPARAM(ref) TArray<FWidgetAnimState>& States, float DeltaTime);

	/** Applies a combined result to a widget's RenderOpacity and RenderTransform. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void ApplyWidgetAnimResult(UWidget* Widget, const FWidgetAnimResult& Result);

	/** Convenience: ticks every entry and applies the combined result to Widget in one call. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void PlayWidgetAnimations(UWidget* Widget, const TArray<FWidgetAnimSettings>& Animations, UPARAM(ref) TArray<FWidgetAnimState>& States, float DeltaTime);

	/**
	 * Snaps Widget to the correct pre-animation pose (every entry's Start
	 * value, ignoring Delay) and resets playback state. Call this once from
	 * Construct so the widget never flashes at its designer-default
	 * opacity/scale/etc. before Tick starts driving PlayWidgetAnimations.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void InitializeWidgetAnimations(UWidget* Widget, const TArray<FWidgetAnimSettings>& Animations, UPARAM(ref) TArray<FWidgetAnimState>& States);

	/** Resets every entry's runtime state back to the beginning (e.g. call this before showing the widget again). */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void ResetWidgetAnimations(const TArray<FWidgetAnimSettings>& Animations, UPARAM(ref) TArray<FWidgetAnimState>& States);

	/** True once every non-looping entry has finished. Looping entries are ignored by this check. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation")
	static bool AreWidgetAnimationsFinished(const TArray<FWidgetAnimSettings>& Animations, const TArray<FWidgetAnimState>& States);

private:

	static float EvaluateEasedAlpha(const FWidgetAnimSettings& Settings, float LinearAlpha);
	static float EaseOutBack(float Alpha, float Overshoot);
	static FVector2D GetSlideDirectionOffset(EWidgetSlideDirection Direction);

	/** Applies one entry's contribution at a given 0-1 phase into OutResult. Shared by tick and initialize paths. */
	static void ApplyAnimationContribution(const FWidgetAnimSettings& Settings, float DirectedAlpha, FWidgetAnimResult& OutResult);
	static void EvaluateSingleAnimation(const FWidgetAnimSettings& Settings, FWidgetAnimState& State, float DeltaTime, FWidgetAnimResult& OutResult);
};