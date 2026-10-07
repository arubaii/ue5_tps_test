// Copyright AR GameSystems 2026. All rights reserved.

#include "FlowListScrollBar.h"

#include "Algo/BinarySearch.h"
#include "FlowList.h"
#include "FlowListTypes.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"
#include "Rendering/DrawElements.h"

#define LOCTEXT_NAMESPACE "FlowListScrollBar"

namespace FlowListScrollBarPrivate
{
	/** A pill: a rounded box whose ends are half circles, whichever way it is long. */
	void MakePill(FSlateBrush& Brush, const FLinearColor& Color)
	{
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Color);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		Brush.OutlineSettings.Width = 0.f;
	}

	/** How often a bar with no list looks for one, in seconds - the list may be added later. */
	constexpr double SearchInterval = 0.5;

	/** The brush every flat or gradient line draws with: no texture, so the vertex colors are all there is. */
	const FSlateBrush& GetUntexturedBrush()
	{
		static const FSlateBrush Brush;
		return Brush;
	}

	/** How many screen pixels one local unit covers, as SWidgetShape measures it. */
	float GetPixelScale(const FGeometry& Geometry)
	{
		const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
		const FVector2f AxisX = Transform.TransformVector(FVector2f(1.f, 0.f));
		const FVector2f AxisY = Transform.TransformVector(FVector2f(0.f, 1.f));
		return FMath::Max(FMath::Max(AxisX.Size(), AxisY.Size()), 0.01f);
	}

	/** Hover Tint as far as the pointer has brought it, times Pressed Tint while held. */
	FLinearColor MakeTint(const FLinearColor& HoverTint, const FLinearColor& PressedTint, float Hover, bool bPressed, float Opacity)
	{
		FLinearColor Tint = FMath::Lerp(FLinearColor::White, HoverTint, Hover);
		if (bPressed)
		{
			Tint *= PressedTint;
		}
		Tint.A *= Opacity;
		return Tint;
	}

	/** Across the path at a point where it runs along Tangent: the side a positive Curve Bow bows to. */
	FVector2f AcrossPath(const FVector2f& Tangent, bool bVertical)
	{
		return bVertical ? FVector2f(Tangent.Y, -Tangent.X) : FVector2f(-Tangent.Y, Tangent.X);
	}

	/** A turn by Radians about Pivot (local units), for a box drawn along the path. */
	FSlateRenderTransform MakeTurn(float Radians, const FVector2f& Pivot)
	{
		const float Cos = FMath::Cos(Radians);
		const float Sin = FMath::Sin(Radians);
		const FSlateRenderTransform Turn(FMatrix2x2f(Cos, Sin, -Sin, Cos), FVector2f::ZeroVector);
		return ::Concatenate(::Concatenate(FSlateRenderTransform(-Pivot), Turn), FSlateRenderTransform(Pivot));
	}

	/** Index - Scroll, the short way round a looping list of Count. */
	float DistanceTo(int32 Index, float Scroll, int32 Count, bool bLoop)
	{
		float Distance = static_cast<float>(Index) - Scroll;
		if (bLoop && Count > 0)
		{
			Distance = FMath::Fmod(Distance, static_cast<float>(Count));
			if (Distance < -Count * 0.5f)
			{
				Distance += Count;
			}
			else if (Distance >= Count * 0.5f)
			{
				Distance -= Count;
			}
		}
		return Distance;
	}

	/** Items shown in the designer when the bar has no list to follow, so its look can be seen. */
	constexpr int32 DesignerPreviewCount = 5;
}

FFlowListScrollBarLine::FFlowListScrollBarLine(const FLinearColor& Color, float InThickness, float InHoverThickness)
	: Thickness(InThickness)
	, HoverThickness(InHoverThickness)
{
	Paint.Color = Color;
}

// =====================================================================================================
// Path
// =====================================================================================================

FVector2f SFlowListScrollBar::FBarPath::At(float Distance, FVector2f* OutTangent) const
{
	if (Points.Num() < 2)
	{
		if (OutTangent)
		{
			*OutTangent = FVector2f(1.f, 0.f);
		}
		return Points.Num() == 1 ? Points[0] : FVector2f::ZeroVector;
	}
	const float Clamped = FMath::Clamp(Distance, 0.f, Total);
	const int32 Segment = FMath::Clamp(Algo::UpperBound(Distances, Clamped) - 1, 0, Points.Num() - 2);
	const float SegmentLength = Distances[Segment + 1] - Distances[Segment];
	const float Alpha = SegmentLength > 0.f ? (Clamped - Distances[Segment]) / SegmentLength : 0.f;
	if (OutTangent)
	{
		*OutTangent = (Points[Segment + 1] - Points[Segment]).GetSafeNormal();
	}
	return FMath::Lerp(Points[Segment], Points[Segment + 1], Alpha);
}

float SFlowListScrollBar::FBarPath::Project(const FVector2f& Point) const
{
	float Best = 0.f;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 Segment = 0; Segment + 1 < Points.Num(); ++Segment)
	{
		const FVector2f A = Points[Segment];
		const FVector2f Delta = Points[Segment + 1] - A;
		const float LengthSquared = Delta.SizeSquared();
		const float Alpha = LengthSquared > 0.f ? FMath::Clamp(FVector2f::DotProduct(Point - A, Delta) / LengthSquared, 0.f, 1.f) : 0.f;
		const float DistanceSquared = FVector2f::DistSquared(A + Delta * Alpha, Point);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = FMath::Lerp(Distances[Segment], Distances[Segment + 1], Alpha);
		}
	}
	return Best;
}

void SFlowListScrollBar::FBarPath::Slice(float From, float To, TArray<FVector2f>& OutPoints) const
{
	OutPoints.Reset();
	if (Points.Num() < 2)
	{
		return;
	}
	const float Low = FMath::Clamp(FMath::Min(From, To), 0.f, Total);
	const float High = FMath::Clamp(FMath::Max(From, To), 0.f, Total);
	OutPoints.Add(At(Low));
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (Distances[Index] > Low && Distances[Index] < High)
		{
			OutPoints.Add(Points[Index]);
		}
	}
	OutPoints.Add(At(High));
}

// =====================================================================================================
// Slate
// =====================================================================================================

void SFlowListScrollBar::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	SetCanTick(false);
	LastActivityTime = FPlatformTime::Seconds();
}

void SFlowListScrollBar::NotifyActivity()
{
	LastActivityTime = FPlatformTime::Seconds();
	Invalidate(EInvalidateWidgetReason::Paint);
	WakeUp();
}

bool SFlowListScrollBar::IsVertical() const
{
	const UFlowListScrollBar* Bar = Owner.Get();
	if (!Bar)
	{
		return true;
	}
	if (Bar->bMatchListOrientation)
	{
		if (const UFlowList* List = Bar->GetFlowList())
		{
			return List->ListLayout.Orientation == Orient_Vertical;
		}
	}
	return Bar->Orientation == Orient_Vertical;
}

float SFlowListScrollBar::GetShownOpacity() const
{
	const UFlowListScrollBar* Bar = Owner.Get();
	if (!Bar || !Bar->bAutoHide || bDragging || IsHovered() || Bar->IsDesignTime())
	{
		return 1.f;
	}
	const double Since = FPlatformTime::Seconds() - LastActivityTime;
	if (Since <= Bar->HideDelay)
	{
		return 1.f;
	}
	return Bar->FadeTime > 0.f ? FMath::Clamp(1.f - static_cast<float>((Since - Bar->HideDelay) / Bar->FadeTime), 0.f, 1.f) : 0.f;
}

float SFlowListScrollBar::GetListFraction() const
{
	const UFlowListScrollBar* Bar = Owner.Get();
	const UFlowList* List = Bar ? Bar->GetFlowList() : nullptr;
	return List ? FMath::Clamp(List->GetScrollFraction(), 0.f, 1.f) : 0.f;
}

float SFlowListScrollBar::GetShownFraction() const
{
	const UFlowListScrollBar* Bar = Owner.Get();
	const bool bSmooth = Bar && Bar->bSmoothThumb && Bar->Style == EFlowListScrollBarStyle::Bar && !Bar->IsDesignTime();
	return bSmooth && bSmoothPlaced && !bDragging ? FMath::Clamp(SmoothFraction, 0.f, 1.f) : GetListFraction();
}

void SFlowListScrollBar::BuildPath(const FVector2f& Size, FBarPath& Out) const
{
	using namespace FlowListScrollBarPrivate;

	Out.Points.Reset();
	Out.Distances.Reset();
	Out.Total = 0.f;

	const UFlowListScrollBar* Bar = Owner.Get();
	const bool bVertical = IsVertical();
	const float Length = bVertical ? Size.Y : Size.X;
	const float Cross = bVertical ? Size.X : Size.Y;
	if (!Bar || Length <= 0.f)
	{
		return;
	}

	// Room at the ends for what is drawn past them - a rounded or squared end, half a knob - so nothing pokes out of the bar.
	float EndRoom = Bar->EndPadding;
	if (Bar->Style == EFlowListScrollBarStyle::Bar)
	{
		auto CapRoom = [](const FFlowListScrollBarLine& Line)
		{
			return Line.bShow && Line.Cap != EWidgetShapeLineCap::Butt ? FMath::Max(Line.Thickness, Line.HoverThickness) * 0.5f + Line.OutlineWidth : 0.f;
		};
		float Extra = FMath::Max(CapRoom(Bar->Track), CapRoom(Bar->Fill));
		if (Bar->ThumbKind == EFlowListScrollBarThumbKind::Knob)
		{
			const float Knob = static_cast<float>(FMath::Max(FMath::Max(Bar->KnobSize.X, Bar->KnobSize.Y), FMath::Max(Bar->KnobHoverSize.X, Bar->KnobHoverSize.Y)));
			Extra = FMath::Max(Extra, Knob * 0.5f);
		}
		else
		{
			Extra = FMath::Max(Extra, CapRoom(Bar->Thumb));
		}
		EndRoom += Extra;
	}
	EndRoom = FMath::Clamp(EndRoom, 0.f, FMath::Max(Length * 0.5f - 0.5f, 0.f));

	const float Start = EndRoom;
	const float Span = FMath::Max(Length - 2.f * EndRoom, 0.f);
	const float Middle = Cross * 0.5f;

	auto AddPoint = [&Out](const FVector2f& Local)
	{
		if (Out.Points.Num() == 0 || FVector2f::DistSquared(Out.Points.Last(), Local) > 1e-6f)
		{
			Out.Points.Add(Local);
		}
	};

	if (Bar->PathShape == EFlowListScrollBarPath::Spline)
	{
		// Drawn on the canvas in fractions of the bar - the same curve a Flow List's Spline path makes of its points.
		TArray<FVector2f> Curve;
		FlowListTypes::BuildSplineCurve(Bar->SplinePoints, Size, Curve);
		for (const FVector2f& Point : Curve)
		{
			AddPoint(Point);
		}
	}
	else
	{
		// Along and across the bar first, then turned to its orientation - and, for a Curve, by its rotation.
		const bool bCurve = Bar->PathShape == EFlowListScrollBarPath::Curve;
		const float Chord = Span * (bCurve ? FMath::Clamp(Bar->CurveLength, 0.05f, 1.f) : 1.f);
		const float ChordStart = Start + (Span - Chord) * 0.5f;
		const float Half = Chord * 0.5f;
		const float Bow = bCurve ? FMath::Min(FMath::Abs(Bar->CurveBow), Half) : 0.f;

		TArray<FVector2f, TInlineAllocator<64>> AlongCross;
		if (Bow >= 0.25f && Half > 0.f)
		{
			// A circle's arc through both ends, its middle Bow out from the line between them; the bow is centered across.
			const float Radius = (Half * Half + Bow * Bow) / (2.f * Bow);
			const float Sweep = 4.f * FMath::Atan(Bow / Half);
			const float CenterCross = Middle + Bow * 0.5f - Radius;
			const int32 Segments = FMath::Clamp(FMath::CeilToInt(FMath::RadiansToDegrees(Sweep) / 3.f), 8, 96);
			for (int32 Index = 0; Index <= Segments; ++Index)
			{
				const float Phi = -Sweep * 0.5f + Sweep * static_cast<float>(Index) / static_cast<float>(Segments);
				float Across = CenterCross + Radius * FMath::Cos(Phi);
				if (Bar->CurveBow < 0.f)
				{
					Across = Cross - Across;
				}
				AlongCross.Add(FVector2f(ChordStart + Half + Radius * FMath::Sin(Phi), Across));
			}
		}
		else
		{
			AlongCross.Add(FVector2f(ChordStart, Middle));
			AlongCross.Add(FVector2f(ChordStart + Chord, Middle));
		}

		const float Turn = bCurve ? FMath::DegreesToRadians(Bar->CurveRotation) : 0.f;
		const float Cos = FMath::Cos(Turn);
		const float Sin = FMath::Sin(Turn);
		const FVector2f Pivot = Size * 0.5f;
		for (const FVector2f& Point : AlongCross)
		{
			const FVector2f Local = bVertical ? FVector2f(Point.Y, Point.X) : Point;
			const FVector2f FromPivot = Local - Pivot;
			AddPoint(Pivot + FVector2f(FromPivot.X * Cos - FromPivot.Y * Sin, FromPivot.X * Sin + FromPivot.Y * Cos));
		}
	}
	if (Out.Points.Num() == 1)
	{
		Out.Points.Add(Out.Points[0]);
	}

	Out.Distances.SetNumUninitialized(Out.Points.Num());
	float Walked = 0.f;
	for (int32 Index = 0; Index < Out.Points.Num(); ++Index)
	{
		if (Index > 0)
		{
			Walked += FVector2f::Distance(Out.Points[Index - 1], Out.Points[Index]);
		}
		Out.Distances[Index] = Walked;
	}
	Out.Total = Walked;
}

SFlowListScrollBar::FThumbPlace SFlowListScrollBar::PlaceThumb(const FBarPath& Path) const
{
	FThumbPlace Place;
	const UFlowListScrollBar* Bar = Owner.Get();
	const UFlowList* List = Bar ? Bar->GetFlowList() : nullptr;
	const float Fraction = GetShownFraction();
	if (Bar && Bar->ThumbKind == EFlowListScrollBarThumbKind::Knob)
	{
		Place.Room = Path.Total;
		Place.Center = Path.Total * Fraction;
		Place.Start = Place.Center;
		return Place;
	}
	const float View = List ? List->GetViewFraction() : 0.3f;
	const float MinThumb = Bar ? Bar->MinThumbLength : 20.f;
	Place.Length = FMath::Clamp(FMath::Max(Path.Total * View, MinThumb), 0.f, Path.Total);
	Place.Room = Path.Total - Place.Length;
	Place.Start = Place.Room * Fraction;
	Place.Center = Place.Start + Place.Length * 0.5f;
	return Place;
}

FVector2D SFlowListScrollBar::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const UFlowListScrollBar* Bar = Owner.Get();
	if (!Bar)
	{
		return FVector2D(8.0, 64.0);
	}
	const bool bVertical = IsVertical();

	// How far the path bows across for a bar of the usual length - arcs and curves need the room.
	constexpr double UsualLength = 64.0;
	double Bow = 0.0;
	if (Bar->PathShape == EFlowListScrollBarPath::Curve)
	{
		Bow = FMath::Min(FMath::Abs(static_cast<double>(Bar->CurveBow)), UsualLength * FMath::Clamp(static_cast<double>(Bar->CurveLength), 0.05, 1.0) * 0.5);
	}
	else if (Bar->PathShape == EFlowListScrollBarPath::Spline)
	{
		// Any shape: a square to draw it in.
		return FVector2D(UsualLength, UsualLength);
	}

	if (Bar->Style == EFlowListScrollBarStyle::Dots)
	{
		const UFlowList* List = Bar->GetFlowList();
		const int32 Count = FMath::Min(List ? List->GetItemCount() : 3, Bar->MaxDots);
		const double Along = Bar->ActiveDotSize.X + (Bar->DotSize.X + Bar->DotSpacing) * FMath::Max(Count - 1, 0);
		const double Across = FMath::Max(Bar->DotSize.Y, Bar->ActiveDotSize.Y) + Bow;
		return bVertical ? FVector2D(Across, Along) : FVector2D(Along, Across);
	}

	auto LineAcross = [](const FFlowListScrollBarLine& Line)
	{
		return Line.bShow ? static_cast<double>(FMath::Max(Line.Thickness, Line.HoverThickness) + 2.f * Line.OutlineWidth) : 0.0;
	};
	double Across = FMath::Max(LineAcross(Bar->Track), LineAcross(Bar->Fill));
	if (Bar->ThumbKind == EFlowListScrollBarThumbKind::Knob)
	{
		Across = FMath::Max(Across, FMath::Max(FMath::Max(Bar->KnobSize.X, Bar->KnobSize.Y), FMath::Max(Bar->KnobHoverSize.X, Bar->KnobHoverSize.Y)));
	}
	else
	{
		Across = FMath::Max(Across, LineAcross(Bar->Thumb));
	}
	if (Bar->bShowTicks)
	{
		Across = FMath::Max(Across, static_cast<double>(FMath::Max(Bar->TickLength, Bar->ActiveTickLength) + 2.f * FMath::Abs(Bar->TickOffset)));
	}
	Across = FMath::Max(Across, 1.0) + Bow;
	return bVertical ? FVector2D(Across, UsualLength) : FVector2D(UsualLength, Across);
}

// =====================================================================================================
// Painting
// =====================================================================================================

void SFlowListScrollBar::PaintMesh(const FWidgetShapeMesh& Mesh, const FSlateBrush* ImageBrush, const FWidgetShapeFrame& Frame, const FGeometry& Geometry,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, TFunctionRef<FLinearColor(int32, const FVector2f&)> ColorAt) const
{
	using namespace FlowListScrollBarPrivate;

	if (Mesh.IsEmpty())
	{
		return;
	}
	const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
	const FSlateBrush& Brush = ImageBrush ? *ImageBrush : GetUntexturedBrush();

	// An image from an atlas covers only part of its texture: its UV Region says which.
	const FBox2f UVRegion = Brush.GetUVRegion();
	const bool bHasUVRegion = ImageBrush && UVRegion.bIsValid && UVRegion.GetArea() > 0.f;

	Vertices.SetNumUninitialized(Mesh.Vertices.Num());
	for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
	{
		const FWidgetShapeMeshVertex& Vertex = Mesh.Vertices[Index];
		const FVector2f BoxPoint = Frame.ToBox(Vertex.Position);
		FLinearColor Color = ColorAt(Index, BoxPoint);
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

void SFlowListScrollBar::PaintLine(const TArray<FVector2f>& Points, const FFlowListScrollBarLine& Line, float Thickness, const FLinearColor& Tint,
	const FGeometry& Geometry, const FWidgetShapeFrame& Frame, float PixelScale, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& WidgetStyle) const
{
	if (Points.Num() < 2 || Thickness <= 0.f)
	{
		return;
	}

	FWidgetShapeOutline Outline;
	FWidgetShapeLine& Contour = Outline.Lines.AddDefaulted_GetRef();
	Contour.Points = Points;
	Contour.bClosed = false;
	const TArray<int8> FillOnLeft = { 0 };

	const float Tolerance = WidgetShapeGeometry::GetTolerance(PixelScale);
	const float AntiAlias = 1.f / PixelScale;
	const FLinearColor WidgetTint = WidgetStyle.GetColorAndOpacityTint();

	FWidgetShapeStroke Stroke;
	Stroke.bEnabled = true;
	Stroke.Align = EWidgetShapeStrokeAlign::Center;
	Stroke.Join = EWidgetShapeLineJoin::Round;
	Stroke.Cap = Line.Cap;
	Stroke.DashLength = Line.DashLength;
	Stroke.DashGap = Line.DashGap;

	// The outline: the same line, wider, underneath.
	if (Line.OutlineWidth > 0.f && Line.OutlineColor.A > 0.f)
	{
		FWidgetShapeStroke Around = Stroke;
		Around.Width = Thickness + 2.f * Line.OutlineWidth;
		FWidgetShapeMesh Mesh;
		WidgetShapeGeometry::BuildStroke(Outline, FillOnLeft, Around, FWidgetShapeTrim(), AntiAlias, Tolerance, Mesh);
		FLinearColor OutlineColor = Line.OutlineColor * WidgetTint;
		OutlineColor.A *= Tint.A;
		PaintMesh(Mesh, nullptr, Frame, Geometry, OutDrawElements, LayerId, [&OutlineColor](int32, const FVector2f&) { return OutlineColor; });
	}

	Stroke.Width = Thickness;
	FWidgetShapeMesh Mesh;
	WidgetShapeGeometry::BuildStroke(Outline, FillOnLeft, Stroke, FWidgetShapeTrim(), AntiAlias, Tolerance, Mesh);

	// A gradient's cells about six pixels across on screen, as a shape's.
	WidgetShapeGeometry::CutForGradient(Mesh, Line.Paint, Frame, 6.f / PixelScale);

	const bool bImage = Line.Paint.Type == EWidgetShapePaintType::Image && Line.Paint.Image.GetResourceObject() != nullptr;
	const FLinearColor BrushTint = bImage ? Line.Paint.Image.GetTint(WidgetStyle) : FLinearColor::White;
	const FLinearColor Multiply = BrushTint * WidgetTint * Tint;
	PaintMesh(Mesh, bImage ? &Line.Paint.Image : nullptr, Frame, Geometry, OutDrawElements, LayerId + 1,
		[&Line, &Multiply](int32, const FVector2f& BoxPoint) { return Line.Paint.Evaluate(BoxPoint) * Multiply; });
}

int32 SFlowListScrollBar::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const UFlowListScrollBar* Bar = Owner.Get();
	if (!Bar)
	{
		return LayerId;
	}
	const float Opacity = GetShownOpacity();
	if (Opacity <= 0.f)
	{
		return LayerId;
	}
	return Bar->Style == EFlowListScrollBarStyle::Dots
		? PaintDots(AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle, Opacity)
		: PaintBar(AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle, Opacity);
}

int32 SFlowListScrollBar::PaintBar(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, float Opacity) const
{
	using namespace FlowListScrollBarPrivate;

	const UFlowListScrollBar* Bar = Owner.Get();
	const UFlowList* List = Bar->GetFlowList();
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	FBarPath Path;
	BuildPath(Size, Path);
	if (Path.Points.Num() < 2)
	{
		return LayerId;
	}

	const bool bVertical = IsVertical();
	const FWidgetShapeFrame Frame = FWidgetShapeFrame::Make(Size, /*bKeepAspect=*/false, 1.f, FVector2f(1.f, 1.f), 0.f);
	const float PixelScale = GetPixelScale(AllottedGeometry);
	const int32 Count = List ? List->GetItemCount() : (Bar->IsDesignTime() ? DesignerPreviewCount : 0);
	const bool bHasItems = Count > 0;

	auto ThicknessOf = [this](const FFlowListScrollBarLine& Line) { return FMath::Lerp(Line.Thickness, Line.HoverThickness, HoverAmount); };
	auto TintOf = [this, Opacity](const FFlowListScrollBarLine& Line) { return MakeTint(Line.HoverTint, Line.PressedTint, HoverAmount, bDragging, Opacity); };

	// Track, then what fills it, then the ticks, then the thumb on top - each line two layers: its outline and itself.
	if (Bar->Track.bShow)
	{
		PaintLine(Path.Points, Bar->Track, ThicknessOf(Bar->Track), TintOf(Bar->Track), AllottedGeometry, Frame, PixelScale, OutDrawElements, LayerId, InWidgetStyle);
	}

	const FThumbPlace Place = PlaceThumb(Path);
	const bool bKnob = Bar->ThumbKind == EFlowListScrollBarThumbKind::Knob;
	TArray<FVector2f> Slice;

	if (Bar->Fill.bShow && bHasItems)
	{
		const float FillTo = bKnob ? Place.Center : Place.Start + Place.Length;
		if (FillTo > 0.01f)
		{
			Path.Slice(0.f, FillTo, Slice);
			PaintLine(Slice, Bar->Fill, ThicknessOf(Bar->Fill), TintOf(Bar->Fill), AllottedGeometry, Frame, PixelScale, OutDrawElements, LayerId + 2, InWidgetStyle);
		}
	}

	// A tick at each item's place - where the thumb's middle is when the list is at that item - lit as the list reaches it.
	if (Bar->bShowTicks && Count > 1)
	{
		const bool bLoop = List && List->Scrolling.bLoop;
		const float Scroll = List ? List->GetScrollPosition() : 0.f;
		const int32 Every = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Count) / static_cast<float>(FMath::Max(Bar->MaxTicks, 2))));
		const float Steps = static_cast<float>(bLoop ? Count : Count - 1);

		FWidgetShapeStroke TickStroke;
		TickStroke.bEnabled = true;
		TickStroke.Width = Bar->TickWidth;
		TickStroke.Cap = EWidgetShapeLineCap::Butt;
		const TArray<int8> FillOnLeft = { 0 };
		const float Tolerance = WidgetShapeGeometry::GetTolerance(PixelScale);

		FWidgetShapeMesh Mesh;
		TArray<float> VertexLight;
		for (int32 Index = 0; Index < Count; Index += Every)
		{
			const float Fraction = static_cast<float>(Index) / FMath::Max(Steps, 1.f);
			const float Along = bKnob ? Path.Total * Fraction : Place.Room * Fraction + Place.Length * 0.5f;
			const float Light = FMath::Clamp(1.f - FMath::Abs(DistanceTo(Index, Scroll, Count, bLoop)) / static_cast<float>(Every), 0.f, 1.f);
			const float Length = FMath::Lerp(Bar->TickLength, Bar->ActiveTickLength, Light);

			FVector2f Tangent;
			const FVector2f Point = Path.At(Along, &Tangent);
			const FVector2f Across = AcrossPath(Tangent, bVertical);
			const FVector2f Middle = Point + Across * Bar->TickOffset;

			FWidgetShapeOutline Tick;
			FWidgetShapeLine& Line = Tick.Lines.AddDefaulted_GetRef();
			Line.bClosed = false;
			Line.Points = { Middle - Across * (Length * 0.5f), Middle + Across * (Length * 0.5f) };
			WidgetShapeGeometry::BuildStroke(Tick, FillOnLeft, TickStroke, FWidgetShapeTrim(), 1.f / PixelScale, Tolerance, Mesh);
			while (VertexLight.Num() < Mesh.Vertices.Num())
			{
				VertexLight.Add(Light);
			}
		}

		const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
		PaintMesh(Mesh, nullptr, Frame, AllottedGeometry, OutDrawElements, LayerId + 4, [&](int32 Vertex, const FVector2f&)
		{
			FLinearColor Color = FMath::Lerp(Bar->TickColor, Bar->ActiveTickColor, VertexLight.IsValidIndex(Vertex) ? VertexLight[Vertex] : 0.f) * WidgetTint;
			Color.A *= Opacity;
			return Color;
		});
	}

	if (bHasItems)
	{
		if (bKnob)
		{
			FVector2f Tangent;
			const FVector2f Point = Path.At(Place.Center, &Tangent);
			const FVector2f KnobSize(FMath::Lerp(Bar->KnobSize, Bar->KnobHoverSize, static_cast<double>(HoverAmount)));
			const FSlateLayoutTransform Layout(Point - KnobSize * 0.5f);
			const FGeometry KnobGeometry = Bar->bAlignKnobToPath
				? AllottedGeometry.MakeChild(KnobSize, Layout, MakeTurn(FMath::Atan2(Tangent.Y, Tangent.X), KnobSize * 0.5f), FVector2f::ZeroVector)
				: AllottedGeometry.MakeChild(KnobSize, Layout);
			const FLinearColor Tint = Bar->KnobBrush.GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint()
				* MakeTint(Bar->KnobHoverTint, Bar->KnobPressedTint, HoverAmount, bDragging, Opacity);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 6, KnobGeometry.ToPaintGeometry(), &Bar->KnobBrush, ESlateDrawEffect::None, Tint);
		}
		else if (Bar->Thumb.bShow)
		{
			Path.Slice(Place.Start, Place.Start + Place.Length, Slice);
			PaintLine(Slice, Bar->Thumb, ThicknessOf(Bar->Thumb), TintOf(Bar->Thumb), AllottedGeometry, Frame, PixelScale, OutDrawElements, LayerId + 5, InWidgetStyle);
		}
	}
	return LayerId + 6;
}

int32 SFlowListScrollBar::PaintDots(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, float Opacity) const
{
	using namespace FlowListScrollBarPrivate;

	// Each grows and lights as the list nears its item, gliding from one to the next between items - laid along the path.
	const UFlowListScrollBar* Bar = Owner.Get();
	const UFlowList* List = Bar->GetFlowList();
	DotHits.Reset();
	const int32 Count = List ? List->GetItemCount() : (Bar->IsDesignTime() ? DesignerPreviewCount : 0);
	if (Count <= 0)
	{
		return LayerId;
	}
	FBarPath Path;
	BuildPath(FVector2f(AllottedGeometry.GetLocalSize()), Path);
	if (Path.Points.Num() < 2)
	{
		return LayerId;
	}
	const bool bLoop = List && List->Scrolling.bLoop && Count > 1;
	const float Scroll = List ? List->GetScrollPosition() : 0.f;

	// A long list shows the dots round the current item; those at the window's edges shrink to say there are more.
	const int32 Shown = FMath::Min(Count, FMath::Max(Bar->MaxDots, 3));
	int32 First = 0;
	if (Count > Shown)
	{
		int32 Center = FMath::RoundToInt(Scroll);
		Center = ((Center % Count) + Count) % Count;
		First = FMath::Clamp(Center - Shown / 2, 0, Count - Shown);
	}
	const int32 Last = First + Shown - 1;

	struct FDot
	{
		int32 Index;
		FVector2f Size;
		FLinearColor Color;
	};
	TArray<FDot, TInlineAllocator<32>> Dots;
	float Total = 0.f;
	for (int32 Index = First; Index <= Last; ++Index)
	{
		const float Active = FMath::Clamp(1.f - FMath::Abs(DistanceTo(Index, Scroll, Count, bLoop)), 0.f, 1.f);
		float Edge = 1.f;
		if (Count > Shown)
		{
			const bool bMoreBefore = First > 0;
			const bool bMoreAfter = Last < Count - 1;
			if ((Index == First && bMoreBefore) || (Index == Last && bMoreAfter))
			{
				Edge = 0.5f;
			}
			else if ((Index == First + 1 && bMoreBefore) || (Index == Last - 1 && bMoreAfter))
			{
				Edge = 0.75f;
			}
		}
		// In the path's own frame: X along it, Y across.
		const FVector2f DotSize = FMath::Lerp(FVector2f(Bar->DotSize), FVector2f(Bar->ActiveDotSize), Active) * Edge;
		const FLinearColor Color = FMath::Lerp(Bar->DotColor, Bar->ActiveDotColor, Active) * Bar->DotBrush.GetTint(InWidgetStyle);
		Dots.Add({ Index, DotSize, Color });
		Total += DotSize.X;
	}
	Total += Bar->DotSpacing * static_cast<float>(FMath::Max(Dots.Num() - 1, 0));

	const bool bVertical = IsVertical();
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint() * MakeTint(Bar->DotHoverTint, FLinearColor::White, HoverAmount, false, Opacity);
	float Along = (Path.Total - Total) * 0.5f;
	for (const FDot& Dot : Dots)
	{
		FVector2f Tangent;
		const FVector2f Middle = Path.At(Along + Dot.Size.X * 0.5f, &Tangent);

		// Laid along the path: X along it. Without turning, a vertical bar's dots are stood on end instead.
		const FVector2f Drawn = Bar->bAlignDotsToPath || !bVertical ? Dot.Size : FVector2f(Dot.Size.Y, Dot.Size.X);
		const FSlateLayoutTransform Layout(Middle - Drawn * 0.5f);
		const float Angle = FMath::Atan2(Tangent.Y, Tangent.X);
		const FGeometry DotGeometry = Bar->bAlignDotsToPath && FMath::Abs(Angle) > KINDA_SMALL_NUMBER
			? AllottedGeometry.MakeChild(Drawn, Layout, MakeTurn(Angle, Drawn * 0.5f), FVector2f::ZeroVector)
			: AllottedGeometry.MakeChild(Drawn, Layout);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, DotGeometry.ToPaintGeometry(), &Bar->DotBrush, ESlateDrawEffect::None, Dot.Color * Tint);

		// A little more than the dot itself, so small dots are easy to hit.
		DotHits.Add({ Dot.Index, Middle, FMath::Max(Dot.Size.X, Dot.Size.Y) * 0.5f + 4.f });
		Along += Dot.Size.X + Bar->DotSpacing;
	}
	return LayerId;
}

// =====================================================================================================
// Input
// =====================================================================================================

float SFlowListScrollBar::FractionAt(const FGeometry& Geometry, const FVector2f& Absolute) const
{
	FBarPath Path;
	BuildPath(FVector2f(Geometry.GetLocalSize()), Path);
	const FThumbPlace Place = PlaceThumb(Path);
	const float Along = Path.Project(FVector2f(Geometry.AbsoluteToLocal(Absolute))) - GrabOffset;
	return Place.Room > 0.f ? FMath::Clamp(Along / Place.Room, 0.f, 1.f) : 0.f;
}

FReply SFlowListScrollBar::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UFlowListScrollBar* Bar = Owner.Get();
	UFlowList* List = Bar ? Bar->GetFlowList() : nullptr;
	if (!Bar || !List || !Bar->bInteractive || Bar->IsDesignTime() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	const FVector2f Local(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	if (Bar->Style == EFlowListScrollBarStyle::Dots)
	{
		for (const FDotHit& Dot : DotHits)
		{
			if (FVector2f::DistSquared(Dot.Center, Local) <= FMath::Square(Dot.Radius))
			{
				List->ScrollToIndex(Dot.Index, true);
				NotifyActivity();
				return FReply::Handled();
			}
		}
		return FReply::Unhandled();
	}

	// On the thumb: held where it was taken. On the track: the thumb jumps under the pointer and is held by its middle.
	FBarPath Path;
	BuildPath(FVector2f(MyGeometry.GetLocalSize()), Path);
	const FThumbPlace Place = PlaceThumb(Path);
	const float Along = Path.Project(Local);
	bool bOnThumb = false;
	if (Bar->ThumbKind == EFlowListScrollBarThumbKind::Knob)
	{
		const float Reach = static_cast<float>(FMath::Max(Bar->KnobHoverSize.X, Bar->KnobHoverSize.Y)) * 0.5f;
		bOnThumb = FMath::Abs(Along - Place.Center) <= Reach;
		GrabOffset = bOnThumb ? Along - Place.Center : 0.f;
	}
	else
	{
		bOnThumb = Along >= Place.Start && Along <= Place.Start + Place.Length;
		GrabOffset = bOnThumb ? Along - Place.Start : Place.Length * 0.5f;
	}
	bDragging = true;
	List->SetScrollFraction(FractionAt(MyGeometry, FVector2f(MouseEvent.GetScreenSpacePosition())), /*bAnimate=*/!bOnThumb);
	NotifyActivity();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SFlowListScrollBar::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging)
	{
		return FReply::Unhandled();
	}
	UFlowListScrollBar* Bar = Owner.Get();
	if (UFlowList* List = Bar ? Bar->GetFlowList() : nullptr)
	{
		List->SetScrollFraction(FractionAt(MyGeometry, FVector2f(MouseEvent.GetScreenSpacePosition())), /*bAnimate=*/false);
	}
	NotifyActivity();
	return FReply::Handled();
}

FReply SFlowListScrollBar::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging)
	{
		return FReply::Unhandled();
	}
	bDragging = false;

	// Let go between items: a snapping list settles on the nearest. A smooth thumb picks up from where it was let go.
	SmoothFraction = GetListFraction();
	SmoothVelocity = 0.f;
	UFlowListScrollBar* Bar = Owner.Get();
	if (UFlowList* List = Bar ? Bar->GetFlowList() : nullptr)
	{
		List->SnapToNearestItem();
	}
	NotifyActivity();
	return FReply::Handled().ReleaseMouseCapture();
}

void SFlowListScrollBar::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SLeafWidget::OnMouseCaptureLost(CaptureLostEvent);
	if (bDragging)
	{
		bDragging = false;
		SmoothFraction = GetListFraction();
		SmoothVelocity = 0.f;
		UFlowListScrollBar* Bar = Owner.Get();
		if (UFlowList* List = Bar ? Bar->GetFlowList() : nullptr)
		{
			List->SnapToNearestItem();
		}
		NotifyActivity();
	}
}

void SFlowListScrollBar::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseEnter(MyGeometry, MouseEvent);
	WakeUp();
}

void SFlowListScrollBar::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseLeave(MouseEvent);
	WakeUp();
}

void SFlowListScrollBar::WakeUp()
{
	if (!ActiveTimer.IsValid())
	{
		ActiveTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFlowListScrollBar::OnAnimate));
	}
}

EActiveTimerReturnType SFlowListScrollBar::OnAnimate(double InCurrentTime, float InDeltaTime)
{
	// The bar thickens under the pointer and while held, and - with Auto Hide - fades once the list rests.
	const bool bThick = IsHovered() || bDragging;
	const float Goal = bThick ? 1.f : 0.f;
	const float Step = FMath::Clamp(InDeltaTime / 0.12f, 0.f, 1.f);
	HoverAmount = HoverAmount < Goal ? FMath::Min(HoverAmount + Step, Goal) : FMath::Max(HoverAmount - Step, Goal);

	const UFlowListScrollBar* Bar = Owner.Get();
	const float Opacity = GetShownOpacity();
	const bool bFading = Bar && Bar->bAutoHide && Opacity > 0.f && !bThick;

	// Smooth Thumb: a spring after the list. Round a looping list, a wrap from its end to its start jumps rather than sweeping back.
	bool bGliding = false;
	if (Bar && Bar->bSmoothThumb && !bDragging)
	{
		const float ListFraction = GetListFraction();
		const UFlowList* List = Bar->GetFlowList();
		if (!bSmoothPlaced || (List && List->Scrolling.bLoop && FMath::Abs(ListFraction - SmoothFraction) > 0.5f))
		{
			SmoothFraction = ListFraction;
			SmoothVelocity = 0.f;
			bSmoothPlaced = true;
		}
		FWidgetSpringState State;
		State.Value = SmoothFraction;
		State.Velocity = SmoothVelocity;
		bGliding = !UWidgetAnimSpringLibrary::AdvanceSpring(Bar->ThumbSpring, ListFraction, State, FMath::Min(InDeltaTime, 0.1f), 0.0005f, 0.005f);
		SmoothFraction = State.Value;
		SmoothVelocity = State.Velocity;
	}
	Invalidate(EInvalidateWidgetReason::Paint);

	if (HoverAmount != Goal || bFading || bGliding)
	{
		return EActiveTimerReturnType::Continue;
	}
	ActiveTimer.Reset();
	return EActiveTimerReturnType::Stop;
}

// =====================================================================================================
// UMG
// =====================================================================================================

UFlowListScrollBar::UFlowListScrollBar()
{
	FlowListScrollBarPrivate::MakePill(DotBrush, FLinearColor::White);
	FlowListScrollBarPrivate::MakePill(KnobBrush, FLinearColor::White);
	Track = FFlowListScrollBarLine(FLinearColor(1.f, 1.f, 1.f, 0.08f), 5.f, 9.f);
	Thumb = FFlowListScrollBarLine(FLinearColor(1.f, 1.f, 1.f, 0.6f), 5.f, 9.f);
	Fill = FFlowListScrollBarLine(FLinearColor(1.f, 1.f, 1.f, 0.3f), 5.f, 9.f);
	Fill.bShow = false;
	ThumbSpring.Mode = EWidgetSpringMode::DurationAndBounce;
	ThumbSpring.VisualDuration = 0.25f;
	ThumbSpring.Bounce = 0.1f;
}

void UFlowListScrollBar::ApplyStylePreset(EFlowListScrollBarPreset Preset)
{
	if (Preset == EFlowListScrollBarPreset::Custom)
	{
		return;
	}

	// Every preset starts from the defaults, so picking one after another leaves nothing of the first behind. Which list it
	// follows, which way it runs and whether it can be used are about the screen, not the look, so they are kept.
	const UFlowListScrollBar* Defaults = GetDefault<UFlowListScrollBar>();
	Style = EFlowListScrollBarStyle::Bar;
	bAutoHide = Defaults->bAutoHide;
	HideDelay = Defaults->HideDelay;
	FadeTime = Defaults->FadeTime;
	PathShape = Defaults->PathShape;
	CurveBow = Defaults->CurveBow;
	CurveLength = Defaults->CurveLength;
	CurveRotation = Defaults->CurveRotation;
	SplinePoints = Defaults->SplinePoints;
	EndPadding = Defaults->EndPadding;
	Track = Defaults->Track;
	ThumbKind = Defaults->ThumbKind;
	Thumb = Defaults->Thumb;
	MinThumbLength = Defaults->MinThumbLength;
	KnobBrush = Defaults->KnobBrush;
	KnobSize = Defaults->KnobSize;
	KnobHoverSize = Defaults->KnobHoverSize;
	KnobHoverTint = Defaults->KnobHoverTint;
	KnobPressedTint = Defaults->KnobPressedTint;
	bAlignKnobToPath = Defaults->bAlignKnobToPath;
	Fill = Defaults->Fill;
	bSmoothThumb = Defaults->bSmoothThumb;
	ThumbSpring = Defaults->ThumbSpring;
	bShowTicks = Defaults->bShowTicks;
	TickLength = Defaults->TickLength;
	TickWidth = Defaults->TickWidth;
	TickOffset = Defaults->TickOffset;
	TickColor = Defaults->TickColor;
	ActiveTickColor = Defaults->ActiveTickColor;
	ActiveTickLength = Defaults->ActiveTickLength;
	MaxTicks = Defaults->MaxTicks;
	DotBrush = Defaults->DotBrush;
	DotSize = Defaults->DotSize;
	ActiveDotSize = Defaults->ActiveDotSize;
	DotColor = Defaults->DotColor;
	ActiveDotColor = Defaults->ActiveDotColor;
	DotHoverTint = Defaults->DotHoverTint;
	DotSpacing = Defaults->DotSpacing;
	MaxDots = Defaults->MaxDots;
	bAlignDotsToPath = Defaults->bAlignDotsToPath;

	switch (Preset)
	{
	case EFlowListScrollBarPreset::Minimal:
		bAutoHide = true;
		Track = FFlowListScrollBarLine(FLinearColor(1.f, 1.f, 1.f, 0.06f), 3.f, 8.f);
		Thumb = FFlowListScrollBarLine(FLinearColor(1.f, 1.f, 1.f, 0.55f), 3.f, 8.f);
		Thumb.HoverTint = FLinearColor(1.f, 1.f, 1.f, 1.4f);
		break;

	case EFlowListScrollBarPreset::Classic:
		Track = FFlowListScrollBarLine(FLinearColor(0.f, 0.f, 0.f, 0.35f), 12.f, 12.f);
		Track.Cap = EWidgetShapeLineCap::Square;
		Thumb = FFlowListScrollBarLine(FLinearColor(0.75f, 0.75f, 0.78f, 1.f), 10.f, 10.f);
		Thumb.Cap = EWidgetShapeLineCap::Square;
		Thumb.HoverTint = FLinearColor(1.15f, 1.15f, 1.15f, 1.f);
		Thumb.PressedTint = FLinearColor(0.8f, 0.8f, 0.8f, 1.f);
		Thumb.OutlineWidth = 1.f;
		Thumb.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);
		EndPadding = 2.f;
		break;

	case EFlowListScrollBarPreset::Segmented:
		Track = FFlowListScrollBarLine(FLinearColor(1.f, 1.f, 1.f, 0.15f), 6.f, 6.f);
		Track.Cap = EWidgetShapeLineCap::Butt;
		Track.DashLength = 6.f;
		Track.DashGap = 3.f;
		Fill = Track;
		Fill.Paint.Color = FLinearColor(0.35f, 0.8f, 1.f, 1.f);
		Thumb.bShow = false;
		MinThumbLength = 4.f;
		break;

	case EFlowListScrollBarPreset::Arc:
		PathShape = EFlowListScrollBarPath::Curve;
		CurveBow = 10.f;
		Track = FFlowListScrollBarLine(FLinearColor(1.f, 1.f, 1.f, 0.1f), 4.f, 8.f);
		Thumb = FFlowListScrollBarLine(FLinearColor(1.f, 1.f, 1.f, 0.85f), 4.f, 8.f);
		break;

	case EFlowListScrollBarPreset::Dial:
		PathShape = EFlowListScrollBarPath::Curve;
		CurveBow = 18.f;
		Track = FFlowListScrollBarLine(FLinearColor(1.f, 1.f, 1.f, 0.12f), 2.f, 2.f);
		ThumbKind = EFlowListScrollBarThumbKind::Knob;
		KnobSize = FVector2D(12.0, 12.0);
		KnobHoverSize = FVector2D(16.0, 16.0);
		bShowTicks = true;
		TickLength = 8.f;
		ActiveTickLength = 14.f;
		TickWidth = 2.f;
		bSmoothThumb = true;
		break;

	case EFlowListScrollBarPreset::PageDots:
		Style = EFlowListScrollBarStyle::Dots;
		break;

	default:
		break;
	}

	if (MyBar.IsValid())
	{
		MyBar->NotifyActivity();
		MyBar->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void UFlowListScrollBar::RefreshBar()
{
	if (MyBar.IsValid())
	{
		MyBar->NotifyActivity();
		MyBar->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

TSharedRef<SWidget> UFlowListScrollBar::RebuildWidget()
{
	MyBar = SNew(SFlowListScrollBar).Owner(this);
	return MyBar.ToSharedRef();
}

void UFlowListScrollBar::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// The name may have changed: look again.
	Unbind();
	LastSearchTime = -1.0e9;
	if (MyBar.IsValid())
	{
		MyBar->NotifyActivity();
		MyBar->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void UFlowListScrollBar::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Unbind();
	MyBar.Reset();
}

void UFlowListScrollBar::SetFlowList(UFlowList* List)
{
	ExplicitList = List;
	Unbind();
	LastSearchTime = -1.0e9;
	if (MyBar.IsValid())
	{
		MyBar->NotifyActivity();
	}
}

UFlowList* UFlowListScrollBar::GetFlowList() const
{
	if (UFlowList* Bound = BoundList.Get())
	{
		return Bound;
	}

	UFlowList* Found = ExplicitList.Get();
	if (!Found)
	{
		// Not too often: a bar with no list near would otherwise search the tree every paint.
		const double Now = FPlatformTime::Seconds();
		if (Now - LastSearchTime < FlowListScrollBarPrivate::SearchInterval)
		{
			return nullptr;
		}
		LastSearchTime = Now;
		Found = UFlowList::FindNearestFlowList(this, FlowListName);
	}
	if (Found)
	{
		Bind(Found);
	}
	return Found;
}

void UFlowListScrollBar::Bind(UFlowList* List) const
{
	Unbind();
	BoundList = List;
	UpdatedHandle = List->OnListUpdated.AddUObject(this, &UFlowListScrollBar::HandleListUpdated);
}

void UFlowListScrollBar::Unbind() const
{
	if (UFlowList* List = BoundList.Get())
	{
		List->OnListUpdated.Remove(UpdatedHandle);
	}
	BoundList.Reset();
	UpdatedHandle.Reset();
}

void UFlowListScrollBar::HandleListUpdated() const
{
	if (MyBar.IsValid())
	{
		MyBar->NotifyActivity();
	}
}

#if WITH_EDITOR
const FText UFlowListScrollBar::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}

void UFlowListScrollBar::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UFlowListScrollBar, StylePreset))
	{
		ApplyStylePreset(StylePreset);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

#undef LOCTEXT_NAMESPACE
