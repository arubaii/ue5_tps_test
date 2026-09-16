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
     * Sounds used by every entry above that doesn't author its own. Most assets want one set of tooltip
     * noises for the whole file, and repeating it per entry is both tedious and something that drifts out of
     * sync the moment one card is retuned.
     *
     * A per-entry Sounds array wins outright rather than merging: a card that has bothered to specify its own
     * sounds means those and not these. An anchor's Settings Overrides > Sound then sits on top of whichever
     * of the two won.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Info Card|Sound", meta = (DisplayName = "Default Sounds"))
    TArray<FInfoCardSoundSet> DefaultSounds;

    /**
     * Animations used by every entry above whose own Additive Animations > Animations array is empty.
     *
     * The animation counterpart of Default Sounds, and it follows the same fallback-not-merge rule: an
     * entry that named its own animations meant those and not these. An anchor's Additive Animation
     * Overrides then sit on top of whichever of the two won.
     *
     * Additive, as always - these are layered on top of whatever the card class itself animates, never
     * instead of it - and every layer animates the whole card, since an animation shared across assets
     * has no way to name a widget inside one particular card.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Info Card|Animations", meta = (DisplayName = "Default Animations", TitleProperty = "Animation"))
    TArray<FWidgetAnimEntryBase> DefaultAdditiveAnimations;

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
