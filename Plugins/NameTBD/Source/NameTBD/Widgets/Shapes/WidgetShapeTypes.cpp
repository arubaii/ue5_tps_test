// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetShapeTypes.h"

#include "Algo/StableSort.h"

namespace WidgetShapeTypesPrivate
{
	// Gradients are blended in sRGB, as CSS and the vector tools blend them. It is also what the GPU does between two
	// vertices of a mesh whose colors are sRGB bytes - which Slate's are - so a color worked out here at a vertex and the
	// colors the GPU fills in around it agree, and a gradient cut at its stops comes out exactly linear.
	float ToSRGB(float Linear)
	{
		Linear = FMath::Max(Linear, 0.f);
		return Linear <= 0.0031308f ? Linear * 12.92f : 1.055f * FMath::Pow(Linear, 1.f / 2.4f) - 0.055f;
	}

	float FromSRGB(float Encoded)
	{
		Encoded = FMath::Max(Encoded, 0.f);
		return Encoded <= 0.04045f ? Encoded / 12.92f : FMath::Pow((Encoded + 0.055f) / 1.055f, 2.4f);
	}

	FLinearColor LerpSRGB(const FLinearColor& A, const FLinearColor& B, float Alpha)
	{
		return FLinearColor(
			FromSRGB(FMath::Lerp(ToSRGB(A.R), ToSRGB(B.R), Alpha)),
			FromSRGB(FMath::Lerp(ToSRGB(A.G), ToSRGB(B.G), Alpha)),
			FromSRGB(FMath::Lerp(ToSRGB(A.B), ToSRGB(B.B), Alpha)),
			FMath::Lerp(A.A, B.A, Alpha));
	}

	FVector2D Lerp(const FVector2D& A, const FVector2D& B, double Alpha)
	{
		return A + (B - A) * Alpha;
	}
}

// =====================================================================================================
// Paint
// =====================================================================================================

FWidgetShapePaint::FWidgetShapePaint()
{
	// A gradient switched on shows something straight away: white into a dark gray.
	GradientStops.Add(FWidgetShapeGradientStop(0.f, FLinearColor::White));
	GradientStops.Add(FWidgetShapeGradientStop(1.f, FLinearColor(0.05f, 0.05f, 0.05f, 1.f)));
}

float FWidgetShapePaint::GetGradientPosition(const FVector2f& BoxPoint) const
{
	if (Type == EWidgetShapePaintType::RadialGradient)
	{
		const FVector2f Center(static_cast<float>(GradientCenter.X), static_cast<float>(GradientCenter.Y));
		return (BoxPoint - Center).Size() / FMath::Max(GradientRadius, 0.001f);
	}

	// Linear: across the box along the angle, so the gradient's ends touch the box's corners whatever the angle.
	const float Radians = FMath::DegreesToRadians(GradientAngle);
	const FVector2f Direction(FMath::Cos(Radians), FMath::Sin(Radians));
	const float Extent = 0.5f * (FMath::Abs(Direction.X) + FMath::Abs(Direction.Y));
	const FVector2f FromMiddle = BoxPoint - FVector2f(0.5f, 0.5f);
	return 0.5f + FVector2f::DotProduct(FromMiddle, Direction) / (2.f * FMath::Max(Extent, KINDA_SMALL_NUMBER));
}

FLinearColor FWidgetShapePaint::SampleGradient(float Position) const
{
	using namespace WidgetShapeTypesPrivate;

	if (GradientStops.Num() == 0)
	{
		return FLinearColor::White;
	}
	if (GradientStops.Num() == 1)
	{
		return GradientStops[0].Color;
	}

	// Usually two or three stops: sorted on the stack rather than asking the author to keep them in order.
	TArray<FWidgetShapeGradientStop, TInlineAllocator<8>> Sorted(GradientStops);
	Algo::StableSortBy(Sorted, &FWidgetShapeGradientStop::Position);

	Position = FMath::Clamp(Position, 0.f, 1.f);
	if (Position <= Sorted[0].Position)
	{
		return Sorted[0].Color;
	}
	for (int32 Index = 1; Index < Sorted.Num(); ++Index)
	{
		const FWidgetShapeGradientStop& Before = Sorted[Index - 1];
		const FWidgetShapeGradientStop& After = Sorted[Index];
		if (Position <= After.Position)
		{
			const float Span = After.Position - Before.Position;
			const float Alpha = Span > KINDA_SMALL_NUMBER ? (Position - Before.Position) / Span : 1.f;
			return LerpSRGB(Before.Color, After.Color, Alpha);
		}
	}
	return Sorted.Last().Color;
}

FLinearColor FWidgetShapePaint::Evaluate(const FVector2f& BoxPoint) const
{
	if (IsGradient())
	{
		return SampleGradient(GetGradientPosition(BoxPoint)) * Color;
	}
	return Color;
}

void FWidgetShapePaint::GetStopPositions(TArray<float>& OutPositions) const
{
	OutPositions.Reset();

	// The ends always: past them the color is held, which a mesh blending across them wouldn't do.
	OutPositions.Add(0.f);
	OutPositions.Add(1.f);
	for (const FWidgetShapeGradientStop& Stop : GradientStops)
	{
		OutPositions.AddUnique(FMath::Clamp(Stop.Position, 0.f, 1.f));
	}
	OutPositions.Sort();
}

uint32 FWidgetShapePaint::GetLayoutHash() const
{
	uint32 Hash = GetTypeHash(static_cast<uint8>(Type));
	if (IsGradient())
	{
		Hash = HashCombineFast(Hash, GetTypeHash(GradientAngle));
		Hash = HashCombineFast(Hash, GetTypeHash(GradientCenter));
		Hash = HashCombineFast(Hash, GetTypeHash(GradientRadius));
		for (const FWidgetShapeGradientStop& Stop : GradientStops)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Stop.Position));
		}
	}
	return Hash;
}

FWidgetShapeStroke::FWidgetShapeStroke()
{
	Paint.Color = FLinearColor::Black;
}

bool FWidgetShapeTrim::IsWhole() const
{
	const float Low = FMath::Min(Start, End);
	const float High = FMath::Max(Start, End);
	return Low <= 0.f && High >= 1.f;
}

// =====================================================================================================
// Vertices
// =====================================================================================================

void FWidgetShapeVertex::EnforceType(bool bOutMoved)
{
	const FVector2D Moved = bOutMoved ? OutHandle : InHandle;
	FVector2D& Other = bOutMoved ? InHandle : OutHandle;

	switch (Type)
	{
	case EWidgetShapeVertexType::Mirrored:
		Other = -Moved;
		break;

	case EWidgetShapeVertexType::Asymmetric:
	{
		const double Length = Other.Size();
		const FVector2D Direction = Moved.GetSafeNormal();
		if (!Direction.IsNearlyZero())
		{
			Other = -Direction * Length;
		}
		break;
	}

	default:
		break;
	}
}

// =====================================================================================================
// Contours
// =====================================================================================================

int32 FWidgetShapeContour::GetSegmentCount() const
{
	const int32 Count = Vertices.Num();
	if (Count < 2)
	{
		return 0;
	}
	return bClosed ? Count : Count - 1;
}

void FWidgetShapeContour::GetSegment(int32 Segment, FVector2D& P0, FVector2D& P1, FVector2D& P2, FVector2D& P3) const
{
	const FWidgetShapeVertex& From = Vertices[Segment];
	const FWidgetShapeVertex& To = Vertices[(Segment + 1) % Vertices.Num()];
	P0 = From.Position;
	P1 = From.Position + From.GetOut();
	P2 = To.Position + To.GetIn();
	P3 = To.Position;
}

bool FWidgetShapeContour::IsSegmentStraight(int32 Segment) const
{
	const FWidgetShapeVertex& From = Vertices[Segment];
	const FWidgetShapeVertex& To = Vertices[(Segment + 1) % Vertices.Num()];
	return From.GetOut().IsNearlyZero(1e-6) && To.GetIn().IsNearlyZero(1e-6);
}

FVector2D FWidgetShapeContour::EvaluateSegment(int32 Segment, float T) const
{
	FVector2D P0, P1, P2, P3;
	GetSegment(Segment, P0, P1, P2, P3);
	const double U = 1.0 - T;
	return P0 * (U * U * U) + P1 * (3.0 * U * U * T) + P2 * (3.0 * U * T * T) + P3 * (static_cast<double>(T) * T * T);
}

int32 FWidgetShapeContour::SplitSegment(int32 Segment, float T)
{
	using WidgetShapeTypesPrivate::Lerp;

	const int32 FromIndex = Segment;
	const int32 ToIndex = (Segment + 1) % Vertices.Num();
	const int32 NewIndex = Segment + 1;

	FWidgetShapeVertex NewVertex;
	if (IsSegmentStraight(Segment))
	{
		NewVertex.Position = Lerp(Vertices[FromIndex].Position, Vertices[ToIndex].Position, T);
		NewVertex.Type = EWidgetShapeVertexType::Straight;
	}
	else
	{
		// De Casteljau: the two halves of the curve, so nothing moves.
		FVector2D P0, P1, P2, P3;
		GetSegment(Segment, P0, P1, P2, P3);
		const FVector2D A = Lerp(P0, P1, T);
		const FVector2D B = Lerp(P1, P2, T);
		const FVector2D C = Lerp(P2, P3, T);
		const FVector2D D = Lerp(A, B, T);
		const FVector2D E = Lerp(B, C, T);
		const FVector2D Point = Lerp(D, E, T);

		NewVertex.Position = Point;
		NewVertex.Type = EWidgetShapeVertexType::Asymmetric;
		NewVertex.InHandle = D - Point;
		NewVertex.OutHandle = E - Point;

		// The ends' handles shorten to the halves'. A Straight end has none, and a curve through it keeps none.
		FWidgetShapeVertex& From = Vertices[FromIndex];
		FWidgetShapeVertex& To = Vertices[ToIndex];
		if (From.HasHandles())
		{
			From.OutHandle = A - P0;
			if (From.Type == EWidgetShapeVertexType::Mirrored)
			{
				// Its handles no longer match in length: still smooth, so Asymmetric.
				From.Type = EWidgetShapeVertexType::Asymmetric;
			}
		}
		if (To.HasHandles())
		{
			To.InHandle = C - P3;
			if (To.Type == EWidgetShapeVertexType::Mirrored)
			{
				To.Type = EWidgetShapeVertexType::Asymmetric;
			}
		}
	}

	Vertices.Insert(NewVertex, NewIndex);
	return NewIndex;
}

void FWidgetShapeContour::Reverse()
{
	if (Vertices.Num() < 2)
	{
		return;
	}

	// The first vertex stays first so a Trim Path keeps starting where it did; the rest run the other way round.
	TArray<FWidgetShapeVertex> Reversed;
	Reversed.Reserve(Vertices.Num());
	Reversed.Add(Vertices[0]);
	for (int32 Index = Vertices.Num() - 1; Index >= 1; --Index)
	{
		Reversed.Add(Vertices[Index]);
	}
	if (!bClosed)
	{
		// An open contour can't keep its first vertex first: its ends swap.
		Reversed.Reset();
		for (int32 Index = Vertices.Num() - 1; Index >= 0; --Index)
		{
			Reversed.Add(Vertices[Index]);
		}
	}
	for (FWidgetShapeVertex& Vertex : Reversed)
	{
		Swap(Vertex.InHandle, Vertex.OutHandle);
	}
	Vertices = MoveTemp(Reversed);
}

// =====================================================================================================
// Paths
// =====================================================================================================

int32 FWidgetShapePath::GetVertexCount() const
{
	int32 Count = 0;
	for (const FWidgetShapeContour& Contour : Contours)
	{
		Count += Contour.Vertices.Num();
	}
	return Count;
}

FBox2D FWidgetShapePath::GetBounds(bool bIncludeHandles) const
{
	FBox2D Bounds(ForceInit);
	for (const FWidgetShapeContour& Contour : Contours)
	{
		for (const FWidgetShapeVertex& Vertex : Contour.Vertices)
		{
			Bounds += Vertex.Position;
			if (bIncludeHandles && Vertex.HasHandles())
			{
				Bounds += Vertex.Position + Vertex.InHandle;
				Bounds += Vertex.Position + Vertex.OutHandle;
			}
		}
	}
	return Bounds;
}

void FWidgetShapePath::FitToBox()
{
	// The curve's extremes, not its handles': a handle may stick out well past what is drawn.
	FBox2D Bounds(ForceInit);
	for (const FWidgetShapeContour& Contour : Contours)
	{
		for (int32 Segment = 0; Segment < Contour.GetSegmentCount(); ++Segment)
		{
			for (int32 Step = 0; Step <= 32; ++Step)
			{
				Bounds += Contour.EvaluateSegment(Segment, Step / 32.f);
			}
		}
		if (Contour.Vertices.Num() == 1)
		{
			Bounds += Contour.Vertices[0].Position;
		}
	}
	if (!Bounds.bIsValid)
	{
		return;
	}

	const FVector2D Size = Bounds.GetSize();
	const FVector2D Scale(Size.X > 1e-6 ? 1.0 / Size.X : 1.0, Size.Y > 1e-6 ? 1.0 / Size.Y : 1.0);
	for (FWidgetShapeContour& Contour : Contours)
	{
		for (FWidgetShapeVertex& Vertex : Contour.Vertices)
		{
			Vertex.Position = (Vertex.Position - Bounds.Min) * Scale;
			Vertex.InHandle *= Scale;
			Vertex.OutHandle *= Scale;
		}
	}
}

void FWidgetShapePath::Flip(bool bHorizontal)
{
	for (FWidgetShapeContour& Contour : Contours)
	{
		for (FWidgetShapeVertex& Vertex : Contour.Vertices)
		{
			if (bHorizontal)
			{
				Vertex.Position.X = 1.0 - Vertex.Position.X;
				Vertex.InHandle.X = -Vertex.InHandle.X;
				Vertex.OutHandle.X = -Vertex.OutHandle.X;
			}
			else
			{
				Vertex.Position.Y = 1.0 - Vertex.Position.Y;
				Vertex.InHandle.Y = -Vertex.InHandle.Y;
				Vertex.OutHandle.Y = -Vertex.OutHandle.Y;
			}
		}
	}
}

uint32 FWidgetShapePath::GetHash() const
{
	uint32 Hash = GetTypeHash(Contours.Num());
	for (const FWidgetShapeContour& Contour : Contours)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Contour.bClosed));
		for (const FWidgetShapeVertex& Vertex : Contour.Vertices)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Vertex.Position));
			Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Vertex.Type)));
			Hash = HashCombineFast(Hash, GetTypeHash(Vertex.InHandle));
			Hash = HashCombineFast(Hash, GetTypeHash(Vertex.OutHandle));
			Hash = HashCombineFast(Hash, GetTypeHash(Vertex.CornerRadius));
		}
	}
	return Hash;
}
