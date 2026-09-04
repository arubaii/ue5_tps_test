#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "UI/Cards/InfoCardTypes.h"

#include "InfoCardSpecDataAsset.generated.h"

UCLASS(BlueprintType)
class TPS_TEST_API UInfoCardDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FInfoCardSpec> InfoCards;

    /**
     * When an anchor's suppression tags (see UInfoCardAnchor::AddSuppressionTag)
     * contain any tag in this container, EVERY card sourced from this data
     * asset is suppressed on that anchor — unlike FInfoCardSpec::SuppressionTags,
     * which only suppresses the one entry it's set on. Root tag: InfoCard.Suppress.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Info Card|Suppression")
    FGameplayTagContainer SuppressionTags;
};