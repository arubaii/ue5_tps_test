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

	Number UMETA(ToolTip = "Any number: a charge level, a count, a progress for a percentage animation, a layer's weight."),

	Trigger UMETA(ToolTip = "A one-frame pulse: Clicked, Rewarded, Error. Fired, it is true for the next frame the machine runs, then false again."),

	Hold UMETA(DisplayName = "Press And Hold", ToolTip = "Press and hold: Is True while the widget is held down, until it has been held for Hold Time - then it is Fired, once, as a Trigger is. Let go before that, it is false again: the hold didn't complete, and the state it held blends back. As a number it is how far through Hold Time the hold is, 0 - 1: a state's Progress Input can follow it. Held by the widget's own pressed state (the built-in Pressed), by Driven By, or from Blueprint (Set State Machine Input: true to press, false to let go).")
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
	IsFalse UMETA(DisplayName = "Is False", ToolTip = "A Bool is false. A Press And Hold isn't being held: let go before its Hold Time, or never pressed - never the frame it completes."),
	Fired UMETA(ToolTip = "A Trigger was fired since the machine last ran. A Press And Hold has just been held for its whole Hold Time."),
	Equal UMETA(DisplayName = "=="),
	NotEqual UMETA(DisplayName = "!="),
	Less UMETA(DisplayName = "<"),
	LessOrEqual UMETA(DisplayName = "<="),
	Greater UMETA(DisplayName = ">"),
	GreaterOrEqual UMETA(DisplayName = ">=")
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

	/** What it is until something sets it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimMachineInputType::Bool", EditConditionHides))
	bool bDefaultBool = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimMachineInputType::Number", EditConditionHides))
	float DefaultNumber = 0.f;

	/** How long a Press And Hold has to be held to complete. Let go sooner, it doesn't. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Hold Time", ClampMin = "0.05", UIMin = "0.1", UIMax = "3", Units = "s", EditCondition = "Type == EWidgetAnimMachineInputType::Hold", EditConditionHides))
	float HoldTime = 1.f;

	/**
	 * Where the input is read from, every frame, with no code: a Value Input - the pointer over a widget, it being hovered,
	 * pressed or focused, a drag, a scroll. A Bool is true while the value is over 0.5; a Trigger fires as it rises past 0.5;
	 * a Number is the value; a Press And Hold is held while it is over 0.5 (None: while the widget is pressed). None: it is
	 * set from Blueprint or a View Model. Read at runtime only - in the editor, the Inputs panel sets it.
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

	/** The input tested: one of the machine's, or a built-in one (Hovered, Pressed, Focused, Selected, Disabled, Closing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (GetOptions = "GetInputNameOptions"))
	FName Input;

	/** Is True / Is False for a Bool, Fired for a Trigger, a comparison for a Number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EWidgetAnimMachineCompare Compare = EWidgetAnimMachineCompare::IsTrue;

	/** What a Number is compared against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (EditCondition = "Compare != EWidgetAnimMachineCompare::IsTrue && Compare != EWidgetAnimMachineCompare::IsFalse && Compare != EWidgetAnimMachineCompare::Fired", EditConditionHides))
	float Value = 0.f;
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

/** One node of a layer: a state, or one of the special nodes. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimMachineState
{
	GENERATED_BODY()

	FWidgetAnimMachineState();

	UPROPERTY()
	FGuid Id;

	/** What it is called: in the graph, in conditions' events (On Button Entered Hovered), and to Is In State. Unique in its layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	FName Name;

	UPROPERTY()
	EWidgetAnimMachineNodeKind Kind = EWidgetAnimMachineNodeKind::State;

	/** The animation the state plays: a Widget Animation, aimed through its Layer Groups at the machine's targets. None: the state poses nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	TObjectPtr<UWidgetAnimPresetAsset> Animation = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	EWidgetAnimMachineStatePlayback Playback = EWidgetAnimMachineStatePlayback::Once;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (ClampMin = "0.01", UIMin = "0.1", UIMax = "4", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	float PlaybackSpeed = 1.f;

	/**
	 * For a percentage animation (Time In Percent): the Number input its progress follows, 0 for 0% and 1 for 100% - a charge
	 * meter - or a Press And Hold, filling as it is held: a hold ring. None: it plays from 0% to 100% over one second.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (DisplayName = "Progress Input", GetOptions = "GetNumberInputOptions", EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	FName ProgressInput;

#if WITH_EDITORONLY_DATA
	/** Where the node sits in its layer's graph. */
	UPROPERTY()
	FVector2D EditorPosition = FVector2D::ZeroVector;

	/** A note, shown in a bubble over the node - as a Blueprint node's comment is. */
	UPROPERTY(EditAnywhere, Category = "State", meta = (MultiLine = true, EditCondition = "Kind == EWidgetAnimMachineNodeKind::State", EditConditionHides))
	FString Comment;

	/** Whether the comment's bubble is shown. */
	UPROPERTY()
	bool bCommentBubbleVisible = true;

	/** Whether the node wears Node Color instead of its own (dark gray for a state, green Entry, red Exit, blue Any State). */
	UPROPERTY(EditAnywhere, Category = "State", meta = (InlineEditConditionToggle))
	bool bOverrideNodeColor = false;

	/**
	 * The node's color in the graph - to group states by what they are for. Only how the graph looks: nothing at runtime. While
	 * the watched widget is in the state it is lit brighter in this color, and its animation's progress fills it in it.
	 */
	UPROPERTY(EditAnywhere, Category = "State", meta = (EditCondition = "bOverrideNodeColor", DisplayName = "Node Color", HideAlphaChannel))
	FLinearColor NodeColor = FLinearColor(0.10f, 0.22f, 0.40f);
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

	virtual void PostLoad() override;
	virtual void PostInitProperties() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;

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

	/** All six, in that order. */
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
