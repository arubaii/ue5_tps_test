// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimEditorSettings.h"

#include "NameTBDColors.h"

#define LOCTEXT_NAMESPACE "WidgetAnimEditorSettings"

UWidgetAnimEditorSettings::UWidgetAnimEditorSettings()
{
	BlueprintTypeTrackColor = NameTBD::Colors::WidgetAnimEditor::LayerBlueprintType;
	CueTrackColor = NameTBD::Colors::WidgetAnimEditor::LayerCue;

	// DEVELOPER ONLY: the palette's values to start from - see the header.
	RangeStartColor = NameTBD::Colors::WidgetAnimEditor::RangeStart;
	RangeEndColor = NameTBD::Colors::WidgetAnimEditor::RangeEnd;
	RangeLineOpacity = NameTBD::Colors::WidgetAnimEditor::RangeLineOpacity;
	PlayheadColor = NameTBD::Colors::WidgetAnimEditor::Playhead;
	PlayheadLineOpacity = NameTBD::Colors::WidgetAnimEditor::PlayheadLineOpacity;
	OutOfRangeShade = NameTBD::Colors::WidgetAnimEditor::OutOfRangeShade;
	GridMajorColor = NameTBD::Colors::WidgetAnimEditor::GridMajor;
	GridMinorColor = NameTBD::Colors::WidgetAnimEditor::GridMinor;
	CueTrackIconTint = NameTBD::Colors::WidgetAnimEditor::CueTrackIcon;
	MachinePlayPauseOutline = NameTBD::Colors::StateMachineEditor::FloatingButtonOutline;
	ControlsSwitchColor = NameTBD::Colors::WidgetAnimEditor::ControlsSwitchBackground;
	ControlsSwitchSelectedColor = NameTBD::Colors::WidgetAnimEditor::ControlsSwitchSelected;
	ControlsSwitchHoveredColor = NameTBD::Colors::WidgetAnimEditor::ControlsSwitchHover;
	ControlsBackgroundColor = NameTBD::Colors::WidgetAnimEditor::ControlsPill;
	ControlsSelectedColor = NameTBD::Colors::WidgetAnimEditor::ToggleOnBackground;
	ControlsHoveredColor = NameTBD::Colors::WidgetAnimEditor::ControlsHover;

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
	case EWidgetAnimType::Shake:          return Palette::LayerShake;
	case EWidgetAnimType::MaterialScalar: return Palette::LayerMaterialScalar;
	case EWidgetAnimType::MaterialVector: return Palette::LayerMaterialVector;
	case EWidgetAnimType::Transform:      return Palette::LayerTransform;
	case EWidgetAnimType::ShapeBox:       return Palette::LayerShapeBox;
	case EWidgetAnimType::Shape:          return Palette::LayerShape;
	case EWidgetAnimType::Design:         return Palette::LayerDesign;
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

void UWidgetAnimEditorSettings::SetShowTimeInFrames(bool bEnabled)
{
	if (bShowTimeInFrames != bEnabled)
	{
		bShowTimeInFrames = bEnabled;
		SaveConfig();
	}
}

void UWidgetAnimEditorSettings::SetPreviewFrameRate(float FrameRate)
{
	const float Clamped = FMath::Clamp(FrameRate, 1.f, 1000.f);
	if (!FMath::IsNearlyEqual(PreviewFrameRate, Clamped))
	{
		PreviewFrameRate = Clamped;
		SaveConfig();
	}
}

void UWidgetAnimEditorSettings::SetGizmoAutoKey(bool bEnabled)
{
	if (bGizmoAutoKey != bEnabled)
	{
		bGizmoAutoKey = bEnabled;
		SaveConfig();
	}
}

void UWidgetAnimEditorSettings::SetControlsShowTrackControls(bool bTrackControls)
{
	if (bControlsShowTrackControls != bTrackControls)
	{
		bControlsShowTrackControls = bTrackControls;
		SaveConfig();
	}
}

void UWidgetAnimEditorSettings::SetShowMotionPaths(bool bEnabled)
{
	if (bShowMotionPaths != bEnabled)
	{
		bShowMotionPaths = bEnabled;
		SaveConfig();
	}
}

void UWidgetAnimEditorSettings::SetMotionPathPathsOnly(bool bEnabled)
{
	if (bMotionPathPathsOnly != bEnabled)
	{
		bMotionPathPathsOnly = bEnabled;
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
	return LOCTEXT("SectionDescription", "Track colors, track controls, previews and motion paths for the Widget Animations panel in the Widget Blueprint editor.");
}
#endif

#undef LOCTEXT_NAMESPACE