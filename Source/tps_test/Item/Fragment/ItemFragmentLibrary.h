// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Item/Structs/Definition.h"
#include "Item/Structs/Instance.h"
#include "Item/Fragment/ItemFragment.h"
#include "Utils/WildcardThunk/WildcardThunkMacros.h"

#include "InstancedStruct.h"
#include "ItemFragmentLibrary.generated.h"

/**
 * Wildcard Blueprint access to item fragments.
 *
 * The connected pin's struct type IS the query: plug a FWeaponItemFragment variable into
 * the wildcard output and you get the weapon fragment back. One node covers every fragment
 * type you will ever add, so the UI never needs a bespoke getter per type.
 *
 * These operate on an FItemInstance VALUE. Writing through them mutates that value only -
 * to change a stored item, use UInventoryComponent's "By Id" variants, which fetch, write
 * and replicate in one step.
 */
UCLASS()
class TPS_TEST_API UItemFragmentLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Read a fragment off an instance. */
	UFUNCTION(BlueprintPure, CustomThunk, Category = "Items|Fragments",
		meta = (CustomStructureParam = "OutFragment",
				StructTypePin        = "FragmentType",
				DisplayName			 = "Get Item Fragment",
				BlueprintInternalUseOnly = "true"))
	static bool GetItemFragment(const FItemInstance& Instance,
		UPARAM(meta = (MetaStruct = "/Script/tps_test.ItemFragment")) UScriptStruct* FragmentType, 
		int32& OutFragment);
	
	/**
	Writes a fragment into the passed instance, replacing an existing fragment of that type or adding one if absent. 
	This mutates your local instance ONLY — the inventory's stored copy is unaffected. Follow with SetItemInstance, 
	or use SetItemFragmentById.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Items|Fragments",
	meta = (CustomStructureParam	 = "InFragment",
			StructTypePin			 = "FragmentType",
			DisplayName              = "Set Item Fragment",
			BlueprintInternalUseOnly = "true"))
	static bool SetItemFragment(UPARAM(ref) FItemInstance& Instance,
		UPARAM(meta = (MetaStruct = "/Script/tps_test.ItemFragment")) UScriptStruct* FragmentType,
		const int32& InFragment);

	/** Read a definition's authored default fragment. */
	UFUNCTION(BlueprintPure, CustomThunk, Category = "Items|Fragments",
	meta = (CustomStructureParam	 = "OutFragment",
			StructTypePin            = "FragmentType",
			DisplayName              = "Get Default Fragment",
			BlueprintInternalUseOnly = "true"))
	static bool GetDefaultFragment(const FItemDefinition& Definition,
		UPARAM(meta = (MetaStruct = "/Script/tps_test.ItemFragment")) UScriptStruct* FragmentType,
		int32& OutFragment);
	
	/** Presence check of a selected fragment. */
	UFUNCTION(BlueprintPure, Category = "Items|Fragments")
	static bool HasItemFragment(const FItemInstance& Instance, 
								UPARAM(meta = (MetaStruct = "/Script/tps_test.ItemFragment")) UScriptStruct* FragmentType);
	
	
	WILDCARD_THUNK_GET(
	GetItemFragment,
	P_GET_STRUCT_REF(FItemInstance, Instance); P_GET_OBJECT(UScriptStruct, FragmentType);,
	bResult = Generic_GetFragment(Instance.Fragments, WildcardArg.Property, WildcardArg.Address);
	)
	
	WILDCARD_THUNK_SET(
	SetItemFragment,
	P_GET_STRUCT_REF(FItemInstance, Instance); P_GET_OBJECT(UScriptStruct, FragmentType);,
	bResult = Generic_SetFragment(Instance, WildcardArg.Property, WildcardArg.Address);
	)
	
	WILDCARD_THUNK_GET(
	GetDefaultFragment,
	P_GET_STRUCT_REF(FItemDefinition, Definition); P_GET_OBJECT(UScriptStruct, FragmentType);,
	bResult = Generic_GetFragment(Definition.DefaultFragments, WildcardArg.Property, WildcardArg.Address);
	)
	
	// Shared implementations, also used by UInventoryComponent's thunks
	static bool Generic_GetFragment(const TArray<FInstancedStruct>& Fragments, FStructProperty* OutProp, void* OutPtr);
	static bool Generic_SetFragment(FItemInstance& Instance, const FStructProperty* InProp, const void* InPtr);
	
	
};