// Copyright NameTBD Plugin. All Rights Reserved.

#include "Widgets/NameTBDFlippableCard.h"

#define LOCTEXT_NAMESPACE "NameTBD"

const FName UNameTBDFlippableCard::FrontSlotName(TEXT("FrontContent"));
const FName UNameTBDFlippableCard::BackSlotName(TEXT("BackContent"));

UNameTBDFlippableCard::UNameTBDFlippableCard()
{
	bIsVariable = true;
}

FNameTBDMotionSettings UNameTBDFlippableCard::BuildMotionStruct() const
{
	FNameTBDMotionSettings Settings;
	Settings.Motion = Motion;
	Settings.EnterDuration = EnterDuration;
	Settings.ExitDuration = ExitDuration;
	Settings.Easing = Easing;
	return Settings;
}

TSharedRef<SWidget> UNameTBDFlippableCard::RebuildWidget()
{
	MyFlippableCard = SNew(SNameTBDFlippableCard)
		.bInitiallyShowBack(bShowBackFace)
		.FlipAxis(FlipAxis)
		.MotionSettings(BuildMotionStruct())
		.MidFlipElevationScale(MidFlipElevationScale)
		.PerspectiveSkewStrength(PerspectiveSkewStrength)
		.bFlipOnClick(bFlipOnClick)
		.OnFaceChanged(BIND_UOBJECT_DELEGATE(FOnNameTBDCardFaceChangedSlate, HandleSlateFaceChanged))
		.FrontContent()
		[
			FrontContent ? FrontContent->TakeWidget() : SNullWidget::NullWidget
		]
		.BackContent()
		[
			BackContent ? BackContent->TakeWidget() : SNullWidget::NullWidget
		];

	return MyFlippableCard.ToSharedRef();
}

void UNameTBDFlippableCard::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyFlippableCard.IsValid())
	{
		MyFlippableCard->SetMotionSettings(BuildMotionStruct());
		MyFlippableCard->SetFlipAxis(FlipAxis);
		MyFlippableCard->SetFlipOnClick(bFlipOnClick);
		if (bShowBackFace)
		{
			MyFlippableCard->FlipToBack(false);
		}
		else
		{
			MyFlippableCard->FlipToFront(false);
		}
	}
}

void UNameTBDFlippableCard::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyFlippableCard.Reset();
	if (bReleaseChildren)
	{
		if (FrontContent)
		{
			FrontContent->ReleaseSlateResources(true);
		}
		if (BackContent)
		{
			BackContent->ReleaseSlateResources(true);
		}
	}
}

void UNameTBDFlippableCard::FlipToBack(bool bAnimate)
{
	bShowBackFace = true;
	if (MyFlippableCard.IsValid())
	{
		MyFlippableCard->FlipToBack(bAnimate);
	}
	else
	{
		HandleSlateFaceChanged(true);
	}
}

void UNameTBDFlippableCard::FlipToFront(bool bAnimate)
{
	bShowBackFace = false;
	if (MyFlippableCard.IsValid())
	{
		MyFlippableCard->FlipToFront(bAnimate);
	}
	else
	{
		HandleSlateFaceChanged(false);
	}
}

void UNameTBDFlippableCard::ToggleFlip(bool bAnimate)
{
	if (bShowBackFace)
	{
		FlipToFront(bAnimate);
	}
	else
	{
		FlipToBack(bAnimate);
	}
}

float UNameTBDFlippableCard::GetFlipProgress() const
{
	return MyFlippableCard.IsValid() ? MyFlippableCard->GetFlipProgress() : (bShowBackFace ? 1.0f : 0.0f);
}

void UNameTBDFlippableCard::HandleSlateFaceChanged(bool bNewShowingBack)
{
	bShowBackFace = bNewShowingBack;
	OnCardFlipped.Broadcast(bShowBackFace);
}

void UNameTBDFlippableCard::GetSlotNames(TArray<FName>& SlotNames) const
{
	SlotNames.Add(FrontSlotName);
	SlotNames.Add(BackSlotName);
}

UWidget* UNameTBDFlippableCard::GetContentForSlot(FName SlotName) const
{
	if (SlotName == FrontSlotName)
	{
		return FrontContent;
	}
	if (SlotName == BackSlotName)
	{
		return BackContent;
	}
	return nullptr;
}

void UNameTBDFlippableCard::SetContentForSlot(FName SlotName, UWidget* Content)
{
	if (SlotName == FrontSlotName)
	{
		FrontContent = Content;
		if (MyFlippableCard.IsValid())
		{
			MyFlippableCard->SetFrontContent(Content ? Content->TakeWidget() : SNullWidget::NullWidget);
		}
	}
	else if (SlotName == BackSlotName)
	{
		BackContent = Content;
		if (MyFlippableCard.IsValid())
		{
			MyFlippableCard->SetBackContent(Content ? Content->TakeWidget() : SNullWidget::NullWidget);
		}
	}
}

bool UNameTBDFlippableCard::SetActiveDesignTargetByID_Implementation(FName InTargetID, bool bAnimateTransition)
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

bool UNameTBDFlippableCard::SetActiveDesignTargetByValue_Implementation(float NormalizedValue, bool bAnimateTransition)
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
const FText UNameTBDFlippableCard::GetPaletteCategory()
{
	return LOCTEXT("NameTBDPaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
