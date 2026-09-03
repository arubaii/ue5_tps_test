// Copyright arubaii. All rights reserved.

#include "InteractionStatics.h"
#include "Interactable/Components/InteractableComponent.h"
#include "Interactable/Components/InteractorComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

UInteractableComponent* UInteractionStatics::GetInteractableComponent(AActor* Actor)
{
	return IsValid(Actor) ? Actor->FindComponentByClass<UInteractableComponent>() : nullptr;
}

UInteractorComponent* UInteractionStatics::GetInteractorComponent(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return nullptr;
	}

	if (UInteractorComponent* Direct = Actor->FindComponentByClass<UInteractorComponent>())
	{
		return Direct;
	}

	// Walk the usual places. A project that puts the interactor on the controller rather
	// than the pawn should not have to tell every caller about it.
	if (const APawn* AsPawn = Cast<APawn>(Actor))
	{
		if (AController* Controller = AsPawn->GetController())
		{
			if (UInteractorComponent* Found = Controller->FindComponentByClass<UInteractorComponent>())
			{
				return Found;
			}
			if (Controller->PlayerState)
			{
				return Controller->PlayerState->FindComponentByClass<UInteractorComponent>();
			}
		}
		return nullptr;
	}

	if (const AController* AsController = Cast<AController>(Actor))
	{
		if (APawn* Pawn = AsController->GetPawn())
		{
			if (UInteractorComponent* Found = Pawn->FindComponentByClass<UInteractorComponent>())
			{
				return Found;
			}
		}
		if (AsController->PlayerState)
		{
			return AsController->PlayerState->FindComponentByClass<UInteractorComponent>();
		}
		return nullptr;
	}

	if (const APlayerState* AsPlayerState = Cast<APlayerState>(Actor))
	{
		if (APawn* Pawn = AsPlayerState->GetPawn())
		{
			return Pawn->FindComponentByClass<UInteractorComponent>();
		}
	}

	return nullptr;
}

bool UInteractionStatics::CanInteractWith(AActor* Actor, AActor* Interactor)
{
	const UInteractableComponent* Interactable = GetInteractableComponent(Actor);
	return Interactable && Interactable->CanBeInteractedWith(Interactor);
}

bool UInteractionStatics::ForceInteract(AActor* Target, AActor* Interactor)
{
	UInteractableComponent* Interactable = GetInteractableComponent(Target);
	return Interactable && Interactable->PerformInteraction(Interactor);
}
