#include "GameplayTagUtilsLibrary.h"
#include "Utils/Tags/StateTagsComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

void UGameplayTagUtilsLibrary::GetPlayerStateTags(AActor* TargetActor, FGameplayTagContainer& OutStateTags, UStateTagsComponent*& OutComponent)
{
	OutStateTags = FGameplayTagContainer::EmptyContainer;
	OutComponent = nullptr;

	if (!TargetActor)
	{
		return;
	}

	// 1. Try finding the component directly on the passed actor
	OutComponent = TargetActor->FindComponentByClass<UStateTagsComponent>();

	// 2. If not found and the target is a Pawn, check its PlayerState (Best practice for multiplayer)
	if (!OutComponent)
	{
		if (const APawn* Pawn = Cast<APawn>(TargetActor))
		{
			if (APlayerState* PS = Pawn->GetPlayerState())
			{
				OutComponent = PS->FindComponentByClass<UStateTagsComponent>();
			}
		}
	}

	// 3. If not found and the target is a Controller, check its PlayerState
	if (!OutComponent)
	{
		if (const APlayerController* PC = Cast<APlayerController>(TargetActor))
		{
			if (APlayerState* PS = PC->PlayerState)
			{
				OutComponent = PS->FindComponentByClass<UStateTagsComponent>();
			}
		}
	}

	// If successfully found on any of the above, output the component and its current tags
	if (OutComponent)
	{
		OutStateTags = OutComponent->StateTags;
	}
}