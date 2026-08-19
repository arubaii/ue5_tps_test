#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

/** Holds the resolved property + raw memory address for a CustomStructureParam wildcard arg. */
struct FWildcardStructArg
{
	FStructProperty* Property = nullptr;
	void* Address = nullptr;

	bool IsValid() const { return Property != nullptr && Address != nullptr; }
};

struct FWildcardThunkHelpers
{
	/** Steps one wildcard struct argument off the VM stack. Call after all fixed P_GET_* calls. */
	static FWildcardStructArg StepWildcardStruct(FFrame& Stack)
	{
		Stack.StepCompiledIn<FStructProperty>(nullptr);

		FWildcardStructArg Out;
		Out.Property = CastField<FStructProperty>(Stack.MostRecentProperty);
		Out.Address = Stack.MostRecentPropertyAddress;
		return Out;
	}
};