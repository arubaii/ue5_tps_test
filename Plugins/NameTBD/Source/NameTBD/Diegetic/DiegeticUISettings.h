// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "DiegeticUISettings.generated.h"

class UMaterialInterface;
enum class EDiegeticMaterialVariant : uint8;

/**
 * Project Settings > Plugins > Diegetic UI: what every Diegetic Screen and Diegetic Interactor in the project shares - the
 * redraw budget, whether players get an interactor on their own, and the keys a Diegetic Interactor binds when it isn't
 * given input of its own. See DiegeticTypes.h.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Diegetic UI"))
class NAMETBD_API UDiegeticUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UDiegeticUISettings();

	static const UDiegeticUISettings& Get() { return *GetDefault<UDiegeticUISettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	// -------------------------------------------------------------------------------------------------
	// Performance
	// -------------------------------------------------------------------------------------------------

	/**
	 * At most this many Diegetic Screens redraw in one frame in a world - those set to By Distance or When Changed; Every Frame
	 * screens and screens being pointed at or used aren't counted. The ones waiting longest past their rate go first (Budget
	 * Priority before that). 0: no limit.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Performance", meta = (ClampMin = "0", UIMin = "0", UIMax = "32"))
	int32 MaxRedrawsPerFrame = 6;

	// -------------------------------------------------------------------------------------------------
	// Interaction
	// -------------------------------------------------------------------------------------------------

	/**
	 * Every locally controlled pawn gets a Diegetic Interactor (aiming with the camera) once a world has a Diegetic Screen, unless
	 * it already has a Widget Interaction Component of its own - so screens can be pointed at and clicked with nothing set up.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Interaction")
	bool bAutoAddInteractor = true;

	/** The Interaction Distance an interactor added on its own starts with (cm), measured from the pawn. */
	UPROPERTY(Config, EditAnywhere, Category = "Interaction", meta = (ClampMin = "0", UIMin = "50", UIMax = "2000", ForceUnits = "cm"))
	float DefaultInteractionDistance = 400.f;

	/**
	 * The priority of the interactor's input mapping contexts. They are only added while a screen is pointed at (over something
	 * that can be clicked) or used, and their keys are consumed then - so a click on a screen's button doesn't also fire a weapon.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Interaction")
	int32 InputPriority = 100;

	// -------------------------------------------------------------------------------------------------
	// Materials
	// -------------------------------------------------------------------------------------------------

	/**
	 * The plugin's screen materials (Material Automatic) a packaged game loads: made by the editor in /NameTBD/Diegetic (or
	 * /Game/NameTBD/Diegetic when the plugin's folder can't be written), and cooked with it (the plugin's DefaultGame.ini). In
	 * the editor the screens use the editor's own, made or remade as needed.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> ScreenMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> GlassScreenMaterial;

	UPROPERTY(Config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> OpaqueScreenMaterial;

	/** The material for a variant. */
	const TSoftObjectPtr<UMaterialInterface>& GetScreenMaterial(EDiegeticMaterialVariant Variant) const;

	/** Scroll speed of the Scroll Stick keys at full tilt, in mouse wheel notches a second. */
	UPROPERTY(Config, EditAnywhere, Category = "Interaction", meta = (ClampMin = "0", UIMin = "1", UIMax = "30"))
	float StickScrollSpeed = 10.f;

	// -------------------------------------------------------------------------------------------------
	// Default keys (an interactor without input mapping contexts of its own)
	// -------------------------------------------------------------------------------------------------

	/** Click what the screen is pointed at - or, on a Focus View screen not in use, start using it. */
	UPROPERTY(Config, EditAnywhere, Category = "Default Keys")
	TArray<FKey> ClickKeys;

	/** Right click what the screen is pointed at. */
	UPROPERTY(Config, EditAnywhere, Category = "Default Keys")
	TArray<FKey> SecondaryClickKeys;

	/** Scroll where the screen is pointed at, a notch at a time. */
	UPROPERTY(Config, EditAnywhere, Category = "Default Keys")
	TArray<FKey> ScrollWheelKeys;

	/** Scroll where the screen is pointed at, smoothly, while held - a stick's axis. Only while a screen is in use. */
	UPROPERTY(Config, EditAnywhere, Category = "Default Keys")
	TArray<FKey> ScrollStickKeys;

	/** Leave the screen in use (unless its widget handles Back - see Diegetic Screen Widget). */
	UPROPERTY(Config, EditAnywhere, Category = "Default Keys")
	TArray<FKey> BackKeys;

	/** Press the focused widget of the screen in use - a gamepad's confirm. */
	UPROPERTY(Config, EditAnywhere, Category = "Default Keys")
	TArray<FKey> AcceptKeys;

	/** Move focus on the screen in use. */
	UPROPERTY(Config, EditAnywhere, Category = "Default Keys")
	TArray<FKey> NavigateUpKeys;

	UPROPERTY(Config, EditAnywhere, Category = "Default Keys")
	TArray<FKey> NavigateDownKeys;

	UPROPERTY(Config, EditAnywhere, Category = "Default Keys")
	TArray<FKey> NavigateLeftKeys;

	UPROPERTY(Config, EditAnywhere, Category = "Default Keys")
	TArray<FKey> NavigateRightKeys;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif
};
