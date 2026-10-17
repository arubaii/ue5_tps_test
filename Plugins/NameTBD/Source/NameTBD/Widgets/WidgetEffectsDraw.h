// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"

class FSlateWindowElementList;
class FWidgetSVGBrush;
class FWidgetEffectMaterials;
struct FWidgetEffectMaterialInputs;
class SWidget;
struct FGeometry;
struct FSlateBrush;
struct FWidgetShapePaint;
class FWidgetStyle;

/**
 * Drawing shared by the effect widgets - Glass Panel, Drop Shadow, Flip Card, Foldable Dropdown, Advanced Progress Bar - on
 * top of the shapes' geometry (WidgetShapeGeometry): rounded rectangles, soft shadows that are real Gaussian blurs of a
 * rounded rectangle, film grain, and meshes painted with per-vertex colors in one draw each. Everything is plain Slate
 * (custom vertices), so it draws the same on every renderer and platform Slate does.
 *
 * Sizes are in the widget's local space; PixelScale is how many screen pixels a local unit covers.
 */
namespace WidgetEffectsDraw
{
	/** How many screen pixels one local unit covers (each axis's length once transformed - scale, whatever the rotation). */
	NAMETBD_API float GetPixelScale(const FGeometry& Geometry);

	/** The scale rounded to an eighth of a doubling, so a smooth zoom rebuilds a mesh a few times rather than every frame. */
	NAMETBD_API float QuantizeScale(float Scale);

	/** Corner radii (X top left, Y top right, Z bottom right, W bottom left) held to what fits: half the shorter side. */
	NAMETBD_API FVector4f FitRadii(const FVector4f& Radii, const FVector2f& Size);

	/** A hash of corner radii, for mesh caches. */
	inline uint32 HashRadii(const FVector4f& Radii)
	{
		return HashCombineFast(HashCombineFast(GetTypeHash(Radii.X), GetTypeHash(Radii.Y)), HashCombineFast(GetTypeHash(Radii.Z), GetTypeHash(Radii.W)));
	}

	/** Appends a rounded rectangle from Min, Size across, as one closed line (clockwise on screen). Radii as FitRadii. */
	NAMETBD_API void AppendRoundedRect(FWidgetShapeOutline& Out, const FVector2f& Min, const FVector2f& Size, const FVector4f& Radii, float Tolerance);

	/** The signed distance from Point to a rounded rectangle (negative inside), Radii per quadrant as FitRadii. */
	NAMETBD_API float RoundedRectDistance(const FVector2f& Point, const FVector2f& Center, const FVector2f& HalfSize, const FVector4f& Radii);

	/** The rounded rectangle's outward normal nearest Point: the distance's gradient. */
	NAMETBD_API FVector2f RoundedRectNormal(const FVector2f& Point, const FVector2f& Center, const FVector2f& HalfSize, const FVector4f& Radii);

	/** A rounded rectangle blurred: what a soft shadow is. Sigma is the Gaussian's (a CSS blur radius is twice it). */
	struct FSoftBox
	{
		FVector2f Center = FVector2f::ZeroVector;
		FVector2f HalfSize = FVector2f::ZeroVector;
		FVector4f Radii = FVector4f(0.f, 0.f, 0.f, 0.f);
		float Sigma = 0.f;
	};

	/**
	 * How much of the blurred box covers Point, 0 to 1: the rounded rectangle convolved with a Gaussian - exact along x, six
	 * samples along y (Evan Wallace's method), so a box smaller than its blur is as faint as it should be. Sigma 0 is the hard
	 * box, anti-aliased over AntiAlias local units.
	 */
	NAMETBD_API float EvaluateSoftBox(const FSoftBox& Box, const FVector2f& Point, float AntiAlias);

	/**
	 * A mesh of rings round a rounded rectangle, one per Distances entry (signed: negative inside, ascending), every ring the
	 * same vertices so neighbours join by quads. Edges are cut at most MaxSegment apart so a value that changes along them
	 * (an offset shadow) is followed. bCore fans the innermost ring in to the middle. Coverage is left at 1, for the caller.
	 */
	NAMETBD_API void BuildRings(const FVector2f& Center, const FVector2f& HalfSize, const FVector4f& Radii, TConstArrayView<float> Distances,
		float MaxSegment, float PixelScale, bool bCore, FWidgetShapeMesh& Out);

	/** Distances from Inner to Outer (signed), at most Step apart, denser - Fine apart - within Band of FocusDistance. */
	NAMETBD_API void MakeRingDistances(float Inner, float Outer, float Step, float FocusDistance, float Band, float Fine, int32 MaxRings, TArray<float>& Out);

	/**
	 * Paints a mesh with one untextured (or Brush-textured) draw: each vertex colored ColorOf(vertex) x its Coverage x the
	 * widget's tint. UVOf gives texture coordinates when a Brush is given.
	 */
	NAMETBD_API void PaintMesh(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FWidgetShapeMesh& Mesh,
		const FLinearColor& WidgetTint, TFunctionRef<FLinearColor(const FWidgetShapeMeshVertex&)> ColorOf,
		const FSlateBrush* Brush = nullptr, TFunction<FVector2f(const FWidgetShapeMeshVertex&)> UVOf = nullptr);

	/**
	 * Paints a mesh with a shape paint (color, gradient, image, SVG or material) laid over Box (local space): what a shape's fill
	 * does. The mesh should already be cut for the paint's gradient (WidgetShapeGeometry::CutForGradient with MakeBoxFrame(Box)).
	 * ColorScale multiplies every color (a hover brightening, an opacity). False while an SVG's first drawing isn't ready.
	 * A Material paint is drawn through Materials (its instance kept under MaterialKey, handed MaterialInputs); without
	 * Materials it is drawn as its Color. bAlongOutline: an image or material laid along the outline (U from each vertex's Along).
	 */
	NAMETBD_API bool PaintMeshWithPaint(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FWidgetShapeMesh& Mesh,
		const FWidgetShapePaint& Paint, const FBox2f& Box, FWidgetSVGBrush& SVGBrush, const SWidget& Widget, const FWidgetStyle& InWidgetStyle,
		const FLinearColor& ColorScale = FLinearColor::White, FWidgetEffectMaterials* Materials = nullptr, uint64 MaterialKey = 0,
		const FWidgetEffectMaterialInputs* MaterialInputs = nullptr, bool bAlongOutline = false);

	/** The frame a shape paint is laid over for a box in local space: unturned, its middle and size. */
	NAMETBD_API FWidgetShapeFrame MakeBoxFrame(const FBox2f& Box);

	/**
	 * Film grain: a 128 x 128 tile of black and white specks of random strength (alpha), made once. Drawn with
	 * BuildGrainMesh, which tiles it without relying on the sampler wrapping.
	 */
	NAMETBD_API const FSlateBrush* GetGrainBrush();

	/** The grain tiled over a convex outline (its first line), CellSize local units per tile; UVs in each vertex's Along. */
	NAMETBD_API void BuildGrainMesh(const FWidgetShapeLine& ConvexLine, float CellSize, FWidgetShapeMesh& Out);

	/** Clips a polygon by a convex one (Sutherland-Hodgman). Both clockwise or both counter-clockwise. */
	NAMETBD_API void ClipPolygon(const TArray<FVector2f>& Subject, const TArray<FVector2f>& ConvexClip, TArray<FVector2f>& Out);

	/** A polygon's signed area (positive counter-clockwise in a y-up sense: clockwise on screen). */
	NAMETBD_API float SignedArea(const TArray<FVector2f>& Polygon);

	/** The error function, to 1.5e-7 (Abramowitz and Stegun 7.1.26). */
	NAMETBD_API float Erf(float X);
}
