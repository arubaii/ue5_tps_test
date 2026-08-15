#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ItemLibrary.generated.h"

class UDataTable;

/**
 * Central config for item data tables. Edit in Project Settings -> Game -> Item Tables.
 * Map each row-name prefix (e.g. "WPN_") to its data table. Add one entry per table.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Item Tables"))
class TPS_TEST_API UItemTableSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Config, Category = "Item Tables")
	TMap<FString, TSoftObjectPtr<UDataTable>> PrefixToTable;
};

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
	UFUNCTION(BlueprintPure, CustomThunk, Category = "Item Library",
	   meta = (CustomStructureParam = "OutItemRow", ReturnDisplayName = "Found"))
	static bool GetItemDef(FName RowName, int32& OutItemRow);
	DECLARE_FUNCTION(execGetItemDef);
	
	
	/** C++-friendly: get the shared FItemStructure for a row name. */
	UFUNCTION(BlueprintPure, Category = "Item Library", meta = (ReturnDisplayName = "Found"))
	static bool GetItemStructTyped(FName RowName, FItemStructure& OutItemStructure);

private:
	static UDataTable* GetTableForRowName(FName RowName);

	static bool Generic_GetRow(FName RowName, void* OutPtr,
						 const UScriptStruct* OutStruct, bool bExtractItemDetails);
};