// Copyright arubaii. All rights reserved.

#include "InteractableActor.h"

#include "Components/InteractableComponent.h"
#include "Components/InteractionTagsComponent.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"

AInteractableActor::AInteractableActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// The detection volume is what the interactor's sweep finds. Keeping it separate
	// from the mesh means the reach of an object is authored, not an accident of how
	// large its art happens to be.
	//
	// Both shapes are created up front because the class of an inherited component
	// cannot be swapped from Blueprint. ApplyVolumeShape decides which one is live;
	// the other sits at NoCollision and is invisible to every query.
	InteractionVolume = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionVolume"));
	InteractionVolume->SetupAttachment(Mesh);
	InteractionVolume->SetSphereRadius(64.f);
	InteractionVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionVolume->SetGenerateOverlapEvents(false);

	InteractionVolumeBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionVolumeBox"));
	InteractionVolumeBox->SetupAttachment(Mesh);
	InteractionVolumeBox->SetBoxExtent(FVector(64.f, 64.f, 64.f));
	InteractionVolumeBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionVolumeBox->SetGenerateOverlapEvents(false);

	ApplyVolumeShape();

	LineTracePoint = CreateDefaultSubobject<USceneComponent>(TEXT("LineTracePoint"));
	LineTracePoint->SetupAttachment(Mesh);

	Interactable = CreateDefaultSubobject<UInteractableComponent>(TEXT("Interactable"));
	InteractionTags = CreateDefaultSubobject<UInteractionTagsComponent>(TEXT("InteractionTags"));
}

void AInteractableActor::BeginPlay()
{
	Super::BeginPlay();
	ApplyVolumeShape();
}

void AInteractableActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyVolumeShape();
}

#if WITH_EDITOR
void AInteractableActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AInteractableActor, VolumeShape))
	{
		ApplyVolumeShape();
	}
}
#endif

void AInteractableActor::ApplyVolumeShape()
{
	const bool bUseBox = (VolumeShape == EInteractionVolumeShape::Box);

	// NoCollision is what makes the unused shape safe: a disabled primitive is never
	// returned by a sweep or a trace, whatever object type it carries.
	if (IsValid(InteractionVolume))
	{
		InteractionVolume->SetCollisionEnabled(bUseBox ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
		InteractionVolume->SetVisibility(!bUseBox);
		InteractionVolume->SetHiddenInGame(true);
	}

	if (IsValid(InteractionVolumeBox))
	{
		InteractionVolumeBox->SetCollisionEnabled(bUseBox ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		InteractionVolumeBox->SetVisibility(bUseBox);
		InteractionVolumeBox->SetHiddenInGame(true);
	}
}

UShapeComponent* AInteractableActor::GetActiveInteractionVolume() const
{
	return (VolumeShape == EInteractionVolumeShape::Box)
		? static_cast<UShapeComponent*>(InteractionVolumeBox)
		: static_cast<UShapeComponent*>(InteractionVolume);
}

FVector AInteractableActor::GetInteractionFocusPoint_Implementation() const
{
	// The authored trace point beats everything.
	if (IsValid(LineTracePoint))
	{
		return LineTracePoint->GetComponentLocation();
	}
	return GetActorLocation();
}