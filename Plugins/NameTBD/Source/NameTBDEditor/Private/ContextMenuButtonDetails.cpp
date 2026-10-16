// Copyright AR GameSystems 2026. All rights reserved.

#include "ContextMenuButtonDetails.h"

#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Widgets/StyledButtons.h"
#include "NameTBD/Widgets/WidgetEditorHooks.h"
#include "StyledButtonsEditor.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#include "WidgetBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "ScopedTransaction.h"
#include "Containers/Ticker.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Blueprint/WidgetTree.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "ContextMenuButtonDetails"

namespace ContextMenuButtonDetailsPrivate
{
	FDelegateHandle RenamedHandle;

	/** The editor's toast in the bottom right: Title, then one line per change. */
	void Notify(const FText& Title, const TArray<FText>& Lines, bool bSuccess)
	{
		FNotificationInfo Info(Lines.Num() > 0
			? FText::Format(LOCTEXT("NotifyFormat", "{0}\n{1}"), Title, FText::Join(FText::FromString(TEXT("\n")), Lines))
			: Title);
		Info.ExpireDuration = bSuccess ? 5.f : 8.f;
		Info.bFireAndForget = true;
		Info.bUseSuccessFailIcons = true;
		Info.bUseLargeFont = false;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	/** Whether Name is free for a new member of Blueprint (no variable, dispatcher, function or graph by that name). */
	bool IsNameFree(const UWidgetBlueprint* Blueprint, FName Name)
	{
		return FKismetNameValidator(Blueprint).IsValid(Name) == EValidatorResult::Ok;
	}
}

void FContextMenuButtonDetails::Register()
{
	using namespace ContextMenuButtonDetailsPrivate;
	if (!RenamedHandle.IsValid())
	{
		RenamedHandle = UContextMenuButton::OnRenamedInEditor.AddStatic(&FContextMenuButtonDetails::HandleButtonRenamed);
	}
}

void FContextMenuButtonDetails::Unregister()
{
	using namespace ContextMenuButtonDetailsPrivate;
	UContextMenuButton::OnRenamedInEditor.Remove(RenamedHandle);
	RenamedHandle.Reset();
}

void FContextMenuButtonDetails::HandleButtonRenamed(UContextMenuButton* Button, FName OldName)
{
	// The Widget Blueprint's own button renamed in the designer (its preview copy, renamed alongside it, is skipped). Undo / redo
	// restore names without a rename; the Details catch those up when they open (ReconcileRenamed).
	if (!Button || !Button->bInstanceOverridable || GIsTransacting || !Button->GetTypedOuter<UWidgetBlueprint>())
	{
		return;
	}

	// Next tick: the designer is still in the middle of its own rename (and compile).
	const TWeakObjectPtr<UContextMenuButton> WeakButton(Button);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakButton, OldName](float)
	{
		if (UContextMenuButton* Renamed = WeakButton.Get())
		{
			RenameInstanceOverridable(Renamed, OldName);
		}
		return false;
	}));
}

TSharedRef<IDetailCustomization> FContextMenuButtonDetails::MakeInstance()
{
	return MakeShared<FContextMenuButtonDetails>();
}

FContextMenuButtonDetails::~FContextMenuButtonDetails()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
}

void FContextMenuButtonDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	WeakBuilder = DetailBuilder;
	if (DetailBuilder.IsValid())
	{
		CustomizeDetails(*DetailBuilder);
	}
}

void FContextMenuButtonDetails::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (!Object || (Event.ChangeType & EPropertyChangeType::Interactive) != 0
		|| !CustomizedButtons.ContainsByPredicate([Object](const TWeakObjectPtr<UContextMenuButton>& Button) { return Button.Get() == Object; }))
	{
		return;
	}

	const FName Name = Event.GetPropertyName();
	const FName Member = Event.GetMemberPropertyName();

	// Instance Overridable: the Widget Blueprint is changed (and compiled) next tick, outside the property edit.
	if (Name == GET_MEMBER_NAME_CHECKED(UContextMenuButton, bInstanceOverridable))
	{
		const TWeakObjectPtr<UContextMenuButton> WeakButton(Cast<UContextMenuButton>(Object));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakButton](float)
		{
			if (UContextMenuButton* Button = WeakButton.Get())
			{
				SyncInstanceOverridable(Button);
			}
			return false;
		}));
		return;
	}

	// What a target aims at, the Shape Style, or a preset picked decides whether Style Settings or a Shape shows - and Style
	// Settings newly laid out for another kind of shape have other rows. The property bag's rows are only made again when the
	// rows holding it are.
	const bool bArrayChange = (Event.ChangeType & (EPropertyChangeType::ArrayAdd | EPropertyChangeType::ArrayRemove | EPropertyChangeType::ArrayClear | EPropertyChangeType::Duplicate)) != 0;
	const bool bShapeTargets = Member == GET_MEMBER_NAME_CHECKED(UContextMenuButton, ShapeTargets);
	const bool bDesignTargets = Member == GET_MEMBER_NAME_CHECKED(UContextMenuButton, DesignTargets);

	// Switching a state's override on or off gives it Style Settings or takes them away.
	static const FName OverrideToggles[] = {
		GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideNormalShape),
		GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideHoveredShape),
		GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverridePressedShape),
		GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideDisabledShape),
		GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideEnabledShape) };
	bool bOverrideToggled = false;
	for (const FName& Toggle : OverrideToggles)
	{
		bOverrideToggled |= Name == Toggle;
	}

	const bool bShapeLayoutChange = bShapeTargets && (Name == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, TargetWidgetName)
		|| Name == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, ShapeStyle)
		|| Name == GET_MEMBER_NAME_CHECKED(FContextMenuShapeStateSettings, ShapeStyleClass)
		|| Name == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, PresetAsset)
		|| bArrayChange || bOverrideToggled);

	// A target's change rebuilds only that list's rows, so the panel stays where it is; a Button Preset picked changes the whole
	// button and lays the panel out again.
	const bool bDesignLayoutChange = bDesignTargets && Name == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, PresetAsset);
	const bool bButtonPresetPicked = Member == GET_MEMBER_NAME_CHECKED(UContextMenuButton, ButtonPreset);
	if (bShapeLayoutChange || bDesignLayoutChange || bButtonPresetPicked)
	{
		const TWeakPtr<IDetailLayoutBuilder> Weak = WeakBuilder;
		TWeakPtr<IPropertyHandle> WeakList;
		if (!bButtonPresetPicked)
		{
			WeakList = bShapeLayoutChange ? ShapeTargetsHandle : DesignTargetsHandle;
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak, WeakList](float)
		{
			const TSharedPtr<IPropertyHandle> ListHandle = WeakList.Pin();
			if (ListHandle.IsValid() && ListHandle->IsValidHandle())
			{
				ListHandle->RequestRebuildChildren();
			}
			else if (const TSharedPtr<IDetailLayoutBuilder> Builder = Weak.Pin())
			{
				Builder->ForceRefreshDetails();
			}
			return false;
		}));
	}
}

TArray<FContextMenuButtonDetails::FDispatcherEventInfo> FContextMenuButtonDetails::GetEventInfos()
{
	// Suffixes chosen to satisfy the runtime matcher's keywords (Click / Enabled / Hover / Press) while
	// reading like something a person would have named. Parameter order is the order the button broadcasts
	// in - they are delivered positionally, so this is the part worth not getting wrong by hand.
	TArray<FDispatcherEventInfo> Infos;

	{
		FDispatcherEventInfo& Info = Infos.AddDefaulted_GetRef();
		Info.NameSuffix = TEXT("Clicked");
		Info.ParameterNames = { TEXT("bShouldDestroyCard"), TEXT("bEnabling") };
		Info.Description = LOCTEXT("ClickedDescription", "Called by {0} when it is clicked.");
	}
	{
		FDispatcherEventInfo& Info = Infos.AddDefaulted_GetRef();
		Info.NameSuffix = TEXT("EnabledStateChanged");
		Info.ParameterNames = { TEXT("bIsEnabled") };
		Info.Description = LOCTEXT("EnabledDescription", "Called by {0} when it is switched on or off (Enabled state).");
	}
	{
		FDispatcherEventInfo& Info = Infos.AddDefaulted_GetRef();
		Info.NameSuffix = TEXT("Hovered");
		Info.ParameterNames = { TEXT("bHovered") };
		Info.Description = LOCTEXT("HoveredDescription", "Called by {0} when the pointer moves onto it or off it.");
	}
	{
		FDispatcherEventInfo& Info = Infos.AddDefaulted_GetRef();
		Info.NameSuffix = TEXT("PressCompleted");
		Info.ParameterNames = {};
		Info.Description = LOCTEXT("PressCompletedDescription", "Called by {0} when a press completes (a hold reaching its end).");
	}

	return Infos;
}

FName FContextMenuButtonDetails::GetDispatcherName(const UContextMenuButton* Button, const FDispatcherEventInfo& EventInfo)
{
	return Button ? GetDispatcherName(Button->GetFName(), EventInfo) : NAME_None;
}

FName FContextMenuButtonDetails::GetDispatcherName(FName ButtonName, const FDispatcherEventInfo& EventInfo)
{
	return ButtonName.IsNone() ? NAME_None : FName(*FString::Printf(TEXT("On%s%s"), *ButtonName.ToString(), EventInfo.NameSuffix));
}

bool FContextMenuButtonDetails::IsOwnVariable(const UWidgetBlueprint* Blueprint, FName VariableName)
{
	return Blueprint && Blueprint->NewVariables.ContainsByPredicate([VariableName](const FBPVariableDescription& Variable) { return Variable.VarName == VariableName; });
}

UEdGraph* FContextMenuButtonDetails::FindDispatcherGraph(UWidgetBlueprint* Blueprint, FName DispatcherName)
{
	if (!Blueprint)
	{
		return nullptr;
	}
	for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
	{
		if (Graph && Graph->GetFName() == DispatcherName)
		{
			return Graph;
		}
	}
	return nullptr;
}

void FContextMenuButtonDetails::DescribeDispatcher(UWidgetBlueprint* Blueprint, FName DispatcherName, FName ButtonName, const FDispatcherEventInfo& EventInfo)
{
	UEdGraph* Graph = FindDispatcherGraph(Blueprint, DispatcherName);
	if (!Graph)
	{
		return;
	}
	TArray<UK2Node_FunctionEntry*> EntryNodes;
	Graph->GetNodesOfClass(EntryNodes);
	for (UK2Node_FunctionEntry* Entry : EntryNodes)
	{
		if (Entry)
		{
			Entry->Modify();
			Entry->MetaData.ToolTip = FText::Format(LOCTEXT("DispatcherDescription",
				"{0}\n\nMade by Instance Overridable on {1}, and renamed with it. Don't rename or delete it: untick Instance Overridable on the button to remove it."),
				FText::Format(EventInfo.Description, FText::FromName(ButtonName)), FText::FromName(ButtonName));
		}
	}
}

void FContextMenuButtonDetails::DescribeOverridesVariable(UWidgetBlueprint* Blueprint, FName VariableName, FName ButtonName)
{
	if (!Blueprint)
	{
		return;
	}
	const FText Description = FText::Format(LOCTEXT("OverridesVariableDescription",
		"Generated when Instance Overridable was ticked on {0}: each placement of this widget restyles {0} here (its Shape and Design Targets, Show Preview) - set it in a placement's Details. Don't delete or rename this variable: it is renamed with the button, and removed by unticking Instance Overridable on it."),
		FText::FromName(ButtonName));
	FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, FBlueprintMetadata::MD_Tooltip, Description.ToString());
}

UWidgetBlueprint* FContextMenuButtonDetails::FindOwningWidgetBlueprint(const UContextMenuButton* Button)
{
	if (!Button)
	{
		return nullptr;
	}

	// The template in the Widget Blueprint's own tree reaches the blueprint through its outers.
	if (UWidgetBlueprint* Direct = Button->GetTypedOuter<UWidgetBlueprint>())
	{
		return Direct;
	}

	// A preview instance is outered to its generated class instead, which knows what generated it. Worth
	// covering: the panel can end up showing the preview depending on how the selection was made.
#if WITH_EDITORONLY_DATA
	if (const UClass* GeneratedClass = Button->WidgetGeneratedByClass.Get())
	{
		return Cast<UWidgetBlueprint>(GeneratedClass->ClassGeneratedBy);
	}
#endif

	return nullptr;
}

bool FContextMenuButtonDetails::DoesDispatcherExist(const UWidgetBlueprint* Blueprint, FName DispatcherName)
{
	if (!Blueprint)
	{
		return false;
	}

	// Checked against the generated class rather than the blueprint's variable list: an inherited
	// dispatcher is just as usable as a locally declared one, and re-adding it would fail to compile.
	if (const UClass* GeneratedClass = Blueprint->GeneratedClass)
	{
		for (TFieldIterator<FMulticastDelegateProperty> It(GeneratedClass); It; ++It)
		{
			if (It->GetFName() == DispatcherName)
			{
				return true;
			}
		}
	}

	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == DispatcherName)
		{
			return true;
		}
	}

	return false;
}

bool FContextMenuButtonDetails::AddDispatcher(UWidgetBlueprint* Blueprint, FName DispatcherName, const TArray<FName>& BoolParameterNames)
{
	if (!Blueprint)
	{
		return false;
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// An Event Dispatcher is two things: a multicast delegate member variable, and a signature graph that
	// describes its parameters. Both have to exist, which is why this can't be a single AddMemberVariable.
	FEdGraphPinType DelegateType;
	DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;

	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, DispatcherName, DelegateType))
	{
		return false;
	}

	UEdGraph* SignatureGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		DispatcherName,
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass());

	if (!SignatureGraph)
	{
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, DispatcherName);
		return false;
	}

	// The graph describes the signature and is not something anyone edits as a graph.
	SignatureGraph->bEditable = false;

	K2Schema->CreateDefaultNodesForGraph(*SignatureGraph);
	K2Schema->CreateFunctionGraphTerminators(*SignatureGraph, static_cast<UClass*>(nullptr));
	K2Schema->AddExtraFunctionFlags(SignatureGraph, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
	K2Schema->MarkFunctionEntryAsEditable(SignatureGraph, true);

	Blueprint->DelegateSignatureGraphs.Add(SignatureGraph);

	// Parameters are pins on the entry node. Output direction: from the signature's point of view they are
	// values it hands to whoever bound it.
	if (BoolParameterNames.Num() > 0)
	{
		TArray<UK2Node_FunctionEntry*> EntryNodes;
		SignatureGraph->GetNodesOfClass(EntryNodes);

		if (EntryNodes.Num() > 0 && EntryNodes[0])
		{
			FEdGraphPinType BoolType;
			BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;

			for (const FName& ParameterName : BoolParameterNames)
			{
				EntryNodes[0]->CreateUserDefinedPin(ParameterName, BoolType, EGPD_Output);
			}
		}
	}

	return true;
}

FName FContextMenuButtonDetails::GetOverridesVariableName(const UContextMenuButton* Button)
{
	return Button ? GetOverridesVariableName(Button->GetFName()) : NAME_None;
}

FName FContextMenuButtonDetails::GetOverridesVariableName(FName ButtonName)
{
	return ButtonName.IsNone() ? NAME_None : FName(*FString::Printf(TEXT("%sInstanceOverrides"), *ButtonName.ToString()));
}

bool FContextMenuButtonDetails::DoesOverridesVariableExist(const UWidgetBlueprint* Blueprint, const UContextMenuButton* Button)
{
	if (!Blueprint || !Button)
	{
		return false;
	}

	const UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (!GeneratedClass)
	{
		return false;
	}

	const UStruct* OverrideStruct = FContextMenuButtonInstanceOverrides::StaticStruct();
	const FName ExpectedName = GetOverridesVariableName(Button);

	for (TFieldIterator<FStructProperty> It(GeneratedClass); It; ++It)
	{
		if (It->Struct != OverrideStruct)
		{
			continue;
		}

		// By name, or by a struct that has already claimed this button. Either way adding another would
		// give the button two candidates and it would refuse to pick, which is worse than doing nothing.
		if (It->GetFName() == ExpectedName)
		{
			return true;
		}

		if (const UObject* CDO = GeneratedClass->GetDefaultObject())
		{
			const FContextMenuButtonInstanceOverrides* Value = It->ContainerPtrToValuePtr<FContextMenuButtonInstanceOverrides>(CDO);
			if (Value && Value->TargetButtonName == Button->GetFName())
			{
				return true;
			}
		}
	}

	return false;
}

TArray<FName> FContextMenuButtonDetails::FindOwnOverridesVariables(const UWidgetBlueprint* Blueprint, FName ButtonName)
{
	TArray<FName> Found;
	if (!Blueprint || ButtonName.IsNone())
	{
		return Found;
	}

	const UObject* CDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
	const FName ExpectedName = GetOverridesVariableName(ButtonName);
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarType.PinSubCategoryObject.Get() != FContextMenuButtonInstanceOverrides::StaticStruct())
		{
			continue;
		}
		bool bClaims = Variable.VarName == ExpectedName;
		if (!bClaims && CDO)
		{
			if (const FStructProperty* Property = FindFProperty<FStructProperty>(CDO->GetClass(), Variable.VarName))
			{
				const FContextMenuButtonInstanceOverrides* Value = Property->ContainerPtrToValuePtr<FContextMenuButtonInstanceOverrides>(CDO);
				bClaims = Value && Value->TargetButtonName == ButtonName;
			}
		}
		if (bClaims)
		{
			Found.Add(Variable.VarName);
		}
	}
	return Found;
}

void FContextMenuButtonDetails::StampTargetButtonName(UWidgetBlueprint* Blueprint, FName VariableName, FName TargetButtonName)
{
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return;
	}

	UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
	FStructProperty* Prop = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, VariableName);
	if (!CDO || !Prop)
	{
		return;
	}

	FContextMenuButtonInstanceOverrides* Value = Prop->ContainerPtrToValuePtr<FContextMenuButtonInstanceOverrides>(CDO);
	if (!Value)
	{
		return;
	}

	Value->TargetButtonName = TargetButtonName;

	// Written to the CDO AND serialised back into the blueprint's variable description. The CDO alone
	// would be correct right now and gone on the next full recompile, which rebuilds defaults from these
	// strings - that is the difference between a default that sticks and one that quietly disappears a
	// week later.
	FString DefaultValueString;
	if (FBlueprintEditorUtils::PropertyValueToString(Prop, reinterpret_cast<const uint8*>(CDO), DefaultValueString, Blueprint))
	{
		for (FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (Variable.VarName == VariableName)
			{
				Variable.DefaultValue = DefaultValueString;
				break;
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
}

void FContextMenuButtonDetails::SyncInstanceOverridable(UContextMenuButton* Button)
{
	using namespace ContextMenuButtonDetailsPrivate;
	UWidgetBlueprint* Blueprint = FindOwningWidgetBlueprint(Button);
	if (!Blueprint)
	{
		return;
	}
	const FName ButtonName = Button->GetFName();
	const FText BlueprintName = FText::FromString(Blueprint->GetName());
	TArray<FText> Lines;

	if (Button->bInstanceOverridable)
	{
		const FScopedTransaction Transaction(LOCTEXT("MakeOverridable", "Make Button Instance Overridable"));
		Blueprint->Modify();

		// The events placements bind: one dispatcher per event the button broadcasts by itself, each with its parameters.
		for (const FDispatcherEventInfo& EventInfo : GetEventInfos())
		{
			const FName DispatcherName = GetDispatcherName(ButtonName, EventInfo);
			if (!DoesDispatcherExist(Blueprint, DispatcherName) && AddDispatcher(Blueprint, DispatcherName, EventInfo.ParameterNames))
			{
				DescribeDispatcher(Blueprint, DispatcherName, ButtonName, EventInfo);
				Lines.Add(FText::Format(LOCTEXT("AddedDispatcherLine", "Event Dispatcher {0}"), FText::FromName(DispatcherName)));
			}
		}

		// The overrides: a variable placements can set (Instance Editable), claimed by this button.
		const FName VariableName = GetOverridesVariableName(ButtonName);
		bool bAddedVariable = false;
		if (!DoesOverridesVariableExist(Blueprint, Button))
		{
			FEdGraphPinType StructType;
			StructType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			StructType.PinSubCategoryObject = FContextMenuButtonInstanceOverrides::StaticStruct();
			if (FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, StructType))
			{
				FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VariableName, /*bNewBlueprintOnly=*/false);
				DescribeOverridesVariable(Blueprint, VariableName, ButtonName);
				bAddedVariable = true;
				Lines.Add(FText::Format(LOCTEXT("AddedVariableLine", "Variable {0}"), FText::FromName(VariableName)));
			}
		}

		if (Lines.Num() == 0)
		{
			return;
		}

		// Compiled now rather than left dirty, so the dispatchers are bindable and the variable shows on placements at once. The
		// variable's default is stamped after: its CDO property only exists once compiled.
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (bAddedVariable)
		{
			StampTargetButtonName(Blueprint, VariableName, ButtonName);
		}
		Notify(FText::Format(LOCTEXT("AddedTitle", "{0}: added for {1} (Instance Overridable)"), BlueprintName, FText::FromName(ButtonName)), Lines, true);
		return;
	}

	// Unticked: everything it made goes - the overrides variable and the button's dispatchers declared in this blueprint (undo brings
	// them back). Nodes elsewhere bound to a dispatcher show as broken until removed.
	const TArray<FName> Variables = FindOwnOverridesVariables(Blueprint, ButtonName);
	TArray<FName> Dispatchers;
	for (const FDispatcherEventInfo& EventInfo : GetEventInfos())
	{
		const FName DispatcherName = GetDispatcherName(ButtonName, EventInfo);
		if (IsOwnVariable(Blueprint, DispatcherName))
		{
			Dispatchers.Add(DispatcherName);
		}
	}
	if (Variables.Num() == 0 && Dispatchers.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("MakeNotOverridable", "Make Button Not Instance Overridable"));
	Blueprint->Modify();
	for (const FName& DispatcherName : Dispatchers)
	{
		// As the My Blueprint panel deletes one: the variable, then its signature graph.
		UEdGraph* SignatureGraph = FindDispatcherGraph(Blueprint, DispatcherName);
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, DispatcherName);
		if (SignatureGraph)
		{
			FBlueprintEditorUtils::RemoveGraph(Blueprint, SignatureGraph, EGraphRemoveFlags::None);
		}
		Lines.Add(FText::Format(LOCTEXT("RemovedDispatcherLine", "Event Dispatcher {0}"), FText::FromName(DispatcherName)));
	}
	for (const FName& VariableName : Variables)
	{
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VariableName);
		Lines.Add(FText::Format(LOCTEXT("RemovedVariableLine", "Variable {0}"), FText::FromName(VariableName)));
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	Notify(FText::Format(LOCTEXT("RemovedTitle", "{0}: removed for {1} (Instance Overridable off)"), BlueprintName, FText::FromName(ButtonName)), Lines, true);
}

void FContextMenuButtonDetails::RenameInstanceOverridable(UContextMenuButton* Button, FName OldName)
{
	using namespace ContextMenuButtonDetailsPrivate;
	UWidgetBlueprint* Blueprint = FindOwningWidgetBlueprint(Button);
	const FName NewName = Button ? Button->GetFName() : NAME_None;
	if (!Blueprint || OldName.IsNone() || OldName == NewName)
	{
		return;
	}

	// What there is to rename, worked out first: nothing made for the old name, nothing to do (and no transaction).
	struct FRename
	{
		FName From;
		FName To;
		int32 EventIndex;
	};
	const TArray<FDispatcherEventInfo> EventInfos = GetEventInfos();
	TArray<FRename> Renames;
	for (int32 Index = 0; Index < EventInfos.Num(); ++Index)
	{
		const FName From = GetDispatcherName(OldName, EventInfos[Index]);
		if (IsOwnVariable(Blueprint, From))
		{
			Renames.Add({ From, GetDispatcherName(NewName, EventInfos[Index]), Index });
		}
	}
	const TArray<FName> Variables = FindOwnOverridesVariables(Blueprint, OldName);
	for (const FName& VariableName : Variables)
	{
		// Only the name it was given; one named by hand keeps its name and is just pointed at the new name.
		if (VariableName == GetOverridesVariableName(OldName))
		{
			Renames.Add({ VariableName, GetOverridesVariableName(NewName), INDEX_NONE });
		}
	}
	if (Renames.Num() == 0 && Variables.Num() == 0)
	{
		return;
	}

	const FText BlueprintName = FText::FromString(Blueprint->GetName());
	TArray<FText> Lines;
	TArray<FText> Failed;
	TArray<FName> FinalVariables = Variables;
	{
		const FScopedTransaction Transaction(LOCTEXT("RenameOverridable", "Rename Instance Overridable Members"));
		Blueprint->Modify();
		for (const FRename& Rename : Renames)
		{
			if (!IsNameFree(Blueprint, Rename.To))
			{
				Failed.Add(FText::Format(LOCTEXT("RenameTakenLine", "{0} kept: {1} is already taken"), FText::FromName(Rename.From), FText::FromName(Rename.To)));
				continue;
			}
			UEdGraph* SignatureGraph = Rename.EventIndex != INDEX_NONE ? FindDispatcherGraph(Blueprint, Rename.From) : nullptr;
			FBlueprintEditorUtils::RenameMemberVariable(Blueprint, Rename.From, Rename.To);
			if (SignatureGraph && SignatureGraph->GetFName() != Rename.To)
			{
				FBlueprintEditorUtils::RenameGraph(SignatureGraph, Rename.To.ToString());
			}
			if (Rename.EventIndex != INDEX_NONE)
			{
				DescribeDispatcher(Blueprint, Rename.To, NewName, EventInfos[Rename.EventIndex]);
			}
			else
			{
				FinalVariables.Remove(Rename.From);
				FinalVariables.Add(Rename.To);
			}
			Lines.Add(FText::Format(LOCTEXT("RenamedLine", "{0} -> {1}"), FText::FromName(Rename.From), FText::FromName(Rename.To)));
		}
		for (const FName& VariableName : FinalVariables)
		{
			DescribeOverridesVariable(Blueprint, VariableName, NewName);
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		// Pointed at the new name (the default lives on the compiled class, so after the compile).
		for (const FName& VariableName : FinalVariables)
		{
			StampTargetButtonName(Blueprint, VariableName, NewName);
			if (!Lines.ContainsByPredicate([VariableName](const FText& Line) { return Line.ToString().EndsWith(VariableName.ToString()); }))
			{
				Lines.Add(FText::Format(LOCTEXT("RepointedLine", "{0} now points at {1}"), FText::FromName(VariableName), FText::FromName(NewName)));
			}
		}
	}

	const FText Title = FText::Format(LOCTEXT("RenamedTitle", "{0}: {1} renamed to {2} - Instance Overridable updated"), BlueprintName, FText::FromName(OldName), FText::FromName(NewName));
	Lines.Append(Failed);
	Notify(Title, Lines, Failed.Num() == 0);
}

void FContextMenuButtonDetails::ReconcileRenamed(UContextMenuButton* Button)
{
	UWidgetBlueprint* Blueprint = FindOwningWidgetBlueprint(Button);
	if (!Button || !Button->bInstanceOverridable || !Blueprint || !Blueprint->WidgetTree || !Blueprint->GeneratedClass
		|| DoesOverridesVariableExist(Blueprint, Button))
	{
		return;
	}
	for (const FDispatcherEventInfo& EventInfo : GetEventInfos())
	{
		if (IsOwnVariable(Blueprint, GetDispatcherName(Button, EventInfo)))
		{
			return;
		}
	}

	// An overrides variable of this blueprint pointed at a widget that no longer exists (a rename the designer didn't report, an
	// undone rename): when it is the only one, it was this button's.
	const UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
	TArray<FName> Orphans;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarType.PinSubCategoryObject.Get() != FContextMenuButtonInstanceOverrides::StaticStruct())
		{
			continue;
		}
		const FStructProperty* Property = CDO ? FindFProperty<FStructProperty>(CDO->GetClass(), Variable.VarName) : nullptr;
		const FContextMenuButtonInstanceOverrides* Value = Property ? Property->ContainerPtrToValuePtr<FContextMenuButtonInstanceOverrides>(CDO) : nullptr;
		if (Value && !Value->TargetButtonName.IsNone() && !Blueprint->WidgetTree->FindWidget(Value->TargetButtonName))
		{
			Orphans.AddUnique(Value->TargetButtonName);
		}
	}
	if (Orphans.Num() != 1)
	{
		return;
	}

	const TWeakObjectPtr<UContextMenuButton> WeakButton(Button);
	const FName OldName = Orphans[0];
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakButton, OldName](float)
	{
		// Still nothing made for its name now (Instance Overridable may have made it meanwhile).
		UContextMenuButton* Renamed = WeakButton.Get();
		if (Renamed && Renamed->bInstanceOverridable && !DoesOverridesVariableExist(FindOwningWidgetBlueprint(Renamed), Renamed))
		{
			RenameInstanceOverridable(Renamed, OldName);
		}
		return false;
	}));
}

FText FContextMenuButtonDetails::GetInstanceOverridableTooltip() const
{
	return FText::Format(LOCTEXT("OverridableTooltip", "{0}\n\n{1}"), GetInstanceOverridableStatus(),
		LOCTEXT("OverridableAbout", "Ticked, the Widget Blueprint that contains this button gets an Instance Editable '{Button}InstanceOverrides' variable already pointed at it, and the event dispatchers the button calls by itself; renaming the button renames them. Unticked, the variable and the dispatchers are removed again."));
}

FText FContextMenuButtonDetails::GetInstanceOverridableStatus() const
{
	const UContextMenuButton* Button = CustomizedButtons.Num() > 0 ? CustomizedButtons[0].Get() : nullptr;
	UWidgetBlueprint* Blueprint = FindOwningWidgetBlueprint(Button);
	if (!Button || !Blueprint)
	{
		return LOCTEXT("OverridableNoBlueprint", "Select the button in the Widget Blueprint that contains it.");
	}

	const bool bHasVariable = DoesOverridesVariableExist(Blueprint, Button);
	int32 Dispatchers = 0;
	for (const FDispatcherEventInfo& EventInfo : GetEventInfos())
	{
		Dispatchers += DoesDispatcherExist(Blueprint, GetDispatcherName(Button, EventInfo)) ? 1 : 0;
	}

	if (!Button->bInstanceOverridable && !bHasVariable)
	{
		return FText::Format(LOCTEXT("OverridableOff",
			"Placements of {0} can't restyle this button. Tick Instance Overridable to let them, and to give them its events."),
			FText::FromString(Blueprint->GetName()));
	}

	return FText::Format(LOCTEXT("OverridableOn",
		"Each placement of {0} restyles this button under '{1}' in its Details{2}, and binds its events ({3} of 4 dispatchers: On{4}Clicked, ...EnabledStateChanged, ...Hovered, ...PressCompleted)."),
		FText::FromString(Blueprint->GetName()),
		FText::FromName(GetOverridesVariableName(Button)),
		bHasVariable ? FText::GetEmpty() : LOCTEXT("OverridableVariablePending", " (variable not made yet - compile, or tick the box again)"),
		FText::AsNumber(Dispatchers),
		FText::FromString(Button->GetName()));
}

UContextMenuButton* FContextMenuButtonDetails::GetAuthoredButton(UContextMenuButton* Button)
{
	UContextMenuButton* Template = Cast<UContextMenuButton>(WidgetEditorHooks::FindDesignerTemplate(Button));
	return Template ? Template : Button;
}

TSharedRef<SWidget> FContextMenuButtonDetails::MakeSaveTargetPresetMenu(bool bShape) const
{
	FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	const TWeakObjectPtr<UContextMenuButton> WeakButton = CustomizedButtons.Num() > 0 ? CustomizedButtons[0] : nullptr;
	UContextMenuButton* Button = WeakButton.Get();
	UContextMenuButton* Authored = Button ? GetAuthoredButton(Button) : nullptr;
	const int32 Count = Authored ? (bShape ? Authored->ShapeTargets.Num() : Authored->DesignTargets.Num()) : 0;

	Menu.BeginSection(NAME_None, bShape ? LOCTEXT("SaveShapeHeading", "Save Shape Target As Preset") : LOCTEXT("SaveDesignHeading", "Save Design Target As Preset"));
	if (Count == 0)
	{
		Menu.AddWidget(SNew(STextBlock).Text(LOCTEXT("NoTargets", "No targets yet.")).Font(IDetailLayoutBuilder::GetDetailFont()), FText::GetEmpty());
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FName TargetName = bShape ? Authored->ShapeTargets[Index].TargetWidgetName : Authored->DesignTargets[Index].TargetWidgetName;
		const FName TargetId = bShape ? Authored->ShapeTargets[Index].TargetId : Authored->DesignTargets[Index].TargetId;
		const FText Label = TargetId.IsNone()
			? FText::Format(LOCTEXT("TargetEntry", "{0}: {1}"), FText::AsNumber(Index), FText::FromName(TargetName))
			: FText::Format(LOCTEXT("TargetEntryWithId", "{0}: {1} ({2})"), FText::AsNumber(Index), FText::FromName(TargetName), FText::FromName(TargetId));
		Menu.AddMenuEntry(Label,
			LOCTEXT("SaveTargetTooltip", "Saves this target's look as a new preset asset and picks it on the target."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakButton, Index, bShape]()
			{
				UContextMenuButton* Current = WeakButton.Get();
				UContextMenuButton* Source = Current ? GetAuthoredButton(Current) : nullptr;
				if (!Source)
				{
					return;
				}
				const FScopedTransaction Transaction(LOCTEXT("SaveTargetPreset", "Save Target As Preset"));
				const bool bSaved = bShape ? Source->SaveShapeTargetAsPreset(Index) : Source->SaveDesignTargetAsPreset(Index);

				// The designer's copy shows the preset picked too.
				if (bSaved && Current != Source)
				{
					if (bShape && Current->ShapeTargets.IsValidIndex(Index) && Source->ShapeTargets.IsValidIndex(Index))
					{
						Current->ShapeTargets[Index].PresetAsset = Source->ShapeTargets[Index].PresetAsset;
					}
					else if (!bShape && Current->DesignTargets.IsValidIndex(Index) && Source->DesignTargets.IsValidIndex(Index))
					{
						Current->DesignTargets[Index].PresetAsset = Source->DesignTargets[Index].PresetAsset;
					}
				}
			})));
	}
	Menu.EndSection();
	return Menu.MakeWidget();
}

void FContextMenuButtonDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> SelectedObjects;
	DetailBuilder.GetObjectsBeingCustomized(SelectedObjects);

	CustomizedButtons.Reset();
	for (const TWeakObjectPtr<UObject>& Object : SelectedObjects)
	{
		if (UContextMenuButton* Button = Cast<UContextMenuButton>(Object.Get()))
		{
			CustomizedButtons.Add(Button);
		}
	}

	if (CustomizedButtons.Num() == 0)
	{
		return;
	}

	// A ready-made button just dropped from the Palette builds its parts now that it has really been placed (next tick).
	StyledButtonsEditor::OnButtonsShown(CustomizedButtons);

	for (const TWeakObjectPtr<UContextMenuButton>& WeakButton : CustomizedButtons)
	{
		UContextMenuButton* Button = WeakButton.Get();
		if (!Button)
		{
			continue;
		}

		// Style Settings laid out for what each target aims at, before any row is made from them.
		Button->RefreshShapeTargetLooks();

		// A Widget Blueprint that already gives its placements an overrides variable for this button (made by hand, or before
		// Instance Overridable existed) shows ticked: that is what the box describes.
		if (!Button->bInstanceOverridable && DoesOverridesVariableExist(FindOwningWidgetBlueprint(Button), Button))
		{
			Button->bInstanceOverridable = true;
			if (UContextMenuButton* Template = Cast<UContextMenuButton>(WidgetEditorHooks::FindDesignerTemplate(Button)))
			{
				Template->bInstanceOverridable = true;
			}
		}

		// Renamed without the designer saying so (an undone rename): what Instance Overridable made follows the name it has now.
		ReconcileRenamed(Button);
	}

	// The state preview is the designer toolbar's now (FButtonPreviewToolbar), with transitions; Show Preview stays the state a
	// button shows until the toolbar picks one.
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UContextMenuButton, ShowPreview), UContextMenuButton::StaticClass());

	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FContextMenuButtonDetails::HandleObjectPropertyChanged);

	// A ready-made button: its Style first, with the switch that lists its parts in the Hierarchy or hides them.
	const bool bAnyReadyMade = CustomizedButtons.ContainsByPredicate([](const TWeakObjectPtr<UContextMenuButton>& WeakButton)
	{
		return WeakButton.IsValid() && WeakButton->IsA<UStyledButtonBase>();
	});
	if (bAnyReadyMade)
	{
		IDetailCategoryBuilder& StyleCategory = DetailBuilder.EditCategory(TEXT("Style"), LOCTEXT("StyleCategory", "Style"), ECategoryPriority::Important);
		const TArray<TWeakObjectPtr<UContextMenuButton>> PartsButtons = CustomizedButtons;
		StyleCategory.AddCustomRow(LOCTEXT("PartsFilter", "Show Parts In Hierarchy"))
		.WholeRowContent()
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text_Lambda([PartsButtons]()
			{
				return StyledButtonsEditor::ArePartsShown(PartsButtons)
					? LOCTEXT("HidePartsButton", "Hide Parts From Hierarchy")
					: LOCTEXT("ShowPartsButton", "Show Parts In Hierarchy");
			})
			.ToolTipText(LOCTEXT("PartsTooltip",
				"Lists the widgets this button is made of in the Hierarchy, or takes them out of it again. Hidden, they are the same widgets - kept inside the button, still drawn and driven by its targets - and every change made to them is kept. While hidden they can't be Blueprint variables or Sequencer targets: show them for that."))
			.IsEnabled_Lambda([PartsButtons]()
			{
				return StyledButtonsEditor::CanToggleParts(PartsButtons);
			})
			.OnClicked_Lambda([PartsButtons]()
			{
				StyledButtonsEditor::ToggleParts(PartsButtons);
				return FReply::Handled();
			})
		];
	}

	const TWeakObjectPtr<UContextMenuButton> FirstButton = CustomizedButtons[0];
	const bool bSingle = CustomizedButtons.Num() == 1;

	// Rows are added onto the properties' own rows (EditDefaultProperty), never as custom rows in an EditCategory of a property's
	// category: a "Context Menu Button|Shape" category is a sub-category of Context Menu Button in this panel, and editing it by that
	// name makes a second, top-level category that lists the property again.

	// ---- Button Preset: save this button as one, or write it back into the one it follows ----
	const TSharedRef<IPropertyHandle> PresetHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UContextMenuButton, ButtonPreset), UContextMenuButton::StaticClass());
	if (IDetailPropertyRow* PresetRow = DetailBuilder.EditDefaultProperty(PresetHandle))
	{
		PresetRow->CustomWidget()
		.NameContent()
		[
			PresetHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		.MaxDesiredWidth(TOptional<float>())
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				PresetHandle->CreatePropertyValueWidget()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SWrapBox)
				.UseAllottedSize(true)
				+ SWrapBox::Slot()
				.Padding(0.f, 2.f, 4.f, 2.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("SaveButtonPreset", "Save As Preset..."))
					.ToolTipText(LOCTEXT("SaveButtonPresetTooltip", "Saves this button's settings as a new Context Menu Button Preset asset and picks it, so other buttons can pick it too."))
					.IsEnabled(bSingle)
					.OnClicked_Lambda([FirstButton]()
					{
						UContextMenuButton* Current = FirstButton.Get();
						UContextMenuButton* Source = Current ? GetAuthoredButton(Current) : nullptr;
						if (Source)
						{
							const FScopedTransaction Transaction(LOCTEXT("SaveButtonPresetTransaction", "Save Button As Preset"));
							if (Source->SaveAsButtonPreset() && Current != Source)
							{
								Current->ButtonPreset = Source->ButtonPreset;
							}
						}
						return FReply::Handled();
					})
				]
				+ SWrapBox::Slot()
				.Padding(0.f, 2.f, 4.f, 2.f)
				[
					SNew(SButton)
					.Text(LOCTEXT("UpdateButtonPreset", "Update Preset"))
					.ToolTipText(LOCTEXT("UpdateButtonPresetTooltip", "Writes this button's settings into its Button Preset; every button following that preset takes them the next time it loads."))
					.IsEnabled_Lambda([FirstButton, bSingle]()
					{
						const UContextMenuButton* Current = FirstButton.Get();
						return bSingle && Current && Current->ButtonPreset != nullptr;
					})
					.OnClicked_Lambda([FirstButton]()
					{
						UContextMenuButton* Current = FirstButton.Get();
						if (UContextMenuButton* Source = Current ? GetAuthoredButton(Current) : nullptr)
						{
							if (!Source->ButtonPreset)
							{
								Source->ButtonPreset = Current->ButtonPreset;
							}
							Source->UpdateButtonPreset();
						}
						return FReply::Handled();
					})
				]
			]
		];
	}

	// ---- Save a Shape / Design Target as a preset: a menu on the list's own header, next to its + and bin ----
	auto AddSaveTargetMenu = [this, &DetailBuilder, bSingle](FName ListProperty, bool bShape) -> TSharedPtr<IPropertyHandle>
	{
		const TSharedRef<IPropertyHandle> ListHandle = DetailBuilder.GetProperty(ListProperty, UContextMenuButton::StaticClass());
		IDetailPropertyRow* ListRow = DetailBuilder.EditDefaultProperty(ListHandle);
		if (!ListRow)
		{
			return ListHandle;
		}
		ListRow->CustomWidget(/*bShowChildren=*/true)
		.NameContent()
		[
			ListHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(250.f)
		.MaxDesiredWidth(TOptional<float>())
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				ListHandle->CreatePropertyValueWidget(/*bDisplayDefaultPropertyButtons=*/true)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SComboButton)
				.IsEnabled(bSingle)
				.ToolTipText(bShape
					? LOCTEXT("SaveShapeTargetTooltip", "Pick a Shape Target: its look is saved as a new Context Menu Shape Preset asset, which any Shape Target of any button can then pick (Preset).")
					: LOCTEXT("SaveDesignTargetTooltip", "Pick a Design Target: its look is saved as a new Context Menu Design Preset asset, which any Design Target of any button can then pick (Preset)."))
				.OnGetMenuContent(FOnGetContent::CreateSP(this, &FContextMenuButtonDetails::MakeSaveTargetPresetMenu, bShape))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(LOCTEXT("SaveTargetAsPreset", "Save As Preset"))
				]
			]
		];
		return ListHandle;
	};
	ShapeTargetsHandle = AddSaveTargetMenu(GET_MEMBER_NAME_CHECKED(UContextMenuButton, ShapeTargets), /*bShape=*/true);
	DesignTargetsHandle = AddSaveTargetMenu(GET_MEMBER_NAME_CHECKED(UContextMenuButton, DesignTargets), /*bShape=*/false);

	// ---- Instance Overridable: what the placements get, in its tooltip ----
	const TSharedRef<IPropertyHandle> OverridableHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UContextMenuButton, bInstanceOverridable), UContextMenuButton::StaticClass());
	if (IDetailPropertyRow* OverridableRow = DetailBuilder.EditDefaultProperty(OverridableHandle))
	{
		const TAttribute<FText> OverridableTip = TAttribute<FText>::CreateSP(this, &FContextMenuButtonDetails::GetInstanceOverridableTooltip);
		OverridableRow->CustomWidget()
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(OverridableHandle->GetPropertyDisplayName())
			.ToolTipText(OverridableTip)
		]
		.ValueContent()
		[
			SNew(SBox)
			.ToolTipText(OverridableTip)
			[
				OverridableHandle->CreatePropertyValueWidget()
			]
		];
	}
}

#undef LOCTEXT_NAMESPACE
