// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetEffectsDraw.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Geometry.h"
#include "Math/RandomStream.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/SlateBrush.h"
#include "Styling/WidgetStyle.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeTypes.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"

namespace WidgetEffectsDrawPrivate
{
	/** The brush flat and gradient colors draw with: no texture, so the vertex colors are all there is. */
	const FSlateBrush& GetUntexturedBrush()
	{
		static const FSlateBrush Brush;
		return Brush;
	}

	/** The grain tile's side, in texels. */
	constexpr int32 GrainSide = 128;

	FVector2f DirectionAt(float Degrees)
	{
		float Sin = 0.f;
		float Cos = 0.f;
		FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(Degrees));
		return FVector2f(Cos, Sin);
	}

	/** The radius of the quadrant Point (relative to the middle) is in: X top left, Y top right, Z bottom right, W bottom left. */
	float QuadrantRadius(const FVector2f& FromMiddle, const FVector4f& Radii)
	{
		if (FromMiddle.X > 0.f)
		{
			return FromMiddle.Y > 0.f ? Radii.Z : Radii.Y;
		}
		return FromMiddle.Y > 0.f ? Radii.W : Radii.X;
	}

	/** One row of a blurred rounded box: exact along x (Evan Wallace, "Fast Rounded Rectangle Shadows"). */
	float SoftBoxRow(float X, float Y, float Sigma, float Corner, const FVector2f& HalfSize)
	{
		const float Delta = FMath::Min(HalfSize.Y - Corner - FMath::Abs(Y), 0.f);
		const float Curved = HalfSize.X - Corner + FMath::Sqrt(FMath::Max(0.f, Corner * Corner - Delta * Delta));
		const float Scale = 0.70710678f / Sigma;
		const float Low = 0.5f + 0.5f * WidgetEffectsDraw::Erf((X - Curved) * Scale);
		const float High = 0.5f + 0.5f * WidgetEffectsDraw::Erf((X + Curved) * Scale);
		return High - Low;
	}

	float Gaussian(float X, float Sigma)
	{
		return FMath::Exp(-(X * X) / (2.f * Sigma * Sigma)) / (2.50662827f * Sigma);
	}
}

namespace WidgetEffectsDraw
{
	float GetPixelScale(const FGeometry& Geometry)
	{
		const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
		const FVector2f AxisX = Transform.TransformVector(FVector2f(1.f, 0.f));
		const FVector2f AxisY = Transform.TransformVector(FVector2f(0.f, 1.f));
		return FMath::Max(FMath::Max(AxisX.Size(), AxisY.Size()), 0.01f);
	}

	float QuantizeScale(float Scale)
	{
		const float Steps = FMath::RoundToFloat(FMath::Log2(FMath::Max(Scale, 0.01f)) * 8.f) / 8.f;
		return FMath::Pow(2.f, Steps);
	}

	FVector4f FitRadii(const FVector4f& Radii, const FVector2f& Size)
	{
		const float Most = FMath::Max(0.f, FMath::Min(Size.X, Size.Y) * 0.5f);
		return FVector4f(
			FMath::Clamp(Radii.X, 0.f, Most),
			FMath::Clamp(Radii.Y, 0.f, Most),
			FMath::Clamp(Radii.Z, 0.f, Most),
			FMath::Clamp(Radii.W, 0.f, Most));
	}

	void AppendRoundedRect(FWidgetShapeOutline& Out, const FVector2f& Min, const FVector2f& Size, const FVector4f& Radii, float Tolerance)
	{
		if (Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		const FVector4f Fitted = FitRadii(Radii, Size);
		const TArray<FVector2f> Corners = { Min, FVector2f(Min.X + Size.X, Min.Y), Min + Size, FVector2f(Min.X, Min.Y + Size.Y) };
		const TArray<float> CornerRadii = { Fitted.X, Fitted.Y, Fitted.Z, Fitted.W };
		WidgetShapeGeometry::AppendRoundedPolygon(Out, Corners, CornerRadii, Tolerance);
	}

	float RoundedRectDistance(const FVector2f& Point, const FVector2f& Center, const FVector2f& HalfSize, const FVector4f& Radii)
	{
		const FVector2f FromMiddle = Point - Center;
		const float Radius = FMath::Min(WidgetEffectsDrawPrivate::QuadrantRadius(FromMiddle, Radii), FMath::Min(HalfSize.X, HalfSize.Y));
		const FVector2f Q(FMath::Abs(FromMiddle.X) - HalfSize.X + Radius, FMath::Abs(FromMiddle.Y) - HalfSize.Y + Radius);
		const FVector2f Outside(FMath::Max(Q.X, 0.f), FMath::Max(Q.Y, 0.f));
		return FMath::Min(FMath::Max(Q.X, Q.Y), 0.f) + Outside.Size() - Radius;
	}

	FVector2f RoundedRectNormal(const FVector2f& Point, const FVector2f& Center, const FVector2f& HalfSize, const FVector4f& Radii)
	{
		constexpr float Step = 0.25f;
		const float DX = RoundedRectDistance(Point + FVector2f(Step, 0.f), Center, HalfSize, Radii) - RoundedRectDistance(Point - FVector2f(Step, 0.f), Center, HalfSize, Radii);
		const float DY = RoundedRectDistance(Point + FVector2f(0.f, Step), Center, HalfSize, Radii) - RoundedRectDistance(Point - FVector2f(0.f, Step), Center, HalfSize, Radii);
		const FVector2f Gradient(DX, DY);
		const float Length = Gradient.Size();
		return Length > KINDA_SMALL_NUMBER ? Gradient / Length : FVector2f(0.f, -1.f);
	}

	float EvaluateSoftBox(const FSoftBox& Box, const FVector2f& Point, float AntiAlias)
	{
		using namespace WidgetEffectsDrawPrivate;

		if (Box.HalfSize.X <= 0.f || Box.HalfSize.Y <= 0.f)
		{
			return 0.f;
		}
		if (Box.Sigma <= 0.05f)
		{
			const float Distance = RoundedRectDistance(Point, Box.Center, Box.HalfSize, Box.Radii);
			return FMath::Clamp(0.5f - Distance / FMath::Max(AntiAlias, 0.01f), 0.f, 1.f);
		}

		const FVector2f FromMiddle = Point - Box.Center;
		const float Corner = FMath::Min(QuadrantRadius(FromMiddle, Box.Radii), FMath::Min(Box.HalfSize.X, Box.HalfSize.Y));
		const float Sigma = Box.Sigma;

		// The Gaussian along y, sampled over the part of it (three sigma either way) that falls on the box.
		constexpr int32 Rows = 6;
		const float Low = FromMiddle.Y - Box.HalfSize.Y;
		const float High = FromMiddle.Y + Box.HalfSize.Y;
		const float Start = FMath::Clamp(-3.f * Sigma, Low, High);
		const float End = FMath::Clamp(3.f * Sigma, Low, High);
		const float RowStep = (End - Start) / static_cast<float>(Rows);
		float Y = Start + RowStep * 0.5f;
		float Value = 0.f;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			Value += SoftBoxRow(FromMiddle.X, FromMiddle.Y - Y, Sigma, Corner, Box.HalfSize) * Gaussian(Y, Sigma) * RowStep;
			Y += RowStep;
		}
		return FMath::Clamp(Value, 0.f, 1.f);
	}

	void BuildRings(const FVector2f& Center, const FVector2f& HalfSize, const FVector4f& Radii, TConstArrayView<float> Distances,
		float MaxSegment, float PixelScale, bool bCore, FWidgetShapeMesh& Out)
	{
		using namespace WidgetEffectsDrawPrivate;

		Out.Reset();
		if (Distances.Num() == 0 || HalfSize.X <= 0.f || HalfSize.Y <= 0.f)
		{
			return;
		}

		const FVector4f Fitted = FitRadii(Radii, HalfSize * 2.f);
		const float CornerRadius[4] = { Fitted.X, Fitted.Y, Fitted.Z, Fitted.W };

		// Corner middles and the way each corner points (top left, top right, bottom right, bottom left), and where its arc starts.
		const FVector2f Signs[4] = { FVector2f(-1.f, -1.f), FVector2f(1.f, -1.f), FVector2f(1.f, 1.f), FVector2f(-1.f, 1.f) };
		const float StartAngles[4] = { 180.f, 270.f, 0.f, 90.f };
		FVector2f Middles[4];
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Middles[Corner] = Center + FVector2f(Signs[Corner].X * (HalfSize.X - CornerRadius[Corner]), Signs[Corner].Y * (HalfSize.Y - CornerRadius[Corner]));
		}

		// As many steps round a corner as the widest ring's arc needs on screen (about three pixels a step).
		const float Widest = FMath::Max(FMath::Max(Fitted.X, Fitted.Y), FMath::Max(Fitted.Z, Fitted.W)) + FMath::Max(Distances.Last(), 0.f);
		const int32 CornerSteps = FMath::Clamp(FMath::CeilToInt(Widest * PixelScale * UE_HALF_PI / 3.f), 2, 24);

		// Each straight edge (top, right, bottom, left - from one corner's arc to the next's) in pieces at most MaxSegment long.
		int32 EdgeSteps[4];
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const float Length = (Middles[(Corner + 1) % 4] - Middles[Corner]).Size();
			EdgeSteps[Corner] = FMath::Clamp(FMath::CeilToInt(Length / FMath::Max(MaxSegment, 0.5f)), 1, 48);
		}

		int32 PerRing = 0;
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			PerRing += CornerSteps + 1 + (EdgeSteps[Corner] - 1);
		}

		Out.Vertices.Reserve(PerRing * Distances.Num() + 1);
		TArray<FVector2f> Ring;
		Ring.Reserve(PerRing);
		for (const float Distance : Distances)
		{
			Ring.Reset();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const float Reach = CornerRadius[Corner] + Distance;
				FVector2f ArcEnd;
				for (int32 Step = 0; Step <= CornerSteps; ++Step)
				{
					// Inside by more than the corner's rounding, the ring's corner is sharp (every step lands on it).
					const FVector2f Point = Reach >= 0.f
						? Middles[Corner] + DirectionAt(StartAngles[Corner] + 90.f * static_cast<float>(Step) / static_cast<float>(CornerSteps)) * Reach
						: Middles[Corner] + Signs[Corner] * Reach;
					Ring.Add(Point);
					ArcEnd = Point;
				}

				const int32 Next = (Corner + 1) % 4;
				const float NextReach = CornerRadius[Next] + Distance;
				const FVector2f NextStart = NextReach >= 0.f
					? Middles[Next] + DirectionAt(StartAngles[Next]) * NextReach
					: Middles[Next] + Signs[Next] * NextReach;
				for (int32 Step = 1; Step < EdgeSteps[Corner]; ++Step)
				{
					Ring.Add(FMath::Lerp(ArcEnd, NextStart, static_cast<float>(Step) / static_cast<float>(EdgeSteps[Corner])));
				}
			}
			for (const FVector2f& Point : Ring)
			{
				Out.AddVertex(Point, 1.f);
			}
		}

		for (int32 RingIndex = 0; RingIndex + 1 < Distances.Num(); ++RingIndex)
		{
			const uint32 Inner = static_cast<uint32>(RingIndex * PerRing);
			const uint32 Outer = static_cast<uint32>((RingIndex + 1) * PerRing);
			for (int32 Index = 0; Index < PerRing; ++Index)
			{
				const uint32 NextIndex = static_cast<uint32>((Index + 1) % PerRing);
				Out.AddQuad(Inner + Index, Inner + NextIndex, Outer + NextIndex, Outer + Index);
			}
		}

		if (bCore)
		{
			const uint32 Middle = Out.AddVertex(Center, 1.f);
			for (int32 Index = 0; Index < PerRing; ++Index)
			{
				Out.AddTriangle(Middle, static_cast<uint32>((Index + 1) % PerRing), static_cast<uint32>(Index));
			}
		}
	}

	void MakeRingDistances(float Inner, float Outer, float Step, float FocusDistance, float Band, float Fine, int32 MaxRings, TArray<float>& Out)
	{
		Out.Reset();
		if (Outer <= Inner)
		{
			Out.Add(Inner);
			return;
		}

		float Coarse = FMath::Max(Step, 0.05f);
		float Dense = FMath::Clamp(Fine, 0.05f, Coarse);
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			Out.Reset();
			float Distance = Inner;
			while (Distance < Outer && Out.Num() <= MaxRings)
			{
				Out.Add(Distance);
				Distance += FMath::Abs(Distance - FocusDistance) < Band ? Dense : Coarse;
			}
			if (Out.Num() < MaxRings)
			{
				Out.Add(Outer);
				return;
			}
			Coarse *= 1.6f;
			Dense *= 1.6f;
		}
		Out.Add(Outer);
	}

	void PaintMesh(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FWidgetShapeMesh& Mesh,
		const FLinearColor& WidgetTint, TFunctionRef<FLinearColor(const FWidgetShapeMeshVertex&)> ColorOf,
		const FSlateBrush* Brush, TFunction<FVector2f(const FWidgetShapeMeshVertex&)> UVOf)
	{
		if (Mesh.IsEmpty())
		{
			return;
		}

		const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
		TArray<FSlateVertex> Vertices;
		Vertices.SetNumUninitialized(Mesh.Vertices.Num());
		bool bAnyVisible = false;
		for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
		{
			const FWidgetShapeMeshVertex& Vertex = Mesh.Vertices[Index];
			FLinearColor Color = ColorOf(Vertex) * WidgetTint;
			Color.A *= Vertex.Coverage;
			bAnyVisible |= Color.A > 0.002f;
			const FVector2f UV = UVOf ? UVOf(Vertex) : FVector2f::ZeroVector;
			Vertices[Index] = FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Vertex.Position, UV, Color.ToFColor(true));
		}
		if (!bAnyVisible)
		{
			return;
		}

		TArray<SlateIndex> Indices;
		Indices.SetNumUninitialized(Mesh.Indices.Num());
		for (int32 Index = 0; Index < Mesh.Indices.Num(); ++Index)
		{
			Indices[Index] = static_cast<SlateIndex>(Mesh.Indices[Index]);
		}

		const FSlateBrush& DrawBrush = Brush ? *Brush : WidgetEffectsDrawPrivate::GetUntexturedBrush();
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, DrawBrush.GetRenderingResource(), Vertices, Indices, nullptr, 0, 0);
	}

	FWidgetShapeFrame MakeBoxFrame(const FBox2f& Box)
	{
		FWidgetShapeFrame Frame;
		Frame.Center = Box.GetCenter();
		Frame.Size = Box.GetSize();
		return Frame;
	}

	bool PaintMeshWithPaint(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FWidgetShapeMesh& Mesh,
		const FWidgetShapePaint& Paint, const FBox2f& Box, FWidgetSVGBrush& SVGBrush, const SWidget& Widget, const FWidgetStyle& InWidgetStyle,
		const FLinearColor& ColorScale, FWidgetEffectMaterials* Materials, uint64 MaterialKey, const FWidgetEffectMaterialInputs* MaterialInputs, bool bAlongOutline)
	{
		if (Mesh.IsEmpty())
		{
			return true;
		}

		const FSlateBrush* SVGImage = nullptr;
		if (Paint.DrawsSVG())
		{
			SVGImage = SVGBrush.Get(Paint.SVG, nullptr, Geometry, Box.GetSize(), Widget);
			if (!SVGImage)
			{
				return false;
			}
		}
		else
		{
			SVGBrush.Reset();
		}

		const FSlateBrush* MaterialImage = (Materials && Paint.DrawsMaterial())
			? Materials->GetMaterialBrush(MaterialKey, Paint.Material, Paint.MaterialParameters, MaterialInputs) : nullptr;
		const bool bImage = SVGImage || MaterialImage || Paint.DrawsBrush();
		const FSlateBrush* Brush = SVGImage ? SVGImage : MaterialImage ? MaterialImage : bImage ? &Paint.Image : nullptr;
		const FLinearColor BrushTint = (bImage && !SVGImage && !MaterialImage) ? Paint.Image.GetTint(InWidgetStyle) : FLinearColor::White;
		FBox2f UVRegion(ForceInit);
		if (Brush)
		{
			UVRegion = Brush->GetUVRegion();
		}
		const bool bHasUVRegion = bImage && UVRegion.bIsValid && UVRegion.GetArea() > 0.f;
		const FWidgetShapeFrame Frame = MakeBoxFrame(Box);

		TFunction<FVector2f(const FWidgetShapeMeshVertex&)> UVOf;
		if (bImage)
		{
			const bool bAlong = bAlongOutline && Paint.MapsAlongOutline();
			UVOf = [Frame, bHasUVRegion, UVRegion, bAlong, &Paint](const FWidgetShapeMeshVertex& Vertex)
			{
				if (bAlong)
				{
					return Paint.GetOutlineUV(Vertex.Along, Vertex.OutlineLength);
				}
				const FVector2f BoxPoint = Frame.ToBox(Vertex.Position);
				return bHasUVRegion ? UVRegion.Min + BoxPoint * UVRegion.GetSize() : BoxPoint;
			};
		}

		PaintMesh(OutDrawElements, LayerId, Geometry, Mesh, InWidgetStyle.GetColorAndOpacityTint(),
			[&Paint, &Frame, &BrushTint, &ColorScale](const FWidgetShapeMeshVertex& Vertex)
			{
				return Paint.Evaluate(Frame.ToBox(Vertex.Position)) * BrushTint * ColorScale;
			},
			Brush, MoveTemp(UVOf));
		return true;
	}

	const FSlateBrush* GetGrainBrush()
	{
		using namespace WidgetEffectsDrawPrivate;

		static TSharedPtr<FSlateDynamicImageBrush> GrainBrush;
		static bool bTried = false;
		if (!bTried && FSlateApplication::IsInitialized())
		{
			bTried = true;
			if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
			{
				// Specks of either black or white, of random strength: over any color they average out to it, as film grain does.
				FRandomStream Random(0x6A1A55);
				TArray<uint8> Pixels;
				Pixels.SetNumUninitialized(GrainSide * GrainSide * 4);
				for (int32 Pixel = 0; Pixel < GrainSide * GrainSide; ++Pixel)
				{
					const uint8 Shade = Random.FRand() < 0.5f ? 0 : 255;
					const float Strength = FMath::Pow(Random.FRand(), 1.5f);
					Pixels[Pixel * 4 + 0] = Shade;
					Pixels[Pixel * 4 + 1] = Shade;
					Pixels[Pixel * 4 + 2] = Shade;
					Pixels[Pixel * 4 + 3] = static_cast<uint8>(FMath::Clamp(Strength * 255.f, 0.f, 255.f));
				}
				const FName ResourceName(TEXT("NameTBD_GlassGrain"));
				if (Renderer->GenerateDynamicImageResource(ResourceName, GrainSide, GrainSide, Pixels))
				{
					GrainBrush = MakeShared<FSlateDynamicImageBrush>(ResourceName, FVector2D(GrainSide, GrainSide));
				}
			}
		}
		return GrainBrush.Get();
	}

	void BuildGrainMesh(const FWidgetShapeLine& ConvexLine, float CellSize, FWidgetShapeMesh& Out)
	{
		Out.Reset();
		if (ConvexLine.Points.Num() < 3 || CellSize <= 0.f)
		{
			return;
		}

		FBox2f Bounds(ForceInit);
		for (const FVector2f& Point : ConvexLine.Points)
		{
			Bounds += Point;
		}
		// At most 64 tiles each way: a huge panel gets bigger grain rather than thousands of tiles.
		const float Cell = FMath::Max3(CellSize, Bounds.GetSize().X / 64.f, Bounds.GetSize().Y / 64.f);
		const int32 Columns = FMath::Max(1, FMath::CeilToInt(Bounds.GetSize().X / Cell));
		const int32 Rows = FMath::Max(1, FMath::CeilToInt(Bounds.GetSize().Y / Cell));

		TArray<FVector2f> Clipped;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Column = 0; Column < Columns; ++Column)
			{
				const FVector2f Min = Bounds.Min + FVector2f(Column * Cell, Row * Cell);
				const TArray<FVector2f> CellPolygon = { Min, Min + FVector2f(Cell, 0.f), Min + FVector2f(Cell, Cell), Min + FVector2f(0.f, Cell) };
				ClipPolygon(CellPolygon, ConvexLine.Points, Clipped);
				if (Clipped.Num() < 3)
				{
					continue;
				}
				const uint32 First = static_cast<uint32>(Out.Vertices.Num());
				for (const FVector2f& Point : Clipped)
				{
					// The tile's UV rides in Along: 0..1 across this cell.
					Out.AddVertex(Point, 1.f, (Point - Min) / Cell, 0.f);
				}
				for (int32 Index = 1; Index + 1 < Clipped.Num(); ++Index)
				{
					Out.AddTriangle(First, First + Index, First + Index + 1);
				}
			}
		}
	}

	float SignedArea(const TArray<FVector2f>& Polygon)
	{
		float Area = 0.f;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector2f& A = Polygon[Index];
			const FVector2f& B = Polygon[(Index + 1) % Polygon.Num()];
			Area += A.X * B.Y - B.X * A.Y;
		}
		return Area * 0.5f;
	}

	void ClipPolygon(const TArray<FVector2f>& Subject, const TArray<FVector2f>& ConvexClip, TArray<FVector2f>& Out)
	{
		Out = Subject;
		if (ConvexClip.Num() < 3 || Subject.Num() < 3)
		{
			Out.Reset();
			return;
		}

		const float Orientation = SignedArea(ConvexClip) >= 0.f ? 1.f : -1.f;
		TArray<FVector2f> Input;
		for (int32 Edge = 0; Edge < ConvexClip.Num() && Out.Num() > 0; ++Edge)
		{
			const FVector2f& A = ConvexClip[Edge];
			const FVector2f& B = ConvexClip[(Edge + 1) % ConvexClip.Num()];
			const FVector2f Along = B - A;
			if (Along.SizeSquared() < 1e-10f)
			{
				continue;
			}
			auto SideOf = [&A, &Along, Orientation](const FVector2f& Point)
			{
				return (Along.X * (Point.Y - A.Y) - Along.Y * (Point.X - A.X)) * Orientation;
			};

			Input = MoveTemp(Out);
			Out.Reset();
			for (int32 Index = 0; Index < Input.Num(); ++Index)
			{
				const FVector2f& Current = Input[Index];
				const FVector2f& Previous = Input[(Index + Input.Num() - 1) % Input.Num()];
				const float CurrentSide = SideOf(Current);
				const float PreviousSide = SideOf(Previous);
				if (CurrentSide >= 0.f)
				{
					if (PreviousSide < 0.f)
					{
						Out.Add(FMath::Lerp(Previous, Current, PreviousSide / (PreviousSide - CurrentSide)));
					}
					Out.Add(Current);
				}
				else if (PreviousSide >= 0.f)
				{
					Out.Add(FMath::Lerp(Previous, Current, PreviousSide / (PreviousSide - CurrentSide)));
				}
			}
		}
	}

	float Erf(float X)
	{
		const float Sign = X < 0.f ? -1.f : 1.f;
		const float A = FMath::Abs(X);
		const float T = 1.f / (1.f + 0.3275911f * A);
		const float Y = 1.f - (((((1.061405429f * T - 1.453152027f) * T) + 1.421413741f) * T - 0.284496736f) * T + 0.254829592f) * T * FMath::Exp(-A * A);
		return Sign * Y;
	}
}
