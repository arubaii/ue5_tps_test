// Copyright AR GameSystems 2026. All rights reserved.

#include "ShapeWidgets.h"

namespace ShapeWidgetsPrivate
{
	/** A point of an ellipse in box fractions: 0 at the top, clockwise. */
	FVector2D EllipsePoint(const FVector2D& Center, const FVector2D& Radii, double Angle)
	{
		return Center + FVector2D(FMath::Sin(Angle) * Radii.X, -FMath::Cos(Angle) * Radii.Y);
	}

	/** Which way the ellipse runs at Angle, per radian - a Bezier handle's direction and its length scale. */
	FVector2D EllipseTangent(const FVector2D& Radii, double Angle)
	{
		return FVector2D(FMath::Cos(Angle) * Radii.X, FMath::Sin(Angle) * Radii.Y);
	}

	/**
	 * Appends an arc of an ellipse to a contour as Bezier curves of a quarter turn or less (the usual 4/3 tan(a/4) handles,
	 * within 0.03% of the ellipse). Starts with a new vertex when the contour is empty, else carries on from its last.
	 */
	void AppendBezierArc(FWidgetShapeContour& Contour, const FVector2D& Center, const FVector2D& Radii, double Start, double Sweep)
	{
		const int32 Pieces = FMath::Max(1, FMath::CeilToInt(FMath::Abs(Sweep) / (HALF_PI + 1e-6)));
		const double Step = Sweep / Pieces;
		const double Handle = (4.0 / 3.0) * FMath::Tan(Step / 4.0);
		if (Contour.Vertices.Num() == 0)
		{
			Contour.Vertices.Add(FWidgetShapeVertex(EllipsePoint(Center, Radii, Start)));
		}
		for (int32 Piece = 0; Piece < Pieces; ++Piece)
		{
			const double A0 = Start + Step * Piece;
			const double A1 = A0 + Step;
			FWidgetShapeVertex& From = Contour.Vertices.Last();
			From.OutHandle = EllipseTangent(Radii, A0) * Handle;
			From.Type = EWidgetShapeVertexType::Disconnected;

			FWidgetShapeVertex To(EllipsePoint(Center, Radii, A1));
			To.Type = EWidgetShapeVertexType::Disconnected;
			To.InHandle = -EllipseTangent(Radii, A1) * Handle;
			Contour.Vertices.Add(To);
		}
	}

	/** Names each vertex's kind from its handles: none Straight, opposite and equal Mirrored, opposite Asymmetric. */
	void SettleVertexTypes(FWidgetShapeContour& Contour)
	{
		for (FWidgetShapeVertex& Vertex : Contour.Vertices)
		{
			const bool bIn = !Vertex.InHandle.IsNearlyZero(1e-9);
			const bool bOut = !Vertex.OutHandle.IsNearlyZero(1e-9);
			if (!bIn && !bOut)
			{
				Vertex.Type = EWidgetShapeVertexType::Straight;
			}
			else if (bIn && bOut && (Vertex.InHandle + Vertex.OutHandle).IsNearlyZero(1e-6))
			{
				Vertex.Type = EWidgetShapeVertexType::Mirrored;
			}
			else if (bIn && bOut && FMath::Abs(FVector2D::CrossProduct(Vertex.InHandle.GetSafeNormal(), Vertex.OutHandle.GetSafeNormal())) < 1e-4)
			{
				Vertex.Type = EWidgetShapeVertexType::Asymmetric;
			}
			else
			{
				Vertex.Type = EWidgetShapeVertexType::Disconnected;
			}
		}
	}

	/** Joins a closed contour's last vertex onto its first when they are the same point. */
	void CloseContour(FWidgetShapeContour& Contour)
	{
		Contour.bClosed = true;
		if (Contour.Vertices.Num() > 2 && Contour.Vertices[0].Position.Equals(Contour.Vertices.Last().Position, 1e-6))
		{
			Contour.Vertices[0].InHandle = Contour.Vertices.Last().InHandle;
			Contour.Vertices.Pop();
		}
	}

	/** A polygon's corners as a closed contour of Straight vertices, each rounded by its radius. */
	void AppendPolygonContour(FWidgetShapePath& Path, const TArray<FVector2D>& Corners, TFunctionRef<float(int32)> RadiusOf)
	{
		FWidgetShapeContour& Contour = Path.Contours.AddDefaulted_GetRef();
		Contour.bClosed = true;
		for (int32 Index = 0; Index < Corners.Num(); ++Index)
		{
			Contour.Vertices.Add(FWidgetShapeVertex(Corners[Index], RadiusOf(Index)));
		}
	}
}

// =====================================================================================================
// Polygons
// =====================================================================================================

namespace WidgetShapePolygons
{
	void MakeCorners(int32 Count, bool bStar, float OuterRadius, float InnerRadius, bool bFillBox,
		TArray<FVector2D>& OutCorners, TArray<bool>& OutInner, float& OutAspect)
	{
		OutCorners.Reset();
		OutInner.Reset();
		OutAspect = 1.f;
		Count = FMath::Clamp(Count, 3, 100);

		const int32 Total = bStar ? Count * 2 : Count;
		auto UnitCorner = [&](int32 Index, double Outer, double Inner)
		{
			const double Angle = (2.0 * PI * Index) / Total;
			const double Radius = (bStar && (Index % 2) == 1) ? Inner : Outer;
			return FVector2D(FMath::Sin(Angle) * Radius, -FMath::Cos(Angle) * Radius);
		};

		// The shape at radius 1 decides where the box's edges are, so Outer Radius under 1 shrinks it inside them.
		FBox2D Reference(ForceInit);
		const double ReferenceInner = OuterRadius > KINDA_SMALL_NUMBER ? InnerRadius / OuterRadius : InnerRadius;
		for (int32 Index = 0; Index < Total; ++Index)
		{
			Reference += UnitCorner(Index, 1.0, ReferenceInner);
		}
		FVector2D Middle = FVector2D::ZeroVector;
		FVector2D Scale(0.5, 0.5);
		if (bFillBox && Reference.bIsValid)
		{
			const FVector2D Size = Reference.GetSize();
			Middle = Reference.GetCenter();
			Scale = FVector2D(Size.X > 1e-6 ? 1.0 / Size.X : 0.5, Size.Y > 1e-6 ? 1.0 / Size.Y : 0.5);
			OutAspect = Size.Y > 1e-6 ? static_cast<float>(Size.X / Size.Y) : 1.f;
		}

		for (int32 Index = 0; Index < Total; ++Index)
		{
			OutCorners.Add(FVector2D(0.5, 0.5) + (UnitCorner(Index, OuterRadius, InnerRadius) - Middle * OuterRadius) * Scale);
			OutInner.Add(bStar && (Index % 2) == 1);
		}
	}
}

// =====================================================================================================
// Ellipse
// =====================================================================================================

void UEllipseShape::SetArc(float InStartAngle, float InSweep)
{
	StartAngle = InStartAngle;
	Sweep = FMath::Clamp(InSweep, 0.f, 360.f);
	InvalidateShape();
}

void UEllipseShape::SetHole(float InHole)
{
	Hole = FMath::Clamp(InHole, 0.f, 0.999f);
	InvalidateShape();
}

bool UEllipseShape::SetShapeValue(EWidgetShapeValue Value, float InValue)
{
	switch (Value)
	{
	case EWidgetShapeValue::Hole: Hole = FMath::Clamp(InValue, 0.f, 0.999f); break;
	case EWidgetShapeValue::StartAngle: StartAngle = InValue; break;
	case EWidgetShapeValue::Sweep: Sweep = FMath::Clamp(InValue, 0.f, 360.f); break;
	default: return Super::SetShapeValue(Value, InValue);
	}
	InvalidateShape();
	return true;
}

bool UEllipseShape::GetShapeValue(EWidgetShapeValue Value, float& OutValue) const
{
	switch (Value)
	{
	case EWidgetShapeValue::Hole: OutValue = Hole; return true;
	case EWidgetShapeValue::StartAngle: OutValue = StartAngle; return true;
	case EWidgetShapeValue::Sweep: OutValue = Sweep; return true;
	default: return Super::GetShapeValue(Value, OutValue);
	}
}

void UEllipseShape::BuildOutline(const FWidgetShapeFrame& InFrame, float Tolerance, FWidgetShapeOutline& Out) const
{
	const float SweepDegrees = FMath::Clamp(Sweep, 0.f, 360.f);
	if (SweepDegrees <= 1e-3f)
	{
		return;
	}

	const float Start = FMath::DegreesToRadians(StartAngle);
	const float Turn = FMath::DegreesToRadians(SweepDegrees);
	const FVector2f Center(0.5f, 0.5f);
	const FVector2f Radii(0.5f, 0.5f);
	const float HoleSize = FMath::Clamp(Hole, 0.f, 0.999f);
	const FVector2f InnerRadii = Radii * HoleSize;
	const bool bWhole = SweepDegrees >= 360.f - 1e-3f;

	if (bWhole)
	{
		// Each a closed line, starting at Start Angle so a Trim Path does too. The inner one, inside the outer, is a hole.
		FWidgetShapeLine& OuterLine = Out.Lines.AddDefaulted_GetRef();
		WidgetShapeGeometry::AppendEllipseArc(OuterLine.Points, InFrame, Center, Radii, Start, 2.f * PI, Tolerance, true);
		OuterLine.Points.Pop(EAllowShrinking::No);
		if (HoleSize > 0.f)
		{
			FWidgetShapeLine& InnerLine = Out.Lines.AddDefaulted_GetRef();
			WidgetShapeGeometry::AppendEllipseArc(InnerLine.Points, InFrame, Center, InnerRadii, Start, 2.f * PI, Tolerance, true);
			InnerLine.Points.Pop(EAllowShrinking::No);
		}
		return;
	}

	FWidgetShapeLine& Line = Out.Lines.AddDefaulted_GetRef();
	Line.bClosed = true;
	TArray<FVector2f>& Points = Line.Points;

	if (HoleSize <= 0.f)
	{
		// A pie: the middle, then round the edge.
		Points.Add(InFrame.ToLocal(Center));
		WidgetShapeGeometry::AppendEllipseArc(Points, InFrame, Center, Radii, Start, Turn, Tolerance, true);
		return;
	}

	// An arc: round the outside, back round the inside, its ends rounded if asked.
	auto Forward = [&](float Angle)
	{
		const FVector2f Tangent(FMath::Cos(Angle) * Radii.X, FMath::Sin(Angle) * Radii.Y);
		const FVector2f Local = InFrame.VectorToLocal(Tangent);
		const float Length = Local.Size();
		return Length > 1e-8f ? Local / Length : FVector2f(1.f, 0.f);
	};
	auto AppendCap = [&](const FVector2f& From, const FVector2f& To, const FVector2f& Ahead)
	{
		// Half a circle from From round to To, bulging Ahead: across the band's width at its end.
		const FVector2f Middle = (From + To) * 0.5f;
		const float Radius = (From - Middle).Size();
		if (Radius <= 1e-6f)
		{
			return;
		}
		const FVector2f Across = (From - Middle) / Radius;
		const float Step = 2.f * FMath::Acos(FMath::Clamp(1.f - Tolerance / FMath::Max(Radius, Tolerance), -1.f, 1.f));
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(PI / FMath::Max(Step, 0.01f)), 2, 256);
		for (int32 Index = 1; Index < Steps; ++Index)
		{
			const float Angle = PI * Index / Steps;
			Points.Add(Middle + (Across * FMath::Cos(Angle) + Ahead * FMath::Sin(Angle)) * Radius);
		}
	};

	WidgetShapeGeometry::AppendEllipseArc(Points, InFrame, Center, Radii, Start, Turn, Tolerance, true);
	const FVector2f OuterEnd = Points.Last();
	const FVector2f InnerEnd = InFrame.ToLocal(Center + FVector2f(FMath::Sin(Start + Turn) * InnerRadii.X, -FMath::Cos(Start + Turn) * InnerRadii.Y));
	if (bRoundEnds)
	{
		AppendCap(OuterEnd, InnerEnd, Forward(Start + Turn));
	}
	WidgetShapeGeometry::AppendEllipseArc(Points, InFrame, Center, InnerRadii, Start + Turn, -Turn, Tolerance, true);
	if (bRoundEnds)
	{
		const FVector2f InnerStart = Points.Last();
		const FVector2f OuterStart = Points[0];
		AppendCap(InnerStart, OuterStart, -Forward(Start));
	}
}

void UEllipseShape::BuildPath(FWidgetShapePath& OutPath) const
{
	using namespace ShapeWidgetsPrivate;

	const double SweepDegrees = FMath::Clamp(Sweep, 0.f, 360.f);
	if (SweepDegrees <= 1e-3)
	{
		return;
	}
	const double Start = FMath::DegreesToRadians(static_cast<double>(StartAngle));
	const double Turn = FMath::DegreesToRadians(SweepDegrees);
	const FVector2D Center(0.5, 0.5);
	const FVector2D Radii(0.5, 0.5);
	const double HoleSize = FMath::Clamp(Hole, 0.f, 0.999f);
	const FVector2D InnerRadii = Radii * HoleSize;

	if (SweepDegrees >= 360.0 - 1e-3)
	{
		FWidgetShapeContour& OuterContour = OutPath.Contours.AddDefaulted_GetRef();
		AppendBezierArc(OuterContour, Center, Radii, Start, 2.0 * PI);
		CloseContour(OuterContour);
		SettleVertexTypes(OuterContour);
		if (HoleSize > 0.0)
		{
			FWidgetShapeContour& InnerContour = OutPath.Contours.AddDefaulted_GetRef();
			AppendBezierArc(InnerContour, Center, InnerRadii, Start, 2.0 * PI);
			CloseContour(InnerContour);
			SettleVertexTypes(InnerContour);
		}
		return;
	}

	FWidgetShapeContour& Contour = OutPath.Contours.AddDefaulted_GetRef();
	if (HoleSize <= 0.0)
	{
		Contour.Vertices.Add(FWidgetShapeVertex(Center));
		Contour.Vertices.Add(FWidgetShapeVertex(Center + FVector2D(FMath::Sin(Start) * Radii.X, -FMath::Cos(Start) * Radii.Y)));
		AppendBezierArc(Contour, Center, Radii, Start, Turn);
	}
	else
	{
		AppendBezierArc(Contour, Center, Radii, Start, Turn);
		Contour.Vertices.Add(FWidgetShapeVertex(Center + FVector2D(FMath::Sin(Start + Turn) * InnerRadii.X, -FMath::Cos(Start + Turn) * InnerRadii.Y)));
		AppendBezierArc(Contour, Center, InnerRadii, Start + Turn, -Turn);
	}
	CloseContour(Contour);
	SettleVertexTypes(Contour);
}

uint32 UEllipseShape::GetMeshHash() const
{
	uint32 Hash = Super::GetMeshHash();
	Hash = HashCombineFast(Hash, GetTypeHash(Hole));
	Hash = HashCombineFast(Hash, GetTypeHash(StartAngle));
	Hash = HashCombineFast(Hash, GetTypeHash(Sweep));
	Hash = HashCombineFast(Hash, GetTypeHash(bRoundEnds));
	return Hash;
}

// =====================================================================================================
// Triangle
// =====================================================================================================

bool UTriangleShape::SetShapeValue(EWidgetShapeValue Value, float InValue)
{
	if (Value != EWidgetShapeValue::CornerRounding)
	{
		return Super::SetShapeValue(Value, InValue);
	}
	CornerRounding = FMath::Max(InValue, 0.f);
	InvalidateShape();
	return true;
}

bool UTriangleShape::GetShapeValue(EWidgetShapeValue Value, float& OutValue) const
{
	if (Value != EWidgetShapeValue::CornerRounding)
	{
		return Super::GetShapeValue(Value, OutValue);
	}
	OutValue = CornerRounding;
	return true;
}

void UTriangleShape::BuildPath(FWidgetShapePath& OutPath) const
{
	const TArray<FVector2D> Corners = { FVector2D(0.5, 0.0), FVector2D(1.0, 1.0), FVector2D(0.0, 1.0) };
	ShapeWidgetsPrivate::AppendPolygonContour(OutPath, Corners, [this](int32) { return CornerRounding; });
}

float UTriangleShape::GetNaturalAspect() const
{
	// Equilateral: as wide as 2 / sqrt(3) of its height.
	return 1.1547005f;
}

uint32 UTriangleShape::GetMeshHash() const
{
	return HashCombineFast(Super::GetMeshHash(), GetTypeHash(CornerRounding));
}

// =====================================================================================================
// Star
// =====================================================================================================

bool UStarShape::SetShapeValue(EWidgetShapeValue Value, float InValue)
{
	switch (Value)
	{
	case EWidgetShapeValue::Points: Points = FMath::Clamp(FMath::RoundToInt(InValue), 3, 100); break;
	case EWidgetShapeValue::InnerRadius: InnerRadius = FMath::Clamp(InValue, 0.f, 2.f); break;
	case EWidgetShapeValue::OuterRadius: OuterRadius = FMath::Clamp(InValue, 0.f, 2.f); break;
	case EWidgetShapeValue::CornerRounding: CornerRounding = FMath::Max(InValue, 0.f); break;
	case EWidgetShapeValue::InnerRounding: InnerRounding = FMath::Max(InValue, 0.f); break;
	default: return Super::SetShapeValue(Value, InValue);
	}
	InvalidateShape();
	return true;
}

bool UStarShape::GetShapeValue(EWidgetShapeValue Value, float& OutValue) const
{
	switch (Value)
	{
	case EWidgetShapeValue::Points: OutValue = static_cast<float>(Points); return true;
	case EWidgetShapeValue::InnerRadius: OutValue = InnerRadius; return true;
	case EWidgetShapeValue::OuterRadius: OutValue = OuterRadius; return true;
	case EWidgetShapeValue::CornerRounding: OutValue = CornerRounding; return true;
	case EWidgetShapeValue::InnerRounding: OutValue = InnerRounding; return true;
	default: return Super::GetShapeValue(Value, OutValue);
	}
}

void UStarShape::BuildPath(FWidgetShapePath& OutPath) const
{
	TArray<FVector2D> Corners;
	TArray<bool> bInner;
	float Aspect = 1.f;
	WidgetShapePolygons::MakeCorners(Points, /*bStar=*/true, OuterRadius, InnerRadius, bFillBox, Corners, bInner, Aspect);
	ShapeWidgetsPrivate::AppendPolygonContour(OutPath, Corners, [this, &bInner](int32 Index) { return bInner[Index] ? InnerRounding : CornerRounding; });
}

float UStarShape::GetNaturalAspect() const
{
	TArray<FVector2D> Corners;
	TArray<bool> bInner;
	float Aspect = 1.f;
	WidgetShapePolygons::MakeCorners(Points, /*bStar=*/true, OuterRadius, InnerRadius, bFillBox, Corners, bInner, Aspect);
	return Aspect;
}

uint32 UStarShape::GetMeshHash() const
{
	uint32 Hash = Super::GetMeshHash();
	Hash = HashCombineFast(Hash, GetTypeHash(Points));
	Hash = HashCombineFast(Hash, GetTypeHash(InnerRadius));
	Hash = HashCombineFast(Hash, GetTypeHash(OuterRadius));
	Hash = HashCombineFast(Hash, GetTypeHash(CornerRounding));
	Hash = HashCombineFast(Hash, GetTypeHash(InnerRounding));
	Hash = HashCombineFast(Hash, GetTypeHash(bFillBox));
	return Hash;
}

// =====================================================================================================
// N-gon
// =====================================================================================================

bool UPolygonShape::SetShapeValue(EWidgetShapeValue Value, float InValue)
{
	switch (Value)
	{
	case EWidgetShapeValue::Sides: Sides = FMath::Clamp(FMath::RoundToInt(InValue), 3, 100); break;
	case EWidgetShapeValue::CornerRounding: CornerRounding = FMath::Max(InValue, 0.f); break;
	default: return Super::SetShapeValue(Value, InValue);
	}
	InvalidateShape();
	return true;
}

bool UPolygonShape::GetShapeValue(EWidgetShapeValue Value, float& OutValue) const
{
	switch (Value)
	{
	case EWidgetShapeValue::Sides: OutValue = static_cast<float>(Sides); return true;
	case EWidgetShapeValue::CornerRounding: OutValue = CornerRounding; return true;
	default: return Super::GetShapeValue(Value, OutValue);
	}
}

void UPolygonShape::BuildPath(FWidgetShapePath& OutPath) const
{
	TArray<FVector2D> Corners;
	TArray<bool> bInner;
	float Aspect = 1.f;
	WidgetShapePolygons::MakeCorners(Sides, /*bStar=*/false, 1.f, 1.f, bFillBox, Corners, bInner, Aspect);
	ShapeWidgetsPrivate::AppendPolygonContour(OutPath, Corners, [this](int32) { return CornerRounding; });
}

float UPolygonShape::GetNaturalAspect() const
{
	TArray<FVector2D> Corners;
	TArray<bool> bInner;
	float Aspect = 1.f;
	WidgetShapePolygons::MakeCorners(Sides, /*bStar=*/false, 1.f, 1.f, bFillBox, Corners, bInner, Aspect);
	return Aspect;
}

uint32 UPolygonShape::GetMeshHash() const
{
	uint32 Hash = Super::GetMeshHash();
	Hash = HashCombineFast(Hash, GetTypeHash(Sides));
	Hash = HashCombineFast(Hash, GetTypeHash(CornerRounding));
	Hash = HashCombineFast(Hash, GetTypeHash(bFillBox));
	return Hash;
}

// =====================================================================================================
// Custom Shape
// =====================================================================================================

UCustomShape::UCustomShape()
{
	// Something to start from: a square, its corners Straight. Edit Shape on the canvas to make it anything else.
	FWidgetShapeContour& Contour = Path.Contours.AddDefaulted_GetRef();
	Contour.bClosed = true;
	Contour.Vertices.Add(FWidgetShapeVertex(FVector2D(0.0, 0.0)));
	Contour.Vertices.Add(FWidgetShapeVertex(FVector2D(1.0, 0.0)));
	Contour.Vertices.Add(FWidgetShapeVertex(FVector2D(1.0, 1.0)));
	Contour.Vertices.Add(FWidgetShapeVertex(FVector2D(0.0, 1.0)));
}

void UCustomShape::SetPath(const FWidgetShapePath& InPath)
{
	Path = InPath;
	InvalidateShape();
}

void UCustomShape::BuildPath(FWidgetShapePath& OutPath) const
{
	OutPath = Path;
}

uint32 UCustomShape::GetMeshHash() const
{
	return HashCombineFast(Super::GetMeshHash(), Path.GetHash());
}
