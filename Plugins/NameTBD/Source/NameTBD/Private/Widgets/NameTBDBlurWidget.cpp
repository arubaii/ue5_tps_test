// Copyright NameTBD Plugin. All Rights Reserved.

#include "Widgets/NameTBDBlurWidget.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "NameTBD"

UNameTBDBlurWidget::UNameTBDBlurWidget()
{
	bIsVariable = true;
}

TSharedRef<SWidget> UNameTBDBlurWidget::RebuildWidget()
{
	MyBlurWidget = SNew(SNameTBDBlurWidget)
		.Style(GlassStyle)
		.Padding(ContentPadding)
		.HAlign(HorizontalAlignment)
		.VAlign(VerticalAlignment);

	if (GetChildrenCount() > 0)
	{
		if (UWidget* ChildContent = GetContentSlot()->Content)
		{
			MyBlurWidget->SetContent(ChildContent->TakeWidget());
		}
	}

	return MyBlurWidget.ToSharedRef();
}

void UNameTBDBlurWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyBlurWidget.IsValid())
	{
		MyBlurWidget->SetStyle(GlassStyle);
		MyBlurWidget->SetContentPadding(ContentPadding);
	}
}

void UNameTBDBlurWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyBlurWidget.Reset();
}

void UNameTBDBlurWidget::OnSlotAdded(UPanelSlot* InSlot)
{
	if (MyBlurWidget.IsValid() && InSlot && InSlot->Content)
	{
		MyBlurWidget->SetContent(InSlot->Content->TakeWidget());
	}
}

void UNameTBDBlurWidget::OnSlotRemoved(UPanelSlot* InSlot)
{
	(void)InSlot;
	if (MyBlurWidget.IsValid())
	{
		MyBlurWidget->SetContent(SNullWidget::NullWidget);
	}
}

void UNameTBDBlurWidget::SetBlurStrength(float InBlurStrength)
{
	GlassStyle.BlurStrength = FMath::Max(0.0f, InBlurStrength);
	if (MyBlurWidget.IsValid())
	{
		MyBlurWidget->SetStyle(GlassStyle);
	}
}

void UNameTBDBlurWidget::SetSurfaceTint(FLinearColor InSurfaceTint)
{
	GlassStyle.SurfaceTint = InSurfaceTint;
	if (MyBlurWidget.IsValid())
	{
		MyBlurWidget->SetStyle(GlassStyle);
	}
}

void UNameTBDBlurWidget::SetThemePreset(ENameTBDGlassThemeMode InThemePreset)
{
	GlassStyle.ThemePreset = InThemePreset;
	if (MyBlurWidget.IsValid())
	{
		MyBlurWidget->SetStyle(GlassStyle);
	}
}

bool UNameTBDBlurWidget::SetActiveDesignTargetByID_Implementation(FName InTargetID, bool bAnimateTransition)
{
	(void)bAnimateTransition;
	for (const FNameTBDDesignTarget& Target : DesignTargets)
	{
		if (Target.TargetID == InTargetID)
		{
			ActiveDesignTargetID = Target.TargetID;
			GlassStyle.SurfaceTint = Target.PrimaryColor;
			GlassStyle.CornerRadii = Target.CornerRadii;
			if (MyBlurWidget.IsValid())
			{
				MyBlurWidget->SetStyle(GlassStyle);
			}
			return true;
		}
	}
	return false;
}

bool UNameTBDBlurWidget::SetActiveDesignTargetByValue_Implementation(float NormalizedValue, bool bAnimateTransition)
{
	for (const FNameTBDDesignTarget& Target : DesignTargets)
	{
		if (NormalizedValue >= Target.MinValueThreshold && NormalizedValue <= Target.MaxValueThreshold)
		{
			return Execute_SetActiveDesignTargetByID(this, Target.TargetID, bAnimateTransition);
		}
	}
	return false;
}

#if WITH_EDITOR
const FText UNameTBDBlurWidget::GetPaletteCategory()
{
	return LOCTEXT("NameTBDPaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
