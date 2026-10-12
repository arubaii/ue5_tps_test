// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "AssetRegistry/AssetData.h"
#include "Widgets/SCompoundWidget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"
#include "WidgetAnimMachineJump.h"

#include "SWidgetAnimStateMachineEditor.generated.h"

class FAssetEditorToolkit;
class FLayoutExtender;
class FWidgetAnimMachinePreviewHost;
class FWidgetAnimMachinePickInput;
class FWidgetBlueprintApplicationMode;
class FWorkflowAllowedTabSet;
class IDetailsView;
class IInputProcessor;
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

	/** Backs Parameters From Inputs: the parameters of the state's animation (or samples), and the machine's inputs. */
	UFUNCTION()
	TArray<FString> GetStateParameterOptions() const;

	UFUNCTION()
	TArray<FString> GetInputNameOptions() const;

	/** Backs Blend Input: the machine's Float, Integer, Press And Hold and Vector 2D inputs. */
	UFUNCTION()
	TArray<FString> GetBlendInputOptions() const;

	/** Backs an action's Input. */
	UFUNCTION()
	TArray<FString> GetSettableInputOptions() const;
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

	/** Backs a condition's Layer: every layer but the transition's own. */
	UFUNCTION()
	TArray<FString> GetConditionLayerOptions() const;

	/** Backs a condition's State: every state of every other layer. */
	UFUNCTION()
	TArray<FString> GetConditionStateOptions() const;

	/** Backs an On Taken action's Input. */
	UFUNCTION()
	TArray<FString> GetSettableInputOptions() const;
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

	/**
	 * Backs the layer's Affected Widgets: (This Widget), then the widgets of the widget the machine is previewed on, as paths -
	 * or, previewed on none, the machine's targets.
	 */
	UFUNCTION()
	TArray<FString> GetMaskTargetOptions() const;

	/** Backs the layer's Widget Input Targets: as GetMaskTargetOptions. */
	UFUNCTION()
	TArray<FString> GetWidgetInputTargetOptions() const;

	/** The widgets of the widget the editor previews the machine on, as paths, (this widget) first. Set by the editor. */
	TFunction<TArray<FString>()> GetPreviewWidgetOptions;

	/** Whether the editor previews the machine on a widget - what the layer's "No widget picked" notes are shown for. Set by the editor. */
	TFunction<bool()> HasPreviewWidget;

private:

	/** GetMaskTargetOptions and GetWidgetInputTargetOptions. */
	TArray<FString> GetLayerWidgetOptions() const;
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

	/** The mouse wheel over the graph, before the graph editor sees it: zooms in past 1:1, to MaxGraphZoom. True if taken. */
	bool HandleGraphWheel(const FPointerEvent& WheelEvent);

	/** How far the wheel zooms the graph in, without Ctrl: a little past 1:1. */
	static constexpr float MaxGraphZoom = 1.5f;

	/**
	 * A double-click over the graph, before the graph editor sees it: on a transition's wire - not on a node - it puts a reroute
	 * point there, as a double-click on a Blueprint wire adds a reroute node. True if taken.
	 */
	bool HandleGraphDoubleClick(const FPointerEvent& MouseEvent);

	/**
	 * R pressed (or let go) while a wire is dragged out of a state over the graph, before the graph editor sees it: pressed, a
	 * reroute point is dropped under the mouse and the wire runs on from it - the transition the drag makes is routed through
	 * it (see UWidgetAnimMachineGraph::PendingDragReroutes). Taken either way while such a drag is on, so the graph editor never
	 * makes a reroute knot of its own, unjoined to anything. True if taken.
	 */
	bool HandleGraphRerouteKey(const FKeyEvent& KeyEvent, bool bPressed);

	/** Whether a wire is being dragged over the shown graph - see HandleGraphRerouteKey. */
	bool IsDraggingWireOverGraph() const;

	/** The shown graph's dropped reroute points kept while their drag is on, and placed on screen for its preview. */
	void UpdatePendingDragReroutes();

	/**
	 * The asset's own window's toolbar, at the window's top: its Target Widget Blueprint, play / pause and restart for the
	 * preview, the widget watched in Play In Editor, and Find. See FWidgetAnimStateMachineAssetEditor.
	 */
	TSharedRef<SWidget> MakeStandaloneToolbar();

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

	/**
	 * The asset's own window - no Widget Blueprint around it: no bar over the graph (its toolbar is the window's - see
	 * MakeStandaloneToolbar), the lock in the layer row, and the preview on the Target Widget Blueprint's own widget.
	 */
	bool bStandalone = false;

	/** The asset's own window: the Widget Blueprint the machine is tried on (FWidgetAnimStateMachineAsset::PreviewWidgetBlueprint). */
	TWeakObjectPtr<UWidgetBlueprint> TargetWidgetBlueprint;

	/** The asset's own window: a design-time widget of the Target Widget Blueprint, shown in the Preview section and played on. */
	TObjectPtr<UUserWidget> StandalonePreview = nullptr;
	TSharedPtr<SBox> StandalonePreviewBox;

	/** The Widget Blueprint whose timelines are listed to drag in: the one the editor is in, else the Target Widget Blueprint. */
	UWidgetBlueprint* GetTimelineBlueprint() const;

	/** Whether there is a widget to preview on: a designer, or the asset's own window's Target Widget Blueprint. */
	bool HasPreviewBlueprint() const;

	void SetTargetWidgetBlueprint(UWidgetBlueprint* Blueprint);

	/** The Target Widget Blueprint's preview widget, made (again, after a compile) when it isn't the class's own. */
	void EnsureStandalonePreview();
	void DropStandalonePreview();

	/** The lock (see bMachineLocked): on the bar, or - in the asset's own window - at the right of the layer row. */
	TSharedRef<SWidget> MakeLockButton();

	/** Play In Editor: which widget playing the machine is watched. */
	TSharedRef<SWidget> MakeDebugPicker();
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

	/** Frames until the selection - collapsed states just made - is straightened (Q); 0: none waiting. */
	int32 PendingStraightenFrames = 0;

	/** The inputs the previews were started with: a change restarts them, so they have the new inputs. Unset: start them again. */
	TOptional<uint32> PreviewSignature;

	UWidgetAnimMachineGraph* GetOrMakeGraph(int32 LayerIndex);
	TSharedPtr<SGraphEditor> GetShownGraphEditor() const;
	UWidgetAnimMachineGraph* GetShownGraph() const;

	/** The sub-machine the shown graph has open - where new states, comments and pastes go. Invalid: the layer's own graph. */
	FGuid GetOpenMachine() const;
	void ShowLayer(int32 LayerIndex);
	void RebuildLayerTabs();
	void RebuildInputs();

	/** An input's row in the Inputs panel: its name put into editing, removed, or its right-click menu opened. */
	void RenameInputInPanel(FName Input);
	void RemoveInputFromPanel(FName Input);
	void OpenInputMenu(FName Input, const FPointerEvent& MouseEvent);

	/**
	 * A category's heading in the Inputs panel: click to fold it, double-click its name to rename the category. Returns the box
	 * its rows go in, hidden while it is folded (folding rebuilds nothing).
	 */
	TSharedRef<SVerticalBox> AddInputCategoryHeader(const TSharedRef<SVerticalBox>& Rows, const FString& Category);

	/** The Inputs panel's folded categories - the panel's own, never saved. */
	TSet<FString> CollapsedInputCategories;

	/**
	 * The Widget Inputs panel's sub-dropdowns (Events, Gamepad and each of its groups - "Gamepad/D-Pad") opened or closed by
	 * hand. One not in here is open when the machine reads any of its inputs.
	 */
	TMap<FName, bool> WidgetInputGroupsOpen;

	/** The left panel's folded sections (Inputs, Widget Inputs, Timelines, Animations, Preview), by heading - never saved. */
	TSet<FString> FoldedPanelSections;
	void ToggleFoldedPanelSection(const FString& Key);

	/**
	 * The Animations section: every Widget Animation asset in the project, by name (gathered from the asset registry, not
	 * loaded), the search typed over them, and the rows shown. Gathered again on the tick after the registry tells of an asset
	 * added, removed or renamed.
	 */
	TArray<FAssetData> AnimationAssets;
	FString AnimationAssetFilter;
	TSharedPtr<SVerticalBox> AnimationAssetRows;
	bool bAnimationAssetsDirty = true;
	FDelegateHandle AnimationAssetAddedHandle;
	FDelegateHandle AnimationAssetRemovedHandle;
	FDelegateHandle AnimationAssetRenamedHandle;
	void GatherAnimationAssets();
	void RefillAnimationAssetRows();
	void OnAnimationAssetsChanged();
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

	/**
	 * Animations dropped on the graph (or a timeline double-clicked in the Animations list, ScreenPosition unset - the middle of
	 * the graph): a state for each, side by side from where they were let go, named for it and playing it. One undo step.
	 */
	void AddStatesForAnimations(const TArray<UWidgetAnimPresetAsset*>& Animations, const TArray<FString>& Names, TOptional<FVector2D> ScreenPosition);

	/**
	 * What a state of this machine plays for Animation: an asset as it is; a timeline of this Widget Blueprint as it is, in its
	 * own machine (saved with it - its changes play at once; Save State Machine To saves it as an asset in the copy it makes);
	 * in a machine that is an asset, the timeline's saved asset, made or brought up to date now. Null when none could be made.
	 */
	UWidgetAnimPresetAsset* ResolveAnimationForMachine(UWidgetAnimPresetAsset* Animation);

	/** The Widget Blueprint's timelines as last listed in the Animations list - a change lists them again. */
	uint32 TimelineListSignature = 0;
	uint32 ComputeTimelineListSignature() const;

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

	/** A point on screen, in the shown graph. */
	FVector2D ScreenToGraph(const FVector2D& ScreenPosition) const;

	/** Whether a Widget Inputs row can be dragged onto the graph: the machine doesn't read it yet, and the layer has no state for it. */
	bool CanDragWidgetInput(FName Input) const;

	/** A widget input dropped on the graph from the Widget Inputs panel: the state for it, where it was let go, selected. */
	void AddWidgetInputState(FName Input, const FVector2D& ScreenPosition);

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

	/** The Vector 2D inputs' values the Inputs panel holds. */
	TMap<FName, FVector2D> VectorValues;

	/** The widget being watched in Play In Editor, picked in the Debug menu - or the first one found. */
	TWeakObjectPtr<UWidgetAnimationComponent> DebugComponent;
	bool bDebugPickedByHand = false;

	/**
	 * When the components in Play In Editor were last searched for one playing the machine, finding none. Searched again at
	 * most a few times a second: every node of the graph asks for the watched widget as it paints, and with thousands of
	 * animated widgets playing, a search on every one of those asks made the whole editor crawl.
	 */
	mutable double LastDebugSearchTime = -1.0;

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
	uint32 MakePreviewSignature() const;

	/** The watched machine's state of the layer, for the graph to light. */
	bool GetLayerDebug(int32 LayerIndex, struct FWidgetAnimMachineLayerDebug& OutDebug) const;

	void SetInputValue(FName Input, float Value);
	void SetVectorValue(FName Input, const FVector2D& Value);
	FVector2D GetShownVector(FName Input) const;
	void FireInput(FName Input);
	void SetBuiltInValue(FName Input, bool bValue);

	/** Fires the built-in Clicked on the preview - a pulse: nothing is kept to push again. */
	void FireBuiltIn(FName Input);
	float GetShownInputValue(FName Input) const;
	bool GetShownBuiltInValue(FName Input) const;

	/** How far through its Hold Time a Press And Hold is, 0 - 1, in the widget watched or previewed - for its Hold button. */
	float GetShownHoldProgress(FName Input) const;
	bool IsDebuggingPlay() const;

	// -------------------------------------------------------------------------------------------------
	// Preview Targets
	// -------------------------------------------------------------------------------------------------

	/** The designer's preview of the Widget Blueprint: what Preview Targets are paths into. Null outside a Widget Blueprint. */
	UUserWidget* GetDesignerPreview() const;

	/** Every widget a target can be, as paths - (this widget) first, for the widget the machine plays on, stored as None. */
	TArray<FString> GetTargetOptions() const;

	/** The option that names Widget, or empty. (this widget) for the designer preview itself. */
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
	TSharedPtr<IInputProcessor> ZoomInput;

	/** Keeps the editor drawing every frame while something in the shown graph moves - see IsGraphAnimating. */
	TSharedPtr<FActiveTimerHandle> AnimateTimer;
	bool IsGraphAnimating() const;

	/** The designer preview paused from the toolbar: the machines hold where they are. */
	bool bPreviewPaused = false;
	bool IsPreviewPaused() const;
	EActiveTimerReturnType OnAnimateGraph(double InCurrentTime, float InDeltaTime);
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

	/**
	 * The layer and state the watched widget is in: the shown layer's, if it is in one, else the first layer's that is. False
	 * when nothing is watched.
	 */
	bool FindActiveState(int32& OutLayer, FGuid& OutState) const;

	/** "(Hovered)" - the state in play, after the button's "Jump to Active State"; empty when nothing is in play. */
	FText GetJumpToActiveStateName() const;

	/** Shows the active state's layer and brings its state into view. */
	void JumpToActiveState();

	/**
	 * Jump To State: plans the preview inputs that take a layer to State (see WidgetAnimMachineJump.h) and sets them a step at a
	 * time - each step once the layer has arrived where the last one took it - so every transition on the way plays. With
	 * nothing watched to follow, the last step's inputs are set at once. State may be the layer's Exit (its way out, as it
	 * closes) or its Entry, which starts the preview again from the start (RestartFromEntry).
	 */
	void JumpToState(int32 LayerIndex, const FGuid& State);

	/** Every preview input back to its default, and the designer preview started again from Entry. Not in Play In Editor. */
	void RestartFromEntry();

	/** Lets go of the Press And Hold inputs a jump step pressed for a moment (FInputPlan::Tap), once the machine has seen the press. */
	void TickPendingLetGo();

	/** Sets the preview's inputs to a jump step's. */
	void ApplyJumpPlan(const WidgetAnimMachineJump::FInputPlan& Plan);

	/** Moves a jump in progress on to its next step once the layer has arrived - or given up waiting. */
	void TickJump();

	TArray<WidgetAnimMachineJump::FStep> JumpSteps;
	int32 JumpStep = INDEX_NONE;
	int32 JumpLayer = INDEX_NONE;
	double JumpStepTime = 0.0;

	/** Press And Hold inputs a jump step pressed, to let go of shortly after - and when they were pressed. */
	TArray<FName> PendingLetGo;
	double PendingLetGoTime = 0.0;
	/** A new machine: in the Default Folder, or - bChooseLocation - wherever the editor's Save Asset As window says. */
	void CreateNewMachine(bool bChooseLocation);

	// The Widget Blueprint's own machine (UWidgetAnimTimelinesExtension::StateMachine) - the State Machine mode, as Animation
	// Timeline is the Widget Animations panel's - or an asset (Edit Existing Asset).

	/** Whether Edited is a Widget Blueprint's own machine rather than an asset. */
	static bool IsDraftMachine(const UWidgetAnimStateMachineAsset* Edited);

	/** The Widget Blueprint's own machine - made (with one layer and an Idle state) the first time it is asked for. */
	UWidgetAnimStateMachineAsset* GetOrMakeDraftMachine();

	/** State Machine mode: the Widget Blueprint's own machine. Edit Existing Asset: the asset edited last, or the one it plays. */
	void SwitchToDraft();
	void SwitchToAsset();

	/** The asset the Widget Blueprint's machine was last saved as, or null. */
	UWidgetAnimStateMachineAsset* GetDraftSavedAsset() const;

	/** Save State Machine To: a new asset of the Widget Blueprint's machine, in the Default Folder or (bChooseLocation) where picked. */
	void SaveDraftTo(bool bChooseLocation);

	/** Save, in State Machine mode: the Widget Blueprint's machine into the asset it was saved as, and that asset saved. */
	void SaveDraftToSavedAsset();

	/** Jump To Active State, beside Add State: back to the state in play when the graph has been scrolled away from it. */
	TSharedRef<SWidget> MakeJumpToActiveButton();

	// -------------------------------------------------------------------------------------------------
	// Find (Ctrl + F)
	// -------------------------------------------------------------------------------------------------

	/** A state or transition Find turned up: in which layer, its id, and how the list shows it. */
	struct FFindHit
	{
		int32 Layer = INDEX_NONE;
		FGuid Id;
		FText Title;
		FText Where;
		bool bTransition = false;
		bool bSubMachine = false;
	};

	/**
	 * Find, over the graph's top right: a search box and the states and transitions of every layer and sub-machine whose name -
	 * or, for a transition, its route or conditions - has what is typed. Enter or a click goes to the one picked (FocusOn, opening
	 * its layer and sub-machine, and selects it); Up and Down pick; Escape closes it.
	 */
	TSharedRef<SWidget> MakeFindPanel();
	void OpenFind();
	void CloseFind();
	void RefreshFind();
	void GoToFindHit(int32 HitIndex);
	bool bFindOpen = false;
	FString FindText;
	int32 FindPicked = 0;
	TArray<FFindHit> FindHits;
	TSharedPtr<class SSearchBox> FindBox;
	TSharedPtr<SVerticalBox> FindResults;

	/** Play / pause the preview, floating over the graph at its bottom left. */
	TSharedRef<SWidget> MakePlayPauseButton();

	/** State Machine mode: the Widget Blueprint's own machine's name, as a field on the bar - double-click to rename it. */
	TSharedRef<SWidget> MakeDraftNameField();

	/**
	 * In the layer row, while a sub-machine or collapsed states are open: the layer (the way back to all of it), each sub-machine
	 * in to the one open, then the collapsed states. Its steps are made again when that changes - see RefreshBreadcrumb.
	 */
	TSharedRef<SWidget> MakeGroupBreadcrumb();
	void RefreshBreadcrumb();
	TSharedPtr<SBox> BreadcrumbBox;
	uint32 BreadcrumbSignature = 0;

	/**
	 * Brings a state or a transition of a layer into view - showing the layer, opening the sub-machine it is in - and, bSelect,
	 * selects it (its Details shown). The graph may need making again first: done once it is (TickPendingFocus).
	 */
	void FocusOn(int32 LayerIndex, const FGuid& Id, bool bSelect);
	void TickPendingFocus();
	FGuid PendingFocusId;
	bool bPendingFocusSelect = false;
	int32 PendingFocusFrames = 0;

	/** The lock: the machine read-only - a preview - as the Widget Animations panel's lock. See IsMachineEditable. */
	bool bMachineLocked = false;

	/** Whether the machine can be edited here: one is open and the lock is off. */
	bool IsMachineEditable() const { return Machine != nullptr && !bMachineLocked; }

	/** The asset edited last in Edit Existing Asset, for switching back to it. */
	TWeakObjectPtr<UWidgetAnimStateMachineAsset> LastAssetMachine;

	/** Where New puts a machine without asking: the project's Default Save Folder, else beside the Widget Blueprint. */
	FString GetDefaultMachineFolder() const;
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

	/** Docks the tab again, quietly, if it was docked last time in the mode this editor is in - see WidgetAnimDockPreference. */
	void RestoreDock(const TSharedRef<FAssetEditorToolkit>& Toolkit);

	/** Lets go of every drawer's editor - from the module's shutdown, before the machine asset's statics are gone. */
	void Shutdown();
}
