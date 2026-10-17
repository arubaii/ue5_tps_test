// Copyright AR GameSystems 2026. All rights reserved.

#include "CurveFitting.h"

namespace CurveFittingPrivate
{
	/** Below this (drawn units) two points are taken as one. */
	constexpr double Tiny = 1e-6;

	/**
	 * One stretch of a contour worked out on its own: between two corners, between an open line's ends, or - Periodic - the
	 * whole of a closed contour with no corner. Indices are into the contour (they wrap on a closed one).
	 */
	struct FRun
	{
		TArray<int32> Indices;
		bool bPeriodic = false;
	};

	/** What a run's interpolation gives each of its points: handles (offsets), and where the point goes (the B-Spline moves them). */
	struct FRunResult
	{
		TArray<FVector2D> Positions;
		TArray<FVector2D> Ins;
		TArray<FVector2D> Outs;

		void Init(const TArray<FVector2D>& Points)
		{
			Positions = Points;
			Ins.Init(FVector2D::ZeroVector, Points.Num());
			Outs.Init(FVector2D::ZeroVector, Points.Num());
		}
	};

	bool IsCorner(const FWidgetShapeVertex& Vertex)
	{
		return Vertex.Type == EWidgetShapeVertexType::Disconnected;
	}

	/** The contour cut into runs at its corners. */
	void MakeRuns(const FWidgetShapeContour& Contour, TArray<FRun>& OutRuns)
	{
		OutRuns.Reset();
		const int32 Count = Contour.Vertices.Num();
		if (Count < 2)
		{
			return;
		}
		TArray<int32> Corners;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (IsCorner(Contour.Vertices[Index]))
			{
				Corners.Add(Index);
			}
		}

		if (!Contour.bClosed)
		{
			// From the first point to the last, stopping at every corner on the way.
			FRun* Run = &OutRuns.AddDefaulted_GetRef();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Run->Indices.Add(Index);
				if (Index > 0 && Index < Count - 1 && IsCorner(Contour.Vertices[Index]))
				{
					Run = &OutRuns.AddDefaulted_GetRef();
					Run->Indices.Add(Index);
				}
			}
			return;
		}

		if (Corners.Num() == 0)
		{
			FRun& Run = OutRuns.AddDefaulted_GetRef();
			Run.bPeriodic = true;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Run.Indices.Add(Index);
			}
			return;
		}

		// Closed with corners: from each corner round to the next.
		for (int32 CornerIndex = 0; CornerIndex < Corners.Num(); ++CornerIndex)
		{
			const int32 From = Corners[CornerIndex];
			const int32 To = Corners[(CornerIndex + 1) % Corners.Num()];
			FRun& Run = OutRuns.AddDefaulted_GetRef();
			int32 Index = From;
			Run.Indices.Add(Index);
			do
			{
				Index = (Index + 1) % Count;
				Run.Indices.Add(Index);
			}
			while (Index != To);
		}
	}

	/**
	 * Puts the runs' results on the contour. Points inside a run are smooth (Asymmetric); an open line's ends keep their one
	 * handle; corners take the handle each run gives them and stay Disconnected - or Straight with no handles when they have a
	 * Corner Radius to round them with.
	 */
	void Apply(FWidgetShapeContour& Contour, const TArray<FRun>& Runs, const TArray<FRunResult>& Results, double Scale)
	{
		const int32 Count = Contour.Vertices.Num();
		TArray<FVector2D> Ins;
		TArray<FVector2D> Outs;
		TArray<FVector2D> Positions;
		Ins.Init(FVector2D::ZeroVector, Count);
		Outs.Init(FVector2D::ZeroVector, Count);
		Positions.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Positions[Index] = Contour.Vertices[Index].Position;
		}

		for (int32 RunIndex = 0; RunIndex < Runs.Num(); ++RunIndex)
		{
			const FRun& Run = Runs[RunIndex];
			const FRunResult& Result = Results[RunIndex];
			const int32 Last = Run.Indices.Num() - 1;
			for (int32 Place = 0; Place <= Last; ++Place)
			{
				const int32 Index = Run.Indices[Place];
				const bool bStart = !Run.bPeriodic && Place == 0;
				const bool bEnd = !Run.bPeriodic && Place == Last;
				if (!bEnd)
				{
					Outs[Index] = Result.Outs[Place] * Scale;
				}
				if (!bStart)
				{
					Ins[Index] = Result.Ins[Place] * Scale;
				}
				if (!bStart && !bEnd)
				{
					Positions[Index] = Result.Positions[Place];
				}
			}
		}

		for (int32 Index = 0; Index < Count; ++Index)
		{
			FWidgetShapeVertex& Vertex = Contour.Vertices[Index];
			Vertex.Position = Positions[Index];
			if (IsCorner(Vertex) && Vertex.CornerRadius > 0.f)
			{
				Vertex.Type = EWidgetShapeVertexType::Straight;
				Vertex.InHandle = FVector2D::ZeroVector;
				Vertex.OutHandle = FVector2D::ZeroVector;
				continue;
			}
			const bool bAny = !Ins[Index].IsNearlyZero(Tiny) || !Outs[Index].IsNearlyZero(Tiny);
			if (!IsCorner(Vertex))
			{
				Vertex.Type = bAny ? EWidgetShapeVertexType::Asymmetric : EWidgetShapeVertexType::Straight;
			}
			Vertex.InHandle = Ins[Index];
			Vertex.OutHandle = Outs[Index];
		}
	}

	/** Runs every run of Contour through Solve (its points, periodic or not, filling the result) and puts the results on it. */
	template <typename TSolve>
	void SolveRuns(FWidgetShapeContour& Contour, double Scale, TSolve Solve)
	{
		TArray<FRun> Runs;
		MakeRuns(Contour, Runs);
		TArray<FRunResult> Results;
		Results.SetNum(Runs.Num());
		TArray<FVector2D> Points;
		for (int32 RunIndex = 0; RunIndex < Runs.Num(); ++RunIndex)
		{
			Points.Reset();
			for (const int32 Index : Runs[RunIndex].Indices)
			{
				Points.Add(Contour.Vertices[Index].Position);
			}
			Results[RunIndex].Init(Points);
			Solve(Points, Runs[RunIndex].bPeriodic, Results[RunIndex]);
		}
		Apply(Contour, Runs, Results, Scale);
	}

	/** A point of a run by index: wrapped when periodic, else past either end mirrored through it (the curve leaves its end straight on). */
	FVector2D PointAt(const TArray<FVector2D>& Points, bool bPeriodic, int32 Index)
	{
		const int32 Count = Points.Num();
		if (bPeriodic)
		{
			return Points[((Index % Count) + Count) % Count];
		}
		if (Index < 0)
		{
			return Points[0] * 2.0 - Points[FMath::Min(1, Count - 1)];
		}
		if (Index >= Count)
		{
			return Points[Count - 1] * 2.0 - Points[FMath::Max(Count - 2, 0)];
		}
		return Points[Index];
	}

	void SolveCatmullRom(const TArray<FVector2D>& Points, bool bPeriodic, double Alpha, FRunResult& Out)
	{
		const int32 Count = Points.Num();
		const int32 Segments = bPeriodic ? Count : Count - 1;
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const FVector2D P0 = PointAt(Points, bPeriodic, Segment - 1);
			const FVector2D P1 = PointAt(Points, bPeriodic, Segment);
			const FVector2D P2 = PointAt(Points, bPeriodic, Segment + 1);
			const FVector2D P3 = PointAt(Points, bPeriodic, Segment + 2);
			const double Chord = (P2 - P1).Size();
			if (Chord <= Tiny)
			{
				continue;
			}
			// Knot spacing |Pi+1 - Pi|^Alpha, and the Bezier the non-uniform Catmull-Rom segment is (Yuksel, Schaefer, Keyser).
			const double D1 = FMath::Pow(FMath::Max((P1 - P0).Size(), Tiny), Alpha);
			const double D2 = FMath::Pow(Chord, Alpha);
			const double D3 = FMath::Pow(FMath::Max((P3 - P2).Size(), Tiny), Alpha);
			const FVector2D B1 = (P2 * (D1 * D1) - P0 * (D2 * D2) + P1 * (2.0 * D1 * D1 + 3.0 * D1 * D2 + D2 * D2)) / (3.0 * D1 * (D1 + D2));
			const FVector2D B2 = (P1 * (D3 * D3) - P3 * (D2 * D2) + P2 * (2.0 * D3 * D3 + 3.0 * D3 * D2 + D2 * D2)) / (3.0 * D3 * (D3 + D2));
			Out.Outs[Segment] = B1 - P1;
			Out.Ins[(Segment + 1) % Count] = B2 - P2;
		}
	}

	/** Solves a tridiagonal system (Thomas): Lower[i] x[i-1] + Diagonal[i] x[i] + Upper[i] x[i+1] = Right[i]. */
	template <typename TValue>
	void SolveTridiagonal(const TArray<double>& Lower, const TArray<double>& Diagonal, const TArray<double>& Upper, const TArray<TValue>& Right, TArray<TValue>& OutX)
	{
		const int32 Count = Diagonal.Num();
		TArray<double> Gamma;
		Gamma.SetNum(Count);
		OutX.SetNum(Count);
		double Beta = Diagonal[0];
		OutX[0] = Right[0] / Beta;
		for (int32 Index = 1; Index < Count; ++Index)
		{
			Gamma[Index] = Upper[Index - 1] / Beta;
			Beta = Diagonal[Index] - Lower[Index] * Gamma[Index];
			if (FMath::Abs(Beta) < 1e-12)
			{
				Beta = 1e-12;
			}
			OutX[Index] = (Right[Index] - OutX[Index - 1] * Lower[Index]) / Beta;
		}
		for (int32 Index = Count - 2; Index >= 0; --Index)
		{
			OutX[Index] = OutX[Index] - OutX[Index + 1] * Gamma[Index + 1];
		}
	}

	void SolveNatural(const TArray<FVector2D>& Points, bool bPeriodic, FRunResult& Out)
	{
		const int32 Count = Points.Num();
		if (Count < 2 || (bPeriodic && Count < 3))
		{
			SolveCatmullRom(Points, bPeriodic, 0.5, Out);
			return;
		}

		// Knot intervals: the chord lengths.
		const int32 Intervals = bPeriodic ? Count : Count - 1;
		TArray<double> H;
		H.SetNum(Intervals);
		for (int32 Index = 0; Index < Intervals; ++Index)
		{
			H[Index] = FMath::Max((Points[(Index + 1) % Count] - Points[Index]).Size(), 1e-4);
		}

		// The tangent at each point (per unit of length) that makes the curvature continuous through it:
		//   h(i) D(i-1) + 2 (h(i-1) + h(i)) D(i) + h(i-1) D(i+1) = 3 (h(i-1) (P(i+1) - P(i)) / h(i) + h(i) (P(i) - P(i-1)) / h(i-1))
		// with no bending at an open run's ends: 2 D0 + D1 = 3 (P1 - P0) / h0, and the same at the other end.
		TArray<double> Lower;
		TArray<double> Diagonal;
		TArray<double> Upper;
		TArray<FVector2D> Right;
		Lower.Init(0.0, Count);
		Diagonal.Init(0.0, Count);
		Upper.Init(0.0, Count);
		Right.Init(FVector2D::ZeroVector, Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const bool bFirst = !bPeriodic && Index == 0;
			const bool bLast = !bPeriodic && Index == Count - 1;
			if (bFirst)
			{
				Diagonal[Index] = 2.0;
				Upper[Index] = 1.0;
				Right[Index] = (Points[1] - Points[0]) * (3.0 / H[0]);
				continue;
			}
			if (bLast)
			{
				Lower[Index] = 1.0;
				Diagonal[Index] = 2.0;
				Right[Index] = (Points[Count - 1] - Points[Count - 2]) * (3.0 / H[Count - 2]);
				continue;
			}
			const double Before = H[(Index - 1 + Intervals) % Intervals];
			const double After = H[Index % Intervals];
			const FVector2D Previous = Points[(Index - 1 + Count) % Count];
			const FVector2D Next = Points[(Index + 1) % Count];
			Lower[Index] = After;
			Diagonal[Index] = 2.0 * (Before + After);
			Upper[Index] = Before;
			Right[Index] = ((Next - Points[Index]) * (Before / After) + (Points[Index] - Previous) * (After / Before)) * 3.0;
		}

		TArray<FVector2D> D;
		if (!bPeriodic)
		{
			SolveTridiagonal(Lower, Diagonal, Upper, Right, D);
		}
		else
		{
			// Cyclic: the first row also reaches the last unknown and the last row the first. Sherman-Morrison over the plain
			// tridiagonal solve (Numerical Recipes' cyclic).
			const double CornerTop = Lower[0];
			const double CornerBottom = Upper[Count - 1];
			const double Gamma = -Diagonal[0];
			TArray<double> Modified = Diagonal;
			Modified[0] = Diagonal[0] - Gamma;
			Modified[Count - 1] = Diagonal[Count - 1] - CornerBottom * CornerTop / Gamma;
			TArray<FVector2D> X;
			SolveTridiagonal(Lower, Modified, Upper, Right, X);
			TArray<double> U;
			U.Init(0.0, Count);
			U[0] = Gamma;
			U[Count - 1] = CornerBottom;
			TArray<double> Z;
			SolveTridiagonal(Lower, Modified, Upper, U, Z);
			const FVector2D Factor = (X[0] + X[Count - 1] * (CornerTop / Gamma)) / (1.0 + Z[0] + CornerTop * Z[Count - 1] / Gamma);
			D.SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				D[Index] = X[Index] - Factor * Z[Index];
			}
		}

		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (bPeriodic || Index < Count - 1)
			{
				Out.Outs[Index] = D[Index] * (H[Index % Intervals] / 3.0);
			}
			if (bPeriodic || Index > 0)
			{
				Out.Ins[Index] = -D[Index] * (H[(Index - 1 + Intervals) % Intervals] / 3.0);
			}
		}
	}

	void SolveMonotone(const TArray<FVector2D>& Points, bool bPeriodic, FRunResult& Out)
	{
		const int32 Count = Points.Num();
		if (bPeriodic || Count < 2)
		{
			SolveCatmullRom(Points, bPeriodic, 0.5, Out);
			return;
		}
		// One way across, or not monotone at all: worked out left to right (a run going right to left is mirrored first).
		const double Way = Points[Count - 1].X >= Points[0].X ? 1.0 : -1.0;
		for (int32 Index = 0; Index + 1 < Count; ++Index)
		{
			if ((Points[Index + 1].X - Points[Index].X) * Way <= Tiny)
			{
				SolveCatmullRom(Points, bPeriodic, 0.5, Out);
				return;
			}
		}

		TArray<double> H;
		TArray<double> S;
		H.SetNum(Count - 1);
		S.SetNum(Count - 1);
		for (int32 Index = 0; Index + 1 < Count; ++Index)
		{
			H[Index] = (Points[Index + 1].X - Points[Index].X) * Way;
			S[Index] = (Points[Index + 1].Y - Points[Index].Y) / H[Index];
		}

		// Steffen (1990): each slope the smaller of its neighbours' and half their weighted mean, 0 at a peak or a dip.
		TArray<double> M;
		M.Init(0.0, Count);
		if (Count == 2)
		{
			M[0] = S[0];
			M[1] = S[0];
		}
		else
		{
			for (int32 Index = 1; Index < Count - 1; ++Index)
			{
				const double Before = S[Index - 1];
				const double After = S[Index];
				const double Mean = (Before * H[Index] + After * H[Index - 1]) / (H[Index - 1] + H[Index]);
				M[Index] = (FMath::Sign(Before) + FMath::Sign(After)) * FMath::Min(FMath::Min(FMath::Abs(Before), FMath::Abs(After)), 0.5 * FMath::Abs(Mean));
			}
			auto EndSlope = [](double Near, double Far, double HNear, double HFar)
			{
				const double Guess = Near * (1.0 + HNear / (HNear + HFar)) - Far * HNear / (HNear + HFar);
				if (Guess * Near <= 0.0)
				{
					return 0.0;
				}
				return FMath::Abs(Guess) > 2.0 * FMath::Abs(Near) ? 2.0 * Near : Guess;
			};
			M[0] = EndSlope(S[0], S[1], H[0], H[1]);
			M[Count - 1] = EndSlope(S[Count - 2], S[Count - 3], H[Count - 2], H[Count - 3]);
		}

		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Index < Count - 1)
			{
				const double Step = H[Index] / 3.0;
				Out.Outs[Index] = FVector2D(Step * Way, M[Index] * Step);
			}
			if (Index > 0)
			{
				const double Step = H[Index - 1] / 3.0;
				Out.Ins[Index] = FVector2D(-Step * Way, -M[Index] * Step);
			}
		}
	}

	void SolveBSpline(const TArray<FVector2D>& Points, bool bPeriodic, double Blend, FRunResult& Out)
	{
		const int32 Count = Points.Num();
		const int32 Segments = bPeriodic ? Count : Count - 1;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// On the curve: a sixth of each neighbour and two thirds of the point (an open run's ends stay where they are).
			const FVector2D OnCurve = (PointAt(Points, bPeriodic, Index - 1) + Points[Index] * 4.0 + PointAt(Points, bPeriodic, Index + 1)) / 6.0;
			Out.Positions[Index] = Points[Index] + (OnCurve - Points[Index]) * Blend;
		}
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const int32 Next = (Segment + 1) % Count;
			const FVector2D C1 = (Points[Segment] * 2.0 + Points[Next]) / 3.0;
			const FVector2D C2 = (Points[Segment] + Points[Next] * 2.0) / 3.0;
			Out.Outs[Segment] = (C1 - Out.Positions[Segment]) * Blend;
			Out.Ins[Next] = (C2 - Out.Positions[Next]) * Blend;
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Fitting (Schneider, "An Algorithm for Automatically Fitting Digitized Curves", Graphics Gems, 1990)
	// ---------------------------------------------------------------------------------------------

	struct FCubic
	{
		FVector2D P[4];
	};

	double B0(double U) { const double V = 1.0 - U; return V * V * V; }
	double B1(double U) { const double V = 1.0 - U; return 3.0 * U * V * V; }
	double B2(double U) { const double V = 1.0 - U; return 3.0 * U * U * V; }
	double B3(double U) { return U * U * U; }

	FVector2D Evaluate(const FCubic& Cubic, double U)
	{
		return Cubic.P[0] * B0(U) + Cubic.P[1] * B1(U) + Cubic.P[2] * B2(U) + Cubic.P[3] * B3(U);
	}

	/** Where each point falls along the stretch, by the distance walked from its first point (0 to 1). */
	void ChordParameters(const TArray<FVector2D>& Points, int32 First, int32 Last, TArray<double>& OutU)
	{
		OutU.SetNum(Last - First + 1);
		OutU[0] = 0.0;
		for (int32 Index = First + 1; Index <= Last; ++Index)
		{
			OutU[Index - First] = OutU[Index - First - 1] + (Points[Index] - Points[Index - 1]).Size();
		}
		const double Total = OutU.Last();
		for (double& U : OutU)
		{
			U = Total > Tiny ? U / Total : 0.0;
		}
	}

	/** The least-squares Bezier from Points[First] to Points[Last] with its handles along the given tangents (only their lengths free). */
	FCubic GenerateBezier(const TArray<FVector2D>& Points, int32 First, int32 Last, const TArray<double>& U, const FVector2D& Tangent1, const FVector2D& Tangent2)
	{
		const FVector2D Start = Points[First];
		const FVector2D End = Points[Last];
		double C00 = 0.0, C01 = 0.0, C11 = 0.0, X0 = 0.0, X1 = 0.0;
		for (int32 Index = 0; Index < U.Num(); ++Index)
		{
			const FVector2D A0 = Tangent1 * B1(U[Index]);
			const FVector2D A1 = Tangent2 * B2(U[Index]);
			C00 += FVector2D::DotProduct(A0, A0);
			C01 += FVector2D::DotProduct(A0, A1);
			C11 += FVector2D::DotProduct(A1, A1);
			const FVector2D Off = Points[First + Index] - (Start * (B0(U[Index]) + B1(U[Index])) + End * (B2(U[Index]) + B3(U[Index])));
			X0 += FVector2D::DotProduct(A0, Off);
			X1 += FVector2D::DotProduct(A1, Off);
		}
		const double Determinant = C00 * C11 - C01 * C01;
		double AlphaLeft = FMath::Abs(Determinant) > 1e-12 ? (X0 * C11 - X1 * C01) / Determinant : 0.0;
		double AlphaRight = FMath::Abs(Determinant) > 1e-12 ? (C00 * X1 - C01 * X0) / Determinant : 0.0;

		// Handles that come out backwards, vanishingly short or absurdly long: a third of the chord each, as a fallback.
		const double Chord = (End - Start).Size();
		const double Epsilon = 1e-6 * Chord;
		if (AlphaLeft < Epsilon || AlphaRight < Epsilon || AlphaLeft > Chord * 3.0 || AlphaRight > Chord * 3.0)
		{
			AlphaLeft = Chord / 3.0;
			AlphaRight = Chord / 3.0;
		}
		FCubic Cubic;
		Cubic.P[0] = Start;
		Cubic.P[1] = Start + Tangent1 * AlphaLeft;
		Cubic.P[2] = End + Tangent2 * AlphaRight;
		Cubic.P[3] = End;
		return Cubic;
	}

	/** The worst squared distance of a point from the Bezier at its parameter, and which point. */
	double MaxError(const TArray<FVector2D>& Points, int32 First, int32 Last, const FCubic& Cubic, const TArray<double>& U, int32& OutSplit)
	{
		double Worst = 0.0;
		OutSplit = (First + Last) / 2;
		for (int32 Index = First + 1; Index < Last; ++Index)
		{
			const double Distance = (Evaluate(Cubic, U[Index - First]) - Points[Index]).SizeSquared();
			if (Distance >= Worst)
			{
				Worst = Distance;
				OutSplit = Index;
			}
		}
		return Worst;
	}

	/** Each parameter moved toward the place on the Bezier nearest its point (a Newton step). */
	void Reparameterize(const TArray<FVector2D>& Points, int32 First, const FCubic& Cubic, TArray<double>& InOutU)
	{
		const FVector2D Q1[3] = { (Cubic.P[1] - Cubic.P[0]) * 3.0, (Cubic.P[2] - Cubic.P[1]) * 3.0, (Cubic.P[3] - Cubic.P[2]) * 3.0 };
		const FVector2D Q2[2] = { (Q1[1] - Q1[0]) * 2.0, (Q1[2] - Q1[1]) * 2.0 };
		for (int32 Index = 0; Index < InOutU.Num(); ++Index)
		{
			const double U = InOutU[Index];
			const double V = 1.0 - U;
			const FVector2D Off = Evaluate(Cubic, U) - Points[First + Index];
			const FVector2D D1 = Q1[0] * (V * V) + Q1[1] * (2.0 * U * V) + Q1[2] * (U * U);
			const FVector2D D2 = Q2[0] * V + Q2[1] * U;
			const double Denominator = FVector2D::DotProduct(D1, D1) + FVector2D::DotProduct(Off, D2);
			if (FMath::Abs(Denominator) > 1e-12)
			{
				InOutU[Index] = FMath::Clamp(U - FVector2D::DotProduct(Off, D1) / Denominator, 0.0, 1.0);
			}
		}
	}

	/** Which way the line leaves Points[Index] toward Step (+1 forward, -1 back): toward the first point at least Reach away (or the last there is). */
	FVector2D TangentAt(const TArray<FVector2D>& Points, int32 Index, int32 Step, int32 Limit, double Reach)
	{
		FVector2D Toward = Points[Index];
		for (int32 Walk = Index + Step; Step > 0 ? Walk <= Limit : Walk >= Limit; Walk += Step)
		{
			Toward = Points[Walk];
			if ((Toward - Points[Index]).Size() >= Reach)
			{
				break;
			}
		}
		return (Toward - Points[Index]).GetSafeNormal();
	}

	void FitCubic(const TArray<FVector2D>& Points, int32 First, int32 Last, const FVector2D& Tangent1, const FVector2D& Tangent2, double ErrorSquared, int32 Depth, TArray<FCubic>& Out)
	{
		const int32 Count = Last - First + 1;
		if (Count <= 2 || Depth > 32)
		{
			const double Third = (Points[Last] - Points[First]).Size() / 3.0;
			FCubic Cubic;
			Cubic.P[0] = Points[First];
			Cubic.P[1] = Points[First] + Tangent1 * Third;
			Cubic.P[2] = Points[Last] + Tangent2 * Third;
			Cubic.P[3] = Points[Last];
			Out.Add(Cubic);
			return;
		}

		TArray<double> U;
		ChordParameters(Points, First, Last, U);
		FCubic Cubic = GenerateBezier(Points, First, Last, U, Tangent1, Tangent2);
		int32 Split = 0;
		double Error = MaxError(Points, First, Last, Cubic, U, Split);
		if (Error < ErrorSquared)
		{
			Out.Add(Cubic);
			return;
		}
		// Close: a few Newton steps on where each point falls along it often bring it within the tolerance.
		if (Error < ErrorSquared * 16.0)
		{
			for (int32 Iteration = 0; Iteration < 4; ++Iteration)
			{
				Reparameterize(Points, First, Cubic, U);
				Cubic = GenerateBezier(Points, First, Last, U, Tangent1, Tangent2);
				Error = MaxError(Points, First, Last, Cubic, U, Split);
				if (Error < ErrorSquared)
				{
					Out.Add(Cubic);
					return;
				}
			}
		}

		// Split at the worst point, smooth through it.
		Split = FMath::Clamp(Split, First + 1, Last - 1);
		FVector2D Center = (Points[Split - 1] - Points[Split + 1]).GetSafeNormal();
		if (Center.IsNearlyZero())
		{
			Center = (Points[Split - 1] - Points[Split]).GetSafeNormal();
		}
		FitCubic(Points, First, Split, Tangent1, Center, ErrorSquared, Depth + 1, Out);
		FitCubic(Points, Split, Last, -Center, Tangent2, ErrorSquared, Depth + 1, Out);
	}
}

namespace WidgetCurveFitting
{
	using namespace CurveFittingPrivate;

	void SmoothBalanced(FWidgetShapeContour& Contour, float Smoothness)
	{
		const int32 Count = Contour.Vertices.Num();
		if (Count < 2)
		{
			return;
		}
		const float Amount = FMath::Max(Smoothness, 0.f);
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

	void SmoothCatmullRom(FWidgetShapeContour& Contour, float Smoothness, float Alpha)
	{
		const double ClampedAlpha = FMath::Clamp(static_cast<double>(Alpha), 0.0, 1.0);
		SolveRuns(Contour, FMath::Max(static_cast<double>(Smoothness), 0.0), [ClampedAlpha](const TArray<FVector2D>& Points, bool bPeriodic, FRunResult& Out)
		{
			SolveCatmullRom(Points, bPeriodic, ClampedAlpha, Out);
		});
	}

	void SmoothNatural(FWidgetShapeContour& Contour, float Smoothness)
	{
		SolveRuns(Contour, FMath::Max(static_cast<double>(Smoothness), 0.0), [](const TArray<FVector2D>& Points, bool bPeriodic, FRunResult& Out)
		{
			SolveNatural(Points, bPeriodic, Out);
		});
	}

	void SmoothMonotone(FWidgetShapeContour& Contour, float Smoothness)
	{
		SolveRuns(Contour, FMath::Clamp(static_cast<double>(Smoothness), 0.0, 1.0), [](const TArray<FVector2D>& Points, bool bPeriodic, FRunResult& Out)
		{
			SolveMonotone(Points, bPeriodic, Out);
		});
	}

	void MakeBSpline(FWidgetShapeContour& Contour, float Smoothness)
	{
		const double Blend = FMath::Clamp(static_cast<double>(Smoothness), 0.0, 1.0);
		SolveRuns(Contour, 1.0, [Blend](const TArray<FVector2D>& Points, bool bPeriodic, FRunResult& Out)
		{
			SolveBSpline(Points, bPeriodic, Blend, Out);
		});
	}

	void Simplify(const TArray<FVector2D>& Points, double Tolerance, const TArray<int32>& Keep, TArray<int32>& OutKept)
	{
		OutKept.Reset();
		const int32 Count = Points.Num();
		if (Count <= 2)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				OutKept.Add(Index);
			}
			return;
		}
		TArray<bool> Kept;
		Kept.Init(false, Count);
		Kept[0] = true;
		Kept[Count - 1] = true;
		for (const int32 Index : Keep)
		{
			if (Kept.IsValidIndex(Index))
			{
				Kept[Index] = true;
			}
		}

		// Between every two points kept so far, the farthest point off their line is kept while it is farther than Tolerance.
		TArray<TPair<int32, int32>> Stack;
		int32 From = 0;
		for (int32 Index = 1; Index < Count; ++Index)
		{
			if (Kept[Index])
			{
				Stack.Emplace(From, Index);
				From = Index;
			}
		}
		while (Stack.Num() > 0)
		{
			const TPair<int32, int32> Span = Stack.Pop();
			double Worst = -1.0;
			int32 WorstIndex = INDEX_NONE;
			for (int32 Index = Span.Key + 1; Index < Span.Value; ++Index)
			{
				const double Distance = DistanceToSegment(Points[Index], Points[Span.Key], Points[Span.Value]);
				if (Distance > Worst)
				{
					Worst = Distance;
					WorstIndex = Index;
				}
			}
			if (WorstIndex != INDEX_NONE && Worst > Tolerance)
			{
				Kept[WorstIndex] = true;
				Stack.Emplace(Span.Key, WorstIndex);
				Stack.Emplace(WorstIndex, Span.Value);
			}
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Kept[Index])
			{
				OutKept.Add(Index);
			}
		}
	}

	void FindCorners(const TArray<FVector2D>& Points, double CornerAngle, double Window, TArray<int32>& OutCorners)
	{
		OutCorners.Reset();
		const int32 Count = Points.Num();
		TArray<double> Turn;
		Turn.Init(0.0, Count);
		for (int32 Index = 1; Index < Count - 1; ++Index)
		{
			int32 Back = Index - 1;
			while (Back > 0 && (Points[Index] - Points[Back]).Size() < Window)
			{
				--Back;
			}
			int32 Ahead = Index + 1;
			while (Ahead < Count - 1 && (Points[Ahead] - Points[Index]).Size() < Window)
			{
				++Ahead;
			}
			const FVector2D In = (Points[Index] - Points[Back]).GetSafeNormal();
			const FVector2D Out = (Points[Ahead] - Points[Index]).GetSafeNormal();
			if (In.IsNearlyZero() || Out.IsNearlyZero())
			{
				continue;
			}
			Turn[Index] = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(In, Out), -1.0, 1.0)));
		}

		// The sharpest point of each stretch that turns more than CornerAngle.
		int32 Index = 1;
		while (Index < Count - 1)
		{
			if (Turn[Index] < CornerAngle)
			{
				++Index;
				continue;
			}
			int32 Best = Index;
			while (Index < Count - 1 && Turn[Index] >= CornerAngle)
			{
				if (Turn[Index] > Turn[Best])
				{
					Best = Index;
				}
				++Index;
			}
			OutCorners.Add(Best);
		}
	}

	void RemoveNearDuplicates(TArray<FVector2D>& Points, double MinDistance, bool bClosed)
	{
		if (Points.Num() < 2)
		{
			return;
		}
		TArray<FVector2D> Kept;
		Kept.Reserve(Points.Num());
		Kept.Add(Points[0]);
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const bool bLast = Index == Points.Num() - 1;
			if ((Points[Index] - Kept.Last()).Size() >= MinDistance)
			{
				Kept.Add(Points[Index]);
			}
			else if (bLast && Kept.Num() > 1)
			{
				// The line's real end wins over the point just before it.
				Kept.Last() = Points[Index];
			}
		}
		while (bClosed && Kept.Num() > 2 && (Kept.Last() - Kept[0]).Size() < MinDistance)
		{
			Kept.Pop();
		}
		Points = MoveTemp(Kept);
	}

	void SmoothStroke(TArray<FVector2D>& Points, double Radius)
	{
		const int32 Count = Points.Num();
		if (Count < 3 || Radius <= Tiny)
		{
			return;
		}
		TArray<double> Along;
		Along.SetNum(Count);
		Along[0] = 0.0;
		for (int32 Index = 1; Index < Count; ++Index)
		{
			Along[Index] = Along[Index - 1] + (Points[Index] - Points[Index - 1]).Size();
		}
		const TArray<FVector2D> Source = Points;
		const double Reach = Radius * 2.0;
		const double TwoSigmaSquared = 2.0 * Radius * Radius;
		for (int32 Index = 1; Index < Count - 1; ++Index)
		{
			// Never further from an end than the end is: the line keeps its length and doesn't pull away from where it started.
			const double Limit = FMath::Min(Reach, FMath::Min(Along[Index], Along[Count - 1] - Along[Index]));
			FVector2D Sum = FVector2D::ZeroVector;
			double Weights = 0.0;
			for (int32 Other = Index; Other >= 0 && Along[Index] - Along[Other] <= Limit; --Other)
			{
				const double Gap = Along[Index] - Along[Other];
				const double Weight = FMath::Exp(-Gap * Gap / TwoSigmaSquared);
				Sum += Source[Other] * Weight;
				Weights += Weight;
			}
			for (int32 Other = Index + 1; Other < Count && Along[Other] - Along[Index] <= Limit; ++Other)
			{
				const double Gap = Along[Other] - Along[Index];
				const double Weight = FMath::Exp(-Gap * Gap / TwoSigmaSquared);
				Sum += Source[Other] * Weight;
				Weights += Weight;
			}
			if (Weights > 0.0)
			{
				Points[Index] = Sum / Weights;
			}
		}
	}

	void FitBeziers(const TArray<FVector2D>& InPoints, double Tolerance, double CornerAngle, bool bClosed, FWidgetShapeContour& Out)
	{
		Out.Vertices.Reset();
		Out.bClosed = false;
		TArray<FVector2D> Points = InPoints;
		RemoveNearDuplicates(Points, FMath::Max(Tolerance * 0.1, 1e-3), bClosed);
		const int32 Count = Points.Num();
		if (Count == 0)
		{
			return;
		}
		if (Count == 1)
		{
			Out.Vertices.Add(FWidgetShapeVertex(Points[0]));
			return;
		}
		bClosed = bClosed && Count >= 3;
		const double Reach = FMath::Max(Tolerance * 3.0, 4.0);

		TArray<int32> Corners;
		FindCorners(Points, CornerAngle, Reach, Corners);
		if (bClosed)
		{
			// A corner where the line started and ended isn't seen from the line's own ends: looked for again from halfway round.
			const int32 Half = Count / 2;
			TArray<FVector2D> FromHalf;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FromHalf.Add(Points[(Half + Index) % Count]);
			}
			TArray<int32> More;
			FindCorners(FromHalf, CornerAngle, Reach, More);
			for (const int32 Corner : More)
			{
				const int32 Original = (Corner + Half) % Count;
				const bool bNear = Corners.ContainsByPredicate([&Points, Original, Reach](int32 Known) { return (Points[Known] - Points[Original]).Size() < Reach; });
				if (!bNear)
				{
					Corners.Add(Original);
				}
			}
			Corners.Sort();
		}

		// A closed line is fitted as an open one from a corner (or its first point) back round to the same point.
		// Only a closed line with no corner at all is smooth where it closes; one with corners starts at its first corner.
		const bool bSmoothSeam = bClosed && Corners.Num() == 0;
		if (bClosed)
		{
			const int32 Start = Corners.Num() > 0 ? Corners[0] : 0;
			TArray<FVector2D> Rotated;
			for (int32 Index = 0; Index <= Count; ++Index)
			{
				Rotated.Add(Points[(Start + Index) % Count]);
			}
			for (int32& Corner : Corners)
			{
				Corner = (Corner - Start + Count) % Count;
			}
			Corners.RemoveAll([](int32 Corner) { return Corner == 0; });
			Points = MoveTemp(Rotated);
		}
		const int32 Last = Points.Num() - 1;
		const double ErrorSquared = Tolerance * Tolerance;

		TArray<int32> Breaks;
		Breaks.Add(0);
		Breaks.Append(Corners);
		Breaks.Add(Last);

		// Smooth through the seam of a closed line: the same tangent leaving its start and arriving back at it.
		FVector2D SeamTangent = FVector2D::ZeroVector;
		if (bSmoothSeam)
		{
			SeamTangent = (TangentAt(Points, 0, 1, Last, Reach) - TangentAt(Points, Last, -1, 0, Reach)).GetSafeNormal();
		}

		TArray<FCubic> Cubics;
		TArray<bool> CornerAfter;
		for (int32 Piece = 0; Piece + 1 < Breaks.Num(); ++Piece)
		{
			const int32 First = Breaks[Piece];
			const int32 End = Breaks[Piece + 1];
			if (End <= First)
			{
				continue;
			}
			FVector2D Tangent1 = TangentAt(Points, First, 1, End, Reach);
			FVector2D Tangent2 = TangentAt(Points, End, -1, First, Reach);
			if (!SeamTangent.IsNearlyZero())
			{
				if (First == 0)
				{
					Tangent1 = SeamTangent;
				}
				if (End == Last)
				{
					Tangent2 = -SeamTangent;
				}
			}
			const int32 Before = Cubics.Num();
			FitCubic(Points, First, End, Tangent1, Tangent2, ErrorSquared, 0, Cubics);
			for (int32 Index = Before; Index < Cubics.Num(); ++Index)
			{
				CornerAfter.Add(Index == Cubics.Num() - 1 && End != Last);
			}
		}
		if (Cubics.Num() == 0)
		{
			return;
		}

		// A vertex at every segment's start (and the open line's end), its handles from the segments either side.
		const int32 Segments = Cubics.Num();
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			FWidgetShapeVertex Vertex(Cubics[Index].P[0]);
			Vertex.OutHandle = Cubics[Index].P[1] - Cubics[Index].P[0];
			const bool bHasBefore = Index > 0 || bClosed;
			if (bHasBefore)
			{
				const FCubic& Previous = Cubics[(Index - 1 + Segments) % Segments];
				Vertex.InHandle = Previous.P[2] - Cubics[Index].P[0];
			}
			const bool bCorner = Index > 0 ? CornerAfter[Index - 1] : (bClosed && !bSmoothSeam);
			Vertex.Type = bCorner ? EWidgetShapeVertexType::Disconnected : EWidgetShapeVertexType::Asymmetric;
			Out.Vertices.Add(Vertex);
		}
		if (!bClosed)
		{
			FWidgetShapeVertex End(Cubics.Last().P[3]);
			End.Type = EWidgetShapeVertexType::Asymmetric;
			End.InHandle = Cubics.Last().P[2] - Cubics.Last().P[3];
			Out.Vertices.Add(End);
		}
		Out.bClosed = bClosed;
	}

	FVector2D EvaluateCubic(const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, double T)
	{
		FCubic Cubic;
		Cubic.P[0] = P0;
		Cubic.P[1] = P1;
		Cubic.P[2] = P2;
		Cubic.P[3] = P3;
		return Evaluate(Cubic, T);
	}

	double DistanceToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double LengthSquared = AB.SizeSquared();
		const double Along = LengthSquared > 1e-12 ? FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
		return (A + AB * Along - Point).Size();
	}
}
