// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Widgets/SPanel.h"
#include "Layout/Children.h"

#include "InfoCardTypes.h"

/** Where the designer shows a previewed card, read off the entry being previewed. */
struct FInfoCardDesignerPlacement
{
	EInfoCardPosition PositionMode = EInfoCardPosition::ToIcon;
	EDirection2D IconPosition = EDirection2D::Up;
	EInfoCardCursorPosition CursorPosition = EInfoCardCursorPosition::UpRight;
	FMargin FillIconPadding = FMargin(0.f);
	FVector2D Offset = FVector2D::ZeroVector;
	float IconAnchorPadding = 0.f;

	static FInfoCardDesignerPlacement FromSpec(const FInfoCardSpec& Spec);
};

/**
 * Design time only: what an Info Card Anchor hands the UMG designer instead of its bare border, so the card it is previewing
 * can be drawn where the game would put it - beside, above or over the anchor - without taking part in layout.
 *
 * Two children. The first is the anchor's own SBorder, arranged over the whole allotted box, and the only one asked for a
 * desired size, so wrapping a widget in an anchor never moves anything in the designer. The second, when there is one, is the
 * previewed card at its desired size, arranged outside the box by FInfoCardDesignerPlacement (Slate draws children outside
 * their parent's box unless something above clips).
 *
 * The designer has no cursor and no game viewport, so To Cursor treats the anchor's centre as the cursor, and To Viewport is
 * shown to the anchor's right. Clamping, flipping and overlap avoidance are runtime-only.
 */
class NAMETBD_API SInfoCardDesignerPreview : public SPanel
{
public:

	SLATE_BEGIN_ARGS(SInfoCardDesignerPreview) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	SInfoCardDesignerPreview();

	void Construct(const FArguments& InArgs);

	/** Shows Card (null: none) placed by Placement. */
	void SetCard(const TSharedPtr<SWidget>& Card, const FInfoCardDesignerPlacement& Placement);

	/** The card's top-left in the anchor's local space, for a card of CardSize beside an anchor of AnchorSize. */
	static FVector2D ComputeCardPosition(const FInfoCardDesignerPlacement& Placement, const FVector2D& AnchorSize, const FVector2D& CardSize);

	// SPanel
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual FChildren* GetChildren() override { return &PanelChildren; }

protected:

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:

	TSlotlessChildren<SWidget> PanelChildren;
	TSharedPtr<SWidget> CardWidget;
	FInfoCardDesignerPlacement CardPlacement;
};
