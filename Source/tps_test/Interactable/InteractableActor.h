// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"

#include "Interfaces/InteractableInterface.h"
#include "Types/InteractionTypes.h"

#include "InteractableActor.generated.h"

class UBoxComponent;
class UInteractableComponent;
class UInteractionTagsComponent;
class UShapeComponent;
class UStaticMeshComponent;
class USceneComponent;
class USphereComponent;

/**
 * Which primitive the detection volume uses.
 *
 * A sphere cannot be scaled non-uniformly — USphereComponent collapses the transform's
 * scale to its smallest axis, because the physics engine has no ellipsoid primitive. Use
 * Box when the reach of an object is directional: a door wants a shallow slab in front of
 * it, not a ball centred on the actor.
 */
UENUM(BlueprintType)
enum class EInteractionVolumeShape : uint8
{
	Sphere	UMETA(DisplayName = "Sphere"),
	Box		UMETA(DisplayName = "Box")
};

/**
 * A ready-made actor with the components an interactable normally wants: a mesh, a
 * detection volume, a focus point, the interactable component and a tag container.
 *
 * Deriving from this is a convenience, not a requirement. Any actor with a
 * UInteractableComponent works; this class just saves assembling the same five
 * components every time.
 *
 * It knows nothing about items, inventories, doors or quests. Anything that has to be
 * COMPUTED rather than authored goes in a subclass, through the IInteractableInterface
 * overrides — that interface is the only seam the plugin offers, and it is enough.
 */
UCLASS(Blueprintable, BlueprintType)
class TPS_TEST_API AInteractableActor : public AActor, public IInteractableInterface
{
	GENERATED_BODY()

public:
	AInteractableActor();

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ---------------------------------------------------------------- components

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interactable")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/**
	 * Where prompts attach and where line-of-sight aims.
	 *
	 * Park it somewhere the mesh does not occlude — a thin weapon lying flat will
	 * otherwise trace against its own barrel and flicker.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interactable")
	TObjectPtr<USceneComponent> LineTracePoint;

	/**
	 * Which volume is live. Both shapes exist on every interactable; the unselected one is
	 * set to NoCollision, so it is invisible to every trace and sweep and costs nothing.
	 *
	 * The class of an inherited component cannot be swapped from Blueprint, so this is how
	 * a designer picks between them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interactable")
	EInteractionVolumeShape VolumeShape = EInteractionVolumeShape::Sphere;

	/** Spherical detection volume. Live when VolumeShape is Sphere. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interactable")
	TObjectPtr<USphereComponent> InteractionVolume;

	/** Box detection volume. Live when VolumeShape is Box. Scales on all three axes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interactable")
	TObjectPtr<UBoxComponent> InteractionVolumeBox;

	/** The volume currently carrying collision. Use this rather than either pointer directly. */
	UFUNCTION(BlueprintPure, Category = "Interactable")
	UShapeComponent* GetActiveInteractionVolume() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interactable")
	TObjectPtr<UInteractableComponent> Interactable;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interactable")
	TObjectPtr<UInteractionTagsComponent> InteractionTags;

	// ---------------------------------------------------------------- IInteractableInterface

	/** Returns LineTracePoint's location, falling back to the actor origin. */
	virtual FVector GetInteractionFocusPoint_Implementation() const override;

protected:
	/** Enables the selected volume and disables the other. Safe to call repeatedly. */
	void ApplyVolumeShape();
};