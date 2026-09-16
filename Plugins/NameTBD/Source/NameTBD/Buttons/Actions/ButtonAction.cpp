#include "ButtonAction.h"

#include "NameTBD/Buttons/ContextMenuButton.h"

void UButtonAction::ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads)
{
}

UWorld* UButtonAction::GetWorld() const
{
	// Actions are instanced sub-objects of the button that owns them; without this a Blueprint subclass
	// gets no world-context nodes at all, which rules out most of what an action would want to do.
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}

	if (const UObject* Owner = GetOuter())
	{
		return Owner->GetWorld();
	}

	return nullptr;
}