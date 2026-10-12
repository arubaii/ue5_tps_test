// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "WidgetAnimLibrary.h"
#include "WidgetAnimStateMachine.h"
#include "WidgetAnimInput.h"

#include "WidgetAnimStateMachineRuntime.generated.h"

class UWidget;

// =====================================================================================================
// Running a state machine
// =====================================================================================================
//
// One FWidgetAnimMachineInstance per machine a Widget Animation Component plays. Each of its layers runs its own track
// player - the state it is in, and a transition animation on the way somewhere - and keeps the pose it showed last frame.
// That pose is what a transition starts from: taken as the transition starts, and blended out of as the next state is
// blended in, so a transition interrupted in the middle starts the next one from wherever the widget had got to.
//
// A layer's pose is a FWidgetAnimResult, with which channels of each widget it owns (FWidgetAnimMachinePose::Masks): an
// Override layer covers the layers under it only where it animates something. The layers fold into the component's frame,
// with Anim In, Anim Out and the animations played by Anim ID, through WidgetAnimStateMachines::Compose - one result,
// applied once.
//
// Idle costs nothing much: a layer whose state has settled, with nothing blending and no value or progress moving, keeps
// the pose it worked out last time instead of working it out again. The inputs are still read - that is what tells it
// when to move on.

/** The channels of one widget, as bits - what a layer's pose says it owns. */
namespace WidgetAnimMachineChannels
{
	constexpr uint8 Opacity = 1 << 0;
	constexpr uint8 Scale = 1 << 1;
	constexpr uint8 Translation = 1 << 2;
	constexpr uint8 Shear = 1 << 3;
	constexpr uint8 Angle = 1 << 4;
	constexpr uint8 All = Opacity | Scale | Translation | Shear | Angle;

	/** The channels a setting writes on its widget: a Fade its opacity, a Shake its translation and angle. 0 for one that writes none. */
	NAMETBD_API uint8 GetChannels(const FWidgetAnimSettings& Settings);
}

/** A layer's pose: what it writes, and which channels of each widget are its. */
USTRUCT()
struct NAMETBD_API FWidgetAnimMachinePose
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FWidgetAnimResult Result;

	/** One per Result.TargetedTransforms: which of that widget's channels the layer animates. */
	UPROPERTY(Transient)
	TArray<uint8> Masks;

	void Reset()
	{
		Result = FWidgetAnimResult();
		Masks.Reset();
	}

	bool IsEmpty() const
	{
		return Result.TargetedTransforms.Num() == 0 && Result.ScalarParameters.Num() == 0 && Result.VectorParameters.Num() == 0
			&& Result.ShapeTargets.Num() == 0 && Result.AuthoritativeWrites.Num() == 0;
	}

	/** Copies in the settings the registered writes point at, so the pose outlives the tracks it was worked out from. */
	void DetachParams();

	/** The entry for Widget's Part - added, at rest and owning nothing, when there is none. */
	int32 FindOrAddTransform(UWidget* Widget, const FWidgetAnimPartKey& Part);
	int32 FindTransform(const UWidget* Widget, const FWidgetAnimPartKey& Part) const;
};

/** One channel value of one widget following a spring - see EWidgetAnimMachineBlend::Spring. */
struct FWidgetAnimMachineSpringChannel
{
	TWeakObjectPtr<UWidget> Widget;
	FWidgetAnimPartKey Part;

	/** 0 opacity, 1 - 2 scale, 3 - 4 translation, 5 - 6 shear, 7 angle. */
	uint8 Component = 0;
	FWidgetSpringState State;
	bool bAtRest = false;
};

/** Where a layer is with its transition. */
enum class EWidgetAnimMachineBlendPhase : uint8
{
	/** In a state, nothing blending. */
	None,

	/** Blending from the pose it was in to the state it is going to. */
	Blending,

	/** Playing a transition animation, blended in from the pose it was in. */
	TransitionAnimation,

	/** The transition animation is done: blending out of it into the state. */
	BlendingAfterAnimation
};

/** One layer of a running machine. */
USTRUCT()
struct NAMETBD_API FWidgetAnimMachineLayerInstance
{
	GENERATED_BODY()

	/** The state's track, and a transition animation's while one plays. */
	UPROPERTY(Transient)
	FWidgetAnimPlayer Player;

	/** A blend state's samples, one player each, in the state's Blend Samples order - empty for any other state. */
	UPROPERTY(Transient)
	TArray<FWidgetAnimPlayer> BlendPlayers;

	/**
	 * The animation the state's track plays - its own, or the Random Variation picked as it started - so its parameters can be
	 * worked out again while it plays (see WidgetAnimStateMachines::RefreshParameters). Null for a blend state.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UWidgetAnimPresetAsset> PlayedAnimation = nullptr;

	/**
	 * The values the state's Parameters From Inputs gave its track as it was last built - one list, or one per blend sample -
	 * what a change of its inputs is compared with.
	 */
	TArray<TArray<FWidgetAnimParameterValue>> PlayedParameterInputs;

	/** How much of each sample the blend state shows now, summing to 1. */
	TArray<float> BlendWeights;

	/** Where the blend input put it last: the 1D value in X, or the 2D point. */
	FVector2f BlendPoint = FVector2f::ZeroVector;

	/** What the state's Speed Input multiplies its speed by now. */
	float SpeedScale = 1.f;

	/** The last few actions it ran, in words - for the editor's debugging. */
	TArray<FString> LastActions;

	/** The pose the running transition started from. */
	UPROPERTY(Transient)
	FWidgetAnimMachinePose FromPose;

	/** The pose the layer showed on its last frame. */
	UPROPERTY(Transient)
	FWidgetAnimMachinePose Output;

	/** The state it is in - or, while a transition runs, the one it is going to. An index into its layer's States. */
	int32 Current = INDEX_NONE;

	/** The state the running transition left. */
	int32 Previous = INDEX_NONE;

	/** The transition running, as an index into its layer's Transitions; INDEX_NONE when none is. */
	int32 Transition = INDEX_NONE;

	EWidgetAnimMachineBlendPhase Phase = EWidgetAnimMachineBlendPhase::None;
	float BlendElapsed = 0.f;

	/** How long the running transition's animation plays, in seconds - for the editor's progress read-out. */
	float TransitionAnimationSeconds = 0.f;

	/** Seconds since the state it is in was entered, at the state's own speed, and how long one pass of its animation is. */
	float TimeInState = 0.f;
	float PassSeconds = 0.f;

	/** The state's track has played its first pass through - see EWidgetAnimMachineExitTime::WhenFinished. */
	bool bStatePlayed = false;

	/** Whether the state's track has been started - a transition animation plays before it. */
	bool bStateTrackStarted = false;

	/** Whether a Pose state's track has been played to its end pose already - done once per state, not every frame. */
	bool bPoseSettled = false;

	/** Reached Exit, and done getting there. */
	bool bExited = false;

	/** Nothing moved last frame: its Output can be shown again as it is. */
	bool bSettled = false;

	/** The weight it applied with last frame. */
	float Weight = 1.f;

	/** How long it has waited, closing, for a way to Exit - see TickLayer. */
	float ClosingWait = 0.f;

	/** The progress last sent to a percentage state's track, so an unchanged one isn't sent again. */
	float LastProgress = 0.f;
	bool bHasProgress = false;

	TArray<FWidgetAnimMachineSpringChannel> Springs;
};

/** One input's value in a running machine, and how it is read. */
struct FWidgetAnimMachineInputState
{
	FName Name;
	EWidgetAnimMachineInputType Type = EWidgetAnimMachineInputType::Bool;

	bool bValue = false;

	/** A Float's or Integer's value (an Integer's whole), a Choice's option as its index in Options. */
	float Number = 0.f;

	/** A Vector 2D's value. */
	FVector2f Vector = FVector2f::ZeroVector;

	/** A Choice's options, from the machine. */
	TArray<FName> Options;

	/** The value as the machine last ran - what Increased, Decreased and Changed compare against. */
	float PreviousNumber = 0.f;
	FVector2f PreviousVector = FVector2f::ZeroVector;

	/** A Trigger fired since the machine last ran. */
	bool bFired = false;

	/** Where it is read from, with its runtime state - this use's copy. Invalid when it is set from outside. */
	FInstancedStruct Source;

	/** The widget path Source is measured on; None for the widget the machine plays on. */
	FName MeasuredOnPath;

	/** The last value Source gave, for a Trigger's rising edge. */
	float LastSample = 0.f;
	bool bHasSample = false;

	// A Press And Hold: bValue is "being held, not yet complete", Number how far through HoldTime, bFired the frame it
	// completes, bLetGo the frame it is let go - worked out each Tick from whether it is held down (see UpdateHolds in the .cpp).

	/** How long it has to be held, and has been. */
	float HoldTime = 1.f;
	float HoldElapsed = 0.f;

	/** Held down from Blueprint or the editor's Inputs panel (Set to true, let go with false) - on top of the widget's own press. */
	bool bHoldSetDown = false;

	/** Held down by Source, as last read. */
	bool bHoldSourceDown = false;

	/** Held all the way this press: it doesn't count again until it is let go. */
	bool bHoldDone = false;

	/** A Press And Hold was let go since the machine last ran - a one-frame pulse, as a Trigger is: Is Let Go. */
	bool bLetGo = false;
};

/** An action waiting to run once the frame's transitions are settled - see FWidgetAnimMachineAction. */
struct FWidgetAnimMachinePendingAction
{
	FWidgetAnimMachineAction Action;
	int32 LayerIndex = INDEX_NONE;

	/** The state it is on - None for a transition's. */
	FName State;
};

/**
 * What reads the built-in inputs off one widget - the widget a machine plays on, or one a layer's Widget Input Targets names -
 * with what it needs between frames, and what it read last.
 */
struct FWidgetAnimMachineWidgetReader
{
	/** The widget, as the layer names it: a widget path, or one of the machine's targets. None for the widget the machine plays on. */
	FName Name;

	/** The widget, once found - found again should it go. */
	TWeakObjectPtr<UWidget> Widget;

	/** What it read last. */
	FWidgetAnimMachineWidgetState State;

	/** How many times a Context Menu Button it reads had gone off when last read - for the built-in Clicked. */
	uint32 LastClickCount = 0;
	bool bHasClickCount = false;

	/**
	 * The counted input events (WidgetAnimInput::GetKeyPressCount and the rest) as it last saw them, by the built-in pulse that
	 * reads each - a changed count is a new event. Only for the pulses the machine reads; an entry is made on the first read, so
	 * what happened before the machine started never fires.
	 */
	TMap<FName, uint32> LastEventCounts;

	/** A right press counted over the widget, waiting for its release - for the built-in Right Clicked. */
	bool bRightPressOver = false;

	/** The plain-widget readers of the built-in inputs, when no provider answers for the widget. */
	FWidgetAnimInput_Interaction HoverReader;
	FWidgetAnimInput_Interaction PressReader;
	FWidgetAnimInput_Interaction FocusReader;
};

/** One state machine as a Widget Animation Component plays it. */
USTRUCT()
struct NAMETBD_API FWidgetAnimMachineInstance
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UWidgetAnimStateMachineAsset> Machine = nullptr;

	UPROPERTY(Transient)
	TArray<FWidgetAnimMachineLayerInstance> Layers;

	/** Which row of the component's State Machines it is. */
	int32 RowIndex = INDEX_NONE;

	TArray<FWidgetAnimMachineInputState> Inputs;

	/** Target name -> widget path, from the row. */
	TMap<FName, FName> TargetPaths;

	/**
	 * Each layer's state - or the one it is on its way to - as every layer was before any of them moved this frame, and as they
	 * were the frame before: what conditions on another layer's state read, so the order layers run in changes nothing.
	 */
	TArray<int32> LayerStatesNow;
	TArray<int32> LayerStatesBefore;

	/** Actions to run once the frame's transitions are settled. */
	TArray<FWidgetAnimMachinePendingAction> PendingActions;

	bool bStarted = false;

	/** The built-in Closing input. */
	bool bClosing = false;

	/** The built-in inputs the machine reads (WidgetAnimMachineInputs::GetBuiltInBit). */
	uint64 UsedBuiltIns = 0;

	/** What reads them off the widget the machine plays on, and what it read last - what every layer reads by default. */
	FWidgetAnimMachineWidgetReader HostReader;

	/**
	 * What reads them off the widgets the layers' Widget Input Targets name, other than the one the machine plays on - each
	 * once, however many layers name it.
	 */
	TArray<FWidgetAnimMachineWidgetReader> TargetReaders;

	/**
	 * Per layer, the readers its Widget Input Targets name - an index into TargetReaders, or INDEX_NONE for HostReader. Empty
	 * for a layer that names none: it reads HostReader.
	 */
	TArray<TArray<int32>> LayerReaders;

	/** Per layer that names widgets, the built-in inputs as those widgets read together (true when any of them is). Runtime only. */
	TArray<FWidgetAnimMachineWidgetState> LayerWidgetStates;

	/** The layer whose transitions, blend and parameters are being worked out - whose built-in inputs are read. INDEX_NONE: the host's. */
	int32 ReadingLayer = INDEX_NONE;

	/** The built-in inputs as the layer being worked out reads them (ReadingLayer): its widgets', or the host's. */
	const FWidgetAnimMachineWidgetState& GetReadWidgetState() const
	{
		return LayerReaders.IsValidIndex(ReadingLayer) && LayerReaders[ReadingLayer].Num() > 0 && LayerWidgetStates.IsValidIndex(ReadingLayer)
			? LayerWidgetStates[ReadingLayer] : HostReader.State;
	}

	/** An input changed since the layers last looked: a settled layer looks again. */
	bool bInputsChanged = true;

	/**
	 * Whether what the machine poses may have changed since its host last composed it: a layer worked its pose out again,
	 * or the machine was started or stopped. Cleared by the host once it has composed and applied the machines. A host
	 * whose machines all have it clear, and whose own tracks rest, has nothing new to apply that frame - which is how a
	 * screen of idle state-machine buttons costs only the reading of their inputs.
	 */
	bool bPoseChanged = true;

	const FWidgetAnimMachineInputState* FindInput(FName Name) const
	{
		return Inputs.FindByPredicate([Name](const FWidgetAnimMachineInputState& Input) { return Input.Name == Name; });
	}

	FWidgetAnimMachineInputState* FindInput(FName Name)
	{
		return Inputs.FindByPredicate([Name](const FWidgetAnimMachineInputState& Input) { return Input.Name == Name; });
	}
};

/** What a running machine needs from where it plays. */
struct NAMETBD_API FWidgetAnimMachineContext
{
	/** The widget the machine plays on: the component's. */
	UWidget* Host = nullptr;

	/** A widget path, as a Target Widget names it, to its widget - null for the host. */
	TFunction<UWidget*(FName)> ResolvePath;

	/** What poses are composed over, when captured. */
	const FWidgetAnimBaselineSet* Baselines = nullptr;

	/** What widgets looked like before anything animated them - for a material parameter blended back to where it was. */
	const FWidgetAnimBaselineSet* RestBaselines = nullptr;

	/** In the editor's designer: the inputs come from the Inputs panel rather than the widget, and no event is sent. */
	bool bDesignTime = false;

	/** Told of every state entered and exited, the frame it happens. Events are sent from here, a frame later. */
	TFunction<void(const FWidgetAnimStateEventInfo&)> OnStateEvent;
};

/** What a layer is doing, for the editor's graph while it debugs a widget. */
struct NAMETBD_API FWidgetAnimMachineLayerDebug
{
	FGuid CurrentState;

	/** The sub-machine states CurrentState is inside, outermost first - empty at the top of the layer. */
	TArray<FGuid> CurrentMachines;
	FGuid PreviousState;
	FGuid ActiveTransition;

	/** How far through the running transition, 0 - 1. */
	float TransitionAlpha = 0.f;

	/** How far through the state's animation, 0 - 1: through its first pass, or - looping - through the pass it is on. */
	float StateAlpha = 0.f;

	/** Whether the state is playing an animation that moves - not a pose, not done - for the editor's progress fill. */
	bool bStatePlaying = false;

	bool bExited = false;
	float Weight = 1.f;

	/** A blend state's weight for each of its samples, and where its input is. Empty for any other state. */
	TArray<float> BlendWeights;
	FVector2f BlendPoint = FVector2f::ZeroVector;

	/** The last few actions the layer ran, in words. */
	TArray<FString> LastActions;

	/**
	 * Each condition on another layer's state of this layer's transitions, and whether it holds now - read as the machine reads
	 * it, every layer as it was before any moved this frame. Entered and Exited hold for their one frame, as they do in play.
	 */
	struct FLayerCondition
	{
		FGuid Transition;
		int32 Condition = INDEX_NONE;
		bool bHolds = false;
	};
	TArray<FLayerCondition> LayerConditions;

	/** Condition ConditionIndex of Transition, when it is one on another layer's state; else null. */
	const FLayerCondition* FindLayerCondition(const FGuid& Transition, int32 ConditionIndex) const
	{
		return LayerConditions.FindByPredicate([&Transition, ConditionIndex](const FLayerCondition& Entry)
		{
			return Entry.Transition == Transition && Entry.Condition == ConditionIndex;
		});
	}
};

namespace WidgetAnimStateMachines
{
	/**
	 * Sets Instance up to play Row's machine: its inputs at their defaults - Row's where it overrides them - its layers
	 * empty, its targets mapped. Doesn't start it.
	 */
	NAMETBD_API void Initialize(FWidgetAnimMachineInstance& Instance, const FWidgetAnimStateMachineEntry& Row, int32 RowIndex);

	/** Starts every layer at its Entry, and takes Entry's transitions at once. Posed on the next Tick. */
	NAMETBD_API void Start(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineContext& Context);

	/** Stops every layer, dropping its pose. */
	NAMETBD_API void Stop(FWidgetAnimMachineInstance& Instance);

	/**
	 * Reads the inputs, takes whatever transitions they allow, moves every layer DeltaTime on and works out its pose. Then
	 * the Triggers fired before it are spent.
	 */
	NAMETBD_API void Tick(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineContext& Context, float DeltaTime);

	/**
	 * Folds the machines' layers into InOutResult - the frame's other tracks, already worked out - as Mode says, so one result
	 * holds everything and is applied once. Widgets the machines posed last frame but not this one are put back at rest.
	 * LastPosed is that list, kept by the caller between frames.
	 */
	NAMETBD_API void Compose(TArrayView<FWidgetAnimMachineInstance> Instances, EWidgetAnimTracksWithMachines Mode, const FWidgetAnimMachineContext& Context, FWidgetAnimResult& InOutResult,
		TArray<TPair<TWeakObjectPtr<UWidget>, FWidgetAnimPartKey>>& InOutLastPosed);

	/** Drops tracks that ended and don't hold, after the frame they ended on has been applied. */
	NAMETBD_API void ReleaseFinishedTracks(FWidgetAnimMachineInstance& Instance);

	/** The Closing input: true plays every layer's way to Exit; false brings layers that exited back in through Entry. */
	NAMETBD_API void SetClosing(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineContext& Context, bool bClosing);

	/** Whether a layer with a way to Exit hasn't got there yet - what a closing widget waits on. */
	NAMETBD_API bool IsExitPending(const FWidgetAnimMachineInstance& Instance);

	/** Sets an input. False when the machine has no input of that name and type. */
	NAMETBD_API bool SetBool(FWidgetAnimMachineInstance& Instance, FName Input, bool bValue);

	/** A Float, or an Integer (rounded). */
	NAMETBD_API bool SetNumber(FWidgetAnimMachineInstance& Instance, FName Input, float Value);
	NAMETBD_API bool SetInteger(FWidgetAnimMachineInstance& Instance, FName Input, int32 Value);

	/** A Choice, by its option's name. False when the input has no such option. */
	NAMETBD_API bool SetChoice(FWidgetAnimMachineInstance& Instance, FName Input, FName Option);

	NAMETBD_API bool SetVector2D(FWidgetAnimMachineInstance& Instance, FName Input, const FVector2D& Value);
	NAMETBD_API bool FireTrigger(FWidgetAnimMachineInstance& Instance, FName Input);

	/**
	 * Sets an input of any type from one number: a Bool is true when it isn't 0, a Trigger fires when it isn't 0, an Integer is
	 * rounded, a Choice takes the option of that number (0 for the first), a Press And Hold is pressed when it isn't 0. Not a
	 * Vector 2D.
	 */
	NAMETBD_API bool SetFromNumber(FWidgetAnimMachineInstance& Instance, FName Input, double Value);

	/** An input's value - a built-in one's too. False when there is none of that name. */
	NAMETBD_API bool GetBool(const FWidgetAnimMachineInstance& Instance, FName Input, bool& OutValue);

	/** A number: a Float or Integer, a Choice's option number, a Vector 2D's length, a Press And Hold's progress. */
	NAMETBD_API bool GetNumber(const FWidgetAnimMachineInstance& Instance, FName Input, float& OutValue);

	/** A Choice's option. */
	NAMETBD_API bool GetChoice(const FWidgetAnimMachineInstance& Instance, FName Input, FName& OutOption);

	NAMETBD_API bool GetVector2D(const FWidgetAnimMachineInstance& Instance, FName Input, FVector2D& OutValue);

	/** Sets the built-in inputs from outside - the editor's Inputs panel, at design time. */
	NAMETBD_API void SetBuiltIn(FWidgetAnimMachineInstance& Instance, FName Input, bool bValue);

	/** Whether LayerName (None: any layer) is in State, or on its way there. */
	NAMETBD_API bool IsInState(const FWidgetAnimMachineInstance& Instance, FName State, FName LayerName);

	/** The state LayerName (None: the first layer) is in, or on its way to. */
	NAMETBD_API FName GetCurrentState(const FWidgetAnimMachineInstance& Instance, FName LayerName);

	/** Sets the value-driven tracks the machine's states play, as Set Widget Animation Value does. How many took it. */
	NAMETBD_API int32 SetTrackValue(FWidgetAnimMachineInstance& Instance, const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap);

	/** Every setting the machine can play, built for the widgets its targets resolve to - what baselines are captured from. */
	NAMETBD_API void GatherAllSettings(const FWidgetAnimStateMachineEntry& Row, const FWidgetAnimMachineContext& Context, TArray<FWidgetAnimSettings>& OutSettings);

	/** Whether any layer is blending, playing something that hasn't settled, or following a value. */
	NAMETBD_API bool IsAnimating(const FWidgetAnimMachineInstance& Instance);

	/** What each layer is doing, for the editor's graph. */
	NAMETBD_API void GetDebug(const FWidgetAnimMachineInstance& Instance, TArray<FWidgetAnimMachineLayerDebug>& OutLayers);

	/**
	 * Works out again the settings of the animations the layers' states are playing, with the parameter values as they are now -
	 * the scopes in effect (Set Widget Animation Parameter, pushed by the caller) and each state's Parameters From Inputs - and
	 * lays them over the states' tracks one for one, so where each part is in its play stays as it was: a playing state follows
	 * a parameter set while it plays, as an entry's animation does. bOnlyIfInputsChanged: only the states whose Parameters From
	 * Inputs give other values than when last built (what Tick asks each frame the inputs change). True if a track changed.
	 */
	NAMETBD_API bool RefreshParameters(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineContext& Context, bool bOnlyIfInputsChanged = false);

	/**
	 * A blend state's weight for each sample, from where its input is: Blend 1D between the two samples either side of Value.X
	 * (held to the ends), Blend 2D by the triangle of samples around Value (barycentric; outside them all, the nearest three
	 * by inverse distance). Shared by the runtime and the editor's preview plot.
	 */
	NAMETBD_API void ComputeBlendWeights(const FWidgetAnimMachineState& State, const FVector2f& Value, TArray<float>& OutWeights);

	/** A layer's blend weights - for Get State Machine Blend Weights. Empty when it isn't in a blend state. */
	NAMETBD_API void GetBlendWeights(const FWidgetAnimMachineInstance& Instance, FName LayerName, TArray<float>& OutWeights);
}
