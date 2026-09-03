#include "StateTagsComponent.h"
#include "Net/UnrealNetwork.h"

UStateTagsComponent::UStateTagsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	
	// Ensure the component replicates so StateTags sync across the server and clients
	SetIsReplicatedByDefault(true);
}

void UStateTagsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UStateTagsComponent, StateTags);
}