// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimBlueprintCue.h"

#define LOCTEXT_NAMESPACE "WidgetAnimBlueprintCue"

// =====================================================================================================
// UWidgetAnimBlueprintCue
// =====================================================================================================

UWidgetAnimBlueprintCue::UWidgetAnimBlueprintCue()
{
	TargetClass = UWidget::StaticClass();
}

FText UWidgetAnimBlueprintCue::GetCueDisplayName() const
{
	return DisplayName.IsEmpty() ? GetClass()->GetDisplayNameText() : DisplayName;
}

// =====================================================================================================
// FWidgetAnimBlueprintCueParams
// =====================================================================================================

const UWidgetAnimBlueprintCue* FWidgetAnimBlueprintCueParams::GetTypeObject() const
{
	const UClass* TypeClass = Type.Get();
	if (!TypeClass || TypeClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}
	return TypeClass->GetDefaultObject<UWidgetAnimBlueprintCue>();
}

bool FWidgetAnimBlueprintCueParams::IsSettingsStructCurrent() const
{
	const UWidgetAnimBlueprintCue* TypeObject = GetTypeObject();
	return !TypeObject || Settings.GetScriptStruct() == TypeObject->SettingsStruct.Get();
}

bool FWidgetAnimBlueprintCueParams::SyncSettingsStruct()
{
	if (IsSettingsStructCurrent())
	{
		return false;
	}
	const UScriptStruct* SettingsStruct = GetTypeObject()->SettingsStruct.Get();
	if (SettingsStruct)
	{
		Settings.InitializeAs(SettingsStruct);
	}
	else
	{
		Settings.Reset();
	}
	return true;
}

FInstancedStruct FWidgetAnimBlueprintCueParams::MakeFor(TSubclassOf<UWidgetAnimBlueprintCue> InType)
{
	FInstancedStruct Out;
	Out.InitializeAs<FWidgetAnimBlueprintCueParams>();

	FWidgetAnimBlueprintCueParams& Params = Out.GetMutable<FWidgetAnimBlueprintCueParams>();
	Params.Type = InType;
	if (const UWidgetAnimBlueprintCue* TypeObject = Params.GetTypeObject())
	{
		Params.Value = TypeObject->DefaultValue;
		if (TypeObject->SettingsStruct)
		{
			Params.Settings.InitializeAs(TypeObject->SettingsStruct);
		}
	}
	return Out;
}

// =====================================================================================================
// FWidgetAnimBlueprintCueEvaluator
// =====================================================================================================

FText FWidgetAnimBlueprintCueEvaluator::GetDisplayName() const
{
	return LOCTEXT("BlueprintCueName", "Blueprint Cue");
}

FText FWidgetAnimBlueprintCueEvaluator::GetTrackDisplayName(const FInstancedStruct& Params) const
{
	const FWidgetAnimBlueprintCueParams* CueParams = GetParams(Params);
	if (const UWidgetAnimBlueprintCue* TypeObject = CueParams ? CueParams->GetTypeObject() : nullptr)
	{
		return TypeObject->GetCueDisplayName();
	}
	return LOCTEXT("NoBlueprintCueName", "Blueprint Cue (none picked)");
}

FText FWidgetAnimBlueprintCueEvaluator::GetTrackDescription(const FInstancedStruct& Params) const
{
	const FWidgetAnimBlueprintCueParams* CueParams = GetParams(Params);
	const UWidgetAnimBlueprintCue* TypeObject = CueParams ? CueParams->GetTypeObject() : nullptr;
	return TypeObject ? TypeObject->Description : FText::GetEmpty();
}

FText FWidgetAnimBlueprintCueEvaluator::GetTrackValueLabel(const FInstancedStruct& Params) const
{
	const FWidgetAnimBlueprintCueParams* CueParams = GetParams(Params);
	const UWidgetAnimBlueprintCue* TypeObject = CueParams ? CueParams->GetTypeObject() : nullptr;
	return TypeObject && !TypeObject->ValueLabel.IsEmpty() ? TypeObject->ValueLabel : GetValueLabel();
}

UClass* FWidgetAnimBlueprintCueEvaluator::GetTrackTargetClass(const FInstancedStruct& Params) const
{
	const FWidgetAnimBlueprintCueParams* CueParams = GetParams(Params);
	const UWidgetAnimBlueprintCue* TypeObject = CueParams ? CueParams->GetTypeObject() : nullptr;
	return TypeObject && TypeObject->TargetClass ? TypeObject->TargetClass.Get() : UWidget::StaticClass();
}

bool FWidgetAnimBlueprintCueEvaluator::FiresAtDesignTime(const FInstancedStruct& Params) const
{
	const FWidgetAnimBlueprintCueParams* CueParams = GetParams(Params);
	const UWidgetAnimBlueprintCue* TypeObject = CueParams ? CueParams->GetTypeObject() : nullptr;
	return TypeObject && TypeObject->bFireAtDesignTime;
}

double FWidgetAnimBlueprintCueEvaluator::GetPointValue(const FInstancedStruct& Params) const
{
	const FWidgetAnimBlueprintCueParams* CueParams = GetParams(Params);
	return CueParams ? CueParams->Value : 0.0;
}

void FWidgetAnimBlueprintCueEvaluator::Fire(const FWidgetAnimCueContext& Context) const
{
	const FWidgetAnimBlueprintCueParams* CueParams = Context.Params ? GetParams(*Context.Params) : nullptr;
	const UWidgetAnimBlueprintCue* TypeObject = CueParams ? CueParams->GetTypeObject() : nullptr;
	if (!TypeObject)
	{
		return;
	}

	// Rare - a cue fires at moments, not every frame - so, unlike a Blueprint track type, not timed.
	TypeObject->OnCue(Cast<UWidget>(Context.Target), CueParams->Settings, Context.Info);
}

#undef LOCTEXT_NAMESPACE
