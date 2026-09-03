// Copyright arubaii. All rights reserved.

#include "WorldItemActor.h"

#include "Interactable/Components/InteractableComponent.h"

#include "Inventory/Components/InventoryComponent.h"
#include "Inventory/Fragment/ItemFragment.h"
#include "Inventory/ItemLibrary.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Misc/Guid.h"
#include "Net/UnrealNetwork.h"

AWorldItemActor::AWorldItemActor()
{
	// Components and volume setup all come from AInteractableActor.
}

void AWorldItemActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWorldItemActor, ItemInstance);
	DOREPLIFETIME(AWorldItemActor, AttachedInstances);
	DOREPLIFETIME(AWorldItemActor, bRuntimeSpawned);
}

void AWorldItemActor::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && !bRuntimeSpawned)
	{
		InitializeFromLevelData();
	}
}

void AWorldItemActor::InitializeFromLevelData()
{
	// A level-placed pickup mints its own instance once, on the server, and replicates
	// it down. The Blueprint did this on every client independently, which produced a
	// different Guid per machine for what is supposed to be one object.
	if (ItemRow.RowName.IsNone())
	{
		return;
	}

	FItemInstance NewInstance;
	NewInstance.UUID = FGuid::NewGuid();
	NewInstance.RowName = ItemRow.RowName;
	NewInstance.Quantity = FMath::Max(1, Quantity);
	NewInstance.IsWorldItem = true;

	// Seed the instance's fragments from the row's authored defaults, so a level-placed
	// weapon arrives with its ammo and durability already correct.
	FItemDefinition Definition;
	if (UItemLibrary::GetItemDefinition(ItemRow.RowName, Definition))
	{
		NewInstance.Fragments = Definition.DefaultFragments;
	}

	ItemInstance = NewInstance;
	OnItemInstanceChanged();
}

void AWorldItemActor::OnRep_ItemInstance()
{
	OnItemInstanceChanged();
}

// =====================================================================================
//  IInteractableInterface — the item system bridge
// =====================================================================================

EInteractionMethod AWorldItemActor::GetInteractionMethod_Implementation() const
{
	// One fragment read. If the item says how it wants to be picked up, that wins; if it
	// has nothing to say, the component's authored default stands.
	if (const FUsableFragment* Usable = ItemInstance.GetFragment<FUsableFragment>())
	{
		return Usable->InteractionMethod;
	}

	return Interactable ? Interactable->Profile.Method : EInteractionMethod::Press;
}

float AWorldItemActor::GetHoldDuration_Implementation() const
{
	if (const FUsableFragment* Usable = ItemInstance.GetFragment<FUsableFragment>())
	{
		if (Usable->HoldDuration > 0.f)
		{
			return Usable->HoldDuration;
		}
	}

	return Interactable ? Interactable->Profile.HoldDuration : 0.f;
}

UObject* AWorldItemActor::GetInteractionWidgetPayload_Implementation() const
{
	// Nothing by default. Override in a subclass to hand widgets a UObject holding the
	// resolved definition — the plugin forwards it blind and never looks inside.
	return nullptr;
}

bool AWorldItemActor::CanInteract_Implementation(AActor* Interactor) const
{
	// Deterministic and side-effect free: the server runs this exact function again
	// before committing, and the two answers must agree.
	if (!IsValid(Interactor))
	{
		return false;
	}

	// An item actor with no instance is mid-initialisation, not interactable yet.
	if (!ItemRow.RowName.IsNone() && !ItemInstance.IsValid())
	{
		return false;
	}

	return true;
}

void AWorldItemActor::OnInteract_Implementation(AActor* Interactor)
{
	if (!HasAuthority() || !IsValid(Interactor))
	{
		return;
	}

	if (!ItemInstance.IsValid())
	{
		return;
	}

	// Resolve the inventory from the ACTUAL interactor. The Blueprint used
	// GetPlayerState(0) here, which on a listen server hands every client's pickup to
	// the host.
	UInventoryComponent* Inventory = nullptr;

	if (const APawn* AsPawn = Cast<APawn>(Interactor))
	{
		if (const AController* Controller = AsPawn->GetController())
		{
			if (APlayerState* PlayerState = Controller->PlayerState)
			{
				Inventory = PlayerState->FindComponentByClass<UInventoryComponent>();
			}
		}
	}

	if (!Inventory)
	{
		Inventory = Interactor->FindComponentByClass<UInventoryComponent>();
	}

	if (Inventory)
	{
		// PickupItem pulls the payload back out through IWorldItemInterface and calls
		// OnWorldItemFullyPickedUp or SetWorldItemInstance depending on the remainder,
		// so this actor's lifetime is handled for us.
		Inventory->PickupItem(this);
	}
}

// =====================================================================================
//  IWorldItemInterface
// =====================================================================================

FItemInstance AWorldItemActor::GetWorldItemInstance_Implementation()
{
	// The Blueprint left this and the one below unimplemented, so every pickup pulled a
	// default-constructed instance out of a world item and silently took nothing.
	return ItemInstance;
}

TArray<FItemInstance> AWorldItemActor::GetWorldItemAttachedInstances_Implementation()
{
	return AttachedInstances;
}

void AWorldItemActor::SetWorldItemInstance_Implementation(const FItemInstance& NewInstance)
{
	if (!HasAuthority())
	{
		return;
	}

	ItemInstance = NewInstance;
	bRuntimeSpawned = true;
	OnItemInstanceChanged();
}

void AWorldItemActor::SetWorldItemPayload_Implementation(const FItemInstance& NewInstance, const TArray<FItemInstance>& InAttachedInstances)
{
	if (!HasAuthority())
	{
		return;
	}

	ItemInstance = NewInstance;
	// The Blueprint accepted this parameter and dropped it on the floor, so a dropped
	// rifle lost its fitted attachments the moment it hit the ground.
	AttachedInstances = InAttachedInstances;
	bRuntimeSpawned = true;
	OnItemInstanceChanged();
}

void AWorldItemActor::OnWorldItemFullyPickedUp_Implementation()
{
	if (HasAuthority())
	{
		Destroy();
	}
}