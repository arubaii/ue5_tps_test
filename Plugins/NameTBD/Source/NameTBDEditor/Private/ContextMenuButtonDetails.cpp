// Copyright AR GameSystems 2026. All rights reserved.

#include "ContextMenuButtonDetails.h"

#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Widgets/StyledButtons.h"
#include "StyledButtonsEditor.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
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

	// What a target aims at, or the Background Shape, decides whether a Look or a Shape shows - and a Look newly laid out
	// for another kind of shape has other rows. The property bag's rows are only made again by a fresh layout.
	const FName Name = Event.GetPropertyName();
	const FName Member = Event.GetMemberPropertyName();
	const bool bArrayChange = (Event.ChangeType & (EPropertyChangeType::ArrayAdd | EPropertyChangeType::ArrayRemove | EPropertyChangeType::ArrayClear | EPropertyChangeType::Duplicate)) != 0;
	const bool bShapeTargets = Member == GET_MEMBER_NAME_CHECKED(UContextMenuButton, ShapeTargets);

	// Switching a state's override on or off gives it a Look or takes it away.
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

	if (Name == GET_MEMBER_NAME_CHECKED(UContextMenuButton, BackgroundShape)
		|| (bShapeTargets && (Name == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, TargetWidgetName) || bArrayChange || bOverrideToggled)))
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

UWidgetBlueprint* FContextMenuButtonDetails::FindOwningWidgetBlueprint(const UContextMenuButton* Button)
{
	if (!Button)
	{
		return nullptr;
	}

	// The object the Details panel shows is the template in the Widget Blueprint's own tree, so the outer
	// chain usually reaches the blueprint directly.
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

bool FContextMenuButtonDetails::CreateDispatcher(UWidgetBlueprint* Blueprint, FName DispatcherName, const TArray<FName>& BoolParameterNames)
{
	if (!Blueprint)
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddMatchingDispatcher", "Add Matching Event Dispatcher"));
	Blueprint->Modify();

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

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	// Compiled now rather than left dirty, so the dispatcher is immediately bindable and the button's own
	// matcher can see it on its next resolve.
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

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

bool FContextMenuButtonDetails::CreateOverridesVariable(UWidgetBlueprint* Blueprint, FName VariableName, FName TargetButtonName)
{
	if (!Blueprint)
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddOverridesVariable", "Add Instance Overrides Variable"));
	Blueprint->Modify();

	FEdGraphPinType StructType;
	StructType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	StructType.PinSubCategoryObject = FContextMenuButtonInstanceOverrides::StaticStruct();

	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, StructType))
	{
		return false;
	}

	// The whole point is that a PLACEMENT can set this, which means Instance Editable. A variable without
	// it is invisible in the parent designer and the override could only ever be set from Blueprint.
	FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VariableName, /*bNewBlueprintOnly=*/false);

	// Compiled before stamping: the CDO and the FStructProperty the default is written through don't exist
	// until the variable has been through a compile.
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	StampTargetButtonName(Blueprint, VariableName, TargetButtonName);

	return true;
}

bool FContextMenuButtonDetails::CanAddOverrides() const
{
	for (const TWeakObjectPtr<UContextMenuButton>& WeakButton : CustomizedButtons)
	{
		const UContextMenuButton* Button = WeakButton.Get();
		if (!Button)
		{
			continue;
		}

		UWidgetBlueprint* Blueprint = FindOwningWidgetBlueprint(Button);
		if (Blueprint && !DoesOverridesVariableExist(Blueprint, Button))
		{
			return true;
		}
	}

	return false;
}

FText FContextMenuButtonDetails::GetAddOverridesTooltip() const
{
	const UContextMenuButton* Button = CustomizedButtons.Num() > 0 ? CustomizedButtons[0].Get() : nullptr;
	if (!Button)
	{
		return LOCTEXT("NoButtonOverrides", "No button selected.");
	}

	UWidgetBlueprint* Blueprint = FindOwningWidgetBlueprint(Button);
	if (!Blueprint)
	{
		return LOCTEXT("NoBlueprintOverrides",
			"Can't reach a Widget Blueprint from here. Select the button in the Widget Blueprint that contains it.");
	}

	const FName VariableName = GetOverridesVariableName(Button);

	if (DoesOverridesVariableExist(Blueprint, Button))
	{
		return FText::Format(
			LOCTEXT("OverridesExist", "{0} already has an overrides variable for this button."),
			FText::FromString(Blueprint->GetName()));
	}

	return FText::Format(
		LOCTEXT("WillCreateOverrides",
			"Creates '{0}' on {1}, marks it Instance Editable, and points it at this button. Every placement of {1} can then override this button's Shape and Design from its own Details panel."),
		FText::FromName(VariableName),
		FText::FromString(Blueprint->GetName()));
}

FReply FContextMenuButtonDetails::OnAddOverridesClicked()
{
	for (const TWeakObjectPtr<UContextMenuButton>& WeakButton : CustomizedButtons)
	{
		UContextMenuButton* Button = WeakButton.Get();
		if (!Button)
		{
			continue;
		}

		UWidgetBlueprint* Blueprint = FindOwningWidgetBlueprint(Button);
		if (!Blueprint || DoesOverridesVariableExist(Blueprint, Button))
		{
			continue;
		}

		CreateOverridesVariable(Blueprint, GetOverridesVariableName(Button), Button->GetFName());
	}

	return FReply::Handled();
}

bool FContextMenuButtonDetails::CanAddDispatcher(FDispatcherEventInfo EventInfo) const
{
	for (const TWeakObjectPtr<UContextMenuButton>& WeakButton : CustomizedButtons)
	{
		const UContextMenuButton* Button = WeakButton.Get();
		if (!Button)
		{
			continue;
		}

		UWidgetBlueprint* Blueprint = FindOwningWidgetBlueprint(Button);
		if (!Blueprint)
		{
			continue;
		}

		const FName DispatcherName(*FString::Printf(TEXT("On%s%s"), *Button->GetName(), EventInfo.NameSuffix));
		if (!DoesDispatcherExist(Blueprint, DispatcherName))
		{
			return true;
		}
	}

	return false;
}

FText FContextMenuButtonDetails::GetAddDispatcherTooltip(FDispatcherEventInfo EventInfo) const
{
	const UContextMenuButton* Button = CustomizedButtons.Num() > 0 ? CustomizedButtons[0].Get() : nullptr;
	if (!Button)
	{
		return LOCTEXT("NoButton", "No button selected.");
	}

	UWidgetBlueprint* Blueprint = FindOwningWidgetBlueprint(Button);
	if (!Blueprint)
	{
		return LOCTEXT("NoBlueprint",
			"Can't reach a Widget Blueprint from here. Select the button in the Widget Blueprint that contains it.");
	}

	const FName DispatcherName(*FString::Printf(TEXT("On%s%s"), *Button->GetName(), EventInfo.NameSuffix));

	if (DoesDispatcherExist(Blueprint, DispatcherName))
	{
		return FText::Format(
			LOCTEXT("AlreadyExists", "'{0}' already exists on {1}."),
			FText::FromName(DispatcherName),
			FText::FromString(Blueprint->GetName()));
	}

	return FText::Format(
		LOCTEXT("WillCreate", "Creates '{0}' on {1}, with {2} parameter(s), and compiles. Parameters are optional - delete them and it still binds."),
		FText::FromName(DispatcherName),
		FText::FromString(Blueprint->GetName()),
		FText::AsNumber(EventInfo.ParameterNames.Num()));
}

FReply FContextMenuButtonDetails::OnAddDispatcherClicked(FDispatcherEventInfo EventInfo)
{
	for (const TWeakObjectPtr<UContextMenuButton>& WeakButton : CustomizedButtons)
	{
		UContextMenuButton* Button = WeakButton.Get();
		if (!Button)
		{
			continue;
		}

		UWidgetBlueprint* Blueprint = FindOwningWidgetBlueprint(Button);
		if (!Blueprint)
		{
			continue;
		}

		const FName DispatcherName(*FString::Printf(TEXT("On%s%s"), *Button->GetName(), EventInfo.NameSuffix));
		if (DoesDispatcherExist(Blueprint, DispatcherName))
		{
			continue;
		}

		CreateDispatcher(Blueprint, DispatcherName, EventInfo.ParameterNames);
	}

	return FReply::Handled();
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

	// Looks laid out for what each target aims at, before any row is made from them.
	for (const TWeakObjectPtr<UContextMenuButton>& WeakButton : CustomizedButtons)
	{
		if (UContextMenuButton* Button = WeakButton.Get())
		{
			Button->RefreshShapeTargetLooks();
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

	// NOTE: Designer Preview used to be hidden here and its children re-added one at a time into an
	// "Animation" category. That is now meta = (ShowOnlyInnerProperties) on the UPROPERTY itself, which
	// does the same thing with no editor module involved - and, more to the point, works for every other
	// class that has a designer preview rather than only for this one. Nothing about laying out an
	// ordinary struct property needs an IDetailCustomization.
	//
	// What is left here genuinely does: creating Event Dispatchers and Blueprint variables is editor-only
	// work with no runtime equivalent.

	// Full category path, matching the UPROPERTY Category string exactly - the '|' is part of the name,
	// not a separator the builder splits on.
	IDetailCategoryBuilder& EventsCategory = DetailBuilder.EditCategory(
		TEXT("Context Menu Button Events"),
		FText::GetEmpty(),
		ECategoryPriority::Default);

	TSharedRef<SWrapBox> ButtonBox = SNew(SWrapBox).UseAllottedSize(true);

	for (const FDispatcherEventInfo& EventInfo : GetEventInfos())
	{
		ButtonBox->AddSlot()
		.Padding(0.0f, 2.0f, 4.0f, 2.0f)
		[
			SNew(SButton)
			.Text(FText::Format(LOCTEXT("AddDispatcherFormat", "Add On...{0}"), FText::FromString(EventInfo.NameSuffix)))
			.ToolTipText(this, &FContextMenuButtonDetails::GetAddDispatcherTooltip, EventInfo)
			.IsEnabled(this, &FContextMenuButtonDetails::CanAddDispatcher, EventInfo)
			.OnClicked(this, &FContextMenuButtonDetails::OnAddDispatcherClicked, EventInfo)
		];
	}

	IDetailCategoryBuilder& OverridesCategory = DetailBuilder.EditCategory(
		TEXT("Context Menu Button Instance Overrides"),
		FText::GetEmpty(),
		ECategoryPriority::Default);

	OverridesCategory.AddCustomRow(LOCTEXT("AddOverridesFilter", "Add Overrides Variable"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AddOverridesHint",
				"Adds a per-placement overrides variable to the widget containing this button, already pointed at it. Every placement of that widget can then restyle this button from its own Details panel."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.AutoWrapText(true)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 4.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("AddOverridesButton", "Add Overrides Variable"))
			.ToolTipText(this, &FContextMenuButtonDetails::GetAddOverridesTooltip)
			.IsEnabled(this, &FContextMenuButtonDetails::CanAddOverrides)
			.OnClicked(this, &FContextMenuButtonDetails::OnAddOverridesClicked)
		]
	];

	EventsCategory.AddCustomRow(LOCTEXT("AddDispatchersFilter", "Add Matching Dispatcher"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AddDispatchersLabel", "Add Matching Dispatcher"))
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AddDispatchersHint",
				"Creates a correctly named Event Dispatcher on the widget that contains this button, and compiles it. Greyed out once it exists."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.AutoWrapText(true)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			ButtonBox
		]
	];
}

#undef LOCTEXT_NAMESPACE