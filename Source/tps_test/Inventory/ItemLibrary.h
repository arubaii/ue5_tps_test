#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"        // UDataTable, FTableRowBase, FindRow
#include "Templates/SubclassOf.h"    
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Structs/InventoryEnums.h"
#include "Utils/WildcardThunk/WildcardThunkMacros.h"
#include "DeveloperSettings/InventorySettings.h"       // UInventorySettings moved here — see that file
#include "ItemLibrary.generated.h"

class UDataTable;

UCLASS()
class TPS_TEST_API UItemLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Returns the shared item structure embedded in a row.
	 * The out pin is a wildcard only because the shared struct is Blueprint-defined and
	 * cannot be named in C++ — always break by S_itemStructure here
	 * @param RowName            The row to look up; its prefix selects the table.
	 * @param OutItemStructure   The shared item structure from the row. Always break by S_itemStructure here.
	 * @return True if the row was found and the structure copied.
	 */
	UFUNCTION(BlueprintPure, CustomThunk, Category = "Item Library", 
		meta = (CustomStructureParam = "OutItemStructure", ReturnDisplayName = "Found"))
	static bool GetItemStruct(FName RowName, int32& OutItemStructure);
	DECLARE_FUNCTION(execGetItemStruct);

	/**
	 * Returns the full typed row for a name.
	 * The out pin is a wildcard so each caller can connect the specific row struct
	 * (S_WeaponRow, S_AttachmentRow, etc.) matching that row's table.
	 * @param RowName       The row to look up; its prefix selects the table.
	 * @param OutItemRow    The full row struct. Always break the specific row type (S_WeaponRow, S_AttachmentRow etc.).
	 * @return True if the row was found and copied.
	 */
	/** Picker-driven row lookup. The node's dropdown types the OutItemRow pin. */
	UFUNCTION(BlueprintPure, CustomThunk, Category = "Item Library",
		meta = (CustomStructureParam = "OutItemRow",
				StructTypePin        = "RowType",
				ReturnDisplayName     = "Found",
				DisplayName           = "Get Item Def Typed",
				BlueprintInternalUseOnly = "true"))
	static bool GetItemDefTyped(FName RowName,
		UPARAM(meta = (MetaStruct = "/Script/TableRow.TableRowBase")) UScriptStruct* RowType,
		int32& OutItemRow);

	WILDCARD_THUNK_GET(
		GetItemDefTyped,
		P_GET_PROPERTY(FNameProperty, RowName); P_GET_OBJECT(UScriptStruct, RowType);,
		bResult = Generic_GetRow(RowName, WildcardArg.Address, WildcardArg.Property->Struct, /*bExtractItemDetails=*/false);
	)
	
	
	/** C++-friendly: get the shared FItemStructure for a row name. */
	UFUNCTION(BlueprintPure, Category = "Item Library", meta = (ReturnDisplayName = "Found"))
	static bool GetItemDefinition(FName RowName, FItemDefinition& OutItemStructure);
	
public:
	/**
	 * Typed row lookup: resolves the DataTable for RowName via the UItemTableSettings
	 * prefix map, verifies the table's row struct is (or derives from) T, and copies the
	 * row into OutRow. Returns false if the prefix/table/row aren't found or the struct
	 * type doesn't match. Header-defined because it's a template.
	 *
	 * Usage: UItemLibrary::GetItemDefTyped<FWeaponRow>(RowName, OutWeaponRow);
	 */
	template<typename T>
	static bool GetItemDefTyped(FName RowName, T& OutRow)
	{
		static_assert(TIsDerivedFrom<T, FTableRowBase>::IsDerived,
		              "GetItemDefTyped<T>: T must derive from FTableRowBase.");

		OutRow = T();

		if (RowName == NAME_None)
		{
			return false;
		}

		const UInventorySettings* Settings = GetDefault<UInventorySettings>();
		if (!Settings)
		{
			return false;
		}

		// Prefix -> table (first matching prefix wins), matching UItemLibrary's own routing.
		const FString RowStr = RowName.ToString();
		UDataTable* Table = nullptr;
		for (const TPair<FString, TSoftObjectPtr<UDataTable>>& Pair : Settings->PrefixToTable)
		{
			if (RowStr.StartsWith(Pair.Key))
			{
				Table = Pair.Value.LoadSynchronous();
				break;
			}
		}

		// The table's row struct must actually be T (GetItemDef's exact-match behavior).
		if (!Table || !Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(T::StaticStruct()))
		{
			return false;
		}

		const T* Found = Table->FindRow<T>(RowName, TEXT("UItemLibrary::GetItemDefTyped"), /*bWarnIfMissing*/ false);
		if (!Found)
		{
			return false;
		}

		OutRow = *Found;
		return true;
	}

private:
	static UDataTable* GetTableForRowName(FName RowName);

	static bool Generic_GetRow(FName RowName, void* OutPtr,
						 const UScriptStruct* OutStruct, bool bExtractItemDetails);
};