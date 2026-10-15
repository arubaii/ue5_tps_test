// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEvaluator.h"
#include "NameTBD/WidgetAnimation/WidgetAnimInput.h"
#include "NameTBD/Widgets/Constraints/WidgetConstraints.h"
#include "NameTBD/Widgets/Pointer/PointerEffects.h"

#include "WidgetAnimInteractionTracks.generated.h"

/*
 * The interaction tracks, listed under Interaction in the Add Track menu: Widget Animations (timelines, state machines, value
 * tracks fed by a Value Input) driving a widget's Constraints and Pointer Effects - a ball sent round a ring (Path Position 0
 * to 1), a constraint faded in as a menu opens (Strength), a leash lengthened (Distance), a card's sheen swept across it on
 * reveal (Pointer X), a gamepad-selected card shown hovered (Hover). Played on the Track Group's widget, or the first widget
 * inside it that has the component.
 *
 * Keyed places (a look target, a pointer sweeping a card) are keyed on a Constraint Target widget's Transform track instead -
 * 2D keys, motion path, gizmo - with constraints (or Pointer Effects' Pointer Source) tied to it: see ConstraintTargetWidget.h.
 */

/** Constraint: animates any number of a widget's constraint - Path Position, Strength, Distance, Offset X, Copy Amount, Radius... */
USTRUCT(BlueprintType, meta = (DisplayName = "Constraint"))
struct NAMETBD_API FWidgetAnimConstraintParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** The constraint: its Name on the widget's Constraints, or - for one left unnamed - its kind's name as the Details show it ("Follow Path"). None: the first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint", meta = (DisplayName = "Constraint", GetOptions = "WidgetConstraintComponent.GetConstraintTrackNameOptions"))
	FName ConstraintName;

	/**
	 * Which of its numbers the track animates, named as its Details show it: "Path Position", "Strength", "Distance", "Offset X",
	 * "Copy Amount X", "Radius X", "Target Point Y", "Elasticity" - a switch ("Orient", "Enabled") is on above 0.5. A new track
	 * picks its constraint's main number (a Follow Path's Path Position, one lap). None: Channel.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint", meta = (DisplayName = "Value", GetOptions = "WidgetConstraintComponent.GetConstraintTrackValueOptions"))
	FName ValueName;

	/** What it animates while Value is None (tracks made before Value). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint", meta = (ShowWhen = "ValueName=None"))
	EWidgetConstraintChannel Channel = EWidgetConstraintChannel::Strength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint", meta = (DisplayName = "Start Value"))
	double StartValue = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint", meta = (DisplayName = "End Value"))
	double EndValue = 1.0;
};

/**
 * What a Pointer Effects track animates: a setting of the effects, or part of the pointer they see - so an animation can show the
 * effects off with no one at the mouse (a sheen swept across a card as it is dealt, a card tilting as it is revealed, a gamepad
 * selection shown hovered, a scripted click with its ripple). While Pointer X or Y is animated the other axis, not animated, is
 * the middle; Hover and Press not animated follow the pointer as measured.
 */
UENUM(BlueprintType)
enum class EWidgetAnimPointerEffectsValue : uint8
{
	Setting UMETA(DisplayName = "Effect Setting", ToolTip = "A setting of the effects (Channel): Intensity, Tilt Angle, Magnet Pull or Light Strength."),
	PointerX UMETA(DisplayName = "Pointer X", ToolTip = "Where the pointer is across the widget: 0 its left edge, 1 its right (past them: outside). While animated the effects see it there, hovering."),
	PointerY UMETA(DisplayName = "Pointer Y", ToolTip = "Where the pointer is down the widget: 0 its top edge, 1 its bottom."),
	Hover UMETA(ToolTip = "How hovered the widget is, 0 to 1 - its tilt, lift, lights and overlays show as much (a selected card shown hovered)."),
	Press UMETA(ToolTip = "Pressed above 0.5: it sinks, and each time the value rises past 0.5 a ripple spreads from the pointer - a scripted click.")
};

/** Pointer Effects: animates a setting of a widget's Pointer Effects, or the pointer they see (Pointer X / Y, Hover, Press). */
USTRUCT(BlueprintType, meta = (DisplayName = "Pointer Effects"))
struct NAMETBD_API FWidgetAnimPointerEffectsParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** A setting (Channel), or part of the pointer the effects see. A new track starts on Pointer X, a sweep across. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pointer Effects", meta = (DisplayName = "Animates"))
	EWidgetAnimPointerEffectsValue Animates = EWidgetAnimPointerEffectsValue::Setting;

	/** The setting, while Animates is Effect Setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pointer Effects", meta = (ShowWhen = "Animates=Setting"))
	EPointerEffectsChannel Channel = EPointerEffectsChannel::Intensity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pointer Effects", meta = (DisplayName = "Start Value"))
	double StartValue = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pointer Effects", meta = (DisplayName = "End Value"))
	double EndValue = 1.0;
};

/** What an interaction track's value was before it was animated. */
USTRUCT()
struct NAMETBD_API FWidgetAnimInteractionBaseline
{
	GENERATED_BODY()

	UPROPERTY()
	float Value = 0.f;

	/** Which value it was: the constraint's name and the channel (EWidgetConstraintChannel or EPointerEffectsChannel). */
	UPROPERTY()
	FName ConstraintName;

	UPROPERTY()
	uint8 Channel = 0;

	/** Constraint: the number it was, by name (Value), when the track names one. */
	UPROPERTY()
	FName ValueName;

	/** Pointer Effects: part of the pointer (EWidgetAnimPointerEffectsValue) rather than a setting - and whether it was animated already. */
	UPROPERTY()
	uint8 PointerPart = 0;

	UPROPERTY()
	bool bWasSet = true;
};

class NAMETBD_API FWidgetAnimConstraintEvaluator : public TWidgetAnimEvaluator<FWidgetAnimConstraintParams>
{
public:
	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FText GetCategory() const override;

	/** A path position or a fade runs at the pace of its keys: a new track starts Linear, not Ease Out (two laps eased out ran most of the way in the first moments). */
	virtual bool PrefersLinearEasing() const override { return true; }
	virtual UObject* ResolveTarget(UWidget* Widget, const FInstancedStruct& Params) const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override;
	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override;
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;
	virtual bool CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const override;
	virtual void RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const override;
	virtual FText GetTrackSummary(const FInstancedStruct& Params) const override;
	virtual void InitializeNewTrack(FInstancedStruct& Params, UWidget* Widget) const override;
};

class NAMETBD_API FWidgetAnimPointerEffectsEvaluator : public TWidgetAnimEvaluator<FWidgetAnimPointerEffectsParams>
{
public:
	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FText GetCategory() const override;
	virtual UObject* ResolveTarget(UWidget* Widget, const FInstancedStruct& Params) const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override;
	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override;
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;
	virtual bool CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const override;
	virtual void RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const override;
	virtual FText GetTrackSummary(const FInstancedStruct& Params) const override;
	virtual void InitializeNewTrack(FInstancedStruct& Params, UWidget* Widget) const override;
};

// =====================================================================================================
// Value input
// =====================================================================================================

/** What a Pointer Distance and Motion input measures. */
UENUM(BlueprintType)
enum class EWidgetAnimPointerMeasure : uint8
{
	Distance UMETA(ToolTip = "How far the pointer is from the widget, in Slate units: from its edges (0 over it) or its middle (From Edges off)."),
	Proximity UMETA(ToolTip = "How near the pointer is: 1 over the widget (or at its middle), falling to 0 at Range. An icon growing as the pointer nears - a dock - a hint fading in."),
	Direction UMETA(ToolTip = "Which way the pointer is from the widget's middle, in degrees clockwise from right (-180 to 180): a needle or an eye turning to it."),
	Speed UMETA(ToolTip = "How fast the pointer moves, in Slate units a second: a trail stretching, a wobble while it moves."),
	VelocityX UMETA(DisplayName = "Velocity X", ToolTip = "How fast it moves across, in Slate units a second (negative: leftward) - a card swaying as it is swept past."),
	VelocityY UMETA(DisplayName = "Velocity Y", ToolTip = "How fast it moves down, in Slate units a second (negative: upward).")
};

/** How far and which way the pointer is from the widget, how near, and how fast it moves. */
USTRUCT(BlueprintType, meta = (DisplayName = "Pointer Distance and Motion"))
struct NAMETBD_API FWidgetAnimInput_PointerMotion : public FWidgetAnimInputSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EWidgetAnimPointerMeasure Measure = EWidgetAnimPointerMeasure::Proximity;

	/** Proximity: how far from the widget the pointer counts as near at all, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (ClampMin = "1", UIMax = "800"))
	float Range = 200.f;

	/** Distance and Proximity: measured from the widget's edges (0 / 1 anywhere over it) rather than its middle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bFromEdges = true;

	virtual bool Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue) override;
	virtual FString GetSummary() const override;

private:
	FVector2f LastLocal = FVector2f::ZeroVector;
	FVector2f Velocity = FVector2f::ZeroVector;
	bool bHasLast = false;
};
