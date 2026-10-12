// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimStateMachineAssetEditor.h"

#include "AssetTypeCategories.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"
#include "NameTBDColors.h"
#include "NameTBDTooltips.h"
#include "SWidgetAnimStateMachineEditor.h"
#include "Styling/AppStyle.h"
#include "WidgetAnimStateMachineGraph.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "WidgetAnimStateMachineAssetEditor"

namespace WidgetAnimStateMachineAssetEditor
{
	const FName AppIdentifier(TEXT("WidgetAnimStateMachineEditorApp"));
	const FName MachineTabId(TEXT("WidgetAnimStateMachineEditor_Machine"));
}

// =====================================================================================================
// Factory
// =====================================================================================================

UWidgetAnimStateMachineFactory::UWidgetAnimStateMachineFactory()
{
	SupportedClass = UWidgetAnimStateMachineAsset::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UWidgetAnimStateMachineFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UWidgetAnimStateMachineAsset* Machine = NewObject<UWidgetAnimStateMachineAsset>(InParent, InClass, InName, Flags | RF_Transactional);
	WidgetAnimMachineEdits::SetUpNewMachine(Machine);
	return Machine;
}

FText UWidgetAnimStateMachineFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryName", "Widget Animation State Machine");
}

uint32 UWidgetAnimStateMachineFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::UI;
}

FString UWidgetAnimStateMachineFactory::GetDefaultNewAssetName() const
{
	return TEXT("WASM_NewStateMachine");
}

// =====================================================================================================
// Asset type actions
// =====================================================================================================

FText FWidgetAnimStateMachineAssetTypeActions::GetName() const
{
	return LOCTEXT("AssetTypeName", "Widget Animation State Machine");
}

FColor FWidgetAnimStateMachineAssetTypeActions::GetTypeColor() const
{
	return NameTBD::Colors::AnimationOrange_Normal.ToFColor(true);
}

UClass* FWidgetAnimStateMachineAssetTypeActions::GetSupportedClass() const
{
	return UWidgetAnimStateMachineAsset::StaticClass();
}

uint32 FWidgetAnimStateMachineAssetTypeActions::GetCategories()
{
	return EAssetTypeCategories::UI;
}

void FWidgetAnimStateMachineAssetTypeActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
	for (UObject* Object : InObjects)
	{
		if (UWidgetAnimStateMachineAsset* Machine = Cast<UWidgetAnimStateMachineAsset>(Object))
		{
			const TSharedRef<FWidgetAnimStateMachineAssetEditor> Editor = MakeShared<FWidgetAnimStateMachineAssetEditor>();
			Editor->InitEditor(Mode, EditWithinLevelEditor, Machine);
		}
	}
}

// =====================================================================================================
// The asset's own editor
// =====================================================================================================

void FWidgetAnimStateMachineAssetEditor::InitEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWidgetAnimStateMachineAsset* Machine)
{
	EditedMachine = Machine;

	// Made first: the window's toolbar holds its controls (MakeStandaloneToolbar).
	MachineEditor = SNew(SWidgetAnimStateMachineEditor)
		.Machine(EditedMachine);

	// One tab, filling the window - no tab row over it: there is nothing to switch to, and its tab could only be closed by mistake.
	// (v2: a layout kept from before, with the row shown, is not used.)
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("Standalone_WidgetAnimStateMachineEditor_v2"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->AddTab(WidgetAnimStateMachineAssetEditor::MachineTabId, ETabState::OpenedTab)
				->SetHideTabWell(true)
			)
		);

	InitAssetEditor(Mode, InitToolkitHost, WidgetAnimStateMachineAssetEditor::AppIdentifier, Layout,
		/*bCreateDefaultStandaloneMenu=*/true, /*bCreateDefaultToolbar=*/true, Machine);

	// The window's toolbar: after Save and Browse, the machine's own - Target Widget Blueprint, play / pause and restart, the
	// widget watched in Play In Editor, Find.
	const TSharedRef<FExtender> ToolbarExtender = MakeShared<FExtender>();
	const TWeakPtr<SWidgetAnimStateMachineEditor> WeakEditor = MachineEditor;
	ToolbarExtender->AddToolBarExtension(TEXT("Asset"), EExtensionHook::After, GetToolkitCommands(), FToolBarExtensionDelegate::CreateLambda([WeakEditor](FToolBarBuilder& Builder)
	{
		if (const TSharedPtr<SWidgetAnimStateMachineEditor> Pinned = WeakEditor.Pin())
		{
			Builder.BeginSection(TEXT("WidgetAnimStateMachine"));
			Builder.AddToolBarWidget(Pinned->MakeStandaloneToolbar());
			Builder.EndSection();
		}
	}));
	AddToolbarExtender(ToolbarExtender);
	RegenerateMenusAndToolbars();
}

void FWidgetAnimStateMachineAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu", "Widget Animation State Machine"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(WidgetAnimStateMachineAssetEditor::MachineTabId, FOnSpawnTab::CreateSP(this, &FWidgetAnimStateMachineAssetEditor::SpawnMachineTab))
		.SetDisplayName(LOCTEXT("MachineTab", "State Machine"))
		.SetTooltipText(NameTBD::Tooltips::StateMachineEditor::StateMachineDrawerTooltip())
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FWidgetAnimStateMachineAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(WidgetAnimStateMachineAssetEditor::MachineTabId);
}

TSharedRef<SDockTab> FWidgetAnimStateMachineAssetEditor::SpawnMachineTab(const FSpawnTabArgs& Args)
{
	if (!MachineEditor.IsValid())
	{
		MachineEditor = SNew(SWidgetAnimStateMachineEditor)
			.Machine(EditedMachine);
	}
	return SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		.Label(LOCTEXT("MachineTab", "State Machine"))
		[
			MachineEditor.ToSharedRef()
		];
}

FName FWidgetAnimStateMachineAssetEditor::GetToolkitFName() const
{
	return TEXT("WidgetAnimStateMachineEditor");
}

FText FWidgetAnimStateMachineAssetEditor::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Widget Animation State Machine");
}

FString FWidgetAnimStateMachineAssetEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("WidgetAnimStateMachine ");
}

FLinearColor FWidgetAnimStateMachineAssetEditor::GetWorldCentricTabColorScale() const
{
	return NameTBD::Colors::AnimationOrange_Normal;
}

#undef LOCTEXT_NAMESPACE
