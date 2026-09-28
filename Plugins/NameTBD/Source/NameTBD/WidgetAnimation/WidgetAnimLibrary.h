// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/Widget.h"
#include "Components/Image.h"
#include "Curves/CurveFloat.h"
#include "WidgetAnimEasing.h"    // EWidgetAnimEasing
#include "WidgetAnimTransform.h" // FWidgetTransformAnim
#include "WidgetAnimSpring.h"    // EWidgetAnimTiming, FWidgetSpringParams
#include "WidgetAnimDriver.h"    // Value-driven tracks: FWidgetAnimValueOptions, FWidgetAnimValueState
#include "WidgetAnimEvaluator.h" // FWidgetAnimTypeParams, registered track types
#include "StructUtils/InstancedStruct.h"
#include "WidgetAnimLibrary.generated.h"

class UMaterialInstanceDynamic;
class UUserWidget;
class UShapeBox;

UENUM(BlueprintType)
enum class EWidgetAnimType : uint8
{
	Fade,
	Scale,
	Slide,
	Rotate,
	Pulse,
	MaterialScalar,
	MaterialVector,

	/**
	 * Keyframed Render Transform (and optionally a Shape Box outline), driven by a curve.
	 *
	 * The curve's keys are the keyframes and its shape between them is the interpolation - linear by
	 * default - so Duration and Easing are hidden. Each key holds one Transform. The general form of Slide /
	 * Scale / Rotate: those each answer one narrow question, this answers "put this widget through these
	 * poses, at these times, moving like this". See FWidgetTransformAnim.
	 *
	 * Appended last on purpose. Enum properties serialize by name, but anything that stored the raw byte
	 * keeps meaning exactly what it meant before.
	 */
	Transform,

	/**
	 * A registered track type: one defined by a settings struct and an evaluator rather than by this enum. Which
	 * type it is lives in the track's Type Params - the struct is the type - so every registered type, first-party
	 * or from another module, shares this one value. See FWidgetAnimEvaluator.
	 *
	 * Appended last for the same reason as Transform. Timed, keyed and looped like any other track that travels
	 * from a start value to an end value.
	 */
	Custom UMETA(DisplayName = "Registered Type")
};

// EWidgetAnimEasing now lives in WidgetAnimEasing.h, included above, so anything that includes this
// header still gets it and needs no include changed.

/**
 * Whether tracks of this type can be Spring timed: every type that travels somewhere - from a start value to an end,
 * or along a Transform's keys, where the spring is fitted to the keys' length. Not Pulse, which has no start and end to
 * travel between: a Spring Timing left on one is ignored and it plays as a Tween. See WidgetAnimSpring.h.
 */
inline bool WidgetAnimTypeSupportsSpring(EWidgetAnimType AnimType)
{
	return AnimType != EWidgetAnimType::Pulse;
}

/** Whether a track of this type offers Timing: every type does, though not every Timing - see WidgetAnimTimingAllowed. */
inline bool WidgetAnimTimingAllowed(EWidgetAnimType AnimType, EWidgetAnimTiming Timing)
{
	return Timing != EWidgetAnimTiming::Spring || WidgetAnimTypeSupportsSpring(AnimType);
}

/**
 * Whether a value-driven track of this type can take this kind of value - the Value Types that mean something on it.
 * Float and Integer move every type. Vector 2D moves a Scale's X and Y separately, or the X and Y of whatever a
 * Transform's Value Drives picks - the whole path, or a whole Translation, Scale or Shear; not one axis or the Angle.
 * Color moves a Material Vector's channels separately. See WidgetAnimDriver.h.
 */
inline bool WidgetAnimValueKindAllowed(EWidgetAnimType AnimType, EWidgetAnimTransformValueTarget TransformTarget, EWidgetAnimValueKind Kind)
{
	switch (Kind)
	{
	case EWidgetAnimValueKind::Vector2D:
		return AnimType == EWidgetAnimType::Scale
			|| (AnimType == EWidgetAnimType::Transform && WidgetAnimValue::TransformTargetTakesTwoAxes(TransformTarget));
	case EWidgetAnimValueKind::Color:
		return AnimType == EWidgetAnimType::MaterialVector;
	default:
		return true;
	}
}

/**
 * Whether tracks of this type take keys between their start and their end: the same types that travel from a
 * start value to an end value. Transform has keys of its own (see FWidgetTransformAnim), and Pulse has no values
 * to key. See FWidgetAnimValueKeys.
 */
inline bool WidgetAnimTypeSupportsValueKeys(EWidgetAnimType AnimType)
{
	return AnimType != EWidgetAnimType::Transform && AnimType != EWidgetAnimType::Pulse;
}

/**
 * One key between a track's start and its end: a value the track passes through on the way.
 *
 * The track's own start and end values are its first and last keys, so a Fade from 0 to 1 with one key of 0.3 at
 * 0.5 rises to 0.3 by the middle and on to 1 at the end - or, keyed 2 in the middle of a Scale from 1 to 1, zooms
 * in and back out again.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimValueKey
{
	GENERATED_BODY()

	/**
	 * Where along the track the key sits: 0 its start, 1 its end. A fraction rather than seconds, so the keys
	 * keep their places when the track is lengthened or shortened. Set by dragging the key on its track.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Key", meta = (DisplayName = "Position"))
	float Time = 0.5f;

	/**
	 * The value at this key, in the track's own terms: an opacity on a Fade, a scale on a Scale, how far along
	 * the slide on a Slide (its Distance at the start, 0 at the end), an angle on a Rotate, the parameter's value
	 * on a Material Scalar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key")
	float Value = 0.f;

	/** The value at this key on a Material Vector track. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key")
	FLinearColor Color = FLinearColor::White;

	/** How the track travels from this key to the next one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key", meta = (ShowOnlyInnerProperties))
	FWidgetAnimSegmentCurve CurveToNext;
};

/**
 * The keys a track passes through between its start and its end, and how it travels between them.
 *
 * Points are counted the way the Widget Animations panel draws them: point 0 is the track's start value, the last
 * point its end value, and every key in Keys one in between. Segment N runs from point N to point N + 1 - so there
 * is always one more segment than there are keys.
 *
 * Each key is reached exactly when the timeline shows it, and each segment is shaped on its own: by its own curve,
 * or - left at Track Easing, the default - by the track's Easing, across that one segment. No keys at all is
 * exactly the track as it always was. A Spring track springs across the whole run however its segments are set,
 * its keys being points along the way it travels; a Tween Scale or Slide's Overshoot applies only while it has no
 * keys.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimValueKeys
{
	GENERATED_BODY()

	/** How the track travels from its start value to its first key - to its end value, when it has no keys. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys", meta = (ShowOnlyInnerProperties))
	FWidgetAnimSegmentCurve StartCurve;

	/** The keys between the start and the end, in order along the track. Added, deleted and moved on the track. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, EditFixedSize, Category = "Keys")
	TArray<FWidgetAnimValueKey> Keys;

	/** Start, every key, end. */
	int32 NumPoints() const { return Keys.Num() + 2; }

	/** Where point Point sits along the track, 0 to 1. */
	float GetPointTime(int32 Point) const
	{
		if (Point <= 0)
		{
			return 0.f;
		}
		return Point <= Keys.Num() ? FMath::Clamp(Keys[Point - 1].Time, 0.f, 1.f) : 1.f;
	}

	/** The curve from point Segment to the next. */
	const FWidgetAnimSegmentCurve& GetSegmentCurve(int32 Segment) const
	{
		return (Segment <= 0 || Keys.Num() == 0) ? StartCurve : Keys[FMath::Min(Segment, Keys.Num()) - 1].CurveToNext;
	}

	FWidgetAnimSegmentCurve& GetSegmentCurve(int32 Segment)
	{
		return (Segment <= 0 || Keys.Num() == 0) ? StartCurve : Keys[FMath::Min(Segment, Keys.Num()) - 1].CurveToNext;
	}

	/**
	 * Whether any segment has a curve of its own rather than the track's Easing - what the Widget Animations panel
	 * names on the track, segment by segment. Never with no keys: a track that only goes from its start to its end
	 * moves by its Easing, and a curve left behind on its one segment by keys since deleted is not used.
	 */
	bool HasSegmentCurves() const
	{
		if (Keys.Num() == 0)
		{
			return false;
		}
		if (!StartCurve.IsTrackEasing())
		{
			return true;
		}
		for (const FWidgetAnimValueKey& Key : Keys)
		{
			if (!Key.CurveToNext.IsTrackEasing())
			{
				return true;
			}
		}
		return false;
	}
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

	/** Tween: Duration and Easing. Spring: a spring, which sets its own length (on a Transform, fitted to its keys). Value: driven from gameplay. Pulse offers Tween and Value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	EWidgetAnimTiming Timing = EWidgetAnimTiming::Tween;

	/** Hidden on a Transform layer, whose curve's last key sets how long it runs, and on a Spring one, which runs until it settles - but shown on any value-driven layer that tweens to its values, as how long each ease takes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.01", EditCondition = "AnimType != EWidgetAnimType::Transform && Timing != EWidgetAnimTiming::Spring || AnimType == EWidgetAnimType::Pulse || Timing == EWidgetAnimTiming::Value && ValueSmoothing == EWidgetAnimValueSmoothing::Tween", EditConditionHides))
	float Duration = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.0"))
	float Delay = 0.f;

	/** Hidden on a Spring layer, whose spring is its motion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "Timing == EWidgetAnimTiming::Tween || Timing == EWidgetAnimTiming::Value && ValueSmoothing == EWidgetAnimValueSmoothing::Tween || AnimType == EWidgetAnimType::Pulse", EditConditionHides))
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "Easing == EWidgetAnimEasing::Custom && Timing == EWidgetAnimTiming::Tween || Easing == EWidgetAnimEasing::Custom && Timing == EWidgetAnimTiming::Value && ValueSmoothing == EWidgetAnimValueSmoothing::Tween || Easing == EWidgetAnimEasing::Custom && AnimType == EWidgetAnimType::Pulse", EditConditionHides))
	UCurveFloat* CustomCurve = nullptr;

	/** The spring a Spring layer travels by. See FWidgetSpringParams. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "Timing == EWidgetAnimTiming::Spring && AnimType != EWidgetAnimType::Pulse || Timing == EWidgetAnimTiming::Value && ValueSmoothing == EWidgetAnimValueSmoothing::Spring", EditConditionHides, ShowOnlyInnerProperties))
	FWidgetSpringParams Spring;

	/**
	 * A value-driven track's name: gameplay moves it with Set Widget Animation Value by this name. Several tracks - in
	 * one animation or several on the widget - may share a name and follow the same value, each its own way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "Timing == EWidgetAnimTiming::Value", EditConditionHides))
	FName ValueName;

	/**
	 * How a value-driven track travels to each new value: Immediate jumps, Tween eases over Duration by Easing, Spring
	 * chases it with the spring. Part of the motion, so authored here only - a widget can't override it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "Timing == EWidgetAnimTiming::Value", EditConditionHides))
	EWidgetAnimValueSmoothing ValueSmoothing = EWidgetAnimValueSmoothing::Spring;

	/**
	 * On a value-driven Transform track: what its one value moves - the whole path of its keys, only their translation,
	 * scale, shear or angle, or one axis of those. Another Transform track takes another value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "Timing == EWidgetAnimTiming::Value && AnimType == EWidgetAnimType::Transform", EditConditionHides))
	EWidgetAnimTransformValueTarget ValueDrives = EWidgetAnimTransformValueTarget::WholePath;

	/**
	 * A value-driven track's value type, range, start and limits - its defaults, which a Widget Animation Component
	 * can override for its own widget. See FWidgetAnimValueOptions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Value", EditCondition = "Timing == EWidgetAnimTiming::Value", EditConditionHides))
	FWidgetAnimValueOptions ValueOptions;

	/** Keys between the start and end values, and the curves between them. See FWidgetAnimValueKeys. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "AnimType != EWidgetAnimType::Transform && AnimType != EWidgetAnimType::Pulse", EditConditionHides))
	FWidgetAnimValueKeys ValueKeys;

	/**
	 * Repeats the animation. Layers that share a Loop Group loop together as one animation - the stagger
	 * between them repeats every loop - rather than each on its own clock.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bLoop = false;

	/** Plays back to the start between plays instead of snapping there. Always comes to rest on the end pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "bLoop", EditConditionHides))
	bool bPingPong = true;

	/**
	 * How many times the animation plays through; 0 loops forever. With Ping Pong it plays back between
	 * plays, so 2 is forward, back, forward - it always ends on the end pose, for any count.
	 */
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

	/**
	 * Which animation this layer belongs to, for looping. Layers in one stack that share a group share one
	 * clock: they loop, ping-pong and settle as a unit. INDEX_NONE loops the layer on its own.
	 *
	 * Stamped by FWidgetAnimEntryBase::BuildSettings with a process-unique id, so stacks built from
	 * different entries can be concatenated freely without their groups merging. Never saved.
	 */
	UPROPERTY(Transient)
	int32 LoopGroup = INDEX_NONE;

	/**
	 * Played as if it weren't there: its place in the timeline still counts - nothing around it moves - but it
	 * poses nothing. Set only by the Widget Animations panel's previews, for a track hidden there (see
	 * FWidgetAnimLayer::bHiddenInEditor). Never saved.
	 */
	UPROPERTY(Transient)
	bool bMuted = false;

	/**
	 * The Widget Animation this layer came from - what its value-driven tracks' values belong to (see
	 * FWidgetAnimValueId). Null for layers that belong to no animation: a component's additive tracks, layers built in
	 * code. Stamped by FWidgetAnimEntryBase::BuildSettings. Never saved.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UObject> ValueScope = nullptr;

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

	/**
	 * How far past its end a Tween Scale pops before settling back. Replaces the Easing while set, and - like any
	 * pop - reaches the end value early on the way past it. Only while the track has no keys between its start and
	 * end; a Spring Scale gets its overshoot from the spring.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EWidgetAnimType::Scale && Timing == EWidgetAnimTiming::Tween", EditConditionHides, ClampMin = "0.0"))
	float Overshoot = 0.4f;

	// ------------------------------------------------------------------
	// Slide
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", EditConditionHides))
	EWidgetSlideDirection SlideFromDirection = EWidgetSlideDirection::Down;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", EditConditionHides, ClampMin = "0.0"))
	float SlideDistance = 100.f;

	/**
	 * How far past its end a Tween Slide travels before settling back. Replaces the Easing while set. Only while the
	 * track has no keys between its start and end; a Spring Slide gets its overshoot from the spring.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide && Timing == EWidgetAnimTiming::Tween", EditConditionHides, ClampMin = "0.0"))
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

	// ------------------------------------------------------------------
	// Transform
	// ------------------------------------------------------------------

	/**
	 * Which Shape Box a Transform layer with Animate Shape on drives, derived the same way
	 * MaterialTargetImage is: the entry's Target Widget, narrowed. Null on anything that is not a Shape
	 * Box, which is what makes Animate Shape quietly do nothing there rather than fail.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform", meta = (EditCondition = "AnimType == EWidgetAnimType::Transform", EditConditionHides))
	TObjectPtr<UShapeBox> ShapeTargetBox = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform", meta = (EditCondition = "AnimType == EWidgetAnimType::Transform", EditConditionHides, ShowOnlyInnerProperties))
	FWidgetTransformAnim Transform;

	// ------------------------------------------------------------------
	// Registered types
	// ------------------------------------------------------------------

	/** Which registered type a Registered Type layer is, and its settings. See FWidgetAnimLayer::TypeParams. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type", meta = (DisplayName = "Type Params", BaseStruct = "/Script/NameTBD.WidgetAnimTypeParams", ExcludeBaseStruct, EditCondition = "AnimType == EWidgetAnimType::Custom", EditConditionHides))
	FInstancedStruct TypeParams;

	/** The evaluator for a Registered Type layer's type. Null on built-in types, and on a type whose module isn't loaded. */
	const FWidgetAnimEvaluator* FindEvaluator() const;

	/** Whether this layer travels by its spring: Spring timing, on a type that has a start and an end to travel between. */
	bool IsSpringTimed() const { return Timing == EWidgetAnimTiming::Spring && WidgetAnimTypeSupportsSpring(AnimType); }

	/** Whether this layer is moved by a value rather than its clock: Value timing. See WidgetAnimDriver.h. */
	bool IsValueDriven() const { return Timing == EWidgetAnimTiming::Value; }

	/** The value type this layer really takes: its Value Type, or Float where its type doesn't allow that one. */
	EWidgetAnimValueKind GetEffectiveValueKind() const
	{
		return WidgetAnimValueKindAllowed(AnimType, ValueDrives, ValueOptions.Kind) ? ValueOptions.Kind : EWidgetAnimValueKind::Float;
	}

	/** The value track's motion - its smoothing, and the tween or spring that smooths it - for WidgetAnimValue. */
	FWidgetAnimValueMotion GetValueMotion() const
	{
		FWidgetAnimValueMotion Motion;
		Motion.Smoothing = ValueSmoothing;
		Motion.TweenDuration = Duration;
		Motion.TweenEasing = Easing;
		Motion.TweenCurve = CustomCurve;
		Motion.Spring = &Spring;
		FWidgetAnimValueOptions Effective;
		Effective.Kind = GetEffectiveValueKind();
		Motion.NumChannels = Effective.GetNumChannels();
		return Motion;
	}
};

/**
 * One layer of an animation, with none of the per-use flags.
 *
 * This is what a Widget Animation asset is made of. Everything here describes the MOTION - how long, how
 * far, what curve - and is the same wherever the animation is used. Everything that is a decision about
 * one particular use of it (does this one loop, is it mirrored, which widget does it move) lives on the
 * FWidgetAnimEntry that references the asset instead.
 *
 * Layers combine rather than overwrite: put Fade and Scale in one animation and both happen. That is the
 * whole point of authoring an animation as layers instead of as a single type - "pop" is a fade plus a
 * scale plus a small slide, not one of the three.
 *
 * Layers run in list order. Each one starts relative to the layer above it - With Previous (the default)
 * starts it alongside that layer, After Previous starts it once that layer has finished - and Delay adds
 * seconds on top - or, negative, takes them off, overlapping the layer above. A delay on the first layer
 * holds back the whole animation. The first layer counts from the start of the animation, and no layer
 * starts before it.
 */
UENUM(BlueprintType)
enum class EWidgetAnimLayerStart : uint8
{
	WithPrevious  UMETA(DisplayName = "With Previous",  ToolTip = "Starts when the track above starts, plus Delay. Tracks left at this play together - a fade, scale and slide making one pop."),
	AfterPrevious UMETA(DisplayName = "After Previous", ToolTip = "Starts when the track above finishes, plus Delay. A run of these plays one after another, as a sequence.")
};

USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EWidgetAnimType AnimType = EWidgetAnimType::Fade;

	/** Whether this track starts with the track above it or after it. The first track counts from the start of the animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (DisplayName = "Starts"))
	EWidgetAnimLayerStart Start = EWidgetAnimLayerStart::WithPrevious;

	/**
	 * How the track travels from its start values to its end values. Tween is authored - a Duration and an
	 * Easing, the same motion every time. Spring is physical - a spring with weight pulls it there, overshoots
	 * as much as its bounce allows and settles, and the track runs for as long as that takes. Value is driven from
	 * gameplay - Set Widget Animation Value by its Value Name says where between its start and end it is, and Value
	 * Smoothing how it travels there - while the rest of the animation plays on its clock.
	 *
	 * On a Transform track the spring is fitted to its keys: it runs the length of its last key, travels through every
	 * key, and overshoots past the last one as far as its bounce says. A Pulse offers Tween and Value, where its value
	 * is how strong it pulses; it has no end for a spring to travel to.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EWidgetAnimTiming Timing = EWidgetAnimTiming::Tween;

	/**
	 * How long the track runs, in seconds. Hidden on a Transform track, whose last key sets how long it runs,
	 * and on a Spring track, which runs until its spring settles. On a value-driven track that tweens to its values
	 * (Value Smoothing = Tween), how long each ease to a new value takes - shown on a Transform track then too.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0.01", EditCondition = "AnimType != EWidgetAnimType::Transform && Timing != EWidgetAnimTiming::Spring || AnimType == EWidgetAnimType::Pulse || Timing == EWidgetAnimTiming::Value && ValueSmoothing == EWidgetAnimValueSmoothing::Tween", EditConditionHides))
	float Duration = 0.3f;

	/**
	 * Seconds after the track above starts (With Previous) or finishes (After Previous) before this one
	 * starts. On the first track, seconds after the animation starts - which holds back every track below.
	 *
	 * May be negative: the track starts that long BEFORE the track above starts or finishes - an overlap. That
	 * is what dragging a track freely along the timeline in the Widget Animations panel writes. No track ever
	 * starts before the animation itself does; a start that would is held at 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	float Delay = 0.f;

	/**
	 * How the track moves between its start and end values: the curve it follows over its Duration. With keys
	 * between them - or on a Transform track - it is how each segment left at Track Easing moves from its key to
	 * the next. Hidden on a Spring track, whose spring is its motion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (EditCondition = "Timing == EWidgetAnimTiming::Tween || Timing == EWidgetAnimTiming::Value && ValueSmoothing == EWidgetAnimValueSmoothing::Tween || AnimType == EWidgetAnimType::Pulse", EditConditionHides))
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (EditCondition = "Easing == EWidgetAnimEasing::Custom && Timing == EWidgetAnimTiming::Tween || Easing == EWidgetAnimEasing::Custom && Timing == EWidgetAnimTiming::Value && ValueSmoothing == EWidgetAnimValueSmoothing::Tween || Easing == EWidgetAnimEasing::Custom && AnimType == EWidgetAnimType::Pulse", EditConditionHides))
	TObjectPtr<UCurveFloat> CustomCurve = nullptr;

	// Spring
	//
	// One nested payload, as Transform is, so every spring setting follows the track through ToSettings and
	// FromSettings in one line - and a whole spring can be copied between tracks as one value.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (EditCondition = "Timing == EWidgetAnimTiming::Spring && AnimType != EWidgetAnimType::Pulse || Timing == EWidgetAnimTiming::Value && ValueSmoothing == EWidgetAnimValueSmoothing::Spring", EditConditionHides, ShowOnlyInnerProperties))
	FWidgetSpringParams Spring;

	/**
	 * A value-driven track's name: gameplay moves it with Set Widget Animation Value by this name. Several tracks - in
	 * one animation or several on the widget - may share a name and follow the same value, each its own way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (EditCondition = "Timing == EWidgetAnimTiming::Value", EditConditionHides))
	FName ValueName;

	/**
	 * How a value-driven track travels to each new value: Immediate jumps, Tween eases over Duration by Easing, Spring
	 * chases it with the spring. Part of the motion, so authored here only - a widget can't override it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (EditCondition = "Timing == EWidgetAnimTiming::Value", EditConditionHides))
	EWidgetAnimValueSmoothing ValueSmoothing = EWidgetAnimValueSmoothing::Spring;

	/**
	 * On a value-driven Transform track: what its one value moves - the whole path of its keys, only their translation,
	 * scale, shear or angle, or one axis of those. A Vector 2D value needs two axes to move. Another Transform track
	 * takes another value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (EditCondition = "Timing == EWidgetAnimTiming::Value && AnimType == EWidgetAnimType::Transform", EditConditionHides))
	EWidgetAnimTransformValueTarget ValueDrives = EWidgetAnimTransformValueTarget::WholePath;

	/**
	 * A value-driven track's value type, range, start and limits - its defaults, which a Widget Animation Component
	 * can override for its own widget. See FWidgetAnimValueOptions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (DisplayName = "Value", EditCondition = "Timing == EWidgetAnimTiming::Value", EditConditionHides))
	FWidgetAnimValueOptions ValueOptions;

	// Keys
	//
	// The values the track passes through between its start and its end, and the curve of each stretch between
	// two of them. Added, moved and deleted on the track in the Widget Animations panel, and edited there by
	// selecting a key - so the Selection panel never shows them as a list. See FWidgetAnimValueKeys.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys", meta = (EditCondition = "AnimType != EWidgetAnimType::Transform && AnimType != EWidgetAnimType::Pulse", EditConditionHides))
	FWidgetAnimValueKeys ValueKeys;

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

	/**
	 * How far past its end a Tween Scale pops before settling back. Replaces the Easing while set, and - like any
	 * pop - reaches the end value early on the way past it. Only while the track has no keys between its start and
	 * end; a Spring Scale gets its overshoot from the spring.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EWidgetAnimType::Scale && Timing == EWidgetAnimTiming::Tween", EditConditionHides, ClampMin = "0.0"))
	float Overshoot = 0.4f;

	// Slide
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", EditConditionHides))
	EWidgetSlideDirection SlideFromDirection = EWidgetSlideDirection::Down;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide", EditConditionHides, ClampMin = "0.0"))
	float SlideDistance = 100.f;

	/**
	 * How far past its end a Tween Slide travels before settling back. Replaces the Easing while set. Only while the
	 * track has no keys between its start and end; a Spring Slide gets its overshoot from the spring.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EWidgetAnimType::Slide && Timing == EWidgetAnimTiming::Tween", EditConditionHides, ClampMin = "0.0"))
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

	// Transform
	//
	// One nested payload instead of a dozen more flat fields. That is the point of the shape: an unused
	// channel collapses to a single row, and the next family costs one property here rather than twelve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform", meta = (EditCondition = "AnimType == EWidgetAnimType::Transform", EditConditionHides, ShowOnlyInnerProperties))
	FWidgetTransformAnim Transform;

	// Registered types
	//
	// A Registered Type track's type and its settings, in one: the struct held here is the type (Count Up is an
	// FWidgetAnimCountUpParams), and its fields are that type's settings. Only this track's own type's fields are
	// carried, which is how new families stop adding flat fields to every track. Empty on every built-in type, so
	// assets authored before registered types existed load exactly as they were. See FWidgetAnimEvaluator.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type", meta = (DisplayName = "Type Params", BaseStruct = "/Script/NameTBD.WidgetAnimTypeParams", ExcludeBaseStruct, EditCondition = "AnimType == EWidgetAnimType::Custom", EditConditionHides))
	FInstancedStruct TypeParams;

	/** The evaluator for a Registered Type track's type. Null on built-in types, and on a type whose module isn't loaded. */
	const FWidgetAnimEvaluator* FindEvaluator() const;

	/**
	 * This layer as the runtime settings struct. Every per-use flag is left at its default for the caller to
	 * fill in, and Delay is this layer's own - relative, not yet placed on the animation's timeline. Use
	 * UWidgetAnimLibrary::BuildLayerSettings for a whole list, which places each layer after the one above.
	 */
	FWidgetAnimSettings ToSettings() const;

	/**
	 * Seconds this layer takes to play once: its curve's length on a Transform layer, its spring's settle time
	 * on a Spring layer, Duration otherwise.
	 */
	float GetRunLength() const;

	/** Whether this layer travels by its spring: Spring timing, on a type that has a start and an end to travel between. */
	bool IsSpringTimed() const { return Timing == EWidgetAnimTiming::Spring && WidgetAnimTypeSupportsSpring(AnimType); }

	/** Whether this layer is moved by a value rather than its clock: Value timing. See WidgetAnimDriver.h. */
	bool IsValueDriven() const { return Timing == EWidgetAnimTiming::Value; }

	/** The value type this layer really takes: its Value Type, or Float where its type doesn't allow that one. */
	EWidgetAnimValueKind GetEffectiveValueKind() const
	{
		return WidgetAnimValueKindAllowed(AnimType, ValueDrives, ValueOptions.Kind) ? ValueOptions.Kind : EWidgetAnimValueKind::Float;
	}

	/** The value track's motion - its smoothing, and the tween or spring that smooths it - for WidgetAnimValue. */
	FWidgetAnimValueMotion GetValueMotion() const
	{
		FWidgetAnimValueMotion Motion;
		Motion.Smoothing = ValueSmoothing;
		Motion.TweenDuration = Duration;
		Motion.TweenEasing = Easing;
		Motion.TweenCurve = CustomCurve;
		Motion.Spring = &Spring;
		FWidgetAnimValueOptions Effective;
		Effective.Kind = GetEffectiveValueKind();
		Motion.NumChannels = Effective.GetNumChannels();
		return Motion;
	}

	/**
	 * Which Layer Group of its animation this layer belongs to - which widget it moves - by the group's
	 * position. Set from the Widget Animations panel, never in a Details panel. Membership only: where the layer
	 * sits in time is still its place in the animation's one list of layers and its Starts setting, so layers
	 * of different groups sequence against each other freely. Unused wherever layers stand alone, such as a
	 * component's Additive Anim Layers - see TargetWidget.
	 */
	UPROPERTY()
	int32 LayerGroup = 0;

	/**
	 * Where a layer stands alone - a Widget Animation Component's Additive Anim Layers - the widget it moves: a path
	 * into the Widget Blueprint the component was added in, the same kind of path an entry's Target Widgets hold.
	 * None moves the component's own widget. Set from the component's inline track editor, never in a Details panel.
	 * Unused in an animation, where the layer's Layer Group says which widget it moves.
	 */
	UPROPERTY()
	FName TargetWidget;

#if WITH_EDITORONLY_DATA
	/**
	 * Locked in the Widget Animations panel: shown and selectable there, but not moved, resized, re-keyed or
	 * edited until unlocked. Authoring state only - playback never reads it, and it is not in cooked builds.
	 */
	UPROPERTY()
	bool bLockedInEditor = false;

	/**
	 * Which line of its group's tracks this layer is drawn on in the Widget Animations panel's Track Editor,
	 * counted down from the group's first. Several layers can share a line, one after another; INDEX_NONE (a
	 * new layer, older data) is a line of its own. Authoring state only - where a layer plays is its Starts and
	 * Delay, never this.
	 */
	UPROPERTY()
	int32 EditorLine = INDEX_NONE;

	/**
	 * Hidden in the Widget Animations panel: the editor's previews play the animation without this track -
	 * everything else stays exactly where it is - so what the others do can be judged on its own. Authoring
	 * state only: a game plays every track.
	 */
	UPROPERTY()
	bool bHiddenInEditor = false;

	/**
	 * The Widget Animation asset this track was brought in from with Insert Existing Widget Animation, by name -
	 * shown beside it in the Widget Animations panel's list. Empty for a track made here. Authoring state only.
	 */
	UPROPERTY()
	FString InsertedFrom;
#endif

	/** The reverse, for migrating data authored before animations were split into layers and entries. */
	static FWidgetAnimLayer FromSettings(const FWidgetAnimSettings& Settings);
};

/**
 * The values at a keyed track's points - see FWidgetAnimValueKeys - read the same way off a layer or off the
 * settings built from it, which share their field names. Point 0 is the track's start value, the last point its
 * end value; a Slide's are its Distance and 0, how far along the slide it is.
 */
namespace WidgetAnimKeyPoints
{
	template <typename TTrack>
	float GetValue(const TTrack& Track, int32 Point)
	{
		const int32 NumKeys = Track.ValueKeys.Keys.Num();
		if (Point > 0 && Point <= NumKeys)
		{
			return Track.ValueKeys.Keys[Point - 1].Value;
		}

		const bool bStart = Point <= 0;
		switch (Track.AnimType)
		{
		case EWidgetAnimType::Fade:           return bStart ? Track.StartOpacity : Track.EndOpacity;
		case EWidgetAnimType::Scale:          return bStart ? Track.StartScale : Track.EndScale;
		case EWidgetAnimType::Slide:          return bStart ? Track.SlideDistance : 0.f;
		case EWidgetAnimType::Rotate:         return bStart ? Track.StartAngle : Track.EndAngle;
		case EWidgetAnimType::MaterialScalar: return bStart ? Track.StartScalarValue : Track.EndScalarValue;
		// A registered type says where it starts and ends; see FWidgetAnimEvaluator::GetEndpointValue. As a float,
		// like every key: this is what the panel draws and what bounds a track's values, not what plays - playback
		// reads the endpoints at full precision.
		case EWidgetAnimType::Custom:         return static_cast<float>(WidgetAnimGetRegisteredEndpoint(Track.TypeParams, bStart));
		default:                              return 0.f;
		}
	}

	template <typename TTrack>
	FLinearColor GetColor(const TTrack& Track, int32 Point)
	{
		const int32 NumKeys = Track.ValueKeys.Keys.Num();
		if (Point > 0 && Point <= NumKeys)
		{
			return Track.ValueKeys.Keys[Point - 1].Color;
		}
		return Point <= 0 ? Track.StartVectorValue : Track.EndVectorValue;
	}

	/** The lowest value any point of the track has - what decides whether a value can sensibly go below zero. */
	template <typename TTrack>
	float GetMinValue(const TTrack& Track)
	{
		float Min = GetValue(Track, 0);
		for (int32 Point = 1; Point < Track.ValueKeys.NumPoints(); ++Point)
		{
			Min = FMath::Min(Min, GetValue(Track, Point));
		}
		return Min;
	}

	/** The highest value any point of the track has. */
	template <typename TTrack>
	float GetMaxValue(const TTrack& Track)
	{
		float Max = GetValue(Track, 0);
		for (int32 Point = 1; Point < Track.ValueKeys.NumPoints(); ++Point)
		{
			Max = FMath::Max(Max, GetValue(Track, Point));
		}
		return Max;
	}
}

/**
 * One widget an animation moves: a name and where it points. The layers themselves stay in the animation's
 * single list, each naming its group with FWidgetAnimLayer::LayerGroup.
 *
 * Keeping the list single is what lets groups interleave: a layer's Starts - With or After Previous - reads
 * the layer above it in the list, whichever group that one belongs to. A title sliding in, then the card
 * flipping, then the title fading, then the icon popping is one sequence across three groups.
 *
 * The group does not name its widget for good. Each use of the animation - an entry on a Widget Animation
 * Component - picks a Target Widget per group, so the same animation drives the matching parts of any
 * widget it is put on. Preview Target is only the widget it animates while it is being built, and the
 * default a new use starts from.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimLayerGroup
{
	GENERATED_BODY()

	/** Optional. Left empty it reads "Layer Group 1", "Layer Group 2"... by position. Uses of the animation label this group's Target Widget with it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Group", meta = (DisplayName = "Group Name"))
	FString GroupName;

	/**
	 * The widget this group animates while it is being authored, as a path into the Widget Blueprint it is
	 * being built in. None animates that widget itself. A use of the animation that has not picked its own
	 * Target Widget for this group starts from this one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Group", meta = (DisplayName = "Preview Target"))
	FName PreviewTarget;

	/**
	 * Only from the brief format where each group held its own layers. Moved into the animation's single list on
	 * load by UWidgetAnimLibrary::NormalizeLayerGroups and emptied; never edited.
	 */
	UPROPERTY()
	TArray<FWidgetAnimLayer> Layers;

	/** GroupName, or "Layer Group N" (1-based) when it was left empty. */
	FString GetDisplayName(int32 GroupIndex) const;

	/** "Layer Group N" for the group at GroupIndex. */
	static FString MakeDefaultName(int32 GroupIndex);
};

USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float DelayRemaining = 0.f;

	/**
	 * For the first layer of a loop group, the group's position on its timeline, which is what is
	 * advanced. For every other layer, that layer's own progress into its duration, kept for reading only.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float CycleTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bFinished = false;

	/** Legs completed. A ping-pong return counts as its own leg. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	int32 LoopsCompleted = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bPlayingForward = true;

	/**
	 * Set by EndWidgetAnimTrackLoops with a soft settle. A looping layer with this set finishes the cycle
	 * it is in instead of starting another one - and if that cycle was a ping-pong return leg, it plays
	 * one more forward leg first, so it comes to rest on its end pose rather than snapping to it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bSettleRequested = false;

	/**
	 * A value-driven layer's value: where it is and where it is heading, as progress along it. Only the track player
	 * moves it (see UWidgetAnimLibrary::TickAnimationsInto); everywhere else a value layer plays on the clock.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FWidgetAnimValueState ValueState;
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

	/** Additive, like Translation and Angle. Nothing wrote this channel before the Transform family. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FVector2D Shear = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float Angle = 0.f;
};

/**
 * One Shape Box's outline for this frame.
 *
 * Authoritative, not accumulated: two shapes cannot be meaningfully added or multiplied, so the last
 * layer to write a given box wins. That is deliberately unlike TargetedTransforms, and is why this is a
 * flat "here is the shape" rather than a running total.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimShapeTarget
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UShapeBox> TargetBox = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FShapeBoxShape Shape;

	/**
	 * Only the box's authored outline, put in for a layer that isn't moving it this frame - see
	 * UWidgetAnimLibrary::TouchLayerTargets. Any outline a layer is actually animating wins over it, whichever
	 * order the two were written in. Not reflected: it only lives for the frame it is worked out in.
	 */
	bool bIsBaseline = false;
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

	/** Shape Box outlines written this frame. Empty unless a Transform layer has Animate Shape on. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimShapeTarget> ShapeTargets;

	/**
	 * Registered-type writes this frame: one per target and channel, the last track to write it winning - the third
	 * way a result composes, next to the transforms that multiply and add and the parameters and shapes that are
	 * assigned. Not reflected: each write points at the settings it came from, and lives only as long as the frame it
	 * is worked out in. See FWidgetAnimAuthoritativeWrite.
	 */
	TArray<FWidgetAnimAuthoritativeWrite> AuthoritativeWrites;

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

	/**
	 * True once every layer of this track has come to rest. A track with an endlessly looping layer
	 * never gets here on its own - it keeps running until it is stopped or its loop is ended.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bFinished = false;

	/**
	 * True once every layer that CAN end has ended - everything except endless loops.
	 *
	 * This, not bFinished, is what "the entrance has landed" / "the exit is done" means. A pulse that
	 * loops forever on top of an entrance must not keep the widget in Playing In for good, but it must
	 * not be frozen either, which is what happened when the two questions were answered by one flag.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bSettled = false;

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

	/**
	 * The last value set for each value, by SetWidgetAnimTrackValue - raw, up to four channels. A value-driven layer
	 * starting later - the widget shown again, Play In restarting everything - starts from it rather than from its
	 * Start Value, so it picks up where it was instead of jumping back.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimRememberedValue> Values;
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
	FVector2D Shear = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float Angle = 0.f;
};

/**
 * What one Shape Box's outline was before any animation touched it.
 *
 * Kept apart from FWidgetAnimBaseline because a shape is not part of the render transform and most
 * animated widgets are not Shape Boxes - there is no point carrying a whole FShapeBoxShape on every
 * baseline for the sake of the few that are. It is also what a Transform key's Use Authored
 * Shape resolves against.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimShapeBaseline
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UShapeBox> Box = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FShapeBoxShape Shape;

	/**
	 * Captured for layers that named no target, off the Shape Box the fallback widget resolves to. What a
	 * lookup for a null box finds, the same way a transform with no target falls back to the animated widget.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bIsFallback = false;
};

USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimBaselineSet
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimBaseline> Baselines;

	/** Authored outlines of every Shape Box a Transform layer animates the shape of. Usually empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimShapeBaseline> ShapeBaselines;

	/**
	 * What every registered track's target held before it was animated - a Text Block's text under a Count Up, say -
	 * one per target and channel. Usually empty.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TArray<FWidgetAnimAuthoritativeBaseline> AuthoritativeBaselines;

	/** Set by the first capture, so a later pass can't re-capture a pose the animation itself just wrote. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bCaptured = false;

	void ResetBaselines()
	{
		Baselines.Reset();
		ShapeBaselines.Reset();
		AuthoritativeBaselines.Reset();
		bCaptured = false;
	}

	/** The captured value of Target's Channel, or null if nothing captured one. */
	const FWidgetAnimAuthoritativeBaseline* FindAuthoritativeBaseline(const UObject* Target, FName Channel) const
	{
		return AuthoritativeBaselines.FindByPredicate([Target, Channel](const FWidgetAnimAuthoritativeBaseline& Baseline)
		{
			return Baseline.Target.Get() == Target && Baseline.Channel == Channel;
		});
	}

	/**
	 * The authored outline captured for Box, or null if nothing captured one. A null Box means "the layer
	 * named no target" and finds the fallback capture, if there was one.
	 */
	const FShapeBoxShape* FindShapeBaseline(const UShapeBox* Box) const
	{
		for (const FWidgetAnimShapeBaseline& Baseline : ShapeBaselines)
		{
			if (Box ? (Baseline.Box == Box) : Baseline.bIsFallback)
			{
				return &Baseline.Shape;
			}
		}

		return nullptr;
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

	/**
	 * Pushes every Shape Box outline in Result onto its box.
	 *
	 * Folded into ApplyWidgetAnimResult and its baseline-aware twin already, so ordinary callers never
	 * need this. It is public for the same reason ApplyMaterialAnimResult is: a host driving the pieces
	 * by hand should be able to.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation")
	static void ApplyShapeAnimResult(const FWidgetAnimResult& Result, UWidget* FallbackWidget = nullptr);

	/**
	 * Sets Box's shape and invalidates it so Slate actually re-arranges and repaints it. Does nothing when the
	 * shape is unchanged. Every shape the library writes - animated or restored - goes through here.
	 */
	static void ApplyShapeToBox(UShapeBox* Box, const FShapeBoxShape& Shape);

	/**
	 * The Shape Box a Transform layer's shape acts through for Widget: Widget itself when it is one, or the
	 * Shape Box it sits inside when it is that box's content. Null otherwise.
	 *
	 * The parent rule is the one the Context Menu Button's shape targets already use. It is what lets a plain
	 * Image be the animated widget - moved by its own Render Transform - while the Shape Box around it takes
	 * the outline, which is exactly how the Widget Animation Builder's preview is laid out.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation")
	static UShapeBox* ResolveShapeBox(UWidget* Widget);

	/**
	 * Every layer as runtime settings, in order, with each one's Delay turned from "after the layer above"
	 * into seconds from the start of the animation - the only place that sequencing rule lives. Everything
	 * that plays a list of layers (entries, a component's Additive Anim Layers, the builder) goes through
	 * this, so a layer is placed the same way no matter where the list is played from.
	 */
	static void BuildLayerSettings(const TArray<FWidgetAnimLayer>& Layers, TArray<FWidgetAnimSettings>& OutSettings);

#if WITH_EDITOR
	/**
	 * For an editor preview: marks the settings built from Layers - starting at FirstSetting, one per layer, in
	 * order - muted wherever the track is hidden in the Widget Animations panel. See FWidgetAnimSettings::bMuted.
	 */
	static void MuteHiddenLayers(const TArray<FWidgetAnimLayer>& Layers, TArray<FWidgetAnimSettings>& Settings, int32 FirstSetting = 0);
#endif

	/**
	 * Brings an animation's groups and layers into shape after loading: layers still held inside groups (a
	 * format that only briefly existed) moved into the single list with their group recorded, at least one
	 * group whenever there are layers, and every layer's LayerGroup pointing at a group that exists.
	 * True if anything changed.
	 */
	static bool NormalizeLayerGroups(TArray<FWidgetAnimLayerGroup>& Groups, TArray<FWidgetAnimLayer>& Layers);

	/** The group a layer really plays in: its LayerGroup, kept in range. INDEX_NONE when there are no groups. */
	static int32 GetLayerGroupIndex(const FWidgetAnimLayer& Layer, int32 NumGroups);

	/**
	 * Seconds one pass of Settings takes: where its last layer ends, delays included, loops ignored. The
	 * clock a scrub slider walks along, and what ApplyWidgetAnimationsAtAlpha measures Alpha against.
	 */
	static float GetStackPassDuration(const TArray<FWidgetAnimSettings>& Settings);

	/**
	 * Seconds into its stack that key KeyIndex of a layer lands: the layer's delay plus the key's time, both
	 * scaled by the layer's playback speed. -1 if the layer has no such key.
	 *
	 * A Transform layer's keys are read off its curve rather than off Keys, so it is right even before Keys has
	 * synced. Any other keyed layer counts its start as key 0, its keys in between, and its end as the last.
	 */
	static float GetKeyStackTime(const FWidgetAnimSettings& Layer, int32 KeyIndex);

	/** GetKeyStackTime, from when only Transform layers had keys. */
	static float GetTransformKeyStackTime(const FWidgetAnimSettings& Layer, int32 KeyIndex) { return GetKeyStackTime(Layer, KeyIndex); }

	/**
	 * Seconds one play of a layer takes, before its playback speed: its curve's length on a Transform layer, its
	 * spring's settle time on a Spring layer, Duration otherwise.
	 */
	static float GetLayerRunSeconds(const FWidgetAnimSettings& Layer);

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
	 * Freezes a stack at one point in one pass of its timeline and applies it. Alpha is a fraction of
	 * GetStackPassDuration, so 0.5 is halfway through the pass in time - and each layer is posed where it
	 * really is at that moment, its Delay included: a layer that has not started yet leaves its widget as it
	 * was authored (at its captured baseline, when there is one - see TouchLayerTargets), one that has finished
	 * holds its end pose, exactly. Looping and ping-pong are ignored; this is one pass.
	 *
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
	 * staggered where it is referenced without editing the preset asset itself. A Spring layer's spring is
	 * retimed to match - the same motion, Speed times faster.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Widget Animation|Authoring")
	static FWidgetAnimSettings RetimeWidgetAnimation(const FWidgetAnimSettings& Settings, float Speed = 1.f, float ExtraDelay = 0.f);

	/**
	 * Appends Source to Destination, copying every entry that has Mirror In Out set into MirrorDestination
	 * as well. The one place the mirroring rule lives, so presets, cards, panels and buttons all mirror
	 * identically.
	 */
	static void AppendWithMirroring(const TArray<FWidgetAnimSettings>& Source, TArray<FWidgetAnimSettings>& Destination, TArray<FWidgetAnimSettings>& MirrorDestination);

	/**
	 * How long one full pass of Settings takes to play, in seconds - the whole stack together, accounting
	 * for staggered starts, shared loop groups, and each layer's own finite Loop Count, exactly as
	 * TickWidgetAnimations plays it. -1 if any layer in the stack loops forever, since then the stack as a
	 * whole never finishes on its own.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Authoring")
	static float GetStackDuration(const TArray<FWidgetAnimSettings>& Settings);

	/**
	 * How far into one pass of Settings the given States currently are, in the same seconds GetStackDuration
	 * reports - the position TickWidgetAnimations left the furthest-along loop group at. 0 if States hasn't
	 * been initialized for this stack yet (nothing has played).
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Authoring")
	static float GetStackElapsed(const TArray<FWidgetAnimSettings>& Settings, const TArray<FWidgetAnimState>& States);

	/**
	 * Whether the layer group that decides GetStackDuration's answer loops, and if so, how long one visible
	 * repeat of it takes - the span AdvanceTimeline repeats after the first leg, doubled by OutPingPong
	 * since a round trip is there-and-back. Returns false, with OutLoopSeconds left at 0, when nothing in
	 * the stack drives a loop (including an empty stack).
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Authoring")
	static bool GetStackDrivingLoopInfo(const TArray<FWidgetAnimSettings>& Settings, bool& OutPingPong, float& OutLoopSeconds);

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

	/**
	 * Sets the value of every value-driven layer Id names, in every track, and remembers it for any that start later. Value is raw, up to four channels (see WidgetAnimValue::FromFloat and the others); each layer maps
	 * it onto itself through its own range. Snap jumps straight there, and may go back even where a layer never goes
	 * backwards - for resetting. Returns how many running layers took it.
	 */
	static int32 SetWidgetAnimTrackValue(FWidgetAnimPlayer& Player, const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap);

	/**
	 * The raw value the first value-driven layer Id names shows now - or, with bTargetValue, the one it is heading to.
	 * When none is running, the last value set for it. False if there is neither.
	 */
	static bool GetWidgetAnimTrackValue(const FWidgetAnimPlayer& Player, const FWidgetAnimValueId& Id, bool bTargetValue, FVector4& OutValue);

	/** Whether any layer in Settings is value-driven and named by Id. */
	static bool HasValueLayer(const TArray<FWidgetAnimSettings>& Settings, const FWidgetAnimValueId& Id);

	/**
	 * Gives every value-driven layer in Layers that has no Value Name one from its type - "Scale", then "Scale 1",
	 * "Scale 2" - unique among Layers and TakenNames, which it adds the names to. Once given, a name is the layer's own:
	 * changing its type or moving it never renames it. True if any name was given.
	 */
	static bool AssignDefaultValueNames(TArray<FWidgetAnimLayer>& Layers, TSet<FName>* TakenNames = nullptr);

	/**
	 * Before an exit: sends every value-driven layer set to Complete First to its end, and answers whether any of them -
	 * or any set to Wait For Value - still has a way to go, which the exit should wait out. Only tracks going in are
	 * asked: they are the ones an exit replaces.
	 */
	static bool PrepareValueLayersForExit(FWidgetAnimPlayer& Player);

	/** Whether an exit starting now would wait for a value-driven layer: PrepareValueLayersForExit's answer, without sending anything anywhere. */
	static bool IsAnyValueLayerHoldingExit(const FWidgetAnimPlayer& Player);

	/** Stops tracks matching any of the given IDs, or every track when the array is empty. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Player")
	static void StopWidgetAnimTracks(UPARAM(ref) FWidgetAnimPlayer& Player, const TArray<FName>& AnimIds, bool bRemoveTracks = true);

	/**
	 * Ends the looping on every running track with this Anim ID. Returns true if any track was affected.
	 *
	 * Soft settle: each looping animation finishes the leg it is in and then stops on its end pose (a
	 * ping-pong caught on its return leg plays one more forward leg to get there). Layers that do not
	 * loop simply carry on to their natural end.
	 *
	 * Hard: every layer jumps to its end pose now. The track is reported finished on the next player
	 * tick, and is dropped then if it was started without Hold When Finished.
	 *
	 * None never matches anything - unnamed tracks cannot be singled out.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Player")
	static bool EndWidgetAnimTrackLoops(UPARAM(ref) FWidgetAnimPlayer& Player, FName AnimId, bool bSoftSettle = true);

	/** True for a layer that loops with no Loop Count, i.e. one that will never end by itself. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation")
	static bool IsEndlessLoop(const FWidgetAnimSettings& Settings);

	/** True if any track matching the IDs is still running. An empty array asks about every track. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Player")
	static bool AreWidgetAnimTracksPlaying(const FWidgetAnimPlayer& Player, const TArray<FName>& AnimIds);

	/**
	 * Advances every track, combines them into one result and applies it once.
	 *
	 * Returns true while anything is still running. IDs that reached their end on this exact frame come
	 * back in OutJustFinished, which is how a caller knows an exit animation is done.
	 *
	 * A track's value-driven layers move toward the last value set for their names instead of on its clock; the track
	 * counts as running while any of them is moving.
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

	/**
	 * A layer's Easing applied to LinearAlpha (0-1): the curve it moves along, custom curve included. Public so
	 * editor tools can draw it - the Widget Animations panel sketches it across each layer's section.
	 *
	 * On a Spring layer it is the spring's progress instead, run forward - which rises past 1 while it
	 * overshoots, so a sketch drawn from it shows the bounce.
	 */
	static float EvaluateEasedAlpha(const FWidgetAnimSettings& Settings, float LinearAlpha);

	/**
	 * How far along its travel a layer is at Alpha (0-1): the layer's Easing - or, on a Tween Scale or Slide with
	 * an overshoot, that overshoot - or, on a Spring layer, its spring, heading the way bTowardStart says. What a
	 * segment left at Track Easing moves by, too. Public so editor tools can sketch a segment's curve.
	 */
	static float EvaluateTrackEase(const FWidgetAnimSettings& Settings, float Alpha, bool bTowardStart = false);

	/**
	 * Where a keyed layer is at DirectedAlpha: which segment - from point OutSegment to the next - and how far
	 * across it, OutAlpha, which may run past 0 or 1 while the layer overshoots. Exactly the first point at 0 and
	 * the last at 1, whatever the easing or the spring makes of either end. See FWidgetAnimValueKeys.
	 */
	static void SampleValueKeys(const FWidgetAnimSettings& Settings, float DirectedAlpha, bool bTowardStart, int32& OutSegment, float& OutAlpha);

private:

	static float EaseOutBack(float Alpha, float Overshoot);
	static FVector2D GetSlideDirectionOffset(EWidgetSlideDirection Direction);

	static FWidgetAnimTargetedTransform& FindOrAddTargetedTransform(FWidgetAnimResult& Result, UWidget* TargetWidget);

	/**
	 * One layer's contribution at DirectedAlpha.
	 *
	 * Baselines is optional and only the Transform family reads it, to resolve its keys' Use
	 * Authored Shape against the outline the baseline captured. Every other type ignores it.
	 *
	 * bTowardStart says the layer is traveling back toward its start values right now - inverted, or on a
	 * ping-pong's return leg, but not both. Only Spring layers read it: they run their spring forward in time
	 * toward the start instead of rewinding it. See UWidgetAnimSpringLibrary::EvaluateDirectedProgress.
	 */
	static void ApplyAnimationContribution(const FWidgetAnimSettings& Settings, float DirectedAlpha, FWidgetAnimResult& OutResult, const FWidgetAnimBaselineSet* Baselines = nullptr, bool bTowardStart = false);

	/**
	 * How far along its travel a layer is at DirectedAlpha: its Easing on a Tween layer, its spring - in the
	 * right direction - on a Spring layer. Overshoot on Scale and Slide is not applied here; it replaces this.
	 */
	static float EvaluateLayerAlpha(const FWidgetAnimSettings& Settings, float DirectedAlpha, bool bTowardStart);

	static bool GetEffectiveInvert(const FWidgetAnimSettings& Settings, bool bIsOutAnimation);

	/** The captured pose for one widget, or an identity baseline when the set doesn't hold it. */
	static FWidgetAnimBaseline FindBaselineFor(const FWidgetAnimBaselineSet& Baselines, UWidget* Widget);

	/** Adds Widget to the set if it isn't in it already, recording its current pose. */
	static void CaptureOne(FWidgetAnimBaselineSet& Baselines, UWidget* Widget);

	/** Records Box's authored outline, if it isn't already recorded. The Shape Box half of CaptureOne. */
	static void CaptureOneShape(FWidgetAnimBaselineSet& Baselines, UShapeBox* Box, bool bIsFallback = false);

	// Baseline-aware cores. The original entry points forward to these with a null set, which is the
	// absolute (pre-baseline) behavior every existing caller already relies on.
	/**
	 * TickWidgetAnimations, accumulating into a caller-owned result so several tracks can share one.
	 *
	 * Returns whether the stack has SETTLED (every layer that can end has ended). bOutAllComplete, when
	 * given, receives whether EVERY layer has ended, endless loops included - which only happens once
	 * a loop has been ended explicitly.
	 *
	 * bDriveValueLayers: value-driven layers follow their values (FWidgetAnimState::ValueState) instead of the clock,
	 * stand outside their loop group, count as ended while their value is at rest and never hold the stack from
	 * settling. Only the track player asks for it; every preview plays them on the clock.
	 */
	static bool TickAnimationsInto(const TArray<FWidgetAnimSettings>& Animations, TArray<FWidgetAnimState>& States, float DeltaTime, bool bIsOutAnimation, FWidgetAnimResult& OutResult, bool* bOutAllComplete = nullptr, const FWidgetAnimBaselineSet* Baselines = nullptr, bool bDriveValueLayers = false);

	/**
	 * Puts a layer that contributes nothing this frame - not started yet, or hidden - into the result anyway, as
	 * the identity: its widget is then written at its captured baseline rather than left at whatever it was last
	 * posed at. Without that, a scrub from the middle of a fade back to before it starts left the widget
	 * half-faded, because nothing wrote it at all. Only when a baseline was captured; see ApplyResultInternal.
	 */
	static void TouchLayerTargets(const FWidgetAnimSettings& Settings, FWidgetAnimResult& OutResult, const FWidgetAnimBaselineSet& Baselines);

	/** True while any layer in the stack is still moving - an endless loop always is. */
	static bool HasRunningLayers(const TArray<FWidgetAnimSettings>& Animations, const TArray<FWidgetAnimState>& States);

	/**
	 * Whether this one layer is still something PlayingIn / PlayingOut should wait on before calling the
	 * stack settled. A real not-yet-finished layer always is. An endless loop is too, until it has played
	 * through its whole span at least once - after that it is just looping on top of a stack that has
	 * otherwise arrived, not still arriving itself, and must not hold the transition to Shown / Hidden
	 * open forever. Without that leg-count check, a stack that is ENTIRELY endless layers (an "entry"
	 * previewed with Loop Count 0, say) would report itself settled from frame one, before any of it had
	 * actually played - which is what used to make a looped preview snap into its held pose immediately
	 * and cut PlayingOut in mid-cycle.
	 */
	static bool BlocksSettle(const FWidgetAnimSettings& Settings, const FWidgetAnimState& State);

	/**
	 * A value-driven layer's contribution at its value: one channel posed like any layer at that progress, or - for a
	 * Vector 2D or Color value - each channel posed on its own and its axis or channel taken from that pose. A Transform
	 * keeps only what its Value Drives picks; a Pulse pulses on its own clock at the strength its value sets.
	 */
	static void ApplyValueLayerContribution(const FWidgetAnimSettings& Settings, const FWidgetAnimValueState& ValueState, FWidgetAnimResult& OutResult, const FWidgetAnimBaselineSet* Baselines);

	/** A remembered value for a layer named Name in Scope: one set there, else one set for the name anywhere. */
	static const FWidgetAnimRememberedValue* FindRememberedValue(const FWidgetAnimPlayer& Player, const UObject* Scope, FName Name);

	/** Puts a newly started track's value-driven layers at the last value set for their names, or their Start Value. */
	static void InitializeValueLayers(const FWidgetAnimPlayer& Player, FWidgetAnimTrack& Track);

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