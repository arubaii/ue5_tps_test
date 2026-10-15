// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuProjectSettings.h"

#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "MenuProjectSettings"

namespace MenuProjectSettingsLocal
{
	FMenuGraphicsFeatureMode MakeMode(const TCHAR* Id, const FText& Name, float ScreenPercentage, const FText& Description,
		const TArray<FMenuCVarAssignment>& Variables = {})
	{
		FMenuGraphicsFeatureMode Mode;
		Mode.Id = Id;
		Mode.DisplayName = Name;
		Mode.Description = Description;
		Mode.ScreenPercentage = ScreenPercentage;
		Mode.Variables = Variables;
		return Mode;
	}

	/** The usual five upscaling modes (per-axis ratio 1.0 / 1.5 / 1.7 / 2.0 / 3.0), each optionally with a quality cvar value. */
	void AddStandardModes(FMenuGraphicsFeature& Feature, const FString& QualityVariable, const TArray<FString>& QualityValues,
		bool bNativeFirst, const TCHAR* NativeId, const FText& NativeName)
	{
		struct FRow { const TCHAR* Id; FText Name; float Percentage; FText Description; };
		const FRow Rows[] = {
			{ NativeId, NativeName, 100.f, LOCTEXT("ModeNative", "Renders at full resolution and uses the upscaler's anti-aliasing only: the sharpest image, and the most expensive.") },
			{ TEXT("Quality"), LOCTEXT("ModeQuality", "Quality"), 66.67f, LOCTEXT("ModeQualityDesc", "Renders at two thirds of the resolution: close to native, noticeably faster.") },
			{ TEXT("Balanced"), LOCTEXT("ModeBalanced", "Balanced"), 58.f, LOCTEXT("ModeBalancedDesc", "A middle ground between image quality and frame rate.") },
			{ TEXT("Performance"), LOCTEXT("ModePerformance", "Performance"), 50.f, LOCTEXT("ModePerformanceDesc", "Renders at half the resolution: a large frame rate gain, softer image. Good for 4K.") },
			{ TEXT("UltraPerformance"), LOCTEXT("ModeUltraPerformance", "Ultra Performance"), 33.33f, LOCTEXT("ModeUltraPerformanceDesc", "Renders at a third of the resolution: for 8K output, or the slowest machines.") },
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Rows); ++Index)
		{
			if (!bNativeFirst && Index == 0)
			{
				continue;
			}
			TArray<FMenuCVarAssignment> Variables;
			if (!QualityVariable.IsEmpty() && QualityValues.IsValidIndex(Index))
			{
				Variables.Add(FMenuCVarAssignment(QualityVariable, QualityValues[Index]));
			}
			Feature.Modes.Add(MakeMode(Rows[Index].Id, Rows[Index].Name, Rows[Index].Percentage, Rows[Index].Description, Variables));
		}
	}
}

UMenuProjectSettings::UMenuProjectSettings()
{
	MakeDefaultGraphicsFeatures(GraphicsFeatures);

	ReservedKeys.Add(EKeys::Escape);
	ReservedKeys.Add(EKeys::Gamepad_Special_Right);

	GamepadHardware.Add(TEXT("XInput"), EMenuGamepadFamily::Xbox);
	GamepadHardware.Add(TEXT("Xbox"), EMenuGamepadFamily::Xbox);
	GamepadHardware.Add(TEXT("DualSense"), EMenuGamepadFamily::PlayStation);
	GamepadHardware.Add(TEXT("DualShock"), EMenuGamepadFamily::PlayStation);
	GamepadHardware.Add(TEXT("PS4"), EMenuGamepadFamily::PlayStation);
	GamepadHardware.Add(TEXT("PS5"), EMenuGamepadFamily::PlayStation);
	GamepadHardware.Add(TEXT("PlayStation"), EMenuGamepadFamily::PlayStation);
	GamepadHardware.Add(TEXT("Switch"), EMenuGamepadFamily::Switch);
	GamepadHardware.Add(TEXT("SteamDeck"), EMenuGamepadFamily::SteamDeck);
	GamepadHardware.Add(TEXT("Steam Deck"), EMenuGamepadFamily::SteamDeck);

	for (const TCHAR* Channel : { TEXT("Master"), TEXT("Music"), TEXT("Effects"), TEXT("Dialogue"), TEXT("UI"), TEXT("Voice") })
	{
		AudioChannels.Add(Channel, TSoftObjectPtr<USoundClass>());
	}
}

const FString* UMenuProjectSettings::FindDefaultOverride(FName SettingId) const
{
	for (const FMenuSettingValue& Entry : DefaultValues)
	{
		if (Entry.Id == SettingId)
		{
			return &Entry.Value;
		}
	}
	return nullptr;
}

void UMenuProjectSettings::MakeDefaultGraphicsFeatures(TArray<FMenuGraphicsFeature>& OutFeatures)
{
	using namespace MenuProjectSettingsLocal;
	OutFeatures.Reset();

	// ---- Upscalers ------------------------------------------------------------------------------------------------

	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("DLSS");
		Feature.DisplayName = LOCTEXT("DLSS", "NVIDIA DLSS");
		Feature.Description = LOCTEXT("DLSSDesc", "Deep Learning Super Sampling: AI upscaling on NVIDIA RTX graphics cards. The best image for the frame rate on RTX. DLAA renders at full resolution for the best anti-aliasing.");
		Feature.Vendor = EMenuGPUVendor::NVIDIA;
		Feature.PreferredOn = EMenuGPUVendor::NVIDIA;
		Feature.DetectVariable = TEXT("r.NGX.DLSS.Enable");
		Feature.SupportFunction = TEXT("/Script/DLSSBlueprint.DLSSLibrary:IsDLSSSupported");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.AntiAliasingMethod"), TEXT("4")), FMenuCVarAssignment(TEXT("r.NGX.DLSS.Enable"), TEXT("1")) };
		Feature.DisableVariables = { FMenuCVarAssignment(TEXT("r.NGX.DLSS.Enable"), TEXT("0")) };
		AddStandardModes(Feature, FString(), {}, true, TEXT("DLAA"), LOCTEXT("ModeDLAA", "DLAA"));
		Feature.DefaultMode = TEXT("Quality");
		Feature.AutoPriority = 100;
		OutFeatures.Add(Feature);
	}
	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("FSR");
		Feature.DisplayName = LOCTEXT("FSR", "AMD FidelityFX Super Resolution");
		Feature.Description = LOCTEXT("FSRDesc", "AMD FSR: high quality upscaling that runs on most graphics cards. Native AA renders at full resolution for anti-aliasing only.");
		Feature.PreferredOn = EMenuGPUVendor::AMD;
		Feature.DetectVariable = TEXT("r.FidelityFX.FSR3.Enabled");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.AntiAliasingMethod"), TEXT("4")), FMenuCVarAssignment(TEXT("r.FidelityFX.FSR3.Enabled"), TEXT("1")) };
		Feature.DisableVariables = { FMenuCVarAssignment(TEXT("r.FidelityFX.FSR3.Enabled"), TEXT("0")) };
		AddStandardModes(Feature, TEXT("r.FidelityFX.FSR3.QualityMode"), { TEXT("0"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4") }, true, TEXT("NativeAA"), LOCTEXT("ModeNativeAA", "Native AA"));
		Feature.DefaultMode = TEXT("Quality");
		Feature.SharpnessVariable = TEXT("r.FidelityFX.FSR3.Sharpness");
		Feature.AutoPriority = 80;
		OutFeatures.Add(Feature);
	}
	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("XeSS");
		Feature.DisplayName = LOCTEXT("XeSS", "Intel XeSS");
		Feature.Description = LOCTEXT("XeSSDesc", "Intel Xe Super Sampling: AI upscaling, fastest on Intel Arc graphics cards and supported on most others.");
		Feature.PreferredOn = EMenuGPUVendor::Intel;
		Feature.DetectVariable = TEXT("r.XeSS.Enabled");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.AntiAliasingMethod"), TEXT("4")), FMenuCVarAssignment(TEXT("r.XeSS.Enabled"), TEXT("1")) };
		Feature.DisableVariables = { FMenuCVarAssignment(TEXT("r.XeSS.Enabled"), TEXT("0")) };
		// XeSS's own ratios (XeSS 1.3+): Native AA 1.0, Ultra Quality Plus 1.3, Ultra Quality 1.5, Quality 1.7, Balanced 2.0, Performance 2.3, Ultra Performance 3.0.
		const FString Quality = TEXT("r.XeSS.Quality");
		Feature.Modes.Add(MakeMode(TEXT("NativeAA"), LOCTEXT("ModeNativeAA", "Native AA"), 100.f, LOCTEXT("XeSSNativeDesc", "Full resolution, XeSS anti-aliasing only."), { FMenuCVarAssignment(Quality, TEXT("6")) }));
		Feature.Modes.Add(MakeMode(TEXT("UltraQualityPlus"), LOCTEXT("ModeUltraQualityPlus", "Ultra Quality Plus"), 76.9f, LOCTEXT("XeSSUQPDesc", "Very close to native."), { FMenuCVarAssignment(Quality, TEXT("5")) }));
		Feature.Modes.Add(MakeMode(TEXT("UltraQuality"), LOCTEXT("ModeUltraQuality", "Ultra Quality"), 66.67f, FText::GetEmpty(), { FMenuCVarAssignment(Quality, TEXT("4")) }));
		Feature.Modes.Add(MakeMode(TEXT("Quality"), LOCTEXT("ModeQuality", "Quality"), 58.8f, FText::GetEmpty(), { FMenuCVarAssignment(Quality, TEXT("3")) }));
		Feature.Modes.Add(MakeMode(TEXT("Balanced"), LOCTEXT("ModeBalanced", "Balanced"), 50.f, FText::GetEmpty(), { FMenuCVarAssignment(Quality, TEXT("2")) }));
		Feature.Modes.Add(MakeMode(TEXT("Performance"), LOCTEXT("ModePerformance", "Performance"), 43.5f, FText::GetEmpty(), { FMenuCVarAssignment(Quality, TEXT("1")) }));
		Feature.Modes.Add(MakeMode(TEXT("UltraPerformance"), LOCTEXT("ModeUltraPerformance", "Ultra Performance"), 33.33f, FText::GetEmpty(), { FMenuCVarAssignment(Quality, TEXT("0")) }));
		Feature.DefaultMode = TEXT("Quality");
		Feature.AutoPriority = 70;
		OutFeatures.Add(Feature);
	}
	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("TSR");
		Feature.DisplayName = LOCTEXT("TSR", "Temporal Super Resolution (TSR)");
		Feature.Description = LOCTEXT("TSRDesc", "Unreal Engine's own upscaler: runs on every graphics card. Custom leaves the resolution to Resolution Scale.");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.AntiAliasingMethod"), TEXT("4")) };
		AddStandardModes(Feature, FString(), {}, true, TEXT("NativeAA"), LOCTEXT("ModeNativeAA", "Native AA"));
		Feature.Modes.Add(MakeMode(TEXT("Custom"), LOCTEXT("ModeCustom", "Custom"), 0.f, LOCTEXT("ModeCustomDesc", "Renders at the Resolution Scale you set.")));
		Feature.DefaultMode = TEXT("Quality");
		Feature.AutoPriority = 50;
		OutFeatures.Add(Feature);
	}
	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("TAA");
		Feature.DisplayName = LOCTEXT("TAA", "Temporal Anti-Aliasing (TAA)");
		Feature.Description = LOCTEXT("TAADesc", "Smooth edges at low cost; can look soft in motion. Resolution follows Resolution Scale.");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.AntiAliasingMethod"), TEXT("2")) };
		Feature.AutoPriority = 20;
		OutFeatures.Add(Feature);
	}
	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("FXAA");
		Feature.DisplayName = LOCTEXT("FXAA", "FXAA");
		Feature.Description = LOCTEXT("FXAADesc", "Fast approximate anti-aliasing: the cheapest, sharp but with some shimmering edges.");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.AntiAliasingMethod"), TEXT("1")) };
		Feature.AutoPriority = 10;
		OutFeatures.Add(Feature);
	}
	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("Off");
		Feature.DisplayName = LOCTEXT("AAOff", "Off");
		Feature.Description = LOCTEXT("AAOffDesc", "No anti-aliasing: jagged edges, the cheapest.");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.AntiAliasingMethod"), TEXT("0")) };
		Feature.AutoPriority = -1;
		OutFeatures.Add(Feature);
	}

	// ---- Frame generation -----------------------------------------------------------------------------------------

	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("DLSSG");
		Feature.Kind = EMenuGraphicsFeatureKind::FrameGeneration;
		Feature.DisplayName = LOCTEXT("DLSSG", "DLSS Frame Generation");
		Feature.Description = LOCTEXT("DLSSGDesc", "Generates frames between rendered ones on NVIDIA RTX 40 series and newer: much smoother motion. Best above 60 fps; turns VSync's role over to the driver.");
		Feature.Vendor = EMenuGPUVendor::NVIDIA;
		Feature.DetectVariable = TEXT("r.Streamline.DLSSG.Enable");
		Feature.SupportFunction = TEXT("/Script/StreamlineBlueprint.StreamlineLibraryDLSSG:IsDLSSGSupported");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.Streamline.DLSSG.Enable"), TEXT("1")) };
		Feature.DisableVariables = { FMenuCVarAssignment(TEXT("r.Streamline.DLSSG.Enable"), TEXT("0")) };
		Feature.AutoPriority = 100;
		OutFeatures.Add(Feature);
	}
	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("FSRFG");
		Feature.Kind = EMenuGraphicsFeatureKind::FrameGeneration;
		Feature.DisplayName = LOCTEXT("FSRFG", "AMD FSR Frame Generation");
		Feature.Description = LOCTEXT("FSRFGDesc", "Generates frames between rendered ones (needs FSR as the upscaler). Best above 60 fps.");
		Feature.DetectVariable = TEXT("r.FidelityFX.FI.Enabled");
		Feature.RequiresFeature = TEXT("FSR");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.FidelityFX.FI.Enabled"), TEXT("1")) };
		Feature.DisableVariables = { FMenuCVarAssignment(TEXT("r.FidelityFX.FI.Enabled"), TEXT("0")) };
		Feature.AutoPriority = 80;
		OutFeatures.Add(Feature);
	}
	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("XeFG");
		Feature.Kind = EMenuGraphicsFeatureKind::FrameGeneration;
		Feature.DisplayName = LOCTEXT("XeFG", "Intel XeSS Frame Generation");
		Feature.Description = LOCTEXT("XeFGDesc", "Generates frames between rendered ones with Intel XeSS 2.");
		Feature.DetectVariable = TEXT("r.XeFG.Enabled");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.XeFG.Enabled"), TEXT("1")) };
		Feature.DisableVariables = { FMenuCVarAssignment(TEXT("r.XeFG.Enabled"), TEXT("0")) };
		Feature.AutoPriority = 70;
		OutFeatures.Add(Feature);
	}

	// ---- Latency reduction ----------------------------------------------------------------------------------------

	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("Reflex");
		Feature.Kind = EMenuGraphicsFeatureKind::LatencyReduction;
		Feature.DisplayName = LOCTEXT("Reflex", "NVIDIA Reflex");
		Feature.Description = LOCTEXT("ReflexDesc", "Lowers the time between your input and the screen on NVIDIA graphics cards. Boost also keeps the GPU clocks up, using more power.");
		Feature.Vendor = EMenuGPUVendor::NVIDIA;
		Feature.DetectVariable = TEXT("t.Streamline.Reflex.Enable");
		Feature.SupportFunction = TEXT("/Script/StreamlineBlueprint.StreamlineLibraryReflex:IsReflexSupported");
		Feature.DisableVariables = { FMenuCVarAssignment(TEXT("t.Streamline.Reflex.Enable"), TEXT("0")) };
		Feature.Modes.Add(MakeMode(TEXT("On"), LOCTEXT("ReflexOn", "NVIDIA Reflex: On"), 0.f, FText::GetEmpty(),
			{ FMenuCVarAssignment(TEXT("t.Streamline.Reflex.Enable"), TEXT("1")), FMenuCVarAssignment(TEXT("t.Streamline.Reflex.Mode"), TEXT("1")) }));
		Feature.Modes.Add(MakeMode(TEXT("Boost"), LOCTEXT("ReflexBoost", "NVIDIA Reflex: On + Boost"), 0.f, FText::GetEmpty(),
			{ FMenuCVarAssignment(TEXT("t.Streamline.Reflex.Enable"), TEXT("1")), FMenuCVarAssignment(TEXT("t.Streamline.Reflex.Mode"), TEXT("2")) }));
		Feature.AutoPriority = 100;
		OutFeatures.Add(Feature);
	}
	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("AntiLag2");
		Feature.Kind = EMenuGraphicsFeatureKind::LatencyReduction;
		Feature.DisplayName = LOCTEXT("AntiLag2", "AMD Anti-Lag 2");
		Feature.Description = LOCTEXT("AntiLag2Desc", "Lowers input latency on AMD Radeon graphics cards.");
		Feature.Vendor = EMenuGPUVendor::AMD;
		Feature.DetectVariable = TEXT("r.FidelityFX.AntiLag2.Enabled");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.FidelityFX.AntiLag2.Enabled"), TEXT("1")) };
		Feature.DisableVariables = { FMenuCVarAssignment(TEXT("r.FidelityFX.AntiLag2.Enabled"), TEXT("0")) };
		Feature.AutoPriority = 90;
		OutFeatures.Add(Feature);
	}
	{
		FMenuGraphicsFeature Feature;
		Feature.Id = TEXT("XeLL");
		Feature.Kind = EMenuGraphicsFeatureKind::LatencyReduction;
		Feature.DisplayName = LOCTEXT("XeLL", "Intel XeLL");
		Feature.Description = LOCTEXT("XeLLDesc", "Xe Low Latency: lowers input latency on Intel Arc graphics cards.");
		Feature.Vendor = EMenuGPUVendor::Intel;
		Feature.DetectVariable = TEXT("r.XeLL.Enabled");
		Feature.EnableVariables = { FMenuCVarAssignment(TEXT("r.XeLL.Enabled"), TEXT("1")) };
		Feature.DisableVariables = { FMenuCVarAssignment(TEXT("r.XeLL.Enabled"), TEXT("0")) };
		Feature.AutoPriority = 80;
		OutFeatures.Add(Feature);
	}
}

#undef LOCTEXT_NAMESPACE
