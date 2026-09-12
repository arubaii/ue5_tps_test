// Copyright arubaii. All rights reserved.

#include "InteractableComponent.h"
#include "Interactable/Interfaces/InteractableInterface.h"
#include "Interactable/Types/InteractionWidgetDataAsset.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

UInteractableComponent::UInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInteractableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UInteractableComponent, bAvailable);
	DOREPLIFETIME(UInteractableComponent, Profile);
	DOREPLIFETIME(UInteractableComponent, InteractionTags);
}

void UInteractableComponent::BeginPlay()
{
	Super::BeginPlay();

	#if !UE_BUILD_SHIPPING
	if (bDebugInteractable)
	{
		if (Profile.AmbientRange <= 0.f && WantsAmbientPresentation())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Interaction] %s has widget specs marked bShowWhenAmbient but AmbientRange is 0; they will never draw."),
				*GetNameSafe(GetOwner()));
		}

		if (bConstantWidgetSize && HasWorldSpaceWidgetSpec())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Interaction] %s has bConstantWidgetSize set but at least one widget spec is World space; ")
				TEXT("that setting only affects Screen space widgets and is ignored for the rest."),
				*GetNameSafe(GetOwner()));
		}
	}
	#endif
}

void UInteractableComponent::SetAvailable(bool bNewAvailable)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bAvailable == bNewAvailable)
	{
		return;
	}

	bAvailable = bNewAvailable;
	OnRep_Available();
}

void UInteractableComponent::OnRep_Available()
{
	OnAvailabilityChanged.Broadcast(bAvailable);
}

bool UInteractableComponent::CanBeInteractedWith(AActor* Interactor) const
{
	if (!bAvailable || !IsValid(Interactor))
	{
		return false;
	}

	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return false;
	}

	if (Owner->HasAuthority() && Profile.Cooldown > 0.f)
	{
		if (const UWorld* World = GetWorld())
		{
			if (World->GetTimeSeconds() - LastInteractionTime < Profile.Cooldown)
			{
				return false;
			}
		}
	}

	if (Owner->Implements<UInteractableInterface>())
	{
		return IInteractableInterface::Execute_CanInteract(Owner, Interactor);
	}

	return true;
}

EInteractionMethod UInteractableComponent::GetResolvedMethod() const
{
	if (const AActor* Owner = GetOwner(); IsValid(Owner) && Owner->Implements<UInteractableInterface>())
	{
		return IInteractableInterface::Execute_GetInteractionMethod(Owner);
	}
	return Profile.Method;
}

float UInteractableComponent::GetResolvedHoldDuration() const
{
	if (const AActor* Owner = GetOwner(); IsValid(Owner) && Owner->Implements<UInteractableInterface>())
	{
		return FMath::Max(0.f, IInteractableInterface::Execute_GetHoldDuration(Owner));
	}
	return FMath::Max(0.f, Profile.HoldDuration);
}

float UInteractableComponent::GetResolvedMaxDistance(float InteractorFallbackRadius) const
{
	return Profile.MaxInteractionDistance > 0.f ? Profile.MaxInteractionDistance : InteractorFallbackRadius;
}

USceneComponent* UInteractableComponent::ResolveAttachComponent() const
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return nullptr;
	}

	if (!AttachSocketName.IsNone())
	{
		TInlineComponentArray<USceneComponent*> SceneComponents(Owner);
		for (USceneComponent* Component : SceneComponents)
		{
			if (Component && Component->GetFName() == AttachSocketName)
			{
				return Component;
			}
		}
	}

	return Owner->GetRootComponent();
}

FVector UInteractableComponent::GetFocusPoint() const
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return FVector::ZeroVector;
	}

	if (const USceneComponent* Attach = ResolveAttachComponent())
	{
		if (!AttachSocketName.IsNone() && Attach->DoesSocketExist(AttachSocketName))
		{
			return Attach->GetSocketLocation(AttachSocketName) + FocusPointOffset;
		}
		return Attach->GetComponentLocation() + FocusPointOffset;
	}

	return Owner->GetActorLocation() + FocusPointOffset;
}

UObject* UInteractableComponent::GetWidgetPayload() const
{
	if (const AActor* Owner = GetOwner(); IsValid(Owner) && Owner->Implements<UInteractableInterface>())
	{
		return IInteractableInterface::Execute_GetInteractionWidgetPayload(Owner);
	}
	return nullptr;
}

bool UInteractableComponent::WantsAmbientPresentation() const
{
	if (WidgetSpecDataAsset)
	{
		for (const FInteractionWidgetSpec& Spec : WidgetSpecDataAsset->WidgetSpecs)
		{
			if (Spec.bShowWhenAmbient && Spec.IsValidSpec())
			{
				return true;
			}
		}
	}

	for (const FInteractionWidgetSpec& Spec : WidgetSpecs)
	{
		if (Spec.bShowWhenAmbient && Spec.IsValidSpec())
		{
			return true;
		}
	}
	return false;
}

bool UInteractableComponent::HasWorldSpaceWidgetSpec() const
{
	if (WidgetSpecDataAsset)
	{
		for (const FInteractionWidgetSpec& Spec : WidgetSpecDataAsset->WidgetSpecs)
		{
			if (Spec.IsValidSpec() && Spec.WidgetSpace == EWidgetSpace::World)
			{
				return true;
			}
		}
	}

	for (const FInteractionWidgetSpec& Spec : WidgetSpecs)
	{
		if (Spec.IsValidSpec() && Spec.WidgetSpace == EWidgetSpace::World)
		{
			return true;
		}
	}
	return false;
}

bool UInteractableComponent::PerformInteraction(AActor* Interactor)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority())
	{
		return false;
	}

	if (!CanBeInteractedWith(Interactor))
	{
		return false;
	}

	if (const UWorld* World = GetWorld())
	{
		LastInteractionTime = World->GetTimeSeconds();
	}

	if (Owner->Implements<UInteractableInterface>())
	{
		IInteractableInterface::Execute_OnInteract(Owner, Interactor);
	}

	if (Profile.bSingleUse)
	{
		SetAvailable(false);
	}

	Multicast_InteractionPerformed(Interactor);
	return true;
}

void UInteractableComponent::Multicast_InteractionPerformed_Implementation(AActor* Interactor)
{
	OnInteractionPerformed.Broadcast(GetOwner(), Interactor);
}