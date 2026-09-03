// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "InteractionSettings.generated.h"

class UMaterialInterface;

/**
 * Project Settings -> Game -> Interaction.
 *
 * Only things that are genuinely project-wide live here. Anything that could reasonably
 * differ between two pawns is on the interactor, and anything that could differ between
 * two props is on the interactable — a setting here is a statement that the whole game
 * agrees.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Interaction"))
class TPS_TEST_API UInteractionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UInteractionSettings();

	virtual FName GetCategoryName() const override { return FName("Game"); }

	/**
	 * Object channels the focus and ambient sweeps run against.
	 *
	 * Give your interactables their own object type and put only that here. Sweeping
	 * against WorldDynamic instead works but makes every crate and door in the level a
	 * candidate, which is the difference between a cheap sweep and a hitch.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Detection")
	TArray<TEnumAsByte<EObjectTypeQuery>> InteractableObjectTypes;

	/**
	 * Trace channel for the ambient line-of-sight check: camera to the interactable's
	 * focus point, "is anything solid in the way".
	 */
	UPROPERTY(config, EditAnywhere, Category = "Detection")
	TEnumAsByte<ETraceTypeQuery> LineOfSightChannel = TraceTypeQuery1;

	/**
	 * Overlay material used to highlight the focused actor. Needs a vector parameter
	 * named by HighlightColorParameter.
	 *
	 * Leave null to disable highlighting project-wide; interactables that ask for it
	 * simply won't get it, with no warnings and no cost.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	TSoftObjectPtr<UMaterialInterface> HighlightMaterial;

	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	FName HighlightColorParameter = FName("OutlineColor");

	/**
	 * Slack multiplier the server applies to the commit distance.
	 *
	 * The client decided it was in range some milliseconds ago; by the time the RPC
	 * lands both actors have moved. 1.0 rejects legitimate interactions on a laggy
	 * connection, anything large enough to notice is exploitable. 1.25 is the usual
	 * compromise.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Networking", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float ServerDistanceTolerance = 1.25f;

	/**
	 * Extra time the server allows a hold before accepting it, as a fraction of the
	 * authored duration. Covers RTT on the completion RPC.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Networking", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ServerHoldTolerance = 0.15f;

	static const UInteractionSettings& Get();
};
