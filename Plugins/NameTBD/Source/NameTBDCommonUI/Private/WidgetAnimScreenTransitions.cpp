// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimScreenTransitions.h"

#include "CommonActivatableWidget.h"
#include "Components/Widget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimScreen, Log, All);

namespace
{
	/** Far more components than any real screen has; only there so a malformed tree can't make the gather run away. */
	constexpr int32 MaxComponentsPerScreen = 1024;

	/**
	 * Whether a component's exit is still going. A component whose widget has lost its Slate widget is no longer on
	 * screen and no longer ticks, so it is treated as done rather than holding the close until the time limit.
	 */
	bool IsStillExiting(const UWidgetAnimationComponent* Component)
	{
		if (!Component)
		{
			return false;
		}

		const UWidget* AnimatedWidget = Component->GetAnimatedWidget();
		if (!AnimatedWidget || !AnimatedWidget->GetCachedWidget().IsValid())
		{
			return false;
		}

		return Component->GetAnimPhase() == EWidgetAnimPhase::PlayingOut;
	}
}

FWidgetAnimScreenTransitionDriver::~FWidgetAnimScreenTransitionDriver()
{
	if (CloseTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CloseTickHandle);
		CloseTickHandle.Reset();
	}
}

// =====================================================================================================
// Components
// =====================================================================================================

void FWidgetAnimScreenTransitionDriver::GetRootComponents(UWidget& Screen, TArray<UWidgetAnimationComponent*>& OutComponents)
{
	OutComponents.Reset();

	// A component on the screen itself cascades into everything inside, so it is the only root. Without one,
	// each nearest component inside is a root of its own branch.
	if (UWidgetAnimationComponent* Own = UWidgetAnimationComponent::FindWidgetAnimationComponent(&Screen))
	{
		OutComponents.Add(Own);
		return;
	}

	UWidgetAnimationComponent::ForEachNearestNestedComponent(&Screen, [&OutComponents](UWidgetAnimationComponent* Component)
	{
		OutComponents.AddUnique(Component);
	});
}

void FWidgetAnimScreenTransitionDriver::GetAllComponents(UWidget& Screen, TArray<UWidgetAnimationComponent*>& OutComponents)
{
	GetRootComponents(Screen, OutComponents);

	// Breadth first along the same walk the cascade takes, so the set is exactly the components Play Out reaches.
	// OutComponents grows as it is walked; indexing (rather than a range for) keeps that valid.
	for (int32 Index = 0; Index < OutComponents.Num() && OutComponents.Num() < MaxComponentsPerScreen; ++Index)
	{
		UWidget* AnimatedWidget = OutComponents[Index] ? OutComponents[Index]->GetAnimatedWidget() : nullptr;
		if (!AnimatedWidget)
		{
			continue;
		}

		UWidgetAnimationComponent::ForEachNearestNestedComponent(AnimatedWidget, [&OutComponents](UWidgetAnimationComponent* Component)
		{
			OutComponents.AddUnique(Component);
		});
	}
}

// =====================================================================================================
// Activation
// =====================================================================================================

void FWidgetAnimScreenTransitionDriver::NotifyActivated(UCommonActivatableWidget& Screen, const FWidgetAnimScreenTransitionSettings& Settings)
{
	// Activated again part-way through its exit: the screen is staying after all, so it stops leaving and
	// plays its activation below from wherever the exit had gotten to.
	if (bClosing)
	{
		CancelClose();
	}

	if (Screen.IsDesignTime() || Settings.OnActivate == EWidgetAnimScreenActivateAnim::None)
	{
		return;
	}

	TArray<UWidgetAnimationComponent*> Roots;
	GetRootComponents(Screen, Roots);

	for (UWidgetAnimationComponent* Root : Roots)
	{
		// Still entering: this is the push that constructed the screen, which already started Anim In in the same
		// frame. Starting it again would be invisible at best and a restart mid-entrance at worst.
		if (!Root || Root->GetAnimPhase() == EWidgetAnimPhase::PlayingIn)
		{
			continue;
		}

		if (Settings.OnActivate == EWidgetAnimScreenActivateAnim::AnimIds)
		{
			// Nothing to play is not the same as "play everything", which is what an empty list means to this call.
			if (Settings.ActivateAnimIds.Num() > 0)
			{
				Root->PlayWidgetAnimationsById(Settings.ActivateAnimIds, /*bReverse=*/false, /*PlaybackSpeed=*/1.f, /*bHoldFinalPose=*/true, /*bRestartIfPlaying=*/true);
			}
		}
		else
		{
			Root->PlayIn(/*bCascadeToChildren=*/true);
		}
	}
}

void FWidgetAnimScreenTransitionDriver::NotifyDeactivated(UCommonActivatableWidget& /*Screen*/)
{
	if (bFinishingClose)
	{
		return;
	}

	// Deactivated by someone else mid-exit - covered by a push, or removed from its stack. Either way the
	// deactivation this close was waiting to make has already happened, so there is nothing left to wait for.
	// A push that covers a closing screen leaves it in the stack, covered, as Common UI always does.
	if (bClosing)
	{
		CancelClose();
	}
}

void FWidgetAnimScreenTransitionDriver::NotifyDestructed(UCommonActivatableWidget& /*Screen*/)
{
	if (bClosing)
	{
		CancelClose();
	}
}

// =====================================================================================================
// Close
// =====================================================================================================

bool FWidgetAnimScreenTransitionDriver::RequestClose(UCommonActivatableWidget& Screen, const FWidgetAnimScreenTransitionSettings& Settings)
{
	if (bClosing)
	{
		return true;
	}

	// An inactive screen is either already gone or covered; deactivating it again would do nothing, and a covered
	// screen is taken off its stack with the container's Remove instead.
	if (!Screen.IsActivated())
	{
		return false;
	}

	TArray<UWidgetAnimationComponent*> AllComponents;
	if (Settings.bAnimateClose && !Screen.IsDesignTime())
	{
		GetAllComponents(Screen, AllComponents);
	}

	// Only wait if something will actually move. A screen with no exit authored anywhere closes this frame, rather
	// than a frame later for no visible reason. A value-driven animation the exit waits for counts - a loading bar
	// filling up before its screen goes, even on a screen with no exit of its own.
	const bool bAnyExit = AllComponents.ContainsByPredicate([](const UWidgetAnimationComponent* Component)
	{
		return Component && Component->GetAnimatedWidget() && Component->HasExitToPlay();
	});

	if (!bAnyExit)
	{
		Screen.DeactivateWidget();
		return false;
	}

	bClosing = true;
	ClosingScreen = &Screen;
	CloseElapsed = 0.f;
	CloseTimeLimit = FMath::Max(Settings.MaxCloseSeconds, 0.1f);

	if (Settings.bBlockInputWhileClosing)
	{
		const ESlateVisibility Current = Screen.GetVisibility();
		if (Current == ESlateVisibility::Visible || Current == ESlateVisibility::SelfHitTestInvisible)
		{
			VisibilityBeforeClose = Current;
			Screen.SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}

	TArray<UWidgetAnimationComponent*> Roots;
	GetRootComponents(Screen, Roots);
	for (UWidgetAnimationComponent* Root : Roots)
	{
		if (Root)
		{
			Root->PlayOut(/*bCascadeToChildren=*/true);
		}
	}

	WaitingOn.Reset(AllComponents.Num());
	for (UWidgetAnimationComponent* Component : AllComponents)
	{
		WaitingOn.Add(Component);
	}

	// The core ticker, the same clock the components run on: it keeps going while the game is paused, which is
	// exactly when a pause menu closes.
	CloseTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(&Screen, [this](float DeltaTime)
	{
		return TickClose(DeltaTime);
	}));

	return true;
}

bool FWidgetAnimScreenTransitionDriver::TickClose(float DeltaTime)
{
	if (!bClosing || !ClosingScreen.IsValid())
	{
		// Returning false unregisters this ticker; the handle must not be removed again later.
		CloseTickHandle.Reset();
		ResetCloseState();
		return false;
	}

	CloseElapsed += DeltaTime;

	const bool bAnyStillExiting = WaitingOn.ContainsByPredicate([](const TWeakObjectPtr<UWidgetAnimationComponent>& Component)
	{
		return IsStillExiting(Component.Get());
	});

	if (bAnyStillExiting && CloseElapsed < CloseTimeLimit)
	{
		return true;
	}

	if (bAnyStillExiting)
	{
		UE_LOG(LogWidgetAnimScreen, Warning, TEXT("[%s] Closed after %.2f s with its exit animation still running (Max Close Seconds). Check for a component that stopped ticking."),
			*ClosingScreen->GetName(), CloseTimeLimit);
	}

	CloseTickHandle.Reset();
	FinishClose();
	return false;
}

void FWidgetAnimScreenTransitionDriver::FinishClose()
{
	UCommonActivatableWidget* Screen = ClosingScreen.Get();

	// Input back before deactivating, so whatever Common UI does with visibility on deactivation starts from the
	// screen's own setting rather than the one the close borrowed.
	RestoreInput();
	ResetCloseState();

	if (Screen && Screen->IsActivated())
	{
		// Deactivation is what makes the stack pop the screen. Flagged so NotifyDeactivated, which runs inside
		// this call, knows it is the close finishing and not something cutting it short.
		TGuardValue<bool> Guard(bFinishingClose, true);
		Screen->DeactivateWidget();
	}
}

void FWidgetAnimScreenTransitionDriver::CancelClose()
{
	if (CloseTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CloseTickHandle);
		CloseTickHandle.Reset();
	}

	RestoreInput();
	ResetCloseState();
}

void FWidgetAnimScreenTransitionDriver::ResetCloseState()
{
	bClosing = false;
	ClosingScreen.Reset();
	WaitingOn.Reset();
	CloseElapsed = 0.f;

	// Normally already given back by RestoreInput; dropped here for the paths where the screen is gone.
	VisibilityBeforeClose.Reset();
}

void FWidgetAnimScreenTransitionDriver::RestoreInput()
{
	if (!VisibilityBeforeClose.IsSet())
	{
		return;
	}

	// Only undone if it is still the visibility the close set: anything that changed it meanwhile meant it.
	UCommonActivatableWidget* Screen = ClosingScreen.Get();
	if (Screen && Screen->GetVisibility() == ESlateVisibility::HitTestInvisible)
	{
		Screen->SetVisibility(VisibilityBeforeClose.GetValue());
	}

	VisibilityBeforeClose.Reset();
}
