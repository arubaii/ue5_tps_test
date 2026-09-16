// Copyright AR Game Systems 2026. All rights reserved.

#include "ContextMenuBuilderBase.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Blueprint/WidgetTree.h"

void UContextMenuBuilderBase::BuildFromData(const FContextMenuButtonData& InButtonData)
{
	if (!WidgetTree)
	{
		return;
	}

	// 1. Populate the Bound Text Blocks
	if (LeftText) LeftText->SetText(InButtonData.ButtonText_Left);
	if (CenterText) CenterText->SetText(InButtonData.ButtonText_Center);
	if (RightText) RightText->SetText(InButtonData.ButtonText_Right);

	// 2. Dynamically Build the Icon Layouts
	BuildIconBox(LeftIconsBox, InButtonData.LeftIcons);
	BuildIconBox(CenterIconsBox, InButtonData.CenterIcons);
	BuildIconBox(RightIconsBox, InButtonData.RightIcons);
}

void UContextMenuBuilderBase::BuildIconBox(UHorizontalBox* TargetBox, const TArray<FContextButtonImageChoice>& Icons)
{
	if (!TargetBox)
	{
		return;
	}

	// Clear out old generated widgets before rebuilding
	TargetBox->ClearChildren();

	for (const FContextButtonImageChoice& Choice : Icons)
	{
		if (Choice.IconChoice == EImageChoice::Icon)
		{
			UImage* NewImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			if (NewImage)
			{
				if (UTexture2D* LoadedTexture = Choice.Icon.LoadSynchronous())
				{
					NewImage->SetBrushFromTexture(LoadedTexture);
				}
				
				NewImage->SetDesiredSizeOverride(IconSize);

				UHorizontalBoxSlot* HBoxSlot = TargetBox->AddChildToHorizontalBox(NewImage);
				if (HBoxSlot)
				{
					HBoxSlot->SetPadding(FMargin(2.0f, 0.0f, 0.0f, 0.0f));
					HBoxSlot->SetVerticalAlignment(VAlign_Center);
				}
			}
		}
		else if (Choice.IconChoice == EImageChoice::Spacer)
		{
			// TODO: Build Spacer widget
		}
		else if (Choice.IconChoice == EImageChoice::Divider)
		{
			// TODO: Build Divider widget
		}
	}
}