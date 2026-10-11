// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "Layout/Margin.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateRenderTransform.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"

#include "ShapeBox.generated.h"

class SVirtualWindow;
class SShapeBoxHitCell;
class FShapeBoxHitTestPath;
struct FShapeBoxRenderResources;

// =====================================================================================================
// Shape
// =====================================================================================================

/** How a corner is cut. */
UENUM(BlueprintType)
enum class EShapeBoxCornerStyle : uint8
{
	/** The corner slides along its edges, so a whole side leans: the cut runs the full length of the side. */
	Bevel,

	/** The corner is cut off with a straight line, leaving the rest of both edges where they were. */
	Chamfer
};

/** One corner's cut. A Size of zero leaves the corner square, whichever style is picked. */
USTRUCT(BlueprintType)
struct NAMETBD_API FShapeBoxCorner
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Corner")
	EShapeBoxCornerStyle Style = EShapeBoxCornerStyle::Bevel;

	/**
	 * How far the corner is cut, in Slate units. X runs along the corner's top or bottom edge, Y along its left or right side.
	 *
	 * Bevel: the corner moves inward by this much along its edges. X alone leans the corner's left/right side over its
	 *        full height (Top Right X = 20: the right side slants from 20 in at the top to the bottom corner). Y alone
	 *        leans its top/bottom edge. Negative flares the corner outward instead.
	 * Chamfer: the length of each leg of the cut. Both X and Y must be above zero; negative counts as zero.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Corner")
	FVector2D Size = FVector2D::ZeroVector;

	/** Blends two corners. A bevel and a chamfer can't be mixed, so the style switches wherever the cut is smallest. */
	static FShapeBoxCorner Lerp(const FShapeBoxCorner& A, const FShapeBoxCorner& B, float Alpha);
};

/** One cut per corner. */
USTRUCT(BlueprintType)
struct NAMETBD_API FShapeBoxCorners
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Corners")
	FShapeBoxCorner TopLeft;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Corners")
	FShapeBoxCorner TopRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Corners")
	FShapeBoxCorner BottomRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Corners")
	FShapeBoxCorner BottomLeft;
};

/** One value per edge, in Slate units. Positive bulges the edge outward, negative pinches it inward. */
USTRUCT(BlueprintType)
struct NAMETBD_API FShapeBoxEdges
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges")
	float Top = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges")
	float Right = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges")
	float Bottom = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edges")
	float Left = 0.0f;
};

/**
 * The outline a Shape Box bends its content into.
 *
 * The controls stack, in this order:
 *   Extrude / Inset - move individual edges out or in. The content is laid out at the new size (resized, not
 *                     stretched), while the box itself keeps its place in the layout.
 *   Corners         - bevels (whole sides lean: one-sided skews, tapers, keystones) or chamfers (corners cut off).
 *   Bend            - arcs the whole box, content included (a flat, curved bar).
 *   Edge Bow        - curves individual edges in or out (brackets, barrels, a single rounded side).
 *   Angle           - turns the result in depth, with Wrap and Perspective (the angled, curved-visor HUD look).
 *   Rotation        - spins the finished shape about its middle.
 *
 * All zero is a normal, untouched box. To lean the whole box, use the widget's own Render Transform Shear.
 *
 * This struct is the single definition of a shape. Anything else that shapes widgets (a Context Menu Button's
 * Shape Targets, for one) stores one of these whole and blends it with Lerp, so a property added here is picked
 * up everywhere without touching the code that uses it.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FShapeBoxShape
{
	GENERATED_BODY()

	/**
	 * Moves edges outward, in Slate units. +X extends the right edge, -X the left, +Y the top, -Y the bottom.
	 * The content is laid out at the grown size - a brush keeps its margins and corners rather than stretching - and the
	 * hit area grows with it. The box's own layout slot doesn't move.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FVector2D Extrude = FVector2D::ZeroVector;

	/**
	 * Pulls edges inward, in Slate units. +X pulls in the right edge, -X the left, +Y the top, -Y the bottom.
	 * The content is laid out at the shrunk size and the hit area shrinks with it. The box's own layout slot doesn't move.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FVector2D Inset = FVector2D::ZeroVector;

	/**
	 * Cuts each corner, either as a Bevel or a Chamfer.
	 * One-sided skew:        Top Right = Bevel, X 20                         (only the right side leans)
	 * Keystone (short right): Top Right = Bevel, Y 12; Bottom Right = Bevel, Y 12
	 * Cut corner:            Top Right = Chamfer, X 12, Y 12
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FShapeBoxCorners Corners;

	/**
	 * Arcs the whole box, content and all, in Slate units: how far the middle moves relative to the ends.
	 * Y bends horizontally-laid-out content: positive sags the middle down, negative arches it up.
	 * X bends vertically-laid-out content: positive pushes the middle right, negative left.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FVector2D Bend = FVector2D::ZeroVector;

	/** Curves each edge on its own, in Slate units at the edge's midpoint. Positive bulges out, negative pinches in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FShapeBoxEdges EdgeBow;

	/**
	 * Turns the box in depth, in degrees, for the angled-HUD look. Everything above is angled with it.
	 * X turns it sideways: positive brings the right side toward you (larger), the left side away (smaller).
	 * Y tips it: positive brings the bottom toward you, negative the top.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape|Angle", meta = (UIMin = "-45", UIMax = "45", ClampMin = "-80", ClampMax = "80", ForceUnits = "Degrees"))
	FVector2D Angle = FVector2D::ZeroVector;

	/**
	 * Wraps the box around a curved surface, in degrees of arc across it - the curved-visor look. Unlike Bend
	 * (a flat arc), this curves in depth, so it tapers and arcs naturally with Angle.
	 * X wraps across the width, Y across the height. Positive curves the edges away from you.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape|Angle", meta = (UIMin = "-90", UIMax = "90", ClampMin = "-180", ClampMax = "180", ForceUnits = "Degrees"))
	FVector2D Wrap = FVector2D::ZeroVector;

	/** Strength of the depth taper for Angle and Wrap. 0 is none, 1 is strong; 0.3 to 0.6 suits most HUDs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape|Angle", meta = (ClampMin = "0", ClampMax = "1"))
	float Perspective = 0.5f;

	/** Spins the finished shape about its middle, in degrees. Positive is clockwise. Applied last, so it turns everything above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (UIMin = "-180", UIMax = "180", ForceUnits = "Degrees"))
	float Rotation = 0.0f;

	/** True when the shape leaves the box untouched. */
	bool IsFlat() const;

	/** True when the shape is a parallelogram (no bends, bows, tapers, chamfers, perspective), which Slate can draw and hit test natively. */
	bool IsAffine() const;

	/** True when any corner is chamfered, i.e. the outline has more than four corners. */
	bool HasChamfer() const;

	/** Extrude and Inset as per-edge offsets, in Slate units. Positive moves that edge outward. */
	FMargin GetEdgeOffsets() const;

	/** Where a point of the content (0..1 across the extruded/inset box) is drawn, in the local space of a box with this layout size. */
	FVector2D Evaluate(const FVector2D& LayoutSize, const FVector2D& NormalizedPoint) const;

	/**
	 * The transform that maps a LayoutSize box onto the drawn shape. Exact when IsAffine; for any other shape it is the
	 * parallelogram through the drawn top-left, top-right and bottom-left corners.
	 */
	FSlateRenderTransform MakeLayoutToShapeTransform(const FVector2D& LayoutSize) const;

	/**
	 * Blends every property between two shapes. Driven entirely by reflection, so a property added to this struct later
	 * blends along with the rest without anything here (or in anything that animates shapes) having to change. Numbers
	 * interpolate (and extrapolate, for overshooting curves); anything that can't be blended switches halfway.
	 */
	static FShapeBoxShape Lerp(const FShapeBoxShape& A, const FShapeBoxShape& B, float Alpha);

	/** Exact comparison of every property, by reflection. */
	bool Equals(const FShapeBoxShape& Other) const;
};

// =====================================================================================================
// Slate widget
// =====================================================================================================

/**
 * Bends its content into an FShapeBoxShape while keeping it fully interactive.
 *
 * Three paths, chosen every frame from the shape:
 *   Flat      - paints the content normally.
 *   Affine    - a parallelogram (Extrude, Inset, matching bevels, Rotation). The content gets a render
 *               transform, so Slate draws and hit tests it natively at no extra cost.
 *   Warped    - anything else (tapers, chamfers, bends, bows, angles). The content is rendered into a texture and
 *               drawn as a mesh following the shape. Input is mapped back through that mesh into the content, and
 *               the hit area is the drawn shape: parts drawn outside the layout rect are clickable, and parts of the
 *               layout rect the shape no longer covers (a chamfered corner, a pinched edge) let clicks through.
 */
class NAMETBD_API SShapeBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SShapeBox)
		: _Shape(FShapeBoxShape())
		, _RenderScale(1.0f)
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ATTRIBUTE(FShapeBoxShape, Shape)
		SLATE_ARGUMENT(float, RenderScale)
	SLATE_END_ARGS()

	SShapeBox();
	virtual ~SShapeBox() override;

	void Construct(const FArguments& InArgs);

	void SetContent(const TSharedRef<SWidget>& InContent);
	void SetShape(const TAttribute<FShapeBoxShape>& InShape);

	/** Texture resolution relative to the screen while warped. Raise above 1 if heavily bent content looks soft. */
	void SetRenderScale(float InRenderScale);

	/**
	 * Warped, the content is drawn offscreen into a texture - a whole Slate render of it - and that texture bent into the
	 * shape. By default it is drawn again on every paint, since the content may be anything and may be animating. A box
	 * whose owner knows exactly when its content changes (a button's brush layer, which only ever shows one brush) can
	 * say so: it is then only drawn again when MarkContentChanged is called, or the texture's size, scale or the box's
	 * enabled state changes - and a screen of warped buttons stops rendering every one of them offscreen every frame.
	 */
	void SetRedrawContentOnlyWhenChanged(bool bInOnlyWhenChanged);

	/** The content looks different: drawn again on the next paint. See SetRedrawContentOnlyWhenChanged. */
	void MarkContentChanged() { bContentChanged = true; }

	/** Where a point of the content (0..1 across the extruded/inset box) is drawn, in this widget's local space. */
	FVector2D GetWarpedLocalPosition(const FVector2D& NormalizedPoint) const;

	/**
	 * Whether a point in this widget's local space lies on the shape as it was last painted: inside the drawn outline,
	 * and not in a chamfered corner. False before the first paint.
	 */
	bool IsLocalPointInShape(const FVector2D& LocalPoint) const;

	// SWidget
	void SetRequestedVisibility(TAttribute<EVisibility> InVisibility);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;

private:
	friend class SShapeBoxHitCell;
	friend class FShapeBoxHitTestPath;

	enum class EMode : uint8 { Flat, Affine, Warped };

	EMode ResolveMode(const FShapeBoxShape& InShape, const FVector2D& Size) const;

	/** ShapeBox.DebugContentBox: where the content is arranged and where it was last painted, drawn over the box and logged. */
	void PaintContentDebug(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
	mutable FString LastContentDebugLine;
	EVisibility GetEffectiveVisibility() const;
	void ClearWarpState() const;

	// Warped path
	bool RenderContent(const FVector2D& ContentSize, float Scale, int32 Padding, const FIntPoint& Target, float DeltaTime) const;
	void BuildMesh(const FVector2D& Size, float PaintScale) const;
	void BuildHitCells(const FVector2D& Size) const;
	void PaintHitCells(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bEnabled) const;

	// Input mapping (used by the hit cells and the hit-test path)
	bool LocalToPixel(const FVector2D& LocalPoint, FVector2D& OutPixel) const;
	FVector2D LocalToPixelOrApprox(const FVector2D& LocalPoint) const;
	TArray<FWidgetAndPointer> GetContentBubblePath(const FVector2D& DesktopSpaceCoordinate, bool bIgnoreEnabledStatus) const;
	TOptional<FVirtualPointerPosition> TranslatePointer(const FVector2D& ScreenSpace, const FVector2D& LastScreenSpace) const;
	void ArrangeContentWindow(FArrangedChildren& ArrangedChildren) const;
	bool IsContentWindow(const SWidget& Widget) const;

	TAttribute<FShapeBoxShape> Shape;
	TAttribute<EVisibility> UserVisibility;
	float RenderScale = 1.0f;

	TSharedPtr<SVirtualWindow> Window;
	TSharedPtr<FShapeBoxHitTestPath> HitTestPath;

	// ---- Per-paint state (mutable: OnPaint is const) ----

	mutable EMode Mode = EMode::Flat;
	mutable FShapeBoxShape PaintedShape;
	mutable FVector2D PaintedSize = FVector2D::ZeroVector;

	/** The extruded/inset box the content was laid out in, in local space. */
	mutable FVector2D PaintedContentOrigin = FVector2D::ZeroVector;
	mutable FVector2D PaintedContentSize = FVector2D::ZeroVector;

	mutable FShapeBoxRenderResources* Resources = nullptr;
	mutable FSlateBrush SurfaceBrush;
	mutable FGeometry WindowGeometry;
	mutable FIntPoint TargetSize = FIntPoint::ZeroValue;
	mutable float PixelScale = 1.0f;
	mutable int32 Padding = 0;
	mutable FVector2D LastWindowSize = FVector2D::ZeroVector;
	mutable bool bLastWindowEnabled = true;

	mutable TArray<FVector2D> MeshPositions;
	mutable TArray<FVector2D> MeshUVs;
	/** Per-vertex coverage: 1 everywhere except the soft strip along a chamfer cut. */
	mutable TArray<float> MeshAlphas;
	mutable TArray<SlateIndex> MeshIndices;
	mutable FBox2D MeshBounds = FBox2D(ForceInit);
	mutable TArray<FSlateVertex> DrawVertices;

	/** Invisible parallelograms tiling the drawn shape. They put the shape (not the layout rect) into the hit-test grid. */
	mutable TArray<TSharedRef<SShapeBoxHitCell>> HitCells;
	mutable TArray<FSlateRenderTransform> HitCellTransforms;
	mutable TArray<FVector2D> HitCellSizes;

	mutable FVector2D LastVirtualPixel = FVector2D::ZeroVector;
	mutable bool bHasLastVirtualPixel = false;

	/**
	 * What the mesh and the hit cells were last built for. Both are a pure function of the shape, the box's size and the
	 * scales it is painted at, and were rebuilt on every paint - thousands of evaluated, clipped vertices per box per frame,
	 * for a box whose shape hadn't moved. Built again only when one of these changes (or the warp state is cleared).
	 */
	mutable bool bMeshBuilt = false;
	mutable FShapeBoxShape MeshBuiltShape;
	mutable FVector2D MeshBuiltSize = FVector2D::ZeroVector;
	mutable float MeshBuiltPaintScale = 0.f;
	mutable float MeshBuiltPixelScale = 0.f;
	mutable FIntPoint MeshBuiltTargetSize = FIntPoint::ZeroValue;
	mutable int32 MeshBuiltPadding = 0;

	mutable bool bHitCellsBuilt = false;
	mutable FShapeBoxShape HitCellsBuiltShape;
	mutable FVector2D HitCellsBuiltSize = FVector2D::ZeroVector;

	/** See SetRedrawContentOnlyWhenChanged: whether the content is drawn again only when it changed, and what it was last drawn at. */
	bool bRedrawOnlyWhenChanged = false;
	mutable bool bContentChanged = true;
	mutable bool bContentDrawn = false;
	mutable FIntPoint ContentDrawnTarget = FIntPoint::ZeroValue;
	mutable float ContentDrawnScale = 0.f;
	mutable int32 ContentDrawnPadding = 0;
	mutable FVector2D ContentDrawnSize = FVector2D::ZeroVector;
	mutable bool bContentDrawnEnabled = true;
};

// =====================================================================================================
// UMG widget
// =====================================================================================================

/**
 * Bends whatever is placed inside it into a stylized shape - extrusions, slants, bevels, chamfered corners, arcs,
 * bowed edges, rotations and angled, curved-visor panels - while buttons and other widgets inside keep working
 * exactly where they are drawn.
 *
 * Recipes:
 *   Grow on the right:        Extrude X = 8                        (content re-laid out 8 wider, box stays put)
 *   Italic slant:             Render Transform Shear X = -12     (the widget's own Shear)
 *   One-sided skew:           Corners > Top Right = Bevel, X 20      (only the right side leans)
 *   Keystone (short right):   Corners > Top Right = Bevel, Y 12; Bottom Right = Bevel, Y 12
 *   Cut corner:               Corners > Top Right = Chamfer, X 12, Y 12
 *   Curved health bar:        Bend Y = -8                          (arches up; add Shear X for speed)
 *   Bracket panel:            Edge Bow Left = 10, Right = 10
 *   Tilted card:              Rotation = -4
 *   Angled HUD panel:         Angle X = 15, Wrap X = 20, Perspective = 0.5
 *   Curved visor row:         Wrap X = 30, Perspective = 0.4    (wrap a whole row of panels in one box)
 *
 * Parallelograms (Extrude, Inset, Rotation, bevels in matching pairs) cost nothing extra. Other shapes render
 * the content into a texture every frame, like a Retainer Box.
 *
 * A Context Menu Button whose Shape Target names this box, or the only widget inside it, drives this box's Shape
 * from its states; while it does, the Shape authored here is overwritten.
 */
UCLASS(meta = (DisplayName = "Shape Box"))
class NAMETBD_API UShapeBox : public UContentWidget
{
	GENERATED_BODY()

public:
	/** The shape. Read live every frame, so it can be set directly from Blueprint or animated freely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Box", meta = (ShowOnlyInnerProperties))
	FShapeBoxShape Shape;

	/** Texture resolution relative to the screen for warped shapes. Raise if strongly bent content looks soft. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "Shape Box", meta = (ClampMin = "0.25", ClampMax = "4.0", UIMin = "0.5", UIMax = "2.0"))
	float RenderScale = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "Shape Box")
	void SetShape(const FShapeBoxShape& InShape) { Shape = InShape; }

	UFUNCTION(BlueprintPure, Category = "Shape Box")
	FShapeBoxShape GetShape() const { return Shape; }

	UFUNCTION(BlueprintCallable, Category = "Shape Box")
	void SetExtrude(FVector2D InExtrude) { Shape.Extrude = InExtrude; }

	UFUNCTION(BlueprintCallable, Category = "Shape Box")
	void SetInset(FVector2D InInset) { Shape.Inset = InInset; }

	UFUNCTION(BlueprintCallable, Category = "Shape Box")
	void SetCorners(const FShapeBoxCorners& InCorners) { Shape.Corners = InCorners; }

	UFUNCTION(BlueprintCallable, Category = "Shape Box")
	void SetBend(FVector2D InBend) { Shape.Bend = InBend; }

	UFUNCTION(BlueprintCallable, Category = "Shape Box")
	void SetEdgeBow(const FShapeBoxEdges& InEdgeBow) { Shape.EdgeBow = InEdgeBow; }

	UFUNCTION(BlueprintCallable, Category = "Shape Box")
	void SetAngle(FVector2D InAngle, FVector2D InWrap, float InPerspective = 0.5f)
	{
		Shape.Angle = InAngle;
		Shape.Wrap = InWrap;
		Shape.Perspective = FMath::Clamp(InPerspective, 0.0f, 1.0f);
	}

	UFUNCTION(BlueprintCallable, Category = "Shape Box")
	void SetRotation(float InRotation) { Shape.Rotation = InRotation; }

	UFUNCTION(BlueprintCallable, Category = "Shape Box")
	void SetWarpRenderScale(float InRenderScale);

	/** Where a point of the content (0..1 across the extruded/inset box, (1,0) = top right) is drawn, in this widget's local space. */
	UFUNCTION(BlueprintPure, Category = "Shape Box")
	FVector2D GetWarpedLocalPosition(FVector2D NormalizedPoint) const;

	// UWidget
	virtual void SetVisibility(ESlateVisibility InVisibility) override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void OnSlotAdded(UPanelSlot* InSlot) override;
	virtual void OnSlotRemoved(UPanelSlot* InSlot) override;

private:
	TSharedPtr<SShapeBox> MyShapeBox;
};
