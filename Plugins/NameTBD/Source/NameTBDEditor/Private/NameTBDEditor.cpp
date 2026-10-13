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
#include "ButtonPreviewToolbar.h"
#include "ToolMenus.h"

#include "NameTBDAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "WidgetAnimBuilderDetails.h"
#include "WidgetAnimationComponentDetails.h"
#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationStateMachineComponent.h"
#include "EdGraphUtilities.h"
#include "WidgetAnimValuePinFactory.h"
#include "WidgetAnimEntryCustomization.h"
#include "WidgetAnimParameterValueCustomization.h"
#include "WidgetAnimMachineConditionCustomization.h"
#include "WidgetAnimEditorSettings.h"
#include "WidgetAnimStateMachineAssetEditor.h"
#include "SWidgetAnimStateMachineEditor.h"
#include "NameTBD/WidgetAnimation/WidgetAnimCue.h"
#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "UMGEditorModule.h"
#include "WidgetShapeDetails.h"
#include "StyledButtonsEditor.h"
#include "WidgetAnimCompileChecks.h"
#include "WidgetShapePathEditor.h"
#include "WidgetAnimTextCanvasEditor.h"
#include "NameTBD/Widgets/Shapes/ShapeWidget.h"
#include "FlowListItemStyleDetails.h"
#include "FlowListPathEditor.h"
#include "WidgetAnimMotionPath.h"
#include "WidgetGizmo.h"
#include "WidgetSpringPresets.h"
#include "NameTBD/Widgets/FlowList/FlowListItemStyle.h"
#include "WidgetDataBinderComponent.h"
#include "WidgetDataBinderDetails.h"
#include "DiegeticScreenDetails.h"
#include "DiegeticScreenMaterial.h"
#include "NameTBD/Diegetic/DiegeticScreenComponent.h"

#define LOCTEXT_NAMESPACE "FNameTBDEditorModule"

void FNameTBDEditorModule::StartupModule()
{
	// 1. Existing Property Editor Customization
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomClassLayout(
		UContextMenuButton::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FContextMenuButtonDetails::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();

	// A Shape track key's shape fields are a property bag, laid out by the engine's own property bag customization -
	// loaded here in case nothing else in the editor has asked for it yet.
	if (FModuleManager::Get().ModuleExists(TEXT("StructUtilsEditor")))
	{
		FModuleManager::Get().LoadModule(TEXT("StructUtilsEditor"));
	}

	// 2. Initialize the Slate Style for small UI icons
	FNameTBDStyle::Initialize();

	// 3. Register the Custom Thumbnail Renderer for the Content Drawer
	UThumbnailManager::Get().RegisterCustomRenderer(UWidgetAnimPresetAsset::StaticClass(), UNameTBDThumbnailRenderer::StaticClass());
	
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TSharedPtr<IAssetTypeActions> CustomAction = MakeShareable(new FNameTBDAssetTypeActions());
	AssetTools.RegisterAssetTypeActions(CustomAction.ToSharedRef());
	RegisteredAssetTypeActions.Add(CustomAction);

	// Widget Animation State Machines (WASM_): opened in their own editor on a double-click.
	TSharedPtr<IAssetTypeActions> MachineAction = MakeShared<FWidgetAnimStateMachineAssetTypeActions>();
	AssetTools.RegisterAssetTypeActions(MachineAction.ToSharedRef());
	RegisteredAssetTypeActions.Add(MachineAction);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		FToolMenuOwnerScoped OwnerScoped(TEXT("NameTBDEditor"));
		FWidgetAnimBuilderToolbar::Register();
		FButtonPreviewToolbar::Register();
	}));
	
	PropertyModule.RegisterCustomClassLayout(
		UWidgetAnimBuilder::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FWidgetAnimBuilderDetails::MakeInstance));

	// The component's Additive Anim Layers get the track editor, inline, instead of plain fields only.
	PropertyModule.RegisterCustomClassLayout(
		UWidgetAnimationComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FWidgetAnimationComponentDetails::MakeInstance));

	// The State Machine Component's: its settings, and the Events group of its machines.
	PropertyModule.RegisterCustomClassLayout(
		UWidgetAnimationStateMachineComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FWidgetAnimationStateMachineComponentDetails::MakeInstance));

	// Every animation entry - a component's, a button's, a card's - shows Target Widgets right under its Animation.
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FWidgetAnimEntry::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FWidgetAnimEntryCustomization::MakeInstance));

	// An animation parameter's value - in an entry's Parameters, a tick, its name, its type and its value.
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FWidgetAnimParameterValue::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FWidgetAnimParameterValueCustomization::MakeInstance));

	// A state machine transition's conditions: Compare offers only what the picked input can be asked.
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FWidgetAnimMachineCondition::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FWidgetAnimMachineConditionCustomization::MakeInstance));

	// Every shape widget's Details: presets, and a Custom Shape's outline tools.
	PropertyModule.RegisterCustomClassLayout(
		UShapeWidget::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FWidgetShapeDetails::MakeInstance));

	// Every spring's Details: its fields, and a Preset menu - the springs production tools ship with, and the project's own.
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FWidgetSpringParams::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FWidgetSpringParamsCustomization::MakeInstance));

	// A Flow List Item Style's looks, laid out for the widget it is on as its Details are shown.
	PropertyModule.RegisterCustomClassLayout(
		UFlowListItemStyle::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FFlowListItemStyleDetails::MakeInstance));

	// A Widget Data Binder's Data Preview: its data live in the designer - sample sets stepped through, values typed, lists changed.
	PropertyModule.RegisterCustomClassLayout(
		UWidgetDataBinderComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FWidgetDataBinderDetails::MakeInstance));

	// A Diegetic Screen's Details: Copy To Project, which copies the plugin's screen material into the project for changing.
	PropertyModule.RegisterCustomClassLayout(
		UDiegeticScreenComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FDiegeticScreenDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();

	// The ready-made buttons: their parts built once placed (never per drop preview), each with its Widget Blueprint GUID.
	StyledButtonsEditor::Register();

	// The plugin's screen materials (a Diegetic Screen's Material Automatic): made in the editor when first needed, and rebuilt
	// when out of date - see DiegeticScreenMaterial.h.
	UDiegeticScreenComponent::SetEditorMaterialProvider([](EDiegeticMaterialVariant Variant)
	{
		return DiegeticScreenMaterial::Get(Variant);
	});

	// What the Widget Animations and state machines a Widget Blueprint holds or plays name but don't have, as warnings in its
	// compile results (the same lines data validation gives their assets).
	WidgetAnimCompileChecks::Register();

	IUMGEditorModule& UMGEditorModule = FModuleManager::LoadModuleChecked<IUMGEditorModule>("UMGEditor");

	// Motion paths - where the Widget Animations panel's animation moves its widgets - on the UMG designer's canvas, with
	// nothing selected: the designer paints each extension every frame it draws, and this one draws the paths then (see
	// WidgetAnimMotionPath.h). First, so every other extension draws over them.
	MotionPathFactory = MakeShared<FWidgetAnimMotionPathFactory>();
	UMGEditorModule.GetDesignerExtensibilityManager()->AddDesignerExtensionFactory(MotionPathFactory.ToSharedRef());

	// A Custom Shape's path editor, on the UMG designer's canvas.
	ShapePathEditorFactory = MakeShared<FWidgetShapePathEditorFactory>();
	UMGEditorModule.GetDesignerExtensibilityManager()->AddDesignerExtensionFactory(ShapePathEditorFactory.ToSharedRef());

	// The animated texts' tag editor, on the same canvas: a toolbar that writes the rich text and Text Reveal tags.
	TextEditorFactory = MakeShared<FWidgetAnimTextEditorFactory>();
	UMGEditorModule.GetDesignerExtensibilityManager()->AddDesignerExtensionFactory(TextEditorFactory.ToSharedRef());

	// A Flow List's Spline path, edited on the same canvas as a Custom Shape's outline.
	FlowListPathEditorFactory = MakeShared<FFlowListPathEditorFactory>();
	UMGEditorModule.GetDesignerExtensibilityManager()->AddDesignerExtensionFactory(FlowListPathEditorFactory.ToSharedRef());

	// The Widget Gizmo: moves, scales, rotates and shears the selected widgets through their Render Transform, and moves their
	// pivot (see WidgetGizmo.h). Last, so it draws over every other extension.
	GizmoFactory = MakeShared<FWidgetGizmoFactory>();
	UMGEditorModule.GetDesignerExtensibilityManager()->AddDesignerExtensionFactory(GizmoFactory.ToSharedRef());

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
	StyledButtonsEditor::Unregister();
	UDiegeticScreenComponent::SetEditorMaterialProvider(nullptr);

	// The State Machine editors kept for the status bar drawers, while what they hold on to is still there.
	WidgetAnimStateMachineEditorHooks::Shutdown();

	WidgetAnimCues::SetPreviewSoundPlayer(nullptr);

	if (ShapePathEditorFactory.IsValid())
	{
		if (IUMGEditorModule* UMGEditorModule = FModuleManager::GetModulePtr<IUMGEditorModule>("UMGEditor"))
		{
			UMGEditorModule->GetDesignerExtensibilityManager()->RemoveDesignerExtensionFactory(ShapePathEditorFactory.ToSharedRef());
		}
		ShapePathEditorFactory.Reset();
	}

	if (TextEditorFactory.IsValid())
	{
		if (IUMGEditorModule* UMGEditorModule = FModuleManager::GetModulePtr<IUMGEditorModule>("UMGEditor"))
		{
			UMGEditorModule->GetDesignerExtensibilityManager()->RemoveDesignerExtensionFactory(TextEditorFactory.ToSharedRef());
		}
		TextEditorFactory.Reset();
	}

	if (MotionPathFactory.IsValid())
	{
		if (IUMGEditorModule* UMGEditorModule = FModuleManager::GetModulePtr<IUMGEditorModule>("UMGEditor"))
		{
			UMGEditorModule->GetDesignerExtensibilityManager()->RemoveDesignerExtensionFactory(MotionPathFactory.ToSharedRef());
		}
		MotionPathFactory.Reset();
	}

	if (FlowListPathEditorFactory.IsValid())
	{
		if (IUMGEditorModule* UMGEditorModule = FModuleManager::GetModulePtr<IUMGEditorModule>("UMGEditor"))
		{
			UMGEditorModule->GetDesignerExtensibilityManager()->RemoveDesignerExtensionFactory(FlowListPathEditorFactory.ToSharedRef());
		}
		FlowListPathEditorFactory.Reset();
	}

	if (GizmoFactory.IsValid())
	{
		if (IUMGEditorModule* UMGEditorModule = FModuleManager::GetModulePtr<IUMGEditorModule>("UMGEditor"))
		{
			UMGEditorModule->GetDesignerExtensibilityManager()->RemoveDesignerExtensionFactory(GizmoFactory.ToSharedRef());
		}
		GizmoFactory.Reset();
	}

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
		PropertyModule->UnregisterCustomClassLayout(TEXT("WidgetAnimationStateMachineComponent"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("WidgetAnimEntry"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("WidgetAnimMachineCondition"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("WidgetAnimParameterValue"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("ShapeWidget"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("FlowListItemStyle"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("WidgetDataBinderComponent"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("DiegeticScreenComponent"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("WidgetSpringParams"));

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