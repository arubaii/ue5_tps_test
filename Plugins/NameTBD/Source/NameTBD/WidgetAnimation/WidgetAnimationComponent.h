// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Extensions/UIComponent.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/ObjectKey.h"
#include "Blueprint/UserWidget.h"
#include "WidgetAnimLibrary.h"
#include "WidgetAnimPreset.h"
#include "WidgetAnimStateMachine.h"
#include "WidgetAnimStateMachineRuntime.h"
#include "WidgetAnimationComponent.generated.h"

class SWidget;
class FActiveTimerHandle;
class UWidgetAnimationComponent;

// =====================================================================================================
// Versioning (UUserWidget_Animatable migration)
// =====================================================================================================

/**
 * Stamped into packages saved by UUserWidget_Animatable. Before this version Anim In / Anim Out and
 * their flags were properties of the widget itself; UUserWidget_Animatable::PostLoad moves them into
 * its component once.
 */
struct NAMETBD_API FWidgetAnimationComponentVersion
{
	enum Type : int32
	{
		BeforeCustomVersionWasAdded = 0,

		/** Anim In / Anim Out and their flags live on UWidgetAnimationComponent instead of the widget. */
		MovedIntoComponent,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static const FGuid GUID;

	FWidgetAnimationComponentVersion() = delete;
};

/** Everything a widget authored about its animations before they moved into the component. */
struct NAMETBD_API FWidgetAnimationLegacySettings
{
	bool bPlayOnlyOnConstructAndDestruct = false;
	bool bPlayInAnimationsOnConstruct = true;
	bool bPlayAnimationsOnVisibilityHidden = false;
	TArray<FWidgetAnimEntry> AnimIn;
	TArray<FWidgetAnimEntry> AnimOut;
	bool bPreserveAuthoredTransforms = false;
	FWidgetAnimPreviewSettings DesignerPreview;
};

/**
 * How the designer toolbar plays a selected widget's animations, in place of its entries' own flags: the same
 * four choices an entry makes about one use of an animation. Editor-only in use; plain data.
 */
struct NAMETBD_API FWidgetAnimDesignerPlayback
{
	bool bLoop = false;
	bool bPingPong = true;
	bool bInvert = false;
	float Speed = 1.f;
};

/**
 * When a component's Additive Anim Layers start: right away, with Anim In or Anim Out (unset) - or within one of its
 * entries, as part of it: on its clock, from its start, whenever it plays - its own Play In, a trigger, a Play By ID
 * of its Anim ID - with its Loop, Ping Pong, Invert and Playback Speed. Set from the component's inline track editor.
 *
 * The entry is remembered by where it is authored - which list, and its place there - and by its animation, so it is
 * found again after the list is reordered. An entry that can no longer be found - removed, or its animation swapped -
 * leaves the layers starting right away again.
 */
USTRUCT()
struct NAMETBD_API FWidgetAnimAdditiveStart
{
	GENERATED_BODY()

	/** The entry is in Anim Out (true) or Anim In. One from the other list plays in this list's direction by Mirror In Out. */
	UPROPERTY()
	bool bInAnimOut = false;

	/** Its place in that list when it was picked. */
	UPROPERTY()
	int32 EntryIndex = INDEX_NONE;

	/** Its animation: what tells it apart, and finds it again at another place. */
	UPROPERTY()
	TObjectPtr<UWidgetAnimPresetAsset> Animation = nullptr;

	bool IsSet() const { return EntryIndex != INDEX_NONE && Animation != nullptr; }
	void Reset() { *this = FWidgetAnimAdditiveStart(); }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWidgetAnimationComponentEvent, UWidgetAnimationComponent*, Component);
DECLARE_MULTICAST_DELEGATE_OneParam(FWidgetAnimationComponentNativeEvent, UWidgetAnimationComponent* /*Component*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWidgetAnimationComponentCueEvent, FName, EventName, const FWidgetAnimCueInfo&, Info);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWidgetAnimationComponentStateEvent, const FWidgetAnimStateEventInfo&, Info);

// =====================================================================================================
// Component
// =====================================================================================================

/**
 * Anim In / Anim Out for any widget, added from the UMG designer's Add Component button.
 *
 * Select a widget in the hierarchy (a Border, a Button, a nested User Widget - anything), press Add
 * Component, pick Widget Animation Component. The component then plays its Anim In when the User Widget
 * it lives in is constructed, and everything else UUserWidget_Animatable used to do: mirroring, the
 * track player, Play In / Play Out with the cascade into nested components, play-by-ID, authored
 * transform baselines, extra layers handed over at runtime, and a designer preview.
 *
 * The widget the component is added to is the one it animates. An entry with no Target Widget animates
 * it; a Target Widget path is looked up in the same Widget Blueprint the component was added in.
 *
 * Things that are different from a widget-based host, because a UI Component is not a widget:
 *  - It keeps its own clock (the core ticker) instead of riding on NativeTick, so it animates whether or
 *    not the owning widget ticks, and poses are applied before Slate lays out the frame.
 *  - It cannot intercept SetVisibility on its owner. With Play Animations On Visibility Hidden on,
 *    becoming visible still plays Anim In automatically; hiding WITH an exit animation goes through
 *    Set Visibility Animated. (UUserWidget_Animatable routes its own SetVisibility there, so it keeps
 *    the old behavior.)
 *
 * Code can also create one and attach it by hand with BindToWidget - UInfoCardWidget and
 * UUserWidget_Animatable both do - in which case the code that bound it also calls
 * NotifyOwnerConstructed / NotifyOwnerDestructed.
 */
UCLASS(DefaultToInstanced, EditInlineNew, BlueprintType, meta = (DisplayName = "Widget Animation Component"))
class NAMETBD_API UWidgetAnimationComponent : public UUIComponent
{
	GENERATED_BODY()

public:

	// -------------------------------------------------------------------------------------------------
	// Authored settings
	// -------------------------------------------------------------------------------------------------

	/**
	 * Plays every Anim In entry on construct and ignores visibility, whatever each entry's own Play In Animation On
	 * Construct and Play Animation On Visibility Hidden say.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bPlayOnlyOnConstructAndDestruct = false;

	/** Moved onto each Anim In entry as Play In Animation On Construct. Read once on load, then cleared into the entries. */
	UPROPERTY()
	bool bPlayInAnimationsOnConstruct_DEPRECATED = true;

	/** Moved onto each entry as Play Animation On Visibility Hidden. Read once on load, then cleared into the entries. */
	UPROPERTY()
	bool bPlayAnimationsOnVisibilityHidden_DEPRECATED = false;

	/**
	 * The animations this widget plays as it appears.
	 *
	 * Each entry points at a Widget Animation asset - which holds the layers, the timing and the curves -
	 * and decides the handful of things that are about THIS use of it: whether it loops, whether it also
	 * plays backwards as the exit, whether it runs inverted, and which widget it moves.
	 *
	 * Give an entry an Anim ID to fire it from Blueprint later with Play Widget Animations By Id.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Anim In", TitleProperty = "Animation"))
	TArray<FWidgetAnimEntry> AnimIn;

	/** The animations this widget plays as it closes. Anything in Anim In marked Mirror In Out is included automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Anim Out", TitleProperty = "Animation"))
	TArray<FWidgetAnimEntry> AnimOut;

	/**
	 * Plain animation layers played on top of Anim In, every time it plays.
	 *
	 * For a touch that belongs to this one widget and isn't worth a Widget Animation asset of its own. The
	 * layers animate the widget this component is on, have no Anim ID (so Play Widget Animations By Id only
	 * includes them when it is asked to play everything), and are not mirrored into Anim Out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Additive Anim Layers In", TitleProperty = "AnimType"))
	TArray<FWidgetAnimLayer> AdditiveAnimLayersIn;

	/** Plain animation layers played on top of Anim Out, every time it plays. See Additive Anim Layers In. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Additive Anim Layers Out", TitleProperty = "AnimType"))
	TArray<FWidgetAnimLayer> AdditiveAnimLayersOut;

	/** When Additive Anim Layers In start: right away with Anim In, or within one of its entries. See FWidgetAnimAdditiveStart. */
	UPROPERTY()
	FWidgetAnimAdditiveStart AdditiveAnimLayersInStart;

	/** When Additive Anim Layers Out start: right away with Anim Out, or within one of its entries. See FWidgetAnimAdditiveStart. */
	UPROPERTY()
	FWidgetAnimAdditiveStart AdditiveAnimLayersOutStart;

	/**
	 * Every value the value-driven tracks in this component's animations and additive tracks use (Timing = Value in
	 * the Widget Animations editor), one row per animation and Value Name, listed for you: set them with Set Widget
	 * Animation Value. Tick a row to give this widget its own range, start value and limits for it, in place of the
	 * tracks' own. The value type and how the tracks travel to a value - their smoothing - belong to the tracks.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Value Tracks", EditFixedSize, TitleProperty = "{ValueName} ({Source})"))
	TArray<FWidgetAnimValueOverride> ValueOverrides;

	/**
	 * State machines this widget plays: each a Widget Animation State Machine (WASM_) - states, transitions and inputs - with
	 * a widget picked for each of its targets. A machine moves the widget between its states on its own as its inputs change:
	 * hovered, pressed, selected, a value from a View Model. Its Entry plays as the widget is constructed (or with Play In),
	 * and its way to Exit as the widget closes, which a screen closing waits for. See WidgetAnimStateMachine.h.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "State Machines", TitleProperty = "StateMachine"))
	TArray<FWidgetAnimStateMachineEntry> StateMachines;

	/**
	 * How Anim In, Anim Out and everything played by Anim ID compose with the state machines: on top of them (the default), or
	 * under them - covered wherever an Override layer of a machine animates. See EWidgetAnimTracksWithMachines.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Animations With State Machines"))
	EWidgetAnimTracksWithMachines TracksWithStateMachines = EWidgetAnimTracksWithMachines::AddOnTop;

	/**
	 * Whether animations compose over whatever each target was authored as, instead of replacing it.
	 *
	 * Off (the default): a Fade ending at 1 means opacity 1, full stop, even on a widget authored at 0.5.
	 * On: opacity and scale multiply, translation and angle add - so that same Fade settles at 0.5, and a
	 * Slide returns to wherever the widget was nudged to rather than to zero.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Preserve Authored Transforms"))
	bool bPreserveAuthoredTransforms = false;

	/**
	 * The old always-on designer preview (its Mode, speed and holds). No longer shown or used - the Widget Animations
	 * toolbar and panel preview instead - and read as Off whatever was saved, so a widget saved previewing doesn't keep
	 * playing with no way to stop it. Kept only so older assets load quietly.
	 */
	UPROPERTY()
	FWidgetAnimPreviewSettings DesignerPreview;

	/** Fired when the exit animation finishes, after any visibility deferred by Set Visibility Animated has been applied. */
	UPROPERTY(BlueprintAssignable, Category = "Animation")
	FWidgetAnimationComponentEvent OnOutAnimationFinished;

	/** Native twin of OnOutAnimationFinished, broadcast just after it. */
	FWidgetAnimationComponentNativeEvent OnOutAnimationFinishedNative;

	/**
	 * An Event cue in one of this component's animations or additive tracks has fired: its Event Name, and where and how
	 * (which widget it is aimed at, which point, its value). Fires a frame after the cue is passed, so anything may be
	 * done here - play another animation, change the text, remove the widget. UMG shows no events for a UI component, so
	 * in a Widget Blueprint use the event's own Custom Event instead - On <Animation> Event Track <Name> Fired, added with
	 * the + in these Details' Events; this is for code holding the component. See WidgetAnimCue.h.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Animation")
	FWidgetAnimationComponentCueEvent OnAnimationEvent;

	/**
	 * A state machine here entered or exited a state: which machine, layer and state, and where it came from or goes. Fired a
	 * frame later, so anything may be done here. In a Widget Blueprint use the state's own Custom Event instead - On <Machine>
	 * Entered <State> - added with the + in these Details' Events.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Animation|State Machine")
	FWidgetAnimationComponentStateEvent OnStateMachineStateChanged;

	// -------------------------------------------------------------------------------------------------
	// Playback
	// -------------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void PlayIn(bool bCascadeToChildren = true);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void PlayOut(bool bCascadeToChildren = true);

	UFUNCTION(BlueprintPure, Category = "Animation")
	bool IsAnimClosing() const;

	UFUNCTION(BlueprintPure, Category = "Animation")
	EWidgetAnimPhase GetAnimPhase() const { return AnimPlayer.Phase; }

	/**
	 * True if any track going in this direction is still running. An endlessly looping track counts as
	 * running until its loop is ended, even after the rest of its direction has landed.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation")
	bool IsPlayingInDirection(bool bIsOut) const;

	/**
	 * Rebuilds the stacks the component plays from Anim In / Anim Out and the additive layers. Call it after
	 * changing any of those at runtime.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void RebuildEffectiveAnimations();

	/**
	 * Extra layers merged on top of Anim In / Anim Out (after the additive layers), for callers that build
	 * FWidgetAnimSettings themselves - the info card additive animations being the case this exists for.
	 * A null Target Widget on a layer animates the component's owner. Replaces whatever was set before.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetExtraAnimSettings(const TArray<FWidgetAnimSettings>& InExtraAnimIn, const TArray<FWidgetAnimSettings>& InExtraAnimOut);

	/**
	 * Sets the owner's visibility, with animation: hiding plays Anim Out first and applies the
	 * visibility when it finishes; showing applies it immediately and plays Anim In. Without Play
	 * Animations On Visibility Hidden this is a plain SetVisibility.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetVisibilityAnimated(ESlateVisibility InVisibility);

	/**
	 * Advances every running animation by DeltaTime and applies the result. Returns true on the frame the
	 * exit animation finishes. The component already calls this itself every frame while its owner is
	 * constructed; only call it for a component you are clocking by hand.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	bool TickAnimationPlayback(float DeltaTime);

	// -------------------------------------------------------------------------------------------------
	// Play by ID
	// -------------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Animation", meta = (AutoCreateRefTerm = "AnimIds"))
	void PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse = false, float PlaybackSpeed = 1.f, bool bHoldFinalPose = true, bool bRestartIfPlaying = true);

	UFUNCTION(BlueprintCallable, Category = "Animation", meta = (AutoCreateRefTerm = "AnimIds"))
	void StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart = false);

	UFUNCTION(BlueprintPure, Category = "Animation", meta = (AutoCreateRefTerm = "AnimIds"))
	bool AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const;

	/**
	 * Ends the looping of the running animation with this Anim ID, in either direction.
	 *
	 * Soft Settle on: the animation finishes the loop it is in, then stops on its end pose. A ping-pong
	 * loop caught on its way back plays forward once more so it settles rather than snapping.
	 * Soft Settle off: the animation ends immediately, jumping to its end pose.
	 *
	 * Every running entry with this ID is ended together, the same way they start together. Does nothing
	 * if AnimId is None, is not playing, or is conflicting (two entries with this ID on the same widget in
	 * the same direction) - the last case also puts an error on screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void EndLoopingAnimation(FName AnimId, bool bSoftSettle = true);

	/**
	 * Anim IDs used by more than one entry on the same widget going the same direction (mirrored copies
	 * included), as of the last Rebuild Effective Animations. Sharing an ID across different widgets is
	 * intended and is not listed here.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation")
	TArray<FName> GetConflictingAnimationIds() const { return ConflictingAnimIds; }

	// -------------------------------------------------------------------------------------------------
	// Value-driven tracks
	// -------------------------------------------------------------------------------------------------

	/**
	 * Sets the value of the value-driven tracks with this Value Name (Timing = Value in the Widget Animations editor),
	 * in every animation that has one - the Set Widget Animation Value node picks one animation instead. Each track
	 * maps it onto itself through its Value At Start and Value At End - 0 and 1 by default, so 0.5 is halfway along -
	 * and travels there by its own smoothing. Call it as often as the value changes.
	 *
	 * Snap jumps straight to the value, and may go back even where a track never goes backwards - for resetting. Start
	 * If Not Playing starts the Anim In entries that have such a track but haven't played yet, so no Play call is needed
	 * first; an entry already playing is never restarted. Set before the widget is constructed, the value is where the
	 * tracks start. Returns whether a running track took the value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation|Value", meta = (AdvancedDisplay = "bSnap,bStartIfNotPlaying"))
	bool SetWidgetAnimationValue(FName ValueName, float Value, bool bSnap = false, bool bStartIfNotPlaying = true);

	/** Set Widget Animation Value, for a track whose Value Type is Integer. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Value", meta = (DisplayName = "Set Widget Animation Value (Integer)", AdvancedDisplay = "bSnap,bStartIfNotPlaying"))
	bool SetWidgetAnimationValueInteger(FName ValueName, int32 Value, bool bSnap = false, bool bStartIfNotPlaying = true);

	/** Set Widget Animation Value, for a track whose Value Type is Vector 2D: X and Y each move their own axis. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Value", meta = (DisplayName = "Set Widget Animation Value (Vector 2D)", AdvancedDisplay = "bSnap,bStartIfNotPlaying"))
	bool SetWidgetAnimationValueVector2D(FName ValueName, FVector2D Value, bool bSnap = false, bool bStartIfNotPlaying = true);

	/** Set Widget Animation Value, for a track whose Value Type is Color: R, G, B and A each move their own channel. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Value", meta = (DisplayName = "Set Widget Animation Value (Color)", AdvancedDisplay = "bSnap,bStartIfNotPlaying"))
	bool SetWidgetAnimationValueColor(FName ValueName, FLinearColor Value, bool bSnap = false, bool bStartIfNotPlaying = true);

	/**
	 * Any of the four, for the value Id names - one animation's, the additive tracks', or the name anywhere - with the
	 * value raw, up to four channels. See WidgetAnimValue::FromFloat and the others.
	 */
	bool SetWidgetAnimationValues(const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying);

	/**
	 * The value the first value-driven track with this Value Name shows now - or, with Target Value, the one it is
	 * heading to (the last one set) - in the value's own units. 0 when nothing has this name and no value was set.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation|Value")
	float GetWidgetAnimationValue(FName ValueName, bool bTargetValue = false) const;

	UFUNCTION(BlueprintPure, Category = "Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Integer)"))
	int32 GetWidgetAnimationValueInteger(FName ValueName, bool bTargetValue = false) const;

	UFUNCTION(BlueprintPure, Category = "Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Vector 2D)"))
	FVector2D GetWidgetAnimationValueVector2D(FName ValueName, bool bTargetValue = false) const;

	UFUNCTION(BlueprintPure, Category = "Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Color)"))
	FLinearColor GetWidgetAnimationValueColor(FName ValueName, bool bTargetValue = false) const;

	/** The raw value behind the four getters, for the value Id names. */
	FVector4 GetWidgetAnimationValues(const FWidgetAnimValueId& Id, bool bTargetValue) const;

	// Percentage animations
	// -------------------------------------------------------------------------------------------------

	/**
	 * Sets how far along the percentage animations played by the entries with this Anim ID are (see EWidgetAnimTimeUnit):
	 * 0 is 0%, 1 is 100% - a hold timer's elapsed share, a charge level. Every track of the animation follows it - its
	 * keys, staggers, cues and the animations inserted into it - travelling there by the animation's Smoothing. Call it as
	 * often as the progress changes. An Anim ID of None sets every percentage animation here.
	 *
	 * Snap jumps straight to it, even backwards where the animation never goes backwards. Start If Not Playing starts the
	 * Anim In entries with that Anim ID that haven't played yet. An entry playing over its Progress Animation Duration
	 * carries on from wherever it has got to. Returns whether a running animation took the progress.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation|Progress", meta = (DisplayName = "Set Widget Animation Progress By Id", AdvancedDisplay = "bSnap,bStartIfNotPlaying"))
	bool SetWidgetAnimationProgress(FName AnimId, float Progress, bool bSnap = false, bool bStartIfNotPlaying = true);

	/**
	 * How far along the percentage animation an entry with this Anim ID plays is now, 0 - 1 - or, with Target Progress,
	 * where it is heading (the last progress set). 0 when none is playing.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation|Progress", meta = (DisplayName = "Get Widget Animation Progress By Id"))
	float GetWidgetAnimationProgress(FName AnimId, bool bTargetProgress = false) const;

	/**
	 * Whether any animation here - an Anim In or Anim Out entry's, or the additive tracks - has a value-driven track
	 * that takes the value Id names. What the Set Widget Animation Value node looks for among the components on and
	 * under its Target, to set the first one (or every one) that has it.
	 */
	bool HasWidgetAnimationValue(const FWidgetAnimValueId& Id) const;

	/**
	 * Lists every value the component's animations and additive tracks use in Value Tracks - one row per animation and
	 * Value Name, in the order first found - keeping the rows already overridden and dropping values no longer used.
	 * Rows not overridden show their tracks' settings. Run on load and on every edit. True if anything changed.
	 */
	bool RefreshValueOverrides();

	/** Names every value-driven additive track that has no Value Name yet after its type, unique across both lists. */
	bool AssignAdditiveValueNames();

	/**
	 * True while Play Out is waiting for a value-driven track - here, or in a component it cascades into - to arrive
	 * before the exit starts (its On Exit is Wait For Value or Complete First). The phase is already Playing Out.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation")
	bool IsExitWaitingForValues() const { return PendingExit != EPendingExit::None; }

	/**
	 * Whether Play Out would move anything here: an exit to play, or a value-driven track it would wait for first.
	 * What a screen closing asks before deciding it has an exit to wait for.
	 */
	bool HasExitToPlay() const;

	// -------------------------------------------------------------------------------------------------
	// State machines
	// -------------------------------------------------------------------------------------------------

	/**
	 * Sets a Bool input of the state machines here - of StateMachine only, when one is given. Returns whether a machine had
	 * the input.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine", meta = (DisplayName = "Set State Machine Bool"))
	bool SetStateMachineBool(FName Input, bool bValue, UWidgetAnimStateMachineAsset* StateMachine = nullptr);

	/** Sets a Number input of the state machines here - of StateMachine only, when one is given. */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine", meta = (DisplayName = "Set State Machine Number"))
	bool SetStateMachineNumber(FName Input, float Value, UWidgetAnimStateMachineAsset* StateMachine = nullptr);

	/** Fires a Trigger input of the state machines here: true for the next frame the machines run, then false again. */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine", meta = (DisplayName = "Fire State Machine Trigger"))
	bool FireStateMachineTrigger(FName Input, UWidgetAnimStateMachineAsset* StateMachine = nullptr);

	/** Sets an input of any type from a number: a Bool is true when it isn't 0, a Trigger fires when it isn't 0. What a View Model's Set Input calls. */
	bool SetStateMachineInputValue(FName Input, double Value, UWidgetAnimStateMachineAsset* StateMachine = nullptr);

	/** A Bool input's value - a built-in one's too (Hovered, Pressed, Focused, Selected, Disabled, Closing). False when there is none. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine", meta = (DisplayName = "Get State Machine Bool"))
	bool GetStateMachineBool(FName Input, UWidgetAnimStateMachineAsset* StateMachine = nullptr) const;

	/** A Number input's value. 0 when there is none. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine", meta = (DisplayName = "Get State Machine Number"))
	float GetStateMachineNumber(FName Input, UWidgetAnimStateMachineAsset* StateMachine = nullptr) const;

	/** Whether a state machine here is in State - or on its way there - on Layer, or on any layer when Layer is None. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine")
	bool IsInState(FName State, UWidgetAnimStateMachineAsset* StateMachine = nullptr, FName Layer = NAME_None) const;

	/** The state Layer of a state machine here is in, or on its way to - its first layer when Layer is None. None when it isn't playing. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine")
	FName GetCurrentStateOfLayer(UWidgetAnimStateMachineAsset* StateMachine = nullptr, FName Layer = NAME_None) const;

	/** Starts every state machine here that isn't playing - those not set to Start On Construct. Each starts at its Entry. */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine")
	void StartStateMachines();

	/** Stops every state machine here, leaving the widgets as the rest of the animations say. Start State Machines starts them again. */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine")
	void StopStateMachines();

	/** Whether a machine here - StateMachine, or any when null - has an input of that name, declared or built in. */
	bool HasStateMachineInput(const UWidgetAnimStateMachineAsset* StateMachine, FName Input) const;

	/** Whether StateMachine is one of the State Machines here. */
	bool PlaysStateMachine(const UWidgetAnimStateMachineAsset* StateMachine) const;

	/** The machines as they play, for the editor's graph to show where each is. */
	const TArray<FWidgetAnimMachineInstance>& GetStateMachineInstances() const { return MachineInstances; }

	/** Sizes every State Machines row's targets and inputs to its machine - see FWidgetAnimStateMachineEntry::SyncToMachine. */
	bool SyncStateMachines();

	/**
	 * For the State Machine editor's Inputs panel: plays this component's state machines in the designer, from their Entry,
	 * for the panel to drive. The toolbar's own preview stops. False when the designer hasn't made the widget's Slate widget
	 * yet - ask again on a later frame.
	 */
	bool StartDesignerStateMachinePreview();

	/** Stops the designer's state machine preview and puts the widgets back as authored. */
	void StopDesignerStateMachinePreview();

	bool IsDesignerStateMachinePreviewing() const { return bMachinePreviewing; }

	/** Holds the designer's state machine preview still where it is (the State Machine editor's pause), or lets it run on. */
	void SetDesignerStateMachinePreviewPaused(bool bPaused) { bMachinePreviewPaused = bPaused; }
	bool IsDesignerStateMachinePreviewPaused() const { return bMachinePreviewPaused; }

	/** Sets a built-in input (Hovered, Pressed, Focused, Selected, Disabled, Closing) of the machines playing in the designer. */
	void SetDesignerStateMachineBuiltIn(FName Input, bool bValue, const UWidgetAnimStateMachineAsset* StateMachine = nullptr);

	// -------------------------------------------------------------------------------------------------
	// Owner and lookup
	// -------------------------------------------------------------------------------------------------

	/** The widget this component animates. */
	UFUNCTION(BlueprintPure, Category = "Animation")
	UWidget* GetAnimatedWidget() const { return AnimOwner.Get(); }

	/** The Widget Animation Component on Widget, or null. Works for designer-added and code-bound components alike. */
	UFUNCTION(BlueprintPure, Category = "Animation")
	static UWidgetAnimationComponent* FindWidgetAnimationComponent(const UWidget* Widget);

	/**
	 * Supplies Anim In and Anim Out entries for a component whose own lists are both empty - how an animated text widget
	 * plays the project's default text animations (Project Settings > Widget Animations > Text Widgets) until it is
	 * given entries of its own. Returns false for "no defaults". Asked each time the effective animations are built, so
	 * a changed project default reaches every widget the next time it is constructed. Never saved; set by the owner.
	 */
	using FDefaultEntriesProvider = TFunction<bool(TArray<FWidgetAnimEntry>& OutAnimIn, TArray<FWidgetAnimEntry>& OutAnimOut)>;
	void SetDefaultEntriesProvider(FDefaultEntriesProvider InProvider) { DefaultEntriesProvider = MoveTemp(InProvider); }

	/**
	 * Calls Visit for the nearest components below Widget (Widget's own component is not included): a branch that
	 * has its own component is handed to it and not walked further, because that component cascades into its own
	 * children in turn. Steps into nested User Widgets. The same walk Play In / Play Out cascade along, so code that
	 * drives a whole widget from outside - a screen transition waiting on every exit - sees the components the
	 * cascade reaches.
	 */
	static void ForEachNearestNestedComponent(UWidget* Widget, TFunctionRef<void(UWidgetAnimationComponent*)> Visit);

	/**
	 * Attaches this component to InWidget by hand, for code that creates the component itself rather than
	 * having it added in the designer. Paths are then resolved inside InWidget if it is a User Widget.
	 * The caller also becomes responsible for NotifyOwnerConstructed / NotifyOwnerDestructed.
	 */
	void BindToWidget(UWidget* InWidget);

	/** Owner lifecycle. Both are idempotent, so being told twice (designer system and host code) is harmless. */
	void NotifyOwnerConstructed();
	void NotifyOwnerDestructed();

	/** At design time, (re)starts the designer preview from the widget's authored pose. No-op at runtime. */
	void NotifyOwnerDesignTimeRefresh();

	/**
	 * The owner - an animated text widget - was given a new text: plays the Anim In entries set to Play When Text
	 * Changes, from their start, leaving everything else playing. Runtime only. When it arrives in the middle of a frame
	 * being applied - a text set by an event the animation fired - it plays as soon as that frame is done. False when
	 * nothing is set to play.
	 */
	bool PlayForTextChange();

	/** Registers the designer preview's active timer if the preview needs frames and it isn't registered yet. */
	void EnsureDesignerPreviewTimer();

	/**
	 * For the designer toolbar shown when an animated widget is selected: plays this component's animations in
	 * the designer - the entrance, or the exit from the settled pose - with the toolbar's Loop, Ping Pong,
	 * Invert and Speed in place of its entries' own, whatever its Designer Preview is set to. Lasts until
	 * Reset, or until the designer rebuilds the widget and the preview is Designer Preview's again.
	 */
	void PlayDesignerPreviewOverride(bool bOut, const FWidgetAnimDesignerPlayback& Options);

	/** Freezes the toolbar's preview at Alpha of one pass - of the exit if the last play was Play Out. */
	void ScrubDesignerPreviewOverride(float Alpha, const FWidgetAnimDesignerPlayback& Options);

	/**
	 * The toolbar's Loop / Ping Pong / Invert / Speed changed. Applied to what is showing where it stands - no
	 * restart, so flipping Invert or dragging Speed keeps the preview where it is. Nothing to do while the
	 * toolbar isn't driving this component.
	 */
	void UpdateDesignerPreviewOptions(const FWidgetAnimDesignerPlayback& Options);

	/** Puts the widget back as authored and holds it there. The toolbar's Reset. */
	void ResetDesignerPreviewOverride();

	/** The toolbar's readouts: where the preview is in one pass, in seconds and as a fraction, and how long a pass is. */
	float GetDesignerPreviewSeconds() const;
	float GetDesignerPreviewDuration() const;
	float GetDesignerPreviewAlpha() const;

	/** Whether the toolbar's preview is running - Play pressed, not yet finished, frozen or reset. */
	bool IsDesignerPreviewPlaying() const;

	/** Whether there is anything for the designer toolbar to play. */
	bool HasDesignerPreviewAnimations() const { return FlattenedIn.Num() > 0 || FlattenedOut.Num() > 0; }

	/** True while this component is applying a visibility itself. A SetVisibility override that routes into the component must pass the call straight through while this is set. */
	bool IsApplyingVisibility() const { return bApplyingVisibility; }

	/** The widget an entry's Target Widget path refers to, or null to mean "the animated widget". */
	UWidget* ResolveAnimTarget(FName TargetWidgetName) const;

	// -------------------------------------------------------------------------------------------------
	// When the additive layers start
	// -------------------------------------------------------------------------------------------------

	/**
	 * The entry an Additive Anim Layers list starts within - bOutList picks Out or In - as its place in Anim Out
	 * (bOutInAnimOut) or Anim In. INDEX_NONE when the list starts right away, or its entry can't be found.
	 */
	int32 ResolveAdditiveStart(bool bOutList, bool& bOutInAnimOut) const;

	/**
	 * Whether an authored entry can host an Additive Anim Layers list: it has an animation, and plays in the list's
	 * direction - in its own list, or mirrored into it by Mirror In Out.
	 */
	bool CanHostAdditiveList(bool bOutList, bool bInAnimOut, int32 EntryIndex) const;

	/** Points each list's start at its entry's current place, and clears one whose entry is gone. True if either changed. */
	bool RefreshAdditiveStarts();

	/**
	 * Backs the Target Widget dropdown on every entry: (This Widget) first, then the widget the component is on and every
	 * widget inside it - a Border with a Text in it offers (This Widget), Border and Text - nested User Widgets' insides
	 * as "Nested/Inner". On a whole User Widget, its whole tree. Must stay a UFUNCTION for GetOptions.
	 */
	UFUNCTION()
	TArray<FString> GetAnimTargetWidgetOptions() const;

	const TArray<FWidgetAnimSettings>& GetFlattenedAnimIn() const { return FlattenedIn; }
	const TArray<FWidgetAnimSettings>& GetFlattenedAnimOut() const { return FlattenedOut; }

	// -------------------------------------------------------------------------------------------------
	// Migration
	// -------------------------------------------------------------------------------------------------

	/**
	 * Takes settings that were saved on the owning widget before they moved here. With ArchetypeLegacy
	 * null everything is taken; with it set, only fields that differ from it are.
	 */
	void ImportLegacySettings(const FWidgetAnimationLegacySettings& Legacy, const FWidgetAnimationLegacySettings* ArchetypeLegacy);

	/** Whether an owner's legacy fields should be compared against Archetype's (see ImportLegacySettings). */
	static bool ShouldDiffLegacyAgainstArchetype(const UObject* Archetype);

	virtual void BeginDestroy() override;

	/** Brings every entry's Target Widgets in line with its animation's Layer Groups - see SyncEntryGroupTargets. */
	virtual void PostLoad() override;

#if WITH_EDITOR
	/** Keeps every Transform layer's Keys in step with its curve, and every entry's Target Widgets in step with its animation's groups. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	 * Sizes and labels each Anim In / Anim Out entry's Target Widgets to its animation's Layer Groups. Run on
	 * load and on every edit, so the Details panel lists exactly the groups the picked animation has; the
	 * built stacks never depend on it - an entry that is behind resolves missing groups to their defaults.
	 */
	bool SyncEntryGroupTargets();

protected:

	// UUIComponent
#if UE_VERSION_OLDER_THAN(5, 6, 0)
	virtual bool OnInitialize() override;
#else
	virtual void OnInitialize() override;
	virtual void OnPreConstruct(bool bIsDesignTime) override;
#endif
	virtual void OnConstruct() override;
	virtual void OnDestruct() override;

private:

	/** See SetDefaultEntriesProvider. */
	FDefaultEntriesProvider DefaultEntriesProvider;

	/** The widget being animated. Set from the UI Component owner, or by BindToWidget. */
	TWeakObjectPtr<UWidget> AnimOwner;

	/** True when BindToWidget attached this rather than the designer's component system. */
	bool bBoundByCode = false;

	bool bOwnerConstructed = false;
	bool bApplyingVisibility = false;
	bool bLastSeenVisible = false;

	UPROPERTY(Transient)
	TArray<FWidgetAnimEntry> EffectiveInEntries;

	UPROPERTY(Transient)
	TArray<FWidgetAnimEntry> EffectiveOutEntries;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> FlattenedIn;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> FlattenedOut;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> ExtraAnimIn;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> ExtraAnimOut;

	/** Additive Anim Layers followed by the extra layers: everything without an Anim ID, played as one track. */
	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> UnnamedAnimIn;

	/** An Additive Anim Layers list that starts within an entry, placed, and that entry's place among the effective entries. */
	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> LinkedAdditiveIn;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> LinkedAdditiveOut;

	int32 LinkedEffectiveIn = INDEX_NONE;
	int32 LinkedEffectiveOut = INDEX_NONE;

	/** Where an authored entry is among the effective entries of a direction - see AppendEntriesWithMirroring. */
	int32 ToEffectiveIndex(bool bOutDirection, bool bInAnimOut, int32 EntryIndex) const;

	/**
	 * Appends the Additive Anim Layers list that starts within effective entry EffectiveIndex - if one does - to that
	 * entry's settings, EntrySettings from First on: stamped with the entry's own per-use flags and its clock, so it
	 * plays as part of it.
	 */
	void AppendLinkedAdditive(int32 EffectiveIndex, bool bOut, TArray<FWidgetAnimSettings>& EntrySettings, int32 First) const;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> UnnamedAnimOut;

	UPROPERTY(Transient)
	FWidgetAnimPlayer AnimPlayer;

	UPROPERTY(Transient)
	bool bIsDeferringVisibility = false;

	UPROPERTY(Transient)
	ESlateVisibility DeferredVisibility = ESlateVisibility::Hidden;

	/** Authored pose of every animated widget. Captured at design time always, at runtime only for Preserve Authored Transforms. */
	UPROPERTY(Transient)
	FWidgetAnimBaselineSet AnimBaselines;

	/**
	 * The authored pose again, captured at runtime when Preserve Authored Transforms is off and AnimBaselines
	 * therefore isn't. Only for putting widgets back - Stop with Reset To Start - never composed over, so animations
	 * still replace the authored pose exactly as that setting says.
	 */
	UPROPERTY(Transient)
	FWidgetAnimBaselineSet ResetPoseBaselines;

	/** Whether any Anim In or Anim Out entry plays on a visibility change - what the visibility watch is for. */
	bool bAnyVisibilityTrigger = false;

	/** Whether any Anim In entry plays when the text changes. */
	bool bAnyTextChangeTrigger = false;

	/** How deep inside applying a frame this component is, and a text change waiting for it to finish - see PlayForTextChange. */
	int32 PlayerTickDepth = 0;
	bool bTextChangePending = false;

	/** Ticks the player by DeltaTime and applies the result, then plays a text change that arrived meanwhile. */
	void TickPlayer(UWidget* AnimatedWidget, float DeltaTime, TArray<FName>& OutJustFinished);

	/** Whether the component sits on an animated text widget - which is what shows Play When Text Changes. */
	bool IsOnTextWidget() const;

	/**
	 * An exit asked for while a value-driven animation still had a way to go, waiting for it to arrive - see
	 * FWidgetAnimValueOptions::OnExit. Which exit it was, and whether it cascades; started from TickAnimationPlayback
	 * once every value it waits on has arrived. Cleared by anything that plays the entrance instead.
	 */
	enum class EPendingExit : uint8
	{
		None,
		PlayOut,
		VisibilityTrigger
	};

	EPendingExit PendingExit = EPendingExit::None;
	bool bPendingExitCascade = true;

	/** Set while a waiting exit is being started, so it doesn't wait again. */
	bool bStartingPendingExit = false;

	/**
	 * Sends this component's value-driven animations - and, with bCascade, those of every component the exit cascades
	 * into - their exit values (Complete First goes to 1), and answers whether any still has a way to go.
	 */
	bool PrepareValuesForExit(bool bCascade);

	/** Starts the waiting exit if every value it waits on has arrived. True once it has started. */
	bool TryStartPendingExit();

	/** Puts this widget's Value Tracks overrides onto the value-driven layers in Settings. */
	void ApplyValueOverrides(TArray<FWidgetAnimSettings>& Settings) const;

	/**
	 * What every call starting an entry does to that entry's settings before its track starts: the Additive Anim Layers
	 * list that starts within it, and this widget's value overrides.
	 */
	void PrepareEntrySettings(int32 EffectiveIndex, bool bOut, TArray<FWidgetAnimSettings>& EntrySettings) const;

	UPROPERTY(Transient)
	FWidgetAnimPreviewState DesignerPreviewState;

	/** The designer toolbar's mode, while it is overriding Designer Preview. Never saved. */
	TOptional<EWidgetAnimPreviewMode> DesignerPreviewModeOverride;

	/** The toolbar's playback options, and where its scrub stands - which stack, and how far along. */
	FWidgetAnimDesignerPlayback DesignerPreviewOptions;
	float DesignerPreviewScrubAlpha = 0.f;
	bool bDesignerPreviewScrubOut = false;

	/** Anim In / Anim Out with the toolbar's options stamped on - what plays while the toolbar is driving. */
	TArray<FWidgetAnimSettings> DesignerOverrideIn;
	TArray<FWidgetAnimSettings> DesignerOverrideOut;

	/** Designer Preview, with the toolbar's mode, speed and scrub in place of its own while it is driving. */
	FWidgetAnimPreviewSettings GetEffectiveDesignerPreview() const;

	/** Restamps DesignerOverrideIn/Out from the built stacks and DesignerPreviewOptions. */
	void RebuildDesignerOverrideStacks();

	/** The stacks the designer preview plays: the toolbar's stamped copies while it is driving, else the built ones. */
	const TArray<FWidgetAnimSettings>& GetDesignerPreviewIn() const { return DesignerPreviewModeOverride.IsSet() ? DesignerOverrideIn : FlattenedIn; }
	const TArray<FWidgetAnimSettings>& GetDesignerPreviewOut() const { return DesignerPreviewModeOverride.IsSet() ? DesignerOverrideOut : FlattenedOut; }

	/** Whether the preview is on the exit: playing it, or holding after it. */
	bool IsDesignerPreviewShowingOut() const;

	/** Runtime clock: the core ticker, registered while the owner is constructed. */
	FTSTicker::FDelegateHandle RuntimeTickHandle;

	/** Designer clock: an active timer on the owner's Slate widget, which is also what keeps the designer repainting. */
	TWeakPtr<FActiveTimerHandle> PreviewTimerHandle;
	TWeakPtr<SWidget> PreviewTimerWidget;

	/** Anim IDs two effective entries use on the same widget in the same direction. Rebuilt with the stacks. */
	TArray<FName> ConflictingAnimIds;

	/** Retries EnsureDesignerPreviewTimer until the owner's Slate widget exists. */
	FTSTicker::FDelegateHandle PreviewRetryHandle;
	int32 PreviewRetriesLeft = 0;

	/** Widget -> component, for FindWidgetAnimationComponent. Game thread only. */
	static TMap<TObjectKey<UWidget>, TWeakObjectPtr<UWidgetAnimationComponent>> ComponentsByWidget;

	void SetAnimOwner(UWidget* InWidget, bool bByCode);
	void Unregister();

	/** The User Widget whose tree Target Widget paths are resolved in. */
	UUserWidget* GetPathRoot() const;

	void CascadeAnimationToChildren(bool bPlayingIn);
	void EnsureRuntimeBaselines();
	void ApplyCurrentAnimPose();

	/** What starts a filtered Play In / Play Out: which of each entry's triggers it answers to. */
	enum class EEntryTrigger : uint8
	{
		Construct,
		Visibility
	};

	/**
	 * Play In or Play Out for one trigger: only the entries set to play on it. The unnamed layers (Additive Anim
	 * Layers and extra layers) join whenever an entry does - or, on construct, when there are no entries at all, as
	 * they always have. False when nothing was started.
	 */
	bool PlayForTrigger(EEntryTrigger Trigger, bool bOut);

	/** Marks the entries' trigger settings visible on this component's own entries, and notes whether any watches visibility. */
	void RefreshEntryTriggers();

	/** Moves the two trigger settings saved on the component before they moved onto entries onto every entry. */
	void MigrateTriggersIntoEntries();
	void ApplyVisibilityToOwner(ESlateVisibility InVisibility);
	void HandleOutAnimationFinished();

	void StartRuntimeTicking();
	void StopRuntimeTicking();
	void RuntimeTick(float DeltaTime);

	void StopDesignerPreview();

	/** True while any track in this direction still has a layer that is going to end - endless loops excluded. Drives Phase. */
	bool IsSettlingInDirection(bool bIsOut) const;

	/** Recomputes ConflictingAnimIds from the effective stacks, reporting any conflict that is new. */
	void RefreshAnimIdConflicts();

	/** Puts the conflict error for AnimId on screen and in the log. */
	void ReportAnimIdConflict(FName AnimId) const;

	/** Reports each of AnimIds that is conflicting. Returns true if any was. */
	bool ReportConflictsIn(const TArray<FName>& AnimIds) const;

	static bool IsVisibleState(ESlateVisibility Visibility);

	// -------------------------------------------------------------------------------------------------
	// State machines
	// -------------------------------------------------------------------------------------------------

	/** The machines as they play, one per State Machines row with a machine. */
	UPROPERTY(Transient)
	TArray<FWidgetAnimMachineInstance> MachineInstances;

	/** The widgets the machines posed last frame - put back at rest when they stop posing them. */
	TArray<TPair<TWeakObjectPtr<UWidget>, FWidgetAnimPartKey>> MachineLastPosed;

	/** State events waiting for the next frame, and the ticker that fires them. Held, so the widgets they name are too. */
	UPROPERTY(Transient)
	TArray<FWidgetAnimStateEventInfo> PendingStateEvents;

	FTSTicker::FDelegateHandle StateEventTickerHandle;

	/** What the machines are handed every frame - see GetMachineContext. */
	FWidgetAnimMachineContext MachineContext;

	/** The frame the machines last moved on, so a second tick with time in one frame re-poses instead - as the track player does. */
	uint64 MachinesLastAdvanceFrame = 0;

	/** The designer's state machine preview: whether it runs, and its clock. */
	bool bMachinePreviewing = false;
	bool bMachinePreviewPaused = false;
	TWeakPtr<FActiveTimerHandle> MachinePreviewTimerHandle;
	TWeakPtr<SWidget> MachinePreviewTimerWidget;

	/** What the machines need from here: the widget, the paths, the baselines, where events go. */
	const FWidgetAnimMachineContext& GetMachineContext();

	/** One instance per row with a machine, set up and not started. */
	void InitializeMachines();

	/** Starts the machines - only those set to Start On Construct when bOnlyOnConstruct. */
	void StartMachines(bool bOnlyOnConstruct);

	/** Whether any machine is playing. */
	bool AreMachinesRunning() const;

	/** Whether a machine with a way to Exit hasn't got there yet - what Playing Out waits on besides the exit's tracks. */
	bool AreMachinesExiting() const;

	/** Whether a visibility change plays anything: an entry set to play on it, or a state machine with a way to Exit. */
	bool ReactsToVisibility() const;

	/** Every setting the machines can play, for baselines to be captured from. */
	void GatherMachineSettings(TArray<FWidgetAnimSettings>& OutSettings);

	/** The Closing input of every machine. */
	void SetMachinesClosing(bool bClosing);

	/** Calls Visit for every running machine, or StateMachine's only. True if any was visited. */
	bool ForEachMachine(const UWidgetAnimStateMachineAsset* StateMachine, TFunctionRef<bool(FWidgetAnimMachineInstance&)> Visit);
	const FWidgetAnimMachineInstance* FindMachine(const UWidgetAnimStateMachineAsset* StateMachine) const;

	/** Ticks the machines and folds them into Result - the frame's other tracks - as Tracks With State Machines says. */
	void TickAndComposeMachines(float DeltaTime, FWidgetAnimResult& Result);

	/** A state was entered or exited: sent next frame. */
	void QueueStateEvent(const FWidgetAnimStateEventInfo& Info);
	void FlushStateEvents();

	/** The designer's state machine clock. */
	void TickDesignerMachines(float DeltaTime);
};