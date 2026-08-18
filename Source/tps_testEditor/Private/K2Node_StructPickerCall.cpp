// Copyright arubaii. All rights reserved.

#include "K2Node_StructPickerCall.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "PropertyCustomizationHelpers.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetNodes/SGraphNodeK2Base.h"
#include "Systems/Item/ItemFragment.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "K2Node_StructPickerCall"

const TCHAR* UK2Node_StructPickerCall::StructTypePinMetaKey = TEXT("StructTypePin");

namespace StructPickerCallPrivate
{
	static const TCHAR* CustomStructureParamKey = TEXT("CustomStructureParam");
	static const TCHAR* MetaStructKey = TEXT("MetaStruct");

	/** Split a comma-separated metadata value into trimmed pin names. */
	static void ParsePinNames(const FString& MetaValue, TArray<FName>& OutNames)
	{
		TArray<FString> tokens;
		MetaValue.ParseIntoArray(tokens, TEXT(","), true);
		for (FString& token : tokens)
		{
			token.TrimStartAndEndInline();
			if (!token.IsEmpty())
				OutNames.Add(FName(*token));
		}
	}

	/** Resolve a struct from a path ("/Script/Module.Name") or a bare name. */
	static UScriptStruct* ResolveStruct(const FString& Identifier)
	{
		if (Identifier.IsEmpty())
			return nullptr;

		if (UScriptStruct* byPath = LoadObject<UScriptStruct>(nullptr, *Identifier))
			return byPath;

		return UClass::TryFindTypeSlow<UScriptStruct>(Identifier);
	}

	/** A function qualifies if it is Blueprint-visible and carries both metadata keys. */
	static bool IsPickerFunction(const UFunction* Function)
	{
		if (!Function)
			return false;

		if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
			return false;

		return Function->HasMetaData(UK2Node_StructPickerCall::StructTypePinMetaKey)
			&& Function->HasMetaData(CustomStructureParamKey);
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

	void Construct(const FArguments& InArgs, UK2Node_StructPickerCall* InNode)
	{
		GraphNode = InNode;
		PickerNode = InNode;
		UpdateGraphNode();
	}

protected:
	virtual void CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox) override
	{
		if (!MainBox.IsValid() || !PickerNode.IsValid())
			return;

		MainBox->AddSlot()
			.AutoHeight()
			.Padding(8.f, 2.f, 8.f, 6.f)
			[
				SNew(SBox)
				.MinDesiredWidth(160.f)
				[
					SNew(SStructPropertyEntryBox)
					.AllowNone(true)
					.MetaStruct(FItemFragment::StaticStruct())
					.SelectedStruct(this, &SGraphNodeStructPickerCall::GetSelectedStruct)
					.OnSetStruct(this, &SGraphNodeStructPickerCall::OnSetStruct)
				]
			];
	}

private:
	const UScriptStruct* GetBaseStruct() const
	{
		return PickerNode.IsValid() ? PickerNode->GetPickerBaseStruct() : nullptr;
	}

	const UScriptStruct* GetSelectedStruct() const
	{
		return PickerNode.IsValid() ? PickerNode->SelectedStruct : nullptr;
	}

	void OnSetStruct(const UScriptStruct* NewStruct)
	{
		if (PickerNode.IsValid())
			PickerNode->SetSelectedStruct(NewStruct);
	}

	TWeakObjectPtr<UK2Node_StructPickerCall> PickerNode;
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

void UK2Node_StructPickerCall::GetWildcardPins(TArray<UEdGraphPin*>& OutPins) const
{
	OutPins.Reset();

	const UFunction* function = GetTargetFunction();
	if (!function || !function->HasMetaData(StructPickerCallPrivate::CustomStructureParamKey))
		return;

	TArray<FName> names;
	StructPickerCallPrivate::ParsePinNames(
		function->GetMetaData(StructPickerCallPrivate::CustomStructureParamKey), names);

	// Direction is deliberately not filtered: a getter's wildcard is an output, a setter's
	// is an input, and one node type should serve both.
	for (const FName& name : names)
	{
		if (UEdGraphPin* pin = FindPin(name))
			OutPins.Add(pin);
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

		if (!it->HasMetaData(StructPickerCallPrivate::MetaStructKey))
			return nullptr;

		return StructPickerCallPrivate::ResolveStruct(
			it->GetMetaData(StructPickerCallPrivate::MetaStructKey));
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
	typePin->DefaultValue = SelectedStruct ? SelectedStruct->GetPathName() : FString();
}

void UK2Node_StructPickerCall::RefreshWildcardPinTypes()
{
	TArray<UEdGraphPin*> wildcardPins;
	GetWildcardPins(wildcardPins);
	if (wildcardPins.Num() == 0)
		return;

	const FName desiredCategory = SelectedStruct ? UEdGraphSchema_K2::PC_Struct : UEdGraphSchema_K2::PC_Wildcard;
	UObject* desiredSubCategory = SelectedStruct;

	bool bChanged = false;
	for (UEdGraphPin* pin : wildcardPins)
	{
		// Skip pins that already match. This guards against reconstruction loops, since
		// retyping a pin can itself trigger another refresh.
		if (pin->PinType.PinCategory == desiredCategory
			&& pin->PinType.PinSubCategoryObject == desiredSubCategory)
			continue;

		// Anything wired to the old type is no longer compatible.
		if (pin->LinkedTo.Num() > 0)
			pin->BreakAllPinLinks();

		pin->PinType.PinCategory = desiredCategory;
		pin->PinType.PinSubCategoryObject = desiredSubCategory;
		pin->PinType.ContainerType = EPinContainerType::None;
		bChanged = true;
	}

	if (!bChanged)
		return;

	if (UEdGraph* graph = GetGraph())
		graph->NotifyGraphChanged();

	if (UBlueprint* blueprint = GetBlueprint())
		FBlueprintEditorUtils::MarkBlueprintAsModified(blueprint);
}

void UK2Node_StructPickerCall::SetSelectedStruct(const UScriptStruct* NewStruct)
{
	if (SelectedStruct == NewStruct)
		return;

	Modify();
	SelectedStruct = const_cast<UScriptStruct*>(NewStruct);

	SyncTypePin();
	RefreshWildcardPinTypes();

	if (UBlueprint* blueprint = GetBlueprint())
		FBlueprintEditorUtils::MarkBlueprintAsModified(blueprint);
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

TSharedPtr<SGraphNode> UK2Node_StructPickerCall::CreateVisualWidget()
{
	return SNew(SGraphNodeStructPickerCall, this);
}

// =====================================================================================
//  Palette
// =====================================================================================

void UK2Node_StructPickerCall::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	const UClass* actionKey = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(actionKey))
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
		spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda(
			[weakFunction](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/)
			{
				UK2Node_StructPickerCall* node = CastChecked<UK2Node_StructPickerCall>(NewNode);
				if (const UFunction* resolved = weakFunction.Get())
					node->SetFromFunction(resolved);
			});

		ActionRegistrar.AddBlueprintAction(actionKey, spawner);
	}
}

FText UK2Node_StructPickerCall::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	// The dropdown is visible on the node, so the title does not repeat the chosen type.
	return Super::GetNodeTitle(TitleType);
}

#undef LOCTEXT_NAMESPACE