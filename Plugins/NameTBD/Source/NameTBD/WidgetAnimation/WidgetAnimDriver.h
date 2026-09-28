// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "WidgetAnimEasing.h" // EWidgetAnimEasing
#include "WidgetAnimSpring.h" // FWidgetSpringParams

#include "WidgetAnimDriver.generated.h"

class UCurveFloat;

// =====================================================================================================
// Value-driven tracks
// =====================================================================================================
//
// Most tracks are driven by time: a clock runs, and the track says where its widget is at each moment. Some UI has
// no timeline at all - a loading bar follows load progress, a health bar follows health, a "73%" counts to whatever
// the server last said. The motion is still authored as a track, with its start, end, keys and curves; only WHERE
// along it the widget is comes from a value instead of the clock.
//
// So being value-driven is a Timing, next to Tween and Spring, chosen per track in the Widget Animations editor. A
// value track has a Value Name - given one from its type ("Scale", "Scale 1") when none is typed - and gameplay sets
// that value (Set Widget Animation Value); the track travels there the way its Value Smoothing says. The rest of the
// animation plays on its clock as usual, so one animation can spin an icon on its clock while its bar follows a
// value. A value belongs to its animation: the same name in two animations is two values (see FWidgetAnimValueId).
// The value is typed - a float, an integer, a 2D vector or a color, as the track type allows - and each channel of
// it is mapped onto the track through Value At Start and Value At End: set those to 0 and 100 and a health of 25
// puts the track a quarter of the way from its start to its end. A Pulse is the exception: its value is how strong
// the pulse is, 0 none and 1 full, while it keeps pulsing on its own.
//
// Smoothing is authored on the track. What a use of the animation may change - where its value starts, the range,
// whether it may go backwards, how fast it may move, what it does on exit - is FWidgetAnimValueOptions, authored on
// the track as its defaults and overridable per widget on the Widget Animation Component.
//
// The value is carried as a fraction of the track - its progress, 0 at the start and 1 at the end, one per channel -
// from the moment it arrives, so smoothing, Never Go Backwards and Minimum Time To Fill mean the same on any range.
// In every preview a value track sweeps once across its Duration on the clock, so it can be judged like any other.

/**
 * What kind of value a value-driven track takes. Each channel is mapped onto the track on its own. Which kinds a
 * track offers depends on its type - see WidgetAnimValueKindAllowed.
 */
UENUM(BlueprintType)
enum class EWidgetAnimValueKind : uint8
{
	Float UMETA(ToolTip = "One number. Set with Set Widget Animation Value."),

	Integer UMETA(ToolTip = "One whole number - a count, a level. Set with Set Widget Animation Value (Integer). Smoothing still passes through the numbers in between, which a Count Up shows as it counts."),

	Vector2D UMETA(DisplayName = "Vector 2D", ToolTip = "Two numbers, each moving its own axis: X and Y of a Scale separately, or of whatever a Transform track's Value Drives. Set with Set Widget Animation Value (Vector 2D)."),

	Color UMETA(ToolTip = "Four numbers, R G B A, each moving its own channel of a Material Vector track. Set with Set Widget Animation Value (Color).")
};

/**
 * What a value-driven Transform track's value moves: the whole path of its keys, or one field of the pose - the
 * widget's translation, scale, shear or angle, or one of the Shape Box's shape settings - whole or one axis of it.
 * The value still travels along the keys - they say what that field is at each point - and the rest of the keys is
 * left out. One value per track: add another Transform track for another. Listed alphabetically after Whole Path.
 *
 * Saved by name, so entries can be reordered freely.
 */
UENUM(BlueprintType)
enum class EWidgetAnimTransformValueTarget : uint8
{
	WholePath UMETA(DisplayName = "Whole Path", ToolTip = "Everything the track's keys animate - translation, scale, shear, angle and shape - moves together along its keys. A Vector 2D value moves the X parts and the Y parts separately."),

	Angle UMETA(ToolTip = "Only the widget's angle. Takes a Float or an Integer."),

	Scale UMETA(ToolTip = "Only the widget's scale, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both."),

	ScaleX UMETA(DisplayName = "Scale X", ToolTip = "Only the X of the widget's scale. Takes a Float or an Integer."),

	ScaleY UMETA(DisplayName = "Scale Y", ToolTip = "Only the Y of the widget's scale. Takes a Float or an Integer."),

	ShapeAngle UMETA(DisplayName = "Shape Angle", ToolTip = "Only the Shape Box's Angle, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeAngleX UMETA(DisplayName = "Shape Angle X", ToolTip = "Only the X of the Shape Box's Angle. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeAngleY UMETA(DisplayName = "Shape Angle Y", ToolTip = "Only the Y of the Shape Box's Angle. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeBend UMETA(DisplayName = "Shape Bend", ToolTip = "Only the Shape Box's Bend, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeBendX UMETA(DisplayName = "Shape Bend X", ToolTip = "Only the X of the Shape Box's Bend. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeBendY UMETA(DisplayName = "Shape Bend Y", ToolTip = "Only the Y of the Shape Box's Bend. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerBottomLeft UMETA(DisplayName = "Shape Corner Bottom Left", ToolTip = "Only the Shape Box's corner Bottom Left cut size, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerBottomLeftX UMETA(DisplayName = "Shape Corner Bottom Left X", ToolTip = "Only the X of the Shape Box's corner Bottom Left cut size. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerBottomLeftY UMETA(DisplayName = "Shape Corner Bottom Left Y", ToolTip = "Only the Y of the Shape Box's corner Bottom Left cut size. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerBottomRight UMETA(DisplayName = "Shape Corner Bottom Right", ToolTip = "Only the Shape Box's corner Bottom Right cut size, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerBottomRightX UMETA(DisplayName = "Shape Corner Bottom Right X", ToolTip = "Only the X of the Shape Box's corner Bottom Right cut size. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerBottomRightY UMETA(DisplayName = "Shape Corner Bottom Right Y", ToolTip = "Only the Y of the Shape Box's corner Bottom Right cut size. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerTopLeft UMETA(DisplayName = "Shape Corner Top Left", ToolTip = "Only the Shape Box's corner Top Left cut size, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerTopLeftX UMETA(DisplayName = "Shape Corner Top Left X", ToolTip = "Only the X of the Shape Box's corner Top Left cut size. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerTopLeftY UMETA(DisplayName = "Shape Corner Top Left Y", ToolTip = "Only the Y of the Shape Box's corner Top Left cut size. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerTopRight UMETA(DisplayName = "Shape Corner Top Right", ToolTip = "Only the Shape Box's corner Top Right cut size, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerTopRightX UMETA(DisplayName = "Shape Corner Top Right X", ToolTip = "Only the X of the Shape Box's corner Top Right cut size. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeCornerTopRightY UMETA(DisplayName = "Shape Corner Top Right Y", ToolTip = "Only the Y of the Shape Box's corner Top Right cut size. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeEdgeBowBottom UMETA(DisplayName = "Shape Edge Bow Bottom", ToolTip = "Only the Shape Box's Edge Bow Bottom. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeEdgeBowLeft UMETA(DisplayName = "Shape Edge Bow Left", ToolTip = "Only the Shape Box's Edge Bow Left. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeEdgeBowRight UMETA(DisplayName = "Shape Edge Bow Right", ToolTip = "Only the Shape Box's Edge Bow Right. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeEdgeBowTop UMETA(DisplayName = "Shape Edge Bow Top", ToolTip = "Only the Shape Box's Edge Bow Top. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeExtrude UMETA(DisplayName = "Shape Extrude", ToolTip = "Only the Shape Box's Extrude, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeExtrudeX UMETA(DisplayName = "Shape Extrude X", ToolTip = "Only the X of the Shape Box's Extrude. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeExtrudeY UMETA(DisplayName = "Shape Extrude Y", ToolTip = "Only the Y of the Shape Box's Extrude. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeInset UMETA(DisplayName = "Shape Inset", ToolTip = "Only the Shape Box's Inset, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeInsetX UMETA(DisplayName = "Shape Inset X", ToolTip = "Only the X of the Shape Box's Inset. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeInsetY UMETA(DisplayName = "Shape Inset Y", ToolTip = "Only the Y of the Shape Box's Inset. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapePerspective UMETA(DisplayName = "Shape Perspective", ToolTip = "Only the Shape Box's Perspective. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeRotation UMETA(DisplayName = "Shape Rotation", ToolTip = "Only the Shape Box's Rotation. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeWrap UMETA(DisplayName = "Shape Wrap", ToolTip = "Only the Shape Box's Wrap, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeWrapX UMETA(DisplayName = "Shape Wrap X", ToolTip = "Only the X of the Shape Box's Wrap. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	ShapeWrapY UMETA(DisplayName = "Shape Wrap Y", ToolTip = "Only the Y of the Shape Box's Wrap. Takes a Float or an Integer. Needs the track's Animate Shape on, and a Shape Box to animate."),

	Shear UMETA(ToolTip = "Only the widget's shear, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both."),

	ShearX UMETA(DisplayName = "Shear X", ToolTip = "Only the X of the widget's shear. Takes a Float or an Integer."),

	ShearY UMETA(DisplayName = "Shear Y", ToolTip = "Only the Y of the widget's shear. Takes a Float or an Integer."),

	Translation UMETA(ToolTip = "Only the widget's translation, X and Y. A Vector 2D value moves each on its own; a Float or an Integer moves both."),

	TranslationX UMETA(DisplayName = "Translation X", ToolTip = "Only the X of the widget's translation. Takes a Float or an Integer."),

	TranslationY UMETA(DisplayName = "Translation Y", ToolTip = "Only the Y of the widget's translation. Takes a Float or an Integer.")
};

/** The field of a Transform pose a value target moves. See WidgetAnimValue::GetTransformPart. */
enum class EWidgetAnimTransformPart : uint8
{
	WholePath,
	Translation,
	Scale,
	Shear,
	Angle,
	ShapeAngle,
	ShapeBend,
	ShapeCornerBottomLeft,
	ShapeCornerBottomRight,
	ShapeCornerTopLeft,
	ShapeCornerTopRight,
	ShapeEdgeBowBottom,
	ShapeEdgeBowLeft,
	ShapeEdgeBowRight,
	ShapeEdgeBowTop,
	ShapeExtrude,
	ShapeInset,
	ShapePerspective,
	ShapeRotation,
	ShapeWrap
};

/** How a value-driven track travels to each new value it is given. Authored on the track. */
UENUM(BlueprintType)
enum class EWidgetAnimValueSmoothing : uint8
{
	Immediate UMETA(ToolTip = "Jumps to each new value. For values that already move smoothly, such as a slider's position."),

	Tween UMETA(ToolTip = "Eases to each new value over the track's Duration, by its Easing. Every new value starts a new ease from wherever it had gotten to."),

	Spring UMETA(ToolTip = "Chases each new value with the track's spring, keeping its speed when the value changes again mid-way. What makes jumpy values, such as load progress arriving in chunks, look smooth.")
};

/** What a value-driven track does when its widget plays its exit while its value is still moving. Not used by a Pulse, whose value is its strength. */
UENUM(BlueprintType)
enum class EWidgetAnimValueExit : uint8
{
	ExitRightAway UMETA(ToolTip = "The exit plays at once, wherever the value has gotten to."),

	WaitForValue UMETA(DisplayName = "Wait For Value", ToolTip = "The exit waits until the track has arrived at the last value set - a bar gliding to 0.8 gets there first."),

	CompleteFirst UMETA(DisplayName = "Complete First", ToolTip = "The track is sent to its end and the exit waits until it gets there - a loading bar always fills before the screen closes, even if the last value set was 90%.")
};

/**
 * How a value-driven track takes its value: the range mapped onto it, where it starts, and how it may move. Authored
 * on the track as its defaults (Timing = Value), and overridable per widget in the Widget Animation Component's Value
 * Tracks - everything except the value type and the smoothing, which belong to the track. See the note at the top of
 * WidgetAnimDriver.h.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimValueOptions
{
	GENERATED_BODY()

	/**
	 * The kind of value the track takes, from those its type allows. Chosen on the track; a widget's override shows it
	 * but can't change it. One the track's type doesn't allow is read as Float.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "Value Type", EditCondition = "bCanEditKind", EditConditionHides, HideEditConditionToggle))
	EWidgetAnimValueKind Kind = EWidgetAnimValueKind::Float;

	/** Whether Value Type is shown. Set by the component on its override rows, where the track decides it. */
	UPROPERTY()
	bool bCanEditKind = true;

	/**
	 * The value that puts the track at its start, and the one that puts it at its end. With 0 and 100, a value of 25
	 * is a quarter of the way along. Values outside the range are held at its ends; swap them to run the other way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (EditCondition = "Kind == EWidgetAnimValueKind::Float || Kind == EWidgetAnimValueKind::Integer", EditConditionHides))
	double ValueAtStart = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (EditCondition = "Kind == EWidgetAnimValueKind::Float || Kind == EWidgetAnimValueKind::Integer", EditConditionHides))
	double ValueAtEnd = 1.0;

	/** The same, per axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "Value At Start", EditCondition = "Kind == EWidgetAnimValueKind::Vector2D", EditConditionHides))
	FVector2D ValueAtStart2D = FVector2D(0.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "Value At End", EditCondition = "Kind == EWidgetAnimValueKind::Vector2D", EditConditionHides))
	FVector2D ValueAtEnd2D = FVector2D(1.0, 1.0);

	/** The same, per channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "Value At Start", EditCondition = "Kind == EWidgetAnimValueKind::Color", EditConditionHides))
	FLinearColor ValueAtStartColor = FLinearColor(0.f, 0.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "Value At End", EditCondition = "Kind == EWidgetAnimValueKind::Color", EditConditionHides))
	FLinearColor ValueAtEndColor = FLinearColor(1.f, 1.f, 1.f, 1.f);

	/**
	 * The value the track shows when it starts, before any value is set. Once one has been set it starts from the
	 * last one instead, so a widget shown again picks up where it was.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (EditCondition = "Kind == EWidgetAnimValueKind::Float || Kind == EWidgetAnimValueKind::Integer", EditConditionHides))
	double StartValue = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "Initial Value", EditCondition = "Kind == EWidgetAnimValueKind::Vector2D", EditConditionHides))
	FVector2D StartValue2D = FVector2D(0.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "Initial Value", EditCondition = "Kind == EWidgetAnimValueKind::Color", EditConditionHides))
	FLinearColor StartValueColor = FLinearColor(0.f, 0.f, 0.f, 0.f);

	/**
	 * Ignores a value that would move the track back toward its start, so it only ever moves forward - a loading bar
	 * that never shrinks. Each channel on its own. Set Widget Animation Value with Snap still moves it back.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value")
	bool bNeverGoBackwards = false;

	/**
	 * The shortest time the track may take to travel from its start to its end: how fast it can move at most. A load
	 * that finishes in a blink still shows the bar filling over this long. 0 lets it move as fast as its smoothing
	 * says. Not used by Immediate smoothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "Minimum Time To Fill", Units = "s", ClampMin = "0", UIMin = "0", UIMax = "5"))
	float MinimumTimeToFill = 0.f;

	/** What happens when its widget plays its exit (Play Out, a screen closing) while the value is still moving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "On Exit"))
	EWidgetAnimValueExit OnExit = EWidgetAnimValueExit::WaitForValue;

	/** 1 for Float and Integer, 2 for Vector 2D, 4 for Color. */
	int32 GetNumChannels() const;

	/** The range of one channel: the values that put the track at its start and at its end. */
	void GetChannelRange(int32 Channel, double& OutAtStart, double& OutAtEnd) const;

	/** A raw value (up to four channels) as progress along the track, each channel held within 0 - 1. */
	FVector4f ToProgress(const FVector4& Value) const;

	/** Progress along the track back to a raw value. */
	FVector4 FromProgress(const FVector4f& Progress) const;

	/** Start Value, raw. */
	FVector4 GetStartValue() const;
};

/**
 * One row of a Widget Animation Component's Value Tracks: a value its animations use - a Value Name in one animation,
 * or in the component's additive tracks - and, with Override on, this widget's own settings for it in place of the
 * track's.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimValueOverride
{
	GENERATED_BODY()

	/** Where the value's tracks are: an animation's name, or Additive Tracks. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Value", meta = (DisplayName = "In"))
	FString Source;

	/** The Value Name, as the tracks give it. Set Widget Animation Value with it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Value")
	FName ValueName;

	/** The animation the value belongs to (a Widget Animation), or none for the component's additive tracks. */
	UPROPERTY()
	TObjectPtr<UObject> Animation = nullptr;

	/** The kind of value its tracks take - which Set Widget Animation Value node to use. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Value", meta = (DisplayName = "Value Type"))
	EWidgetAnimValueKind Kind = EWidgetAnimValueKind::Float;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (InlineEditConditionToggle))
	bool bOverride = false;

	/**
	 * This widget's settings for the value, in place of its tracks' own - on while the checkbox is. Off, it shows what
	 * the tracks say, refreshed whenever the component is loaded or edited.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "Settings", EditCondition = "bOverride"))
	FWidgetAnimValueOptions Options;
};

/**
 * Which value a Set Widget Animation Value sets: a Value Name, in one animation, in a component's additive tracks, or -
 * the name alone - wherever it is used. Values belong to their animation, so "Scale" in two animations are two values;
 * naming only the value reaches both.
 */
struct NAMETBD_API FWidgetAnimValueId
{
	/** The animation (a Widget Animation asset) the value is in. Null: see bAdditiveTracks. */
	const UObject* Animation = nullptr;

	/** With no Animation: the component's additive tracks only (true), or anywhere (false). */
	bool bAdditiveTracks = false;

	FName ValueName;

	/** The name wherever it is used. */
	static FWidgetAnimValueId Anywhere(FName InValueName)
	{
		FWidgetAnimValueId Id;
		Id.ValueName = InValueName;
		return Id;
	}

	/** The name in one scope: an animation, or with a null one the additive tracks. */
	static FWidgetAnimValueId In(const UObject* InAnimation, FName InValueName)
	{
		FWidgetAnimValueId Id;
		Id.Animation = InAnimation;
		Id.bAdditiveTracks = InAnimation == nullptr;
		Id.ValueName = InValueName;
		return Id;
	}

	bool IsAnywhere() const { return Animation == nullptr && !bAdditiveTracks; }

	/** Whether a track named Name, in Scope (its animation, or null for additive tracks), takes this value. */
	bool Matches(const UObject* Scope, FName Name) const
	{
		return !ValueName.IsNone() && Name == ValueName && (IsAnywhere() || Scope == Animation);
	}
};

/** A value set earlier, remembered so value-driven tracks that start later start from it. See FWidgetAnimPlayer::Values. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimRememberedValue
{
	GENERATED_BODY()

	/** The animation it was set in, or none: for the additive tracks, or - with bAnywhere - for the name anywhere. */
	UPROPERTY(BlueprintReadOnly, Category = "Value")
	TObjectPtr<UObject> Animation = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Value")
	bool bAnywhere = false;

	UPROPERTY(BlueprintReadOnly, Category = "Value")
	FName ValueName;

	/** Raw, up to four channels. */
	UPROPERTY(BlueprintReadOnly, Category = "Value")
	FVector4 Value = FVector4(0.0, 0.0, 0.0, 0.0);
};

/** Where a value-driven track is, and where it is heading, as progress along it - up to four channels. Never saved. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimValueState
{
	GENERATED_BODY()

	/** Where the track is posed now, 0 - 1 per channel. */
	UPROPERTY(BlueprintReadOnly, Category = "Value")
	FVector4f Progress = FVector4f(0.f, 0.f, 0.f, 0.f);

	/** Where the last value set puts it, which Progress travels toward. */
	UPROPERTY(BlueprintReadOnly, Category = "Value")
	FVector4f Target = FVector4f(0.f, 0.f, 0.f, 0.f);

	/** Spring smoothing: its speed per channel, carried across a change of target. */
	UPROPERTY(BlueprintReadOnly, Category = "Value")
	FVector4f Velocity = FVector4f(0.f, 0.f, 0.f, 0.f);

	/** Tween smoothing: where the current ease started, and how far into it it is. */
	UPROPERTY(BlueprintReadOnly, Category = "Value")
	FVector4f TweenFrom = FVector4f(0.f, 0.f, 0.f, 0.f);

	UPROPERTY(BlueprintReadOnly, Category = "Value")
	float TweenElapsed = 0.f;

	/** A value-driven Pulse's own clock: where in its pulse it is, in seconds, while its value sets its strength. */
	UPROPERTY(BlueprintReadOnly, Category = "Value")
	float PulseTime = 0.f;

	/** True while it is still traveling toward its target. */
	bool IsMoving() const { return Progress != Target || Velocity != FVector4f(0.f, 0.f, 0.f, 0.f); }
};

/** How a value-driven track moves, gathered from the track for the maths below. */
struct NAMETBD_API FWidgetAnimValueMotion
{
	EWidgetAnimValueSmoothing Smoothing = EWidgetAnimValueSmoothing::Spring;
	float TweenDuration = 0.3f;
	EWidgetAnimEasing TweenEasing = EWidgetAnimEasing::EaseOut;
	const UCurveFloat* TweenCurve = nullptr;
	const FWidgetSpringParams* Spring = nullptr;
	int32 NumChannels = 1;
};

/**
 * The maths, as free functions: called per value-driven track per frame from the track player, never wired up by
 * hand. The Blueprint face of it is Set Widget Animation Value on the hosts.
 */
namespace WidgetAnimValue
{
	/**
	 * The field Target moves, and which of its axes: both for a two-axis field moved whole, one for a single axis. A
	 * field with one value (Angle, Edge Bow, Perspective, Rotation) answers X only.
	 */
	NAMETBD_API EWidgetAnimTransformPart GetTransformPart(EWidgetAnimTransformValueTarget Target, bool& bOutX, bool& bOutY);

	/** Whether Target has an X and a Y for a Vector 2D value to move one each: the whole path, or a whole two-axis field. */
	NAMETBD_API bool TransformTargetTakesTwoAxes(EWidgetAnimTransformValueTarget Target);

	/** Whether Part is one of the Shape Box's settings, which only move on a track with Animate Shape on. */
	NAMETBD_API bool IsShapePart(EWidgetAnimTransformPart Part);

	/** Puts a track at Progress, still, with nothing left to travel. Where a value track starts, and what a Snap does. */
	NAMETBD_API void Snap(FWidgetAnimValueState& State, const FVector4f& Progress);

	/**
	 * Sends a track toward new progress: a channel that would go back is ignored when the options say never to, and
	 * it is snapped to at once with bSnap or Immediate smoothing. Returns whether anything changed.
	 */
	NAMETBD_API bool SetTarget(const FWidgetAnimValueOptions& Options, const FWidgetAnimValueMotion& Motion, FWidgetAnimValueState& State, const FVector4f& NewTarget, bool bSnap);

	/**
	 * Moves Progress DeltaTime seconds toward Target, the way the motion says, no faster than Minimum Time To Fill
	 * allows. True once it has arrived and stopped, when it is also exactly on Target.
	 */
	NAMETBD_API bool Advance(const FWidgetAnimValueOptions& Options, const FWidgetAnimValueMotion& Motion, FWidgetAnimValueState& State, float DeltaTime);

	/** A value of one number, as every channel. */
	inline FVector4 FromFloat(double Value) { return FVector4(Value, Value, Value, Value); }
	inline FVector4 FromVector2D(const FVector2D& Value) { return FVector4(Value.X, Value.Y, 0.0, 1.0); }
	inline FVector4 FromColor(const FLinearColor& Value) { return FVector4(Value.R, Value.G, Value.B, Value.A); }
}
