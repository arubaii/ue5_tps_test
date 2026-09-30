// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimEditorSettings.h"

#include "NameTBDColors.h"

#define LOCTEXT_NAMESPACE "WidgetAnimEditorSettings"

UWidgetAnimEditorSettings::UWidgetAnimEditorSettings()
{
	BlueprintTypeTrackColor = NameTBD::Colors::WidgetAnimEditor::LayerBlueprintType;
	CueTrackColor = NameTBD::Colors::WidgetAnimEditor::LayerCue;
	FillMissingTrackColors();
}

void UWidgetAnimEditorSettings::PostInitProperties()
{
	Super::PostInitProperties();

	// Config has been read by now. A layer type added since these settings were saved has no entry in them.
	FillMissingTrackColors();
}

void UWidgetAnimEditorSettings::FillMissingTrackColors()
{
	const UEnum* TypeEnum = StaticEnum<EWidgetAnimType>();
	if (!TypeEnum)
	{
		return;
	}

	// NumEnums includes the generated _MAX entry, which is not a type.
	for (int32 Index = 0; Index < TypeEnum->NumEnums() - 1; ++Index)
	{
		const EWidgetAnimType AnimType = static_cast<EWidgetAnimType>(TypeEnum->GetValueByIndex(Index));
		if (!TrackColors.Contains(AnimType))
		{
			TrackColors.Add(AnimType, GetDefaultTrackColor(AnimType));
		}
	}
}

FLinearColor UWidgetAnimEditorSettings::GetTrackColor(EWidgetAnimType AnimType) const
{
	if (const FLinearColor* Found = TrackColors.Find(AnimType))
	{
		return *Found;
	}
	return GetDefaultTrackColor(AnimType);
}

FLinearColor UWidgetAnimEditorSettings::GetDefaultTrackColor(EWidgetAnimType AnimType)
{
	namespace Palette = NameTBD::Colors::WidgetAnimEditor;

	switch (AnimType)
	{
	case EWidgetAnimType::Fade:           return Palette::LayerFade;
	case EWidgetAnimType::Scale:          return Palette::LayerScale;
	case EWidgetAnimType::Slide:          return Palette::LayerSlide;
	case EWidgetAnimType::Rotate:         return Palette::LayerRotate;
	case EWidgetAnimType::Pulse:          return Palette::LayerPulse;
	case EWidgetAnimType::MaterialScalar: return Palette::LayerMaterialScalar;
	case EWidgetAnimType::MaterialVector: return Palette::LayerMaterialVector;
	case EWidgetAnimType::Transform:      return Palette::LayerTransform;
	case EWidgetAnimType::Custom:         return Palette::LayerCustom;
	default:                              return Palette::LayerUnknown;
	}
}

void UWidgetAnimEditorSettings::SetTrackSnapping(bool bEnabled)
{
	if (bTrackSnapping != bEnabled)
	{
		bTrackSnapping = bEnabled;
		SaveConfig();
	}
}

void UWidgetAnimEditorSettings::SetLinkedEdges(bool bEnabled)
{
	if (bLinkedEdges != bEnabled)
	{
		bLinkedEdges = bEnabled;
		SaveConfig();
	}
}

#if WITH_EDITOR
FText UWidgetAnimEditorSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "Widget Animations Editor");
}

FText UWidgetAnimEditorSettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "Track colors, track controls and previews for the Widget Animations panel in the Widget Blueprint editor.");
}
#endif

#undef LOCTEXT_NAMESPACE