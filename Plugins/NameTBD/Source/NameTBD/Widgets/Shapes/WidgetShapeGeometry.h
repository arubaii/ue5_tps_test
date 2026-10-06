// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

struct FWidgetShapePath;
struct FWidgetShapeStroke;
struct FWidgetShapeTrim;
struct FWidgetShapePaint;

/**
 * Where a shape's box sits in its widget: its middle, its size and how it is turned. A shape is described in box
 * fractions - (0, 0) the box's top left, (1, 1) its bottom right - and drawn in the widget's local space through this.
 */
struct NAMETBD_API FWidgetShapeFrame
{
	FVector2f Center = FVector2f::ZeroVector;
	FVector2f Size = FVector2f(1.f, 1.f);
	float Cos = 1.f;
	float Sin = 0.f;

	FVector2f ToLocal(const FVector2f& BoxPoint) const
	{
		const FVector2f FromMiddle = (BoxPoint - FVector2f(0.5f, 0.5f)) * Size;
		return Center + FVector2f(FromMiddle.X * Cos - FromMiddle.Y * Sin, FromMiddle.X * Sin + FromMiddle.Y * Cos);
	}

	FVector2f ToBox(const FVector2f& LocalPoint) const
	{
		const FVector2f Offset = LocalPoint - Center;
		const FVector2f Unturned(Offset.X * Cos + Offset.Y * Sin, -Offset.X * Sin + Offset.Y * Cos);
		return FVector2f(
			Size.X != 0.f ? Unturned.X / Size.X : 0.f,
			Size.Y != 0.f ? Unturned.Y / Size.Y : 0.f) + FVector2f(0.5f, 0.5f);
	}

	/** A direction in box fractions, in local space - for handles. */
	FVector2f VectorToLocal(const FVector2f& BoxVector) const
	{
		const FVector2f Scaled = BoxVector * Size;
		return FVector2f(Scaled.X * Cos - Scaled.Y * Sin, Scaled.X * Sin + Scaled.Y * Cos);
	}

	/**
	 * The frame for a widget of LocalSize. bKeepAspect: as large as fits at Aspect (width over height), centered. Scale
	 * shrinks or grows the box about its middle; Rotation turns it, in degrees, clockwise.
	 */
	static FWidgetShapeFrame Make(const FVector2f& LocalSize, bool bKeepAspect, float Aspect, const FVector2f& Scale, float RotationDegrees);
};

/** One contour of a shape's outline, flattened to points in local space. */
struct NAMETBD_API FWidgetShapeLine
{
	TArray<FVector2f> Points;
	bool bClosed = true;
};

/** A shape's whole outline, flattened. */
struct NAMETBD_API FWidgetShapeOutline
{
	TArray<FWidgetShapeLine> Lines;

	void Reset() { Lines.Reset(); }
};

/** A vertex of a shape's mesh before it is colored: where it is, and how much of it is covered (the anti-aliased edge fades to 0). */
struct FWidgetShapeMeshVertex
{
	FVector2f Position = FVector2f::ZeroVector;
	float Coverage = 1.f;

	FWidgetShapeMeshVertex() = default;
	FWidgetShapeMeshVertex(const FVector2f& InPosition, float InCoverage) : Position(InPosition), Coverage(InCoverage) {}
};

/** Triangles, as indices into Vertices. */
struct NAMETBD_API FWidgetShapeMesh
{
	TArray<FWidgetShapeMeshVertex> Vertices;
	TArray<uint32> Indices;

	void Reset() { Vertices.Reset(); Indices.Reset(); }
	bool IsEmpty() const { return Indices.Num() == 0; }

	uint32 AddVertex(const FVector2f& Position, float Coverage)
	{
		return static_cast<uint32>(Vertices.Add(FWidgetShapeMeshVertex(Position, Coverage)));
	}

	void AddTriangle(uint32 A, uint32 B, uint32 C)
	{
		Indices.Add(A);
		Indices.Add(B);
		Indices.Add(C);
	}

	void AddQuad(uint32 A, uint32 B, uint32 C, uint32 D)
	{
		AddTriangle(A, B, C);
		AddTriangle(A, C, D);
	}
};

/**
 * Turning a shape into triangles: its path flattened to points, corners rounded, the fill triangulated (holes and all),
 * the stroke built with its joins, caps, dashes and trim, both edged with a one-pixel anti-aliasing fade, and the mesh cut
 * where a gradient needs it to be drawn exactly.
 *
 * Every size here is in the widget's local space. PixelScale is how many screen pixels one local unit covers, so the
 * curves are as smooth as the screen needs and the fade is a pixel wide wherever the shape is scaled to.
 */
namespace WidgetShapeGeometry
{
	/**
	 * How far a flattened curve may stray from the true one, in local units: a twentieth of a pixel. The edge's fade is drawn
	 * from the flattened outline, so a fifth of a pixel already showed as a faint faceting along big circles.
	 */
	NAMETBD_API float GetTolerance(float PixelScale);

	/** Appends a cubic Bezier as points, its start too when bIncludeStart. As few points as Tolerance allows. */
	NAMETBD_API void AppendCubic(TArray<FVector2f>& Out, const FVector2f& P0, const FVector2f& P1, const FVector2f& P2, const FVector2f& P3, float Tolerance, bool bIncludeStart);

	/**
	 * Appends an arc of an ellipse in box fractions, mapped through Frame: Radii across and down, StartAngle and Sweep in
	 * radians, 0 at the top and clockwise. Includes its start when bIncludeStart.
	 */
	NAMETBD_API void AppendEllipseArc(TArray<FVector2f>& Out, const FWidgetShapeFrame& Frame, const FVector2f& Center, const FVector2f& Radii, float StartAngle, float Sweep, float Tolerance, bool bIncludeStart);

	/** Flattens a Custom Shape's path (box fractions) to local space through Frame, rounding Straight vertices by their Corner Radius. */
	NAMETBD_API void FlattenPath(const FWidgetShapePath& Path, const FWidgetShapeFrame& Frame, float Tolerance, FWidgetShapeOutline& Out);

	/**
	 * Flattens closed polygons given as corner points in local space, rounding every corner by CornerRadii (one per point,
	 * Slate units). For the polygon shapes - Triangle, Star, N-gon - which round each corner as a Straight vertex is rounded.
	 */
	NAMETBD_API void AppendRoundedPolygon(FWidgetShapeOutline& Out, const TArray<FVector2f>& Corners, const TArray<float>& CornerRadii, float Tolerance);

	/**
	 * The fill: every closed contour (an open one is closed across its ends), contours inside an odd number of others
	 * cutting holes. AntiAliasWidth 0 draws hard edges. OutFillOnLeft gets, per line, +1 when the filled side is to the
	 * left of its direction, -1 to the right, 0 when the line encloses nothing - what a stroke's Inside and Outside follow.
	 */
	NAMETBD_API void BuildFill(const FWidgetShapeOutline& Outline, float AntiAliasWidth, FWidgetShapeMesh& Out, TArray<int8>& OutFillOnLeft);

	/** Which side of each line is filled, as BuildFill works it out, without building the fill. */
	NAMETBD_API void ClassifyLines(const FWidgetShapeOutline& Outline, TArray<int8>& OutFillOnLeft);

	/** The stroke along the outline: trimmed, dashed, aligned, joined and capped. FillOnLeft from BuildFill or ClassifyLines. */
	NAMETBD_API void BuildStroke(const FWidgetShapeOutline& Outline, const TArray<int8>& FillOnLeft, const FWidgetShapeStroke& Stroke, const FWidgetShapeTrim& Trim, float AntiAliasWidth, float Tolerance, FWidgetShapeMesh& Out);

	/**
	 * Cuts a mesh so Paint's gradient can be colored per vertex and still be exact: a Linear Gradient along the lines its
	 * stops fall on (the colors between are then exactly linear), a Radial Gradient into a grid of cells CellSize across,
	 * small enough that the rings read round. Nothing for a flat color or an image.
	 */
	NAMETBD_API void CutForGradient(FWidgetShapeMesh& Mesh, const FWidgetShapePaint& Paint, const FWidgetShapeFrame& Frame, float CellSize);

	/** Whether a local point is inside the outline's fill (even-odd, open contours closed across). */
	NAMETBD_API bool IsInside(const FWidgetShapeOutline& Outline, const FVector2f& LocalPoint);

	/** The length of a line of points, closed back to its start when bClosed. */
	NAMETBD_API float GetLength(const TArray<FVector2f>& Points, bool bClosed);
}
