// Copyright AR GameSystems 2026. All rights reserved.

#include "ContextMenuChevron.h"

#include "Rendering/DrawElements.h"

#define LOCTEXT_NAMESPACE "ContextMenuChevron"

void SContextMenuChevron::Construct(const FArguments& InArgs)
{
	ChevronStyle = InArgs._ChevronStyle;
	ArrowColor = InArgs._ArrowColor;
}

void SContextMenuChevron::SetChevronStyle(const FContextMenuChevronStyle& InStyle)
{
	ChevronStyle = InStyle;
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SContextMenuChevron::SetArrowColor(const FLinearColor& InColor)
{
	if (ArrowColor != InColor)
	{
		ArrowColor = InColor;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FVector2D SContextMenuChevron::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// The strokes stick out by half their width on every side.
	return ChevronStyle.Size + FVector2D(ChevronStyle.Thickness);
}

int32 SContextMenuChevron::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Centred on the geometry, not on a font's baseline: the arrow sits in the middle of the row whatever the text does.
	const FVector2f Centre = FVector2f(AllottedGeometry.GetLocalSize()) * 0.5f + FVector2f(ChevronStyle.Offset);
	const FVector2f Half = FVector2f(ChevronStyle.Size) * 0.5f;
	TArray<FVector2f> Points;
	Points.Add(FVector2f(Centre.X - Half.X, Centre.Y - Half.Y));
	Points.Add(FVector2f(Centre.X + Half.X, Centre.Y));
	Points.Add(FVector2f(Centre.X - Half.X, Centre.Y + Half.Y));

	const FLinearColor Tint = ArrowColor * InWidgetStyle.GetColorAndOpacityTint();
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points,
		ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect, Tint, ChevronStyle.bAntiAlias, ChevronStyle.Thickness);
	return LayerId;
}

void UContextMenuChevron::SetArrowColor(FLinearColor InColor)
{
	ArrowColor = InColor;
	if (ChevronWidget.IsValid())
	{
		ChevronWidget->SetArrowColor(ArrowColor);
	}
}

void UContextMenuChevron::SetChevron(const FContextMenuChevronStyle& InChevron)
{
	Chevron = InChevron;
	if (ChevronWidget.IsValid())
	{
		ChevronWidget->SetChevronStyle(Chevron);
	}
}

TSharedRef<SWidget> UContextMenuChevron::RebuildWidget()
{
	ChevronWidget = SNew(SContextMenuChevron)
		.ChevronStyle(Chevron)
		.ArrowColor(ArrowColor);
	return ChevronWidget.ToSharedRef();
}

void UContextMenuChevron::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (ChevronWidget.IsValid())
	{
		ChevronWidget->SetChevronStyle(Chevron);
		ChevronWidget->SetArrowColor(ArrowColor);
	}
}

void UContextMenuChevron::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ChevronWidget.Reset();
}

#if WITH_EDITOR
const FText UContextMenuChevron::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
