// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachineRuntime.h"

#include "WidgetAnimStateMachineGraph.generated.h"

class UEdGraphNode_Comment;
class UWidgetAnimStateMachineAsset;
class UWidgetAnimMachineGraphNode;
class UWidgetAnimMachineTransitionNode;

// =====================================================================================================
// The Widget Animation State Machine editor's graph
// =====================================================================================================
//
// The machine asset is the truth: its layers' states, transitions and comment boxes, and where each sits. A graph is only a
// view of one layer, rebuilt from the asset whenever it changes - so an edit made anywhere (the graph, the Details, an undo)
// shows everywhere, and nothing about the graph is ever saved. Every edit made in the graph is made to the asset, in a
// transaction, and the graph rebuilt from it (see WidgetAnimMachineEdits). What the graph's own widgets change in place -
// a dragged node, a resized comment, a comment bubble typed into - is written back by the editor once the mouse is let go.
//
// It looks like a Blueprint graph with an Animation Blueprint's state machine in it: states as rounded nodes, transitions as
// Blueprint wires with a round transition node in the middle that points the way the transition goes and shows how it blends;
// drag from a state's edge to another state to connect them, double-click a state to open its animation, right-click to add
// a state, C for a comment box, Q to straighten wires.

/** What the clipboard holds for states copied from a layer: the states, the transitions between them, and comment boxes. */
USTRUCT()
struct FWidgetAnimMachineClipboard
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FWidgetAnimMachineState> States;

	UPROPERTY()
	TArray<FWidgetAnimMachineTransition> Transitions;

	UPROPERTY()
	TArray<FWidgetAnimMachineComment> Comments;
};

/** Every edit the editor makes to a machine: each in its own transaction, the machine normalized and told after. */
namespace WidgetAnimMachineEdits
{
	/** Adds a state to Layer at Position, with Animation, and returns its id. */
	FGuid AddState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, FName Name, const FVector2D& Position, UWidgetAnimPresetAsset* Animation = nullptr);

	/** Adds a transition between two nodes of Layer, set up for where it goes - a Cut from Entry, Closing Is True to Exit - and returns its id. Invalid when the two can't be joined. */
	FGuid AddTransition(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& From, const FGuid& To);

	/** Gives a transition one more condition. */
	void AddCondition(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Transition, FName Input, EWidgetAnimMachineCompare Compare);

	/** Whether a transition can go from one node to the other: not into Entry or Any State, not out of Exit, not to itself. */
	bool CanConnect(const UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& From, const FGuid& To);

	/** Deletes states - never Entry, Any State or Exit - transitions and comment boxes; transitions of a deleted state go too. */
	void Delete(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& States, const TArray<FGuid>& Transitions, const TArray<FGuid>& Comments = TArray<FGuid>());

	/** Renames a state, made unique in its layer. */
	void RenameState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& State, const FString& NewName);

	/** Moves states to where the graph has them. Not told to anyone: the graph already shows them. */
	void MoveStates(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TMap<FGuid, FVector2D>& Positions);

	/** Makes State the one Entry goes to: Entry's first transition points at it, or a new one is made. */
	void SetEntryState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& State);

	/** Moves a transition one place up (Delta -1) or down in priority among those leaving the same node. */
	void MoveTransitionPriority(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Transition, int32 Delta);

	/** Switches transitions on or off. */
	void SetTransitionsEnabled(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& Transitions, bool bEnabled);

	/** Adds a layer, its Entry, Any State and Exit placed, and returns its index. */
	int32 AddLayer(UWidgetAnimStateMachineAsset* Machine);

	/** Removes a layer - never the last. */
	void RemoveLayer(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex);

	/** Renames a layer, made unique in the machine. */
	void RenameLayer(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FString& NewName);

	/** Adds an input of Type, named for it, and returns its name. */
	FName AddInput(UWidgetAnimStateMachineAsset* Machine, EWidgetAnimMachineInputType Type);

	/** Removes an input; the conditions that test it stay, and never hold, until they are changed. */
	void RemoveInput(UWidgetAnimStateMachineAsset* Machine, FName Input);

	/**
	 * Renames an input, and every condition, Progress Input and Weight Input that reads it - made unique, never a built-in
	 * name. Returns the name it got: the old one when nothing changed.
	 */
	FName RenameInput(UWidgetAnimStateMachineAsset* Machine, FName Input, const FString& NewName);

	/** Why NewName can't be an input's new name, or empty when it can. */
	FText ValidateInputName(const UWidgetAnimStateMachineAsset* Machine, FName Input, const FString& NewName);

	/** Every place an input is read: renames them all from Old to New, in place. No transaction - see RenameInput. */
	void ReplaceInputReferences(UWidgetAnimStateMachineAsset* Machine, FName Old, FName New);

	/** A state's comment bubble: its text, and whether it is shown. */
	void SetStateComment(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& State, const FString& Comment, bool bVisible);

	/** Adds a comment box round Bounds, and returns its id. */
	FGuid AddComment(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FBox2D& Bounds, const FString& Text);

	/**
	 * Writes comment boxes as the graph has them now - moved, resized, retitled, recolored - into the layer. Not told to anyone:
	 * the graph already shows them. False when nothing changed.
	 */
	bool UpdateComments(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FWidgetAnimMachineComment>& Comments);

	/** The states, the transitions among them and the comment boxes, as text for the clipboard. Entry, Any State and Exit are left out. */
	FString CopyToText(const UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& States, const TArray<FGuid>& Comments);

	/** Whether Text is something CopyToText made. */
	bool CanPasteText(const FString& Text);

	/**
	 * Adds what Text holds to the layer as new states, transitions and comment boxes - new ids, unique names - with the top
	 * left of what was copied at Location. Returns the new states' and comments' ids.
	 */
	TArray<FGuid> PasteText(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FString& Text, const FVector2D& Location);

	/** A new machine's content: one layer with an Idle state that Entry goes to. */
	void SetUpNewMachine(UWidgetAnimStateMachineAsset* Machine);
}

/**
 * Where a transition's wire runs: a Blueprint spline - a Hermite curve, its tangents along the way the two states lie - from
 * the edge of one state to the edge of the other, and where on it the transition node sits. Wires between the same two
 * states run side by side, parallel, each in a lane of its own.
 */
struct FWidgetAnimMachineWire
{
	FVector2f Start = FVector2f::ZeroVector;
	FVector2f StartTangent = FVector2f::ZeroVector;
	FVector2f End = FVector2f::ZeroVector;
	FVector2f EndTangent = FVector2f::ZeroVector;

	/** How far along the wire the transition node sits, 0 - 1. */
	float NodeT = 0.5f;

	FVector2f NodePoint = FVector2f::ZeroVector;

	/** The way the wire runs through the transition node, unit length. */
	FVector2f NodeDirection = FVector2f(1.f, 0.f);

	FVector2f Evaluate(float T) const;
	FVector2f Derivative(float T) const;
};

namespace WidgetAnimMachineWires
{
	/** The transition node's size, in graph units. */
	constexpr float NodeDiameter = 24.f;

	/** How far apart parallel wires run, at most, in graph units. */
	constexpr float LaneSpacing = 16.f;

	/**
	 * Lays out the wire of a transition between two nodes, given as boxes in graph space. Lane and LaneCount: where it is among
	 * the wires between the two (see GetLane); bReversed: it runs from Second to First, not First to Second.
	 */
	FWidgetAnimMachineWire Compute(const FBox2f& First, const FBox2f& Second, int32 Lane, int32 LaneCount, bool bReversed);
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
	UWidgetAnimMachineTransitionNode* FindTransitionNode(const FGuid& TransitionId) const;
	UEdGraphNode_Comment* FindCommentNode(const FGuid& CommentId) const;

	/** Where a transition's wire is among those between its two nodes, how many there are, and whether it runs against the pair's own order. */
	void GetLane(const UWidgetAnimMachineTransitionNode* Node, int32& OutLane, int32& OutCount, bool& bOutReversed) const;

	/** The canonical first and second node of a transition's pair - the order its lanes are counted in. */
	static void GetPairOrder(const UWidgetAnimMachineTransitionNode* Node, FGuid& OutFirst, FGuid& OutSecond);

	/**
	 * What the layer is doing in the widget being watched - the designer preview, or a widget in Play In Editor. False when
	 * nothing is. Set by the editor; read by the nodes and wires to light up the state and transition in play.
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
	virtual bool CanDuplicateNode() const override { return Kind == EWidgetAnimMachineNodeKind::State; }
	virtual TSharedPtr<class SGraphNode> CreateVisualWidget() override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
};

/** A transition: drawn as the wire between its two nodes, with its transition node in the middle. */
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

	/** Its conditions and blend in words, one line each. */
	FText DescribeRule() const;

	/** Its conditions in words, joined by "and" - "Always" with none. */
	FText DescribeConditions() const;

	/** How long it takes, short, for the label beside it: "0.2s", "~0.3s" for a spring, "Cut". */
	FString GetLengthLabel() const;

	/** Its priority among the transitions leaving the same node: 1 for the first checked. And how many there are. */
	void GetPriority(int32& OutPriority, int32& OutCount) const;

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

	/**
	 * The states the Add State menus offer ready-named, one for each built-in input a state is usually made for - Hovered,
	 * Pressed, Selected, Focused, Disabled - and Idle. Each paired with the input it is for (None for Idle).
	 */
	static const TArray<TPair<FName, FName>>& GetPresetStates();

	/**
	 * Adds a preset state - or a plain one, for None - at Location: named for it, and, dragged from a state, joined from that
	 * state by a transition that tests its input. Returns its id.
	 */
	static FGuid AddPresetState(UWidgetAnimMachineGraph* Graph, FName Preset, const FVector2D& Location, const UWidgetAnimMachineGraphNode* From);
};
