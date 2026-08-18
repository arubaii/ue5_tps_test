// Copyright arubaii. All rights reserved.

#include "Public/Systems/Item/ItemFragmentLibrary.h"

bool UItemFragmentLibrary::Generic_GetFragment(const TArray<FInstancedStruct>& Fragments,
                                               FStructProperty* OutProp, void* OutPtr)
{
	if (!OutProp || !OutPtr)
	{
		return false;
	}

	UScriptStruct* wanted = OutProp->Struct;
	if (!wanted || !wanted->IsChildOf(FItemFragment::StaticStruct()))
	{
		// The connected pin is not a fragment type. Failing beats copying garbage into it.
		return false;
	}

	for (const FInstancedStruct& fragment : Fragments)
	{
		const UScriptStruct* actual = fragment.GetScriptStruct();
		if (actual && actual->IsChildOf(wanted))
		{
			// Copying a derived fragment into a base-typed pin slices to the base portion,
			// which is the expected result when querying by a base type.
			wanted->CopyScriptStruct(OutPtr, fragment.GetMemory());
			return true;
		}
	}
	return false;
}

bool UItemFragmentLibrary::Generic_SetFragment(FItemInstance& Instance,
                                               const FStructProperty* InProp, const void* InPtr)
{
	if (!InProp || !InPtr)
	{
		return false;
	}

	UScriptStruct* incoming = InProp->Struct;
	if (!incoming || !incoming->IsChildOf(FItemFragment::StaticStruct()))
	{
		return false;
	}

	// Replace an existing fragment of exactly this type...
	for (FInstancedStruct& fragment : Instance.Fragments)
	{
		if (fragment.GetScriptStruct() == incoming)
		{
			fragment.InitializeAs(incoming, static_cast<const uint8*>(InPtr));
			return true;
		}
	}

	// ...or add one the definition did not author.
	FInstancedStruct newFragment;
	newFragment.InitializeAs(incoming, static_cast<const uint8*>(InPtr));
	Instance.Fragments.Add(MoveTemp(newFragment));
	return true;
}

bool UItemFragmentLibrary::HasItemFragment(const FItemInstance& Instance, UScriptStruct* FragmentType)
{
	if (!FragmentType)
	{
		return false;
	}

	for (const FInstancedStruct& fragment : Instance.Fragments)
	{
		const UScriptStruct* actual = fragment.GetScriptStruct();
		if (actual && actual->IsChildOf(FragmentType))
		{
			return true;
		}
	}
	return false;
}