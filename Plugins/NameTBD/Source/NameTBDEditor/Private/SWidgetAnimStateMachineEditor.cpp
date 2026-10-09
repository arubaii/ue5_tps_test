// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimStateMachineEditor.h"
#include "WidgetAnimTargetOption.h"
#include "WidgetAnimTypePill.h"

#include "Algo/StableSort.h"
#include "AssetRegistry/AssetData.h"
#include "AssetToolsModule.h"
#include "Misc/PackageName.h"
#include "NameTBD/WidgetAnimation/WidgetAnimRuntimeSettings.h"
#include "NameTBDStyle.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "BlueprintModes/WidgetBlueprintApplicationMode.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Containers/Ticker.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraphNode_Comment.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/LayoutExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "IAssetTools.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachineRuntime.h"
#include "NameTBDColors.h"
#include "NameTBDTooltips.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "StatusBarSubsystem.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/IToolkitHost.h"
#include "UObject/UObjectIterator.h"
#include "WidgetAnimBuilderToolbar.h"
#include "WidgetAnimMachineEditorStyle.h"
#include "WidgetAnimNameField.h"
#include "WidgetAnimStateMachineAssetEditor.h"
#include "WidgetAnimStateMachineGraph.h"
#include "WidgetAnimTimelines.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "WidgetAnimDockPreference.h"
#include "WidgetAnimEditorSettings.h"
#include "WidgetDrawerConfig.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/StyleColors.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimStateMachineDock, Log, All);

#define LOCTEXT_NAMESPACE "SWidgetAnimStateMachineEditor"

namespace MachineColors = NameTBD::Colors::StateMachineEditor;
namespace MachineTips = NameTBD::Tooltips::StateMachineEditor;
namespace TipPalette = NameTBD::Colors::WidgetAnimEditor;

const FName SWidgetAnimStateMachineEditor::HostPickKey(TEXT("(Plays On)"));

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

	// Everything but what only the graph changes: where it sits (and the size it was kept with), what kind of node it is, its id.
	FWidgetAnimMachineState& Target = Live->Layers[LayerIndex].States[Index];
	const FVector2D Position = Target.EditorPosition;
	const FVector2D Size = Target.EditorSize;
	const float SizeZoom = Target.EditorSizeZoom;
	const EWidgetAnimMachineNodeKind Kind = Target.Kind;
	Target = State;
	Target.Id = StateId;
	Target.Kind = Kind;
	Target.EditorPosition = Position;
	Target.EditorSize = Size;
	Target.EditorSizeZoom = SizeZoom;

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

TArray<FString> UWidgetAnimMachineStateProxy::GetStateParameterOptions() const
{
	TArray<FString> Options;
	TArray<const UWidgetAnimPresetAsset*, TInlineAllocator<8>> Played;
	Played.Add(State.Animation);
	for (const FWidgetAnimMachineRandomVariation& Variation : State.RandomVariations)
	{
		Played.Add(Variation.Animation);
	}
	for (const FWidgetAnimMachineBlendSample& Sample : State.BlendSamples)
	{
		Played.Add(Sample.Animation);
	}
	for (const UWidgetAnimPresetAsset* Animation : Played)
	{
		if (Animation)
		{
			for (const FWidgetAnimParameter& Parameter : Animation->Parameters)
			{
				Options.AddUnique(Parameter.Name.ToString());
			}
		}
	}
	return Options;
}

TArray<FString> UWidgetAnimMachineStateProxy::GetBlendInputOptions() const
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	return Live ? Live->GetBlendInputOptions() : TArray<FString>();
}

TArray<FString> UWidgetAnimMachineStateProxy::GetSettableInputOptions() const
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	return Live ? Live->GetSettableInputOptions() : TArray<FString>();
}

TArray<FString> UWidgetAnimMachineTransitionProxy::GetConditionLayerOptions() const
{
	TArray<FString> Options;
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	for (int32 Index = 0; Live && Index < Live->Layers.Num(); ++Index)
	{
		if (Index != LayerIndex)
		{
			Options.Add(Live->Layers[Index].Name.ToString());
		}
	}
	return Options;
}

TArray<FString> UWidgetAnimMachineTransitionProxy::GetConditionStateOptions() const
{
	TArray<FString> Options;
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	for (int32 Index = 0; Live && Index < Live->Layers.Num(); ++Index)
	{
		if (Index == LayerIndex)
		{
			continue;
		}
		for (const FWidgetAnimMachineState& Candidate : Live->Layers[Index].States)
		{
			if (Candidate.Kind == EWidgetAnimMachineNodeKind::State || Candidate.Kind == EWidgetAnimMachineNodeKind::Exit)
			{
				Options.AddUnique(Candidate.Name.ToString());
			}
		}
	}
	return Options;
}

TArray<FString> UWidgetAnimMachineTransitionProxy::GetSettableInputOptions() const
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	return Live ? Live->GetSettableInputOptions() : TArray<FString>();
}

TArray<FString> UWidgetAnimMachineStateProxy::GetInputNameOptions() const
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	return Live ? Live->GetInputNameOptions() : TArray<FString>();
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
	SyncShownDuration();
	return true;
}

void UWidgetAnimMachineTransitionProxy::SyncShownDuration()
{
	// Not set by hand: Animation Duration reads as the animation's own length, which is what the transition takes.
	if (!Transition.bOverrideAnimationDuration && Transition.TransitionAnimation)
	{
		Transition.AnimationDuration = FMath::Max(Transition.GetTransitionAnimationSeconds(), 0.01f);
	}
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

	// Its ends - and where its node was dragged, and its wire's reroute points - are the graph's to change.
	FWidgetAnimMachineTransition& Target = Layer->Transitions[Index];
	const FGuid From = Target.From;
	const FGuid To = Target.To;
	const bool bNodePlaced = Target.bEditorNodePlaced;
	const FVector2D NodeOffset = Target.EditorNodeOffset;
	const TArray<FVector2D> ReroutePoints = Target.EditorReroutePoints;
	Target = Transition;
	Target.Id = TransitionId;
	Target.From = From;
	Target.To = To;
	Target.bEditorNodePlaced = bNodePlaced;
	Target.EditorNodeOffset = NodeOffset;
	Target.EditorReroutePoints = ReroutePoints;

	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		Live->Normalize();
		Load();
	}
	else
	{
		// Mid-drag - Animation Speed, say: shown as the machine will read it, so nothing refreshes the Details under the drag.
		SyncShownDuration();
	}
	Live->MarkPackageDirty();
	Live->NotifyMachineChanged();
}

TArray<FString> UWidgetAnimMachineTransitionProxy::GetInputNameOptions() const
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	return Live ? Live->GetInputNameOptions() : TArray<FString>();
}

bool UWidgetAnimMachineInputProxy::Load()
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	const FWidgetAnimMachineInput* Found = Live ? Live->FindInput(InputName) : nullptr;
	if (!Found)
	{
		return false;
	}
	Input = *Found;
	return true;
}

void UWidgetAnimMachineInputProxy::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	if (UWidgetAnimStateMachineAsset* Live = Machine.Get())
	{
		Live->Modify();
	}
}

void UWidgetAnimMachineInputProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UWidgetAnimStateMachineAsset* Live = Machine.Get();
	const FName OldName = InputName;
	const int32 Index = Live ? Live->Inputs.IndexOfByPredicate([OldName](const FWidgetAnimMachineInput& Candidate) { return Candidate.Name == OldName; }) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return;
	}

	FWidgetAnimMachineInput& Target = Live->Inputs[Index];
	Target = Input;
	Target.Name = OldName;

	// Renamed: unique, never a built-in name, and everything that reads it follows.
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive && Input.Name != OldName)
	{
		const FName Unique = Live->MakeUniqueInputName(Input.Name.IsNone() ? FName(TEXT("Input")) : Input.Name, Index);
		Target.Name = Unique;
		WidgetAnimMachineEdits::ReplaceInputReferences(Live, OldName, Unique);
		InputName = Unique;
	}

	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		Live->Normalize();
		Load();
	}
	Live->MarkPackageDirty();
	Live->NotifyMachineChanged();
}

TArray<FString> UWidgetAnimMachineInputProxy::GetTargetNameOptions() const
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	return Live ? Live->GetTargetNameOptions() : TArray<FString>();
}

bool UWidgetAnimMachineLayerProxy::Load()
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	if (!Live || !Live->Layers.IsValidIndex(LayerIndex))
	{
		return false;
	}
	Layer = Live->Layers[LayerIndex];
	return true;
}

void UWidgetAnimMachineLayerProxy::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	if (UWidgetAnimStateMachineAsset* Live = Machine.Get())
	{
		Live->Modify();
	}
}

void UWidgetAnimMachineLayerProxy::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	UWidgetAnimStateMachineAsset* Live = Machine.Get();
	if (!Live || !Live->Layers.IsValidIndex(LayerIndex))
	{
		return;
	}

	// Its settings only: its states, transitions and comments are the graph's.
	FWidgetAnimMachineLayer& Target = Live->Layers[LayerIndex];
	Target.Name = Layer.Name;
	Target.BlendMode = Layer.BlendMode;
	Target.Weight = Layer.Weight;
	Target.WeightInput = Layer.WeightInput;
	Target.MaskMode = Layer.MaskMode;
	Target.MaskTargets = Layer.MaskTargets;
	Target.MaskChannels = Layer.MaskChannels;

	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		Live->Normalize();
		Load();
	}
	Live->MarkPackageDirty();
	Live->NotifyMachineChanged();
}

TArray<FString> UWidgetAnimMachineLayerProxy::GetNumberInputOptions() const
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	return Live ? Live->GetNumberInputOptions() : TArray<FString>();
}

TArray<FString> UWidgetAnimMachineLayerProxy::GetMaskTargetOptions() const
{
	const UWidgetAnimStateMachineAsset* Live = Machine.Get();
	return Live ? Live->GetMaskTargetOptions() : TArray<FString>();
}

// =====================================================================================================
// Details layouts
// =====================================================================================================

namespace
{
	/**
	 * A transition: Blend's fields in one category, in the order they are read - and the spring's among them, flat, shown only
	 * for a Spring, as Duration is only for a Tween. Animation Duration only for a transition animation.
	 */
	/**
	 * A state's Details in categories of their own, in this order: State Properties (Name, Plays, Playback, Playback Speed,
	 * Comment, Node Color), Play (what it plays and what drives it - Animation, Random Variations, Blend Input and Samples,
	 * Speed, Progress, Parameters From Inputs), then Actions. The state is one struct shown by its inner properties, which
	 * would otherwise all sit in its one category: each field goes into the category it names.
	 */
	class FWidgetAnimMachineStateDetails : public IDetailCustomization
	{
	public:

		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
		{
			const TSharedRef<IPropertyHandle> StateHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimMachineStateProxy, State), UWidgetAnimMachineStateProxy::StaticClass());
			if (!StateHandle->IsValidHandle())
			{
				return;
			}
			DetailBuilder.HideProperty(StateHandle);

			// Made in this order, so they are listed in it.
			for (const TCHAR* Ordered : { TEXT("State Properties"), TEXT("Play"), TEXT("Actions") })
			{
				DetailBuilder.EditCategory(Ordered);
			}
			uint32 NumChildren = 0;
			StateHandle->GetNumChildren(NumChildren);
			for (uint32 Index = 0; Index < NumChildren; ++Index)
			{
				const TSharedPtr<IPropertyHandle> Child = StateHandle->GetChildHandle(Index);
				const FProperty* Field = Child.IsValid() ? Child->GetProperty() : nullptr;
				// An inline edit condition's box (Node Color's) is drawn in its field's row, never as a row of its own.
				if (!Field || !Field->HasAnyPropertyFlags(CPF_Edit) || Field->HasMetaData(TEXT("InlineEditConditionToggle")))
				{
					continue;
				}
				const FString Category = Field->GetMetaData(TEXT("Category"));
				DetailBuilder.EditCategory(Category.IsEmpty() ? FName(TEXT("State Properties")) : FName(*Category)).AddProperty(Child);
			}
		}
	};

	class FWidgetAnimMachineTransitionDetails : public IDetailCustomization
	{
	public:

		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
		{
			const TSharedRef<IPropertyHandle> TransitionHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimMachineTransitionProxy, Transition), UWidgetAnimMachineTransitionProxy::StaticClass());
			if (!TransitionHandle->IsValidHandle())
			{
				return;
			}

			const TSharedPtr<IPropertyHandle> BlendHandle = TransitionHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineTransition, Blend));
			auto ShowFor = [BlendHandle](EWidgetAnimMachineBlend Wanted)
			{
				return TAttribute<EVisibility>::CreateLambda([BlendHandle, Wanted]()
				{
					uint8 Value = 0;
					const bool bIs = BlendHandle.IsValid() && BlendHandle->GetValue(Value) == FPropertyAccess::Success && Value == static_cast<uint8>(Wanted);
					return bIs ? EVisibility::Visible : EVisibility::Collapsed;
				});
			};

			IDetailCategoryBuilder& Blend = DetailBuilder.EditCategory(TEXT("Blend"));
			const FName BlendFields[] =
			{
				GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineTransition, Blend),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineTransition, Duration),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineTransition, TransitionAnimation),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineTransition, AnimationDuration),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineTransition, TransitionAnimationSpeed),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineTransition, AnimationBlendTime),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineTransition, Easing),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineTransition, CustomCurve),
			};
			for (const FName Field : BlendFields)
			{
				const TSharedPtr<IPropertyHandle> Handle = TransitionHandle->GetChildHandle(Field);
				if (!Handle.IsValid())
				{
					continue;
				}
				IDetailPropertyRow& Row = Blend.AddProperty(Handle);

				// Its tick box is Override: without it the animation's own length is shown, greyed.
				if (Field == GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineTransition, AnimationDuration))
				{
					Row.Visibility(ShowFor(EWidgetAnimMachineBlend::Animation));
				}
			}

			// The spring's own fields, flat in Blend - only while Blend is Spring.
			const TSharedPtr<IPropertyHandle> SpringHandle = TransitionHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineTransition, Spring));
			if (SpringHandle.IsValid())
			{
				DetailBuilder.HideProperty(SpringHandle);
				uint32 NumFields = 0;
				SpringHandle->GetNumChildren(NumFields);
				for (uint32 FieldIndex = 0; FieldIndex < NumFields; ++FieldIndex)
				{
					const TSharedPtr<IPropertyHandle> Field = SpringHandle->GetChildHandle(FieldIndex);
					if (Field.IsValid())
					{
						Blend.AddProperty(Field).Visibility(ShowFor(EWidgetAnimMachineBlend::Spring));
					}
				}
			}
		}
	};

	/** The Widget Blueprint's own state machine's name (see UWidgetAnimTimelinesExtension::StateMachineName). */
	namespace WidgetAnimDraftMachineName
	{
		/** The extension holding Draft - null for a machine that is an asset of its own. */
		UWidgetAnimTimelinesExtension* ExtensionOf(const UWidgetAnimStateMachineAsset* Draft)
		{
			return Draft ? Cast<UWidgetAnimTimelinesExtension>(Draft->GetOuter()) : nullptr;
		}

		/** As renamed, else "Card State Machine" for WBP_Card. */
		FText Get(const UWidgetAnimTimelinesExtension* Extension)
		{
			if (Extension && !Extension->StateMachineName.IsEmpty())
			{
				return FText::FromString(Extension->StateMachineName);
			}
			const UWidgetBlueprint* Owner = Extension ? Extension->GetTypedOuter<UWidgetBlueprint>() : nullptr;
			FString BaseName = Owner ? Owner->GetName() : FString();
			BaseName.RemoveFromStart(TEXT("WBP_"));
			return BaseName.IsEmpty()
				? LOCTEXT("DefaultDraftMachineName", "State Machine")
				: FText::Format(LOCTEXT("DefaultDraftMachineNameFor", "{0} State Machine"), FText::FromString(BaseName));
		}

		/** Renamed, as one undo step; empty goes back to the default. */
		void Set(UWidgetAnimTimelinesExtension* Extension, const FString& NewName)
		{
			const FString Trimmed = NewName.TrimStartAndEnd();
			if (!Extension || Trimmed == Extension->StateMachineName || Trimmed == Get(Extension).ToString())
			{
				return;
			}
			const FScopedTransaction Transaction(LOCTEXT("RenameDraftMachine", "Rename State Machine"));
			Extension->Modify();
			Extension->StateMachineName = Trimmed;
			Extension->MarkPackageDirty();
		}
	}

	/**
	 * A layer: its settings, and the machine's own under them - what the Details show when nothing is picked: the Widget
	 * Blueprint's own machine's Name, and the machine's Description.
	 */
	class FWidgetAnimMachineLayerDetails : public IDetailCustomization
	{
	public:

		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
		{
			TArray<TWeakObjectPtr<UObject>> Objects;
			DetailBuilder.GetObjectsBeingCustomized(Objects);
			const UWidgetAnimMachineLayerProxy* Proxy = Objects.Num() == 1 ? Cast<UWidgetAnimMachineLayerProxy>(Objects[0].Get()) : nullptr;
			UWidgetAnimStateMachineAsset* Machine = Proxy ? Proxy->Machine.Get() : nullptr;
			if (!Machine)
			{
				return;
			}
			IDetailCategoryBuilder& MachineCategory = DetailBuilder.EditCategory(TEXT("StateMachine"), LOCTEXT("MachineCategory", "State Machine"));
			if (UWidgetAnimTimelinesExtension* Extension = WidgetAnimDraftMachineName::ExtensionOf(Machine))
			{
				const TWeakObjectPtr<UWidgetAnimTimelinesExtension> WeakExtension(Extension);
				MachineCategory.AddCustomRow(LOCTEXT("DraftNameFilter", "Name"))
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(LOCTEXT("DraftNameLabel", "Name"))
					.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::DraftMachineNameTooltip()))
				]
				.ValueContent()
				.MinDesiredWidth(180.f)
				[
					SNew(SEditableTextBox)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([WeakExtension]() { return WidgetAnimDraftMachineName::Get(WeakExtension.Get()); })
					.OnTextCommitted_Lambda([WeakExtension](const FText& NewText, ETextCommit::Type)
					{
						WidgetAnimDraftMachineName::Set(WeakExtension.Get(), NewText.ToString());
					})
				];
			}
			TArray<UObject*> MachineObjects;
			MachineObjects.Add(Machine);
			MachineCategory.AddExternalObjectProperty(MachineObjects, GET_MEMBER_NAME_CHECKED(UWidgetAnimStateMachineAsset, Description));
		}
	};

	/** The machine itself: its inputs and layers are edited in their own panels, so their arrays aren't shown here. */
	class FWidgetAnimMachineAssetDetails : public IDetailCustomization
	{
	public:

		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
		{
			DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimStateMachineAsset, Inputs)));
			DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimStateMachineAsset, Layers)));
		}
	};
}

// =====================================================================================================
// The preview on Preview Targets
// =====================================================================================================

/**
 * The machine played on widgets of the designer's preview with no Widget Animation Component: the widget it plays on and a
 * widget per target, picked in the editor's Preview Targets. Plays as a component's designer preview does - from the authored
 * pose, put back as it stops - on an active timer of its own, which keeps the designer repainting while it moves.
 */
class FWidgetAnimMachinePreviewHost : public TSharedFromThis<FWidgetAnimMachinePreviewHost>, public FGCObject
{
public:

	virtual ~FWidgetAnimMachinePreviewHost() override
	{
		Stop();
	}

	bool Start(UWidgetAnimStateMachineAsset* Machine, UUserWidget* InRoot, FName HostPath, const TMap<FName, FName>& TargetPaths)
	{
		Stop();
		if (!Machine || !InRoot)
		{
			return false;
		}

		UWidget* HostWidget = HostPath.IsNone() ? InRoot : UWidgetAnimLibrary::FindWidgetByPathInTree(InRoot, HostPath);
		if (!HostWidget)
		{
			HostWidget = InRoot;
		}
		const TSharedPtr<SWidget> SlateWidget = HostWidget->GetCachedWidget();
		if (!SlateWidget.IsValid())
		{
			return false;
		}

		Root = InRoot;
		Host = HostWidget;

		// A use of the machine, as a component's row would be: a widget for each target.
		Row = FWidgetAnimStateMachineEntry();
		Row.StateMachine = Machine;
		TArray<FName> TargetNames;
		Machine->GetTargetNames(TargetNames);
		for (const FName TargetName : TargetNames)
		{
			FWidgetAnimGroupTarget& Target = Row.Targets.AddDefaulted_GetRef();
			Target.GroupName = TargetName.ToString();
			const FName* Picked = TargetPaths.Find(TargetName);
			Target.TargetWidgetName = Picked ? *Picked : Machine->GetTargetDefault(TargetName);
		}

		const TWeakObjectPtr<UUserWidget> WeakRoot = Root;
		Context = FWidgetAnimMachineContext();
		Context.Host = HostWidget;
		Context.bDesignTime = true;
		Context.Baselines = &Baselines;
		Context.RestBaselines = &Baselines;
		Context.ResolvePath = [WeakRoot](FName Path) -> UWidget*
		{
			UUserWidget* Live = WeakRoot.Get();
			return Live && !Path.IsNone() ? UWidgetAnimLibrary::FindWidgetByPathInTree(Live, Path) : nullptr;
		};

		WidgetAnimStateMachines::Initialize(Instance, Row, /*RowIndex=*/0);

		// From the authored pose: every widget the machine can move, captured before anything moves it.
		TArray<FWidgetAnimSettings> ToCapture;
		WidgetAnimStateMachines::GatherAllSettings(Row, Context, ToCapture);
		Baselines.ResetBaselines();
		UWidgetAnimLibrary::CaptureWidgetAnimBaselines(HostWidget, ToCapture, TArray<FWidgetAnimSettings>(), Baselines);

		WidgetAnimStateMachines::Start(Instance, Context);
		bRunning = true;
		Tick(0.f);

		const TSharedRef<FActiveTimerHandle> Handle = SlateWidget->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &FWidgetAnimMachinePreviewHost::OnTimer));
		TimerHandle = Handle;
		TimerWidget = SlateWidget;
		return true;
	}

	void Stop()
	{
		if (!bRunning)
		{
			return;
		}
		bRunning = false;

		const TSharedPtr<FActiveTimerHandle> Handle = TimerHandle.Pin();
		const TSharedPtr<SWidget> Widget = TimerWidget.Pin();
		if (Handle.IsValid() && Widget.IsValid())
		{
			Widget->UnRegisterActiveTimer(Handle.ToSharedRef());
		}
		TimerHandle.Reset();
		TimerWidget.Reset();

		WidgetAnimStateMachines::Stop(Instance);
		LastPosed.Reset();
		if (Baselines.bCaptured && Root.IsValid())
		{
			UWidgetAnimLibrary::RestoreWidgetAnimBaselines(Baselines);
		}
		Baselines.ResetBaselines();
		Instance = FWidgetAnimMachineInstance();
	}

	bool IsRunning() const { return bRunning && Root.IsValid() && Host.IsValid(); }

	/** Paused: posed where it is, its time standing still - inputs still reach it, and take effect once it runs on. */
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }
	UUserWidget* GetRoot() const { return Root.Get(); }
	UWidget* GetHost() const { return Host.Get(); }
	const FWidgetAnimMachineInstance& GetInstance() const { return Instance; }

	void SetInput(FName Input, EWidgetAnimMachineInputType Type, float Value)
	{
		if (!bRunning)
		{
			return;
		}
		switch (Type)
		{
		case EWidgetAnimMachineInputType::Number:
		case EWidgetAnimMachineInputType::Integer:
			WidgetAnimStateMachines::SetNumber(Instance, Input, Value);
			break;
		case EWidgetAnimMachineInputType::Choice:
			WidgetAnimStateMachines::SetFromNumber(Instance, Input, Value);
			break;
		case EWidgetAnimMachineInputType::Trigger:
			if (Value != 0.f)
			{
				WidgetAnimStateMachines::FireTrigger(Instance, Input);
			}
			break;
		default:
			WidgetAnimStateMachines::SetBool(Instance, Input, Value > 0.5f);
			break;
		}
	}

	void SetVector(FName Input, const FVector2D& Value)
	{
		if (bRunning)
		{
			WidgetAnimStateMachines::SetVector2D(Instance, Input, Value);
		}
	}

	void SetBuiltIn(FName Input, bool bValue)
	{
		if (!bRunning)
		{
			return;
		}
		if (Input == WidgetAnimMachineInputs::Closing)
		{
			WidgetAnimStateMachines::SetClosing(Instance, Context, bValue);
		}
		else
		{
			WidgetAnimStateMachines::SetBuiltIn(Instance, Input, bValue);
		}
	}

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddPropertyReferences(FWidgetAnimMachineInstance::StaticStruct(), &Instance);
		Collector.AddPropertyReferences(FWidgetAnimStateMachineEntry::StaticStruct(), &Row);
	}
	virtual FString GetReferencerName() const override { return TEXT("FWidgetAnimMachinePreviewHost"); }

private:

	EActiveTimerReturnType OnTimer(double InCurrentTime, float InDeltaTime)
	{
		if (!bRunning || !Host.IsValid() || !Root.IsValid())
		{
			return EActiveTimerReturnType::Stop;
		}
		Tick(bPaused ? 0.f : InDeltaTime);
		return EActiveTimerReturnType::Continue;
	}

	void Tick(float DeltaTime)
	{
		UWidget* HostWidget = Host.Get();
		if (!bRunning || !HostWidget)
		{
			return;
		}
		WidgetAnimStateMachines::Tick(Instance, Context, DeltaTime);
		FWidgetAnimResult Result;
		WidgetAnimStateMachines::Compose(MakeArrayView(&Instance, 1), EWidgetAnimTracksWithMachines::AddOnTop, Context, Result, LastPosed);
		UWidgetAnimLibrary::ApplyComposedResult(HostWidget, Result, Baselines);
		WidgetAnimStateMachines::ReleaseFinishedTracks(Instance);
	}

	FWidgetAnimMachineInstance Instance;
	FWidgetAnimStateMachineEntry Row;
	FWidgetAnimBaselineSet Baselines;
	FWidgetAnimMachineContext Context;
	TArray<TPair<TWeakObjectPtr<UWidget>, FWidgetAnimPartKey>> LastPosed;
	TWeakObjectPtr<UUserWidget> Root;
	TWeakObjectPtr<UWidget> Host;
	TWeakPtr<FActiveTimerHandle> TimerHandle;
	TWeakPtr<SWidget> TimerWidget;
	bool bRunning = false;
	bool bPaused = false;
};

/**
 * While a preview target is being picked in the designer: the cursor an eyedropper, the widget under it named beside it, a
 * click on one taken as the pick - before the designer can select it - and Escape calling it off. Registered only while a pick
 * waits.
 */
class FWidgetAnimMachinePickInput : public IInputProcessor
{
public:

	explicit FWidgetAnimMachinePickInput(TWeakPtr<SWidgetAnimStateMachineEditor> InEditor)
		: Editor(InEditor)
	{
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
		const TSharedPtr<SWidgetAnimStateMachineEditor> Pinned = Editor.Pin();
		if (Pinned && Pinned->IsPickingTarget())
		{
			Cursor->SetType(EMouseCursor::EyeDropper);
		}
	}

	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimStateMachineEditor> Pinned = Editor.Pin();
		if (Pinned && Pinned->IsPickingTarget())
		{
			Pinned->UpdatePickHover(MouseEvent.GetScreenSpacePosition());
		}
		return false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimStateMachineEditor> Pinned = Editor.Pin();
		if (!Pinned || !Pinned->IsPickingTarget())
		{
			return false;
		}
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			Pinned->CancelTargetPick();
			return true;
		}
		Pinned->UpdatePickHover(MouseEvent.GetScreenSpacePosition());
		return Pinned->CommitPickHover();
	}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		const TSharedPtr<SWidgetAnimStateMachineEditor> Pinned = Editor.Pin();
		if (Pinned && Pinned->IsPickingTarget() && InKeyEvent.GetKey() == EKeys::Escape)
		{
			Pinned->CancelTargetPick();
			return true;
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("WidgetAnimMachineTargetPick"); }

private:

	TWeakPtr<SWidgetAnimStateMachineEditor> Editor;
};

/**
 * Lets the mouse wheel zoom the graph in past 1:1 - a little, to MaxGraphZoom - without Ctrl held, which the graph editor
 * otherwise wants for anything past 1:1. Seen before the graph editor, since it handles the wheel itself. Also takes a
 * double-click on a wire, for a reroute point there (see HandleGraphDoubleClick).
 */
class FWidgetAnimMachineZoomInput : public IInputProcessor
{
public:

	explicit FWidgetAnimMachineZoomInput(TWeakPtr<SWidgetAnimStateMachineEditor> InEditor)
		: Editor(InEditor)
	{
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
	}

	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent) override
	{
		const TSharedPtr<SWidgetAnimStateMachineEditor> Pinned = Editor.Pin();
		return Pinned.IsValid() && InGestureEvent == nullptr && Pinned->HandleGraphWheel(InWheelEvent);
	}

	/** A double-click on a wire: the graph editor never hears of it (the wires are drawn by the graph, not its pins). */
	virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimStateMachineEditor> Pinned = Editor.Pin();
		return Pinned.IsValid() && Pinned->HandleGraphDoubleClick(MouseEvent);
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("WidgetAnimMachineZoom"); }

private:

	TWeakPtr<SWidgetAnimStateMachineEditor> Editor;
};

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

	/** An input's type as the Blueprint type it reads as - see WidgetAnimTypePill. */
	WidgetAnimTypePill::EKind GetTypeKind(EWidgetAnimMachineInputType Type)
	{
		switch (Type)
		{
		case EWidgetAnimMachineInputType::Number:   return WidgetAnimTypePill::EKind::Float;
		case EWidgetAnimMachineInputType::Integer:  return WidgetAnimTypePill::EKind::Integer;
		case EWidgetAnimMachineInputType::Trigger:  return WidgetAnimTypePill::EKind::Trigger;
		case EWidgetAnimMachineInputType::Hold:     return WidgetAnimTypePill::EKind::Hold;
		case EWidgetAnimMachineInputType::Choice:   return WidgetAnimTypePill::EKind::Choice;
		case EWidgetAnimMachineInputType::Vector2D: return WidgetAnimTypePill::EKind::Vector2D;
		default:                                    return WidgetAnimTypePill::EKind::Bool;
		}
	}

	FText GetTypeLabel(EWidgetAnimMachineInputType Type)
	{
		return WidgetAnimTypePill::GetLabel(GetTypeKind(Type));
	}

	/** An editor icon at 16px, whatever size its brush was authored at. */
	/**
	 * A section of the left panel as the My Blueprint panel lays out its Variables: its heading in small capitals on the header
	 * strip, with Extra (an Add button, toggles) at its right, then its rows on the panel's recessed color - both edge to edge,
	 * so a row's selection spans the panel. Rows pad themselves, by RowPadding.
	 */
	TSharedRef<SWidget> MakePanelSection(const FText& Title, const FText& ToolTip, const TSharedRef<SWidget>& Extra, const TSharedRef<SWidget>& Rows)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
				.Padding(FMargin(8.f, 4.f, 4.f, 4.f))
				.ToolTip(WidgetAnimMachineStyle::MakeToolTip(ToolTip))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))
						.ColorAndOpacity(FStyleColors::ForegroundHover)
						.Text(Title.ToUpper())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						Extra
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
				.Padding(FMargin(0.f, 3.f))
				[
					Rows
				]
			];
	}

	/** Inside a panel row, edge to edge: its content's inset from the panel's sides. */
	const FMargin RowPadding(8.f, 3.f);

	/** A panel row's selection: square, edge to edge, as the My Blueprint panel's rows are. */
	const FSlateBrush* GetRowBrush()
	{
		return FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	}

	/** The Preview section's Targets / Components toggles: the editor's toggle buttons, checked in a dimmed animation orange. */
	const FCheckBoxStyle& GetPreviewSourceToggleStyle()
	{
		static const FCheckBoxStyle Style = []()
		{
			FCheckBoxStyle Made = FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox");
			const FLinearColor Orange = FLinearColor(NameTBD::Colors::AnimationOrange_Normal) * FLinearColor(0.78f, 0.78f, 0.78f, 1.f);
			Made.CheckedImage.TintColor = FSlateColor(Orange);
			Made.CheckedHoveredImage.TintColor = FSlateColor(FLinearColor(NameTBD::Colors::AnimationOrange_Normal) * FLinearColor(0.9f, 0.9f, 0.9f, 1.f));
			Made.CheckedPressedImage.TintColor = FSlateColor(Orange);
			return Made;
		}();
		return Style;
	}

	TSharedRef<SWidget> MakeFixedIcon(const FSlateBrush* Brush, TAttribute<FSlateColor> Color = FSlateColor::UseForeground())
	{
		return SNew(SBox)
			.WidthOverride(16.f)
			.HeightOverride(16.f)
			[
				SNew(SImage)
				.Image(Brush)
				.ColorAndOpacity(Color)
			];
	}

	/**
	 * A dropdown of widget paths with a search box at its top - as the Widget Animations panel's Preview Target picker is:
	 * typing narrows the list to the paths containing what is typed, Enter picks the first one left. The options are asked
	 * for as it opens, so widgets added to the designer since are there.
	 */
	TSharedRef<SWidget> MakeSearchablePicker(TFunction<TArray<FString>()> GetOptions, const TAttribute<FText>& Current, TFunction<void(const FString&)> OnPicked, const FText& ToolTip)
	{
		struct FPickerState
		{
			TArray<TSharedPtr<FString>> All;
			TArray<TSharedPtr<FString>> Filtered;
			TSharedPtr<SListView<TSharedPtr<FString>>> List;
			TWeakPtr<SComboButton> Combo;
		};
		const TSharedRef<FPickerState> State = MakeShared<FPickerState>();

		auto Pick = [State, OnPicked](const TSharedPtr<FString>& Item)
		{
			if (Item.IsValid())
			{
				OnPicked(*Item);
			}
			if (const TSharedPtr<SComboButton> Combo = State->Combo.Pin())
			{
				Combo->SetIsOpen(false);
			}
		};

		auto Filter = [State](const FString& Text)
		{
			State->Filtered.Reset();
			const FString Needle = Text.TrimStartAndEnd();
			for (const TSharedPtr<FString>& Option : State->All)
			{
				if (Option.IsValid() && (Needle.IsEmpty() || Option->Contains(Needle, ESearchCase::IgnoreCase)))
				{
					State->Filtered.Add(Option);
				}
			}
			if (State->List.IsValid())
			{
				State->List->RequestListRefresh();
			}
		};

		const TSharedRef<SComboButton> Combo = SNew(SComboButton)
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(ToolTip))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(Current)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			];

		Combo->SetOnGetMenuContent(FOnGetContent::CreateLambda([State, Filter, Pick, Current, GetOptions]() -> TSharedRef<SWidget>
		{
			State->All.Reset();
			for (const FString& Option : GetOptions())
			{
				State->All.Add(MakeShared<FString>(Option));
			}
			Filter(FString());

			const TSharedRef<SSearchBox> Search = SNew(SSearchBox)
				.HintText(LOCTEXT("SearchWidgetsHint", "Search widgets"))
				.OnTextChanged_Lambda([Filter](const FText& Text) { Filter(Text.ToString()); })
				.OnTextCommitted_Lambda([State, Pick](const FText&, ETextCommit::Type CommitType)
				{
					if (CommitType == ETextCommit::OnEnter && State->Filtered.Num() > 0)
					{
						Pick(State->Filtered[0]);
					}
				});

			const FString CurrentText = Current.Get().ToString();
			State->List = SNew(SListView<TSharedPtr<FString>>)
				.ListItemsSource(&State->Filtered)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow_Lambda([CurrentText](TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner)
				{
					const bool bCurrent = Item.IsValid() && *Item == CurrentText;
					return SNew(STableRow<TSharedPtr<FString>>, Owner)
						.Padding(FMargin(6.f, 2.f))
						[
							SNew(STextBlock)
							.Font(bCurrent ? IDetailLayoutBuilder::GetDetailFontBold() : IDetailLayoutBuilder::GetDetailFont())
							.Text(FText::FromString(Item.IsValid() ? *Item : FString()))
						];
				})
				.OnMouseButtonClick_Lambda([Pick](TSharedPtr<FString> Item) { Pick(Item); });

			if (const TSharedPtr<SComboButton> Pinned = State->Combo.Pin())
			{
				Pinned->SetMenuContentWidgetToFocus(Search);
			}

			return SNew(SBox)
				.MinDesiredWidth(240.f)
				.MaxDesiredHeight(360.f)
				.Padding(FMargin(4.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 0.f, 0.f, 4.f))
					[
						Search
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						State->List.ToSharedRef()
					]
				];
		}));
		State->Combo = Combo;
		return Combo;
	}

	/** A node's box in graph space, as the graph lays it out. */
	bool GetNodeBox(const TSharedPtr<SGraphEditor>& Editor, const UEdGraphNode* Node, FBox2f& OutBox)
	{
		FSlateRect Rect;
		if (!Editor.IsValid() || !Node || !Editor->GetBoundsForNode(Node, Rect, 0.f))
		{
			return false;
		}
		OutBox = FBox2f(FVector2f(Rect.Left, Rect.Top), FVector2f(Rect.Right, Rect.Bottom));
		return true;
	}

	bool BoxesOverlap(const FBox2f& A, const FBox2f& B, float Margin)
	{
		return A.Min.X < B.Max.X + Margin && B.Min.X < A.Max.X + Margin && A.Min.Y < B.Max.Y + Margin && B.Min.Y < A.Max.Y + Margin;
	}

	FBox2f ShiftBox(const FBox2f& Box, const FVector2f& By)
	{
		return FBox2f(Box.Min + By, Box.Max + By);
	}

	/** How far apart states are kept when they are lined up. */
	constexpr float ArrangeMargin = 24.f;
}

namespace
{
	/**
	 * How wide the editor's three columns were left - inputs, graph, Details - kept in the editor's per-project settings as
	 * they are dragged, so the editor opens the way it was left: after a close, a dock or undock, a restart. The drawer and the
	 * docked tab each keep their own, being shaped so differently.
	 */
	const TCHAR* LayoutConfigSection = TEXT("NameTBD.WidgetAnimationStateMachineEditor");

	TMap<FName, float>& GetKnownSplits()
	{
		static TMap<FName, float> Known;
		return Known;
	}

	float GetSplit(FName Key, float Default)
	{
		if (const float* Known = GetKnownSplits().Find(Key))
		{
			return *Known;
		}
		float Saved = Default;
		if (!GConfig || !GConfig->GetFloat(LayoutConfigSection, *Key.ToString(), Saved, GEditorPerProjectIni) || Saved <= 0.f)
		{
			Saved = Default;
		}
		GetKnownSplits().Add(Key, Saved);
		return Saved;
	}

	void SetSplit(FName Key, float NewSize)
	{
		GetKnownSplits().Add(Key, NewSize);
		if (GConfig)
		{
			GConfig->SetFloat(LayoutConfigSection, *Key.ToString(), NewSize, GEditorPerProjectIni);
		}
	}

	/**
	 * The machine last edited for each Widget Blueprint, by the Blueprint's path - so the editor opens on it again, in this
	 * session or the next, rather than asking which one. Per user and project, like the column widths above.
	 */
	const TCHAR* EditedMachineConfigSection = TEXT("NameTBD.WidgetAnimationStateMachineEditor.EditedMachines");

	UWidgetAnimStateMachineAsset* LoadEditedMachine(const UWidgetBlueprint* Blueprint)
	{
		FString Path;
		if (!Blueprint || !GConfig || !GConfig->GetString(EditedMachineConfigSection, *Blueprint->GetPathName(), Path, GEditorPerProjectIni) || Path.IsEmpty())
		{
			return nullptr;
		}
		return Cast<UWidgetAnimStateMachineAsset>(FSoftObjectPath(Path).TryLoad());
	}

	void SaveEditedMachine(const UWidgetBlueprint* Blueprint, const UWidgetAnimStateMachineAsset* EditedMachine)
	{
		if (!Blueprint || !GConfig)
		{
			return;
		}
		const FString Key = Blueprint->GetPathName();
		const FString Path = EditedMachine ? FSoftObjectPath(EditedMachine).ToString() : FString();
		FString Saved;
		GConfig->GetString(EditedMachineConfigSection, *Key, Saved, GEditorPerProjectIni);
		if (Saved == Path)
		{
			return;
		}
		if (Path.IsEmpty())
		{
			GConfig->RemoveKey(EditedMachineConfigSection, *Key, GEditorPerProjectIni);
		}
		else
		{
			GConfig->SetString(EditedMachineConfigSection, *Key, *Path, GEditorPerProjectIni);
		}
		GConfig->Flush(/*bRemoveFromCache=*/false, GEditorPerProjectIni);
	}

	FName MakeSplitKey(bool bInDrawer, const TCHAR* Column)
	{
		return FName(FString::Printf(TEXT("%s.%s"), bInDrawer ? TEXT("Drawer") : TEXT("Tab"), Column));
	}

	TAttribute<float> SplitSize(FName Key, float Default)
	{
		return TAttribute<float>::CreateLambda([Key, Default]() { return GetSplit(Key, Default); });
	}

	SSplitter::FOnSlotResized SplitResized(FName Key)
	{
		return SSplitter::FOnSlotResized::CreateLambda([Key](float NewSize) { SetSplit(Key, NewSize); });
	}
}

void SWidgetAnimStateMachineEditor::Construct(const FArguments& InArgs)
{
	WidgetBlueprint = InArgs._WidgetBlueprint;
	bInDrawer = InArgs._InDrawer;
	OnDockInLayout = InArgs._OnDockInLayout;

	StateProxy = NewObject<UWidgetAnimMachineStateProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	TransitionProxy = NewObject<UWidgetAnimMachineTransitionProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	InputProxy = NewObject<UWidgetAnimMachineInputProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	LayerProxy = NewObject<UWidgetAnimMachineLayerProxy>(GetTransientPackage(), NAME_None, RF_Transient);
	PreviewHost = MakeShared<FWidgetAnimMachinePreviewHost>();

	MapGraphCommands();

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsArgs.bAllowSearch = true;
	DetailsView = PropertyModule.CreateDetailView(DetailsArgs);
	DetailsView->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateLambda([this]() { return !bMachineLocked; }));
	RegisterDetailLayouts();

	MachineChangedHandle = UWidgetAnimStateMachineAsset::OnMachineChanged().AddSP(this, &SWidgetAnimStateMachineEditor::OnMachineChanged);

	// Three panels side by side, all the way to the top - as the Widget Animations panel's are - with the bar (what is edited,
	// Save, the preview's controls) over the graph alone.
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)

			// Preview targets and inputs: the machine's, live, and the widget's own.
			+ SSplitter::Slot()
			.Value(SplitSize(MakeSplitKey(bInDrawer, TEXT("Inputs")), 0.2f))
			.OnSlotResized(SplitResized(MakeSplitKey(bInDrawer, TEXT("Inputs"))))
			[
				// The panel's recessed color, all of it: its sections' header strips and rows edge to edge.
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
				.Padding(FMargin(0.f))
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(PreviewBox, SVerticalBox)
					]
					+ SScrollBox::Slot()
					[
						SAssignNew(InputsBox, SVerticalBox)
					]
				]
			]

			// The layers, as graphs.
			+ SSplitter::Slot()
			.Value(SplitSize(MakeSplitKey(bInDrawer, TEXT("Graph")), 0.55f))
			.OnSlotResized(SplitResized(MakeSplitKey(bInDrawer, TEXT("Graph"))))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MakeToolbar()
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(2.f, 2.f, 2.f, 0.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SAssignNew(LayerTabs, SHorizontalBox)
					]
					// Collapsed states open on their own: where the graph is, and the way back.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
					[
						MakeGroupBreadcrumb()
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SSpacer)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SComboButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.HasDownArrow(true)
						.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::AddStateButtonTooltip()))
						.IsEnabled_Lambda([this]() { return IsMachineEditable(); })
						.OnGetMenuContent(this, &SWidgetAnimStateMachineEditor::MakeAddStateMenu)
						.ButtonContent()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								MakeFixedIcon(FAppStyle::GetBrush(TEXT("Icons.Plus")))
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("AddStateButton", "Add State"))
							]
						]
					]
					// Beside Add State: back to the state in play.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(2.f, 0.f, 0.f, 0.f))
					[
						MakeJumpToActiveButton()
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SAssignNew(GraphHost, SBox)
				]
			]

			// What is picked.
			+ SSplitter::Slot()
			.Value(SplitSize(MakeSplitKey(bInDrawer, TEXT("Details")), 0.25f))
			.OnSlotResized(SplitResized(MakeSplitKey(bInDrawer, TEXT("Details"))))
			[
				DetailsView.ToSharedRef()
			]
		]
	];

	// The one asked for; else the one last edited for this Widget Blueprint (kept across sessions); else the first the
	// Widget Blueprint plays.
	UWidgetAnimStateMachineAsset* Start = InArgs._Machine.Get();
	if (!Start)
	{
		Start = LoadEditedMachine(WidgetBlueprint.Get());
	}
	if (!Start)
	{
		Start = FindMachineInWidgetBlueprint();
	}
	if (!Start)
	{
		// The Widget Blueprint's own machine, if it has one already - never made just by opening the editor.
		const UWidgetAnimTimelinesExtension* Extension = UWidgetAnimTimelinesExtension::Find(WidgetBlueprint.Get());
		Start = Extension ? Extension->StateMachine.Get() : nullptr;
	}
	SetMachine(Start);

	// The wheel zooms in a little past 1:1 - see FWidgetAnimMachineZoomInput.
	ZoomInput = MakeShared<FWidgetAnimMachineZoomInput>(SharedThis(this));
	FSlateApplication::Get().RegisterInputPreProcessor(ZoomInput);
}

SWidgetAnimStateMachineEditor::~SWidgetAnimStateMachineEditor()
{
	if (ZoomInput.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(ZoomInput);
	}
	ZoomInput.Reset();
	UWidgetAnimStateMachineAsset::OnMachineChanged().Remove(MachineChangedHandle);
	CancelTargetPick();
	StopPreviews();
}

void SWidgetAnimStateMachineEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Machine);
	Collector.AddReferencedObjects(Graphs);
	Collector.AddReferencedObject(StateProxy);
	Collector.AddReferencedObject(TransitionProxy);
	Collector.AddReferencedObject(InputProxy);
	Collector.AddReferencedObject(LayerProxy);
}

void SWidgetAnimStateMachineEditor::RegisterDetailLayouts()
{
	DetailsView->RegisterInstancedCustomPropertyLayout(UWidgetAnimMachineStateProxy::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateLambda([]() -> TSharedRef<IDetailCustomization> { return MakeShared<FWidgetAnimMachineStateDetails>(); }));
	DetailsView->RegisterInstancedCustomPropertyLayout(UWidgetAnimMachineTransitionProxy::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateLambda([]() -> TSharedRef<IDetailCustomization> { return MakeShared<FWidgetAnimMachineTransitionDetails>(); }));
	DetailsView->RegisterInstancedCustomPropertyLayout(UWidgetAnimMachineLayerProxy::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateLambda([]() -> TSharedRef<IDetailCustomization> { return MakeShared<FWidgetAnimMachineLayerDetails>(); }));
	DetailsView->RegisterInstancedCustomPropertyLayout(UWidgetAnimStateMachineAsset::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateLambda([]() -> TSharedRef<IDetailCustomization> { return MakeShared<FWidgetAnimMachineAssetDetails>(); }));
}

void SWidgetAnimStateMachineEditor::MapGraphCommands()
{
	GraphCommands = MakeShared<FUICommandList>();
	const FGenericCommands& Generic = FGenericCommands::Get();
	GraphCommands->MapAction(Generic.Delete,
		FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::DeleteSelected),
		FCanExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::CanDeleteSelected));
	GraphCommands->MapAction(Generic.Rename,
		FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::RenameSelected),
		FCanExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::CanRenameSelected));
	GraphCommands->MapAction(Generic.SelectAll,
		FExecuteAction::CreateLambda([this]()
		{
			if (const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor())
			{
				Editor->SelectAllNodes();
			}
		}));
	GraphCommands->MapAction(Generic.Copy,
		FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::CopySelected),
		FCanExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::CanCopySelected));
	GraphCommands->MapAction(Generic.Cut,
		FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::CutSelected),
		FCanExecuteAction::CreateLambda([this]() { return IsMachineEditable() && CanCopySelected(); }));
	GraphCommands->MapAction(Generic.Paste,
		FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::PasteHere),
		FCanExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::CanPaste));
	GraphCommands->MapAction(Generic.Duplicate,
		FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::DuplicateSelected),
		FCanExecuteAction::CreateLambda([this]() { return IsMachineEditable() && CanCopySelected(); }));

	// C for a comment box, as in a Blueprint graph.
	const FGraphEditorCommandsImpl& Graph = FGraphEditorCommands::Get();
	GraphCommands->MapAction(Graph.CreateComment, FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::AddCommentAroundSelection));

	// Keys taken before the graph editor sees them (see OnPreviewKeyDown). The graph editor binds Q to its own Straighten
	// Connections, which lines nodes up by their pins, and its binding wins over ours - so Q is caught first and does ours. The
	// align and distribute keys are caught too, and do nothing: the graph editor's own versions stack states on each other.
	PreviewCommands = MakeShared<FUICommandList>();
	PreviewCommands->MapAction(Graph.StraightenConnections, FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::StraightenSelected));
	for (const TSharedPtr<FUICommandInfo>& Blocked : { Graph.AlignNodesLeft, Graph.AlignNodesCenter, Graph.AlignNodesRight, Graph.AlignNodesTop,
		Graph.AlignNodesMiddle, Graph.AlignNodesBottom, Graph.DistributeNodesHorizontally, Graph.DistributeNodesVertically })
	{
		PreviewCommands->MapAction(Blocked, FExecuteAction::CreateLambda([]() {}));
	}
	GraphCommands->MapAction(Graph.EnableNodes,
		FExecuteAction::CreateLambda([this]() { SetSelectedTransitionsEnabled(true); }),
		FCanExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::HasSelectedTransitions));
	GraphCommands->MapAction(Graph.DisableNodes,
		FExecuteAction::CreateLambda([this]() { SetSelectedTransitionsEnabled(false); }),
		FCanExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::HasSelectedTransitions));
}

void SWidgetAnimStateMachineEditor::SetMachine(UWidgetAnimStateMachineAsset* InMachine)
{
	CancelTargetPick();
	StopPreviews();
	PreviewSignature.Reset();

	Machine = InMachine;
	if (Machine && !IsDraftMachine(Machine))
	{
		LastAssetMachine = Machine;
	}
	SaveEditedMachine(WidgetBlueprint.Get(), Machine);
	JumpSteps.Reset();
	JumpStep = INDEX_NONE;
	Graphs.Reset();
	GraphEditors.Reset();
	ShownLayer = 0;
	InputValues.Reset();
	BuiltInValues.Reset();
	DebugComponent.Reset();
	bDebugPickedByHand = false;
	bPreviewSourceChosen = false;
	PendingSelection.Reset();
	bPanelsDirty = true;

	if (Machine)
	{
		Machine->Normalize();
		for (const FWidgetAnimMachineInput& Input : Machine->Inputs)
		{
			// A Press And Hold starts let go, whatever a Default left from when it was a Bool says.
			InputValues.Add(Input.Name, Input.Type == EWidgetAnimMachineInputType::Hold || Input.Type == EWidgetAnimMachineInputType::Choice ? 0.f
				: Input.Type == EWidgetAnimMachineInputType::Integer ? static_cast<float>(Input.DefaultInteger)
				: Input.Type == EWidgetAnimMachineInputType::Number ? Input.DefaultNumber : (Input.bDefaultBool ? 1.f : 0.f));
		}
		ShowLayer(0);
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
	Graph->GetDebug = [WeakSelf, Graph](FWidgetAnimMachineLayerDebug& OutDebug)
	{
		// The graph's layer as it is now - layers move when one before them is removed.
		const TSharedPtr<SWidgetAnimStateMachineEditor> Self = WeakSelf.Pin();
		return Self.IsValid() && Self->GetLayerDebug(Graph->LayerIndex, OutDebug);
	};
	Graph->CanEditMachine = [WeakSelf]()
	{
		const TSharedPtr<SWidgetAnimStateMachineEditor> Self = WeakSelf.Pin();
		return Self.IsValid() && Self->IsMachineEditable();
	};
	Graph->OnCollapsed = [WeakSelf](const FGuid& GroupId)
	{
		if (const TSharedPtr<SWidgetAnimStateMachineEditor> Self = WeakSelf.Pin())
		{
			// Picked once the graph is made again, then straightened once its box has been laid out (see Tick).
			Self->PendingSelection = { GroupId };
			Self->PendingStraightenFrames = 3;
		}
	};
	Graph->OnStraighten = [WeakSelf]()
	{
		if (const TSharedPtr<SWidgetAnimStateMachineEditor> Self = WeakSelf.Pin())
		{
			Self->StraightenSelected();
		}
	};
	Graph->OnOpenState = [WeakSelf](const FGuid& StateId)
	{
		if (const TSharedPtr<SWidgetAnimStateMachineEditor> Self = WeakSelf.Pin())
		{
			Self->OpenState(StateId);
		}
	};
	Graph->OnJumpToState = [WeakSelf, Graph](const FGuid& StateId)
	{
		if (const TSharedPtr<SWidgetAnimStateMachineEditor> Self = WeakSelf.Pin())
		{
			Self->JumpToState(Graph->LayerIndex, StateId);
		}
	};
	Graph->GetSelectedTransitions = [WeakSelf](TArray<FGuid>& OutTransitions)
	{
		if (const TSharedPtr<SWidgetAnimStateMachineEditor> Self = WeakSelf.Pin())
		{
			TArray<FGuid> States;
			TArray<FGuid> Comments;
			Self->GetSelectedIds(States, OutTransitions, Comments);
		}
	};
	Graph->GetSelectedStates = [WeakSelf](TArray<FGuid>& OutStates)
	{
		if (const TSharedPtr<SWidgetAnimStateMachineEditor> Self = WeakSelf.Pin())
		{
			TArray<FGuid> Transitions;
			TArray<FGuid> Comments;
			Self->GetSelectedIds(OutStates, Transitions, Comments);
		}
	};
	Graph->RebuildFromMachine();
	Graphs[LayerIndex] = Graph;
	return Graph;
}

TSharedPtr<SGraphEditor> SWidgetAnimStateMachineEditor::GetShownGraphEditor() const
{
	return GraphEditors.IsValidIndex(ShownLayer) ? GraphEditors[ShownLayer] : nullptr;
}

UWidgetAnimMachineGraph* SWidgetAnimStateMachineEditor::GetShownGraph() const
{
	return Graphs.IsValidIndex(ShownLayer) ? Graphs[ShownLayer].Get() : nullptr;
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
		// Locked (the bar's lock): read only, as the Widget Animations panel's lock makes its tracks.
		.IsEditable_Lambda([this]() { return IsMachineEditable(); })
		.Appearance(Appearance)
		.GraphToEdit(Graph)
		.GraphEvents(Events);
}

namespace
{
	/**
	 * Over the graph while collapsed states are open on their own: its edges darkened - a nested graph - as the Widget Animations
	 * panel darkens its Track Editor while a collapsed track is open, a little lighter (GraphScopeVignette). No frame line round
	 * it: the graph has its own edge.
	 */
	class SWidgetAnimMachineScopeVignette : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetAnimMachineScopeVignette) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&) {}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			namespace ScopeLook = NameTBD::Colors::WidgetAnimEditor;
			const FVector2f Size = AllottedGeometry.GetLocalSize();
			const float Reach = FMath::Min(ScopeLook::ScopeVignetteWidth, FMath::Min(Size.X, Size.Y) * 0.5f);
			if (Reach > 1.f)
			{
				const FLinearColor Dark = ScopeLook::GraphScopeVignette;
				const FLinearColor Clear = Dark.CopyWithNewOpacity(0.f);
				// One gradient per edge, darkest at the edge. Orient_Vertical runs the stops along X, Orient_Horizontal along Y.
				auto Edge = [&](float X, float Y, float W, float H, EOrientation Orientation, bool bDarkFirst)
				{
					const float Length = Orientation == Orient_Vertical ? W : H;
					TArray<FSlateGradientStop> Stops;
					Stops.Add(FSlateGradientStop(FVector2D(0.0, 0.0), bDarkFirst ? Dark : Clear));
					Stops.Add(FSlateGradientStop(Orientation == Orient_Vertical ? FVector2D(Length, 0.0) : FVector2D(0.0, Length), bDarkFirst ? Clear : Dark));
					FSlateDrawElement::MakeGradient(OutDrawElements, LayerId,
						AllottedGeometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
						MoveTemp(Stops), Orientation, ESlateDrawEffect::None);
				};
				Edge(0.f, 0.f, Reach, Size.Y, Orient_Vertical, /*bDarkFirst=*/true);
				Edge(Size.X - Reach, 0.f, Reach, Size.Y, Orient_Vertical, /*bDarkFirst=*/false);
				Edge(0.f, 0.f, Size.X, Reach, Orient_Horizontal, /*bDarkFirst=*/true);
				Edge(0.f, Size.Y - Reach, Size.X, Reach, Orient_Horizontal, /*bDarkFirst=*/false);
			}
			return LayerId + 1;
		}
	};
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

	// The graph; over it, while collapsed states are open on their own, the vignette (the way back is in the layer row -
	// MakeGroupBreadcrumb); and play / pause at its bottom left.
	const TWeakObjectPtr<UWidgetAnimMachineGraph> WeakGraph(Graph);
	GraphHost->SetContent(
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			GraphEditors[ShownLayer].ToSharedRef()
		]
		// A collapsed group open on its own: the edges darkened toward a thin frame - a nested graph, as a collapsed track
		// open in the Widget Animations panel reads.
		+ SOverlay::Slot()
		[
			SNew(SWidgetAnimMachineScopeVignette)
			.Visibility_Lambda([WeakGraph]()
			{
				const UWidgetAnimMachineGraph* Shown = WeakGraph.Get();
				return Shown && Shown->OpenGroup.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
		]
		// Play / pause the preview: floating at the bottom left.
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(10.f))
		[
			MakePlayPauseButton()
		]);
	ShowLayerDetails();
}

namespace
{
	/**
	 * A layer tab's badge: what its Affects and Channels leave out - "only 2", "all but 1", "Scale", "only 2, 3 channels".
	 * Empty for a layer that poses everything.
	 */
	FText MakeLayerMaskBadgeText(const FWidgetAnimMachineLayer& Layer)
	{
		TArray<FText> Parts;
		if (Layer.MaskMode == EWidgetAnimMachineLayerMask::OnlyThese)
		{
			Parts.Add(FText::Format(LOCTEXT("LayerMaskOnly", "only {0}"), FText::AsNumber(Layer.MaskTargets.Num())));
		}
		else if (Layer.MaskMode == EWidgetAnimMachineLayerMask::AllButThese)
		{
			Parts.Add(FText::Format(LOCTEXT("LayerMaskAllBut", "all but {0}"), FText::AsNumber(Layer.MaskTargets.Num())));
		}
		if (Layer.HasChannelMask())
		{
			const UEnum* Channels = StaticEnum<EWidgetAnimMachineLayerChannel>();
			TArray<FText> Names;
			for (int32 Bit = 0; Bit < 8; ++Bit)
			{
				if (Layer.MaskChannels & (1 << Bit))
				{
					Names.Add(Channels->GetDisplayNameTextByValue(static_cast<int64>(1) << Bit));
				}
			}
			Parts.Add(Names.Num() == 0 ? LOCTEXT("LayerMaskNoChannels", "no channels")
				: Names.Num() == 1 ? Names[0]
				: FText::Format(LOCTEXT("LayerMaskChannels", "{0} channels"), FText::AsNumber(Names.Num())));
		}
		return FText::Join(LOCTEXT("LayerMaskJoin", ", "), Parts);
	}
}

void SWidgetAnimStateMachineEditor::RebuildLayerTabs()
{
	LayerTabs->ClearChildren();
	LayerNameTexts.Reset();
	if (!Machine)
	{
		return;
	}

	// The tabs together in one dark, rounded box - a segmented control, as the editor's own (Dynamic | Fixed | Programmable)
	// are - the layer shown lighter.
	TSharedRef<SHorizontalBox> Segments = SNew(SHorizontalBox);
	LayerTabs->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
		[
			SNew(SBorder)
			.BorderImage(WidgetAnimMachineStyle::RoundedBrush(4.f))
			.BorderBackgroundColor(FLinearColor(0.008f, 0.008f, 0.008f, 1.f))
			.Padding(FMargin(2.f))
			[
				Segments
			]
		];

	for (int32 LayerIndex = 0; LayerIndex < Machine->Layers.Num(); ++LayerIndex)
	{
		const FWidgetAnimMachineLayer& Layer = Machine->Layers[LayerIndex];
		const FText Blend = StaticEnum<EWidgetAnimMachineLayerBlend>()->GetDisplayNameTextByValue(static_cast<int64>(Layer.BlendMode));
		TSharedPtr<SInlineEditableTextBlock> NameText;

		// A tab: click shows the layer and its settings, double-click renames it.
		Segments->AddSlot()
			.AutoWidth()
			.Padding(FMargin(0.f, 0.f, LayerIndex + 1 < Machine->Layers.Num() ? 2.f : 0.f, 0.f))
			[
				SNew(SBorder)
				.BorderImage(WidgetAnimMachineStyle::RoundedBrush(3.f))
				.BorderBackgroundColor_Lambda([this, LayerIndex]()
				{
					if (ShownLayer != LayerIndex)
					{
						return FSlateColor(FLinearColor::Transparent);
					}
					return FSlateColor(IsShowingLayer(LayerIndex) ? MachineColors::InputRowSelected : FLinearColor(1.f, 1.f, 1.f, 0.12f));
				})
				.Padding(FMargin(10.f, 3.f))
				.ToolTip(WidgetAnimMachineStyle::MakeToolTip(FText::Format(MachineTips::LayerTabTooltip(), FText::FromName(Layer.Name), Blend, FText::AsNumber(Layer.Weight))))
				.OnMouseButtonDown_Lambda([this, LayerIndex](const FGeometry&, const FPointerEvent& MouseEvent)
				{
					if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
					{
						return FReply::Unhandled();
					}
					if (ShownLayer != LayerIndex)
					{
						ShowLayer(LayerIndex);
					}
					else
					{
						ShowLayerDetails();
					}
					return FReply::Handled();
				})
				.OnMouseDoubleClick_Lambda([this, LayerIndex](const FGeometry&, const FPointerEvent&)
				{
					if (LayerNameTexts.IsValidIndex(LayerIndex) && LayerNameTexts[LayerIndex].IsValid())
					{
						LayerNameTexts[LayerIndex]->EnterEditingMode();
					}
					return FReply::Handled();
				})
				// Right-click: Rename and Delete, as a state's, an input's and a track's menus have them.
				.OnMouseButtonUp_Lambda([this, LayerIndex](const FGeometry&, const FPointerEvent& MouseEvent)
				{
					if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton || !IsMachineEditable())
					{
						return FReply::Unhandled();
					}
					FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
					Menu.BeginSection(TEXT("Layer"), Machine && Machine->Layers.IsValidIndex(LayerIndex) ? FText::FromName(Machine->Layers[LayerIndex].Name) : FText::GetEmpty());
					Menu.AddMenuEntry(
						LOCTEXT("RenameLayerMenu", "Rename"),
						LOCTEXT("RenameLayerMenuTip", "Renames the layer - conditions on its states, in the other layers, follow."),
						FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GenericCommands.Rename")),
						FUIAction(FExecuteAction::CreateLambda([this, LayerIndex]()
						{
							if (LayerNameTexts.IsValidIndex(LayerIndex) && LayerNameTexts[LayerIndex].IsValid())
							{
								LayerNameTexts[LayerIndex]->EnterEditingMode();
							}
						})));
					Menu.AddMenuEntry(
						LOCTEXT("DeleteLayerMenu", "Delete"),
						LOCTEXT("DeleteLayerMenuTip", "Deletes the layer and its states. A machine keeps one layer at least."),
						FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GenericCommands.Delete")),
						FUIAction(
							FExecuteAction::CreateLambda([this, LayerIndex]() { WidgetAnimMachineEdits::RemoveLayer(Machine, LayerIndex); }),
							FCanExecuteAction::CreateLambda([this]() { return Machine && Machine->Layers.Num() > 1; })));
					Menu.EndSection();
					const FWidgetPath Path = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
					FSlateApplication::Get().PushMenu(AsShared(), Path, Menu.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SAssignNew(NameText, SInlineEditableTextBlock)
						.Text(FText::FromName(Layer.Name))
						.IsReadOnly(bMachineLocked)
						.OnTextCommitted_Lambda([this, LayerIndex](const FText& NewText, ETextCommit::Type)
						{
							WidgetAnimMachineEdits::RenameLayer(Machine, LayerIndex, NewText.ToString());
						})
					]
					// A layer limited to some targets (Affects) or channels (Channels): a small badge, so it reads at a glance.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(5.f, 0.f, 0.f, 0.f))
					[
						SNew(SBorder)
						.BorderImage(WidgetAnimMachineStyle::RoundedBrush(3.f))
						.BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.14f))
						.Padding(FMargin(4.f, 0.f))
						.Visibility(Layer.MaskMode == EWidgetAnimMachineLayerMask::All && !Layer.HasChannelMask() ? EVisibility::Collapsed : EVisibility::Visible)
						.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::LayerMaskBadgeTooltip()))
						[
							SNew(STextBlock)
							.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
							.Text(MakeLayerMaskBadgeText(Layer))
						]
					]
				]
			];
		LayerNameTexts.Add(NameText);
	}

	// Add a layer; remove the one shown.
	LayerTabs->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::AddLayerTooltip()))
			.IsEnabled_Lambda([this]() { return IsMachineEditable(); })
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
				// The Add Track button's green.
				MakeFixedIcon(FAppStyle::GetBrush(TEXT("Icons.Plus")), FSlateColor(FStyleColors::AccentGreen))
			]
		];
	LayerTabs->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::RemoveLayerTooltip()))
			.IsEnabled_Lambda([this]() { return IsMachineEditable() && Machine->Layers.Num() > 1; })
			.OnClicked_Lambda([this]()
			{
				WidgetAnimMachineEdits::RemoveLayer(Machine, ShownLayer);
				return FReply::Handled();
			})
			[
				// Delete Track's red.
				MakeFixedIcon(FAppStyle::GetBrush(TEXT("Icons.Delete")), FSlateColor(NameTBD::Colors::WidgetAnimEditor::MenuDeleteIconTint))
			]
		];
}

void SWidgetAnimStateMachineEditor::RenameInputInPanel(FName Input)
{
	if (const TSharedPtr<SInlineEditableTextBlock>* Text = InputNameTexts.Find(Input))
	{
		if (Text->IsValid())
		{
			(*Text)->EnterEditingMode();
		}
	}
}

void SWidgetAnimStateMachineEditor::RemoveInputFromPanel(FName Input)
{
	const bool bShown = IsShowingInput(Input);
	WidgetAnimMachineEdits::RemoveInput(Machine, Input);
	if (bShown)
	{
		ShowLayerDetails();
	}
}

void SWidgetAnimStateMachineEditor::OpenInputMenu(FName Input, const FPointerEvent& MouseEvent)
{
	// Locked: nothing to rename, delete or move.
	if (!IsMachineEditable())
	{
		return;
	}
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	Menu.BeginSection(TEXT("Input"), FText::FromName(Input));
	Menu.AddMenuEntry(
		LOCTEXT("RenameInputMenu", "Rename"),
		LOCTEXT("RenameInputMenuTip", "Renames the input - the conditions that test it follow."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GenericCommands.Rename")),
		FUIAction(FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::RenameInputInPanel, Input)));
	Menu.AddMenuEntry(
		LOCTEXT("RemoveInputMenu", "Delete"),
		MachineTips::RemoveInputTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GenericCommands.Delete")),
		FUIAction(FExecuteAction::CreateSP(this, &SWidgetAnimStateMachineEditor::RemoveInputFromPanel, Input)));

	// Move To Category: the machine's categories, none, or a new one - named in the Details' Category, or by double-clicking
	// its heading.
	Menu.AddSubMenu(
		LOCTEXT("MoveInputToCategory", "Move To Category"),
		MachineTips::MoveInputToCategoryTooltip(),
		FNewMenuDelegate::CreateLambda([this, Input](FMenuBuilder& SubMenu)
		{
			const FWidgetAnimMachineInput* Found = Machine ? Machine->FindInput(Input) : nullptr;
			const FString Current = Found ? Found->Category : FString();
			auto AddCategory = [this, Input, &SubMenu, &Current](const FString& Category, const FText& Label)
			{
				SubMenu.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this, Input, Category]() { WidgetAnimMachineEdits::SetInputCategory(Machine, Input, Category); }),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([Current, Category]() { return Current == Category; })),
					NAME_None, EUserInterfaceActionType::RadioButton);
			};
			AddCategory(FString(), LOCTEXT("NoInputCategory", "(none)"));
			for (const FString& Category : Machine ? Machine->GetInputCategoryOptions() : TArray<FString>())
			{
				AddCategory(Category, FText::FromString(Category));
			}
			SubMenu.AddMenuSeparator();
			SubMenu.AddMenuEntry(LOCTEXT("NewInputCategory", "New Category"), MachineTips::NewInputCategoryTooltip(), FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Plus")),
				FUIAction(FExecuteAction::CreateLambda([this, Input]()
				{
					if (!Machine)
					{
						return;
					}
					// A fresh name, unused by any category.
					const TArray<FString> Existing = Machine->GetInputCategoryOptions();
					FString Wanted = TEXT("New Category");
					for (int32 Suffix = 2; Existing.Contains(Wanted); ++Suffix)
					{
						Wanted = FString::Printf(TEXT("New Category %d"), Suffix);
					}
					WidgetAnimMachineEdits::SetInputCategory(Machine, Input, Wanted);
				})));
		}));
	Menu.EndSection();

	const FWidgetPath Path = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
	FSlateApplication::Get().PushMenu(AsShared(), Path, Menu.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

namespace
{
	/**
	 * The Inputs panel's Press And Hold button: the editor's own button, as Fire is - held while the mouse is down on it - its
	 * label tinted toward the hold color so it reads as a hold, not a Fire. As it is held, a dimmed fill of the hold color runs
	 * across inside its edge, under the label, which stays legible.
	 */
	class SWidgetAnimHoldInputButton : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetAnimHoldInputButton) {}
			SLATE_ARGUMENT(TFunction<float()>, GetProgress)
			SLATE_ARGUMENT(TFunction<void(bool)>, OnHeld)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ProgressOf = InArgs._GetProgress;
			HeldHandler = InArgs._OnHeld;
			const FLinearColor HoldColor = MachineColors::InputHold;
			// Dimmed toward the button's own dark, so white text over it stays readable.
			const FLinearColor FillColor = FMath::Lerp(FLinearColor(0.02f, 0.02f, 0.02f, 1.f), HoldColor, 0.42f);
			const FLinearColor LabelColor = FMath::Lerp(FLinearColor::White, HoldColor, 0.35f);
			auto Progress = [this]() { return FMath::Clamp(ProgressOf ? ProgressOf() : 0.f, 0.f, 1.f); };
			ChildSlot
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SButton)
					.ContentPadding(FMargin(0.f))
					.OnPressed_Lambda([this]() { SetHeld(true); })
					.OnReleased_Lambda([this]() { SetHeld(false); })
					[
						SNew(SBox)
						.MinDesiredWidth(56.f)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding(FMargin(8.f, 1.f))
						[
							// Sizes the button as Fire's label sizes Fire; the label drawn over the fill is the one seen.
							SNew(STextBlock)
							.TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("ButtonText"))
							.ColorAndOpacity(FLinearColor::Transparent)
							.Text(LOCTEXT("HoldButton", "Hold"))
						]
					]
				]
				// The fill, inside the button's edge, as wide as the hold has got.
				+ SOverlay::Slot()
				.Padding(FMargin(2.f))
				[
					SNew(SHorizontalBox)
					.Visibility(EVisibility::HitTestInvisible)
					+ SHorizontalBox::Slot()
					.FillWidth(TAttribute<float>::CreateLambda(Progress))
					[
						SNew(SBorder)
						.BorderImage(WidgetAnimMachineStyle::RoundedBrush(3.f))
						.BorderBackgroundColor(FSlateColor(FillColor))
						.Padding(FMargin(0.f))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(TAttribute<float>::CreateLambda([Progress]() { return 1.f - Progress(); }))
					[
						SNullWidget::NullWidget
					]
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Visibility(EVisibility::HitTestInvisible)
					// The same text as Fire's - SButton's own ButtonText - only tinted.
					.TextStyle(&FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("ButtonText"))
					.ColorAndOpacity(FSlateColor(LabelColor))
					.Text(LOCTEXT("HoldButton", "Hold"))
				]
			];
		}

	private:
		void SetHeld(bool bNow)
		{
			if (bHeld != bNow)
			{
				bHeld = bNow;
				if (HeldHandler)
				{
					HeldHandler(bNow);
				}
			}
		}

		TFunction<float()> ProgressOf;
		TFunction<void(bool)> HeldHandler;
		bool bHeld = false;
	};

	/** An input type from the type picker's kind - see GetTypeKind for the other way. */
	EWidgetAnimMachineInputType InputTypeOf(WidgetAnimTypePill::EKind Kind)
	{
		switch (Kind)
		{
		case WidgetAnimTypePill::EKind::Float:    return EWidgetAnimMachineInputType::Number;
		case WidgetAnimTypePill::EKind::Integer:  return EWidgetAnimMachineInputType::Integer;
		case WidgetAnimTypePill::EKind::Trigger:  return EWidgetAnimMachineInputType::Trigger;
		case WidgetAnimTypePill::EKind::Hold:     return EWidgetAnimMachineInputType::Hold;
		case WidgetAnimTypePill::EKind::Choice:   return EWidgetAnimMachineInputType::Choice;
		case WidgetAnimTypePill::EKind::Vector2D: return EWidgetAnimMachineInputType::Vector2D;
		default:                                  return EWidgetAnimMachineInputType::Bool;
		}
	}
}

void SWidgetAnimStateMachineEditor::AddInputCategoryHeader(const TSharedRef<SVerticalBox>& Rows, const FString& Category)
{
	// A strip edge to edge, set off from the rows above it.
	Rows->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 8.f, 0.f, 2.f))
		[
			SNew(SBorder)
			.BorderImage(GetRowBrush())
			.BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.05f))
			.Padding(FMargin(4.f, 2.f, 8.f, 2.f))
			[
				SNew(SHorizontalBox)
				// Folded or unfolded, as a Blueprint's variable categories are.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(1.f))
					.OnClicked_Lambda([this, Category]()
					{
						if (CollapsedInputCategories.Remove(Category) == 0)
						{
							CollapsedInputCategories.Add(Category);
						}
						RebuildInputs();
						return FReply::Handled();
					})
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image(FAppStyle::GetBrush(CollapsedInputCategories.Contains(Category) ? TEXT("TreeArrow_Collapsed") : TEXT("TreeArrow_Expanded")))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(SInlineEditableTextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
					.ColorAndOpacity(FStyleColors::White)
					.IsReadOnly(bMachineLocked)
					.Text(FText::FromString(Category))
					.ToolTipText(MachineTips::InputCategoryTooltip())
					.OnTextCommitted_Lambda([this, Category](const FText& NewText, ETextCommit::Type)
					{
						const FString Renamed = NewText.ToString().TrimStartAndEnd();
						if (CollapsedInputCategories.Remove(Category) > 0 && !Renamed.IsEmpty())
						{
							CollapsedInputCategories.Add(Renamed);
						}
						WidgetAnimMachineEdits::RenameInputCategory(Machine, Category, Renamed);
					})
				]
			]
		];
}

void SWidgetAnimStateMachineEditor::RebuildInputs()
{
	InputsBox->ClearChildren();
	InputNameTexts.Reset();
	if (!Machine)
	{
		return;
	}

	// A section as the My Blueprint panel's: its heading on the header strip, its rows on the panel's recessed color below it.
	auto AddSection = [this](const FText& Title, const FText& ToolTip, TSharedRef<SWidget> Extra) -> TSharedRef<SVerticalBox>
	{
		const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		InputsBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
			[
				MakePanelSection(Title, ToolTip, Extra, Rows)
			];
		return Rows;
	};

	// The machine's inputs, with + to add one: a type picker as the Blueprint editor's variable types are picked.
	const TSharedRef<SWidget> AddInputButton = WidgetAnimTypePill::MakeTypePickerButton(
		{ WidgetAnimTypePill::EKind::Bool, WidgetAnimTypePill::EKind::Float, WidgetAnimTypePill::EKind::Integer, WidgetAnimTypePill::EKind::Trigger,
			WidgetAnimTypePill::EKind::Hold, WidgetAnimTypePill::EKind::Choice, WidgetAnimTypePill::EKind::Vector2D },
		[this](WidgetAnimTypePill::EKind Kind)
		{
			const FName Added = WidgetAnimMachineEdits::AddInput(Machine, InputTypeOf(Kind));
			if (!Added.IsNone())
			{
				ShowInputDetails(Added);
			}
		},
		[](WidgetAnimTypePill::EKind Kind)
		{
			const UEnum* Types = StaticEnum<EWidgetAnimMachineInputType>();
			return Types->GetToolTipTextByIndex(Types->GetIndexByValue(static_cast<int64>(InputTypeOf(Kind))));
		},
		MachineTips::AddInputTooltip(),
		TAttribute<bool>::CreateLambda([this]() { return IsMachineEditable(); }),
		/*bAsAddButton=*/true);
	const TSharedRef<SVerticalBox> InputRows = AddSection(LOCTEXT("InputsHeader", "Inputs"), MachineTips::InputsPanelTooltip(), AddInputButton);

	if (Machine->Inputs.Num() == 0)
	{
		InputRows->AddSlot()
			.AutoHeight()
			.Padding(RowPadding)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(MachineTips::NoInputsNote())
			];
	}

	// One row: the name, its type's pill, its value - unread inputs (nothing in the machine reads them) dimmed, and listed last.
	auto AddInputRow = [this, InputRows](const FWidgetAnimMachineInput& Input)
	{
		const FName Name = Input.Name;
		const EWidgetAnimMachineInputType Type = Input.Type;
		const bool bRead = Machine->UsesInput(Name);

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
				.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::FireTooltip()))
				.OnClicked_Lambda([this, Name]() { FireInput(Name); return FReply::Handled(); });
			break;
		case EWidgetAnimMachineInputType::Integer:
			Control = SNew(SBox)
				.WidthOverride(80.f)
				[
					SNew(SSpinBox<int32>)
					.Value_Lambda([this, Name]() { return FMath::RoundToInt32(GetShownInputValue(Name)); })
					.OnValueChanged_Lambda([this, Name](int32 Value) { SetInputValue(Name, static_cast<float>(Value)); })
				];
			break;
		case EWidgetAnimMachineInputType::Choice:
			// Its options, by name.
			Control = SNew(SComboButton)
				.OnGetMenuContent_Lambda([this, Name]()
				{
					FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
					const FWidgetAnimMachineInput* Definition = Machine ? Machine->FindInput(Name) : nullptr;
					const TArray<FName> Options = Definition ? Definition->Options : TArray<FName>();
					for (int32 Index = 0; Index < Options.Num(); ++Index)
					{
						Menu.AddMenuEntry(FText::FromName(Options[Index]), FText::GetEmpty(), FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([this, Name, Index]() { SetInputValue(Name, static_cast<float>(Index)); }),
								FCanExecuteAction(),
								FIsActionChecked::CreateLambda([this, Name, Index]() { return FMath::RoundToInt32(GetShownInputValue(Name)) == Index; })),
							NAME_None, EUserInterfaceActionType::RadioButton);
					}
					if (Options.Num() == 0)
					{
						Menu.AddMenuEntry(LOCTEXT("NoChoiceOptions", "No options"), MachineTips::ChoiceNoOptionsTooltip(), FSlateIcon(),
							FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
					}
					return Menu.MakeWidget();
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([this, Name]()
					{
						const FWidgetAnimMachineInput* Definition = Machine ? Machine->FindInput(Name) : nullptr;
						const int32 Index = FMath::RoundToInt32(GetShownInputValue(Name));
						return Definition && Definition->Options.IsValidIndex(Index) ? FText::FromName(Definition->Options[Index]) : LOCTEXT("NoChoice", "-");
					})
				];
			break;
		case EWidgetAnimMachineInputType::Vector2D:
			// X and Y.
			Control = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(0.f, 0.f, 2.f, 0.f))
				[
					SNew(SBox)
					.WidthOverride(52.f)
					[
						SNew(SSpinBox<float>)
						.ToolTipText(LOCTEXT("VectorX", "X"))
						.Delta(0.01f)
						.Value_Lambda([this, Name]() { return static_cast<float>(GetShownVector(Name).X); })
						.OnValueChanged_Lambda([this, Name](float Value) { SetVectorValue(Name, FVector2D(Value, GetShownVector(Name).Y)); })
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(52.f)
					[
						SNew(SSpinBox<float>)
						.ToolTipText(LOCTEXT("VectorY", "Y (negative is up)"))
						.Delta(0.01f)
						.Value_Lambda([this, Name]() { return static_cast<float>(GetShownVector(Name).Y); })
						.OnValueChanged_Lambda([this, Name](float Value) { SetVectorValue(Name, FVector2D(GetShownVector(Name).X, Value)); })
					]
				];
			break;
		case EWidgetAnimMachineInputType::Hold:
			// Held while the mouse is down on it - let go early and the hold doesn't complete - filling as it is held.
			Control = SNew(SWidgetAnimHoldInputButton)
				.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::HoldButtonTooltip()))
				.GetProgress([this, Name]() { return GetShownHoldProgress(Name); })
				.OnHeld([this, Name](bool bHeld) { SetInputValue(Name, bHeld ? 1.f : 0.f); });
			break;
		default:
			Control = SNew(SCheckBox)
				.IsChecked_Lambda([this, Name]() { return GetShownInputValue(Name) > 0.5f ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Name](ECheckBoxState State) { SetInputValue(Name, State == ECheckBoxState::Checked ? 1.f : 0.f); });
			break;
		}

		// Its tooltip: what it is for, and how to work it.
		const FText Description = Input.Description.IsEmpty() ? GetTypeLabel(Type) : FText::FromString(Input.Description);
		const FText RowTip = FText::Format(MachineTips::InputRowTooltip(), FText::FromName(Name), Description);

		TSharedPtr<SInlineEditableTextBlock> NameText;
		InputRows->AddSlot()
			.AutoHeight()
			[
				// Click: its settings in the Details. Double-click: rename it. Right-click: its menu. Selected, edge to edge.
				SNew(SBorder)
				.BorderImage(GetRowBrush())
				.BorderBackgroundColor_Lambda([this, Name]() { return FSlateColor(IsShowingInput(Name) ? MachineColors::InputRowSelected : FLinearColor::Transparent); })
				.Padding(RowPadding)
				.ToolTip(WidgetAnimMachineStyle::MakeToolTip(RowTip))
				.OnMouseButtonDown_Lambda([this, Name](const FGeometry&, const FPointerEvent& MouseEvent)
				{
					if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
					{
						return FReply::Unhandled();
					}
					ShowInputDetails(Name);
					return FReply::Handled();
				})
				.OnMouseDoubleClick_Lambda([this, Name](const FGeometry&, const FPointerEvent&)
				{
					RenameInputInPanel(Name);
					return FReply::Handled();
				})
				.OnMouseButtonUp_Lambda([this, Name](const FGeometry&, const FPointerEvent& MouseEvent)
				{
					// Right-click: what can be done to it - rename, remove - as a Blueprint variable's menu has it.
					if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton)
					{
						return FReply::Unhandled();
					}
					ShowInputDetails(Name);
					OpenInputMenu(Name, MouseEvent);
					return FReply::Handled();
				})
				[
					// As a Blueprint's variables are listed: the name, then its type in a column of its own - a pill in the
					// type's pin color and the type's name - then its value, so every row's name and type line up.
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
					[
						SAssignNew(NameText, SInlineEditableTextBlock)
						.Text(FText::FromName(Name))
						.IsReadOnly(bMachineLocked)
						.ColorAndOpacity(bRead ? FSlateColor::UseForeground() : FSlateColor(MachineColors::BuiltInUnused))
						.OnVerifyTextChanged_Lambda([this, Name](const FText& NewText, FText& OutError)
						{
							OutError = WidgetAnimMachineEdits::ValidateInputName(Machine, Name, NewText.ToString());
							return OutError.IsEmpty();
						})
						.OnTextCommitted_Lambda([this, Name](const FText& NewText, ETextCommit::Type)
						{
							const FString Trimmed = NewText.ToString().TrimStartAndEnd();
							if (Trimmed.IsEmpty() || FName(*Trimmed) == Name)
							{
								return;
							}
							// The Details follow it to its new name.
							const bool bShown = IsShowingInput(Name);
							if (bShown)
							{
								InputProxy->InputName = FName(*Trimmed);
							}
							const FName Renamed = WidgetAnimMachineEdits::RenameInput(Machine, Name, Trimmed);
							if (const float* Value = InputValues.Find(Name))
							{
								InputValues.Add(Renamed, *Value);
							}
							if (bShown)
							{
								ShowInputDetails(Renamed);
							}
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(WidgetAnimTypePill::TypeColumnWidth)
						[
							WidgetAnimTypePill::MakePill(GetTypeKind(Type), /*bMuted=*/!bRead)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						// Read-only while nothing in the machine reads it - setting it would do nothing - and it says so.
						SNew(SBox)
						.WidthOverride(WidgetAnimTypePill::ValueColumnWidth)
						.HAlign(HAlign_Right)
						.IsEnabled(bRead)
						.ToolTip(bRead ? nullptr : WidgetAnimMachineStyle::MakeToolTip(MachineTips::UnreadInputValueTooltip()))
						[
							Control
						]
					]
				]
			];
		InputNameTexts.Add(Name, NameText);
	};

	// Uncategorized first, then each category (as a Blueprint's variables are) - in each, the inputs the machine reads first,
	// the unread after them, else in the machine's order.
	TArray<FString> Categories;
	Categories.Add(FString());
	for (const FString& Category : Machine->GetInputCategoryOptions())
	{
		Categories.Add(Category);
	}
	for (const FString& Category : Categories)
	{
		TArray<const FWidgetAnimMachineInput*> InCategory;
		for (const FWidgetAnimMachineInput& Input : Machine->Inputs)
		{
			if (Input.Category.TrimStartAndEnd() == Category)
			{
				InCategory.Add(&Input);
			}
		}
		if (InCategory.Num() == 0)
		{
			continue;
		}
		Algo::StableSort(InCategory, [this](const FWidgetAnimMachineInput* Left, const FWidgetAnimMachineInput* Right)
		{
			return Machine->UsesInput(Left->Name) && !Machine->UsesInput(Right->Name);
		});

		if (!Category.IsEmpty())
		{
			AddInputCategoryHeader(InputRows, Category);
			if (CollapsedInputCategories.Contains(Category))
			{
				continue;
			}
		}
		for (const FWidgetAnimMachineInput* Input : InCategory)
		{
			AddInputRow(*Input);
		}
	}

	// The widget's own, read off it at runtime - set here to try the machine in the designer. One the machine doesn't read is
	// grayed and italic, and can't be ticked: ticking it would do nothing.
	const TSharedRef<SVerticalBox> WidgetRows = AddSection(LOCTEXT("WidgetHeader", "Widget Inputs"), MachineTips::BuiltInPanelTooltip(), SNullWidget::NullWidget);
	// Those the machine reads first, the rest after them.
	TArray<FName> BuiltIns = WidgetAnimMachineInputs::GetBuiltIns();
	Algo::StableSort(BuiltIns, [this](FName Left, FName Right) { return Machine->UsesInput(Left) && !Machine->UsesInput(Right); });
	for (const FName BuiltIn : BuiltIns)
	{
		const bool bUsed = Machine->UsesInput(BuiltIn);
		if (!bUsed && BuiltInValues.FindRef(BuiltIn))
		{
			SetBuiltInValue(BuiltIn, false);
		}

		const FText Tip = bUsed
			? WidgetAnimMachineInputs::GetBuiltInDescription(BuiltIn)
			: FText::Format(MachineTips::BuiltInUnusedTooltip(), FText::FromName(BuiltIn), WidgetAnimMachineInputs::GetBuiltInDescription(BuiltIn));
		// Clicked is a pulse: fired, not ticked.
		TSharedRef<SWidget> BuiltInControl = SNullWidget::NullWidget;
		if (BuiltIn == WidgetAnimMachineInputs::Clicked)
		{
			BuiltInControl = SNew(SButton)
				.Text(LOCTEXT("FireClicked", "Fire"))
				.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::FireTooltip()))
				.IsEnabled_Lambda([this, bUsed]() { return bUsed && !IsDebuggingPlay(); })
				.OnClicked_Lambda([this, BuiltIn]() { FireBuiltIn(BuiltIn); return FReply::Handled(); });
		}
		else
		{
			BuiltInControl = SNew(SCheckBox)
				.IsEnabled_Lambda([this, bUsed]() { return bUsed && !IsDebuggingPlay(); })
				.IsChecked_Lambda([this, BuiltIn, bUsed]() { return bUsed && GetShownBuiltInValue(BuiltIn) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, BuiltIn, bUsed](ECheckBoxState State) { SetBuiltInValue(BuiltIn, bUsed && State == ECheckBoxState::Checked); });
		}

		WidgetRows->AddSlot()
			.AutoHeight()
			.Padding(RowPadding)
			[
				SNew(SHorizontalBox)
				.ToolTip(WidgetAnimMachineStyle::MakeToolTip(Tip))
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromName(BuiltIn))
					.Font(bUsed ? FCoreStyle::GetDefaultFontStyle("Regular", 10) : FCoreStyle::GetDefaultFontStyle("Italic", 10))
					.ColorAndOpacity(bUsed ? FSlateColor::UseForeground() : FSlateColor(MachineColors::BuiltInUnused))
				]
				// In the same columns as the machine's own inputs: Bool, and Clicked a Trigger.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(WidgetAnimTypePill::TypeColumnWidth)
					[
						WidgetAnimTypePill::MakePill(BuiltIn == WidgetAnimMachineInputs::Clicked ? WidgetAnimTypePill::EKind::Trigger : WidgetAnimTypePill::EKind::Bool, /*bMuted=*/!bUsed)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(WidgetAnimTypePill::ValueColumnWidth)
					.HAlign(HAlign_Right)
					[
						BuiltInControl
					]
				]
			];
	}
}

void SWidgetAnimStateMachineEditor::RebuildPreviewPanel()
{
	PreviewBox->ClearChildren();

	// Only where there is a designer to preview in.
	if (!Machine || !WidgetBlueprint.IsValid())
	{
		return;
	}

	auto MakeSourceToggle = [this](EPreviewSource Source, const FText& Label, const FText& ToolTip)
	{
		return SNew(SCheckBox)
			.Style(&GetPreviewSourceToggleStyle())
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(ToolTip))
			.IsChecked_Lambda([this, Source]() { return PreviewSource == Source ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Source](ECheckBoxState)
			{
				if (PreviewSource != Source)
				{
					PreviewSource = Source;
					StopPreviews();
					PreviewSignature.Reset();
					bPanelsDirty = true;
				}
				bPreviewSourceChosen = true;
			})
			[
				SNew(SBox)
				.Padding(FMargin(6.f, 1.f))
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
					.Text(Label)
				]
			];
	};

	// A section as the inputs' are: Preview on the header strip with the two toggles, its rows below.
	const TSharedRef<SVerticalBox> PreviewRows = SNew(SVerticalBox);
	PreviewBox->AddSlot()
		.AutoHeight()
		[
			MakePanelSection(LOCTEXT("PreviewHeader", "Preview"), MachineTips::PreviewPanelTooltip(),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeSourceToggle(EPreviewSource::Targets, LOCTEXT("PreviewTargetsToggle", "Targets"), MachineTips::PreviewTargetsSourceTooltip())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(2.f, 0.f, 0.f, 0.f))
				[
					MakeSourceToggle(EPreviewSource::Components, LOCTEXT("PreviewComponentsToggle", "Components"), MachineTips::PreviewComponentsSourceTooltip())
				],
				SNew(SBox)
				.Padding(FMargin(RowPadding.Left, 0.f, RowPadding.Right, 0.f))
				[
					PreviewRows
				])
		];

	if (PreviewSource == EPreviewSource::Components)
	{
		PreviewRows->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 0.f, 0.f, 4.f))
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(MachineTips::PreviewComponentsNote())
			];
		return;
	}

	// The widget it plays on, then one for each target.
	PreviewRows->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 1.f))
		[
			MakeTargetRow(HostPickKey, LOCTEXT("PlaysOnLabel", "Plays On"), MachineTips::PreviewHostTooltip())
		];

	TArray<FName> TargetNames;
	Machine->GetTargetNames(TargetNames);
	for (const FName TargetName : TargetNames)
	{
		PreviewRows->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 1.f))
			[
				MakeTargetRow(TargetName, FText::FromName(TargetName), FText::Format(MachineTips::PreviewTargetTooltip(), FText::FromName(TargetName)))
			];
	}
	if (TargetNames.Num() == 0)
	{
		PreviewRows->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 2.f))
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(MachineTips::NoPreviewTargetsNote())
			];
	}

	// While a pick waits for its click, say where to click.
	PreviewRows->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 3.f))
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Font(IDetailLayoutBuilder::GetDetailFontItalic())
			.ColorAndOpacity(FSlateColor(NameTBD::Colors::AnimationOrange_Normal))
			.Text(MachineTips::PickingTargetNote())
			.Visibility_Lambda([this]() { return IsPickingTarget() ? EVisibility::Visible : EVisibility::Collapsed; })
		];
}

TSharedRef<SWidget> SWidgetAnimStateMachineEditor::MakeTargetRow(FName Target, const FText& Label, const FText& ToolTip)
{
	const bool bHost = Target == HostPickKey;
	return SNew(SHorizontalBox)

		// The name takes the room: the picker beside it is kept to a set width, so "Layer Group 1" reads whole.
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
		[
			SNew(SBox)
			.MinDesiredWidth(72.f)
			[
				SNew(STextBlock)
				.Text(Label)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ToolTip(WidgetAnimMachineStyle::MakeToolTip(ToolTip))
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(140.f)
			[
			MakeSearchablePicker(
				[this]() { return GetTargetOptions(); },
				TAttribute<FText>::CreateLambda([this, Target, bHost]()
				{
					const FName Path = bHost ? GetPreviewHostPath() : GetPreviewTargetPath(Target);
					return FText::FromString(WidgetAnimTargetOption::FromPath(Path));
				}),
				[this, Target, bHost](const FString& Picked)
				{
					const FName Path = WidgetAnimTargetOption::ToPath(Picked);
					if (bHost)
					{
						SetPreviewHostPath(Path);
					}
					else
					{
						SetPreviewTargetPath(Target, Path);
					}
				},
				ToolTip)
			]
		]

		// Or pick it by clicking the widget in the designer: lit while it waits for that click.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(3.f, 2.f))
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::PickTargetInDesignerTooltip()))
			.IsEnabled_Lambda([this]() { return GetDesignerPreview() != nullptr; })
			.OnClicked_Lambda([this, Target]()
			{
				if (PickingTarget.IsSet() && PickingTarget.GetValue() == Target)
				{
					CancelTargetPick();
				}
				else
				{
					BeginTargetPick(Target);
				}
				return FReply::Handled();
			})
			[
				MakeFixedIcon(
					FindEditorBrush({ TEXT("Icons.EyeDropper"), TEXT("ColorPicker.EyeDropper"), TEXT("Icons.Picker"), TEXT("Icons.Search") }),
					TAttribute<FSlateColor>::CreateLambda([this, Target]()
					{
						return PickingTarget.IsSet() && PickingTarget.GetValue() == Target
							? FSlateColor(NameTBD::Colors::AnimationOrange_Normal)
							: FSlateColor::UseForeground();
					}))
			]
		];
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

	// What the Details show, as it is now - or, gone, the layer instead. An edit made from the Details themselves leaves them
	// as they are; one from anywhere else - an undo, a rename in the graph - refreshes them.
	const UObject* Shown = nullptr;
	if (DetailsView.IsValid())
	{
		const TArray<TWeakObjectPtr<UObject>> Selected = DetailsView->GetSelectedObjects();
		Shown = Selected.Num() == 1 ? Selected[0].Get() : nullptr;
	}
	auto Refresh = [this](auto* Proxy, const UScriptStruct* Struct, auto Member)
	{
		const auto Before = Proxy->*Member;
		if (!Proxy->Load())
		{
			// Gone: what was shown under it.
			LayerProxy->Machine = Machine;
			LayerProxy->LayerIndex = FMath::Clamp(ShownLayer, 0, FMath::Max(Machine->Layers.Num() - 1, 0));
			if (LayerProxy->Load())
			{
				DetailsView->SetObject(LayerProxy, /*bForceRefresh=*/true);
			}
			else
			{
				DetailsView->SetObject(nullptr);
			}
		}
		else if (!Struct->CompareScriptStruct(&Before, &(Proxy->*Member), PPF_None))
		{
			DetailsView->ForceRefresh();
		}
	};
	if (Shown && Shown == StateProxy.Get())
	{
		Refresh(StateProxy.Get(), FWidgetAnimMachineState::StaticStruct(), &UWidgetAnimMachineStateProxy::State);
	}
	else if (Shown && Shown == TransitionProxy.Get())
	{
		Refresh(TransitionProxy.Get(), FWidgetAnimMachineTransition::StaticStruct(), &UWidgetAnimMachineTransitionProxy::Transition);
	}
	else if (Shown && Shown == InputProxy.Get())
	{
		Refresh(InputProxy.Get(), FWidgetAnimMachineInput::StaticStruct(), &UWidgetAnimMachineInputProxy::Input);
	}
	else if (Shown && Shown == LayerProxy.Get())
	{
		Refresh(LayerProxy.Get(), FWidgetAnimMachineLayer::StaticStruct(), &UWidgetAnimMachineLayerProxy::Layer);
	}

	bPanelsDirty = true;
}

// =====================================================================================================
// Details
// =====================================================================================================

void SWidgetAnimStateMachineEditor::ShowLayerDetails()
{
	if (!Machine)
	{
		DetailsView->SetObject(nullptr);
		return;
	}
	LayerProxy->Machine = Machine;
	LayerProxy->LayerIndex = ShownLayer;
	if (LayerProxy->Load())
	{
		DetailsView->SetObject(LayerProxy, /*bForceRefresh=*/true);
	}
	else
	{
		DetailsView->SetObject(nullptr);
	}
}

void SWidgetAnimStateMachineEditor::ShowInputDetails(FName Input)
{
	if (!Machine)
	{
		return;
	}
	InputProxy->Machine = Machine;
	InputProxy->InputName = Input;
	if (InputProxy->Load())
	{
		// One thing picked at a time: the graph lets go of its selection.
		if (const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor())
		{
			TGuardValue<bool> Quiet(bRestoringSelection, true);
			Editor->ClearSelectionSet();
		}
		DetailsView->SetObject(InputProxy, /*bForceRefresh=*/true);
	}
}

bool SWidgetAnimStateMachineEditor::IsShowingInput(FName Input) const
{
	if (!DetailsView.IsValid() || !InputProxy || InputProxy->InputName != Input)
	{
		return false;
	}
	const TArray<TWeakObjectPtr<UObject>> Selected = DetailsView->GetSelectedObjects();
	return Selected.Num() == 1 && Selected[0].Get() == InputProxy.Get();
}

bool SWidgetAnimStateMachineEditor::IsShowingLayer(int32 LayerIndex) const
{
	if (!DetailsView.IsValid() || !LayerProxy || LayerProxy->LayerIndex != LayerIndex)
	{
		return false;
	}
	const TArray<TWeakObjectPtr<UObject>> Selected = DetailsView->GetSelectedObjects();
	return Selected.Num() == 1 && Selected[0].Get() == LayerProxy.Get();
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
			// Entry, Exit and Any State too: they show only their Node Color (the rest is a state's alone).
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
		else if (const UWidgetAnimMachineRerouteNode* RerouteNode = Cast<UWidgetAnimMachineRerouteNode>(Selected))
		{
			// A reroute point: the transition whose wire it is on.
			TransitionProxy->Machine = Machine;
			TransitionProxy->LayerIndex = ShownLayer;
			TransitionProxy->TransitionId = RerouteNode->TransitionId;
			if (TransitionProxy->Load())
			{
				DetailsView->SetObject(TransitionProxy, /*bForceRefresh=*/true);
				return;
			}
		}
		else if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Selected))
		{
			// The comment box itself: its color, font size, and how it moves. Written into the machine as it changes.
			DetailsView->SetObject(CommentNode, /*bForceRefresh=*/true);
			return;
		}
	}

	// Nothing picked, or Entry, Exit, Any State, or several: the layer's settings.
	ShowLayerDetails();
}

void SWidgetAnimStateMachineEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* Node)
{
	if (!Machine)
	{
		return;
	}
	if (const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Node))
	{
		if (StateNode->bIsGroup)
		{
			WidgetAnimMachineEdits::RenameGroup(Machine, ShownLayer, StateNode->StateId, NewText.ToString());
		}
		else
		{
			WidgetAnimMachineEdits::RenameState(Machine, ShownLayer, StateNode->StateId, NewText.ToString());
		}
	}
	else if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
	{
		// Written into the machine with the comment's other changes, once the mouse is let go (CommitGraphEdits).
		CommentNode->NodeComment = NewText.ToString();
	}
}

void SWidgetAnimStateMachineEditor::GetSelectedIds(TArray<FGuid>& OutStates, TArray<FGuid>& OutTransitions, TArray<FGuid>& OutComments) const
{
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (!Editor.IsValid())
	{
		return;
	}
	for (UObject* Selected : Editor->GetSelectedNodes())
	{
		if (const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Selected))
		{
			OutStates.Add(StateNode->StateId);
		}
		else if (const UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Selected))
		{
			OutTransitions.Add(TransitionNode->TransitionId);
		}
		else if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Selected))
		{
			OutComments.Add(CommentNode->NodeGuid);
		}
	}
}

void SWidgetAnimStateMachineEditor::DeleteSelected()
{
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (!Machine || !Editor.IsValid())
	{
		return;
	}

	TArray<FGuid> States;
	TArray<FGuid> Transitions;
	TArray<FGuid> Comments;
	TMap<FGuid, TArray<int32>> Reroutes;
	for (UObject* Selected : Editor->GetSelectedNodes())
	{
		if (const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Selected))
		{
			if (StateNode->CanUserDeleteNode())
			{
				States.Add(StateNode->StateId);
			}
		}
		else if (const UWidgetAnimMachineRerouteNode* RerouteNode = Cast<UWidgetAnimMachineRerouteNode>(Selected))
		{
			Reroutes.FindOrAdd(RerouteNode->TransitionId).AddUnique(RerouteNode->RerouteIndex);
		}
		else if (const UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Selected))
		{
			Transitions.Add(TransitionNode->TransitionId);
		}
		else if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Selected))
		{
			Comments.Add(CommentNode->NodeGuid);
		}
	}
	Editor->ClearSelectionSet();
	WidgetAnimMachineEdits::Delete(Machine, ShownLayer, States, Transitions, Comments, Reroutes);
	ShowLayerDetails();
}

bool SWidgetAnimStateMachineEditor::CanDeleteSelected() const
{
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (!Editor.IsValid() || !IsMachineEditable())
	{
		return false;
	}
	for (UObject* Selected : Editor->GetSelectedNodes())
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
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (!Editor.IsValid())
	{
		return;
	}
	const FGraphPanelSelectionSet Selected = Editor->GetSelectedNodes();
	if (Selected.Num() == 1)
	{
		if (const UEdGraphNode* Node = Cast<UEdGraphNode>(*Selected.CreateConstIterator()))
		{
			Editor->IsNodeTitleVisible(Node, /*bRequestRename=*/true);
		}
	}
}

bool SWidgetAnimStateMachineEditor::CanRenameSelected() const
{
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (!Editor.IsValid() || !IsMachineEditable())
	{
		return false;
	}
	const FGraphPanelSelectionSet Selected = Editor->GetSelectedNodes();
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

// -----------------------------------------------------------------------------------------------------
// Copy, paste, duplicate, comments
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimStateMachineEditor::CopySelected()
{
	TArray<FGuid> States;
	TArray<FGuid> Transitions;
	TArray<FGuid> Comments;
	GetSelectedIds(States, Transitions, Comments);
	const FString Text = WidgetAnimMachineEdits::CopyToText(Machine, ShownLayer, States, Comments);
	if (!Text.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*Text);
	}
}

bool SWidgetAnimStateMachineEditor::CanCopySelected() const
{
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (!Editor.IsValid())
	{
		return false;
	}
	for (UObject* Selected : Editor->GetSelectedNodes())
	{
		const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Selected);
		if ((StateNode && StateNode->Kind == EWidgetAnimMachineNodeKind::State) || Cast<UEdGraphNode_Comment>(Selected))
		{
			return true;
		}
	}
	return false;
}

void SWidgetAnimStateMachineEditor::CutSelected()
{
	CopySelected();
	DeleteSelected();
}

void SWidgetAnimStateMachineEditor::PasteHere()
{
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (!Machine || !Editor.IsValid())
	{
		return;
	}
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	const FVector2f Location = Editor->GetPasteLocation2f();
	PendingSelection = WidgetAnimMachineEdits::PasteText(Machine, ShownLayer, Text, FVector2D(Location));
}

bool SWidgetAnimStateMachineEditor::CanPaste() const
{
	if (!IsMachineEditable())
	{
		return false;
	}
	FString Text;
	FPlatformApplicationMisc::ClipboardPaste(Text);
	return Machine && WidgetAnimMachineEdits::CanPasteText(Text);
}

void SWidgetAnimStateMachineEditor::DuplicateSelected()
{
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (!Machine || !Editor.IsValid())
	{
		return;
	}
	TArray<FGuid> States;
	TArray<FGuid> Transitions;
	TArray<FGuid> Comments;
	GetSelectedIds(States, Transitions, Comments);
	const FString Text = WidgetAnimMachineEdits::CopyToText(Machine, ShownLayer, States, Comments);
	if (Text.IsEmpty())
	{
		return;
	}

	// A step down and right of what was duplicated - never on top of it - and the clipboard left alone.
	FSlateRect Bounds;
	const FVector2D At = Editor->GetBoundsForSelectedNodes(Bounds, 0.f) ? FVector2D(Bounds.Left + 40.f, Bounds.Top + 40.f) : GetViewCenter();
	PendingSelection = WidgetAnimMachineEdits::PasteText(Machine, ShownLayer, Text, At);
}

void SWidgetAnimStateMachineEditor::AddCommentAroundSelection()
{
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (!Machine || !Editor.IsValid())
	{
		return;
	}

	// Round the selection, with room for its title above; or, with nothing selected, where the mouse is.
	FSlateRect Bounds;
	FBox2D Box;
	if (Editor->GetBoundsForSelectedNodes(Bounds, 30.f))
	{
		Box = FBox2D(FVector2D(Bounds.Left, Bounds.Top - 30.f), FVector2D(Bounds.Right, Bounds.Bottom));
	}
	else
	{
		const FVector2D At(FVector2f(Editor->GetPasteLocation2f()));
		Box = FBox2D(At, At + FVector2D(400.0, 200.0));
	}
	const FGuid Added = WidgetAnimMachineEdits::AddComment(Machine, ShownLayer, Box, LOCTEXT("NewComment", "Comment").ToString());
	if (Added.IsValid())
	{
		PendingSelection = { Added };
	}
}

bool SWidgetAnimStateMachineEditor::HasSelectedTransitions() const
{
	TArray<FGuid> States;
	TArray<FGuid> Transitions;
	TArray<FGuid> Comments;
	GetSelectedIds(States, Transitions, Comments);
	return Transitions.Num() > 0;
}

void SWidgetAnimStateMachineEditor::SetSelectedTransitionsEnabled(bool bEnabled)
{
	TArray<FGuid> States;
	TArray<FGuid> Transitions;
	TArray<FGuid> Comments;
	GetSelectedIds(States, Transitions, Comments);
	WidgetAnimMachineEdits::SetTransitionsEnabled(Machine, ShownLayer, Transitions, bEnabled);
}

FVector2D SWidgetAnimStateMachineEditor::GetViewCenter() const
{
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (!Editor.IsValid())
	{
		return FVector2D::ZeroVector;
	}
	FVector2f ViewLocation;
	float Zoom = 1.f;
	Editor->GetViewLocation(ViewLocation, Zoom);
	const FVector2f Size = Editor->GetCachedGeometry().GetLocalSize();
	return FVector2D(ViewLocation + Size * 0.5f / FMath::Max(Zoom, 0.05f) - FVector2f(60.f, 20.f));
}

void SWidgetAnimStateMachineEditor::AddStateAtViewCenter(FName Preset)
{
	UWidgetAnimMachineGraph* Graph = GetShownGraph();
	if (!Graph)
	{
		return;
	}
	const FGuid Added = UWidgetAnimMachineGraphSchema::AddPresetState(Graph, Preset, GetViewCenter(), nullptr);
	if (Added.IsValid())
	{
		PendingSelection = { Added };
	}
}

TSharedRef<SWidget> SWidgetAnimStateMachineEditor::MakeAddStateMenu()
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	Menu.AddMenuEntry(
		LOCTEXT("AddPlainState", "State"),
		MachineTips::AddStateActionTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { AddStateAtViewCenter(NAME_None); })));

	Menu.BeginSection(TEXT("CommonStates"), LOCTEXT("CommonStatesSection", "Common States"));
	const FWidgetAnimMachineLayer* Layer = Machine && Machine->Layers.IsValidIndex(ShownLayer) ? &Machine->Layers[ShownLayer] : nullptr;
	for (const TPair<FName, FName>& Preset : UWidgetAnimMachineGraphSchema::GetPresetStates())
	{
		const FName PresetName = Preset.Key;
		const bool bExists = Layer && Layer->FindStateIndexByName(PresetName) != INDEX_NONE;
		const FText ToolTip = bExists
			? FText::Format(MachineTips::PresetStateExistsTooltip(), FText::FromName(PresetName))
			: UWidgetAnimMachineGraphSchema::GetPresetTooltip(Preset);
		Menu.AddMenuEntry(
			FText::FromName(PresetName),
			ToolTip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, PresetName]() { AddStateAtViewCenter(PresetName); }),
				FCanExecuteAction::CreateLambda([bExists]() { return !bExists; })));
	}
	Menu.EndSection();
	return Menu.MakeWidget();
}

// -----------------------------------------------------------------------------------------------------
// Lining states up
// -----------------------------------------------------------------------------------------------------

namespace
{
	/** A state node of the graph and its box - where it is, and where it is being moved to. */
	struct FArrangeState
	{
		UWidgetAnimMachineGraphNode* Node = nullptr;
		FBox2f Box;
		FBox2f Original;
	};

	/** Every state node of the graph with its box, and which of them are selected. */
	void GatherArrangeStates(const TSharedPtr<SGraphEditor>& Editor, const UWidgetAnimMachineGraph* Graph, TArray<FArrangeState>& OutAll, TArray<int32>& OutSelected)
	{
		if (!Editor.IsValid() || !Graph)
		{
			return;
		}
		const FGraphPanelSelectionSet& Selection = Editor->GetSelectedNodes();
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Node);
			FBox2f Box;
			if (!StateNode || !GetNodeBox(Editor, StateNode, Box))
			{
				continue;
			}
			FArrangeState& Entry = OutAll.AddDefaulted_GetRef();
			Entry.Node = StateNode;
			Entry.Box = Box;
			Entry.Original = Box;
			if (Selection.Contains(StateNode))
			{
				OutSelected.Add(OutAll.Num() - 1);
			}
		}
	}

	/**
	 * Lines Point up with Neighbour - a reroute point, or a state's middle - on whichever axis they already nearly share: the
	 * same height when they lie side by side, the same X when one is above the other. Each axis once: bInOutX / bInOutY say
	 * it is done, so a second neighbour only lines it up the other way.
	 */
	void LevelRoutePoint(FVector2f& Point, const FVector2f& Neighbour, bool& bInOutX, bool& bInOutY)
	{
		const FVector2f Delta = Point - Neighbour;
		if (FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y))
		{
			if (!bInOutY)
			{
				Point.Y = Neighbour.Y;
				bInOutY = true;
			}
		}
		else if (!bInOutX)
		{
			Point.X = Neighbour.X;
			bInOutX = true;
		}
	}

	/** Whether Box would land on any state but Except. */
	bool LandsOnOther(const TArray<FArrangeState>& All, int32 Except, const FBox2f& Box)
	{
		for (int32 Index = 0; Index < All.Num(); ++Index)
		{
			if (Index != Except && BoxesOverlap(Box, All[Index].Box, ArrangeMargin * 0.5f))
			{
				return true;
			}
		}
		return false;
	}
}

void SWidgetAnimStateMachineEditor::StraightenSelected()
{
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	UWidgetAnimMachineGraph* Graph = GetShownGraph();
	const FWidgetAnimMachineLayer* Layer = Graph ? Graph->GetLayer() : nullptr;
	if (!Machine || !Layer)
	{
		return;
	}

	// All of it one undo step: the moves, the transition nodes put back, the reroute points kept, the wires straightened.
	const FScopedTransaction Transaction(LOCTEXT("StraightenConnections", "Straighten Connections"));

	// Selected transition nodes that were dragged somewhere of their own go back onto the middle of their wires.
	if (Editor.IsValid())
	{
		TMap<FGuid, TOptional<FVector2D>> Reset;
		for (UObject* SelectedNode : Editor->GetSelectedNodes())
		{
			UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(SelectedNode);
			if (TransitionNode && TransitionNode->bPlaced)
			{
				TransitionNode->bPlaced = false;
				TransitionNode->PlacedOffset = FVector2f::ZeroVector;
				Reset.Add(TransitionNode->TransitionId, TOptional<FVector2D>());
			}
		}
		WidgetAnimMachineEdits::PlaceTransitions(Machine, ShownLayer, Reset);

		// Selected reroute points are lined up with the points either side of them on their wire - the reroute point before
		// or after, or the middle of the state the wire leaves or goes into - so the straight lines between run straight across
		// or straight up and down, out of the middle of a state's side. Level with the one before it first, across or above or
		// below whichever way it already lies; then, the other way, with the one after it - a corner of the route. First to
		// last along each wire, so a run of them lines up.
		TArray<UWidgetAnimMachineRerouteNode*> Reroutes;
		for (UObject* SelectedNode : Editor->GetSelectedNodes())
		{
			UWidgetAnimMachineRerouteNode* RerouteNode = Cast<UWidgetAnimMachineRerouteNode>(SelectedNode);
			if (RerouteNode && RerouteNode->TransitionNode && RerouteNode->TransitionNode->RerouteOffsets.IsValidIndex(RerouteNode->RerouteIndex))
			{
				Reroutes.Add(RerouteNode);
			}
		}
		Reroutes.Sort([](const UWidgetAnimMachineRerouteNode& A, const UWidgetAnimMachineRerouteNode& B) { return A.RerouteIndex < B.RerouteIndex; });

		TMap<FGuid, TArray<FVector2D>> Leveled;
		for (UWidgetAnimMachineRerouteNode* RerouteNode : Reroutes)
		{
			UWidgetAnimMachineTransitionNode* TransitionNode = RerouteNode->TransitionNode;
			FGuid FirstId;
			FGuid SecondId;
			UWidgetAnimMachineGraph::GetPairOrder(TransitionNode, FirstId, SecondId);
			FBox2f FirstBox;
			FBox2f SecondBox;
			FBox2f FromBox;
			FBox2f ToBox;
			if (!GetNodeBox(Editor, Graph->FindStateNode(FirstId), FirstBox) || !GetNodeBox(Editor, Graph->FindStateNode(SecondId), SecondBox)
				|| !GetNodeBox(Editor, Graph->FindStateNode(TransitionNode->FromId), FromBox) || !GetNodeBox(Editor, Graph->FindStateNode(TransitionNode->ToId), ToBox))
			{
				continue;
			}
			const FVector2f Anchor = WidgetAnimMachineWires::GetPlacementAnchor(FirstBox, SecondBox);
			TArray<FVector2f>& Offsets = TransitionNode->RerouteOffsets;
			const int32 Index = RerouteNode->RerouteIndex;
			FVector2f Point = Anchor + Offsets[Index];
			bool bLeveledX = false;
			bool bLeveledY = false;
			if (Index > 0)
			{
				LevelRoutePoint(Point, Anchor + Offsets[Index - 1], bLeveledX, bLeveledY);
			}
			else
			{
				LevelRoutePoint(Point, FromBox.GetCenter(), bLeveledX, bLeveledY);
			}
			if (Index < Offsets.Num() - 1)
			{
				LevelRoutePoint(Point, Anchor + Offsets[Index + 1], bLeveledX, bLeveledY);
			}
			else
			{
				LevelRoutePoint(Point, ToBox.GetCenter(), bLeveledX, bLeveledY);
			}
			Offsets[Index] = Point - Anchor;

			TArray<FVector2D>& Points = Leveled.FindOrAdd(TransitionNode->TransitionId);
			Points.Reset();
			for (const FVector2f& Offset : Offsets)
			{
				Points.Add(FVector2D(Offset));
			}
		}
		WidgetAnimMachineEdits::MoveReroutes(Machine, ShownLayer, Leveled);
	}

	TArray<FArrangeState> All;
	TArray<int32> Selected;
	GatherArrangeStates(Editor, Graph, All, Selected);
	if (Selected.Num() == 0)
	{
		return;
	}

	// A transition's end as the graph shows it - a collapsed group's box for a state in it - as an index into All.
	auto IndexOf = [&All, Graph](const FGuid& StateId)
	{
		const FGuid Shown = Graph->GetShownNodeId(StateId);
		return Shown.IsValid() ? All.IndexOfByPredicate([&Shown](const FArrangeState& Entry) { return Entry.Node->StateId == Shown; }) : INDEX_NONE;
	};

	// One state alone is lined up on the state it shares the most transitions with, which stays put.
	TSet<int32> Fixed;
	const int32 Lone = Selected.Num() == 1 ? Selected[0] : INDEX_NONE;
	if (Lone != INDEX_NONE)
	{
		TMap<int32, int32> Shared;
		for (const FWidgetAnimMachineTransition& Transition : Layer->Transitions)
		{
			// A wire routed through reroute points doesn't run between the two: its state lines up with its points instead (below).
			if (Transition.EditorReroutePoints.Num() > 0)
			{
				continue;
			}
			const int32 From = IndexOf(Transition.From);
			const int32 To = IndexOf(Transition.To);
			const int32 OtherIndex = From == To ? INDEX_NONE : (From == Lone ? To : (To == Lone ? From : INDEX_NONE));
			if (OtherIndex != INDEX_NONE)
			{
				Shared.FindOrAdd(OtherIndex)++;
			}
		}
		int32 Best = INDEX_NONE;
		int32 BestCount = 0;
		for (const TPair<int32, int32>& Entry : Shared)
		{
			if (Entry.Value > BestCount)
			{
				Best = Entry.Key;
				BestCount = Entry.Value;
			}
		}
		if (Best != INDEX_NONE)
		{
			Selected.Insert(Best, 0);
			Fixed.Add(Best);
		}
	}

	// The wires between the selected states, each pair once.
	TMap<int32, TArray<int32>> Links;
	for (const FWidgetAnimMachineTransition& Transition : Layer->Transitions)
	{
		const int32 From = IndexOf(Transition.From);
		const int32 To = IndexOf(Transition.To);
		if (From == INDEX_NONE || To == INDEX_NONE || From == To || !Selected.Contains(From) || !Selected.Contains(To) || Transition.EditorReroutePoints.Num() > 0)
		{
			continue;
		}
		Links.FindOrAdd(From).AddUnique(To);
		Links.FindOrAdd(To).AddUnique(From);
	}

	// A transition node dragged somewhere of its own, on a wire between two of the states being lined up, bends that wire
	// through it whatever the states do - so it goes back onto the middle of its wire, and the wire runs straight with them.
	{
		TMap<FGuid, TOptional<FVector2D>> Unplaced;
		for (const FWidgetAnimMachineTransition& Transition : Layer->Transitions)
		{
			const int32 From = IndexOf(Transition.From);
			const int32 To = IndexOf(Transition.To);
			if (Transition.bEditorNodePlaced && Transition.EditorReroutePoints.Num() == 0 && From != To && Selected.Contains(From) && Selected.Contains(To))
			{
				Unplaced.Add(Transition.Id, TOptional<FVector2D>());
				if (UWidgetAnimMachineTransitionNode* TransitionNode = Graph->FindTransitionNode(Transition.Id))
				{
					TransitionNode->bPlaced = false;
					TransitionNode->PlacedOffset = FVector2f::ZeroVector;
				}
			}
		}
		WidgetAnimMachineEdits::PlaceTransitions(Machine, ShownLayer, Unplaced);
	}

	// Outward from the best-connected state, each state put level with the one it was reached from - across from it, or above
	// or below it, whichever way it already lies - so their wire runs straight. A state that would land on another is moved
	// further along that way instead, and left where it is if there is no room. One that lies on a slant from it stays where
	// it is: their wire is drawn dead straight, corner to corner, below - moving it onto the 45-degree line would pull it out
	// of line with everything else it is level with.
	TArray<int32> Seeds = Selected;
	if (Fixed.Num() == 0)
	{
		Algo::StableSort(Seeds, [&Links, &All](int32 A, int32 B)
		{
			const int32 LinksA = Links.Contains(A) ? Links[A].Num() : 0;
			const int32 LinksB = Links.Contains(B) ? Links[B].Num() : 0;
			return LinksA != LinksB ? LinksA > LinksB : All[A].Box.Min.X < All[B].Box.Min.X;
		});
	}

	TSet<int32> Placed;
	for (const int32 Seed : Seeds)
	{
		if (Placed.Contains(Seed))
		{
			continue;
		}
		Placed.Add(Seed);
		TArray<int32> Queue = { Seed };
		while (Queue.Num() > 0)
		{
			const int32 From = Queue[0];
			Queue.RemoveAt(0);
			const TArray<int32>* Neighbors = Links.Find(From);
			if (!Neighbors)
			{
				continue;
			}
			for (const int32 To : *Neighbors)
			{
				if (Placed.Contains(To))
				{
					continue;
				}
				Placed.Add(To);
				Queue.Add(To);
				if (Fixed.Contains(To))
				{
					continue;
				}

				// On a slant (between 25 and 65 degrees, corner to corner): left where it is - see above. Nearly side by side,
				// or nearly one above the other: level with it, across or above or below.
				if (WidgetAnimMachineWires::IsSlanted(All[From].Box, All[To].Box))
				{
					continue;
				}
				const FVector2f FromCenter = All[From].Box.GetCenter();
				const FVector2f ToCenter = All[To].Box.GetCenter();
				const FVector2f Delta = ToCenter - FromCenter;
				const bool bAcross = FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y);
				const FVector2f Level = bAcross ? FVector2f(0.f, FromCenter.Y - ToCenter.Y) : FVector2f(FromCenter.X - ToCenter.X, 0.f);
				FBox2f Candidate = ShiftBox(All[To].Box, Level);

				if (LandsOnOther(All, To, Candidate))
				{
					// Further along the way it lies, a node's length at a time.
					const FVector2f Away = bAcross ? FVector2f(Delta.X >= 0.f ? 1.f : -1.f, 0.f) : FVector2f(0.f, Delta.Y >= 0.f ? 1.f : -1.f);
					const float Step = (bAcross ? Candidate.GetSize().X : Candidate.GetSize().Y) + ArrangeMargin;
					bool bFound = false;
					for (int32 Try = 1; Try <= 6 && !bFound; ++Try)
					{
						const FBox2f Pushed = ShiftBox(Candidate, Away * (Step * Try * 0.5f));
						if (!LandsOnOther(All, To, Pushed))
						{
							Candidate = Pushed;
							bFound = true;
						}
					}
					if (!bFound)
					{
						continue;
					}
				}
				All[To].Box = Candidate;
			}
		}
	}

	// A selected state with no straight wire to the others but a routed one: its middle lined up with the reroute point next
	// to it on that wire - across from it or above or below it, whichever way it lies - so the wire leaves the middle of its
	// side straight. Not if it would land on another state.
	auto AnchorOf = [&All, &IndexOf, Graph](const FWidgetAnimMachineTransition& Transition, bool bOriginal, FVector2f& OutAnchor)
	{
		// Paired as the graph pairs its ends (see UWidgetAnimMachineGraph::GetPairOrder): by the nodes they are shown as.
		const bool bFromFirst = Graph->GetShownNodeId(Transition.From) < Graph->GetShownNodeId(Transition.To);
		const int32 First = IndexOf(bFromFirst ? Transition.From : Transition.To);
		const int32 Second = IndexOf(bFromFirst ? Transition.To : Transition.From);
		if (First == INDEX_NONE || Second == INDEX_NONE || First == Second)
		{
			return false;
		}
		OutAnchor = bOriginal
			? WidgetAnimMachineWires::GetPlacementAnchor(All[First].Original, All[Second].Original)
			: WidgetAnimMachineWires::GetPlacementAnchor(All[First].Box, All[Second].Box);
		return true;
	};
	for (const int32 StateIndex : Selected)
	{
		if (Fixed.Contains(StateIndex) || Links.Contains(StateIndex))
		{
			continue;
		}
		const FWidgetAnimMachineTransition* Routed = Layer->Transitions.FindByPredicate([&IndexOf, StateIndex](const FWidgetAnimMachineTransition& Transition)
		{
			const int32 From = IndexOf(Transition.From);
			const int32 To = IndexOf(Transition.To);
			return Transition.EditorReroutePoints.Num() > 0 && (From == StateIndex || To == StateIndex) && From != To;
		});
		const UWidgetAnimMachineTransitionNode* RoutedNode = Routed ? Graph->FindTransitionNode(Routed->Id) : nullptr;
		FVector2f RouteAnchor;
		if (!RoutedNode || RoutedNode->RerouteOffsets.Num() == 0 || !AnchorOf(*Routed, /*bOriginal=*/true, RouteAnchor))
		{
			continue;
		}
		const FVector2f Knot = RouteAnchor + (IndexOf(Routed->From) == StateIndex ? RoutedNode->RerouteOffsets[0] : RoutedNode->RerouteOffsets.Last());
		FVector2f Center = All[StateIndex].Box.GetCenter();
		bool bLeveledX = false;
		bool bLeveledY = false;
		const FVector2f Before = Center;
		LevelRoutePoint(Center, Knot, bLeveledX, bLeveledY);
		if (Center.Equals(Before, 0.25f))
		{
			continue;
		}
		const FBox2f Candidate = ShiftBox(All[StateIndex].Box, Center - Before);
		if (!LandsOnOther(All, StateIndex, Candidate))
		{
			All[StateIndex].Box = Candidate;
		}
	}

	// Reroute points stay where they are while their states move: they are kept from halfway between the two states, so each
	// is moved back by what that halfway point moved - the routes Q has just made straight stay straight.
	TMap<FGuid, TArray<FVector2D>> Kept;
	for (const FWidgetAnimMachineTransition& Transition : Layer->Transitions)
	{
		UWidgetAnimMachineTransitionNode* TransitionNode = Transition.EditorReroutePoints.Num() > 0 ? Graph->FindTransitionNode(Transition.Id) : nullptr;
		FVector2f Before;
		FVector2f After;
		if (!TransitionNode || !AnchorOf(Transition, /*bOriginal=*/true, Before) || !AnchorOf(Transition, /*bOriginal=*/false, After) || Before.Equals(After, 0.25f))
		{
			continue;
		}
		TArray<FVector2D>& Points = Kept.Add(Transition.Id);
		for (FVector2f& Offset : TransitionNode->RerouteOffsets)
		{
			Offset += Before - After;
			Points.Add(FVector2D(Offset));
		}
	}
	WidgetAnimMachineEdits::MoveReroutes(Machine, ShownLayer, Kept);

	// Plain wires between the states lined up - with one state selected, every one of its own - drawn dead straight where the
	// two lie on a slant, corner to corner at whatever angle, and curving as usual again where they now lie level (straight
	// anyway).
	{
		TArray<FGuid> MakeStraight;
		TArray<FGuid> MakeCurved;
		for (const FWidgetAnimMachineTransition& Transition : Layer->Transitions)
		{
			const int32 From = IndexOf(Transition.From);
			const int32 To = IndexOf(Transition.To);
			if (From == INDEX_NONE || To == INDEX_NONE || From == To || Transition.EditorReroutePoints.Num() > 0 || Transition.bEditorNodePlaced)
			{
				continue;
			}
			const bool bCovered = Lone != INDEX_NONE ? (From == Lone || To == Lone) : (Selected.Contains(From) && Selected.Contains(To));
			if (!bCovered)
			{
				continue;
			}
			const bool bSlanted = WidgetAnimMachineWires::IsSlanted(All[From].Box, All[To].Box);
			if (bSlanted != Transition.bEditorStraight)
			{
				(bSlanted ? MakeStraight : MakeCurved).Add(Transition.Id);
				if (UWidgetAnimMachineTransitionNode* TransitionNode = Graph->FindTransitionNode(Transition.Id))
				{
					TransitionNode->bStraight = bSlanted;
				}
			}
		}
		WidgetAnimMachineEdits::SetTransitionsStraight(Machine, ShownLayer, MakeStraight, /*bStraight=*/true);
		WidgetAnimMachineEdits::SetTransitionsStraight(Machine, ShownLayer, MakeCurved, /*bStraight=*/false);
	}

	// Into the machine, and the graph.
	TMap<FGuid, FVector2D> Positions;
	for (const FArrangeState& Entry : All)
	{
		if (!Entry.Box.Min.Equals(Entry.Original.Min, 0.5f))
		{
			const FVector2f Move = Entry.Box.Min - Entry.Original.Min;
			Entry.Node->NodePosX += FMath::RoundToInt32(Move.X);
			Entry.Node->NodePosY += FMath::RoundToInt32(Move.Y);
			Positions.Add(Entry.Node->StateId, FVector2D(Entry.Node->NodePosX, Entry.Node->NodePosY));
		}
	}
	WidgetAnimMachineEdits::MoveStates(Machine, ShownLayer, Positions);
}

void SWidgetAnimStateMachineEditor::CommitGraphEdits()
{
	// Once the mouse is let go: a drag moves nodes every frame, and is one move.
	UWidgetAnimMachineGraph* Graph = GetShownGraph();
	if (!Machine || !Graph || FSlateApplication::Get().GetPressedMouseButtons().Num() > 0)
	{
		return;
	}
	const FWidgetAnimMachineLayer* Layer = Graph->GetLayer();
	if (!Layer)
	{
		return;
	}

	TMap<FGuid, FVector2D> Moved;
	TMap<FGuid, TOptional<FVector2D>> Placed;
	TMap<FGuid, TArray<FVector2D>> Rerouted;
	TArray<FWidgetAnimMachineComment> ChangedComments;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Node))
		{
			// A state, or collapsed states' box - its position is the group's (it was never written back, so a dragged box
			// went back where it was the next time the graph was made: a restart, an undo, any edit).
			const FVector2D Position(StateNode->NodePosX, StateNode->NodePosY);
			if (StateNode->bIsGroup)
			{
				const FWidgetAnimMachineStateGroup* Group = WidgetAnimMachineEdits::FindGroup(*Layer, StateNode->StateId);
				if (Group && !Position.Equals(Group->Position, 0.5))
				{
					Moved.Add(StateNode->StateId, Position);
				}
				continue;
			}
			const FWidgetAnimMachineState* State = Layer->FindState(StateNode->StateId);
			if (State && !Position.Equals(State->EditorPosition, 0.5))
			{
				Moved.Add(StateNode->StateId, Position);
			}
		}
		else if (const UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Node))
		{
			// A transition node dragged somewhere of its own - or put back on its wire.
			const int32 Index = Layer->FindTransitionIndex(TransitionNode->TransitionId);
			if (Index == INDEX_NONE)
			{
				continue;
			}
			const FWidgetAnimMachineTransition& Transition = Layer->Transitions[Index];
			const FVector2D Offset(TransitionNode->PlacedOffset);
			const bool bChanged = TransitionNode->bPlaced != Transition.bEditorNodePlaced
				|| (TransitionNode->bPlaced && !Offset.Equals(Transition.EditorNodeOffset, 0.5));
			if (bChanged)
			{
				Placed.Add(TransitionNode->TransitionId, TransitionNode->bPlaced ? TOptional<FVector2D>(Offset) : TOptional<FVector2D>());
			}

			// Its wire's reroute points, dragged. (Added or taken off, the machine has them already: the graph is made again.)
			if (TransitionNode->RerouteOffsets.Num() == Transition.EditorReroutePoints.Num())
			{
				TArray<FVector2D> Points;
				bool bMoved = false;
				for (int32 Point = 0; Point < TransitionNode->RerouteOffsets.Num(); ++Point)
				{
					Points.Add(FVector2D(TransitionNode->RerouteOffsets[Point]));
					bMoved |= !Points.Last().Equals(Transition.EditorReroutePoints[Point], 0.5);
				}
				if (bMoved)
				{
					Rerouted.Add(TransitionNode->TransitionId, MoveTemp(Points));
				}
			}
		}
		else if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
		{
			// A comment box as the graph has it - moved, resized, retitled, recolored in the Details.
			const FWidgetAnimMachineComment* Current = Layer->Comments.FindByPredicate([CommentNode](const FWidgetAnimMachineComment& Candidate) { return Candidate.Id == CommentNode->NodeGuid; });
			if (!Current)
			{
				continue;
			}
			FWidgetAnimMachineComment Now = *Current;
			Now.Position = FVector2D(CommentNode->NodePosX, CommentNode->NodePosY);
			Now.Size = FVector2D(CommentNode->NodeWidth, CommentNode->NodeHeight);
			Now.Text = CommentNode->NodeComment;
			Now.Color = CommentNode->CommentColor;
			Now.FontSize = CommentNode->FontSize;
			Now.bGroupMovement = CommentNode->MoveMode == ECommentBoxMode::GroupMovement;
			Now.bShowBubbleWhenZoomed = !!CommentNode->bCommentBubbleVisible_InDetailsPanel;
			Now.bColorBubble = !!CommentNode->bColorCommentBubble;
			if (!FWidgetAnimMachineComment::StaticStruct()->CompareScriptStruct(Current, &Now, PPF_None))
			{
				ChangedComments.Add(Now);
			}
		}
	}
	WidgetAnimMachineEdits::MoveStates(Machine, ShownLayer, Moved);
	WidgetAnimMachineEdits::PlaceTransitions(Machine, ShownLayer, Placed);
	WidgetAnimMachineEdits::MoveReroutes(Machine, ShownLayer, Rerouted);
	WidgetAnimMachineEdits::UpdateComments(Machine, ShownLayer, ChangedComments);
}

// =====================================================================================================
// Tick and keys
// =====================================================================================================

void SWidgetAnimStateMachineEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!Machine)
	{
		return;
	}

	TickPendingLetGo();
	TickJump();

	// The stripes on the state in play scroll on - unless the designer preview is paused.
	if (!IsPreviewPaused())
	{
		for (UWidgetAnimMachineGraph* Graph : Graphs)
		{
			if (Graph)
			{
				Graph->StripeClock += InDeltaTime;
			}
		}
	}

	// While something in the shown graph moves - a wire's dots, a fill - the editor is kept drawing every frame, so they run
	// smoothly rather than in steps whenever something else happens to redraw.
	if (!AnimateTimer.IsValid() && IsGraphAnimating())
	{
		AnimateTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SWidgetAnimStateMachineEditor::OnAnimateGraph));
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
		// Reroute points by their transition and place on its wire - and how many the wire had, so a point added or taken off
		// before it doesn't leave its neighbour picked instead.
		struct FSelectedReroute
		{
			FGuid TransitionId;
			int32 Index = 0;
			int32 Count = 0;
		};
		TArray<FSelectedReroute> SelectedReroutes;
		if (GraphEditor.IsValid())
		{
			for (UObject* Selected : GraphEditor->GetSelectedNodes())
			{
				if (const UWidgetAnimMachineRerouteNode* RerouteNode = Cast<UWidgetAnimMachineRerouteNode>(Selected))
				{
					const int32 Count = RerouteNode->TransitionNode ? RerouteNode->TransitionNode->RerouteOffsets.Num() : 0;
					SelectedReroutes.Add({ RerouteNode->TransitionId, RerouteNode->RerouteIndex, Count });
					continue;
				}
				if (const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Selected))
				{
					SelectedIds.Add(StateNode->StateId);
				}
				else if (const UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Selected))
				{
					SelectedIds.Add(TransitionNode->TransitionId);
				}
				else if (const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Selected))
				{
					SelectedIds.Add(CommentNode->NodeGuid);
				}
			}
			// Quietly: the Details keep showing what they show (OnMachineChanged has refreshed or replaced it already).
			TGuardValue<bool> Quiet(bRestoringSelection, true);
			GraphEditor->ClearSelectionSet();
		}

		// A comment box shown in the Details is one of the nodes about to go: found again by its id, below.
		FGuid ShownCommentId;
		if (DetailsView.IsValid())
		{
			const TArray<TWeakObjectPtr<UObject>> Shown = DetailsView->GetSelectedObjects();
			if (const UEdGraphNode_Comment* ShownComment = Shown.Num() == 1 ? Cast<UEdGraphNode_Comment>(Shown[0].Get()) : nullptr)
			{
				ShownCommentId = ShownComment->GetGraph() == Graph ? ShownComment->NodeGuid : FGuid();
			}
		}

		Graph->RebuildFromMachine();
		TGuardValue<bool> Quiet(bRestoringSelection, true);

		// What was just added, pasted or duplicated is what is selected now.
		const bool bTakePending = LayerIndex == ShownLayer && PendingSelection.Num() > 0;
		if (bTakePending)
		{
			SelectedIds.Reset();
			SelectedIds.Append(PendingSelection);
			PendingSelection.Reset();
		}

		if (GraphEditor.IsValid() && !bTakePending && SelectedReroutes.Num() > 0)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				const UWidgetAnimMachineRerouteNode* RerouteNode = Cast<UWidgetAnimMachineRerouteNode>(Node);
				const int32 Count = RerouteNode && RerouteNode->TransitionNode ? RerouteNode->TransitionNode->RerouteOffsets.Num() : 0;
				if (RerouteNode && SelectedReroutes.ContainsByPredicate([RerouteNode, Count](const FSelectedReroute& Was)
				{
					return Was.TransitionId == RerouteNode->TransitionId && Was.Index == RerouteNode->RerouteIndex && Was.Count == Count;
				}))
				{
					GraphEditor->SetNodeSelection(Node, true);
				}
			}
		}

		if (GraphEditor.IsValid() && SelectedIds.Num() > 0)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Node);
				const UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Node);
				const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node);
				const FGuid Id = StateNode ? StateNode->StateId : (TransitionNode ? TransitionNode->TransitionId : (CommentNode ? CommentNode->NodeGuid : FGuid()));
				if (Id.IsValid() && SelectedIds.Contains(Id))
				{
					GraphEditor->SetNodeSelection(Node, true);
				}
			}
		}

		if (ShownCommentId.IsValid())
		{
			if (UEdGraphNode_Comment* Again = Graph->FindCommentNode(ShownCommentId))
			{
				DetailsView->SetObject(Again, /*bForceRefresh=*/true);
			}
			else
			{
				ShowLayerDetails();
			}
		}

		// One new thing picked: its Details, as a click on it would show.
		if (bTakePending && GraphEditor.IsValid() && GraphEditor->GetSelectedNodes().Num() == 1)
		{
			TGuardValue<bool> Loud(bRestoringSelection, false);
			OnSelectionChanged(GraphEditor->GetSelectedNodes());
		}
	}

	// States just collapsed: their box, picked when the graph was made again, lined up with what it is wired to - as Q would -
	// a frame or two later, once it has been laid out and has a size.
	if (PendingStraightenFrames > 0 && PendingSelection.Num() == 0)
	{
		--PendingStraightenFrames;
		if (PendingStraightenFrames == 0 && IsMachineEditable())
		{
			StraightenSelected();
		}
	}

	if (bPanelsDirty)
	{
		bPanelsDirty = false;
		RebuildPreviewPanel();
		RebuildInputs();
		RebuildLayerTabs();
	}

	CommitGraphEdits();
	UpdatePreviews();
	UpdateTargetPick();
}

FReply SWidgetAnimStateMachineEditor::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Never while a name or a comment is being typed: the text box has the keys, for its own typing.
	const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	const bool bTyping = Focused.IsValid() && Focused->GetType().ToString().Contains(TEXT("EditableText"));

	// Ctrl / Cmd + Z, and Ctrl / Cmd + Y or Ctrl / Cmd + Shift + Z: the editor's undo and redo, anywhere in the editor - as the
	// Widget Animations panel has them. Not in the middle of an edit (a drag), which is still open.
	const FKey Key = InKeyEvent.GetKey();
	if (!bTyping && (Key == EKeys::Z || Key == EKeys::Y) && (InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown()) && !InKeyEvent.IsAltDown())
	{
		if (GEditor && !GEditor->IsTransactionActive())
		{
			if (Key == EKeys::Y || InKeyEvent.IsShiftDown())
			{
				GEditor->RedoTransaction();
			}
			else
			{
				GEditor->UndoTransaction();
			}
		}
		return FReply::Handled();
	}

	// Only while the graph has the keyboard.
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (Editor.IsValid() && PreviewCommands.IsValid() && (Editor->HasKeyboardFocus() || Editor->HasFocusedDescendants()))
	{
		if (!bTyping && PreviewCommands->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}
	}
	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

FReply SWidgetAnimStateMachineEditor::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// What a Blueprint graph's own keys do: F frames the selection, Home the whole graph.
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (Editor.IsValid() && !InKeyEvent.IsControlDown() && !InKeyEvent.IsAltDown() && !InKeyEvent.IsShiftDown())
	{
		if (InKeyEvent.GetKey() == EKeys::F)
		{
			Editor->ZoomToFit(/*bOnlySelection=*/Editor->GetSelectedNodes().Num() > 0);
			return FReply::Handled();
		}
		if (InKeyEvent.GetKey() == EKeys::Home)
		{
			Editor->ZoomToFit(/*bOnlySelection=*/false);
			return FReply::Handled();
		}
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

// =====================================================================================================
// Preview and debugging
// =====================================================================================================

UUserWidget* SWidgetAnimStateMachineEditor::GetDesignerPreview() const
{
	FWidgetBlueprintEditor* Editor = FindWidgetBlueprintEditor(WidgetBlueprint.Get());
	return Editor ? Editor->GetPreview() : nullptr;
}

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
		const double Now = FPlatformTime::Seconds();
		if (!bDebugPickedByHand && (LastDebugSearchTime < 0.0 || Now - LastDebugSearchTime >= 0.25))
		{
			LastDebugSearchTime = Now;
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

	// Else the first designer widget previewing it through its component.
	if (PreviewSource == EPreviewSource::Components)
	{
		for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
		{
			UWidgetAnimationComponent* Live = Weak.Get();
			if (Live && Live->IsDesignerStateMachinePreviewing())
			{
				return Live;
			}
		}
	}
	return nullptr;
}

const FWidgetAnimMachineInstance* SWidgetAnimStateMachineEditor::GetWatchedInstance() const
{
	if (!Machine)
	{
		return nullptr;
	}
	if (const UWidgetAnimationComponent* Watched = GetWatchedComponent())
	{
		return Watched->GetStateMachineInstances().FindByPredicate([this](const FWidgetAnimMachineInstance& Candidate)
		{
			return Candidate.bStarted && Candidate.Machine == Machine;
		});
	}
	if (!(GEditor && GEditor->PlayWorld) && PreviewSource == EPreviewSource::Targets && PreviewHost.IsValid() && PreviewHost->IsRunning())
	{
		const FWidgetAnimMachineInstance& Instance = PreviewHost->GetInstance();
		return Instance.bStarted && Instance.Machine == Machine ? &Instance : nullptr;
	}
	return nullptr;
}

void SWidgetAnimStateMachineEditor::JumpToState(int32 LayerIndex, const FGuid& State)
{
	if (!Machine || !Machine->Layers.IsValidIndex(LayerIndex))
	{
		return;
	}

	// Entry: back to the start, rather than a way there - nothing leads into Entry.
	const FWidgetAnimMachineState* Node = Machine->Layers[LayerIndex].FindState(State);
	if (Node && Node->Kind == EWidgetAnimMachineNodeKind::Entry)
	{
		RestartFromEntry();
		return;
	}

	// From where the watched widget's layer is now - or, with nothing watched, from where the layer starts.
	FWidgetAnimMachineLayerDebug Debug;
	const bool bWatched = GetLayerDebug(LayerIndex, Debug);
	TArray<WidgetAnimMachineJump::FStep> Steps;
	FText Problem;
	WidgetAnimMachineJump::Plan(*Machine, LayerIndex, bWatched ? Debug.CurrentState : FGuid(), State,
		InputValues, VectorValues, Steps, Problem);

	// No way there, or a way that may stop short: said either way.
	if (!Problem.IsEmpty())
	{
		FNotificationInfo Info(Problem);
		Info.ExpireDuration = 4.f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	JumpStep = INDEX_NONE;
	if (Steps.Num() == 0)
	{
		return;
	}
	JumpSteps = MoveTemp(Steps);
	JumpLayer = LayerIndex;
	if (!bWatched)
	{
		ApplyJumpPlan(JumpSteps.Last().Plan);
		return;
	}
	JumpStep = 0;
	JumpStepTime = FPlatformTime::Seconds();
	ApplyJumpPlan(JumpSteps[0].Plan);
}

void SWidgetAnimStateMachineEditor::RestartFromEntry()
{
	JumpStep = INDEX_NONE;
	JumpSteps.Reset();
	PendingLetGo.Reset();
	if (!Machine)
	{
		return;
	}

	// A widget playing in Play In Editor is the game's: it isn't started again from here.
	if (IsDebuggingPlay())
	{
		FNotificationInfo Info(LOCTEXT("NoEntryJumpInPlay", "Jump To Entry starts the designer preview again - a widget in Play In Editor isn't restarted from here."));
		Info.ExpireDuration = 4.f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	// Every input as it starts, every built-in let go - then started again, which pushes them onto the new preview.
	InputValues.Reset();
	VectorValues.Reset();
	TArray<FName> BuiltIns;
	BuiltInValues.GetKeys(BuiltIns);
	for (const FName BuiltIn : BuiltIns)
	{
		SetBuiltInValue(BuiltIn, false);
	}
	PreviewSignature.Reset();
}

void SWidgetAnimStateMachineEditor::TickPendingLetGo()
{
	// A few frames held, so the machine has run with it held: its next run sees it let go.
	constexpr double TapSeconds = 0.06;
	if (PendingLetGo.Num() == 0 || FPlatformTime::Seconds() - PendingLetGoTime < TapSeconds)
	{
		return;
	}
	const TArray<FName> LetGo = MoveTemp(PendingLetGo);
	PendingLetGo.Reset();
	for (const FName Input : LetGo)
	{
		SetInputValue(Input, 0.f);
	}
}

void SWidgetAnimStateMachineEditor::ApplyJumpPlan(const WidgetAnimMachineJump::FInputPlan& Plan)
{
	if (!Machine)
	{
		return;
	}

	// Values first, then the pulses - a trigger fired onto inputs already where the step needs them.
	for (const FWidgetAnimMachineInput& Input : Machine->Inputs)
	{
		if (Input.Type == EWidgetAnimMachineInputType::Vector2D)
		{
			if (const FVector2D* Vector = Plan.Vectors.Find(Input.Name))
			{
				SetVectorValue(Input.Name, *Vector);
			}
		}
		else if (Input.Type == EWidgetAnimMachineInputType::Hold && Plan.Tap.Contains(Input.Name))
		{
			// Is Let Go wants it let go: pressed now, let go a moment later (TickPendingLetGo).
			SetInputValue(Input.Name, 1.f);
			PendingLetGo.AddUnique(Input.Name);
			PendingLetGoTime = FPlatformTime::Seconds();
		}
		else if (Input.Type != EWidgetAnimMachineInputType::Trigger)
		{
			if (const float* Value = Plan.Values.Find(Input.Name))
			{
				SetInputValue(Input.Name, *Value);
			}
		}
	}
	for (const TPair<FName, bool>& BuiltIn : Plan.BuiltIns)
	{
		if (GetShownBuiltInValue(BuiltIn.Key) != BuiltIn.Value)
		{
			SetBuiltInValue(BuiltIn.Key, BuiltIn.Value);
		}
	}
	for (const FWidgetAnimMachineInput& Input : Machine->Inputs)
	{
		if (Input.Type == EWidgetAnimMachineInputType::Trigger && Plan.Fire.Contains(Input.Name))
		{
			FireInput(Input.Name);
		}
	}
	if (Plan.Fire.Contains(WidgetAnimMachineInputs::Clicked))
	{
		FireBuiltIn(WidgetAnimMachineInputs::Clicked);
	}
}

void SWidgetAnimStateMachineEditor::TickJump()
{
	if (JumpStep == INDEX_NONE || !JumpSteps.IsValidIndex(JumpStep))
	{
		JumpStep = INDEX_NONE;
		return;
	}

	// On once the layer is in the step's state and its transition has finished - or, if it never gets there (an Exit Time
	// longer than expected, a Press And Hold still filling), after a few seconds anyway.
	constexpr double GiveUpSeconds = 4.0;
	FWidgetAnimMachineLayerDebug Debug;
	const bool bWatched = GetLayerDebug(JumpLayer, Debug);
	const bool bArrived = bWatched && Debug.CurrentState == JumpSteps[JumpStep].To && !Debug.ActiveTransition.IsValid();
	const double Now = FPlatformTime::Seconds();
	if (!bArrived && Now - JumpStepTime < GiveUpSeconds)
	{
		return;
	}
	++JumpStep;
	if (!JumpSteps.IsValidIndex(JumpStep))
	{
		JumpStep = INDEX_NONE;
		return;
	}
	JumpStepTime = Now;
	ApplyJumpPlan(JumpSteps[JumpStep].Plan);
}

bool SWidgetAnimStateMachineEditor::IsDebuggingPlay() const
{
	const UWidgetAnimationComponent* Watched = GetWatchedComponent();
	const UWidget* Widget = Watched ? Watched->GetAnimatedWidget() : nullptr;
	return Widget && !Widget->IsDesignTime();
}

bool SWidgetAnimStateMachineEditor::FindActiveState(int32& OutLayer, FGuid& OutState) const
{
	OutLayer = INDEX_NONE;
	OutState.Invalidate();
	if (!Machine)
	{
		return false;
	}
	FWidgetAnimMachineLayerDebug Debug;
	if (GetLayerDebug(ShownLayer, Debug) && Debug.CurrentState.IsValid())
	{
		OutLayer = ShownLayer;
		OutState = Debug.CurrentState;
		return true;
	}
	for (int32 LayerIndex = 0; LayerIndex < Machine->Layers.Num(); ++LayerIndex)
	{
		if (GetLayerDebug(LayerIndex, Debug) && Debug.CurrentState.IsValid())
		{
			OutLayer = LayerIndex;
			OutState = Debug.CurrentState;
			return true;
		}
	}
	return false;
}

FText SWidgetAnimStateMachineEditor::GetJumpToActiveStateName() const
{
	int32 LayerIndex = INDEX_NONE;
	FGuid StateId;
	const FWidgetAnimMachineState* State = FindActiveState(LayerIndex, StateId) && Machine->Layers.IsValidIndex(LayerIndex)
		? Machine->Layers[LayerIndex].FindState(StateId)
		: nullptr;
	if (!State)
	{
		return FText::GetEmpty();
	}
	const FText Name = State->IsSpecial()
		? StaticEnum<EWidgetAnimMachineNodeKind>()->GetDisplayNameTextByValue(static_cast<int64>(State->Kind))
		: FText::FromName(State->Name);
	return FText::Format(LOCTEXT("JumpToActiveName", "({0})"), Name);
}

void SWidgetAnimStateMachineEditor::JumpToActiveState()
{
	int32 LayerIndex = INDEX_NONE;
	FGuid StateId;
	if (!FindActiveState(LayerIndex, StateId))
	{
		return;
	}
	if (LayerIndex != ShownLayer)
	{
		ShowLayer(LayerIndex);
	}
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	const UWidgetAnimMachineGraph* Graph = GetShownGraph();
	const UWidgetAnimMachineGraphNode* Node = Graph ? Graph->FindStateNode(StateId) : nullptr;
	if (Editor.IsValid() && Node)
	{
		// Brought into view, not selected: the Details keep showing what they show.
		Editor->JumpToNode(Node, /*bRequestRename=*/false, /*bSelectNode=*/false);
	}
}

bool SWidgetAnimStateMachineEditor::IsPreviewPaused() const
{
	// Only the designer preview pauses: a widget watched in Play In Editor runs as the game does.
	return bPreviewPaused && bPreviewInDesigner && !(GEditor && GEditor->PlayWorld) && !IsDebuggingPlay();
}

bool SWidgetAnimStateMachineEditor::IsGraphAnimating() const
{
	// A state in play: its stripes scroll for as long as it is.
	FWidgetAnimMachineLayerDebug Debug;
	if (GetLayerDebug(ShownLayer, Debug) && (Debug.CurrentState.IsValid() || Debug.ActiveTransition.IsValid() || Debug.bStatePlaying))
	{
		return true;
	}

	// A Press And Hold filling: its Hold button in the Inputs panel fills with it.
	if (Machine)
	{
		for (const FWidgetAnimMachineInput& Input : Machine->Inputs)
		{
			const float Progress = Input.Type == EWidgetAnimMachineInputType::Hold ? GetShownHoldProgress(Input.Name) : 0.f;
			if (Progress > 0.f && Progress < 1.f)
			{
				return true;
			}
		}
	}

	// A wire's dots fading out after its transition.
	const UWidgetAnimMachineGraph* Graph = Graphs.IsValidIndex(ShownLayer) ? Graphs[ShownLayer].Get() : nullptr;
	if (Graph)
	{
		const double Now = FPlatformTime::Seconds();
		for (const TPair<FGuid, double>& Active : Graph->LastActiveTimes)
		{
			if (Now - Active.Value < NameTBD::Colors::StateMachineEditor::WireLinger + 0.1)
			{
				return true;
			}
		}
	}
	return false;
}

EActiveTimerReturnType SWidgetAnimStateMachineEditor::OnAnimateGraph(double InCurrentTime, float InDeltaTime)
{
	if (IsGraphAnimating())
	{
		return EActiveTimerReturnType::Continue;
	}
	AnimateTimer.Reset();
	return EActiveTimerReturnType::Stop;
}

bool SWidgetAnimStateMachineEditor::HandleGraphWheel(const FPointerEvent& WheelEvent)
{
	// Ctrl: the graph editor's own full range. Out: the graph editor's own, which is free once past 1:1.
	if (WheelEvent.IsControlDown() || WheelEvent.GetWheelDelta() <= 0.f || !FSlateApplication::IsInitialized())
	{
		return false;
	}
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	if (!Editor.IsValid())
	{
		return false;
	}

	// Only over the graph itself - not a menu or window over it.
	FSlateApplication& Slate = FSlateApplication::Get();
	const FWidgetPath Path = Slate.LocateWindowUnderMouse(WheelEvent.GetScreenSpacePosition(), Slate.GetInteractiveTopLevelWindows());
	bool bOverGraph = false;
	for (int32 Index = Path.Widgets.Num() - 1; Index >= 0 && !bOverGraph; --Index)
	{
		bOverGraph = &Path.Widgets[Index].Widget.Get() == Editor.Get();
	}
	if (!bOverGraph)
	{
		return false;
	}

	FVector2f ViewLocation;
	float Zoom = 1.f;
	Editor->GetViewLocation(ViewLocation, Zoom);
	if (Zoom < 0.99f)
	{
		// Below 1:1 the graph editor zooms as it always does.
		return false;
	}

	// The graph editor's own steps past 1:1, as far as MaxGraphZoom. At the last, the wheel does nothing more.
	static const float Steps[] = { 1.25f, 1.375f, 1.5f };
	float Next = 0.f;
	for (const float Step : Steps)
	{
		if (Step > Zoom + 0.01f && Step <= MaxGraphZoom + 0.01f)
		{
			Next = Step;
			break;
		}
	}
	if (Next <= 0.f)
	{
		return true;
	}

	// Zoomed about the mouse: the point under it stays under it.
	const FVector2f Local = Editor->GetCachedGeometry().AbsoluteToLocal(FVector2f(WheelEvent.GetScreenSpacePosition()));
	const FVector2f Under = ViewLocation + Local / FMath::Max(Zoom, 0.05f);
	Editor->SetViewLocation(Under - Local / Next, Next);
	return true;
}

bool SWidgetAnimStateMachineEditor::HandleGraphDoubleClick(const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || MouseEvent.IsAltDown() || MouseEvent.IsControlDown() || MouseEvent.IsShiftDown()
		|| !Machine || !FSlateApplication::IsInitialized())
	{
		return false;
	}
	const TSharedPtr<SGraphEditor> Editor = GetShownGraphEditor();
	UWidgetAnimMachineGraph* Graph = GetShownGraph();
	if (!Editor.IsValid() || !Graph)
	{
		return false;
	}

	// Only on the graph itself - not a menu or window over it, and not a name or comment title (a double-click there renames).
	FSlateApplication& Slate = FSlateApplication::Get();
	const FWidgetPath Path = Slate.LocateWindowUnderMouse(MouseEvent.GetScreenSpacePosition(), Slate.GetInteractiveTopLevelWindows());
	bool bOverGraph = false;
	for (int32 Index = Path.Widgets.Num() - 1; Index >= 0 && !bOverGraph; --Index)
	{
		const SWidget& Widget = Path.Widgets[Index].Widget.Get();
		bOverGraph = &Widget == Editor.Get();
		if (!bOverGraph && Widget.GetType().ToString().Contains(TEXT("EditableText")))
		{
			return false;
		}
	}
	if (!bOverGraph)
	{
		return false;
	}

	// Where in the graph: as the wheel's zoom works it out.
	FVector2f ViewLocation;
	float Zoom = 1.f;
	Editor->GetViewLocation(ViewLocation, Zoom);
	Zoom = FMath::Max(Zoom, 0.05f);
	const FVector2f Local = Editor->GetCachedGeometry().AbsoluteToLocal(FVector2f(MouseEvent.GetScreenSpacePosition()));
	const FVector2f Point = ViewLocation + Local / Zoom;

	// On a node - a state, a transition node, a reroute point: that node's own double-click (a state opens its animation).
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		FBox2f Box;
		if (!Node || Node->IsA<UEdGraphNode_Comment>() || !GetNodeBox(Editor, Node, Box))
		{
			continue;
		}
		if (Box.ExpandBy(2.f).IsInside(Point))
		{
			return false;
		}
	}

	// The wire nearest the click, if it is close enough - a few pixels on screen, however far the graph is zoomed.
	const float Reach = (WidgetAnimMachineWires::WireHitDistance + NameTBD::Colors::StateMachineEditor::WireThickness * 0.5f * Zoom) / Zoom;
	const UWidgetAnimMachineTransitionNode* Hit = nullptr;
	FWidgetAnimMachineWire HitWire;
	int32 HitSegment = INDEX_NONE;
	FVector2f HitPoint = Point;
	FVector2f HitAnchor = FVector2f::ZeroVector;
	float HitDistance = Reach;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		const UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Node);
		if (!TransitionNode)
		{
			continue;
		}
		FGuid FirstId;
		FGuid SecondId;
		UWidgetAnimMachineGraph::GetPairOrder(TransitionNode, FirstId, SecondId);
		FBox2f FirstBox;
		FBox2f SecondBox;
		if (!GetNodeBox(Editor, Graph->FindStateNode(FirstId), FirstBox) || !GetNodeBox(Editor, Graph->FindStateNode(SecondId), SecondBox))
		{
			continue;
		}
		const FWidgetAnimMachineWire Wire = Graph->ComputeWire(TransitionNode, FirstBox, SecondBox);
		int32 Segment = INDEX_NONE;
		FVector2f Nearest = Point;
		const float Distance = Wire.GetDistance(Point, Segment, &Nearest);
		if (Segment != INDEX_NONE && Distance <= HitDistance)
		{
			Hit = TransitionNode;
			HitWire = Wire;
			HitSegment = Segment;
			HitPoint = Nearest;
			HitAnchor = WidgetAnimMachineWires::GetPlacementAnchor(FirstBox, SecondBox);
			HitDistance = Distance;
		}
	}
	if (!Hit || !HitWire.Segments.IsValidIndex(HitSegment))
	{
		return false;
	}

	// On the wire where it was clicked, between the points either side of it - kept, as the transition node's own place is,
	// from halfway between the two states' corners.
	WidgetAnimMachineEdits::AddReroute(Machine, ShownLayer, Hit->TransitionId, HitWire.Segments[HitSegment].ReroutesBefore, FVector2D(HitPoint - HitAnchor));
	return true;
}

bool SWidgetAnimStateMachineEditor::GetLayerDebug(int32 LayerIndex, FWidgetAnimMachineLayerDebug& OutDebug) const
{
	const FWidgetAnimMachineInstance* Instance = GetWatchedInstance();
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

uint32 SWidgetAnimStateMachineEditor::MakePreviewSignature() const
{
	// What a preview has to start again for: the inputs it was set up with, the layers it has, the animations it captured
	// its widgets for - and, on Preview Targets, the widgets it plays on. Durations, conditions and the rest are read live.
	// Hashed in one walk over the machine, every frame: written out as text, with every target's default found by walking
	// every animation again, it cost more the bigger the machine grew - most of all with many states.
	if (!Machine)
	{
		return 0;
	}

	// Each object by itself, its name and its package: what its path names.
	auto HashObject = [](uint32 InHash, const UObject* Object)
	{
		InHash = HashCombine(InHash, PointerHash(Object));
		InHash = HashCombine(InHash, GetTypeHash(Object->GetFName()));
		return HashCombine(InHash, GetTypeHash(Object->GetPackage()->GetFName()));
	};

	uint32 Signature = HashObject(0, Machine);
	for (const FWidgetAnimMachineInput& Input : Machine->Inputs)
	{
		Signature = HashCombine(Signature, GetTypeHash(Input.Name));
		Signature = HashCombine(Signature, GetTypeHash(static_cast<int32>(Input.Type)));
	}
	Signature = HashCombine(Signature, GetTypeHash(Machine->Layers.Num()));

	// Every animation it plays, each once, in the order the machine first names it - as GatherAnimations gathers them.
	TArray<const UWidgetAnimPresetAsset*, TInlineAllocator<32>> Animations;
	TSet<const UWidgetAnimPresetAsset*> Gathered;
	auto Gather = [&Animations, &Gathered](const UWidgetAnimPresetAsset* Animation)
	{
		bool bAlready = false;
		Gathered.Add(Animation, &bAlready);
		if (!bAlready)
		{
			Animations.Add(Animation);
		}
	};
	for (const FWidgetAnimMachineLayer& Layer : Machine->Layers)
	{
		for (const FWidgetAnimMachineState& State : Layer.States)
		{
			if (State.Animation && State.Kind == EWidgetAnimMachineNodeKind::State)
			{
				Gather(State.Animation);
			}
		}
		for (const FWidgetAnimMachineTransition& Transition : Layer.Transitions)
		{
			if (Transition.TransitionAnimation && Transition.Blend == EWidgetAnimMachineBlend::Animation)
			{
				Gather(Transition.TransitionAnimation);
			}
		}
	}
	for (const UWidgetAnimPresetAsset* Animation : Animations)
	{
		Signature = HashObject(Signature, Animation);
	}

	if (PreviewSource == EPreviewSource::Targets)
	{
		Signature = HashCombine(Signature, GetTypeHash(GetPreviewHostPath()));

		// Each target - the animations' groups, matched by name, as GetTargetNames has them - and where it previews: picked here,
		// else the Preview Target of the first group of that name (GetTargetDefault), read in the same walk.
		TArray<FName, TInlineAllocator<32>> TargetNames;
		TMap<FName, FName> TargetDefaults;
		for (const UWidgetAnimPresetAsset* Animation : Animations)
		{
			for (int32 GroupIndex = 0; GroupIndex < Animation->LayerGroups.Num(); ++GroupIndex)
			{
				const FName TargetName(*Animation->LayerGroups[GroupIndex].GetDisplayName(GroupIndex));
				if (!TargetDefaults.Contains(TargetName))
				{
					TargetDefaults.Add(TargetName, Animation->LayerGroups[GroupIndex].PreviewTarget);
					TargetNames.Add(TargetName);
				}
			}
		}
		for (const FName TargetName : TargetNames)
		{
			const FName* Picked = Machine->PreviewTargetPaths.Find(TargetName);
			Signature = HashCombine(Signature, GetTypeHash(TargetName));
			Signature = HashCombine(Signature, GetTypeHash(Picked ? *Picked : TargetDefaults.FindRef(TargetName)));
		}
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
	const bool bSearch = LastPreviewSearch < 0.0 || Now - LastPreviewSearch > 0.25;
	TArray<UWidgetAnimationComponent*> Found;
	if (bSearch)
	{
		LastPreviewSearch = Now;
		FindPreviewComponents(Found);
		PreviewRoot = GetDesignerPreview();

		// Where to preview, the first time: on the widgets that play it, if any do - else on Preview Targets.
		if (!bPreviewSourceChosen)
		{
			bPreviewSourceChosen = true;
			const EPreviewSource Chosen = Found.Num() > 0 ? EPreviewSource::Components : EPreviewSource::Targets;
			if (Chosen != PreviewSource)
			{
				PreviewSource = Chosen;
				bPanelsDirty = true;
			}
		}
	}

	const uint32 Signature = MakePreviewSignature();
	const bool bRestart = !PreviewSignature.IsSet() || Signature != PreviewSignature.GetValue();
	PreviewSignature = Signature;

	if (PreviewSource == EPreviewSource::Targets)
	{
		StopComponentPreviews();

		// On the designer's preview as it is now: started again when it is made again, or the machine changed.
		UUserWidget* Root = PreviewRoot.Get();
		const bool bRootChanged = PreviewHost->IsRunning() && PreviewHost->GetRoot() != Root;
		if (!Root)
		{
			PreviewHost->Stop();
			return;
		}
		if (bRestart || bRootChanged || !PreviewHost->IsRunning())
		{
			// Not every frame when it can't start - the preview not built yet: with the next search.
			if (!PreviewHost->IsRunning() && !bRestart && !bRootChanged && !bSearch)
			{
				return;
			}
			TMap<FName, FName> TargetPaths;
			TArray<FName> TargetNames;
			Machine->GetTargetNames(TargetNames);
			for (const FName TargetName : TargetNames)
			{
				TargetPaths.Add(TargetName, GetPreviewTargetPath(TargetName));
			}
			if (PreviewHost->Start(Machine, Root, GetPreviewHostPath(), TargetPaths))
			{
				PushInputsToHost();
			}
		}
		PreviewHost->SetPaused(bPreviewPaused);
		return;
	}

	// On the widgets whose components play it.
	PreviewHost->Stop();
	if (bSearch)
	{
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
		Live->SetDesignerStateMachinePreviewPaused(bPreviewPaused);
	}
}

void SWidgetAnimStateMachineEditor::StopComponentPreviews()
{
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		if (UWidgetAnimationComponent* Live = Weak.Get())
		{
			Live->SetDesignerStateMachinePreviewPaused(false);
			Live->StopDesignerStateMachinePreview();
		}
	}
	PreviewComponents.Reset();
}

void SWidgetAnimStateMachineEditor::StopPreviews()
{
	StopComponentPreviews();
	if (PreviewHost.IsValid())
	{
		PreviewHost->Stop();
	}
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
		if (Input.Type == EWidgetAnimMachineInputType::Vector2D)
		{
			Component->SetStateMachineVector2D(Input.Name, GetShownVector(Input.Name), Machine);
		}
		else if (Input.Type != EWidgetAnimMachineInputType::Trigger)
		{
			Component->SetStateMachineInputValue(Input.Name, GetShownInputValue(Input.Name), Machine);
		}
	}
	for (const TPair<FName, bool>& BuiltIn : BuiltInValues)
	{
		Component->SetDesignerStateMachineBuiltIn(BuiltIn.Key, BuiltIn.Value, Machine);
	}
}

void SWidgetAnimStateMachineEditor::PushInputsToHost() const
{
	if (!Machine || !PreviewHost.IsValid() || !PreviewHost->IsRunning())
	{
		return;
	}
	for (const FWidgetAnimMachineInput& Input : Machine->Inputs)
	{
		if (Input.Type == EWidgetAnimMachineInputType::Vector2D)
		{
			PreviewHost->SetVector(Input.Name, GetShownVector(Input.Name));
		}
		else if (Input.Type != EWidgetAnimMachineInputType::Trigger)
		{
			PreviewHost->SetInput(Input.Name, Input.Type, GetShownInputValue(Input.Name));
		}
	}
	for (const TPair<FName, bool>& BuiltIn : BuiltInValues)
	{
		PreviewHost->SetBuiltIn(BuiltIn.Key, BuiltIn.Value);
	}
}

void SWidgetAnimStateMachineEditor::SetInputValue(FName Input, float Value)
{
	InputValues.Add(Input, Value);

	// In Play In Editor, onto the widget watched; in the designer, onto the preview.
	if (IsDebuggingPlay())
	{
		if (UWidgetAnimationComponent* Watched = GetWatchedComponent())
		{
			Watched->SetStateMachineInputValue(Input, Value, Machine);
		}
		return;
	}
	if (const FWidgetAnimMachineInput* Definition = Machine ? Machine->FindInput(Input) : nullptr)
	{
		PreviewHost->SetInput(Input, Definition->Type, Value);
	}
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		if (UWidgetAnimationComponent* Live = Weak.Get())
		{
			Live->SetStateMachineInputValue(Input, Value, Machine);
		}
	}
}

void SWidgetAnimStateMachineEditor::SetVectorValue(FName Input, const FVector2D& Value)
{
	VectorValues.Add(Input, Value);

	// In Play In Editor, onto the widget watched; in the designer, onto the preview.
	if (IsDebuggingPlay())
	{
		if (UWidgetAnimationComponent* Watched = GetWatchedComponent())
		{
			Watched->SetStateMachineVector2D(Input, Value, Machine);
		}
		return;
	}
	PreviewHost->SetVector(Input, Value);
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		if (UWidgetAnimationComponent* Live = Weak.Get())
		{
			Live->SetStateMachineVector2D(Input, Value, Machine);
		}
	}
}

FVector2D SWidgetAnimStateMachineEditor::GetShownVector(FName Input) const
{
	if (IsDebuggingPlay())
	{
		const UWidgetAnimationComponent* Watched = GetWatchedComponent();
		return Watched ? Watched->GetStateMachineVector2D(Input, Machine) : FVector2D::ZeroVector;
	}
	if (const FVector2D* Value = VectorValues.Find(Input))
	{
		return *Value;
	}
	const FWidgetAnimMachineInput* Definition = Machine ? Machine->FindInput(Input) : nullptr;
	return Definition ? Definition->DefaultVector2D : FVector2D::ZeroVector;
}

void SWidgetAnimStateMachineEditor::FireBuiltIn(FName Input)
{
	if (IsDebuggingPlay())
	{
		return;
	}
	PreviewHost->SetBuiltIn(Input, true);
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		if (UWidgetAnimationComponent* Live = Weak.Get())
		{
			Live->SetDesignerStateMachineBuiltIn(Input, true, Machine);
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
	PreviewHost->SetInput(Input, EWidgetAnimMachineInputType::Trigger, 1.f);
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
	PreviewHost->SetBuiltIn(Input, bValue);
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
			if (Definition && (Definition->Type == EWidgetAnimMachineInputType::Number || Definition->Type == EWidgetAnimMachineInputType::Integer
				|| Definition->Type == EWidgetAnimMachineInputType::Choice))
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
	if (!Definition || Definition->Type == EWidgetAnimMachineInputType::Hold || Definition->Type == EWidgetAnimMachineInputType::Choice)
	{
		return 0.f;
	}
	if (Definition->Type == EWidgetAnimMachineInputType::Integer)
	{
		return static_cast<float>(Definition->DefaultInteger);
	}
	return Definition->Type == EWidgetAnimMachineInputType::Number ? Definition->DefaultNumber : (Definition->bDefaultBool ? 1.f : 0.f);
}

float SWidgetAnimStateMachineEditor::GetShownHoldProgress(FName Input) const
{
	// How far through its Hold Time a Press And Hold is, in whatever is playing the machine: the watched widget in Play In
	// Editor, else the designer's preview.
	if (IsDebuggingPlay())
	{
		const UWidgetAnimationComponent* Watched = GetWatchedComponent();
		return Watched ? Watched->GetStateMachineNumber(Input, Machine) : 0.f;
	}
	float Progress = 0.f;
	if (PreviewHost.IsValid() && PreviewHost->IsRunning() && WidgetAnimStateMachines::GetNumber(PreviewHost->GetInstance(), Input, Progress))
	{
		return Progress;
	}
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		if (const UWidgetAnimationComponent* Live = Weak.Get())
		{
			return Live->GetStateMachineNumber(Input, Machine);
		}
	}
	return 0.f;
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
// Preview Targets
// =====================================================================================================

TArray<FString> SWidgetAnimStateMachineEditor::GetTargetOptions() const
{
	TArray<FString> Options;
	if (UUserWidget* Root = GetDesignerPreview())
	{
		UWidgetAnimLibrary::GatherTargetableWidgetPathsInTree(Root, Options);
	}
	// First, so there is a way back to "the widget itself" once something has been picked.
	Options.Insert(WidgetAnimTargetOption::ThisWidget(), 0);
	return Options;
}

FString SWidgetAnimStateMachineEditor::FindTargetOptionFor(const UWidget* Widget) const
{
	UUserWidget* Root = GetDesignerPreview();
	if (!Widget || !Root)
	{
		return FString();
	}
	if (Widget == Root)
	{
		return WidgetAnimTargetOption::ThisWidget();
	}
	for (const FString& Option : GetTargetOptions())
	{
		if (!WidgetAnimTargetOption::IsThisWidget(Option) && UWidgetAnimLibrary::FindWidgetByPathInTree(Root, FName(*Option)) == Widget)
		{
			return Option;
		}
	}
	return FString();
}

FName SWidgetAnimStateMachineEditor::GetPreviewHostPath() const
{
	return Machine ? Machine->PreviewHostPath : NAME_None;
}

FName SWidgetAnimStateMachineEditor::GetPreviewTargetPath(FName Target) const
{
	if (!Machine)
	{
		return NAME_None;
	}
	if (const FName* Picked = Machine->PreviewTargetPaths.Find(Target))
	{
		return *Picked;
	}
	// Not picked here: where its animation's group previews in the Widget Animations panel.
	return Machine->GetTargetDefault(Target);
}

void SWidgetAnimStateMachineEditor::SetPreviewHostPath(FName Path)
{
	if (!Machine || Machine->PreviewHostPath == Path)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("SetPreviewHost", "Set State Machine Preview Widget"));
	Machine->Modify();
	Machine->PreviewHostPath = Path;
	Machine->MarkPackageDirty();
}

void SWidgetAnimStateMachineEditor::SetPreviewTargetPath(FName Target, FName Path)
{
	if (!Machine || GetPreviewTargetPath(Target) == Path)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("SetPreviewTarget", "Set State Machine Preview Target"));
	Machine->Modify();
	Machine->PreviewTargetPaths.Add(Target, Path);
	Machine->MarkPackageDirty();
}

UWidget* SWidgetAnimStateMachineEditor::GetDesignerSelectedWidget() const
{
	FWidgetBlueprintEditor* Editor = FindWidgetBlueprintEditor(WidgetBlueprint.Get());
	if (!Editor || Editor->GetSelectedWidgets().Num() != 1)
	{
		return nullptr;
	}
	return Editor->GetSelectedWidgets().CreateConstIterator()->GetPreview();
}

void SWidgetAnimStateMachineEditor::BeginTargetPick(FName Target)
{
	if (!GetDesignerPreview() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	// A pick for another target gives way to this one.
	CancelTargetPick();

	// What is selected there now doesn't count: the next widget selected does - or, under the eyedropper, the one clicked.
	PickingTarget = Target;
	UWidget* Current = GetDesignerSelectedWidget();
	PickStartSelection = Current;
	bPickStartedWithSelection = Current != nullptr;

	PickInput = MakeShared<FWidgetAnimMachinePickInput>(SharedThis(this));
	FSlateApplication::Get().RegisterInputPreProcessor(PickInput);

	// The name of the widget under the mouse, in orange beside the cursor - so it is known before the click.
	PickLabelWindow = SWindow::MakeCursorDecorator();
	PickLabelWindow->SetContent(
		SNew(SBorder)
		.BorderImage(WidgetAnimMachineStyle::RoundedBrush(3.f))
		.BorderBackgroundColor(MachineColors::PickLabelBackground)
		.Padding(FMargin(6.f, 2.f))
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(MachineColors::PickLabelText)
			.Text_Lambda([WeakEditor = TWeakPtr<SWidgetAnimStateMachineEditor>(SharedThis(this))]()
			{
				const TSharedPtr<SWidgetAnimStateMachineEditor> Pinned = WeakEditor.Pin();
				return Pinned ? FText::FromString(Pinned->PickHoverLabel) : FText::GetEmpty();
			})
		]);
	FSlateApplication::Get().AddWindow(PickLabelWindow.ToSharedRef(), /*bShowImmediately=*/false);
}

void SWidgetAnimStateMachineEditor::CancelTargetPick()
{
	if (!PickingTarget.IsSet())
	{
		return;
	}
	PickingTarget.Reset();
	PickStartSelection.Reset();
	bPickStartedWithSelection = false;
	PickHoverWidget.Reset();
	PickHoverLabel.Reset();

	if (FSlateApplication::IsInitialized())
	{
		// Taken off on the next tick, not now: this is often reached from inside the processor's own handling of a click,
		// while Slate walks its list of processors. Until then it does nothing - nothing is being picked.
		if (PickInput.IsValid())
		{
			const TWeakPtr<IInputProcessor> WeakInput = PickInput;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakInput](float)
			{
				const TSharedPtr<IInputProcessor> Input = WeakInput.Pin();
				if (Input.IsValid() && FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().UnregisterInputPreProcessor(Input);
				}
				return false;
			}));
		}
		if (PickLabelWindow.IsValid())
		{
			PickLabelWindow->HideWindow();
			PickLabelWindow->RequestDestroyWindow();
		}
	}
	PickLabelWindow.Reset();

	// The processor lets go on the next tick; this keeps it alive until then.
	if (PickInput.IsValid())
	{
		const TSharedPtr<FWidgetAnimMachinePickInput> KeepAlive = PickInput;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([KeepAlive](float) { return false; }), 0.5f);
	}
	PickInput.Reset();
}

void SWidgetAnimStateMachineEditor::UpdateTargetPick()
{
	if (!PickingTarget.IsSet())
	{
		return;
	}
	if (!GetDesignerPreview())
	{
		CancelTargetPick();
		return;
	}

	// The eyedropper's click is the pick (see FWidgetAnimMachinePickInput); a widget selected in the designer some other
	// way - its hierarchy, say - picks it too.
	UWidget* Now = GetDesignerSelectedWidget();
	if (bPickStartedWithSelection)
	{
		if (Now == PickStartSelection.Get())
		{
			return;
		}
		bPickStartedWithSelection = false;
	}
	if (Now)
	{
		ApplyPickedTarget(Now);
	}
}

UWidget* SWidgetAnimStateMachineEditor::FindPreviewWidgetAt(const FVector2D& ScreenPosition, FString& OutOption) const
{
	OutOption.Reset();
	UUserWidget* Root = GetDesignerPreview();
	if (!Root || !Root->WidgetTree || !FSlateApplication::IsInitialized())
	{
		return nullptr;
	}

	// Never while over this editor - docked, it may sit beside or over the designer.
	FSlateApplication& Slate = FSlateApplication::Get();
	const FWidgetPath Path = Slate.LocateWindowUnderMouse(ScreenPosition, Slate.GetInteractiveTopLevelWindows(), /*bIgnoreEnabledStatus=*/true);
	for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
	{
		if (&Path.Widgets[Index].Widget.Get() == this)
		{
			return nullptr;
		}
	}

	// The widget drawn on top under the mouse that can be a target: of every visible widget of the preview whose box holds
	// the point, the last in paint order - children after their parent, later siblings after earlier ones, a Canvas Panel's
	// children in their Z Order. Not Slate's hit testing, which skips what doesn't take the mouse - an Image, a Text Block.
	UWidget* Best = nullptr;
	FString BestOption;
	TFunction<void(UWidget*, int32)> Visit;
	Visit = [&](UWidget* Candidate, int32 Depth)
	{
		if (!Candidate || Depth > 64)
		{
			return;
		}
		const TSharedPtr<SWidget> CandidateSlate = Candidate->GetCachedWidget();
		if (!CandidateSlate.IsValid() || !CandidateSlate->GetVisibility().IsVisible())
		{
			return;
		}

		const FGeometry& Geometry = Candidate->GetCachedGeometry();
		const FVector2D Extent(Geometry.GetAbsoluteSize());
		if (Extent.X > 0.0 && Extent.Y > 0.0 && Geometry.IsUnderLocation(ScreenPosition))
		{
			const FString Option = FindTargetOptionFor(Candidate);
			if (!Option.IsEmpty())
			{
				Best = Candidate;
				BestOption = Option;
			}
		}

		if (const UUserWidget* Nested = Cast<UUserWidget>(Candidate))
		{
			if (Nested != Root && Nested->WidgetTree)
			{
				Visit(Nested->WidgetTree->RootWidget, Depth + 1);
			}
			return;
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Candidate))
		{
			TArray<UWidget*, TInlineAllocator<16>> Children;
			TArray<int32, TInlineAllocator<16>> Orders;
			for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
			{
				UWidget* Child = Panel->GetChildAt(ChildIndex);
				const UCanvasPanelSlot* CanvasSlot = Child ? Cast<UCanvasPanelSlot>(Child->Slot) : nullptr;
				Children.Add(Child);
				Orders.Add(CanvasSlot ? CanvasSlot->GetZOrder() : 0);
			}
			TArray<int32, TInlineAllocator<16>> Sorted;
			for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
			{
				Sorted.Add(ChildIndex);
			}
			Algo::StableSort(Sorted, [&Orders](int32 A, int32 B) { return Orders[A] < Orders[B]; });
			for (const int32 ChildIndex : Sorted)
			{
				Visit(Children[ChildIndex], Depth + 1);
			}
		}
	};
	Visit(Root->WidgetTree->RootWidget, 0);

	// The preview itself, under the mouse but nothing in it.
	if (!Best && Root->GetCachedWidget().IsValid() && Root->GetCachedGeometry().IsUnderLocation(ScreenPosition))
	{
		Best = Root;
		BestOption = WidgetAnimTargetOption::ThisWidget();
	}

	OutOption = BestOption;
	return Best;
}

void SWidgetAnimStateMachineEditor::UpdatePickHover(const FVector2D& ScreenPosition)
{
	if (!PickingTarget.IsSet())
	{
		return;
	}

	FString Option;
	UWidget* Hovered = FindPreviewWidgetAt(ScreenPosition, Option);
	PickHoverWidget = Hovered;
	PickHoverLabel = !Hovered ? FString() : WidgetAnimTargetOption::IsThisWidget(Option) ? Hovered->GetName() : Option;

	if (PickLabelWindow.IsValid())
	{
		if (Hovered)
		{
			PickLabelWindow->MoveWindowTo(ScreenPosition + FVector2D(18.0, 18.0));
			if (!PickLabelWindow->IsVisible())
			{
				PickLabelWindow->ShowWindow();
			}
		}
		else if (PickLabelWindow->IsVisible())
		{
			PickLabelWindow->HideWindow();
		}
	}
}

bool SWidgetAnimStateMachineEditor::CommitPickHover()
{
	UWidget* Hovered = PickHoverWidget.Get();
	if (!PickingTarget.IsSet() || !Hovered)
	{
		return false;
	}
	ApplyPickedTarget(Hovered);
	return true;
}

void SWidgetAnimStateMachineEditor::ApplyPickedTarget(UWidget* Picked)
{
	if (!PickingTarget.IsSet())
	{
		return;
	}
	const FName Target = PickingTarget.GetValue();
	CancelTargetPick();

	const FString Option = FindTargetOptionFor(Picked);
	if (Option.IsEmpty())
	{
		FNotificationInfo Info(MachineTips::PickTargetNotFoundNote());
		Info.ExpireDuration = 4.f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	const FName Path = WidgetAnimTargetOption::ToPath(Option);
	if (Target == HostPickKey)
	{
		SetPreviewHostPath(Path);
	}
	else
	{
		SetPreviewTargetPath(Target, Path);
	}
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
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(ToolTip))
			.ContentPadding(FMargin(4.f, 2.f))
			.OnClicked_Lambda([OnClicked]() { OnClicked(); return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeFixedIcon(Icon)
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

	// The editor's own save icon and a word, as the Widget Animations panel's Save buttons are.
	auto SaveContent = [](const FText& Label)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				MakeFixedIcon(FAppStyle::GetBrush(TEXT("Icons.Save")))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
			];
	};
	// Save State Machine To's menu: the Default Folder, or a Custom Location - as Save Animation To's is.
	auto MakeSaveToMenu = [this]() -> TSharedRef<SWidget>
	{
		FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
		const FString Folder = GetDefaultMachineFolder();
		Menu.AddMenuEntry(
			FUIAction(FExecuteAction::CreateLambda([this]() { SaveDraftTo(/*bChooseLocation=*/false); })),
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock).Text(NameTBD::Tooltips::WidgetAnimEditor::SaveToDefaultFolder())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::FromString(Folder))
			]);
		Menu.AddMenuEntry(
			NameTBD::Tooltips::WidgetAnimEditor::SaveToCustomLocation(),
			NameTBD::Tooltips::WidgetAnimEditor::SaveToCustomLocationTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this]() { SaveDraftTo(/*bChooseLocation=*/true); })));
		return Menu.MakeWidget();
	};
	auto IsDraft = [this]() { return IsDraftMachine(Machine); };
	auto HasSavedDraft = [this]() { return IsDraftMachine(Machine) && GetDraftSavedAsset() != nullptr; };

	// Left: what is edited - in State Machine mode, its name. Right: the watched widget in Play In Editor, Dock in Layout, the
	// lock, and Save - right-aligned. (Play / pause floats over the graph: MakePlayPauseButton.)
	TSharedRef<SHorizontalBox> Left = SNew(SHorizontalBox)

		// State Machine or Edit Existing Asset: the Widget Blueprint's own machine, or a WASM_ asset - as the Widget Animations
		// panel's Animation Timeline / Edit Existing Asset. Only in a Widget Blueprint: the asset editor edits its asset.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(2.f, 2.f, 6.f, 2.f))
		[
			SNew(SComboButton)
			.ContentPadding(FMargin(4.f, 0.f))
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::MachineModeTooltip()))
			.Visibility_Lambda([this]() { return WidgetBlueprint.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnGetMenuContent_Lambda([this]()
			{
				FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
				Menu.AddMenuEntry(
					LOCTEXT("DraftMachineMode", "State Machine"),
					MachineTips::DraftMachineModeTooltip(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GraphEditor.StateMachine_16x")),
					FUIAction(FExecuteAction::CreateLambda([this]() { SwitchToDraft(); })));
				Menu.AddMenuEntry(
					LOCTEXT("EditExistingMachine", "Edit Existing Asset"),
					MachineTips::EditExistingMachineTooltip(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.DataAsset")),
					FUIAction(FExecuteAction::CreateLambda([this]() { SwitchToAsset(); })));
				return Menu.MakeWidget();
			})
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
				[
					SNew(SBox)
					.WidthOverride(16.f)
					.HeightOverride(16.f)
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image_Lambda([IsDraft]()
						{
							return IsDraft()
								? FindEditorBrush({ TEXT("GraphEditor.StateMachine_16x"), TEXT("ClassIcon.AnimStateMachine"), TEXT("Icons.Blueprints") })
								: FindEditorBrush({ TEXT("ClassIcon.DataAsset"), TEXT("Icons.Package") });
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([IsDraft]() { return IsDraft() ? LOCTEXT("DraftMachineMode", "State Machine") : LOCTEXT("EditExistingMachine", "Edit Existing Asset"); })
				]
			]
		]

		// State Machine: its name, as a field - double-click to rename it (as a timeline's name in the Widget Animations panel).
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(2.f, 2.f))
		[
			MakeDraftNameField()
		]

		// Edit Existing Asset: which WASM_.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(2.f, 2.f))
		[
			SNew(SBox)
			.MinDesiredWidth(200.f)
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::MachinePickerTooltip()))
			.Visibility_Lambda([IsDraft]() { return IsDraft() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UWidgetAnimStateMachineAsset::StaticClass())
				.AllowClear(false)
				.DisplayThumbnail(false)
				.DisplayUseSelected(false)
				.DisplayBrowse(true)
				.ObjectPath_Lambda([this]() { return Machine && !IsDraftMachine(Machine) ? Machine->GetPathName() : FString(); })
				.OnObjectChanged_Lambda([this](const FAssetData& AssetData) { SetMachine(Cast<UWidgetAnimStateMachineAsset>(AssetData.GetAsset())); })
			]
		];

	TSharedRef<SHorizontalBox> Right = SNew(SHorizontalBox)

		// Play In Editor: the widget watched.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(SComboButton)
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::DebugMenuTooltip()))
			.Visibility_Lambda([]() { return GEditor && GEditor->PlayWorld ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnGetMenuContent(this, &SWidgetAnimStateMachineEditor::MakeDebugMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &SWidgetAnimStateMachineEditor::GetDebugLabel)
			]
		]

		// The lock: the machine read-only - a preview - as the Widget Animations panel's lock makes its animation. Its graph,
		// inputs, layers and Details can't be edited; the preview's inputs still work, to try it.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(2.f, 0.f))
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.f, 2.f))
			.IsEnabled_Lambda([this]() { return Machine != nullptr; })
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(TAttribute<FText>::CreateLambda([this]()
			{
				return bMachineLocked ? NameTBD::Tooltips::WidgetAnimEditor::UnlockTooltip() : MachineTips::LockMachineTooltip();
			})))
			.OnClicked_Lambda([this]()
			{
				bMachineLocked = !bMachineLocked;
				RebuildInputs();
				RebuildLayerTabs();
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(16.f)
					.HeightOverride(16.f)
					[
						SNew(SImage)
						.Image_Lambda([this]()
						{
							return bMachineLocked
								? FindEditorBrush({ TEXT("Icons.Lock"), TEXT("PropertyWindow.Locked") })
								: FindEditorBrush({ TEXT("Icons.Unlock"), TEXT("PropertyWindow.Unlocked") });
						})
						.ColorAndOpacity_Lambda([this]()
						{
							return bMachineLocked ? FSlateColor(NameTBD::Colors::WidgetAnimEditor::ReadOnlyLabel) : FSlateColor::UseForeground();
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(STextBlock)
					.Visibility_Lambda([this]() { return bMachineLocked ? EVisibility::Visible : EVisibility::Collapsed; })
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.ColorAndOpacity(NameTBD::Colors::WidgetAnimEditor::ReadOnlyLabel)
					.Text(LOCTEXT("MachineReadOnlyLabel", "Read Only"))
				]
			]
		]

		// Save. Editing an asset: that asset. The Widget Blueprint's own machine, saved as an asset before: into that asset
		// again, with Save State Machine To beside it to make another; never saved: Save State Machine To. Floating buttons,
		// as the rest of the bar's.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.f, 2.f))
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(TAttribute<FText>::CreateLambda([this]()
			{
				const UWidgetAnimStateMachineAsset* Saved = GetDraftSavedAsset();
				return IsDraftMachine(Machine) && Saved
					? FText::Format(MachineTips::SaveDraftToSavedTooltip(), FText::FromString(Saved->GetName()))
					: MachineTips::SaveMachineTooltip();
			})))
			.Visibility_Lambda([IsDraft, HasSavedDraft]() { return !IsDraft() || HasSavedDraft() ? EVisibility::Visible : EVisibility::Collapsed; })
			.IsEnabled_Lambda([this]() { return Machine != nullptr; })
			.OnClicked_Lambda([this]()
			{
				if (IsDraftMachine(Machine))
				{
					SaveDraftToSavedAsset();
				}
				else
				{
					SaveMachine();
				}
				return FReply::Handled();
			})
			[
				SaveContent(LOCTEXT("SaveMachine", "Save"))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 2.f, 0.f))
		[
			SNew(SComboButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2.f, 2.f))
			.HasDownArrow(true)
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::SaveDraftToTooltip()))
			.Visibility_Lambda([HasSavedDraft]() { return HasSavedDraft() ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnGetMenuContent_Lambda(MakeSaveToMenu)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 2.f, 0.f))
		[
			SNew(SComboButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.f, 2.f))
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::SaveDraftToTooltip()))
			.Visibility_Lambda([IsDraft, HasSavedDraft]() { return IsDraft() && !HasSavedDraft() ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnGetMenuContent_Lambda(MakeSaveToMenu)
			.ButtonContent()
			[
				SaveContent(LOCTEXT("SaveMachineTo", "Save State Machine To"))
			]
		];

	if (bInDrawer && OnDockInLayout.IsBound())
	{
		Right->InsertSlot(0)
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeIconButton(FindEditorBrush({ TEXT("Icons.Layout"), TEXT("Icons.Pinned"), TEXT("Icons.Plus") }), LOCTEXT("DockInLayout", "Dock in Layout"), MachineTips::DockInLayoutTooltip(), [this]() { OnDockInLayout.ExecuteIfBound(); })
			];
	}

	// As the Widget Animations panel's header strip.
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
		.Padding(FMargin(6.f, 0.f, 4.f, 0.f))
		[
			SNew(SBox)
			.HeightOverride(28.f)
			.VAlign(VAlign_Center)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					Left
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					Right
				]
			]
		];
}

TSharedRef<SWidget> SWidgetAnimStateMachineEditor::MakeJumpToActiveButton()
{
	return
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.f, 2.f))
			.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::JumpToActiveTooltip()))
			.Visibility_Lambda([this]()
			{
				int32 Layer = INDEX_NONE;
				FGuid State;
				return FindActiveState(Layer, State) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.OnClicked_Lambda([this]()
			{
				JumpToActiveState();
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(16.f)
					.HeightOverride(16.f)
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image(FindEditorBrush({ TEXT("Icons.Search"), TEXT("GraphEditor.ZoomToFit"), TEXT("Icons.ArrowRight") }))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("JumpToActive", "Jump to Active State"))
				]
				// The state's name, in the glow's orange and bold: the one in play, as the graph rings it.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
					.ColorAndOpacity(MachineColors::ActiveRing)
					.Text(this, &SWidgetAnimStateMachineEditor::GetJumpToActiveStateName)
				]
			];
}

TSharedRef<SWidget> SWidgetAnimStateMachineEditor::MakeGroupBreadcrumb()
{
	// Where the graph is, in the layer row - as the Widget Animations panel shows a collapsed track open in its header: the
	// layer, the way back to all of it, then the collapsed states open on their own.
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
		.Padding(FMargin(2.f, 0.f))
		.Visibility_Lambda([this]()
		{
			const UWidgetAnimMachineGraph* Shown = GetShownGraph();
			return Shown && Shown->OpenGroup.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(3.f, 1.f))
				.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::LeaveGroupTooltip()))
				.OnClicked_Lambda([this]()
				{
					if (UWidgetAnimMachineGraph* Shown = GetShownGraph())
					{
						Shown->OpenGroupView(FGuid());
					}
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
					[
						MakeFixedIcon(FindEditorBrush({ TEXT("Icons.ArrowLeft"), TEXT("Icons.ChevronLeft") }))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text_Lambda([this]()
						{
							const UWidgetAnimMachineGraph* Shown = GetShownGraph();
							const FWidgetAnimMachineLayer* Layer = Shown ? Shown->GetLayer() : nullptr;
							return Layer ? FText::FromName(Layer->Name) : FText::GetEmpty();
						})
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(3.f, 0.f))
			[
				MakeFixedIcon(FindEditorBrush({ TEXT("Icons.ChevronRight"), TEXT("TreeArrow_Collapsed") }))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				MakeFixedIcon(FindEditorBrush({ TEXT("GraphEditor.SubGraph_16x"), TEXT("Icons.FolderClosed") }))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
				.ColorAndOpacity(NameTBD::Colors::WidgetAnimEditor::CollapsedNodeText)
				.Text_Lambda([this]()
				{
					const UWidgetAnimMachineGraph* Shown = GetShownGraph();
					const FWidgetAnimMachineLayer* Layer = Shown ? Shown->GetLayer() : nullptr;
					const FWidgetAnimMachineStateGroup* Group = Layer ? WidgetAnimMachineEdits::FindGroup(*Layer, Shown->OpenGroup) : nullptr;
					return Group ? FText::FromName(Group->Name) : FText::GetEmpty();
				})
			]
		];
}

TSharedRef<SWidget> SWidgetAnimStateMachineEditor::MakeDraftNameField()
{
	const TSharedRef<SWidgetAnimNameField> NameText = SNew(SWidgetAnimNameField)
		.Text_Lambda([this]() { return WidgetAnimDraftMachineName::Get(WidgetAnimDraftMachineName::ExtensionOf(Machine)); })
		.IsReadOnly_Lambda([this]() { return bMachineLocked || !IsDraftMachine(Machine); })
		.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
		{
			WidgetAnimDraftMachineName::Set(WidgetAnimDraftMachineName::ExtensionOf(Machine), NewText.ToString());
		});

	// Anywhere on the field, as on a timeline's name.
	const TWeakPtr<SWidgetAnimNameField> WeakNameText = NameText;
	return SNew(SBorder)
		.BorderImage(WidgetAnimMachineStyle::RoundedBrush(3.f))
		.BorderBackgroundColor(FStyleColors::Input)
		.Padding(FMargin(6.f, 2.f))
		.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::DraftMachineNameTooltip()))
		.Visibility_Lambda([this]() { return IsDraftMachine(Machine) ? EVisibility::Visible : EVisibility::Collapsed; })
		.OnMouseDoubleClick_Lambda([this, WeakNameText](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			const TSharedPtr<SWidgetAnimNameField> Pinned = WeakNameText.Pin();
			if (!Pinned.IsValid() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || bMachineLocked || !IsDraftMachine(Machine))
			{
				return FReply::Unhandled();
			}
			Pinned->EnterEditingMode();
			return FReply::Handled();
		})
		[
			SNew(SBox)
			.MinDesiredWidth(160.f)
			[
				NameText
			]
		];
}

namespace
{
	/**
	 * A rounded outline in any color, set live: an antialiased closed path round the widget's box, inset by half its width.
	 * (A rounded box brush's outline color is the brush's own - a tint doesn't reach it.) Such a path dims where its two ends
	 * meet, so it starts at the middle of the bottom edge and runs on past its start by the DEVELOPER ONLY State Machine Ring
	 * Seam Overlap - its ends overlap rather than meet.
	 */
	class SWidgetAnimMachineRoundedOutline : public SLeafWidget
	{
	public:

		SLATE_BEGIN_ARGS(SWidgetAnimMachineRoundedOutline)
			: _Radius(5.f)
			, _Thickness(1.f)
		{}
			SLATE_ATTRIBUTE(FLinearColor, LineColor)
			SLATE_ARGUMENT(float, Radius)
			SLATE_ARGUMENT(float, Thickness)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			LineColor = InArgs._LineColor;
			Radius = InArgs._Radius;
			Thickness = InArgs._Thickness;
			SetVisibility(EVisibility::HitTestInvisible);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D::ZeroVector;
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const FVector2f Size = AllottedGeometry.GetLocalSize();
			const float Inset = Thickness * 0.5f;
			const FVector2f Min(Inset, Inset);
			const FVector2f Max = Size - FVector2f(Inset, Inset);
			if (Max.X <= Min.X || Max.Y <= Min.Y)
			{
				return LayerId;
			}
			const float Corner = FMath::Clamp(Radius - Inset, 0.5f, FMath::Min(Max.X - Min.X, Max.Y - Min.Y) * 0.5f);
			const FVector2f Centers[] =
			{
				FVector2f(Max.X - Corner, Min.Y + Corner),
				FVector2f(Max.X - Corner, Max.Y - Corner),
				FVector2f(Min.X + Corner, Max.Y - Corner),
				FVector2f(Min.X + Corner, Min.Y + Corner),
			};
			// From the middle of the bottom edge, clockwise round each corner, and on past the start.
			TArray<FVector2f> Points;
			const FVector2f BottomMiddle((Min.X + Max.X) * 0.5f, Max.Y);
			Points.Add(BottomMiddle);
			constexpr int32 ArcSegments = 8;
			for (int32 Step = 0; Step < 4; ++Step)
			{
				const int32 Arc = (Step + 2) % 4;
				const float StartAngle = -UE_HALF_PI + Arc * UE_HALF_PI;
				for (int32 Segment = 0; Segment <= ArcSegments; ++Segment)
				{
					const float Angle = StartAngle + UE_HALF_PI * Segment / ArcSegments;
					Points.Add(Centers[Arc] + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Corner);
				}
			}
			const float Overlap = FMath::Max(UWidgetAnimEditorSettings::Get().MachineRingSeamOverlap, 0.f);
			const float Straight = FMath::Max((Max.X - Min.X) * 0.5f - Corner, 0.f);
			Points.Add(BottomMiddle - FVector2f(FMath::Min(Overlap, Straight), 0.f));
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
				LineColor.Get() * InWidgetStyle.GetColorAndOpacityTint(), /*bAntialias=*/true, Thickness);
			return LayerId + 1;
		}

	private:

		TAttribute<FLinearColor> LineColor;
		float Radius = 5.f;
		float Thickness = 1.f;
	};
}

TSharedRef<SWidget> SWidgetAnimStateMachineEditor::MakePlayPauseButton()
{
	// Floating at the graph's bottom left, on a dark backing as the graph's own overlays are, with a faint outline that sets it
	// off from the graph. Play and Pause both in the Widget Animations panel's Play orange.
	return SNew(SOverlay)
		.Visibility_Lambda([this]() { return WidgetBlueprint.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(WidgetAnimMachineStyle::RoundedBrush(6.f))
			.BorderBackgroundColor(MachineColors::FloatingButtonBacking)
			.Padding(FMargin(3.f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(6.f, 4.f))
				.ToolTip(WidgetAnimMachineStyle::MakeToolTip(TAttribute<FText>::CreateLambda([this]()
				{
					return bPreviewPaused ? MachineTips::PreviewPlayTooltip() : MachineTips::PreviewPauseTooltip();
				})))
				.IsEnabled_Lambda([this]() { return bPreviewInDesigner && !(GEditor && GEditor->PlayWorld); })
				.OnClicked_Lambda([this]()
				{
					bPreviewPaused = !bPreviewPaused;
					return FReply::Handled();
				})
				[
					SNew(SBox)
					.WidthOverride(16.f)
					.HeightOverride(16.f)
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor(NameTBD::Colors::WidgetAnimEditor::PlayTint))
						.Image_Lambda([this]()
						{
							return bPreviewPaused
								? FindEditorBrush({ TEXT("Icons.Play"), TEXT("PlayWorld.ResumePlaySession"), TEXT("GenericPlay") })
								: FindEditorBrush({ TEXT("Icons.Pause"), TEXT("PlayWorld.PausePlaySession"), TEXT("GenericPause") });
						})
					]
				]
			]
		]
		// Its outline: Editor Preferences > Widget Animations Editor > DEVELOPER ONLY > State Machine Play / Pause Outline.
		+ SOverlay::Slot()
		[
			SNew(SWidgetAnimMachineRoundedOutline)
			.Radius(5.f)
			.Thickness(1.f)
			.LineColor_Lambda([]() { return UWidgetAnimEditorSettings::Get().MachinePlayPauseOutline; })
		];
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
	if (PreviewSource == EPreviewSource::Targets)
	{
		const UWidget* Host = PreviewHost.IsValid() && PreviewHost->IsRunning() ? PreviewHost->GetHost() : nullptr;
		return Host ? FText::Format(MachineTips::PreviewingTargetsStatus(), FText::FromString(Host->GetDisplayLabel().IsEmpty() ? Host->GetName() : Host->GetDisplayLabel())) : MachineTips::NothingToPreviewStatus();
	}
	int32 Count = 0;
	for (const TWeakObjectPtr<UWidgetAnimationComponent>& Weak : PreviewComponents)
	{
		Count += Weak.IsValid() ? 1 : 0;
	}
	return Count > 0 ? FText::Format(MachineTips::PreviewingStatus(), Count) : MachineTips::NoComponentsStatus();
}

FText SWidgetAnimStateMachineEditor::GetDebugLabel() const
{
	const UWidgetAnimationComponent* Watched = GetWatchedComponent();
	return Watched ? FText::Format(LOCTEXT("DebugLabel", "Debug: {0}"), DescribeMachineComponent(Watched)) : LOCTEXT("DebugNone", "Debug: nothing");
}

FString SWidgetAnimStateMachineEditor::GetDefaultMachineFolder() const
{
	const FString ProjectFolder = UWidgetAnimRuntimeSettings::Get().GetDefaultSavePackagePath();
	if (!ProjectFolder.IsEmpty())
	{
		return ProjectFolder;
	}
	const UWidgetBlueprint* Blueprint = WidgetBlueprint.Get();
	return Blueprint ? FPackageName::GetLongPackagePath(Blueprint->GetOutermost()->GetName()) : FString(TEXT("/Game"));
}

void SWidgetAnimStateMachineEditor::CreateNewMachine(bool bChooseLocation)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UWidgetAnimStateMachineFactory* Factory = NewObject<UWidgetAnimStateMachineFactory>();

	const FString Folder = GetDefaultMachineFolder();
	FString UniquePackageName;
	FString UniqueAssetName;
	AssetTools.CreateUniqueAssetName(Folder / TEXT("WASM_NewStateMachine"), FString(), UniquePackageName, UniqueAssetName);

	UObject* Created = bChooseLocation
		? AssetTools.CreateAssetWithDialog(UniqueAssetName, Folder, UWidgetAnimStateMachineAsset::StaticClass(), Factory)
		: AssetTools.CreateAsset(UniqueAssetName, Folder, UWidgetAnimStateMachineAsset::StaticClass(), Factory);
	if (UWidgetAnimStateMachineAsset* NewMachine = Cast<UWidgetAnimStateMachineAsset>(Created))
	{
		if (GEditor)
		{
			TArray<UObject*> ToSelect{ NewMachine };
			GEditor->SyncBrowserToObjects(ToSelect);
		}
		SetMachine(NewMachine);
	}
}

bool SWidgetAnimStateMachineEditor::IsDraftMachine(const UWidgetAnimStateMachineAsset* Edited)
{
	return Edited && Edited->GetOuter() && Edited->GetOuter()->IsA<UWidgetAnimTimelinesExtension>();
}

UWidgetAnimStateMachineAsset* SWidgetAnimStateMachineEditor::GetOrMakeDraftMachine()
{
	UWidgetBlueprint* Blueprint = WidgetBlueprint.Get();
	if (!Blueprint)
	{
		return nullptr;
	}
	if (UWidgetAnimTimelinesExtension* Existing = UWidgetAnimTimelinesExtension::Find(Blueprint))
	{
		if (Existing->StateMachine)
		{
			return Existing->StateMachine;
		}
	}

	// Made inside an undo step, with the extension if the Widget Blueprint has none yet - undone, both go.
	const FScopedTransaction Transaction(LOCTEXT("MakeDraftMachine", "Add Widget Blueprint State Machine"));
	Blueprint->Modify();
	UWidgetAnimTimelinesExtension* Extension = UWidgetAnimTimelinesExtension::FindOrAdd(Blueprint);
	if (!Extension)
	{
		return nullptr;
	}
	Extension->Modify();
	UWidgetAnimStateMachineAsset* Draft = NewObject<UWidgetAnimStateMachineAsset>(Extension, NAME_None, RF_Transactional);
	WidgetAnimMachineEdits::SetUpNewMachine(Draft);
	Extension->StateMachine = Draft;
	Blueprint->MarkPackageDirty();
	return Draft;
}

void SWidgetAnimStateMachineEditor::SwitchToDraft()
{
	if (!IsDraftMachine(Machine))
	{
		SetMachine(GetOrMakeDraftMachine());
	}
}

void SWidgetAnimStateMachineEditor::SwitchToAsset()
{
	if (!IsDraftMachine(Machine))
	{
		return;
	}
	UWidgetAnimStateMachineAsset* Asset = LastAssetMachine.Get();
	if (!Asset)
	{
		Asset = GetDraftSavedAsset();
	}
	if (!Asset)
	{
		Asset = FindMachineInWidgetBlueprint();
	}
	SetMachine(Asset);
}

UWidgetAnimStateMachineAsset* SWidgetAnimStateMachineEditor::GetDraftSavedAsset() const
{
	const UWidgetAnimTimelinesExtension* Extension = UWidgetAnimTimelinesExtension::Find(WidgetBlueprint.Get());
	return Extension ? Extension->SavedStateMachine.Get() : nullptr;
}

namespace
{
	/** Every setting of From laid over To - the whole machine: inputs, layers, states, transitions, the graph's layout. */
	void CopyMachineInto(const UWidgetAnimStateMachineAsset* From, UWidgetAnimStateMachineAsset* To)
	{
		To->Modify();
		for (TFieldIterator<FProperty> It(UWidgetAnimStateMachineAsset::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Transient))
			{
				It->CopyCompleteValue_InContainer(To, From);
			}
		}
		To->Normalize();
		To->PostEditChange();
		To->MarkPackageDirty();
	}
}

void SWidgetAnimStateMachineEditor::SaveDraftTo(bool bChooseLocation)
{
	UWidgetBlueprint* Blueprint = WidgetBlueprint.Get();
	UWidgetAnimStateMachineAsset* Draft = IsDraftMachine(Machine) ? Machine : nullptr;
	if (!Blueprint || !Draft)
	{
		return;
	}

	// Named for the machine's own name, letters and digits only: "Card Hover" is WASM_CardHover. Unrenamed, for the Widget
	// Blueprint: WBP_Card's machine is WASM_Card.
	FString BaseName = Blueprint->GetName();
	BaseName.RemoveFromStart(TEXT("WBP_"));
	if (const UWidgetAnimTimelinesExtension* Named = WidgetAnimDraftMachineName::ExtensionOf(Draft))
	{
		FString Clean;
		for (const TCHAR Letter : Named->StateMachineName)
		{
			if (FChar::IsAlnum(Letter) || Letter == TEXT('_'))
			{
				Clean.AppendChar(Letter);
			}
		}
		if (!Clean.IsEmpty())
		{
			BaseName = Clean;
		}
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UWidgetAnimStateMachineFactory* Factory = NewObject<UWidgetAnimStateMachineFactory>();
	const FString Folder = GetDefaultMachineFolder();
	FString UniquePackageName;
	FString UniqueAssetName;
	AssetTools.CreateUniqueAssetName(Folder / (TEXT("WASM_") + BaseName), FString(), UniquePackageName, UniqueAssetName);
	UObject* Created = bChooseLocation
		? AssetTools.CreateAssetWithDialog(UniqueAssetName, Folder, UWidgetAnimStateMachineAsset::StaticClass(), Factory)
		: AssetTools.CreateAsset(UniqueAssetName, Folder, UWidgetAnimStateMachineAsset::StaticClass(), Factory);
	UWidgetAnimStateMachineAsset* Saved = Cast<UWidgetAnimStateMachineAsset>(Created);
	if (!Saved)
	{
		return;
	}
	CopyMachineInto(Draft, Saved);

	// Remembered, so Save puts it there again.
	if (UWidgetAnimTimelinesExtension* Extension = UWidgetAnimTimelinesExtension::Find(Blueprint))
	{
		const FScopedTransaction Transaction(LOCTEXT("RememberSavedMachine", "Save State Machine To"));
		Extension->Modify();
		Extension->SavedStateMachine = Saved;
	}
	TArray<UPackage*> Packages;
	Packages.Add(Saved->GetPackage());
	FEditorFileUtils::PromptForCheckoutAndSave(Packages, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
	if (GEditor)
	{
		TArray<UObject*> ToSelect{ Saved };
		GEditor->SyncBrowserToObjects(ToSelect);
	}
}

void SWidgetAnimStateMachineEditor::SaveDraftToSavedAsset()
{
	UWidgetAnimStateMachineAsset* Saved = GetDraftSavedAsset();
	if (!Saved || !IsDraftMachine(Machine))
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("SaveDraftMachine", "Save State Machine"));
	CopyMachineInto(Machine, Saved);
	TArray<UPackage*> Packages;
	Packages.Add(Saved->GetPackage());
	FEditorFileUtils::PromptForCheckoutAndSave(Packages, /*bCheckDirty=*/false, /*bPromptToSave=*/false);
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
		// A new id from fix17: under the old one, the saved layout of anyone whose tab once ended up floating kept it there - and
		// reopened it there, floating, every time after.
		const FName TabId(TEXT("WidgetAnimBuilder.WidgetAnimationStateMachineTab"));
		const FName DockedTabId(TEXT("WidgetAnimBuilder.WidgetAnimationStateMachineDocked"));

		/**
		 * The tabs of the tab well along the bottom of the editor, across its whole width, best first: the Widget Animations
		 * panel docked there, the native Animations tab under the names it has gone by, then the bottom well's other tabs.
		 */
		const FName BottomWellTabIds[] =
		{
			TEXT("WidgetAnimBuilder.WidgetAnimationsPanel"),
			TEXT("WidgetAnimBuilder.WidgetAnimationsDocked"),
			TEXT("Animations"),
			TEXT("WidgetAnimSequencer"),
			TEXT("CompilerResults"),
		};

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

		TSharedPtr<SDockTab> FindLiveTab(const TSharedPtr<FTabManager>& TabManager);

		/** The editor as a tab's body - watched, so closing the tab by hand means it isn't docked any more (WidgetAnimDockPreference). */
		TSharedRef<SWidget> MakeTabBody(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit)
		{
			const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
			return WidgetAnimDockPreference::Watch(WidgetAnimDockPreference::StateMachinePanel, WeakToolkit,
				[WeakToolkit]() { const TSharedPtr<FAssetEditorToolkit> Live = WeakToolkit.Pin(); return Live.IsValid() && FindLiveTab(Live->GetTabManager()).IsValid(); },
				SNew(SWidgetAnimStateMachineEditor)
				.WidgetBlueprint(Toolkit.IsValid() ? GetEditedWidgetBlueprint(*Toolkit) : nullptr));
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

		/**
		 * Hands the tab manager the very tab to dock beside, rather than an id for it to look up - a missed lookup floats the new
		 * tab in a window of its own. What Widget Animation Scripting's Dock in Layout does (see WidgetAnimBuilderToolbar.cpp).
		 */
		class FDockBesideTab : public FTabManager::FSearchPreference
		{
		public:
			explicit FDockBesideTab(const TSharedRef<SDockTab>& InBeside) : Beside(InBeside) {}

			virtual TSharedPtr<SDockTab> Search(const FTabManager& Manager, FName PlaceholderId, const TSharedRef<SDockTab>& UnmanagedTab) const override
			{
				return Beside;
			}

		private:
			TSharedRef<SDockTab> Beside;
		};

		/** Every tab under a widget - tabs are their wells' children, so a tab behind another in its well is found too. */
		void GatherTabs(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SDockTab>>& OutTabs, int32 Depth)
		{
			if (Depth > 256)
			{
				return;
			}
			if (Widget->GetType() == FName(TEXT("SDockTab")))
			{
				OutTabs.Add(StaticCastSharedRef<SDockTab>(Widget));
			}
			if (FChildren* Children = Widget->GetChildren())
			{
				for (int32 ChildIndex = 0; ChildIndex < Children->Num(); ++ChildIndex)
				{
					GatherTabs(Children->GetChildAt(ChildIndex), OutTabs, Depth + 1);
				}
			}
		}

		/**
		 * Where the tab may go, best first - the tab well along the bottom of the editor, which runs the editor's whole width:
		 * beside one of its known tabs that is open - Widget Animations docked there, the native Animations tab, Compiler
		 * Results - found among the tabs inside the editor itself (so it never depends on the id an engine version gives them)
		 * or asked of the tab manager by id. Then, for a bottom well whose tabs go by other ids, the lowest tab in the bottom
		 * half of the editor whose well runs nearly its whole width (never the designer, which shares its row).
		 */
		TArray<TSharedRef<SDockTab>> FindDockTargets(const TSharedRef<FAssetEditorToolkit>& Toolkit, const TSharedPtr<FTabManager>& TabManager)
		{
			TArray<TSharedRef<SDockTab>> Tabs;
			TSharedPtr<SWidget> HostWidget;
			if (Toolkit->IsHosted())
			{
				HostWidget = Toolkit->GetToolkitHost()->GetParentWidget();
				GatherTabs(HostWidget.ToSharedRef(), Tabs, 0);
				if (Tabs.Num() == 0)
				{
					if (const TSharedPtr<SWindow> HostWindow = FSlateApplication::Get().FindWidgetWindow(HostWidget.ToSharedRef()))
					{
						GatherTabs(HostWindow.ToSharedRef(), Tabs, 0);
					}
				}
			}
			const FGeometry HostGeometry = HostWidget.IsValid() ? HostWidget->GetTickSpaceGeometry() : FGeometry();
			const FVector2f HostPosition(HostGeometry.GetAbsolutePosition());
			const FVector2f HostSize(HostGeometry.GetAbsoluteSize());

			// What there is to dock beside, for the log - so a dock that goes wrong can be told about.
			UE_LOG(LogWidgetAnimStateMachineDock, Log, TEXT("Dock in Layout: %d tabs in the editor (%.0f x %.0f)."), Tabs.Num(), HostSize.X, HostSize.Y);
			for (const TSharedRef<SDockTab>& Tab : Tabs)
			{
				const FGeometry& Geometry = Tab->GetContent()->GetTickSpaceGeometry();
				UE_LOG(LogWidgetAnimStateMachineDock, Log, TEXT("Dock in Layout:   '%s' (%s)%s, at %.0f,%.0f size %.0f x %.0f."),
					*Tab->GetTabLabel().ToString(), *Tab->GetLayoutIdentifier().ToString(), Tab->GetParentWindow().IsValid() ? TEXT(" floating") : TEXT(""),
					FVector2f(Geometry.GetAbsolutePosition()).X, FVector2f(Geometry.GetAbsolutePosition()).Y, FVector2f(Geometry.GetAbsoluteSize()).X, FVector2f(Geometry.GetAbsoluteSize()).Y);
			}

			TArray<TSharedRef<SDockTab>> Targets;
			for (const FName WantedId : BottomWellTabIds)
			{
				for (const TSharedRef<SDockTab>& Tab : Tabs)
				{
					// Not one floating in a window of its own: only a floating tab's area reports a window.
					if (Tab->GetLayoutIdentifier().TabType == WantedId && !Tab->GetParentWindow().IsValid())
					{
						Targets.AddUnique(Tab);
					}
				}
				if (TabManager.IsValid())
				{
					const TSharedPtr<SDockTab> ById = TabManager->FindExistingLiveTab(FTabId(WantedId));
					if (ById.IsValid() && !ById->GetParentWindow().IsValid())
					{
						Targets.AddUnique(ById.ToSharedRef());
					}
				}
			}

			// The bottom well by its shape.
			if (HostSize.X > 0.f)
			{
				TSharedPtr<SDockTab> Lowest;
				float LowestBottom = -FLT_MAX;
				for (const TSharedRef<SDockTab>& Tab : Tabs)
				{
					const FName Type = Tab->GetLayoutIdentifier().TabType;
					if (Tab->GetTabRole() == ETabRole::MajorTab || Type == TabId || Type == DockedTabId || Tab->GetParentWindow().IsValid())
					{
						continue;
					}
					const FGeometry& Geometry = Tab->GetContent()->GetTickSpaceGeometry();
					const FVector2f Position(Geometry.GetAbsolutePosition());
					const FVector2f Size(Geometry.GetAbsoluteSize());
					const bool bWide = Size.X >= HostSize.X * 0.85f;
					const bool bLow = Position.Y >= HostPosition.Y + HostSize.Y * 0.5f;
					if (bWide && bLow && Position.Y + Size.Y > LowestBottom)
					{
						Lowest = Tab;
						LowestBottom = Position.Y + Size.Y;
					}
				}
				if (Lowest.IsValid())
				{
					Targets.AddUnique(Lowest.ToSharedRef());
				}
			}

			for (const TSharedRef<SDockTab>& Target : Targets)
			{
				UE_LOG(LogWidgetAnimStateMachineDock, Log, TEXT("Dock in Layout: may dock beside '%s' (%s)."), *Target->GetTabLabel().ToString(), *Target->GetLayoutIdentifier().ToString());
			}
			return Targets;
		}

		/**
		 * Takes Tab out of its well at once. A close request alone can leave it among the live tabs a moment longer, and the
		 * next insert under the same id then trips the tab manager's ensure (FindTabInLiveAreas) - with the cost of a callstack
		 * capture, which made docking hitch.
		 */
		void CloseTabNow(const TSharedRef<SDockTab>& Tab)
		{
			const TSharedPtr<FTabManager> Manager = Tab->GetTabManagerPtr();
			Tab->RequestCloseTab();
			if (Manager.IsValid() && Manager->FindExistingLiveTab(Tab->GetLayoutIdentifier()) == Tab)
			{
				Tab->RemoveTabFromParent();
			}
		}

		TSharedPtr<SDockTab> FindLiveTab(const TSharedPtr<FTabManager>& TabManager)
		{
			if (!TabManager.IsValid())
			{
				return nullptr;
			}
			for (const FName OpenId : { TabId, DockedTabId })
			{
				if (const TSharedPtr<SDockTab> Live = TabManager->FindExistingLiveTab(FTabId(OpenId)))
				{
					return Live;
				}
			}
			return nullptr;
		}

		/**
		 * Docks the editor as a tab in the well along the bottom of the editor, from left to right - beside Widget Animations,
		 * or the native Animations and Compiler Results - or brings it forward, open already. As Widget Animation Scripting's
		 * Dock in Layout does: the drawer is dismissed first; the tab goes into the well of the very tab found on screen, under
		 * a spawner-less document id, and is checked to have landed there; one left floating by an earlier dock is docked
		 * properly. Failing all that, the tab's own slot in the layout - after Animations - or floating if the layout has none.
		 */
		void DockInLayout(const TWeakPtr<FAssetEditorToolkit>& WeakToolkit, bool bRestore = false)
		{
			const TSharedPtr<FAssetEditorToolkit> Toolkit = WeakToolkit.Pin();
			const TSharedPtr<FTabManager> TabManager = Toolkit.IsValid() ? Toolkit->GetTabManager() : nullptr;
			if (!TabManager.IsValid())
			{
				return;
			}

			if (UStatusBarSubsystem* StatusBars = !bRestore && GEditor ? GEditor->GetEditorSubsystem<UStatusBarSubsystem>() : nullptr)
			{
				StatusBars->ForceDismissDrawer();
			}
			EnsureTabSpawner(TabManager, WeakToolkit);

			// Docked: brought forward (not when restored as the editor opens), and the choice kept for every editor opened after.
			auto BringForward = [&Toolkit, bRestore](const TSharedRef<SDockTab>& Tab)
			{
				WidgetAnimDockPreference::SetDocked(WidgetAnimDockPreference::StateMachinePanel, WidgetAnimDockPreference::GetEditorMode(Toolkit));
				if (bRestore)
				{
					return;
				}
				Tab->ActivateInParent(ETabActivationCause::SetDirectly);
				if (const TSharedPtr<FTabManager> Manager = Tab->GetTabManagerPtr())
				{
					Manager->DrawAttention(Tab);
				}
			};

			if (const TSharedPtr<SDockTab> Live = FindLiveTab(TabManager))
			{
				// Docked in the editor - a tab inside the editor reports no window of its own: brought forward.
				if (!Live->GetParentWindow().IsValid())
				{
					BringForward(Live.ToSharedRef());
					return;
				}
				// Floating in a window of its own: closed, and docked properly below.
				CloseTabNow(Live.ToSharedRef());
			}

			for (const TSharedRef<SDockTab>& Beside : FindDockTargets(Toolkit.ToSharedRef(), TabManager))
			{
				const TSharedPtr<FTabManager> DockManager = Beside->GetTabManagerPtr().IsValid() ? Beside->GetTabManagerPtr() : TabManager;

				// One docked already under this manager - which the editor's own manager doesn't list when the well is another
				// manager's: brought forward. One left floating there: closed first, so no two tabs ever share the id.
				if (const TSharedPtr<SDockTab> Existing = DockManager->FindExistingLiveTab(FTabId(DockedTabId)))
				{
					if (!Existing->GetParentWindow().IsValid())
					{
						BringForward(Existing.ToSharedRef());
						return;
					}
					CloseTabNow(Existing.ToSharedRef());
				}

				const TSharedRef<SDockTab> Tab = MakeTab(WeakToolkit);
				WidgetAnimDockPreference::InsertDocumentTab(*DockManager, DockedTabId, FDockBesideTab(Beside), Tab);

				const TSharedPtr<SWidget> TabWell = Tab->GetParentWidget();
				if (((TabWell.IsValid() && TabWell == Beside->GetParentWidget()) || DockManager->FindExistingLiveTab(Tab->GetLayoutIdentifier()) == Tab)
					&& !Tab->GetParentWindow().IsValid())
				{
					BringForward(Tab);
					UE_LOG(LogWidgetAnimStateMachineDock, Log, TEXT("Dock in Layout: docked beside '%s' (%s)."), *Beside->GetTabLabel().ToString(), *Beside->GetLayoutIdentifier().ToString());
					return;
				}
				UE_LOG(LogWidgetAnimStateMachineDock, Warning, TEXT("Dock in Layout: couldn't put the tab beside '%s' (%s)."), *Beside->GetTabLabel().ToString(), *Beside->GetLayoutIdentifier().ToString());
				CloseTabNow(Tab);
			}

			// Its own slot in the layout: the bottom well, after Animations, where the layout extension put it - as Widget
			// Animations' Dock in Layout falls back to its own.
			if (const TSharedPtr<SDockTab> Opened = TabManager->TryInvokeTab(FTabId(TabId)))
			{
				BringForward(Opened.ToSharedRef());
				UE_LOG(LogWidgetAnimStateMachineDock, Warning, TEXT("Dock in Layout: nothing open in the bottom well to dock beside - opened in the tab's own slot of the layout%s."),
					Opened->GetParentWindow().IsValid() ? TEXT(", which floats (the layout has no Animations tab to put it after)") : TEXT(""));
			}
		}
	}

	void RestoreDock(const TSharedRef<FAssetEditorToolkit>& Toolkit)
	{
		if (Toolkit->IsHosted() && WidgetAnimDockPreference::IsDockedIn(WidgetAnimDockPreference::StateMachinePanel, Toolkit))
		{
			DockInLayout(TWeakPtr<FAssetEditorToolkit>(Toolkit), /*bRestore=*/true);
		}
	}

	void RegisterTabFactory(const FWidgetBlueprintApplicationMode& Mode, FWorkflowAllowedTabSet& TabFactories)
	{
		TabFactories.RegisterFactory(MakeShared<FStateMachineTabSummoner>(Mode.GetBlueprintEditor()));
	}

	void ExtendLayout(FLayoutExtender& Extender)
	{
		// A closed slot after the native Animations tab's - the well along the bottom - for the tab to open in.
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
