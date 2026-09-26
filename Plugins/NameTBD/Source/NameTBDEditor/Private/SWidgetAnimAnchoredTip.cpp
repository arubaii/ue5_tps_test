// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimAnchoredTip.h"

#include "NameTBDColors.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** The same dark, faintly outlined panel the Track Editor's tooltips are drawn on. */
	const FSlateBrush* AnchoredTipBrush()
	{
		namespace Palette = NameTBD::Colors::WidgetAnimEditor;
		static const FSlateRoundedBoxBrush Brush(Palette::TooltipBackground, 4.f, Palette::TooltipOutline, 1.f);
		return &Brush;
	}
}

void SWidgetAnimAnchoredTip::Construct(const FArguments& InArgs)
{
	Text = InArgs._Text;
	ShowDelay = InArgs._ShowDelay;
	WrapWidth = InArgs._WrapWidth;

	// In this window, off the application's menu stack: a popup there would take focus, close any open menu,
	// and treat the next click anywhere as a click to dismiss it.
	ChildSlot
	[
		SAssignNew(Anchor, SMenuAnchor)
		.Placement(MenuPlacement_CenteredAboveAnchor)
		.Method(EPopupMethod::UseCurrentWindow)
		.UseApplicationMenuStack(false)
		.OnGetMenuContent(FOnGetContent::CreateSP(this, &SWidgetAnimAnchoredTip::MakeTipContent))
		[
			InArgs._Content.Widget
		]
	];
}

void SWidgetAnimAnchoredTip::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);

	if (!PendingOpen.IsValid() && !(Anchor.IsValid() && Anchor->IsOpen()))
	{
		PendingOpen = RegisterActiveTimer(ShowDelay, FWidgetActiveTimerDelegate::CreateSP(this, &SWidgetAnimAnchoredTip::OpenTip));
	}
}

void SWidgetAnimAnchoredTip::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	CloseTip();
}

FReply SWidgetAnimAnchoredTip::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Pressing the control is using it: the tip is in the way. The press itself carries on to the control.
	CloseTip();
	return FReply::Unhandled();
}

EActiveTimerReturnType SWidgetAnimAnchoredTip::OpenTip(double InCurrentTime, float InDeltaTime)
{
	PendingOpen.Reset();

	if (Anchor.IsValid() && IsHovered() && !Text.Get().IsEmpty())
	{
		Anchor->SetIsOpen(/*InIsOpen=*/true, /*bFocusMenu=*/false);
	}
	return EActiveTimerReturnType::Stop;
}

void SWidgetAnimAnchoredTip::CloseTip()
{
	if (PendingOpen.IsValid())
	{
		UnRegisterActiveTimer(PendingOpen.ToSharedRef());
		PendingOpen.Reset();
	}

	if (Anchor.IsValid() && Anchor->IsOpen())
	{
		Anchor->SetIsOpen(false);
	}
}

TSharedRef<SWidget> SWidgetAnimAnchoredTip::MakeTipContent() const
{
	namespace Palette = NameTBD::Colors::WidgetAnimEditor;

	// Never hit-testable: a tip under the cursor would take the hover from the control, which would close it.
	return SNew(SBox)
		.Visibility(EVisibility::HitTestInvisible)
		.Padding(FMargin(0.f, 0.f, 0.f, 5.f))
		[
			SNew(SBorder)
			.BorderImage(AnchoredTipBrush())
			.Padding(FMargin(9.f, 5.f))
			[
				SNew(STextBlock)
				.Text(Text.Get())
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(Palette::TooltipValue)
				.WrapTextAt(WrapWidth)
			]
		];
}