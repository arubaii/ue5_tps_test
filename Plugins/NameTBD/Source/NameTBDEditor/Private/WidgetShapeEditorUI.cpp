// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetShapeEditorUI.h"

#include "NameTBDColors.h"
#include "NameTBDStyle.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace WidgetShapeEditorUIImpl
{
	namespace Palette = NameTBD::Colors::ShapeEditor;

	/** A button that draws nothing itself: the chip's own rounded border is its look. */
	const FButtonStyle& BareButtonStyle()
	{
		static const FButtonStyle Style = FButtonStyle()
			.SetNormal(FSlateNoResource())
			.SetHovered(FSlateNoResource())
			.SetPressed(FSlateNoResource())
			.SetDisabled(FSlateNoResource())
			.SetNormalPadding(FMargin(0.f))
			.SetPressedPadding(FMargin(0.f));
		return Style;
	}

	const FSlateBrush* RoundedBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 4.f);
		return &Brush;
	}
}

namespace WidgetShapeEditorUI
{
	TSharedRef<SWidget> MakeChip(const FChip& Chip)
	{
		using namespace WidgetShapeEditorUIImpl;

		// The button is made after the lambdas that ask whether it is hovered, so they reach it through this.
		const TSharedRef<TWeakPtr<SButton>> Self = MakeShared<TWeakPtr<SButton>>();

		auto Active = [Chip]() { return Chip.IsActive && Chip.IsActive(); };
		auto Enabled = [Chip]() { return !Chip.IsEnabled || Chip.IsEnabled(); };
		auto Hovered = [Self]() { const TSharedPtr<SButton> Button = Self->Pin(); return Button.IsValid() && Button->IsHovered(); };

		auto Background = [Active, Hovered]()
		{
			if (Active())
			{
				return FSlateColor(Hovered() ? Palette::ChipActiveHovered : Palette::ChipActive);
			}
			return FSlateColor(Hovered() ? Palette::ChipIdleHovered : Palette::ChipIdle);
		};
		auto Foreground = [Active, Enabled]()
		{
			const FLinearColor Color = Active() ? Palette::ChipTextActive : Palette::ChipText;
			return FSlateColor(Enabled() ? Color : Color.CopyWithNewOpacity(Palette::ChipDisabledOpacity));
		};

		auto IconBrush = [Chip]() -> const FSlateBrush*
		{
			const FName Name = Chip.GetIcon ? Chip.GetIcon() : Chip.Icon;
			return Name.IsNone() ? nullptr : FNameTBDStyle::FindIcon(Name);
		};
		auto LabelText = [Chip]() { return Chip.GetLabel ? Chip.GetLabel() : Chip.Label; };
		const bool bHasIcon = !Chip.Icon.IsNone() || static_cast<bool>(Chip.GetIcon);
		const bool bHasLabel = !Chip.Label.IsEmpty() || static_cast<bool>(Chip.GetLabel);

		TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);
		if (bHasIcon)
		{
			Content->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, bHasLabel ? 5.f : 0.f, 0.f))
				[
					SNew(SImage)
					.DesiredSizeOverride(FVector2D(14.f, 14.f))
					.Image_Lambda(IconBrush)
					.ColorAndOpacity_Lambda(Foreground)
					.Visibility_Lambda([IconBrush]() { return IconBrush() ? EVisibility::Visible : EVisibility::Collapsed; })
				];
		}
		if (bHasLabel || bHasIcon)
		{
			// An icon whose file is missing falls back to the tooltip's name as its label, so the chip never comes up blank.
			Content->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity_Lambda(Foreground)
					.Text_Lambda([LabelText, IconBrush, Chip, bHasLabel]()
					{
						if (bHasLabel)
						{
							return LabelText();
						}
						if (IconBrush())
						{
							return FText::GetEmpty();
						}
						FString Name = (Chip.GetToolTip ? Chip.GetToolTip() : Chip.ToolTip).ToString();
						int32 Colon = INDEX_NONE;
						if (Name.FindChar(TEXT(':'), Colon))
						{
							Name.LeftInline(Colon);
						}
						return FText::FromString(Name);
					})
					.Visibility_Lambda([bHasLabel, IconBrush]() { return bHasLabel || !IconBrush() ? EVisibility::Visible : EVisibility::Collapsed; })
				];
		}

		TSharedRef<SButton> Button = SNew(SButton)
			.ButtonStyle(&BareButtonStyle())
			.ContentPadding(FMargin(0.f))
			.IsEnabled_Lambda(Enabled)
			.ToolTipText_Lambda([Chip]() { return Chip.GetToolTip ? Chip.GetToolTip() : Chip.ToolTip; })
			.OnClicked_Lambda([Chip]()
			{
				if (Chip.OnClick)
				{
					Chip.OnClick();
				}
				return FReply::Handled();
			})
			[
				SNew(SBox)
				.HeightOverride(Palette::ChipHeight)
				.MinDesiredWidth(Palette::ChipHeight)
				[
					SNew(SBorder)
					.BorderImage(RoundedBrush())
					.BorderBackgroundColor_Lambda(Background)
					.Padding(FMargin(bHasLabel ? 8.f : 4.f, 0.f))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						Content
					]
				]
			];
		*Self = Button;
		return Button;
	}
}
