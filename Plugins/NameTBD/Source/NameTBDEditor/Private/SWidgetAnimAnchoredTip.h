// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SMenuAnchor;
class FActiveTimerHandle;

/**
 * A tooltip that stays put: wraps a control, and after a short hover shows its text in a small popup centred just
 * above it - not beside the cursor, and not following it - in the Widget Animations panel's own dark tooltip
 * style. It goes away when the mouse leaves or the control is pressed.
 *
 * Use it in place of ToolTipText on compact controls laid out in a row - icon buttons, toggles in a pill - where
 * a tooltip that chases the cursor covers the very controls beside it.
 *
 *     SNew(SWidgetAnimAnchoredTip)
 *     .Text(LOCTEXT("PlayTip", "Play (Space)"))
 *     [
 *         SNew(SButton) ...
 *     ]
 *
 * The popup is drawn in the control's own window and never takes focus or the mouse, so it can't swallow a click
 * or close a menu.
 */
class SWidgetAnimAnchoredTip : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimAnchoredTip)
		: _ShowDelay(0.45f)
		, _WrapWidth(280.f)
	{}

		/** What the tip says. Read each time it opens, so it can follow the control's state. Empty shows nothing. */
		SLATE_ATTRIBUTE(FText, Text)

		/** Seconds of hovering before it shows. */
		SLATE_ARGUMENT(float, ShowDelay)

		/** Longest a line of the tip runs, in pixels, before it wraps. */
		SLATE_ARGUMENT(float, WrapWidth)

		/** The control the tip is for. */
		SLATE_DEFAULT_SLOT(FArguments, Content)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:

	EActiveTimerReturnType OpenTip(double InCurrentTime, float InDeltaTime);
	void CloseTip();
	TSharedRef<SWidget> MakeTipContent() const;

	TSharedPtr<SMenuAnchor> Anchor;
	TSharedPtr<FActiveTimerHandle> PendingOpen;

	TAttribute<FText> Text;
	float ShowDelay = 0.45f;
	float WrapWidth = 280.f;
};