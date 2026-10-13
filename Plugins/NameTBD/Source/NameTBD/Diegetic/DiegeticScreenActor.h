// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DiegeticScreenActor.generated.h"

class UDiegeticScreenComponent;
class USceneComponent;

/**
 * A Diegetic Screen to drop into a level: pick its Widget Class and size in its Details, and it is shown, scheduled, woken and
 * usable with nothing else set up (see DiegeticTypes.h). Its screen hangs off a plain root, so the actor can be scaled and
 * turned like any other while Physical Size sets the screen's own scale. A Blueprint child adds a frame mesh around it.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Diegetic Screen Actor"))
class NAMETBD_API ADiegeticScreenActor : public AActor
{
	GENERATED_BODY()

public:

	ADiegeticScreenActor(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	UDiegeticScreenComponent* GetScreen() const { return Screen; }

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Diegetic Screen")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Diegetic Screen")
	TObjectPtr<UDiegeticScreenComponent> Screen;
};
