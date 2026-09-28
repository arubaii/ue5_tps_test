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
// value track has a Value Name; gameplay sets that name's value (Set Widget Animation Value) and the track travels
// there the way its Value Smoothing says. The rest of the animation plays on its clock as usual, so one animation
// can spin an icon on its clock while its bar follows a value. The value is typed - a float, an integer, a 2D
// vector or a colour - and each channel of it is mapped onto the track through Value At Start and Value At End:
// set those to 0 and 100 and a health of 25 puts the track a quarter of the way from its start to its end.
//
// Smoothing is authored on the track. What a use of the animation may change - where its value starts, the range,
// whether it may go backwards, how fast it may move, what it does on exit - is FWidgetAnimValueOptions, authored on
// the track as its defaults and overridable per widget on the Widget Animation Component.
//
// The value is carried as a fraction of the track - its progress, 0 at the start and 1 at the end, one per channel -
// from the moment it arrives, so smoothing, Never Go Backwards and Minimum Time To Fill mean the same on any range.
// In every preview a value track sweeps once across its Duration on the clock, so it can be judged like any other.

/** What kind of value a value-driven track takes. Each channel is mapped onto the track on its own. */
UENUM(BlueprintType)
enum class EWidgetAnimValueKind : uint8
{
	Float UMETA(ToolTip = "One number. Set with Set Widget Animation Value."),

	Integer UMETA(ToolTip = "One whole number - a count, a level. Set with Set Widget Animation Value (Integer). Smoothing still passes through the numbers in between, which a Count Up shows as it counts."),

	Vector2D UMETA(DisplayName = "Vector 2D", ToolTip = "Two numbers, each moving its own axis: X and Y of a Scale separately, X and Y of anything with two axes. A track with one axis uses X. Set with Set Widget Animation Value (Vector 2D)."),

	Color UMETA(DisplayName = "Colour", ToolTip = "Four numbers, R G B A, each moving its own channel of a Material Vector track. A track with one channel uses R. Set with Set Widget Animation Value (Colour).")
};

/** How a value-driven track travels to each new value it is given. Authored on the track. */
UENUM(BlueprintType)
enum class EWidgetAnimValueSmoothing : uint8
{
	Immediate UMETA(ToolTip = "Jumps to each new value. For values that already move smoothly, such as a slider's position."),

	Tween UMETA(ToolTip = "Eases to each new value over the track's Duration, by its Easing. Every new value starts a new ease from wherever it had got to."),

	Spring UMETA(ToolTip = "Chases each new value with the track's spring, keeping its speed when the value changes again mid-way. What makes jumpy values, such as load progress arriving in chunks, look smooth.")
};

/** What a value-driven track does when its widget plays its exit while its value is still moving. */
UENUM(BlueprintType)
enum class EWidgetAnimValueExit : uint8
{
	ExitRightAway UMETA(ToolTip = "The exit plays at once, wherever the value has got to."),

	WaitForValue UMETA(DisplayName = "Wait For Value", ToolTip = "The exit waits until the track has arrived at the last value set - a bar gliding to 0.8 gets there first."),

	CompleteFirst UMETA(DisplayName = "Complete First", ToolTip = "The track is sent to its end and the exit waits until it gets there - a loading bar always fills before the screen closes, even if the last value set was 90%.")
};

/**
 * How a value-driven track takes its value: the range mapped onto it, where it starts, and how it may move. Authored
 * on the track as its defaults (Timing = Value), and overridable per widget in the Widget Animation Component's Value
 * Tracks - everything except the smoothing, which belongs to the motion. See the note at the top of WidgetAnimDriver.h.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimValueOptions
{
	GENERATED_BODY()

	/** The kind of value the track takes. Chosen on the track; a widget's override shows it but can't change it. */
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "Start Value", EditCondition = "Kind == EWidgetAnimValueKind::Vector2D", EditConditionHides))
	FVector2D StartValue2D = FVector2D(0.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Value", meta = (DisplayName = "Start Value", EditCondition = "Kind == EWidgetAnimValueKind::Color", EditConditionHides))
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

	/** 1 for Float and Integer, 2 for Vector 2D, 4 for Colour. */
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
 * One row of a Widget Animation Component's Value Tracks: a Value Name its animations use, and - with Override on -
 * this widget's own settings for it in place of the track's.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimValueOverride
{
	GENERATED_BODY()

	/** The Value Name, as the tracks give it. Set Widget Animation Value with this name. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Value")
	FName ValueName;

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

	/** True while it is still travelling toward its target. */
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