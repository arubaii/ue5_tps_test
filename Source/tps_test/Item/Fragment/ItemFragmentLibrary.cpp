// Copyright arubaii. All rights reserved.

#include "Item/Fragment/ItemFragmentLibrary.h"


bool UItemFragmentLibrary::Generic_GetFragment(const TArray<FInstancedStruct>& fragments,
                                               FStructProperty* out_prop, void* out_ptr)
{
	if (!out_prop || !out_ptr)
	{
		return false;
	}

	UScriptStruct* wanted = out_prop->Struct;
	if (!wanted || !wanted->IsChildOf(FItemFragment::StaticStruct()))
	{
		// The connected pin is not a fragment type. Failing beats copying garbage into it.
		return false;
	}

	for (const FInstancedStruct& fragment : fragments)
	{
		const UScriptStruct* actual = fragment.GetScriptStruct();
		if (actual && actual->IsChildOf(wanted))
		{
			// Copying a derived fragment into a base-typed pin slices to the base portion,
			// which is the expected result when querying by a base type.
			wanted->CopyScriptStruct(out_ptr, fragment.GetMemory());
			return true;
		}
	}
	return false;
}

bool UItemFragmentLibrary::Generic_SetFragment(FItemInstance& instance,
                                               const FStructProperty* in_prop, const void* in_ptr)
{
	if (!in_prop || !in_ptr)
	{
		return false;
	}

	UScriptStruct* incoming = in_prop->Struct;
	if (!incoming || !incoming->IsChildOf(FItemFragment::StaticStruct()))
	{
		return false;
	}

	// Replace an existing fragment of exactly this type...
	for (FInstancedStruct& fragment : instance.Fragments)
	{
		if (fragment.GetScriptStruct() == incoming)
		{
			fragment.InitializeAs(incoming, static_cast<const uint8*>(in_ptr));
			return true;
		}
	}

	// ...or add one the definition did not author.
	FInstancedStruct newFragment;
	newFragment.InitializeAs(incoming, static_cast<const uint8*>(in_ptr));
	instance.Fragments.Add(MoveTemp(newFragment));
	return true;
}

bool UItemFragmentLibrary::HasItemFragment(const FItemInstance& instance, UScriptStruct* fragment_type)
{
	if (!fragment_type)
	{
		return false;
	}

	for (const FInstancedStruct& fragment : instance.Fragments)
	{
		const UScriptStruct* actual = fragment.GetScriptStruct();
		if (actual && actual->IsChildOf(fragment_type))
		{
			return true;
		}
	}
	return false;
}