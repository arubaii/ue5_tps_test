// Copyright AR GameSystems 2026. All rights reserved.

#include "SMenuImage.h"

#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "NameTBD/Widgets/WidgetSVG.h"

void SMenuImage::Construct(const FArguments& InArgs)
{
	ImageSize = InArgs._ImageSize;
	Tint = InArgs._Tint;
	SetImage(InArgs._Image);
	SetCanTick(false);
}

void SMenuImage::SetImage(const FMenuImage& InImage)
{
	MenuImage = InImage;
	TextureBrush = FSlateBrush();
	if (MenuImage.ImageSource == EWidgetImageSource::Texture && MenuImage.Image)
	{
		TextureBrush.SetResourceObject(MenuImage.Image);
		TextureBrush.DrawAs = ESlateBrushDrawType::Image;
		TextureBrush.ImageSize = FVector2f(GetSourceSize());
	}
	SVGBrush.Reset();
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SMenuImage::SetImageSize(const FVector2D& InSize)
{
	if (ImageSize != InSize)
	{
		ImageSize = InSize;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

FVector2D SMenuImage::GetSourceSize() const
{
	if (const UTexture2D* Texture = Cast<UTexture2D>(MenuImage.Image))
	{
		return FVector2D(FMath::Max(1, Texture->GetSizeX()), FMath::Max(1, Texture->GetSizeY()));
	}
	return FVector2D(1.f, 1.f);
}

FVector2D SMenuImage::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	if (!MenuImage.HasImage())
	{
		return FVector2D::ZeroVector;
	}
	const FVector2D Source = GetSourceSize();
	const double Aspect = Source.X / FMath::Max(1.0, Source.Y);
	FVector2D Size = ImageSize;
	if (Size.X <= 0.0)
	{
		Size.X = Size.Y * Aspect;
	}
	if (Size.Y <= 0.0)
	{
		Size.Y = Size.X / FMath::Max(0.01, Aspect);
	}
	return Size;
}

int32 SMenuImage::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!MenuImage.HasImage())
	{
		return LayerId;
	}
	const FLinearColor Color = MenuImage.Tint * Tint.Get(FSlateColor(FLinearColor::White)).GetColor(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint();
	const ESlateDrawEffect Effect = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	if (MenuImage.ImageSource == EWidgetImageSource::SVG)
	{
		const FVector2f LocalSize(AllottedGeometry.GetLocalSize());
		if (const FSlateBrush* Brush = SVGBrush.Get(MenuImage.SVG, nullptr, AllottedGeometry, LocalSize, *this))
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Brush, Effect, Color);
		}
		return LayerId;
	}
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &TextureBrush, Effect, Color);
	return LayerId;
}
