// Copyright AR GameSystems 2026. All rights reserved.

#include "SInfoCardDesignerPreview.h"

#include "Layout/ArrangedChildren.h"
#include "Rendering/SlateLayoutTransform.h"

FInfoCardDesignerPlacement FInfoCardDesignerPlacement::FromSpec(const FInfoCardSpec& Spec)
{
	FInfoCardDesignerPlacement Placement;
	Placement.PositionMode      = Spec.PositionMode;
	Placement.IconPosition      = Spec.IconPosition;
	Placement.CursorPosition    = Spec.CursorPosition;
	Placement.FillIconPadding   = Spec.FillIconPadding;
	Placement.Offset            = Spec.CardOffset;
	Placement.IconAnchorPadding = Spec.IconAnchorPadding;
	return Placement;
}

SInfoCardDesignerPreview::SInfoCardDesignerPreview()
	: PanelChildren(this)
{
}

void SInfoCardDesignerPreview::Construct(const FArguments& InArgs)
{
	PanelChildren.Add(InArgs._Content.Widget);
}

void SInfoCardDesignerPreview::SetCard(const TSharedPtr<SWidget>& Card, const FInfoCardDesignerPlacement& Placement)
{
	CardPlacement = Placement;
	if (Card == CardWidget)
	{
		Invalidate(EInvalidateWidgetReason::Layout);
		return;
	}

	// The anchor's border stays at index 0 throughout; the card, when there is one, is always index 1.
	while (PanelChildren.Num() > 1)
	{
		PanelChildren.RemoveAt(PanelChildren.Num() - 1);
	}

	CardWidget = Card;
	if (CardWidget.IsValid())
	{
		PanelChildren.Add(CardWidget.ToSharedRef());
	}
	Invalidate(EInvalidateWidgetReason::ChildOrder);
}

FVector2D SInfoCardDesignerPreview::ComputeCardPosition(const FInfoCardDesignerPlacement& Placement, const FVector2D& AnchorSize, const FVector2D& CardSize)
{
	// The same arithmetic UInfoCardWidget::PositionToIconRaw / PositionToCursorRaw use at runtime, with the anchor's box at
	// the origin instead of in viewport space.
	const FVector2D Center = AnchorSize * 0.5f;
	const float Padding = Placement.IconAnchorPadding;
	FVector2D Result = FVector2D::ZeroVector;

	if (Placement.PositionMode == EInfoCardPosition::ToIcon)
	{
		switch (Placement.IconPosition)
		{
		case EDirection2D::Up:        Result = FVector2D(Center.X - CardSize.X * 0.5f, -CardSize.Y - Padding); break;
		case EDirection2D::Down:      Result = FVector2D(Center.X - CardSize.X * 0.5f, AnchorSize.Y + Padding); break;
		case EDirection2D::Left:      Result = FVector2D(-CardSize.X - Padding, Center.Y - CardSize.Y * 0.5f); break;
		case EDirection2D::Right:     Result = FVector2D(AnchorSize.X + Padding, Center.Y - CardSize.Y * 0.5f); break;
		case EDirection2D::UpLeft:    Result = FVector2D(-CardSize.X * 0.5f - Padding, -CardSize.Y * 0.5f - Padding); break;
		case EDirection2D::UpRight:   Result = FVector2D(AnchorSize.X - CardSize.X * 0.5f + Padding, -CardSize.Y * 0.5f - Padding); break;
		case EDirection2D::DownLeft:  Result = FVector2D(-CardSize.X * 0.5f - Padding, AnchorSize.Y - CardSize.Y * 0.5f + Padding); break;
		case EDirection2D::DownRight: Result = FVector2D(AnchorSize.X - CardSize.X * 0.5f + Padding, AnchorSize.Y - CardSize.Y * 0.5f + Padding); break;
		case EDirection2D::FillIcon:  Result = FVector2D(Placement.FillIconPadding.Left, Placement.FillIconPadding.Top); break;
		}
		return Result;
	}

	if (Placement.PositionMode == EInfoCardPosition::ToViewport)
	{
		// No viewport in the designer: shown beside the anchor so it can still be read.
		return FVector2D(AnchorSize.X + 8.f, Center.Y - CardSize.Y * 0.5f);
	}

	// To Cursor, with the anchor's centre standing in for the cursor.
	const FVector2D& Offset = Placement.Offset;
	switch (Placement.CursorPosition)
	{
	case EInfoCardCursorPosition::UpRight:     Result = FVector2D(Center.X + Offset.X, Center.Y - CardSize.Y - Offset.Y); break;
	case EInfoCardCursorPosition::UpLeft:      Result = FVector2D(Center.X - CardSize.X - Offset.X, Center.Y - CardSize.Y - Offset.Y); break;
	case EInfoCardCursorPosition::DownRight:   Result = FVector2D(Center.X + Offset.X, Center.Y + Offset.Y); break;
	case EInfoCardCursorPosition::DownLeft:    Result = FVector2D(Center.X - CardSize.X - Offset.X, Center.Y + Offset.Y); break;
	case EInfoCardCursorPosition::UpCenter:    Result = FVector2D(Center.X - CardSize.X * 0.5f + Offset.X, Center.Y - CardSize.Y - Offset.Y); break;
	case EInfoCardCursorPosition::DownCenter:  Result = FVector2D(Center.X - CardSize.X * 0.5f + Offset.X, Center.Y + Offset.Y); break;
	case EInfoCardCursorPosition::LeftCenter:  Result = FVector2D(Center.X - CardSize.X - Offset.X, Center.Y - CardSize.Y * 0.5f + Offset.Y); break;
	case EInfoCardCursorPosition::RightCenter: Result = FVector2D(Center.X + Offset.X, Center.Y - CardSize.Y * 0.5f + Offset.Y); break;
	}
	return Result;
}

void SInfoCardDesignerPreview::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const FVector2D AnchorSize = AllottedGeometry.GetLocalSize();

	if (PanelChildren.Num() > 0)
	{
		const TSharedRef<SWidget> Content = ConstCastSharedRef<SWidget>(PanelChildren.GetChildAt(0));
		if (ArrangedChildren.Accepts(Content->GetVisibility()))
		{
			ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(Content, AnchorSize, FSlateLayoutTransform()));
		}
	}

	if (PanelChildren.Num() > 1)
	{
		const TSharedRef<SWidget> Card = ConstCastSharedRef<SWidget>(PanelChildren.GetChildAt(1));
		if (ArrangedChildren.Accepts(Card->GetVisibility()))
		{
			const bool bFill = CardPlacement.PositionMode == EInfoCardPosition::ToIcon && CardPlacement.IconPosition == EDirection2D::FillIcon;
			const FVector2D CardSize = bFill
				? (AnchorSize - FVector2D(CardPlacement.FillIconPadding.GetTotalSpaceAlong<Orient_Horizontal>(), CardPlacement.FillIconPadding.GetTotalSpaceAlong<Orient_Vertical>())).ComponentMax(FVector2D::ZeroVector)
				: Card->GetDesiredSize();
			const FVector2D Position = ComputeCardPosition(CardPlacement, AnchorSize, CardSize);
			ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(Card, CardSize, FSlateLayoutTransform(Position)));
		}
	}
}

FVector2D SInfoCardDesignerPreview::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// Only the anchor's own content takes part in layout; the card floats.
	if (PanelChildren.Num() > 0)
	{
		const TSharedRef<SWidget> Content = ConstCastSharedRef<SWidget>(PanelChildren.GetChildAt(0));
		if (Content->GetVisibility() != EVisibility::Collapsed)
		{
			return Content->GetDesiredSize();
		}
	}
	return FVector2D::ZeroVector;
}
