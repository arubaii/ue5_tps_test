// Copyright AR GameSystems 2026. All rights reserved.

#include "ShapeBox.h"

#include "Components/PanelSlot.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Input/HittestGrid.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/ArrangedWidget.h"
#include "Layout/WidgetPath.h"
#include "Misc/App.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "UObject/GCObject.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SVirtualWindow.h"

#define LOCTEXT_NAMESPACE "ShapeBox"

// =====================================================================================================
// Helpers
// =====================================================================================================

DEFINE_LOG_CATEGORY_STATIC(LogShapeBox, Log, All);

namespace ShapeBoxPrivate
{
	static TAutoConsoleVariable<int32> CVarDebugContentBox(
		TEXT("ShapeBox.DebugContentBox"),
		0,
		TEXT("1: every Shape Box draws where it arranges its content (magenta) and where that content was last painted (cyan), and logs both to LogShapeBox when they change - to see why an outline (the designer's) is somewhere the content isn't drawn."),
		ECVF_Default);

	constexpr double Tolerance = 0.01;
	// Short of edge-on (90): a Flip Card turns its faces all the way round through this. The Details still stop Angle at 80.
	constexpr double MaxAngleDegrees = 89.5;
	constexpr double MaxWrapDegrees = 180.0;
	constexpr double MaxPerspectiveFov = 90.0;

	/** The smallest content box the shape math will divide by. An Inset that swallows the box leaves this much. */
	constexpr double MinContentSize = 0.01;

	/** Transparent texel border the warped edges filter into - the box's anti-aliasing. */
	constexpr int32 FeatherTexels = 2;
	constexpr int32 MaxRenderTargetDimension = 4096;

	/** Render mesh: a vertex every ~16 units, curves accurate to a quarter unit. */
	constexpr double RenderSpacing = 16.0;
	constexpr double RenderTolerance = 0.25;
	constexpr int32 MaxRenderSegments = 64;

	/** Hit cells: coarser, accurate to about a unit. Each cell is one entry in the hit-test grid. */
	constexpr double HitSpacing = 64.0;
	constexpr double HitTolerance = 1.0;
	constexpr int32 MaxHitSegments = 16;

	/** Hit cells a chamfer cuts through are re-tiled at about this spacing, so the cut edge is followed closely. */
	constexpr double HitCutSpacing = 6.0;
	constexpr int32 MaxHitCutSubdivisions = 8;

	/** Magnified areas (the near side of an angled box) get up to this much extra texture resolution. */
	constexpr double MaxAutoMagnification = 2.0;

	/**
	 * A chamfer's two legs. One leg left at zero (or below) follows the other, so a single value cuts a 45-degree corner - the
	 * way a chamfer is usually given - instead of nothing until both are typed in.
	 */
	FVector2D GetChamferLegs(const FShapeBoxCorner& Corner)
	{
		const double LegX = FMath::Max(Corner.Size.X, 0.0);
		const double LegY = FMath::Max(Corner.Size.Y, 0.0);
		return FVector2D(LegX > Tolerance ? LegX : LegY, LegY > Tolerance ? LegY : LegX);
	}

	bool IsChamferActive(const FShapeBoxCorner& Corner)
	{
		if (Corner.Style != EShapeBoxCornerStyle::Chamfer)
		{
			return false;
		}
		const FVector2D Legs = GetChamferLegs(Corner);
		return Legs.X > Tolerance && Legs.Y > Tolerance;
	}

	FVector2D GetBevel(const FShapeBoxCorner& Corner)
	{
		return Corner.Style == EShapeBoxCornerStyle::Bevel ? Corner.Size : FVector2D::ZeroVector;
	}

	/**
	 * How far each corner moves for its bevel, in TL, TR, BR, BL order. A bevel slides the corner along its own edges,
	 * so the cut always runs along the edge rather than off it. Never depends on the box's size, which is what lets
	 * IsAffine answer without one.
	 */
	void ComputeBevelOffsets(const FShapeBoxShape& Shape, FVector2D OutOffsets[4])
	{
		const FVector2D AlongTop(1.0, 0.0);   // top and bottom edges, pointing right
		const FVector2D AlongSide(0.0, 1.0);  // left and right sides, pointing down

		const FVector2D TopLeft = GetBevel(Shape.Corners.TopLeft);
		const FVector2D TopRight = GetBevel(Shape.Corners.TopRight);
		const FVector2D BottomRight = GetBevel(Shape.Corners.BottomRight);
		const FVector2D BottomLeft = GetBevel(Shape.Corners.BottomLeft);

		// Positive always moves the corner into the box.
		OutOffsets[0] = AlongTop * TopLeft.X + AlongSide * TopLeft.Y;
		OutOffsets[1] = -AlongTop * TopRight.X + AlongSide * TopRight.Y;
		OutOffsets[2] = -AlongTop * BottomRight.X - AlongSide * BottomRight.Y;
		OutOffsets[3] = AlongTop * BottomLeft.X - AlongSide * BottomLeft.Y;
	}

	/** One chamfer, as a straight line in content units (0..Size across the extruded/inset box, before any warp). */
	struct FChamferCut
	{
		/** A point on the cut. */
		FVector2D Point = FVector2D::ZeroVector;

		/** Unit normal pointing into the part that is kept. */
		FVector2D Normal = FVector2D::ZeroVector;

		/** Signed distance from the cut, in content units: positive is kept, negative is cut away. */
		double Distance(const FVector2D& Content) const
		{
			return FVector2D::DotProduct(Content - Point, Normal);
		}
	};

	/**
	 * Evaluates an FShapeBoxShape for one layout size. U, V run 0..1 across the content box - the layout rect with
	 * Extrude/Inset applied - and values outside extrapolate smoothly, for the feather border. Returns a position in the
	 * box's local space.
	 *
	 * Stage 1 (flat): the content box, with beveled corners joined by optionally bowed edges, filled as a Coons
	 * patch, plus Bend.
	 * Stage 2 (depth): the flat result is wrapped onto a cylinder, turned by Angle about the box center and projected,
	 * so the far side shrinks and everything on it foreshortens correctly.
	 * Stage 3 (roll): the result is spun by Rotation about the center.
	 *
	 * Chamfers are not part of the mapping: they are straight cuts in content space, clipped out of the mesh and the
	 * hit cells.
	 */
	struct FShapeEvaluator
	{
		/** The content box in local space. */
		FVector2D Origin = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		FVector2D Center = FVector2D::ZeroVector;

		FVector2D TL, TR, BR, BL;
		FVector2D TopBow, RightBow, BottomBow, LeftBow;
		FVector2D Bend;

		bool bDepth = false;
		bool bWrapX = false;
		bool bWrapY = false;
		double WrapRadiusX = 0.0;
		double WrapRadiusY = 0.0;
		double SinYaw = 0.0, CosYaw = 1.0;
		double SinPitch = 0.0, CosPitch = 1.0;
		bool bPerspective = false;
		double Focal = 0.0;

		bool bRoll = false;
		double SinRoll = 0.0, CosRoll = 1.0;

		FChamferCut Cuts[4];
		int32 NumCuts = 0;

		FShapeEvaluator(const FShapeBoxShape& Shape, const FVector2D& LayoutSize)
		{
			const FMargin Edges = Shape.GetEdgeOffsets();
			Origin = FVector2D(-Edges.Left, -Edges.Top);
			Size = FVector2D(
				FMath::Max(MinContentSize, LayoutSize.X + Edges.Left + Edges.Right),
				FMath::Max(MinContentSize, LayoutSize.Y + Edges.Top + Edges.Bottom));
			Center = Origin + Size * 0.5;

			FVector2D Bevels[4];
			ComputeBevelOffsets(Shape, Bevels);

			TL = FVector2D(Origin.X, Origin.Y) + Bevels[0];
			TR = FVector2D(Origin.X + Size.X, Origin.Y) + Bevels[1];
			BR = FVector2D(Origin.X + Size.X, Origin.Y + Size.Y) + Bevels[2];
			BL = FVector2D(Origin.X, Origin.Y + Size.Y) + Bevels[3];

			// Edges walk clockwise, so the right-hand normal of each points out of the box.
			TopBow = OutwardNormal(TL, TR) * static_cast<double>(Shape.EdgeBow.Top);
			RightBow = OutwardNormal(TR, BR) * static_cast<double>(Shape.EdgeBow.Right);
			BottomBow = OutwardNormal(BR, BL) * static_cast<double>(Shape.EdgeBow.Bottom);
			LeftBow = OutwardNormal(BL, TL) * static_cast<double>(Shape.EdgeBow.Left);

			Bend = Shape.Bend;

			// Wrap: the arc spans the whole side, so the radius is side length over arc.
			const double WrapX = FMath::DegreesToRadians(FMath::Clamp(Shape.Wrap.X, -MaxWrapDegrees, MaxWrapDegrees));
			const double WrapY = FMath::DegreesToRadians(FMath::Clamp(Shape.Wrap.Y, -MaxWrapDegrees, MaxWrapDegrees));
			bWrapX = FMath::Abs(WrapX) > 1e-4;
			bWrapY = FMath::Abs(WrapY) > 1e-4;
			WrapRadiusX = bWrapX ? Size.X / WrapX : 0.0;
			WrapRadiusY = bWrapY ? Size.Y / WrapY : 0.0;

			FMath::SinCos(&SinYaw, &CosYaw, FMath::DegreesToRadians(FMath::Clamp(Shape.Angle.X, -MaxAngleDegrees, MaxAngleDegrees)));
			FMath::SinCos(&SinPitch, &CosPitch, FMath::DegreesToRadians(FMath::Clamp(Shape.Angle.Y, -MaxAngleDegrees, MaxAngleDegrees)));

			// Focal length chosen so an unturned box projects at exactly its content size.
			const double Fov = FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(Shape.Perspective), 0.0, 1.0) * MaxPerspectiveFov);
			bPerspective = Fov > 1e-3;
			Focal = bPerspective ? 0.5 * FMath::Max(Size.X, Size.Y) / FMath::Tan(Fov * 0.5) : 0.0;

			bDepth = bWrapX || bWrapY || !Shape.Angle.IsNearlyZero(Tolerance);

			bRoll = !FMath::IsNearlyZero(static_cast<double>(Shape.Rotation), Tolerance);
			if (bRoll)
			{
				FMath::SinCos(&SinRoll, &CosRoll, FMath::DegreesToRadians(static_cast<double>(Shape.Rotation)));
			}

			// Each leg runs from the corner along one of its edges. Legs are clamped to the box so an oversized cut
			// can't cross to the far side.
			auto AddCut = [this](const FShapeBoxCorner& Corner, const FVector2D& CornerPoint, const FVector2D& AlongHorizontal, const FVector2D& AlongVertical)
			{
				if (!IsChamferActive(Corner))
				{
					return;
				}

				const FVector2D Legs = GetChamferLegs(Corner);
				const double LegX = FMath::Min(Legs.X, Size.X);
				const double LegY = FMath::Min(Legs.Y, Size.Y);
				const FVector2D P1 = CornerPoint + AlongHorizontal * LegX;
				const FVector2D P2 = CornerPoint + AlongVertical * LegY;
				const FVector2D Direction = (P2 - P1).GetSafeNormal();

				FChamferCut& Cut = Cuts[NumCuts++];
				Cut.Point = P1;
				Cut.Normal = FVector2D(-Direction.Y, Direction.X);
				if (Cut.Distance(CornerPoint) > 0.0)
				{
					Cut.Normal = -Cut.Normal;  // the corner itself is the part that goes
				}
			};

			AddCut(Shape.Corners.TopLeft, FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.0, 1.0));
			AddCut(Shape.Corners.TopRight, FVector2D(Size.X, 0.0), FVector2D(-1.0, 0.0), FVector2D(0.0, 1.0));
			AddCut(Shape.Corners.BottomRight, FVector2D(Size.X, Size.Y), FVector2D(-1.0, 0.0), FVector2D(0.0, -1.0));
			AddCut(Shape.Corners.BottomLeft, FVector2D(0.0, Size.Y), FVector2D(1.0, 0.0), FVector2D(0.0, -1.0));
		}

		static FVector2D OutwardNormal(const FVector2D& From, const FVector2D& To)
		{
			const FVector2D Direction = To - From;
			return FVector2D(Direction.Y, -Direction.X).GetSafeNormal();
		}

		FVector2D EvaluateFlat(double U, double V) const
		{
			// 1 at an edge's midpoint, 0 at its ends.
			const double ArcU = 4.0 * U * (1.0 - U);
			const double ArcV = 4.0 * V * (1.0 - V);

			const FVector2D Bilinear = TL * ((1.0 - U) * (1.0 - V)) + TR * (U * (1.0 - V)) + BL * ((1.0 - U) * V) + BR * (U * V);

			return Bilinear
				+ (TopBow * (1.0 - V) + BottomBow * V) * ArcU
				+ (LeftBow * (1.0 - U) + RightBow * U) * ArcV
				+ FVector2D(Bend.X * ArcV, Bend.Y * ArcU);
		}

		FVector2D Project(const FVector2D& FlatPoint) const
		{
			double X = FlatPoint.X - Center.X;
			double Y = FlatPoint.Y - Center.Y;
			double Z = 0.0;  // +Z is away from the viewer.

			// Wrap onto a cylinder. Arc length is preserved, so content folds away rather than stretching.
			if (bWrapX)
			{
				const double A = X / WrapRadiusX;
				Z += WrapRadiusX * (1.0 - FMath::Cos(A));
				X = WrapRadiusX * FMath::Sin(A);
			}
			if (bWrapY)
			{
				const double A = Y / WrapRadiusY;
				Z += WrapRadiusY * (1.0 - FMath::Cos(A));
				Y = WrapRadiusY * FMath::Sin(A);
			}

			// Yaw: positive brings +X toward the viewer.
			{
				const double RX = X * CosYaw + Z * SinYaw;
				const double RZ = -X * SinYaw + Z * CosYaw;
				X = RX;
				Z = RZ;
			}

			// Pitch: positive brings +Y (the bottom) toward the viewer.
			{
				const double RY = Y * CosPitch + Z * SinPitch;
				const double RZ = -Y * SinPitch + Z * CosPitch;
				Y = RY;
				Z = RZ;
			}

			if (bPerspective)
			{
				const double Scale = Focal / FMath::Max(Focal + Z, Focal * 0.05);
				X *= Scale;
				Y *= Scale;
			}

			return Center + FVector2D(X, Y);
		}

		FVector2D Roll(const FVector2D& Point) const
		{
			// Slate's Y points down, so this turns clockwise on screen for a positive angle.
			const FVector2D Offset = Point - Center;
			return Center + FVector2D(Offset.X * CosRoll - Offset.Y * SinRoll, Offset.X * SinRoll + Offset.Y * CosRoll);
		}

		FVector2D Evaluate(double U, double V) const
		{
			FVector2D Point = EvaluateFlat(U, V);
			if (bDepth)
			{
				Point = Project(Point);
			}
			if (bRoll)
			{
				Point = Roll(Point);
			}
			return Point;
		}

		bool HasCuts() const
		{
			return NumCuts > 0;
		}

		/** Distance to the nearest chamfer cut, in content units. Positive is kept. Huge when nothing is chamfered. */
		double CutDistance(const FVector2D& Content) const
		{
			double Nearest = TNumericLimits<double>::Max();
			for (int32 Index = 0; Index < NumCuts; ++Index)
			{
				Nearest = FMath::Min(Nearest, Cuts[Index].Distance(Content));
			}
			return Nearest;
		}

		/** Coverage at a content point: 1 inside, 0 cut away, a linear ramp across 2 * Ramp either side of each cut. */
		float CutCoverage(const FVector2D& Content, double Ramp) const
		{
			double Coverage = 1.0;
			for (int32 Index = 0; Index < NumCuts; ++Index)
			{
				Coverage *= FMath::Clamp((Cuts[Index].Distance(Content) + Ramp) / (2.0 * Ramp), 0.0, 1.0);
			}
			return static_cast<float>(Coverage);
		}
	};

	/** Sutherland-Hodgman against one side of a cut: keeps the part with Distance - Threshold >= 0 (bKeepAbove) or < 0. */
	void ClipPolygon(const TArray<FVector2D>& In, const FChamferCut& Cut, double Threshold, bool bKeepAbove, TArray<FVector2D>& Out)
	{
		Out.Reset();
		const int32 Count = In.Num();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D& Current = In[Index];
			const FVector2D& Next = In[(Index + 1) % Count];
			const double DistanceCurrent = Cut.Distance(Current) - Threshold;
			const double DistanceNext = Cut.Distance(Next) - Threshold;
			const bool bCurrentInside = bKeepAbove ? DistanceCurrent >= 0.0 : DistanceCurrent < 0.0;
			const bool bNextInside = bKeepAbove ? DistanceNext >= 0.0 : DistanceNext < 0.0;

			if (bCurrentInside)
			{
				Out.Add(Current);
			}
			if (bCurrentInside != bNextInside)
			{
				const double T = DistanceCurrent / (DistanceCurrent - DistanceNext);
				Out.Add(Current + (Next - Current) * T);
			}
		}
	}

	/** Segments along one axis: enough for the length at Spacing, and enough that curves and tapers stay within Tol. */
	int32 SegmentCount(double Length, double Curve, double Twist, double Spacing, double Tol, int32 MaxSegments)
	{
		double Count = FMath::CeilToDouble(Length / Spacing);
		Count = FMath::Max(Count, FMath::CeilToDouble(FMath::Sqrt(FMath::Abs(Curve) / Tol)));  // a curve's sag per segment is Curve / N^2
		Count = FMath::Max(Count, FMath::CeilToDouble(FMath::Sqrt(Twist / Tol)));
		return FMath::Clamp(static_cast<int32>(Count), 1, MaxSegments);
	}

	/** Measures how curved and tapered the final shape actually is, and picks a grid fine enough to follow it. */
	FIntPoint ComputeGrid(const FShapeEvaluator& E, double Spacing, double Tol, int32 MaxSegments)
	{
		const FVector2D P00 = E.Evaluate(0.0, 0.0), P10 = E.Evaluate(1.0, 0.0);
		const FVector2D P01 = E.Evaluate(0.0, 1.0), P11 = E.Evaluate(1.0, 1.0);
		const FVector2D Top = E.Evaluate(0.5, 0.0), Bottom = E.Evaluate(0.5, 1.0);
		const FVector2D Left = E.Evaluate(0.0, 0.5), Right = E.Evaluate(1.0, 0.5);
		const FVector2D Mid = E.Evaluate(0.5, 0.5);

		// Sag of each line from its chord: how far the midpoint strays from halfway between the ends.
		const double CurveAlongX = FMath::Max3(
			(Top - (P00 + P10) * 0.5).Size(),
			(Bottom - (P01 + P11) * 0.5).Size(),
			(Mid - (Left + Right) * 0.5).Size());
		const double CurveAlongY = FMath::Max3(
			(Left - (P00 + P01) * 0.5).Size(),
			(Right - (P10 + P11) * 0.5).Size(),
			(Mid - (Top + Bottom) * 0.5).Size());

		const double Twist = (P00 + P11 - P10 - P01).Size();
		const double LengthX = FMath::Max((P10 - P00).Size(), (P11 - P01).Size());
		const double LengthY = FMath::Max((P01 - P00).Size(), (P11 - P10).Size());

		return FIntPoint(
			SegmentCount(LengthX, CurveAlongX, Twist, Spacing, Tol, MaxSegments),
			SegmentCount(LengthY, CurveAlongY, Twist, Spacing, Tol, MaxSegments));
	}

	/** How much the shape enlarges its content at most, so the texture can be rendered sharp enough for it. */
	double ComputeMagnification(const FShapeEvaluator& E)
	{
		const FVector2D P00 = E.Evaluate(0.0, 0.0), P10 = E.Evaluate(1.0, 0.0);
		const FVector2D P01 = E.Evaluate(0.0, 1.0), P11 = E.Evaluate(1.0, 1.0);

		const double Magnification = FMath::Max(
			FMath::Max((P10 - P00).Size(), (P11 - P01).Size()) / E.Size.X,
			FMath::Max((P01 - P00).Size(), (P11 - P10).Size()) / E.Size.Y);

		return FMath::Clamp(Magnification, 1.0, MaxAutoMagnification);
	}

	/**
	 * The parallelogram mapping a FromSize box (origin at 0,0) onto the drawn shape, through its top-left, top-right
	 * and bottom-left corners. Only exact when the shape IsAffine.
	 */
	FSlateRenderTransform MakeAffineTransform(const FShapeEvaluator& Evaluator, const FVector2D& FromSize)
	{
		const double FromX = FMath::Max(FromSize.X, MinContentSize);
		const double FromY = FMath::Max(FromSize.Y, MinContentSize);

		const FVector2D Origin = Evaluator.Evaluate(0.0, 0.0);
		const FVector2D AxisX = (Evaluator.Evaluate(1.0, 0.0) - Origin) / FromX;
		const FVector2D AxisY = (Evaluator.Evaluate(0.0, 1.0) - Origin) / FromY;
		return FSlateRenderTransform(
			FMatrix2x2f(static_cast<float>(AxisX.X), static_cast<float>(AxisX.Y), static_cast<float>(AxisY.X), static_cast<float>(AxisY.Y)),
			FVector2f(Origin));
	}

	/** Vertex color for premultiplied, gamma-space content (what the widget renderer writes). */
	FColor MakePremultipliedColor(const FLinearColor& Tint)
	{
		const float Alpha = FMath::Clamp(Tint.A, 0.0f, 1.0f);
		FColor Encoded = FLinearColor(Tint.R, Tint.G, Tint.B, 1.0f).ToFColor(/*bSRGB=*/true);
		Encoded.R = static_cast<uint8>(FMath::RoundToInt(Encoded.R * Alpha));
		Encoded.G = static_cast<uint8>(FMath::RoundToInt(Encoded.G * Alpha));
		Encoded.B = static_cast<uint8>(FMath::RoundToInt(Encoded.B * Alpha));
		Encoded.A = static_cast<uint8>(FMath::RoundToInt(Alpha * 255.0f));
		return Encoded;
	}

	/** A premultiplied color scaled by coverage. Premultiplied, so every channel scales together. */
	FColor ScalePremultipliedColor(const FColor& Color, float Coverage)
	{
		if (Coverage >= 1.0f)
		{
			return Color;
		}
		const float Clamped = FMath::Clamp(Coverage, 0.0f, 1.0f);
		return FColor(
			static_cast<uint8>(FMath::RoundToInt(Color.R * Clamped)),
			static_cast<uint8>(FMath::RoundToInt(Color.G * Clamped)),
			static_cast<uint8>(FMath::RoundToInt(Color.B * Clamped)),
			static_cast<uint8>(FMath::RoundToInt(Color.A * Clamped)));
	}

	// ---- Reflection-driven blending (FShapeBoxShape::Lerp) ----

	void LerpStruct(const UScriptStruct* Struct, const void* A, const void* B, void* Out, float Alpha);

	void LerpProperty(const FProperty* Property, const void* ContainerA, const void* ContainerB, void* ContainerOut, float Alpha)
	{
		for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
		{
			const void* ValueA = Property->ContainerPtrToValuePtr<void>(ContainerA, ArrayIndex);
			const void* ValueB = Property->ContainerPtrToValuePtr<void>(ContainerB, ArrayIndex);
			void* ValueOut = Property->ContainerPtrToValuePtr<void>(ContainerOut, ArrayIndex);

			if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
			{
				FloatProperty->SetPropertyValue(ValueOut, FMath::Lerp(FloatProperty->GetPropertyValue(ValueA), FloatProperty->GetPropertyValue(ValueB), Alpha));
			}
			else if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
			{
				DoubleProperty->SetPropertyValue(ValueOut, FMath::Lerp(DoubleProperty->GetPropertyValue(ValueA), DoubleProperty->GetPropertyValue(ValueB), static_cast<double>(Alpha)));
			}
			else if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
			{
				const double Blended = FMath::Lerp(static_cast<double>(IntProperty->GetPropertyValue(ValueA)), static_cast<double>(IntProperty->GetPropertyValue(ValueB)), static_cast<double>(Alpha));
				IntProperty->SetPropertyValue(ValueOut, FMath::RoundToInt32(Blended));
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct == FShapeBoxCorner::StaticStruct())
				{
					// A corner's style and size only make sense together; blended as a unit.
					*static_cast<FShapeBoxCorner*>(ValueOut) = FShapeBoxCorner::Lerp(*static_cast<const FShapeBoxCorner*>(ValueA), *static_cast<const FShapeBoxCorner*>(ValueB), Alpha);
				}
				else
				{
					LerpStruct(StructProperty->Struct, ValueA, ValueB, ValueOut, Alpha);
				}
			}
			else
			{
				// Bools, enums, names, objects: nothing in between to blend through, so switch halfway.
				Property->CopySingleValue(ValueOut, Alpha < 0.5f ? ValueA : ValueB);
			}
		}
	}

	void LerpStruct(const UScriptStruct* Struct, const void* A, const void* B, void* Out, float Alpha)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			LerpProperty(*It, A, B, Out, Alpha);
		}
	}
}

// =====================================================================================================
// FShapeBoxCorner / FShapeBoxShape
// =====================================================================================================

FShapeBoxCorner FShapeBoxCorner::Lerp(const FShapeBoxCorner& A, const FShapeBoxCorner& B, float Alpha)
{
	using namespace ShapeBoxPrivate;

	FShapeBoxCorner Result;
	Result.Size = FMath::Lerp(A.Size, B.Size, static_cast<double>(Alpha));

	if (A.Style == B.Style)
	{
		Result.Style = A.Style;
	}
	else if (A.Size.IsNearlyZero(Tolerance))
	{
		// A was square anyway, so start out as B: the cut grows in B's style from nothing.
		Result.Style = B.Style;
	}
	else if (B.Size.IsNearlyZero(Tolerance))
	{
		// B ends square, so stay A until the end: the cut shrinks away in A's style.
		Result.Style = Alpha >= 1.0f ? B.Style : A.Style;
	}
	else
	{
		Result.Style = Alpha < 0.5f ? A.Style : B.Style;
	}

	return Result;
}

FMargin FShapeBoxShape::GetEdgeOffsets() const
{
	auto Positive = [](double Value) { return static_cast<float>(FMath::Max(Value, 0.0)); };

	// Slate's Y axis points down, so +Y (up) maps to the Top edge.
	FMargin Offsets;
	Offsets.Right = Positive(Extrude.X) - Positive(Inset.X);
	Offsets.Left = Positive(-Extrude.X) - Positive(-Inset.X);
	Offsets.Top = Positive(Extrude.Y) - Positive(Inset.Y);
	Offsets.Bottom = Positive(-Extrude.Y) - Positive(-Inset.Y);
	return Offsets;
}

bool FShapeBoxShape::HasChamfer() const
{
	using namespace ShapeBoxPrivate;

	return IsChamferActive(Corners.TopLeft)
		|| IsChamferActive(Corners.TopRight)
		|| IsChamferActive(Corners.BottomRight)
		|| IsChamferActive(Corners.BottomLeft);
}

bool FShapeBoxShape::IsAffine() const
{
	using namespace ShapeBoxPrivate;

	// A chamfer adds corners, and nothing with more than four corners is a parallelogram.
	if (HasChamfer())
	{
		return false;
	}

	// Bevels keep a parallelogram only if opposite corners move by matching amounts.
	FVector2D Bevels[4];
	ComputeBevelOffsets(*this, Bevels);
	const FVector2D Twist = Bevels[0] + Bevels[2] - Bevels[1] - Bevels[3];

	// Angle without perspective only foreshortens, which is still a parallelogram. Wrap never is. Rotation always is.
	const bool bDepthIsAffine = Wrap.IsNearlyZero(Tolerance)
		&& (Angle.IsNearlyZero(Tolerance) || Perspective <= static_cast<float>(Tolerance));

	return bDepthIsAffine
		&& Twist.IsNearlyZero(Tolerance)
		&& Bend.IsNearlyZero(Tolerance)
		&& FMath::IsNearlyZero(EdgeBow.Top, static_cast<float>(Tolerance))
		&& FMath::IsNearlyZero(EdgeBow.Right, static_cast<float>(Tolerance))
		&& FMath::IsNearlyZero(EdgeBow.Bottom, static_cast<float>(Tolerance))
		&& FMath::IsNearlyZero(EdgeBow.Left, static_cast<float>(Tolerance));
}

bool FShapeBoxShape::IsFlat() const
{
	using namespace ShapeBoxPrivate;

	const FMargin Edges = GetEdgeOffsets();
	const bool bNoEdgeOffsets = FMath::IsNearlyZero(Edges.Left, static_cast<float>(Tolerance))
		&& FMath::IsNearlyZero(Edges.Top, static_cast<float>(Tolerance))
		&& FMath::IsNearlyZero(Edges.Right, static_cast<float>(Tolerance))
		&& FMath::IsNearlyZero(Edges.Bottom, static_cast<float>(Tolerance));

	return IsAffine()
		&& bNoEdgeOffsets
		&& Angle.IsNearlyZero(Tolerance)
		&& Wrap.IsNearlyZero(Tolerance)
		&& FMath::IsNearlyZero(Rotation, static_cast<float>(Tolerance))
		&& GetBevel(Corners.TopLeft).IsNearlyZero(Tolerance)
		&& GetBevel(Corners.TopRight).IsNearlyZero(Tolerance)
		&& GetBevel(Corners.BottomRight).IsNearlyZero(Tolerance)
		&& GetBevel(Corners.BottomLeft).IsNearlyZero(Tolerance);
}

FVector2D FShapeBoxShape::Evaluate(const FVector2D& LayoutSize, const FVector2D& NormalizedPoint) const
{
	return ShapeBoxPrivate::FShapeEvaluator(*this, LayoutSize).Evaluate(NormalizedPoint.X, NormalizedPoint.Y);
}

FSlateRenderTransform FShapeBoxShape::MakeLayoutToShapeTransform(const FVector2D& LayoutSize) const
{
	return ShapeBoxPrivate::MakeAffineTransform(ShapeBoxPrivate::FShapeEvaluator(*this, LayoutSize), LayoutSize);
}

FShapeBoxShape FShapeBoxShape::Lerp(const FShapeBoxShape& A, const FShapeBoxShape& B, float Alpha)
{
	FShapeBoxShape Result = A;
	ShapeBoxPrivate::LerpStruct(StaticStruct(), &A, &B, &Result, Alpha);
	return Result;
}

bool FShapeBoxShape::Equals(const FShapeBoxShape& Other) const
{
	return StaticStruct()->CompareScriptStruct(this, &Other, PPF_None);
}

// =====================================================================================================
// Render resources
// =====================================================================================================

/** The texture and renderer. Destroyed via BeginCleanup: the render thread may still be using them. */
struct FShapeBoxRenderResources : public FDeferredCleanupInterface, public FGCObject
{
	FWidgetRenderer* WidgetRenderer = nullptr;
	TObjectPtr<UTextureRenderTarget2D> RenderTarget = nullptr;

	virtual ~FShapeBoxRenderResources() override
	{
		delete WidgetRenderer;
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(RenderTarget);
	}

	virtual FString GetReferencerName() const override
	{
		return TEXT("SShapeBox Render Resources");
	}
};

// =====================================================================================================
// Hit cell + hit-test path
// =====================================================================================================

/**
 * Invisible and draws nothing. While warped, the box tiles its drawn shape with these so the hit-test grid
 * sees the shape rather than the layout rect. Each forwards the pointer into the content through the
 * shared hit-test path, and arranges the content window as its child so paths rebuilt later (mouse
 * capture, focus) resolve the same way the hit test did.
 */
class SShapeBoxHitCell : public SWidget
{
public:
	SLATE_BEGIN_ARGS(SShapeBoxHitCell) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TWeakPtr<const SShapeBox>& InOwner)
	{
		Owner = InOwner;
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

	virtual FChildren* GetChildren() override
	{
		return &FNoChildren::NoChildrenInstance;
	}

	virtual void OnArrangeChildren(const FGeometry&, FArrangedChildren& ArrangedChildren) const override
	{
		if (const TSharedPtr<const SShapeBox> Pinned = Owner.Pin())
		{
			Pinned->ArrangeContentWindow(ArrangedChildren);
		}
	}

	virtual TOptional<FVirtualPointerPosition> TranslateMouseCoordinateForCustomHitTestChild(const SWidget& ChildWidget, const FGeometry&, const FVector2D ScreenSpaceMouseCoordinate, const FVector2D LastScreenSpaceMouseCoordinate) const override
	{
		if (const TSharedPtr<const SShapeBox> Pinned = Owner.Pin())
		{
			if (Pinned->IsContentWindow(ChildWidget))
			{
				return Pinned->TranslatePointer(ScreenSpaceMouseCoordinate, LastScreenSpaceMouseCoordinate);
			}
		}
		return TOptional<FVirtualPointerPosition>();
	}

private:
	TWeakPtr<const SShapeBox> Owner;
};

/** Attached to every hit cell. Maps the pointer back through the warp and returns the content widgets under it. */
class FShapeBoxHitTestPath : public ICustomHitTestPath
{
public:
	explicit FShapeBoxHitTestPath(const TWeakPtr<const SShapeBox>& InOwner)
		: Owner(InOwner)
	{
	}

	virtual TArray<FWidgetAndPointer> GetBubblePathAndVirtualCursors(const FGeometry&, FVector2D DesktopSpaceCoordinate, bool bIgnoreEnabledStatus) const override
	{
		if (const TSharedPtr<const SShapeBox> Pinned = Owner.Pin())
		{
			return Pinned->GetContentBubblePath(DesktopSpaceCoordinate, bIgnoreEnabledStatus);
		}
		return TArray<FWidgetAndPointer>();
	}

	virtual void ArrangeCustomHitTestChildren(FArrangedChildren& ArrangedChildren) const override
	{
		if (const TSharedPtr<const SShapeBox> Pinned = Owner.Pin())
		{
			Pinned->ArrangeContentWindow(ArrangedChildren);
		}
	}

	virtual TOptional<FVirtualPointerPosition> TranslateMouseCoordinateForCustomHitTestChild(const SWidget& ChildWidget, const FGeometry&, const FVector2D ScreenSpaceMouseCoordinate, const FVector2D LastScreenSpaceMouseCoordinate) const override
	{
		if (const TSharedPtr<const SShapeBox> Pinned = Owner.Pin())
		{
			if (Pinned->IsContentWindow(ChildWidget))
			{
				return Pinned->TranslatePointer(ScreenSpaceMouseCoordinate, LastScreenSpaceMouseCoordinate);
			}
		}
		return TOptional<FVirtualPointerPosition>();
	}

private:
	TWeakPtr<const SShapeBox> Owner;
};

// =====================================================================================================
// SShapeBox - setup
// =====================================================================================================

SShapeBox::SShapeBox()
{
	SurfaceBrush.DrawAs = ESlateBrushDrawType::Image;
}

SShapeBox::~SShapeBox()
{
	if (Resources)
	{
		BeginCleanup(Resources);
		Resources = nullptr;
	}
}

void SShapeBox::Construct(const FArguments& InArgs)
{
	Shape = InArgs._Shape;
	RenderScale = InArgs._RenderScale;

	Window = SNew(SVirtualWindow);
	HitTestPath = MakeShared<FShapeBoxHitTestPath>(SharedThis(this));

	SetContent(InArgs._Content.Widget);

	// Route the construction visibility through the shape-aware wrapper (SCompoundWidget::SetVisibility is final).
	SetRequestedVisibility(InArgs._Visibility);

	// The shape is read every frame (so it can be animated from anywhere), which needs a repaint every frame.
	ForceVolatile(true);
}

void SShapeBox::SetContent(const TSharedRef<SWidget>& InContent)
{
	// Same arrangement as SRetainerWidget: the content sits in the child slot for the flat and affine
	// paths (and for layout), and in the virtual window for the warped path.
	ChildSlot
	[
		InContent
	];
	Window->SetContent(InContent);
	bContentChanged = true;
}

void SShapeBox::SetShape(const TAttribute<FShapeBoxShape>& InShape)
{
	Shape = InShape;
}

void SShapeBox::SetRenderScale(float InRenderScale)
{
	RenderScale = FMath::Clamp(InRenderScale, 0.25f, 4.0f);
}

void SShapeBox::SetRedrawContentOnlyWhenChanged(bool bInOnlyWhenChanged)
{
	bRedrawOnlyWhenChanged = bInOnlyWhenChanged;
	bContentChanged = true;
}

FVector2D SShapeBox::GetWarpedLocalPosition(const FVector2D& NormalizedPoint) const
{
	const FVector2D Size = PaintedSize.X > 0.0 ? PaintedSize : GetTickSpaceGeometry().GetLocalSize();
	return ShapeBoxPrivate::FShapeEvaluator(Shape.Get(), Size).Evaluate(NormalizedPoint.X, NormalizedPoint.Y);
}

bool SShapeBox::MapDesktopToContent(const FVector2D& Desktop, FVector2D& OutContent, bool& bOutOnShape) const
{
	if (Mode != EMode::Warped || !Window.IsValid())
	{
		return false;
	}
	// The content window's own space is the texture's pixels - what its hit-test grid, and so the widgets in it, are laid out in.
	bOutOnShape = LocalToPixel(GetTickSpaceGeometry().AbsoluteToLocal(Desktop), OutContent);
	return true;
}

bool SShapeBox::IsLocalPointInShape(const FVector2D& LocalPoint) const
{
	using namespace ShapeBoxPrivate;

	if (PaintedSize.X < 1.0 || PaintedSize.Y < 1.0)
	{
		return false;
	}

	auto IsInContent = [](const FVector2D& Content, const FShapeEvaluator& Evaluator)
	{
		return Content.X >= 0.0 && Content.Y >= 0.0 && Content.X <= Evaluator.Size.X && Content.Y <= Evaluator.Size.Y
			&& Evaluator.CutDistance(Content) >= 0.0;
	};

	switch (Mode)
	{
	case EMode::Warped:
	{
		FVector2D Pixel;
		if (!LocalToPixel(LocalPoint, Pixel) || PixelScale <= 0.0f)
		{
			return false;
		}

		// Texels back to content units: the texture is the content box at PixelScale, inset by the feather.
		const FShapeEvaluator Evaluator(PaintedShape, PaintedSize);
		const FVector2D Content = (Pixel - FVector2D(Padding, Padding)) / static_cast<double>(PixelScale);
		return IsInContent(Content, Evaluator);
	}

	case EMode::Affine:
	{
		const FShapeEvaluator Evaluator(PaintedShape, PaintedSize);
		const FSlateRenderTransform ContentToLocal = MakeAffineTransform(Evaluator, Evaluator.Size);
		const FVector2D Content(ContentToLocal.Inverse().TransformPoint(FVector2f(LocalPoint)));
		return IsInContent(Content, Evaluator);
	}

	case EMode::Flat:
	default:
		return LocalPoint.X >= 0.0 && LocalPoint.Y >= 0.0 && LocalPoint.X <= PaintedSize.X && LocalPoint.Y <= PaintedSize.Y;
	}
}

void SShapeBox::SetRequestedVisibility(TAttribute<EVisibility> InVisibility)
{
	UserVisibility = InVisibility;
	SetVisibility(TAttribute<EVisibility>::CreateSP(this, &SShapeBox::GetEffectiveVisibility));
}

EVisibility SShapeBox::GetEffectiveVisibility() const
{
	const EVisibility Requested = UserVisibility.Get(EVisibility::Visible);

	// While shaped, the content (or, warped, the hit cells) is this box's hit area. If the box itself were
	// hit-testable too, its layout rect would swallow clicks wherever the shape no longer covers it: the corners a
	// bevel leaves bare, an inset margin, a chamfered corner.
	if (Mode != EMode::Flat && Requested == EVisibility::Visible)
	{
		return EVisibility::SelfHitTestInvisible;
	}
	return Requested;
}

// =====================================================================================================
// SShapeBox - layout and paint
// =====================================================================================================

SShapeBox::EMode SShapeBox::ResolveMode(const FShapeBoxShape& InShape, const FVector2D& Size) const
{
	if (Size.X < 1.0 || Size.Y < 1.0 || InShape.IsFlat())
	{
		return EMode::Flat;
	}
	if (InShape.IsAffine())
	{
		return EMode::Affine;
	}

	const bool bCanRenderOffscreen = Window.IsValid()
		&& FApp::CanEverRender()
		&& FSlateApplication::IsInitialized()
		&& FSlateApplication::Get().GetRenderer() != nullptr;

	return bCanRenderOffscreen ? EMode::Warped : EMode::Flat;
}

void SShapeBox::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	if (Mode == EMode::Warped)
	{
		const int32 Count = FMath::Min(HitCells.Num(), HitCellTransforms.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FGeometry CellGeometry = AllottedGeometry.MakeChild(HitCellSizes[Index], FSlateLayoutTransform(), HitCellTransforms[Index], FVector2D::ZeroVector);
			ArrangedChildren.AddWidget(EVisibility::Visible, FArrangedWidget(HitCells[Index], CellGeometry));
		}
		return;
	}

	if (Mode == EMode::Affine)
	{
		const ShapeBoxPrivate::FShapeEvaluator Evaluator(PaintedShape, AllottedGeometry.GetLocalSize());
		const FVector2D ContentSize = Evaluator.Size;
		const TSharedRef<SWidget>& Content = ChildSlot.GetWidget();

		// The content is laid out at the extruded/inset size (resized, not stretched), then mapped onto the shape.
		FSlateRenderTransform Transform = ShapeBoxPrivate::MakeAffineTransform(Evaluator, ContentSize);

		// Keep any render transform the content has of its own, applied before the shape.
		const TOptional<FSlateRenderTransform> ContentTransform = Content->GetRenderTransformWithRespectToFlowDirection();
		if (ContentTransform.IsSet())
		{
			const FVector2f PivotPx(FVector2D(Content->GetRenderTransformPivotWithRespectToFlowDirection()) * ContentSize);
			const FSlateRenderTransform AboutPivot = ::Concatenate(::Concatenate(FSlateRenderTransform(-PivotPx), ContentTransform.GetValue()), FSlateRenderTransform(PivotPx));
			Transform = ::Concatenate(AboutPivot, Transform);
		}

		const FGeometry ContentGeometry = AllottedGeometry.MakeChild(ContentSize, FSlateLayoutTransform(), Transform, FVector2D::ZeroVector);
		ArrangedChildren.AddWidget(Content->GetVisibility(), FArrangedWidget(Content, ContentGeometry));
		return;
	}

	SCompoundWidget::OnArrangeChildren(AllottedGeometry, ArrangedChildren);
}

void SShapeBox::ClearWarpState() const
{
	MeshPositions.Reset();
	MeshUVs.Reset();
	MeshAlphas.Reset();
	MeshIndices.Reset();
	MeshBounds = FBox2D(ForceInit);
	HitCells.Reset();  // Destroying them drops their stale entries from the hit-test grid.
	HitCellTransforms.Reset();
	HitCellSizes.Reset();
	bHasLastVirtualPixel = false;
	bMeshBuilt = false;
	bHitCellsBuilt = false;

	// Not warped meanwhile: whatever the content became is drawn afresh when it is again.
	bContentDrawn = false;
}

int32 SShapeBox::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace ShapeBoxPrivate;

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	PaintedShape = Shape.Get();
	PaintedSize = Size;
	Mode = ResolveMode(PaintedShape, Size);

	{
		const FShapeEvaluator Evaluator(PaintedShape, Size);
		PaintedContentOrigin = Evaluator.Origin;
		PaintedContentSize = Evaluator.Size;
	}

	if (Mode != EMode::Warped)
	{
		ClearWarpState();
		const int32 MaxLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
		if (CVarDebugContentBox.GetValueOnGameThread() > 0)
		{
			PaintContentDebug(AllottedGeometry, OutDrawElements, MaxLayer + 1);
			return MaxLayer + 1;
		}
		return MaxLayer;
	}

	// The window is drawn offscreen, so it doesn't inherit our enabled state. Push it across on change.
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	if (bLastWindowEnabled != bEnabled)
	{
		Window->SetEnabled(bEnabled);
		bLastWindowEnabled = bEnabled;
	}

	// ---- Render the content flat into the texture, at its extruded/inset size ----

	const FVector2D ContentSize = PaintedContentSize;
	const float MaxContent = static_cast<float>(MaxRenderTargetDimension - 2 * FeatherTexels);
	// Extra resolution where the shape magnifies its content (the near side of an angled box), so it stays sharp.
	const float Magnification = static_cast<float>(ComputeMagnification(FShapeEvaluator(PaintedShape, Size)));
	float Scale = FMath::Max(0.01f, AllottedGeometry.Scale * RenderScale * Magnification);
	Scale = FMath::Min(Scale, MaxContent / static_cast<float>(ContentSize.X));
	Scale = FMath::Min(Scale, MaxContent / static_cast<float>(ContentSize.Y));

	const FIntPoint Target(
		FMath::Max(1, FMath::CeilToInt32(static_cast<float>(ContentSize.X) * Scale)) + 2 * FeatherTexels,
		FMath::Max(1, FMath::CeilToInt32(static_cast<float>(ContentSize.Y) * Scale)) + 2 * FeatherTexels);

	if (!RenderContent(ContentSize, Scale, FeatherTexels, Target, Args.GetDeltaTime()))
	{
		Mode = EMode::Flat;
		ClearWarpState();
		return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	// ---- Draw it as a mesh following the shape ----

	// Built again only when what it is built from changed: a box held in its shape - a tilted card at rest - reuses it.
	const bool bMeshCurrent = bMeshBuilt
		&& MeshBuiltSize.Equals(Size, 0.0)
		&& MeshBuiltPaintScale == AllottedGeometry.Scale
		&& MeshBuiltPixelScale == PixelScale
		&& MeshBuiltTargetSize == TargetSize
		&& MeshBuiltPadding == Padding
		&& MeshBuiltShape.Equals(PaintedShape);
	if (!bMeshCurrent)
	{
		BuildMesh(Size, AllottedGeometry.Scale);
		bMeshBuilt = true;
		MeshBuiltShape = PaintedShape;
		MeshBuiltSize = Size;
		MeshBuiltPaintScale = AllottedGeometry.Scale;
		MeshBuiltPixelScale = PixelScale;
		MeshBuiltTargetSize = TargetSize;
		MeshBuiltPadding = Padding;
	}

	const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(SurfaceBrush);
	if (Handle.IsValid() && MeshIndices.Num() > 0)
	{
		const FColor Color = MakePremultipliedColor(InWidgetStyle.GetColorAndOpacityTint());
		const FSlateRenderTransform& RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();

		DrawVertices.Reset(MeshPositions.Num());
		for (int32 Index = 0; Index < MeshPositions.Num(); ++Index)
		{
			DrawVertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
				RenderTransform,
				FVector2f(MeshPositions[Index]),
				FVector2f(MeshUVs[Index]),
				FVector2f(1.0f, 1.0f),
				ScalePremultipliedColor(Color, MeshAlphas.IsValidIndex(Index) ? MeshAlphas[Index] : 1.0f)));
		}

		// Premultiplied: the renderer composites onto a transparent target. NoGamma: already gamma space.
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, Handle, DrawVertices, MeshIndices, nullptr, 0, 0,
			ESlateDrawEffect::PreMultipliedAlpha | ESlateDrawEffect::NoGamma);
	}

	// ---- Put the drawn shape into the hit-test grid ----

	if (!bHitCellsBuilt || !HitCellsBuiltSize.Equals(Size, 0.0) || !HitCellsBuiltShape.Equals(PaintedShape))
	{
		BuildHitCells(Size);
		bHitCellsBuilt = true;
		HitCellsBuiltShape = PaintedShape;
		HitCellsBuiltSize = Size;
	}
	PaintHitCells(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bEnabled);

	if (CVarDebugContentBox.GetValueOnGameThread() > 0)
	{
		PaintContentDebug(AllottedGeometry, OutDrawElements, LayerId + 2);
		return LayerId + 2;
	}
	return LayerId + 1;
}

void SShapeBox::PaintContentDebug(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const TSharedRef<SWidget>& Content = ChildSlot.GetWidget();
	auto Outline = [&OutDrawElements, LayerId](const FGeometry& Geometry, const FLinearColor& Color)
	{
		const FVector2f Size(Geometry.GetLocalSize());
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(),
			TArray<FVector2f>{ FVector2f(0.f, 0.f), FVector2f(Size.X, 0.f), Size, FVector2f(0.f, Size.Y), FVector2f(0.f, 0.f) },
			ESlateDrawEffect::None, Color, /*bAntialias=*/true, 1.f);
	};

	// Where Slate arranges the content from this box now (warped: where its window is drawn into) - magenta - and where the
	// content itself was last painted - cyan. A designer outline follows the second.
	FString Arranged = TEXT("(not arranged here - drawn offscreen)");
	if (Mode != EMode::Warped)
	{
		FArrangedChildren Children(EVisibility::All);
		OnArrangeChildren(AllottedGeometry, Children);
		if (Children.Num() > 0)
		{
			const FGeometry& ContentGeometry = Children[0].Geometry;
			Outline(ContentGeometry, FLinearColor(1.f, 0.f, 1.f, 1.f));
			Arranged = FString::Printf(TEXT("at %s size %s"), *FVector2D(ContentGeometry.GetAbsolutePosition()).ToString(), *FVector2D(ContentGeometry.GetAbsoluteSize()).ToString());
		}
	}
	const FGeometry& Painted = Content->GetPaintSpaceGeometry();
	Outline(Painted, FLinearColor(0.f, 1.f, 1.f, 1.f));

	static const TCHAR* ModeNames[] = { TEXT("Flat"), TEXT("Affine"), TEXT("Warped") };
	const FString Line = FString::Printf(TEXT("Shape Box %s: mode %s, box at %s size %s, content box %s size %s (local); content arranged %s; content last painted at %s size %s; content render transform %s."),
		*Content->GetTypeAsString(), ModeNames[static_cast<int32>(Mode)],
		*FVector2D(AllottedGeometry.GetAbsolutePosition()).ToString(), *FVector2D(AllottedGeometry.GetAbsoluteSize()).ToString(),
		*PaintedContentOrigin.ToString(), *PaintedContentSize.ToString(), *Arranged,
		*FVector2D(Painted.GetAbsolutePosition()).ToString(), *FVector2D(Painted.GetAbsoluteSize()).ToString(),
		Content->GetRenderTransform().IsSet() ? TEXT("set") : TEXT("none"));
	if (Line != LastContentDebugLine)
	{
		LastContentDebugLine = Line;
		UE_LOG(LogShapeBox, Log, TEXT("%s"), *Line);
	}
}

bool SShapeBox::RenderContent(const FVector2D& ContentSize, float Scale, int32 InPadding, const FIntPoint& Target, float DeltaTime) const
{
	if (!Resources)
	{
		Resources = new FShapeBoxRenderResources();
	}

	UTextureRenderTarget2D* RenderTarget = Resources->RenderTarget.Get();
	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		if (!RenderTarget)
		{
			return false;
		}

		// Gamma-space color stored as-is, cleared to transparent so the feather border is empty.
		RenderTarget->ClearColor = FLinearColor::Transparent;
		RenderTarget->TargetGamma = 1.0f;
		RenderTarget->SRGB = false;
		RenderTarget->Filter = TF_Bilinear;
		RenderTarget->AddressX = TA_Clamp;
		RenderTarget->AddressY = TA_Clamp;
		RenderTarget->InitCustomFormat(Target.X, Target.Y, PF_B8G8R8A8, /*bInForceLinearGamma=*/true);
		RenderTarget->UpdateResourceImmediate(true);

		Resources->RenderTarget = RenderTarget;
		SurfaceBrush.SetResourceObject(RenderTarget);

		// A new texture holds nothing yet.
		bContentDrawn = false;
	}
	else if (RenderTarget->SizeX != Target.X || RenderTarget->SizeY != Target.Y)
	{
		RenderTarget->ResizeTarget(Target.X, Target.Y);
	}

	SurfaceBrush.ImageSize = FVector2D(Target.X, Target.Y);

	if (!Resources->WidgetRenderer)
	{
		Resources->WidgetRenderer = new FWidgetRenderer(/*bUseGammaCorrection=*/true, /*bInClearTarget=*/true);
	}

	const FVector2D WindowSize(Target.X, Target.Y);
	if (!WindowSize.Equals(LastWindowSize))
	{
		Window->Resize(WindowSize);
		LastWindowSize = WindowSize;
	}

	// Content at its extruded/inset size, scaled to texels and inset by the feather. Everything inside the window,
	// its hit-test grid included, lives in these texel coordinates.
	WindowGeometry = FGeometry::MakeRoot(ContentSize, FSlateLayoutTransform(Scale, FVector2D(InPadding, InPadding)));

	// Drawn again unless the owner has said the content only changes when it says so, and it hasn't, and the texture
	// would be drawn exactly as last time - same size, scale and inset, and the same enabled state. The window's hit-test
	// grid is the one that draw filled, still true for geometry that hasn't changed.
	const bool bReuseContent = bRedrawOnlyWhenChanged && bContentDrawn && !bContentChanged
		&& ContentDrawnTarget == Target
		&& ContentDrawnScale == Scale
		&& ContentDrawnPadding == InPadding
		&& ContentDrawnSize.Equals(ContentSize, 0.0)
		&& bContentDrawnEnabled == bLastWindowEnabled;
	if (!bReuseContent)
	{
		Resources->WidgetRenderer->DrawWindow(
			RenderTarget,
			Window->GetHittestGrid(),
			Window.ToSharedRef(),
			WindowGeometry,
			FSlateRect(0.0f, 0.0f, static_cast<float>(Target.X), static_cast<float>(Target.Y)),
			DeltaTime,
			/*bDeferRenderTargetUpdate=*/false);

		bContentDrawn = true;
		bContentChanged = false;
		ContentDrawnTarget = Target;
		ContentDrawnScale = Scale;
		ContentDrawnPadding = InPadding;
		ContentDrawnSize = ContentSize;
		bContentDrawnEnabled = bLastWindowEnabled;
	}

	TargetSize = Target;
	PixelScale = Scale;
	Padding = InPadding;
	return true;
}

void SShapeBox::BuildMesh(const FVector2D& Size, float PaintScale) const
{
	using namespace ShapeBoxPrivate;

	MeshPositions.Reset();
	MeshUVs.Reset();
	MeshAlphas.Reset();
	MeshIndices.Reset();
	MeshBounds = FBox2D(ForceInit);

	const FShapeEvaluator Evaluator(PaintedShape, Size);
	const FIntPoint Grid = ComputeGrid(Evaluator, RenderSpacing, RenderTolerance, MaxRenderSegments);
	const double Scale = FMath::Max(static_cast<double>(PixelScale), 1e-4);

	// Half the width of the soft strip along a chamfer cut, in content units: about a screen pixel across in all.
	// Clipped geometry has no texel border to filter into the way the outer edges do, so the strip fades instead.
	const double Ramp = 0.5 / FMath::Max(static_cast<double>(PaintScale), 0.01);

	// Content units (0..Size across the content box) to and from the texture, which holds the content at
	// PixelScale inset by the feather.
	auto TextureToContent = [&](double U, double V)
	{
		return FVector2D((U * TargetSize.X - Padding) / Scale, (V * TargetSize.Y - Padding) / Scale);
	};
	auto ContentToTexture = [&](const FVector2D& Content)
	{
		return FVector2D((Content.X * Scale + Padding) / TargetSize.X, (Content.Y * Scale + Padding) / TargetSize.Y);
	};

	auto AddVertex = [&](const FVector2D& Content) -> SlateIndex
	{
		const FVector2D Position = Evaluator.Evaluate(Content.X / Evaluator.Size.X, Content.Y / Evaluator.Size.Y);
		MeshPositions.Add(Position);
		MeshUVs.Add(ContentToTexture(Content));
		MeshAlphas.Add(Evaluator.HasCuts() ? Evaluator.CutCoverage(Content, Ramp) : 1.0f);
		MeshBounds += Position;
		return static_cast<SlateIndex>(MeshPositions.Num() - 1);
	};

	// The mesh covers the whole texture, feather included, so the shape is extrapolated a feather's width
	// past the content edge. That transparent rim is what the edges filter into instead of stair-stepping.
	TArray<FVector2D> GridContent;
	GridContent.Reserve((Grid.X + 1) * (Grid.Y + 1));
	const int32 Columns = Grid.X + 1;

	for (int32 Row = 0; Row <= Grid.Y; ++Row)
	{
		const double V = static_cast<double>(Row) / Grid.Y;
		for (int32 Column = 0; Column <= Grid.X; ++Column)
		{
			const double U = static_cast<double>(Column) / Grid.X;
			const FVector2D Content = TextureToContent(U, V);
			GridContent.Add(Content);
			AddVertex(Content);
		}
	}

	// A chamfered triangle is clipped against every cut half a ramp outside it, then split along the ramp's inner
	// edge, so the coverage (linear across the strip) interpolates exactly across each piece.
	TArray<FVector2D> Polygon;
	TArray<FVector2D> Clipped;
	TArray<TArray<FVector2D>> Pieces;
	TArray<TArray<FVector2D>> NextPieces;

	auto AddClippedTriangle = [&](const FVector2D& A, const FVector2D& B, const FVector2D& C)
	{
		Polygon.Reset();
		Polygon.Append({ A, B, C });

		for (int32 CutIndex = 0; CutIndex < Evaluator.NumCuts; ++CutIndex)
		{
			ClipPolygon(Polygon, Evaluator.Cuts[CutIndex], -Ramp, /*bKeepAbove=*/true, Clipped);
			Swap(Polygon, Clipped);
			if (Polygon.Num() < 3)
			{
				return;
			}
		}

		Pieces.Reset();
		Pieces.Add(Polygon);
		for (int32 CutIndex = 0; CutIndex < Evaluator.NumCuts; ++CutIndex)
		{
			NextPieces.Reset();
			for (const TArray<FVector2D>& Piece : Pieces)
			{
				ClipPolygon(Piece, Evaluator.Cuts[CutIndex], Ramp, /*bKeepAbove=*/true, Clipped);
				if (Clipped.Num() >= 3)
				{
					NextPieces.Add(Clipped);
				}
				ClipPolygon(Piece, Evaluator.Cuts[CutIndex], Ramp, /*bKeepAbove=*/false, Clipped);
				if (Clipped.Num() >= 3)
				{
					NextPieces.Add(Clipped);
				}
			}
			Swap(Pieces, NextPieces);
		}

		for (const TArray<FVector2D>& Piece : Pieces)
		{
			const SlateIndex First = AddVertex(Piece[0]);
			SlateIndex Previous = AddVertex(Piece[1]);
			for (int32 Index = 2; Index < Piece.Num(); ++Index)
			{
				const SlateIndex Current = AddVertex(Piece[Index]);
				MeshIndices.Append({ First, Previous, Current });
				Previous = Current;
			}
		}
	};

	const bool bCut = Evaluator.HasCuts();
	for (int32 Row = 0; Row < Grid.Y; ++Row)
	{
		for (int32 Column = 0; Column < Grid.X; ++Column)
		{
			const int32 TopLeft = Row * Columns + Column;
			const int32 TopRight = TopLeft + 1;
			const int32 BottomLeft = TopLeft + Columns;
			const int32 BottomRight = BottomLeft + 1;

			const int32 Triangles[2][3] = { { TopLeft, TopRight, BottomLeft }, { TopRight, BottomRight, BottomLeft } };
			for (const int32 (&Triangle)[3] : Triangles)
			{
				// Well clear of every cut (the usual case): the grid vertices as they are.
				const bool bClear = !bCut
					|| (Evaluator.CutDistance(GridContent[Triangle[0]]) >= Ramp
						&& Evaluator.CutDistance(GridContent[Triangle[1]]) >= Ramp
						&& Evaluator.CutDistance(GridContent[Triangle[2]]) >= Ramp);

				if (bClear)
				{
					MeshIndices.Append({ static_cast<SlateIndex>(Triangle[0]), static_cast<SlateIndex>(Triangle[1]), static_cast<SlateIndex>(Triangle[2]) });
				}
				else
				{
					AddClippedTriangle(GridContent[Triangle[0]], GridContent[Triangle[1]], GridContent[Triangle[2]]);
				}
			}
		}
	}
}

void SShapeBox::BuildHitCells(const FVector2D& Size) const
{
	using namespace ShapeBoxPrivate;

	HitCellTransforms.Reset();
	HitCellSizes.Reset();

	const FShapeEvaluator Evaluator(PaintedShape, Size);
	const FIntPoint Grid = ComputeGrid(Evaluator, HitSpacing, HitTolerance, MaxHitSegments);

	auto AddCell = [&](double U0, double V0, double U1, double V1)
	{
		const FVector2D P00 = Evaluator.Evaluate(U0, V0);
		const FVector2D P10 = Evaluator.Evaluate(U1, V0);
		const FVector2D P01 = Evaluator.Evaluate(U0, V1);
		const FVector2D P11 = Evaluator.Evaluate(U1, V1);

		// The parallelogram that best fits this (slightly non-parallel) cell: averaged sides, same center.
		const FVector2D AxisX = ((P10 - P00) + (P11 - P01)) * 0.5;
		const FVector2D AxisY = ((P01 - P00) + (P11 - P10)) * 0.5;
		const FVector2D Origin = (P00 + P10 + P01 + P11) * 0.25 - (AxisX + AxisY) * 0.5;

		const double LengthX = AxisX.Size();
		const double LengthY = AxisY.Size();
		const double Area = AxisX.X * AxisY.Y - AxisX.Y * AxisY.X;
		if (LengthX < Tolerance || LengthY < Tolerance || Area <= Tolerance)
		{
			return;  // Degenerate or folded over; nothing sensible to click.
		}

		HitCellTransforms.Add(FSlateRenderTransform(
			FMatrix2x2f(
				static_cast<float>(AxisX.X / LengthX), static_cast<float>(AxisX.Y / LengthX),
				static_cast<float>(AxisY.X / LengthY), static_cast<float>(AxisY.Y / LengthY)),
			FVector2f(Origin)));
		HitCellSizes.Add(FVector2D(LengthX, LengthY));
	};

	for (int32 Row = 0; Row < Grid.Y; ++Row)
	{
		const double V0 = static_cast<double>(Row) / Grid.Y;
		const double V1 = static_cast<double>(Row + 1) / Grid.Y;

		for (int32 Column = 0; Column < Grid.X; ++Column)
		{
			const double U0 = static_cast<double>(Column) / Grid.X;
			const double U1 = static_cast<double>(Column + 1) / Grid.X;

			if (!Evaluator.HasCuts())
			{
				AddCell(U0, V0, U1, V1);
				continue;
			}

			// A cut is a straight line in content space and a cell a rectangle there, so its four corners settle it.
			const FVector2D CellCorners[4] =
			{
				FVector2D(U0 * Evaluator.Size.X, V0 * Evaluator.Size.Y),
				FVector2D(U1 * Evaluator.Size.X, V0 * Evaluator.Size.Y),
				FVector2D(U0 * Evaluator.Size.X, V1 * Evaluator.Size.Y),
				FVector2D(U1 * Evaluator.Size.X, V1 * Evaluator.Size.Y)
			};

			bool bWhollyKept = true;
			bool bWhollyCut = false;
			for (int32 CutIndex = 0; CutIndex < Evaluator.NumCuts; ++CutIndex)
			{
				double Lowest = TNumericLimits<double>::Max();
				double Highest = TNumericLimits<double>::Lowest();
				for (const FVector2D& Corner : CellCorners)
				{
					const double Distance = Evaluator.Cuts[CutIndex].Distance(Corner);
					Lowest = FMath::Min(Lowest, Distance);
					Highest = FMath::Max(Highest, Distance);
				}
				bWhollyCut |= Highest < 0.0;
				bWhollyKept &= Lowest >= 0.0;
			}

			if (bWhollyCut)
			{
				continue;
			}
			if (bWhollyKept)
			{
				AddCell(U0, V0, U1, V1);
				continue;
			}

			// Partly cut: tile it finely and keep the pieces whose middle survives, so the clickable edge follows the cut.
			const double Extent = FMath::Max((U1 - U0) * Evaluator.Size.X, (V1 - V0) * Evaluator.Size.Y);
			const int32 Subdivisions = FMath::Clamp(FMath::CeilToInt32(Extent / HitCutSpacing), 2, MaxHitCutSubdivisions);
			for (int32 SubRow = 0; SubRow < Subdivisions; ++SubRow)
			{
				const double SubV0 = FMath::Lerp(V0, V1, static_cast<double>(SubRow) / Subdivisions);
				const double SubV1 = FMath::Lerp(V0, V1, static_cast<double>(SubRow + 1) / Subdivisions);
				for (int32 SubColumn = 0; SubColumn < Subdivisions; ++SubColumn)
				{
					const double SubU0 = FMath::Lerp(U0, U1, static_cast<double>(SubColumn) / Subdivisions);
					const double SubU1 = FMath::Lerp(U0, U1, static_cast<double>(SubColumn + 1) / Subdivisions);
					const FVector2D Middle((SubU0 + SubU1) * 0.5 * Evaluator.Size.X, (SubV0 + SubV1) * 0.5 * Evaluator.Size.Y);
					if (Evaluator.CutDistance(Middle) >= 0.0)
					{
						AddCell(SubU0, SubV0, SubU1, SubV1);
					}
				}
			}
		}
	}

	const int32 Needed = HitCellTransforms.Num();
	while (HitCells.Num() < Needed)
	{
		HitCells.Add(SNew(SShapeBoxHitCell, SharedThis(this)));
	}
	if (HitCells.Num() > Needed)
	{
		HitCells.RemoveAt(Needed, HitCells.Num() - Needed);
	}
}

void SShapeBox::PaintHitCells(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bEnabled) const
{
	// Our children aren't hit-testable (the box is Hit Test Invisible or under something that is).
	if (!Args.GetInheritedHittestability() || !HitTestPath.IsValid())
	{
		return;
	}

	FArrangedChildren Cells(EVisibility::Visible);
	ArrangeChildren(AllottedGeometry, Cells);

	const FPaintArgs CellArgs = Args.WithNewParent(this);
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		const FArrangedWidget& Cell = Cells[Index];
		Cell.Widget->Paint(CellArgs, Cell.Geometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bEnabled);
		Args.GetHittestGrid().InsertCustomHitTestPath(&Cell.Widget.Get(), HitTestPath.ToSharedRef());
	}
}

// =====================================================================================================
// SShapeBox - input
// =====================================================================================================

bool SShapeBox::LocalToPixel(const FVector2D& LocalPoint, FVector2D& OutPixel) const
{
	constexpr double Epsilon = 1e-4;

	if (MeshIndices.Num() == 0 || !MeshBounds.IsInside(LocalPoint))
	{
		return false;
	}

	for (int32 Triangle = 0; Triangle + 2 < MeshIndices.Num(); Triangle += 3)
	{
		const int32 I0 = MeshIndices[Triangle];
		const int32 I1 = MeshIndices[Triangle + 1];
		const int32 I2 = MeshIndices[Triangle + 2];

		const FVector2D Edge0 = MeshPositions[I1] - MeshPositions[I0];
		const FVector2D Edge1 = MeshPositions[I2] - MeshPositions[I0];
		const FVector2D ToPoint = LocalPoint - MeshPositions[I0];

		const double Denominator = Edge0.X * Edge1.Y - Edge1.X * Edge0.Y;
		if (FMath::Abs(Denominator) < 1e-9)
		{
			continue;
		}

		const double W1 = (ToPoint.X * Edge1.Y - Edge1.X * ToPoint.Y) / Denominator;
		const double W2 = (Edge0.X * ToPoint.Y - ToPoint.X * Edge0.Y) / Denominator;
		const double W0 = 1.0 - W1 - W2;

		if (W0 >= -Epsilon && W1 >= -Epsilon && W2 >= -Epsilon)
		{
			// Interpolated exactly as the GPU samples the triangle, so this is the texel under the pointer.
			const FVector2D UV = MeshUVs[I0] * W0 + MeshUVs[I1] * W1 + MeshUVs[I2] * W2;
			OutPixel = FVector2D(UV.X * TargetSize.X, UV.Y * TargetSize.Y);
			return true;
		}
	}

	return false;
}

FVector2D SShapeBox::LocalToPixelOrApprox(const FVector2D& LocalPoint) const
{
	FVector2D Pixel;
	if (LocalToPixel(LocalPoint, Pixel))
	{
		return Pixel;
	}

	// Off the shape (a drag or captured press that left it): fall back to the unwarped mapping, which
	// keeps the pointer somewhere sensible and outside the content, so e.g. a release off a button misses it.
	return FVector2D(Padding, Padding) + (LocalPoint - PaintedContentOrigin) * static_cast<double>(PixelScale);
}

TArray<FWidgetAndPointer> SShapeBox::GetContentBubblePath(const FVector2D& DesktopSpaceCoordinate, bool bIgnoreEnabledStatus) const
{
	TArray<FWidgetAndPointer> Result;
	if (Mode != EMode::Warped || !Window.IsValid())
	{
		return Result;
	}

	FVector2D Pixel;
	if (!LocalToPixel(GetTickSpaceGeometry().AbsoluteToLocal(DesktopSpaceCoordinate), Pixel))
	{
		bHasLastVirtualPixel = false;
		return Result;
	}

	const FVector2D LastPixel = bHasLastVirtualPixel ? LastVirtualPixel : Pixel;
	LastVirtualPixel = Pixel;
	bHasLastVirtualPixel = true;

	const FVirtualPointerPosition VirtualPosition(Pixel, LastPixel);
	for (const FWidgetAndPointer& Entry : Window->GetHittestGrid().GetBubblePath(Pixel, 0.0f, bIgnoreEnabledStatus))
	{
		Result.Emplace(static_cast<const FArrangedWidget&>(Entry), TOptional<FVirtualPointerPosition>(VirtualPosition));
	}
	return Result;
}

TOptional<FVirtualPointerPosition> SShapeBox::TranslatePointer(const FVector2D& ScreenSpace, const FVector2D& LastScreenSpace) const
{
	if (Mode != EMode::Warped || MeshIndices.Num() == 0)
	{
		return TOptional<FVirtualPointerPosition>();
	}

	const FGeometry& Geometry = GetTickSpaceGeometry();
	return FVirtualPointerPosition(
		LocalToPixelOrApprox(Geometry.AbsoluteToLocal(ScreenSpace)),
		LocalToPixelOrApprox(Geometry.AbsoluteToLocal(LastScreenSpace)));
}

void SShapeBox::ArrangeContentWindow(FArrangedChildren& ArrangedChildren) const
{
	if (Mode == EMode::Warped && Window.IsValid())
	{
		ArrangedChildren.AddWidget(Window->GetVisibility(), FArrangedWidget(Window.ToSharedRef(), WindowGeometry));
	}
}

bool SShapeBox::IsContentWindow(const SWidget& Widget) const
{
	return Window.IsValid() && &Widget == Window.Get();
}

// =====================================================================================================
// UShapeBox
// =====================================================================================================

TSharedRef<SWidget> UShapeBox::RebuildWidget()
{
	MyShapeBox = SNew(SShapeBox)
		.Shape(TAttribute<FShapeBoxShape>::Create(TAttribute<FShapeBoxShape>::FGetter::CreateWeakLambda(this, [this]() { return Shape; })))
		.RenderScale(RenderScale);

	UPanelSlot* ContentSlot = GetContentSlot();
	if (ContentSlot && ContentSlot->Content)
	{
		MyShapeBox->SetContent(ContentSlot->Content->TakeWidget());
	}

	return MyShapeBox.ToSharedRef();
}

void UShapeBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (MyShapeBox.IsValid())
	{
		MyShapeBox->SetRenderScale(RenderScale);
		MyShapeBox->SetRequestedVisibility(UWidget::ConvertSerializedVisibilityToRuntime(GetVisibility()));
	}
}

void UShapeBox::SetVisibility(ESlateVisibility InVisibility)
{
	Super::SetVisibility(InVisibility);

	if (MyShapeBox.IsValid())
	{
		MyShapeBox->SetRequestedVisibility(UWidget::ConvertSerializedVisibilityToRuntime(InVisibility));
	}
}

void UShapeBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyShapeBox.Reset();
}

void UShapeBox::OnSlotAdded(UPanelSlot* InSlot)
{
	if (MyShapeBox.IsValid())
	{
		MyShapeBox->SetContent(InSlot && InSlot->Content ? InSlot->Content->TakeWidget() : SNullWidget::NullWidget);
	}
}

void UShapeBox::OnSlotRemoved(UPanelSlot* InSlot)
{
	if (MyShapeBox.IsValid())
	{
		MyShapeBox->SetContent(SNullWidget::NullWidget);
	}
}

void UShapeBox::SetWarpRenderScale(float InRenderScale)
{
	RenderScale = FMath::Clamp(InRenderScale, 0.25f, 4.0f);
	if (MyShapeBox.IsValid())
	{
		MyShapeBox->SetRenderScale(RenderScale);
	}
}

FVector2D UShapeBox::GetWarpedLocalPosition(FVector2D NormalizedPoint) const
{
	if (MyShapeBox.IsValid())
	{
		return MyShapeBox->GetWarpedLocalPosition(NormalizedPoint);
	}
	return Shape.Evaluate(GetCachedGeometry().GetLocalSize(), NormalizedPoint);
}

bool UShapeBox::MapDesktopToContent(const FVector2D& Desktop, FVector2D& OutContent, bool& bOutOnShape) const
{
	return MyShapeBox.IsValid() && MyShapeBox->MapDesktopToContent(Desktop, OutContent, bOutOnShape);
}

#if WITH_EDITOR
const FText UShapeBox::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE