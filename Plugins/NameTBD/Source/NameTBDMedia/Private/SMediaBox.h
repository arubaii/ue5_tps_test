// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

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

private:
	TWeakObjectPtr<UMediaBox> WeakOwnerBox;
};
