// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Editor-only module for tps_test.
 *
 * Its job is to give every USTRUCT declared in the tps_test runtime module real
 * collapsible groups in the details panel, built from the Category metadata already
 * written on the struct's members.
 *
 * WHY: Category = "Jitter" groups properties correctly on a UCLASS, but Unreal ignores it
 * for a USTRUCT displayed NESTED (Blueprint variable, component property, DataTable row)
 * and renders one flat list instead (see UE-32252).
 *
 * ZERO MAINTENANCE: registration is automatic. Declare a struct in the tps_test module,
 * put Category = "..." on its members, and the groups appear. You never edit this file.
 * Structs whose members carry no categories are unaffected - the customization produces
 * the same flat layout the engine would have.
 */
class FTpsTestEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Register the category-grouping customization for every UScriptStruct in the runtime module. */
	void RegisterAllRuntimeStructs();

	/** Struct type names registered by this module, unregistered on shutdown. */
	TArray<FName> m_RegisteredStructNames;
};