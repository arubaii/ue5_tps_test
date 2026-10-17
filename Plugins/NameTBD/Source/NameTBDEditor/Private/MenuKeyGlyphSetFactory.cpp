// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuKeyGlyphSetFactory.h"

#include "AssetTypeCategories.h"
#include "InputCoreTypes.h"

#include "NameTBD/Menu/Input/MenuInputTypes.h"

#define LOCTEXT_NAMESPACE "MenuKeyGlyphSetFactory"

UMenuKeyGlyphSetFactory::UMenuKeyGlyphSetFactory()
{
	SupportedClass = UMenuKeyGlyphSet::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UMenuKeyGlyphSetFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UMenuKeyGlyphSet* GlyphSet = NewObject<UMenuKeyGlyphSet>(InParent, InClass, InName, Flags | RF_Transactional);

	// Every button a pad prompt names, ready for its art: the set is filled in, not typed out key by key. A keyboard set
	// (For Keyboard on) removes these and lists its keys instead.
	const FKey PadKeys[] = {
		EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top,
		EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_LeftTrigger, EKeys::Gamepad_RightTrigger,
		EKeys::Gamepad_LeftThumbstick, EKeys::Gamepad_RightThumbstick, EKeys::Gamepad_Special_Left, EKeys::Gamepad_Special_Right,
		EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right,
	};
	for (const FKey& PadKey : PadKeys)
	{
		FMenuKeyGlyph& Glyph = GlyphSet->Glyphs.AddDefaulted_GetRef();
		Glyph.Key = PadKey;
	}
	return GlyphSet;
}

FText UMenuKeyGlyphSetFactory::GetDisplayName() const
{
	return LOCTEXT("FactoryName", "Key Glyph Set");
}

FText UMenuKeyGlyphSetFactory::GetToolTip() const
{
	return LOCTEXT("FactoryTooltip", "Button prompt art for one gamepad family (or keyboard and mouse): an image or a key cap label per key. List it in Project Settings > Menus > Key Glyph Sets.");
}

uint32 UMenuKeyGlyphSetFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::UI;
}

FString UMenuKeyGlyphSetFactory::GetDefaultNewAssetName() const
{
	return TEXT("KGS_NewKeyGlyphSet");
}

#undef LOCTEXT_NAMESPACE
