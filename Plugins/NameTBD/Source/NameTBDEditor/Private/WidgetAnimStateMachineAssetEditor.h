// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "Factories/Factory.h"
#include "Toolkits/AssetEditorToolkit.h"

#include "WidgetAnimStateMachineAssetEditor.generated.h"

class SWidgetAnimStateMachineEditor;
class UWidgetAnimStateMachineAsset;

/**
 * Makes a Widget Animation State Machine asset - Content Browser > Add > User Interface > Widget Animation State Machine, or
 * New in the State Machine editor - with a Base layer and an Idle state that Entry goes to, ready for its first transition.
 */
UCLASS()
class UWidgetAnimStateMachineFactory : public UFactory
{
	GENERATED_BODY()

public:

	UWidgetAnimStateMachineFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
	virtual FString GetDefaultNewAssetName() const override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
};

/** How the Content Browser shows a WASM_: its name, its color, and the editor it opens in on a double-click. */
class FWidgetAnimStateMachineAssetTypeActions : public FAssetTypeActions_Base
{
public:

	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;
};

/**
 * A WASM_ asset's own editor window: the Widget Animation State Machine editor, on its own - the graph, the Inputs panel and
 * the Details, without a designer to preview in. Open a Widget Blueprint that plays the machine to see it move (its status
 * bar's Widget Animation State Machine drawer is the same editor, previewing in that designer).
 */
class FWidgetAnimStateMachineAssetEditor : public FAssetEditorToolkit
{
public:

	void InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWidgetAnimStateMachineAsset* Machine);

	// FAssetEditorToolkit
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

private:

	TSharedRef<SDockTab> SpawnMachineTab(const FSpawnTabArgs& Args);

	TWeakObjectPtr<UWidgetAnimStateMachineAsset> EditedMachine;
	TSharedPtr<SWidgetAnimStateMachineEditor> MachineEditor;
};
