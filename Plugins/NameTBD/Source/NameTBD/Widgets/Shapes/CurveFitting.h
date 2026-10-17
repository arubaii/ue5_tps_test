// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "WidgetShapeTypes.h"

/**
 * The Curve's math, apart from drawing: how a smooth line is run through points (the interpolations a Curve offers), and how
 * a line drawn by hand - the canvas Pencil, points added from Blueprint as the pointer moves - is turned into a few points
 * or Bezier segments that follow it.
 *
 * Everything here works on positions in drawn units (Slate units at the size the curve is shown, or any units with the same
 * proportions), never on box fractions: a curve's handles worked out in a box stretched 200 x 80 would bend the wrong way
 * once stretched back. The caller scales positions out of box fractions first and handles back after.
 *
 * Interpolations fill each vertex's Type and handles; positions are kept, except the B-Spline's, which moves its points
 * onto the curve its control points make. A Disconnected vertex is a corner the curve is split at: each run between corners
 * is worked out on its own and ends at the corner as an open line ends. A corner with a Corner Radius is left Straight, so
 * the radius rounds it.
 */
namespace WidgetCurveFitting
{
	/** How sharply (degrees) a line drawn by hand must turn for the fit to keep a corner there rather than round it. */
	inline constexpr double DefaultCornerAngle = 70.0;

	/**
	 * Handles along the line between each point's neighbours, a third of the way to each (times Smoothness): the Curve's first
	 * smoothing, kept as Balanced. Corners are Straight; an open line's ends aim at their neighbour's handle tip.
	 */
	NAMETBD_API void SmoothBalanced(FWidgetShapeContour& Contour, float Smoothness);

	/**
	 * A Catmull-Rom spline through the points, as Bezier handles. Alpha 0 is uniform (can loop and cusp where points bunch
	 * up), 0.5 centripetal (never loops or cusps - the standard for curves through points), 1 chordal (rounder, wider turns).
	 * Smoothness scales every handle (1 is the true spline).
	 */
	NAMETBD_API void SmoothCatmullRom(FWidgetShapeContour& Contour, float Smoothness, float Alpha);

	/**
	 * The smoothest curve through the points: a natural cubic spline (curvature continuous - C2 - and with the least bending
	 * overall), knots spaced by chord length. Every point pulls on the whole run, so moving one ripples along it a little.
	 */
	NAMETBD_API void SmoothNatural(FWidgetShapeContour& Contour, float Smoothness);

	/**
	 * For charts: a curve that never rises above or dips below its points between them (Steffen's monotone cubic). Needs each
	 * run's points to go one way across (left to right or right to left); a run that doesn't is smoothed Catmull-Rom instead.
	 * Smoothness above 1 is taken as 1 (more would bend the curve back on itself).
	 */
	NAMETBD_API void SmoothMonotone(FWidgetShapeContour& Contour, float Smoothness);

	/**
	 * A uniform cubic B-spline: the points are a control cage the curve is drawn inside, through the first and last points of
	 * each run (and corners) only - the softest of all, as Blender's NURBS and the vector tools' B-spline mode. Vertices are
	 * moved onto the curve. Smoothness 0..1 blends from the cage to the full spline.
	 */
	NAMETBD_API void MakeBSpline(FWidgetShapeContour& Contour, float Smoothness);

	/**
	 * Douglas-Peucker: the indices of the points kept when every dropped point is within Tolerance of the line through the
	 * ones kept. The first and last are always kept, and every index in Keep.
	 */
	NAMETBD_API void Simplify(const TArray<FVector2D>& Points, double Tolerance, const TArray<int32>& Keep, TArray<int32>& OutKept);

	/**
	 * Where a line drawn by hand turns sharply: points where it turns more than CornerAngle degrees over a short stretch
	 * (Window units either side), the sharpest of each turn. Not the first or last point.
	 */
	NAMETBD_API void FindCorners(const TArray<FVector2D>& Points, double CornerAngle, double Window, TArray<int32>& OutCorners);

	/** Points closer than MinDistance to the one before dropped (the last kept); a closed line's end that comes back to its start too. */
	NAMETBD_API void RemoveNearDuplicates(TArray<FVector2D>& Points, double MinDistance, bool bClosed);

	/** Each point moved toward the average of its neighbours within Radius units (a Gaussian), ends kept: takes the hand's jitter out. */
	NAMETBD_API void SmoothStroke(TArray<FVector2D>& Points, double Radius);

	/**
	 * Bezier segments that follow a line drawn by hand to within Tolerance (Schneider's algorithm, as the vector tools' pencils
	 * fit): the fewest segments that fit, split where they don't and at corners (Disconnected vertices), smooth everywhere else
	 * (Asymmetric). bClosed: the line comes back to its start and is closed.
	 */
	NAMETBD_API void FitBeziers(const TArray<FVector2D>& Points, double Tolerance, double CornerAngle, bool bClosed, FWidgetShapeContour& Out);

	/** The point a cubic Bezier passes at T. */
	NAMETBD_API FVector2D EvaluateCubic(const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, double T);

	/** The distance from Point to the segment A-B. */
	NAMETBD_API double DistanceToSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B);
}
