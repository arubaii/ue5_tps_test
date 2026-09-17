// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimBuilderDetails.h"

#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBDColors.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimBuilderDetails"

namespace
{
	// ---------------------------------------------------------------------------------------------
	// The button's colours. Change these and nothing else.
	// ---------------------------------------------------------------------------------------------
	
	
	
	const FLinearColor CreateTextColor = FLinearColor(0.8, 0.8, 0.8, 1.0f);
	const FLinearColor CreateTextDisabledColor(0.5f, 0.5f, 0.5f, 1.0f);
	constexpr float ButtonBrightness = 0.9f;
	const float CreateCornerRadius = 4.f;

	/**
	 * Built once and handed out by pointer, because SButton keeps the pointer rather than a copy and the
	 * style has to outlive every button wearing it.
	 *
	 * Rounded box brushes rather than images: no art to ship, and the color is a value in this file.
	 */
	const FButtonStyle& CreateButtonStyle()
	{
		static const FButtonStyle Style = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(NameTBD::Colors::AnimationOrange_Normal.CopyWithNewOpacity(ButtonBrightness), CreateCornerRadius))
			.SetHovered(FSlateRoundedBoxBrush(NameTBD::Colors::AnimationOrange_Hovered.CopyWithNewOpacity(ButtonBrightness), CreateCornerRadius))
			.SetPressed(FSlateRoundedBoxBrush(NameTBD::Colors::AnimationOrange_Pressed.CopyWithNewOpacity(ButtonBrightness), CreateCornerRadius))
			.SetDisabled(FSlateRoundedBoxBrush(NameTBD::Colors::AnimationOrange_Disabled.CopyWithNewOpacity(ButtonBrightness), CreateCornerRadius))
			.SetNormalPadding(FMargin(8.f, 4.f))
			.SetPressedPadding(FMargin(8.f, 5.f, 8.f, 3.f));

		return Style;
	}
}

TSharedRef<IDetailCustomization> FWidgetAnimBuilderDetails::MakeInstance()
{
	return MakeShared<FWidgetAnimBuilderDetails>();
}

void FWidgetAnimBuilderDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Widget Animation");
	FSlateFontInfo OutlinedFont = FAppStyle::Get().GetFontStyle("PropertyWindow.BoldFont");

	// 2. Configure the outline settings
	OutlinedFont.OutlineSettings.OutlineSize = 1; // Size of the outline in pixels
	OutlinedFont.OutlineSettings.OutlineColor = FLinearColor::Black;
	
	// A whole row rather than a name/value pair: there is no property here, only an action, and the
	// property editor's own CallInEditor button is laid out the same way.
	Category.AddCustomRow(LOCTEXT("CreateFilter", "Create Widget Animation Asset"))
		.WholeRowContent()
		.HAlign(HAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(&CreateButtonStyle())
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.IsEnabled(this, &FWidgetAnimBuilderDetails::IsCreateEnabled)
			.ToolTipText(this, &FWidgetAnimBuilderDetails::GetCreateTooltip)
			.OnClicked(this, &FWidgetAnimBuilderDetails::OnCreateClicked)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CreateLabel", "Create Widget Animation Asset"))
				.Font(OutlinedFont) // Pass the modified font here
				.ColorAndOpacity_Lambda([this]()
				{
				   return FSlateColor(IsCreateEnabled() ? CreateTextColor : CreateTextDisabledColor);
				})
			]
		];
}

bool FWidgetAnimBuilderDetails::IsCreateEnabled() const
{
	// Greyed out rather than refused with a notification, for the one case the function already rejects:
	// what is on screen while Preview Animation is set is not what would be saved.
	for (const TWeakObjectPtr<UObject>& Object : CustomizedObjects)
	{
		const UWidgetAnimBuilder* Builder = Cast<UWidgetAnimBuilder>(Object.Get());
		if (Builder && !Builder->PreviewAnimation && Builder->Layers.Num() > 0)
		{
			return true;
		}
	}

	return false;
}

FText FWidgetAnimBuilderDetails::GetCreateTooltip() const
{
	for (const TWeakObjectPtr<UObject>& Object : CustomizedObjects)
	{
		const UWidgetAnimBuilder* Builder = Cast<UWidgetAnimBuilder>(Object.Get());
		if (!Builder)
		{
			continue;
		}

		if (Builder->PreviewAnimation)
		{
			return LOCTEXT("CreateBlockedByPreview", "Clear Preview Animation first. This saves Layers, not the animation being previewed.");
		}

		if (Builder->Layers.Num() == 0)
		{
			return LOCTEXT("CreateBlockedByLayers", "Add at least one layer before creating a Widget Animation.");
		}
	}

	return LOCTEXT("CreateTooltip", "Save Layers and Description as a new WA_ Widget Animation asset");
}

FReply FWidgetAnimBuilderDetails::OnCreateClicked()
{
	for (const TWeakObjectPtr<UObject>& Object : CustomizedObjects)
	{
		if (UWidgetAnimBuilder* Builder = Cast<UWidgetAnimBuilder>(Object.Get()))
		{
			Builder->CreateWidgetAnimation();
		}
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE