// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Misc/Guid.h"
#include "StructUtils/InstancedStruct.h"
#include "WidgetAnimEasing.h"
#include "WidgetAnimSpring.h"
#include "WidgetAnimPreset.h" // FWidgetAnimGroupTarget

#include "WidgetAnimStateMachine.generated.h"

class UCurveFloat;
class UWidget;
class UWidgetAnimPresetAsset;
class UWidgetAnimStateMachineAsset;

// =====================================================================================================
// State machines
// =====================================================================================================
//
// Everything else in the library is imperative: something calls Play By Id at the right moment. A state machine makes a
// widget declarative instead: it knows its states - Idle, Hovered, Pressed, Selected, Disabled - and moves between them on
// its own as its inputs change. A Widget Animation State Machine asset (WASM_) is reusable: one "Standard Button" machine
// serves every button, each Widget Animation Component that plays it mapping the machine's targets onto its own widgets.
//
// THE PARTS
//  - Inputs: the machine's public interface. Bool, Number, Trigger (a one-frame pulse) and Press And Hold (held for a
//    time to complete; let go early, it doesn't - and the state it held blends back). Set from Blueprint (Set State
//    Machine Input), from a View Model (the Widget Data Binder's Set Input), from a Value Input (Hover, Press or Focus,
//    Pointer, Drag, Scroll) with no code at all - or read from the widget itself: the built-in inputs Hovered, Pressed,
//    Focused, Selected and Disabled need no declaring, and on a Common UI button are the button's own states. Closing is
//    built in too: true while the widget plays out.
//  - Layers: parallel machines in one asset - interaction, selection, a badge. Each has its own states and transitions,
//    and composes with the layers under it: Override (the default) owns the channels it animates; Additive adds on top.
//    A layer has a Weight, which a Number input can drive. Later layers win.
//  - States: the animations already authored - a Widget Animation, played once, looped, ping-ponged, or held at its end
//    pose. A percentage animation can follow a Number input: a one-dimensional blend state.
//  - Special nodes: Entry (where a layer starts - plays the role of Anim In), Exit (where a layer ends as the widget closes
//    - the role of Anim Out) and Any State (transitions from wherever the layer is).
//  - Transitions: conditions on inputs, all of which must hold (a second arrow is an OR). How the layer gets there: a
//    Tween from the pose it is actually in (the default), a Spring that keeps its speed when interrupted, a transition
//    animation, or a Cut. When it may go: whenever its conditions hold, or only after part of the state has played. Whether
//    another transition may interrupt it.
//
// INTERRUPTION
//  A transition always starts from the pose the widget is in this frame - never from the state it is leaving as authored -
//  so hovering, leaving and hovering again within a frame or two never snaps. The pose the layer was showing is taken as
//  the transition's start, and the new state's pose is blended in over it.
//
// See WidgetAnimStateMachineRuntime.h for how a machine runs, and UWidgetAnimationComponent::StateMachines for where one
// is played.

/** What kind of value an input holds. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineInputType : uint8
{
	Bool UMETA(ToolTip = "True or false: Hovered, Selected, Has Notification."),

	Number UMETA(DisplayName = "Float", ToolTip = "A number with a fraction: a charge level, a progress for a percentage animation, a layer's weight, a ratio. Compared with == != < <= > >=, or Increased / Decreased / Changed the frame it moves."),

	Trigger UMETA(ToolTip = "A one-frame pulse: Clicked, Rewarded, Error. Fired, it is true for the next frame the machine runs, then false again."),

	Hold UMETA(DisplayName = "Press And Hold", ToolTip = "Press and hold: Is True while the widget is held down, until it has been held for Hold Time - then it is Fired, once, as a Trigger is. Let go before that, it is false again: the hold didn't complete, and the state it held blends back. As a number it is how far through Hold Time the hold is, 0 - 1: a state's Progress Input can follow it. Held by the widget's own pressed state (the built-in Pressed), by Driven By, or from Blueprint (Set State Machine Input: true to press, false to let go)."),

	Integer UMETA(ToolTip = "A whole number: a count, stacks, a level, an index - which list item or tab this is. Compared exactly, so Index == Selected Index can be trusted; or Increased / Decreased / Changed the frame it moves."),

	Choice UMETA(ToolTip = "One of a list of named Options: a rarity (Common, Rare, Epic), a tab (Home, Shop, Profile), a mood. Compared with Is / Is Not, or Changed the frame it switches. Starts at its first option. Set by name from Blueprint, or by its option's number (0 for the first) from a View Model."),

	Vector2D UMETA(DisplayName = "Vector 2D", ToolTip = "Two numbers together, X and Y: a stick, a drag, the pointer over a widget, a swipe. Asked which way it Points (Up, Down, Left, Right - screen space: Up is toward the top, negative Y), or its Length, X or Y compared, or Changed the frame it moves. Driven By takes both parts of a Pointer Position or Drag.")
};

/** Which way a Vector 2D points, for a Points condition - screen space, as the pointer and a drag measure it: Up is negative Y. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineDirection : uint8
{
	Up UMETA(ToolTip = "Toward the top of the screen: Y below 0, and further up than it is to either side."),
	Down UMETA(ToolTip = "Toward the bottom of the screen: Y above 0, and further down than it is to either side."),
	Left UMETA(ToolTip = "X below 0, and further to the side than it is up or down."),
	Right UMETA(ToolTip = "X above 0, and further to the side than it is up or down.")
};

/** What number a condition reads off a Vector 2D. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineVectorPart : uint8
{
	Length UMETA(ToolTip = "How far from 0, whichever way: how far a stick is pushed, how far a drag has gone."),
	X,
	Y
};

/** What a comparison of numbers - or of choices - is made against. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineCompareTo : uint8
{
	Value UMETA(ToolTip = "A value typed here: Charge >= 0.5, Rarity Is Epic."),
	Input UMETA(DisplayName = "Another Input", ToolTip = "Another input's value as it is now: Index == Selected Index, Charge >= Charge Needed, Hovered Tab Is Current Tab.")
};

/** What a node of a layer's graph is. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineNodeKind : uint8
{
	State UMETA(ToolTip = "Plays an animation, or holds its end pose."),

	Entry UMETA(ToolTip = "Where the layer starts. Its transitions pick the first state, the first whose conditions hold."),

	Exit UMETA(ToolTip = "Where the layer goes as the widget closes (the built-in Closing input). A screen closing waits for every layer with a way to Exit to get there."),

	AnyState UMETA(DisplayName = "Any State", ToolTip = "Its transitions are taken from wherever the layer is, before the current state's own.")
};

/** How a state plays its animation. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineStatePlayback : uint8
{
	Once UMETA(DisplayName = "Play Once", ToolTip = "Plays the animation once and holds its last pose."),

	Loop UMETA(ToolTip = "Plays it again and again, from the start."),

	PingPong UMETA(DisplayName = "Ping Pong", ToolTip = "Plays it forward, then back, again and again."),

	Pose UMETA(DisplayName = "Hold End Pose", ToolTip = "Shows the animation's end pose and nothing else: the state is a pose, and the transitions into it do the moving. The steadiest choice for hover and press states that are interrupted all the time.")
};

/** What a condition asks of its input. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineCompare : uint8
{
	IsTrue UMETA(DisplayName = "Is True", ToolTip = "A Bool is true. A Press And Hold is being held, and hasn't been held for its Hold Time yet."),
	IsFalse UMETA(DisplayName = "Is False", ToolTip = "A Bool is false. A Press And Hold (Is Let Go): the frame it is let go - once, as a Trigger fires - never the frame it completes."),
	Fired UMETA(ToolTip = "A Trigger was fired since the machine last ran. A Press And Hold has just been held for its whole Hold Time."),
	Equal UMETA(DisplayName = "==", ToolTip = "Equal - for a Choice, Is: the option picked is that one."),
	NotEqual UMETA(DisplayName = "!=", ToolTip = "Not equal - for a Choice, Is Not."),
	Less UMETA(DisplayName = "<"),
	LessOrEqual UMETA(DisplayName = "<="),
	Greater UMETA(DisplayName = ">"),
	GreaterOrEqual UMETA(DisplayName = ">="),
	Increased UMETA(ToolTip = "The frame a Float, Integer or Vector 2D's Length/X/Y goes up: a counter popping as gold comes in. Like a Trigger, it holds for that one frame."),
	Decreased UMETA(ToolTip = "The frame a Float, Integer or Vector 2D's Length/X/Y goes down: a health bar flashing as it drops. Holds for that one frame."),
	Changed UMETA(ToolTip = "The frame the value changes at all - a Choice switching option, a Vector 2D moving. Holds for that one frame."),
	Points UMETA(ToolTip = "A Vector 2D points that way, and is at least Dead Zone long: a stick pushed left, a drag pulled down, a swipe up.")
};

/** How a transition gets the layer from the pose it is in to the next state. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineBlend : uint8
{
	Tween UMETA(ToolTip = "Blends from the pose the widget is actually in to the next state over Duration, by Easing. Interrupted, the next transition starts from wherever this one had got to."),

	Spring UMETA(ToolTip = "A spring pulls every channel from the pose the widget is in to the next state. Interrupted, it keeps its speed - the liveliest choice for hover and press."),

	Animation UMETA(DisplayName = "Play Transition Animation", ToolTip = "Plays a transition animation first - blended in from the pose the widget is in over Duration - then goes on to the next state, blending into it over Duration again."),

	Cut UMETA(ToolTip = "Jumps straight to the next state.")
};

/** When a transition may be taken. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineExitTime : uint8
{
	Immediately UMETA(DisplayName = "Whenever The Conditions Hold", ToolTip = "As soon as its conditions hold."),

	AtProgress UMETA(DisplayName = "After Part Of The State", ToolTip = "Only once the state has played Exit At of the way through its animation's first pass."),

	WhenFinished UMETA(DisplayName = "When The State Has Played", ToolTip = "Only once the state has played its animation through once - for a looping state, at the end of its first pass. A state with no animation counts as played.")
};

/** How a layer combines with the layers under it. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineLayerBlend : uint8
{
	Override UMETA(ToolTip = "Owns the channels it animates - a widget's opacity, scale, translation, shear or angle, a material parameter, a text: the layers under it are covered there, by its Weight. Channels it doesn't animate are left to them."),

	Additive UMETA(ToolTip = "Adds on top, like any track does: opacity and scale multiply, translation, shear and angle add - by its Weight. Material parameters and texts are assigned, as an Override layer's are.")
};

/**
 * How a Widget Animation Component's other animations - Anim In, Anim Out and everything played by Anim ID - compose with
 * its state machines.
 */
UENUM(BlueprintType)
enum class EWidgetAnimTracksWithMachines : uint8
{
	AddOnTop UMETA(DisplayName = "Add On Top", ToolTip = "They add on top of the state machines' pose: a one-shot pulse played by ID scales a widget its machine is holding hovered. The default."),

	UnderMachines UMETA(DisplayName = "Under State Machines", ToolTip = "They play under the machines: a machine's Override layer covers them on every channel it animates, and only Additive layers add on top of them.")
};

/** What a condition tests: an input, or which state another layer of the machine is in. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineConditionSource : uint8
{
	Input UMETA(ToolTip = "One of the machine's inputs, or a built-in one (Hovered, Pressed...)."),

	LayerState UMETA(DisplayName = "Another Layer's State", ToolTip = "Which state another layer of the machine is in: a badge layer waiting for the interaction layer to be in Pressed. A layer on its way to a state counts as in it - what 'Press went down' means to a UI - and every layer is read as it was before any of them moved this frame, so layer order doesn't matter.")
};

/** What a condition on another layer's state asks. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineLayerStateTest : uint8
{
	IsIn UMETA(DisplayName = "Is In", ToolTip = "The layer is in the state, or on its way there."),
	IsNotIn UMETA(DisplayName = "Is Not In", ToolTip = "The layer is neither in the state nor on its way there."),
	Entered UMETA(ToolTip = "The frame the layer goes into the state - holds for that one frame, as a Trigger does."),
	Exited UMETA(ToolTip = "The frame the layer leaves the state - holds for that one frame.")
};

/** Which of the machine's targets a layer may animate. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineLayerMask : uint8
{
	All UMETA(ToolTip = "Every target: the layer poses whatever its states' animations move."),
	OnlyThese UMETA(DisplayName = "Only These", ToolTip = "Only the targets listed: what the states animate on any other widget is left out, for the layers under it to keep."),
	AllButThese UMETA(DisplayName = "All But These", ToolTip = "Every target but those listed: those are left to the layers under it.")
};

/** How a state picks what it plays: one animation, or several blended by an input. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineBlendSpace : uint8
{
	None UMETA(DisplayName = "Single Animation", ToolTip = "Plays its Animation."),
	OneD UMETA(DisplayName = "Blend 1D", ToolTip = "Blends its Blend Samples by a Float, Integer or Press And Hold input: each sample sits at a number, and the two either side of the input's value are mixed - lean left, centre, lean right by a stick's X; idle to charged by a charge."),
	TwoD UMETA(DisplayName = "Blend 2D", ToolTip = "Blends its Blend Samples by a Vector 2D input: each sample sits at a point, and the three around the input's value are mixed - a card tilting toward the pointer, a stick's direction.")
};

/** What a state's or transition's action does. */
UENUM(BlueprintType)
enum class EWidgetAnimMachineActionKind : uint8
{
	SetInput UMETA(DisplayName = "Set Input", ToolTip = "Sets one of the machine's inputs - a Bool, Float, Integer, Choice or Vector 2D - to a value. Seen by the machine's next frame, so an action never loops within one."),
	FireTrigger UMETA(DisplayName = "Fire Trigger", ToolTip = "Fires a Trigger (or presses or lets go of a Press And Hold, by Bool Value), seen next frame - on entering Charged, fire Ready."),
	FireEvent UMETA(DisplayName = "Fire Event", ToolTip = "Sends a named event out to Blueprint: the Widget Animation Component's On State Event, with Event Name set, and the Custom Event On <Machine> <Event Name> on the User Widget around it, if it has one.")
};

/** One thing a state does as it is entered or left, or a transition as it is taken. Run after the frame's transitions are settled. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimMachineAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	EWidgetAnimMachineActionKind Kind = EWidgetAnimMachineActionKind::SetInput;

	/** The input set or fired. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (GetOptions = "GetSettableInputOptions", EditCondition = "Kind != EWidgetAnimMachineActionKind::FireEvent", EditConditionHides))
	FName Input;

	/** A Bool's value - or, firing a Press And Hold, true to press it and false to let go. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (DisplayName = "Bool Value", EditCondition = "Kind != EWidgetAnimMachineActionKind::FireEvent", EditConditionHides))
	bool bBoolValue = true;

	/** A Float's or Integer's value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (DisplayName = "Number Value", EditCondition = "Kind == EWidgetAnimMachineActionKind::SetInput", EditConditionHides))
	float NumberValue = 0.f;

	/** A Choice's option, by name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (EditCondition = "Kind == EWidgetAnimMachineActionKind::SetInput", EditConditionHides))
	FName Option;

	/** A Vector 2D's value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (DisplayName = "Vector Value", EditCondition = "Kind == EWidgetAnimMachineActionKind::SetInput", EditConditionHides))
	FVector2D VectorValue = FVector2D::ZeroVector;

	/** The event's name, as Blueprint receives it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (EditCondition = "Kind == EWidgetAnimMachineActionKind::FireEvent", EditConditionHides))
	FName EventName;
};

/** One of a state's Random Variations: an animation, and how likely each start of the state plays it. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimMachineRandomVariation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation")
	TObjectPtr<UWidgetAnimPresetAsset> Animation = nullptr;

	/**
	 * How likely a start of the state plays this one, 0 - 1. The state's variations add up to 1 at most (kept so: over 1,
	 * they are scaled down together); what is left over plays the state's own Animation - 0.3 alone: 30% this, 70% the
	 * state's own; three at 0.25: 25% each, 25% the state's own.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variation", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Chance = 0.25f;
};

/** One animation of a blend state, and where it sits on the blend input. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimMachineBlendSample
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sample")
	TObjectPtr<UWidgetAnimPresetAsset> Animation = nullptr;

	/** Blend 1D: the input's value at which this sample plays alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sample")
	float Position = 0.f;

	/** Blend 2D: the point - the Vector 2D input's value - at which this sample plays alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sample", meta = (DisplayName = "Position 2D"))
	FVector2D Position2D = FVector2D::ZeroVector;
};

/**
 * A group of states collapsed into one box in the State Machine editor's graph - as a Blueprint collapses part of a graph into
 * a node - to keep a big graph tidy. Double-click it to open it. Only how the graph shows the states: they stay the layer's,
 * and play exactly as they would anywhere. Editor only.
 */
USTRUCT()
struct NAMETBD_API FWidgetAnimMachineStateGroup
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;

	UPROPERTY()
	FName Name;

	/** Where its box sits in the layer's graph while collapsed. */
	UPROPERTY()
	FVector2D Position = FVector2D::ZeroVector;

	/**
	 * Its box's size in the graph when its position was last kept, and the graph's zoom then - kept by the graph, with no
	 * undo step of its own, so an undo or a redo brings a position back with the size it went with and the node lands where
	 * it was (the graph keeps a node's middle in place as its size changes).
	 */
	UPROPERTY()
	FVector2D EditorSize = FVector2D::ZeroVector;

	UPROPERTY()
	float EditorSizeZoom = 0.f;

	/** Collapsed: its states are shown as its box. Expanded: as themselves, in the layer's graph. */
	UPROPERTY()
	bool bCollapsed = true;
};

/** One input of a machine: its name, what it holds, and - optionally - where it is read from with no code. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimMachineInput
{
	GENERATED_BODY()

	/** What conditions, the Set State Machine Input node and a View Model's Set Input name it by. Unique in the machine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EWidgetAnimMachineInputType Type = EWidgetAnimMachineInputType::Bool;

	/**
	 * The heading it is listed under in the State Machine editor's Inputs panel, as a Blueprint variable's Category groups it -
	 * "Movement", "Combat". Empty: listed first, with no heading. Typed here, or picked from the input's right-click menu in the
	 * panel (Move To Category). Only how the panel lists it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	FString Category;

	/** What it is until something sets it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimMachineInputType::Bool", EditConditionHides))
	bool bDefaultBool = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimMachineInputType::Number", EditConditionHides))
	float DefaultNumber = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimMachineInputType::Integer", EditConditionHides))
	int32 DefaultInteger = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimMachineInputType::Vector2D", EditConditionHides))
	FVector2D DefaultVector2D = FVector2D::ZeroVector;

	/** A Choice's options, by name. It starts at the first. Conditions ask Is / Is Not one of them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (EditCondition = "Type == EWidgetAnimMachineInputType::Choice", EditConditionHides))
	TArray<FName> Options;

	/** How long a Press And Hold has to be held to complete. Let go sooner, it doesn't. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Hold Time", ClampMin = "0.05", UIMin = "0.1", UIMax = "3", Units = "s", EditCondition = "Type == EWidgetAnimMachineInputType::Hold", EditConditionHides))
	float HoldTime = 1.f;

	/**
	 * Where the input is read from, every frame, with no code: a Value Input - the pointer over a widget, it being hovered,
	 * pressed or focused, a drag, a scroll. A Bool is true while the value is over 0.5; a Trigger fires as it rises past 0.5;
	 * a Float is the value, an Integer the value rounded, a Choice the option of that number; a Vector 2D takes both parts of
	 * the value (a Pointer Position or a Drag with Output X and Y); a Press And Hold is held while it is over 0.5 (None: while
	 * the widget is pressed). None: it is set from Blueprint or a View Model. Read at runtime only - in the editor, the
	 * Inputs panel sets it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Driven By", BaseStruct = "/Script/NameTBD.WidgetAnimInputSource", ExcludeBaseStruct))
	FInstancedStruct Source;

	/**
	 * Which of the machine's targets Driven By is measured on - a Layer Group name its animations share. None: the widget the
	 * machine plays on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Measured On", GetOptions = "GetTargetNameOptions"))
	FName MeasuredOn;

	/** What it is for, in words: shown wherever it is picked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (MultiLine = true))
	FString Description;
};

/** One test a transition makes on an input. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimMachineCondition
{
	GENERATED_BODY()

	/** What it tests: an input (the default), or which state another layer is in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (DisplayName = "Test"))
	EWidgetAnimMachineConditionSource Source = EWidgetAnimMachineConditionSource::Input;

	/** The input tested: one of the machine's, or a built-in one (Hovered, Pressed, Focused, Selected, Disabled, Closing, Clicked). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (GetOptions = "GetInputNameOptions"))
	FName Input;

	/** Another Layer's State: the layer, by name - any of the machine's but the transition's own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (GetOptions = "GetConditionLayerOptions"))
	FName Layer;

	/** Another Layer's State: what is asked of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (DisplayName = "Layer Test"))
	EWidgetAnimMachineLayerStateTest LayerTest = EWidgetAnimMachineLayerStateTest::IsIn;

	/** Another Layer's State: the state, by name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (GetOptions = "GetConditionStateOptions"))
	FName State;

	bool TestsLayerState() const { return Source == EWidgetAnimMachineConditionSource::LayerState; }

	/**
	 * What is asked of it - only what fits its type is offered in the editor: Is True / Is False for a Bool, Fired for a
	 * Trigger, the comparisons and Increased / Decreased / Changed for a Float or Integer, Is / Is Not / Changed for a Choice,
	 * Points and the comparisons for a Vector 2D, Is Held / Is Let Go / Completed for a Press And Hold.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EWidgetAnimMachineCompare Compare = EWidgetAnimMachineCompare::IsTrue;

	/** For a Vector 2D: the number compared - its Length, X or Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EWidgetAnimMachineVectorPart Part = EWidgetAnimMachineVectorPart::Length;

	/** For a Points condition: which way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EWidgetAnimMachineDirection Direction = EWidgetAnimMachineDirection::Right;

	/** For a Points condition: how long the Vector 2D has to be to point anywhere - a stick at rest, a drag just begun, don't. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (ClampMin = "0", UIMin = "0", UIMax = "1"))
	float DeadZone = 0.25f;

	/** A comparison against a value typed here, or against another input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (DisplayName = "Compare To"))
	EWidgetAnimMachineCompareTo CompareTo = EWidgetAnimMachineCompareTo::Value;

	/** What a number is compared against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	float Value = 0.f;

	/** What a Choice is compared against: one of its options. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	FName Option;

	/** Compared against another input: which - a number for a number, the same kind of Choice for a Choice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (DisplayName = "Other Input", GetOptions = "GetInputNameOptions"))
	FName OtherInput;

	/** The other input, a Vector 2D: the number compared - its Length, X or Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (DisplayName = "Other Part"))
	EWidgetAnimMachineVectorPart OtherPart = EWidgetAnimMachineVectorPart::Length;
};

/** An arrow between two nodes of a layer: when it is taken, and how the layer gets to where it points. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimMachineTransition
{
	GENERATED_BODY()

	FWidgetAnimMachineTransition();

	UPROPERTY()
	FGuid Id;

	/** The node it leaves - a state, Entry or Any State - and the one it goes to: a state or Exit. */
	UPROPERTY()
	FGuid From;

	UPROPERTY()
	FGuid To;

	/** Off, it is never taken - for trying the machine without it, without deleting it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition", meta = (DisplayName = "Enabled"))
	bool bEnabled = true;

	/** Every one must hold for the transition to be taken. None: it is taken as soon as Exit Time allows. A second arrow is an OR. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transition")
	TArray<FWidgetAnimMachineCondition> Conditions;

	/** How the layer gets from the pose it is in to the next state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend")
	EWidgetAnimMachineBlend Blend = EWidgetAnimMachineBlend::Tween;

	/** How long the blend takes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (ClampMin = "0", UIMax = "2", Units = "s", EditCondition = "Blend == EWidgetAnimMachineBlend::Tween", EditConditionHides))
	float Duration = 0.2f;

	/** The animation played on the way: a press squash, a pop between two states, Anim Out on the way to Exit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (EditCondition = "Blend == EWidgetAnimMachineBlend::Animation", EditConditionHides))
	TObjectPtr<UWidgetAnimPresetAsset> TransitionAnimation = nullptr;

	/** Ticked: the transition animation is played in Animation Duration, sped up or slowed down to fit. Unticked: it plays at its own length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (InlineEditConditionToggle))
	bool bOverrideAnimationDuration = false;

	/**
	 * How long the transition animation plays. Unticked, it is the animation's own length (at Animation Speed) - shown here
	 * once an animation is picked. Ticked, the animation is played in exactly this long. (Shown only for Play Transition
	 * Animation: the State Machine editor's Details lay it out.)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (DisplayName = "Animation Duration", ClampMin = "0.01", UIMax = "2", Units = "s", EditCondition = "bOverrideAnimationDuration"))
	float AnimationDuration = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (DisplayName = "Animation Speed", ClampMin = "0.01", UIMin = "0.1", UIMax = "4", EditCondition = "Blend == EWidgetAnimMachineBlend::Animation && !bOverrideAnimationDuration", EditConditionHides))
	float TransitionAnimationSpeed = 1.f;

	/** How long it takes to blend into the transition animation from the pose the widget is in, and out of it into the next state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (DisplayName = "Blend In And Out", ClampMin = "0", UIMax = "1", Units = "s", EditCondition = "Blend == EWidgetAnimMachineBlend::Animation", EditConditionHides))
	float AnimationBlendTime = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (EditCondition = "Blend == EWidgetAnimMachineBlend::Tween || Blend == EWidgetAnimMachineBlend::Animation", EditConditionHides))
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (EditCondition = "Easing == EWidgetAnimEasing::Custom && (Blend == EWidgetAnimMachineBlend::Tween || Blend == EWidgetAnimMachineBlend::Animation)", EditConditionHides))
	TObjectPtr<UCurveFloat> CustomCurve = nullptr;

	/**
	 * The spring every channel follows. Visual Duration is roughly how long it takes; Bounce how much it overshoots. Its fields
	 * are shown in Blend, and only while Blend is Spring - the editor's Details lay them out (see the State Machine editor).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blend", meta = (EditCondition = "Blend == EWidgetAnimMachineBlend::Spring", EditConditionHides))
	FWidgetSpringParams Spring;

	/** When it may be taken: whenever its conditions hold, or only after part - or all - of the state has played. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (DisplayName = "Exit Time"))
	EWidgetAnimMachineExitTime ExitTime = EWidgetAnimMachineExitTime::Immediately;

	/** How much of the state's animation has to have played first: 0.5 is halfway through its first pass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (DisplayName = "Exit At", ClampMin = "0", ClampMax = "1", EditCondition = "ExitTime == EWidgetAnimMachineExitTime::AtProgress", EditConditionHides))
	float ExitAtProgress = 0.5f;

	/**
	 * Whether another transition may cut in while this one plays - the next state's own, or Any State's - from the pose the
	 * widget has got to. Off: this one finishes first.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (DisplayName = "Interruptible"))
	bool bInterruptible = true;

	/**
	 * Random: when this one could be taken, one of the Random transitions leaving the same node whose conditions hold is taken
	 * instead, picked at random by their Random Weights - an idle with three fidgets. Off: taken in priority order, as ever.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (DisplayName = "Random"))
	bool bRandom = false;

	/** How often a Random transition is picked against the others: 2 is twice as often as 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (DisplayName = "Random Weight", ClampMin = "0", UIMax = "10", EditCondition = "bRandom", EditConditionHides))
	float RandomWeight = 1.f;

	/** What it does as it is taken: set an input, fire a trigger, send an event. See FWidgetAnimMachineAction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actions", meta = (DisplayName = "On Taken"))
	TArray<FWidgetAnimMachineAction> OnTaken;

	/**
	 * How long the whole transition takes, from leaving one state to being in the next: 0 for a Cut, Duration for a Tween,
	 * the spring's Visual Duration (it may run on a little as it settles), and for a transition animation its length plus the
	 * blend out of it. What the editor labels the transition with.
	 */
	float GetBlendSeconds() const;

	/** How long a Tween - or the blend into and out of a transition animation - takes. */
	float GetTweenSeconds() const;

	/** The transition animation's own length at speed 1, in seconds: 0 with none. */
	float GetTransitionAnimationLength() const;

	/** How long the transition animation plays: Animation Duration when overridden, else its length at Animation Speed. */
	float GetTransitionAnimationSeconds() const;

	/** The speed the transition animation is played at, so it takes GetTransitionAnimationSeconds. */
	float GetTransitionAnimationSpeed() const;

#if WITH_EDITORONLY_DATA
	/** Whether its transition node was dragged somewhere of its own in the graph: the wire then runs through it. */
	UPROPERTY()
	bool bEditorNodePlaced = false;

	/** Where it was dragged: its node's middle, from halfway between its two nodes' corners - so it follows them as they move. */
	UPROPERTY()
	FVector2D EditorNodeOffset = FVector2D::ZeroVector;

	/**
	 * The reroute points its wire runs through in the graph, in order from the node it leaves to the node it goes to - each
	 * measured, as EditorNodeOffset is, from halfway between its two nodes' corners, so they follow the nodes as they move.
	 * Added by double-clicking the wire. Only how the graph looks: nothing at runtime.
	 */
	UPROPERTY()
	TArray<FVector2D> EditorReroutePoints;

	/**
	 * Its wire drawn dead straight from state to state - corner to corner between states that lie on a slant - rather than
	 * curving out of and into them. Set by Straighten Connections (Q) on a wire between states on a slant, and from the wire's
	 * menu. Only how the graph looks: nothing at runtime.
	 */
	UPROPERTY()
	bool bEditorStraight = false;
#endif
};

/** A comment box on a layer's graph, as Blueprint graphs have: a note, and a frame round the nodes it is about. Editor only. */
USTRUCT()
struct NAMETBD_API FWidgetAnimMachineComment
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Id;

	UPROPERTY()
	FString Text;

	UPROPERTY()
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY()
	FVector2D Size = FVector2D(400.0, 200.0);

	UPROPERTY()
	FLinearColor Color = FLinearColor::White;

	UPROPERTY()
	int32 FontSize = 18;

	/** Moving the comment moves the nodes in it. */
	UPROPERTY()
	bool bGroupMovement = true;

	/** The comment bubble shown when zoomed out. */
	UPROPERTY()
	bool bShowBubbleWhenZoomed = true;

	UPROPERTY()
	bool bColorBubble = false;
};

/**
 * One parameter of a state's animation (see WidgetAnimParameters.h), set from one of the machine's inputs each time the state
 * starts: "Distance <- DragOffset". A Number, Integer or Press And Hold (its progress) gives a number; a Bool or a built-in
 * input true or false; a Choice its option, by name, to a Choice parameter (its option's number to any other); a Vector 2D
 * both its parts.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimMachineParameterBinding
{
	GENERATED_BODY()

	/** The parameter of the state's animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter", meta = (GetOptions = "GetStateParameterOptions"))
	FName Parameter;

	/** The input it takes its value from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter", meta = (GetOptions = "GetInputNameOptions"))
	FName Input;
};

/** One node of a layer: a state, or one of the special nodes. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimMachineState
{
	GENERATED_BODY()

	FWidgetAnimMachineState();

	UPROPERTY()
	FGuid Id;

	/** What it is called: in the graph, in conditions' events (On Button Entered Hovered), and to Is In State. Unique in its layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Properties", meta = (EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	FName Name;

	UPROPERTY()
	EWidgetAnimMachineNodeKind Kind = EWidgetAnimMachineNodeKind::State;

	/** Single Animation, or a Blend 1D / 2D of several by an input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Properties", meta = (DisplayName = "Plays", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	EWidgetAnimMachineBlendSpace BlendSpace = EWidgetAnimMachineBlendSpace::None;

	/** The animation the state plays: a Widget Animation, aimed through its Layer Groups at the machine's targets. None: the state poses nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play", meta = (EditCondition = "Kind == EWidgetAnimMachineNodeKind::State && BlendSpace == EWidgetAnimMachineBlendSpace::None", EditConditionHides))
	TObjectPtr<UWidgetAnimPresetAsset> Animation = nullptr;

	/**
	 * More animations to pick from, each with its Chance: each time the state starts it plays one of these by its chance, or
	 * else Animation - idle variety. The chances add up to 1 at most; what is left over is Animation's.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play", meta = (DisplayName = "Random Variations", TitleProperty = "Animation", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State && BlendSpace == EWidgetAnimMachineBlendSpace::None", EditConditionHides))
	TArray<FWidgetAnimMachineRandomVariation> RandomVariations;

	/** The Random Variations of StateMachine v1, with no chance each: moved into RandomVariations as the asset loads (Normalize). */
	UPROPERTY()
	TArray<TObjectPtr<UWidgetAnimPresetAsset>> RandomAnimations;

	/** The Random Variation a start of the state plays for a roll of 0 - 1, or Animation when the roll falls past them all. */
	UWidgetAnimPresetAsset* PickRandomAnimation(float Roll) const;

	/** Blend 1D: a Float, Integer or Press And Hold input. Blend 2D: a Vector 2D input. Where between the samples the state is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play", meta = (DisplayName = "Blend Input", GetOptions = "GetBlendInputOptions", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State && BlendSpace != EWidgetAnimMachineBlendSpace::None", EditConditionHides))
	FName BlendInput;

	/** The animations blended, each at its place on the Blend Input. Played together, at the state's speed and playback, and mixed by where the input is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play", meta = (DisplayName = "Blend Samples", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State && BlendSpace != EWidgetAnimMachineBlendSpace::None", EditConditionHides))
	TArray<FWidgetAnimMachineBlendSample> BlendSamples;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Properties", meta = (EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	EWidgetAnimMachineStatePlayback Playback = EWidgetAnimMachineStatePlayback::Once;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Properties", meta = (ClampMin = "0.01", UIMin = "0.1", UIMax = "4", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	float PlaybackSpeed = 1.f;

	/** A Float input the state's speed follows, as it plays - multiplying Playback Speed (held at 0 or more). None: Playback Speed alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play", meta = (DisplayName = "Speed Input", GetOptions = "GetNumberInputOptions", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	FName SpeedInput;

	/**
	 * For a percentage animation (Time In Percent): the Number input its progress follows, 0 for 0% and 1 for 100% - a charge
	 * meter - or a Press And Hold, filling as it is held: a hold ring. None: it plays from 0% to 100% over one second.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play", meta = (DisplayName = "Progress Input", GetOptions = "GetNumberInputOptions", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	FName ProgressInput;

	/**
	 * Parameters of the animation set from the machine's inputs, each time the state starts - a slide as far as the drag went,
	 * a flash in the color a Choice picks. Read as the state starts: a change while it plays shows the next time it starts. A
	 * value set on the widget with Set Widget Animation Parameter wins over these. See FWidgetAnimMachineParameterBinding.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Play", meta = (DisplayName = "Parameters From Inputs", TitleProperty = "Parameter", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	TArray<FWidgetAnimMachineParameterBinding> ParameterInputs;

	/** What it does as it is entered: set an input, fire a trigger, send an event - run once the frame's transitions are settled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actions", meta = (DisplayName = "On Enter", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	TArray<FWidgetAnimMachineAction> OnEnter;

	/** What it does as it is left. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actions", meta = (DisplayName = "On Exit", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	TArray<FWidgetAnimMachineAction> OnExit;

	/** Whether it blends several animations - see BlendSpace. */
	bool IsBlendState() const { return Kind == EWidgetAnimMachineNodeKind::State && BlendSpace != EWidgetAnimMachineBlendSpace::None; }

	/** Whether it has any action to run. */
	bool HasActions() const { return OnEnter.Num() > 0 || OnExit.Num() > 0; }

#if WITH_EDITORONLY_DATA
	/** Where the node sits in its layer's graph. */
	UPROPERTY()
	FVector2D EditorPosition = FVector2D::ZeroVector;

	/**
	 * The node's size in the graph when its position was last kept, and the graph's zoom then - kept by the graph, with no
	 * undo step of its own, so an undo or a redo brings a position back with the size it went with and the node lands where
	 * it was (the graph keeps a node's middle in place as its size changes).
	 */
	UPROPERTY()
	FVector2D EditorSize = FVector2D::ZeroVector;

	UPROPERTY()
	float EditorSizeZoom = 0.f;

	/** A note, shown in a bubble over the node - as a Blueprint node's comment is. */
	UPROPERTY(EditAnywhere, Category = "State Properties", meta = (MultiLine = true, EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	FString Comment;

	/** Whether the comment's bubble is shown. */
	UPROPERTY()
	bool bCommentBubbleVisible = true;

	/** Whether the node wears Node Color instead of its own (dark gray for a state, green Entry, red Exit, blue Any State). */
	UPROPERTY(EditAnywhere, Category = "State Properties", meta = (InlineEditConditionToggle))
	bool bOverrideNodeColor = false;

	/**
	 * The node's color in the graph - to group states by what they are for. Only how the graph looks: nothing at runtime. While
	 * the watched widget is in the state it is lit brighter in this color, and its animation's progress fills it in it.
	 */
	UPROPERTY(EditAnywhere, Category = "State Properties", meta = (EditCondition = "bOverrideNodeColor", DisplayName = "Node Color", HideAlphaChannel))
	FLinearColor NodeColor = FLinearColor(0.10f, 0.22f, 0.40f);

	/** The collapsed group it is shown in, in the graph - see FWidgetAnimMachineStateGroup. Invalid: none. */
	UPROPERTY()
	FGuid EditorGroup;
#endif

	bool IsSpecial() const { return Kind != EWidgetAnimMachineNodeKind::State; }
};

/** One layer of a machine: a graph of states and the transitions between them, running alongside the other layers. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimMachineLayer
{
	GENERATED_BODY()

	/** Its name: the graph's tab, and what Current State Of Layer asks by. Unique in the machine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	FName Name;

	/** How it combines with the layers before it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (DisplayName = "Blend Mode"))
	EWidgetAnimMachineLayerBlend BlendMode = EWidgetAnimMachineLayerBlend::Override;

	/** How strongly it applies: 0 not at all, 1 fully. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0", ClampMax = "1"))
	float Weight = 1.f;

	/** A Number input its weight follows instead, held between 0 and 1. None: Weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (DisplayName = "Weight Input", GetOptions = "GetNumberInputOptions"))
	FName WeightInput;

	/**
	 * Which of the machine's targets the layer may animate: All (the default), Only These, or All But These. What its states
	 * animate on a target left out is dropped before the layer is blended, so the layers under it keep it - two Override layers
	 * on one widget no longer have to be ordered just so.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (DisplayName = "Affects"))
	EWidgetAnimMachineLayerMask MaskMode = EWidgetAnimMachineLayerMask::All;

	/** The targets Affects names - the Layer Group names of the machine's animations; (Widget) for the widget it plays on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (DisplayName = "Targets", GetOptions = "GetMaskTargetOptions", EditCondition = "MaskMode != EWidgetAnimMachineLayerMask::All", EditConditionHides))
	TArray<FName> MaskTargets;

	/** Its nodes: Entry, Any State and Exit, and its states. Edited in the graph. */
	UPROPERTY()
	TArray<FWidgetAnimMachineState> States;

	/** Its arrows, in priority order: of two that could be taken from one node, the earlier is. Edited in the graph. */
	UPROPERTY()
	TArray<FWidgetAnimMachineTransition> Transitions;

#if WITH_EDITORONLY_DATA
	/** The comment boxes on its graph. */
	UPROPERTY()
	TArray<FWidgetAnimMachineComment> Comments;

	/** Its collapsed groups of states, in the graph. See FWidgetAnimMachineStateGroup. */
	UPROPERTY()
	TArray<FWidgetAnimMachineStateGroup> EditorGroups;
#endif

	int32 FindStateIndex(const FGuid& Id) const;
	int32 FindStateIndexByName(FName StateName) const;
	int32 FindSpecialIndex(EWidgetAnimMachineNodeKind Kind) const;
	int32 FindTransitionIndex(const FGuid& Id) const;
	const FWidgetAnimMachineState* FindState(const FGuid& Id) const;

	/** Whether any transition goes to Exit: whether this layer has an exit to play as the widget closes. */
	bool HasExitPath() const;
};

/**
 * A Widget Animation State Machine (WASM_): states, the transitions between them and the inputs they test, in layers.
 * Reusable: played by a Widget Animation Component's State Machines, which maps the machine's targets - the Layer Groups
 * of the animations it plays, matched by name - onto its own widgets. Edited in the Widget Animation State Machine editor:
 * the status bar drawer of any Widget Blueprint editor, or a double-click on the asset. See the note at the top of
 * WidgetAnimStateMachine.h.
 *
 * The WASM_ prefix is a working name, like the plugin's; it changes when the plugin is named.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Widget Animation State Machine"))
class NAMETBD_API UWidgetAnimStateMachineAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	/** What the machine is for, in words. Shown as the tooltip wherever it is picked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State Machine", meta = (MultiLine = true))
	FText Description;

	/** Its public interface: what conditions test, set from Blueprint, a View Model or a Value Input. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State Machine", meta = (TitleProperty = "Name"))
	TArray<FWidgetAnimMachineInput> Inputs;

	/** Its layers, first to last: later layers win where two Override layers animate the same channel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "State Machine", meta = (TitleProperty = "Name"))
	TArray<FWidgetAnimMachineLayer> Layers;

#if WITH_EDITORONLY_DATA
	/**
	 * Where the State Machine editor previews the machine in a designer that has no Widget Animation Component playing it: the
	 * widget it plays on, as a path into the Widget Blueprint (None: the Widget Blueprint itself), and a widget for each of
	 * its targets - by default the Preview Target of the animation group the target is named for. Picked in the editor's
	 * Preview Targets.
	 */
	UPROPERTY()
	FName PreviewHostPath;

	UPROPERTY()
	TMap<FName, FName> PreviewTargetPaths;
#endif

	/** The machine's name in events and messages: the asset's name without WASM_ (WASM_Button is Button). */
	FString GetMachineDisplayName() const;

	const FWidgetAnimMachineInput* FindInput(FName InputName) const;

	int32 FindLayerIndex(FName LayerName) const;

	/**
	 * The targets the machine's animations aim at: the Layer Group names of every animation its states and transitions play,
	 * each once, in the order first found. A use of the machine picks a widget for each.
	 */
	void GetTargetNames(TArray<FName>& OutNames) const;

	/** Where a use starts a target: the Preview Target of the first group with that name. */
	FName GetTargetDefault(FName TargetName) const;

	/** Every animation the machine plays, each once. */
	void GatherAnimations(TArray<UWidgetAnimPresetAsset*>& OutAnimations) const;

	/** Whether any layer has a way to Exit: whether the widget has an exit to wait for when the machine closes. */
	bool HasExitPath() const;

	/** Whether any condition or weight reads this input. */
	bool UsesInput(FName InputName) const;

	/**
	 * Gives every layer its Entry, Any State and Exit, every node and transition an id, and every layer, state and input a
	 * name of its own; drops transitions whose ends are gone. Run on load and on every edit. True if anything changed.
	 */
	bool Normalize();

	/** BaseName, or BaseName with a number after it, unused by any state of Layer. */
	static FName MakeUniqueStateName(const FWidgetAnimMachineLayer& Layer, FName BaseName, const FGuid& Except = FGuid());
	FName MakeUniqueLayerName(FName BaseName, int32 ExceptIndex = INDEX_NONE) const;
	FName MakeUniqueInputName(FName BaseName, int32 ExceptIndex = INDEX_NONE) const;

	/** A layer with its Entry, Any State and Exit placed, and nothing else. */
	static FWidgetAnimMachineLayer MakeLayer(FName LayerName);

	/** Backs the Measured On dropdown: (Widget) - the widget the machine plays on - then every target. */
	UFUNCTION()
	TArray<FString> GetTargetNameOptions() const;

	/** Backs the Weight Input and Progress Input dropdowns: None, then every Number and Press And Hold input. */
	UFUNCTION()
	TArray<FString> GetNumberInputOptions() const;

	/** Backs a condition's Input dropdown: every input, then the built-in ones. */
	UFUNCTION()
	TArray<FString> GetInputNameOptions() const;

	/** Backs a state's Parameters From Inputs dropdown: the parameters of every state's animation. */
	UFUNCTION()
	TArray<FString> GetStateParameterOptions() const;

	/** Backs a condition's Layer dropdown: every layer. (The State Machine editor leaves out the transition's own.) */
	UFUNCTION()
	TArray<FString> GetConditionLayerOptions() const;

	/** Backs a condition's State dropdown: every state of every layer. (The State Machine editor lists the picked layer's.) */
	UFUNCTION()
	TArray<FString> GetConditionStateOptions() const;

	/** Backs a layer's Targets dropdown: (Widget), then every target. */
	UFUNCTION()
	TArray<FString> GetMaskTargetOptions() const;

	/** The categories the machine's inputs are in, sorted - for the Inputs panel's Move To Category. */
	TArray<FString> GetInputCategoryOptions() const;

	/** Backs a blend state's Blend Input: every Float, Integer, Press And Hold and Vector 2D input. */
	UFUNCTION()
	TArray<FString> GetBlendInputOptions() const;

	/** Backs an action's Input: every input but the built-in ones. */
	UFUNCTION()
	TArray<FString> GetSettableInputOptions() const;

	/** What a layer's Targets name the widget the machine plays on: "(Widget)". */
	static const FName MaskHostTarget;

	/**
	 * After a layer or state is renamed: every condition naming it by its old name follows it. Layer None renames a layer;
	 * otherwise a state of that layer.
	 */
	void RenameLayerStateReferences(FName LayerName, FName OldName, FName NewName);

	virtual void PostLoad() override;
	virtual void PostInitProperties() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;

	/**
	 * Warns of what names something the machine doesn't have: a condition on a layer or state that is gone, a state's
	 * Parameters From Inputs naming a parameter none of its animations has. Each is simply false / ignored when it plays.
	 */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;

	/** Anything about a machine changed - an edit, an undo. The editor and the components playing it in the designer listen. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMachineChanged, UWidgetAnimStateMachineAsset* /*Machine*/);
	static FOnMachineChanged& OnMachineChanged();

	/** Tells OnMachineChanged. */
	void NotifyMachineChanged();
#endif
};

// =====================================================================================================
// Built-in inputs
// =====================================================================================================

/** The inputs every machine has without declaring them, read from the widget it plays on. */
namespace WidgetAnimMachineInputs
{
	NAMETBD_API extern const FName Hovered;
	NAMETBD_API extern const FName Pressed;
	NAMETBD_API extern const FName Focused;
	NAMETBD_API extern const FName Selected;
	NAMETBD_API extern const FName Disabled;

	/** True while the widget plays out - Play Out, or hidden with Set Visibility Animated - and false again as it plays in. */
	NAMETBD_API extern const FName Closing;

	/**
	 * Fires the frame the widget is clicked: a Context Menu Button's action going off (on release or on press, as it is set,
	 * and on every repeat while a Hold button is held); anything else let go while hovered or focused after being pressed.
	 * A pulse, as a Trigger is.
	 */
	NAMETBD_API extern const FName Clicked;

	/** All seven, in that order. */
	NAMETBD_API const TArray<FName>& GetBuiltIns();

	/** Which bit of a built-in mask Name is, or 0 for an input that isn't built in. */
	NAMETBD_API uint8 GetBuiltInBit(FName Name);

	NAMETBD_API bool IsBuiltIn(FName Name);

	/** A line about a built-in input, for its tooltip. */
	NAMETBD_API FText GetBuiltInDescription(FName Name);
}

/** What the built-in inputs read off a widget. */
struct NAMETBD_API FWidgetAnimMachineWidgetState
{
	bool bHovered = false;
	bool bPressed = false;
	bool bFocused = false;
	bool bSelected = false;
	bool bDisabled = false;

	/** Clicked this frame - a pulse. A provider that knows sets it; otherwise it is a press let go over the widget. */
	bool bClicked = false;
};

/**
 * Where the built-in inputs come from. The core reads a plain widget - Slate's hover and focus, a Button's pressed state, a
 * Check Box's checked state as Selected, the widget being enabled. A module that knows better registers a provider: the
 * Common UI module reads a Common Button's own hovered, pressed, selected and disabled states, so a machine on one needs no
 * setup at all. The first provider that answers wins.
 */
namespace WidgetAnimMachineWidgetStates
{
	/** Answers for Widget - the widget a machine plays on - and returns true, or returns false to leave it to the next. */
	using FProvider = TFunction<bool(UWidget* /*Widget*/, FWidgetAnimMachineWidgetState& /*OutState*/)>;

	/** Registers a provider; returns a handle for Unregister. */
	NAMETBD_API int32 RegisterProvider(FProvider Provider);
	NAMETBD_API void UnregisterProvider(int32 Handle);

	/** Asks the providers about Widget. False when none answered. */
	NAMETBD_API bool ReadFromProviders(UWidget* Widget, FWidgetAnimMachineWidgetState& OutState);

	/**
	 * Selection from outside the widget: something that selects whole widgets - a Flow List selecting its items - says
	 * whether Widget is in the one selected. Asked only when the widget's own state doesn't say Selected, so a button inside
	 * a Flow List's selected item reads Selected with no setup at all. Answers true when any provider does.
	 */
	using FSelectedProvider = TFunction<bool(UWidget* /*Widget*/)>;

	NAMETBD_API int32 RegisterSelectedProvider(FSelectedProvider Provider);
	NAMETBD_API void UnregisterSelectedProvider(int32 Handle);
	NAMETBD_API bool IsSelectedByContainer(UWidget* Widget);
}

// =====================================================================================================
// Events
// =====================================================================================================

/** What a state event hands over: which machine, which layer and state, and where it came from or goes. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimStateEventInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UWidgetAnimStateMachineAsset> Machine = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FName Layer;

	/** The state entered or exited. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FName State;

	/** Entered: the state it came from. Exited: the state it is going to. None for Entry and Exit. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FName OtherState;

	/** True for Entered, false for Exited. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bEntered = true;

	/** A Fire Event action's Event Name - None for a state entered or exited. State is then the state the action is on (None for a transition's). */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FName EventName;

	/** The widget the machine plays on. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UWidget> Host = nullptr;
};

namespace WidgetAnimMachineEvents
{
	/**
	 * The Custom Event a state's event calls - "On Button Entered Hovered" for Hovered in WASM_Button - on the nearest User
	 * Widget around the widget playing the machine that has one. Added from the + beside it in the Widget Animation
	 * Component's Details. None for a state with no name.
	 */
	NAMETBD_API FName GetStateEventFunctionName(const UWidgetAnimStateMachineAsset* Machine, FName State, bool bEntered);

	/** The Custom Event a Fire Event action calls - "On Button Charged Ready" for Charged Ready in WASM_Button. None for no name. */
	NAMETBD_API FName GetActionEventFunctionName(const UWidgetAnimStateMachineAsset* Machine, FName EventName);
}

// =====================================================================================================
// Use on a component
// =====================================================================================================

/** A use's own setting for one input: where it is read from on this widget, and what it starts at. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimMachineInputOverride
{
	GENERATED_BODY()

	/** The input, by name. Listed for you from the machine. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input")
	FName Input;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Input")
	EWidgetAnimMachineInputType Type = EWidgetAnimMachineInputType::Bool;

	/** Tick to give this widget its own settings for the input, in place of the machine's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Override"))
	bool bOverride = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Default", EditCondition = "bOverride && Type == EWidgetAnimMachineInputType::Bool", EditConditionHides))
	bool bDefaultBool = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Default", EditCondition = "bOverride && Type == EWidgetAnimMachineInputType::Number", EditConditionHides))
	float DefaultNumber = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Default", EditCondition = "bOverride && Type == EWidgetAnimMachineInputType::Integer", EditConditionHides))
	int32 DefaultInteger = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Default", EditCondition = "bOverride && Type == EWidgetAnimMachineInputType::Vector2D", EditConditionHides))
	FVector2D DefaultVector2D = FVector2D::ZeroVector;

	/** How long this widget's Press And Hold has to be held. See FWidgetAnimMachineInput::HoldTime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Hold Time", ClampMin = "0.05", UIMin = "0.1", UIMax = "3", Units = "s", EditCondition = "bOverride && Type == EWidgetAnimMachineInputType::Hold", EditConditionHides))
	float HoldTime = 1.f;

	/** Where it is read from on this widget. See FWidgetAnimMachineInput::Source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Driven By", EditCondition = "bOverride", BaseStruct = "/Script/NameTBD.WidgetAnimInputSource", ExcludeBaseStruct))
	FInstancedStruct Source;

	/** The widget Driven By is measured on. (This Widget) - or None - the widget the machine plays on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Measured On Widget", EditCondition = "bOverride", GetOptions = "GetAnimTargetWidgetOptions"))
	FName MeasuredOnWidget;
};

/** A state machine played by a Widget Animation Component, and where its targets are on this widget. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimStateMachineEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "State Machine"))
	TObjectPtr<UWidgetAnimStateMachineAsset> StateMachine = nullptr;

	/**
	 * A widget for each of the machine's targets - the Layer Group names of its animations - labeled with its name. Sized to
	 * the machine whenever the component is edited or loaded. A new row starts on the group's Preview Target.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Target Widgets", EditFixedSize, TitleProperty = "GroupName"))
	TArray<FWidgetAnimGroupTarget> Targets;

	/** This widget's own settings for the machine's inputs, one row per input. Untick a row and the machine's are used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Inputs", EditFixedSize, TitleProperty = "Input"))
	TArray<FWidgetAnimMachineInputOverride> InputOverrides;

	/** Starts the machine as the widget is constructed. Off: it starts with Play In, or Start State Machines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Start On Construct"))
	bool bStartOnConstruct = true;

	/** Sizes Targets and InputOverrides to the machine and refreshes their labels. True if anything changed. See FWidgetAnimEntry::SyncGroupTargets. */
	bool SyncToMachine(const TArray<FString>* TargetOptions = nullptr);

	/** The widget path target TargetName is on: its row if it has one, else the default a new row would get. None for the widget the machine plays on. */
	FName GetTargetPath(FName TargetName) const;

	/** The row overriding InputName, or null when it isn't overridden. */
	const FWidgetAnimMachineInputOverride* FindInputOverride(FName InputName) const;
};
