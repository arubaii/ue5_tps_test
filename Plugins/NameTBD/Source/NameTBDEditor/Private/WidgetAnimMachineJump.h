// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"

class UWidgetAnimStateMachineAsset;
struct FWidgetAnimMachineLayer;

/**
 * Jump To State: what the State Machine editor's preview inputs have to be for a layer to go to a state - so a state can be
 * looked at without working out, and setting by hand, every input the way there tests.
 *
 * The way is the fewest enabled transitions from the state the layer is in (Any State's from anywhere; Entry's only before it
 * has started). Each step is one transition: every input goes back to its default - what isn't needed is unset - and then the
 * transition's conditions are made to hold. A transition that would leave the step's state first - an earlier one in priority,
 * or Any State's - and that holds on those values is made not to, where that doesn't undo what the step needs. The last step
 * does the same for every way out of the state jumped to, so it stays there.
 *
 * Every step also keeps the layer from going on from where it arrives before the next step is set. Triggers are fired,
 * Increased / Decreased / Changed moved from where the step before left them, a Press And Hold held. Built-in inputs are set
 * as the Inputs panel sets them. Comparisons between two inputs follow the other input's planned value - set first - and
 * Choices compare by option name, as the runtime does.
 */
namespace WidgetAnimMachineJump
{
	/** The inputs as one step sets them. */
	struct FInputPlan
	{
		/** Bool (0 / 1), Float, Integer, Choice (its option's number), Press And Hold (1 held). */
		TMap<FName, float> Values;
		TMap<FName, FVector2D> Vectors;

		/** Triggers to fire, and the built-in Clicked. */
		TSet<FName> Fire;

		/** Hovered, Pressed, Focused, Selected, Disabled, Closing. */
		TMap<FName, bool> BuiltIns;
	};

	struct FStep
	{
		/** The transition this step takes - none for a step that only keeps the layer where it is. */
		FGuid Transition;

		/** The node it arrives at. */
		FGuid To;

		FInputPlan Plan;
	};

	/**
	 * The steps that take Layer of Machine from From - the state it is in; invalid before it has started - to Target. Current
	 * holds the inputs as they are now (for Increased and Decreased; later steps compare with the step before). False when
	 * Target can't be reached, with OutProblem saying why; OutSteps then still holds a step setting the conditions of a
	 * transition into Target, when it has one. True with OutProblem set when a step's transition couldn't be kept holding
	 * while the others were kept from being taken - the jump may stop short.
	 */
	bool Plan(const UWidgetAnimStateMachineAsset& Machine, int32 LayerIndex, const FGuid& From, const FGuid& Target,
		const TMap<FName, float>& CurrentValues, const TMap<FName, FVector2D>& CurrentVectors, TArray<FStep>& OutSteps, FText& OutProblem);
}
