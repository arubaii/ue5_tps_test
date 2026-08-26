// Copyright Bilcash 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"


class FStructCategoryGroupsEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Scans all loaded UScriptStructs and registers the customization for any opted-in struct. */
	void RegisterOptedInStructs();
	/** Seeds palette assignments for every ColoredCategory path in a struct. */
	void SeedCategoryAssignments(const UScriptStruct* scriptStruct);
	
	/** Struct names currently registered, so ShutdownModule can cleanly unregister them. */
	TArray<FName> m_RegisteredStructNames;
};