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
#include "WidgetAnimationComponentDetails.h"
#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "EdGraphUtilities.h"
#include "WidgetAnimValuePinFactory.h"
#include "WidgetAnimEntryCustomization.h"
#include "WidgetAnimEditorSettings.h"
#include "NameTBD/WidgetAnimation/WidgetAnimCue.h"
#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "Editor.h"

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

	// The component's Additive Anim Layers get the track editor, inline, instead of plain fields only.
	PropertyModule.RegisterCustomClassLayout(
		UWidgetAnimationComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FWidgetAnimationComponentDetails::MakeInstance));

	// Every animation entry - a component's, a button's, a card's - shows Target Widgets right under its Animation.
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FWidgetAnimEntry::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FWidgetAnimEntryCustomization::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();

	ValuePinFactory = MakeShared<FWidgetAnimValuePinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(ValuePinFactory);

	// Sound cues heard in the editor's previews, through the editor's own preview audio - the runtime module can't reach
	// it, so it is handed this. One at a time: a new cue sound cuts off the last. See WidgetAnimCue.h.
	WidgetAnimCues::SetPreviewSoundPlayer([](USoundBase* Sound, float Volume, float Pitch, const WidgetAnimCues::FSoundFade& Fade)
	{
		if (!GEditor || !Sound || !UWidgetAnimEditorSettings::Get().bPlayCueSoundsInPreviews)
		{
			return;
		}

		GEditor->PlayPreviewSound(Sound);
		UAudioComponent* PreviewAudio = GEditor->GetPreviewAudioComponent();
		if (!PreviewAudio)
		{
			return;
		}

		PreviewAudio->SetVolumeMultiplier(Volume);
		PreviewAudio->SetPitchMultiplier(Pitch);

		// A Sound cue's fades, as the game plays them. Fading in starts it again, from silence.
		const EAudioFaderCurve Curve = static_cast<EAudioFaderCurve>(FMath::Min<uint8>(Fade.Curve, static_cast<uint8>(EAudioFaderCurve::Sin)));
		if (Fade.FadeIn > 0.f)
		{
			PreviewAudio->FadeIn(Fade.FadeIn, 1.f, 0.f, Curve);
		}
		if (Fade.FadeOut > 0.f && Fade.FadeOutAt >= 0.f)
		{
			// Later, on the core ticker. The preview has one audio component, which a newer cue sound may have taken over
			// by then; the fade is only for this one.
			const TWeakObjectPtr<UAudioComponent> WeakAudio = PreviewAudio;
			const TWeakObjectPtr<USoundBase> WeakSound = Sound;
			const float FadeOut = Fade.FadeOut;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakAudio, WeakSound, FadeOut, Curve](float)
			{
				UAudioComponent* Live = WeakAudio.Get();
				if (Live && Live->IsPlaying() && Live->Sound == WeakSound.Get())
				{
					Live->FadeOut(FadeOut, 0.f, Curve);
				}
				return false;
			}), FMath::Max(Fade.FadeOutAt, 0.001f));
		}
	});
}

void FNameTBDEditorModule::ShutdownModule()
{
	WidgetAnimCues::SetPreviewSoundPlayer(nullptr);

	if (ValuePinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(ValuePinFactory);
		ValuePinFactory.Reset();
	}

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
		PropertyModule->UnregisterCustomClassLayout(TEXT("WidgetAnimationComponent"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("WidgetAnimEntry"));

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