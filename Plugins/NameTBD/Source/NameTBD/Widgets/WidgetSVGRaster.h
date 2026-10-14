// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

struct FWidgetSVGColor;

/**
 * Reads and draws SVG text (nanosvg, vendored in Source/ThirdParty/nanosvg). Used by the SVG asset's import, by
 * FWidgetSVGBrush (drawing at the size an SVG is shown, on a worker thread) and by SVG Textures. Thread-safe: every
 * call parses and draws on its own.
 */
namespace WidgetSVGRaster
{
	/** The SVG's size in pixels (its width / height, else its viewBox) and its colours in order of first use. */
	NAMETBD_API bool Read(const FString& Source, FVector2D& OutSize, TArray<FColor>& OutColors, FText* OutError = nullptr);

	/** The colour list for newly found colours: each keeps the Draw As it had in Previous (a new colour draws as itself). */
	NAMETBD_API TArray<FWidgetSVGColor> MergeColors(const TArray<FColor>& Found, const TArray<FWidgetSVGColor>& Previous);

	/**
	 * Draws the SVG at Scale texture pixels per SVG pixel, its longest side held to MaxSide, recoloured by Colors (or all
	 * white, keeping opacity). OutBGRA is BGRA8, straight (not premultiplied) alpha, with edge colours spread into the clear
	 * pixels around them so filtering doesn't pull a dark fringe in.
	 */
	NAMETBD_API bool Draw(const FString& Source, const TArray<FWidgetSVGColor>& Colors, bool bDrawInWhite, float Scale, int32 MaxSide,
		TArray<uint8>& OutBGRA, FIntPoint& OutPixelSize, FText* OutError = nullptr);
}
