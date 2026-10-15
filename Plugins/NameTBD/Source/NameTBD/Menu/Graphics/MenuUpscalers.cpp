// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuUpscalers.h"

#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "UObject/UObjectGlobals.h"
#include "NameTBD/Menu/Settings/MenuProjectSettings.h"

#define LOCTEXT_NAMESPACE "MenuUpscalers"

namespace MenuUpscalersLocal
{
	/** What was made active, per kind (the engine has no single place that says "DLSS is on"). */
	TMap<EMenuGraphicsFeatureKind, FName>& ActiveFeatures()
	{
		static TMap<EMenuGraphicsFeatureKind, FName> Active;
		return Active;
	}

	TMap<EMenuGraphicsFeatureKind, FName>& ActiveModes()
	{
		static TMap<EMenuGraphicsFeatureKind, FName> Active;
		return Active;
	}

	/** Support answers, asked once each (the graphics card doesn't change while the game runs). */
	TMap<FName, bool>& SupportCache()
	{
		static TMap<FName, bool> Cache;
		return Cache;
	}

	/** Calls a static "bool Function()" by "/Script/Module.Class:Function" through reflection. False in OutFound when it isn't there. */
	bool CallSupportFunction(const FString& Spec, bool& bOutFound)
	{
		bOutFound = false;
		FString ClassPath;
		FString FunctionName;
		if (!Spec.Split(TEXT(":"), &ClassPath, &FunctionName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			return false;
		}
		UClass* Class = FindObject<UClass>(nullptr, *ClassPath);
		UFunction* Function = Class ? Class->FindFunctionByName(FName(*FunctionName)) : nullptr;
		if (!Function || !Function->HasAnyFunctionFlags(FUNC_Static) || Function->NumParms != 1)
		{
			return false;
		}
		const FBoolProperty* Return = CastField<FBoolProperty>(Function->GetReturnProperty());
		if (!Return)
		{
			return false;
		}
		bOutFound = true;
		uint8* Params = static_cast<uint8*>(FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment()));
		FMemory::Memzero(Params, Function->ParmsSize);
		Class->GetDefaultObject()->ProcessEvent(Function, Params);
		return Return->GetPropertyValue_InContainer(Params);
	}

	void ApplyAssignments(const TArray<FMenuCVarAssignment>& Assignments)
	{
		for (const FMenuCVarAssignment& Assignment : Assignments)
		{
			UMenuGraphicsLibrary::SetConsoleVariable(Assignment.Name, Assignment.Value);
		}
	}

	FText VendorName(EMenuGPUVendor Vendor)
	{
		switch (Vendor)
		{
		case EMenuGPUVendor::NVIDIA: return LOCTEXT("NVIDIA", "NVIDIA");
		case EMenuGPUVendor::AMD: return LOCTEXT("AMD", "AMD");
		case EMenuGPUVendor::Intel: return LOCTEXT("Intel", "Intel");
		default: return FText::GetEmpty();
		}
	}

	/** The upscaler in effect, or (before any is applied - the designer) the one Auto would pick. */
	const FMenuGraphicsFeature* CurrentUpscaler()
	{
		const FName Active = UMenuGraphicsLibrary::GetActiveGraphicsFeature(EMenuGraphicsFeatureKind::Upscaler);
		return UMenuGraphicsLibrary::FindFeature(Active.IsNone() ? UMenuGraphicsLibrary::GetRecommendedUpscaler() : Active);
	}

	const FMenuGraphicsFeatureMode* CurrentUpscalerMode()
	{
		const FMenuGraphicsFeature* Upscaler = CurrentUpscaler();
		if (!Upscaler)
		{
			return nullptr;
		}
		const FName Mode = UMenuGraphicsLibrary::GetActiveGraphicsMode(EMenuGraphicsFeatureKind::Upscaler);
		const FMenuGraphicsFeatureMode* Found = Upscaler->FindMode(Mode);
		return Found ? Found : Upscaler->FindMode(Upscaler->GetDefaultModeId());
	}
}

// =====================================================================================================
//  Feature rows
// =====================================================================================================

const FMenuGraphicsFeatureMode* FMenuGraphicsFeature::FindMode(FName ModeId) const
{
	return Modes.FindByPredicate([ModeId](const FMenuGraphicsFeatureMode& Mode) { return Mode.Id == ModeId; });
}

FName FMenuGraphicsFeature::GetDefaultModeId() const
{
	if (!DefaultMode.IsNone() && FindMode(DefaultMode))
	{
		return DefaultMode;
	}
	return Modes.Num() > 0 ? Modes[0].Id : NAME_None;
}

// =====================================================================================================
//  Library
// =====================================================================================================

EMenuGPUVendor UMenuGraphicsLibrary::GetGPUVendor()
{
	switch (GRHIVendorId)
	{
	case 0x10DE: return EMenuGPUVendor::NVIDIA;
	case 0x1002: return EMenuGPUVendor::AMD;
	case 0x8086: return EMenuGPUVendor::Intel;
	default: return EMenuGPUVendor::Any;
	}
}

FString UMenuGraphicsLibrary::GetGPUName()
{
	return GRHIAdapterName.IsEmpty() ? FPlatformMisc::GetPrimaryGPUBrand() : GRHIAdapterName;
}

const FMenuGraphicsFeature* UMenuGraphicsLibrary::FindFeature(FName FeatureId)
{
	if (FeatureId.IsNone())
	{
		return nullptr;
	}
	return UMenuProjectSettings::Get().GraphicsFeatures.FindByPredicate([FeatureId](const FMenuGraphicsFeature& Feature) { return Feature.Id == FeatureId; });
}

FMenuGraphicsFeatureStatus UMenuGraphicsLibrary::GetStatus(const FMenuGraphicsFeature& Feature)
{
	using namespace MenuUpscalersLocal;
	FMenuGraphicsFeatureStatus Status;
	Status.Id = Feature.Id;
	Status.DisplayName = Feature.DisplayName;
	Status.Kind = Feature.Kind;
	Status.bInstalled = Feature.DetectVariable.IsEmpty() || IConsoleManager::Get().FindConsoleVariable(*Feature.DetectVariable) != nullptr;
	if (!Status.bInstalled)
	{
		Status.Reason = FText::Format(LOCTEXT("NotInstalled", "{0} isn't part of this build of the game."), Feature.DisplayName);
		return Status;
	}
	const EMenuGPUVendor Vendor = GetGPUVendor();
	if (Feature.Vendor != EMenuGPUVendor::Any && Vendor != Feature.Vendor)
	{
		Status.Reason = FText::Format(LOCTEXT("WrongVendor", "Needs an {0} graphics card."), VendorName(Feature.Vendor));
		return Status;
	}
	if (!Feature.SupportFunction.IsEmpty())
	{
		bool* Cached = SupportCache().Find(Feature.Id);
		if (!Cached)
		{
			bool bFound = false;
			const bool bSupported = CallSupportFunction(Feature.SupportFunction, bFound);
			// A support function that can't be found (a renamed library) doesn't hide the feature: the vendor check stands.
			Cached = &SupportCache().Add(Feature.Id, bFound ? bSupported : true);
		}
		if (!*Cached)
		{
			Status.Reason = FText::Format(LOCTEXT("Unsupported", "This graphics card or driver doesn't support {0}."), Feature.DisplayName);
			return Status;
		}
	}
	Status.bSupported = true;
	return Status;
}

TArray<FMenuGraphicsFeatureStatus> UMenuGraphicsLibrary::GetGraphicsFeatures()
{
	TArray<FMenuGraphicsFeatureStatus> Out;
	for (const FMenuGraphicsFeature& Feature : UMenuProjectSettings::Get().GraphicsFeatures)
	{
		Out.Add(GetStatus(Feature));
	}
	return Out;
}

bool UMenuGraphicsLibrary::IsGraphicsFeatureSupported(FName FeatureId)
{
	const FMenuGraphicsFeature* Feature = FindFeature(FeatureId);
	return Feature && GetStatus(*Feature).bSupported;
}

FName UMenuGraphicsLibrary::GetRecommendedUpscaler()
{
	const EMenuGPUVendor Vendor = GetGPUVendor();
	FName Best;
	int32 BestScore = TNumericLimits<int32>::Lowest();
	for (const FMenuGraphicsFeature& Feature : UMenuProjectSettings::Get().GraphicsFeatures)
	{
		if (Feature.Kind != EMenuGraphicsFeatureKind::Upscaler || Feature.AutoPriority < 0 || !GetStatus(Feature).bSupported)
		{
			continue;
		}
		const int32 Score = Feature.AutoPriority + (Feature.PreferredOn != EMenuGPUVendor::Any && Feature.PreferredOn == Vendor ? 50 : 0);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Feature.Id;
		}
	}
	return Best;
}

bool UMenuGraphicsLibrary::SetConsoleVariable(const FString& Name, const FString& Value)
{
	if (IConsoleVariable* Variable = Name.IsEmpty() ? nullptr : IConsoleManager::Get().FindConsoleVariable(*Name))
	{
		Variable->Set(*Value, ECVF_SetByGameSetting);
		return true;
	}
	return false;
}

bool UMenuGraphicsLibrary::ApplyGraphicsFeature(EMenuGraphicsFeatureKind Kind, FName FeatureId, FName ModeId)
{
	using namespace MenuUpscalersLocal;
	const FMenuGraphicsFeature* Feature = FeatureId == TEXT("Off") && Kind != EMenuGraphicsFeatureKind::Upscaler ? nullptr : FindFeature(FeatureId);
	if (Feature && (Feature->Kind != Kind || !GetStatus(*Feature).bSupported))
	{
		UE_LOG(LogNameTBDSettings, Warning, TEXT("Graphics feature %s can't be used here: %s"), *FeatureId.ToString(), *GetStatus(*Feature).Reason.ToString());
		return false;
	}

	// Everything else of this kind off first, so two upscalers are never on together.
	for (const FMenuGraphicsFeature& Other : UMenuProjectSettings::Get().GraphicsFeatures)
	{
		if (Other.Kind == Kind && &Other != Feature)
		{
			ApplyAssignments(Other.DisableVariables);
		}
	}

	FName AppliedMode;
	if (Feature)
	{
		ApplyAssignments(Feature->EnableVariables);
		const FMenuGraphicsFeatureMode* Mode = Feature->FindMode(ModeId);
		if (!Mode)
		{
			Mode = Feature->FindMode(Feature->GetDefaultModeId());
		}
		if (Mode)
		{
			ApplyAssignments(Mode->Variables);
			if (Mode->ScreenPercentage > 0.f)
			{
				SetConsoleVariable(TEXT("r.ScreenPercentage"), MenuSettingValues::FromNumber(Mode->ScreenPercentage));
			}
			AppliedMode = Mode->Id;
		}
	}
	ActiveFeatures().Add(Kind, Feature ? Feature->Id : NAME_None);
	ActiveModes().Add(Kind, AppliedMode);
	UE_LOG(LogNameTBDSettings, Verbose, TEXT("Graphics %s: %s %s"), *UEnum::GetValueAsString(Kind), Feature ? *Feature->Id.ToString() : TEXT("Off"), *AppliedMode.ToString());
	return true;
}

FName UMenuGraphicsLibrary::GetActiveGraphicsFeature(EMenuGraphicsFeatureKind Kind)
{
	const FName* Found = MenuUpscalersLocal::ActiveFeatures().Find(Kind);
	return Found ? *Found : NAME_None;
}

FName UMenuGraphicsLibrary::GetActiveGraphicsMode(EMenuGraphicsFeatureKind Kind)
{
	const FName* Found = MenuUpscalersLocal::ActiveModes().Find(Kind);
	return Found ? *Found : NAME_None;
}

bool UMenuGraphicsLibrary::SetUpscalerSharpness(float Value)
{
	const FMenuGraphicsFeature* Upscaler = MenuUpscalersLocal::CurrentUpscaler();
	return Upscaler && SetConsoleVariable(Upscaler->SharpnessVariable, MenuSettingValues::FromNumber(FMath::Clamp(Value, 0.f, 1.f)));
}

// =====================================================================================================
//  Setting target
// =====================================================================================================

bool UMenuSettingTarget_GraphicsFeature::IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const
{
	using namespace MenuUpscalersLocal;
	switch (Field)
	{
	case EMenuGraphicsFeatureField::Upscaler:
		return true;
	case EMenuGraphicsFeatureField::UpscalerMode:
	{
		const FMenuGraphicsFeature* Upscaler = CurrentUpscaler();
		if (!Upscaler || Upscaler->Modes.Num() == 0)
		{
			OutReason = LOCTEXT("NoModes", "This anti-aliasing method has no quality modes.");
			return false;
		}
		return true;
	}
	case EMenuGraphicsFeatureField::FrameGeneration:
	case EMenuGraphicsFeatureField::LatencyReduction:
	{
		TArray<FMenuSettingOption> Options;
		GetOptions(Setting, Context, Options);
		if (Options.Num() <= 1)
		{
			OutReason = Field == EMenuGraphicsFeatureField::FrameGeneration
				? LOCTEXT("NoFrameGeneration", "No frame generation is available for this graphics card.")
				: LOCTEXT("NoLatency", "No latency reduction is available for this graphics card.");
			return false;
		}
		return true;
	}
	case EMenuGraphicsFeatureField::Sharpness:
	{
		const FMenuGraphicsFeature* Upscaler = CurrentUpscaler();
		if (!Upscaler || Upscaler->SharpnessVariable.IsEmpty() || !IConsoleManager::Get().FindConsoleVariable(*Upscaler->SharpnessVariable))
		{
			OutReason = LOCTEXT("NoSharpness", "This upscaler has no sharpening.");
			return false;
		}
		return true;
	}
	case EMenuGraphicsFeatureField::ResolutionScale:
	{
		const FMenuGraphicsFeatureMode* Mode = CurrentUpscalerMode();
		if (Mode && Mode->ScreenPercentage > 0.f)
		{
			OutReason = LOCTEXT("ScaleByMode", "Set by the upscaling quality. Pick Custom (TSR), TAA or FXAA to set it yourself.");
			return false;
		}
		return true;
	}
	default:
		return true;
	}
}

bool UMenuSettingTarget_GraphicsFeature::GetOptions(const UMenuSetting& Setting, const FMenuSettingContext& Context, TArray<FMenuSettingOption>& OutOptions) const
{
	using namespace MenuUpscalersLocal;
	const UMenuProjectSettings& Settings = UMenuProjectSettings::Get();
	switch (Field)
	{
	case EMenuGraphicsFeatureField::Upscaler:
	{
		const FMenuGraphicsFeature* Recommended = UMenuGraphicsLibrary::FindFeature(UMenuGraphicsLibrary::GetRecommendedUpscaler());
		OutOptions.Add(FMenuSettingOption(TEXT("Auto"),
			Recommended ? FText::Format(LOCTEXT("AutoWith", "Auto ({0})"), Recommended->DisplayName) : LOCTEXT("Auto", "Auto"),
			LOCTEXT("AutoDesc", "The best upscaler for your graphics card.")));
		for (const FMenuGraphicsFeature& Feature : Settings.GraphicsFeatures)
		{
			if (Feature.Kind == EMenuGraphicsFeatureKind::Upscaler && UMenuGraphicsLibrary::GetStatus(Feature).bSupported)
			{
				OutOptions.Add(FMenuSettingOption(Feature.Id.ToString(), Feature.DisplayName, Feature.Description));
			}
		}
		return true;
	}
	case EMenuGraphicsFeatureField::UpscalerMode:
		if (const FMenuGraphicsFeature* Upscaler = CurrentUpscaler())
		{
			for (const FMenuGraphicsFeatureMode& Mode : Upscaler->Modes)
			{
				OutOptions.Add(FMenuSettingOption(Mode.Id.ToString(), Mode.DisplayName, Mode.Description));
			}
		}
		return true;
	case EMenuGraphicsFeatureField::FrameGeneration:
	{
		const FName ActiveUpscaler = CurrentUpscaler() ? CurrentUpscaler()->Id : NAME_None;
		OutOptions.Add(FMenuSettingOption(TEXT("Off"), LOCTEXT("Off", "Off")));
		for (const FMenuGraphicsFeature& Feature : Settings.GraphicsFeatures)
		{
			if (Feature.Kind == EMenuGraphicsFeatureKind::FrameGeneration && UMenuGraphicsLibrary::GetStatus(Feature).bSupported
				&& (Feature.RequiresFeature.IsNone() || Feature.RequiresFeature == ActiveUpscaler))
			{
				OutOptions.Add(FMenuSettingOption(Feature.Id.ToString(), Feature.DisplayName, Feature.Description));
			}
		}
		return true;
	}
	case EMenuGraphicsFeatureField::LatencyReduction:
		OutOptions.Add(FMenuSettingOption(TEXT("Off"), LOCTEXT("Off", "Off")));
		for (const FMenuGraphicsFeature& Feature : Settings.GraphicsFeatures)
		{
			if (Feature.Kind != EMenuGraphicsFeatureKind::LatencyReduction || !UMenuGraphicsLibrary::GetStatus(Feature).bSupported)
			{
				continue;
			}
			if (Feature.Modes.Num() == 0)
			{
				OutOptions.Add(FMenuSettingOption(Feature.Id.ToString(), Feature.DisplayName, Feature.Description));
			}
			for (const FMenuGraphicsFeatureMode& Mode : Feature.Modes)
			{
				OutOptions.Add(FMenuSettingOption(FString::Printf(TEXT("%s.%s"), *Feature.Id.ToString(), *Mode.Id.ToString()),
					Mode.DisplayName.IsEmpty() ? Feature.DisplayName : Mode.DisplayName, Mode.Description.IsEmpty() ? Feature.Description : Mode.Description));
			}
		}
		return true;
	default:
		return false;
	}
}

void UMenuSettingTarget_GraphicsFeature::Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const
{
	using namespace MenuUpscalersLocal;
	if (Context.IsPreview())
	{
		return;
	}
	switch (Field)
	{
	case EMenuGraphicsFeatureField::Upscaler:
	{
		const FName Chosen = Value.Equals(TEXT("Auto"), ESearchCase::IgnoreCase) ? UMenuGraphicsLibrary::GetRecommendedUpscaler() : FName(*Value);
		// Keep the quality mode when the new upscaler has the same one (Quality stays Quality).
		const FName Mode = UMenuGraphicsLibrary::GetActiveGraphicsMode(EMenuGraphicsFeatureKind::Upscaler);
		UMenuGraphicsLibrary::ApplyGraphicsFeature(EMenuGraphicsFeatureKind::Upscaler, Chosen, Mode);
		break;
	}
	case EMenuGraphicsFeatureField::UpscalerMode:
		if (const FMenuGraphicsFeature* Upscaler = CurrentUpscaler())
		{
			UMenuGraphicsLibrary::ApplyGraphicsFeature(EMenuGraphicsFeatureKind::Upscaler, Upscaler->Id, FName(*Value));
		}
		break;
	case EMenuGraphicsFeatureField::FrameGeneration:
		UMenuGraphicsLibrary::ApplyGraphicsFeature(EMenuGraphicsFeatureKind::FrameGeneration, Value.Equals(TEXT("Off"), ESearchCase::IgnoreCase) ? NAME_None : FName(*Value), NAME_None);
		break;
	case EMenuGraphicsFeatureField::LatencyReduction:
	{
		FString FeatureName = Value;
		FString ModeName;
		Value.Split(TEXT("."), &FeatureName, &ModeName);
		if (FeatureName.IsEmpty())
		{
			FeatureName = Value;
		}
		UMenuGraphicsLibrary::ApplyGraphicsFeature(EMenuGraphicsFeatureKind::LatencyReduction,
			Value.Equals(TEXT("Off"), ESearchCase::IgnoreCase) ? NAME_None : FName(*FeatureName), FName(*ModeName));
		break;
	}
	case EMenuGraphicsFeatureField::Sharpness:
		UMenuGraphicsLibrary::SetUpscalerSharpness(static_cast<float>(MenuSettingValues::ToNumber(Value)));
		break;
	case EMenuGraphicsFeatureField::ResolutionScale:
	{
		FText Reason;
		if (IsAvailable(Setting, Context, Reason))
		{
			UMenuGraphicsLibrary::SetConsoleVariable(TEXT("r.ScreenPercentage"), MenuSettingValues::FromNumber(FMath::Clamp(MenuSettingValues::ToNumber(Value, 100.0), 10.0, 200.0)));
		}
		break;
	}
	default:
		break;
	}
}

bool UMenuSettingTarget_GraphicsFeature::Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const
{
	// The upscaler isn't read back: "Auto" must stay Auto (it follows a new graphics card). Modes and on / off states are.
	switch (Field)
	{
	case EMenuGraphicsFeatureField::UpscalerMode:
	{
		const FName Mode = UMenuGraphicsLibrary::GetActiveGraphicsMode(EMenuGraphicsFeatureKind::Upscaler);
		if (Mode.IsNone())
		{
			return false;
		}
		OutValue = Mode.ToString();
		return true;
	}
	case EMenuGraphicsFeatureField::FrameGeneration:
		if (!MenuUpscalersLocal::ActiveFeatures().Contains(EMenuGraphicsFeatureKind::FrameGeneration))
		{
			return false;
		}
		OutValue = UMenuGraphicsLibrary::GetActiveGraphicsFeature(EMenuGraphicsFeatureKind::FrameGeneration).IsNone() ? FString(TEXT("Off")) : UMenuGraphicsLibrary::GetActiveGraphicsFeature(EMenuGraphicsFeatureKind::FrameGeneration).ToString();
		return true;
	case EMenuGraphicsFeatureField::LatencyReduction:
	{
		if (!MenuUpscalersLocal::ActiveFeatures().Contains(EMenuGraphicsFeatureKind::LatencyReduction))
		{
			return false;
		}
		const FName Active = UMenuGraphicsLibrary::GetActiveGraphicsFeature(EMenuGraphicsFeatureKind::LatencyReduction);
		const FName Mode = UMenuGraphicsLibrary::GetActiveGraphicsMode(EMenuGraphicsFeatureKind::LatencyReduction);
		OutValue = Active.IsNone() ? FString(TEXT("Off")) : Mode.IsNone() ? Active.ToString() : FString::Printf(TEXT("%s.%s"), *Active.ToString(), *Mode.ToString());
		return true;
	}
	default:
		return false;
	}
}

bool UMenuSettingTarget_GraphicsFeature::GetDefault(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const
{
	switch (Field)
	{
	case EMenuGraphicsFeatureField::Upscaler:
		OutValue = TEXT("Auto");
		return true;
	case EMenuGraphicsFeatureField::UpscalerMode:
		if (const FMenuGraphicsFeature* Upscaler = MenuUpscalersLocal::CurrentUpscaler())
		{
			OutValue = Upscaler->GetDefaultModeId().ToString();
			return true;
		}
		return false;
	case EMenuGraphicsFeatureField::FrameGeneration:
	case EMenuGraphicsFeatureField::LatencyReduction:
		OutValue = TEXT("Off");
		return true;
	default:
		return false;
	}
}

bool UMenuSettingTarget_GraphicsFeature::WritesDefaultOnStart(const UMenuSetting& Setting) const
{
	// The upscaler, its mode and the on / off features must hold from the first frame (Auto picks DLSS on an RTX card).
	return Field == EMenuGraphicsFeatureField::Upscaler || Field == EMenuGraphicsFeatureField::UpscalerMode
		|| Field == EMenuGraphicsFeatureField::FrameGeneration || Field == EMenuGraphicsFeatureField::LatencyReduction;
}

#undef LOCTEXT_NAMESPACE
