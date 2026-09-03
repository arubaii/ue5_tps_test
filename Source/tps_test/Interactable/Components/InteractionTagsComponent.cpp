// Copyright arubaii. All rights reserved.

#include "Interactable/Components/InteractionTagsComponent.h"
#include "Net/UnrealNetwork.h"

UInteractionTagsComponent::UInteractionTagsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInteractionTagsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UInteractionTagsComponent, InteractionTags);
}

void UInteractionTagsComponent::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.AppendTags(InteractionTags);
}

void UInteractionTagsComponent::AddInteractionTag(FGameplayTag Tag)
{
	if (Tag.IsValid())
	{
		InteractionTags.AddTag(Tag);
	}
}

void UInteractionTagsComponent::RemoveInteractionTag(FGameplayTag Tag)
{
	InteractionTags.RemoveTag(Tag);
}
