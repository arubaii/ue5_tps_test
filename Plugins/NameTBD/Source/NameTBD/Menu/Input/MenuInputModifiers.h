// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputModifiers.h"

#include "MenuInputModifiers.generated.h"

/**
 * Enhanced Input modifiers that read the player's settings, so sensitivity, invert and dead zone settings work by adding a
 * modifier to the mapping - no Blueprint. Each reads its settings from the player's Menu Settings Subsystem every frame (a
 * changed slider is felt at once, while the menu is still open); with no subsystem or no such setting the input passes as is.
 */

/**
 * Multiplies the input by settings (sensitivity) and flips axes by others (invert). Put it on the look mapping: mouse with
 * Controls.MouseSensitivity / Controls.MouseInvertY, the stick with Controls.GamepadSensitivityX / Y / Controls.GamepadInvertY.
 */
UCLASS(NotBlueprintable, meta = (DisplayName = "Scale By Setting (NameTBD)"))
class NAMETBD_API UInputModifierScaleBySetting : public UInputModifier
{
	GENERATED_BODY()

public:

	/** Multiplies X (and Y unless Scale Setting Y is set). None: 1. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Settings")
	FName ScaleSetting = TEXT("Controls.MouseSensitivity");

	/** Multiplies Y. None: Scale Setting. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Settings")
	FName ScaleSettingY;

	/** A Bool setting that flips X. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Settings")
	FName InvertXSetting;

	/** A Bool setting that flips Y. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Settings")
	FName InvertYSetting = TEXT("Controls.MouseInvertY");

	/** Multiplies on top of the settings (the game's own base sensitivity). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Settings")
	FVector BaseScale = FVector::OneVector;

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;
};

/**
 * A dead zone whose size is a setting (Controls.GamepadDeadZone): input under it reads 0, the rest is rescaled so the stick
 * still reaches full. Radial for sticks.
 */
UCLASS(NotBlueprintable, meta = (DisplayName = "Dead Zone By Setting (NameTBD)"))
class NAMETBD_API UInputModifierDeadZoneBySetting : public UInputModifier
{
	GENERATED_BODY()

public:

	/** The dead zone (0..1). None or missing: Default Dead Zone. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Settings")
	FName DeadZoneSetting = TEXT("Controls.GamepadDeadZone");

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "0", ClampMax = "1"))
	float DefaultDeadZone = 0.2f;

	/** Input past this reads as full. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "0", ClampMax = "1"))
	float UpperThreshold = 1.f;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Settings")
	EDeadZoneType Type = EDeadZoneType::Radial;

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;
};
