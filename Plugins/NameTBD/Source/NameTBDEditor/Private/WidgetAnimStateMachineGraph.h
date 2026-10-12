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
class UWidgetAnimMachineRerouteNode;

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
// a state, C for a comment box, Q to straighten wires, double-click a wire for a reroute point on it.

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
	/** Adds a state to Layer at Position, with Animation - in the sub-machine ParentMachine (invalid: the layer's own graph) - and returns its id. */
	FGuid AddState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, FName Name, const FVector2D& Position, UWidgetAnimPresetAsset* Animation = nullptr,
		const FGuid& ParentMachine = FGuid());

	/**
	 * Makes a state a sub-machine - a graph of its own, with its Entry, Any State and Exit - or (bSubMachine false) a plain state
	 * again, which only one with nothing inside it but those three can be.
	 */
	void SetSubMachine(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& State, bool bSubMachine);

	/** Whether a sub-machine holds nothing but its Entry, Any State and Exit. */
	bool IsSubMachineEmpty(const FWidgetAnimMachineLayer& Layer, const FGuid& SubMachine);

	/**
	 * The condition a wire drawn between states gets from a widget input state at either end (FWidgetAnimMachineState::
	 * EditorWidgetInput): into one, its input Is True (Fired, for a pulse); out of one, Is False - or, for a pulse, When
	 * Finished. Into one wins when both ends are. Nothing when neither is, or the transition has conditions already.
	 */
	void AddWidgetInputConditions(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Transition);

	/** The compare a condition on Input starts with: Fired for a pulse or a Trigger, Is True for anything else. */
	EWidgetAnimMachineCompare GetTrueCompare(const UWidgetAnimStateMachineAsset* Machine, FName Input);

	/** Adds a transition between two nodes of Layer, set up for where it goes - a Cut from Entry, Closing Is True to Exit - and returns its id. Invalid when the two can't be joined. */
	FGuid AddTransition(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& From, const FGuid& To);

	/** Gives a transition one more condition. */
	void AddCondition(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Transition, FName Input, EWidgetAnimMachineCompare Compare);

	/** Whether a transition can go from one node to the other: not into Entry or Any State, not out of Exit, not to itself. */
	bool CanConnect(const UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& From, const FGuid& To);

	/**
	 * Deletes states - never Entry, Any State or Exit - transitions, comment boxes and reroute points (by transition, their
	 * indices on its wire); transitions of a deleted state go too.
	 */
	void Delete(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& States, const TArray<FGuid>& Transitions,
		const TArray<FGuid>& Comments = TArray<FGuid>(), const TMap<FGuid, TArray<int32>>& Reroutes = TMap<FGuid, TArray<int32>>());

	/**
	 * Adds a reroute point to a transition's wire, Index-th from the node it leaves, at Offset (from GetPlacementAnchor, as
	 * the transition node's own). Returns whether it was added.
	 */
	bool AddReroute(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Transition, int32 Index, const FVector2D& Offset);

	/** Gives a transition's wire exactly these reroute points (offsets from GetPlacementAnchor, from the node it leaves). */
	void SetReroutes(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Transition, const TArray<FVector2D>& Offsets);

	/** Takes every reroute point off transitions' wires. */
	void ClearReroutes(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& Transitions);

	/** Puts transitions' reroute points where the graph has them - dragged. Not told to anyone: the graph already shows them. */
	void MoveReroutes(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TMap<FGuid, TArray<FVector2D>>& Points);

	/** Renames a state, made unique in its layer. */
	void RenameState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& State, const FString& NewName);

	/** Moves states to where the graph has them. Not told to anyone: the graph already shows them. */
	void MoveStates(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TMap<FGuid, FVector2D>& Positions);

	/**
	 * Puts transition nodes where they were dragged - an offset from halfway between their two nodes (see
	 * WidgetAnimMachineWires::GetPlacementAnchor) - or, unset, back on their wire's own middle. Not told to anyone either.
	 */
	void PlaceTransitions(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TMap<FGuid, TOptional<FVector2D>>& Offsets);

	/** Makes State the one Entry goes to: Entry's first transition points at it, or a new one is made. */
	void SetEntryState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& State);

	/** Moves a transition one place up (Delta -1) or down in priority among those leaving the same node. */
	void MoveTransitionPriority(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Transition, int32 Delta);

	/** Switches transitions on or off. */
	void SetTransitionsEnabled(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& Transitions, bool bEnabled);

	/** Draws transitions' wires dead straight, or curving as usual. Not told to anyone: the graph's nodes are set by the caller. */
	void SetTransitionsStraight(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& Transitions, bool bStraight);

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

	/** Lists an input under Category in the Inputs panel (empty: under none). */
	void SetInputCategory(UWidgetAnimStateMachineAsset* Machine, FName Input, const FString& Category);

	/** Renames a category: every input under Old is listed under New (empty: under none). */
	void RenameInputCategory(UWidgetAnimStateMachineAsset* Machine, const FString& Old, const FString& New);

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

	/** Adds a comment box round Bounds - in the sub-machine ParentMachine's graph (invalid: the layer's own) - and returns its id. */
	FGuid AddComment(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FBox2D& Bounds, const FString& Text, const FGuid& ParentMachine = FGuid());

	/**
	 * Writes comment boxes as the graph has them now - moved, resized, retitled, recolored - into the layer. Not told to anyone:
	 * the graph already shows them. False when nothing changed.
	 */
	bool UpdateComments(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FWidgetAnimMachineComment>& Comments);

	/**
	 * The states, the transitions among them and the comment boxes, as text for the clipboard. Entry, Any State and Exit are left
	 * out - but a sub-machine's go with it, with everything inside it.
	 */
	FString CopyToText(const UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& States, const TArray<FGuid>& Comments);

	/** Whether Text is something CopyToText made. */
	bool CanPasteText(const FString& Text);

	/**
	 * Adds what Text holds to the layer as new states, transitions and comment boxes - new ids, unique names - with the top
	 * left of what was copied at Location, in the sub-machine ParentMachine's graph (invalid: the layer's own). Returns the new
	 * states' and comments' ids.
	 */
	TArray<FGuid> PasteText(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FString& Text, const FVector2D& Location, const FGuid& ParentMachine = FGuid());

	/** A new machine's content: one layer with an Idle state that Entry goes to. */
	void SetUpNewMachine(UWidgetAnimStateMachineAsset* Machine);

	// Collapsed groups - see FWidgetAnimMachineStateGroup. The graph's only: the states play as ever.

	/** Puts states (never Entry, Any State or Exit) in a new collapsed group, its box where they were; returns its id. */
	FGuid CollapseToGroup(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& States);

	/**
	 * Where a state (or a collapsed group's box) sits, set without an undo step of its own - the graph keeping a node's middle
	 * in place as its size changes, part of whatever edit changed it.
	 */
	void SetNodePositionQuietly(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Id, const FVector2D& Position);

	/**
	 * The size a state's (or a collapsed group's) node had when its position was last kept, and the zoom then - false when
	 * there is no such node. Set by SetNodeSizeQuietly: no undo step, and not a change to save on its own.
	 */
	bool GetNodeSize(const UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Id, FVector2D& OutSize, float& OutZoom);
	void SetNodeSizeQuietly(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Id, const FVector2D& Size, float Zoom);

	/** Collapses a group into its box, or expands it to its states in place. */
	void SetGroupCollapsed(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Group, bool bCollapsed);

	/** Takes a group's states out of it, and the group away. */
	void Ungroup(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Group);

	void RenameGroup(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Group, const FString& NewName);

	/** The group with an id in a layer, or null. */
	const FWidgetAnimMachineStateGroup* FindGroup(const FWidgetAnimMachineLayer& Layer, const FGuid& Group);
}

/**
 * Where a transition's wire runs: a Blueprint spline - a Hermite curve, leaving the side or corner of one state that faces the
 * other and arriving at the side or corner of that one facing back - and where on it the transition node sits. Wires between
 * the same two states run side by side, parallel, each in a lane of its own. A wire routed through points of its own - reroute
 * points, and its transition node where that was dragged - is a chain of curves, one from each point to the next, meeting
 * smoothly at each, as a Blueprint wire runs through its reroute nodes.
 */
struct FWidgetAnimMachineWire
{
	/** One curve of the wire: from Start, leaving along StartTangent, to End, arriving along EndTangent. */
	struct FSegment
	{
		FVector2f Start = FVector2f::ZeroVector;
		FVector2f StartTangent = FVector2f::ZeroVector;
		FVector2f End = FVector2f::ZeroVector;
		FVector2f EndTangent = FVector2f::ZeroVector;

		/** How many reroute points the wire has run through before this curve: where one added on it goes in the transition's list. */
		int32 ReroutesBefore = 0;
	};

	/** Its curves, end to end, the way the transition goes: one for a plain wire, one more for each point it runs through. */
	TArray<FSegment, TInlineAllocator<4>> Segments;

	/** How far along the wire the transition node sits, 0 - 1 - each curve an equal share of the way. */
	float NodeT = 0.5f;

	FVector2f NodePoint = FVector2f::ZeroVector;

	/** The way the wire runs through the transition node, unit length. */
	FVector2f NodeDirection = FVector2f(1.f, 0.f);

	FVector2f Evaluate(float T) const;
	FVector2f Derivative(float T) const;

	/** The same wire on screen: every point through Origin + Point * Scale, every tangent scaled. */
	void Transform(const FVector2f& Origin, float Scale);

	/** How far Point is from the wire, the curve it is nearest and the nearest point on it - for a click on the wire. Big with no curves. */
	float GetDistance(const FVector2f& Point, int32& OutSegment, FVector2f* OutNearest = nullptr) const;
};

namespace WidgetAnimMachineWires
{
	/** The transition node's size, in graph units. */
	constexpr float NodeDiameter = 24.f;

	/** How far apart parallel wires run, at most, in graph units: far enough that their transition nodes sit side by side. */
	constexpr float LaneSpacing = 30.f;

	/** How far a corner a wire leaves from is pulled in from the box's corner, onto the state's rounded corner. */
	constexpr float CornerInset = 3.f;

	/**
	 * Of two nodes apart both ways, how much further apart one way must be than the other for the wire to leave a side rather
	 * than a corner.
	 */
	constexpr float CornerRatio = 6.f;

	/**
	 * A wire whose ends both point along the line between them, within this many degrees - lined up by Q, give or take a pixel -
	 * is drawn as a straight line rather than a curve that bows by that pixel.
	 */
	constexpr float StraightSnapDegrees = 8.f;

	/** A reroute point's dot, and the square round it that takes the mouse, in graph units. */
	constexpr float RerouteDiameter = 10.f;
	constexpr float RerouteHitSize = 16.f;

	/** How close to a wire, in pixels on screen, a double-click puts a reroute point on it. */
	constexpr float WireHitDistance = 7.f;

	/**
	 * Lays out the wire of a transition between two nodes, given as boxes in graph space. Lane and LaneCount: where it is among
	 * the wires between the two (see GetLane); bReversed: it runs from Second to First, not First to Second. NodePoint: where
	 * its transition node was dragged, if it was. Reroutes: the points it is routed through, in order the way it goes. Through
	 * either, the wire runs through them all - the dragged node wherever along the way it adds least - and lanes don't apply.
	 * Through reroute points the wire is sharp: straight from point to point, turning at each, as a routed wire is drawn; it
	 * leaves and enters its states from the middle of a side (or a corner), as every wire does.
	 */
	FWidgetAnimMachineWire Compute(const FBox2f& First, const FBox2f& Second, int32 Lane, int32 LaneCount, bool bReversed,
		const FVector2f* NodePoint = nullptr, TConstArrayView<FVector2f> Reroutes = TConstArrayView<FVector2f>(), bool bStraight = false);

	/**
	 * Whether two nodes lie on a slant - apart both ways, the line between their middles between 25 and 65 degrees - so a wire
	 * between them leaves a corner: what Straighten Connections (Q) draws dead straight where they are, rather than moving one.
	 */
	bool IsSlanted(const FBox2f& First, const FBox2f& Second);

	/**
	 * The side or corner of Box that faces Toward - (1,0) right, (0,1) bottom, (1,1) bottom-right and so on: where a wire to it
	 * leaves from. A side when the two overlap across it or lie much further apart one way than the other; a corner when they
	 * lie diagonally.
	 */
	FVector2f GetFacingSide(const FBox2f& Box, const FBox2f& Toward);
	FVector2f GetFacingSide(const FBox2f& Box, const FVector2f& Toward);

	/** The middle of that side - or that corner, pulled in by CornerInset. */
	FVector2f GetSideMiddle(const FBox2f& Box, const FVector2f& Side);

	/** Where a wire leaves Side, Offset along it - for a corner, along whichever edge the offset leads onto. Across: which way is positive. */
	FVector2f GetAnchor(const FBox2f& Box, const FVector2f& Side, float Offset, const FVector2f& Across);

	/** What a dragged transition node's offset is measured from: halfway between its two nodes' top-left corners. */
	FVector2f GetPlacementAnchor(const FBox2f& First, const FBox2f& Second);
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

	/**
	 * The collapsed group opened - double-clicked - so the graph shows its states alone; invalid shows the whole layer, each
	 * collapsed group as one box (see FWidgetAnimMachineStateGroup). Set with OpenGroupView.
	 */
	FGuid OpenGroup;

	/**
	 * Each state node's size when last drawn, by state (or group) id. Emptied as the graph is made again: a node made again
	 * takes the size the machine kept with its position (FWidgetAnimMachineState::EditorSize) - after an undo or a redo,
	 * the one that goes with the position brought back. See SWidgetAnimMachineStateNode::Tick: a node whose size changes
	 * keeps its middle where it was, so the wires lined up with it stay straight.
	 */
	TMap<FGuid, FVector2f> KnownNodeSizes;

	/** The graph's zoom when KnownNodeSizes was last taken: a size that changes with the zoom moves nothing. */
	float KnownNodeZoom = 0.f;

	/** Shows a collapsed group's states alone - or, with an invalid id, the whole layer - from the next tick. */
	void OpenGroupView(const FGuid& Group) { OpenGroup = Group; RequestRebuild(); }

	/**
	 * The sub-machine opened - double-clicked - so the graph shows what is inside it: its Entry, Any State, Exit, states and
	 * transitions (see FWidgetAnimMachineState::bSubMachine). Invalid: the layer's own graph. Set with OpenMachineView.
	 */
	FGuid OpenMachine;

	/** Shows a sub-machine's graph - or, with an invalid id, the layer's own - from the next tick. Any open collapsed states close. */
	void OpenMachineView(const FGuid& SubMachine) { OpenMachine = SubMachine; OpenGroup.Invalidate(); RequestRebuild(); }

	/** The sub-machines from the layer's own graph in to the one open, outermost first: the trail over the graph. */
	void GetOpenMachinePath(TArray<FGuid>& OutPath) const;

	/**
	 * The node a state is shown as: its own, or - in a collapsed group, at the top of its graph - its group's box. Invalid when it
	 * isn't shown: in another sub-machine's graph than the one open, or in a group other than the one open.
	 */
	FGuid GetShownNodeId(const FGuid& StateId) const;

	/** The selected states, for Collapse To Group. Set by the editor. */
	TFunction<void(TArray<FGuid>& /*OutStates*/)> GetSelectedStates;

	/** Makes the graph's nodes again from the machine's layer. */
	void RebuildFromMachine();

	/**
	 * Reroute points dropped with R along the wire being dragged out of a state (SWidgetAnimStateMachineEditor::HandleGraphRerouteKey):
	 * in graph space, in the order dropped - and where each is on screen this frame, for the dragged wire's preview. They go onto
	 * the transition the drag makes (TakePendingDragReroutes); a drag that makes none forgets them. Never a node of their own, so
	 * nothing is left behind unjoined.
	 */
	TArray<FVector2f> PendingDragReroutes;
	TArray<FVector2f> PendingDragRerouteScreen;

	/**
	 * PendingDragReroutes as reroute offsets of a transition from From to To (their shown nodes; ToPosition is To's place when
	 * it was only just added), in the order the wire runs - and forgotten.
	 */
	TArray<FVector2D> TakePendingDragReroutes(const FGuid& From, const FGuid& To, const FVector2D* ToPosition = nullptr);

	/**
	 * Any node in the graph that isn't the machine's - a state, a transition, a reroute point, a comment box - such as a reroute
	 * knot the graph editor makes of its own on a wire let go in empty space: taken out, its links with it. Returns how many.
	 */
	int32 RemoveForeignNodes();

	/** Asks for RebuildFromMachine on the next tick of the editor showing it - never under a drag or a menu in progress. */
	void RequestRebuild() { bRebuildRequested = true; }
	bool ConsumeRebuildRequest() { const bool bWas = bRebuildRequested; bRebuildRequested = false; return bWas; }

	const FWidgetAnimMachineLayer* GetLayer() const;

	UWidgetAnimMachineGraphNode* FindStateNode(const FGuid& StateId) const;
	UWidgetAnimMachineTransitionNode* FindTransitionNode(const FGuid& TransitionId) const;
	UEdGraphNode_Comment* FindCommentNode(const FGuid& CommentId) const;

	/** The layer's state, and transition, with an id - what each node shows, read every frame. Null when it is gone. */
	const FWidgetAnimMachineState* FindLayerState(const FGuid& StateId) const;
	const FWidgetAnimMachineTransition* FindLayerTransition(const FGuid& TransitionId) const;

	/** Where a transition's wire is among those between its two nodes, how many there are, and whether it runs against the pair's own order. */
	void GetLane(const UWidgetAnimMachineTransitionNode* Node, int32& OutLane, int32& OutCount, bool& bOutReversed) const;

	/**
	 * A transition's wire in graph space, given its pair's two nodes' boxes (see GetPairOrder): in its lane, or through where its
	 * node was dragged and its reroute points. What its transition node is placed on, its reroute points follow, and is drawn.
	 */
	FWidgetAnimMachineWire ComputeWire(const UWidgetAnimMachineTransitionNode* Node, const FBox2f& FirstBox, const FBox2f& SecondBox) const;

	/** The canonical first and second node of a transition's pair - the order its lanes are counted in. */
	static void GetPairOrder(const UWidgetAnimMachineTransitionNode* Node, FGuid& OutFirst, FGuid& OutSecond);

	/**
	 * What the layer is doing in the widget being watched - the designer preview, or a widget in Play In Editor. False when
	 * nothing is. Set by the editor; read by the nodes and wires to light up the state and transition in play.
	 */
	TFunction<bool(FWidgetAnimMachineLayerDebug& /*OutDebug*/)> GetDebug;

	/** The node menu's Open Animation: the editor opens the state's animation. */
	TFunction<void(const FGuid& /*StateId*/)> OnOpenState;

	/**
	 * A state node was double-clicked, or its menu's Jump To State picked: the editor sets the preview's inputs so the layer
	 * goes to that state (see WidgetAnimMachineJump.h). Unset, a double-click opens the state's animation as before.
	 */
	TFunction<void(const FGuid& /*StateId*/)> OnJumpToState;

	/** The transitions selected in the graph, for menus that act on the whole selection. */
	TFunction<void(TArray<FGuid>& /*OutTransitions*/)> GetSelectedTransitions;

	/** Q, or the node menu's Straighten Connections: the editor lines the selected states up. */
	TFunction<void()> OnStraighten;

	/** Whether the machine can be edited - not while the editor's lock is on. Unset: always. */
	TFunction<bool()> CanEditMachine;

	/**
	 * States were just collapsed into the collapsed states GroupId (made, or folded back): the editor selects its box and,
	 * once it has been laid out, lines it up with what it is wired to, as Q would.
	 */
	TFunction<void(const FGuid& /*GroupId*/)> OnCollapsed;

	/** Whether the graph is being looked at in Play In Editor - its states then show where the widget is. */
	bool IsDebugging() const;

	/**
	 * When each transition was last being taken, in FPlatformTime seconds - so the dots running along its wire stay a moment
	 * after a short blend, and fade, rather than flashing for a frame or two. Kept by the wires as they are drawn.
	 */
	TMap<FGuid, double> LastActiveTimes;

	/**
	 * While a widget is watched: whether condition ConditionIndex of Transition - one on another layer's state - holds. One
	 * that held within the last LayerConditionLinger seconds still counts, so a one-frame Entered / Exited is seen to fire.
	 * Unset when nothing is watched, or the condition isn't on another layer's state.
	 */
	TOptional<bool> GetLayerConditionHeld(const FGuid& Transition, int32 ConditionIndex) const;

	/** Whether every condition on another layer's state of Transition holds (see GetLayerConditionHeld). Unset when nothing is watched or it has none. */
	TOptional<bool> GetLayerConditionsHeld(const FGuid& Transition) const;

	/** When each such condition last held, in FPlatformTime seconds - kept as they are read. */
	mutable TMap<TPair<FGuid, int32>, double> LayerConditionHeldTimes;

	/** GetLayerConditionHeld, from the layer's debug as already read. */
	TOptional<bool> ReadLayerConditionHeld(const FWidgetAnimMachineLayerDebug& Debug, const FGuid& Transition, int32 ConditionIndex) const;

	/** Seconds the stripes on the state in play have scrolled for - moved on by the editor every frame, but not while the preview is paused. */
	double StripeClock = 0.0;

private:

	bool bRebuildRequested = false;

	/**
	 * Where each node is in Nodes, and each state and transition in the layer, by id - asked by every node and every wire,
	 * every frame, which searched them all for each one: a machine of hundreds of states and transitions cost the square of
	 * that. Only trusted as far as it checks out: an entry pointing anywhere else, or an id it doesn't have, makes it again
	 * from what is there now. Plain C++, never saved.
	 */
	mutable TMap<FGuid, int32> StateNodeIndices;
	mutable TMap<FGuid, int32> TransitionNodeIndices;
	mutable TMap<FGuid, int32> StateIndices;
	mutable TMap<FGuid, int32> TransitionIndices;

	/**
	 * Per pair of nodes (see GetPairOrder), the transition nodes between them, in Nodes order - for GetLane, which every wire
	 * asks every frame. Gathered again each frame it is asked in, whenever Nodes changes size, and on RebuildFromMachine.
	 */
	mutable TMap<TPair<FGuid, FGuid>, TArray<const UWidgetAnimMachineTransitionNode*, TInlineAllocator<2>>> PairNodes;
	mutable uint64 PairNodesFrame = MAX_uint64;
	mutable int32 PairNodesCount = INDEX_NONE;
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

	/** A collapsed group's box rather than a state: StateId is then the group's id. See FWidgetAnimMachineStateGroup. */
	UPROPERTY()
	bool bIsGroup = false;

	/** The node's state in the machine, or null when it is gone - and for a group's box. */
	const FWidgetAnimMachineState* GetState() const;

	/** A group box's group, or null. */
	const FWidgetAnimMachineStateGroup* GetGroup() const;

	/**
	 * Whether the state being watched is this node's - or inside it, for a sub-machine; or, for a group's box, one of its states
	 * (or inside one of them).
	 */
	bool ShowsState(const FGuid& State) const;

	/** Whether the node is in a sub-machine's graph rather than the layer's own. */
	bool IsInSubMachine() const;

	UWidgetAnimMachineGraph* GetMachineGraph() const;

	UEdGraphPin* GetInputPin() const { return Pins.IsValidIndex(0) ? Pins[0] : nullptr; }
	UEdGraphPin* GetOutputPin() const { return Pins.IsValidIndex(1) ? Pins[1] : nullptr; }

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return Kind == EWidgetAnimMachineNodeKind::State && !bIsGroup; }
	virtual bool CanDuplicateNode() const override { return Kind == EWidgetAnimMachineNodeKind::State && !bIsGroup; }
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

	/**
	 * Whether its node was dragged somewhere of its own, and where: its middle, from GetPlacementAnchor. From the machine as
	 * the graph is made; changed by a drag, and written back into the machine once the mouse is let go.
	 */
	bool bPlaced = false;
	FVector2f PlacedOffset = FVector2f::ZeroVector;

	/** Its wire drawn dead straight from state to state (see FWidgetAnimMachineTransition::bEditorStraight). From the machine. */
	bool bStraight = false;

	/**
	 * The reroute points its wire runs through, in order the way it goes, each from GetPlacementAnchor. From the machine as the
	 * graph is made; changed as its reroute nodes are dragged, and written back into the machine once the mouse is let go.
	 */
	TArray<FVector2f> RerouteOffsets;

	const FWidgetAnimMachineTransition* GetTransition() const;
	UWidgetAnimMachineGraph* GetMachineGraph() const;

	UEdGraphPin* GetInputPin() const { return Pins.IsValidIndex(0) ? Pins[0] : nullptr; }
	UEdGraphPin* GetOutputPin() const { return Pins.IsValidIndex(1) ? Pins[1] : nullptr; }

	/** Its conditions and blend in words, one line each. */
	FText DescribeRule() const;

	/** Its conditions in words, joined by "and" - "Always" with none. */
	FText DescribeConditions() const;

	/** How long it takes, short, for the label beside it: "0.2s", "0.5s + 0.1s" for a transition animation, "Cut". */
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

/**
 * A reroute point on a transition's wire - a Blueprint reroute node's dot: the wire runs through it, so wires can be led round
 * states and kept tidy. Made by double-clicking a wire; dragged, deleted, selected with others like any node. Nothing at runtime:
 * the points are the transition's own (FWidgetAnimMachineTransition::EditorReroutePoints).
 */
UCLASS()
class UWidgetAnimMachineRerouteNode : public UEdGraphNode
{
	GENERATED_BODY()

public:

	UPROPERTY()
	FGuid TransitionId;

	/** Which of its transition's reroute points it is, counted from the node the transition leaves. */
	UPROPERTY()
	int32 RerouteIndex = 0;

	/** The transition node of the wire it is on. */
	UPROPERTY()
	TObjectPtr<UWidgetAnimMachineTransitionNode> TransitionNode = nullptr;

	UWidgetAnimMachineGraph* GetMachineGraph() const;

	// UEdGraphNode
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
	 * The states the Add State menus offer ready-named: Idle, and one for every widget input - the widget's states (Holding,
	 * on the machine's Press And Hold, after Pressed), the Events and the Gamepad buttons. Each paired with the input it is for
	 * (None for Idle and Holding - Holding's is the machine's Press And Hold).
	 */
	static const TArray<TPair<FName, FName>>& GetPresetStates();

	/**
	 * The Holding preset: a state played while the machine's Press And Hold is held - made for it if it has none. Dragged from
	 * a state, joined there on Is Held and back on Is Let Go, so a hold let go early blends back.
	 */
	static const FName HoldingPreset;

	/** What a preset state is for, for its menu entry. */
	static FText GetPresetTooltip(const TPair<FName, FName>& Preset);

	/**
	 * Where a preset sits in the graph's right-click menu: Idle with Add State; the widget's own inputs under Widget Inputs, the
	 * Events and the Gamepad buttons (each kind of button) in sub-categories of it - as the Widget Inputs panel lists them.
	 */
	static FText GetPresetCategory(const TPair<FName, FName>& Preset);

	/** The Add Sub-Machine preset: a state with a graph of its own. */
	static const FName SubMachinePreset;

	/**
	 * Adds a preset state - or a plain one, for None - at Location: named for it, and, dragged from a state, joined from that
	 * state by a transition that tests its input. Returns its id.
	 */
	static FGuid AddPresetState(UWidgetAnimMachineGraph* Graph, FName Preset, const FVector2D& Location, const UWidgetAnimMachineGraphNode* From);
};
