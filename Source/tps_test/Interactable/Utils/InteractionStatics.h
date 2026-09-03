// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Interactable/Types/InteractionTypes.h"
#include "InteractionStatics.generated.h"

class UInteractableComponent;
class UInteractorComponent;

/**
 * The handful of lookups that would otherwise be repeated in every project.
 *
 * Nothing here holds state or makes a decision — if you find yourself wanting to add a
 * rule to this class, it belongs on one of the two components instead.
 */
UCLASS()
class TPS_TEST_API UInteractionStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The interactable component on an actor, or null. */
	UFUNCTION(BlueprintPure, Category = "Interaction", meta = (DefaultToSelf = "Actor"))
	static UInteractableComponent* GetInteractableComponent(AActor* Actor);

	/**
	 * The interactor for a pawn, controller or player state — whichever you happen to
	 * have. Resolving from any of the three means callers never have to know where the
	 * component was actually put.
	 */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	static UInteractorComponent* GetInteractorComponent(AActor* Actor);

	/** True if the actor can currently be interacted with by Interactor. */
	UFUNCTION(BlueprintPure, Category = "Interaction", meta = (ReturnDisplayName = "Can Interact"))
	static bool CanInteractWith(AActor* Actor, AActor* Interactor);

	/**
	 * Server helper for scripted interactions — a cutscene opening a door, a trigger
	 * volume taking an item. Skips focus and input entirely but still runs the
	 * interactable's own availability rules.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Interaction",
		meta = (ReturnDisplayName = "Performed"))
	static bool ForceInteract(AActor* Target, AActor* Interactor);
};
