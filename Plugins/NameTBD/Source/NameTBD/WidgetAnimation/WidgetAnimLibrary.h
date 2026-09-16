// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/Widget.h"
#include "Components/Image.h"
#include "Curves/CurveFloat.h"
#include "WidgetAnimLibrary.generated.h"

class UMaterialInstanceDynamic;
class UUserWidget;

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
struct NAMETBD_API FWidgetAnimSettings
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

	/**
	 * Multiplies how fast this layer runs. 2 is twice as fast, 0.5 is half speed.
	 *
	 * Applied to the clock rather than baked into Duration, so Delay is stretched by exactly the same
	 * factor and a stack authored to stagger by 0.1s still staggers proportionally when it is slowed
	 * down. The authored Duration and Delay stay readable in the Details panel either way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Playback Speed", ClampMin = "0.01", UIMin = "0.1", UIMax = "4.0"))
	float PlaybackSpeed = 1.f;

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

/**
 * One layer of an animation, with none of the per-use flags.
 *
 * This is what a Widget Animation asset is made of. Everything here describes the MOTION - how long, how
 * far, what curve - and is the same wherever the animation is used. Everything that is a decision about
 * one particular use of it (does this one loop, is it mirrored, which widget does it move) lives on the
 * FWidgetAnimEntry that references the asset instead.
 *
 * Layers combine rather than overwrite: put Fade and Scale in one animation and both happen, with Delay
 * staggering them. That is the whole point of authoring an animation as layers instead of as a single
 * type - "pop" is a fade plus a scale plus a small slide, not one of the three.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EWidgetAnimType AnimType = EWidgetAnimType::Fade;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0.01"))
	float Duration = 0.3f;

	/** Seconds before this layer starts. The only thing that staggers layers within one animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0.0"))
	float Delay = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (EditCondition = "Easing == EWidgetAnimEasing::Custom", EditConditionHides))
	TObjectPtr<UCurveFloat> CustomCurve = nullptr;

	// Fade
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (EditCondition = "AnimType == EWidgetAnimType::Fade", EditConditionHides))
	float StartOpacity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (EditCondition = "AnimType == EWidgetAnimType::Fade", EditConditionHides))
	float EndOpacity = 1.f;

	// Scale
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EWidgetAnimType::Scale", EditConditionHides))
	float StartScale = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EWidgetAnimType::Scale", EditConditionHides))
	float EndScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EWidgetAnimType::Scale", EditConditionHides, ClampMin = "0.0"))
	float Overshoot = 0.4f;

	// Slide
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", EditConditionHides))
	EWidgetSlideDirection SlideFromDirection = EWidgetSlideDirection::Down;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", EditConditionHides, ClampMin = "0.0"))
	float SlideDistance = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", EditConditionHides, ClampMin = "0.0"))
	float SlideOvershoot = 0.f;

	// Rotate
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotate", meta = (EditCondition = "AnimType == EWidgetAnimType::Rotate", EditConditionHides))
	float StartAngle = -180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotate", meta = (EditCondition = "AnimType == EWidgetAnimType::Rotate", EditConditionHides))
	float EndAngle = 0.f;

	// Pulse
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pulse", meta = (EditCondition = "AnimType == EWidgetAnimType::Pulse", EditConditionHides, ClampMin = "0.0"))
	float PulseScaleAmount = 0.05f;

	// Material
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

	/** This layer as the runtime settings struct. Every per-use flag is left at its default for the caller to fill in. */
	FWidgetAnimSettings ToSettings() const;

	/** The reverse, for migrating data authored before animations were split into layers and entries. */
	static FWidgetAnimLayer FromSettings(const FWidgetAnimSettings& Settings);
};

USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimState
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
struct NAMETBD_API FWidgetAnimTargetedTransform
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
struct NAMETBD_API FWidgetAnimScalarTarget
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	UImage* TargetImage = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float Value = 0.f;
};

USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimVectorTarget
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	UImage* TargetImage = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FLinearColor Value = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimResult
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
struct NAMETBD_API FWidgetAnimPlaybackState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	EWidgetAnimPhase Phase = EWidgetAnimPhase::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimState> InStates;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimState> OutStates;
};

// =====================================================================================================
// Track player
// =====================================================================================================

/**
 * One animation running right now: the flattened settings it was built from, the clock for each of them,
 * and which direction it is going.
 *
 * Tracks exist so several animations can run at once without fighting over the same widget. Every track
 * is evaluated into ONE combined result per frame and that result is applied once, so two animations that
 * both scale the same icon multiply together the way two layers inside one animation already do. Two
 * independent players writing the same widget would instead just take turns overwriting each other.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimTrack
{
	GENERATED_BODY()

	/** The Anim ID from the entry that started this, or None. Stopping and querying match on this. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FName AnimId;

	/** Which entry in the owner's Anim In / Anim Out list started it. Keeps unnamed tracks distinguishable. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	int32 SourceIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bIsOut = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bFinished = false;

	/**
	 * Whether a finished track keeps contributing its final pose.
	 *
	 * On: the widget stays where the animation put it, which is what an entrance has to do or everything
	 * snaps back the instant it lands. Off: the track is dropped once it finishes and the widget returns
	 * to whatever the remaining tracks and its baseline say.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bHoldWhenFinished = true;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float Speed = 1.f;

	UPROPERTY()
	TArray<FWidgetAnimSettings> Settings;

	UPROPERTY()
	TArray<FWidgetAnimState> States;
};

USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimPlayer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimTrack> Tracks;

	/** Whole-widget phase, driven by Play In / Play Out. Play-by-ID does not touch it. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	EWidgetAnimPhase Phase = EWidgetAnimPhase::Idle;
};

// =====================================================================================================
// Baselines
// =====================================================================================================

/**
 * What one animated widget looked like before any animation touched it.
 *
 * An animation result is absolute: opacity is a multiplier that starts from 1, translation an offset that
 * starts from zero. Written straight onto a widget (ApplyWidgetAnimResult) that wipes whatever the widget
 * was authored as - fine for a card that fades itself in from nothing, wrong for an icon inside a button
 * that was already authored at 0.6 opacity and nudged four pixels left.
 *
 * Capture a baseline first and the result is composed over it instead: opacity and scale multiply,
 * translation and angle add. Restoring the set puts every widget back exactly as it was, which is what
 * turning a designer preview back off has to do.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimBaseline
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UWidget> Widget = nullptr;

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
struct NAMETBD_API FWidgetAnimBaselineSet
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimBaseline> Baselines;

	/** Set by the first capture, so a later pass can't re-capture a pose the animation itself just wrote. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bCaptured = false;

	void ResetBaselines()
	{
		Baselines.Reset();
		bCaptured = false;
	}
};

// =====================================================================================================
// Preview
// =====================================================================================================

/**
 * How a preview harness drives a set of animations. Built for the UMG designer, where nothing calls
 * PlayIn / PlayOut for you, but perfectly valid at runtime too - Loop In and Out on a "press me" prompt
 * is a shipping effect, not only an authoring aid.
 */
UENUM(BlueprintType)
enum class EWidgetAnimPreviewMode : uint8
{
	Off UMETA(ToolTip = "No preview. Widgets are left exactly as authored."),

	StartPose UMETA(DisplayName = "Hold Start Pose", ToolTip = "Freeze on the first frame of Anim In - what the widget looks like the instant before it animates on."),

	EndPose UMETA(DisplayName = "Hold End Pose", ToolTip = "Freeze on the last frame of Anim In - what the widget settles to."),

	Scrub UMETA(ToolTip = "Freeze at Scrub Alpha, so the slider walks the animation frame by frame. Scrub Out Animation picks which stack is scrubbed."),

	PlayIn UMETA(DisplayName = "Play In Once", ToolTip = "Play Anim In once and hold the end pose. Restart replays it."),

	PlayOut UMETA(DisplayName = "Play Out Once", ToolTip = "Snap to the Anim In end pose, then play Anim Out once and hold. Restart replays it."),

	LoopIn UMETA(DisplayName = "Loop In", ToolTip = "Anim In, hold for Hold Shown, snap back, repeat."),

	LoopInOut UMETA(DisplayName = "Loop In and Out", ToolTip = "Anim In, hold for Hold Shown, Anim Out, hold for Hold Hidden, repeat. The usual choice for judging a whole entrance/exit pair.")
};

USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimPreviewSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview")
	EWidgetAnimPreviewMode Mode = EWidgetAnimPreviewMode::Off;

	/** Multiplies playback speed. 0.25 is quarter speed, which is how you actually see what a 0.2s pop is doing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (ClampMin = "0.01", UIMin = "0.1", UIMax = "4.0", EditCondition = "Mode == EWidgetAnimPreviewMode::PlayIn || Mode == EWidgetAnimPreviewMode::PlayOut || Mode == EWidgetAnimPreviewMode::LoopIn || Mode == EWidgetAnimPreviewMode::LoopInOut", EditConditionHides))
	float PlaybackSpeed = 1.f;

	/** Seconds held on the shown pose before the loop moves on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (ClampMin = "0.0", EditCondition = "Mode == EWidgetAnimPreviewMode::LoopIn || Mode == EWidgetAnimPreviewMode::LoopInOut", EditConditionHides))
	float HoldShown = 0.75f;

	/** Seconds held on the hidden pose before the loop starts again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (ClampMin = "0.0", EditCondition = "Mode == EWidgetAnimPreviewMode::LoopIn || Mode == EWidgetAnimPreviewMode::LoopInOut", EditConditionHides))
	float HoldHidden = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "Mode == EWidgetAnimPreviewMode::Scrub", EditConditionHides))
	float ScrubAlpha = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (DisplayName = "Scrub Out Animation", EditCondition = "Mode == EWidgetAnimPreviewMode::Scrub", EditConditionHides))
	bool bScrubOutAnimation = false;
};

USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimPreviewState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Preview")
	FWidgetAnimPlaybackState Playback;

	UPROPERTY(BlueprintReadOnly, Category = "Preview")
	float HoldRemaining = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Preview")
	bool bStarted = false;

	/** Changing mode restarts the harness rather than continuing from wherever the previous mode left off. */
	UPROPERTY(BlueprintReadOnly, Category = "Preview")
	EWidgetAnimPreviewMode LastMode = EWidgetAnimPreviewMode::Off;

	/**
	 * The alpha and direction Scrub last actually posed at.
	 *
	 * Scrub keeps its timer registered now, so it is asked for a frame continuously rather than once;
	 * this is what stops it re-posing a slider that has not moved. Negative means nothing has been posed
	 * yet, which no clamped alpha can be.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Preview")
	float LastScrubAlpha = -1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Preview")
	bool bLastScrubWasOut = false;

	/**
	 * Whether something is already driving frames into this state.
	 *
	 * Lives here rather than on each host so the shared harness can answer "is my timer running" without
	 * every host needing its own bool. Deliberately NOT cleared by RestartWidgetAnimPreview: restarting
	 * the animation is not the same as the timer going away, and clearing it there would let a second
	 * timer register on top of the first.
	 */
	UPROPERTY(Transient)
	bool bTimerActive = false;
};

UCLASS()
class NAMETBD_API UWidgetAnimLibrary : public UBlueprintFunctionLibrary
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

	// -------------------------------------------------------------------------------------------------
	// Baseline-aware variants
	//
	// The same calls, composed over a captured pose instead of replacing it. Anything that animates a
	// widget it does not own outright - a button animating its own icon, a preview box animating a border
	// somebody authored at half opacity - wants these.
	// -------------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Baselines")
	static void PlayWidgetAnimationsOver(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, UPARAM(ref) TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation, const FWidgetAnimBaselineSet& Baselines);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Baselines")
	static void InitializeWidgetAnimationsOver(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, UPARAM(ref) TArray<FWidgetAnimState>& States, const FWidgetAnimBaselineSet& Baselines);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Baselines")
	static bool TickWidgetAnimPlaybackOver(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, UPARAM(ref) FWidgetAnimPlaybackState& PlaybackState, float DeltaTime, const FWidgetAnimBaselineSet& Baselines);

	/**
	 * Records the current opacity and render transform of every widget the two stacks touch (each entry's
	 * Target Widget, plus FallbackWidget for entries that have none).
	 *
	 * Does nothing once the set has already been captured: a second pass would be reading a pose the
	 * animation itself wrote, and the baseline would drift a little further every time.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Baselines")
	static void CaptureWidgetAnimBaselines(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, UPARAM(ref) FWidgetAnimBaselineSet& Baselines);

	/** Puts every widget in the set back exactly as it was when captured. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Baselines")
	static void RestoreWidgetAnimBaselines(const FWidgetAnimBaselineSet& Baselines);

	/** ApplyWidgetAnimResult composed over the captured pose: opacity and scale multiply, translation and angle add. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Baselines")
	static void ApplyWidgetAnimResultOver(UWidget* FallbackWidget, const FWidgetAnimResult& Result, const FWidgetAnimBaselineSet& Baselines);

	/** Opacity 1, scale 1, no translation, no angle. The fallback for restoring a widget nothing captured. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Baselines")
	static void ResetWidgetRenderState(UWidget* Widget);

	// -------------------------------------------------------------------------------------------------
	// Posing and preview
	// -------------------------------------------------------------------------------------------------

	/**
	 * Freezes a stack at one point in its timeline and applies it, ignoring Delay, looping and ping-pong.
	 * Drives Hold Start Pose / Hold End Pose / Scrub, and it is the only way to see a single frame of an
	 * animation without running a clock.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Preview")
	static void ApplyWidgetAnimationsAtAlpha(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, float Alpha, bool bIsOutAnimation, const FWidgetAnimBaselineSet& Baselines);

	/**
	 * One frame of a preview harness: playing, holding, restarting, and (for Off) restoring the captured
	 * baseline. Returns true while it still needs frames, which is what an active timer wants to know.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Preview")
	static bool TickWidgetAnimPreview(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, UPARAM(ref) FWidgetAnimPreviewState& State, const FWidgetAnimPreviewSettings& Settings, const FWidgetAnimBaselineSet& Baselines, float DeltaTime);

	/** Forces the next TickWidgetAnimPreview to start the harness from the beginning. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Preview")
	static void RestartWidgetAnimPreview(UPARAM(ref) FWidgetAnimPreviewState& State);

	// -------------------------------------------------------------------------------------------------
	// Authoring helpers
	// -------------------------------------------------------------------------------------------------

	/**
	 * Divides Duration and Delay by Speed, then adds ExtraDelay, so a preset can be slowed down or
	 * staggered where it is referenced without editing the preset asset itself.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Widget Animation|Authoring")
	static FWidgetAnimSettings RetimeWidgetAnimation(const FWidgetAnimSettings& Settings, float Speed = 1.f, float ExtraDelay = 0.f);

	/**
	 * Appends Source to Destination, copying every entry that has Mirror In Out set into MirrorDestination
	 * as well. The one place the mirroring rule lives, so presets, cards, panels and buttons all mirror
	 * identically.
	 */
	static void AppendWithMirroring(const TArray<FWidgetAnimSettings>& Source, TArray<FWidgetAnimSettings>& Destination, TArray<FWidgetAnimSettings>& MirrorDestination);

	// -------------------------------------------------------------------------------------------------
	// Track player
	// -------------------------------------------------------------------------------------------------

	/**
	 * Starts (or restarts) one animation on the player.
	 *
	 * A track already running with the same Anim ID, source index and direction is restarted when
	 * bRestartIfPlaying is set and left alone otherwise, which is what stops a button mashed ten times a
	 * second from stacking ten copies of its own press animation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Player")
	static void StartWidgetAnimTrack(UPARAM(ref) FWidgetAnimPlayer& Player, FName AnimId, int32 SourceIndex, const TArray<FWidgetAnimSettings>& Settings, bool bIsOut, float Speed = 1.f, bool bRestartIfPlaying = true, bool bHoldWhenFinished = true);

	/** Stops tracks matching any of the given IDs, or every track when the array is empty. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Player")
	static void StopWidgetAnimTracks(UPARAM(ref) FWidgetAnimPlayer& Player, const TArray<FName>& AnimIds, bool bRemoveTracks = true);

	/** True if any track matching the IDs is still running. An empty array asks about every track. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Player")
	static bool AreWidgetAnimTracksPlaying(const FWidgetAnimPlayer& Player, const TArray<FName>& AnimIds);

	/**
	 * Advances every track, combines them into one result and applies it once.
	 *
	 * Returns true while anything is still running. IDs that reached their end on this exact frame come
	 * back in OutJustFinished, which is how a caller knows an exit animation is done.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Player")
	static bool TickWidgetAnimPlayer(UWidget* FallbackWidget, UPARAM(ref) FWidgetAnimPlayer& Player, const FWidgetAnimBaselineSet& Baselines, float DeltaTime, TArray<FName>& OutJustFinished);

	// -------------------------------------------------------------------------------------------------
	// Widget targeting
	// -------------------------------------------------------------------------------------------------

	/**
	 * Every widget at or beneath Root, as slash-separated paths.
	 *
	 * Backs the Target Widget dropdowns. Paths rather than plain names because widget names are only
	 * unique within one WidgetTree, so anything inside a nested User Widget is "Nested/Inner".
	 */
	static void GatherTargetableWidgetPaths(UWidget* Root, TArray<FString>& OutPaths);

	/** The widget Path names, searched from Root. Null if there isn't one. */
	static UWidget* FindWidgetByPath(UWidget* Root, FName Path);

	/** The same search rooted at a User Widget's tree rather than at a single widget. */
	static void GatherTargetableWidgetPathsInTree(UUserWidget* Owner, TArray<FString>& OutPaths);
	static UWidget* FindWidgetByPathInTree(UUserWidget* Owner, FName Path);

private:

	static float EvaluateEasedAlpha(const FWidgetAnimSettings& Settings, float LinearAlpha);
	static float EaseOutBack(float Alpha, float Overshoot);
	static FVector2D GetSlideDirectionOffset(EWidgetSlideDirection Direction);

	static FWidgetAnimTargetedTransform& FindOrAddTargetedTransform(FWidgetAnimResult& Result, UWidget* TargetWidget);

	static void ApplyAnimationContribution(const FWidgetAnimSettings& Settings, float DirectedAlpha, FWidgetAnimResult& OutResult);
	static void EvaluateSingleAnimation(const FWidgetAnimSettings& Settings, FWidgetAnimState& State, float DeltaTime, FWidgetAnimResult& OutResult, bool bIsOutAnimation);

	static bool GetEffectiveInvert(const FWidgetAnimSettings& Settings, bool bIsOutAnimation);

	/** The captured pose for one widget, or an identity baseline when the set doesn't hold it. */
	static FWidgetAnimBaseline FindBaselineFor(const FWidgetAnimBaselineSet& Baselines, UWidget* Widget);

	/** Adds Widget to the set if it isn't in it already, recording its current pose. */
	static void CaptureOne(FWidgetAnimBaselineSet& Baselines, UWidget* Widget);

	// Baseline-aware cores. The original entry points forward to these with a null set, which is the
	// absolute (pre-baseline) behaviour every existing caller already relies on.
	/** TickWidgetAnimations, accumulating into a caller-owned result so several tracks can share one. */
	static bool TickAnimationsInto(const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation, FWidgetAnimResult& OutResult);

	static void ApplyResultInternal(UWidget* FallbackWidget, const FWidgetAnimResult& Result, const FWidgetAnimBaselineSet* Baselines);
	static void PlayInternal(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation, const FWidgetAnimBaselineSet* Baselines);
	static void InitializeInternal(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, const FWidgetAnimBaselineSet* Baselines);
	static bool TickPlaybackInternal(UWidget* FallbackWidget, const TArray<FWidgetAnimSettings>& InAnimations, const TArray<FWidgetAnimSettings>& OutAnimations, FWidgetAnimPlaybackState& PlaybackState, float DeltaTime, const FWidgetAnimBaselineSet* Baselines);
};

#if WITH_EDITOR
/**
 * Lets a Details panel edit reach the widget you are actually looking at, while the mouse is still down.
 *
 * The UMG Details panel edits the TEMPLATE object stored in the Widget Blueprint; the thing painted in
 * the viewport is a separate preview instance, and the two are reconciled by a preview rebuild that only
 * happens when an edit is committed. For a checkbox that is invisible. For a slider it means nothing
 * moves until you let go, which makes scrubbing an animation useless - the one control where you need to
 * see every intermediate value.
 *
 * So design-time widgets register themselves here, and a template handling an interactive
 * PostEditChangeProperty pushes straight across to its live counterparts. Matched on class and name,
 * which is unambiguous: names are unique within a WidgetTree.
 */
namespace WidgetAnimDesignerSync
{
	/** Call from SynchronizeProperties when IsDesignTime(). Registering twice is harmless. */
	NAMETBD_API void RegisterPreview(UObject* PreviewWidget);

	/** Call from ReleaseSlateResources / BeginDestroy. */
	NAMETBD_API void UnregisterPreview(UObject* PreviewWidget);

	/** Runs Update on every live preview that corresponds to Template. */
	NAMETBD_API void ForEachPreviewOf(const UObject* Template, TFunctionRef<void(UObject*)> Update);

	/** True for the mid-drag changes the preview rebuild is going to miss. */
	NAMETBD_API bool IsInteractiveChange(const struct FPropertyChangedEvent& PropertyChangedEvent);
}
#endif