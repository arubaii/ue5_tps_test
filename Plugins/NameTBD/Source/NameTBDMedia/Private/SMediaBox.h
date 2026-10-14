// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"

class UMediaBox;

/**
 * What a Media Box draws: its letterbox, the clip's poster, the video fading in over it, the error image, the subtitle line.
 * Everything it draws comes from its UMediaBox; it only lays it out (Fit, Alignment, Crop Focus) and paints it.
 */
class SMediaBox : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMediaBox) {}
		SLATE_ARGUMENT(UMediaBox*, OwnerBox)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	/**
	 * How much bigger than its layout scale Geometry is drawn: the Render Transform scale above it (a Widget Gizmo scale, an
	 * animation). Slate rasterizes text at the layout scale only, so text under a render scale is stretched - blurry. Text laid out
	 * this much bigger, under a render transform shrinking it back, is drawn sharp. 1 without a render scale; steps of 1/8.
	 */
	static float GetCrispScale(const FGeometry& Geometry);

private:
	TWeakObjectPtr<UMediaBox> WeakOwnerBox;

	/** The Error Image as an SVG (Error Image Source SVG), drawn at the size it is shown. */
	mutable FWidgetSVGBrush ErrorSVGBrush;
};
