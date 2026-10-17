// Copyright AR GameSystems 2026. All rights reserved.

#include "CurveShape.h"

#include "NameTBD/Widgets/WidgetEditorHooks.h"
#include "NameTBD/Widgets/Paths/WidgetPathTypes.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"

namespace CurveShapePrivate
{
	/** Lines whose color or width changes along them are cut into runs no longer than this (Slate units), so both read smoothly. */
	constexpr float ResampleStep = 4.f;

	FWidgetShapeVertex MakeVertex(const FVector2D& Position, EWidgetShapeVertexType Type = EWidgetShapeVertexType::Straight,
		const FVector2D& In = FVector2D::ZeroVector, const FVector2D& Out = FVector2D::ZeroVector)
	{
		FWidgetShapeVertex Vertex(Position);
		Vertex.Type = Type;
		Vertex.InHandle = In;
		Vertex.OutHandle = Out;
		return Vertex;
	}

	/** One open (or closed) contour through Points, every vertex of Type. */
	void AddContour(FWidgetShapePath& Into, const TArray<FVector2D>& Points, bool bClosed, EWidgetShapeVertexType Type = EWidgetShapeVertexType::Straight)
	{
		FWidgetShapeContour& Contour = Into.Contours.AddDefaulted_GetRef();
		Contour.bClosed = bClosed;
		for (const FVector2D& Point : Points)
		{
			Contour.Vertices.Add(MakeVertex(Point, Type));
		}
	}

	/** The direction of a line in degrees, clockwise from pointing right (Slate's Y runs down). */
	float AngleOf(const FVector2f& Direction)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	}

	/** The distance along Points of the point on them nearest Target. */
	float NearestDistance(const TArray<FVector2f>& Points, const FVector2f& Target)
	{
		float Best = TNumericLimits<float>::Max();
		float BestDistance = 0.f;
		float Walked = 0.f;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector2f From = Points[Index - 1];
			const FVector2f Segment = Points[Index] - From;
			const float Length = Segment.Size();
			if (Length <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			const float Along = FMath::Clamp(FVector2f::DotProduct(Target - From, Segment) / (Length * Length), 0.f, 1.f);
			const float Away = FVector2f::DistSquared(From + Segment * Along, Target);
			if (Away < Best)
			{
				Best = Away;
				BestDistance = Walked + Along * Length;
			}
			Walked += Length;
		}
		return BestDistance;
	}
}

// =====================================================================================================
// Curve Preset
// =====================================================================================================

UWidgetCurvePresetAsset::UWidgetCurvePresetAsset()
{
	UCurveShape::MakeTemplatePath(EWidgetCurveTemplate::Wave, Path, Smoothing);
	Stroke.bEnabled = true;
	Stroke.Width = 4.f;
	Stroke.Cap = EWidgetShapeLineCap::Round;
	Stroke.Join = EWidgetShapeLineJoin::Round;
	Stroke.Paint.Color = FLinearColor::White;
}

// =====================================================================================================
// Curve
// =====================================================================================================

UCurveShape::UCurveShape()
{
	// A line, not a filled shape: stroked, round ends, nothing inside - and a wave to start from, smoothed through its points.
	bFill = false;
	Stroke.bEnabled = true;
	Stroke.Width = 4.f;
	Stroke.Cap = EWidgetShapeLineCap::Round;
	Stroke.Join = EWidgetShapeLineJoin::Round;
	Stroke.Paint.Color = FLinearColor::White;
	DesiredSize = FVector2D(200.0, 80.0);
	ContentArea = EWidgetShapeContentArea::Bounds;
	MakeTemplatePath(EWidgetCurveTemplate::Wave, Path, Smoothing);

	// Laid over a UI, a line mustn't take the pointer from what is under it (a widget it holds still can).
	UWidget::SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UCurveShape::SmoothContour(FWidgetShapeContour& Contour, float InSmoothness)
{
	const int32 Count = Contour.Vertices.Num();
	if (Count < 2)
	{
		return;
	}
	const float Amount = FMath::Max(InSmoothness, 0.f);
	const bool bClosed = Contour.bClosed;
	auto PositionAt = [&Contour, Count](int32 Index) { return Contour.Vertices[(Index + Count) % Count].Position; };

	// Every point but the ends of an open line: its handles along the line between its neighbors, each a third of the way to
	// that side's neighbor - smooth through the point, never overshooting a neighbor much closer than the other.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FWidgetShapeVertex& Vertex = Contour.Vertices[Index];
		if (Vertex.Type == EWidgetShapeVertexType::Disconnected)
		{
			// A sharp corner of the smooth curve (still rounded by its Corner Radius).
			Vertex.Type = EWidgetShapeVertexType::Straight;
			Vertex.InHandle = FVector2D::ZeroVector;
			Vertex.OutHandle = FVector2D::ZeroVector;
			continue;
		}
		// An open line's ends are aimed below, once every point between has its handles; until then they have none.
		const bool bEnd = !bClosed && (Index == 0 || Index == Count - 1);
		if (bEnd)
		{
			Vertex.Type = EWidgetShapeVertexType::Straight;
			Vertex.InHandle = FVector2D::ZeroVector;
			Vertex.OutHandle = FVector2D::ZeroVector;
			continue;
		}
		const FVector2D Previous = PositionAt(Index - 1);
		const FVector2D Next = PositionAt(Index + 1);
		const FVector2D Direction = (Next - Previous).GetSafeNormal();
		Vertex.Type = EWidgetShapeVertexType::Asymmetric;
		if (Direction.IsNearlyZero() || Amount <= 0.f)
		{
			Vertex.InHandle = FVector2D::ZeroVector;
			Vertex.OutHandle = FVector2D::ZeroVector;
			continue;
		}
		Vertex.InHandle = -Direction * ((Vertex.Position - Previous).Size() / 3.0 * Amount);
		Vertex.OutHandle = Direction * ((Next - Vertex.Position).Size() / 3.0 * Amount);
	}

	if (bClosed)
	{
		return;
	}

	// An open line's ends: aimed at the tip of their neighbor's handle, so the curve leaves and arrives the way it bends.
	for (const int32 End : { 0, Count - 1 })
	{
		FWidgetShapeVertex& Vertex = Contour.Vertices[End];
		if (Amount <= 0.f)
		{
			continue;
		}
		const bool bFirst = End == 0;
		const FWidgetShapeVertex& Neighbor = Contour.Vertices[bFirst ? 1 : Count - 2];
		const FVector2D Aim = Neighbor.Position + (bFirst ? Neighbor.GetIn() : Neighbor.GetOut());
		const FVector2D Direction = (Aim - Vertex.Position).GetSafeNormal();
		const double Length = (Neighbor.Position - Vertex.Position).Size() / 3.0 * Amount;
		Vertex.Type = EWidgetShapeVertexType::Asymmetric;
		Vertex.InHandle = bFirst ? FVector2D::ZeroVector : Direction * Length;
		Vertex.OutHandle = bFirst ? Direction * Length : FVector2D::ZeroVector;
	}
}

void UCurveShape::BuildPath(FWidgetShapePath& OutPath) const
{
	OutPath = Path;
	for (FWidgetShapeContour& Contour : OutPath.Contours)
	{
		switch (Smoothing)
		{
		case EWidgetCurveSmoothing::Smooth:
			SmoothContour(Contour, Smoothness);
			break;
		case EWidgetCurveSmoothing::Straight:
			for (FWidgetShapeVertex& Vertex : Contour.Vertices)
			{
				Vertex.Type = EWidgetShapeVertexType::Straight;
			}
			break;
		default:
			break;
		}
	}
}

float UCurveShape::WidthScaleAt(float Fraction) const
{
	const float Along = FMath::Clamp(Fraction, 0.f, 1.f);
	float Scale = FMath::Lerp(StartWidthScale, EndWidthScale, Along);
	if (bUseWidthCurve)
	{
		if (const FRichCurve* Profile = WidthCurve.GetRichCurveConst())
		{
			if (Profile->GetNumKeys() > 0)
			{
				Scale *= Profile->Eval(Along);
			}
		}
	}
	return FMath::Max(Scale, 0.f);
}

bool UCurveShape::HasVaryingWidth() const
{
	return !FMath::IsNearlyEqual(StartWidthScale, 1.f) || !FMath::IsNearlyEqual(EndWidthScale, 1.f) || bUseWidthCurve;
}

uint32 UCurveShape::GetMeshHash() const
{
	uint32 Hash = Super::GetMeshHash();
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Smoothing)));
	Hash = HashCombineFast(Hash, GetTypeHash(Smoothness));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(StartMarker)) | (GetTypeHash(static_cast<uint8>(EndMarker)) << 8));
	Hash = HashCombineFast(Hash, GetTypeHash(MarkerSize));
	Hash = HashCombineFast(Hash, GetTypeHash(StartWidthScale));
	Hash = HashCombineFast(Hash, GetTypeHash(EndWidthScale));
	Hash = HashCombineFast(Hash, GetTypeHash(bUseWidthCurve) | (GetTypeHash(bGradientAlongCurve) << 1));
	if (bUseWidthCurve)
	{
		if (const FRichCurve* Profile = WidthCurve.GetRichCurveConst())
		{
			for (const FRichCurveKey& Key : Profile->GetConstRefOfKeys())
			{
				Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(Key.Time), GetTypeHash(Key.Value)));
				Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(Key.ArriveTangent), GetTypeHash(Key.LeaveTangent)));
			}
		}
	}
	return Hash;
}

void UCurveShape::BuildStrokeMesh(const FWidgetShapeOutline& InOutline, const TArray<int8>& FillOnLeft, float AntiAlias, float Tolerance, FWidgetShapeMesh& Out) const
{
	using namespace CurveShapePrivate;

	const bool bMarkers = StartMarker != EWidgetPathMarker::None || EndMarker != EWidgetPathMarker::None;
	const bool bVarying = HasVaryingWidth();
	const bool bResample = bVarying || (bGradientAlongCurve && Stroke.Paint.IsGradient());
	if (!Stroke.IsDrawn() || (!bMarkers && !bResample))
	{
		Super::BuildStrokeMesh(InOutline, FillOnLeft, AntiAlias, Tolerance, Out);
		return;
	}

	TArray<int8> OpenSide;
	OpenSide.Add(0);
	TArray<FVector2f> Cut;
	for (int32 LineIndex = 0; LineIndex < InOutline.Lines.Num(); ++LineIndex)
	{
		const FWidgetShapeLine& Line = InOutline.Lines[LineIndex];
		if (Line.Points.Num() < 2)
		{
			continue;
		}

		// A closed line has no ends to mark or taper toward: drawn as every shape draws it.
		if (Line.bClosed)
		{
			FWidgetShapeOutline Single;
			Single.Lines.Add(Line);
			TArray<int8> Side;
			Side.Add(FillOnLeft.IsValidIndex(LineIndex) ? FillOnLeft[LineIndex] : 0);
			WidgetShapeGeometry::BuildStroke(Single, Side, Stroke, Trim, AntiAlias, Tolerance, Out);
			continue;
		}

		TArray<FVector2f> Points = Line.Points;
		if (bResample)
		{
			WidgetPathGeometry::Resample(Points, ResampleStep);
		}
		const float Length = WidgetShapeGeometry::GetLength(Points, /*bClosed=*/false);
		if (Length <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		// The part drawn, as Trim Path measures it: fractions of the line, Offset wrapping past its ends.
		TArray<TPair<float, float>, TInlineAllocator<2>> Pieces;
		const float Low = FMath::Min(Trim.Start, Trim.End);
		const float High = FMath::Max(Trim.Start, Trim.End);
		if (High - Low >= 1.f - 1e-6f)
		{
			Pieces.Emplace(0.f, Length);
		}
		else if (High - Low > 1e-6f)
		{
			const float From = FMath::Frac(Low + Trim.Offset);
			const float To = From + (High - Low);
			if (To <= 1.f)
			{
				Pieces.Emplace(From * Length, To * Length);
			}
			else
			{
				Pieces.Emplace(From * Length, Length);
				Pieces.Emplace(0.f, (To - 1.f) * Length);
			}
		}
		if (Pieces.Num() == 0)
		{
			continue;
		}

		auto WidthAt = [this, Length](float Distance) { return Stroke.Width * WidthScaleAt(Distance / Length); };
		const float DrawnStart = Pieces[0].Key;
		const float DrawnEnd = Pieces.Last().Value;
		const float StartInset = WidgetPathGeometry::GetMarkerInset(StartMarker, MarkerSize, WidthAt(DrawnStart));
		const float EndInset = WidgetPathGeometry::GetMarkerInset(EndMarker, MarkerSize, WidthAt(DrawnEnd));

		for (int32 PieceIndex = 0; PieceIndex < Pieces.Num(); ++PieceIndex)
		{
			// The line stops short of an arrowhead's tip, so its end doesn't poke through it.
			const float A = Pieces[PieceIndex].Key + (PieceIndex == 0 ? StartInset : 0.f);
			const float B = Pieces[PieceIndex].Value - (PieceIndex == Pieces.Num() - 1 ? EndInset : 0.f);
			if (B - A <= 0.05f)
			{
				continue;
			}

			if (bVarying)
			{
				WidgetPathGeometry::BuildDashedRibbons(Points, A, B, Stroke.DashLength, Stroke.DashGap, Stroke.DashOffset, WidthAt,
					[](float) { return 0.f; }, AntiAlias, Length, Out);
				continue;
			}

			WidgetPathGeometry::CutBetween(Points, A, B, Cut);
			if (Cut.Num() < 2)
			{
				continue;
			}
			FWidgetShapeOutline Single;
			FWidgetShapeLine& Piece = Single.Lines.AddDefaulted_GetRef();
			Piece.Points = Cut;
			Piece.bClosed = false;

			// Dashes measured from the line's start, not the piece's, so they stay put while it draws on.
			FWidgetShapeStroke PieceStroke = Stroke;
			PieceStroke.DashOffset = Stroke.DashOffset + A;

			const int32 FirstVertex = Out.Vertices.Num();
			WidgetShapeGeometry::BuildStroke(Single, OpenSide, PieceStroke, FWidgetShapeTrim(), AntiAlias, Tolerance, Out);
			for (int32 VertexIndex = FirstVertex; VertexIndex < Out.Vertices.Num(); ++VertexIndex)
			{
				Out.Vertices[VertexIndex].Along.X += A;
				Out.Vertices[VertexIndex].OutlineLength = Length;
			}
		}

		// The marks at the drawn part's ends, turned along the stretch of line they cover (not just its last step), painted with the stroke.
		auto AddMarker = [&](EWidgetPathMarker Marker, float At, bool bAtStart)
		{
			if (Marker == EWidgetPathMarker::None)
			{
				return;
			}
			FVector2f Tip;
			FVector2f Heading;
			if (!WidgetPathGeometry::SampleAt(Points, At, Tip, Heading))
			{
				return;
			}
			FVector2f Behind;
			FVector2f Unused;
			const float BehindAt = bAtStart ? FMath::Min(At + MarkerSize, Length) : FMath::Max(At - MarkerSize, 0.f);
			FVector2f Outward = bAtStart ? -Heading : Heading;
			if (WidgetPathGeometry::SampleAt(Points, BehindAt, Behind, Unused) && !(Tip - Behind).IsNearlyZero())
			{
				Outward = (Tip - Behind).GetSafeNormal();
			}
			const int32 FirstVertex = Out.Vertices.Num();
			WidgetPathGeometry::BuildEndMarker(Marker, Tip, Outward, MarkerSize, WidthAt(At), AntiAlias, Tolerance, Out);
			for (int32 VertexIndex = FirstVertex; VertexIndex < Out.Vertices.Num(); ++VertexIndex)
			{
				Out.Vertices[VertexIndex].Along = FVector2f(At, 0.5f);
				Out.Vertices[VertexIndex].OutlineLength = Length;
			}
		};
		AddMarker(StartMarker, DrawnStart, /*bAtStart=*/true);
		AddMarker(EndMarker, DrawnEnd, /*bAtStart=*/false);
	}
}

// =====================================================================================================
// Templates
// =====================================================================================================

void UCurveShape::MakeTemplatePath(EWidgetCurveTemplate Template, FWidgetShapePath& OutPath, EWidgetCurveSmoothing& OutSmoothing)
{
	using namespace CurveShapePrivate;

	OutPath.Contours.Reset();
	OutSmoothing = EWidgetCurveSmoothing::Smooth;
	switch (Template)
	{
	case EWidgetCurveTemplate::Line:
		OutSmoothing = EWidgetCurveSmoothing::Straight;
		AddContour(OutPath, { FVector2D(0.0, 0.5), FVector2D(1.0, 0.5) }, false);
		break;

	case EWidgetCurveTemplate::Arc:
	{
		// Half an ellipse standing on the box's bottom: Bezier quarter arcs (0.552 of the radius).
		OutSmoothing = EWidgetCurveSmoothing::Handles;
		FWidgetShapeContour& Contour = OutPath.Contours.AddDefaulted_GetRef();
		Contour.bClosed = false;
		Contour.Vertices.Add(MakeVertex(FVector2D(0.0, 1.0), EWidgetShapeVertexType::Asymmetric, FVector2D::ZeroVector, FVector2D(0.0, -0.552)));
		Contour.Vertices.Add(MakeVertex(FVector2D(0.5, 0.0), EWidgetShapeVertexType::Mirrored, FVector2D(-0.276, 0.0), FVector2D(0.276, 0.0)));
		Contour.Vertices.Add(MakeVertex(FVector2D(1.0, 1.0), EWidgetShapeVertexType::Asymmetric, FVector2D(0.0, -0.552), FVector2D::ZeroVector));
		break;
	}

	case EWidgetCurveTemplate::SCurve:
	{
		OutSmoothing = EWidgetCurveSmoothing::Handles;
		FWidgetShapeContour& Contour = OutPath.Contours.AddDefaulted_GetRef();
		Contour.bClosed = false;
		Contour.Vertices.Add(MakeVertex(FVector2D(0.0, 1.0), EWidgetShapeVertexType::Asymmetric, FVector2D::ZeroVector, FVector2D(0.55, 0.0)));
		Contour.Vertices.Add(MakeVertex(FVector2D(1.0, 0.0), EWidgetShapeVertexType::Asymmetric, FVector2D(-0.55, 0.0), FVector2D::ZeroVector));
		break;
	}

	case EWidgetCurveTemplate::Wave:
	{
		// Two periods of a sine: its crests, troughs and crossings, smoothed through.
		TArray<FVector2D> Points;
		for (int32 Index = 0; Index <= 8; ++Index)
		{
			Points.Add(FVector2D(Index / 8.0, 0.5 - 0.5 * FMath::Sin(Index * UE_DOUBLE_HALF_PI)));
		}
		AddContour(OutPath, Points, false);
		break;
	}

	case EWidgetCurveTemplate::Zigzag:
	{
		OutSmoothing = EWidgetCurveSmoothing::Straight;
		TArray<FVector2D> Points;
		for (int32 Index = 0; Index <= 6; ++Index)
		{
			Points.Add(FVector2D(Index / 6.0, Index % 2 == 0 ? 1.0 : 0.0));
		}
		AddContour(OutPath, Points, false);
		break;
	}

	case EWidgetCurveTemplate::Spiral:
	{
		// Two and a half turns outward from the middle, an eighth of a turn between points.
		TArray<FVector2D> Points;
		const int32 Steps = 20;
		for (int32 Index = 0; Index <= Steps; ++Index)
		{
			const double Angle = Index * UE_DOUBLE_PI / 4.0;
			const double Radius = 0.5 * Index / Steps;
			Points.Add(FVector2D(0.5 + Radius * FMath::Cos(Angle), 0.5 + Radius * FMath::Sin(Angle)));
		}
		AddContour(OutPath, Points, false);
		OutPath.FitToBox();
		break;
	}

	case EWidgetCurveTemplate::Loop:
		AddContour(OutPath, { FVector2D(0.0, 0.8), FVector2D(0.4, 0.8), FVector2D(0.62, 0.45), FVector2D(0.5, 0.1), FVector2D(0.38, 0.45),
			FVector2D(0.6, 0.8), FVector2D(1.0, 0.8) }, false);
		break;

	case EWidgetCurveTemplate::Heartbeat:
		OutSmoothing = EWidgetCurveSmoothing::Straight;
		AddContour(OutPath, { FVector2D(0.0, 0.6), FVector2D(0.3, 0.6), FVector2D(0.36, 0.42), FVector2D(0.42, 0.6), FVector2D(0.48, 0.6),
			FVector2D(0.54, 0.0), FVector2D(0.61, 1.0), FVector2D(0.67, 0.6), FVector2D(0.76, 0.6), FVector2D(0.82, 0.45), FVector2D(0.88, 0.6),
			FVector2D(1.0, 0.6) }, false);
		break;

	case EWidgetCurveTemplate::Swoosh:
		AddContour(OutPath, { FVector2D(0.0, 0.85), FVector2D(0.35, 0.55), FVector2D(0.75, 0.4), FVector2D(1.0, 0.15) }, false);
		break;

	case EWidgetCurveTemplate::Bounce:
	{
		// A ball dropped and bouncing lower each time: smooth arcs meeting the ground in sharp (Disconnected) points.
		FWidgetShapeContour& Contour = OutPath.Contours.AddDefaulted_GetRef();
		Contour.bClosed = false;
		const FVector2D Points[] = { FVector2D(0.0, 0.0), FVector2D(0.2, 1.0), FVector2D(0.35, 0.35), FVector2D(0.5, 1.0), FVector2D(0.62, 0.6),
			FVector2D(0.74, 1.0), FVector2D(0.82, 0.8), FVector2D(0.9, 1.0), FVector2D(1.0, 1.0) };
		const int32 Count = static_cast<int32>(UE_ARRAY_COUNT(Points));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const bool bGround = Points[Index].Y >= 1.0 && Index != Count - 1;
			Contour.Vertices.Add(MakeVertex(Points[Index], bGround ? EWidgetShapeVertexType::Disconnected : EWidgetShapeVertexType::Asymmetric));
		}
		break;
	}

	case EWidgetCurveTemplate::Steps:
		OutSmoothing = EWidgetCurveSmoothing::Straight;
		AddContour(OutPath, { FVector2D(0.0, 1.0), FVector2D(0.25, 1.0), FVector2D(0.25, 0.667), FVector2D(0.5, 0.667), FVector2D(0.5, 0.333),
			FVector2D(0.75, 0.333), FVector2D(0.75, 0.0), FVector2D(1.0, 0.0) }, false);
		break;

	case EWidgetCurveTemplate::Infinity:
	{
		// A lemniscate, closed, sampled off its crossing so no two points meet.
		TArray<FVector2D> Points;
		for (int32 Index = 0; Index < 12; ++Index)
		{
			const double T = UE_DOUBLE_PI / 12.0 + Index * UE_DOUBLE_PI / 6.0;
			const double Sin = FMath::Sin(T);
			const double Cos = FMath::Cos(T);
			const double Denominator = 1.0 + Sin * Sin;
			Points.Add(FVector2D(Cos / Denominator, Sin * Cos / Denominator));
		}
		AddContour(OutPath, Points, true);
		OutPath.FitToBox();
		break;
	}

	default:
		AddContour(OutPath, { FVector2D(0.0, 0.5), FVector2D(1.0, 0.5) }, false);
		break;
	}
}

// =====================================================================================================
// Blueprint
// =====================================================================================================

void UCurveShape::SetCurveFromTemplate(EWidgetCurveTemplate Template)
{
	MakeTemplatePath(Template, Path, Smoothing);
	CurvePreset = nullptr;
	PresetAppliedHash = 0;
	InvalidateShape();
}

void UCurveShape::SetPoints(const TArray<FVector2D>& Points, bool bClosed)
{
	Path.Contours.Reset();
	CurveShapePrivate::AddContour(Path, Points, bClosed);
	CurvePreset = nullptr;
	PresetAppliedHash = 0;
	InvalidateShape();
}

TArray<FVector2D> UCurveShape::GetPoints() const
{
	TArray<FVector2D> Points;
	if (Path.Contours.Num() > 0)
	{
		for (const FWidgetShapeVertex& Vertex : Path.Contours[0].Vertices)
		{
			Points.Add(Vertex.Position);
		}
	}
	return Points;
}

void UCurveShape::AddPoint(FVector2D Point, float MinSpacing)
{
	if (Path.Contours.Num() == 0)
	{
		FWidgetShapeContour& Started = Path.Contours.AddDefaulted_GetRef();
		Started.bClosed = false;
	}
	FWidgetShapeContour& Contour = Path.Contours.Last();
	if (MinSpacing > 0.f && Contour.Vertices.Num() > 0)
	{
		// Measured in Slate units at the size the widget was last laid out, so a stretched box doesn't skew it.
		FVector2D Size = GetCachedGeometry().GetLocalSize();
		if (Size.X <= 0.0 || Size.Y <= 0.0)
		{
			Size = DesiredSize;
		}
		const FVector2D Step = (Point - Contour.Vertices.Last().Position) * Size;
		if (Step.Size() < MinSpacing)
		{
			return;
		}
	}
	Contour.Vertices.Add(FWidgetShapeVertex(Point));
	CurvePreset = nullptr;
	PresetAppliedHash = 0;
	InvalidateShape();
}

void UCurveShape::AddPointAtLocalPosition(FVector2D LocalPosition, float MinSpacing)
{
	FVector2D Size = GetCachedGeometry().GetLocalSize();
	if (Size.X <= 0.0 || Size.Y <= 0.0)
	{
		Size = DesiredSize;
	}
	const FWidgetShapeFrame LocalFrame = MakeFrame(FVector2f(static_cast<float>(Size.X), static_cast<float>(Size.Y)));
	const FVector2f Box = LocalFrame.ToBox(FVector2f(static_cast<float>(LocalPosition.X), static_cast<float>(LocalPosition.Y)));
	AddPoint(FVector2D(Box), MinSpacing);
}

void UCurveShape::ClearPoints()
{
	Path.Contours.Reset();
	CurvePreset = nullptr;
	PresetAppliedHash = 0;
	InvalidateShape();
}

void UCurveShape::PlotValues(const TArray<float>& Values, float MinValue, float MaxValue, bool bAutoRange)
{
	float Low = MinValue;
	float High = MaxValue;
	if (bAutoRange && Values.Num() > 0)
	{
		Low = Values[0];
		High = Values[0];
		for (const float Value : Values)
		{
			Low = FMath::Min(Low, Value);
			High = FMath::Max(High, Value);
		}
	}
	const float Range = High - Low;

	TArray<FVector2D> Points;
	Points.Reserve(Values.Num());
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		const double X = Values.Num() > 1 ? static_cast<double>(Index) / (Values.Num() - 1) : 0.5;
		// A flat line (every value the same) sits across the middle rather than on an edge.
		const double Level = FMath::Abs(Range) > KINDA_SMALL_NUMBER ? (Values[Index] - Low) / Range : 0.5;
		Points.Add(FVector2D(X, 1.0 - Level));
	}
	SetPoints(Points, false);
}

const FWidgetShapeLine* UCurveShape::GetDrawnLine() const
{
	if (!MyShape.IsValid())
	{
		return nullptr;
	}
	const FWidgetShapeOutline& Drawn = MyShape->GetPaintedOutline();
	return Drawn.Lines.Num() > 0 && Drawn.Lines[0].Points.Num() >= 2 ? &Drawn.Lines[0] : nullptr;
}

float UCurveShape::GetCurveLength() const
{
	const FWidgetShapeLine* Line = GetDrawnLine();
	return Line ? WidgetShapeGeometry::GetLength(Line->Points, Line->bClosed) : 0.f;
}

bool UCurveShape::GetPointAtDistance(float Distance, FVector2D& OutPosition, float& OutAngle) const
{
	const FWidgetShapeLine* Line = GetDrawnLine();
	if (!Line)
	{
		return false;
	}
	TArray<FVector2f> Points = Line->Points;
	if (Line->bClosed)
	{
		Points.Add(Points[0]);
	}
	const float Length = WidgetShapeGeometry::GetLength(Points, /*bClosed=*/false);
	float Along = Distance;
	if (Line->bClosed && Length > KINDA_SMALL_NUMBER)
	{
		// Round and round a closed curve.
		Along = FMath::Fmod(Along, Length);
		Along = Along < 0.f ? Along + Length : Along;
	}
	FVector2f Point;
	FVector2f Direction;
	if (!WidgetPathGeometry::SampleAt(Points, FMath::Clamp(Along, 0.f, Length), Point, Direction))
	{
		return false;
	}
	OutPosition = FVector2D(Point);
	OutAngle = CurveShapePrivate::AngleOf(Direction);
	return true;
}

bool UCurveShape::GetPointAtFraction(float Fraction, FVector2D& OutPosition, float& OutAngle) const
{
	return GetPointAtDistance(Fraction * GetCurveLength(), OutPosition, OutAngle);
}

float UCurveShape::FindFractionNearest(FVector2D LocalPosition) const
{
	const FWidgetShapeLine* Line = GetDrawnLine();
	if (!Line)
	{
		return 0.f;
	}
	TArray<FVector2f> Points = Line->Points;
	if (Line->bClosed)
	{
		Points.Add(Points[0]);
	}
	const float Length = WidgetShapeGeometry::GetLength(Points, /*bClosed=*/false);
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	const FVector2f Target(static_cast<float>(LocalPosition.X), static_cast<float>(LocalPosition.Y));
	return CurveShapePrivate::NearestDistance(Points, Target) / Length;
}

// =====================================================================================================
// Presets
// =====================================================================================================

void UCurveShape::GetPresetCoveredProperties(bool bWithLook, TArray<const FProperty*>& OutProperties) const
{
	OutProperties.Reset();
	static const FName Outline[] =
	{
		GET_MEMBER_NAME_CHECKED(UCustomShape, Path),
		GET_MEMBER_NAME_CHECKED(UCurveShape, Smoothing),
		GET_MEMBER_NAME_CHECKED(UCurveShape, Smoothness)
	};
	static const FName Look[] =
	{
		GET_MEMBER_NAME_CHECKED(UShapeWidget, Stroke),
		GET_MEMBER_NAME_CHECKED(UShapeWidget, bFill),
		GET_MEMBER_NAME_CHECKED(UShapeWidget, Fill),
		GET_MEMBER_NAME_CHECKED(UCurveShape, StartMarker),
		GET_MEMBER_NAME_CHECKED(UCurveShape, EndMarker),
		GET_MEMBER_NAME_CHECKED(UCurveShape, MarkerSize),
		GET_MEMBER_NAME_CHECKED(UCurveShape, StartWidthScale),
		GET_MEMBER_NAME_CHECKED(UCurveShape, EndWidthScale),
		GET_MEMBER_NAME_CHECKED(UCurveShape, bUseWidthCurve),
		GET_MEMBER_NAME_CHECKED(UCurveShape, WidthCurve),
		GET_MEMBER_NAME_CHECKED(UCurveShape, bGradientAlongCurve)
	};
	for (const FName& Name : Outline)
	{
		if (const FProperty* Property = UCurveShape::StaticClass()->FindPropertyByName(Name))
		{
			OutProperties.Add(Property);
		}
	}
	if (bWithLook)
	{
		for (const FName& Name : Look)
		{
			if (const FProperty* Property = UCurveShape::StaticClass()->FindPropertyByName(Name))
			{
				OutProperties.Add(Property);
			}
		}
	}
}

uint32 UCurveShape::HashPresetCovered() const
{
	TArray<const FProperty*> Properties;
	GetPresetCoveredProperties(CurvePreset && CurvePreset->bApplyLook, Properties);
	uint32 Hash = 0x5eed;
	for (const FProperty* Property : Properties)
	{
		FString Value;
		Property->ExportTextItem_InContainer(Value, this, nullptr, nullptr, PPF_None);
		Hash = HashCombineFast(Hash, GetTypeHash(Value));
	}
	return Hash;
}

void UCurveShape::ApplyCurvePreset(UWidgetCurvePresetAsset* InPreset)
{
	if (!InPreset)
	{
		return;
	}
	CurvePreset = InPreset;
	Path = InPreset->Path;
	Smoothing = InPreset->Smoothing;
	Smoothness = InPreset->Smoothness;
	if (InPreset->bApplyLook)
	{
		Stroke = InPreset->Stroke;
		bFill = InPreset->bFill;
		Fill = InPreset->Fill;
		StartMarker = InPreset->StartMarker;
		EndMarker = InPreset->EndMarker;
		MarkerSize = InPreset->MarkerSize;
		StartWidthScale = InPreset->StartWidthScale;
		EndWidthScale = InPreset->EndWidthScale;
		bUseWidthCurve = InPreset->bUseWidthCurve;
		WidthCurve = InPreset->WidthCurve;
		bGradientAlongCurve = InPreset->bGradientAlongCurve;
	}
	PresetAppliedHash = HashPresetCovered();
	InvalidateShape();
}

void UCurveShape::CopyToCurvePreset(UWidgetCurvePresetAsset* Target) const
{
	if (!Target)
	{
		return;
	}
	Target->Modify();
	Target->Path = Path;
	Target->Smoothing = Smoothing;
	Target->Smoothness = Smoothness;
	Target->Stroke = Stroke;
	Target->bFill = bFill;
	Target->Fill = Fill;
	Target->StartMarker = StartMarker;
	Target->EndMarker = EndMarker;
	Target->MarkerSize = MarkerSize;
	Target->StartWidthScale = StartWidthScale;
	Target->EndWidthScale = EndWidthScale;
	Target->bUseWidthCurve = bUseWidthCurve;
	Target->WidthCurve = WidthCurve;
	Target->bGradientAlongCurve = bGradientAlongCurve;
	Target->MarkPackageDirty();
}

void UCurveShape::SaveAsCurvePreset()
{
#if WITH_EDITOR
	const FString DefaultName = FString::Printf(TEXT("WCP_%s"), *GetName());
	UWidgetCurvePresetAsset* Created = Cast<UWidgetCurvePresetAsset>(WidgetEditorHooks::CreateAsset(UWidgetCurvePresetAsset::StaticClass(), DefaultName));
	if (!Created)
	{
		return;
	}
	CopyToCurvePreset(Created);
	Modify();
	CurvePreset = Created;
	PresetAppliedHash = HashPresetCovered();
	SyncPresetToTemplate();
#endif
}

void UCurveShape::SyncPresetToTemplate()
{
#if WITH_EDITOR
	UCurveShape* Template = Cast<UCurveShape>(WidgetEditorHooks::FindDesignerTemplate(this));
	if (!Template || Template == this)
	{
		return;
	}
	Template->Modify();
	TArray<const FProperty*> Properties;
	GetPresetCoveredProperties(/*bWithLook=*/true, Properties);
	for (const FProperty* Property : Properties)
	{
		Property->CopyCompleteValue_InContainer(Template, this);
	}
	Template->CurvePreset = CurvePreset;
	Template->PresetAppliedHash = PresetAppliedHash;
	Template->MarkPackageDirty();
#endif
}

void UCurveShape::PostLoad()
{
	Super::PostLoad();

	// Following a preset and unchanged since it was applied: take the preset again, so edits made to the asset since reach this
	// curve. Changed by anything that didn't let the preset go (an edit made outside the Details): it is let go now.
	if (CurvePreset && PresetAppliedHash != 0 && !CurvePreset->HasAnyFlags(RF_NeedLoad))
	{
		if (HashPresetCovered() == PresetAppliedHash)
		{
			ApplyCurvePreset(CurvePreset);
		}
		else
		{
			CurvePreset = nullptr;
			PresetAppliedHash = 0;
		}
	}
}

#if WITH_EDITOR
void UCurveShape::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
	bool bSync = false;
	if (MemberName == GET_MEMBER_NAME_CHECKED(UCurveShape, CurvePreset))
	{
		if (CurvePreset)
		{
			ApplyCurvePreset(CurvePreset);
		}
		else
		{
			PresetAppliedHash = 0;
		}
		bSync = true;
	}
	else if (CurvePreset)
	{
		TArray<const FProperty*> Covered;
		GetPresetCoveredProperties(CurvePreset->bApplyLook, Covered);
		if (Covered.ContainsByPredicate([MemberName](const FProperty* Property) { return Property->GetFName() == MemberName; }))
		{
			// Changing what the preset holds lets it go: the curve is its own from now on.
			CurvePreset = nullptr;
			PresetAppliedHash = 0;
			bSync = true;
		}
	}

	// Handles As Drawn picked from Smooth Through Points: the handles worked out become the points' own, so the curve keeps its
	// shape and they can be dragged from there.
	if (MemberName == GET_MEMBER_NAME_CHECKED(UCurveShape, Smoothing) && Smoothing == EWidgetCurveSmoothing::Handles)
	{
		bool bAnyHandles = false;
		for (const FWidgetShapeContour& Contour : Path.Contours)
		{
			for (const FWidgetShapeVertex& Vertex : Contour.Vertices)
			{
				bAnyHandles |= Vertex.HasHandles() && !(Vertex.InHandle.IsNearlyZero(1e-9) && Vertex.OutHandle.IsNearlyZero(1e-9));
			}
		}
		if (!bAnyHandles)
		{
			for (FWidgetShapeContour& Contour : Path.Contours)
			{
				SmoothContour(Contour, Smoothness);
			}
			bSync = true;
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
	InvalidateShape();

	// The Details panel edits the designer's preview copy and hands only the edited value on to the Widget Blueprint's own
	// widget: what a preset (or the switch to Handles) just wrote is written there too, or the next compile loses it.
	if (bSync)
	{
		SyncPresetToTemplate();
	}
}
#endif
