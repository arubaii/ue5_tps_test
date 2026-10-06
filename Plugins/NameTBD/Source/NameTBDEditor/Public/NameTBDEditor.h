#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class IAssetTypeActions; 
class FWidgetShapePathEditorFactory;
struct FGraphPanelPinFactory;

class FNameTBDEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// Keeps track of our registered asset actions for safe cleanup
	TArray<TSharedPtr<IAssetTypeActions>> RegisteredAssetTypeActions;

	// Draws the Set Widget Animation Value node's Animation and Value Name pins as dropdowns.
	TSharedPtr<FGraphPanelPinFactory> ValuePinFactory;

	// Puts the Custom Shape path editor on the UMG designer's canvas.
	TSharedPtr<FWidgetShapePathEditorFactory> ShapePathEditorFactory;
};