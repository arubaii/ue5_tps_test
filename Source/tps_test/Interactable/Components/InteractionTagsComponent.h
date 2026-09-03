// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "InteractionTagsComponent.generated.h"

/**
 * Gameplay tags describing an object. The native BPC_InteractionTags.
 *
 * NOTE ON SCOPE: the Blueprint version of this component was doing two jobs. One was
 * legitimate — authored tags describing what a thing is. The other was storing
 * ItemWidgetState.ShowingInteractable / ShowingAmbient / ShowingStatic, which is not a
 * property of the object at all but of one particular player's view of it, and which
 * broke the moment two players looked at the same crate. That half now lives inside
 * UInteractorComponent and this component kept only the job it was actually good at.
 *
 * Implements IGameplayTagAssetInterface, which means the interaction plugin's
 * suppression query finds these tags with no extra wiring — put this on a PlayerState
 * with a State.UI.Menu tag in it and prompts hide themselves.
 */
UCLASS(ClassGroup = (Tags), meta = (BlueprintSpawnableComponent))
class TPS_TEST_API UInteractionTagsComponent : public UActorComponent, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UInteractionTagsComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Tags")
	FGameplayTagContainer InteractionTags;

	// IGameplayTagAssetInterface
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	UFUNCTION(BlueprintCallable, Category = "Tags")
	void AddInteractionTag(FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "Tags")
	void RemoveInteractionTag(FGameplayTag Tag);
};
