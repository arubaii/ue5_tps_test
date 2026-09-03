#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "UI/Cards/InfoCardTypes.h"

#include "InfoCardSpecDataAsset.generated.h"

UCLASS(BlueprintType)
class TPS_TEST_API UInfoCardDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FInfoCardSpec> InfoCards;
};