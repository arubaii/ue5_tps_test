// Copyright AR GameSystems 2026. All rights reserved.

#include "DiegeticUISettings.h"

#include "DiegeticScreenComponent.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "DiegeticUISettings"

UDiegeticUISettings::UDiegeticUISettings()
{
	SectionName = TEXT("NameTBDDiegeticUI");

	// The keys a game usually gives "interact" and "use the cursor": the mouse buttons, and a gamepad's left face button (the
	// bottom one is usually jump, and stays the game's until a screen is in use).
	ClickKeys = { EKeys::LeftMouseButton, EKeys::Gamepad_FaceButton_Left };
	SecondaryClickKeys = { EKeys::RightMouseButton };
	ScrollWheelKeys = { EKeys::MouseWheelAxis };
	ScrollStickKeys = { EKeys::Gamepad_RightY };
	// Backspace as well as Escape: in the editor, Escape stops Play In Editor before the game sees it.
	BackKeys = { EKeys::Escape, EKeys::BackSpace, EKeys::Gamepad_FaceButton_Right };
	AcceptKeys = { EKeys::Gamepad_FaceButton_Bottom, EKeys::Enter };
	NavigateUpKeys = { EKeys::Gamepad_DPad_Up, EKeys::Gamepad_LeftStick_Up, EKeys::Up };
	NavigateDownKeys = { EKeys::Gamepad_DPad_Down, EKeys::Gamepad_LeftStick_Down, EKeys::Down };
	NavigateLeftKeys = { EKeys::Gamepad_DPad_Left, EKeys::Gamepad_LeftStick_Left, EKeys::Left };
	NavigateRightKeys = { EKeys::Gamepad_DPad_Right, EKeys::Gamepad_LeftStick_Right, EKeys::Right };

	// Where the editor makes them (DiegeticScreenMaterial.h in the editor module).
	ScreenMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/NameTBD/Diegetic/M_DiegeticScreen.M_DiegeticScreen")));
	GlassScreenMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/NameTBD/Diegetic/M_DiegeticScreen_Glass.M_DiegeticScreen_Glass")));
	OpaqueScreenMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/NameTBD/Diegetic/M_DiegeticScreen_Opaque.M_DiegeticScreen_Opaque")));
}

const TSoftObjectPtr<UMaterialInterface>& UDiegeticUISettings::GetScreenMaterial(EDiegeticMaterialVariant Variant) const
{
	switch (Variant)
	{
	case EDiegeticMaterialVariant::Glass:  return GlassScreenMaterial;
	case EDiegeticMaterialVariant::Opaque: return OpaqueScreenMaterial;
	default:                               return ScreenMaterial;
	}
}

#if WITH_EDITOR
FText UDiegeticUISettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "Diegetic UI");
}

FText UDiegeticUISettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "UI in the world (NameTBD): the Diegetic Screens' redraw budget, the Diegetic Interactor every player gets, and the keys it binds.");
}
#endif

#undef LOCTEXT_NAMESPACE
