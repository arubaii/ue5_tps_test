// Copyright AR GameSystems 2026. All rights reserved.

#include "ShapeWidget.h"

#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"

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
}

FVector2D SWidgetShape::ComputeDesiredSize(float) const
{
	const UShapeWidget* Shape = ShapeWidget.Get();
	return Shape ? Shape->DesiredSize : FVector2D(64.0, 64.0);
}

void SWidgetShape::Rebuild(const UShapeWidget& Shape, const FVector2f& LocalSize, float PixelScale) const
{
	Frame = Shape.MakeFrame(LocalSize);
	const float Tolerance = WidgetShapeGeometry::GetTolerance(PixelScale);
	const float AntiAlias = Shape.bAntiAlias ? 1.f / PixelScale : 0.f;

	Outline.Reset();
	Shape.BuildOutline(Frame, Tolerance, Outline);

	FillMesh.Reset();
	StrokeMesh.Reset();
	TArray<int8> FillOnLeft;
	if (Shape.bFill)
	{
		WidgetShapeGeometry::BuildFill(Outline, AntiAlias, FillMesh, FillOnLeft);
	}
	else
	{
		WidgetShapeGeometry::ClassifyLines(Outline, FillOnLeft);
	}
	WidgetShapeGeometry::BuildStroke(Outline, FillOnLeft, Shape.Stroke, Shape.Trim, AntiAlias, Tolerance, StrokeMesh);

	// A radial gradient's cells about six pixels across on screen: its rings then read round.
	const float CellSize = 6.f / PixelScale;
	WidgetShapeGeometry::CutForGradient(FillMesh, Shape.Fill, Frame, CellSize);
	WidgetShapeGeometry::CutForGradient(StrokeMesh, Shape.Stroke.Paint, Frame, CellSize);
}

void SWidgetShape::PaintMesh(const FWidgetShapeMesh& Mesh, const FWidgetShapePaint& Paint, const FGeometry& AllottedGeometry,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	using namespace ShapeWidgetPrivate;

	const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();

	const bool bImage = Paint.Type == EWidgetShapePaintType::Image && Paint.Image.GetResourceObject() != nullptr;
	const FSlateBrush& Brush = bImage ? Paint.Image : GetUntexturedBrush();
	const FLinearColor BrushTint = bImage ? Paint.Image.GetTint(InWidgetStyle) : FLinearColor::White;

	// An image from an atlas covers only part of its texture: its UV Region says which.
	const FBox2f UVRegion = Brush.GetUVRegion();
	const bool bHasUVRegion = bImage && UVRegion.bIsValid && UVRegion.GetArea() > 0.f;

	Vertices.SetNumUninitialized(Mesh.Vertices.Num());
	for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
	{
		const FWidgetShapeMeshVertex& Vertex = Mesh.Vertices[Index];
		const FVector2f BoxPoint = Frame.ToBox(Vertex.Position);
		FLinearColor Color = Paint.Evaluate(BoxPoint) * BrushTint * WidgetTint;
		Color.A *= Vertex.Coverage;
		const FVector2f UV = bHasUVRegion ? UVRegion.Min + BoxPoint * UVRegion.GetSize() : BoxPoint;
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

	uint32 Hash = Shape->GetMeshHash();
	Hash = HashCombineFast(Hash, GetTypeHash(LocalSize));
	Hash = HashCombineFast(Hash, GetTypeHash(PixelScale));
	if (!bBuilt || Hash != BuiltHash)
	{
		Rebuild(*Shape, LocalSize, PixelScale);
		BuiltHash = Hash;
		bBuilt = true;
	}

	if (Shape->bFill && !FillMesh.IsEmpty())
	{
		PaintMesh(FillMesh, Shape->Fill, AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle);
	}
	if (!StrokeMesh.IsEmpty())
	{
		PaintMesh(StrokeMesh, Shape->Stroke.Paint, AllottedGeometry, OutDrawElements, LayerId + 1, InWidgetStyle);
	}
	return LayerId + 1;
}

// =====================================================================================================
// UMG widget
// =====================================================================================================

UShapeWidget::UShapeWidget()
{
	Fill.Color = FLinearColor(0.9f, 0.9f, 0.9f, 1.f);
}

void UShapeWidget::InvalidateShape()
{
	if (MyShape.IsValid())
	{
		MyShape->Invalidate(EInvalidateWidgetReason::Paint);
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
	return MyShape.ToSharedRef();
}

void UShapeWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	InvalidateShape();
}

void UShapeWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyShape.Reset();
}

#if WITH_EDITOR
const FText UShapeWidget::GetPaletteCategory()
{
	return FText::FromString(TEXT("AR GameSystems UI"));
}
#endif

#undef LOCTEXT_NAMESPACE
