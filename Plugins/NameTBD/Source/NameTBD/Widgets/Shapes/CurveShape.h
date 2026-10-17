// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "ShapeWidgets.h"
#include "NameTBD/Widgets/Paths/WidgetPathTypes.h"

#include "CurveShape.generated.h"

class UCurveShape;

/*
 * The Curve: a line of your own - a swoosh under a heading, a hand-drawn arrow pointing at a button, a divider, a wave, a
 * tutorial's path across the screen, a sparkline plotting values, a signature drawn live. It is a Custom Shape made for lines:
 * open, stroked, unfilled, smoothed through its points, with arrowheads, a taper and a gradient along it, and saved as a Curve
 * Preset asset any other curve can follow. Everything a shape has comes with it: the canvas path editor (Edit Shape), the
 * Details' shape presets, Trim Path (draw on), dashes that march, a material laid along it, Shape tracks in Widget Animations.
 */

/** How a Curve runs through its points. */
UENUM(BlueprintType)
enum class EWidgetCurveSmoothing : uint8
{
	Smooth UMETA(DisplayName = "Smooth Through Points", ToolTip = "A smooth curve through every point, its handles worked out for you (Smoothness sets how round): place points and the curve flows through them. A Disconnected point stays a sharp corner."),

	Handles UMETA(DisplayName = "Handles As Drawn", ToolTip = "Each point's own Bezier handles, as a Custom Shape draws them: drag them on the canvas for full control."),

	Straight UMETA(DisplayName = "Straight Lines", ToolTip = "Straight lines from point to point (a zigzag, a chart, a step), each corner rounded by its Corner Radius.")
};

/**
 * How Smooth Through Points works the curve out between its points - the interpolations production tools offer. Each is
 * worked out at the curve's drawn proportions (a stretched widget doesn't bend it). Balanced is what a Curve placed before
 * these existed keeps; a Curve placed from the palette now starts with Catmull-Rom.
 */
UENUM(BlueprintType)
enum class EWidgetCurveInterpolation : uint8
{
	Balanced UMETA(DisplayName = "Balanced", ToolTip = "Each point's handles along the line between its neighbours, a third of the way to each: quick and predictable, can kink where points bunch up. What Curves made before the other interpolations keep."),

	CatmullRom UMETA(DisplayName = "Catmull-Rom", ToolTip = "A Catmull-Rom spline (Alpha 0.5, centripetal, by default): flows through every point, never loops or cusps however the points are spaced, and moving a point only bends the curve near it. The standard for curves through points - start here."),

	Natural UMETA(DisplayName = "Natural (Smoothest)", ToolTip = "A natural cubic spline: the smoothest curve through the points, its curvature continuous everywhere - long, even undulations. Every point pulls on the whole line, so moving one ripples along it a little, and it can swing wide past points that bunch up."),

	Monotone UMETA(DisplayName = "Monotone (Charts)", ToolTip = "For charts and graphs: never rises above or dips below its points between them, so a plotted value never seems to go where it didn't. Points must run one way across (a line that doubles back is smoothed Catmull-Rom instead)."),

	BSpline UMETA(DisplayName = "B-Spline (Soft)", ToolTip = "The points are a control cage the curve is drawn inside: it passes only through each line's ends (and corners) and is pulled toward the points between - the softest, most fluid curve, as Blender's NURBS and the vector tools' B-spline mode.")
};

/**
 * A ready-made curve to start from, for the Details' Start From and Set Curve From Template. A Disconnected point of a
 * smoothed curve is a sharp corner.
 */
UENUM(BlueprintType)
enum class EWidgetCurveTemplate : uint8
{
	Line,
	Arc,
	SCurve UMETA(DisplayName = "S-Curve"),
	Wave,
	Zigzag,
	Spiral,
	Loop,
	Heartbeat,
	Swoosh,
	Bounce,
	Steps,
	Infinity
};

/**
 * A curve of your own, kept as an asset: its points, how it runs through them and - with Apply Look - how it is drawn. Made
 * from a Curve with Save As Curve Preset (its Details), or in the Content Browser (Miscellaneous > Data Asset > Curve Preset).
 * A Curve that picks it takes all of it and follows it: edits to the asset reach every Curve still using it unchanged the next
 * time they load (or at once with Apply Curve Preset), and editing what the preset holds on a Curve lets it go.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Curve Preset"))
class NAMETBD_API UWidgetCurvePresetAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UWidgetCurvePresetAsset();

	/** The points, as fractions of the Curve's box. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve")
	FWidgetShapePath Path;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve")
	EWidgetCurveSmoothing Smoothing = EWidgetCurveSmoothing::Smooth;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve", meta = (EditCondition = "Smoothing == EWidgetCurveSmoothing::Smooth", EditConditionHides))
	EWidgetCurveInterpolation Interpolation = EWidgetCurveInterpolation::Balanced;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve", meta = (ClampMin = "0", ClampMax = "2", UIMin = "0", UIMax = "1"))
	float Smoothness = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve", meta = (DisplayName = "Catmull-Rom Alpha", EditCondition = "Smoothing == EWidgetCurveSmoothing::Smooth && Interpolation == EWidgetCurveInterpolation::CatmullRom", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float CatmullRomAlpha = 0.5f;

	/** Puts the look below on the Curve too (off: only the points, the Curve keeps how it is drawn). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (DisplayName = "Apply Look"))
	bool bApplyLook = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook"))
	FWidgetShapeStroke Stroke;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook"))
	bool bFill = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook && bFill"))
	FWidgetShapePaint Fill;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook"))
	EWidgetPathMarker StartMarker = EWidgetPathMarker::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook"))
	EWidgetPathMarker EndMarker = EWidgetPathMarker::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook", ClampMin = "1", UIMax = "64", ForceUnits = "px"))
	float MarkerSize = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook", ClampMin = "-80", ClampMax = "80", ForceUnits = "Degrees"))
	float StartCapAngle = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook", ClampMin = "-80", ClampMax = "80", ForceUnits = "Degrees"))
	float EndCapAngle = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook", ClampMin = "0", UIMax = "3"))
	float StartWidthScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook", ClampMin = "0", UIMax = "3"))
	float EndWidthScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook", DisplayName = "Use Width Curve"))
	bool bUseWidthCurve = false;

	UPROPERTY(EditAnywhere, Category = "Look", meta = (EditCondition = "bApplyLook && bUseWidthCurve"))
	FRuntimeFloatCurve WidthCurve;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (EditCondition = "bApplyLook", DisplayName = "Gradient Along Curve"))
	bool bGradientAlongCurve = true;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

/**
 * A line of your own, drawn through points you place - edited on the designer's canvas like a Custom Shape, smoothed through
 * them for you, with arrowheads, a taper and a gradient along it.
 *
 * Select it and press Edit Shape: click with the Pen to add points (the curve flows through them), draw freehand with the
 * Pencil, Shift-click anywhere to add a point, drag them, double-click a point to make it a corner, Delete to remove. Start From has ready-made curves (a wave, a spiral, a heartbeat...). Save As Curve Preset
 * keeps it as an asset another Curve can pick and follow; the shape Preset row saves its whole look under a name.
 *
 * Trim End animated from 0 to 1 draws it on, arrowheads riding its ends. From Blueprint: Set Points / Add Point / Plot Values
 * draw it from data (a chart, a signature, a path the player took), and Get Point At Fraction finds where it runs, to move an
 * icon along it. Positions are fractions of the widget's box, so it follows the widget's size; widths stay in Slate units.
 */
UCLASS(meta = (DisplayName = "Curve"))
class NAMETBD_API UCurveShape : public UCustomShape
{
	GENERATED_BODY()

public:
	UCurveShape();

	/** How the curve runs through its points. Smooth Through Points works the handles out for you. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve")
	EWidgetCurveSmoothing Smoothing = EWidgetCurveSmoothing::Smooth;

	/**
	 * Smooth Through Points: how the curve is worked out between its points - Catmull-Rom flows through them and never loops,
	 * Natural is the smoothest, Monotone never overshoots (charts), B-Spline is drawn inside them as a control cage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve", meta = (EditCondition = "Smoothing == EWidgetCurveSmoothing::Smooth", EditConditionHides))
	EWidgetCurveInterpolation Interpolation = EWidgetCurveInterpolation::Balanced;

	/**
	 * Smooth Through Points: how round it runs through each point - every handle's length, as a multiple of what the
	 * interpolation gives. 0 is straight lines, 1 the interpolation's own curve, more swings wider (Monotone and B-Spline stop at 1).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Curve", meta = (EditCondition = "Smoothing == EWidgetCurveSmoothing::Smooth", EditConditionHides, ClampMin = "0", ClampMax = "2", UIMin = "0", UIMax = "1"))
	float Smoothness = 1.f;

	/**
	 * Catmull-Rom: how the points' spacing shapes the curve. 0.5 (centripetal) never loops or cusps; 0 (uniform) treats every
	 * stretch alike and can loop where points bunch up; 1 (chordal) turns wider and rounder.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve", meta = (DisplayName = "Catmull-Rom Alpha", EditCondition = "Smoothing == EWidgetCurveSmoothing::Smooth && Interpolation == EWidgetCurveInterpolation::CatmullRom", EditConditionHides, ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float CatmullRomAlpha = 0.5f;

	/** A mark at the curve's first point - at the start of the part drawn, with a Trim Path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve|Ends")
	EWidgetPathMarker StartMarker = EWidgetPathMarker::None;

	/** A mark at the curve's last point - an arrowhead shows which way it points, and rides the end of a Trim Path drawing it on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve|Ends")
	EWidgetPathMarker EndMarker = EWidgetPathMarker::None;

	/**
	 * Slants the start of the line, in degrees from square across it (clockwise): the flat end cut at an angle - a speed line, a
	 * slanted bar, a ribbon's notch. The same angle at both ends slants them in parallel; opposite angles taper the line.
	 * Applies to a Butt or Square cap where the end has no mark (a Round cap stays round).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Curve|Ends", meta = (ClampMin = "-80", ClampMax = "80", UIMin = "-60", UIMax = "60", ForceUnits = "Degrees"))
	float StartCapAngle = 0.f;

	/** Slants the end of the line, in degrees from square across it (clockwise). See Start Cap Angle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Curve|Ends", meta = (ClampMin = "-80", ClampMax = "80", UIMin = "-60", UIMax = "60", ForceUnits = "Degrees"))
	float EndCapAngle = 0.f;

	/** How big the marks are, in Slate units. Painted with the stroke. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Curve|Ends", meta = (ClampMin = "1", UIMin = "2", UIMax = "64", ForceUnits = "px"))
	float MarkerSize = 12.f;

	/** The stroke's width at the first point, as a multiple of its Width (a straight blend to End Width Scale) - a brush stroke. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Curve|Width", meta = (ClampMin = "0", UIMin = "0", UIMax = "3"))
	float StartWidthScale = 1.f;

	/** The stroke's width at the last point, as a multiple of its Width: 0 tapers it to a point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Curve|Width", meta = (ClampMin = "0", UIMin = "0", UIMax = "3"))
	float EndWidthScale = 1.f;

	/** Shapes the width along the curve (X 0 at its first point, 1 at its last; Y multiplies the width) - a swell in the middle, a calligraphic stroke. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve|Width", meta = (DisplayName = "Use Width Curve"))
	bool bUseWidthCurve = false;

	UPROPERTY(EditAnywhere, Category = "Curve|Width", meta = (EditCondition = "bUseWidthCurve", EditConditionHides))
	FRuntimeFloatCurve WidthCurve;

	/**
	 * A gradient on the stroke runs along the curve - its first stop at the first point, its last at the end - instead of across
	 * the box at Gradient Angle: a line that cools from one color into another as it goes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve", meta = (DisplayName = "Gradient Along Curve"))
	bool bGradientAlongCurve = true;

	/**
	 * A Curve Preset asset this curve follows: picking one puts its points (and, with its Apply Look, its look) here. While this
	 * curve keeps them unchanged, edits to the asset reach it the next time it loads; changing what the preset holds lets it go.
	 * Save As Curve Preset (Details) makes one from this curve.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Curve")
	TObjectPtr<UWidgetCurvePresetAsset> CurvePreset = nullptr;

	// ---------------------------------------------------------------------------------------------
	// Blueprint
	// ---------------------------------------------------------------------------------------------

	/** Puts a Curve Preset's points (and look, when it applies its look) on this curve, and follows it. */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void ApplyCurvePreset(UWidgetCurvePresetAsset* InPreset);

	/** Writes this curve's points and look into a Curve Preset asset (at runtime too - curves the player draws, kept). */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void CopyToCurvePreset(UWidgetCurvePresetAsset* Target) const;

	/** How Smooth Through Points works the curve out between its points. */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void SetInterpolation(EWidgetCurveInterpolation InInterpolation);

	/** Slants the line's ends (degrees from square across it; see Start Cap Angle). */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void SetCapAngles(float InStartCapAngle, float InEndCapAngle);

	/**
	 * Turns the points the curve has - many of them, drawn as the pointer moved (Add Point) - into the few that follow them to
	 * within Tolerance (Slate units, at the widget's last drawn size), corners kept sharp: a signature or a gesture drawn live,
	 * tidied into a smooth line. As Smoothing runs: points it smooths through, Bezier handles (Handles As Drawn) or straight
	 * lines. Every line of the curve.
	 */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void FitCurveToPoints(float Tolerance = 2.f);

	/** Runs the curve the other way: its first point becomes its last (arrowheads, a draw-on and Get Point At Fraction follow). */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void ReverseCurve();

	/** Replaces the curve with a ready-made one (its Smoothing too). */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void SetCurveFromTemplate(EWidgetCurveTemplate Template);

	/**
	 * Replaces the curve with one line through Points, as fractions of the widget's box ((0, 0) its top left, (1, 1) its bottom
	 * right). bClosed joins the last point back to the first.
	 */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void SetPoints(const TArray<FVector2D>& Points, bool bClosed = false);

	/** The points of the curve's first line, as fractions of the widget's box. */
	UFUNCTION(BlueprintPure, Category = "Shape|Curve")
	TArray<FVector2D> GetPoints() const;

	/**
	 * Adds a point at the end of the curve's last line (a box fraction) - drawing as the pointer moves, a trail. With
	 * MinSpacing (Slate units, at the widget's last drawn size) a point closer than that to the last one is skipped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void AddPoint(FVector2D Point, float MinSpacing = 0.f);

	/** Adds a point where a widget-local position is (what a pointer event's position becomes in this widget). */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void AddPointAtLocalPosition(FVector2D LocalPosition, float MinSpacing = 2.f);

	/** Leaves the curve with no points (Add Point then starts a new line). */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void ClearPoints();

	/**
	 * Draws Values as a line chart across the widget: evenly spaced left to right, MinValue at the bottom and MaxValue at the top.
	 * bAutoRange fits the range to the values instead. With Smooth Through Points it reads as a smooth trend line.
	 */
	UFUNCTION(BlueprintCallable, Category = "Shape|Curve")
	void PlotValues(const TArray<float>& Values, float MinValue = 0.f, float MaxValue = 1.f, bool bAutoRange = true);

	/** The curve's length as last drawn, in widget-local Slate units (its first line). 0 before it is shown. */
	UFUNCTION(BlueprintPure, Category = "Shape|Curve")
	float GetCurveLength() const;

	/**
	 * Where the curve runs a fraction of the way along it (0 its first point, 1 its last), in widget-local space as last drawn, and
	 * which way it is heading there (degrees, clockwise from pointing right - a Render Transform Angle). False before it is shown.
	 */
	UFUNCTION(BlueprintPure, Category = "Shape|Curve")
	bool GetPointAtFraction(float Fraction, FVector2D& OutPosition, float& OutAngle) const;

	/** The same, Distance Slate units along the curve. */
	UFUNCTION(BlueprintPure, Category = "Shape|Curve")
	bool GetPointAtDistance(float Distance, FVector2D& OutPosition, float& OutAngle) const;

	/** How far along the curve (0..1) it comes closest to a widget-local position - for dragging something along it. */
	UFUNCTION(BlueprintPure, Category = "Shape|Curve")
	float FindFractionNearest(FVector2D LocalPosition) const;

	/** Editor: saves this curve as a new Curve Preset asset (the Save Asset As window), and follows it. */
	void SaveAsCurvePreset();

	/** Makes the path a ready-made curve. OutSmoothing: how that curve is meant to be run through. */
	static void MakeTemplatePath(EWidgetCurveTemplate Template, FWidgetShapePath& OutPath, EWidgetCurveSmoothing& OutSmoothing);

	/** The handles a Smooth Through Points curve has, worked out for Contour at Smoothness (Disconnected points left as corners) - Balanced. */
	static void SmoothContour(FWidgetShapeContour& Contour, float InSmoothness);

	/**
	 * The path as drawn, in box fractions: the points run through as Smoothing and Interpolation say, worked out at PixelsPerBox
	 * (local units per box unit each way - the drawn proportions). What the outline is flattened from, and what the canvas
	 * editor draws while editing.
	 */
	void BuildCurvePath(FWidgetShapePath& OutPath, const FVector2D& PixelsPerBox) const;

	/** Local units per box unit at the size the curve was last drawn (Desired Size before it is shown). */
	FVector2D GetDrawnPixelsPerBox() const;

	/** The width multiplier at a fraction along the curve: the taper and the Width Curve. */
	float WidthScaleAt(float Fraction) const;

	/** Whether the stroke's width changes along the curve. */
	bool HasVaryingWidth() const;

	// UShapeWidget
	virtual void BuildPath(FWidgetShapePath& OutPath) const override;
	virtual void BuildOutline(const FWidgetShapeFrame& InFrame, float Tolerance, FWidgetShapeOutline& Out) const override;
	virtual uint32 GetMeshHash() const override;
	virtual void BuildStrokeMesh(const FWidgetShapeOutline& InOutline, const TArray<int8>& FillOnLeft, float AntiAlias, float Tolerance, FWidgetShapeMesh& Out) const override;
	virtual bool PaintsStrokeGradientAlongOutline() const override { return bGradientAlongCurve; }

	// UCustomShape
	virtual bool UsesVertexHandles() const override { return Smoothing == EWidgetCurveSmoothing::Handles; }
	virtual void BuildDisplayPath(FWidgetShapePath& OutPath, const FVector2D& PixelsPerBox) const override { BuildCurvePath(OutPath, PixelsPerBox); }
	virtual bool ShowsControlCage() const override { return Smoothing == EWidgetCurveSmoothing::Smooth && Interpolation == EWidgetCurveInterpolation::BSpline; }
	virtual void MakeContourFromStroke(const TArray<FVector2D>& BoxPoints, const FVector2D& PixelsPerBox, float Tolerance, bool bClosed, FWidgetShapeContour& Out) const override;

	// UObject
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// UWidget
	virtual void OnCreationFromPalette() override;
#endif

private:
	/** The settings a Curve Preset covers on this curve: its points always, its look when the preset applies it. */
	void GetPresetCoveredProperties(bool bWithLook, TArray<const FProperty*>& OutProperties) const;

	/** A hash of what the followed preset covers, as this curve holds it now - unchanged since it was applied means still following. */
	uint32 HashPresetCovered() const;

	/** Copies what the preset covers from this designer preview curve onto the Widget Blueprint's own, so it survives the next compile. */
	void SyncPresetToTemplate();

	/** Works out a contour's handles (positions in drawn units) as Interpolation says. */
	void InterpolateContour(FWidgetShapeContour& Contour) const;

	/** Smooths every contour through its points (Smooth Through Points, whatever Smoothing is now) at PixelsPerBox. */
	void SmoothPath(FWidgetShapePath& InOutPath, const FVector2D& PixelsPerBox) const;

	/** The first line as last drawn, in widget-local space. Null before it is shown. */
	const struct FWidgetShapeLine* GetDrawnLine() const;

	/** The hash of the covered settings when the preset was applied. 0: not applied since this was added. */
	UPROPERTY()
	uint32 PresetAppliedHash = 0;
};
