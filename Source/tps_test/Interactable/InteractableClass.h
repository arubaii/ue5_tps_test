// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

/** 
 * Solely created for the use of TSubclassOf<AInteractable>, allowing for stronger typing
*/

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractableClass.generated.h"

UCLASS()
class TPS_TEST_API AInteractable : public AActor
{
	GENERATED_BODY()
};
