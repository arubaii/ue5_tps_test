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
 * tracks fed by a Value Input) driving a widget's Constraints and Pointer Effects - a ball sent once round a ring (Path
 * Position 0 to 1), a constraint faded in as a menu opens (Strength), a card's tilt switched on in one state and off in
 * another (Intensity). Played on the Track Group's widget, or the first widget inside it that has the component.
 */

/** Constraint: animates one value of a widget's constraint - Strength, Path Position, Offset, Angle Offset or Distance. */
USTRUCT(BlueprintType, meta = (DisplayName = "Constraint"))
struct NAMETBD_API FWidgetAnimConstraintParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** The constraint's Name on the widget's Constraints. None: the first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint", meta = (DisplayName = "Constraint"))
	FName ConstraintName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint")
	EWidgetConstraintChannel Channel = EWidgetConstraintChannel::Strength;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint", meta = (DisplayName = "Start Value"))
	double StartValue = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Constraint", meta = (DisplayName = "End Value"))
	double EndValue = 1.0;
};

/** Pointer Effects: animates one value of a widget's Pointer Effects - Intensity, Tilt Angle, Magnet Pull or Light Strength. */
USTRUCT(BlueprintType, meta = (DisplayName = "Pointer Effects"))
struct NAMETBD_API FWidgetAnimPointerEffectsParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pointer Effects")
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
};

class NAMETBD_API FWidgetAnimConstraintEvaluator : public TWidgetAnimEvaluator<FWidgetAnimConstraintParams>
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
