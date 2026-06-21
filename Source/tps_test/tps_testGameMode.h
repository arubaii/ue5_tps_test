// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "tps_testGameMode.generated.h"

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class Atps_testGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	Atps_testGameMode();
};



