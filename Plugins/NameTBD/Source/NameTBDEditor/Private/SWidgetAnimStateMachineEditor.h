// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"

#include "SWidgetAnimStateMachineEditor.generated.h"

class FAssetEditorToolkit;
class FLayoutExtender;
class FWidgetBlueprintApplicationMode;
class FWorkflowAllowedTabSet;
class IDetailsView;
class SHorizontalBox;
class SBox;
class SGraphEditor;
class SVerticalBox;
class FUICommandList;
class UWidgetAnimationComponent;
class UWidgetAnimMachineGraph;
class UWidgetBlueprint;

/**
 * One state of a machine, as the editor's Details show it: a copy, written back into the machine on every edit - which is
 * what lets the Details edit a state held in an array of structs, and undo restore it.
 */
UCLASS(Transient)
class UWidgetAnimMachineStateProxy : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "State", meta = (ShowOnlyInnerProperties))
	FWidgetAnimMachineState State;

	TWeakObjectPtr<UWidgetAnimStateMachineAsset> Machine;
	int32 LayerIndex = INDEX_NONE;
	FGuid StateId;

	/** Copies the state in from the machine. False when it is gone. */
	bool Load();

	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	UFUNCTION()
	TArray<FString> GetNumberInputOptions() const;
};

/** One transition of a machine, as the editor's Details show it. See UWidgetAnimMachineStateProxy. */
UCLASS(Transient)
class UWidgetAnimMachineTransitionProxy : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Transition", meta = (ShowOnlyInnerProperties))
	FWidgetAnimMachineTransition Transition;

	TWeakObjectPtr<UWidgetAnimStateMachineAsset> Machine;
	int32 LayerIndex = INDEX_NONE;
	FGuid TransitionId;

	bool Load();

	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	UFUNCTION()
	TArray<FString> GetInputNameOptions() const;
};

/**
 * The Widget Animation State Machine editor: the machine's layers as graphs, its inputs - live, driving the designer's preview
 * - and the Details of whatever is picked. The status bar drawer and tab of every Widget Blueprint editor, and the editor a
 * WASM_ asset opens in from the Content Browser.
 *
 * Inside a Widget Blueprint it previews on the widgets of that Widget Blueprint's designer: every Widget Animation Component
 * there that plays the machine (only the selected widget's, when one with the machine is selected) plays it in the designer,
 * driven by the Inputs panel. In Play In Editor it watches a widget playing the machine - the graph lights the state it is in
 * and the transition it is taking - and its inputs can be set from here.
 */
class SWidgetAnimStateMachineEditor : public SCompoundWidget, public FGCObject
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimStateMachineEditor)
		: _InDrawer(false)
	{}
		/** The Widget Blueprint whose designer the machine previews in. None for the asset's own editor. */
		SLATE_ARGUMENT(TWeakObjectPtr<UWidgetBlueprint>, WidgetBlueprint)

		/** The machine to start on. Inside a Widget Blueprint, picked from its widgets when none is given. */
		SLATE_ARGUMENT(TWeakObjectPtr<UWidgetAnimStateMachineAsset>, Machine)

		/** Shown in a status bar drawer: offers Dock in Layout. */
		SLATE_ARGUMENT(bool, InDrawer)

		SLATE_EVENT(FSimpleDelegate, OnDockInLayout)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SWidgetAnimStateMachineEditor() override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SWidgetAnimStateMachineEditor"); }

	/** Edits Machine from now on. */
	void SetMachine(UWidgetAnimStateMachineAsset* Machine);
	UWidgetAnimStateMachineAsset* GetMachine() const { return Machine; }

private:

	// -------------------------------------------------------------------------------------------------
	// What is edited
	// -------------------------------------------------------------------------------------------------

	TObjectPtr<UWidgetAnimStateMachineAsset> Machine = nullptr;
	TWeakObjectPtr<UWidgetBlueprint> WidgetBlueprint;
	bool bInDrawer = false;
	FSimpleDelegate OnDockInLayout;

	/** One graph per layer, made as the layer is first shown. */
	TArray<TObjectPtr<UWidgetAnimMachineGraph>> Graphs;
	TArray<TSharedPtr<SGraphEditor>> GraphEditors;
	int32 ShownLayer = 0;

	TObjectPtr<UWidgetAnimMachineStateProxy> StateProxy = nullptr;
	TObjectPtr<UWidgetAnimMachineTransitionProxy> TransitionProxy = nullptr;

	TSharedPtr<FUICommandList> GraphCommands;
	TSharedPtr<SBox> GraphHost;
	TSharedPtr<SVerticalBox> InputsBox;
	TSharedPtr<SHorizontalBox> LayerTabs;
	TSharedPtr<IDetailsView> DetailsView;

	FDelegateHandle MachineChangedHandle;

	/** Whether the inputs or layers changed since the panels were built - rebuilt on the next tick. */
	bool bPanelsDirty = true;

	/** Whether layers were added or removed since the graphs were laid out - followed on the next tick. */
	bool bLayersChanged = false;

	/** Set while a rebuilt graph's selection is put back: the Details aren't changed for it. */
	bool bRestoringSelection = false;

	/** The inputs the previews were started with: a change restarts them, so they have the new inputs. */
	FString PreviewSignature;

	UWidgetAnimMachineGraph* GetOrMakeGraph(int32 LayerIndex);
	void ShowLayer(int32 LayerIndex);
	void RebuildLayerTabs();
	void RebuildInputs();
	void OnMachineChanged(UWidgetAnimStateMachineAsset* ChangedMachine);

	// -------------------------------------------------------------------------------------------------
	// Graph
	// -------------------------------------------------------------------------------------------------

	TSharedRef<SGraphEditor> MakeGraphEditor(UWidgetAnimMachineGraph* Graph);
	void OnSelectionChanged(const TSet<UObject*>& Selection);
	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* Node);
	void DeleteSelected();
	bool CanDeleteSelected() const;
	void RenameSelected();
	bool CanRenameSelected() const;
	void OpenState(const FGuid& StateId);

	/** Positions dragged in the graph, written into the machine once the mouse is let go. */
	void CommitMovedNodes();

	// -------------------------------------------------------------------------------------------------
	// Preview and debugging
	// -------------------------------------------------------------------------------------------------

	/** Whether the designer plays the machine, driven by the Inputs panel. */
	bool bPreviewInDesigner = true;

	/** The values the Inputs panel holds, pushed to every preview - and to the watched widget in Play In Editor. */
	TMap<FName, float> InputValues;
	TMap<FName, bool> BuiltInValues;

	/** The widget being watched in Play In Editor, picked in the Debug menu - or the first one found. */
	TWeakObjectPtr<UWidgetAnimationComponent> DebugComponent;
	bool bDebugPickedByHand = false;

	/** The designer components previewing the machine. */
	TArray<TWeakObjectPtr<UWidgetAnimationComponent>> PreviewComponents;
	double LastPreviewSearch = -1.0;

	void FindPreviewComponents(TArray<UWidgetAnimationComponent*>& OutComponents) const;
	void FindDebugCandidates(TArray<UWidgetAnimationComponent*>& OutComponents) const;
	UWidgetAnimationComponent* GetWatchedComponent() const;
	void UpdatePreviews();
	void StopPreviews();
	void PushInputs(UWidgetAnimationComponent* Component) const;
	FString MakePreviewSignature() const;

	/** The watched component's state of the layer, for the graph to light. */
	bool GetLayerDebug(int32 LayerIndex, struct FWidgetAnimMachineLayerDebug& OutDebug) const;

	void SetInputValue(FName Input, float Value);
	void FireInput(FName Input);
	void SetBuiltInValue(FName Input, bool bValue);
	float GetShownInputValue(FName Input) const;
	bool GetShownBuiltInValue(FName Input) const;
	bool IsDebuggingPlay() const;

	// -------------------------------------------------------------------------------------------------
	// Toolbar
	// -------------------------------------------------------------------------------------------------

	TSharedRef<SWidget> MakeToolbar();
	TSharedRef<SWidget> MakeMachineMenu();
	TSharedRef<SWidget> MakeDebugMenu();
	FText GetPreviewStatus() const;
	FText GetDebugLabel() const;
	void CreateNewMachine();
	void SaveMachine();

	/** A machine to start on: one the selected widget plays, else one any widget of the Widget Blueprint plays. */
	UWidgetAnimStateMachineAsset* FindMachineInWidgetBlueprint() const;
};

/** The State Machine editor in Widget Blueprint editors: its drawer and its tab. Called from FWidgetAnimBuilderToolbar. */
namespace WidgetAnimStateMachineEditorHooks
{
	/** Adds the tab to a Widget Blueprint editor mode's tabs, and its closed slot to the layout. */
	void RegisterTabFactory(const FWidgetBlueprintApplicationMode& Mode, FWorkflowAllowedTabSet& TabFactories);
	void ExtendLayout(FLayoutExtender& Extender);

	/** Puts the Widget Animation State Machine drawer on this editor's status bar, and makes sure its tab can be opened. */
	void PlaceDrawer(const TSharedRef<FAssetEditorToolkit>& Toolkit, int32 Slot);

	/** Lets go of every drawer's editor - from the module's shutdown, before the machine asset's statics are gone. */
	void Shutdown();
}
