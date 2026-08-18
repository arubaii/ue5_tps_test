// Copyright arubaii. All rights reserved.

#include "tps_testEditor.h"

#include "CategorizedStructCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_MODULE(FTpsTestEditorModule, tps_testEditor)

namespace
{
	/**
	 * Package that owns the runtime module's reflected types. Every USTRUCT declared in
	 * Source/tps_test lives here, which is what lets us enrol them all without naming any
	 * of them individually.
	 */
	const TCHAR* RuntimeModulePackageName = TEXT("/Script/tps_test");
}

void FTpsTestEditorModule::RegisterAllRuntimeStructs()
{
	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	const UPackage* RuntimePackage = FindPackage(nullptr, RuntimeModulePackageName);
	if (!RuntimePackage)
	{
		// The runtime module's package should always be loaded by PostEngineInit. If it is
		// not, registering nothing is the safe outcome: the editor falls back to the
		// engine's default flat layout rather than misbehaving.
		return;
	}

	for (TObjectIterator<UScriptStruct> StructIt; StructIt; ++StructIt)
	{
		UScriptStruct* ScriptStruct = *StructIt;
		if (!ScriptStruct || ScriptStruct->GetOutermost() != RuntimePackage)
		{
			continue;
		}

		// RegisterCustomPropertyTypeLayout keys on the struct's name WITHOUT the F prefix,
		// which is exactly what UScriptStruct::GetFName() returns.
		const FName StructName = ScriptStruct->GetFName();
		if (RegisteredStructNames.Contains(StructName))
		{
			continue;
		}

		PropertyModule.RegisterCustomPropertyTypeLayout(
			StructName,
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCategorizedStructCustomization::MakeInstance));

		RegisteredStructNames.Add(StructName);
	}

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FTpsTestEditorModule::StartupModule()
{
	// Every struct in the runtime module, present and future. Nothing to maintain here.
	RegisterAllRuntimeStructs();
}

void FTpsTestEditorModule::ShutdownModule()
{
	if (!FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		RegisteredStructNames.Reset();
		return;
	}

	FPropertyEditorModule& PropertyModule =
		FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	for (const FName& StructName : RegisteredStructNames)
	{
		PropertyModule.UnregisterCustomPropertyTypeLayout(StructName);
	}
	RegisteredStructNames.Reset();

	PropertyModule.NotifyCustomizationModuleChanged();
}
