// Copyright arubaii. All rights reserved.

#include "K2Node_StructPickerCall.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "PropertyCustomizationHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetNodes/SGraphNodeK2Base.h"
#include "KismetCompiler.h"
#include "K2Node_CallFunction.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Editor.h"


#define LOCTEXT_NAMESPACE "K2Node_StructPickerCall"

const TCHAR* UK2Node_StructPickerCall::StructTypePinMetaKey = TEXT("StructTypePin");

namespace StructPickerCallPrivate
{
    static const TCHAR* s_CustomStructureParamKey = TEXT("CustomStructureParam");
    static const TCHAR* s_MetaStructKey = TEXT("MetaStruct");

    /** Split a comma-separated metadata value into trimmed pin names. */
    static void ParsePinNames(const FString& meta_value, TArray<FName>& out_names)
    {
       TArray<FString> tokens;
       meta_value.ParseIntoArray(tokens, TEXT(","), true);
       for (FString& token : tokens)
       {
          token.TrimStartAndEndInline();
          if (!token.IsEmpty())
             out_names.Add(FName(*token));
       }
    }

    /** Resolve a struct from a path ("/Script/Module.Name") or a bare name. */
    static UScriptStruct* ResolveStruct(const FString& identifier)
    {
       if (identifier.IsEmpty())
          return nullptr;

       if (UScriptStruct* byPath = LoadObject<UScriptStruct>(nullptr, *identifier))
          return byPath;

       return UClass::TryFindTypeSlow<UScriptStruct>(identifier);
    }

    /** A function qualifies if it is Blueprint-visible and carries both metadata keys. */
    static bool IsPickerFunction(const UFunction* function)
    {
       if (!function)
          return false;

       if (!function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
          return false;

       return function->HasMetaData(UK2Node_StructPickerCall::StructTypePinMetaKey)
          && function->HasMetaData(s_CustomStructureParamKey);
    }
}

// =====================================================================================
//  Node widget - draws the dropdown under the pins
// =====================================================================================

/**
 * The whole reason this class exists: a dropdown is a default-value widget, and default
 * values only render on unconnected INPUT pins. To put one under the pins instead, the
 * node has to draw it itself.
 */
class SGraphNodeStructPickerCall : public SGraphNodeK2Base
{
public:
    SLATE_BEGIN_ARGS(SGraphNodeStructPickerCall) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& in_args, UK2Node_StructPickerCall* in_node)
    {
       GraphNode = in_node;
       m_PickerNode = in_node;
       UpdateGraphNode();
    }

protected:
    virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> main_box) override
    {
       if (!main_box.IsValid() || !m_PickerNode.IsValid())
          return;

       main_box->AddSlot()
          .AutoHeight()
          .Padding(8.f, 2.f, 8.f, 6.f)
          [
             SNew(SBox)
             .MinDesiredWidth(160.f)
             [
                SNew(SStructPropertyEntryBox)
                .AllowNone(true)
                .MetaStruct(GetBaseStruct())
                .SelectedStruct(this, &SGraphNodeStructPickerCall::GetSelectedStruct)
                .OnSetStruct(this, &SGraphNodeStructPickerCall::OnSetStruct)
             ]
          ];
    }

private:
    const UScriptStruct* GetBaseStruct() const
    {
       return m_PickerNode.IsValid() ? m_PickerNode->GetPickerBaseStruct() : nullptr;
    }

    const UScriptStruct* GetSelectedStruct() const
    {
       return m_PickerNode.IsValid() ? m_PickerNode->SelectedStruct : nullptr;
    }

    void OnSetStruct(const UScriptStruct* new_struct)
    {
       if (m_PickerNode.IsValid())
          m_PickerNode->SetSelectedStruct(new_struct);
    }

    TWeakObjectPtr<UK2Node_StructPickerCall> m_PickerNode;
};

// =====================================================================================
//  Pin lookup
// =====================================================================================

UEdGraphPin* UK2Node_StructPickerCall::GetTypePin() const
{
    const UFunction* function = GetTargetFunction();
    if (!function || !function->HasMetaData(StructTypePinMetaKey))
       return nullptr;

    const FString pinName = function->GetMetaData(StructTypePinMetaKey);
    if (pinName.IsEmpty())
       return nullptr;

    return FindPin(FName(*pinName), EGPD_Input);
}

void UK2Node_StructPickerCall::GetWildcardPins(TArray<UEdGraphPin*>& out_pins) const
{
    out_pins.Reset();

    const UFunction* function = GetTargetFunction();
    if (!function || !function->HasMetaData(StructPickerCallPrivate::s_CustomStructureParamKey))
       return;

    TArray<FName> names;
    StructPickerCallPrivate::ParsePinNames(
       function->GetMetaData(StructPickerCallPrivate::s_CustomStructureParamKey), names);

    // Direction is deliberately not filtered: a getter's wildcard is an output, a setter's
    // is an input, and one node type should serve both.
    for (const FName& name : names)
    {
       if (UEdGraphPin* pin = FindPin(name))
          out_pins.Add(pin);
    }
}

UScriptStruct* UK2Node_StructPickerCall::GetPickerBaseStruct() const
{
    const UFunction* function = GetTargetFunction();
    if (!function || !function->HasMetaData(StructTypePinMetaKey))
       return nullptr;

    // Parameter metadata lands on the FProperty, so MetaStruct is read from the parameter
    // itself. Each opted-in function can therefore filter to a different base struct.
    const FName paramName(*function->GetMetaData(StructTypePinMetaKey));
    for (TFieldIterator<FProperty> it(function); it && (it->PropertyFlags & CPF_Parm); ++it)
    {
       if (it->GetFName() != paramName)
          continue;

       if (!it->HasMetaData(StructPickerCallPrivate::s_MetaStructKey))
          return nullptr;

       return StructPickerCallPrivate::ResolveStruct(
          it->GetMetaData(StructPickerCallPrivate::s_MetaStructKey));
    }
    return nullptr;
}

// =====================================================================================
//  Pin state
// =====================================================================================

void UK2Node_StructPickerCall::SyncTypePin()
{
    UEdGraphPin* typePin = GetTypePin();
    if (!typePin)
       return;

    // The dropdown replaces this pin, so it must not be visible or wireable - but it still
    // carries the value the function receives at runtime.
    typePin->bHidden = true;
    typePin->bNotConnectable = true;
    if (typePin->LinkedTo.Num() > 0)
       typePin->BreakAllPinLinks();

    typePin->DefaultObject = SelectedStruct;
}

void UK2Node_StructPickerCall::RefreshWildcardPinTypes()
{
   TArray<UEdGraphPin*> wildcardPins;
   GetWildcardPins(wildcardPins);
   if (wildcardPins.Num() == 0)
   {
      return;
   }

   const FName desiredCategory = SelectedStruct ? FName(TEXT("struct")) : FName(TEXT("wildcard"));
   UObject* desiredSubCategory = SelectedStruct;

   for (UEdGraphPin* pin : wildcardPins)
   {
      pin->PinType.PinCategory = desiredCategory;
      pin->PinType.PinSubCategoryObject = desiredSubCategory;
      pin->PinType.ContainerType = EPinContainerType::None;
   }
}

void UK2Node_StructPickerCall::SetSelectedStruct(const UScriptStruct* new_struct)
{
    if (SelectedStruct == new_struct)
       return;

    Modify();
    SelectedStruct = const_cast<UScriptStruct*>(new_struct);

    // The picker's own combo box is still on the call stack here (this fires from inside
    // SStructPropertyEntryBox's OnSetStruct). RefreshWildcardPinTypes -> NotifyGraphChanged
    // -> UpdateGraphNode rebuilds this node's widget tree, which destroys the very combo
    // box that is currently calling us. Deferring one tick lets that call stack unwind
    // before anything gets rebuilt.
    TWeakObjectPtr<UK2Node_StructPickerCall> weakThis(this);
    GEditor->GetTimerManager()->SetTimerForNextTick([weakThis]()
    {
       UK2Node_StructPickerCall* node = weakThis.Get();
       if (!node)
          return;

       node->SyncTypePin();
       node->RefreshWildcardPinTypes();

       if (UBlueprint* blueprint = node->GetBlueprint())
          FBlueprintEditorUtils::MarkBlueprintAsModified(blueprint);
    });
}

// =====================================================================================
//  Lifecycle
// =====================================================================================

void UK2Node_StructPickerCall::AllocateDefaultPins()
{
    Super::AllocateDefaultPins();

    // Pins were just rebuilt from the function signature, so both the hidden flag and the
    // wildcard types have been reset to their declared state.
    SyncTypePin();
    RefreshWildcardPinTypes();
}

void UK2Node_StructPickerCall::PostPlacedNewNode()
{
    Super::PostPlacedNewNode();

    SyncTypePin();
    RefreshWildcardPinTypes();
}

void UK2Node_StructPickerCall::PostReconstructNode()
{
    Super::PostReconstructNode();

    // Without this the type survives until the first recompile or editor restart, then
    // silently reverts to wildcard and breaks every downstream link.
    SyncTypePin();
    RefreshWildcardPinTypes();
}

void UK2Node_StructPickerCall::ExpandNode(FKismetCompilerContext& compiler_context, UEdGraph* source_graph)
{
    Super::ExpandNode(compiler_context, source_graph);

    const UFunction* targetFunc = GetTargetFunction();
    if (!targetFunc)
    {
        compiler_context.MessageLog.Error(*LOCTEXT("InvalidFunction_Error", "UK2Node_StructPickerCall has no target function assigned.").ToString(), this);
        BreakAllNodeLinks();
        return;
    }

    // 1. Spawn standard UK2Node_CallFunction for the underlying C++ function
    UK2Node_CallFunction* callFuncNode = compiler_context.SpawnIntermediateNode<UK2Node_CallFunction>(this, source_graph);
    callFuncNode->SetFromFunction(targetFunc);
    callFuncNode->AllocateDefaultPins();

    // 2. Pass SelectedStruct to the hidden type selector pin
    UEdGraphPin* typePin = GetTypePin();
    if (typePin)
    {
        if (UEdGraphPin* targetTypePin = callFuncNode->FindPin(typePin->PinName))
        {
            targetTypePin->DefaultObject = SelectedStruct;
        }
    }

    TArray<UEdGraphPin*> wildcardPins;
    GetWildcardPins(wildcardPins);

    // 3. Process all pins generically
    for (UEdGraphPin* originalPin : Pins)
    {
        if (!originalPin || originalPin->bHidden || originalPin == typePin)
            continue;

        UEdGraphPin* targetPin = callFuncNode->FindPin(originalPin->PinName);
        if (!targetPin)
            continue;

        const bool bIsWildcard = wildcardPins.Contains(originalPin);

        if (bIsWildcard && SelectedStruct)
        {
            // Update the underlying function pin's struct type directly so the 
            // Blueprint compiler emits native wildcard struct bytecode
            targetPin->PinType.PinCategory = FName(TEXT("struct"));
            targetPin->PinType.PinSubCategoryObject = SelectedStruct;
        }

        compiler_context.MovePinLinksToIntermediate(*originalPin, *targetPin);
    }

    BreakAllNodeLinks();
}

TSharedPtr<SGraphNode> UK2Node_StructPickerCall::CreateVisualWidget()
{
    return SNew(SGraphNodeStructPickerCall, this);
}

// =====================================================================================
//  Palette
// =====================================================================================

void UK2Node_StructPickerCall::GetMenuActions(FBlueprintActionDatabaseRegistrar& action_registrar) const
{
    const UClass* actionKey = GetClass();
    if (!action_registrar.IsOpenForRegistration(actionKey))
       return;

    // One node per opted-in function, discovered by metadata rather than a hardcoded list.
    for (TObjectIterator<UFunction> it; it; ++it)
    {
       UFunction* function = *it;
       if (!StructPickerCallPrivate::IsPickerFunction(function))
          continue;

       UBlueprintNodeSpawner* spawner = UBlueprintNodeSpawner::Create(GetClass());
       check(spawner);

       const TWeakObjectPtr<UFunction> weakFunction(function);
       spawner->CustomizeNodeDelegate.BindLambda(
          [weakFunction](UEdGraphNode* newNode, bool /*bIsTemplateNode*/)
          {
             UK2Node_StructPickerCall* node = CastChecked<UK2Node_StructPickerCall>(newNode);
             if (const UFunction* resolved = weakFunction.Get())
                node->SetFromFunction(resolved);
          });

       action_registrar.AddBlueprintAction(actionKey, spawner);
    }
}

FText UK2Node_StructPickerCall::GetNodeTitle(ENodeTitleType::Type title_type) const
{
    // The dropdown is visible on the node, so the title does not repeat the chosen type.
    return Super::GetNodeTitle(title_type);
}

void UK2Node_StructPickerCall::NotifyPinConnectionListChanged(UEdGraphPin* pin)
{
   Super::NotifyPinConnectionListChanged(pin);

   // Whenever a connection changes, ensure our wildcard pins retain their SelectedStruct type
   RefreshWildcardPinTypes();
}

#undef LOCTEXT_NAMESPACE