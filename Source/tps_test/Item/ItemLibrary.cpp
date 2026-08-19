#include "Item/ItemLibrary.h"
#include "Engine/DataTable.h"
#include "Structs/Definition.h"

UDataTable* UItemLibrary::GetTableForRowName(FName RowName)
{
    const UItemTableSettings* Settings = GetDefault<UItemTableSettings>();
    if (!Settings)
    {
        return nullptr;
    }

    const FString RowStr = RowName.ToString();
    for (const TPair<FString, TSoftObjectPtr<UDataTable>>& Pair : Settings->PrefixToTable)
    {
        if (RowStr.StartsWith(Pair.Key))
        {
            // Loads the table if not already loaded (soft ptr)
            return Pair.Value.LoadSynchronous();
        }
    }
    return nullptr;
}

bool UItemLibrary::Generic_GetRow(FName RowName, void* OutPtr,
                                  const UScriptStruct* OutStruct, bool bExtractItemDetails)
{
    if (!OutPtr || !OutStruct)
    {
        return false;
    }

    UDataTable* Table = GetTableForRowName(RowName);
    if (!Table)
    {
        return false;
    }

    const uint8* RowData = Table->FindRowUnchecked(RowName);
    if (!RowData)
    {
        return false;
    }

    const UScriptStruct* RowStruct = Table->GetRowStruct();
    if (!RowStruct)
    {
        return false;
    }

    // Case shared by both modes: the row struct IS the connected struct.
    // (e.g. the Generic table's row is S_itemStructure directly, so there's no embedded
    //  field to search for — the whole row is what we want to copy.)
    if (RowStruct == OutStruct)
    {
        RowStruct->CopyScriptStruct(OutPtr, RowData);
        return true;
    }

    if (!bExtractItemDetails)
    {
        // Full typed row requested, but the row struct doesn't match the connected struct.
        return false;
    }

    // Extract the embedded shared item structure from a larger row
    // (e.g. S_WeaponRow.ItemDetails), found by matching the connected struct's type.
    for (TFieldIterator<FStructProperty> It(RowStruct); It; ++It)
    {
        FStructProperty* StructProp = *It;
        if (StructProp->Struct == OutStruct)
        {
            const void* FieldPtr = StructProp->ContainerPtrToValuePtr<void>(RowData);
            OutStruct->CopyScriptStruct(OutPtr, FieldPtr);
            return true;
        }
    }
    return false;
}

DEFINE_FUNCTION(UItemLibrary::execGetItemStruct)
{
    P_GET_PROPERTY(FNameProperty, RowName);

    // Read the wildcard output struct pin off the stack
    Stack.StepCompiledIn<FStructProperty>(nullptr);
    void* OutPtr = Stack.MostRecentPropertyAddress;
    FStructProperty* StructProp = CastField<FStructProperty>(Stack.MostRecentProperty);

    P_FINISH;

    bool bResult = false;
    if (StructProp && OutPtr)
    {
        P_NATIVE_BEGIN;
        bResult = Generic_GetRow(RowName, OutPtr, StructProp->Struct, /*bExtractItemDetails=*/true);
        P_NATIVE_END;
    }
    *(bool*)RESULT_PARAM = bResult;
}



bool UItemLibrary::GetItemDefinition(FName RowName, FItemDefinition& OutItemStructure)
{
    return Generic_GetRow(RowName, &OutItemStructure, FItemDefinition::StaticStruct(), /*bExtractItemDetails=*/true);
}