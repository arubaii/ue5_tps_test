// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"

#include "SWidgetAnimStateMachineEditor.generated.h"

class FAssetEditorToolkit;
class FLayoutExtender;
class FWidgetAnimMachinePreviewHost;
class FWidgetAnimMachinePickInput;
class FWidgetBlueprintApplicationMode;
class FWorkflowAllowedTabSet;
class IDetailsView;
class SHorizontalBox;
class SBox;
class SGraphEditor;
class SInlineEditableTextBlock;
class SVerticalBox;
class SWindow;
class FUICommandList;
class UEdGraphNode;
class UWidget;
class UWidgetAnimationComponent;
class UWidgetAnimMachineGraph;
class UWidgetAnimMachineGraphNode;
class UUserWidget;
class UWidgetBlueprint;
struct FWidgetAnimMachineInstance;

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

	/**
	 * Copies the transition in from the machine. False when it is gone. While the transition animation plays at its own length,
	 * Animation Duration is shown as that length - so ticking it to set one starts from it.
	 */
	bool Load();

	/** Animation Duration, while not set by hand, as the length the transition animation plays at. */
	void SyncShownDuration();

	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	UFUNCTION()
	TArray<FString> GetInputNameOptions() const;
};

/** One input of a machine, as the editor's Details show it - what clicking it in the Inputs panel shows. See UWidgetAnimMachineStateProxy. */
UCLASS(Transient)
class UWidgetAnimMachineInputProxy : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Input", meta = (ShowOnlyInnerProperties))
	FWidgetAnimMachineInput Input;

	TWeakObjectPtr<UWidgetAnimStateMachineAsset> Machine;

	/** The input it shows, by name - followed when it is renamed. */
	FName InputName;

	bool Load();

	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	UFUNCTION()
	TArray<FString> GetTargetNameOptions() const;
};

/** One layer of a machine, as the editor's Details show it - what clicking its tab shows. See UWidgetAnimMachineStateProxy. */
UCLASS(Transient)
class UWidgetAnimMachineLayerProxy : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Layer", meta = (ShowOnlyInnerProperties))
	FWidgetAnimMachineLayer Layer;

	TWeakObjectPtr<UWidgetAnimStateMachineAsset> Machine;
	int32 LayerIndex = INDEX_NONE;

	bool Load();

	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	UFUNCTION()
	TArray<FString> GetNumberInputOptions() const;
};

/**
 * The Widget Animation State Machine editor: the machine's layers as graphs, its inputs - live, driving the designer's preview
 * - and the Details of whatever is picked. The status bar drawer and tab of every Widget Blueprint editor, and the editor a
 * WASM_ asset opens in from the Content Browser.
 *
 * Inside a Widget Blueprint it previews in that Widget Blueprint's designer, one of two ways (the Preview section): on Preview
 * Targets - a widget for the machine to play on and one for each of its targets, picked here, with no Widget Animation
 * Component needed - or on every Widget Animation Component there that plays the machine (only the selected widget's, when one
 * with the machine is selected). Either way the Inputs panel drives it. In Play In Editor it watches a widget playing the
 * machine - the graph lights the state it is in and the transition it is taking - and its inputs can be set from here.
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
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SWidgetAnimStateMachineEditor"); }

	/** Edits Machine from now on. */
	void SetMachine(UWidgetAnimStateMachineAsset* Machine);
	UWidgetAnimStateMachineAsset* GetMachine() const { return Machine; }

	// Picking a preview target in the designer - see FWidgetAnimMachinePickInput.
	bool IsPickingTarget() const { return PickingTarget.IsSet(); }
	void UpdatePickHover(const FVector2D& ScreenPosition);
	bool CommitPickHover();
	void CancelTargetPick();

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
	TObjectPtr<UWidgetAnimMachineInputProxy> InputProxy = nullptr;
	TObjectPtr<UWidgetAnimMachineLayerProxy> LayerProxy = nullptr;

	TSharedPtr<FUICommandList> GraphCommands;
	TSharedPtr<SBox> GraphHost;
	TSharedPtr<SVerticalBox> InputsBox;
	TSharedPtr<SVerticalBox> PreviewBox;
	TSharedPtr<SHorizontalBox> LayerTabs;
	TSharedPtr<IDetailsView> DetailsView;

	/** The name text of each input row and layer tab, for a double-click to rename. */
	TMap<FName, TSharedPtr<SInlineEditableTextBlock>> InputNameTexts;
	TArray<TSharedPtr<SInlineEditableTextBlock>> LayerNameTexts;

	FDelegateHandle MachineChangedHandle;

	/** Whether the inputs or layers changed since the panels were built - rebuilt on the next tick. */
	bool bPanelsDirty = true;

	/** Whether layers were added or removed since the graphs were laid out - followed on the next tick. */
	bool bLayersChanged = false;

	/** Set while a rebuilt graph's selection is put back: the Details aren't changed for it. */
	bool bRestoringSelection = false;

	/** What to select once the shown graph is next rebuilt - what was just pasted, duplicated or added. */
	TArray<FGuid> PendingSelection;

	/** The inputs the previews were started with: a change restarts them, so they have the new inputs. */
	FString PreviewSignature;

	UWidgetAnimMachineGraph* GetOrMakeGraph(int32 LayerIndex);
	TSharedPtr<SGraphEditor> GetShownGraphEditor() const;
	UWidgetAnimMachineGraph* GetShownGraph() const;
	void ShowLayer(int32 LayerIndex);
	void RebuildLayerTabs();
	void RebuildInputs();
	void RebuildPreviewPanel();
	void OnMachineChanged(UWidgetAnimStateMachineAsset* ChangedMachine);

	// -------------------------------------------------------------------------------------------------
	// Details
	// -------------------------------------------------------------------------------------------------

	/** Shows the shown layer's settings - what the Details show when nothing in particular is picked. */
	void ShowLayerDetails();
	void ShowInputDetails(FName Input);
	bool IsShowingInput(FName Input) const;
	bool IsShowingLayer(int32 LayerIndex) const;
	void RegisterDetailLayouts();

	// -------------------------------------------------------------------------------------------------
	// Graph
	// -------------------------------------------------------------------------------------------------

	TSharedRef<SGraphEditor> MakeGraphEditor(UWidgetAnimMachineGraph* Graph);
	void MapGraphCommands();
	void OnSelectionChanged(const TSet<UObject*>& Selection);
	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* Node);
	void DeleteSelected();
	bool CanDeleteSelected() const;
	void RenameSelected();
	bool CanRenameSelected() const;
	void OpenState(const FGuid& StateId);

	void CopySelected();
	bool CanCopySelected() const;
	void CutSelected();
	void PasteHere();
	bool CanPaste() const;
	void DuplicateSelected();
	void AddCommentAroundSelection();
	void SetSelectedTransitionsEnabled(bool bEnabled);
	bool HasSelectedTransitions() const;

	/** The selected states' and comment boxes' ids. */
	void GetSelectedIds(TArray<FGuid>& OutStates, TArray<FGuid>& OutTransitions, TArray<FGuid>& OutComments) const;

	/** Where a new state or a paste goes when nothing says where: the middle of the graph's view. */
	FVector2D GetViewCenter() const;

	/** Adds a state - a preset's, for a built-in input, or a plain one for None - in the middle of the view, and selects it. */
	void AddStateAtViewCenter(FName Preset);

	/** Q: the selected states lined up with the states they connect to - never onto another state. */
	void StraightenSelected();

	/** Keys handled before the graph editor's own bindings - see MapGraphCommands. */
	TSharedPtr<FUICommandList> PreviewCommands;

	/** Positions dragged in the graph and comment boxes changed in place, written into the machine once the mouse is let go. */
	void CommitGraphEdits();

	// -------------------------------------------------------------------------------------------------
	// Preview and debugging
	// -------------------------------------------------------------------------------------------------

	/** Where the designer preview plays: on the Preview Targets picked here, or on the widgets whose components play the machine. */
	enum class EPreviewSource : uint8 { Targets, Components };
	EPreviewSource PreviewSource = EPreviewSource::Targets;

	/** Whether the source was picked - by hand, or once from what the designer has. Until then, the first search picks it. */
	bool bPreviewSourceChosen = false;

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

	/** The machine playing on the Preview Targets, with no component - see FWidgetAnimMachinePreviewHost. */
	TSharedPtr<FWidgetAnimMachinePreviewHost> PreviewHost;

	/** The designer's preview the Preview Targets are paths into. */
	TWeakObjectPtr<UUserWidget> PreviewRoot;

	void FindPreviewComponents(TArray<UWidgetAnimationComponent*>& OutComponents) const;
	void FindDebugCandidates(TArray<UWidgetAnimationComponent*>& OutComponents) const;
	UWidgetAnimationComponent* GetWatchedComponent() const;

	/** The machine running where the editor looks: the widget watched in Play In Editor, or the designer preview. */
	const FWidgetAnimMachineInstance* GetWatchedInstance() const;
	void UpdatePreviews();
	void StopPreviews();
	void StopComponentPreviews();
	void PushInputs(UWidgetAnimationComponent* Component) const;
	void PushInputsToHost() const;
	FString MakePreviewSignature() const;

	/** The watched machine's state of the layer, for the graph to light. */
	bool GetLayerDebug(int32 LayerIndex, struct FWidgetAnimMachineLayerDebug& OutDebug) const;

	void SetInputValue(FName Input, float Value);
	void FireInput(FName Input);
	void SetBuiltInValue(FName Input, bool bValue);
	float GetShownInputValue(FName Input) const;
	bool GetShownBuiltInValue(FName Input) const;
	bool IsDebuggingPlay() const;

	// -------------------------------------------------------------------------------------------------
	// Preview Targets
	// -------------------------------------------------------------------------------------------------

	/** The designer's preview of the Widget Blueprint: what Preview Targets are paths into. Null outside a Widget Blueprint. */
	UUserWidget* GetDesignerPreview() const;

	/** Every widget a target can be, as paths - "None" first, for the widget the machine plays on. */
	TArray<FString> GetTargetOptions() const;

	/** The path that names Widget, or empty. "None" for the designer preview itself. */
	FString FindTargetOptionFor(const UWidget* Widget) const;

	/** The widget the machine plays on in the preview, and each target's: picked here, or else where they start. */
	FName GetPreviewHostPath() const;
	FName GetPreviewTargetPath(FName Target) const;
	void SetPreviewHostPath(FName Path);
	void SetPreviewTargetPath(FName Target, FName Path);

	/** Which target is being picked by clicking the designer - HostPickKey for the widget it plays on. */
	TOptional<FName> PickingTarget;
	static const FName HostPickKey;
	TWeakObjectPtr<UWidget> PickHoverWidget;
	FString PickHoverLabel;
	TWeakObjectPtr<UWidget> PickStartSelection;
	bool bPickStartedWithSelection = false;
	TSharedPtr<FWidgetAnimMachinePickInput> PickInput;
	TSharedPtr<SWindow> PickLabelWindow;

	void BeginTargetPick(FName Target);
	void UpdateTargetPick();
	void ApplyPickedTarget(UWidget* Picked);
	UWidget* GetDesignerSelectedWidget() const;
	UWidget* FindPreviewWidgetAt(const FVector2D& ScreenPosition, FString& OutOption) const;
	TSharedRef<SWidget> MakeTargetRow(FName Target, const FText& Label, const FText& ToolTip);

	// -------------------------------------------------------------------------------------------------
	// Toolbar
	// -------------------------------------------------------------------------------------------------

	TSharedRef<SWidget> MakeToolbar();
	TSharedRef<SWidget> MakeDebugMenu();
	TSharedRef<SWidget> MakeAddStateMenu();
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
