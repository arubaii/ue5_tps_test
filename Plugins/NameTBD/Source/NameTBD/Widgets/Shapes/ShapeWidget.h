// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "Components/PanelSlot.h"
#include "Layout/Margin.h"
#include "Rendering/RenderingCommon.h"
#include "Types/SlateEnums.h"
#include "Widgets/SCompoundWidget.h"

#include "WidgetShapeGeometry.h"
#include "WidgetShapeTypes.h"
#include "NameTBD/Widgets/WidgetEffectImage.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"

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

/** Where a shape lays out the widget it holds. */
UENUM(BlueprintType)
enum class EWidgetShapeContentArea : uint8
{
	Bounds UMETA(ToolTip = "The widget's whole box, as a Border's content - for a Rectangle, or content meant to run over the outline."),
	InsideStroke UMETA(DisplayName = "Inside Stroke", ToolTip = "The widget's box less the part of the stroke drawn inside the outline - a framed Rectangle's content clear of its frame."),
	Inscribed UMETA(ToolTip = "The largest rectangle inside the outline and its stroke, at the content's proportions: text in a circle, a star or a triangle stays inside it. Follows Shape Scale and Shape Rotation.")
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
 * and paints both as meshes - then the one widget it holds, laid out in its Content Area, on top.
 *
 * The UMG widget is read live on every paint, so a value set from Blueprint, a UMG animation or a Widget Animation shows
 * on the next frame without being pushed. The mesh is only built again when something that shapes it changes - the shape,
 * the widget's size, or the scale it is drawn at (rounded, so a shape scaling up doesn't rebuild every frame). Colors and
 * the render transform are applied to the cached mesh as it is painted, which costs a pass over its vertices.
 *
 * Hit Test Outline: the widget is then Self Hit Test Invisible and tiles its fill with invisible rectangles (built with
 * the mesh, a handful per shape), so only the outline's inside is hovered or clicked - its content keeps its own hit area.
 */
class NAMETBD_API SWidgetShape : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWidgetShape)
		: _PaintOnly(false)
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(TWeakObjectPtr<const UShapeWidget>, Shape)
		/** Only paints the shape - no content, no hit area of its own, the shape's Desired Size: a Context Menu Button's background. */
		SLATE_ARGUMENT(bool, PaintOnly)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetContent(const TSharedRef<SWidget>& InContent);
	void SetContentPadding(const FMargin& InPadding);
	void SetContentAlignment(EHorizontalAlignment InHorizontalAlignment, EVerticalAlignment InVerticalAlignment);

	/**
	 * The visibility asked for (the UMG widget's), from now on put through the shape: Visible becomes Self Hit Test Invisible
	 * plus the hit rectangles while the shape hit tests its outline. Not called, the widget's visibility is left as set.
	 */
	void SetRequestedVisibility(const TAttribute<EVisibility>& InVisibility);

	/** Where the content goes in a widget of LocalSize - the shape's Content Area, before the slot's padding and alignment. */
	FBox2f GetContentArea(const FVector2f& LocalSize) const;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;

	/** The outline as last painted, in local space - for the designer's path editor. Empty before the first paint. */
	const FWidgetShapeOutline& GetPaintedOutline() const { return Outline; }

	/** The Content Area as last arranged, in local space. Invalid before the first arrange. */
	const FBox2f& GetArrangedContentArea() const { return ArrangedArea; }

	/**
	 * While the shape's own Fill is off, its inside is painted with Brush instead (its image stretched across the shape's box, or
	 * its tint where it has no image) - a Context Menu Button's own brush, cut to its unfilled Shape Style. Null: nothing inside.
	 * Returns true when that changed what is painted.
	 */
	bool SetUnfilledBrush(const FSlateBrush* Brush);

	/** The dynamic instance a Material paint is drawn with (the fill's, or the stroke's), once it has been drawn. */
	UMaterialInstanceDynamic* GetPaintMaterial(bool bStroke) const { return PaintMaterials.FindDynamic(bStroke ? 1 : 0); }

private:
	void Rebuild(const UShapeWidget& ShapeWidget, const FVector2f& LocalSize, float PixelScale, const FWidgetShapePaint* FillPaint) const;
	void PaintMesh(const FWidgetShapeMesh& Mesh, const FWidgetShapePaint& Paint, FWidgetSVGBrush& PaintSVG, bool bStroke, const FGeometry& AllottedGeometry,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;

	/** A widget is held and not collapsed. */
	bool HasContent() const;

	/** What the content asks for, its padding included. */
	FVector2f GetContentDesiredSize() const;

	/** The Inscribed area of a widget of LocalSize at Aspect, Margin clear of the outline. False when nothing fits. */
	bool ComputeInscribed(const UShapeWidget& Shape, const FVector2f& LocalSize, float Aspect, float Margin, FBox2f& OutArea) const;

	/** The size that gives the content its desired size in the Content Area: Size To Content. */
	FVector2D ComputeSizeForContent(const UShapeWidget& Shape) const;

	/** Only the outline's inside is hit: Hit Test Outline on, routed and asked to be Visible. */
	bool HitTestsOutline() const;
	EVisibility GetEffectiveVisibility() const;
	void PaintHitCells(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bEnabled) const;

	TWeakObjectPtr<const UShapeWidget> ShapeWidget;
	bool bPaintOnly = false;

	/** SetUnfilledBrush's brush (a copy: the one handed in may not outlive the call). */
	bool bHasUnfilledBrush = false;
	FSlateBrush UnfilledBrush;

	FMargin ContentPadding;
	EHorizontalAlignment ContentHorizontalAlignment = HAlign_Center;
	EVerticalAlignment ContentVerticalAlignment = VAlign_Center;

	TAttribute<EVisibility> RequestedVisibility;
	bool bRoutesVisibility = false;

	/** The Content Area last worked out, and for what (the shape's mesh hash, the size, the content's aspect). */
	mutable bool bAreaBuilt = false;
	mutable uint32 AreaHash = 0;
	mutable FBox2f AreaRect = FBox2f(ForceInit);
	mutable FBox2f ArrangedArea = FBox2f(ForceInit);

	/** Size To Content's size last worked out, and for what. */
	mutable bool bSizeBuilt = false;
	mutable uint32 SizeHash = 0;
	mutable FVector2D SizeForContent = FVector2D::ZeroVector;

	/** Hit Test Outline: the fill as rectangles (built with the mesh) and an invisible widget for each. */
	mutable bool bHitRectsBuilt = false;
	mutable TArray<FBox2f> HitRects;
	mutable TArray<TSharedRef<SWidget>> HitCells;

	mutable bool bBuilt = false;
	mutable uint32 BuiltHash = 0;
	mutable FWidgetShapeFrame Frame;
	mutable FWidgetShapeOutline Outline;
	mutable FWidgetShapeMesh FillMesh;
	mutable FWidgetShapeMesh StrokeMesh;
	mutable TArray<FSlateVertex> Vertices;
	mutable TArray<SlateIndex> Indices;

	/** The fill's and the stroke's SVG, for an Image paint with Image Source SVG: drawn at the shape's box size on screen. */
	mutable FWidgetSVGBrush FillSVG;
	mutable FWidgetSVGBrush StrokeSVG;

	/** The fill's (key 0) and the stroke's (key 1) dynamic material instances, for a Material paint. */
	mutable FWidgetEffectMaterials PaintMaterials;

	/** The first contour's length as last built - handed to a Material paint as OutlineLength. */
	mutable float OutlineLength = 0.f;
};

// =====================================================================================================
// UMG widget
// =====================================================================================================

/**
 * Where a shape lays out the one widget it holds - inside its Content Area (the shape's own setting), as a Border's slot does
 * inside the Border.
 */
UCLASS(meta = (DisplayName = "Shape Slot"))
class NAMETBD_API UShapeWidgetSlot : public UPanelSlot
{
	GENERATED_BODY()

public:
	/** Space kept clear round the content, inside the shape's Content Area. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Shape Slot")
	FMargin Padding;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Shape Slot")
	TEnumAsByte<EHorizontalAlignment> HorizontalAlignment = HAlign_Center;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Shape Slot")
	TEnumAsByte<EVerticalAlignment> VerticalAlignment = VAlign_Center;

	UFUNCTION(BlueprintCallable, Category = "Layout|Shape Slot")
	void SetPadding(FMargin InPadding);

	UFUNCTION(BlueprintCallable, Category = "Layout|Shape Slot")
	void SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment);

	UFUNCTION(BlueprintCallable, Category = "Layout|Shape Slot")
	void SetVerticalAlignment(EVerticalAlignment InVerticalAlignment);

	/** Puts the content and this slot's layout on the shape's Slate widget. */
	void BuildSlot(const TSharedRef<SWidgetShape>& InShape);

	// UPanelSlot
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	TWeakPtr<SWidgetShape> SlateShape;
};

/**
 * The base of every shape widget: Ellipse, Rectangle, Triangle, Star, N-gon and Custom Shape.
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
 *
 * Holds one widget, drawn over the shape as a Border holds one: a star button's label, a badge's count, an icon in a
 * circle - no Overlay needed. Content Area says where it goes (the box, inside the stroke, or the largest rectangle inside
 * the outline), the slot its padding and alignment; Size To Content grows the shape round it. Hit Test Outline makes only
 * the outline's inside hoverable and clickable. The shape is the background and the content sits on it unchanged; to bend
 * or warp the content itself, put it in a Shape Box.
 */
UCLASS(Abstract, meta = (DisplayName = "Shape"))
class NAMETBD_API UShapeWidget : public UContentWidget
{
	GENERATED_BODY()

public:
	UShapeWidget(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

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

	/**
	 * Where the widget the shape holds is laid out (then padded and aligned by its slot). Inscribed keeps it inside the
	 * outline - text in a circle or a star - at the content's own proportions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	EWidgetShapeContentArea ContentArea = EWidgetShapeContentArea::Inscribed;

	/**
	 * With a widget inside, the shape asks for the size that gives it its desired size in the Content Area - growing round
	 * a label as a Border does (an Inscribed circle round text ends up about 1.4 times the text). Without one, or off, the
	 * shape asks for Desired Size.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content", meta = (DisplayName = "Size To Content"))
	bool bSizeToContent = true;

	/**
	 * Only the outline's inside is hovered and clicked: the corners of the box round a circle or a star let the pointer
	 * through to what is under them. The widget inside keeps its own hit area. Applies while the shape is Visible; in the
	 * designer the whole box is picked as before.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content", meta = (DisplayName = "Hit Test Outline"))
	bool bHitTestOutline = false;

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

	/**
	 * Paints the outline (bStroke) or the fill with a Material paint: its parameters listed on the paint and set there (Set Paint
	 * ... Parameter), live values handed to it. None goes back to Color. Turns the stroke or fill on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Shape|Material")
	void SetPaintMaterial(bool bStroke, UMaterialInterface* InMaterial);

	/** Sets a scalar parameter of the outline's (bStroke) or the fill's Material paint. */
	UFUNCTION(BlueprintCallable, Category = "Shape|Material")
	void SetPaintScalarParameter(bool bStroke, FName ParameterName, float InValue);

	/** Sets a color (vector) parameter of the outline's (bStroke) or the fill's Material paint. */
	UFUNCTION(BlueprintCallable, Category = "Shape|Material")
	void SetPaintColorParameter(bool bStroke, FName ParameterName, FLinearColor InValue);

	/** Sets a texture parameter of the outline's (bStroke) or the fill's Material paint. */
	UFUNCTION(BlueprintCallable, Category = "Shape|Material")
	void SetPaintTextureParameter(bool bStroke, FName ParameterName, UTexture* InValue);

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

	UFUNCTION(BlueprintCallable, Category = "Shape|Content")
	void SetContentArea(EWidgetShapeContentArea InContentArea);

	UFUNCTION(BlueprintCallable, Category = "Shape|Content")
	void SetSizeToContent(bool bInSizeToContent);

	UFUNCTION(BlueprintCallable, Category = "Shape|Content")
	void SetHitTestOutline(bool bInHitTestOutline);

	/**
	 * Where the widget the shape holds is laid out, in widget-local space, as last arranged - the Content Area before the
	 * slot's padding and alignment. False before the shape is shown.
	 */
	UFUNCTION(BlueprintPure, Category = "Shape|Content")
	bool GetContentAreaRect(FVector2D& OutTopLeft, FVector2D& OutSize) const;

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

	/**
	 * The stroke's mesh, from the flattened outline: by default the Stroke and Trim Path as every shape draws them. A shape that
	 * draws more along its outline - a Curve's arrowheads and taper - builds it here. FillOnLeft as BuildFill gives it.
	 */
	virtual void BuildStrokeMesh(const FWidgetShapeOutline& InOutline, const TArray<int8>& FillOnLeft, float AntiAlias, float Tolerance, FWidgetShapeMesh& Out) const;

	/** Whether a gradient on the stroke runs along the outline (by distance, from each contour's first vertex) instead of across the box. */
	virtual bool PaintsStrokeGradientAlongOutline() const { return false; }

	/** Repaints (and lays out again, when the shape sizes to a widget it holds). Called by every setter; anything changing a shape's settings directly should call it too. */
	void InvalidateShape();

	/** How far the stroke reaches inside the outline: all of it Inside, half Centered, none Outside or not drawn. */
	float GetInnerStrokeWidth() const;

	/**
	 * Whether a property is one of the Content settings (Content Area, Size To Content, Hit Test Outline) - how the shape holds
	 * a widget, not how it looks - or a link to an asset (a Curve's Curve Preset): kept out of Looks, Shape tracks, Flow List
	 * item looks and presets, as Desired Size is.
	 */
	static bool IsContentProperty(const FProperty* Property);

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
	virtual void SetVisibility(ESlateVisibility InVisibility) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;

	// UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// UPanelWidget
	virtual UClass* GetSlotClass() const override;
	virtual void OnSlotAdded(UPanelSlot* InSlot) override;
	virtual void OnSlotRemoved(UPanelSlot* InSlot) override;

	/** Routes the widget's visibility through the Slate shape (Hit Test Outline), at runtime only. */
	void SyncShapeVisibility();

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
