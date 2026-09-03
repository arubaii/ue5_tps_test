// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable/Types/InteractionTypes.h"
#include "InteractableInterface.generated.h"

class UInteractableComponent;

UINTERFACE(MinimalAPI, Blueprintable, meta = (DisplayName = "Interactable"))
class UInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Optional actor-side hooks.
 *
 * ============================== WHEN YOU NEED THIS ==============================
 * You do not, for the common case. Adding a UInteractableComponent is enough: it
 * answers every question below out of authored data, and an actor that implements
 * nothing behaves exactly as configured.
 *
 * Implement this when a value has to be COMPUTED rather than authored — a chest whose
 * prompt depends on whether it's locked, a door that reads a quest flag, or an item
 * actor whose interaction method comes from its inventory fragment rather than from
 * the component's default. That last case is the entire reason this interface exists:
 * it is the seam the item system plugs into, and the plugin never knows an item system
 * is on the other side of it.
 * ===============================================================================
 *
 * Every function is a BlueprintNativeEvent with a working native default, so a partial
 * implementation is fine. Anything you don't override falls back to the component.
 */
class TPS_TEST_API IInteractableInterface
{
	GENERATED_BODY()

public:
	/**
	 * Can Interactor interact with this right now?
	 *
	 * Called on the client to decide whether to draw a prompt, and AGAIN on the server
	 * before committing — so this must be deterministic and must not mutate anything.
	 * A client that lies about it gets rejected server-side.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	bool CanInteract(AActor* Interactor) const;
	virtual bool CanInteract_Implementation(AActor* Interactor) const { return true; }

	/**
	 * How this actor is committed. Overriding is how a per-instance source of truth (an
	 * item fragment, a save flag) beats the component's authored default.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	EInteractionMethod GetInteractionMethod() const;
	virtual EInteractionMethod GetInteractionMethod_Implementation() const;

	/** Hold length in seconds. Ignored unless the method is Hold. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	float GetHoldDuration() const;
	virtual float GetHoldDuration_Implementation() const;

	/**
	 * AUTHORITY ONLY. The interaction committed. Do the thing here — open the door, hand
	 * the item over, start the dialogue.
	 *
	 * Never called on clients. If you need client-side feedback, bind
	 * UInteractableComponent::OnInteractionPerformed, which fires everywhere.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	void OnInteract(AActor* Interactor);
	virtual void OnInteract_Implementation(AActor* Interactor) {}

	/**
	 * Where the camera aims when testing line of sight, and where distance is measured
	 * from. Defaults to the component's authored socket, then the actor origin.
	 *
	 * This is the native home of the Blueprint's LineTracePoint scene component: point
	 * it at a spot the mesh doesn't occlude and prompts stop flickering on thin geometry.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	FVector GetInteractionFocusPoint() const;
	virtual FVector GetInteractionFocusPoint_Implementation() const;

	/**
	 * Object handed to every prompt widget this actor spawns, via
	 * IInteractionWidgetInterface::SetInteractionPayload.
	 *
	 * The plugin treats it as an opaque UObject* and never dereferences it. Return
	 * whatever your widgets need to read; returning null is normal.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	UObject* GetInteractionWidgetPayload() const;
	virtual UObject* GetInteractionWidgetPayload_Implementation() const { return nullptr; }

	/** Convenience: the component on this actor, if any. */
	static UInteractableComponent* FindInteractableComponent(const AActor* Actor);
	
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
	FVector2D GetInteractionWidgetSize() const;
};
