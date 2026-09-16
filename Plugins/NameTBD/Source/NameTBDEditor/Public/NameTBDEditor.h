#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class IAssetTypeActions; // Forward declaration

class FNameTBDEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// Keeps track of our registered asset actions for safe cleanup
	TArray<TSharedPtr<IAssetTypeActions>> RegisteredAssetTypeActions;
};