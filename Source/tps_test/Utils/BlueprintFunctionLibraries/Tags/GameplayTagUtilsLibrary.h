#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "GameplayTagUtilsLibrary.generated.h"

class UStateTagsComponent;

UCLASS()
class TPS_TEST_API UGameplayTagUtilsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 
	 * Replaces the Blueprint Macro. Safely retrieves the State Tags Component and its Tags 
	 * for any given Actor in a multiplayer environment. Checks the Actor directly, 
	 * then falls back to checking its PlayerState if it is a Pawn or Controller.
	 */
	UFUNCTION(BlueprintPure, Category = "Tags")
	static void GetPlayerStateTags(AActor* TargetActor, FGameplayTagContainer& OutStateTags, UStateTagsComponent*& OutComponent);
};