// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Rendering/RenderingCommon.h"
#include "Widgets/SLeafWidget.h"

#include "WidgetShapeGeometry.h"
#include "WidgetShapeTypes.h"

#include "ShapeWidget.generated.h"

class UShapeWidget;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * A value of a shape that can be set by name - from Blueprint, and by the Widget Animations editor's Shape track. Each
 * shape answers the ones it has: a Star's Points, an Ellipse's Sweep. The rest it ignores.
 */
UENUM(BlueprintType)
enum class EWidgetShapeValue : uint8
{
	TrimStart UMETA(DisplayName = "Trim Start"),
	TrimEnd UMETA(DisplayName = "Trim End", ToolTip = "Animated 0 to 1 with Trim Start at 0, the stroke draws itself on."),
	TrimOffset UMETA(DisplayName = "Trim Offset"),
	StrokeWidth UMETA(DisplayName = "Stroke Width"),
	DashOffset UMETA(DisplayName = "Dash Offset", ToolTip = "Animated steadily, the dashes march along the outline."),
	ShapeRotation UMETA(DisplayName = "Shape Rotation"),
	ShapeScale UMETA(DisplayName = "Shape Scale", ToolTip = "Both ways at once. Unlike the widget's Render Transform scale, strokes and rounding keep their width."),
	CornerRounding UMETA(DisplayName = "Corner Rounding", ToolTip = "Rectangle, Triangle, Star (its points), N-gon."),
	InnerRounding UMETA(DisplayName = "Inner Rounding", ToolTip = "Star: its inner corners."),
	Points UMETA(ToolTip = "Star. Whole numbers only."),
	InnerRadius UMETA(DisplayName = "Inner Radius", ToolTip = "Star."),
	OuterRadius UMETA(DisplayName = "Outer Radius", ToolTip = "Star."),
	Sides UMETA(ToolTip = "N-gon. Whole numbers only."),
	Hole UMETA(ToolTip = "Ellipse: the size of the hole that makes it a ring."),
	StartAngle UMETA(DisplayName = "Start Angle", ToolTip = "Ellipse."),
	Sweep UMETA(ToolTip = "Ellipse: how much of the way round it goes. 0 to 360 fills a progress ring."),
	FillColor UMETA(DisplayName = "Fill Color"),
	StrokeColor UMETA(DisplayName = "Stroke Color")
};

/** Whether a shape value is a color rather than a number. */
NAMETBD_API bool WidgetShapeValueIsColor(EWidgetShapeValue Value);

/** Whether a shape value only takes whole numbers. */
NAMETBD_API bool WidgetShapeValueIsWhole(EWidgetShapeValue Value);

// =====================================================================================================
// Slate widget
// =====================================================================================================

/**
 * Draws a shape widget: asks it for its outline, triangulates the fill and builds the stroke (see WidgetShapeGeometry.h),
 * and paints both as meshes.
 *
 * The UMG widget is read live on every paint, so a value set from Blueprint, a UMG animation or a Widget Animation shows
 * on the next frame without being pushed. The mesh is only built again when something that shapes it changes - the shape,
 * the widget's size, or the scale it is drawn at (rounded, so a shape scaling up doesn't rebuild every frame). Colors and
 * the render transform are applied to the cached mesh as it is painted, which costs a pass over its vertices.
 */
class NAMETBD_API SWidgetShape : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWidgetShape) {}
		SLATE_ARGUMENT(TWeakObjectPtr<const UShapeWidget>, Shape)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	/** The outline as last painted, in local space - for the designer's path editor. Empty before the first paint. */
	const FWidgetShapeOutline& GetPaintedOutline() const { return Outline; }

private:
	void Rebuild(const UShapeWidget& ShapeWidget, const FVector2f& LocalSize, float PixelScale) const;
	void PaintMesh(const FWidgetShapeMesh& Mesh, const FWidgetShapePaint& Paint, bool bStroke, const FGeometry& AllottedGeometry,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;

	TWeakObjectPtr<const UShapeWidget> ShapeWidget;

	mutable bool bBuilt = false;
	mutable uint32 BuiltHash = 0;
	mutable FWidgetShapeFrame Frame;
	mutable FWidgetShapeOutline Outline;
	mutable FWidgetShapeMesh FillMesh;
	mutable FWidgetShapeMesh StrokeMesh;
	mutable TArray<FSlateVertex> Vertices;
	mutable TArray<SlateIndex> Indices;
};

// =====================================================================================================
// UMG widget
// =====================================================================================================

/**
 * The base of every shape widget: Ellipse, Triangle, Star, N-gon and Custom Shape.
 *
 * What every shape shares - the production vector tools' fill, stroke and trim:
 *   Fill        - a color, a linear or radial gradient, or an image or material cut to the outline.
 *   Stroke      - a line along the outline: inside, centered or outside it, with miter, round or bevel corners, flat, round
 *                 or square ends, and dashes that can march. Painted with an image or a material, it can also be laid Along
 *                 Outline: U runs round the outline and V across it, so a material flows, pulses or glows along the edge.
 *   Trim Path   - only part of the outline stroked: animated, the outline draws itself on.
 *   Layout      - how the shape fits a widget that isn't its own proportions, and a scale and turn of the shape inside the
 *                 widget that leave its strokes and rounding at their width (unlike a Render Transform).
 * Every edge is anti-aliased to the pixel at whatever scale it is drawn, and every shape lands exactly where its numbers
 * put it: what is drawn is a mesh built from the outline, not a texture.
 *
 * Presets: the Details panel's Preset row saves a shape's settings under a name, and puts them on any other shape of the
 * same kind from its dropdown.
 */
UCLASS(Abstract, meta = (DisplayName = "Shape"))
class NAMETBD_API UShapeWidget : public UWidget
{
	GENERATED_BODY()

public:
	UShapeWidget();

	/** Whether the inside is painted. Off leaves only the stroke - an outline shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fill", meta = (DisplayName = "Fill"))
	bool bFill = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fill", meta = (EditCondition = "bFill", DisplayName = "Fill Paint", ShowOnlyInnerProperties))
	FWidgetShapePaint Fill;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stroke", meta = (ShowOnlyInnerProperties))
	FWidgetShapeStroke Stroke;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trim Path", meta = (ShowOnlyInnerProperties))
	FWidgetShapeTrim Trim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape Layout")
	EWidgetShapeFit Fit = EWidgetShapeFit::Stretch;

	/** Grows or shrinks the shape inside the widget, about its middle. Strokes and rounding keep their width. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape Layout", meta = (UIMin = "0", UIMax = "2"))
	FVector2D ShapeScale = FVector2D(1.0, 1.0);

	/** Turns the shape inside the widget, in degrees, clockwise. Strokes and rounding keep their width; the widget's box doesn't turn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Shape Layout", meta = (UIMin = "-180", UIMax = "180", ForceUnits = "Degrees"))
	float ShapeRotation = 0.f;

	/** The size the widget asks for when its slot sizes it to its content, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape Layout")
	FVector2D DesiredSize = FVector2D(64.0, 64.0);

	/** Anti-aliases every edge to the pixel. Off draws hard edges - for pixel art. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "Shape Layout", meta = (DisplayName = "Smooth Edges"))
	bool bAntiAlias = true;

	// ---------------------------------------------------------------------------------------------
	// Blueprint
	// ---------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetFillEnabled(bool bInFill);

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetFill(const FWidgetShapePaint& InFill);

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetFillColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetStroke(const FWidgetShapeStroke& InStroke);

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetStrokeColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetStrokeWidth(float InWidth);

	/**
	 * Paints the outline with a material (or clears it with None: back to Color). Laid as the stroke's Image Mapping says -
	 * Along Outline for a material that flows round the edge. Turns the stroke on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Shape|Material")
	void SetStrokeMaterial(UMaterialInterface* InMaterial);

	/** Paints the fill with a material (or clears it with None: back to Color). */
	UFUNCTION(BlueprintCallable, Category = "Shape|Material")
	void SetFillMaterial(UMaterialInterface* InMaterial);

	/**
	 * The outline's material as a Dynamic Material Instance, made on first call, to set its parameters from Blueprint - a
	 * glow's strength on hover, a sweep's progress. Null unless the stroke is painted with a material.
	 */
	UFUNCTION(BlueprintCallable, Category = "Shape|Material")
	UMaterialInstanceDynamic* GetDynamicStrokeMaterial();

	/** The fill's material as a Dynamic Material Instance, made on first call. Null unless the fill is painted with a material. */
	UFUNCTION(BlueprintCallable, Category = "Shape|Material")
	UMaterialInstanceDynamic* GetDynamicFillMaterial();

	/** Draws only part of the outline's stroke. Start and End are fractions of the outline, Offset turns of it. */
	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetTrim(float InStart, float InEnd, float InOffset = 0.f);

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetShapeRotation(float InDegrees);

	UFUNCTION(BlueprintCallable, Category = "Shape")
	void SetShapeScale(FVector2D InScale);

	/** Sets one of the shape's values by name. False when this kind of shape has no such value. */
	UFUNCTION(BlueprintCallable, Category = "Shape")
	virtual bool SetShapeValue(EWidgetShapeValue Value, float InValue);

	/** One of the shape's values by name. False when this kind of shape has no such value. */
	UFUNCTION(BlueprintPure, Category = "Shape")
	virtual bool GetShapeValue(EWidgetShapeValue Value, float& OutValue) const;

	/** Sets the Fill Color or Stroke Color. False for any other value. */
	UFUNCTION(BlueprintCallable, Category = "Shape")
	bool SetShapeColor(EWidgetShapeValue Value, FLinearColor InColor);

	UFUNCTION(BlueprintPure, Category = "Shape")
	bool GetShapeColor(EWidgetShapeValue Value, FLinearColor& OutColor) const;

	/** Whether a widget-local point is on the shape's fill as last drawn (even-odd, holes excluded). */
	UFUNCTION(BlueprintPure, Category = "Shape")
	bool IsLocalPointInShape(FVector2D LocalPoint) const;

	// ---------------------------------------------------------------------------------------------
	// What each shape supplies
	// ---------------------------------------------------------------------------------------------

	/** Where the shape's box sits in a widget of LocalSize: Fit, Shape Scale and Shape Rotation applied. */
	FWidgetShapeFrame MakeFrame(const FVector2f& LocalSize) const;

	/** The outline, flattened, in local space through Frame. By default the shape's path, flattened. */
	virtual void BuildOutline(const FWidgetShapeFrame& InFrame, float Tolerance, FWidgetShapeOutline& Out) const;

	/** The shape as a Custom Shape's path, in box fractions - what Custom Shape's Copy From uses. */
	virtual void BuildPath(FWidgetShapePath& OutPath) const {}

	/** Width over height of the shape at its own proportions - what Keep Aspect Ratio keeps. */
	virtual float GetNaturalAspect() const { return 1.f; }

	/** A hash of everything that changes the mesh. A shape adds its own settings to the base's. */
	virtual uint32 GetMeshHash() const;

	/** Repaints. Called by every setter; anything changing a shape's settings directly should call it too. */
	void InvalidateShape();

	/**
	 * The paint an image or material set on the shape as a whole goes to: the outline's when it is drawn with an image,
	 * otherwise the fill's. What a Context Menu Button's Design Target swaps and sets parameters on.
	 */
	FWidgetShapePaint& GetImagePaint();
	const FWidgetShapePaint& GetImagePaint() const;

	/** A paint's material as a Dynamic Material Instance owned by this shape, made on first call. Null unless it is a material. */
	UMaterialInstanceDynamic* GetDynamicMaterial(FWidgetShapePaint& Paint);

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	TSharedPtr<SWidgetShape> MyShape;

public:
	/**
	 * Another widget that paints this shape and so is repainted along with it - a Context Menu Button whose background is
	 * this shape, drawn without a widget of its own in any tree.
	 */
	void SetExternalPainter(const TSharedPtr<SWidget>& InPainter) { ExternalPainter = InPainter; }

private:
	TWeakPtr<SWidget> ExternalPainter;
};
