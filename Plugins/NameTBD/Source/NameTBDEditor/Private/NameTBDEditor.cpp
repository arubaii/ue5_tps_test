// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDEditor.h"

// Existing UI/Button includes
#include "ContextMenuButtonDetails.h"
#include "NameTBD/Buttons/ContextMenuButton.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

// For icons and thumbnails
#include "NameTBDStyle.h"
#include "NameTBDThumbnailRenderer.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h" 

#include "WidgetAnimBuilderToolbar.h"
#include "ToolMenus.h"

#include "NameTBDAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "WidgetAnimBuilderDetails.h"
#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"

#define LOCTEXT_NAMESPACE "FNameTBDEditorModule"

void FNameTBDEditorModule::StartupModule()
{
	// 1. Existing Property Editor Customization
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomClassLayout(
		UContextMenuButton::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FContextMenuButtonDetails::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();

	// 2. Initialize the Slate Style for small UI icons
	FNameTBDStyle::Initialize();

	// 3. Register the Custom Thumbnail Renderer for the Content Drawer
	UThumbnailManager::Get().RegisterCustomRenderer(UWidgetAnimPresetAsset::StaticClass(), UNameTBDThumbnailRenderer::StaticClass());
	
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TSharedPtr<IAssetTypeActions> CustomAction = MakeShareable(new FNameTBDAssetTypeActions());
	AssetTools.RegisterAssetTypeActions(CustomAction.ToSharedRef());
	RegisteredAssetTypeActions.Add(CustomAction);
	
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		FToolMenuOwnerScoped OwnerScoped(TEXT("NameTBDEditor"));
		FWidgetAnimBuilderToolbar::Register();
	}));
	
	PropertyModule.RegisterCustomClassLayout(
		UWidgetAnimBuilder::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FWidgetAnimBuilderDetails::MakeInstance));
}

void FNameTBDEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (auto Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action.ToSharedRef());
		}
	}
	RegisteredAssetTypeActions.Empty();
	
	// Existing Property Editor Unregistration
	if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		// Unregistered by name rather than by class pointer: on shutdown the UObject system may already be
		// gone, and StaticClass() would be reaching into it.
		PropertyModule->UnregisterCustomClassLayout(TEXT("ContextMenuButton"));
		PropertyModule->NotifyCustomizationModuleChanged();
		PropertyModule->UnregisterCustomClassLayout(TEXT("WidgetAnimBuilder"));

	}
	
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(TEXT("NameTBDEditor"));

	
	// Unregister Slate Style
	FNameTBDStyle::Shutdown();

	// Unregister Thumbnail Renderer
	if (UObjectInitialized())
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UWidgetAnimPresetAsset::StaticClass());
	}
	
	
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FNameTBDEditorModule, NameTBDEditor)