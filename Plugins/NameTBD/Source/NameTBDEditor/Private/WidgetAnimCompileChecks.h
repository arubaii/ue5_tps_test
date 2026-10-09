// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintCompilerExtension.h"

#include "WidgetAnimCompileChecks.generated.h"

class UBlueprint;

/**
 * Reports, in a Widget Blueprint's compile results, what the Widget Animations and state machines it holds or plays name but
 * don't have - the same lines data validation gives on their assets, now where they are seen while working:
 *  - a state machine's condition on an input, layer or state that is gone, and a state's Parameters From Inputs naming a
 *    parameter none of its animations has (or an input the machine doesn't have);
 *  - an animation's track bound to a parameter it doesn't have, and a Plays When on one.
 * What is checked: the Widget Blueprint's own state machine and timelines, and - for every Widget Animation Component in it -
 * the machines its State Machines play (and their states' animations) and its Anim In / Anim Out animations. Each machine or
 * animation once per compile. Warnings only: each problem is simply false / ignored when it plays.
 */
UCLASS()
class UWidgetAnimCompilerExtension : public UBlueprintCompilerExtension
{
	GENERATED_BODY()

protected:

	virtual void ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data) override;
};

namespace WidgetAnimCompileChecks
{
	/** Registers the compiler extension for Widget Blueprints. Once, from the editor module's startup. */
	void Register();

	/** Every problem, in words, of what Blueprint holds or plays - see UWidgetAnimCompilerExtension. */
	void Gather(const UBlueprint* Blueprint, TArray<FText>& OutProblems);
}
