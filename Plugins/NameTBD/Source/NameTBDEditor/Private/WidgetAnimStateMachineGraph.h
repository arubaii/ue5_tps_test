// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachineRuntime.h"

#include "WidgetAnimStateMachineGraph.generated.h"

class UWidgetAnimStateMachineAsset;
class UWidgetAnimMachineGraphNode;
class UWidgetAnimMachineTransitionNode;

// =====================================================================================================
// The Widget Animation State Machine editor's graph
// =====================================================================================================
//
// The machine asset is the truth: its layers' states and transitions, and where each state sits. A graph is only a view of
// one layer, rebuilt from the asset whenever it changes - so an edit made anywhere (the graph, the Details, an undo) shows
// everywhere, and nothing about the graph is ever saved. Every edit made in the graph is made to the asset, in a
// transaction, and the graph rebuilt from it (see WidgetAnimMachineEdits).
//
// It looks and behaves like an Animation Blueprint's state machine: states as rounded nodes, transitions as arrows with a
// rule icon in the middle; drag from a state's edge to another state to connect them, double-click a state to open its
// animation, right-click to add a state.

/** Every edit the editor makes to a machine: each in its own transaction, the machine normalized and told after. */
namespace WidgetAnimMachineEdits
{
	/** Adds a state to Layer at Position, with Animation, and returns its id. */
	FGuid AddState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, FName Name, const FVector2D& Position, UWidgetAnimPresetAsset* Animation = nullptr);

	/** Adds a transition between two nodes of Layer, set up for where it goes - a Cut from Entry, Closing Is True to Exit - and returns its id. Invalid when the two can't be joined. */
	FGuid AddTransition(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& From, const FGuid& To);

	/** Whether a transition can go from one node to the other: not into Entry or Any State, not out of Exit, not to itself. */
	bool CanConnect(const UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& From, const FGuid& To);

	/** Deletes states - never Entry, Any State or Exit - and transitions; transitions of a deleted state go too. */
	void Delete(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& States, const TArray<FGuid>& Transitions);

	/** Renames a state, made unique in its layer. */
	void RenameState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& State, const FString& NewName);

	/** Moves states to where the graph has them. */
	void MoveStates(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TMap<FGuid, FVector2D>& Positions);

	/** Makes State the one Entry goes to: Entry's first transition points at it, or a new one is made. */
	void SetEntryState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& State);

	/** Moves a transition one place up (Delta -1) or down in priority among those leaving the same node. */
	void MoveTransitionPriority(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Transition, int32 Delta);

	/** Adds a layer, its Entry, Any State and Exit placed, and returns its index. */
	int32 AddLayer(UWidgetAnimStateMachineAsset* Machine);

	/** Removes a layer - never the last. */
	void RemoveLayer(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex);

	/** Adds an input of Type, named for it, and returns its name. */
	FName AddInput(UWidgetAnimStateMachineAsset* Machine, EWidgetAnimMachineInputType Type);

	/** Removes an input; the conditions that test it stay, and never hold, until they are changed. */
	void RemoveInput(UWidgetAnimStateMachineAsset* Machine, FName Input);

	/** A new machine's content: one layer with an Idle state that Entry goes to. */
	void SetUpNewMachine(UWidgetAnimStateMachineAsset* Machine);
}

/**
 * One layer of a machine, as a graph. Transient: rebuilt from the machine - see the note at the top of this file.
 */
UCLASS(Transient)
class UWidgetAnimMachineGraph : public UEdGraph
{
	GENERATED_BODY()

public:

	UPROPERTY()
	TObjectPtr<UWidgetAnimStateMachineAsset> Machine = nullptr;

	int32 LayerIndex = INDEX_NONE;

	/** Makes the graph's nodes again from the machine's layer. */
	void RebuildFromMachine();

	/** Asks for RebuildFromMachine on the next tick of the editor showing it - never under a drag or a menu in progress. */
	void RequestRebuild() { bRebuildRequested = true; }
	bool ConsumeRebuildRequest() { const bool bWas = bRebuildRequested; bRebuildRequested = false; return bWas; }

	const FWidgetAnimMachineLayer* GetLayer() const;

	UWidgetAnimMachineGraphNode* FindStateNode(const FGuid& StateId) const;

	/**
	 * What the layer is doing in the widget being watched - the designer preview, or a widget in Play In Editor. False when
	 * nothing is. Set by the editor; read by the nodes and arrows to light up the state and transition in play.
	 */
	TFunction<bool(FWidgetAnimMachineLayerDebug& /*OutDebug*/)> GetDebug;

	/** A state node was double-clicked: the editor opens its animation. */
	TFunction<void(const FGuid& /*StateId*/)> OnOpenState;

	/** Whether the graph is being looked at in Play In Editor - its states then show where the widget is. */
	bool IsDebugging() const;

private:

	bool bRebuildRequested = false;
};

/** A node of the layer: a state, Entry, Any State or Exit. */
UCLASS()
class UWidgetAnimMachineGraphNode : public UEdGraphNode
{
	GENERATED_BODY()

public:

	UPROPERTY()
	FGuid StateId;

	UPROPERTY()
	EWidgetAnimMachineNodeKind Kind = EWidgetAnimMachineNodeKind::State;

	/** The node's state in the machine, or null when it is gone. */
	const FWidgetAnimMachineState* GetState() const;

	UWidgetAnimMachineGraph* GetMachineGraph() const;

	UEdGraphPin* GetInputPin() const { return Pins.IsValidIndex(0) ? Pins[0] : nullptr; }
	UEdGraphPin* GetOutputPin() const { return Pins.IsValidIndex(1) ? Pins[1] : nullptr; }

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return Kind == EWidgetAnimMachineNodeKind::State; }
	virtual bool CanDuplicateNode() const override { return false; }
	virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
};

/** A transition: drawn as the arrow between its two nodes, with its rule icon in the middle. */
UCLASS()
class UWidgetAnimMachineTransitionNode : public UEdGraphNode
{
	GENERATED_BODY()

public:

	UPROPERTY()
	FGuid TransitionId;

	UPROPERTY()
	FGuid FromId;

	UPROPERTY()
	FGuid ToId;

	const FWidgetAnimMachineTransition* GetTransition() const;
	UWidgetAnimMachineGraph* GetMachineGraph() const;

	UEdGraphPin* GetInputPin() const { return Pins.IsValidIndex(0) ? Pins[0] : nullptr; }
	UEdGraphPin* GetOutputPin() const { return Pins.IsValidIndex(1) ? Pins[1] : nullptr; }

	/** Its conditions and blend in words, one line each - its tooltip, and what the editor shows for it. */
	FText DescribeRule() const;

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanDuplicateNode() const override { return false; }
	virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
};

/** What the graph allows: states joined by transitions, added from the right-click menu. */
UCLASS()
class UWidgetAnimMachineGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:

	static const FName PC_Transition;

	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;
	virtual FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const override;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	virtual bool ShouldHidePinDefaultValue(UEdGraphPin* Pin) const override { return true; }
	virtual void GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;

	/** The links are the machine's transitions: they are removed by deleting the transition, never by breaking a link in the graph alone. */
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const override {}
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override {}

	/** The state node a pin belongs to, and its graph. */
	static UWidgetAnimMachineGraphNode* GetStateNode(const UEdGraphPin* Pin);
};
