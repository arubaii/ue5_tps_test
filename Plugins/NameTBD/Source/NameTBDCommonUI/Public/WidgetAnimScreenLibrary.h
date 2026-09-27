// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetAnimScreenLibrary.generated.h"

class UCommonActivatableWidget;
class UCommonActivatableWidgetContainerBase;

/**
 * Closing screens on a Common UI stack with their exit animations.
 *
 * Every node works on any activatable widget: a screen that animates (UWidgetAnimActivatableWidget, or a class of
 * your own built on FWidgetAnimScreenTransitionDriver) plays its Anim Out first; a plain one closes at once, exactly
 * as Deactivate Widget / Remove Widget would. So a stack can mix both, and the caller never has to know which is which.
 */
UCLASS()
class NAMETBDCOMMONUI_API UWidgetAnimScreenLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Closes Screen: plays its exit, then deactivates it, which pops it off its stack. Does nothing when Screen is not
	 * active - a covered screen is taken off its stack with Remove Screen Animated.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Screens")
	static void CloseScreenAnimated(UCommonActivatableWidget* Screen);

	/** Closes the screen on top of Container (the one being shown), with its exit. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Screens")
	static void PopScreenAnimated(UCommonActivatableWidgetContainerBase* Container);

	/**
	 * Takes Screen out of Container. The screen being shown closes with its exit; one that is covered is not on
	 * screen, so it is removed at once.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Screens")
	static void RemoveScreenAnimated(UCommonActivatableWidgetContainerBase* Container, UCommonActivatableWidget* Screen);

	/** True while Screen is playing its exit and waiting to deactivate. Always false for a plain screen. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Screens")
	static bool IsScreenClosingAnimated(const UCommonActivatableWidget* Screen);
};
