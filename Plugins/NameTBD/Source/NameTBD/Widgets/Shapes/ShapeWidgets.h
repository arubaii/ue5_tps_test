// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShapeWidget.h"

#include "ShapeWidgets.generated.h"

/**
 * An ellipse, filling the widget - a circle in a square one. Also a ring (Hole), a pie or an arc (Sweep), and a progress
 * ring with rounded ends (Hole, Sweep and Round Ends - animate Sweep from 0 to 360). Drawn exactly: its edge is sampled from
 * the ellipse itself, as finely as the screen needs, not from Bezier curves.
 *
 * Was the Ellipsoid: old Ellipsoids load as Ellipses, though their brush and radius settings don't carry over.
 */
UCLASS(meta = (DisplayName = "Ellipse"))
class NAMETBD_API UEllipseShape : public UShapeWidget
{
	GENERATED_BODY()

public:
	/** A hole in the middle, as a fraction of the ellipse: 0 is solid, 0.8 a thin ring. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape", meta = (ClampMin = "0", ClampMax = "0.999", UIMin = "0", UIMax = "0.99"))
	float Hole = 0.f;

	/** Where the ellipse starts, in degrees: 0 at the top, going clockwise. A Trim Path starts here too. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape", meta = (UIMin = "-180", UIMax = "180", ForceUnits = "Degrees"))
	float StartAngle = 0.f;

	/** How far round it goes, in degrees, clockwise from Start Angle. Under 360 makes a pie, or an arc with a Hole. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape", meta = (ClampMin = "0", ClampMax = "360", UIMin = "0", UIMax = "360", ForceUnits = "Degrees"))
	float Sweep = 360.f;

	/** Rounds the ends of an arc - a ring with a Sweep under 360 - the way a progress ring's are. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (EditCondition = "Hole > 0 && Sweep < 360"))
	bool bRoundEnds = false;

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetArc(float InStartAngle, float InSweep);

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetHole(float InHole);

	virtual bool SetShapeValue(EWidgetShapeValue Value, float InValue) override;
	virtual bool GetShapeValue(EWidgetShapeValue Value, float& OutValue) const override;
	virtual void BuildOutline(const FWidgetShapeFrame& InFrame, float Tolerance, FWidgetShapeOutline& Out) const override;
	virtual void BuildPath(FWidgetShapePath& OutPath) const override;
	virtual uint32 GetMeshHash() const override;
};

/**
 * A rectangle filling the widget, its corners rounded - the shape most of a UI is made of: a button, a card, a field, a pill
 * (Fully Rounded), a progress bar's track. Its outline starts at the top left corner, so a Trim Path draws on clockwise from
 * there. Rounding is in Slate units and held to what fits, so it stays round however the widget is stretched.
 */
UCLASS(meta = (DisplayName = "Rectangle"))
class NAMETBD_API URectangleShape : public UShapeWidget
{
	GENERATED_BODY()

public:
	/** Rounds every corner, in Slate units. Held to what fits: a value past half the shorter side makes the ends round. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape", meta = (EditCondition = "!bFullyRounded && !bIndividualCorners", ClampMin = "0", UIMin = "0", UIMax = "64", ForceUnits = "px"))
	float CornerRounding = 8.f;

	/** Ends as round as they go - a pill, or a circle in a square widget - whatever the widget's size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (DisplayName = "Fully Rounded"))
	bool bFullyRounded = false;

	/** Rounds each corner by its own amount: Corner Radii instead of Corner Rounding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (EditCondition = "!bFullyRounded"))
	bool bIndividualCorners = false;

	/** Each corner's rounding, in Slate units: X top left, Y top right, Z bottom right, W bottom left - a tab's top corners only, say. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape", meta = (EditCondition = "!bFullyRounded && bIndividualCorners", EditConditionHides))
	FVector4 CornerRadii = FVector4(8.0, 8.0, 8.0, 8.0);

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetCornerRounding(float InRounding);

	virtual bool SetShapeValue(EWidgetShapeValue Value, float InValue) override;
	virtual bool GetShapeValue(EWidgetShapeValue Value, float& OutValue) const override;
	virtual void BuildPath(FWidgetShapePath& OutPath) const override;
	virtual uint32 GetMeshHash() const override;
};

/** A triangle filling the widget: its point at the top middle, its base along the bottom. Keep Aspect Ratio makes it equilateral. */
UCLASS(meta = (DisplayName = "Triangle"))
class NAMETBD_API UTriangleShape : public UShapeWidget
{
	GENERATED_BODY()

public:
	/** Rounds the corners, in Slate units, along their edges. Held to what fits, so a large value rounds them fully. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape", meta = (ClampMin = "0", UIMin = "0", UIMax = "64", ForceUnits = "px"))
	float CornerRounding = 0.f;

	virtual bool SetShapeValue(EWidgetShapeValue Value, float InValue) override;
	virtual bool GetShapeValue(EWidgetShapeValue Value, float& OutValue) const override;
	virtual void BuildPath(FWidgetShapePath& OutPath) const override;
	virtual float GetNaturalAspect() const override;
	virtual uint32 GetMeshHash() const override;
};

/** A star: points round a circle, each between two inner corners. Its first point is at the top. */
UCLASS(meta = (DisplayName = "Star"))
class NAMETBD_API UStarShape : public UShapeWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (ClampMin = "3", ClampMax = "100", UIMin = "3", UIMax = "24"))
	int32 Points = 5;

	/** How far out the inner corners are, as a fraction of half the box. 0.382 makes the classic five-pointed star. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape", meta = (ClampMin = "0", ClampMax = "2", UIMin = "0", UIMax = "1"))
	float InnerRadius = 0.382f;

	/** How far out the points are, as a fraction of half the box. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape", meta = (ClampMin = "0", ClampMax = "2", UIMin = "0", UIMax = "1"))
	float OuterRadius = 1.f;

	/** Rounds the points, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape", meta = (ClampMin = "0", UIMin = "0", UIMax = "64", ForceUnits = "px"))
	float CornerRounding = 0.f;

	/** Rounds the inner corners, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape", meta = (ClampMin = "0", UIMin = "0", UIMax = "64", ForceUnits = "px"))
	float InnerRounding = 0.f;

	/**
	 * Stretches the star so its points touch all four sides of the box (at Outer Radius 1), as the vector tools do. Off, its
	 * points lie on the circle inside the box, which leaves a gap under an odd number of them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape")
	bool bFillBox = true;

	virtual bool SetShapeValue(EWidgetShapeValue Value, float InValue) override;
	virtual bool GetShapeValue(EWidgetShapeValue Value, float& OutValue) const override;
	virtual void BuildPath(FWidgetShapePath& OutPath) const override;
	virtual float GetNaturalAspect() const override;
	virtual uint32 GetMeshHash() const override;
};

/** A regular polygon of any number of sides - a pentagon, a hexagon - its first corner at the top. */
UCLASS(meta = (DisplayName = "N-gon"))
class NAMETBD_API UPolygonShape : public UShapeWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (ClampMin = "3", ClampMax = "100", UIMin = "3", UIMax = "16"))
	int32 Sides = 5;

	/** Rounds the corners, in Slate units, along their edges. Held to what fits, so a large value rounds them fully. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape", meta = (ClampMin = "0", UIMin = "0", UIMax = "64", ForceUnits = "px"))
	float CornerRounding = 0.f;

	/**
	 * Stretches the polygon so its corners touch all four sides of the box, as the vector tools do. Off, its corners lie on
	 * the circle inside the box, which leaves a gap under an odd number of sides.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape")
	bool bFillBox = true;

	virtual bool SetShapeValue(EWidgetShapeValue Value, float InValue) override;
	virtual bool GetShapeValue(EWidgetShapeValue Value, float& OutValue) const override;
	virtual void BuildPath(FWidgetShapePath& OutPath) const override;
	virtual float GetNaturalAspect() const override;
	virtual uint32 GetMeshHash() const override;
};

/**
 * A shape of your own: contours of vertices joined by straight edges or Bezier curves, edited on the designer's canvas.
 *
 * Select it in the designer and press Edit Shape (on the canvas or in the Details panel): drag vertices and their handles,
 * double-click an edge to add a vertex on it, Delete to remove the selected ones, draw new vertices with the Pen. Each vertex
 * is Straight (with a Corner Radius), Mirrored, Asymmetric or Disconnected, as in Rive, Figma and Illustrator. A contour
 * inside another cuts a hole. Positions are fractions of the widget's box, so the shape follows the widget's size.
 */
UCLASS(meta = (DisplayName = "Custom Shape"))
class NAMETBD_API UCustomShape : public UShapeWidget
{
	GENERATED_BODY()

public:
	UCustomShape();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape")
	FWidgetShapePath Path;

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetPath(const FWidgetShapePath& InPath);

	virtual void BuildPath(FWidgetShapePath& OutPath) const override;
	virtual uint32 GetMeshHash() const override;

	/** Whether the vertices' own Bezier handles shape the outline - what the canvas path editor shows and drags. A Curve smoothing through its points works out its own. */
	virtual bool UsesVertexHandles() const { return true; }

	/**
	 * The path as it is drawn, in box fractions, at PixelsPerBox (local units per box unit each way) - what the canvas path
	 * editor draws and hit-tests. The Path itself here; a Curve's points smoothed. Same contours and vertex count as Path.
	 */
	virtual void BuildDisplayPath(FWidgetShapePath& OutPath, const FVector2D& PixelsPerBox) const { OutPath = Path; }

	/** Whether the canvas path editor shows the points joined by straight lines too: they are a control cage the line is drawn inside (a B-Spline Curve). */
	virtual bool ShowsControlCage() const { return false; }

	/**
	 * A line drawn by hand (box fractions, in the order drawn) as one contour of this shape: Bezier segments fitted to within
	 * Tolerance (local units, at PixelsPerBox), corners kept sharp - what the canvas Pencil adds. A Curve fits points it runs
	 * through as it is set to.
	 */
	virtual void MakeContourFromStroke(const TArray<FVector2D>& BoxPoints, const FVector2D& PixelsPerBox, float Tolerance, bool bClosed, FWidgetShapeContour& Out) const;
};

/** The polygons' corners, for the shapes built from them. */
namespace WidgetShapePolygons
{
	/**
	 * The corners of a regular polygon (Count corners) or a star (Count points and as many inner corners), in box fractions,
	 * first at the top, clockwise. bFillBox stretches the shape at radius 1 to touch the box's sides. OutInner: per corner,
	 * whether it is a star's inner corner. OutAspect: the shape's width over height at its own proportions.
	 */
	NAMETBD_API void MakeCorners(int32 Count, bool bStar, float OuterRadius, float InnerRadius, bool bFillBox,
		TArray<FVector2D>& OutCorners, TArray<bool>& OutInner, float& OutAspect);
}
