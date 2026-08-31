#include "UI/Cards/InfoCardTypes.h"

UWorld* UInfoCardPayload::GetWorld() const
{
	// Payloads are instanced sub-objects; borrow the world from whatever owns us
	// so Blueprint subclasses can still use world-context nodes.
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