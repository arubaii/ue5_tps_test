// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DragVisualSettings.generated.h"

UENUM(BlueprintType)
enum class EDragVisualStyle : uint8
{
	Default UMETA(DisplayName = "Default Instant Snap"),
	SmoothFollow UMETA(DisplayName = "Preset 1: Smooth Follow"),
	BouncyElastic UMETA(DisplayName = "Preset 2: Bouncy Elastic"),
	DynamicVelocityTilt UMETA(DisplayName = "Preset 3: Dynamic Velocity Tilt")
};

UCLASS(Config=Game, defaultconfig, meta=(DisplayName="Drag Visual Settings"))
class NAMETBD_API UDragVisualSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDragVisualSettings()
	{
		// Default physical tuning values
		DragStyle = EDragVisualStyle::DynamicVelocityTilt;
		InterpSpeed = 15.0f;
		SpringStiffness = 400.0f; 
		SpringDamping = 25.0f;     // Slightly underdamped for a nice bounce
		TiltMultiplier = 0.015f;
		MaxTiltAngle = 20.0f;
	}

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Drag Settings")
	EDragVisualStyle DragStyle;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Drag Settings|Smooth Follow")
	float InterpSpeed;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Drag Settings|Physics")
	float SpringStiffness;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Drag Settings|Physics")
	float SpringDamping;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Drag Settings|Tilt")
	float TiltMultiplier;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Drag Settings|Tilt")
	float MaxTiltAngle;
};