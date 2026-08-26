// Copyright Bilcash 2026. All rights reserved.

#include "StructCategoryGroupsEditorModule.h"

#include "StructCategoryGroups.h"
#include "StructCategoryGroupsSettings.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
    static const FName s_UseCategoryGroupsMetaKey(TEXT("UseCategoryGroups"));

    /**
     * Normalizes a property's Category metadata into the same "A|B|C" path form
     * that FStructCategoryGroups::CustomizeChildren builds at display time.
     * Both must agree exactly or the seeded key won't match the lookup key.
     * Returns false if the property has no usable category.
     */
    bool BuildCategoryPath(const FProperty* property,
                           const UScriptStruct* owningStruct,
                           FString& outPath)
    {
       outPath.Reset();

       if (!property || !owningStruct)
       {
          return false;
       }

       FString category = property->GetMetaData(TEXT("Category"));
       if (category.IsEmpty())
       {
          return false;
       }

       TArray<FString> segments;
       category.ParseIntoArray(segments, TEXT("|"), /*InCullEmpty*/ true);
       for (FString& segment : segments)
       {
          segment.TrimStartAndEndInline();
       }

       // Mirrors CustomizeChildren: a leading segment matching the struct's own
       // name is redundant nesting and gets dropped.
       if (segments.Num() > 0 && segments[0].Equals(owningStruct->GetName(), ESearchCase::IgnoreCase))
       {
          segments.RemoveAt(0);
       }

       if (segments.Num() == 0)
       {
          return false;
       }

       for (const FString& segment : segments)
       {
          outPath = outPath.IsEmpty() ? segment : (outPath + TEXT("|") + segment);
       }
       return true;
    }
}

void FStructCategoryGroupsEditorModule::SeedCategoryAssignments(const UScriptStruct* scriptStruct)
{
    UStructCategoryGroupsSettings* settings = GetMutableDefault<UStructCategoryGroupsSettings>();
    if (!settings || !scriptStruct || settings->Palette.Num() == 0)
    {
       return;
    }

    bool bDirty = false;

    // Seed the struct's own root header, same rules as any category path.
    const FString structKey = scriptStruct->GetName();
    if (!settings->CategoryAssignments.Contains(structKey))
    {
       const int32 structIndex = FMath::Abs((int32)GetTypeHash(structKey)) % settings->Palette.Num();
       settings->CategoryAssignments.Add(structKey, settings->Palette[structIndex].Name);
       bDirty = true;
    }

    for (TFieldIterator<FProperty> propertyIt(scriptStruct); propertyIt; ++propertyIt)
    {
       const FProperty* property = *propertyIt;
       if (!property)
       {
          continue;
       }

       FString categoryPath;
       if (!BuildCategoryPath(property, scriptStruct, categoryPath))
       {
          continue; // uncategorized property, nothing to seed.
       }

       // Only seed paths that have never been touched. Contains() (rather than
       // ResolveCategoryColor) is deliberate: it also skips paths whose assigned
       // palette name was later deleted, so a stale assignment stays visible in
       // settings instead of being silently reseeded to a different color.
       if (settings->CategoryAssignments.Contains(categoryPath))
       {
          continue;
       }

       // Hash the path so sibling categories land on different palette entries
       // instead of every new category defaulting to the first one.
       const int32 index = FMath::Abs((int32)GetTypeHash(categoryPath)) % settings->Palette.Num();
       settings->CategoryAssignments.Add(categoryPath, settings->Palette[index].Name);
       bDirty = true;
    }

    if (bDirty)
    {
       settings->SaveConfig();
    }
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
       // NOTE: Removing forced opt-in may cause engine editor side effects. For example, collision presets
       // in the details panel becomes invisible.
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

       // Give every category path in this struct a palette color up front, so
       // newly added categories render styled on first open instead of falling
       // back to the stock header until someone right-clicks them.
       SeedCategoryAssignments(scriptStruct);
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