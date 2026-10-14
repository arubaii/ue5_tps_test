// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetPathTypes.h"

#include "NameTBD/Widgets/WidgetEffectsDraw.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"

#include "Algo/Reverse.h"

// =====================================================================================================
// Settings
// =====================================================================================================

uint32 FWidgetPathRouting::GetHash() const
{
	uint32 Hash = GetTypeHash(static_cast<uint8>(Route));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Orientation)));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(FromSide)));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(ToSide)));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Corner)));
	Hash = HashCombineFast(Hash, GetTypeHash(CornerRadius));
	Hash = HashCombineFast(Hash, GetTypeHash(Curvature));
	Hash = HashCombineFast(Hash, GetTypeHash(ElbowPosition));
	Hash = HashCombineFast(Hash, GetTypeHash(Bend));
	Hash = HashCombineFast(Hash, GetTypeHash(OrbitCenter));
	Hash = HashCombineFast(Hash, GetTypeHash(OrbitPoint));
	Hash = HashCombineFast(Hash, GetTypeHash(bOrbitLongWay));
	for (const FVector2D& Point : Waypoints)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Point));
	}
	Hash = HashCombineFast(Hash, GetTypeHash(bSmoothWaypoints));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(NodeShape)));
	Hash = HashCombineFast(Hash, GetTypeHash(StartGap));
	Hash = HashCombineFast(Hash, GetTypeHash(EndGap));
	Hash = HashCombineFast(Hash, GetTypeHash(bSpreadPorts));
	Hash = HashCombineFast(Hash, GetTypeHash(PortSpacing));
	return Hash;
}

FWidgetPathLook::FWidgetPathLook()
{
	GradientStops.Add(FWidgetShapeGradientStop(0.f, FLinearColor::White));
	GradientStops.Add(FWidgetShapeGradientStop(1.f, FLinearColor(1.f, 1.f, 1.f, 0.35f)));
}

bool FWidgetPathLook::IsAnimated() const
{
	return (DashLength > 0.f && DashSpeed != 0.f)
		|| (Flow != EWidgetPathFlow::None && FlowSpeed != 0.f)
		|| Breathe > 0.f
		|| (Paint == EWidgetPathPaint::Image && ImageScrollSpeed != 0.f);
}

FLinearColor FWidgetPathLook::ColorAt(float Fraction) const
{
	if (Paint != EWidgetPathPaint::Gradient || GradientStops.Num() == 0)
	{
		return Color;
	}

	// Stops in any order: the nearest below and above Fraction.
	const float Position = FMath::Clamp(Fraction, 0.f, 1.f);
	const FWidgetShapeGradientStop* Below = nullptr;
	const FWidgetShapeGradientStop* Above = nullptr;
	for (const FWidgetShapeGradientStop& Stop : GradientStops)
	{
		if (Stop.Position <= Position && (!Below || Stop.Position >= Below->Position))
		{
			Below = &Stop;
		}
		if (Stop.Position >= Position && (!Above || Stop.Position <= Above->Position))
		{
			Above = &Stop;
		}
	}
	FLinearColor Blended;
	if (!Below)
	{
		Blended = Above->Color;
	}
	else if (!Above || Above == Below || Above->Position <= Below->Position)
	{
		Blended = Below->Color;
	}
	else
	{
		Blended = FMath::Lerp(Below->Color, Above->Color, (Position - Below->Position) / (Above->Position - Below->Position));
	}
	return Blended * Color;
}

// =====================================================================================================
// Geometry
// =====================================================================================================

float FWidgetPathNodeBox::DistanceTo(const FVector2f& Point) const
{
	const FVector2f Offset = Point - Center;
	if (bPoint)
	{
		return Offset.Size();
	}
	if (bEllipse)
	{
		// Along the ray from the middle: how far past the ellipse the point is (what clipping a line aimed at it needs).
		const FVector2f Radii2(FMath::Max(HalfSize.X, 0.01f), FMath::Max(HalfSize.Y, 0.01f));
		const float Scaled = (Offset / Radii2).Size();
		if (Scaled <= KINDA_SMALL_NUMBER)
		{
			return -FMath::Min(Radii2.X, Radii2.Y);
		}
		return Offset.Size() * (1.f - 1.f / Scaled);
	}
	return WidgetEffectsDraw::RoundedRectDistance(Point, Center, HalfSize, Radii);
}

namespace WidgetPathGeometryPrivate
{
	bool IsCenterSide(EWidgetPathSide Side)
	{
		return Side == EWidgetPathSide::Center || Side == EWidgetPathSide::Auto;
	}

	/** Whether the route runs across (true) or down: Orientation, or whichever way the nodes are further apart. */
	bool IsHorizontal(const FWidgetPathRouteInput& Input)
	{
		if (Input.Routing.Orientation != EWidgetPathAxis::Auto)
		{
			return Input.Routing.Orientation == EWidgetPathAxis::Horizontal;
		}
		const FVector2f Delta = Input.To.Center - Input.From.Center;
		const float ApartX = FMath::Abs(Delta.X) - (Input.From.HalfSize.X + Input.To.HalfSize.X);
		const float ApartY = FMath::Abs(Delta.Y) - (Input.From.HalfSize.Y + Input.To.HalfSize.Y);
		return ApartX >= ApartY;
	}

	/** Where a line attaches to a side of a node: the side's middle, slid Offset along it, Gap out from it. */
	FVector2f PortPoint(const FWidgetPathNodeBox& Node, EWidgetPathSide Side, float Offset, float Gap)
	{
		const FVector2f Normal = WidgetPathGeometry::SideNormal(Side);
		if (Normal.IsNearlyZero())
		{
			return Node.Center;
		}
		const FVector2f Along(FMath::Abs(Normal.Y), FMath::Abs(Normal.X));
		const FVector2f Reach = Node.bPoint ? FVector2f::ZeroVector : FVector2f(Node.HalfSize.X * Normal.X, Node.HalfSize.Y * Normal.Y);
		return Node.Center + Reach + Along * Offset + Normal * Gap;
	}

	float Sign(float Value)
	{
		return Value < 0.f ? -1.f : 1.f;
	}

	/** Wraps an angle into (-PI, PI]. */
	float WrapAngle(float Angle)
	{
		while (Angle > UE_PI)
		{
			Angle -= UE_TWO_PI;
		}
		while (Angle <= -UE_PI)
		{
			Angle += UE_TWO_PI;
		}
		return Angle;
	}

	/** Drops points that repeat the last one or sit on a straight run between their neighbours. */
	void Simplify(TArray<FVector2f>& Points)
	{
		TArray<FVector2f> Out;
		Out.Reserve(Points.Num());
		for (const FVector2f& Point : Points)
		{
			if (Out.Num() > 0 && FVector2f::DistSquared(Out.Last(), Point) < 1e-6f)
			{
				continue;
			}
			if (Out.Num() >= 2)
			{
				const FVector2f A = Out[Out.Num() - 2];
				const FVector2f B = Out.Last();
				const FVector2f In = (B - A).GetSafeNormal();
				const FVector2f OutDir = (Point - B).GetSafeNormal();
				if (FMath::Abs(FVector2f::CrossProduct(In, OutDir)) < 1e-4f && FVector2f::DotProduct(In, OutDir) > 0.f)
				{
					Out.Last() = Point;
					continue;
				}
			}
			Out.Add(Point);
		}
		Points = MoveTemp(Out);
	}

	/**
	 * Corners of a polyline shaped: rounded by a circular arc of Radius (a cubic close enough to one), cut off Radius back,
	 * or left sharp. Each corner takes at most half of each run it sits on (all of a run to an end).
	 */
	void ShapeCorners(const TArray<FVector2f>& Points, EWidgetPathCorner Corner, float Radius, float Tolerance, TArray<FVector2f>& Out)
	{
		Out.Reset();
		const int32 Count = Points.Num();
		if (Count < 3 || Corner == EWidgetPathCorner::Sharp || Radius <= 0.f)
		{
			Out = Points;
			return;
		}

		Out.Add(Points[0]);
		for (int32 Index = 1; Index < Count - 1; ++Index)
		{
			const FVector2f Previous = Points[Index - 1];
			const FVector2f Point = Points[Index];
			const FVector2f Next = Points[Index + 1];
			const float LengthIn = FVector2f::Distance(Previous, Point);
			const float LengthOut = FVector2f::Distance(Point, Next);
			if (LengthIn < 1e-3f || LengthOut < 1e-3f)
			{
				Out.Add(Point);
				continue;
			}
			const FVector2f DirIn = (Point - Previous) / LengthIn;
			const FVector2f DirOut = (Next - Point) / LengthOut;
			const float Turn = FMath::Acos(FMath::Clamp(FVector2f::DotProduct(DirIn, DirOut), -1.f, 1.f));
			if (Turn < 1e-3f)
			{
				Out.Add(Point);
				continue;
			}
			const float MaxTangent = FMath::Min(LengthIn * (Index == 1 ? 1.f : 0.5f), LengthOut * (Index == Count - 2 ? 1.f : 0.5f));
			if (Corner == EWidgetPathCorner::Chamfer)
			{
				const float Cut = FMath::Min(Radius, MaxTangent);
				Out.Add(Point - DirIn * Cut);
				Out.Add(Point + DirOut * Cut);
				continue;
			}

			// A circular fillet: tangent length R tan(turn / 2), held to what fits (the radius shrinks with it).
			const float HalfTan = FMath::Tan(Turn * 0.5f);
			if (HalfTan > 1e3f)
			{
				// Turning straight back: no arc fits.
				Out.Add(Point);
				continue;
			}
			float Tangent = Radius * HalfTan;
			float ArcRadius = Radius;
			if (Tangent > MaxTangent)
			{
				Tangent = MaxTangent;
				ArcRadius = Tangent / FMath::Max(HalfTan, 1e-4f);
			}
			const FVector2f Start = Point - DirIn * Tangent;
			const FVector2f End = Point + DirOut * Tangent;
			const float Handle = (4.f / 3.f) * FMath::Tan(Turn * 0.25f) * ArcRadius;
			Out.Add(Start);
			WidgetShapeGeometry::AppendCubic(Out, Start, Start + DirIn * Handle, End - DirOut * Handle, End, Tolerance, /*bIncludeStart=*/false);
		}
		Out.Add(Points.Last());
	}

	/** A Catmull-Rom curve through every point. */
	void SmoothThrough(const TArray<FVector2f>& Points, float Tolerance, TArray<FVector2f>& Out)
	{
		Out.Reset();
		const int32 Count = Points.Num();
		if (Count < 3)
		{
			Out = Points;
			return;
		}
		Out.Add(Points[0]);
		for (int32 Index = 0; Index < Count - 1; ++Index)
		{
			const FVector2f P0 = Points[FMath::Max(Index - 1, 0)];
			const FVector2f P1 = Points[Index];
			const FVector2f P2 = Points[Index + 1];
			const FVector2f P3 = Points[FMath::Min(Index + 2, Count - 1)];
			WidgetShapeGeometry::AppendCubic(Out, P1, P1 + (P2 - P0) / 6.f, P2 - (P3 - P1) / 6.f, P2, Tolerance, /*bIncludeStart=*/false);
		}
	}

	/** How many steps an arc of Radius over Sweep radians needs to stay within Tolerance. */
	int32 ArcSteps(float Radius, float Sweep, float Tolerance)
	{
		const float Ratio = FMath::Clamp(1.f - Tolerance / FMath::Max(Radius, 1e-3f), -1.f, 1.f);
		const float Step = FMath::Max(2.f * FMath::Acos(Ratio), 0.01f);
		return FMath::Clamp(FMath::CeilToInt(FMath::Abs(Sweep) / Step), 2, 512);
	}

	/** The right-angled route: out of each node along its side, across, in. Goes round when the target is behind. */
	void BuildElbow(const FVector2f& Start, const FVector2f& FromDir, const FVector2f& End, const FVector2f& ToDir, float ElbowPosition, float Stub, TArray<FVector2f>& Out)
	{
		Out.Reset();
		Out.Add(Start);
		const bool bParallel = FMath::Abs(FVector2f::DotProduct(FromDir, ToDir)) > 0.5f;
		if (bParallel)
		{
			// Both ends along one axis (P), crossing along the other (Q).
			const FVector2f AxisP(FMath::Abs(FromDir.X), FMath::Abs(FromDir.Y));
			const FVector2f AxisQ(AxisP.Y, AxisP.X);
			const float Ahead = FVector2f::DotProduct(End - Start, FromDir);
			const bool bFacing = FVector2f::DotProduct(FromDir, ToDir) < 0.f;
			if (bFacing && Ahead >= Stub)
			{
				const float Cross = FVector2f::DotProduct(Start, AxisP) + FVector2f::DotProduct(End - Start, AxisP) * ElbowPosition;
				const FVector2f A = AxisP * Cross + AxisQ * FVector2f::DotProduct(Start, AxisQ);
				const FVector2f B = AxisP * Cross + AxisQ * FVector2f::DotProduct(End, AxisQ);
				Out.Add(A);
				Out.Add(B);
			}
			else if (!bFacing)
			{
				// Both leave the same way: out past the further of the two, then across.
				const float StartOut = FVector2f::DotProduct(Start + FromDir * Stub, FromDir);
				const float EndOut = FVector2f::DotProduct(End + ToDir * Stub, FromDir);
				const float Reach = FMath::Max(StartOut, EndOut);
				const float Cross = Reach * FVector2f::DotProduct(FromDir, AxisP);
				Out.Add(AxisP * Cross + AxisQ * FVector2f::DotProduct(Start, AxisQ));
				Out.Add(AxisP * Cross + AxisQ * FVector2f::DotProduct(End, AxisQ));
			}
			else
			{
				// The target is behind: out, across halfway between them, back past it, in.
				const FVector2f OutStart = Start + FromDir * Stub;
				const FVector2f OutEnd = End + ToDir * Stub;
				const float Middle = (FVector2f::DotProduct(Start, AxisQ) + FVector2f::DotProduct(End, AxisQ)) * 0.5f;
				Out.Add(OutStart);
				Out.Add(AxisP * FVector2f::DotProduct(OutStart, AxisP) + AxisQ * Middle);
				Out.Add(AxisP * FVector2f::DotProduct(OutEnd, AxisP) + AxisQ * Middle);
				Out.Add(OutEnd);
			}
		}
		else
		{
			// One end across, the other down: an L when each end faces the other, else out of both and joined.
			const float Ahead = FVector2f::DotProduct(End - Start, FromDir);
			const float Behind = FVector2f::DotProduct(Start - End, ToDir);
			if (Ahead >= Stub * 0.5f && Behind >= Stub * 0.5f)
			{
				Out.Add(Start + FromDir * Ahead);
			}
			else
			{
				const FVector2f OutStart = Start + FromDir * Stub;
				const FVector2f OutEnd = End + ToDir * Stub;
				const FVector2f AxisTo(FMath::Abs(ToDir.X), FMath::Abs(ToDir.Y));
				Out.Add(OutStart);
				Out.Add(OutStart + AxisTo * FVector2f::DotProduct(OutEnd - OutStart, AxisTo));
				Out.Add(OutEnd);
			}
		}
		Out.Add(End);
		Simplify(Out);
	}

	/** Straight runs joined by a 45-degree diagonal, the first run along Axis. */
	void BuildDiagonal(const FVector2f& Start, const FVector2f& End, bool bHorizontal, float ElbowPosition, TArray<FVector2f>& Out)
	{
		const FVector2f AxisA = bHorizontal ? FVector2f(1.f, 0.f) : FVector2f(0.f, 1.f);
		const FVector2f AxisB = bHorizontal ? FVector2f(0.f, 1.f) : FVector2f(1.f, 0.f);
		const FVector2f Delta = End - Start;
		const float AlongA = FVector2f::DotProduct(Delta, AxisA);
		const float AlongB = FVector2f::DotProduct(Delta, AxisB);
		Out.Reset();
		Out.Add(Start);
		if (FMath::Abs(AlongA) >= FMath::Abs(AlongB))
		{
			const float Rest = FMath::Abs(AlongA) - FMath::Abs(AlongB);
			const FVector2f First = Start + AxisA * (Sign(AlongA) * Rest * ElbowPosition);
			Out.Add(First);
			Out.Add(First + AxisA * (Sign(AlongA) * FMath::Abs(AlongB)) + AxisB * AlongB);
		}
		else
		{
			const float Rest = FMath::Abs(AlongB) - FMath::Abs(AlongA);
			const FVector2f First = Start + AxisB * (Sign(AlongB) * Rest * ElbowPosition);
			Out.Add(First);
			Out.Add(First + AxisA * AlongA + AxisB * (Sign(AlongB) * FMath::Abs(AlongA)));
		}
		Out.Add(End);
		Simplify(Out);
	}

	/** A circular arc from Start to End bowing Bend (fraction of half the chord) to the left of the way it runs. */
	void BuildArc(const FVector2f& Start, const FVector2f& End, float Bend, float Tolerance, TArray<FVector2f>& Out)
	{
		Out.Reset();
		const FVector2f Chord = End - Start;
		const float ChordLength = Chord.Size();
		const float Sagitta = Bend * ChordLength * 0.5f;
		if (ChordLength < 1e-3f || FMath::Abs(Sagitta) < 0.05f)
		{
			Out.Add(Start);
			Out.Add(End);
			return;
		}
		const FVector2f Normal(Chord.Y / ChordLength, -Chord.X / ChordLength);
		const FVector2f Middle = (Start + End) * 0.5f;
		const float Radius = (ChordLength * ChordLength * 0.25f + Sagitta * Sagitta) / (2.f * FMath::Abs(Sagitta));
		const FVector2f Peak = Middle + Normal * Sagitta;
		const FVector2f ArcCenter = Peak - Normal * (Sign(Sagitta) * Radius);

		const float StartAngle = FMath::Atan2(Start.Y - ArcCenter.Y, Start.X - ArcCenter.X);
		const float EndAngle = FMath::Atan2(End.Y - ArcCenter.Y, End.X - ArcCenter.X);
		const float PeakAngle = FMath::Atan2(Peak.Y - ArcCenter.Y, Peak.X - ArcCenter.X);
		float Sweep = WrapAngle(EndAngle - StartAngle);
		const float ToPeak = WrapAngle(PeakAngle - StartAngle);
		if (Sign(ToPeak) != Sign(Sweep) || FMath::Abs(ToPeak) > FMath::Abs(Sweep))
		{
			Sweep -= Sign(Sweep) * UE_TWO_PI;
		}
		const int32 Steps = ArcSteps(Radius, Sweep, Tolerance);
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Angle = StartAngle + Sweep * (static_cast<float>(Step) / Steps);
			Out.Add(ArcCenter + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		Out[0] = Start;
		Out.Last() = End;
	}

	/** Round Center from Start to End, the radius changing evenly between theirs (a spiral when they differ). */
	void BuildOrbit(const FVector2f& Start, const FVector2f& End, const FVector2f& OrbitCenter, bool bLongWay, float Tolerance, TArray<FVector2f>& Out)
	{
		Out.Reset();
		const float StartRadius = FVector2f::Distance(Start, OrbitCenter);
		const float EndRadius = FVector2f::Distance(End, OrbitCenter);
		if (StartRadius < 1.f || EndRadius < 1.f)
		{
			Out.Add(Start);
			Out.Add(End);
			return;
		}
		const float StartAngle = FMath::Atan2(Start.Y - OrbitCenter.Y, Start.X - OrbitCenter.X);
		const float EndAngle = FMath::Atan2(End.Y - OrbitCenter.Y, End.X - OrbitCenter.X);
		float Sweep = WrapAngle(EndAngle - StartAngle);
		if (bLongWay && FMath::Abs(Sweep) > 1e-4f)
		{
			Sweep -= Sign(Sweep) * UE_TWO_PI;
		}
		if (FMath::Abs(Sweep) < 1e-4f)
		{
			// Along a spoke.
			Out.Add(Start);
			Out.Add(End);
			return;
		}
		const int32 Steps = ArcSteps(FMath::Max(StartRadius, EndRadius), Sweep, Tolerance);
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Alpha = static_cast<float>(Step) / Steps;
			const float Angle = StartAngle + Sweep * Alpha;
			const float Radius = FMath::Lerp(StartRadius, EndRadius, Alpha);
			Out.Add(OrbitCenter + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		Out[0] = Start;
		Out.Last() = End;
	}

	/** Where along segment A-B a node's distance first reaches Gap, searched (the distance grows along it). */
	FVector2f FindExit(const FWidgetPathNodeBox& Node, const FVector2f& Inside, const FVector2f& Outside, float Gap)
	{
		FVector2f Low = Inside;
		FVector2f High = Outside;
		for (int32 Iteration = 0; Iteration < 24; ++Iteration)
		{
			const FVector2f Mid = (Low + High) * 0.5f;
			if (Node.DistanceTo(Mid) < Gap)
			{
				Low = Mid;
			}
			else
			{
				High = Mid;
			}
		}
		return High;
	}

	/** Cuts the start of a line off where it is still within Gap of the node's outline. False when it never leaves. */
	bool ClipStart(TArray<FVector2f>& Points, const FWidgetPathNodeBox& Node, float Gap)
	{
		if (!Node.bValid || Points.Num() < 2)
		{
			return Points.Num() >= 2;
		}
		if (Node.DistanceTo(Points[0]) >= Gap)
		{
			return true;
		}
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			if (Node.DistanceTo(Points[Index]) >= Gap)
			{
				const FVector2f Exit = FindExit(Node, Points[Index - 1], Points[Index], Gap);
				Points.RemoveAt(0, Index - 1, EAllowShrinking::No);
				Points[0] = Exit;
				return Points.Num() >= 2;
			}
		}
		return false;
	}

	bool ClipEnd(TArray<FVector2f>& Points, const FWidgetPathNodeBox& Node, float Gap)
	{
		Algo::Reverse(Points);
		const bool bKept = ClipStart(Points, Node, Gap);
		Algo::Reverse(Points);
		return bKept;
	}
}

namespace WidgetPathGeometry
{
	using namespace WidgetPathGeometryPrivate;

	FVector2f SideNormal(EWidgetPathSide Side)
	{
		switch (Side)
		{
		case EWidgetPathSide::Left: return FVector2f(-1.f, 0.f);
		case EWidgetPathSide::Right: return FVector2f(1.f, 0.f);
		case EWidgetPathSide::Top: return FVector2f(0.f, -1.f);
		case EWidgetPathSide::Bottom: return FVector2f(0.f, 1.f);
		default: return FVector2f::ZeroVector;
		}
	}

	void ResolveSides(const FWidgetPathRouteInput& Input, EWidgetPathSide& OutFrom, EWidgetPathSide& OutTo)
	{
		OutFrom = Input.Routing.FromSide;
		OutTo = Input.Routing.ToSide;
		const EWidgetPathRoute Route = Input.Routing.Route;
		const bool bAimed = Route == EWidgetPathRoute::Straight || Route == EWidgetPathRoute::Arc || Route == EWidgetPathRoute::Orbit || Route == EWidgetPathRoute::Waypoints;
		if (bAimed)
		{
			OutFrom = OutFrom == EWidgetPathSide::Auto ? EWidgetPathSide::Center : OutFrom;
			OutTo = OutTo == EWidgetPathSide::Auto ? EWidgetPathSide::Center : OutTo;
			return;
		}

		const FVector2f Delta = Input.To.Center - Input.From.Center;
		const bool bHorizontal = IsHorizontal(Input);
		const EWidgetPathSide Forward = bHorizontal ? (Delta.X >= 0.f ? EWidgetPathSide::Right : EWidgetPathSide::Left) : (Delta.Y >= 0.f ? EWidgetPathSide::Bottom : EWidgetPathSide::Top);
		const EWidgetPathSide Backward = bHorizontal ? (Delta.X >= 0.f ? EWidgetPathSide::Left : EWidgetPathSide::Right) : (Delta.Y >= 0.f ? EWidgetPathSide::Top : EWidgetPathSide::Bottom);
		if (OutFrom == EWidgetPathSide::Auto)
		{
			OutFrom = Forward;
		}
		if (OutTo == EWidgetPathSide::Auto)
		{
			if (Route == EWidgetPathRoute::Step)
			{
				// Turns once: enters across the first run.
				OutTo = bHorizontal ? (Delta.Y >= 0.f ? EWidgetPathSide::Top : EWidgetPathSide::Bottom) : (Delta.X >= 0.f ? EWidgetPathSide::Left : EWidgetPathSide::Right);
				if (FMath::Abs(bHorizontal ? Delta.Y : Delta.X) < (bHorizontal ? Input.To.HalfSize.Y : Input.To.HalfSize.X))
				{
					// Level with it: straight in.
					OutTo = Backward;
				}
			}
			else
			{
				OutTo = Backward;
			}
		}
	}

	void BuildRoute(const FWidgetPathRouteInput& Input, float Tolerance, TArray<FVector2f>& OutPoints)
	{
		OutPoints.Reset();
		const FWidgetPathRouting& Routing = Input.Routing;

		EWidgetPathSide FromSide;
		EWidgetPathSide ToSide;
		ResolveSides(Input, FromSide, ToSide);
		const bool bFromCenter = IsCenterSide(FromSide);
		const bool bToCenter = IsCenterSide(ToSide);
		const FVector2f Start = bFromCenter ? Input.From.Center : PortPoint(Input.From, FromSide, Input.FromPortOffset, Routing.StartGap);
		const FVector2f End = bToCenter ? Input.To.Center : PortPoint(Input.To, ToSide, Input.ToPortOffset, Routing.EndGap);

		// The way each end leaves its node: its side, or along the route's axis toward the other.
		const bool bHorizontal = IsHorizontal(Input);
		const FVector2f Axis = bHorizontal ? FVector2f(1.f, 0.f) : FVector2f(0.f, 1.f);
		const float Toward = Sign(FVector2f::DotProduct(End - Start, Axis));
		const FVector2f FromDir = bFromCenter ? Axis * Toward : SideNormal(FromSide);
		const FVector2f ToDir = bToCenter ? Axis * -Toward : SideNormal(ToSide);
		const float Stub = FMath::Max(Routing.CornerRadius, 12.f);

		TArray<FVector2f> Corners;
		switch (Routing.Route)
		{
		case EWidgetPathRoute::Straight:
			OutPoints.Add(Start);
			OutPoints.Add(End);
			break;

		case EWidgetPathRoute::Curve:
		{
			const float Span = FMath::Max3(FMath::Abs(FVector2f::DotProduct(End - Start, Axis)), 0.3f * FVector2f::Distance(Start, End), 24.f);
			const float Reach = Routing.Curvature * Span;
			OutPoints.Add(Start);
			WidgetShapeGeometry::AppendCubic(OutPoints, Start, Start + FromDir * Reach, End + ToDir * Reach, End, Tolerance, /*bIncludeStart=*/false);
			break;
		}

		case EWidgetPathRoute::Elbow:
		case EWidgetPathRoute::Step:
			BuildElbow(Start, FromDir, End, ToDir, Routing.Route == EWidgetPathRoute::Step ? 1.f : Routing.ElbowPosition, Stub, Corners);
			ShapeCorners(Corners, Routing.Corner, Routing.CornerRadius, Tolerance, OutPoints);
			break;

		case EWidgetPathRoute::Diagonal:
			BuildDiagonal(Start, End, FMath::Abs(FromDir.X) >= FMath::Abs(FromDir.Y), Routing.ElbowPosition, Corners);
			ShapeCorners(Corners, Routing.Corner, Routing.CornerRadius, Tolerance, OutPoints);
			break;

		case EWidgetPathRoute::Arc:
			BuildArc(Start, End, Routing.Bend, Tolerance, OutPoints);
			break;

		case EWidgetPathRoute::Orbit:
			BuildOrbit(Start, End, Input.OrbitCenter, Routing.bOrbitLongWay, Tolerance, OutPoints);
			break;

		case EWidgetPathRoute::Waypoints:
		{
			Corners.Add(Start);
			for (const FVector2D& Waypoint : Routing.Waypoints)
			{
				Corners.Add(FVector2f(static_cast<float>(Waypoint.X), static_cast<float>(Waypoint.Y)));
			}
			Corners.Add(End);
			Simplify(Corners);
			if (Routing.bSmoothWaypoints)
			{
				SmoothThrough(Corners, Tolerance, OutPoints);
			}
			else
			{
				ShapeCorners(Corners, Routing.Corner, Routing.CornerRadius, Tolerance, OutPoints);
			}
			break;
		}
		}

		// Ends aimed at a node's middle stop at its outline, Gap short of it.
		if (bFromCenter && !ClipStart(OutPoints, Input.From, Routing.StartGap))
		{
			OutPoints.Reset();
			return;
		}
		if (bToCenter && !ClipEnd(OutPoints, Input.To, Routing.EndGap))
		{
			OutPoints.Reset();
			return;
		}
		Simplify(OutPoints);
		if (OutPoints.Num() < 2)
		{
			OutPoints.Reset();
		}
	}

	void Resample(TArray<FVector2f>& Points, float MaxStep)
	{
		if (Points.Num() < 2 || MaxStep <= 0.f)
		{
			return;
		}
		TArray<FVector2f> Out;
		Out.Reserve(Points.Num() * 2);
		Out.Add(Points[0]);
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector2f From = Points[Index - 1];
			const FVector2f To = Points[Index];
			const int32 Pieces = FMath::Clamp(FMath::CeilToInt(FVector2f::Distance(From, To) / MaxStep), 1, 4096);
			for (int32 Piece = 1; Piece <= Pieces; ++Piece)
			{
				Out.Add(FMath::Lerp(From, To, static_cast<float>(Piece) / Pieces));
			}
		}
		Points = MoveTemp(Out);
	}

	void CutBetween(const TArray<FVector2f>& Points, float StartDistance, float EndDistance, TArray<FVector2f>& Out)
	{
		Out.Reset();
		if (Points.Num() < 2 || EndDistance <= StartDistance)
		{
			return;
		}
		float Walked = 0.f;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector2f From = Points[Index - 1];
			const FVector2f To = Points[Index];
			const float Length = FVector2f::Distance(From, To);
			const float SegmentStart = Walked;
			const float SegmentEnd = Walked + Length;
			Walked = SegmentEnd;
			if (SegmentEnd < StartDistance || Length <= 0.f)
			{
				continue;
			}
			if (SegmentStart > EndDistance)
			{
				break;
			}
			if (Out.Num() == 0)
			{
				Out.Add(FMath::Lerp(From, To, FMath::Clamp((StartDistance - SegmentStart) / Length, 0.f, 1.f)));
			}
			if (SegmentEnd >= EndDistance)
			{
				Out.Add(FMath::Lerp(From, To, FMath::Clamp((EndDistance - SegmentStart) / Length, 0.f, 1.f)));
				break;
			}
			Out.Add(To);
		}
		if (Out.Num() < 2)
		{
			Out.Reset();
		}
	}

	bool SampleAt(const TArray<FVector2f>& Points, float Distance, FVector2f& OutPoint, FVector2f& OutDirection)
	{
		if (Points.Num() < 2)
		{
			return false;
		}
		float Walked = 0.f;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector2f From = Points[Index - 1];
			const FVector2f To = Points[Index];
			const float Length = FVector2f::Distance(From, To);
			if (Length <= 0.f)
			{
				continue;
			}
			if (Walked + Length >= Distance || Index == Points.Num() - 1)
			{
				const float Alpha = FMath::Clamp((Distance - Walked) / Length, 0.f, 1.f);
				OutPoint = FMath::Lerp(From, To, Alpha);
				OutDirection = (To - From) / Length;
				return true;
			}
			Walked += Length;
		}
		OutPoint = Points.Last();
		OutDirection = (Points.Last() - Points[Points.Num() - 2]).GetSafeNormal();
		return true;
	}
}

// =====================================================================================================
// Presets
// =====================================================================================================

namespace WidgetPathPresets
{
	FName Locked() { static const FName Name(TEXT("Locked")); return Name; }
	FName Available() { static const FName Name(TEXT("Available")); return Name; }
	FName Unlocked() { static const FName Name(TEXT("Unlocked")); return Name; }

	FName LockedStyle() { static const FName Name(TEXT("Locked")); return Name; }
	FName AvailableStyle() { static const FName Name(TEXT("Available")); return Name; }
	FName UnlockedStyle() { static const FName Name(TEXT("Unlocked")); return Name; }
	FName HighlightedStyle() { static const FName Name(TEXT("Highlighted")); return Name; }

	namespace
	{
		FLinearColor SRGB(uint8 R, uint8 G, uint8 B, float Alpha = 1.f)
		{
			FLinearColor Color = FLinearColor::FromSRGBColor(FColor(R, G, B));
			Color.A = Alpha;
			return Color;
		}

		FWidgetPathLook Line(float Width, const FLinearColor& Color)
		{
			FWidgetPathLook Look;
			Look.Width = Width;
			Look.Color = Color;
			return Look;
		}

		FWidgetPathLook& Glow(FWidgetPathLook& Look, float Size, const FLinearColor& Color, float Breathe = 0.f)
		{
			Look.bGlow = true;
			Look.GlowSize = Size;
			Look.GlowColor = Color;
			Look.Breathe = Breathe;
			return Look;
		}

		FWidgetPathLook& Dashes(FWidgetPathLook& Look, float Length, float Gap, float Speed = 0.f)
		{
			Look.DashLength = Length;
			Look.DashGap = Gap;
			Look.DashSpeed = Speed;
			return Look;
		}

		FWidgetPathLook& Flow(FWidgetPathLook& Look, EWidgetPathFlow Kind, const FLinearColor& Color, float Speed, float Spacing, float Size = 5.f, float Length = 36.f)
		{
			Look.Flow = Kind;
			Look.FlowColor = Color;
			Look.FlowSpeed = Speed;
			Look.FlowSpacing = Spacing;
			Look.FlowSize = Size;
			Look.FlowLength = Length;
			return Look;
		}

		FWidgetPathLook& Outline(FWidgetPathLook& Look, float Width, const FLinearColor& Color)
		{
			Look.bOutline = true;
			Look.OutlineWidth = Width;
			Look.OutlineColor = Color;
			return Look;
		}

		FWidgetPathLook& Markers(FWidgetPathLook& Look, EWidgetPathMarker Start, EWidgetPathMarker End, float Size)
		{
			Look.StartMarker = Start;
			Look.EndMarker = End;
			Look.MarkerSize = Size;
			return Look;
		}

		void AddStyle(TArray<FWidgetPathStyle>& Styles, FName Name, const FWidgetPathLook& Look, int32 DrawOrder)
		{
			FWidgetPathStyle& Style = Styles.AddDefaulted_GetRef();
			Style.Name = Name;
			Style.Look = Look;
			Style.Look.DrawOrder = DrawOrder;
		}

		/** The rules every preset uses: a highlighted route first, then unlocked to unlocked, unlocked to available, the rest locked. */
		void AddDefaultRules(TArray<FWidgetPathStyleRule>& Rules)
		{
			Rules.Add(FWidgetPathStyleRule(NAME_None, NAME_None, HighlightedStyle(), EWidgetPathMatch::Yes));
			Rules.Add(FWidgetPathStyleRule(Unlocked(), Unlocked(), UnlockedStyle()));
			Rules.Add(FWidgetPathStyleRule(Unlocked(), Available(), AvailableStyle()));
			Rules.Add(FWidgetPathStyleRule(NAME_None, NAME_None, LockedStyle()));
		}
	}

	void Apply(EWidgetPathPreset Preset, FWidgetPathRouting& OutRouting, TArray<FWidgetPathStyle>& OutStyles, TArray<FWidgetPathStyleRule>& OutRules)
	{
		if (Preset == EWidgetPathPreset::Custom)
		{
			return;
		}

		OutRouting = FWidgetPathRouting();
		OutStyles.Reset();
		OutRules.Reset();
		AddDefaultRules(OutRules);

		const FLinearColor Gold = SRGB(255, 205, 96);
		const FLinearColor GoldGlow = SRGB(255, 160, 40, 0.5f);

		switch (Preset)
		{
		case EWidgetPathPreset::SkillTree:
		{
			OutRouting.Route = EWidgetPathRoute::Elbow;
			OutRouting.Corner = EWidgetPathCorner::Round;
			OutRouting.CornerRadius = 14.f;
			FWidgetPathLook LockedLook = Line(2.f, SRGB(140, 150, 170, 0.4f));
			Dashes(LockedLook, 4.f, 6.f);
			FWidgetPathLook ReadyLook = Line(3.f, SRGB(150, 205, 255, 0.85f));
			Glow(ReadyLook, 8.f, SRGB(80, 150, 255, 0.45f), 0.7f);
			FWidgetPathLook OpenLook = Line(3.5f, SRGB(200, 235, 255));
			Glow(OpenLook, 10.f, SRGB(90, 170, 255, 0.55f));
			Flow(OpenLook, EWidgetPathFlow::Pulse, FLinearColor(1.f, 1.f, 1.f, 0.8f), 90.f, 140.f, 5.f, 40.f);
			FWidgetPathLook LitLook = Line(4.f, Gold);
			Glow(LitLook, 10.f, GoldGlow);
			Dashes(LitLook, 10.f, 6.f, 40.f);
			AddStyle(OutStyles, LockedStyle(), LockedLook, 0);
			AddStyle(OutStyles, AvailableStyle(), ReadyLook, 1);
			AddStyle(OutStyles, UnlockedStyle(), OpenLook, 2);
			AddStyle(OutStyles, HighlightedStyle(), LitLook, 3);
			break;
		}

		case EWidgetPathPreset::Constellation:
		{
			OutRouting.Route = EWidgetPathRoute::Straight;
			OutRouting.StartGap = 6.f;
			OutRouting.EndGap = 6.f;
			FWidgetPathLook LockedLook = Line(1.5f, SRGB(180, 190, 230, 0.35f));
			Dashes(LockedLook, 0.5f, 5.f);
			FWidgetPathLook ReadyLook = Line(1.5f, SRGB(200, 220, 255, 0.75f));
			Glow(ReadyLook, 6.f, SRGB(140, 180, 255, 0.4f), 0.6f);
			FWidgetPathLook OpenLook = Line(2.f, FLinearColor::White);
			Glow(OpenLook, 10.f, SRGB(150, 200, 255, 0.6f));
			FWidgetPathLook LitLook = Line(2.5f, Gold);
			Glow(LitLook, 10.f, GoldGlow);
			AddStyle(OutStyles, LockedStyle(), LockedLook, 0);
			AddStyle(OutStyles, AvailableStyle(), ReadyLook, 1);
			AddStyle(OutStyles, UnlockedStyle(), OpenLook, 2);
			AddStyle(OutStyles, HighlightedStyle(), LitLook, 3);
			break;
		}

		case EWidgetPathPreset::Circuit:
		{
			OutRouting.Route = EWidgetPathRoute::Diagonal;
			OutRouting.Corner = EWidgetPathCorner::Chamfer;
			OutRouting.CornerRadius = 10.f;
			OutRouting.bSpreadPorts = true;
			FWidgetPathLook LockedLook = Line(2.f, SRGB(60, 90, 90, 0.85f));
			Outline(LockedLook, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
			FWidgetPathLook ReadyLook = Line(2.5f, SRGB(50, 230, 180, 0.8f));
			Outline(ReadyLook, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
			Glow(ReadyLook, 6.f, SRGB(30, 255, 180, 0.35f), 0.8f);
			FWidgetPathLook OpenLook = Line(3.f, SRGB(80, 255, 205));
			Outline(OpenLook, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
			Glow(OpenLook, 8.f, SRGB(30, 255, 180, 0.4f));
			Flow(OpenLook, EWidgetPathFlow::Dots, FLinearColor::White, 80.f, 60.f, 4.f);
			FWidgetPathLook LitLook = Line(3.f, Gold);
			Outline(LitLook, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.6f));
			Glow(LitLook, 8.f, GoldGlow);
			Flow(LitLook, EWidgetPathFlow::Dots, FLinearColor::White, 120.f, 40.f, 4.f);
			AddStyle(OutStyles, LockedStyle(), LockedLook, 0);
			AddStyle(OutStyles, AvailableStyle(), ReadyLook, 1);
			AddStyle(OutStyles, UnlockedStyle(), OpenLook, 2);
			AddStyle(OutStyles, HighlightedStyle(), LitLook, 3);
			break;
		}

		case EWidgetPathPreset::Flowchart:
		{
			OutRouting.Route = EWidgetPathRoute::Curve;
			OutRouting.Curvature = 0.5f;
			FWidgetPathLook LockedLook = Line(2.f, SRGB(150, 155, 165, 0.7f));
			Markers(LockedLook, EWidgetPathMarker::None, EWidgetPathMarker::Arrow, 9.f);
			FWidgetPathLook ReadyLook = Line(2.f, SRGB(100, 175, 255, 0.9f));
			Dashes(ReadyLook, 6.f, 4.f, 20.f);
			Markers(ReadyLook, EWidgetPathMarker::None, EWidgetPathMarker::Arrow, 9.f);
			FWidgetPathLook OpenLook = Line(2.5f, SRGB(80, 170, 255));
			Markers(OpenLook, EWidgetPathMarker::None, EWidgetPathMarker::Arrow, 10.f);
			FWidgetPathLook LitLook = Line(3.f, SRGB(255, 150, 50));
			Markers(LitLook, EWidgetPathMarker::None, EWidgetPathMarker::Arrow, 11.f);
			Flow(LitLook, EWidgetPathFlow::Arrows, FLinearColor(1.f, 1.f, 1.f, 0.9f), 60.f, 36.f, 5.f);
			AddStyle(OutStyles, LockedStyle(), LockedLook, 0);
			AddStyle(OutStyles, AvailableStyle(), ReadyLook, 1);
			AddStyle(OutStyles, UnlockedStyle(), OpenLook, 2);
			AddStyle(OutStyles, HighlightedStyle(), LitLook, 3);
			break;
		}

		case EWidgetPathPreset::Metro:
		{
			OutRouting.Route = EWidgetPathRoute::Diagonal;
			OutRouting.Corner = EWidgetPathCorner::Round;
			OutRouting.CornerRadius = 18.f;
			OutRouting.StartGap = 0.f;
			OutRouting.EndGap = 0.f;
			FWidgetPathLook LockedLook = Line(6.f, SRGB(170, 170, 175));
			Outline(LockedLook, 2.f, FLinearColor::White);
			FWidgetPathLook ReadyLook = Line(6.f, SRGB(240, 170, 60));
			Outline(ReadyLook, 2.f, FLinearColor::White);
			FWidgetPathLook OpenLook = Line(6.f, SRGB(220, 40, 70));
			Outline(OpenLook, 2.f, FLinearColor::White);
			FWidgetPathLook LitLook = Line(7.f, SRGB(255, 200, 0));
			Outline(LitLook, 2.f, FLinearColor::White);
			Flow(LitLook, EWidgetPathFlow::Dots, FLinearColor::White, 70.f, 50.f, 3.f);
			AddStyle(OutStyles, LockedStyle(), LockedLook, 0);
			AddStyle(OutStyles, AvailableStyle(), ReadyLook, 1);
			AddStyle(OutStyles, UnlockedStyle(), OpenLook, 2);
			AddStyle(OutStyles, HighlightedStyle(), LitLook, 3);
			break;
		}

		case EWidgetPathPreset::Organic:
		{
			OutRouting.Route = EWidgetPathRoute::Curve;
			OutRouting.Curvature = 0.7f;
			FWidgetPathLook LockedLook = Line(2.f, SRGB(130, 145, 130, 0.45f));
			FWidgetPathLook ReadyLook = Line(2.5f, SRGB(150, 230, 170, 0.8f));
			Glow(ReadyLook, 8.f, SRGB(80, 230, 140, 0.35f), 0.6f);
			FWidgetPathLook OpenLook = Line(3.5f, FLinearColor::White);
			OpenLook.Paint = EWidgetPathPaint::Gradient;
			OpenLook.GradientStops.Reset();
			OpenLook.GradientStops.Add(FWidgetShapeGradientStop(0.f, SRGB(110, 255, 160)));
			OpenLook.GradientStops.Add(FWidgetShapeGradientStop(1.f, SRGB(60, 170, 255)));
			Glow(OpenLook, 10.f, SRGB(80, 230, 170, 0.4f));
			FWidgetPathLook LitLook = Line(4.f, Gold);
			Glow(LitLook, 10.f, GoldGlow);
			Flow(LitLook, EWidgetPathFlow::Pulse, FLinearColor::White, 120.f, 120.f, 5.f, 50.f);
			AddStyle(OutStyles, LockedStyle(), LockedLook, 0);
			AddStyle(OutStyles, AvailableStyle(), ReadyLook, 1);
			AddStyle(OutStyles, UnlockedStyle(), OpenLook, 2);
			AddStyle(OutStyles, HighlightedStyle(), LitLook, 3);
			break;
		}

		case EWidgetPathPreset::Radial:
		{
			OutRouting.Route = EWidgetPathRoute::Orbit;
			OutRouting.NodeShape = EWidgetPathNodeShape::Ellipse;
			FWidgetPathLook LockedLook = Line(3.f, SRGB(120, 110, 95, 0.6f));
			Outline(LockedLook, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.5f));
			FWidgetPathLook ReadyLook = Line(3.f, SRGB(200, 180, 140, 0.9f));
			Outline(ReadyLook, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.5f));
			Glow(ReadyLook, 6.f, SRGB(255, 220, 160, 0.3f), 0.6f);
			FWidgetPathLook OpenLook = Line(4.f, SRGB(255, 225, 160));
			Outline(OpenLook, 1.f, FLinearColor(0.f, 0.f, 0.f, 0.5f));
			Glow(OpenLook, 10.f, SRGB(255, 190, 90, 0.5f));
			FWidgetPathLook LitLook = Line(4.f, SRGB(120, 210, 255));
			Glow(LitLook, 10.f, SRGB(60, 160, 255, 0.55f));
			Dashes(LitLook, 12.f, 6.f, 50.f);
			AddStyle(OutStyles, LockedStyle(), LockedLook, 0);
			AddStyle(OutStyles, AvailableStyle(), ReadyLook, 1);
			AddStyle(OutStyles, UnlockedStyle(), OpenLook, 2);
			AddStyle(OutStyles, HighlightedStyle(), LitLook, 3);
			break;
		}

		default:
			break;
		}
	}
}
