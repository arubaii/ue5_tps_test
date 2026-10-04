// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetAnimStateMachineLibrary.generated.h"

class UWidgetAnimationComponent;
class UWidgetAnimStateMachineAsset;

/**
 * State machines from Blueprint, aimed at a widget: the Widget Animation Components on it and inside it - its child widgets,
 * nested User Widgets - that play the machine, or outwards from it when none does. The Set State Machine Input node compiles
 * to the setters here; the getters are for graphs and View Model conversion functions alike.
 */
UCLASS()
class NAMETBD_API UWidgetAnimStateMachineLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|State Machine", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Target"))
	static bool SetStateMachineBool(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bValue, bool bSearchParentWidgets = true, bool bSetAllMatches = false);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|State Machine", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Target"))
	static bool SetStateMachineNumber(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, double Value, bool bSearchParentWidgets = true, bool bSetAllMatches = false);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|State Machine", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Target"))
	static bool SetStateMachineInteger(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, int32 Value, bool bSearchParentWidgets = true, bool bSetAllMatches = false);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|State Machine", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Target"))
	static bool SetStateMachineChoice(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, FName Option, bool bSearchParentWidgets = true, bool bSetAllMatches = false);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|State Machine", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Target"))
	static bool SetStateMachineVector2D(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, FVector2D Value, bool bSearchParentWidgets = true, bool bSetAllMatches = false);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|State Machine", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Target"))
	static bool FireStateMachineTrigger(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bSearchParentWidgets = true, bool bSetAllMatches = false);

	/** A Bool input of the state machine on or in Target - a built-in one's too: Hovered, Pressed, Focused, Selected, Disabled, Closing. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|State Machine", meta = (DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static bool GetStateMachineBool(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bSearchParentWidgets = true);

	/** A number input of the state machine on or in Target: a Float or Integer, a Choice's option number, a Vector 2D's length, a Press And Hold's progress. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|State Machine", meta = (DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static float GetStateMachineNumber(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bSearchParentWidgets = true);

	/** An Integer input of the state machine on or in Target. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|State Machine", meta = (DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static int32 GetStateMachineInteger(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bSearchParentWidgets = true);

	/** A Choice input's option, of the state machine on or in Target. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|State Machine", meta = (DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static FName GetStateMachineChoice(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bSearchParentWidgets = true);

	/** A Vector 2D input of the state machine on or in Target. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|State Machine", meta = (DisplayName = "Get State Machine Vector 2D", DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static FVector2D GetStateMachineVector2D(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bSearchParentWidgets = true);

	/**
	 * Whether the state machine on or in Target is in State - or on its way there - on Layer, or on any layer when Layer is
	 * None. StateMachine None: any machine there.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|State Machine", meta = (DisplayName = "Is In State", DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static bool IsWidgetInState(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName State, FName Layer = NAME_None, bool bSearchParentWidgets = true);

	/** The state Layer of the state machine on or in Target is in, or on its way to - its first layer when Layer is None. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|State Machine", meta = (DisplayName = "Current State Of Layer", DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static FName GetCurrentStateOfLayer(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Layer = NAME_None, bool bSearchParentWidgets = true);

	/**
	 * The Widget Animation Components a call aimed at Target reaches: Target itself when it is one; else those on Target and
	 * inside it that play StateMachine (any machine when null) and have Input (any when None) - the first, or with bAll every
	 * one - else, with bSearchParentWidgets, the nearest outwards from Target.
	 */
	static void FindComponents(UObject* Target, const UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bSearchParentWidgets, bool bAll, TArray<UWidgetAnimationComponent*>& OutComponents);
};
