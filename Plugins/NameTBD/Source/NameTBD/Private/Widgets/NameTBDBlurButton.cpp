// Copyright NameTBD Plugin. All Rights Reserved.

#include "Widgets/NameTBDBlurButton.h"
#include "Widgets/Input/SButton.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "NameTBD"

UNameTBDBlurButton::UNameTBDBlurButton()
{
	bIsVariable = true;
	GlassStyle.BlurStrength = 12.0f;
	GlassStyle.CornerRadii = FVector4(20.0, 20.0, 20.0, 20.0);
}

TSharedRef<SWidget> UNameTBDBlurButton::RebuildWidget()
{
	MyBlurSurface = SNew(SNameTBDBlurWidget)
		.Style(GlassStyle)
		.Padding(ContentPadding)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center);

	if (GetChildrenCount() > 0)
	{
		if (UWidget* ChildContent = GetContentSlot()->Content)
		{
			MyBlurSurface->SetContent(ChildContent->TakeWidget());
		}
	}

	MyInputButton = SNew(SButton)
		.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
		.ContentPadding(FMargin(0.0f))
		.OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked, HandleSlateClicked))
		.OnPressed(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleSlatePressed))
		.OnReleased(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleSlateReleased))
		.OnHovered_UObject(this, &UNameTBDBlurButton::HandleSlateHovered)
		.OnUnhovered_UObject(this, &UNameTBDBlurButton::HandleSlateUnhovered)
		[
			MyBlurSurface.ToSharedRef()
		];

	return MyInputButton.ToSharedRef();
}

void UNameTBDBlurButton::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyBlurSurface.IsValid())
	{
		MyBlurSurface->SetStyle(GlassStyle);
		MyBlurSurface->SetContentPadding(ContentPadding);
	}
}

void UNameTBDBlurButton::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyBlurSurface.Reset();
	MyInputButton.Reset();
}

void UNameTBDBlurButton::OnSlotAdded(UPanelSlot* InSlot)
{
	if (MyBlurSurface.IsValid() && InSlot && InSlot->Content)
	{
		MyBlurSurface->SetContent(InSlot->Content->TakeWidget());
	}
}

void UNameTBDBlurButton::OnSlotRemoved(UPanelSlot* InSlot)
{
	(void)InSlot;
	if (MyBlurSurface.IsValid())
	{
		MyBlurSurface->SetContent(SNullWidget::NullWidget);
	}
}

FReply UNameTBDBlurButton::HandleSlateClicked()
{
	OnClicked.Broadcast();
	return FReply::Handled();
}

void UNameTBDBlurButton::HandleSlatePressed()
{
	bIsPressedState = true;
	RefreshInteractiveState();
	OnPressed.Broadcast();
}

void UNameTBDBlurButton::HandleSlateReleased()
{
	bIsPressedState = false;
	RefreshInteractiveState();
	OnReleased.Broadcast();
}

void UNameTBDBlurButton::HandleSlateHovered()
{
	bIsHoveredState = true;
	RefreshInteractiveState();
	OnHovered.Broadcast();
}

void UNameTBDBlurButton::HandleSlateUnhovered()
{
	bIsHoveredState = false;
	RefreshInteractiveState();
	OnUnhovered.Broadcast();
}

void UNameTBDBlurButton::RefreshInteractiveState()
{
	if (!MyBlurSurface.IsValid())
	{
		return;
	}

	if (bIsPressedState)
	{
		MyBlurSurface->SetInteractiveModulation(HoverBlurMultiplier * 1.1f, HoverTintBrightnessOffset * 0.5f, HoverRimBoost * 1.25f);
		SetRenderScale(FVector2D(PressedScale, PressedScale));
	}
	else if (bIsHoveredState)
	{
		MyBlurSurface->SetInteractiveModulation(HoverBlurMultiplier, HoverTintBrightnessOffset, HoverRimBoost);
		SetRenderScale(FVector2D(HoverScale, HoverScale));
	}
	else
	{
		MyBlurSurface->SetInteractiveModulation(1.0f, 0.0f, 0.0f);
		SetRenderScale(FVector2D(1.0, 1.0));
	}
}

bool UNameTBDBlurButton::SetActiveDesignTargetByID_Implementation(FName InTargetID, bool bAnimateTransition)
{
	(void)bAnimateTransition;
	for (const FNameTBDDesignTarget& Target : DesignTargets)
	{
		if (Target.TargetID == InTargetID)
		{
			ActiveDesignTargetID = Target.TargetID;
			GlassStyle.SurfaceTint = Target.PrimaryColor;
			if (Target.ShapePrimitive == ENameTBDShapePrimitive::Circle)
			{
				GlassStyle.CornerRadii = FVector4(999.0, 999.0, 999.0, 999.0);
			}
			else if (Target.ShapePrimitive == ENameTBDShapePrimitive::Square)
			{
				GlassStyle.CornerRadii = FVector4(0.0, 0.0, 0.0, 0.0);
			}
			else
			{
				GlassStyle.CornerRadii = Target.CornerRadii;
			}

			if (MyBlurSurface.IsValid())
			{
				MyBlurSurface->SetStyle(GlassStyle);
			}
			return true;
		}
	}
	return false;
}

bool UNameTBDBlurButton::SetActiveDesignTargetByValue_Implementation(float NormalizedValue, bool bAnimateTransition)
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
const FText UNameTBDBlurButton::GetPaletteCategory()
{
	return LOCTEXT("NameTBDPaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
