// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Widgets/SLeafWidget.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeTypes.h"
#include "NameTBD/Widgets/Shapes/CurveShape.h"
#include "NameTBD/Widgets/WidgetEffectImage.h"
#include "NameTBD/Widgets/WidgetMotion.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"

#include "AdvancedProgressBar.generated.h"

/** The shape of an Advanced Progress Bar. */
UENUM(BlueprintType)
enum class EProgressBarShape : uint8
{
	Bar UMETA(ToolTip = "A straight bar, across or up (Fill Direction)."),
	Ring UMETA(ToolTip = "A ring or an arc: a full circle, or a gauge with Sweep under 360."),
	Curve UMETA(ToolTip = "Along a line of your own: a Curve's ready-made line, a Curve Preset asset, or a Curve widget in this Widget Blueprint - a swoosh, an S, a winding path, a loop. Fills from the line's first point.")
};

/** Where a Curve-shaped Advanced Progress Bar's line comes from. */
UENUM(BlueprintType)
enum class EProgressCurveSource : uint8
{
	Template UMETA(DisplayName = "Ready-Made Curve", ToolTip = "One of the Curve's ready-made lines (Curve Template)."),
	Preset UMETA(DisplayName = "Curve Preset", ToolTip = "A Curve Preset asset's line (saved from any Curve with Save Preset) - shared by every bar that picks it."),
	Widget UMETA(DisplayName = "Curve Widget", ToolTip = "A Curve widget in this Widget Blueprint, drawn in the bar's own box: edit its line on the canvas (Edit Shape, Pencil) and the bar follows it. Hide the Curve, or keep it as a guide.")
};

/** Which way a bar fills. */
UENUM(BlueprintType)
enum class EProgressFillDirection : uint8
{
	LeftToRight UMETA(DisplayName = "Left to Right"),
	RightToLeft UMETA(DisplayName = "Right to Left"),
	BottomToTop UMETA(DisplayName = "Bottom to Top"),
	TopToBottom UMETA(DisplayName = "Top to Bottom"),
	FromCenter UMETA(DisplayName = "From Center", ToolTip = "Grows out from the middle both ways.")
};

/** The leading edge of a bar's fill. */
UENUM(BlueprintType)
enum class EProgressFillEnd : uint8
{
	Rounded UMETA(ToolTip = "The fill is a rounded bar of its own, its leading edge as round as the track's ends; under a full rounding it grows out of the track's end."),
	Flat UMETA(ToolTip = "The fill is the track cut straight across where it ends.")
};

/** How a ring's arcs end. */
UENUM(BlueprintType)
enum class EProgressRingCap : uint8
{
	Round,
	Flat
};

/** What a gradient fill runs along. */
UENUM(BlueprintType)
enum class EProgressGradientSpan : uint8
{
	Track UMETA(DisplayName = "Along Track", ToolTip = "The gradient runs from the start of the track to its end (round a ring), uncovered as the bar fills: a heat bar, a ring going green to red."),
	Fill UMETA(DisplayName = "Along Fill", ToolTip = "The whole gradient is squeezed into what is filled, so its last color is always at the leading edge."),
	Box UMETA(DisplayName = "Over Box", ToolTip = "Laid over the widget as a shape's paint is: the Fill's own Gradient Angle / Center.")
};

/** How an Advanced Progress Bar looks. */
USTRUCT(BlueprintType)
struct NAMETBD_API FAdvancedProgressBarStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	EProgressBarShape Shape = EProgressBarShape::Bar;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Bar", EditConditionHides))
	EProgressFillDirection Direction = EProgressFillDirection::LeftToRight;

	/** The size it asks for: X its length, Y its thickness (a vertical bar is turned). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Bar", EditConditionHides))
	FVector2D BarSize = FVector2D(240.0, 10.0);

	/** The ends as round as they go. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Fully Rounded", EditCondition = "Shape == EProgressBarShape::Bar", EditConditionHides))
	bool bFullyRounded = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Bar && !bFullyRounded", EditConditionHides, ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float CornerRadius = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Bar", EditConditionHides))
	EProgressFillEnd FillEnd = EProgressFillEnd::Rounded;

	/** The fill drawn inside the track by this much all round - a fill floating in its track. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0", UIMax = "8", ForceUnits = "px"))
	float FillInset = 0.f;

	/** The ring's size it asks for (its outer edge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Ring", EditConditionHides, ClampMin = "8", UIMax = "512", ForceUnits = "px"))
	float RingDiameter = 72.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Ring", EditConditionHides, ClampMin = "1", UIMax = "64", ForceUnits = "px"))
	float RingThickness = 8.f;

	/** Where the ring starts, in degrees: 0 the top, 90 the right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Ring", EditConditionHides, UIMin = "-180", UIMax = "180", ForceUnits = "Degrees"))
	float StartAngle = 0.f;

	/** How far round it goes, in degrees: 360 a full ring, 270 a gauge (start it at -135). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Ring", EditConditionHides, ClampMin = "10", ClampMax = "360", ForceUnits = "Degrees"))
	float Sweep = 360.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Ring", EditConditionHides))
	bool bClockwise = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Ring", EditConditionHides))
	EProgressRingCap RingCap = EProgressRingCap::Round;

	/** Curve: where its line comes from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Curve", EditConditionHides))
	EProgressCurveSource CurveSource = EProgressCurveSource::Template;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Curve && CurveSource == EProgressCurveSource::Template", EditConditionHides))
	EWidgetCurveTemplate CurveTemplate = EWidgetCurveTemplate::Swoosh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Curve && CurveSource == EProgressCurveSource::Preset", EditConditionHides))
	TObjectPtr<UWidgetCurvePresetAsset> CurvePreset = nullptr;

	/** The Curve widget whose line the bar fills along (drawn in the bar's own box). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (GetOptions = "GetWidgetOptions", EditCondition = "Shape == EProgressBarShape::Curve && CurveSource == EProgressCurveSource::Widget", EditConditionHides))
	FName CurveWidget;

	/** The size it asks for; the line is laid in it, kept clear of its edges by half the thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Curve", EditConditionHides))
	FVector2D CurveSize = FVector2D(240.0, 80.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Curve", EditConditionHides, ClampMin = "1", UIMax = "64", ForceUnits = "px"))
	float CurveThickness = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Curve", EditConditionHides))
	EProgressRingCap CurveCap = EProgressRingCap::Round;

	/**
	 * Flat ends cut at a slant, in degrees from square (clockwise) - every end: the track's, each segment's and the fill's
	 * leading edge. Slanted segments read as chevrons; a slanted leading edge as a sci-fi gauge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "Shape == EProgressBarShape::Curve && CurveCap == EProgressRingCap::Flat", EditConditionHides, ClampMin = "-80", ClampMax = "80", UIMin = "-60", UIMax = "60", ForceUnits = "Degrees"))
	float CurveCapAngle = 0.f;

	/** Fills from the line's last point instead of its first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Reverse Curve", EditCondition = "Shape == EProgressBarShape::Curve", EditConditionHides))
	bool bReverseCurve = false;

	/** The filled part: a color, a gradient (see Gradient Span), an image, an SVG or a material (handed Progress, 0 to 1, as it fills). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fill")
	FWidgetShapePaint Fill;

	/** For a gradient Fill: what it runs along. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fill")
	EProgressGradientSpan GradientSpan = EProgressGradientSpan::Track;

	/** The fill's color follows the value (Value Colors at the percent shown) - health going from green to red. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fill")
	bool bColorByValue = false;

	/** The color at each value, 0 empty to 1 full. Tints a gradient or image Fill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fill", meta = (EditCondition = "bColorByValue", EditConditionHides))
	TArray<FWidgetShapeGradientStop> ValueColors;

	/** The empty part under the fill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track")
	FWidgetShapePaint Track;

	/** An outline round the track, in Slate units, inside it. 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track", meta = (ClampMin = "0", UIMax = "4", ForceUnits = "px"))
	float TrackOutline = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Track", meta = (EditCondition = "TrackOutline > 0", EditConditionHides))
	FLinearColor TrackOutlineColor = FLinearColor(1.f, 1.f, 1.f, 0.15f);

	/** Pieces the bar (or ring) is split into: 1 is one bar; 5 a battery, 10 the pips of a health bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segments", meta = (ClampMin = "1", ClampMax = "100"))
	int32 Segments = 1;

	/** The space between segments, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segments", meta = (EditCondition = "Segments > 1", EditConditionHides, ClampMin = "0", UIMax = "16", ForceUnits = "px"))
	float SegmentGap = 3.f;

	/** Only whole segments light: a segment fills once the value reaches its end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segments", meta = (EditCondition = "Segments > 1", EditConditionHides))
	bool bWholeSegments = false;

	/**
	 * A trail behind changes: a loss leaves the lost part showing in Loss Color for Trail Delay, then drains away; a gain shows
	 * at once in Gain Color and the fill catches up after Trail Delay - fighting-game health bars.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail")
	bool bTrail = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (EditCondition = "bTrail", EditConditionHides))
	FLinearColor LossColor = FLinearColor(1.f, 0.32f, 0.28f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (EditCondition = "bTrail", EditConditionHides))
	FLinearColor GainColor = FLinearColor(0.45f, 1.f, 0.62f, 0.75f);

	/** Seconds the trail waits before it follows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (EditCondition = "bTrail", EditConditionHides, ClampMin = "0", UIMax = "2", Units = "s"))
	float TrailDelay = 0.45f;

	/** Seconds a loss trail takes to drain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (EditCondition = "bTrail", EditConditionHides, ClampMin = "0.05", UIMax = "2", Units = "s"))
	float TrailDuration = 0.5f;

	/** A soft glow round the fill, in its own color: how strong. 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (ClampMin = "0", ClampMax = "1"))
	float Glow = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (EditCondition = "Glow > 0", EditConditionHides, ClampMin = "1", UIMax = "48", ForceUnits = "px"))
	float GlowSize = 10.f;

	/** A band of light sweeping along the fill now and then. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	bool bShine = false;

	/** Seconds between sweeps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (EditCondition = "bShine", EditConditionHides, ClampMin = "0.5", UIMax = "10", Units = "s"))
	float ShineInterval = 2.4f;

	/** The band's width, as a fraction of the track. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (EditCondition = "bShine", EditConditionHides, ClampMin = "0.02", ClampMax = "1"))
	float ShineWidth = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (EditCondition = "bShine", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float ShineOpacity = 0.35f;

	/** Bar: diagonal stripes moving along the fill - loading, charging, in progress. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (EditCondition = "Shape == EProgressBarShape::Bar", EditConditionHides))
	bool bStripes = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (EditCondition = "Shape == EProgressBarShape::Bar && bStripes", EditConditionHides, ClampMin = "2", UIMax = "32", ForceUnits = "px"))
	float StripeWidth = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (EditCondition = "Shape == EProgressBarShape::Bar && bStripes", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float StripeOpacity = 0.15f;

	/** Slate units a second; negative runs them backward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects", meta = (EditCondition = "Shape == EProgressBarShape::Bar && bStripes", EditConditionHides, UIMin = "-100", UIMax = "100"))
	float StripeSpeed = 24.f;

	/** Ticks at these values (0 to 1): thresholds, checkpoints, a quarter of a health bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers")
	TArray<float> Markers;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers")
	FLinearColor MarkerColor = FLinearColor(0.f, 0.f, 0.f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers", meta = (ClampMin = "0.5", UIMax = "6", ForceUnits = "px"))
	float MarkerWidth = 2.f;

	FAdvancedProgressBarStyle();
};

DECLARE_DELEGATE_OneParam(FOnProgressReachedEnd, bool /*bFull*/);

/** The Slate side of an Advanced Progress Bar. */
class NAMETBD_API SAdvancedProgressBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAdvancedProgressBar) {}
		SLATE_EVENT(FOnProgressReachedEnd, OnReachedEnd)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetBarStyle(const FAdvancedProgressBarStyle& InStyle);
	void SetMotion(EWidgetMotion InMotion, float InEnterDuration, float InExitDuration);
	void SetPercent(float InPercent, bool bAnimate);

	/** Read every frame while bound (a binding in the designer). */
	void SetPercentSource(TAttribute<float> InSource);

	/** The Curve a Curve-shaped bar fills along (its line is drawn in the bar's box). */
	void SetCurve(const UCurveShape* InCurve);
	void SetIndeterminate(bool bInIndeterminate);

	float GetDisplayedPercent() const { return FillValue.Value; }

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void Wake();
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);
	bool NeedsClock() const;

	int32 PaintBar(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;
	int32 PaintRing(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;
	int32 PaintCurve(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;

	/** The live values a Material paint is handed: Progress (what is shown), ShapeSize. */
	FWidgetEffectMaterialInputs MakeMaterialInputs(const FVector2f& Size) const;

	/** The fill's paint, its color taken from the value when Color By Value. */
	FWidgetShapePaint MakeFillPaint(float Shown) const;

	/** Where the indeterminate segment is now, in 0..1 along the track. */
	void GetIndeterminateSpan(float& OutStart, float& OutEnd) const;

	FAdvancedProgressBarStyle BarStyle;
	EWidgetMotion Motion = EWidgetMotion::Smooth;
	float EnterDuration = 0.f;
	float ExitDuration = 0.f;
	FOnProgressReachedEnd OnReachedEnd;
	TAttribute<float> PercentSource;
	bool bIndeterminate = false;

	float Target = 0.f;
	FWidgetMotionValue FillValue;
	FWidgetMotionValue GhostValue;
	bool bGain = false;
	float Hold = 0.f;
	double Clock = 0.0;
	TSharedPtr<FActiveTimerHandle> AnimateTimer;

	mutable FWidgetSVGBrush FillSVG;
	mutable FWidgetSVGBrush TrackSVG;
	mutable uint32 TrackHash = 0;
	mutable bool bTrackBuilt = false;
	mutable FWidgetShapeMesh TrackMesh;
	mutable FWidgetShapeMesh TrackOutlineMesh;

	/** A Material Fill (key 1) or Track (key 0) paint's own instances. */
	mutable FWidgetEffectMaterials Materials;

	/** Curve: the line it fills along, and its points in local space as last built (and for what). */
	TWeakObjectPtr<const UCurveShape> Curve;
	mutable TArray<FVector2f> CurvePoints;
	mutable float CurveLength = 0.f;
	mutable bool bCurveClosed = false;
	mutable uint32 CurveHash = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAdvancedProgressChanged, float, NewPercent, float, OldPercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAdvancedProgressEvent);

/**
 * Advanced Progress Bar: a progress bar or ring for health, stamina, loading, XP, timers and gauges. A straight bar (any
 * direction, or growing from the middle), a ring / arc gauge, or along a curve of your own (a ready-made line, a Curve Preset
 * or a Curve widget, flat ends optionally slanted); rounded or flat leading edge; segments (a battery, health
 * pips); a trail that shows what was just lost or gained; gradients along the track or the fill, an image or SVG, a color that
 * follows the value; glow, a sweeping shine, moving stripes, markers; an indeterminate (busy) mode. Every change is animated
 * with its Motion. Set Percent as a Progress Bar's; vector-drawn, sharp at any size.
 */
UCLASS(meta = (DisplayName = "Advanced Progress Bar"))
class NAMETBD_API UAdvancedProgressBar : public UWidget
{
	GENERATED_BODY()

public:
	UAdvancedProgressBar(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** How full, 0 to 1. Changes animate (Motion); in the designer it shows at once. */
	UPROPERTY(EditAnywhere, BlueprintSetter = SetPercent, Category = "Progress", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Percent = 0.65f;

	UPROPERTY()
	FGetFloat PercentDelegate;

	/** Busy, with no value to show: a segment sweeps along the bar (or round the ring). */
	UPROPERTY(EditAnywhere, BlueprintSetter = SetIndeterminate, Category = "Progress", meta = (DisplayName = "Indeterminate"))
	bool bIndeterminate = false;

	/** How the fill moves to a new value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progress")
	EWidgetMotion Motion = EWidgetMotion::Smooth;

	/** Seconds to rise to a higher value. 0: Motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progress", meta = (EditCondition = "Motion != EWidgetMotion::Instant", EditConditionHides, ClampMin = "0", UIMax = "3", Units = "s"))
	float EnterDuration = 0.35f;

	/** Seconds to fall to a lower value. 0: Motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progress", meta = (EditCondition = "Motion != EWidgetMotion::Instant", EditConditionHides, ClampMin = "0", UIMax = "3", Units = "s"))
	float ExitDuration = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FAdvancedProgressBarStyle BarStyle;

	/** Set Percent changed the value. */
	UPROPERTY(BlueprintAssignable, Category = "Progress")
	FOnAdvancedProgressChanged OnPercentChanged;

	/** The fill reached full. */
	UPROPERTY(BlueprintAssignable, Category = "Progress")
	FOnAdvancedProgressEvent OnFilled;

	/** The fill reached empty. */
	UPROPERTY(BlueprintAssignable, Category = "Progress")
	FOnAdvancedProgressEvent OnEmptied;

	/** Sets the value, 0 to 1 - animated by Motion (with the trail). */
	UFUNCTION(BlueprintCallable, Category = "Advanced Progress Bar")
	void SetPercent(float InPercent);

	/** Sets the value at once: no animation, no trail. */
	UFUNCTION(BlueprintCallable, Category = "Advanced Progress Bar")
	void SnapToPercent(float InPercent);

	UFUNCTION(BlueprintPure, Category = "Advanced Progress Bar")
	float GetPercent() const { return Percent; }

	/** The value drawn now, while the fill is still moving to Percent. */
	UFUNCTION(BlueprintPure, Category = "Advanced Progress Bar")
	float GetDisplayedPercent() const;

	UFUNCTION(BlueprintCallable, Category = "Advanced Progress Bar")
	void SetIndeterminate(bool bInIndeterminate);

	UFUNCTION(BlueprintCallable, Category = "Advanced Progress Bar")
	void SetBarStyle(const FAdvancedProgressBarStyle& InStyle);

	UFUNCTION(BlueprintPure, Category = "Advanced Progress Bar")
	FAdvancedProgressBarStyle GetBarStyle() const { return BarStyle; }

	/** The fill as one color. */
	UFUNCTION(BlueprintCallable, Category = "Advanced Progress Bar")
	void SetFillColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "Advanced Progress Bar")
	void SetSegments(int32 InSegments, float InGap = 3.f);

	UFUNCTION(BlueprintCallable, Category = "Advanced Progress Bar")
	void SetMarkers(const TArray<float>& InMarkers);

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;

	// UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void HandleReachedEnd(bool bFull);

	/** Percent through its binding, when it has one. */
	float GetBoundPercent() const;

	/** The Curve a Curve-shaped bar fills along: the named Curve widget, or one made here from the ready-made line or preset. */
	const UCurveShape* ResolveCurve();

	/** The widgets in this Widget Blueprint, for Curve Widget. */
	UFUNCTION()
	TArray<FString> GetWidgetOptions() const;

	TSharedPtr<SAdvancedProgressBar> MyBar;

	/** The Curve made for a ready-made line or a Curve Preset (never in a tree, never drawn itself). */
	UPROPERTY(Transient)
	TObjectPtr<UCurveShape> MadeCurve;
};
