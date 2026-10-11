// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetGizmoSettings.h"

#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "WidgetGizmoSettings"

UWidgetGizmoSettings::UWidgetGizmoSettings()
	: GrabKey(EKeys::G)
	, ScaleKey(EKeys::S)
	, RotateKey(EKeys::R)
	, ShearKey(EKeys::Z)
	, HideKey(EKeys::H)
{
}

#if WITH_EDITOR
FText UWidgetGizmoSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "Widget Gizmo");
}

FText UWidgetGizmoSettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "The gizmo that moves, scales, rotates and shears the widgets selected in the UMG designer, and moves their pivot: whether it shows, its keys, its size and its snapping.");
}
#endif

#undef LOCTEXT_NAMESPACE
