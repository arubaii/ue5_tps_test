#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class IAssetTypeActions; 
class FWidgetShapePathEditorFactory;
class FWidgetAnimTextEditorFactory;
class FFlowListPathEditorFactory;
class FWidgetPathConnectFactory;
class FWidgetAnimMotionPathFactory;
class FWidgetGizmoFactory;
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

	// Puts the animated texts' tag editor on the UMG designer's canvas.
	TSharedPtr<FWidgetAnimTextEditorFactory> TextEditorFactory;

	// Puts the Flow List's Spline path editor on the UMG designer's canvas.
	TSharedPtr<FFlowListPathEditorFactory> FlowListPathEditorFactory;

	// Puts the Widget Path Layer's Connect / Disconnect toolbar over several selected widgets on the UMG designer's canvas.
	TSharedPtr<FWidgetPathConnectFactory> WidgetPathConnectFactory;

	// Draws the motion paths of the Widget Animations panel's animation on the UMG designer's canvas.
	TSharedPtr<FWidgetAnimMotionPathFactory> MotionPathFactory;

	// Puts the Widget Gizmo - move, scale, rotate, shear and the pivot of the selected widgets - on the UMG designer's canvas.
	TSharedPtr<FWidgetGizmoFactory> GizmoFactory;
};