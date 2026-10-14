// Copyright NameTBD Plugin. All Rights Reserved.

#include "Widgets/NameTBDFoldableDropdown.h"
#include "Components/PanelWidget.h"

#define LOCTEXT_NAMESPACE "NameTBD"

const FName UNameTBDFoldableDropdown::HeaderSlotName(TEXT("HeaderContent"));
const FName UNameTBDFoldableDropdown::BodySlotName(TEXT("BodyContent"));

UNameTBDFoldableDropdown::UNameTBDFoldableDropdown()
{
	bIsVariable = true;
}

FNameTBDMotionSettings UNameTBDFoldableDropdown::BuildMotionStruct() const
{
	FNameTBDMotionSettings Settings;
	Settings.Motion = Motion;
	Settings.EnterDuration = EnterDuration;
	Settings.ExitDuration = ExitDuration;
	Settings.Easing = Easing;
	return Settings;
}

TSharedRef<SWidget> UNameTBDFoldableDropdown::RebuildWidget()
{
	MyFoldable = SNew(SNameTBDFoldableDropdown)
		.bInitiallyExpanded(bIsExpanded)
		.AnimationMode(AnimationMode)
		.MotionSettings(BuildMotionStruct())
		.bShowChevron(bShowHeaderChevron)
		.OnExpansionChanged(BIND_UOBJECT_DELEGATE(FOnNameTBDFoldStateChangedSlate, HandleSlateExpansionChanged))
		.HeaderContent()
		[
			HeaderContent ? HeaderContent->TakeWidget() : SNullWidget::NullWidget
		]
		.BodyContent()
		[
			BodyContent ? BodyContent->TakeWidget() : SNullWidget::NullWidget
		];

	return MyFoldable.ToSharedRef();
}

void UNameTBDFoldableDropdown::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyFoldable.IsValid())
	{
		MyFoldable->SetMotionSettings(BuildMotionStruct());
		MyFoldable->SetAnimationMode(AnimationMode);
		MyFoldable->SetExpanded(bIsExpanded, false);
	}
}

void UNameTBDFoldableDropdown::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyFoldable.Reset();
	if (bReleaseChildren)
	{
		if (HeaderContent)
		{
			HeaderContent->ReleaseSlateResources(true);
		}
		if (BodyContent)
		{
			BodyContent->ReleaseSlateResources(true);
		}
	}
}

void UNameTBDFoldableDropdown::SetExpanded(bool bInExpanded, bool bAnimate)
{
	bIsExpanded = bInExpanded;
	if (MyFoldable.IsValid())
	{
		MyFoldable->SetExpanded(bInExpanded, bAnimate);
	}
	else
	{
		HandleSlateExpansionChanged(bInExpanded);
	}
}

void UNameTBDFoldableDropdown::ToggleExpanded(bool bAnimate)
{
	SetExpanded(!bIsExpanded, bAnimate);
}

float UNameTBDFoldableDropdown::GetExpansionAlpha() const
{
	return MyFoldable.IsValid() ? MyFoldable->GetExpansionAlpha() : (bIsExpanded ? 1.0f : 0.0f);
}

void UNameTBDFoldableDropdown::HandleSlateExpansionChanged(bool bNewExpanded)
{
	bIsExpanded = bNewExpanded;

	if (bIsExpanded && !AccordionGroupName.IsNone())
	{
		if (UPanelWidget* ParentPanel = GetParent())
		{
			const int32 SiblingCount = ParentPanel->GetChildrenCount();
			for (int32 i = 0; i < SiblingCount; ++i)
			{
				if (UNameTBDFoldableDropdown* Sibling = Cast<UNameTBDFoldableDropdown>(ParentPanel->GetChildAt(i)))
				{
					if (Sibling != this && Sibling->AccordionGroupName == AccordionGroupName && Sibling->IsExpanded())
					{
						Sibling->SetExpanded(false, true);
					}
				}
			}
		}
	}

	OnExpansionChanged.Broadcast(bIsExpanded);
}

void UNameTBDFoldableDropdown::GetSlotNames(TArray<FName>& SlotNames) const
{
	SlotNames.Add(HeaderSlotName);
	SlotNames.Add(BodySlotName);
}

UWidget* UNameTBDFoldableDropdown::GetContentForSlot(FName SlotName) const
{
	if (SlotName == HeaderSlotName)
	{
		return HeaderContent;
	}
	if (SlotName == BodySlotName)
	{
		return BodyContent;
	}
	return nullptr;
}

void UNameTBDFoldableDropdown::SetContentForSlot(FName SlotName, UWidget* Content)
{
	if (SlotName == HeaderSlotName)
	{
		HeaderContent = Content;
		if (MyFoldable.IsValid())
		{
			MyFoldable->SetHeaderContent(Content ? Content->TakeWidget() : SNullWidget::NullWidget);
		}
	}
	else if (SlotName == BodySlotName)
	{
		BodyContent = Content;
		if (MyFoldable.IsValid())
		{
			MyFoldable->SetBodyContent(Content ? Content->TakeWidget() : SNullWidget::NullWidget);
		}
	}
}

bool UNameTBDFoldableDropdown::SetActiveDesignTargetByID_Implementation(FName InTargetID, bool bAnimateTransition)
{
	(void)bAnimateTransition;
	for (const FNameTBDDesignTarget& Target : DesignTargets)
	{
		if (Target.TargetID == InTargetID)
		{
			ActiveDesignTargetID = Target.TargetID;
			return true;
		}
	}
	return false;
}

bool UNameTBDFoldableDropdown::SetActiveDesignTargetByValue_Implementation(float NormalizedValue, bool bAnimateTransition)
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
const FText UNameTBDFoldableDropdown::GetPaletteCategory()
{
	return LOCTEXT("NameTBDPaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
