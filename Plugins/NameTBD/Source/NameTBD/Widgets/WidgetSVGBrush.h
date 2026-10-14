// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

class FWidgetSVGDrawing;
class SWidget;
class UWidgetSVG;
struct FGeometry;

/**
 * Draws an SVG asset for a Slate widget at the size it is shown on screen, so it is sharp at any size: one per image a
 * widget draws (an SVG Image has one; a shape one for its fill and one for its stroke). Game thread only (Slate paint).
 *
 * A drawing is an image of the SVG at one size in pixels, made on a worker thread (or at once while nothing is shown
 * yet and it is small) and held as a Slate dynamic image. Drawings are shared: two widgets showing the same SVG at the
 * same size use one. When the size changes (a resize, a scale animation, the DPI) a new drawing is asked for and the
 * last one is shown, stretched, until it arrives; one is made at a time, so an animated size doesn't queue a drawing per
 * frame. An edit to the SVG (its colours, a reimport) draws it again.
 */
class NAMETBD_API FWidgetSVGBrush
{
public:

	/**
	 * The brush to draw SVG with over LocalSize (Slate units) of Geometry: Template's look (Tint, Draw As, Margin, Tiling,
	 * outline; with no Template, a plain Image) with the SVG drawn at that size in screen pixels as its image, Image Size
	 * LocalSize. Null with no SVG, and while its first drawing isn't ready (Widget is repainted when it is).
	 */
	const FSlateBrush* Get(const UWidgetSVG* SVG, const FSlateBrush* Template, const FGeometry& Geometry, const FVector2f& LocalSize, const SWidget& Widget);

	/** The same at a fixed size in pixels, Image Size Template's (Niagara particles: one drawing for every particle). */
	const FSlateBrush* GetAtPixelSize(const UWidgetSVG* SVG, const FSlateBrush* Template, const FIntPoint& PixelSize, const SWidget* Widget);

	/** Lets go of the drawings (the widget's resources are released). */
	void Reset();

	/** The size in screen pixels LocalSize of Geometry covers: its render transforms and the DPI scale included. */
	static FIntPoint GetPixelSize(const FGeometry& Geometry, const FVector2f& LocalSize);

	/** The longest side a drawing is made at, in pixels. Bigger is drawn at this and stretched. */
	static constexpr int32 MaxPixelSide = 4096;

private:

	/** The drawing shown (ready). */
	TSharedPtr<FWidgetSVGDrawing> Shown;

	/** The drawing for the size and revision last asked for: Shown once ready. */
	TSharedPtr<FWidgetSVGDrawing> Wanted;

	/** The brush handed out: Shown's image with the template's look. */
	FSlateBrush Composed;
};
