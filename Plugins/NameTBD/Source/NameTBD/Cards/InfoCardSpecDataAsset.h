// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "InfoCardTypes.h"

#include "InfoCardSpecDataAsset.generated.h"

UCLASS(BlueprintType)
class NAMETBD_API UInfoCardDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FInfoCardSpec> InfoCards;

    /**
     * Total anchor suppression: 
     * When the owning anchor's suppression tags (by InfoCardAnchor->AddSuppressionTag) contain 
     * any tag in this container, the info card will be suppressed: it will not spawn, and if it is
     * already showing it is closed.
     * 
     * - For extra per-card suppression, add tags in the array element field SuppressionTags. 
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Info Card|Suppression")
    FGameplayTagContainer AnchorSuppressionTags;
};