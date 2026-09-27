// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimScreenLibrary.h"

#include "CommonActivatableWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "WidgetAnimScreenTransitions.h"

void UWidgetAnimScreenLibrary::CloseScreenAnimated(UCommonActivatableWidget* Screen)
{
	if (!Screen || !Screen->IsActivated())
	{
		return;
	}

	if (IWidgetAnimScreen* AnimatedScreen = Cast<IWidgetAnimScreen>(Screen))
	{
		AnimatedScreen->RequestAnimatedClose();
		return;
	}

	Screen->DeactivateWidget();
}

void UWidgetAnimScreenLibrary::PopScreenAnimated(UCommonActivatableWidgetContainerBase* Container)
{
	if (Container)
	{
		CloseScreenAnimated(Container->GetActiveWidget());
	}
}

void UWidgetAnimScreenLibrary::RemoveScreenAnimated(UCommonActivatableWidgetContainerBase* Container, UCommonActivatableWidget* Screen)
{
	if (!Container || !Screen)
	{
		return;
	}

	// The shown screen leaves by deactivating, which the container already treats as removal - so its exit can
	// play first. A covered one is invisible, and closing it through deactivation would do nothing (it is already
	// inactive), so the container removes it directly.
	if (Screen == Container->GetActiveWidget() && Screen->IsActivated())
	{
		CloseScreenAnimated(Screen);
		return;
	}

	Container->RemoveWidget(*Screen);
}

bool UWidgetAnimScreenLibrary::IsScreenClosingAnimated(const UCommonActivatableWidget* Screen)
{
	const IWidgetAnimScreen* AnimatedScreen = Cast<const IWidgetAnimScreen>(Screen);
	return AnimatedScreen && AnimatedScreen->IsClosingAnimated();
}
