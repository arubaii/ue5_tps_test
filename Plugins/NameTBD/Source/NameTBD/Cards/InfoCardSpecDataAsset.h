// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "InfoCardTypes.h"

#include "InfoCardSpecDataAsset.generated.h"

/**
 * Info cards shared by every Info Card Anchor that points at this asset. Each entry is one independent card (a quick hover
 * tip, a detailed one, a right-click card); an anchor layers its own Payload Defaults and Settings Overrides on top.
 *
 * Each entry's settings are grouped (Card, Trigger, Placement, Dismissal, Payloads, Animation, Sound, Suppression) by
 * the editor module's FInfoCardSpec customization; only Card, Trigger and Payloads start expanded.
 */
UCLASS(BlueprintType)
class NAMETBD_API UInfoCardDataAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    
    /** Every card this asset holds. Give each a Card ID: anchors match their per-instance defaults and overrides by it. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Info Cards", meta = (TitleProperty = "CardId"))
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
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (DisplayName = "Default Sounds"))
    TArray<FInfoCardSoundSet> DefaultSounds;

    /**
     * Additive animations used by every entry above whose own Additive Animations would play nothing.
     *
     * The animation counterpart of Default Sounds, and it follows the same fallback-not-merge rule: an
     * entry that authored its own animations meant those and not these. An anchor's Additive Animation
     * Overrides then sit on top of whichever of the two won.
     *
     * Additive, as always - these are layered on top of whatever the card class itself animates, never
     * instead of it - and every layer animates the whole card.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (DisplayName = "Default Additive Animations"))
    FInfoCardAdditiveAnimations DefaultCardAnimations;

    /** The Widget Animation list Default Additive Animations replaced. Hidden; PostLoad moves it into Default Additive Animations > Anim In. */
    UPROPERTY()
    TArray<FWidgetAnimEntryBase> DefaultAdditiveAnimations;

    /**
     * Total anchor suppression: 
     * When the owning anchor's suppression tags (by InfoCardAnchor->AddSuppressionTag) contain 
     * any tag in this container, the info card will be suppressed: it will not spawn, and if it is
     * already showing it is closed.
     * 
     * - For extra per-card suppression, add tags in the array element field SuppressionTags. 
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suppression")
    FGameplayTagContainer AnchorSuppressionTags;

    virtual void PostLoad() override
    {
        Super::PostLoad();

        // Fallback semantics kept: the old list was only used for entries with no animations of their own, which is
        // what Default Additive Animations is.
        if (!DefaultAdditiveAnimations.IsEmpty() && DefaultCardAnimations.IsEmpty())
        {
            DefaultCardAnimations.MigrateLegacyAnimations(DefaultAdditiveAnimations);
        }
        DefaultAdditiveAnimations.Reset();
    }
};
