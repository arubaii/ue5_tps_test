// Copyright Bilcash. All rights reserved.

#include "StructCategoryGroupsEditorModule.h"

#include "StructCategoryGroups.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/UObjectIterator.h"

namespace
{
    static const FName s_UseCategoryGroupsMetaKey(TEXT("UseCategoryGroups"));
}

void FStructCategoryGroupsEditorModule::RegisterOptedInStructs()
{
    FPropertyEditorModule& propertyModule =
       FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    for (TObjectIterator<UScriptStruct> structIt; structIt; ++structIt)
    {
       UScriptStruct* scriptStruct = *structIt;
       if (!scriptStruct)
       {
          continue;
       }

       // Opt-in gate: only structs whose USTRUCT() carries meta = (UseCategoryGroups)
       // are enrolled.
#if WITH_EDITORONLY_DATA
       if (!scriptStruct->HasMetaData(s_UseCategoryGroupsMetaKey))
       {
          continue;
       }
#else
       continue;
#endif

       const FName structName = scriptStruct->GetFName();
       if (m_RegisteredStructNames.Contains(structName))
       {
          continue;
       }

       propertyModule.RegisterCustomPropertyTypeLayout(
          structName,
          FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStructCategoryGroups::MakeInstance));

       m_RegisteredStructNames.Add(structName);
    }

    propertyModule.NotifyCustomizationModuleChanged();
}

void FStructCategoryGroupsEditorModule::StartupModule()
{
    RegisterOptedInStructs();
}

void FStructCategoryGroupsEditorModule::ShutdownModule()
{
    if (!FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
    {
       m_RegisteredStructNames.Reset();
       return;
    }

    FPropertyEditorModule& propertyModule =
       FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

    for (const FName& structName : m_RegisteredStructNames)
    {
       propertyModule.UnregisterCustomPropertyTypeLayout(structName);
    }
    m_RegisteredStructNames.Reset();

    propertyModule.NotifyCustomizationModuleChanged();
}

IMPLEMENT_MODULE(FStructCategoryGroupsEditorModule, StructCategoryGroupsEditor)