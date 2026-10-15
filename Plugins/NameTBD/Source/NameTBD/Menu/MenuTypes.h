// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NameTBD/Notifications/NotificationTypes.h"
#include "MenuTypes.generated.h"

class UInputMappingContext;
class UUserWidget;
class UInputAction;

/**
 * Which input regime a screen wants while it is open.
 *
 * Replaces E_InputContextType. Kept as an enum rather than a tag because the set is
 * genuinely closed - it describes how input is routed, not what content exists.
 */
UENUM(BlueprintType)
enum class EMenuInputContext : uint8
{
	/** Gameplay input only; cursor hidden, look input enabled. */
	Gameplay,
	/** Game and UI; cursor shown, look input suppressed. */
	Menu
};

/** How input is routed while a screen is the top-most open one, past its Input Context. */
UENUM(BlueprintType)
enum class EMenuScreenInputMode : uint8
{
	Default UMETA(ToolTip = "As the Input Context says: Menu is game and UI with the cursor shown, Gameplay is game only."),
	UIOnly UMETA(DisplayName = "UI Only", ToolTip = "Only the UI gets input (the game sees none): full-screen menus, pads navigate the screen."),
	GameAndUI UMETA(DisplayName = "Game and UI", ToolTip = "Both, cursor shown (an inventory over a live game).")
};

/** Where keyboard / gamepad focus goes when a screen opens (pads need focus to navigate). */
UENUM(BlueprintType)
enum class EMenuScreenFocus : uint8
{
	None UMETA(ToolTip = "Focus isn't moved (the screen does it in On Screen Opened)."),
	Screen UMETA(ToolTip = "The screen widget itself (it passes focus on: a Settings Menu Screen to its first setting)."),
	NamedWidget UMETA(ToolTip = "A widget of the screen by name (Focus Widget Name).")
};

/**
 * Everything the manager needs to know about one screen.
 *
 * This is the scalability change: E_MenuType plus the ScreenClasses / MappingContexts /
 * PreWarmScreens maps collapse into one row keyed by a GameplayTag. Adding a main menu
 * means adding a row - no enum edit, no three maps to keep in sync, no placeholder
 * enumerator to work around the map-default quirk.
 */
USTRUCT(BlueprintType)
struct FMenuScreenDefinition
{
	GENERATED_BODY()

	/** Widget class to instantiate. Soft so unopened screens cost nothing to reference. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSoftClassPtr<UUserWidget> WidgetClass;

	/** Input regime applied while this screen is the top-most open screen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	EMenuInputContext InputContext = EMenuInputContext::Menu;

	/** Created and hidden at BeginPlay rather than on first open. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	bool bPreWarm = false;

	/** Viewport Z-order. Higher draws on top. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	int32 ZOrder = 0;

	/**
	 * Pause the game while open. Honoured in standalone only - see
	 * UMenuManagerComponent::ApplyPauseState for why this cannot be obeyed in multiplayer.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	bool bPausesGame = false;
	
	/** Screens that must close when this one opens (a pause menu closing the loadout). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen")
	FGameplayTagContainer ClosesScreens;

	/** Optional input action that toggles this specific screen. Bound automatically on the local owner. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleAction;

	/** How input is routed while this is the top screen. Default keeps the Input Context's routing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	EMenuScreenInputMode InputMode = EMenuScreenInputMode::Default;

	/** Added while this is the top screen (its own actions: tab switching, a map's zoom), at the manager's priority + 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> ScreenMappingContext;

	/** The cursor hides while the player uses a gamepad on this screen, and comes back with the mouse. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	bool bHideCursorWithGamepad = false;

	/** Back (Close Top Screen Action, Request Back) closes this screen when it doesn't handle Back itself. Off: Back does nothing here (a title screen). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	bool bCloseOnBack = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Focus")
	EMenuScreenFocus FocusOnOpen = EMenuScreenFocus::None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Focus", meta = (EditCondition = "FocusOnOpen == EMenuScreenFocus::NamedWidget"))
	FName FocusWidgetName;

	/** When a screen above closes and this one is top again, focus goes back to the widget that had it (else Focus On Open again). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Focus")
	bool bRestoreFocus = true;

	/** Loads the widget class in the background on first open (no hitch); it opens when loaded. Off: loaded at once, as before. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen")
	bool bAsyncLoad = false;

	/**
	 * Seconds the widget stays visible after it is closed, so its exit animation can play (On Screen Closed is called at once;
	 * input goes back at once). 0: hidden at once, as before.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen", meta = (ClampMin = "0", UIMax = "2", Units = "s"))
	float CloseDelay = 0.f;

	/** Closing this screen opens again the screens it closed when it opened (Settings opened from Pause goes back to Pause). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen")
	bool bReturnToClosedScreens = false;

	/** Notifications are held back while this screen is open (the plugin's notification system). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen")
	bool bSuppressNotifications = false;

	/** Freeze: those on screen stay frozen; Hold: they leave and come back; Drop: new ones go straight to the history. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Screen", meta = (EditCondition = "bSuppressNotifications"))
	ENotificationSuppressionMode NotificationSuppression = ENotificationSuppressionMode::Freeze;
};