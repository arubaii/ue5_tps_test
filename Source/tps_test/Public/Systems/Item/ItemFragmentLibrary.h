// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Public/Systems/Item/Definition.h"
#include "Public/Systems/Item/Instance.h"
#include "Public/Systems/Item/ItemFragment.h"
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
	/** Read a fragment off an instance. The wildcard pin's type selects which. */
	UFUNCTION(BlueprintPure, CustomThunk, Category = "Items|Fragments",
		meta = (CustomStructureParam = "OutFragment", DisplayName = "Get Item Fragment"))
	static bool GetItemFragment(const FItemInstance& Instance, int32& OutFragment);

	/** Read a definition's AUTHORED default fragment, before any instance exists. */
	UFUNCTION(BlueprintPure, CustomThunk, Category = "Items|Fragments",
		meta = (CustomStructureParam = "OutFragment", DisplayName = "Get Default Fragment"))
	static bool GetDefaultFragment(const FItemStructure& Definition, int32& OutFragment);

	/**
	Writes a fragment into the instance you pass, replacing an existing fragment of that type or adding one if absent. 
	This mutates your local instance only — the inventory's stored copy is unaffected. Follow with SetItemInstance, 
	or use SetItemFragmentById.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Items|Fragments",
		meta = (CustomStructureParam = "InFragment", DisplayName = "Set Item Fragment"))
	static bool SetItemFragment(UPARAM(ref) FItemInstance& Instance, const int32& InFragment);

	/** Presence test with a type picker: "is this a weapon?" */
	UFUNCTION(BlueprintPure, Category = "Items|Fragments")
	static bool HasItemFragment(const FItemInstance& Instance, 
								UPARAM(meta = (MetaStruct = "/Script/tps_test.ItemFragment")) UScriptStruct* FragmentType);

	// --- Shared implementations, also used by UInventoryComponent's thunks. ---
	static bool Generic_GetFragment(const TArray<FInstancedStruct>& Fragments, FStructProperty* OutProp, void* OutPtr);
	static bool Generic_SetFragment(FItemInstance& Instance, const FStructProperty* InProp, const void* InPtr);

	DECLARE_FUNCTION(execGetItemFragment)
	{
		P_GET_STRUCT_REF(FItemInstance, Instance);
		Stack.StepCompiledIn<FStructProperty>(nullptr);
		void* OutPtr = Stack.MostRecentPropertyAddress;
		FStructProperty* OutProp = CastField<FStructProperty>(Stack.MostRecentProperty);
		P_FINISH;
		bool bResult = false;
		P_NATIVE_BEGIN;
		bResult = Generic_GetFragment(Instance.Fragments, OutProp, OutPtr);
		P_NATIVE_END;
		*static_cast<bool*>(RESULT_PARAM) = bResult;
	}

	DECLARE_FUNCTION(execGetDefaultFragment)
	{
		P_GET_STRUCT_REF(FItemStructure, Definition);
		Stack.StepCompiledIn<FStructProperty>(nullptr);
		void* OutPtr = Stack.MostRecentPropertyAddress;
		FStructProperty* OutProp = CastField<FStructProperty>(Stack.MostRecentProperty);
		P_FINISH;
		bool bResult = false;
		P_NATIVE_BEGIN;
		bResult = Generic_GetFragment(Definition.DefaultFragments, OutProp, OutPtr);
		P_NATIVE_END;
		*static_cast<bool*>(RESULT_PARAM) = bResult;
	}

	DECLARE_FUNCTION(execSetItemFragment)
	{
		P_GET_STRUCT_REF(FItemInstance, Instance);
		Stack.StepCompiledIn<FStructProperty>(nullptr);
		const void* InPtr = Stack.MostRecentPropertyAddress;
		FStructProperty* InProp = CastField<FStructProperty>(Stack.MostRecentProperty);
		P_FINISH;
		bool bResult = false;
		P_NATIVE_BEGIN;
		bResult = Generic_SetFragment(Instance, InProp, InPtr);
		P_NATIVE_END;
		*static_cast<bool*>(RESULT_PARAM) = bResult;
	}
	
	
	/** Picker-driven read. The FragmentType pin types the output pin (see UK2Node_GetItemFragment). */
	/** Picker-driven read. The FragmentType pin types the OutFragment pin. */
	/** Picker-driven read. The node's dropdown types the OutFragment pin. */
	UFUNCTION(BlueprintPure, CustomThunk, Category = "Items|Fragments",
		meta = (CustomStructureParam = "OutFragment",
				StructTypePin        = "FragmentType",
				DisplayName          = "Get Item Fragment"))
	static bool GetItemFragmentTyped(const FItemInstance& Instance,
		UPARAM(meta = (MetaStruct = "/Script/tps_test.ItemFragment")) UScriptStruct* FragmentType,
		int32& OutFragment);

	DECLARE_FUNCTION(execGetItemFragmentTyped)
	{
		P_GET_STRUCT_REF(FItemInstance, Instance);
		P_GET_OBJECT(UScriptStruct, FragmentType);
		Stack.StepCompiledIn<FStructProperty>(nullptr);
		void* outPtr = Stack.MostRecentPropertyAddress;
		FStructProperty* outProp = CastField<FStructProperty>(Stack.MostRecentProperty);
		P_FINISH;
		bool bResult = false;
		P_NATIVE_BEGIN;
		bResult = Generic_GetFragment(Instance.Fragments, outProp, outPtr);
		P_NATIVE_END;
		*static_cast<bool*>(RESULT_PARAM) = bResult;
	}
	
	/* ================ Common fragment helpers ================= */
	
	UFUNCTION(BlueprintPure, Category = "Items|Fragments")
	static bool IsStackable(const FItemInstance& Instance)
	{
		return HasItemFragment(Instance, FStackableFragment::StaticStruct());
	}
	
	
	
};