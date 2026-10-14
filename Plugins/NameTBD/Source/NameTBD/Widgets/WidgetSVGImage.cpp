// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetSVGImage.h"

#include "Rendering/DrawElements.h"
#include "WidgetSVG.h"

#define LOCTEXT_NAMESPACE "WidgetSVGImage"

// ---- SWidgetSVGImage --------------------------------------------------------------------------------------------

void SWidgetSVGImage::Construct(const FArguments& InArgs)
{
}

void SWidgetSVGImage::SetSVG(UWidgetSVG* InSVG)
{
	if (SVG.Get() != InSVG)
	{
		SVG = InSVG;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SWidgetSVGImage::SetTint(const FLinearColor& InTint)
{
	if (Tint != InTint)
	{
		Tint = InTint;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SWidgetSVGImage::SetFit(EWidgetSVGFit InFit)
{
	if (Fit != InFit)
	{
		Fit = InFit;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SWidgetSVGImage::SetDesiredSizeOverride(const TOptional<FVector2D>& InSize)
{
	if (DesiredSizeOverride != InSize)
	{
		DesiredSizeOverride = InSize;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

FVector2D SWidgetSVGImage::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	if (DesiredSizeOverride.IsSet())
	{
		return DesiredSizeOverride.GetValue();
	}
	const UWidgetSVG* ShownSVG = SVG.Get();
	return ShownSVG ? ShownSVG->SVGSize : FVector2D::ZeroVector;
}

int32 SWidgetSVGImage::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const UWidgetSVG* ShownSVG = SVG.Get();
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2f BoxSize(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	if (!ShownSVG || BoxSize.X <= 0.f || BoxSize.Y <= 0.f)
	{
		return LayerId;
	}

	// Contain: the biggest box at the SVG's proportions that fits, centred.
	FVector2f DrawSize = BoxSize;
	if (Fit == EWidgetSVGFit::Contain && ShownSVG->SVGSize.X > 0.0 && ShownSVG->SVGSize.Y > 0.0)
	{
		const float FitScale = FMath::Min(BoxSize.X / static_cast<float>(ShownSVG->SVGSize.X), BoxSize.Y / static_cast<float>(ShownSVG->SVGSize.Y));
		DrawSize = FVector2f(static_cast<float>(ShownSVG->SVGSize.X), static_cast<float>(ShownSVG->SVGSize.Y)) * FitScale;
	}
	const FGeometry DrawGeometry = AllottedGeometry.MakeChild(DrawSize, FSlateLayoutTransform((BoxSize - DrawSize) * 0.5f));

	const FSlateBrush* Brush = SVGBrush.Get(ShownSVG, nullptr, DrawGeometry, DrawSize, *this);
	if (!Brush)
	{
		return LayerId;
	}

	const ESlateDrawEffect Effects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, DrawGeometry.ToPaintGeometry(), Brush, Effects,
		InWidgetStyle.GetColorAndOpacityTint() * Tint);
	return LayerId;
}

// ---- UWidgetSVGImage --------------------------------------------------------------------------------------------

TSharedRef<SWidget> UWidgetSVGImage::RebuildWidget()
{
	MySVGImage = SNew(SWidgetSVGImage);
	return MySVGImage.ToSharedRef();
}

void UWidgetSVGImage::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (MySVGImage.IsValid())
	{
		MySVGImage->SetSVG(SVG);
		MySVGImage->SetTint(ColorAndOpacity);
		MySVGImage->SetFit(Fit);
		MySVGImage->SetDesiredSizeOverride(bOverrideImageSize ? TOptional<FVector2D>(ImageSize) : TOptional<FVector2D>());
	}
}

void UWidgetSVGImage::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MySVGImage.Reset();
}

void UWidgetSVGImage::SetSVG(UWidgetSVG* InSVG)
{
	SVG = InSVG;
	if (MySVGImage.IsValid())
	{
		MySVGImage->SetSVG(SVG);
	}
}

void UWidgetSVGImage::SetColorAndOpacity(FLinearColor InColorAndOpacity)
{
	ColorAndOpacity = InColorAndOpacity;
	if (MySVGImage.IsValid())
	{
		MySVGImage->SetTint(ColorAndOpacity);
	}
}

void UWidgetSVGImage::SetFit(EWidgetSVGFit InFit)
{
	Fit = InFit;
	if (MySVGImage.IsValid())
	{
		MySVGImage->SetFit(Fit);
	}
}

void UWidgetSVGImage::SetImageSize(FVector2D InImageSize)
{
	bOverrideImageSize = true;
	ImageSize = InImageSize;
	if (MySVGImage.IsValid())
	{
		MySVGImage->SetDesiredSizeOverride(ImageSize);
	}
}

void UWidgetSVGImage::ClearImageSize()
{
	bOverrideImageSize = false;
	if (MySVGImage.IsValid())
	{
		MySVGImage->SetDesiredSizeOverride(TOptional<FVector2D>());
	}
}

#if WITH_EDITOR
const FText UWidgetSVGImage::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
