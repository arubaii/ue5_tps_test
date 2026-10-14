// Copyright NameTBD Plugin. All Rights Reserved.

#include "Widgets/NameTBDProgressBar.h"

#define LOCTEXT_NAMESPACE "NameTBD"

UNameTBDProgressBar::UNameTBDProgressBar()
{
	ValueChangeMotion.Motion = ENameTBDMotionType::SmoothLerp;
	ValueChangeMotion.EnterDuration = 0.28f;
	ValueChangeMotion.ExitDuration = 0.16f;
}

float UNameTBDProgressBar::HandleGetPercent() const
{
	if (PercentDelegate.IsBound())
	{
		return PercentDelegate.Execute();
	}
	return Percent;
}

TSharedRef<SWidget> UNameTBDProgressBar::RebuildWidget()
{
	TAttribute<float> PercentBinding = PercentDelegate.IsBound()
		? TAttribute<float>::Create(TAttribute<float>::FGetter::CreateUObject(this, &UNameTBDProgressBar::HandleGetPercent))
		: TAttribute<float>(Percent);

	MyProgressBar = SNew(SNameTBDProgressBar)
		.Percent(PercentBinding)
		.Style(BarStyle)
		.MotionSettings(ValueChangeMotion);

	return MyProgressBar.ToSharedRef();
}

void UNameTBDProgressBar::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyProgressBar.IsValid())
	{
		TAttribute<float> PercentBinding = PercentDelegate.IsBound()
			? TAttribute<float>::Create(TAttribute<float>::FGetter::CreateUObject(this, &UNameTBDProgressBar::HandleGetPercent))
			: TAttribute<float>(Percent);

		MyProgressBar->SetStyle(BarStyle);
		MyProgressBar->SetMotionSettings(ValueChangeMotion);
		MyProgressBar->SetPercent(PercentBinding);
	}
}

void UNameTBDProgressBar::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyProgressBar.Reset();
}

void UNameTBDProgressBar::SetPercent(float InPercent)
{
	const float Clamped = FMath::Clamp(InPercent, 0.0f, 1.0f);
	if (!FMath::IsNearlyEqual(Percent, Clamped, KINDA_SMALL_NUMBER))
	{
		const float OldPercent = Percent;
		Percent = Clamped;
		if (MyProgressBar.IsValid())
		{
			MyProgressBar->SetPercent(Percent);
		}
		OnPercentChanged.Broadcast(Percent, OldPercent);
	}
}

float UNameTBDProgressBar::GetDisplayedPercent() const
{
	return MyProgressBar.IsValid() ? MyProgressBar->GetDisplayedPercent() : Percent;
}

void UNameTBDProgressBar::SetBarShape(ENameTBDProgressBarShape InShape)
{
	BarStyle.Shape = InShape;
	if (MyProgressBar.IsValid())
	{
		MyProgressBar->SetStyle(BarStyle);
	}
}

void UNameTBDProgressBar::SetSegments(int32 InSegmentCount, float InSegmentGap)
{
	BarStyle.SegmentCount = FMath::Max(1, InSegmentCount);
	BarStyle.SegmentGap = FMath::Max(0.0f, InSegmentGap);
	if (MyProgressBar.IsValid())
	{
		MyProgressBar->SetStyle(BarStyle);
	}
}

void UNameTBDProgressBar::ApplyDesignTarget(const FNameTBDDesignTarget& Target)
{
	ActiveDesignTargetID = Target.TargetID;
	BarStyle.FillColor = Target.PrimaryColor;
	BarStyle.CornerRadius = static_cast<float>(Target.CornerRadii.X);
	if (Target.ShapePrimitive == ENameTBDShapePrimitive::LinearBar)
	{
		BarStyle.Shape = ENameTBDProgressBarShape::Linear;
	}
	else if (Target.ShapePrimitive == ENameTBDShapePrimitive::CircularRing)
	{
		BarStyle.Shape = ENameTBDProgressBarShape::Circular;
	}
	else if (Target.ShapePrimitive == ENameTBDShapePrimitive::HybridBar)
	{
		BarStyle.Shape = ENameTBDProgressBarShape::Both;
	}
	if (MyProgressBar.IsValid())
	{
		MyProgressBar->SetStyle(BarStyle);
	}
}

bool UNameTBDProgressBar::SetActiveDesignTargetByID_Implementation(FName InTargetID, bool bAnimateTransition)
{
	(void)bAnimateTransition;
	for (const FNameTBDDesignTarget& Target : DesignTargets)
	{
		if (Target.TargetID == InTargetID)
		{
			ApplyDesignTarget(Target);
			return true;
		}
	}
	return false;
}

bool UNameTBDProgressBar::SetActiveDesignTargetByValue_Implementation(float NormalizedValue, bool bAnimateTransition)
{
	(void)bAnimateTransition;
	for (const FNameTBDDesignTarget& Target : DesignTargets)
	{
		if (NormalizedValue >= Target.MinValueThreshold && NormalizedValue <= Target.MaxValueThreshold)
		{
			ApplyDesignTarget(Target);
			return true;
		}
	}
	return false;
}

#if WITH_EDITOR
const FText UNameTBDProgressBar::GetPaletteCategory()
{
	return LOCTEXT("NameTBDPaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
