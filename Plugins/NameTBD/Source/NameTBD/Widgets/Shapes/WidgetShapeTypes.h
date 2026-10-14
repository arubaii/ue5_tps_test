// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "NameTBD/Widgets/WidgetSVG.h"

#include "WidgetShapeTypes.generated.h"

/**
 * The data every shape widget is made of (Ellipse, Triangle, Star, N-gon, Custom Shape): how its outline is painted (fill
 * and stroke), what part of the outline is drawn (Trim Path), and for the Custom Shape the outline itself - contours of
 * vertices joined by straight lines or Bezier curves, edited on the designer's canvas the way a vector tool edits a path.
 *
 * The vertex model follows the production vector tools (Rive, Figma, Illustrator, After Effects): a vertex is Straight
 * (no handles, and can be rounded with a Corner Radius), Mirrored (handles always opposite and the same length),
 * Asymmetric (handles opposite, lengths free) or Disconnected (each handle free). A Custom Shape's positions are kept as
 * fractions of the widget's box, so the shape follows the widget's size the way a vector layer set to Scale does; corner
 * radii and stroke widths stay in Slate units, so they don't stretch with it.
 */

// =====================================================================================================
// Painting
// =====================================================================================================

/** What a fill or a stroke is painted with. */
UENUM(BlueprintType)
enum class EWidgetShapePaintType : uint8
{
	Color UMETA(ToolTip = "One flat color."),

	LinearGradient UMETA(DisplayName = "Linear Gradient", ToolTip = "Colors blended along a line across the shape, at Gradient Angle. Exact, however many stops."),

	RadialGradient UMETA(DisplayName = "Radial Gradient", ToolTip = "Colors blended outward from Gradient Center to Gradient Radius."),

	Image UMETA(ToolTip = "A brush - a texture or a material - stretched across the shape's box and cut to its outline, tinted by the brush's Tint and by Color.")
};

/**
 * How an Image paint - a texture or a material - is laid over what it paints. A fill is always stretched over the shape's box;
 * a stroke can also run along its outline, the way a vector tool's brushed or patterned stroke does.
 */
UENUM(BlueprintType)
enum class EWidgetShapeImageMapping : uint8
{
	Box UMETA(DisplayName = "Stretch Over Shape", ToolTip = "Stretched across the shape's box and cut to what is painted: U left to right, V top to bottom."),

	AlongOutline UMETA(DisplayName = "Along Outline", ToolTip = "Strokes only. U runs along the outline - 0 to 1 once round each contour, or one per Tile Length - and V across the stroke, 0 on its inside edge to 1 on its outside. A material can then flow, pulse or dash along the outline (animate with Time on U), or glow outward (V). Dashes and a Trim Path keep their place on it, so a material doesn't slide as they move.")
};

/** One color of a gradient, at a place along it. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetShapeGradientStop
{
	GENERATED_BODY()

	/** Where along the gradient, 0 (its start) to 1 (its end). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Position = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gradient")
	FLinearColor Color = FLinearColor::White;

	FWidgetShapeGradientStop() = default;
	FWidgetShapeGradientStop(float InPosition, const FLinearColor& InColor) : Position(InPosition), Color(InColor) {}
};

/** How a fill or a stroke is colored. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetShapePaint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint")
	EWidgetShapePaintType Type = EWidgetShapePaintType::Color;

	/** The color - and, on a gradient or an image, a tint over all of it (white leaves it as it is). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Paint")
	FLinearColor Color = FLinearColor::White;

	/** The gradient's colors, by place. Sorted when drawn, so they can be listed in any order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint", meta = (EditCondition = "Type == EWidgetShapePaintType::LinearGradient || Type == EWidgetShapePaintType::RadialGradient", EditConditionHides))
	TArray<FWidgetShapeGradientStop> GradientStops;

	/** Which way a Linear Gradient runs, in degrees: 0 left to right, 90 top to bottom. It spans the shape's box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Paint", meta = (EditCondition = "Type == EWidgetShapePaintType::LinearGradient", EditConditionHides, UIMin = "-180", UIMax = "180", ForceUnits = "Degrees"))
	float GradientAngle = 90.f;

	/** Where a Radial Gradient starts, as a fraction of the shape's box: (0.5, 0.5) is the middle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Paint", meta = (EditCondition = "Type == EWidgetShapePaintType::RadialGradient", EditConditionHides))
	FVector2D GradientCenter = FVector2D(0.5, 0.5);

	/** How far a Radial Gradient reaches, as a fraction of the shape's box: 0.5 reaches its sides from the middle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Paint", meta = (EditCondition = "Type == EWidgetShapePaintType::RadialGradient", EditConditionHides, ClampMin = "0.001", UIMin = "0.05", UIMax = "1"))
	float GradientRadius = 0.5f;

	/**
	 * Where an Image paint's picture comes from: the Image brush (Texture 2D: a texture or a material), or an SVG drawn at
	 * the size the shape is shown, so it is sharp at any size.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint", meta = (EditCondition = "Type == EWidgetShapePaintType::Image", EditConditionHides))
	EWidgetImageSource ImageSource = EWidgetImageSource::Texture;

	/** The image or material, for Image. Stretched across the shape's box (its Image Size isn't used), tinted by its Tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint", meta = (EditCondition = "Type == EWidgetShapePaintType::Image && ImageSource == EWidgetImageSource::Texture", EditConditionHides))
	FSlateBrush Image;

	/** The SVG, for Image with Image Source SVG: stretched across the shape's box like the brush, tinted by Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint", meta = (EditCondition = "Type == EWidgetShapePaintType::Image && ImageSource == EWidgetImageSource::SVG", EditConditionHides, DisplayName = "SVG"))
	TObjectPtr<UWidgetSVG> SVG = nullptr;

	/** How the image lies over what it paints. Along Outline is for strokes: on a fill it is stretched over the shape, as Stretch Over Shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint", meta = (EditCondition = "Type == EWidgetShapePaintType::Image", EditConditionHides))
	EWidgetShapeImageMapping ImageMapping = EWidgetShapeImageMapping::Box;

	/**
	 * Along Outline: how long one repeat of the image is along the outline, in Slate units - U then counts repeats, so a pattern
	 * keeps its size whatever the shape's size (a texture repeats if it tiles; a material can Frac it). 0 runs U from 0 to 1
	 * once round each contour, for a material that fills or sweeps the whole outline.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Paint", meta = (EditCondition = "Type == EWidgetShapePaintType::Image && ImageMapping == EWidgetShapeImageMapping::AlongOutline", EditConditionHides, ClampMin = "0", UIMin = "0", UIMax = "200", ForceUnits = "px"))
	float ImageTileLength = 0.f;

	FWidgetShapePaint();

	/** Whether this is an Image paint drawn from an SVG (Image Source SVG, an SVG set). */
	bool DrawsSVG() const { return Type == EWidgetShapePaintType::Image && ImageSource == EWidgetImageSource::SVG && SVG != nullptr; }

	/** Whether this is an Image paint drawn from its brush (Image Source Texture 2D, a texture or material set). */
	bool DrawsBrush() const { return Type == EWidgetShapePaintType::Image && ImageSource == EWidgetImageSource::Texture && Image.GetResourceObject() != nullptr; }

	/** Whether this paints an image laid along the outline (strokes only). */
	bool MapsAlongOutline() const { return Type == EWidgetShapePaintType::Image && ImageMapping == EWidgetShapeImageMapping::AlongOutline; }

	/** The UV a vertex of a stroke laid Along Outline takes: Along is its distance along the outline and how far across (0..1), OutlineLength its contour's length. */
	FVector2f GetOutlineUV(const FVector2f& Along, float OutlineLength) const
	{
		const float Repeat = ImageTileLength > KINDA_SMALL_NUMBER ? ImageTileLength : FMath::Max(OutlineLength, KINDA_SMALL_NUMBER);
		return FVector2f(Along.X / Repeat, Along.Y);
	}

	/** Whether colors vary across the shape - the mesh is then cut so the gradient is drawn exactly. */
	bool IsGradient() const { return Type == EWidgetShapePaintType::LinearGradient || Type == EWidgetShapePaintType::RadialGradient; }

	/** The color at a point of the shape's box (0..1 each way), Color tint included. */
	FLinearColor Evaluate(const FVector2f& BoxPoint) const;

	/** Where a point of the box falls along the gradient, 0..1 (unclamped), for a gradient. */
	float GetGradientPosition(const FVector2f& BoxPoint) const;

	/** The gradient's color at Position (clamped), its stops sorted. */
	FLinearColor SampleGradient(float Position) const;

	/** Stop positions inside 0..1 a Linear Gradient's mesh is cut at, sorted. */
	void GetStopPositions(TArray<float>& OutPositions) const;

	/** A hash of everything that changes where the mesh has to be cut (type, angle, stop places) - not colors. */
	uint32 GetLayoutHash() const;
};

// =====================================================================================================
// Stroke
// =====================================================================================================

/** Where a stroke sits relative to the outline of a closed contour. Open contours always center it. */
UENUM(BlueprintType)
enum class EWidgetShapeStrokeAlign : uint8
{
	Center UMETA(ToolTip = "Half inside the outline, half outside."),
	Inside UMETA(ToolTip = "Entirely inside: the shape keeps its size."),
	Outside UMETA(ToolTip = "Entirely outside: the fill keeps its size and the stroke grows around it.")
};

/** How a stroke turns a corner. */
UENUM(BlueprintType)
enum class EWidgetShapeLineJoin : uint8
{
	Miter UMETA(ToolTip = "Sharp, the edges carried on until they meet - cut off as a Bevel past Miter Limit."),
	Round UMETA(ToolTip = "Rounded."),
	Bevel UMETA(ToolTip = "Cut off flat.")
};

/** How a stroke ends where its outline does: at the ends of an open contour, of a Trim Path and of each dash. */
UENUM(BlueprintType)
enum class EWidgetShapeLineCap : uint8
{
	Butt UMETA(ToolTip = "Flat, exactly at the end."),
	Round UMETA(ToolTip = "Rounded past the end by half the width."),
	Square UMETA(ToolTip = "Flat, past the end by half the width.")
};

/** A line drawn along a shape's outline. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetShapeStroke
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke")
	bool bEnabled = false;

	/** In Slate units. Under a pixel on screen it fades rather than thins, so a hairline stays even. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Stroke", meta = (EditCondition = "bEnabled", ClampMin = "0", UIMin = "0", UIMax = "32", ForceUnits = "px"))
	float Width = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (EditCondition = "bEnabled"))
	EWidgetShapeStrokeAlign Align = EWidgetShapeStrokeAlign::Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (EditCondition = "bEnabled"))
	EWidgetShapeLineJoin Join = EWidgetShapeLineJoin::Miter;

	/** How long a Miter corner may be, in widths, before it is cut off as a Bevel. 4 keeps all but the sharpest corners sharp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (EditCondition = "bEnabled && Join == EWidgetShapeLineJoin::Miter", ClampMin = "1", UIMin = "1", UIMax = "10"))
	float MiterLimit = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (EditCondition = "bEnabled"))
	EWidgetShapeLineCap Cap = EWidgetShapeLineCap::Butt;

	/** The length of each dash, in Slate units. 0 draws a solid line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Stroke", meta = (EditCondition = "bEnabled", ClampMin = "0", UIMin = "0", UIMax = "40", ForceUnits = "px"))
	float DashLength = 0.f;

	/** The space between dashes, in Slate units. 0 with a Dash Length leaves gaps as long as the dashes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Stroke", meta = (EditCondition = "bEnabled && DashLength > 0", ClampMin = "0", UIMin = "0", UIMax = "40", ForceUnits = "px"))
	float DashGap = 0.f;

	/** Slides the dashes along the outline, in Slate units - animated, it marches them (a selection outline). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Stroke", meta = (EditCondition = "bEnabled && DashLength > 0", UIMin = "-40", UIMax = "40", ForceUnits = "px"))
	float DashOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (EditCondition = "bEnabled"))
	FWidgetShapePaint Paint;

	FWidgetShapeStroke();

	bool IsDrawn() const { return bEnabled && Width > 0.f; }
	bool IsDashed() const { return DashLength > 0.f; }
};

/**
 * Draws only part of the outline - the stroke's "draw on" reveal of After Effects, Lottie and Rive. Start and End are
 * fractions of each contour's length, measured from its first vertex (an Ellipse's top, at Start Angle); Offset slides
 * the drawn part around, wrapping on closed contours. The fill is left whole.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetShapeTrim
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Trim Path", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Start = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Trim Path", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float End = 1.f;

	/** Turns of the outline, added to Start and End. 0.25 moves the drawn part a quarter of the way round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category = "Trim Path", meta = (UIMin = "-1", UIMax = "1"))
	float Offset = 0.f;

	/** Whether this draws the whole outline. */
	bool IsWhole() const;
};

/** How a shape fills a widget that isn't the shape's own proportions. */
UENUM(BlueprintType)
enum class EWidgetShapeFit : uint8
{
	Stretch UMETA(ToolTip = "Fills the widget, stretched to its proportions - as a vector layer set to Scale does."),
	KeepAspect UMETA(DisplayName = "Keep Aspect Ratio", ToolTip = "Kept to the shape's own proportions - a regular Triangle, Star or N-gon, a circle, a square Custom Shape - as large as fits, centered.")
};

// =====================================================================================================
// Custom Shape paths
// =====================================================================================================

/** What a vertex's Bezier handles do. The production vector tools' four kinds. */
UENUM(BlueprintType)
enum class EWidgetShapeVertexType : uint8
{
	Straight UMETA(ToolTip = "No handles: straight edges meet here, and Corner Radius can round them."),
	Mirrored UMETA(ToolTip = "Handles always opposite each other and the same length: a smooth curve."),
	Asymmetric UMETA(ToolTip = "Handles opposite each other, each its own length: smooth, with more pull on one side."),
	Disconnected UMETA(ToolTip = "Each handle its own direction and length: a sharp point between curves.")
};

/**
 * One vertex of a Custom Shape. Positions and handles are fractions of the shape's box, (0, 0) its top left and (1, 1) its
 * bottom right; handles are offsets from Position. A Straight vertex has no handles whatever they hold.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetShapeVertex
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex")
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex")
	EWidgetShapeVertexType Type = EWidgetShapeVertexType::Straight;

	/** Toward the vertex before, as an offset from Position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex", meta = (EditCondition = "Type != EWidgetShapeVertexType::Straight", EditConditionHides))
	FVector2D InHandle = FVector2D::ZeroVector;

	/** Toward the vertex after, as an offset from Position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex", meta = (EditCondition = "Type != EWidgetShapeVertexType::Straight", EditConditionHides))
	FVector2D OutHandle = FVector2D::ZeroVector;

	/** Rounds the corner, in Slate units, along the edges either side. Held to what fits between this vertex and the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vertex", meta = (EditCondition = "Type == EWidgetShapeVertexType::Straight", EditConditionHides, ClampMin = "0", UIMin = "0", UIMax = "64", ForceUnits = "px"))
	float CornerRadius = 0.f;

	FWidgetShapeVertex() = default;
	explicit FWidgetShapeVertex(const FVector2D& InPosition, float InCornerRadius = 0.f) : Position(InPosition), CornerRadius(InCornerRadius) {}

	bool HasHandles() const { return Type != EWidgetShapeVertexType::Straight; }
	FVector2D GetIn() const { return HasHandles() ? InHandle : FVector2D::ZeroVector; }
	FVector2D GetOut() const { return HasHandles() ? OutHandle : FVector2D::ZeroVector; }

	/**
	 * Makes the handles obey Type after one of them moved: Mirrored copies the moved handle's opposite, Asymmetric turns
	 * the other to face away and keeps its length. bOutMoved: the out handle is the one that moved.
	 */
	void EnforceType(bool bOutMoved);
};

/** One continuous outline of a Custom Shape. A contour inside another cuts a hole in it. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetShapeContour
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contour")
	TArray<FWidgetShapeVertex> Vertices;

	/** Whether the last vertex joins back to the first. An open contour is still filled - across from its last vertex to its first - and stroked with Caps at its ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Contour")
	bool bClosed = true;

	/** How many segments join its vertices: one per vertex when closed, one fewer when open. */
	int32 GetSegmentCount() const;

	/** A segment's four Bezier points, in box fractions. A segment between two Straight vertices has its handles on its ends. */
	void GetSegment(int32 Segment, FVector2D& P0, FVector2D& P1, FVector2D& P2, FVector2D& P3) const;

	/** Whether a segment is straight: no handle pulls it. */
	bool IsSegmentStraight(int32 Segment) const;

	/** The point a segment passes at T (0..1). */
	FVector2D EvaluateSegment(int32 Segment, float T) const;

	/**
	 * Adds a vertex where a segment passes at T without changing the curve's shape (De Casteljau's split): the curve's
	 * handles either side are shortened to match. Returns the new vertex's index.
	 */
	int32 SplitSegment(int32 Segment, float T);

	/** Reverses the contour's direction - its first vertex stays first - swapping each vertex's handles. */
	void Reverse();
};

/** A Custom Shape's outline: one or more contours. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetShapePath
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	TArray<FWidgetShapeContour> Contours;

	int32 GetVertexCount() const;

	/** The box fractions every vertex and handle reaches. Empty when there are no vertices. */
	FBox2D GetBounds(bool bIncludeHandles) const;

	/** Moves and scales every vertex so the path's bounds fill 0..1 both ways (what a shape drawn off its box needs). */
	void FitToBox();

	/** Mirrors every vertex across the middle of the box. */
	void Flip(bool bHorizontal);

	uint32 GetHash() const;
};
