// Copyright arubaii. All rights reserved.

#include "InteractableInterface.h"
#include "Interactable/Components/InteractableComponent.h"
#include "GameFramework/Actor.h"

UInteractableComponent* IInteractableInterface::FindInteractableComponent(const AActor* Actor)
{
	return IsValid(Actor) ? Actor->FindComponentByClass<UInteractableComponent>() : nullptr;
}

// The defaults below all resolve through the component. That fall-through is what makes
// the interface optional: implement a function to compute the answer, implement nothing
// and get the authored one.

EInteractionMethod IInteractableInterface::GetInteractionMethod_Implementation() const
{
	if (const UInteractableComponent* Component = FindInteractableComponent(Cast<AActor>(_getUObject())))
	{
		return Component->Profile.Method;
	}
	return EInteractionMethod::Press;
}

float IInteractableInterface::GetHoldDuration_Implementation() const
{
	if (const UInteractableComponent* Component = FindInteractableComponent(Cast<AActor>(_getUObject())))
	{
		return Component->Profile.HoldDuration;
	}
	return 0.f;
}

FVector IInteractableInterface::GetInteractionFocusPoint_Implementation() const
{
	if (const UInteractableComponent* Component = FindInteractableComponent(Cast<AActor>(_getUObject())))
	{
		return Component->GetFocusPoint();
	}

	const AActor* AsActor = Cast<AActor>(_getUObject());
	return AsActor ? AsActor->GetActorLocation() : FVector::ZeroVector;
}
