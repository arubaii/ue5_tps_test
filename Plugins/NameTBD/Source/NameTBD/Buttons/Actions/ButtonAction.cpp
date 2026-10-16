#include "ButtonAction.h"

#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Actions/ContextMenuInterface.h"

void UButtonAction::ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads)
{
}

void UButtonAction::ExecuteMenuAction_Implementation(const FContextMenuActionContext& Context)
{
	TArray<UContextButtonPayload*> RawPayloads;
	RawPayloads.Reserve(Context.Payloads.Num());
	for (const TObjectPtr<UContextButtonPayload>& Payload : Context.Payloads)
	{
		RawPayloads.Add(Payload.Get());
	}

	Execute_ExecuteAction(this, Context.Button, RawPayloads);
}

UContextButtonPayload* UButtonAction::FindActionPayload(const TArray<UContextButtonPayload*>& Payloads, TSubclassOf<UContextButtonPayload> PayloadClass)
{
	for (UContextButtonPayload* Payload : Payloads)
	{
		if (Payload && (!PayloadClass || Payload->IsA(PayloadClass)))
		{
			return Payload;
		}
	}
	return nullptr;
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
