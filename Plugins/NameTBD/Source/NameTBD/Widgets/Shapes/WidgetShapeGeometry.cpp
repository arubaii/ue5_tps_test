// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetShapeGeometry.h"

#include "WidgetShapeTypes.h"

#include "Algo/BinarySearch.h"
#include "Algo/Reverse.h"

namespace WidgetShapeGeometryPrivate
{
	constexpr float Epsilon = 1e-5f;

	/** The furthest a miter of the fill's anti-aliasing fade reaches out of a sharp point, in fade widths. */
	constexpr float MaxFringeMiter = 4.f;

	/** At most this many cells across a Radial Gradient, however small the cell size asks for. */
	constexpr int32 MaxGradientCells = 128;

	float Cross(const FVector2f& A, const FVector2f& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	float Dot(const FVector2f& A, const FVector2f& B)
	{
		return A.X * B.X + A.Y * B.Y;
	}

	FVector2f Normalized(const FVector2f& V)
	{
		const float Length = V.Size();
		return Length > 1e-12f ? V / Length : FVector2f::ZeroVector;
	}

	/** The left side of a direction - with Slate's y down, the side a clockwise turn is made toward is the right. */
	FVector2f LeftOf(const FVector2f& Direction)
	{
		return FVector2f(-Direction.Y, Direction.X);
	}

	FVector2f Rotate(const FVector2f& V, float Angle)
	{
		const float C = FMath::Cos(Angle);
		const float S = FMath::Sin(Angle);
		return FVector2f(V.X * C - V.Y * S, V.X * S + V.Y * C);
	}

	float SignedArea(const TArray<FVector2f>& Points)
	{
		double Area = 0.0;
		const int32 Count = Points.Num();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2f& A = Points[Index];
			const FVector2f& B = Points[(Index + 1) % Count];
			Area += static_cast<double>(A.X) * B.Y - static_cast<double>(B.X) * A.Y;
		}
		return static_cast<float>(Area * 0.5);
	}

	bool PointInPolygon(const TArray<FVector2f>& Polygon, const FVector2f& Point)
	{
		bool bInside = false;
		const int32 Count = Polygon.Num();
		for (int32 Index = 0, Previous = Count - 1; Index < Count; Previous = Index++)
		{
			const FVector2f& A = Polygon[Index];
			const FVector2f& B = Polygon[Previous];
			if ((A.Y > Point.Y) != (B.Y > Point.Y))
			{
				const float X = A.X + (Point.Y - A.Y) * (B.X - A.X) / (B.Y - A.Y);
				if (Point.X < X)
				{
					bInside = !bInside;
				}
			}
		}
		return bInside;
	}

	/** Points with near-duplicates in a row (and a closing copy of the first) removed. */
	void RemoveDuplicates(TArray<FVector2f>& Points, bool bClosed, float MinDistance)
	{
		if (Points.Num() < 2)
		{
			return;
		}
		const float MinSquared = MinDistance * MinDistance;
		TArray<FVector2f> Kept;
		Kept.Reserve(Points.Num());
		for (const FVector2f& Point : Points)
		{
			if (Kept.Num() == 0 || (Point - Kept.Last()).SizeSquared() > MinSquared)
			{
				Kept.Add(Point);
			}
		}
		if (bClosed)
		{
			while (Kept.Num() > 1 && (Kept[0] - Kept.Last()).SizeSquared() <= MinSquared)
			{
				Kept.Pop(EAllowShrinking::No);
			}
		}
		Points = MoveTemp(Kept);
	}

	/**
	 * The offset of a corner's edge by one unit to the left of both its edges: (n0 + n1) / 2, scaled so it is one unit from
	 * each edge - a miter. Held to MaxLength, since a hairpin's miter runs off toward infinity.
	 */
	FVector2f MiterOf(const FVector2f& LeftIn, const FVector2f& LeftOut, float MaxLength)
	{
		const FVector2f Average = (LeftIn + LeftOut) * 0.5f;
		const float LengthSquared = Dot(Average, Average);
		if (LengthSquared < 1e-6f)
		{
			return LeftIn;
		}
		const float Scale = FMath::Min(1.f / LengthSquared, MaxLength / FMath::Sqrt(LengthSquared));
		return Average * Scale;
	}

	// ---------------------------------------------------------------------------------------------
	// Lines measured along their length
	// ---------------------------------------------------------------------------------------------

	struct FMeasured
	{
		TArray<FVector2f> Points;
		TArray<float> Distances;
		float Length = 0.f;

		void Build(const TArray<FVector2f>& InPoints, bool bClosed)
		{
			Points = InPoints;
			if (bClosed && Points.Num() > 0)
			{
				// A copy first: Add may grow the array and move the element it was handed.
				const FVector2f First = Points[0];
				Points.Add(First);
			}
			Distances.SetNumUninitialized(Points.Num());
			float Running = 0.f;
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				if (Index > 0)
				{
					Running += (Points[Index] - Points[Index - 1]).Size();
				}
				Distances[Index] = Running;
			}
			Length = Running;
		}

		FVector2f PointAt(float Distance) const
		{
			if (Points.Num() == 0)
			{
				return FVector2f::ZeroVector;
			}
			if (Distance <= 0.f)
			{
				return Points[0];
			}
			if (Distance >= Length)
			{
				return Points.Last();
			}
			const int32 Upper = FMath::Max(1, Algo::UpperBound(Distances, Distance));
			const int32 Lower = Upper - 1;
			const float Span = Distances[Upper] - Distances[Lower];
			const float Alpha = Span > 1e-12f ? (Distance - Distances[Lower]) / Span : 0.f;
			return FMath::Lerp(Points[Lower], Points[Upper], Alpha);
		}

		/** The points from From to To along the line (From < To, both within 0..Length), ends included. */
		void Extract(float From, float To, TArray<FVector2f>& Out) const
		{
			Out.Reset();
			Out.Add(PointAt(From));
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				if (Distances[Index] > From + Epsilon && Distances[Index] < To - Epsilon)
				{
					Out.Add(Points[Index]);
				}
			}
			Out.Add(PointAt(To));
		}
	};

	// ---------------------------------------------------------------------------------------------
	// Contours with rounded corners
	// ---------------------------------------------------------------------------------------------

	/**
	 * One contour flattened, its Straight corners rounded. Points: each vertex. Out / In: each vertex's handle points (equal
	 * to the vertex for none). Radii: each vertex's Corner Radius (0 for a curved vertex).
	 *
	 * Each segment is flattened on its own; a rounded corner then takes the same distance off the end of the segment coming
	 * in and the start of the one going out, and joins the two cut points with an arc - an exact circular arc where both
	 * segments are straight (a polygon's corners), a cubic close to one where either is curved. The distance is held to
	 * what fits: half a segment where the corner at its other end is rounded too, the whole of it otherwise.
	 */
	void FlattenContour(const TArray<FVector2f>& Points, const TArray<FVector2f>& Out, const TArray<FVector2f>& In,
		const TArray<float>& Radii, bool bClosed, float Tolerance, TArray<FVector2f>& Result)
	{
		Result.Reset();
		const int32 Count = Points.Num();
		if (Count == 0)
		{
			return;
		}
		if (Count == 1)
		{
			Result.Add(Points[0]);
			return;
		}

		const int32 SegmentCount = bClosed ? Count : Count - 1;
		TArray<FMeasured> Segments;
		TArray<bool> SegmentStraight;
		Segments.SetNum(SegmentCount);
		SegmentStraight.SetNum(SegmentCount);
		TArray<FVector2f> Flat;
		for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
		{
			const int32 From = Segment;
			const int32 To = (Segment + 1) % Count;
			Flat.Reset();
			const bool bStraight = (Out[From] - Points[From]).SizeSquared() < 1e-10f && (In[To] - Points[To]).SizeSquared() < 1e-10f;
			SegmentStraight[Segment] = bStraight;
			if (bStraight)
			{
				Flat.Add(Points[From]);
				Flat.Add(Points[To]);
			}
			else
			{
				WidgetShapeGeometry::AppendCubic(Flat, Points[From], Out[From], In[To], Points[To], Tolerance, /*bIncludeStart=*/true);
			}
			Segments[Segment].Build(Flat, false);
		}

		auto SegmentIn = [&](int32 Vertex) -> int32 { return bClosed ? (Vertex + Count - 1) % Count : Vertex - 1; };
		auto SegmentOut = [&](int32 Vertex) -> int32 { return bClosed ? Vertex : (Vertex < Count - 1 ? Vertex : INDEX_NONE); };

		// Which corners ask to be rounded, then how far each cuts back - which depends on its neighbors asking too.
		TArray<bool> bWantsRound;
		bWantsRound.SetNumZeroed(Count);
		for (int32 Vertex = 0; Vertex < Count; ++Vertex)
		{
			const int32 InSegment = SegmentIn(Vertex);
			const int32 OutSegment = SegmentOut(Vertex);
			bWantsRound[Vertex] = Radii.IsValidIndex(Vertex) && Radii[Vertex] > Epsilon && InSegment >= 0 && OutSegment != INDEX_NONE;
		}

		TArray<float> Cut;
		TArray<float> Radius;
		TArray<FVector2f> DirIn;
		TArray<FVector2f> DirOut;
		Cut.SetNumZeroed(Count);
		Radius.SetNumZeroed(Count);
		DirIn.SetNumZeroed(Count);
		DirOut.SetNumZeroed(Count);
		for (int32 Vertex = 0; Vertex < Count; ++Vertex)
		{
			if (!bWantsRound[Vertex])
			{
				continue;
			}
			const FMeasured& Incoming = Segments[SegmentIn(Vertex)];
			const FMeasured& Outgoing = Segments[SegmentOut(Vertex)];
			if (Incoming.Length < Epsilon || Outgoing.Length < Epsilon)
			{
				continue;
			}

			// The directions right at the corner: the last step of the segment coming in, the first going out.
			const FVector2f In0 = Normalized(Incoming.Points.Last() - Incoming.PointAt(Incoming.Length - FMath::Min(Incoming.Length, Tolerance * 2.f)));
			const FVector2f Out0 = Normalized(Outgoing.PointAt(FMath::Min(Outgoing.Length, Tolerance * 2.f)) - Outgoing.Points[0]);
			const float CosInterior = FMath::Clamp(Dot(-In0, Out0), -1.f, 1.f);
			const float Interior = FMath::Acos(CosInterior);
			if (Interior < 1e-3f || Interior > PI - 1e-3f)
			{
				// A hairpin has no room for an arc, and a straight run has no corner.
				continue;
			}

			const float HalfTan = FMath::Tan(Interior * 0.5f);
			const float Wanted = Radii[Vertex] / HalfTan;
			const int32 PreviousVertex = bClosed ? (Vertex + Count - 1) % Count : Vertex - 1;
			const int32 NextVertex = (Vertex + 1) % Count;
			const float RoomIn = Incoming.Length * (bWantsRound[PreviousVertex] ? 0.5f : 1.f);
			const float RoomOut = Outgoing.Length * (bWantsRound[NextVertex] ? 0.5f : 1.f);
			// Exactly the room when held to it, so two corners sharing a segment meet at one point rather than leaving a sliver
			// of an edge between them (which the fill's fade can't turn on cleanly).
			const float Distance = FMath::Min3(Wanted, RoomIn, RoomOut);
			if (Distance < Epsilon)
			{
				continue;
			}

			Cut[Vertex] = Distance;
			Radius[Vertex] = Distance * HalfTan;
			DirIn[Vertex] = In0;
			DirOut[Vertex] = Out0;
		}

		Result.Reserve(Count * 4);
		for (int32 Vertex = 0; Vertex < Count; ++Vertex)
		{
			if (Cut[Vertex] > 0.f)
			{
				const int32 InSegment = SegmentIn(Vertex);
				const int32 OutSegment = SegmentOut(Vertex);
				const FMeasured& Incoming = Segments[InSegment];
				const FMeasured& Outgoing = Segments[OutSegment];
				const FVector2f Start = Incoming.PointAt(Incoming.Length - Cut[Vertex]);
				const FVector2f End = Outgoing.PointAt(Cut[Vertex]);
				const float Turn = Cross(DirIn[Vertex], DirOut[Vertex]) >= 0.f ? 1.f : -1.f;

				if (SegmentStraight[InSegment] && SegmentStraight[OutSegment])
				{
					// Exact: the circle touching both edges at the cut points.
					const FVector2f Center = Start + LeftOf(DirIn[Vertex]) * Turn * Radius[Vertex];
					const float StartAngle = FMath::Atan2(Start.Y - Center.Y, Start.X - Center.X);
					float Sweep = FMath::Atan2(End.Y - Center.Y, End.X - Center.X) - StartAngle;
					while (Sweep > PI) { Sweep -= 2.f * PI; }
					while (Sweep < -PI) { Sweep += 2.f * PI; }
					const float Step = 2.f * FMath::Acos(FMath::Clamp(1.f - Tolerance / FMath::Max(Radius[Vertex], Tolerance), -1.f, 1.f));
					const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::Abs(Sweep) / FMath::Max(Step, 0.01f)), 1, 128);
					Result.Add(Start);
					for (int32 Index = 1; Index < Steps; ++Index)
					{
						const float Angle = StartAngle + Sweep * Index / Steps;
						Result.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius[Vertex]);
					}
					Result.Add(End);
				}
				else
				{
					// Along a curve there is no circle touching both sides exactly: a cubic shaped like one.
					const float TurnAngle = PI - FMath::Acos(FMath::Clamp(Dot(-DirIn[Vertex], DirOut[Vertex]), -1.f, 1.f));
					const float Handle = (4.f / 3.f) * FMath::Tan(TurnAngle * 0.25f) * Radius[Vertex];
					WidgetShapeGeometry::AppendCubic(Result, Start, Start + DirIn[Vertex] * Handle, End - DirOut[Vertex] * Handle, End, Tolerance, /*bIncludeStart=*/true);
				}
			}
			else
			{
				Result.Add(Points[Vertex]);
			}

			const int32 OutSegment = SegmentOut(Vertex);
			if (OutSegment == INDEX_NONE)
			{
				continue;
			}

			// The segment's points between this corner's cut and the next corner's.
			const FMeasured& Segment = Segments[OutSegment];
			const int32 NextVertex = (Vertex + 1) % Count;
			const float From = Cut[Vertex];
			const float To = Segment.Length - Cut[NextVertex];
			for (int32 Index = 1; Index < Segment.Points.Num() - 1; ++Index)
			{
				const float Distance = Segment.Distances[Index];
				if (Distance > From + Epsilon && Distance < To - Epsilon)
				{
					Result.Add(Segment.Points[Index]);
				}
			}
		}

		RemoveDuplicates(Result, bClosed, FMath::Max(Epsilon, Tolerance * 0.05f));
	}

	// ---------------------------------------------------------------------------------------------
	// Filling
	// ---------------------------------------------------------------------------------------------

	/** One closed ring of the fill. Depth: how many other rings it is inside - odd is a hole. */
	struct FRing
	{
		TArray<FVector2f> Points;
		float Area = 0.f;
		int32 Line = INDEX_NONE;
		int32 Depth = 0;
	};

	/** One edge of the fill, from A to B, with where the fade around it ends (AOut, BOut). */
	struct FFillEdge
	{
		FVector2f A;
		FVector2f B;
		FVector2f AOut;
		FVector2f BOut;
		float MinY = 0.f;
		float MaxY = 0.f;

		float AlphaAt(float Y) const
		{
			return (Y - A.Y) / (B.Y - A.Y);
		}

		/** The edge's point at height Y - exactly an end when Y is that end's, so neighbors meet without a seam. */
		FVector2f At(float Y) const
		{
			if (Y == A.Y) { return A; }
			if (Y == B.Y) { return B; }
			return FMath::Lerp(A, B, AlphaAt(Y));
		}

		FVector2f OutAt(float Y) const
		{
			if (Y == A.Y) { return AOut; }
			if (Y == B.Y) { return BOut; }
			return FMath::Lerp(AOut, BOut, AlphaAt(Y));
		}
	};

	/** The height where two edges cross, if they do (not where they only share an end). */
	bool CrossingHeight(const FFillEdge& First, const FFillEdge& Second, float& OutY)
	{
		const FVector2f R = First.B - First.A;
		const FVector2f S = Second.B - Second.A;
		const float Denominator = Cross(R, S);
		if (FMath::Abs(Denominator) < 1e-12f)
		{
			return false;
		}
		const FVector2f Between = Second.A - First.A;
		const float T = Cross(Between, S) / Denominator;
		const float U = Cross(Between, R) / Denominator;
		constexpr float Inside = 1e-5f;
		if (T <= Inside || T >= 1.f - Inside || U <= Inside || U >= 1.f - Inside)
		{
			return false;
		}
		OutY = First.A.Y + R.Y * T;
		return true;
	}

	/** Rings from the outline's lines, with which are holes worked out by how many others each is inside. */
	void MakeRings(const FWidgetShapeOutline& Outline, TArray<FRing>& OutRings, float MinDistance)
	{
		OutRings.Reset();
		for (int32 Line = 0; Line < Outline.Lines.Num(); ++Line)
		{
			FRing Ring;
			Ring.Points = Outline.Lines[Line].Points;
			RemoveDuplicates(Ring.Points, true, MinDistance);
			if (Ring.Points.Num() < 3)
			{
				continue;
			}
			// Kept unless it has no extent: a figure eight's two halves cancel out to no area but still fill.
			Ring.Area = SignedArea(Ring.Points);
			FBox2f Extent(ForceInit);
			for (const FVector2f& Point : Ring.Points)
			{
				Extent += Point;
			}
			if (Extent.GetSize().X < 1e-4f || Extent.GetSize().Y < 1e-4f)
			{
				continue;
			}
			Ring.Line = Line;
			OutRings.Add(MoveTemp(Ring));
		}

		for (int32 Index = 0; Index < OutRings.Num(); ++Index)
		{
			FRing& Ring = OutRings[Index];
			// A point that can't be on another ring's edge by chance: the middle of the ring's first edge, nudged inside it.
			const FVector2f EdgeMiddle = (Ring.Points[0] + Ring.Points[1]) * 0.5f;
			const FVector2f Inward = LeftOf(Normalized(Ring.Points[1] - Ring.Points[0])) * (Ring.Area > 0.f ? 1.f : -1.f);
			const FVector2f Probe = EdgeMiddle + Inward * 1e-3f;

			for (int32 Other = 0; Other < OutRings.Num(); ++Other)
			{
				if (Other != Index && PointInPolygon(OutRings[Other].Points, Probe))
				{
					++Ring.Depth;
				}
			}
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Stroke
	// ---------------------------------------------------------------------------------------------

	/** One side of a stroke's cross-section: where it starts, which way it goes (a miter-scaled unit) and how far. */
	struct FSide
	{
		FVector2f Base;
		FVector2f Direction;
	};

	struct FSection
	{
		FSide Left;
		FSide Right;
		bool bFaded = false;

		/** How far along the outline the section is, in local units - for an image laid along it. */
		float Along = 0.f;
	};

	struct FStrokeBuilder
	{
		FWidgetShapeMesh& Mesh;
		float LeftExtent = 1.f;
		float RightExtent = 1.f;
		float HalfFade = 0.5f;
		float Opacity = 1.f;
		float Tolerance = 0.2f;
		EWidgetShapeLineJoin Join = EWidgetShapeLineJoin::Miter;
		float MiterLimit = 4.f;
		EWidgetShapeLineCap Cap = EWidgetShapeLineCap::Butt;

		// Where the sections being added lie on the outline: the distance the next section is at, the length of the
		// contour, and which side of the line its inside is (what V = 0 means). Only an image laid along the outline reads
		// them; nothing about the mesh's shape depends on them.
		float CurrentAlong = 0.f;
		float OutlineLength = 0.f;
		bool bInsideOnLeft = true;

		/** A closed run's length, so its last quad can run on to a copy of its first section rather than back across it. */
		float ClosedLength = 0.f;

		TArray<FSection> Sections;

		explicit FStrokeBuilder(FWidgetShapeMesh& InMesh) : Mesh(InMesh) {}

		void AddSection(const FVector2f& Point, const FVector2f& LeftDirection, const FVector2f& RightDirection, bool bFaded = false)
		{
			FSection Section;
			Section.Left = { Point, LeftDirection };
			Section.Right = { Point, RightDirection };
			Section.bFaded = bFaded;
			Section.Along = CurrentAlong;
			Sections.Add(Section);
		}

		/** Across the stroke for a point Offset to the left of the line (negative: to its right): 0 on its inside edge, 1 on its outside. */
		float AcrossAt(float Offset) const
		{
			const float Width = FMath::Max(LeftExtent + RightExtent, KINDA_SMALL_NUMBER);
			const float FromLeft = (LeftExtent - Offset) / Width;
			return bInsideOnLeft ? FromLeft : 1.f - FromLeft;
		}

		int32 RoundSteps(float Angle, float Radius) const
		{
			const float Step = 2.f * FMath::Acos(FMath::Clamp(1.f - Tolerance / FMath::Max(Radius, Tolerance), -1.f, 1.f));
			return FMath::Clamp(FMath::CeilToInt(FMath::Abs(Angle) / FMath::Max(Step, 0.02f)), 1, 64);
		}

		/** Sections for a corner turning from In to Out at Point. */
		void AddJoin(const FVector2f& Point, const FVector2f& In, const FVector2f& Out, float LengthIn, float LengthOut)
		{
			const FVector2f LeftIn = LeftOf(In);
			const FVector2f LeftOut = LeftOf(Out);
			const float Turn = Cross(In, Out);
			const float Along = Dot(In, Out);
			if (FMath::Abs(Turn) < 1e-6f && Along > 0.f)
			{
				AddSection(Point, LeftIn, -LeftIn);
				return;
			}

			const FVector2f Miter = MiterOf(LeftIn, LeftOut, 1e4f);
			const float MiterLength = Miter.Size();

			// Turning left the corner's inside is the left side; turning right, the right.
			const bool bLeftInner = Turn > 0.f;
			const float InnerExtent = bLeftInner ? LeftExtent : RightExtent;
			const float OuterExtent = bLeftInner ? RightExtent : LeftExtent;

			// The inside meets at the miter point, unless the corner is sharp enough for that to lie past either segment's far
			// end - then it is left as two points, one per edge, and the edges overlap a little there rather than folding over.
			// A gentle bend (a curve's many short steps) always meets at the miter, however wide the stroke.
			const float InnerReach = FMath::Max(InnerExtent + HalfFade, KINDA_SMALL_NUMBER);
			const bool bInnerMiter = MiterLength <= FMath::Max(1.01f, FMath::Min(LengthIn, LengthOut) / InnerReach);
			const float InnerSign = bLeftInner ? 1.f : -1.f;
			auto InnerOf = [InnerSign](const FVector2f& LeftNormal) { return LeftNormal * InnerSign; };
			auto OuterOf = [InnerSign](const FVector2f& LeftNormal) { return LeftNormal * -InnerSign; };

			auto Add = [&](const FVector2f& InnerDirection, const FVector2f& OuterDirection)
			{
				if (bLeftInner)
				{
					AddSection(Point, InnerDirection, OuterDirection);
				}
				else
				{
					AddSection(Point, OuterDirection, InnerDirection);
				}
			};

			const bool bMiterFits = Join == EWidgetShapeLineJoin::Miter && MiterLength <= MiterLimit;
			if (bMiterFits && bInnerMiter)
			{
				Add(InnerOf(Miter), OuterOf(Miter));
				return;
			}

			const FVector2f InnerA = bInnerMiter ? InnerOf(Miter) : InnerOf(LeftIn);
			const FVector2f InnerB = bInnerMiter ? InnerOf(Miter) : InnerOf(LeftOut);

			if (bMiterFits)
			{
				// The outside is still mitered; only the inside is split.
				Add(InnerA, OuterOf(Miter));
				Add(InnerB, OuterOf(Miter));
				return;
			}

			if (Join == EWidgetShapeLineJoin::Round)
			{
				const FVector2f From = OuterOf(LeftIn);
				const FVector2f To = OuterOf(LeftOut);
				float Sweep = FMath::Atan2(Cross(From, To), Dot(From, To));
				const int32 Steps = RoundSteps(Sweep, OuterExtent + HalfFade);
				for (int32 Step = 0; Step <= Steps; ++Step)
				{
					const float Alpha = static_cast<float>(Step) / Steps;
					Add(Alpha < 0.5f ? InnerA : InnerB, Rotate(From, Sweep * Alpha));
				}
				return;
			}

			// Bevel, or a miter past its limit.
			Add(InnerA, OuterOf(LeftIn));
			Add(InnerB, OuterOf(LeftOut));
		}

		/** A section that is only fade - the end of a flat cap's anti-aliasing. */
		void AddFadedEnd(const FVector2f& Point, const FVector2f& Direction)
		{
			const FVector2f Left = LeftOf(Direction);
			AddSection(Point, Left, -Left, /*bFaded=*/true);
		}

		uint32 SideVertex(const FSection& Section, bool bLeft, float Extent, float Coverage, float AlongOffset = 0.f)
		{
			const FSide& Side = bLeft ? Section.Left : Section.Right;
			const FVector2f UV(Section.Along + AlongOffset, AcrossAt(bLeft ? Extent : -Extent));
			return Mesh.AddVertex(Side.Base + Side.Direction * Extent, Coverage, UV, OutlineLength);
		}

		/** Turns the sections into the strip: fade, solid, fade across, joined section to section. */
		void EmitStrip(bool bClosed)
		{
			const int32 Count = Sections.Num();
			if (Count < 2)
			{
				Sections.Reset();
				return;
			}

			auto EmitSection = [this](const FSection& Section, uint32 (&Out)[4], float AlongOffset)
			{
				const float Solid = Section.bFaded ? 0.f : Opacity;
				// A side with no extent (an Inside or Outside stroke's edge on the outline) is solid right up to the outline and
				// fades outward from it, rather than starting its fade across on the other side, where at a sharp corner it would
				// swing round past the corner and overlap itself. The fill's own fade covers that edge anyway.
				Out[0] = SideVertex(Section, true, FMath::Max(LeftExtent + HalfFade, HalfFade), 0.f, AlongOffset);
				Out[1] = SideVertex(Section, true, FMath::Max(LeftExtent - HalfFade, 0.f), Solid, AlongOffset);
				Out[2] = SideVertex(Section, false, FMath::Max(RightExtent - HalfFade, 0.f), Solid, AlongOffset);
				Out[3] = SideVertex(Section, false, FMath::Max(RightExtent + HalfFade, HalfFade), 0.f, AlongOffset);
			};

			uint32 Previous[4] = { 0, 0, 0, 0 };
			for (int32 Index = 0; Index < Count; ++Index)
			{
				uint32 Current[4];
				EmitSection(Sections[Index], Current, 0.f);
				if (Index > 0)
				{
					EmitBetween(Previous, Current);
				}
				FMemory::Memcpy(Previous, Current, sizeof(Current));
			}
			if (bClosed)
			{
				// Back to the start through a copy of the first section, a whole outline further along: the same place, so no
				// seam shows, but an image laid along the outline runs on into its next repeat instead of back across the
				// last edge.
				uint32 First[4];
				EmitSection(Sections[0], First, ClosedLength);
				EmitBetween(Previous, First);
			}
			Sections.Reset();
		}

		void EmitBetween(const uint32 (&A)[4], const uint32 (&B)[4])
		{
			Mesh.AddQuad(A[0], A[1], B[1], B[0]);
			Mesh.AddQuad(A[1], A[2], B[2], B[1]);
			Mesh.AddQuad(A[2], A[3], B[3], B[2]);
		}

		/**
		 * A round cap: a half disc behind Point, facing back along Direction, with its fade around it. Along: where Point is
		 * on the outline; AlongSign: +1 when Direction runs the outline's way (the start cap), -1 against it (the end cap).
		 */
		void EmitRoundCap(const FVector2f& Point, const FVector2f& Direction, float Radius, float Along, float AlongSign)
		{
			const FVector2f Left = LeftOf(Direction);
			const int32 Steps = RoundSteps(PI, Radius + HalfFade);
			auto CapVertex = [&](const FVector2f& Out, float Distance, float Coverage)
			{
				// Out in Direction's frame: its left is the outline's left at the start cap, its right at the end cap.
				const float Sideways = Dot(Out, Left) * Distance * AlongSign;
				const float Behind = -Dot(Out, Direction) * Distance * AlongSign;
				return Mesh.AddVertex(Point + Out * Distance, Coverage, FVector2f(Along - Behind, AcrossAt(Sideways)), OutlineLength);
			};
			const uint32 Middle = CapVertex(FVector2f::ZeroVector, 0.f, Opacity);
			uint32 PreviousSolid = 0;
			uint32 PreviousFade = 0;
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				// From the left, round behind the point, to the right.
				const float Angle = PI * Step / Steps;
				const FVector2f Out = Left * FMath::Cos(Angle) - Direction * FMath::Sin(Angle);
				const uint32 Solid = CapVertex(Out, Radius - HalfFade, Opacity);
				const uint32 Fade = CapVertex(Out, Radius + HalfFade, 0.f);
				if (Step > 0)
				{
					Mesh.AddTriangle(Middle, PreviousSolid, Solid);
					Mesh.AddQuad(PreviousSolid, PreviousFade, Fade, Solid);
				}
				PreviousSolid = Solid;
				PreviousFade = Fade;
			}
		}

		/**
		 * The stroke along one run of points. bClosed joins it back on itself; an open run gets the Cap at each end.
		 * FillSide: +1 / -1 for a closed run that bounds the fill (left / right), which Inside and Outside lean toward.
		 */
		void Stroke(TArray<FVector2f> Points, bool bClosed, float StartAlong)
		{
			RemoveDuplicates(Points, bClosed, 1e-4f);
			if (bClosed && Points.Num() < 3)
			{
				bClosed = false;
			}
			const int32 Count = Points.Num();
			if (Count < 2)
			{
				return;
			}

			TArray<FVector2f> Directions;
			TArray<float> Lengths;
			const int32 SegmentCount = bClosed ? Count : Count - 1;
			Directions.SetNumUninitialized(SegmentCount);
			Lengths.SetNumUninitialized(SegmentCount);
			for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
			{
				const FVector2f Delta = Points[(Segment + 1) % Count] - Points[Segment];
				Lengths[Segment] = Delta.Size();
				Directions[Segment] = Normalized(Delta);
			}

			// Where each point is along the outline.
			TArray<float> Alongs;
			Alongs.SetNumUninitialized(Count);
			Alongs[0] = StartAlong;
			for (int32 Index = 1; Index < Count; ++Index)
			{
				Alongs[Index] = Alongs[Index - 1] + Lengths[Index - 1];
			}

			if (bClosed)
			{
				ClosedLength = Alongs.Last() + Lengths.Last() - StartAlong;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const int32 In = (Index + Count - 1) % Count;
					CurrentAlong = Alongs[Index];
					AddJoin(Points[Index], Directions[In], Directions[Index], Lengths[In], Lengths[Index]);
				}
				EmitStrip(true);
				return;
			}

			const float HalfWidth = (LeftExtent + RightExtent) * 0.5f;
			const FVector2f StartDirection = Directions[0];
			const FVector2f EndDirection = Directions.Last();
			FVector2f Start = Points[0];
			FVector2f End = Points.Last();

			// Flat caps fade across their end the way the sides fade: the line stops half a fade short, and a fully faded
			// section half a fade past. Square caps do the same half a width further out.
			const float CapReach = Cap == EWidgetShapeLineCap::Square ? HalfWidth : 0.f;
			const float StartReach = Cap != EWidgetShapeLineCap::Round ? CapReach - HalfFade : 0.f;
			if (Cap != EWidgetShapeLineCap::Round)
			{
				CurrentAlong = Alongs[0] - (CapReach + HalfFade);
				AddFadedEnd(Start - StartDirection * (CapReach + HalfFade), StartDirection);
				Start = Start - StartDirection * (CapReach - HalfFade);
			}
			CurrentAlong = Alongs[0] - StartReach;
			AddSection(Start, LeftOf(StartDirection), -LeftOf(StartDirection));
			for (int32 Index = 1; Index < Count - 1; ++Index)
			{
				CurrentAlong = Alongs[Index];
				AddJoin(Points[Index], Directions[Index - 1], Directions[Index], Lengths[Index - 1], Lengths[Index]);
			}
			if (Cap != EWidgetShapeLineCap::Round)
			{
				End = End + EndDirection * (CapReach - HalfFade);
			}
			CurrentAlong = Alongs.Last() + StartReach;
			AddSection(End, LeftOf(EndDirection), -LeftOf(EndDirection));
			if (Cap != EWidgetShapeLineCap::Round)
			{
				CurrentAlong = Alongs.Last() + CapReach + HalfFade;
				AddFadedEnd(Points.Last() + EndDirection * (CapReach + HalfFade), EndDirection);
			}
			EmitStrip(false);

			if (Cap == EWidgetShapeLineCap::Round)
			{
				EmitRoundCap(Points[0], StartDirection, HalfWidth, Alongs[0], 1.f);
				EmitRoundCap(Points.Last(), -EndDirection, HalfWidth, Alongs.Last(), -1.f);
			}
		}
	};

	/**
	 * The parts of a run left to draw by Trim Path: none, the whole of it (closed stays closed), or one or two open pieces -
	 * two where the drawn part wraps past a closed run's start.
	 */
	void TrimRun(const TArray<FVector2f>& Points, bool bClosed, const FWidgetShapeTrim& Trim, TArray<TArray<FVector2f>>& OutPieces, TArray<float>& OutStarts, bool& bOutWhole)
	{
		OutPieces.Reset();
		OutStarts.Reset();
		bOutWhole = false;

		const float Low = FMath::Min(Trim.Start, Trim.End);
		const float High = FMath::Max(Trim.Start, Trim.End);
		if (High - Low >= 1.f - 1e-6f)
		{
			bOutWhole = true;
			return;
		}
		if (High - Low <= 1e-6f)
		{
			return;
		}

		FMeasured Measured;
		Measured.Build(Points, bClosed);
		if (Measured.Length <= Epsilon)
		{
			return;
		}

		// Offset turns the drawn part round; past the ends it wraps, on open runs too (as After Effects does).
		float From = FMath::Frac(Low + Trim.Offset);
		const float Span = High - Low;
		float To = From + Span;
		auto AddPiece = [&](float A, float B)
		{
			if (B - A <= 1e-6f)
			{
				return;
			}
			TArray<FVector2f> Piece;
			Measured.Extract(A * Measured.Length, B * Measured.Length, Piece);
			OutPieces.Add(MoveTemp(Piece));
			OutStarts.Add(A * Measured.Length);
		};

		if (To <= 1.f)
		{
			AddPiece(From, To);
		}
		else if (bClosed)
		{
			// One piece running on across the start: the run's end joined to its beginning.
			TArray<FVector2f> First;
			TArray<FVector2f> Second;
			Measured.Extract(From * Measured.Length, Measured.Length, First);
			Measured.Extract(0.f, (To - 1.f) * Measured.Length, Second);
			First.Append(Second);
			OutPieces.Add(MoveTemp(First));
			OutStarts.Add(From * Measured.Length);
		}
		else
		{
			AddPiece(From, 1.f);
			AddPiece(0.f, To - 1.f);
		}
	}

	/** Cuts a run into dashes. Each dash an open run; OutStarts: where each starts along the run. */
	void DashRun(const TArray<FVector2f>& Points, bool bClosed, float DashLength, float Gap, float Offset, TArray<TArray<FVector2f>>& OutDashes, TArray<float>& OutStarts)
	{
		OutDashes.Reset();
		OutStarts.Reset();
		FMeasured Measured;
		Measured.Build(Points, bClosed);
		const float Period = DashLength + Gap;
		if (Measured.Length <= Epsilon || Period <= Epsilon)
		{
			return;
		}

		// At most this many dashes, so a dash length typed as 0.001 can't make a million of them.
		const int32 MaxDashes = 4096;
		const float Phase = FMath::Fmod(Offset, Period);
		float Start = -Phase;
		if (Start > 0.f)
		{
			Start -= Period;
		}
		for (int32 Count = 0; Start < Measured.Length && Count < MaxDashes; Start += Period, ++Count)
		{
			const float A = FMath::Max(Start, 0.f);
			const float B = FMath::Min(Start + DashLength, Measured.Length);
			if (B - A > Epsilon)
			{
				TArray<FVector2f> Dash;
				Measured.Extract(A, B, Dash);
				OutDashes.Add(MoveTemp(Dash));
				OutStarts.Add(A);
			}
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Gradient cuts
	// ---------------------------------------------------------------------------------------------

	/**
	 * Cuts every triangle the line Value(p) = Level crosses into pieces that don't cross it. Value must be linear in
	 * position (it is: a gradient's position, or a grid line). Where an edge is cut, the new vertex is shared by both
	 * triangles either side of it, so no crack opens.
	 */
	template <typename TValue>
	void CutAlong(FWidgetShapeMesh& Mesh, float Level, TValue Value)
	{
		TArray<float> Values;
		Values.SetNumUninitialized(Mesh.Vertices.Num());
		for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
		{
			Values[Index] = Value(Mesh.Vertices[Index].Position) - Level;
		}

		TMap<uint64, uint32> EdgeCuts;
		auto CutEdge = [&](uint32 A, uint32 B) -> uint32
		{
			const uint64 Key = A < B ? (static_cast<uint64>(A) << 32 | B) : (static_cast<uint64>(B) << 32 | A);
			if (const uint32* Found = EdgeCuts.Find(Key))
			{
				return *Found;
			}
			const float VA = Values[A];
			const float VB = Values[B];
			const float Alpha = VA / (VA - VB);
			const FWidgetShapeMeshVertex& From = Mesh.Vertices[A];
			const FWidgetShapeMeshVertex& To = Mesh.Vertices[B];
			const uint32 New = Mesh.AddVertex(FMath::Lerp(From.Position, To.Position, Alpha), FMath::Lerp(From.Coverage, To.Coverage, Alpha),
				FMath::Lerp(From.Along, To.Along, Alpha), FMath::Lerp(From.OutlineLength, To.OutlineLength, Alpha));
			Values.Add(0.f);
			EdgeCuts.Add(Key, New);
			return New;
		};

		TArray<uint32> OldIndices = MoveTemp(Mesh.Indices);
		Mesh.Indices.Reset();
		Mesh.Indices.Reserve(OldIndices.Num() + OldIndices.Num() / 2);
		for (int32 Triangle = 0; Triangle + 2 < OldIndices.Num(); Triangle += 3)
		{
			const uint32 T[3] = { OldIndices[Triangle], OldIndices[Triangle + 1], OldIndices[Triangle + 2] };
			int32 Above = 0;
			int32 Below = 0;
			for (const uint32 Index : T)
			{
				Above += Values[Index] > 1e-6f ? 1 : 0;
				Below += Values[Index] < -1e-6f ? 1 : 0;
			}
			if (Above == 0 || Below == 0)
			{
				Mesh.AddTriangle(T[0], T[1], T[2]);
				continue;
			}

			// Rotate so the lone vertex - the one on its own side of the line - comes first.
			int32 Lone = 0;
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const float V = Values[T[Corner]];
				const bool bLonely = (Above == 1 && V > 1e-6f) || (Above != 1 && Below == 1 && V < -1e-6f);
				if (bLonely)
				{
					Lone = Corner;
					break;
				}
			}
			const uint32 A = T[Lone];
			const uint32 B = T[(Lone + 1) % 3];
			const uint32 C = T[(Lone + 2) % 3];
			const float VB = Values[B];
			const float VC = Values[C];

			if (FMath::Abs(VB) <= 1e-6f || FMath::Abs(VC) <= 1e-6f)
			{
				// The line runs through B or C: one cut, two triangles.
				if (FMath::Abs(VB) <= 1e-6f)
				{
					const uint32 AC = CutEdge(A, C);
					Mesh.AddTriangle(A, B, AC);
					Mesh.AddTriangle(B, C, AC);
				}
				else
				{
					const uint32 AB = CutEdge(A, B);
					Mesh.AddTriangle(A, AB, C);
					Mesh.AddTriangle(AB, B, C);
				}
				continue;
			}

			const uint32 AB = CutEdge(A, B);
			const uint32 AC = CutEdge(A, C);
			Mesh.AddTriangle(A, AB, AC);
			Mesh.AddTriangle(AB, B, C);
			Mesh.AddTriangle(AB, C, AC);
		}
	}

}

// =====================================================================================================
// Public
// =====================================================================================================

FWidgetShapeFrame FWidgetShapeFrame::Make(const FVector2f& LocalSize, bool bKeepAspect, float Aspect, const FVector2f& Scale, float RotationDegrees)
{
	FWidgetShapeFrame Frame;
	Frame.Center = LocalSize * 0.5f;

	FVector2f Box = LocalSize;
	if (bKeepAspect && Aspect > KINDA_SMALL_NUMBER && LocalSize.X > 0.f && LocalSize.Y > 0.f)
	{
		// As large as fits at Aspect.
		if (LocalSize.X / LocalSize.Y > Aspect)
		{
			Box = FVector2f(LocalSize.Y * Aspect, LocalSize.Y);
		}
		else
		{
			Box = FVector2f(LocalSize.X, LocalSize.X / Aspect);
		}
	}
	Frame.Size = Box * Scale;

	const float Radians = FMath::DegreesToRadians(RotationDegrees);
	Frame.Cos = FMath::Cos(Radians);
	Frame.Sin = FMath::Sin(Radians);
	return Frame;
}

namespace WidgetShapeGeometry
{
	float GetTolerance(float PixelScale)
	{
		return 0.05f / FMath::Max(PixelScale, 0.01f);
	}

	void AppendCubic(TArray<FVector2f>& Out, const FVector2f& P0, const FVector2f& P1, const FVector2f& P2, const FVector2f& P3, float Tolerance, bool bIncludeStart)
	{
		// Wang's formula: the fewest even steps that keep every point of the curve within Tolerance of the steps' chords.
		const FVector2f D1 = P0 - P1 * 2.f + P2;
		const FVector2f D2 = P1 - P2 * 2.f + P3;
		const float Bend = FMath::Sqrt(FMath::Max(D1.SizeSquared(), D2.SizeSquared()));
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::Sqrt(0.75f * Bend / FMath::Max(Tolerance, 1e-4f))), 1, 256);
		if (bIncludeStart)
		{
			Out.Add(P0);
		}
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			const float T = static_cast<float>(Step) / Steps;
			const float U = 1.f - T;
			Out.Add(P0 * (U * U * U) + P1 * (3.f * U * U * T) + P2 * (3.f * U * T * T) + P3 * (T * T * T));
		}
	}

	void AppendEllipseArc(TArray<FVector2f>& Out, const FWidgetShapeFrame& Frame, const FVector2f& Center, const FVector2f& Radii, float StartAngle, float Sweep, float Tolerance, bool bIncludeStart)
	{
		// As many steps as the largest radius on screen needs, evenly in angle.
		const FVector2f LocalRadii = Radii * FVector2f(FMath::Abs(Frame.Size.X), FMath::Abs(Frame.Size.Y));
		const float Largest = FMath::Max3(LocalRadii.X, LocalRadii.Y, Tolerance);
		const float Step = 2.f * FMath::Acos(FMath::Clamp(1.f - Tolerance / Largest, -1.f, 1.f));
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::Abs(Sweep) / FMath::Max(Step, 0.005f)), 1, 1024);
		for (int32 Index = bIncludeStart ? 0 : 1; Index <= Steps; ++Index)
		{
			const float Angle = StartAngle + Sweep * Index / Steps;
			const FVector2f BoxPoint = Center + FVector2f(FMath::Sin(Angle) * Radii.X, -FMath::Cos(Angle) * Radii.Y);
			Out.Add(Frame.ToLocal(BoxPoint));
		}
	}

	void FlattenPath(const FWidgetShapePath& Path, const FWidgetShapeFrame& Frame, float Tolerance, FWidgetShapeOutline& Out)
	{
		using namespace WidgetShapeGeometryPrivate;

		TArray<FVector2f> Points;
		TArray<FVector2f> OutHandles;
		TArray<FVector2f> InHandles;
		TArray<float> Radii;
		for (const FWidgetShapeContour& Contour : Path.Contours)
		{
			const int32 Count = Contour.Vertices.Num();
			if (Count < 2)
			{
				continue;
			}
			Points.Reset();
			OutHandles.Reset();
			InHandles.Reset();
			Radii.Reset();
			for (const FWidgetShapeVertex& Vertex : Contour.Vertices)
			{
				const FVector2f Position(static_cast<float>(Vertex.Position.X), static_cast<float>(Vertex.Position.Y));
				const FVector2f OutHandle(static_cast<float>(Vertex.GetOut().X), static_cast<float>(Vertex.GetOut().Y));
				const FVector2f InHandle(static_cast<float>(Vertex.GetIn().X), static_cast<float>(Vertex.GetIn().Y));
				const FVector2f Local = Frame.ToLocal(Position);
				Points.Add(Local);
				OutHandles.Add(Local + Frame.VectorToLocal(OutHandle));
				InHandles.Add(Local + Frame.VectorToLocal(InHandle));
				Radii.Add(Vertex.HasHandles() ? 0.f : Vertex.CornerRadius);
			}

			FWidgetShapeLine& Line = Out.Lines.AddDefaulted_GetRef();
			Line.bClosed = Contour.bClosed;
			FlattenContour(Points, OutHandles, InHandles, Radii, Contour.bClosed, Tolerance, Line.Points);
			if (Line.Points.Num() < 2)
			{
				Out.Lines.Pop(EAllowShrinking::No);
			}
		}
	}

	void AppendRoundedPolygon(FWidgetShapeOutline& Out, const TArray<FVector2f>& Corners, const TArray<float>& CornerRadii, float Tolerance)
	{
		using namespace WidgetShapeGeometryPrivate;

		if (Corners.Num() < 3)
		{
			return;
		}
		FWidgetShapeLine& Line = Out.Lines.AddDefaulted_GetRef();
		Line.bClosed = true;
		FlattenContour(Corners, Corners, Corners, CornerRadii, true, Tolerance, Line.Points);
	}

	void ClassifyLines(const FWidgetShapeOutline& Outline, TArray<int8>& OutFillOnLeft)
	{
		using namespace WidgetShapeGeometryPrivate;

		OutFillOnLeft.Init(0, Outline.Lines.Num());
		TArray<FRing> Rings;
		MakeRings(Outline, Rings, 1e-4f);
		for (const FRing& Ring : Rings)
		{
			// An outer ring's inside is filled; a hole's outside. Positive area has its inside on the left.
			const bool bHole = (Ring.Depth % 2) == 1;
			const bool bInsideOnLeft = Ring.Area > 0.f;
			OutFillOnLeft[Ring.Line] = (bInsideOnLeft != bHole) ? 1 : -1;
		}
	}

	void BuildFill(const FWidgetShapeOutline& Outline, float AntiAliasWidth, FWidgetShapeMesh& Out, TArray<int8>& OutFillOnLeft)
	{
		using namespace WidgetShapeGeometryPrivate;

		// Points closer than a tenth of the fade are one point: an edge that short has no direction worth following.
		OutFillOnLeft.Init(0, Outline.Lines.Num());
		TArray<FRing> Rings;
		MakeRings(Outline, Rings, FMath::Max(1e-4f, AntiAliasWidth * 0.1f));

		// Every ring's points pulled in by half the fade toward the fill (the fill's own edge) and pushed out by half (where
		// the fade ends), so the fade is centered on the true outline. The filled side of a ring is its inside for an outer
		// ring and its outside for a hole; positive area has the inside on the left.
		const float HalfFade = AntiAliasWidth * 0.5f;
		TArray<FFillEdge> Edges;
		for (const FRing& Ring : Rings)
		{
			const bool bHole = (Ring.Depth % 2) == 1;
			const float FillSide = ((Ring.Area > 0.f) != bHole) ? 1.f : -1.f;
			OutFillOnLeft[Ring.Line] = FillSide > 0.f ? 1 : -1;

			const TArray<FVector2f>& Points = Ring.Points;
			const int32 Count = Points.Num();
			TArray<FVector2f> Inner;
			TArray<FVector2f> Outer;
			Inner.SetNumUninitialized(Count);
			Outer.SetNumUninitialized(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FVector2f& Previous = Points[(Index + Count - 1) % Count];
				const FVector2f& Point = Points[Index];
				const FVector2f& Next = Points[(Index + 1) % Count];
				const FVector2f Miter = MiterOf(LeftOf(Normalized(Point - Previous)), LeftOf(Normalized(Next - Point)), MaxFringeMiter) * FillSide;
				Inner[Index] = Point + Miter * HalfFade;
				Outer[Index] = Point - Miter * HalfFade;
			}
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const int32 Next = (Index + 1) % Count;
				FFillEdge& Edge = Edges.AddDefaulted_GetRef();
				Edge.A = Inner[Index];
				Edge.B = Inner[Next];
				Edge.AOut = Outer[Index];
				Edge.BOut = Outer[Next];
				Edge.MinY = FMath::Min(Edge.A.Y, Edge.B.Y);
				Edge.MaxY = FMath::Max(Edge.A.Y, Edge.B.Y);
			}
		}
		if (Edges.Num() < 3)
		{
			return;
		}

		/**
		 * The fill is cut into horizontal bands at every point's height and wherever two edges cross. Inside one band no
		 * edge crosses another, so the edges through it sorted left to right alternate out, in, out (even-odd) and each pair
		 * bounds one trapezoid of fill. That is exact for any outline - a contour crossing itself, touching itself, running
		 * along itself, holes inside holes - where ear clipping can only guess.
		 */
		TArray<float> Heights;
		Heights.Reserve(Edges.Num() * 2);
		for (const FFillEdge& Edge : Edges)
		{
			Heights.Add(Edge.A.Y);
		}
		TArray<int32> ByTop;
		ByTop.SetNumUninitialized(Edges.Num());
		for (int32 Index = 0; Index < Edges.Num(); ++Index)
		{
			ByTop[Index] = Index;
		}
		ByTop.Sort([&Edges](int32 A, int32 B) { return Edges[A].MinY < Edges[B].MinY; });
		for (int32 First = 0; First < ByTop.Num(); ++First)
		{
			const FFillEdge& EdgeA = Edges[ByTop[First]];
			for (int32 Second = First + 1; Second < ByTop.Num(); ++Second)
			{
				const FFillEdge& EdgeB = Edges[ByTop[Second]];
				if (EdgeB.MinY > EdgeA.MaxY)
				{
					break;
				}
				float Y = 0.f;
				if (CrossingHeight(EdgeA, EdgeB, Y))
				{
					Heights.Add(Y);
				}
			}
		}
		Heights.Sort();

		// Heights closer than this are one band boundary; an edge ending within it of a boundary counts as ending there.
		constexpr float Same = 1e-4f;
		TArray<float> Bands;
		Bands.Reserve(Heights.Num());
		for (const float Y : Heights)
		{
			if (Bands.Num() == 0 || Y - Bands.Last() > Same)
			{
				Bands.Add(Y);
			}
		}

		// Each edge's points at each band boundary, made once and shared by the bands either side and by its fade, so the
		// fill and its fade meet vertex to vertex with no cracks.
		TMap<uint64, uint32> InnerVertex;
		TMap<uint64, uint32> OuterVertex;
		auto Key = [](int32 Edge, int32 Band) { return (static_cast<uint64>(static_cast<uint32>(Edge)) << 32) | static_cast<uint32>(Band); };
		auto InnerAt = [&](int32 Edge, int32 Band) -> uint32
		{
			const uint64 K = Key(Edge, Band);
			if (const uint32* Found = InnerVertex.Find(K))
			{
				return *Found;
			}
			const uint32 New = Out.AddVertex(Edges[Edge].At(Bands[Band]), 1.f);
			InnerVertex.Add(K, New);
			return New;
		};
		auto OuterAt = [&](int32 Edge, int32 Band) -> uint32
		{
			const uint64 K = Key(Edge, Band);
			if (const uint32* Found = OuterVertex.Find(K))
			{
				return *Found;
			}
			const uint32 New = Out.AddVertex(Edges[Edge].OutAt(Bands[Band]), 0.f);
			OuterVertex.Add(K, New);
			return New;
		};

		struct FCrossing
		{
			int32 Edge;
			float MiddleX;
		};
		TArray<FCrossing> Crossings;
		int32 NextByTop = 0;
		TArray<int32> Active;
		for (int32 Band = 0; Band + 1 < Bands.Num(); ++Band)
		{
			const float Top = Bands[Band];
			const float Bottom = Bands[Band + 1];
			const float Middle = (Top + Bottom) * 0.5f;

			while (NextByTop < ByTop.Num() && Edges[ByTop[NextByTop]].MinY <= Top + Same)
			{
				Active.Add(ByTop[NextByTop++]);
			}
			Active.RemoveAll([&Edges, Bottom](int32 Edge) { return Edges[Edge].MaxY < Bottom - Same; });

			Crossings.Reset();
			for (const int32 Edge : Active)
			{
				const FFillEdge& Fill = Edges[Edge];
				if (Fill.MaxY - Fill.MinY > Same && Fill.MinY <= Top + Same && Fill.MaxY >= Bottom - Same)
				{
					Crossings.Add({ Edge, Fill.At(Middle).X });
				}
			}
			Crossings.Sort([](const FCrossing& A, const FCrossing& B) { return A.MiddleX < B.MiddleX; });

			for (int32 Pair = 0; Pair + 1 < Crossings.Num(); Pair += 2)
			{
				const int32 Left = Crossings[Pair].Edge;
				const int32 Right = Crossings[Pair + 1].Edge;
				Out.AddQuad(InnerAt(Left, Band), InnerAt(Right, Band), InnerAt(Right, Band + 1), InnerAt(Left, Band + 1));
			}

			// The fade along every edge through this band, in step with the fill.
			if (AntiAliasWidth > 0.f)
			{
				for (const FCrossing& Crossing : Crossings)
				{
					Out.AddQuad(InnerAt(Crossing.Edge, Band), OuterAt(Crossing.Edge, Band), OuterAt(Crossing.Edge, Band + 1), InnerAt(Crossing.Edge, Band + 1));
				}
			}
		}

		// Level edges lie along a band boundary and cross no band: their fade is laid along them whole.
		if (AntiAliasWidth > 0.f)
		{
			for (const FFillEdge& Edge : Edges)
			{
				if (Edge.MaxY - Edge.MinY <= Same)
				{
					Out.AddQuad(Out.AddVertex(Edge.A, 1.f), Out.AddVertex(Edge.AOut, 0.f), Out.AddVertex(Edge.BOut, 0.f), Out.AddVertex(Edge.B, 1.f));
				}
			}
		}
	}

	void BuildStroke(const FWidgetShapeOutline& Outline, const TArray<int8>& FillOnLeft, const FWidgetShapeStroke& Stroke, const FWidgetShapeTrim& Trim, float AntiAliasWidth, float Tolerance, FWidgetShapeMesh& Out)
	{
		using namespace WidgetShapeGeometryPrivate;

		if (!Stroke.IsDrawn())
		{
			return;
		}

		FStrokeBuilder Builder(Out);
		Builder.Join = Stroke.Join;
		Builder.MiterLimit = FMath::Max(Stroke.MiterLimit, 1.f);
		Builder.Cap = Stroke.Cap;
		Builder.Tolerance = Tolerance;
		Builder.HalfFade = AntiAliasWidth * 0.5f;

		// A stroke thinner than the fade is drawn a fade wide and as much fainter: a hairline stays even instead of breaking up.
		float Width = Stroke.Width;
		Builder.Opacity = 1.f;
		if (AntiAliasWidth > 0.f && Width < AntiAliasWidth)
		{
			Builder.Opacity = FMath::Clamp(Width / AntiAliasWidth, 0.f, 1.f);
			Width = AntiAliasWidth;
		}

		TArray<TArray<FVector2f>> Pieces;
		TArray<float> PieceStarts;
		TArray<TArray<FVector2f>> Dashes;
		TArray<float> DashStarts;
		for (int32 LineIndex = 0; LineIndex < Outline.Lines.Num(); ++LineIndex)
		{
			const FWidgetShapeLine& Line = Outline.Lines[LineIndex];

			// Inside and Outside only mean something on a closed line that bounds the fill. Positive FillOnLeft: the fill is
			// to the left of the line's direction, so Inside leans left.
			const int8 FillSide = FillOnLeft.IsValidIndex(LineIndex) ? FillOnLeft[LineIndex] : 0;
			float LeftExtent = Width * 0.5f;
			float RightExtent = Width * 0.5f;
			if (Line.bClosed && FillSide != 0 && Stroke.Align != EWidgetShapeStrokeAlign::Center)
			{
				const bool bLeanLeft = (Stroke.Align == EWidgetShapeStrokeAlign::Inside) == (FillSide > 0);
				LeftExtent = bLeanLeft ? Width : 0.f;
				RightExtent = bLeanLeft ? 0.f : Width;
			}
			Builder.LeftExtent = LeftExtent;
			Builder.RightExtent = RightExtent;

			// For an image laid along the outline: the contour's length, and its inside - the fill's side, or the left of an
			// open line.
			Builder.OutlineLength = GetLength(Line.Points, Line.bClosed);
			Builder.bInsideOnLeft = FillSide >= 0;

			bool bWhole = false;
			TrimRun(Line.Points, Line.bClosed, Trim, Pieces, PieceStarts, bWhole);
			if (bWhole)
			{
				Pieces.Reset();
				Pieces.Add(Line.Points);
				PieceStarts.Reset();
				PieceStarts.Add(0.f);
			}
			const bool bPiecesClosed = bWhole && Line.bClosed;

			for (int32 PieceIndex = 0; PieceIndex < Pieces.Num(); ++PieceIndex)
			{
				const TArray<FVector2f>& Piece = Pieces[PieceIndex];
				const float PieceStart = PieceStarts.IsValidIndex(PieceIndex) ? PieceStarts[PieceIndex] : 0.f;
				if (!Stroke.IsDashed())
				{
					Builder.Stroke(Piece, bPiecesClosed, PieceStart);
					continue;
				}

				const float Gap = Stroke.DashGap > 0.f ? Stroke.DashGap : Stroke.DashLength;
				DashRun(Piece, bPiecesClosed, Stroke.DashLength, Gap, Stroke.DashOffset, Dashes, DashStarts);
				for (int32 DashIndex = 0; DashIndex < Dashes.Num(); ++DashIndex)
				{
					Builder.Stroke(Dashes[DashIndex], false, PieceStart + (DashStarts.IsValidIndex(DashIndex) ? DashStarts[DashIndex] : 0.f));
				}
			}
		}
	}

	void CutForGradient(FWidgetShapeMesh& Mesh, const FWidgetShapePaint& Paint, const FWidgetShapeFrame& Frame, float CellSize)
	{
		using namespace WidgetShapeGeometryPrivate;

		if (Mesh.IsEmpty() || !Paint.IsGradient())
		{
			return;
		}

		if (Paint.Type == EWidgetShapePaintType::LinearGradient)
		{
			// The gradient's position is linear in local position, so cutting at each stop leaves only linear blends, which
			// the GPU draws exactly.
			TArray<float> Stops;
			Paint.GetStopPositions(Stops);
			for (const float Stop : Stops)
			{
				CutAlong(Mesh, Stop, [&](const FVector2f& Position) { return Paint.GetGradientPosition(Frame.ToBox(Position)); });
			}
			return;
		}

		// Radial: a grid of cells over the mesh's bounds, in local space, fine enough that the rings read round.
		FBox2f Bounds(ForceInit);
		for (const FWidgetShapeMeshVertex& Vertex : Mesh.Vertices)
		{
			Bounds += Vertex.Position;
		}
		const FVector2f Size = Bounds.GetSize();
		const float Cell = FMath::Max3(CellSize, Size.X / MaxGradientCells, Size.Y / MaxGradientCells);
		if (Cell <= 0.f)
		{
			return;
		}
		for (float X = FMath::FloorToFloat(Bounds.Min.X / Cell) * Cell + Cell; X < Bounds.Max.X; X += Cell)
		{
			CutAlong(Mesh, X, [](const FVector2f& Position) { return Position.X; });
		}
		for (float Y = FMath::FloorToFloat(Bounds.Min.Y / Cell) * Cell + Cell; Y < Bounds.Max.Y; Y += Cell)
		{
			CutAlong(Mesh, Y, [](const FVector2f& Position) { return Position.Y; });
		}
	}

	bool IsInside(const FWidgetShapeOutline& Outline, const FVector2f& LocalPoint)
	{
		using namespace WidgetShapeGeometryPrivate;

		bool bInside = false;
		for (const FWidgetShapeLine& Line : Outline.Lines)
		{
			if (Line.Points.Num() >= 3 && PointInPolygon(Line.Points, LocalPoint))
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	float GetLength(const TArray<FVector2f>& Points, bool bClosed)
	{
		float Length = 0.f;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Length += (Points[Index] - Points[Index - 1]).Size();
		}
		if (bClosed && Points.Num() > 1)
		{
			Length += (Points[0] - Points.Last()).Size();
		}
		return Length;
	}
}

// =====================================================================================================
// Content area and hit area
// =====================================================================================================

namespace WidgetShapeGeometryPrivate
{
	/** Every edge of every contour of three points or more, open ones closed across their ends - as IsInside reads them. */
	void GatherEdges(const FWidgetShapeOutline& Outline, TArray<TPair<FVector2f, FVector2f>>& OutEdges, FBox2f& OutBounds)
	{
		OutEdges.Reset();
		OutBounds = FBox2f(ForceInit);
		for (const FWidgetShapeLine& Line : Outline.Lines)
		{
			const int32 Count = Line.Points.Num();
			if (Count < 3)
			{
				continue;
			}
			for (int32 Index = 0, Previous = Count - 1; Index < Count; Previous = Index++)
			{
				OutEdges.Emplace(Line.Points[Previous], Line.Points[Index]);
				OutBounds += Line.Points[Index];
			}
		}
	}

	/** Whether any part of the segment A-B lies in the box, its edges included (Liang-Barsky clipping). */
	bool SegmentTouchesBox(const FVector2f& A, const FVector2f& B, const FVector2f& BoxMin, const FVector2f& BoxMax)
	{
		const FVector2f Delta = B - A;
		const float Direction[4] = { -Delta.X, Delta.X, -Delta.Y, Delta.Y };
		const float Room[4] = { A.X - BoxMin.X, BoxMax.X - A.X, A.Y - BoxMin.Y, BoxMax.Y - A.Y };
		float Enter = 0.f;
		float Leave = 1.f;
		for (int32 Side = 0; Side < 4; ++Side)
		{
			if (FMath::IsNearlyZero(Direction[Side]))
			{
				if (Room[Side] < 0.f)
				{
					return false;
				}
				continue;
			}
			const float Crossing = Room[Side] / Direction[Side];
			if (Direction[Side] < 0.f)
			{
				if (Crossing > Leave)
				{
					return false;
				}
				Enter = FMath::Max(Enter, Crossing);
			}
			else
			{
				if (Crossing < Enter)
				{
					return false;
				}
				Leave = FMath::Min(Leave, Crossing);
			}
		}
		return Enter <= Leave;
	}

	/** Grows rectangles at one aspect about a point until they touch the outline. */
	struct FInscribedSearch
	{
		const FWidgetShapeOutline& Outline;
		TArray<TPair<FVector2f, FVector2f>> Edges;
		float Aspect = 1.f;
		float Margin = 0.f;
		float MaxHalfHeight = 0.f;

		explicit FInscribedSearch(const FWidgetShapeOutline& InOutline) : Outline(InOutline) {}

		/** No edge crosses or touches the rectangle of HalfHeight about Center, grown by Margin. */
		bool Fits(const FVector2f& Center, float HalfHeight) const
		{
			const FVector2f Half(Aspect * HalfHeight + Margin, HalfHeight + Margin);
			const FVector2f BoxMin = Center - Half;
			const FVector2f BoxMax = Center + Half;
			for (const TPair<FVector2f, FVector2f>& Edge : Edges)
			{
				if (SegmentTouchesBox(Edge.Key, Edge.Value, BoxMin, BoxMax))
				{
					return false;
				}
			}
			return true;
		}

		/**
		 * The half height of the largest rectangle about Center that fits, or -1 when Center isn't on the fill. With no edge
		 * touching it and its middle on the fill, the whole rectangle is: nothing inside it can be a hole's edge.
		 */
		float Largest(const FVector2f& Center) const
		{
			if (!WidgetShapeGeometry::IsInside(Outline, Center) || !Fits(Center, 0.f))
			{
				return -1.f;
			}
			if (Fits(Center, MaxHalfHeight))
			{
				return MaxHalfHeight;
			}
			float Low = 0.f;
			float High = MaxHalfHeight;
			for (int32 Step = 0; Step < 18; ++Step)
			{
				const float Middle = (Low + High) * 0.5f;
				if (Fits(Center, Middle))
				{
					Low = Middle;
				}
				else
				{
					High = Middle;
				}
			}
			return Low;
		}
	};
}

namespace WidgetShapeGeometry
{
	bool FindInscribedRect(const FWidgetShapeOutline& Outline, float Aspect, float Margin, FBox2f& OutRect)
	{
		using namespace WidgetShapeGeometryPrivate;

		FInscribedSearch Search(Outline);
		FBox2f Bounds(ForceInit);
		GatherEdges(Outline, Search.Edges, Bounds);
		if (!Bounds.bIsValid || Search.Edges.Num() < 3)
		{
			return false;
		}

		Search.Aspect = FMath::Clamp(Aspect, 0.01f, 100.f);
		Search.Margin = FMath::Max(Margin, 0.f);
		const FVector2f Extent = Bounds.GetSize();
		Search.MaxHalfHeight = FMath::Min(Extent.Y, Extent.X / Search.Aspect) * 0.5f;
		if (Search.MaxHalfHeight <= 0.f)
		{
			return false;
		}

		// Where to start: the middle, the centroid (a triangle's room is low in it), and a grid between.
		TArray<FVector2f, TInlineAllocator<12>> Starts;
		Starts.Add(Bounds.GetCenter());
		{
			float Area = 0.f;
			FVector2f Weighted = FVector2f::ZeroVector;
			for (const TPair<FVector2f, FVector2f>& Edge : Search.Edges)
			{
				const float Cross = Edge.Key.X * Edge.Value.Y - Edge.Value.X * Edge.Key.Y;
				Area += Cross;
				Weighted += (Edge.Key + Edge.Value) * Cross;
			}
			if (!FMath::IsNearlyZero(Area))
			{
				Starts.Add(Weighted / (3.f * Area));
			}
		}
		for (int32 Row = 1; Row <= 3; ++Row)
		{
			for (int32 Column = 1; Column <= 3; ++Column)
			{
				if (Row != 2 || Column != 2)
				{
					Starts.Add(Bounds.Min + Extent * FVector2f(Column * 0.25f, Row * 0.25f));
				}
			}
		}

		FVector2f Best = Starts[0];
		float BestHalf = -1.f;
		for (const FVector2f& Start : Starts)
		{
			const float Half = Search.Largest(Start);
			if (Half > BestHalf)
			{
				BestHalf = Half;
				Best = Start;
			}
		}
		if (BestHalf <= 0.f)
		{
			return false;
		}

		// Moved toward more room, eight ways, the step halved whenever no way gives more.
		static const FVector2f Ways[8] =
		{
			FVector2f(1.f, 0.f), FVector2f(-1.f, 0.f), FVector2f(0.f, 1.f), FVector2f(0.f, -1.f),
			FVector2f(0.7071f, 0.7071f), FVector2f(-0.7071f, 0.7071f), FVector2f(0.7071f, -0.7071f), FVector2f(-0.7071f, -0.7071f)
		};
		const float LongestSide = FMath::Max(Extent.X, Extent.Y);
		float StepLength = LongestSide * 0.125f;
		for (int32 Round = 0; Round < 64 && StepLength > LongestSide * 0.002f; ++Round)
		{
			FVector2f Moved = Best;
			float MovedHalf = BestHalf;
			for (const FVector2f& Way : Ways)
			{
				const FVector2f Candidate = Best + Way * StepLength;
				const float Half = Search.Largest(Candidate);
				if (Half > MovedHalf + KINDA_SMALL_NUMBER)
				{
					MovedHalf = Half;
					Moved = Candidate;
				}
			}
			if (MovedHalf > BestHalf)
			{
				Best = Moved;
				BestHalf = MovedHalf;
			}
			else
			{
				StepLength *= 0.5f;
			}
		}

		const FVector2f HalfSize(Search.Aspect * BestHalf, BestHalf);
		OutRect = FBox2f(Best - HalfSize, Best + HalfSize);
		return true;
	}

	void BuildFillRects(const FWidgetShapeOutline& Outline, float RowHeight, int32 MaxRows, TArray<FBox2f>& OutRects)
	{
		using namespace WidgetShapeGeometryPrivate;

		OutRects.Reset();
		TArray<TPair<FVector2f, FVector2f>> Edges;
		FBox2f Bounds(ForceInit);
		GatherEdges(Outline, Edges, Bounds);
		const float Height = Bounds.bIsValid ? Bounds.Max.Y - Bounds.Min.Y : 0.f;
		if (Edges.Num() < 3 || Height <= 0.f)
		{
			return;
		}

		const int32 Rows = FMath::Clamp(FMath::CeilToInt32(Height / FMath::Max(RowHeight, 0.01f)), 1, FMath::Max(MaxRows, 1));
		const float Step = Height / static_cast<float>(Rows);
		constexpr float SameEdge = 0.25f;

		TArray<float> Crossings;
		TArray<int32> Open;
		TArray<int32> NextOpen;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const float Top = Bounds.Min.Y + Step * static_cast<float>(Row);
			const float Bottom = Top + Step;
			const float Y = Top + Step * 0.5f;

			Crossings.Reset();
			for (const TPair<FVector2f, FVector2f>& Edge : Edges)
			{
				const FVector2f& A = Edge.Key;
				const FVector2f& B = Edge.Value;
				if ((A.Y <= Y) != (B.Y <= Y))
				{
					Crossings.Add(A.X + (Y - A.Y) * (B.X - A.X) / (B.Y - A.Y));
				}
			}
			Crossings.Sort();

			// Even-odd: inside between the first and second crossing, the third and fourth...
			const int32 Spans = Crossings.Num() / 2;
			const bool bSameCount = Open.Num() == Spans;
			NextOpen.Reset();
			for (int32 Span = 0; Span < Spans; ++Span)
			{
				const float Left = Crossings[Span * 2];
				const float Right = Crossings[Span * 2 + 1];
				if (bSameCount)
				{
					FBox2f& Above = OutRects[Open[Span]];
					if (FMath::Abs(Above.Min.X - Left) <= SameEdge && FMath::Abs(Above.Max.X - Right) <= SameEdge)
					{
						Above.Max.Y = Bottom;
						NextOpen.Add(Open[Span]);
						continue;
					}
				}
				NextOpen.Add(OutRects.Add(FBox2f(FVector2f(Left, Top), FVector2f(Right, Bottom))));
			}
			Swap(Open, NextOpen);
		}
	}
}
