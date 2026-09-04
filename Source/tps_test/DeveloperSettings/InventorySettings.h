#pragma once

#include "CoreMinimal.h"
#include "InventoryUITheme.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/DataTable.h"
#include "Inventory/Structs/InventoryEnums.h"
#include "InventorySettings.generated.h"

class UDataTable;
class UAudioCueSet;
class UInventoryTheme;

/**
 * Central config for item data tables. Edit in Project Settings -> Game -> Item Tables.
 * Map each row-name prefix (e.g. "WPN_") to its data table. Add one entry per table.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Inventory System"))
class TPS_TEST_API UInventorySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Config, Category = "Item Tables")
	TMap<FString, TSoftObjectPtr<UDataTable>> PrefixToTable;

	/* Inventory slots will default to the background image set here by their slot type */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Config, Category = "Inventory Slot")
	TMap<ESlotType, TSoftObjectPtr<UTexture2D>> DefaultSlotBackground;
	/** Reference slot size every inventory widget's SlotSizeMultiplier scales from. Change this once to resize every slot across the project. */
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Config, Category = "Inventory Slot")
	FVector2D BaseSlotSize = FVector2D(128.0f, 128.0f);
	/**
	 * Fallback audio cue set, used by any UInventoryComponent whose own AudioCues
	 * property is left unset. A component's own AudioCues always takes priority when
	 * assigned — this exists so a chest, vendor, or any inventory a designer forgot to
	 * assign sounds to still plays something instead of staying silent, without forcing
	 * every inventory in the project to share identical sounds.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Audio")
	TSoftObjectPtr<UAudioCueSet> DefaultAudioCues;

	/**
	 * Fallback fonts/colors, used by any inventory widget whose own Theme property is
	 * left unset. Same precedence as DefaultAudioCues: a widget's own Theme always wins
	 * when assigned. This is what makes "swap every inventory widget's fonts in one go"
	 * actually possible — change this one asset reference, every widget that hasn't
	 * been given its own override picks it up automatically.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Config, Category = "Style")
	TSoftObjectPtr<UInventoryUITheme> DefaultTheme;

	/**
	 * Blueprint's way in. GetDefault<T>() (how C++ reaches a UDeveloperSettings' single
	 * live instance) has no Blueprint equivalent, so a pure-BP widget with no C++ base
	 * of its own has no node that returns "the project's inventory settings" without
	 * this — call this first, then read DefaultTheme (or the other fields above) off
	 * the result.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	static const UInventorySettings* GetInventorySettings() { return GetDefault<UInventorySettings>(); }
	
	/** DefaultTheme, already loaded — skips the manual Resolve/Load-Asset step in Blueprint. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	static UInventoryUITheme* GetInventoryTheme() { return GetDefault<UInventorySettings>()->DefaultTheme.LoadSynchronous(); }
};