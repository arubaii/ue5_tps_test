// Copyright AR Game Systems 2026. All rights reserved.

#include "InfoCardDeveloperSettings.h"
#include "NativeGameplayTags.h"

// Root category tag
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_UIContext_Button, "UIContext.Button");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_UIContext_Button_0_Divider, "UIContext.Button.0_Divider");

UInfoCardDeveloperSettings::UInfoCardDeveloperSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("Info Card");

	// Only seeded when there's nothing here yet — LoadConfig() runs after
	// this constructor and overwrites Presets from DefaultGame.ini if that
	// ini already has an entry for it, so this never stomps a project's own
	// saved presets on subsequent loads; it only fires the very first time
	// (or if every preset is ever deleted down to zero).
	if (Presets.Num() == 0)
	{
		FInfoCardPreset DefaultPreset;
		DefaultPreset.PresetName = TEXT("Default");
		Presets.Add(DefaultPreset);
	}

	// Same seeding rule for the context menu button lists. The "Default" shape preset is
	// deliberately neutral (identity transforms, no extrude/inset) so every button that was
	// already on Use Project Settings keeps looking exactly as it did until a designer edits it.
	if (ContextMenuShapePresets.Num() == 0)
	{
		FContextMenuShapePreset DefaultShapePreset;
		DefaultShapePreset.PresetName = TEXT("Default");
		ContextMenuShapePresets.Add(DefaultShapePreset);
	}

	// Placements most games want named: a context menu at the cursor, a tooltip above its widget, a dropdown below it, a
	// flyout to its right.
	if (PlacementPresets.Num() == 0)
	{
		FInfoCardPlacementPreset AtCursor;
		AtCursor.PresetName = TEXT("Default");
		AtCursor.Placement.ZOrder = 1500;
		PlacementPresets.Add(AtCursor);

		FInfoCardPlacementPreset Tooltip;
		Tooltip.PresetName = TEXT("Tooltip");
		Tooltip.Placement = FInfoCardPositioning::MakeBeside(EDirection2D::Up, 6.f);
		PlacementPresets.Add(Tooltip);

		FInfoCardPlacementPreset Dropdown;
		Dropdown.PresetName = TEXT("Dropdown");
		Dropdown.Placement = FInfoCardPositioning::MakeBeside(EDirection2D::Down, 2.f, 1500);
		PlacementPresets.Add(Dropdown);

		FInfoCardPlacementPreset Flyout;
		Flyout.PresetName = TEXT("Flyout");
		Flyout.Placement = FInfoCardPositioning::MakeBeside(EDirection2D::Right, 4.f, 1500);
		PlacementPresets.Add(Flyout);
	}

	if (ContextMenuDesignPresets.Num() == 0)
	{
		FContextMenuDesignPreset DefaultDesignPreset;
		DefaultDesignPreset.PresetName = TEXT("Default");
		ContextMenuDesignPresets.Add(DefaultDesignPreset);
	}
}

const UInfoCardDeveloperSettings* UInfoCardDeveloperSettings::Get()
{
	return GetDefault<UInfoCardDeveloperSettings>();
}

bool UInfoCardDeveloperSettings::TryGetPresetValue(FName PresetName, EInfoCardConfigurableFloatKind Kind, float& OutValue) const
{
	for (const FInfoCardPreset& Preset : Presets)
	{
		if (Preset.PresetName != PresetName)
		{
			continue;
		}

		switch (Kind)
		{
		case EInfoCardConfigurableFloatKind::HoverDelay:           OutValue = Preset.HoverDelay; return true;
		case EInfoCardConfigurableFloatKind::KeepOpenSlack:        OutValue = Preset.KeepOpenSlack; return true;
		case EInfoCardConfigurableFloatKind::KeepOpenGracePeriod:  OutValue = Preset.KeepOpenGracePeriod; return true;
		case EInfoCardConfigurableFloatKind::SuccessiveHoverWindow:OutValue = Preset.SuccessiveHoverWindow; return true;
		case EInfoCardConfigurableFloatKind::AutoDismissDuration:  OutValue = Preset.AutoDismissDuration; return true;
		default:                                                   return false;
		}
	}

	return false; // no preset currently has this name
}

TArray<FString> UInfoCardDeveloperSettings::GetPresetNameOptions()
{
	TArray<FString> Options;

	if (const UInfoCardDeveloperSettings* Settings = Get())
	{
		Options.Reserve(Settings->Presets.Num());
		for (const FInfoCardPreset& Preset : Settings->Presets)
		{
			Options.Add(Preset.PresetName.ToString());
		}
	}

	return Options;
}

const FContextMenuShapePreset* UInfoCardDeveloperSettings::FindContextMenuShapePreset(FName PresetName) const
{
	return ContextMenuShapePresets.FindByPredicate([PresetName](const FContextMenuShapePreset& Preset)
	{
		return Preset.PresetName == PresetName;
	});
}

const FContextMenuDesignPreset* UInfoCardDeveloperSettings::FindContextMenuDesignPreset(FName PresetName) const
{
	return ContextMenuDesignPresets.FindByPredicate([PresetName](const FContextMenuDesignPreset& Preset)
	{
		return Preset.PresetName == PresetName;
	});
}

TArray<FString> UInfoCardDeveloperSettings::GetContextMenuShapePresetNameOptions()
{
	TArray<FString> Options;

	if (const UInfoCardDeveloperSettings* Settings = Get())
	{
		Options.Reserve(Settings->ContextMenuShapePresets.Num());
		for (const FContextMenuShapePreset& Preset : Settings->ContextMenuShapePresets)
		{
			Options.Add(Preset.PresetName.ToString());
		}
	}

	return Options;
}

TArray<FString> UInfoCardDeveloperSettings::GetContextMenuDesignPresetNameOptions()
{
	TArray<FString> Options;

	if (const UInfoCardDeveloperSettings* Settings = Get())
	{
		Options.Reserve(Settings->ContextMenuDesignPresets.Num());
		for (const FContextMenuDesignPreset& Preset : Settings->ContextMenuDesignPresets)
		{
			Options.Add(Preset.PresetName.ToString());
		}
	}

	return Options;
}

const FInfoCardPlacementPreset* UInfoCardDeveloperSettings::FindPlacementPreset(FName PresetName) const
{
	return PlacementPresets.FindByPredicate([PresetName](const FInfoCardPlacementPreset& Preset) { return Preset.PresetName == PresetName; });
}

TArray<FString> UInfoCardDeveloperSettings::GetPlacementPresetNameOptions()
{
	TArray<FString> Options;
	if (const UInfoCardDeveloperSettings* Settings = Get())
	{
		Options.Reserve(Settings->PlacementPresets.Num());
		for (const FInfoCardPlacementPreset& Preset : Settings->PlacementPresets)
		{
			Options.Add(Preset.PresetName.ToString());
		}
	}
	return Options;
}
