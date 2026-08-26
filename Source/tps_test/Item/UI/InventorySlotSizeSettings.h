// Copyright Bilcash 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InventorySlotSizeSettings.generated.h"

USTRUCT(BlueprintType)
struct FInventorySlotSizeEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Identifier = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector2D Size = FVector2D(128.f, 128.f);
};

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Inventory Slot Sizes"))
class TPS_TEST_API UInventorySlotSizeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UInventorySlotSizeSettings();

	// Fully data-driven: add/remove/rename entries in Project Settings, no C++ needed.
	UPROPERTY(EditAnywhere, config, Category = "Inventory Slot Sizes")
	TArray<FInventorySlotSizeEntry> SlotSizes;

	static const TArray<FInventorySlotSizeEntry>& GetSlotSizes();

	UFUNCTION(BlueprintPure, Category = "Inventory Slot Sizes", DisplayName = "Get Slot Size")
	static FVector2D GetSlotSize(FName Identifier, bool& bFound);
	
	// Returns all valid identifiers, used to drive dropdowns via GetOptions
	UFUNCTION()
	static TArray<FName> GetSlotSizeIdentifiers();
};