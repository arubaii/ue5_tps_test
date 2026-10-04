// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimStateMachineAssetEditor.h"

#include "AssetTypeCategories.h"
#include "Framework/Docking/TabManager.h"
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

	// One tab, filling the window: the editor has its own toolbar, inputs and Details.
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("Standalone_WidgetAnimStateMachineEditor_v1"))
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
