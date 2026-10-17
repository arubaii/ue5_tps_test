// Copyright AR GameSystems 2026. All rights reserved.

#include "ShapeWidget.h"
#include "CurveShape.h"

#include "Layout/ArrangedChildren.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "ShapeWidget"

namespace ShapeWidgetPrivate
{
	/** The brush every flat or gradient paint draws with: no texture, so the vertex colors are all there is. */
	const FSlateBrush& GetUntexturedBrush()
	{
		static const FSlateBrush Brush;
		return Brush;
	}

	/**
	 * How many screen pixels one local unit covers: the length of each local axis once transformed - its scale, whatever it is
	 * rotated or sheared by. Not GetAbsoluteSize, which is the size vector rotated and shrinks toward 0 as a widget turns.
	 */
	float GetPixelScale(const FGeometry& Geometry)
	{
		const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
		const FVector2f AxisX = Transform.TransformVector(FVector2f(1.f, 0.f));
		const FVector2f AxisY = Transform.TransformVector(FVector2f(0.f, 1.f));
		return FMath::Max(FMath::Max(AxisX.Size(), AxisY.Size()), 0.01f);
	}

	/**
	 * The scale rounded to an eighth of a doubling, so a shape scaling smoothly up or down is built again a few times along
	 * the way rather than on every frame. The fade is then within 9% of a pixel wide, which no one can see.
	 */
	float QuantizeScale(float Scale)
	{
		const float Steps = FMath::RoundToFloat(FMath::Log2(Scale) * 8.f) / 8.f;
		return FMath::Pow(2.f, Steps);
	}

	/** How finely the Content Area's outline is flattened, in local units: a quarter unit is far finer than any layout needs. */
	constexpr float AreaTolerance = 0.25f;

	/** Hit Test Outline's rows: at least this tall (local units), at most this many. */
	constexpr float HitRowHeight = 2.f;
	constexpr int32 MaxHitRows = 96;

	/**
	 * Invisible, draws nothing and asks for no room. Hit Test Outline tiles the shape's fill with these, so the hit-test
	 * grid sees the outline rather than the box; painted with the shape as their paint parent, the pointer over one bubbles
	 * through the shape and everything above it, as if the shape itself were hit.
	 */
	class SWidgetShapeHitCell : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetShapeHitCell) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&)
		{
			SetCanTick(false);
		}

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry&, const FSlateRect&, FSlateWindowElementList&, int32 LayerId, const FWidgetStyle&, bool) const override
		{
			return LayerId;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D::ZeroVector;
		}
	};

	/** Places Length of content along an axis of Room starting at Start: Fill takes it all, the others its desired length. */
	void AlignAxis(int32 Alignment, float Start, float Room, float Desired, float& OutOffset, float& OutLength)
	{
		// 0 Fill, 1 Left / Top, 2 Center, 3 Right / Bottom - as EHorizontalAlignment and EVerticalAlignment number them.
		if (Alignment == 0)
		{
			OutOffset = Start;
			OutLength = Room;
			return;
		}
		OutLength = FMath::Min(Desired, Room);
		OutOffset = Alignment == 1 ? Start : Alignment == 3 ? Start + Room - OutLength : Start + (Room - OutLength) * 0.5f;
	}
}

bool WidgetShapeValueIsColor(EWidgetShapeValue Value)
{
	return Value == EWidgetShapeValue::FillColor || Value == EWidgetShapeValue::StrokeColor;
}

bool WidgetShapeValueIsWhole(EWidgetShapeValue Value)
{
	return Value == EWidgetShapeValue::Points || Value == EWidgetShapeValue::Sides;
}

// =====================================================================================================
// Slate widget
// =====================================================================================================

void SWidgetShape::Construct(const FArguments& InArgs)
{
	ShapeWidget = InArgs._Shape;
	bPaintOnly = InArgs._PaintOnly;
	ChildSlot
	[
		bPaintOnly ? SNullWidget::NullWidget : InArgs._Content.Widget
	];
}

void SWidgetShape::SetContent(const TSharedRef<SWidget>& InContent)
{
	if (bPaintOnly)
	{
		return;
	}
	ChildSlot
	[
		InContent
	];
	bAreaBuilt = false;
	bSizeBuilt = false;
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::ChildOrder);
}

void SWidgetShape::SetContentPadding(const FMargin& InPadding)
{
	if (ContentPadding != InPadding)
	{
		ContentPadding = InPadding;
		bAreaBuilt = false;
		bSizeBuilt = false;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SWidgetShape::SetContentAlignment(EHorizontalAlignment InHorizontalAlignment, EVerticalAlignment InVerticalAlignment)
{
	if (ContentHorizontalAlignment != InHorizontalAlignment || ContentVerticalAlignment != InVerticalAlignment)
	{
		ContentHorizontalAlignment = InHorizontalAlignment;
		ContentVerticalAlignment = InVerticalAlignment;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SWidgetShape::SetRequestedVisibility(const TAttribute<EVisibility>& InVisibility)
{
	if (bPaintOnly)
	{
		return;
	}
	RequestedVisibility = InVisibility;
	bRoutesVisibility = true;
	SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SWidgetShape::GetEffectiveVisibility));
}

bool SWidgetShape::HitTestsOutline() const
{
	const UShapeWidget* Shape = ShapeWidget.Get();
	return bRoutesVisibility && !bPaintOnly && Shape && Shape->bHitTestOutline
		&& RequestedVisibility.Get(EVisibility::Visible) == EVisibility::Visible;
}

EVisibility SWidgetShape::GetEffectiveVisibility() const
{
	const EVisibility Requested = RequestedVisibility.Get(EVisibility::Visible);

	// The box itself would catch the pointer over the corners round the outline: the hit cells stand in for it.
	if (HitTestsOutline())
	{
		return EVisibility::SelfHitTestInvisible;
	}
	return Requested;
}

bool SWidgetShape::HasContent() const
{
	if (bPaintOnly)
	{
		return false;
	}
	const TSharedRef<SWidget>& Content = ChildSlot.GetWidget();
	return Content != SNullWidget::NullWidget && Content->GetVisibility() != EVisibility::Collapsed;
}

FVector2f SWidgetShape::GetContentDesiredSize() const
{
	const FVector2f Desired(ChildSlot.GetWidget()->GetDesiredSize());
	return Desired + FVector2f(ContentPadding.Left + ContentPadding.Right, ContentPadding.Top + ContentPadding.Bottom);
}

bool SWidgetShape::ComputeInscribed(const UShapeWidget& Shape, const FVector2f& LocalSize, float Aspect, float Margin, FBox2f& OutArea) const
{
	FWidgetShapeOutline AreaOutline;
	Shape.BuildOutline(Shape.MakeFrame(LocalSize), ShapeWidgetPrivate::AreaTolerance, AreaOutline);
	return WidgetShapeGeometry::FindInscribedRect(AreaOutline, Aspect, Margin, OutArea);
}

FBox2f SWidgetShape::GetContentArea(const FVector2f& LocalSize) const
{
	const FBox2f Whole(FVector2f::ZeroVector, LocalSize);
	const UShapeWidget* Shape = ShapeWidget.Get();
	if (!Shape || LocalSize.X <= 0.f || LocalSize.Y <= 0.f)
	{
		return Whole;
	}

	const float Inset = Shape->GetInnerStrokeWidth();
	switch (Shape->ContentArea)
	{
	case EWidgetShapeContentArea::Bounds:
		return Whole;

	case EWidgetShapeContentArea::InsideStroke:
	{
		const FVector2f Clear(FMath::Min(Inset, LocalSize.X * 0.5f), FMath::Min(Inset, LocalSize.Y * 0.5f));
		return FBox2f(Clear, LocalSize - Clear);
	}

	case EWidgetShapeContentArea::Inscribed:
	default:
		break;
	}

	// At the content's proportions (padding included), so a label gets the widest box that keeps it inside; with nothing
	// that asks for room, at the widget's.
	const FVector2f ContentSize = HasContent() ? GetContentDesiredSize() : FVector2f::ZeroVector;
	const float Aspect = ContentSize.X > 0.5f && ContentSize.Y > 0.5f ? ContentSize.X / ContentSize.Y : LocalSize.X / LocalSize.Y;

	uint32 Hash = Shape->GetMeshHash();
	Hash = HashCombineFast(Hash, GetTypeHash(LocalSize));
	Hash = HashCombineFast(Hash, GetTypeHash(Aspect));
	if (bAreaBuilt && Hash == AreaHash)
	{
		return AreaRect;
	}

	FBox2f Found;
	AreaRect = ComputeInscribed(*Shape, LocalSize, Aspect, Inset, Found) ? Found : FBox2f(LocalSize * 0.5f, LocalSize * 0.5f);
	AreaHash = Hash;
	bAreaBuilt = true;
	return AreaRect;
}

FVector2D SWidgetShape::ComputeSizeForContent(const UShapeWidget& Shape) const
{
	const FVector2f ContentSize = GetContentDesiredSize();

	uint32 Hash = Shape.GetMeshHash();
	Hash = HashCombineFast(Hash, GetTypeHash(ContentSize));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Shape.ContentArea)));
	if (bSizeBuilt && Hash == SizeHash)
	{
		return SizeForContent;
	}

	const float Inset = Shape.GetInnerStrokeWidth();
	FVector2f Size = ContentSize;
	switch (Shape.ContentArea)
	{
	case EWidgetShapeContentArea::Bounds:
		break;

	case EWidgetShapeContentArea::InsideStroke:
		Size += FVector2f(Inset * 2.f, Inset * 2.f);
		break;

	case EWidgetShapeContentArea::Inscribed:
	default:
	{
		// The area grows with the widget (all but the strokes and roundings, which are in units), so a few steps of
		// "grow by what is missing" settle it. Grown evenly: the shape takes the content's proportions.
		Size = FVector2f(FMath::Max(ContentSize.X, 1.f), FMath::Max(ContentSize.Y, 1.f));
		const float Aspect = Size.X / Size.Y;
		for (int32 Pass = 0; Pass < 6; ++Pass)
		{
			FBox2f Found;
			if (!ComputeInscribed(Shape, Size, Aspect, Inset, Found))
			{
				break;
			}
			const FVector2f Got = Found.GetSize();
			if (Got.X <= KINDA_SMALL_NUMBER || Got.Y <= KINDA_SMALL_NUMBER)
			{
				break;
			}
			const float Grow = FMath::Clamp(FMath::Max(ContentSize.X / Got.X, ContentSize.Y / Got.Y), 0.25f, 16.f);
			Size *= Grow;
			if (FMath::IsNearlyEqual(Grow, 1.f, 0.002f))
			{
				break;
			}
		}
		Size = FVector2f(FMath::Min(Size.X, 8192.f), FMath::Min(Size.Y, 8192.f));
		break;
	}
	}

	SizeForContent = FVector2D(FMath::CeilToFloat(Size.X), FMath::CeilToFloat(Size.Y));
	SizeHash = Hash;
	bSizeBuilt = true;
	return SizeForContent;
}

FVector2D SWidgetShape::ComputeDesiredSize(float) const
{
	const UShapeWidget* Shape = ShapeWidget.Get();
	if (!Shape)
	{
		return FVector2D(64.0, 64.0);
	}
	if (!Shape->bSizeToContent || !HasContent())
	{
		return Shape->DesiredSize;
	}
	return ComputeSizeForContent(*Shape);
}

void SWidgetShape::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	using namespace ShapeWidgetPrivate;

	if (!HasContent())
	{
		return;
	}

	const TSharedRef<SWidget>& Content = ChildSlot.GetWidget();
	const EVisibility ContentVisibility = Content->GetVisibility();
	if (!ArrangedChildren.Accepts(ContentVisibility))
	{
		return;
	}

	const FVector2f LocalSize(AllottedGeometry.GetLocalSize());
	ArrangedArea = GetContentArea(LocalSize);

	// Padding and alignment as a Border's slot lays them, mirrored right to left.
	const bool bRightToLeft = GSlateFlowDirection == EFlowDirection::RightToLeft;
	const float PadLeft = bRightToLeft ? ContentPadding.Right : ContentPadding.Left;
	const float PadRight = bRightToLeft ? ContentPadding.Left : ContentPadding.Right;
	int32 Horizontal = static_cast<int32>(ContentHorizontalAlignment);
	if (bRightToLeft && (Horizontal == 1 || Horizontal == 3))
	{
		Horizontal = 4 - Horizontal;
	}

	const FVector2f AreaSize = ArrangedArea.GetSize();
	const FVector2f Room(
		FMath::Max(AreaSize.X - PadLeft - PadRight, 0.f),
		FMath::Max(AreaSize.Y - ContentPadding.Top - ContentPadding.Bottom, 0.f));
	const FVector2f Desired(Content->GetDesiredSize());

	float OffsetX = 0.f;
	float OffsetY = 0.f;
	float Width = 0.f;
	float Height = 0.f;
	AlignAxis(Horizontal, ArrangedArea.Min.X + PadLeft, Room.X, Desired.X, OffsetX, Width);
	AlignAxis(static_cast<int32>(ContentVerticalAlignment), ArrangedArea.Min.Y + ContentPadding.Top, Room.Y, Desired.Y, OffsetY, Height);

	ArrangedChildren.AddWidget(ContentVisibility, AllottedGeometry.MakeChild(Content, FVector2D(OffsetX, OffsetY), FVector2D(Width, Height)));
}

void SWidgetShape::PaintHitCells(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bEnabled) const
{
	if (!bHitRectsBuilt)
	{
		WidgetShapeGeometry::BuildFillRects(Outline, ShapeWidgetPrivate::HitRowHeight, ShapeWidgetPrivate::MaxHitRows, HitRects);
		bHitRectsBuilt = true;
	}

	while (HitCells.Num() < HitRects.Num())
	{
		HitCells.Add(SNew(ShapeWidgetPrivate::SWidgetShapeHitCell));
	}
	if (HitCells.Num() > HitRects.Num())
	{
		// Destroying the spare ones drops their entries from the hit-test grid.
		HitCells.RemoveAt(HitRects.Num(), HitCells.Num() - HitRects.Num());
	}

	// Below the content: painted first, so a button inside is hit before the shape round it.
	const FPaintArgs CellArgs = Args.WithNewParent(this);
	for (int32 Index = 0; Index < HitRects.Num(); ++Index)
	{
		const FBox2f& Rect = HitRects[Index];
		const FGeometry CellGeometry = AllottedGeometry.MakeChild(FVector2D(Rect.GetSize()), FSlateLayoutTransform(1.f, FVector2D(Rect.Min)));
		HitCells[Index]->Paint(CellArgs, CellGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bEnabled);
	}
}

bool SWidgetShape::SetUnfilledBrush(const FSlateBrush* Brush)
{
	const bool bHas = Brush != nullptr;
	if (bHas == bHasUnfilledBrush && (!bHas || *Brush == UnfilledBrush))
	{
		return false;
	}
	bHasUnfilledBrush = bHas;
	UnfilledBrush = bHas ? *Brush : FSlateBrush();
	Invalidate(EInvalidateWidgetReason::Paint);
	return true;
}

void SWidgetShape::Rebuild(const UShapeWidget& Shape, const FVector2f& LocalSize, float PixelScale, const FWidgetShapePaint* FillPaint) const
{
	Frame = Shape.MakeFrame(LocalSize);
	const float Tolerance = WidgetShapeGeometry::GetTolerance(PixelScale);
	const float AntiAlias = Shape.bAntiAlias ? 1.f / PixelScale : 0.f;

	Outline.Reset();
	Shape.BuildOutline(Frame, Tolerance, Outline);

	FillMesh.Reset();
	StrokeMesh.Reset();
	TArray<int8> FillOnLeft;
	if (FillPaint)
	{
		WidgetShapeGeometry::BuildFill(Outline, AntiAlias, FillMesh, FillOnLeft);
	}
	else
	{
		WidgetShapeGeometry::ClassifyLines(Outline, FillOnLeft);
	}
	Shape.BuildStrokeMesh(Outline, FillOnLeft, AntiAlias, Tolerance, StrokeMesh);

	// A radial gradient's cells about six pixels across on screen: its rings then read round.
	const float CellSize = 6.f / PixelScale;
	if (FillPaint)
	{
		WidgetShapeGeometry::CutForGradient(FillMesh, *FillPaint, Frame, CellSize);
	}

	// A gradient laid along the outline is colored by each vertex's distance along it, not cut across the box.
	if (!Shape.PaintsStrokeGradientAlongOutline())
	{
		WidgetShapeGeometry::CutForGradient(StrokeMesh, Shape.Stroke.Paint, Frame, CellSize);
	}
}

void SWidgetShape::PaintMesh(const FWidgetShapeMesh& Mesh, const FWidgetShapePaint& Paint, FWidgetSVGBrush& PaintSVG, bool bStroke, const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	using namespace ShapeWidgetPrivate;

	const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();

	// An SVG is drawn at the size of the shape's box on screen; nothing is painted until its first drawing is ready.
	const FSlateBrush* SVGImage = nullptr;
	if (Paint.DrawsSVG())
	{
		SVGImage = PaintSVG.Get(Paint.SVG, nullptr, AllottedGeometry, Frame.Size, *this);
		if (!SVGImage)
		{
			return;
		}
	}
	else
	{
		PaintSVG.Reset();
	}

	const bool bImage = SVGImage || Paint.DrawsBrush();
	const FSlateBrush& Brush = SVGImage ? *SVGImage : bImage ? Paint.Image : GetUntexturedBrush();
	const FLinearColor BrushTint = (bImage && !SVGImage) ? Paint.Image.GetTint(InWidgetStyle) : FLinearColor::White;

	// An image from an atlas covers only part of its texture: its UV Region says which.
	const FBox2f UVRegion = Brush.GetUVRegion();
	const bool bHasUVRegion = bImage && UVRegion.bIsValid && UVRegion.GetArea() > 0.f;

	// A stroke's image laid along its outline: U round the outline, V across the stroke (a fill has no outline to follow).
	const bool bAlongOutline = bImage && bStroke && Paint.MapsAlongOutline();

	// A stroke's gradient laid along its outline (a Curve's Gradient Along Curve): colored by each vertex's distance along it.
	const UShapeWidget* ShapeOwner = ShapeWidget.Get();
	const bool bGradientAlong = bStroke && Paint.IsGradient() && ShapeOwner && ShapeOwner->PaintsStrokeGradientAlongOutline();

	Vertices.SetNumUninitialized(Mesh.Vertices.Num());
	for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
	{
		const FWidgetShapeMeshVertex& Vertex = Mesh.Vertices[Index];
		const FVector2f BoxPoint = Frame.ToBox(Vertex.Position);
		const FLinearColor PaintColor = bGradientAlong
			? Paint.SampleGradient(Vertex.OutlineLength > KINDA_SMALL_NUMBER ? Vertex.Along.X / Vertex.OutlineLength : 0.f) * Paint.Color
			: Paint.Evaluate(BoxPoint);
		FLinearColor Color = PaintColor * BrushTint * WidgetTint;
		Color.A *= Vertex.Coverage;
		const FVector2f UV = bAlongOutline ? Paint.GetOutlineUV(Vertex.Along, Vertex.OutlineLength)
			: bHasUVRegion ? UVRegion.Min + BoxPoint * UVRegion.GetSize() : BoxPoint;
		Vertices[Index] = FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Vertex.Position, UV, Color.ToFColor(true));
	}

	Indices.SetNumUninitialized(Mesh.Indices.Num());
	for (int32 Index = 0; Index < Mesh.Indices.Num(); ++Index)
	{
		Indices[Index] = static_cast<SlateIndex>(Mesh.Indices[Index]);
	}

	FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, Brush.GetRenderingResource(), Vertices, Indices, nullptr, 0, 0);
}

int32 SWidgetShape::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace ShapeWidgetPrivate;

	const UShapeWidget* Shape = ShapeWidget.Get();
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (!Shape || Size.X <= 0.0 || Size.Y <= 0.0)
	{
		return LayerId;
	}

	const FVector2f LocalSize(static_cast<float>(Size.X), static_cast<float>(Size.Y));
	const float PixelScale = QuantizeScale(GetPixelScale(AllottedGeometry));

	// What fills the inside: the shape's Fill, or - Fill off - the brush it was handed (SetUnfilledBrush), or nothing.
	FWidgetShapePaint BrushPaint;
	const FWidgetShapePaint* FillPaint = Shape->bFill ? &Shape->Fill : nullptr;
	if (!FillPaint && bHasUnfilledBrush && UnfilledBrush.DrawAs != ESlateBrushDrawType::NoDrawType)
	{
		if (UnfilledBrush.GetResourceObject())
		{
			BrushPaint.Type = EWidgetShapePaintType::Image;
			BrushPaint.Image = UnfilledBrush;
			BrushPaint.Color = FLinearColor::White;
		}
		else
		{
			BrushPaint.Type = EWidgetShapePaintType::Color;
			BrushPaint.Color = UnfilledBrush.GetTint(InWidgetStyle);
		}
		FillPaint = &BrushPaint;
	}

	uint32 Hash = Shape->GetMeshHash();
	Hash = HashCombineFast(Hash, GetTypeHash(LocalSize));
	Hash = HashCombineFast(Hash, GetTypeHash(PixelScale));
	Hash = HashCombineFast(Hash, GetTypeHash(FillPaint == &BrushPaint));
	if (!bBuilt || Hash != BuiltHash)
	{
		Rebuild(*Shape, LocalSize, PixelScale, FillPaint);
		BuiltHash = Hash;
		bBuilt = true;
		bHitRectsBuilt = false;
	}

	if (FillPaint && !FillMesh.IsEmpty())
	{
		PaintMesh(FillMesh, *FillPaint, FillSVG, /*bStroke=*/false, AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle);
	}
	if (!StrokeMesh.IsEmpty())
	{
		PaintMesh(StrokeMesh, Shape->Stroke.Paint, StrokeSVG, /*bStroke=*/true, AllottedGeometry, OutDrawElements, LayerId + 1, InWidgetStyle);
	}
	int32 MaxLayer = LayerId + 1;

	if (HitTestsOutline() && Args.GetInheritedHittestability())
	{
		PaintHitCells(Args, AllottedGeometry, MyCullingRect, OutDrawElements, MaxLayer, InWidgetStyle, ShouldBeEnabled(bParentEnabled));
	}
	else if (HitCells.Num() > 0)
	{
		HitCells.Reset();
	}

	// The content over the shape, as a Border's over its brush.
	if (HasContent())
	{
		MaxLayer = FMath::Max(MaxLayer, SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, MaxLayer + 1, InWidgetStyle, bParentEnabled));
	}
	return MaxLayer;
}

// =====================================================================================================
// UMG widget
// =====================================================================================================

UShapeWidget::UShapeWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Fill.Color = FLinearColor(0.9f, 0.9f, 0.9f, 1.f);
}

void UShapeWidget::InvalidateShape()
{
	if (MyShape.IsValid())
	{
		// A shape sized round its content asks for another size when its outline changes (Inscribed), or its stroke.
		const bool bSizesToContent = bSizeToContent && GetChildrenCount() > 0;
		MyShape->Invalidate(bSizesToContent ? EInvalidateWidgetReason::Layout : EInvalidateWidgetReason::Paint);
	}
	if (const TSharedPtr<SWidget> Painter = ExternalPainter.Pin())
	{
		Painter->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UShapeWidget::SetFillEnabled(bool bInFill)
{
	bFill = bInFill;
	InvalidateShape();
}

void UShapeWidget::SetFill(const FWidgetShapePaint& InFill)
{
	Fill = InFill;
	InvalidateShape();
}

void UShapeWidget::SetFillColor(FLinearColor InColor)
{
	Fill.Color = InColor;
	InvalidateShape();
}

void UShapeWidget::SetStroke(const FWidgetShapeStroke& InStroke)
{
	Stroke = InStroke;
	InvalidateShape();
}

void UShapeWidget::SetStrokeColor(FLinearColor InColor)
{
	Stroke.Paint.Color = InColor;
	InvalidateShape();
}

void UShapeWidget::SetStrokeWidth(float InWidth)
{
	Stroke.Width = FMath::Max(InWidth, 0.f);
	InvalidateShape();
}

namespace ShapeWidgetPrivate
{
	/** Paints with Material, or back to its color with none. */
	void SetPaintMaterial(FWidgetShapePaint& Paint, UMaterialInterface* Material)
	{
		if (Material)
		{
			Paint.Type = EWidgetShapePaintType::Image;
			Paint.ImageSource = EWidgetImageSource::Texture;
			Paint.Image.SetResourceObject(Material);
		}
		else if (Paint.Type == EWidgetShapePaintType::Image)
		{
			Paint.Type = EWidgetShapePaintType::Color;
			Paint.Image.SetResourceObject(nullptr);
		}
	}
}

void UShapeWidget::SetStrokeMaterial(UMaterialInterface* InMaterial)
{
	ShapeWidgetPrivate::SetPaintMaterial(Stroke.Paint, InMaterial);
	if (InMaterial)
	{
		Stroke.bEnabled = true;
	}
	InvalidateShape();
}

void UShapeWidget::SetFillMaterial(UMaterialInterface* InMaterial)
{
	ShapeWidgetPrivate::SetPaintMaterial(Fill, InMaterial);
	if (InMaterial)
	{
		bFill = true;
	}
	InvalidateShape();
}

UMaterialInstanceDynamic* UShapeWidget::GetDynamicMaterial(FWidgetShapePaint& Paint)
{
	if (Paint.Type != EWidgetShapePaintType::Image)
	{
		return nullptr;
	}
	UMaterialInterface* Material = Cast<UMaterialInterface>(Paint.Image.GetResourceObject());
	if (!Material)
	{
		return nullptr;
	}
	if (UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material))
	{
		return Dynamic;
	}
	UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Material, this);
	Paint.Image.SetResourceObject(Dynamic);
	InvalidateShape();
	return Dynamic;
}

UMaterialInstanceDynamic* UShapeWidget::GetDynamicStrokeMaterial()
{
	return GetDynamicMaterial(Stroke.Paint);
}

UMaterialInstanceDynamic* UShapeWidget::GetDynamicFillMaterial()
{
	return GetDynamicMaterial(Fill);
}

FWidgetShapePaint& UShapeWidget::GetImagePaint()
{
	return (Stroke.IsDrawn() && Stroke.Paint.Type == EWidgetShapePaintType::Image) ? Stroke.Paint : Fill;
}

const FWidgetShapePaint& UShapeWidget::GetImagePaint() const
{
	return const_cast<UShapeWidget*>(this)->GetImagePaint();
}

void UShapeWidget::SetTrim(float InStart, float InEnd, float InOffset)
{
	Trim.Start = FMath::Clamp(InStart, 0.f, 1.f);
	Trim.End = FMath::Clamp(InEnd, 0.f, 1.f);
	Trim.Offset = InOffset;
	InvalidateShape();
}

void UShapeWidget::SetShapeRotation(float InDegrees)
{
	ShapeRotation = InDegrees;
	InvalidateShape();
}

void UShapeWidget::SetShapeScale(FVector2D InScale)
{
	ShapeScale = InScale;
	InvalidateShape();
}

bool UShapeWidget::SetShapeValue(EWidgetShapeValue Value, float InValue)
{
	switch (Value)
	{
	case EWidgetShapeValue::TrimStart: Trim.Start = FMath::Clamp(InValue, 0.f, 1.f); break;
	case EWidgetShapeValue::TrimEnd: Trim.End = FMath::Clamp(InValue, 0.f, 1.f); break;
	case EWidgetShapeValue::TrimOffset: Trim.Offset = InValue; break;
	case EWidgetShapeValue::StrokeWidth: Stroke.Width = FMath::Max(InValue, 0.f); break;
	case EWidgetShapeValue::DashOffset: Stroke.DashOffset = InValue; break;
	case EWidgetShapeValue::ShapeRotation: ShapeRotation = InValue; break;
	case EWidgetShapeValue::ShapeScale: ShapeScale = FVector2D(InValue, InValue); break;
	default: return false;
	}
	InvalidateShape();
	return true;
}

bool UShapeWidget::GetShapeValue(EWidgetShapeValue Value, float& OutValue) const
{
	switch (Value)
	{
	case EWidgetShapeValue::TrimStart: OutValue = Trim.Start; return true;
	case EWidgetShapeValue::TrimEnd: OutValue = Trim.End; return true;
	case EWidgetShapeValue::TrimOffset: OutValue = Trim.Offset; return true;
	case EWidgetShapeValue::StrokeWidth: OutValue = Stroke.Width; return true;
	case EWidgetShapeValue::DashOffset: OutValue = Stroke.DashOffset; return true;
	case EWidgetShapeValue::ShapeRotation: OutValue = ShapeRotation; return true;
	case EWidgetShapeValue::ShapeScale: OutValue = static_cast<float>(ShapeScale.X); return true;
	default: return false;
	}
}

bool UShapeWidget::SetShapeColor(EWidgetShapeValue Value, FLinearColor InColor)
{
	if (Value == EWidgetShapeValue::FillColor)
	{
		Fill.Color = InColor;
	}
	else if (Value == EWidgetShapeValue::StrokeColor)
	{
		Stroke.Paint.Color = InColor;
	}
	else
	{
		return false;
	}
	InvalidateShape();
	return true;
}

bool UShapeWidget::GetShapeColor(EWidgetShapeValue Value, FLinearColor& OutColor) const
{
	if (Value == EWidgetShapeValue::FillColor)
	{
		OutColor = Fill.Color;
		return true;
	}
	if (Value == EWidgetShapeValue::StrokeColor)
	{
		OutColor = Stroke.Paint.Color;
		return true;
	}
	return false;
}

void UShapeWidget::SetContentArea(EWidgetShapeContentArea InContentArea)
{
	ContentArea = InContentArea;
	if (MyShape.IsValid())
	{
		MyShape->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void UShapeWidget::SetSizeToContent(bool bInSizeToContent)
{
	bSizeToContent = bInSizeToContent;
	if (MyShape.IsValid())
	{
		MyShape->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void UShapeWidget::SetHitTestOutline(bool bInHitTestOutline)
{
	bHitTestOutline = bInHitTestOutline;
	SyncShapeVisibility();
	if (MyShape.IsValid())
	{
		MyShape->Invalidate(EInvalidateWidgetReason::Paint | EInvalidateWidgetReason::Visibility);
	}
}

bool UShapeWidget::GetContentAreaRect(FVector2D& OutTopLeft, FVector2D& OutSize) const
{
	if (!MyShape.IsValid() || !MyShape->GetArrangedContentArea().bIsValid)
	{
		return false;
	}
	const FBox2f& Area = MyShape->GetArrangedContentArea();
	OutTopLeft = FVector2D(Area.Min);
	OutSize = FVector2D(Area.GetSize());
	return true;
}

float UShapeWidget::GetInnerStrokeWidth() const
{
	if (!Stroke.IsDrawn())
	{
		return 0.f;
	}
	switch (Stroke.Align)
	{
	case EWidgetShapeStrokeAlign::Inside: return Stroke.Width;
	case EWidgetShapeStrokeAlign::Center: return Stroke.Width * 0.5f;
	default: return 0.f;
	}
}

bool UShapeWidget::IsContentProperty(const FProperty* Property)
{
	static const FName Content[] =
	{
		GET_MEMBER_NAME_CHECKED(UShapeWidget, ContentArea),
		GET_MEMBER_NAME_CHECKED(UShapeWidget, bSizeToContent),
		GET_MEMBER_NAME_CHECKED(UShapeWidget, bHitTestOutline)
	};
	if (!Property)
	{
		return false;
	}

	// A Curve's Curve Preset is a link to an asset, not part of its look: a Look or a preset holding it would re-link every
	// curve it is put on.
	if (Property->GetOwnerClass() && Property->GetOwnerClass()->IsChildOf(UCurveShape::StaticClass()))
	{
		return Property->GetFName() == GET_MEMBER_NAME_CHECKED(UCurveShape, CurvePreset);
	}
	if (Property->GetOwnerClass() != UShapeWidget::StaticClass())
	{
		return false;
	}
	for (const FName& Name : Content)
	{
		if (Property->GetFName() == Name)
		{
			return true;
		}
	}
	return false;
}

bool UShapeWidget::IsLocalPointInShape(FVector2D LocalPoint) const
{
	if (!MyShape.IsValid())
	{
		return false;
	}
	return WidgetShapeGeometry::IsInside(MyShape->GetPaintedOutline(), FVector2f(static_cast<float>(LocalPoint.X), static_cast<float>(LocalPoint.Y)));
}

FWidgetShapeFrame UShapeWidget::MakeFrame(const FVector2f& LocalSize) const
{
	return FWidgetShapeFrame::Make(LocalSize, Fit == EWidgetShapeFit::KeepAspect, GetNaturalAspect(),
		FVector2f(static_cast<float>(ShapeScale.X), static_cast<float>(ShapeScale.Y)), ShapeRotation);
}

void UShapeWidget::BuildOutline(const FWidgetShapeFrame& InFrame, float Tolerance, FWidgetShapeOutline& Out) const
{
	FWidgetShapePath Path;
	BuildPath(Path);
	WidgetShapeGeometry::FlattenPath(Path, InFrame, Tolerance, Out);
}

void UShapeWidget::BuildStrokeMesh(const FWidgetShapeOutline& InOutline, const TArray<int8>& FillOnLeft, float AntiAlias, float Tolerance, FWidgetShapeMesh& Out) const
{
	WidgetShapeGeometry::BuildStroke(InOutline, FillOnLeft, Stroke, Trim, AntiAlias, Tolerance, Out);
}

uint32 UShapeWidget::GetMeshHash() const
{
	uint32 Hash = GetTypeHash(bFill);
	Hash = HashCombineFast(Hash, Fill.GetLayoutHash());
	Hash = HashCombineFast(Hash, GetTypeHash(Stroke.bEnabled));
	Hash = HashCombineFast(Hash, GetTypeHash(Stroke.Width));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Stroke.Align)));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Stroke.Join)));
	Hash = HashCombineFast(Hash, GetTypeHash(Stroke.MiterLimit));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Stroke.Cap)));
	Hash = HashCombineFast(Hash, GetTypeHash(Stroke.DashLength));
	Hash = HashCombineFast(Hash, GetTypeHash(Stroke.DashGap));
	Hash = HashCombineFast(Hash, GetTypeHash(Stroke.DashOffset));
	Hash = HashCombineFast(Hash, Stroke.Paint.GetLayoutHash());
	Hash = HashCombineFast(Hash, GetTypeHash(Trim.Start));
	Hash = HashCombineFast(Hash, GetTypeHash(Trim.End));
	Hash = HashCombineFast(Hash, GetTypeHash(Trim.Offset));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Fit)));
	Hash = HashCombineFast(Hash, GetTypeHash(ShapeScale));
	Hash = HashCombineFast(Hash, GetTypeHash(ShapeRotation));
	Hash = HashCombineFast(Hash, GetTypeHash(bAntiAlias));
	return Hash;
}

TSharedRef<SWidget> UShapeWidget::RebuildWidget()
{
	MyShape = SNew(SWidgetShape).Shape(this);

	UPanelSlot* ContentSlot = GetContentSlot();
	if (UShapeWidgetSlot* ShapeSlot = Cast<UShapeWidgetSlot>(ContentSlot))
	{
		ShapeSlot->BuildSlot(MyShape.ToSharedRef());
	}
	else if (ContentSlot && ContentSlot->Content)
	{
		MyShape->SetContent(ContentSlot->Content->TakeWidget());
	}

	return MyShape.ToSharedRef();
}

void UShapeWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	SyncShapeVisibility();
	InvalidateShape();
}

void UShapeWidget::SyncShapeVisibility()
{
	// Only while the outline is hit tested, and never in the designer (which picks widgets by their box, and shows hidden
	// ones): otherwise the visibility UMG set - a Blueprint binding included - is left alone.
	if (!MyShape.IsValid() || !bHitTestOutline || IsDesignTime())
	{
		return;
	}
	MyShape->SetRequestedVisibility(UWidget::ConvertSerializedVisibilityToRuntime(GetVisibility()));
}

void UShapeWidget::SetVisibility(ESlateVisibility InVisibility)
{
	Super::SetVisibility(InVisibility);
	SyncShapeVisibility();
}

void UShapeWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyShape.Reset();
}

UClass* UShapeWidget::GetSlotClass() const
{
	return UShapeWidgetSlot::StaticClass();
}

void UShapeWidget::OnSlotAdded(UPanelSlot* InSlot)
{
	if (!MyShape.IsValid())
	{
		return;
	}
	if (UShapeWidgetSlot* ShapeSlot = Cast<UShapeWidgetSlot>(InSlot))
	{
		ShapeSlot->BuildSlot(MyShape.ToSharedRef());
	}
	else
	{
		MyShape->SetContent(InSlot && InSlot->Content ? InSlot->Content->TakeWidget() : SNullWidget::NullWidget);
	}
}

void UShapeWidget::OnSlotRemoved(UPanelSlot* InSlot)
{
	if (MyShape.IsValid())
	{
		MyShape->SetContent(SNullWidget::NullWidget);
	}
}

// =====================================================================================================
// Slot
// =====================================================================================================

void UShapeWidgetSlot::BuildSlot(const TSharedRef<SWidgetShape>& InShape)
{
	SlateShape = InShape;
	InShape->SetContentPadding(Padding);
	InShape->SetContentAlignment(HorizontalAlignment, VerticalAlignment);
	InShape->SetContent(Content ? Content->TakeWidget() : SNullWidget::NullWidget);
}

void UShapeWidgetSlot::SetPadding(FMargin InPadding)
{
	Padding = InPadding;
	if (const TSharedPtr<SWidgetShape> Pinned = SlateShape.Pin())
	{
		Pinned->SetContentPadding(InPadding);
	}
}

void UShapeWidgetSlot::SetHorizontalAlignment(EHorizontalAlignment InHorizontalAlignment)
{
	HorizontalAlignment = InHorizontalAlignment;
	if (const TSharedPtr<SWidgetShape> Pinned = SlateShape.Pin())
	{
		Pinned->SetContentAlignment(HorizontalAlignment, VerticalAlignment);
	}
}

void UShapeWidgetSlot::SetVerticalAlignment(EVerticalAlignment InVerticalAlignment)
{
	VerticalAlignment = InVerticalAlignment;
	if (const TSharedPtr<SWidgetShape> Pinned = SlateShape.Pin())
	{
		Pinned->SetContentAlignment(HorizontalAlignment, VerticalAlignment);
	}
}

void UShapeWidgetSlot::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (const TSharedPtr<SWidgetShape> Pinned = SlateShape.Pin())
	{
		Pinned->SetContentPadding(Padding);
		Pinned->SetContentAlignment(HorizontalAlignment, VerticalAlignment);
	}
}

void UShapeWidgetSlot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	SlateShape.Reset();
}

#if WITH_EDITOR
const FText UShapeWidget::GetPaletteCategory()
{
	return FText::FromString(TEXT("AR GameSystems UI"));
}
#endif

#undef LOCTEXT_NAMESPACE
