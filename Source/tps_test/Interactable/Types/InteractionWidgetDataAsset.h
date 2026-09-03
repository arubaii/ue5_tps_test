// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InteractionTypes.h"
#include "InteractionWidgetDataAsset.generated.h"

/**
 * Data asset holding reusable interaction widget specs.
 */
UCLASS(BlueprintType)
class TPS_TEST_API UInteractionWidgetDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	TArray<FInteractionWidgetSpec> WidgetSpecs;
};