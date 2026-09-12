// Copyright AR GameSystems 2026. All rights reserved.

#include "UserWidget_Animatable.h"

void UUserWidget_Animatable::PlayIn(bool bCascadeToChildren)
{
	UWidgetAnimLibrary::PlayWidgetIn(this, EffectiveInAnimations, AnimPlayback);

	if (bCascadeToChildren)
	{
		CascadeAnimationToChildren(/*bPlayingIn=*/true);
	}
}

void UUserWidget_Animatable::PlayOut(bool bCascadeToChildren)
{
	UWidgetAnimLibrary::PlayWidgetOut(AnimPlayback, EffectiveOutAnimations);

	if (bCascadeToChildren)
	{
		CascadeAnimationToChildren(/*bPlayingIn=*/false);
	}
}

void UUserWidget_Animatable::CascadeAnimationToChildren(bool bPlayingIn)
{
	if (!WidgetTree)
	{
		return;
	}

	TArray<UWidget*> AllWidgets;
	WidgetTree->GetAllWidgets(AllWidgets);

	for (UWidget* Child : AllWidgets)
	{
		if (UUserWidget_Animatable* NestedAnimatable = Cast<UUserWidget_Animatable>(Child))
		{
			// Each nested UserWidget owns its own WidgetTree, so GetAllWidgets() above only
			// ever finds DIRECT embedded UserWidget children - never grandchildren. Cascading
			// must stay enabled so each level triggers its own children's children in turn.
			if (bPlayingIn)
			{
				NestedAnimatable->PlayIn(/*bCascadeToChildren=*/true);
			}
			else
			{
				NestedAnimatable->PlayOut(/*bCascadeToChildren=*/true);
			}
		}
	}
}

bool UUserWidget_Animatable::IsAnimClosing() const
{
	return AnimPlayback.Phase == EWidgetAnimPhase::PlayingOut || AnimPlayback.Phase == EWidgetAnimPhase::Hidden;
}

void UUserWidget_Animatable::RebuildEffectiveAnimations()
{
	EffectiveInAnimations = AnimIn;
	EffectiveOutAnimations = AnimOut;

	for (const FWidgetAnimSettings& Settings : AnimIn)
	{
		if (Settings.bMirrorInOut)
		{
			EffectiveOutAnimations.Add(Settings);
		}
	}

	for (const FWidgetAnimSettings& Settings : AnimOut)
	{
		if (Settings.bMirrorInOut)
		{
			EffectiveInAnimations.Add(Settings);
		}
	}
}

void UUserWidget_Animatable::SetVisibility(ESlateVisibility InVisibility)
{
	if (bPlayAnimationsOnVisibilityHidden && !bPlayOnlyOnConstructAndDestruct)
	{
		const bool bIsBecomingInvisible = (InVisibility == ESlateVisibility::Hidden || InVisibility == ESlateVisibility::Collapsed);
		const bool bWasVisible = (GetVisibility() == ESlateVisibility::Visible || GetVisibility() == ESlateVisibility::SelfHitTestInvisible || GetVisibility() == ESlateVisibility::HitTestInvisible);

		// Intercept the hidden call, play the out animation, and cache the desired visibility for later.
		if (bIsBecomingInvisible && bWasVisible && !IsAnimClosing())
		{
			bIsDeferringVisibility = true;
			DeferredVisibility = InVisibility;
			PlayOut();
			return; 
		}
		// If it's returning to a visible state, immediately show it and play the in animation
		else if (!bIsBecomingInvisible && !bWasVisible)
		{
			bIsDeferringVisibility = false;
			Super::SetVisibility(InVisibility);
			PlayIn();
			return;
		}
	}

	bIsDeferringVisibility = false;
	Super::SetVisibility(InVisibility);
}

void UUserWidget_Animatable::NativeConstruct()
{
	Super::NativeConstruct();

	RebuildEffectiveAnimations();

	if (bPlayOnlyOnConstructAndDestruct || bPlayInAnimationsOnConstruct)
	{
		PlayIn();
	}
}

bool UUserWidget_Animatable::TickAnimationPlayback(float DeltaTime)
{
	const bool bJustFinishedOut = UWidgetAnimLibrary::TickWidgetAnimPlayback(this, EffectiveInAnimations, EffectiveOutAnimations, AnimPlayback, DeltaTime);
	if (bJustFinishedOut)
	{
		OnOutAnimationFinished();
	}
	return bJustFinishedOut;
}

void UUserWidget_Animatable::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	TickAnimationPlayback(InDeltaTime);
}

void UUserWidget_Animatable::OnOutAnimationFinished_Implementation()
{
	// If SetVisibility() triggered this exit animation, apply the actual hidden state now
	if (bIsDeferringVisibility)
	{
		bIsDeferringVisibility = false;
		Super::SetVisibility(DeferredVisibility);
	}
}