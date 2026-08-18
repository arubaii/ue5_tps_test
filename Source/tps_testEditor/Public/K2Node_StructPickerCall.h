// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_CallFunction.h"
#include "K2Node_StructPickerCall.generated.h"

/**
 * A call node with a struct dropdown drawn BELOW its pins, which types the node's
 * wildcard struct pin(s).
 *
 * Opt any function in by tagging it with StructTypePin, naming a UScriptStruct* parameter:
 *
 *     UFUNCTION(BlueprintPure, CustomThunk, meta = (
 *         CustomStructureParam = "OutValue",
 *         StructTypePin        = "ValueType"))
 *     static bool GetSomething(const FThing& Thing, UScriptStruct* ValueType, int32& OutValue);
 *
 * That parameter's pin is HIDDEN on the node: the dropdown writes into it, so the
 * function still receives the chosen type at runtime while the graph stays clean. The
 * dropdown's own filter comes from the parameter's MetaStruct metadata.
 *
 * Subclasses UK2Node_CallFunction so the base class builds pins from the signature and
 * handles compilation; this class adds the widget and the retyping.
 */
UCLASS()
class UK2Node_StructPickerCall : public UK2Node_CallFunction
{
	GENERATED_BODY()

public:
	/** Metadata key naming the picker parameter. Presence of this key is the opt-in. */
	static const TCHAR* StructTypePinMetaKey;

	/** The struct chosen in the dropdown. Serialised with the node. */
	UPROPERTY()
	TObjectPtr<UScriptStruct> SelectedStruct = nullptr;

	/** Called by the node widget when the dropdown changes. */
	void SetSelectedStruct(const UScriptStruct* NewStruct);

	/** Base struct the dropdown filters to, read from the parameter's MetaStruct meta. */
	UScriptStruct* GetPickerBaseStruct() const;

	// UEdGraphNode
	virtual void AllocateDefaultPins() override;
	virtual void PostPlacedNewNode() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

	// UK2Node
	virtual void PostReconstructNode() override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;

private:
	/** The hidden picker pin, resolved from the target function's StructTypePin metadata. */
	UEdGraphPin* GetTypePin() const;

	/** Every pin named by the target function's CustomStructureParam metadata. */
	void GetWildcardPins(TArray<UEdGraphPin*>& OutPins) const;

	/** Hide the picker pin and push SelectedStruct into it so the call still receives it. */
	void SyncTypePin();

	/** Stamp SelectedStruct onto the wildcard pins, breaking links that no longer fit. */
	void RefreshWildcardPinTypes();
};