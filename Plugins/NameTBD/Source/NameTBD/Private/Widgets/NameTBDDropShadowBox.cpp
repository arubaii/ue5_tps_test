// Copyright NameTBD Plugin. All Rights Reserved.

#include "Widgets/NameTBDDropShadowBox.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "NameTBD"

namespace NameTBDDropShadowRegistry
{
	static TMap<TWeakObjectPtr<const UWidget>, TWeakObjectPtr<UNameTBDDropShadowComponent>> GWidgetShadowComponents;
}

UNameTBDDropShadowComponent* UNameTBDDropShadowComponent::AttachDropShadowToWidget(UWidget* TargetWidget, FNameTBDDropShadowStyle InStyle)
{
	if (!TargetWidget)
	{
		return nullptr;
	}

	if (UNameTBDDropShadowComponent* Existing = GetDropShadowComponent(TargetWidget))
	{
		Existing->SetShadowStyle(InStyle);
		return Existing;
	}

	UNameTBDDropShadowComponent* NewComp = NewObject<UNameTBDDropShadowComponent>(TargetWidget);
	NewComp->BindToTargetWidget(TargetWidget, InStyle);
	NameTBDDropShadowRegistry::GWidgetShadowComponents.Add(TargetWidget, NewComp);
	return NewComp;
}

UNameTBDDropShadowComponent* UNameTBDDropShadowComponent::GetDropShadowComponent(const UWidget* TargetWidget)
{
	if (!TargetWidget)
	{
		return nullptr;
	}
	if (const TWeakObjectPtr<UNameTBDDropShadowComponent>* Found = NameTBDDropShadowRegistry::GWidgetShadowComponents.Find(TargetWidget))
	{
		return Found->Get();
	}
	return nullptr;
}

void UNameTBDDropShadowComponent::BindToTargetWidget(UWidget* InTargetWidget, const FNameTBDDropShadowStyle& InStyle)
{
	AttachedWidget = InTargetWidget;
	ShadowStyle = InStyle;

	if (UNameTBDDropShadowBox* DirectShadowBox = Cast<UNameTBDDropShadowBox>(InTargetWidget))
	{
		InjectedShadowBox = DirectShadowBox;
		DirectShadowBox->SetShadowStyle(InStyle);
		return;
	}

	if (UNameTBDDropShadowBox* ExistingParentBox = Cast<UNameTBDDropShadowBox>(InTargetWidget->GetParent()))
	{
		InjectedShadowBox = ExistingParentBox;
		ExistingParentBox->SetShadowStyle(InStyle);
		return;
	}

	// Non-destructively insert a UNameTBDDropShadowBox into the target widget's existing UPanelSlot
	// so all UCanvasPanelSlot / UVerticalBoxSlot / UOverlaySlot layout properties remain intact.
	if (UPanelSlot* HostSlot = InTargetWidget->Slot)
	{
		InjectedShadowBox = NewObject<UNameTBDDropShadowBox>(InTargetWidget->GetOuter());
		InjectedShadowBox->ShadowStyle = InStyle;
		InjectedShadowBox->ElevationMotion = ElevationMotion;

		HostSlot->Content = InjectedShadowBox;
		InjectedShadowBox->Slot = HostSlot;
		InTargetWidget->Slot = nullptr;

		InjectedShadowBox->SetContent(InTargetWidget);
		HostSlot->SynchronizeProperties();
	}
}

void UNameTBDDropShadowComponent::SetShadowStyle(const FNameTBDDropShadowStyle& InStyle)
{
	ShadowStyle = InStyle;
	if (InjectedShadowBox)
	{
		InjectedShadowBox->SetShadowStyle(ShadowStyle);
	}
	if (ShadowSlateWrapper.IsValid())
	{
		ShadowSlateWrapper->SetShadowStyle(ShadowStyle);
	}
}

void UNameTBDDropShadowComponent::SetElevationMultiplier(float InMultiplier)
{
	if (InjectedShadowBox)
	{
		InjectedShadowBox->SetElevationMultiplier(InMultiplier);
	}
	if (ShadowSlateWrapper.IsValid())
	{
		ShadowSlateWrapper->SetElevationMultiplier(InMultiplier);
	}
}

TSharedRef<SWidget> UNameTBDDropShadowComponent::WrapSlateWidget(const TSharedRef<SWidget>& InChildWidget)
{
	ShadowSlateWrapper = SNew(SNameTBDDropShadow)
		.ShadowStyle(ShadowStyle)
		.MotionSettings(ElevationMotion)
		[
			InChildWidget
		];
	return ShadowSlateWrapper.ToSharedRef();
}

// ============================================================================
// UNameTBDDropShadowBox
// ============================================================================

UNameTBDDropShadowBox::UNameTBDDropShadowBox()
{
	bIsVariable = true;
}

TSharedRef<SWidget> UNameTBDDropShadowBox::RebuildWidget()
{
	MyDropShadow = SNew(SNameTBDDropShadow)
		.ShadowStyle(ShadowStyle)
		.MotionSettings(ElevationMotion);

	if (GetChildrenCount() > 0)
	{
		if (UWidget* ChildContent = GetContentSlot()->Content)
		{
			MyDropShadow->SetContent(ChildContent->TakeWidget());
		}
	}

	return MyDropShadow.ToSharedRef();
}

void UNameTBDDropShadowBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyDropShadow.IsValid())
	{
		MyDropShadow->SetShadowStyle(ShadowStyle);
		MyDropShadow->SetMotionSettings(ElevationMotion);
	}
}

void UNameTBDDropShadowBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyDropShadow.Reset();
}

void UNameTBDDropShadowBox::OnSlotAdded(UPanelSlot* InSlot)
{
	if (MyDropShadow.IsValid() && InSlot && InSlot->Content)
	{
		MyDropShadow->SetContent(InSlot->Content->TakeWidget());
	}
}

void UNameTBDDropShadowBox::OnSlotRemoved(UPanelSlot* InSlot)
{
	(void)InSlot;
	if (MyDropShadow.IsValid())
	{
		MyDropShadow->SetContent(SNullWidget::NullWidget);
	}
}

void UNameTBDDropShadowBox::SetShadowStyle(const FNameTBDDropShadowStyle& InStyle)
{
	ShadowStyle = InStyle;
	if (MyDropShadow.IsValid())
	{
		MyDropShadow->SetShadowStyle(ShadowStyle);
	}
}

void UNameTBDDropShadowBox::SetElevationMultiplier(float InMultiplier)
{
	if (MyDropShadow.IsValid())
	{
		MyDropShadow->SetElevationMultiplier(InMultiplier);
	}
}

bool UNameTBDDropShadowBox::SetActiveDesignTargetByID_Implementation(FName InTargetID, bool bAnimateTransition)
{
	(void)bAnimateTransition;
	for (const FNameTBDDesignTarget& Target : DesignTargets)
	{
		if (Target.TargetID == InTargetID)
		{
			ActiveDesignTargetID = Target.TargetID;
			ShadowStyle.ShadowColor = Target.PrimaryColor;
			ShadowStyle.CornerRadii = Target.CornerRadii;
			if (MyDropShadow.IsValid())
			{
				MyDropShadow->SetShadowStyle(ShadowStyle);
			}
			return true;
		}
	}
	return false;
}

bool UNameTBDDropShadowBox::SetActiveDesignTargetByValue_Implementation(float NormalizedValue, bool bAnimateTransition)
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
const FText UNameTBDDropShadowBox::GetPaletteCategory()
{
	return LOCTEXT("NameTBDPaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
