// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "NameTBD/Menu/Graphics/MenuUpscalers.h"
#include "NameTBD/Menu/Input/MenuInputTypes.h"
#include "MenuSettingTypes.h"

#include "MenuProjectSettings.generated.h"

class UInputMappingContext;
class UMenuKeyGlyphSet;
class UMenuSettingsSchema;
class UMenuStyleAsset;
class USoundClass;
class USoundMix;

/** Where the performance overlay sits. */
UENUM(BlueprintType)
enum class EMenuOverlayCorner : uint8
{
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight
};

/**
 * Project Settings > Plugins > Menus: which settings pages a game has, where their values are saved, the vendor graphics
 * technology table, audio channels, key binding and button prompt setup, and the look screens and dialogs fall back on.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Menus"))
class NAMETBD_API UMenuProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UMenuProjectSettings();

	static const UMenuProjectSettings& Get() { return *GetDefault<UMenuProjectSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** A project default for Setting Id (Default Values), else empty. */
	const FString* FindDefaultOverride(FName SettingId) const;

	// -------------------------------------------------------------------------------------------------
	// Settings
	// -------------------------------------------------------------------------------------------------

	/** The plugin's pages: Gameplay, Video, Audio, Controls, Accessibility (see MenuSystem.md for every setting). */
	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	bool bIncludeBuiltInPages = true;

	/** Your settings, merged over the built-in pages in Priority order: new pages, sections and settings, or replacements by Id. */
	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	TArray<TSoftObjectPtr<UMenuSettingsSchema>> Schemas;

	/** Built-in pages left out ("Gameplay"). */
	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	TArray<FName> HiddenPages;

	/** Settings left out by Id ("Video.HDROutput", "Accessibility.MenuNarration"). */
	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	TArray<FName> HiddenSettings;

	/** The project's defaults for settings by Id ("Video.VSync" = "true"), over the settings' own. */
	UPROPERTY(Config, EditAnywhere, Category = "Settings", meta = (TitleProperty = "Id"))
	TArray<FMenuSettingValue> DefaultValues;

	/** Seconds the player has to keep a display change before it goes back by itself. */
	UPROPERTY(Config, EditAnywhere, Category = "Settings", meta = (ClampMin = "3", UIMax = "60", Units = "s"))
	float ConfirmationSeconds = 15.f;

	/** A notification tells the player when a change needs a restart (with the plugin's notifications). */
	UPROPERTY(Config, EditAnywhere, Category = "Settings")
	bool bNotifyRestartRequired = true;

	// -------------------------------------------------------------------------------------------------
	// Saving
	// -------------------------------------------------------------------------------------------------

	/** Save slot for the machine's settings (display, graphics). */
	UPROPERTY(Config, EditAnywhere, Category = "Saving")
	FString MachineSaveSlot = TEXT("NameTBD_Settings");

	/** Save slot for each player's settings (saved with the player's platform user index). */
	UPROPERTY(Config, EditAnywhere, Category = "Saving")
	FString UserSaveSlot = TEXT("NameTBD_PlayerSettings");

	/** Saves on a worker thread (no hitch on consoles). Off: at once, on the game thread. */
	UPROPERTY(Config, EditAnywhere, Category = "Saving")
	bool bSaveAsync = true;

	// -------------------------------------------------------------------------------------------------
	// Audio
	// -------------------------------------------------------------------------------------------------

	/** The sound mix volume settings are applied through (pushed once). Empty: one is made at run time. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<USoundMix> SettingsSoundMix;

	/** Sound class per volume channel: Master, Music, Effects, Dialogue, UI, Voice. A channel without one isn't offered (Master always is). */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (ForceInlineRow))
	TMap<FName, TSoftObjectPtr<USoundClass>> AudioChannels;

	// -------------------------------------------------------------------------------------------------
	// Graphics
	// -------------------------------------------------------------------------------------------------

	/**
	 * The vendor technology a game can offer, and the engine's own. Shipped with NVIDIA DLSS / DLAA / Frame Generation / Reflex,
	 * AMD FSR / Frame Generation / Anti-Lag 2, Intel XeSS / Frame Generation / XeLL, TSR, TAA, FXAA and Off. A row whose plugin
	 * isn't enabled is never offered; add a row (or edit one) for a new version.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Graphics", meta = (TitleProperty = "DisplayName"))
	TArray<FMenuGraphicsFeature> GraphicsFeatures;

	/** Where the performance overlay (Video > Performance Overlay) shows. */
	UPROPERTY(Config, EditAnywhere, Category = "Graphics")
	EMenuOverlayCorner PerformanceOverlayCorner = EMenuOverlayCorner::TopRight;

	/** Folder under the project's Saved folder that benchmark CSV files are written to. */
	UPROPERTY(Config, EditAnywhere, Category = "Graphics")
	FString BenchmarkFolder = TEXT("Benchmarks");

	// -------------------------------------------------------------------------------------------------
	// Input
	// -------------------------------------------------------------------------------------------------

	/**
	 * Mapping contexts whose player mappable keys the key binding sections list (registered with Enhanced Input's user settings
	 * for each player at start). Needs Project Settings > Engine > Enhanced Input > Enable User Settings.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TArray<TSoftObjectPtr<UInputMappingContext>> KeyBindingContexts;

	UPROPERTY(Config, EditAnywhere, Category = "Input")
	EMenuKeyConflictPolicy KeyConflictPolicy = EMenuKeyConflictPolicy::Swap;

	/** Keys that can never be bound (the ones that open and close menus). */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TArray<FKey> ReservedKeys;

	/** Seconds a key binding row waits for a key before giving up. */
	UPROPERTY(Config, EditAnywhere, Category = "Input", meta = (ClampMin = "2", UIMax = "30", Units = "s"))
	float KeyCaptureTimeout = 8.f;

	/** Button prompt art (one set per gamepad family, one for keyboard and mouse). */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	TArray<TSoftObjectPtr<UMenuKeyGlyphSet>> KeyGlyphSets;

	/**
	 * Gamepad hardware names (as the platform reports them: "XInputController", "DualSense"...) and whose prompts they show. A name
	 * containing a key here matches it.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Input", meta = (ForceInlineRow))
	TMap<FString, EMenuGamepadFamily> GamepadHardware;

	/** The family a gamepad shows when its hardware isn't recognised. */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	EMenuGamepadFamily DefaultGamepadFamily = EMenuGamepadFamily::Xbox;

	/** Pixels the mouse must move to count as switching back to mouse (stops a resting mouse's jitter flipping prompts). */
	UPROPERTY(Config, EditAnywhere, Category = "Input", meta = (ClampMin = "0", UIMax = "50"))
	float MouseSwitchDistance = 6.f;

	/** How far a stick must move to count as switching to the gamepad. */
	UPROPERTY(Config, EditAnywhere, Category = "Input", meta = (ClampMin = "0.05", ClampMax = "1"))
	float StickSwitchThreshold = 0.4f;

	// -------------------------------------------------------------------------------------------------
	// Look
	// -------------------------------------------------------------------------------------------------

	/** The look settings screens, dialogs and the performance overlay use when they have none of their own. Empty: Simple. */
	UPROPERTY(Config, EditAnywhere, Category = "Look")
	TSoftObjectPtr<UMenuStyleAsset> DefaultStyle;

	/** Fills Graphics Features with the shipped rows (the constructor's; also the editor's reset). */
	static void MakeDefaultGraphicsFeatures(TArray<FMenuGraphicsFeature>& OutFeatures);
};
