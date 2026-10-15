// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"
#include "NameTBD/Menu/Settings/MenuSettingTypes.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"

/**
 * Draws a menu image (FMenuImage: a texture / material, or an SVG drawn at its on-screen size) at a fixed size, aspect kept.
 * Tab icons, key glyphs, option previews.
 */
class NAMETBD_API SMenuImage : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuImage)
		: _ImageSize(FVector2D(24.f, 24.f))
		, _Tint(FLinearColor::White)
	{}
		SLATE_ARGUMENT(FMenuImage, Image)
		/** 0 on an axis: from the other axis and the image's aspect. */
		SLATE_ARGUMENT(FVector2D, ImageSize)
		SLATE_ATTRIBUTE(FSlateColor, Tint)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetImage(const FMenuImage& InImage);
	void SetImageSize(const FVector2D& InSize);
	bool HasImage() const { return MenuImage.HasImage(); }

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FVector2D GetSourceSize() const;

	FMenuImage MenuImage;
	FVector2D ImageSize = FVector2D(24.f, 24.f);
	TAttribute<FSlateColor> Tint;
	FSlateBrush TextureBrush;
	mutable FWidgetSVGBrush SVGBrush;
};
