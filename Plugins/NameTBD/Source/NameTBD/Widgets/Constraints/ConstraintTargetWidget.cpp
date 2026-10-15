// Copyright AR GameSystems 2026. All rights reserved.

#include "ConstraintTargetWidget.h"

#include "Rendering/DrawElements.h"

#define LOCTEXT_NAMESPACE "ConstraintTargetWidget"

// =====================================================================================================
// SConstraintTargetMarker
// =====================================================================================================

void SConstraintTargetMarker::Construct(const FArguments& InArgs)
{
	SetCanTick(false);
}

void SConstraintTargetMarker::SetMarker(const FVector2D& InMarkerSize, const FLinearColor& InMarkerColor, bool bInDrawn)
{
	const bool bResized = !MarkerSize.Equals(InMarkerSize);
	MarkerSize = FVector2D(FMath::Max(InMarkerSize.X, 0.0), FMath::Max(InMarkerSize.Y, 0.0));
	MarkerColor = InMarkerColor;
	bDrawn = bInDrawn;
	Invalidate(bResized ? EInvalidateWidgetReason::Layout : EInvalidateWidgetReason::Paint);
}

int32 SConstraintTargetMarker::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!bDrawn)
	{
		return LayerId;
	}

	// A crosshair through its middle and a ring round it - drawn over what is under it, never hit.
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2f Size(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	const FVector2f Middle = Size * 0.5f;
	const float Reach = FMath::Max(FMath::Min(Size.X, Size.Y) * 0.5f, 4.f);
	const FLinearColor Tint = MarkerColor * InWidgetStyle.GetColorAndOpacityTint();
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();

	TArray<FVector2f> Line;
	Line.Add(Middle - FVector2f(Reach + 4.f, 0.f));
	Line.Add(Middle + FVector2f(Reach + 4.f, 0.f));
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeometry, Line, ESlateDrawEffect::None, Tint, true, 1.f);
	Line[0] = Middle - FVector2f(0.f, Reach + 4.f);
	Line[1] = Middle + FVector2f(0.f, Reach + 4.f);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeometry, Line, ESlateDrawEffect::None, Tint, true, 1.f);

	constexpr int32 Steps = 24;
	TArray<FVector2f> Ring;
	Ring.Reserve(Steps + 1);
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const float Angle = UE_TWO_PI * Step / Steps;
		Ring.Add(Middle + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Reach);
	}
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeometry, Ring, ESlateDrawEffect::None, Tint, true, 1.f);
	return LayerId;
}

// =====================================================================================================
// UConstraintTargetWidget
// =====================================================================================================

UConstraintTargetWidget::UConstraintTargetWidget()
{
	// A point, not something to click: whatever is under it keeps the pointer.
	SetVisibilityInternal(ESlateVisibility::HitTestInvisible);
}

TSharedRef<SWidget> UConstraintTargetWidget::RebuildWidget()
{
	MyMarker = SNew(SConstraintTargetMarker);
	return MyMarker.ToSharedRef();
}

void UConstraintTargetWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyMarker.IsValid())
	{
		MyMarker->SetMarker(MarkerSize, MarkerColor, IsDesignTime() || bShowAtRuntime);
	}
}

void UConstraintTargetWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyMarker.Reset();
}

void UConstraintTargetWidget::SetTargetOffset(FVector2D Point)
{
	SetRenderTranslation(Point);
}

#if WITH_EDITOR
const FText UConstraintTargetWidget::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
