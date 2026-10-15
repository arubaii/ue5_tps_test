// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuSettingTargets.h"

#include "AudioDevice.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/UserInterfaceSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "GeneralProjectSettings.h"
#include "GenericPlatform/GenericPlatformDriver.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetInternationalizationLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Rendering/SlateRenderer.h"
#include "RHI.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "UObject/StrongObjectPtr.h"
#include "MenuProjectSettings.h"
#include "MenuSettingsSubsystem.h"
#include "NameTBD/Menu/Input/MenuInputSubsystem.h"
#include "NameTBD/Notifications/NotificationSubsystem.h"

#define LOCTEXT_NAMESPACE "MenuSettingTargets"

// =====================================================================================================
//  Accessibility flags
// =====================================================================================================

namespace MenuAccessibility
{
	namespace
	{
		bool bReduceMotion = false;
		bool bHighContrast = false;
		bool bMenuNarration = false;

		FSimpleMulticastDelegate& ChangedDelegate()
		{
			static FSimpleMulticastDelegate Delegate;
			return Delegate;
		}

		void SetFlag(bool& bFlag, bool bEnabled)
		{
			if (bFlag != bEnabled)
			{
				bFlag = bEnabled;
				ChangedDelegate().Broadcast();
			}
		}
	}

	bool IsReduceMotionEnabled() { return bReduceMotion; }
	bool IsHighContrastEnabled() { return bHighContrast; }
	bool IsMenuNarrationEnabled() { return bMenuNarration; }
	void SetReduceMotion(bool bEnabled) { SetFlag(bReduceMotion, bEnabled); }
	void SetHighContrast(bool bEnabled) { SetFlag(bHighContrast, bEnabled); }
	void SetMenuNarration(bool bEnabled) { SetFlag(bMenuNarration, bEnabled); }
	FSimpleMulticastDelegate& OnChanged() { return ChangedDelegate(); }
}

namespace MenuSettingTargetsLocal
{
	/** The value a setting kind hands a console variable: Bools as 1 / 0. */
	FString ToCVarText(const UMenuSetting& Setting, const FString& Value, float Scale, bool bInvert)
	{
		switch (Setting.GetKind())
		{
		case EMenuSettingKind::Bool:
			return (MenuSettingValues::ToBool(Value) != bInvert) ? TEXT("1") : TEXT("0");
		case EMenuSettingKind::Number:
			return MenuSettingValues::FromNumber(MenuSettingValues::ToNumber(Value) * Scale);
		default:
			return Value;
		}
	}

	UGameUserSettings* GetGameUserSettings()
	{
		return GEngine ? GEngine->GetGameUserSettings() : nullptr;
	}

	/** The colour vision correction both of its settings drive. */
	EColorVisionDeficiency ColorBlindType = EColorVisionDeficiency::NormalVision;
	int32 ColorBlindSeverity = 10;

	void ApplyColorBlindness()
	{
		if (FSlateApplication::IsInitialized())
		{
			if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
			{
				Renderer->SetColorVisionDeficiencyType(ColorBlindType, ColorBlindSeverity, true, false);
			}
		}
	}

	const TCHAR* ColorBlindNames[] = { TEXT("None"), TEXT("Deuteranope"), TEXT("Protanope"), TEXT("Tritanope") };

	/** The sound mix volumes go through when the project names none. */
	USoundMix* GetRuntimeSoundMix()
	{
		static TStrongObjectPtr<USoundMix> Mix;
		if (!Mix.IsValid())
		{
			Mix.Reset(NewObject<USoundMix>(GetTransientPackage(), TEXT("NameTBD_SettingsSoundMix")));
		}
		return Mix.Get();
	}

	/** A property of the player controller by name (force feedback: not public API). */
	template <typename TPropertyType>
	TPropertyType* FindControllerProperty(const TCHAR* Name)
	{
		return FindFProperty<TPropertyType>(APlayerController::StaticClass(), Name);
	}
}

// =====================================================================================================
//  Console variable
// =====================================================================================================

bool UMenuSettingTarget_ConsoleVariable::IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const
{
	if (Name.IsEmpty() || !IConsoleManager::Get().FindConsoleVariable(*Name))
	{
		OutReason = LOCTEXT("NoCVar", "Not available with this game's setup.");
		return false;
	}
	return true;
}

bool UMenuSettingTarget_ConsoleVariable::Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const
{
	IConsoleVariable* Variable = Name.IsEmpty() ? nullptr : IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!Variable)
	{
		return false;
	}
	FString Raw = Variable->GetString();
	for (const FMenuCVarValueMap& Entry : ValueMap)
	{
		if (Entry.CVarValue.Equals(Raw, ESearchCase::IgnoreCase)
			|| (MenuSettingValues::IsNumber(Raw) && MenuSettingValues::IsNumber(Entry.CVarValue) && FMath::IsNearlyEqual(MenuSettingValues::ToNumber(Raw), MenuSettingValues::ToNumber(Entry.CVarValue), 1e-4)))
		{
			OutValue = Entry.SettingValue;
			return true;
		}
	}
	switch (Setting.GetKind())
	{
	case EMenuSettingKind::Bool:
		OutValue = MenuSettingValues::FromBool(MenuSettingValues::ToBool(Raw) != bInvert);
		break;
	case EMenuSettingKind::Number:
		OutValue = MenuSettingValues::FromNumber(MenuSettingValues::ToNumber(Raw) / (FMath::IsNearlyZero(Scale) ? 1.f : Scale));
		break;
	default:
		OutValue = Raw;
		break;
	}
	return true;
}

void UMenuSettingTarget_ConsoleVariable::Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const
{
	FString Text;
	bool bMapped = false;
	for (const FMenuCVarValueMap& Entry : ValueMap)
	{
		if (Entry.SettingValue.Equals(Value, ESearchCase::IgnoreCase))
		{
			Text = Entry.CVarValue;
			bMapped = true;
			break;
		}
	}
	if (!bMapped)
	{
		Text = MenuSettingTargetsLocal::ToCVarText(Setting, Value, Scale, bInvert);
	}

	TArray<FString> Names = AlsoSet;
	Names.Insert(Name, 0);
	for (const FString& VariableName : Names)
	{
		if (IConsoleVariable* Variable = VariableName.IsEmpty() ? nullptr : IConsoleManager::Get().FindConsoleVariable(*VariableName))
		{
			Variable->Set(*Text, ECVF_SetByGameSetting);
		}
	}
}

// =====================================================================================================
//  Game User Settings
// =====================================================================================================

bool UMenuSettingTarget_GameUserSettings::IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const
{
	UGameUserSettings* Settings = MenuSettingTargetsLocal::GetGameUserSettings();
	if (!Settings)
	{
		return false;
	}
	switch (Field)
	{
	case EMenuGameUserSetting::WindowMode:
	case EMenuGameUserSetting::Resolution:
		if (!PLATFORM_DESKTOP)
		{
			OutReason = LOCTEXT("DesktopOnly", "Set by the system on this platform.");
			return false;
		}
		return true;
	case EMenuGameUserSetting::HDROutput:
	case EMenuGameUserSetting::HDRNits:
		if (!Settings->SupportsHDRDisplayOutput())
		{
			OutReason = LOCTEXT("NoHDR", "This display doesn't support HDR.");
			return false;
		}
		return true;
	default:
		return true;
	}
}

bool UMenuSettingTarget_GameUserSettings::Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const
{
	UGameUserSettings* Settings = MenuSettingTargetsLocal::GetGameUserSettings();
	if (!Settings)
	{
		return false;
	}
	using namespace MenuSettingValues;
	switch (Field)
	{
	case EMenuGameUserSetting::WindowMode:
		switch (Settings->GetFullscreenMode())
		{
		case EWindowMode::Fullscreen: OutValue = TEXT("Fullscreen"); break;
		case EWindowMode::WindowedFullscreen: OutValue = TEXT("WindowedFullscreen"); break;
		default: OutValue = TEXT("Windowed"); break;
		}
		return true;
	case EMenuGameUserSetting::Resolution: OutValue = FromResolution(Settings->GetScreenResolution()); return true;
	case EMenuGameUserSetting::VSync: OutValue = FromBool(Settings->IsVSyncEnabled()); return true;
	case EMenuGameUserSetting::FrameRateLimit: OutValue = FromNumber(Settings->GetFrameRateLimit()); return true;
	case EMenuGameUserSetting::OverallQuality: OutValue = FromNumber(Settings->GetOverallScalabilityLevel()); return true;
	case EMenuGameUserSetting::ViewDistanceQuality: OutValue = FromNumber(Settings->GetViewDistanceQuality()); return true;
	case EMenuGameUserSetting::AntiAliasingQuality: OutValue = FromNumber(Settings->GetAntiAliasingQuality()); return true;
	case EMenuGameUserSetting::ShadowQuality: OutValue = FromNumber(Settings->GetShadowQuality()); return true;
	case EMenuGameUserSetting::GlobalIlluminationQuality: OutValue = FromNumber(Settings->GetGlobalIlluminationQuality()); return true;
	case EMenuGameUserSetting::ReflectionQuality: OutValue = FromNumber(Settings->GetReflectionQuality()); return true;
	case EMenuGameUserSetting::PostProcessQuality: OutValue = FromNumber(Settings->GetPostProcessingQuality()); return true;
	case EMenuGameUserSetting::TextureQuality: OutValue = FromNumber(Settings->GetTextureQuality()); return true;
	case EMenuGameUserSetting::EffectsQuality: OutValue = FromNumber(Settings->GetVisualEffectQuality()); return true;
	case EMenuGameUserSetting::FoliageQuality: OutValue = FromNumber(Settings->GetFoliageQuality()); return true;
	case EMenuGameUserSetting::ShadingQuality: OutValue = FromNumber(Settings->GetShadingQuality()); return true;
	case EMenuGameUserSetting::ResolutionScale:
	{
		float Normalized = 0.f;
		float Current = 100.f;
		float Lowest = 0.f;
		float Highest = 100.f;
		Settings->GetResolutionScaleInformationEx(Normalized, Current, Lowest, Highest);
		OutValue = FromNumber(Current);
		return true;
	}
	case EMenuGameUserSetting::DynamicResolution: OutValue = FromBool(Settings->IsDynamicResolutionEnabled()); return true;
	case EMenuGameUserSetting::HDROutput: OutValue = FromBool(Settings->IsHDREnabled()); return true;
	case EMenuGameUserSetting::HDRNits: OutValue = FromNumber(Settings->GetCurrentHDRDisplayNits()); return true;
	case EMenuGameUserSetting::AudioQuality: OutValue = FromNumber(Settings->GetAudioQualityLevel()); return true;
	default:
		return false;
	}
}

void UMenuSettingTarget_GameUserSettings::Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const
{
	UGameUserSettings* Settings = MenuSettingTargetsLocal::GetGameUserSettings();
	if (!Settings || Context.IsPreview())
	{
		return;
	}
	using namespace MenuSettingValues;
	const int32 Level = static_cast<int32>(ToNumber(Value));
	bool bResolution = false;
	switch (Field)
	{
	case EMenuGameUserSetting::WindowMode:
	{
		const EWindowMode::Type Mode = Value.Equals(TEXT("Fullscreen"), ESearchCase::IgnoreCase) ? EWindowMode::Fullscreen
			: Value.Equals(TEXT("WindowedFullscreen"), ESearchCase::IgnoreCase) ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed;
		Settings->SetFullscreenMode(Mode);
		if (Mode == EWindowMode::WindowedFullscreen)
		{
			Settings->SetScreenResolution(Settings->GetDesktopResolution());
		}
		bResolution = true;
		break;
	}
	case EMenuGameUserSetting::Resolution:
	{
		FIntPoint Resolution;
		if (ToResolution(Value, Resolution))
		{
			Settings->SetScreenResolution(Resolution);
			bResolution = true;
		}
		break;
	}
	case EMenuGameUserSetting::VSync: Settings->SetVSyncEnabled(ToBool(Value)); break;
	case EMenuGameUserSetting::FrameRateLimit: Settings->SetFrameRateLimit(static_cast<float>(ToNumber(Value))); break;
	case EMenuGameUserSetting::OverallQuality:
		if (Level >= 0)
		{
			Settings->SetOverallScalabilityLevel(Level);
		}
		break;
	case EMenuGameUserSetting::ViewDistanceQuality: Settings->SetViewDistanceQuality(Level); break;
	case EMenuGameUserSetting::AntiAliasingQuality: Settings->SetAntiAliasingQuality(Level); break;
	case EMenuGameUserSetting::ShadowQuality: Settings->SetShadowQuality(Level); break;
	case EMenuGameUserSetting::GlobalIlluminationQuality: Settings->SetGlobalIlluminationQuality(Level); break;
	case EMenuGameUserSetting::ReflectionQuality: Settings->SetReflectionQuality(Level); break;
	case EMenuGameUserSetting::PostProcessQuality: Settings->SetPostProcessingQuality(Level); break;
	case EMenuGameUserSetting::TextureQuality: Settings->SetTextureQuality(Level); break;
	case EMenuGameUserSetting::EffectsQuality: Settings->SetVisualEffectQuality(Level); break;
	case EMenuGameUserSetting::FoliageQuality: Settings->SetFoliageQuality(Level); break;
	case EMenuGameUserSetting::ShadingQuality: Settings->SetShadingQuality(Level); break;
	case EMenuGameUserSetting::ResolutionScale: Settings->SetResolutionScaleValueEx(static_cast<float>(ToNumber(Value, 100.0))); break;
	case EMenuGameUserSetting::DynamicResolution: Settings->SetDynamicResolutionEnabled(ToBool(Value)); break;
	case EMenuGameUserSetting::HDROutput: Settings->EnableHDRDisplayOutput(ToBool(Value), Settings->GetCurrentHDRDisplayNits()); return;
	case EMenuGameUserSetting::HDRNits: Settings->EnableHDRDisplayOutput(Settings->IsHDREnabled(), Level); return;
	case EMenuGameUserSetting::AudioQuality: Settings->SetAudioQualityLevel(Level); return;
	default:
		return;
	}

	if (bResolution)
	{
		Settings->ApplyResolutionSettings(false);
	}
	else
	{
		Settings->ApplyNonResolutionSettings();
	}
}

bool UMenuSettingTarget_GameUserSettings::GetDefault(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const
{
	UGameUserSettings* Settings = MenuSettingTargetsLocal::GetGameUserSettings();
	if (!Settings)
	{
		return false;
	}
	switch (Field)
	{
	case EMenuGameUserSetting::Resolution:
		OutValue = MenuSettingValues::FromResolution(Settings->GetDesktopResolution());
		return true;
	case EMenuGameUserSetting::WindowMode:
		switch (UGameUserSettings::GetDefaultWindowMode())
		{
		case EWindowMode::Fullscreen: OutValue = TEXT("Fullscreen"); break;
		case EWindowMode::WindowedFullscreen: OutValue = TEXT("WindowedFullscreen"); break;
		default: OutValue = TEXT("Windowed"); break;
		}
		return true;
	default:
		return false;
	}
}

bool UMenuSettingTarget_GameUserSettings::GetOptions(const UMenuSetting& Setting, const FMenuSettingContext& Context, TArray<FMenuSettingOption>& OutOptions) const
{
	UGameUserSettings* Settings = MenuSettingTargetsLocal::GetGameUserSettings();
	if (Field == EMenuGameUserSetting::Resolution && Settings)
	{
		TArray<FIntPoint> Resolutions;
		if (Settings->GetFullscreenMode() == EWindowMode::Fullscreen)
		{
			UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
		}
		else if (Settings->GetFullscreenMode() == EWindowMode::Windowed)
		{
			UKismetSystemLibrary::GetConvenientWindowedResolutions(Resolutions);
		}
		Resolutions.AddUnique(Settings->GetDesktopResolution());
		Resolutions.AddUnique(Settings->GetScreenResolution());
		Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
		for (const FIntPoint& Resolution : Resolutions)
		{
			if (Resolution.X <= 0 || Resolution.Y <= 0)
			{
				continue;
			}
			OutOptions.Add(FMenuSettingOption(MenuSettingValues::FromResolution(Resolution),
				FText::Format(LOCTEXT("ResolutionLabel", "{0} x {1}"), FText::AsNumber(Resolution.X, &FNumberFormattingOptions::DefaultNoGrouping()), FText::AsNumber(Resolution.Y, &FNumberFormattingOptions::DefaultNoGrouping()))));
		}
		return OutOptions.Num() > 0;
	}
	if (Field == EMenuGameUserSetting::AudioQuality)
	{
		const UAudioSettings* Audio = ::GetDefault<UAudioSettings>();
		for (int32 Index = 0; Audio && Index < Audio->QualityLevels.Num(); ++Index)
		{
			OutOptions.Add(FMenuSettingOption(MenuSettingValues::FromNumber(Index), Audio->QualityLevels[Index].DisplayName));
		}
		return OutOptions.Num() > 0;
	}
	return false;
}

void UMenuSettingTarget_GameUserSettings::Save(const FMenuSettingContext& Context) const
{
	if (UGameUserSettings* Settings = MenuSettingTargetsLocal::GetGameUserSettings())
	{
		Settings->SaveSettings();
	}
}

// =====================================================================================================
//  Sound volume
// =====================================================================================================

USoundClass* UMenuSettingTarget_SoundVolume::ResolveClass() const
{
	if (USoundClass* Own = SoundClass.LoadSynchronous())
	{
		return Own;
	}
	if (const TSoftObjectPtr<USoundClass>* Assigned = UMenuProjectSettings::Get().AudioChannels.Find(Channel))
	{
		return Assigned->LoadSynchronous();
	}
	return nullptr;
}

USoundMix* UMenuSettingTarget_SoundVolume::ResolveMix() const
{
	if (USoundMix* Own = SoundMix.LoadSynchronous())
	{
		return Own;
	}
	if (USoundMix* Project = UMenuProjectSettings::Get().SettingsSoundMix.LoadSynchronous())
	{
		return Project;
	}
	return MenuSettingTargetsLocal::GetRuntimeSoundMix();
}

bool UMenuSettingTarget_SoundVolume::IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const
{
	if (Channel == TEXT("Master") || ResolveClass())
	{
		return true;
	}
	OutReason = LOCTEXT("NoSoundClass", "No sound class is assigned to this volume (Project Settings > Plugins > Menus > Audio Channels).");
	return false;
}

void UMenuSettingTarget_SoundVolume::Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const
{
	const float Volume = FMath::Max(0.f, static_cast<float>(MenuSettingValues::ToNumber(Value, 1.0)));
	UWorld* World = Context.GetWorld();
	if (!World || Context.IsPreview())
	{
		return;
	}
	if (USoundClass* Class = ResolveClass())
	{
		USoundMix* Mix = ResolveMix();
		UGameplayStatics::SetSoundMixClassOverride(World, Mix, Class, Volume, 1.f, 0.f, true);
		UGameplayStatics::PushSoundMixModifier(World, Mix);
		return;
	}
	if (Channel == TEXT("Master") && GEngine)
	{
		// Nothing assigned: the audio device's own volume, so a master volume works with no assets.
		FAudioDeviceHandle Device = GEngine->GetMainAudioDevice();
		if (Device.IsValid())
		{
			Device->SetTransientPrimaryVolume(Volume);
		}
	}
}

// =====================================================================================================
//  Built-in
// =====================================================================================================

bool UMenuSettingTarget_BuiltIn::Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const
{
	using namespace MenuSettingValues;
	switch (Field)
	{
	case EMenuBuiltInSetting::DisplayGamma:
		if (!GEngine)
		{
			return false;
		}
		OutValue = FromNumber(GEngine->DisplayGamma);
		return true;
	case EMenuBuiltInSetting::UIScale:
		OutValue = FromNumber(::GetDefault<UUserInterfaceSettings>()->ApplicationScale);
		return true;
	case EMenuBuiltInSetting::ColorBlindMode:
		OutValue = MenuSettingTargetsLocal::ColorBlindNames[FMath::Clamp(static_cast<int32>(MenuSettingTargetsLocal::ColorBlindType), 0, 3)];
		return true;
	case EMenuBuiltInSetting::ColorBlindStrength:
		OutValue = FromNumber(MenuSettingTargetsLocal::ColorBlindSeverity);
		return true;
	case EMenuBuiltInSetting::Subtitles:
		if (!GEngine)
		{
			return false;
		}
		OutValue = FromBool(GEngine->bSubtitlesEnabled);
		return true;
	case EMenuBuiltInSetting::BackgroundVolume:
		OutValue = FromNumber(FApp::GetUnfocusedVolumeMultiplier());
		return true;
	case EMenuBuiltInSetting::Vibration:
	case EMenuBuiltInSetting::VibrationStrength:
		return false; // per controller, set on each; the saved value is the truth
	case EMenuBuiltInSetting::ReduceMotion:
		OutValue = FromBool(MenuAccessibility::IsReduceMotionEnabled());
		return true;
	case EMenuBuiltInSetting::HighContrast:
		OutValue = FromBool(MenuAccessibility::IsHighContrastEnabled());
		return true;
	case EMenuBuiltInSetting::MenuNarration:
		OutValue = FromBool(MenuAccessibility::IsMenuNarrationEnabled());
		return true;
	case EMenuBuiltInSetting::ControllerButtonStyle:
	case EMenuBuiltInSetting::FieldOfView:
		return false;
	default:
		return false;
	}
}

void UMenuSettingTarget_BuiltIn::Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const
{
	using namespace MenuSettingValues;
	if (Context.IsPreview())
	{
		return;
	}
	switch (Field)
	{
	case EMenuBuiltInSetting::DisplayGamma:
		if (GEngine)
		{
			GEngine->DisplayGamma = FMath::Clamp(static_cast<float>(ToNumber(Value, 2.2)), 0.5f, 5.f);
		}
		break;
	case EMenuBuiltInSetting::UIScale:
		GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale = FMath::Clamp(static_cast<float>(ToNumber(Value, 1.0)), 0.25f, 4.f);
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().InvalidateAllWidgets(false);
		}
		break;
	case EMenuBuiltInSetting::ColorBlindMode:
	{
		int32 Index = 0;
		for (int32 Name = 0; Name < UE_ARRAY_COUNT(MenuSettingTargetsLocal::ColorBlindNames); ++Name)
		{
			if (Value.Equals(MenuSettingTargetsLocal::ColorBlindNames[Name], ESearchCase::IgnoreCase))
			{
				Index = Name;
			}
		}
		MenuSettingTargetsLocal::ColorBlindType = static_cast<EColorVisionDeficiency>(Index);
		MenuSettingTargetsLocal::ApplyColorBlindness();
		break;
	}
	case EMenuBuiltInSetting::ColorBlindStrength:
		MenuSettingTargetsLocal::ColorBlindSeverity = FMath::Clamp(static_cast<int32>(ToNumber(Value, 10.0)), 0, 10);
		MenuSettingTargetsLocal::ApplyColorBlindness();
		break;
	case EMenuBuiltInSetting::Subtitles:
		if (GEngine)
		{
			GEngine->bSubtitlesEnabled = ToBool(Value);
		}
		break;
	case EMenuBuiltInSetting::BackgroundVolume:
		FApp::SetUnfocusedVolumeMultiplier(FMath::Clamp(static_cast<float>(ToNumber(Value, 1.0)), 0.f, 1.f));
		break;
	case EMenuBuiltInSetting::Vibration:
		if (APlayerController* Player = Context.GetPlayerController())
		{
			if (FBoolProperty* Property = MenuSettingTargetsLocal::FindControllerProperty<FBoolProperty>(TEXT("bForceFeedbackEnabled")))
			{
				Property->SetPropertyValue_InContainer(Player, ToBool(Value));
			}
		}
		break;
	case EMenuBuiltInSetting::VibrationStrength:
		if (APlayerController* Player = Context.GetPlayerController())
		{
			if (FFloatProperty* Property = MenuSettingTargetsLocal::FindControllerProperty<FFloatProperty>(TEXT("ForceFeedbackScale")))
			{
				Property->SetPropertyValue_InContainer(Player, FMath::Max(0.f, static_cast<float>(ToNumber(Value, 1.0))));
			}
		}
		break;
	case EMenuBuiltInSetting::ReduceMotion:
		MenuAccessibility::SetReduceMotion(ToBool(Value));
		if (UNotificationSubsystem* Notifications = Context.LocalPlayer ? Context.LocalPlayer->GetSubsystem<UNotificationSubsystem>() : nullptr)
		{
			FNotificationUserPreferences Preferences = Notifications->GetNotificationPreferences();
			Preferences.bReduceMotion = ToBool(Value);
			Notifications->SetNotificationPreferences(Preferences);
		}
		break;
	case EMenuBuiltInSetting::HighContrast:
		MenuAccessibility::SetHighContrast(ToBool(Value));
		break;
	case EMenuBuiltInSetting::MenuNarration:
		MenuAccessibility::SetMenuNarration(ToBool(Value));
		break;
	case EMenuBuiltInSetting::ControllerButtonStyle:
		if (UMenuInputSubsystem* Input = Context.LocalPlayer ? Context.LocalPlayer->GetSubsystem<UMenuInputSubsystem>() : nullptr)
		{
			Input->SetGamepadFamilyOverride(Value);
		}
		break;
	case EMenuBuiltInSetting::FieldOfView:
		if (APlayerController* Player = Context.GetPlayerController())
		{
			const float Degrees = FMath::Clamp(static_cast<float>(ToNumber(Value, 90.0)), 20.f, 170.f);
			if (Player->PlayerCameraManager)
			{
				Player->PlayerCameraManager->DefaultFOV = Degrees;
			}
			if (AActor* ViewTarget = Player->GetViewTarget())
			{
				TInlineComponentArray<UCameraComponent*> Cameras(ViewTarget);
				for (UCameraComponent* Camera : Cameras)
				{
					if (Camera && Camera->IsActive())
					{
						Camera->SetFieldOfView(Degrees);
					}
				}
			}
		}
		break;
	default:
		break;
	}
}

bool UMenuSettingTarget_BuiltIn::ReapplyAfterMapLoad() const
{
	// Force feedback lives on the player controller, which a map change replaces; the field of view on the camera, which a new pawn brings.
	return Field == EMenuBuiltInSetting::Vibration || Field == EMenuBuiltInSetting::VibrationStrength || Field == EMenuBuiltInSetting::FieldOfView;
}

// =====================================================================================================
//  Notifications
// =====================================================================================================

bool UMenuSettingTarget_Notifications::IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const
{
	return Context.IsPreview() || (Context.LocalPlayer && Context.LocalPlayer->GetSubsystem<UNotificationSubsystem>());
}

bool UMenuSettingTarget_Notifications::Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const
{
	const UNotificationSubsystem* Notifications = Context.LocalPlayer ? Context.LocalPlayer->GetSubsystem<UNotificationSubsystem>() : nullptr;
	if (!Notifications)
	{
		return false;
	}
	const FNotificationUserPreferences& Preferences = Notifications->GetNotificationPreferences();
	using namespace MenuSettingValues;
	switch (Field)
	{
	case EMenuNotificationPreference::DurationScale: OutValue = FromNumber(Preferences.DurationScale); return true;
	case EMenuNotificationPreference::KeepUntilDismissed: OutValue = FromBool(Preferences.bKeepUntilDismissed); return true;
	case EMenuNotificationPreference::ScreenReader: OutValue = FromBool(Preferences.bScreenReaderAnnouncements); return true;
	case EMenuNotificationPreference::ReduceMotion: OutValue = FromBool(Preferences.bReduceMotion); return true;
	case EMenuNotificationPreference::PlaySounds: OutValue = FromBool(Preferences.bPlaySounds); return true;
	case EMenuNotificationPreference::DoNotDisturb: OutValue = FromBool(Preferences.bDoNotDisturb); return true;
	default: return false;
	}
}

void UMenuSettingTarget_Notifications::Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const
{
	UNotificationSubsystem* Notifications = Context.LocalPlayer ? Context.LocalPlayer->GetSubsystem<UNotificationSubsystem>() : nullptr;
	if (!Notifications)
	{
		return;
	}
	FNotificationUserPreferences Preferences = Notifications->GetNotificationPreferences();
	using namespace MenuSettingValues;
	switch (Field)
	{
	case EMenuNotificationPreference::DurationScale: Preferences.DurationScale = FMath::Clamp(static_cast<float>(ToNumber(Value, 1.0)), 0.25f, 10.f); break;
	case EMenuNotificationPreference::KeepUntilDismissed: Preferences.bKeepUntilDismissed = ToBool(Value); break;
	case EMenuNotificationPreference::ScreenReader: Preferences.bScreenReaderAnnouncements = ToBool(Value); break;
	case EMenuNotificationPreference::ReduceMotion: Preferences.bReduceMotion = ToBool(Value); break;
	case EMenuNotificationPreference::PlaySounds: Preferences.bPlaySounds = ToBool(Value); break;
	case EMenuNotificationPreference::DoNotDisturb: Preferences.bDoNotDisturb = ToBool(Value); break;
	default: return;
	}
	Notifications->SetNotificationPreferences(Preferences);
}

// =====================================================================================================
//  Language
// =====================================================================================================

bool UMenuSettingTarget_Language::Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const
{
	OutValue = FInternationalization::Get().GetCurrentCulture()->GetName();
	return true;
}

void UMenuSettingTarget_Language::Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const
{
	if (!Value.IsEmpty() && !Context.IsPreview() && Value != FInternationalization::Get().GetCurrentCulture()->GetName())
	{
		UKismetInternationalizationLibrary::SetCurrentCulture(Value, true);
	}
}

bool UMenuSettingTarget_Language::GetDefault(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const
{
	TArray<FMenuSettingOption> Available;
	GetOptions(Setting, Context, Available);
	// The system's language when the game has it ("de-AT" -> "de"), else the first.
	for (const FString& Name : FInternationalization::Get().GetPrioritizedCultureNames(FInternationalization::Get().GetDefaultCulture()->GetName()))
	{
		if (UMenuSettingChoice::FindOption(Available, Name) != INDEX_NONE)
		{
			OutValue = Name;
			return true;
		}
	}
	if (Available.Num() > 0)
	{
		OutValue = Available[0].Value;
		return true;
	}
	return false;
}

bool UMenuSettingTarget_Language::GetOptions(const UMenuSetting& Setting, const FMenuSettingContext& Context, TArray<FMenuSettingOption>& OutOptions) const
{
	TArray<FString> Names = FTextLocalizationManager::Get().GetLocalizedCultureNames(ELocalizationLoadFlags::Game);
	if (Cultures.Num() > 0)
	{
		Names = Names.FilterByPredicate([this](const FString& Name) { return Cultures.ContainsByPredicate([&Name](const FString& Wanted) { return Wanted.Equals(Name, ESearchCase::IgnoreCase); }); });
	}
	Names.Sort();
	for (const FString& Name : Names)
	{
		FString Label = Name;
		if (const FCulturePtr Culture = FInternationalization::Get().GetCulture(Name))
		{
			Label = Culture->GetNativeName();
			if (Label.Len() > 0)
			{
				Label[0] = FChar::ToUpper(Label[0]);
			}
		}
		OutOptions.Add(FMenuSettingOption(Name, FText::AsCultureInvariant(Label)));
	}
	return OutOptions.Num() > 0;
}

// =====================================================================================================
//  System info
// =====================================================================================================

bool UMenuSettingTarget_SystemInfo::Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const
{
	switch (Field)
	{
	case EMenuSystemInfo::GraphicsCard:
		OutValue = GRHIAdapterName.IsEmpty() ? FPlatformMisc::GetPrimaryGPUBrand() : GRHIAdapterName;
		return true;
	case EMenuSystemInfo::GraphicsDriver:
	{
		const FGPUDriverInfo Info = FPlatformMisc::GetGPUDriverInfo(GRHIAdapterName);
		OutValue = Info.UserDriverVersion.IsEmpty() ? Info.InternalDriverVersion : Info.UserDriverVersion;
		if (!Info.DriverDate.IsEmpty())
		{
			OutValue += FString::Printf(TEXT(" (%s)"), *Info.DriverDate);
		}
		return !OutValue.IsEmpty();
	}
	case EMenuSystemInfo::Processor:
		OutValue = FPlatformMisc::GetCPUBrand().TrimStartAndEnd();
		return true;
	case EMenuSystemInfo::Memory:
		OutValue = FString::Printf(TEXT("%u GB"), FPlatformMemory::GetConstants().TotalPhysicalGB);
		return true;
	case EMenuSystemInfo::GraphicsAPI:
		OutValue = FApp::GetGraphicsRHI();
		return !OutValue.IsEmpty();
	case EMenuSystemInfo::DisplayResolution:
		if (UGameUserSettings* Settings = MenuSettingTargetsLocal::GetGameUserSettings())
		{
			const FIntPoint Desktop = Settings->GetDesktopResolution();
			OutValue = FString::Printf(TEXT("%d x %d"), Desktop.X, Desktop.Y);
			return true;
		}
		return false;
	case EMenuSystemInfo::GameVersion:
		OutValue = ::GetDefault<UGeneralProjectSettings>()->ProjectVersion;
		return true;
	default:
		return false;
	}
}

// =====================================================================================================
//  Blueprint
// =====================================================================================================

bool UMenuSettingTarget_Blueprint::ReadValue_Implementation(const UMenuSetting* Setting, APlayerController* Player, FString& OutValue) const
{
	return false;
}

bool UMenuSettingTarget_Blueprint::CanBeUsed_Implementation(const UMenuSetting* Setting, APlayerController* Player, FText& Reason) const
{
	return true;
}

bool UMenuSettingTarget_Blueprint::IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const
{
	return CanBeUsed(&Setting, Context.GetPlayerController(), OutReason);
}

bool UMenuSettingTarget_Blueprint::Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const
{
	return ReadValue(&Setting, Context.GetPlayerController(), OutValue);
}

void UMenuSettingTarget_Blueprint::Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const
{
	if (!Context.IsPreview())
	{
		const_cast<UMenuSettingTarget_Blueprint*>(this)->WriteValue(&Setting, Context.GetPlayerController(), Value);
	}
}

#undef LOCTEXT_NAMESPACE
