// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"
#include "MenuScreenInterface.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UMenuScreenInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * C++ equivalent of BPI_MenuScreen. Widgets implement this to receive their context and
 * open/close notifications; the manager never needs to know a concrete widget class.
 *
 * All three are BlueprintNativeEvent so existing UMG Blueprints can implement them
 * directly with no C++ subclass.
 */
class IMenuScreenInterface
{
	GENERATED_BODY()

public:
	/**
	 * Called once, immediately after the widget is constructed and before it is shown.
	 * Context is whatever the OpenMenu caller passed - the chest's inventory component,
	 * an NPC actor, or null.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Menu")
	void InitializeScreen(UObject* MenuContext);

	/** Called every time the screen becomes visible, including re-opens of a cached widget. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Menu")
	void OnScreenOpened(UObject* MenuContext);

	/** Called every time the screen is hidden. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Menu")
	void OnScreenClosed();

	/**
	 * Back was pressed while this is the top-most screen (the Menu Manager's Close Top Screen Action, or Request Back). Return true
	 * when the screen handled it itself - closed a sub-page, a dialog, asked about unsaved changes, or closed itself - and false (the
	 * default) to let the manager close it.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Menu")
	bool OnBackRequested();
};