// Copyright AR GameSystems 2026. All rights reserved.

#include "ContextMenuButtonDetails.h"

#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Widgets/StyledButtons.h"
#include "NameTBD/Widgets/WidgetEditorHooks.h"
#include "StyledButtonsEditor.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#include "WidgetBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "ScopedTransaction.h"
#include "Containers/Ticker.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "ContextMenuButtonDetails"

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
	// Settings newly laid out for another kind of shape have other rows. The property bag's rows are only made again by a fresh
	// layout.
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
		|| Name == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, PresetAsset)
		|| bArrayChange || bOverrideToggled);
	const bool bPresetPicked = Member == GET_MEMBER_NAME_CHECKED(UContextMenuButton, ButtonPreset)
		|| (bDesignTargets && Name == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, PresetAsset));

	if (bShapeLayoutChange || bPresetPicked)
	{
		const TWeakPtr<IDetailLayoutBuilder> Weak = WeakBuilder;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
		{
			if (const TSharedPtr<IDetailLayoutBuilder> Builder = Weak.Pin())
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
	}
	{
		FDispatcherEventInfo& Info = Infos.AddDefaulted_GetRef();
		Info.NameSuffix = TEXT("EnabledStateChanged");
		Info.ParameterNames = { TEXT("bIsEnabled") };
	}
	{
		FDispatcherEventInfo& Info = Infos.AddDefaulted_GetRef();
		Info.NameSuffix = TEXT("Hovered");
		Info.ParameterNames = { TEXT("bHovered") };
	}
	{
		FDispatcherEventInfo& Info = Infos.AddDefaulted_GetRef();
		Info.NameSuffix = TEXT("PressCompleted");
		Info.ParameterNames = {};
	}

	return Infos;
}

FName FContextMenuButtonDetails::GetDispatcherName(const UContextMenuButton* Button, const FDispatcherEventInfo& EventInfo)
{
	return Button ? FName(*FString::Printf(TEXT("On%s%s"), *Button->GetName(), EventInfo.NameSuffix)) : NAME_None;
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
	return Button ? FName(*FString::Printf(TEXT("%sInstanceOverrides"), *Button->GetName())) : NAME_None;
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

TArray<FName> FContextMenuButtonDetails::FindOwnOverridesVariables(const UWidgetBlueprint* Blueprint, const UContextMenuButton* Button)
{
	TArray<FName> Found;
	if (!Blueprint || !Button)
	{
		return Found;
	}

	const UObject* CDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
	const FName ExpectedName = GetOverridesVariableName(Button);
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
				bClaims = Value && Value->TargetButtonName == Button->GetFName();
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
	UWidgetBlueprint* Blueprint = FindOwningWidgetBlueprint(Button);
	if (!Blueprint)
	{
		return;
	}

	if (Button->bInstanceOverridable)
	{
		const FScopedTransaction Transaction(LOCTEXT("MakeOverridable", "Make Button Instance Overridable"));
		Blueprint->Modify();

		// The events placements bind: one dispatcher per event the button broadcasts by itself, each with its parameters.
		bool bChanged = false;
		for (const FDispatcherEventInfo& EventInfo : GetEventInfos())
		{
			const FName DispatcherName = GetDispatcherName(Button, EventInfo);
			if (!DoesDispatcherExist(Blueprint, DispatcherName))
			{
				bChanged |= AddDispatcher(Blueprint, DispatcherName, EventInfo.ParameterNames);
			}
		}

		// The overrides: a variable placements can set (Instance Editable), claimed by this button.
		const FName VariableName = GetOverridesVariableName(Button);
		bool bAddedVariable = false;
		if (!DoesOverridesVariableExist(Blueprint, Button))
		{
			FEdGraphPinType StructType;
			StructType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			StructType.PinSubCategoryObject = FContextMenuButtonInstanceOverrides::StaticStruct();
			if (FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, StructType))
			{
				FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VariableName, /*bNewBlueprintOnly=*/false);
				bAddedVariable = true;
				bChanged = true;
			}
		}

		if (!bChanged)
		{
			return;
		}

		// Compiled now rather than left dirty, so the dispatchers are bindable and the variable shows on placements at once. The
		// variable's default is stamped after: its CDO property only exists once compiled.
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (bAddedVariable)
		{
			StampTargetButtonName(Blueprint, VariableName, Button->GetFName());
		}
		return;
	}

	// Unticked: the variable goes (undo brings it back). The dispatchers stay - placements' graphs may be bound to them.
	const TArray<FName> Variables = FindOwnOverridesVariables(Blueprint, Button);
	if (Variables.Num() == 0)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("MakeNotOverridable", "Make Button Not Instance Overridable"));
	Blueprint->Modify();
	for (const FName& VariableName : Variables)
	{
		FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VariableName);
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
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

	// Each row goes into the category its property is in, whatever that category is called in this panel.
	auto CategoryOf = [&DetailBuilder](FName PropertyName) -> IDetailCategoryBuilder&
	{
		const TSharedRef<IPropertyHandle> Handle = DetailBuilder.GetProperty(PropertyName, UContextMenuButton::StaticClass());
		return DetailBuilder.EditCategory(Handle->GetDefaultCategoryName());
	};
	const TWeakObjectPtr<UContextMenuButton> FirstButton = CustomizedButtons[0];
	const bool bSingle = CustomizedButtons.Num() == 1;

	// ---- Button Preset: save this button as one, or write it back into the one it follows ----
	CategoryOf(GET_MEMBER_NAME_CHECKED(UContextMenuButton, ButtonPreset))
	.AddCustomRow(LOCTEXT("ButtonPresetFilter", "Save As Preset"))
	.WholeRowContent()
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
	];

	// ---- Save a Shape / Design Target as a preset ----
	auto AddSaveTargetRow = [this, &CategoryOf, bSingle](FName ListProperty, bool bShape)
	{
		CategoryOf(ListProperty)
		.AddCustomRow(LOCTEXT("SaveTargetFilter", "Save Target As Preset"))
		.WholeRowContent()
		[
			SNew(SComboButton)
			.IsEnabled(bSingle)
			.OnGetMenuContent(FOnGetContent::CreateSP(this, &FContextMenuButtonDetails::MakeSaveTargetPresetMenu, bShape))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(bShape ? LOCTEXT("SaveShapeTarget", "Save Shape Target As Preset...") : LOCTEXT("SaveDesignTarget", "Save Design Target As Preset..."))
				.ToolTipText(LOCTEXT("SaveTargetRowTooltip", "Pick a target: its look is saved as a new preset asset, which any target of any button can then pick (Preset)."))
			]
		];
	};
	AddSaveTargetRow(GET_MEMBER_NAME_CHECKED(UContextMenuButton, ShapeTargets), /*bShape=*/true);
	AddSaveTargetRow(GET_MEMBER_NAME_CHECKED(UContextMenuButton, DesignTargets), /*bShape=*/false);

	// ---- Instance Overridable: what the placements get ----
	CategoryOf(GET_MEMBER_NAME_CHECKED(UContextMenuButton, bInstanceOverridable))
	.AddCustomRow(LOCTEXT("OverridableFilter", "Instance Overridable"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.AutoWrapText(true)
		.Text(this, &FContextMenuButtonDetails::GetInstanceOverridableStatus)
	];
}

#undef LOCTEXT_NAMESPACE
