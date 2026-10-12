// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Extensions/UIComponent.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/ObjectKey.h"
#include "WidgetAnimStateMachine.h"
#include "WidgetAnimationComponent.h"
#include "WidgetAnimationStateMachineComponent.generated.h"

/**
 * State machines for any widget, added from the UMG designer's Add Component button: the Widget Animation State Machines
 * (WASM_) the widget plays - its State Machines - each with a widget picked for each of its targets and its own settings for
 * its inputs. A machine moves the widget between its states on its own as its inputs change: hovered, pressed, selected, a
 * value from a View Model. Its Entry plays as the widget is constructed (or with Play In), and its way to Exit as the widget
 * closes, which a screen closing waits for. See WidgetAnimStateMachine.h.
 *
 * It plays them through the widget's Widget Animation Component: with the Anim In, Anim Out and everything played by Anim ID
 * there, composed as its Animations With State Machines says, when the widget has one - or, when it has none, through one
 * of its own, made for it and never shown. Either way Find Widget Animation Component on the widget finds what plays them,
 * and everything aimed at a widget's state machines - Set State Machine Input, a View Model, a Flow List - reaches them.
 *
 * Its functions set and read only its own machines (StateMachine, when one is given, among them).
 */
UCLASS(DefaultToInstanced, EditInlineNew, BlueprintType, meta = (DisplayName = "Widget Animation State Machine Component"))
class NAMETBD_API UWidgetAnimationStateMachineComponent : public UUIComponent
{
	GENERATED_BODY()

public:

	// -------------------------------------------------------------------------------------------------
	// Authored settings
	// -------------------------------------------------------------------------------------------------

	/**
	 * The state machines this widget plays: each a Widget Animation State Machine (WASM_) - states, transitions and inputs -
	 * with a widget picked for each of its targets (Animation Target Widgets), its own settings for its inputs, and whether it
	 * starts as the widget is constructed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Machine", meta = (DisplayName = "State Machines", TitleProperty = "StateMachine"))
	TArray<FWidgetAnimStateMachineEntry> StateMachines;

	/**
	 * How the Widget Animation Component's own animations on the same widget - Anim In, Anim Out and everything played by Anim
	 * ID - compose with these machines: on top of them (the default), or under them, covered wherever an Override layer of a
	 * machine animates. Nothing to compose with on a widget with no Widget Animation Component. See EWidgetAnimTracksWithMachines.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Machine", meta = (DisplayName = "Animations With State Machines"))
	EWidgetAnimTracksWithMachines TracksWithStateMachines = EWidgetAnimTracksWithMachines::AddOnTop;

	/**
	 * Whether the machines' animations compose over whatever each target was authored as, instead of replacing it - as a Widget
	 * Animation Component's Preserve Authored Transforms. On a widget with a Widget Animation Component, that component's says.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Machine", meta = (DisplayName = "Preserve Authored Transforms"))
	bool bPreserveAuthoredTransforms = false;

	/**
	 * A state machine here entered or exited a state, or ran a Fire Event action: which machine, layer and state. Fired a frame
	 * later, so anything may be done here. In a Widget Blueprint use the state's own Custom Event instead - On <Machine> Entered
	 * <State> - added with the + in these Details' Events.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Animation|State Machine")
	FWidgetAnimationComponentStateEvent OnStateMachineStateChanged;

	// -------------------------------------------------------------------------------------------------
	// Inputs and states
	// -------------------------------------------------------------------------------------------------

	/** Sets a Bool input of the state machines here - of StateMachine only, when one is given. Returns whether a machine had the input. */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine", meta = (DisplayName = "Set State Machine Bool"))
	bool SetStateMachineBool(FName Input, bool bValue, UWidgetAnimStateMachineAsset* StateMachine = nullptr);

	/** Sets a Number input of the state machines here - of StateMachine only, when one is given. */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine", meta = (DisplayName = "Set State Machine Number"))
	bool SetStateMachineNumber(FName Input, float Value, UWidgetAnimStateMachineAsset* StateMachine = nullptr);

	/** Sets an Integer input of the state machines here. */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine", meta = (DisplayName = "Set State Machine Integer"))
	bool SetStateMachineInteger(FName Input, int32 Value, UWidgetAnimStateMachineAsset* StateMachine = nullptr);

	/** Sets a Choice input of the state machines here to one of its options, by name. False when no machine had it. */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine", meta = (DisplayName = "Set State Machine Choice"))
	bool SetStateMachineChoice(FName Input, FName Option, UWidgetAnimStateMachineAsset* StateMachine = nullptr);

	/** Sets a Vector 2D input of the state machines here: a stick, a swipe. Up is negative Y, as on screen. */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine", meta = (DisplayName = "Set State Machine Vector 2D"))
	bool SetStateMachineVector2D(FName Input, FVector2D Value, UWidgetAnimStateMachineAsset* StateMachine = nullptr);

	/** Fires a Trigger input of the state machines here: true for the next frame the machines run, then false again. */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine", meta = (DisplayName = "Fire State Machine Trigger"))
	bool FireStateMachineTrigger(FName Input, UWidgetAnimStateMachineAsset* StateMachine = nullptr);

	/** A Bool input's value - a built-in one's too (Hovered, Pressed, Focused, Selected, Disabled, Closing). False when there is none. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine", meta = (DisplayName = "Get State Machine Bool"))
	bool GetStateMachineBool(FName Input, UWidgetAnimStateMachineAsset* StateMachine = nullptr) const;

	/** A Choice input's option. None when there is none. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine", meta = (DisplayName = "Get State Machine Choice"))
	FName GetStateMachineChoice(FName Input, UWidgetAnimStateMachineAsset* StateMachine = nullptr) const;

	/** A Vector 2D input's value. Zero when there is none. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine", meta = (DisplayName = "Get State Machine Vector 2D"))
	FVector2D GetStateMachineVector2D(FName Input, UWidgetAnimStateMachineAsset* StateMachine = nullptr) const;

	/** A number input's value: a Float or Integer, a Choice's option number, a Vector 2D's length, a Press And Hold's progress. 0 when there is none. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine", meta = (DisplayName = "Get State Machine Number"))
	float GetStateMachineNumber(FName Input, UWidgetAnimStateMachineAsset* StateMachine = nullptr) const;

	/** Whether a state machine here is in State - or on its way there - on Layer, or on any layer when Layer is None. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine")
	bool IsInState(FName State, UWidgetAnimStateMachineAsset* StateMachine = nullptr, FName Layer = NAME_None) const;

	/** The state Layer of a state machine here is in, or on its way to - its first layer when Layer is None. None when it isn't playing. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine")
	FName GetCurrentStateOfLayer(UWidgetAnimStateMachineAsset* StateMachine = nullptr, FName Layer = NAME_None) const;

	/**
	 * How much each animation of the blend state Layer is in plays right now, in the order of the state's Blend Samples (0 to 1,
	 * adding up to 1) - its first layer when Layer is None. Empty when that state isn't a blend state.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine", meta = (DisplayName = "Get State Machine Blend Weights"))
	TArray<float> GetStateMachineBlendWeights(UWidgetAnimStateMachineAsset* StateMachine = nullptr, FName Layer = NAME_None) const;

	/**
	 * Starts every state machine on the widget that isn't playing - those not set to Start On Construct (the Widget Animation
	 * Component's own too, when one plays these). Each starts at its Entry.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine")
	void StartStateMachines();

	/** Stops every state machine on the widget (the Widget Animation Component's own too, when one plays these). Start State Machines starts them again. */
	UFUNCTION(BlueprintCallable, Category = "Animation|State Machine")
	void StopStateMachines();

	// -------------------------------------------------------------------------------------------------
	// Owner and lookup
	// -------------------------------------------------------------------------------------------------

	/** The widget this component's machines play on. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine")
	UWidget* GetAnimatedWidget() const { return MachineOwner.Get(); }

	/**
	 * What plays the machines here: the Widget Animation Component on the same widget, or the one made for this component when
	 * the widget has none. Null before the component is on a widget.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine", meta = (DisplayName = "Get Playing Widget Animation Component"))
	UWidgetAnimationComponent* GetPlayer() const;

	/** The Widget Animation State Machine Component on Widget, or null. */
	UFUNCTION(BlueprintPure, Category = "Animation|State Machine")
	static UWidgetAnimationStateMachineComponent* FindWidgetAnimationStateMachineComponent(const UWidget* Widget);

	/** Whether StateMachine is one of the State Machines here. */
	bool HasStateMachine(const UWidgetAnimStateMachineAsset* StateMachine) const;

	/** Sizes every State Machines row's targets and inputs to its machine - see FWidgetAnimStateMachineEntry::SyncToMachine. */
	bool SyncStateMachines();

	/**
	 * Backs the Target Widget dropdowns of Animation Target Widgets and of an input's Measured On Widget: (This Widget) first,
	 * then the widget the component is on and every widget inside it. Must stay a UFUNCTION for GetOptions.
	 */
	UFUNCTION()
	TArray<FString> GetAnimTargetWidgetOptions() const;

	virtual void PostLoad() override;
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	/** Keeps every row's Animation Target Widgets and Inputs in step with its machine. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

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

	/** The widget the machines play on. */
	TWeakObjectPtr<UWidget> MachineOwner;

	/**
	 * The Widget Animation Component made to play the machines on a widget that has none of its own - bound to it, never
	 * added to its components or shown. Null while the widget's own plays them.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UWidgetAnimationComponent> OwnPlayer;

	/** Widget -> component, for FindWidgetAnimationStateMachineComponent. Game thread only. */
	static TMap<TObjectKey<UWidget>, TWeakObjectPtr<UWidgetAnimationStateMachineComponent>> ComponentsByWidget;

	/** Puts the component on InWidget: registered for lookups, and played - by the widget's Widget Animation Component, or OwnPlayer. */
	void SetMachineOwner(UWidget* InWidget);
	void Unregister();

	/**
	 * Makes OwnPlayer when the widget has no Widget Animation Component of its own to play the machines, and lets it go when
	 * one turns up. Returns what plays them.
	 */
	UWidgetAnimationComponent* EnsurePlayer();

	/** The machines a call is for: StateMachine, when given - else every machine here, each once. */
	void GetMachinesFor(UWidgetAnimStateMachineAsset* StateMachine, TArray<UWidgetAnimStateMachineAsset*>& OutMachines) const;
};
