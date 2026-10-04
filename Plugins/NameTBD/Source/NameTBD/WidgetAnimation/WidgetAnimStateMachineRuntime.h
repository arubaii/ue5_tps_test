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
	float Number = 0.f;

	/** A Trigger fired since the machine last ran. */
	bool bFired = false;

	/** Where it is read from, with its runtime state - this use's copy. Invalid when it is set from outside. */
	FInstancedStruct Source;

	/** The widget path Source is measured on; None for the widget the machine plays on. */
	FName MeasuredOnPath;

	/** The last value Source gave, for a Trigger's rising edge. */
	float LastSample = 0.f;
	bool bHasSample = false;
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

	bool bStarted = false;

	/** The built-in Closing input. */
	bool bClosing = false;

	/** The built-in inputs the machine reads (WidgetAnimMachineInputs::GetBuiltInBit), and what they read last. */
	uint8 UsedBuiltIns = 0;
	FWidgetAnimMachineWidgetState WidgetState;

	/** The plain-widget readers of the built-in inputs, when no provider answers for the widget. */
	FWidgetAnimInput_Interaction HoverReader;
	FWidgetAnimInput_Interaction PressReader;
	FWidgetAnimInput_Interaction FocusReader;

	/** An input changed since the layers last looked: a settled layer looks again. */
	bool bInputsChanged = true;

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
	FGuid PreviousState;
	FGuid ActiveTransition;

	/** How far through the running transition, 0 - 1. */
	float TransitionAlpha = 0.f;

	/** How far through the state's animation, 0 - 1 for one pass. */
	float StateAlpha = 0.f;

	bool bExited = false;
	float Weight = 1.f;
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
	NAMETBD_API bool SetNumber(FWidgetAnimMachineInstance& Instance, FName Input, float Value);
	NAMETBD_API bool FireTrigger(FWidgetAnimMachineInstance& Instance, FName Input);

	/** Sets an input of any type from one number: a Bool is true when it isn't 0, a Trigger fires when it isn't 0. */
	NAMETBD_API bool SetFromNumber(FWidgetAnimMachineInstance& Instance, FName Input, double Value);

	/** An input's value - a built-in one's too. False when there is none of that name. */
	NAMETBD_API bool GetBool(const FWidgetAnimMachineInstance& Instance, FName Input, bool& OutValue);
	NAMETBD_API bool GetNumber(const FWidgetAnimMachineInstance& Instance, FName Input, float& OutValue);

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
}
