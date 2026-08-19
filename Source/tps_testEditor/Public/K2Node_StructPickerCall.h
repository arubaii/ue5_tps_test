// Copyright arubaii. All rights reserved.


// =============================================================================================================
// Wildcard pin type-setter via dropdown menu
// =============================================================================================================


/**
 * To get a dropdown-typed pin on a UFUNCTION via UK2Node_StructPickerCall:
 *
 *   1. Add a UScriptStruct* parameter that will hold the picked type.
 *   2. Add a wildcard struct param (in/out) that the pin type applies to (e.g. FInstancedStruct&).
 *   3. Tag the UFUNCTION meta with:
 *        CustomStructureParam = "<wildcard param name>"
 *        StructTypePin        = "<UScriptStruct* param name>"
 *   4. Optionally filter the dropdown with UPARAM(meta=(MetaStruct="/Script/Module.Base"))
 *      on the UScriptStruct* param.
 *   5. Add BlueprintInternalUseOnly="true" if you don't want the plain wildcard node
 *      (from the default schema) showing up alongside the dropdown version.
 *
 * The node auto-discovers any UFUNCTION carrying both meta keys - no per-function C++
 * thunks, DECLARE_FUNCTION blocks, or manual stack popping needed beyond the standard
 * UFUNCTION declaration itself.
 * 
 * =============================================================================================================
 * =============================================================================================================
 * 
 * EXAMPLE: a typed "Get" for FMyBaseFragment-derived structs, dropdown-driven.
 *
 *   UFUNCTION(BlueprintPure, Category = "Example",
 *       meta = (CustomStructureParam = "OutValue",
 *               StructTypePin        = "ValueType",
 *               DisplayName          = "Get Typed Value", // Blueprint function generated name won't space otherwise
 *               BlueprintInternalUseOnly = "true"))
 *   static bool GetTypedValue(const FMyContainer& Container,
 *       UPARAM(meta = (MetaStruct = "/Script/MyModule.MyBaseFragment")) UScriptStruct* ValueType,
 *       FInstancedStruct& OutValue);
 *
 * In Blueprint: search "Get Typed Value", pick a struct in the dropdown (filtered to
 * FMyBaseFragment descendants), and OutValue's pin retypes to that struct - no
 * Promote-to-Variable, no wildcard, and no C++ native thunks required.
 */

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
 *     UFUNCTION(BlueprintPure, meta = (
 *         CustomStructureParam = "OutValue",
 *         StructTypePin        = "ValueType"))
 *     static bool GetSomething(const FThing& Thing, UScriptStruct* ValueType, FInstancedStruct& OutValue);
 *
 * That parameter's pin is HIDDEN on the node: the dropdown writes into it, so the
 * function still receives the chosen type at runtime while the graph stays clean. The
 * dropdown's own filter comes from the parameter's MetaStruct metadata.
 *
 * Subclasses UK2Node_CallFunction so the base class builds pins from the signature;
 * this class adds the widget, retyping, and handles compile-time graph expansion via ExpandNode.
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
    void SetSelectedStruct(const UScriptStruct* new_struct);

    /** Base struct the dropdown filters to, read from the parameter's MetaStruct meta. */
    UScriptStruct* GetPickerBaseStruct() const;

    // UEdGraphNode
    virtual void AllocateDefaultPins() override;
    virtual void PostPlacedNewNode() override;
    virtual FText GetNodeTitle(ENodeTitleType::Type title_type) const override;
    virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

    // UK2Node
    virtual void PostReconstructNode() override;
    virtual void ExpandNode(FKismetCompilerContext& compiler_context, UEdGraph* source_graph) override;
    virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& action_registrar) const override;

private:
    /** The hidden picker pin, resolved from the target function's StructTypePin metadata. */
    UEdGraphPin* GetTypePin() const;

    /** Every pin named by the target function's CustomStructureParam metadata. */
    void GetWildcardPins(TArray<UEdGraphPin*>& out_pins) const;

    /** Hide the picker pin and push SelectedStruct into it so the call still receives it. */
    void SyncTypePin();

    /** Stamp SelectedStruct onto the wildcard pins, breaking links that no longer fit. */
    void RefreshWildcardPinTypes();
    
    void NotifyPinConnectionListChanged(UEdGraphPin* pin);
};