// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimAnchoredTip.h"

#include "NameTBDColors.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
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

	// The control's name, then what it does. Written "Name: what it does" - the name is everything before the first
	// ": ", when that is short enough to be one. A short tip with no ": " is all name ("Go to Start"); a long one is
	// all description.
	constexpr int32 MaxNameLength = 32;
	const FString Whole = Text.Get().ToString();
	FString Name;
	FString Description = Whole;

	int32 Colon = INDEX_NONE;
	if (Whole.FindChar(TEXT(':'), Colon) && Colon > 0 && Colon <= MaxNameLength && Whole.Mid(Colon + 1, 1) == TEXT(" "))
	{
		Name = Whole.Left(Colon).TrimStartAndEnd();
		Description = Whole.Mid(Colon + 1).TrimStartAndEnd();
	}
	else if (Whole.Len() <= MaxNameLength)
	{
		Name = Whole;
		Description.Reset();
	}

	const TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);

	if (!Name.IsEmpty())
	{
		Lines->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(Name))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(Palette::TooltipControlName)
		];
	}

	if (!Description.IsEmpty())
	{
		Lines->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, Name.IsEmpty() ? 0.f : 3.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Description))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(Palette::TooltipValue)
			.WrapTextAt(WrapWidth)
		];
	}

	// Never hit-testable: a tip under the cursor would take the hover from the control, which would close it.
	return SNew(SBox)
		.Visibility(EVisibility::HitTestInvisible)
		.Padding(FMargin(0.f, 0.f, 0.f, 5.f))
		[
			SNew(SBorder)
			.BorderImage(AnchoredTipBrush())
			.Padding(FMargin(12.f, 8.f))
			[
				Lines
			]
		];
}