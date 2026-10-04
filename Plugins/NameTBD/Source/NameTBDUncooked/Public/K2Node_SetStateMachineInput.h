// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"

#include "K2Node_SetStateMachineInput.generated.h"

class UEdGraphPin;

/** One state machine a Set State Machine Input node can pick, with its inputs. */
struct NAMETBDUNCOOKED_API FWidgetAnimMachineChoice
{
	/** What the node's State Machine pin stores: the machine's object path. */
	FString Key;

	FText Label;

	/** Its inputs: name and type. The built-in ones are not set from here. */
	TArray<TPair<FName, EWidgetAnimMachineInputType>> Inputs;
};

/**
 * Set State Machine Input, picked from dropdowns: the State Machine - one of those the Widget Animation Components on the
 * Target and inside it play - then one of its Inputs. A Bool input takes a bool, a Float a float, an Integer an integer, a
 * Choice one of its options by name, a Vector 2D a vector, a Press And Hold a bool (true presses it), and a Trigger nothing: the
 * node fires it. The lists are looked for as Set Widget Animation Value looks for its animations.
 *
 * Compiles to UWidgetAnimStateMachineLibrary::SetStateMachineBool, SetStateMachineNumber, SetStateMachineInteger,
 * SetStateMachineChoice, SetStateMachineVector2D or FireStateMachineTrigger. Set All
 * Matches sets it in every component found that plays the machine, rather than the first - for a screen of many buttons.
 */
UCLASS()
class NAMETBDUNCOOKED_API UK2Node_SetStateMachineInput : public UK2Node
{
	GENERATED_BODY()

public:

	static const FName TargetPinName;
	static const FName StateMachinePinName;
	static const FName InputPinName;
	static const FName ValuePinName;
	static const FName SearchParentWidgetsPinName;
	static const FName SetAllMatchesPinName;

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const override;

	// UK2Node
	virtual bool IsNodePure() const override { return false; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual FText GetMenuCategory() const override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;

	/** Every machine the components the node reaches play, with their inputs. For the dropdowns. */
	void GetMachineChoices(TArray<FWidgetAnimMachineChoice>& OutChoices) const;

	/** The inputs of the picked machine. Empty when none is picked. */
	void GetInputChoices(TArray<TPair<FName, EWidgetAnimMachineInputType>>& OutInputs) const;

	/** The picked Choice input's options, for the Value pin's dropdown. Empty for any other input. */
	void GetChoiceOptions(TArray<FName>& OutOptions) const;

	EWidgetAnimMachineInputType GetInputType() const { return InputType; }

protected:

	/** The picked input's type - which decides the Value pin. Saved, so the pin is right before anything is looked up. */
	UPROPERTY()
	EWidgetAnimMachineInputType InputType = EWidgetAnimMachineInputType::Bool;

	/** The type of SourceKey's input Name, when it has one. */
	bool FindInputType(const UEdGraphPin* TargetPin, const FString& MachineKey, FName Name, EWidgetAnimMachineInputType& OutType) const;

	/** The machines the components Target reaches play. */
	void GatherChoices(const UEdGraphPin* TargetPin, TArray<FWidgetAnimMachineChoice>& OutChoices) const;

	/** Makes the Value pin the picked input's type - none for a Trigger - breaking links that no longer fit. */
	void RefreshValuePin();

	/** Adds the Value pin for Type, if Type has one. */
	void CreateValuePin(EWidgetAnimMachineInputType Type);
};
