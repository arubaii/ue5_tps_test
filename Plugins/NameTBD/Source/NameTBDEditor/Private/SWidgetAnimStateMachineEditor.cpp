// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimStateMachineEditor.h"

#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "BlueprintModes/WidgetBlueprintApplicationMode.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditor.h"
#include "IAssetTools.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachineRuntime.h"
#include "NameTBDColors.h"
#include "NameTBDTooltips.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "StatusBarSubsystem.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/IToolkitHost.h"
#include "UObject/UObjectIterator.h"
#include "WidgetAnimBuilderToolbar.h"
#include "WidgetAnimStateMachineAssetEditor.h"
#include "WidgetAnimStateMachineGraph.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "WidgetDrawerConfig.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

#define LOCTEXT_NAMESPACE "SWidgetAnimStateMachineEditor"

namespace MachineColors = NameTBD::Colors::StateMachineEditor;
namespace MachineTips = NameTBD::Tooltips::StateMachineEditor;

// =====================================================================================================
// Proxies
// =====================================================================================================

bool UWidgetAnimMachineStateProxy::Load()
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	const FWidgetAnimMachineState* Found = Live && Live->Layers.IsValidIndex(LayerIndex) ? Live->Layers[LayerIndex].FindState(StateId) : nullptr;
	if (!Found)
	{
		return false;
	}
	State = *Found;
	return true;
}

void UWidgetAnimMachineStateProxy::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	// Recorded in the edit's transaction, so undo puts the machine back - the proxy is only a window onto it.
	if (UWidgetAnimStateMachineAsset* Live = Machine.Get())
	{
		Live->Modify();
	}
}

void UWidgetAnimMachineStateProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UWidgetAnimStateMachineAsset* Live = Machine.Get();
	const int32 Index = Live && Live->Layers.IsValidIndex(LayerIndex) ? Live->Layers[LayerIndex].FindStateIndex(StateId) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return;
	}

	// Everything but what only the graph changes: where it sits, what kind of node it is, its id.
	FWidgetAnimMachineState& Target = Live->Layers[LayerIndex].States[Index];
	const FVector2D Position = Target.EditorPosition;
	const EWidgetAnimMachineNodeKind Kind = Target.Kind;
	Target = State;
	Target.Id = StateId;
	Target.Kind = Kind;
	Target.EditorPosition = Position;

	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		Live->Normalize();
		Load();
	}
	Live->MarkPackageDirty();
	Live->NotifyMachineChanged();
}

TArray<FString> UWidgetAnimMachineStateProxy::GetNumberInputOptions() const
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	return Live ? Live->GetNumberInputOptions() : TArray<FString>();
}

bool UWidgetAnimMachineTransitionProxy::Load()
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	const FWidgetAnimMachineLayer* Layer = Live && Live->Layers.IsValidIndex(LayerIndex) ? &Live->Layers[LayerIndex] : nullptr;
	const int32 Index = Layer ? Layer->FindTransitionIndex(TransitionId) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Transition = Layer->Transitions[Index];
	return true;
}

void UWidgetAnimMachineTransitionProxy::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	if (UWidgetAnimStateMachineAsset* Live = Machine.Get())
	{
		Live->Modify();
	}
}

void UWidgetAnimMachineTransitionProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UWidgetAnimStateMachineAsset* Live = Machine.Get();
	FWidgetAnimMachineLayer* Layer = Live && Live->Layers.IsValidIndex(LayerIndex) ? &Live->Layers[LayerIndex] : nullptr;
	const int32 Index = Layer ? Layer->FindTransitionIndex(TransitionId) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return;
	}

	// Its ends are the graph's to change.
	FWidgetAnimMachineTransition& Target = Layer->Transitions[Index];
	const FGuid From = Target.From;
	const FGuid To = Target.To;
	Target = Transition;
	Target.Id = TransitionId;
	Target.From = From;
	Target.To = To;

	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		Live->Normalize();
		Load();
	}
	Live->MarkPackageDirty();
	Live->NotifyMachineChanged();
}

TArray<FString> UWidgetAnimMachineTransitionProxy::GetInputNameOptions() const
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	return Live ? Live->GetInputNameOptions() : TArray<FString>();
}

// =====================================================================================================
// SWidgetAnimStateMachineEditor
// =====================================================================================================

namespace
{
	const FSlateBrush* FindEditorBrush(std::initializer_list<FName> Candidates)
	{
		const ISlateStyle& Style = FAppStyle::Get();
		for (const FName Candidate : Candidates)
		{
			if (const FSlateBrush* Brush = Style.GetOptionalBrush(Candidate, nullptr, nullptr))
			{
				return Brush;
			}
		}
		return nullptr;
	}

	/** The Widget Blueprint editor open on WidgetBlueprint, or null. */
	FWidgetBlueprintEditor* FindWidgetBlueprintEditor(const UWidgetBlueprint* WidgetBlueprint)
	{
		UAssetEditorSubsystem* AssetEditors = GEditor && WidgetBlueprint ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		IAssetEditorInstance* Instance = AssetEditors ? AssetEditors->FindEditorForAsset(const_cast<UWidgetBlueprint*>(WidgetBlueprint), /*bFocusIfOpen=*/false) : nullptr;
		return Instance && Instance->GetEditorName() == FName(TEXT("WidgetBlueprintEditor")) ? static_cast<FWidgetBlueprintEditor*>(Instance) : nullptr;
	}

	/** Every Widget Animation Component in Root's tree - nested User Widgets' trees too - Root's own included. */
	void ForEachComponentInTree(UUserWidget* Root, TFunctionRef<void(UWidgetAnimationComponent*)> Visit, int32 Depth = 0)
	{
		if (!Root || Depth > 16)
		{
			return;
		}
		if (UWidgetAnimationComponent* Own = UWidgetAnimationComponent::FindWidgetAnimationComponent(Root))
		{
			Visit(Own);
		}
		if (!Root->WidgetTree)
		{
			return;
		}
		Root->WidgetTree->ForEachWidget([&Visit, Depth](UWidget* Widget)
		{
			if (UUserWidget* Nested = Cast<UUserWidget>(Widget))
			{
				ForEachComponentInTree(Nested, Visit, Depth + 1);
			}
			else if (UWidgetAnimationComponent* Component = UWidgetAnimationComponent::FindWidgetAnimationComponent(Widget))
			{
				Visit(Component);
			}
		});
	}

	FText DescribeMachineComponent(const UWidgetAnimationComponent* Component)
	{
		const UWidget* Widget = Component ? Component->GetAnimatedWidget() : nullptr;
		if (!Widget)
		{
			return LOCTEXT("NoWidget", "(gone)");
		}
		const UUserWidget* Holder = Widget->GetTypedOuter<UUserWidget>();
		const FString WidgetName = Widget->GetDisplayLabel().IsEmpty() ? Widget->GetName() : Widget->GetDisplayLabel();
		return Holder
			? FText::Format(LOCTEXT("ComponentInHolder", "{0} in {1}"), FText::FromString(WidgetName), FText::FromString(Holder->GetClass()->GetName()))
			: FText::FromString(WidgetName);
	}

	FText GetTypeLabel(EWidgetAnimMachineInputType Type)
	{
		switch (Type)
		{
		case EWidgetAnimMachineInputType::Number:  return LOCTEXT("NumberType", "Number");
		case EWidgetAnimMachineInputType::Trigger: return LOCTEXT("TriggerType", "Trigger");
		default:                                    return LOCTEXT("BoolType", "Bool");
		}
	}

	FLinearColor GetTypeColor(EWidgetAnimMachineInputType Type)
	{
		switch (Type)
		{
		case EWidgetAnimMachineInputType::Number:  return MachineColors::InputNumber;
		case EWidgetAnimMachineInputType::Trigger: return MachineColors::InputTrigger;
		default:                                    return MachineColors::InputBool;
		}
	}
}

void SWidgetAnimStateMachineEditor::Construct(const FArguments& InArgs)
{
	WidgetBlueprint = InArgs._WidgetBlueprint;
	bInDrawer = InArgs._InDrawer;
	OnDockInLayout = InArgs._OnDockInLayout;

	StateProxy = NewObject<UWidgetAnimMachineStateProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	TransitionProxy = NewObject<UWidgetAnimMachineTransitionProxy>(GetTransientPackage(), NAME_None, RF_Transient);

	GraphCommands = MakeShared<FUICommandList>();
	GraphCommands->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::DeleteSelected),
		FCanExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::CanDeleteSelected));
	GraphCommands->MapAction(FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::RenameSelected),
		FCanExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::CanRenameSelected));
	GraphCommands->MapAction(FGenericCommands::Get().SelectAll,
		FExecuteAction::CreateLambda([this]()
		{
			if (GraphEditors.IsValidIndex(ShownLayer) && GraphEditors[ShownLayer].IsValid())
			{
				GraphEditors[ShownLayer]->SelectAllNodes();
			}
		}));

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsArgs.bAllowSearch = true;
	DetailsView = PropertyModule.CreateDetailView(DetailsArgs);

	MachineChangedHandle = UWidgetAnimStateMachineAsset::OnMachineChanged().AddSP(this, &SWidgetAnimStateMachineEditor::OnMachineChanged);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeToolbar()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Inputs: the machine's, live, and the widget's own.
			+ SSplitter::Slot()
			.Value(0.2f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
				.Padding(FMargin(6.f))
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(InputsBox, SVerticalBox)
					]
				]
			]

			// The layers, as graphs.
			+ SSplitter::Slot()
			.Value(0.55f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(2.f, 2.f, 2.f, 0.f))
				[
					SAssignNew(LayerTabs, SHorizontalBox)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(GraphHost, SBox)
				]
			]

			// What is picked.
			+ SSplitter::Slot()
			.Value(0.25f)
			[
				DetailsView.ToSharedRef()
			]
		]
	];

	UWidgetAnimStateMachineAsset* Start = InArgs._Machine.Get();
	if (!Start)
	{
		Start = FindMachineInWidgetBlueprint();
	}
	SetMachine(Start);
}

SWidgetAnimStateMachineEditor::~SWidgetAnimStateMachineEditor()
{
	UWidgetAnimStateMachineAsset::OnMachineChanged().Remove(MachineChangedHandle);
	StopPreviews();
}

void SWidgetAnimStateMachineEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Machine);
	Collector.AddReferencedObjects(Graphs);
	Collector.AddReferencedObject(StateProxy);
	Collector.AddReferencedObject(TransitionProxy);
}

void SWidgetAnimStateMachineEditor::SetMachine(UWidgetAnimStateMachineAsset* InMachine)
{
	StopPreviews();
	PreviewSignature.Reset();

	Machine = InMachine;
	Graphs.Reset();
	GraphEditors.Reset();
	ShownLayer = 0;
	InputValues.Reset();
	BuiltInValues.Reset();
	DebugComponent.Reset();
	bDebugPickedByHand = false;
	bPanelsDirty = true;

	if (Machine)
	{
		Machine->Normalize();
		for (const FWidgetAnimMachineInput& Input : Machine->Inputs)
		{
			InputValues.Add(Input.Name, Input.Type == EWidgetAnimMachineInputType::Number ? Input.DefaultNumber : (Input.bDefaultBool ? 1.f : 0.f));
		}
		ShowLayer(0);
		DetailsView->SetObject(Machine);
	}
	else
	{
		GraphHost->SetContent(
			SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(20.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(MachineTips::NoMachineNote())
			]);
		DetailsView->SetObject(nullptr);
	}
}

UWidgetAnimMachineGraph* SWidgetAnimStateMachineEditor::GetOrMakeGraph(int32 LayerIndex)
{
	if (!Machine || !Machine->Layers.IsValidIndex(LayerIndex))
	{
		return nullptr;
	}
	Graphs.SetNum(Machine->Layers.Num());
	GraphEditors.SetNum(Machine->Layers.Num());
	if (Graphs[LayerIndex])
	{
		return Graphs[LayerIndex];
	}

	UWidgetAnimMachineGraph* Graph = NewObject<UWidgetAnimMachineGraph>(GetTransientPackage(), NAME_None, RF_Transient);
	Graph->Schema = UWidgetAnimMachineGraphSchema::StaticClass();
	Graph->Machine = Machine;
	Graph->LayerIndex = LayerIndex;

	const TWeakPtr<SWidgetAnimStateMachineEditor> WeakSelf = SharedThis(this);
	Graph->GetDebug = [WeakSelf, LayerIndex](FWidgetAnimMachineLayerDebug& OutDebug)
	{
		const TSharedPtr<SWidgetAnimStateMachineEditor> Self = WeakSelf.Pin();
		return Self.IsValid() && Self->GetLayerDebug(LayerIndex, OutDebug);
	};
	Graph->OnOpenState = [WeakSelf](const FGuid& StateId)
	{
		if (const TSharedPtr<SWidgetAnimStateMachineEditor> Self = WeakSelf.Pin())
		{
			Self->OpenState(StateId);
		}
	};
	Graph->RebuildFromMachine();
	Graphs[LayerIndex] = Graph;
	return Graph;
}

TSharedRef<SGraphEditor> SWidgetAnimStateMachineEditor::MakeGraphEditor(UWidgetAnimMachineGraph* Graph)
{
	SGraphEditor::FGraphEditorEvents Events;
	Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &SWidgetAnimStateMachineEditor::OnSelectionChanged);
	Events.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &SWidgetAnimStateMachineEditor::OnNodeTitleCommitted);

	FGraphAppearanceInfo Appearance;
	Appearance.CornerText = LOCTEXT("CornerText", "STATE MACHINE");

	return SNew(SGraphEditor)
		.AdditionalCommands(GraphCommands)
		.IsEditable(true)
		.Appearance(Appearance)
		.GraphToEdit(Graph)
		.GraphEvents(Events);
}

void SWidgetAnimStateMachineEditor::ShowLayer(int32 LayerIndex)
{
	if (!Machine || Machine->Layers.Num() == 0)
	{
		return;
	}
	ShownLayer = FMath::Clamp(LayerIndex, 0, Machine->Layers.Num() - 1);
	UWidgetAnimMachineGraph* Graph = GetOrMakeGraph(ShownLayer);
	if (!Graph)
	{
		return;
	}
	if (!GraphEditors[ShownLayer].IsValid())
	{
		GraphEditors[ShownLayer] = MakeGraphEditor(Graph);
	}
	GraphHost->SetContent(GraphEditors[ShownLayer].ToSharedRef());
	DetailsView->SetObject(Machine);
}

void SWidgetAnimStateMachineEditor::RebuildLayerTabs()
{
	LayerTabs->ClearChildren();
	if (!Machine)
	{
		return;
	}

	for (int32 LayerIndex = 0; LayerIndex < Machine->Layers.Num(); ++LayerIndex)
	{
		const FWidgetAnimMachineLayer& Layer = Machine->Layers[LayerIndex];
		const FText Blend = StaticEnum<EWidgetAnimMachineLayerBlend>()->GetDisplayNameTextByValue(static_cast<int64>(Layer.BlendMode));
		LayerTabs->AddSlot()
			.AutoWidth()
			.Padding(FMargin(0.f, 0.f, 2.f, 0.f))
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText(FText::Format(MachineTips::LayerTabTooltip(), FText::FromName(Layer.Name), Blend, FText::AsNumber(Layer.Weight)))
				.IsChecked_Lambda([this, LayerIndex]() { return ShownLayer == LayerIndex ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, LayerIndex](ECheckBoxState) { ShowLayer(LayerIndex); })
				[
					SNew(SBox)
					.Padding(FMargin(8.f, 2.f))
					[
						SNew(STextBlock)
						.Text(FText::FromName(Layer.Name))
					]
				]
			];
	}

	// Add a layer; remove the one shown.
	LayerTabs->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(MachineTips::AddLayerTooltip())
			.OnClicked_Lambda([this]()
			{
				const int32 Added = WidgetAnimMachineEdits::AddLayer(Machine);
				if (Added != INDEX_NONE)
				{
					ShowLayer(Added);
				}
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
	LayerTabs->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(MachineTips::RemoveLayerTooltip())
			.IsEnabled_Lambda([this]() { return Machine && Machine->Layers.Num() > 1; })
			.OnClicked_Lambda([this]()
			{
				WidgetAnimMachineEdits::RemoveLayer(Machine, ShownLayer);
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
}

void SWidgetAnimStateMachineEditor::RebuildInputs()
{
	InputsBox->ClearChildren();
	if (!Machine)
	{
		return;
	}

	auto AddHeader = [this](const FText& Title, const FText& ToolTip, TSharedRef<SWidget> Extra)
	{
		InputsBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 6.f, 0.f, 2.f))
			[
				SNew(SHorizontalBox)
				.ToolTipText(ToolTip)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
					.Text(Title)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					Extra
				]
			];
		InputsBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 0.f, 0.f, 4.f))
			[
				SNew(SSeparator)
			];
	};

	// The machine's inputs, with + to add one of each type.
	const TSharedRef<SWidget> AddInputButton = SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.HasDownArrow(false)
		.ToolTipText(MachineTips::AddInputTooltip())
		.ButtonContent()
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))
			.ColorAndOpacity(FSlateColor::UseForeground())
		]
		.OnGetMenuContent_Lambda([this]()
		{
			FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
			for (const EWidgetAnimMachineInputType Type : { EWidgetAnimMachineInputType::Bool, EWidgetAnimMachineInputType::Number, EWidgetAnimMachineInputType::Trigger })
			{
				Menu.AddMenuEntry(
					GetTypeLabel(Type),
					StaticEnum<EWidgetAnimMachineInputType>()->GetToolTipTextByIndex(static_cast<int32>(Type)),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, Type]()
					{
						WidgetAnimMachineEdits::AddInput(Machine, Type);
						DetailsView->SetObject(Machine, /*bForceRefresh=*/true);
					})));
			}
			return Menu.MakeWidget();
		});
	AddHeader(LOCTEXT("InputsHeader", "Inputs"), MachineTips::InputsPanelTooltip(), AddInputButton);

	if (Machine->Inputs.Num() == 0)
	{
		InputsBox->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(MachineTips::NoInputsNote())
			];
	}

	for (const FWidgetAnimMachineInput& Input : Machine->Inputs)
	{
		const FName Name = Input.Name;
		const EWidgetAnimMachineInputType Type = Input.Type;

		TSharedRef<SWidget> Control = SNullWidget::NullWidget;
		switch (Type)
		{
		case EWidgetAnimMachineInputType::Number:
			Control = SNew(SBox)
				.WidthOverride(80.f)
				[
					SNew(SSpinBox<float>)
					.Value_Lambda([this, Name]() { return GetShownInputValue(Name); })
					.OnValueChanged_Lambda([this, Name](float Value) { SetInputValue(Name, Value); })
					.Delta(0.01f)
				];
			break;
		case EWidgetAnimMachineInputType::Trigger:
			Control = SNew(SButton)
				.Text(LOCTEXT("Fire", "Fire"))
				.ToolTipText(MachineTips::FireTooltip())
				.OnClicked_Lambda([this, Name]() { FireInput(Name); return FReply::Handled(); });
			break;
		default:
			Control = SNew(SCheckBox)
				.IsChecked_Lambda([this, Name]() { return GetShownInputValue(Name) > 0.5f ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Name](ECheckBoxState State) { SetInputValue(Name, State == ECheckBoxState::Checked ? 1.f : 0.f); });
			break;
		}

		InputsBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 2.f))
			[
				SNew(SHorizontalBox)
				.ToolTipText(Input.Description.IsEmpty() ? GetTypeLabel(Type) : FText::FromString(Input.Description))
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush")))
					.BorderBackgroundColor(GetTypeColor(Type))
					.Padding(FMargin(4.f, 1.f))
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
						.ColorAndOpacity(FLinearColor::Black)
						.Text(GetTypeLabel(Type))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromName(Name))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					Control
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(MachineTips::RemoveInputTooltip())
					.OnClicked_Lambda([this, Name]()
					{
						WidgetAnimMachineEdits::RemoveInput(Machine, Name);
						DetailsView->SetObject(Machine, /*bForceRefresh=*/true);
						return FReply::Handled();
					})
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("Icons.X")))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]
			];
	}

	// The widget's own, read off it at runtime - set here to try the machine in the designer.
	AddHeader(LOCTEXT("WidgetHeader", "Widget"), MachineTips::BuiltInPanelTooltip(), SNullWidget::NullWidget);
	for (const FName BuiltIn : WidgetAnimMachineInputs::GetBuiltIns())
	{
		InputsBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 2.f))
			[
				SNew(SHorizontalBox)
				.ToolTipText(WidgetAnimMachineInputs::GetBuiltInDescription(BuiltIn))
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromName(BuiltIn))
					.ColorAndOpacity_Lambda([this, BuiltIn]()
					{
						// Lit when the machine reads it.
						return Machine && Machine->UsesInput(BuiltIn) ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground();
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsEnabled_Lambda([this]() { return !IsDebuggingPlay(); })
					.IsChecked_Lambda([this, BuiltIn]() { return GetShownBuiltInValue(BuiltIn) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this, BuiltIn](ECheckBoxState State) { SetBuiltInValue(BuiltIn, State == ECheckBoxState::Checked); })
				]
			];
	}
}

void SWidgetAnimStateMachineEditor::OnMachineChanged(UWidgetAnimStateMachineAsset* ChangedMachine)
{
	if (!Machine || ChangedMachine != Machine)
	{
		return;
	}

	// Layers added or removed: the graphs and editors follow, on the next tick - this may be called from inside the Details'
	// own edit, which must not have its object swapped under it.
	if (Graphs.Num() != Machine->Layers.Num())
	{
		bLayersChanged = true;
	}

	for (UWidgetAnimMachineGraph* Graph : Graphs)
	{
		if (Graph)
		{
			Graph->RequestRebuild();
		}
	}

	// The picked state or transition shown as it is now - or, gone, the machine instead. An edit made from the Details
	// themselves leaves them as they are; one from anywhere else - an undo - refreshes them.
	const UObject* Shown = nullptr;
	if (DetailsView.IsValid())
	{
		const TArray<TWeakObjectPtr<UObject>> Selected = DetailsView->GetSelectedObjects();
		Shown = Selected.Num() == 1 ? Selected[0].Get() : nullptr;
	}
	if (Shown && Shown == StateProxy.Get())
	{
		const FWidgetAnimMachineState Before = StateProxy->State;
		if (!StateProxy->Load())
		{
			DetailsView->SetObject(Machine);
		}
		else if (!FWidgetAnimMachineState::StaticStruct()->CompareScriptStruct(&Before, &StateProxy->State, PPF_None))
		{
			DetailsView->ForceRefresh();
		}
	}
	else if (Shown && Shown == TransitionProxy.Get())
	{
		const FWidgetAnimMachineTransition Before = TransitionProxy->Transition;
		if (!TransitionProxy->Load())
		{
			DetailsView->SetObject(Machine);
		}
		else if (!FWidgetAnimMachineTransition::StaticStruct()->CompareScriptStruct(&Before, &TransitionProxy->Transition, PPF_None))
		{
			DetailsView->ForceRefresh();
		}
	}

	bPanelsDirty = true;
}

// =====================================================================================================
// Graph
// =====================================================================================================

void SWidgetAnimStateMachineEditor::OnSelectionChanged(const TSet<UObject*>& Selection)
{
	if (!Machine || bRestoringSelection)
	{
		return;
	}

	if (Selection.Num() == 1)
	{
		UObject* Selected = *Selection.CreateConstIterator();
		if (const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Selected))
		{
			if (StateNode->Kind == EWidgetAnimMachineNodeKind::State)
			{
				StateProxy->Machine = Machine;
				StateProxy->LayerIndex = ShownLayer;
				StateProxy->StateId = StateNode->StateId;
				if (StateProxy->Load())
				{
					DetailsView->SetObject(StateProxy, /*bForceRefresh=*/true);
					return;
				}
			}
		}
		else if (const UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Selected))
		{
			TransitionProxy->Machine = Machine;
			TransitionProxy->LayerIndex = ShownLayer;
			TransitionProxy->TransitionId = TransitionNode->TransitionId;
			if (TransitionProxy->Load())
			{
				DetailsView->SetObject(TransitionProxy, /*bForceRefresh=*/true);
				return;
			}
		}
	}

	// Nothing picked, or Entry, Exit, Any State, or several: the machine itself - its inputs and layers.
	DetailsView->SetObject(Machine);
}

void SWidgetAnimStateMachineEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* Node)
{
	const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Node);
	if (StateNode && Machine)
	{
		WidgetAnimMachineEdits::RenameState(Machine, ShownLayer, StateNode->StateId, NewText.ToString());
	}
}

void SWidgetAnimStateMachineEditor::DeleteSelected()
{
	if (!Machine || !GraphEditors.IsValidIndex(ShownLayer) || !GraphEditors[ShownLayer].IsValid())
	{
		return;
	}

	TArray<FGuid> States;
	TArray<FGuid> Transitions;
	for (UObject* Selected : GraphEditors[ShownLayer]->GetSelectedNodes())
	{
		if (const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Selected))
		{
			if (StateNode->CanUserDeleteNode())
			{
				States.Add(StateNode->StateId);
			}
		}
		else if (const UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Selected))
		{
			Transitions.Add(TransitionNode->TransitionId);
		}
	}
	GraphEditors[ShownLayer]->ClearSelectionSet();
	WidgetAnimMachineEdits::Delete(Machine, ShownLayer, States, Transitions);
	DetailsView->SetObject(Machine);
}

bool SWidgetAnimStateMachineEditor::CanDeleteSelected() const
{
	if (!GraphEditors.IsValidIndex(ShownLayer) || !GraphEditors[ShownLayer].IsValid())
	{
		return false;
	}
	for (UObject* Selected : GraphEditors[ShownLayer]->GetSelectedNodes())
	{
		const UEdGraphNode* Node = Cast<UEdGraphNode>(Selected);
		if (Node && Node->CanUserDeleteNode())
		{
			return true;
		}
	}
	return false;
}

void SWidgetAnimStateMachineEditor::RenameSelected()
{
	if (!GraphEditors.IsValidIndex(ShownLayer) || !GraphEditors[ShownLayer].IsValid())
	{
		return;
	}
	const FGraphPanelSelectionSet Selected = GraphEditors[ShownLayer]->GetSelectedNodes();
	if (Selected.Num() == 1)
	{
		if (const UEdGraphNode* Node = Cast<UEdGraphNode>(*Selected.CreateConstIterator()))
		{
			GraphEditors[ShownLayer]->IsNodeTitleVisible(Node, /*bRequestRename=*/true);
		}
	}
}

bool SWidgetAnimStateMachineEditor::CanRenameSelected() const
{
	if (!GraphEditors.IsValidIndex(ShownLayer) || !GraphEditors[ShownLayer].IsValid())
	{
		return false;
	}
	const FGraphPanelSelectionSet Selected = GraphEditors[ShownLayer]->GetSelectedNodes();
	const UEdGraphNode* Node = Selected.Num() == 1 ? Cast<UEdGraphNode>(*Selected.CreateConstIterator()) : nullptr;
	return Node && Node->GetCanRenameNode();
}

void SWidgetAnimStateMachineEditor::OpenState(const FGuid& StateId)
{
	const FWidgetAnimMachineState* State = Machine && Machine->Layers.IsValidIndex(ShownLayer) ? Machine->Layers[ShownLayer].FindState(StateId) : nullptr;
	if (!State || !State->Animation)
	{
		return;
	}

	// In a Widget Blueprint, in its own Widget Animations panel, against its widgets; anywhere else, the asset's own editor.
	if (UWidgetBlueprint* Blueprint = WidgetBlueprint.Get())
	{
		if (FWidgetAnimBuilderToolbar::OpenAnimationInPanel(Blueprint, State->Animation))
		{
			return;
		}
	}
	if (UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
	{
		AssetEditors->OpenEditorForAsset(State->Animation.Get());
	}
}

void SWidgetAnimStateMachineEditor::CommitMovedNodes()
{
	// Once the mouse is let go: a drag moves nodes every frame, and is one move.
	if (!Machine || !Graphs.IsValidIndex(ShownLayer) || !Graphs[ShownLayer] || FSlateApplication::Get().GetPressedMouseButtons().Num() > 0)
	{
		return;
	}
	const FWidgetAnimMachineLayer* Layer = Graphs[ShownLayer]->GetLayer();
	if (!Layer)
	{
		return;
	}

	TMap<FGuid, FVector2D> Moved;
	for (const UEdGraphNode* Node : Graphs[ShownLayer]->Nodes)
	{
		const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Node);
		const FWidgetAnimMachineState* State = StateNode ? Layer->FindState(StateNode->StateId) : nullptr;
		if (!State)
		{
			continue;
		}
		const FVector2D Position(StateNode->NodePosX, StateNode->NodePosY);
		if (!Position.Equals(State->EditorPosition, 0.5))
		{
			Moved.Add(StateNode->StateId, Position);
		}
	}
	WidgetAnimMachineEdits::MoveStates(Machine, ShownLayer, Moved);
}

// =====================================================================================================
// Tick
// =====================================================================================================

void SWidgetAnimStateMachineEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!Machine)
	{
		return;
	}

	if (bLayersChanged)
	{
		bLayersChanged = false;
		Graphs.SetNum(Machine->Layers.Num());
		GraphEditors.SetNum(Machine->Layers.Num());
		for (int32 LayerIndex = 0; LayerIndex < Graphs.Num(); ++LayerIndex)
		{
			if (Graphs[LayerIndex])
			{
				Graphs[LayerIndex]->LayerIndex = LayerIndex;
				Graphs[LayerIndex]->RequestRebuild();
			}
		}
		ShowLayer(FMath::Min(ShownLayer, Machine->Layers.Num() - 1));
	}

	for (int32 LayerIndex = 0; LayerIndex < Graphs.Num(); ++LayerIndex)
	{
		UWidgetAnimMachineGraph* Graph = Graphs[LayerIndex];
		if (!Graph || !Graph->ConsumeRebuildRequest())
		{
			continue;
		}

		// The nodes are made again: what was selected is picked again on the new ones, and the graph editor never holds
		// on to the old ones.
		const TSharedPtr<SGraphEditor> GraphEditor = GraphEditors.IsValidIndex(LayerIndex) ? GraphEditors[LayerIndex] : nullptr;
		TSet<FGuid> SelectedIds;
		if (GraphEditor.IsValid())
		{
			for (UObject* Selected : GraphEditor->GetSelectedNodes())
			{
				if (const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Selected))
				{
					SelectedIds.Add(StateNode->StateId);
				}
				else if (const UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Selected))
				{
					SelectedIds.Add(TransitionNode->TransitionId);
				}
			}
			// Quietly: the Details keep showing what they show (OnMachineChanged has refreshed or replaced it already).
			TGuardValue<bool> Quiet(bRestoringSelection, true);
			GraphEditor->ClearSelectionSet();
		}

		Graph->RebuildFromMachine();
		TGuardValue<bool> Quiet(bRestoringSelection, true);

		if (GraphEditor.IsValid() && SelectedIds.Num() > 0)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Node);
				const UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Node);
				const FGuid Id = StateNode ? StateNode->StateId : (TransitionNode ? TransitionNode->TransitionId : FGuid());
				if (Id.IsValid() && SelectedIds.Contains(Id))
				{
					GraphEditor->SetNodeSelection(Node, true);
				}
			}
		}
	}

	if (bPanelsDirty)
	{
		bPanelsDirty = false;
		RebuildInputs();
		RebuildLayerTabs();
	}

	CommitMovedNodes();
	UpdatePreviews();
}

// =====================================================================================================
// Preview and debugging
// =====================================================================================================

void SWidgetAnimStateMachineEditor::FindPreviewComponents(TArray<UWidgetAnimationComponent*>& OutComponents) const
{
	OutComponents.Reset();
	FWidgetBlueprintEditor* Editor = FindWidgetBlueprintEditor(WidgetBlueprint.Get());
	UUserWidget* Preview = Editor ? Editor->GetPreview() : nullptr;
	if (!Preview || !Machine)
	{
		return;
	}

	TArray<UWidgetAnimationComponent*> All;
	ForEachComponentInTree(Preview, [this, &All](UWidgetAnimationComponent* Component)
	{
		if (Component->PlaysStateMachine(Machine))
		{
			All.AddUnique(Component);
		}
	});

	// Only the selected widget's, when one that plays the machine is selected: one button of a row tried on its own.
	UWidget* Selected = nullptr;
	if (Editor->GetSelectedWidgets().Num() == 1)
	{
		Selected = Editor->GetSelectedWidgets().CreateConstIterator()->GetPreview();
	}
	if (Selected)
	{
		for (UWidgetAnimationComponent* Component : All)
		{
			const UWidget* Animated = Component->GetAnimatedWidget();
			if (Animated && (Animated == Selected || Animated->IsIn(Selected)))
			{
				OutComponents.Add(Component);
			}
		}
		if (OutComponents.Num() > 0)
		{
			return;
		}
	}
	OutComponents = MoveTemp(All);
}

void SWidgetAnimStateMachineEditor::FindDebugCandidates(TArray<UWidgetAnimationComponent*>& OutComponents) const
{
	OutComponents.Reset();
	if (!Machine || !GEditor || !GEditor->PlayWorld)
	{
		return;
	}
	for (TObjectIterator<UWidgetAnimationComponent> It(RF_ClassDefaultObject | RF_ArchetypeObject, /*bIncludeDerivedClasses=*/true, EInternalObjectFlags::Garbage); It; ++It)
	{
		UWidgetAnimationComponent* Component = *It;
		const UWidget* Widget = Component->GetAnimatedWidget();
		const UWorld* World = Widget ? Widget->GetWorld() : nullptr;
		if (!Widget || Widget->IsDesignTime() || !World || World != GEditor->PlayWorld || !Component->PlaysStateMachine(Machine))
		{
			continue;
		}
		const bool bRunning = Component->GetStateMachineInstances().ContainsByPredicate([this](const FWidgetAnimMachineInstance& Instance)
		{
			return Instance.bStarted && Instance.Machine == Machine;
		});
		if (bRunning)
		{
			OutComponents.Add(Component);
		}
	}
}

UWidgetAnimationComponent* SWidgetAnimStateMachineEditor::GetWatchedComponent() const
{
	// Playing in the editor: the widget picked in the Debug menu, or the first that plays the machine.
	if (GEditor && GEditor->PlayWorld)
	{
		if (UWidgetAnimationComponent* Picked = DebugComponent.Get())
		{
			return Picked;
		}
		if (!bDebugPickedByHand)
		{
			TArray<UWidgetAnimationComponent*> Candidates;
			FindDebugCandidates(Candidates);
			if (Candidates.Num() > 0)
			{
				const_cast<SWidgetAnimStateMachineEditor*>(this)->DebugComponent = Candidates[0];
				return Candidates[0];
			}
		}
		return nullptr;
	}

	// Else the first designer widget previewing it.
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		UWidgetAnimationComponent* Live = Weak.Get();
		if (Live && Live->IsDesignerStateMachinePreviewing())
		{
			return Live;
		}
	}
	return nullptr;
}

bool SWidgetAnimStateMachineEditor::IsDebuggingPlay() const
{
	const UWidgetAnimationComponent* Watched = GetWatchedComponent();
	const UWidget* Widget = Watched ? Watched->GetAnimatedWidget() : nullptr;
	return Widget && !Widget->IsDesignTime();
}

bool SWidgetAnimStateMachineEditor::GetLayerDebug(int32 LayerIndex, FWidgetAnimMachineLayerDebug& OutDebug) const
{
	const UWidgetAnimationComponent* Watched = GetWatchedComponent();
	if (!Watched || !Machine)
	{
		return false;
	}
	const FWidgetAnimMachineInstance* Instance = Watched->GetStateMachineInstances().FindByPredicate([this](const FWidgetAnimMachineInstance& Candidate)
	{
		return Candidate.bStarted && Candidate.Machine == Machine;
	});
	if (!Instance)
	{
		return false;
	}
	TArray<FWidgetAnimMachineLayerDebug> Layers;
	WidgetAnimStateMachines::GetDebug(*Instance, Layers);
	if (!Layers.IsValidIndex(LayerIndex))
	{
		return false;
	}
	OutDebug = Layers[LayerIndex];
	return true;
}

FString SWidgetAnimStateMachineEditor::MakePreviewSignature() const
{
	// What a preview has to start again for: the inputs it was set up with, the layers it has, the animations it captured
	// its widgets for. Durations, conditions and the rest are read live.
	if (!Machine)
	{
		return FString();
	}
	FString Signature = Machine->GetPathName();
	for (const FWidgetAnimMachineInput& Input : Machine->Inputs)
	{
		Signature += FString::Printf(TEXT("|%s:%d"), *Input.Name.ToString(), static_cast<int32>(Input.Type));
	}
	Signature += FString::Printf(TEXT("|L%d"), Machine->Layers.Num());
	TArray<UWidgetAnimPresetAsset*> Animations;
	Machine->GatherAnimations(Animations);
	for (const UWidgetAnimPresetAsset* Animation : Animations)
	{
		Signature += TEXT("|") + Animation->GetPathName();
	}
	return Signature;
}

void SWidgetAnimStateMachineEditor::UpdatePreviews()
{
	if (!WidgetBlueprint.IsValid() || !Machine)
	{
		return;
	}
	if (!bPreviewInDesigner || (GEditor && GEditor->PlayWorld))
	{
		StopPreviews();
		return;
	}

	// Looked for again four times a second: the designer makes its preview again on every compile.
	const double Now = FPlatformTime::Seconds();
	if (LastPreviewSearch < 0.0 || Now - LastPreviewSearch > 0.25)
	{
		LastPreviewSearch = Now;
		TArray<UWidgetAnimationComponent*> Found;
		FindPreviewComponents(Found);
		for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
		{
			UWidgetAnimationComponent* Live = Weak.Get();
			if (Live && !Found.Contains(Live))
			{
				Live->StopDesignerStateMachinePreview();
			}
		}
		PreviewComponents.Reset();
		for (UWidgetAnimationComponent* Component : Found)
		{
			PreviewComponents.Add(Component);
		}
	}

	const FString Signature = MakePreviewSignature();
	const bool bRestart = Signature != PreviewSignature;
	PreviewSignature = Signature;

	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		UWidgetAnimationComponent* Live = Weak.Get();
		if (!Live)
		{
			continue;
		}
		if (bRestart && Live->IsDesignerStateMachinePreviewing())
		{
			Live->StopDesignerStateMachinePreview();
		}
		if (!Live->IsDesignerStateMachinePreviewing() && Live->StartDesignerStateMachinePreview())
		{
			PushInputs(Live);
		}
	}
}

void SWidgetAnimStateMachineEditor::StopPreviews()
{
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		if (UWidgetAnimationComponent* Live = Weak.Get())
		{
			Live->StopDesignerStateMachinePreview();
		}
	}
	PreviewComponents.Reset();
	LastPreviewSearch = -1.0;
}

void SWidgetAnimStateMachineEditor::PushInputs(UWidgetAnimationComponent* Component) const
{
	if (!Component || !Machine)
	{
		return;
	}
	for (const FWidgetAnimMachineInput& Input : Machine->Inputs)
	{
		if (Input.Type != EWidgetAnimMachineInputType::Trigger)
		{
			Component->SetStateMachineInputValue(Input.Name, GetShownInputValue(Input.Name), Machine);
		}
	}
	for (const TPair<FName, bool>& BuiltIn : BuiltInValues)
	{
		Component->SetDesignerStateMachineBuiltIn(BuiltIn.Key, BuiltIn.Value, Machine);
	}
}

void SWidgetAnimStateMachineEditor::SetInputValue(FName Input, float Value)
{
	InputValues.Add(Input, Value);

	// In Play In Editor, onto the widget watched; in the designer, onto every preview.
	if (IsDebuggingPlay())
	{
		if (UWidgetAnimationComponent* Watched = GetWatchedComponent())
		{
			Watched->SetStateMachineInputValue(Input, Value, Machine);
		}
		return;
	}
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		if (UWidgetAnimationComponent* Live = Weak.Get())
		{
			Live->SetStateMachineInputValue(Input, Value, Machine);
		}
	}
}

void SWidgetAnimStateMachineEditor::FireInput(FName Input)
{
	if (IsDebuggingPlay())
	{
		if (UWidgetAnimationComponent* Watched = GetWatchedComponent())
		{
			Watched->FireStateMachineTrigger(Input, Machine);
		}
		return;
	}
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		if (UWidgetAnimationComponent* Live = Weak.Get())
		{
			Live->FireStateMachineTrigger(Input, Machine);
		}
	}
}

void SWidgetAnimStateMachineEditor::SetBuiltInValue(FName Input, bool bValue)
{
	BuiltInValues.Add(Input, bValue);
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		if (UWidgetAnimationComponent* Live = Weak.Get())
		{
			Live->SetDesignerStateMachineBuiltIn(Input, bValue, Machine);
		}
	}
}

float SWidgetAnimStateMachineEditor::GetShownInputValue(FName Input) const
{
	// In Play In Editor, what the watched widget's machine holds.
	if (IsDebuggingPlay())
	{
		if (const UWidgetAnimationComponent* Watched = GetWatchedComponent())
		{
			const FWidgetAnimMachineInput* Definition = Machine ? Machine->FindInput(Input) : nullptr;
			if (Definition && Definition->Type == EWidgetAnimMachineInputType::Number)
			{
				return Watched->GetStateMachineNumber(Input, Machine);
			}
			return Watched->GetStateMachineBool(Input, Machine) ? 1.f : 0.f;
		}
	}
	if (const float* Value = InputValues.Find(Input))
	{
		return *Value;
	}
	const FWidgetAnimMachineInput* Definition = Machine ? Machine->FindInput(Input) : nullptr;
	return Definition ? (Definition->Type == EWidgetAnimMachineInputType::Number ? Definition->DefaultNumber : (Definition->bDefaultBool ? 1.f : 0.f)) : 0.f;
}

bool SWidgetAnimStateMachineEditor::GetShownBuiltInValue(FName Input) const
{
	if (IsDebuggingPlay())
	{
		const UWidgetAnimationComponent* Watched = GetWatchedComponent();
		return Watched && Watched->GetStateMachineBool(Input, Machine);
	}
	return BuiltInValues.FindRef(Input);
}

// =====================================================================================================
// Toolbar
// =====================================================================================================

TSharedRef<SWidget> SWidgetAnimStateMachineEditor::MakeToolbar()
{
	auto MakeIconButton = [](const FSlateBrush* Icon, const FText& Label, const FText& ToolTip, TFunction<void()> OnClicked)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(ToolTip)
			.ContentPadding(FMargin(4.f, 2.f))
			.OnClicked_Lambda([OnClicked]() { OnClicked(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(Icon)
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(STextBlock)
					.Text(Label)
				]
			];
	};

	TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox)

		// The machine edited: any WASM_.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MachineLabel", "State Machine"))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(2.f, 2.f))
		[
			SNew(SBox)
			.MinDesiredWidth(220.f)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UWidgetAnimStateMachineAsset::StaticClass())
				.AllowClear(false)
				.DisplayThumbnail(false)
				.DisplayUseSelected(false)
				.DisplayBrowse(true)
				.ObjectPath_Lambda([this]() { return Machine ? Machine->GetPathName() : FString(); })
				.OnObjectChanged_Lambda([this](const FAssetData& AssetData) { SetMachine(Cast<UWidgetAnimStateMachineAsset>(AssetData.GetAsset())); })
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakeIconButton(FAppStyle::GetBrush(TEXT("Icons.Plus")), LOCTEXT("NewMachine", "New"), MachineTips::NewMachineTooltip(), [this]() { CreateNewMachine(); })
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakeIconButton(FAppStyle::GetBrush(TEXT("Icons.Save")), LOCTEXT("SaveMachine", "Save"), MachineTips::SaveMachineTooltip(), [this]() { SaveMachine(); })
		]

		// The designer preview: on or off, and what it plays on.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(12.f, 2.f, 4.f, 2.f))
		[
			SNew(SCheckBox)
			.ToolTipText(MachineTips::PreviewToggleTooltip())
			.Visibility_Lambda([this]() { return WidgetBlueprint.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
			.IsChecked_Lambda([this]() { return bPreviewInDesigner ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
			{
				bPreviewInDesigner = State == ECheckBoxState::Checked;
				if (!bPreviewInDesigner)
				{
					StopPreviews();
				}
				PreviewSignature.Reset();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PreviewInDesigner", "Preview In Designer"))
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.Text(this, &SWidgetAnimStateMachineEditor::GetPreviewStatus)
		]

		// Play In Editor: the widget watched.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(SComboButton)
			.ToolTipText(MachineTips::DebugMenuTooltip())
			.Visibility_Lambda([]() { return GEditor && GEditor->PlayWorld ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnGetMenuContent(this, &SWidgetAnimStateMachineEditor::MakeDebugMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &SWidgetAnimStateMachineEditor::GetDebugLabel)
			]
		];

	if (bInDrawer && OnDockInLayout.IsBound())
	{
		Bar->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeIconButton(FindEditorBrush({ TEXT("Icons.Layout"), TEXT("Icons.Pinned"), TEXT("Icons.Plus") }), LOCTEXT("DockInLayout", "Dock in Layout"), MachineTips::DockInLayoutTooltip(), [this]() { OnDockInLayout.ExecuteIfBound(); })
			];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		.Padding(FMargin(2.f))
		[
			Bar
		];
}

TSharedRef<SWidget> SWidgetAnimStateMachineEditor::MakeMachineMenu()
{
	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SWidgetAnimStateMachineEditor::MakeDebugMenu()
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	TArray<UWidgetAnimationComponent*> Candidates;
	FindDebugCandidates(Candidates);
	if (Candidates.Num() == 0)
	{
		Menu.AddMenuEntry(LOCTEXT("NoDebugTargets", "No widget plays this machine"), MachineTips::NoDebugTargetsTooltip(), FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	}
	for (UWidgetAnimationComponent* Candidate : Candidates)
	{
		const TWeakObjectPtr<UWidgetAnimationComponent> Weak(Candidate);
		Menu.AddMenuEntry(
			DescribeMachineComponent(Candidate),
			MachineTips::DebugTargetTooltip(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Weak]() { DebugComponent = Weak; bDebugPickedByHand = true; }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this, Weak]() { return DebugComponent == Weak; })),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}
	return Menu.MakeWidget();
}

FText SWidgetAnimStateMachineEditor::GetPreviewStatus() const
{
	if (!Machine)
	{
		return FText::GetEmpty();
	}
	if (GEditor && GEditor->PlayWorld)
	{
		return IsDebuggingPlay() ? MachineTips::DebuggingStatus() : MachineTips::NotPlayingStatus();
	}
	if (!WidgetBlueprint.IsValid())
	{
		return MachineTips::AssetEditorStatus();
	}
	if (!bPreviewInDesigner)
	{
		return MachineTips::PreviewOffStatus();
	}
	int32 Count = 0;
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		Count += Weak.IsValid() ? 1 : 0;
	}
	return Count > 0 ? FText::Format(MachineTips::PreviewingStatus(), Count) : MachineTips::NothingToPreviewStatus();
}

FText SWidgetAnimStateMachineEditor::GetDebugLabel() const
{
	const UWidgetAnimationComponent* Watched = GetWatchedComponent();
	return Watched ? FText::Format(LOCTEXT("DebugLabel", "Debug: {0}"), DescribeMachineComponent(Watched)) : LOCTEXT("DebugNone", "Debug: nothing");
}

void SWidgetAnimStateMachineEditor::CreateNewMachine()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UWidgetAnimStateMachineFactory* Factory = NewObject<UWidgetAnimStateMachineFactory>();
	if (UWidgetAnimStateMachineAsset* Created = Cast<UWidgetAnimStateMachineAsset>(AssetTools.CreateAssetWithDialog(UWidgetAnimStateMachineAsset::StaticClass(), Factory)))
	{
		SetMachine(Created);
	}
}

void SWidgetAnimStateMachineEditor::SaveMachine()
{
	if (Machine)
	{
		TArray<UPackage*> Packages;
		Packages.Add(Machine->GetPackage());
		FEditorFileUtils::PromptForCheckoutAndSave(Packages, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
	}
}

UWidgetAnimStateMachineAsset* SWidgetAnimStateMachineEditor::FindMachineInWidgetBlueprint() const
{
	FWidgetBlueprintEditor* Editor = FindWidgetBlueprintEditor(WidgetBlueprint.Get());
	UUserWidget* Preview = Editor ? Editor->GetPreview() : nullptr;
	if (!Preview)
	{
		return nullptr;
	}

	UWidget* Selected = Editor->GetSelectedWidgets().Num() == 1 ? Editor->GetSelectedWidgets().CreateConstIterator()->GetPreview() : nullptr;
	UWidgetAnimStateMachineAsset* First = nullptr;
	UWidgetAnimStateMachineAsset* OfSelected = nullptr;
	ForEachComponentInTree(Preview, [&First, &OfSelected, Selected](UWidgetAnimationComponent* Component)
	{
		for (const FWidgetAnimStateMachineEntry& Row : Component->StateMachines)
		{
			if (!Row.StateMachine)
			{
				continue;
			}
			First = First ? First : Row.StateMachine.Get();
			const UWidget* Animated = Component->GetAnimatedWidget();
			if (!OfSelected && Selected && Animated && (Animated == Selected || Animated->IsIn(Selected)))
			{
				OfSelected = Row.StateMachine;
			}
		}
	});
	return OfSelected ? OfSelected : First;
}

// =====================================================================================================
// Drawer and tab, in every Widget Blueprint editor
// =====================================================================================================

namespace WidgetAnimStateMachineEditorHooks
{
	namespace
	{
		const FName DrawerId(TEXT("WidgetAnimBuilder.WidgetAnimationStateMachine"));
		const FName TabId(TEXT("WidgetAnimBuilder.WidgetAnimationStateMachinePanel"));
		const FName DockedTabId(TEXT("WidgetAnimBuilder.WidgetAnimationStateMachineDocked"));

		/** What Dock in Layout puts the tab beside, the first that is open: the Widget Animations panel, then the bottom well. */
		const FName DockBesideTabIds[] = { TEXT("WidgetAnimBuilder.WidgetAnimationsPanel"), TEXT("WidgetAnimBuilder.WidgetAnimationsDocked"), TEXT("Animations"), TEXT("CompilerResults") };

		struct FEditorEntry
		{
			TWeakPtr<FAssetEditorToolkit> Toolkit;
			TSharedPtr<SWidgetAnimStateMachineEditor> DrawerContent;
		};

		TArray<FEditorEntry>& GetEntries()
		{
			static TArray<FEditorEntry> Entries;
			return Entries;
		}

		FEditorEntry& FindOrAddEntry(const TSharedRef<FAssetEditorToolkit>& Toolkit)
		{
			TArray<FEditorEntry>& Entries = GetEntries();
			Entries.RemoveAll([](const FEditorEntry& Entry) { return !Entry.Toolkit.IsValid(); });
			FEditorEntry* Found = Entries.FindByPredicate([&Toolkit](const FEditorEntry& Entry) { return Entry.Toolkit.Pin() == Toolkit; });
			if (!Found)
			{
				Found = &Entries.AddDefaulted_GetRef();
				Found->Toolkit = Toolkit;
			}
			return *Found;
		}

		UWidgetBlueprint* GetEditedWidgetBlueprint(const FAssetEditorToolkit& Toolkit)
		{
			const TArray<UObject*>* Edited = Toolkit.GetObjectsCurrentlyBeingEdited();
			if (Edited)
			{
				for (UObject* Object : *Edited)
				{
					if (UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Object))
					{
						return Blueprint;
					}
				}
			}
			return nullptr;
		}

		const FSlateBrush* GetIcon()
		{
			const ISlateStyle& Style = FAppStyle::Get();
			for (const FName Candidate : { FName(TEXT("GraphEditor.StateMachine_16x")), FName(TEXT("Graph.StateNode.Icon")), FName(TEXT("Icons.Blueprints")) })
			{
				if (const FSlateBrush* Brush = Style.GetOptionalBrush(Candidate, nullptr, nullptr))
				{
					return Brush;
				}
			}
			return nullptr;
		}

		FSlateIcon GetSlateIcon()
		{
			for (const FName Candidate : { FName(TEXT("GraphEditor.StateMachine_16x")), FName(TEXT("Graph.StateNode.Icon")) })
			{
				if (FAppStyle::Get().GetOptionalBrush(Candidate, nullptr, nullptr))
				{
					return FSlateIcon(FAppStyle::GetAppStyleSetName(), Candidate);
				}
			}
			return FSlateIcon();
		}

		TSharedRef<SWidget> MakeTabBody(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
		{
			const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
			return SNew(SWidgetAnimStateMachineEditor)
				.WidgetBlueprint(Toolkit.IsValid() ? GetEditedWidgetBlueprint(*Toolkit) : nullptr);
		}

		TSharedRef<SDockTab> MakeTab(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
		{
			const TSharedRef<SDockTab> Tab = SNew(SDockTab)
				.TabRole(ETabRole::PanelTab)
				.Label(LOCTEXT("StateMachineTab", "Widget Animation State Machine"));
			Tab->SetTabIcon(GetIcon());
			Tab->SetContent(MakeTabBody(WeakToolkit));
			return Tab;
		}

		class FStateMachineTabSummoner : public FWorkflowTabFactory
		{
		public:

			explicit FStateMachineTabSummoner(TSharedPtr<FAssetEditorToolkit> InHostingApp)
				: FWorkflowTabFactory(TabId, InHostingApp)
			{
				TabLabel = LOCTEXT("StateMachineTab", "Widget Animation State Machine");
				TabIcon = GetSlateIcon();
				bIsSingleton = true;
				ViewMenuDescription = LOCTEXT("StateMachineTabName", "Widget Animation State Machine");
				ViewMenuTooltip = MachineTips::StateMachineDrawerTooltip();
			}

			virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override
			{
				return MakeTabBody(TWeakPtr<FAssetEditorToolkit>(HostingApp.Pin()));
			}
		};

		void EnsureTabSpawner(const TSharedPtr<FTabManager>& TabManager, const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
		{
			if (!TabManager.IsValid() || TabManager->HasTabSpawner(TabId))
			{
				return;
			}
			TabManager->RegisterTabSpawner(TabId, FOnSpawnTab::CreateLambda([WeakToolkit](const FSpawnTabArgs&) { return MakeTab(WeakToolkit); }))
				.SetDisplayName(LOCTEXT("StateMachineTabName", "Widget Animation State Machine"))
				.SetTooltipText(MachineTips::StateMachineDrawerTooltip())
				.SetIcon(GetSlateIcon());
		}

		/** Docks the editor as a tab: beside the Widget Animations panel or the bottom well when one is open, else its own slot. */
		void DockInLayout(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
		{
			const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
			const TSharedPtr<FTabManager> TabManager = Toolkit.IsValid() ? Toolkit->GetTabManager() : nullptr;
			if (!TabManager.IsValid())
			{
				return;
			}

			if (UStatusBarSubsystem* StatusBars = GEditor ? GEditor->GetEditorSubsystem<UStatusBarSubsystem>() : nullptr)
			{
				StatusBars->ForceDismissDrawer();
			}
			EnsureTabSpawner(TabManager, WeakToolkit);

			auto BringForward = [&TabManager](const TSharedRef<SDockTab>& Tab)
			{
				Tab->ActivateInParent(ETabActivationCause::SetDirectly);
				TabManager->DrawAttention(Tab);
			};

			for (const FName OpenId : { TabId, DockedTabId })
			{
				if (const TSharedPtr<SDockTab> Live = TabManager->FindExistingLiveTab(FTabId(OpenId)))
				{
					BringForward(Live.ToSharedRef());
					return;
				}
			}
			for (const FName BesideId : DockBesideTabIds)
			{
				if (TabManager->FindExistingLiveTab(FTabId(BesideId)).IsValid())
				{
					const TSharedRef<SDockTab> Tab = MakeTab(WeakToolkit);
					TabManager->InsertNewDocumentTab(DockedTabId, FTabManager::FLiveTabSearch(BesideId), Tab);
					BringForward(Tab);
					return;
				}
			}
			if (const TSharedPtr<SDockTab> Opened = TabManager->TryInvokeTab(FTabId(TabId)))
			{
				BringForward(Opened.ToSharedRef());
			}
		}
	}

	void RegisterTabFactory(const FWidgetBlueprintApplicationMode& Mode, FWorkflowAllowedTabSet& TabFactories)
	{
		TabFactories.RegisterFactory(MakeShared<FStateMachineTabSummoner>(Mode.GetBlueprintEditor()));
	}

	void ExtendLayout(FLayoutExtender& Extender)
	{
		// A closed slot after the Widget Animations panel's, in the bottom well, for the tab to open in.
		Extender.ExtendLayout(FTabId(TEXT("Animations")), ELayoutExtensionPosition::After, FTabManager::FTab(FTabId(TabId), ETabState::ClosedTab));
	}

	void PlaceDrawer(const TSharedRef<FAssetEditorToolkit>& Toolkit, int32 Slot)
	{
		UStatusBarSubsystem* StatusBars = GEditor ? GEditor->GetEditorSubsystem<UStatusBarSubsystem>() : nullptr;
		if (!StatusBars || !Toolkit->IsHosted())
		{
			return;
		}

		FindOrAddEntry(Toolkit);
		const TWeakPtr<FAssetEditorToolkit> WeakToolkit(Toolkit);
		EnsureTabSpawner(Toolkit->GetTabManager(), WeakToolkit);

		FWidgetDrawerConfig Config(DrawerId);
		Config.ButtonText = LOCTEXT("StateMachineDrawer", "Widget Animation State Machine");
		Config.ToolTipText = MachineTips::StateMachineDrawerTooltip();
		Config.Icon = GetIcon();

		// Built on first open and kept, so the graph's view and the inputs survive the drawer closing.
		Config.GetDrawerContentDelegate.BindLambda([WeakToolkit]() -> TSharedRef<SWidget>
		{
			const TSharedPtr<FAssetEditorToolkit> Live = WeakToolkit.Pin();
			if (!Live.IsValid())
			{
				return SNullWidget::NullWidget;
			}
			FEditorEntry& Found = FindOrAddEntry(Live.ToSharedRef());
			if (!Found.DrawerContent.IsValid())
			{
				Found.DrawerContent = SNew(SWidgetAnimStateMachineEditor)
					.WidgetBlueprint(GetEditedWidgetBlueprint(*Live))
					.InDrawer(true)
					.OnDockInLayout(FSimpleDelegate::CreateLambda([WeakToolkit]() { DockInLayout(WeakToolkit); }));
			}
			return Found.DrawerContent.ToSharedRef();
		});

		const FName StatusBarName = Toolkit->GetToolkitHost()->GetStatusBarName();
		StatusBars->UnregisterDrawer(StatusBarName, DrawerId);
		StatusBars->RegisterDrawer(StatusBarName, MoveTemp(Config), Slot);
	}

	void Shutdown()
	{
		GetEntries().Empty();
	}
}

#undef LOCTEXT_NAMESPACE
