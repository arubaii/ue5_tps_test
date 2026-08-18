// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MenuTypes.generated.h"

class UInputMappingContext;
class UUserWidget;

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
};