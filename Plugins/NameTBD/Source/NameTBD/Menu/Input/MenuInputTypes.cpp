// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuInputTypes.h"

#include "InputMappingContext.h"
#include "NameTBD/Menu/Settings/MenuProjectSettings.h"

#define LOCTEXT_NAMESPACE "MenuInputTypes"

const FMenuKeyGlyph* UMenuKeyGlyphSet::Find(const FKey& Key) const
{
	return Glyphs.FindByPredicate([&Key](const FMenuKeyGlyph& Glyph) { return Glyph.Key == Key; });
}

namespace MenuInputUtils
{
	FText GetKeyLabel(const FKey& Key, EMenuGamepadFamily Family)
	{
		if (!Key.IsValid())
		{
			return LOCTEXT("Unbound", "-");
		}
		if (!Key.IsGamepadKey())
		{
			return Key.GetDisplayName(false);
		}

		struct FLabel { FKey Key; FText Xbox; FText PlayStation; FText Switch; };
		static const FLabel Labels[] = {
			{ EKeys::Gamepad_FaceButton_Bottom, LOCTEXT("XA", "A"), LOCTEXT("PSCross", "Cross"), LOCTEXT("NB", "B") },
			{ EKeys::Gamepad_FaceButton_Right, LOCTEXT("XB", "B"), LOCTEXT("PSCircle", "Circle"), LOCTEXT("NA", "A") },
			{ EKeys::Gamepad_FaceButton_Left, LOCTEXT("XX", "X"), LOCTEXT("PSSquare", "Square"), LOCTEXT("NY", "Y") },
			{ EKeys::Gamepad_FaceButton_Top, LOCTEXT("XY", "Y"), LOCTEXT("PSTriangle", "Triangle"), LOCTEXT("NX", "X") },
			{ EKeys::Gamepad_LeftShoulder, LOCTEXT("XLB", "LB"), LOCTEXT("PSL1", "L1"), LOCTEXT("NL", "L") },
			{ EKeys::Gamepad_RightShoulder, LOCTEXT("XRB", "RB"), LOCTEXT("PSR1", "R1"), LOCTEXT("NR", "R") },
			{ EKeys::Gamepad_LeftTrigger, LOCTEXT("XLT", "LT"), LOCTEXT("PSL2", "L2"), LOCTEXT("NZL", "ZL") },
			{ EKeys::Gamepad_RightTrigger, LOCTEXT("XRT", "RT"), LOCTEXT("PSR2", "R2"), LOCTEXT("NZR", "ZR") },
			{ EKeys::Gamepad_LeftTriggerAxis, LOCTEXT("XLT", "LT"), LOCTEXT("PSL2", "L2"), LOCTEXT("NZL", "ZL") },
			{ EKeys::Gamepad_RightTriggerAxis, LOCTEXT("XRT", "RT"), LOCTEXT("PSR2", "R2"), LOCTEXT("NZR", "ZR") },
			{ EKeys::Gamepad_LeftThumbstick, LOCTEXT("XLS", "LS"), LOCTEXT("PSL3", "L3"), LOCTEXT("NLS", "L Stick") },
			{ EKeys::Gamepad_RightThumbstick, LOCTEXT("XRS", "RS"), LOCTEXT("PSR3", "R3"), LOCTEXT("NRS", "R Stick") },
			{ EKeys::Gamepad_Special_Left, LOCTEXT("XView", "View"), LOCTEXT("PSCreate", "Create"), LOCTEXT("NMinus", "-") },
			{ EKeys::Gamepad_Special_Right, LOCTEXT("XMenu", "Menu"), LOCTEXT("PSOptions", "Options"), LOCTEXT("NPlus", "+") },
			{ EKeys::Gamepad_DPad_Up, LOCTEXT("DUp", "D-Pad Up"), LOCTEXT("DUp", "D-Pad Up"), LOCTEXT("DUp", "D-Pad Up") },
			{ EKeys::Gamepad_DPad_Down, LOCTEXT("DDown", "D-Pad Down"), LOCTEXT("DDown", "D-Pad Down"), LOCTEXT("DDown", "D-Pad Down") },
			{ EKeys::Gamepad_DPad_Left, LOCTEXT("DLeft", "D-Pad Left"), LOCTEXT("DLeft", "D-Pad Left"), LOCTEXT("DLeft", "D-Pad Left") },
			{ EKeys::Gamepad_DPad_Right, LOCTEXT("DRight", "D-Pad Right"), LOCTEXT("DRight", "D-Pad Right"), LOCTEXT("DRight", "D-Pad Right") },
		};
		for (const FLabel& Label : Labels)
		{
			if (Label.Key == Key)
			{
				switch (Family)
				{
				case EMenuGamepadFamily::PlayStation: return Label.PlayStation;
				case EMenuGamepadFamily::Switch: return Label.Switch;
				default: return Label.Xbox;
				}
			}
		}
		return Key.GetDisplayName(false);
	}

	FKey GetContextKey(const UInputMappingContext* Context, const UInputAction* Action, bool bGamepad, FName* OutMappingName)
	{
		if (!Context || !Action)
		{
			return EKeys::Invalid;
		}
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action != Action || !Mapping.Key.IsValid() || Mapping.Key.IsTouch() || Mapping.Key.IsGamepadKey() != bGamepad)
			{
				continue;
			}
			if (OutMappingName)
			{
				*OutMappingName = Mapping.GetMappingName();
			}
			return Mapping.Key;
		}
		return EKeys::Invalid;
	}

	FMenuKeyGlyph GetProjectKeyGlyph(const FKey& Key)
	{
		const UMenuProjectSettings& Settings = UMenuProjectSettings::Get();
		EMenuGamepadFamily Family = Settings.DefaultGamepadFamily;
		const bool bKeyboard = !Key.IsGamepadKey();

		auto FindSet = [&Settings, bKeyboard](EMenuGamepadFamily InFamily, FName CustomName) -> const UMenuKeyGlyphSet*
		{
			for (const TSoftObjectPtr<UMenuKeyGlyphSet>& SetEntry : Settings.KeyGlyphSets)
			{
				const UMenuKeyGlyphSet* GlyphSet = SetEntry.LoadSynchronous();
				if (!GlyphSet || GlyphSet->bForKeyboard != bKeyboard)
				{
					continue;
				}
				if (bKeyboard || (GlyphSet->Family == InFamily && (InFamily != EMenuGamepadFamily::Custom || GlyphSet->CustomFamilyName == CustomName)))
				{
					return GlyphSet;
				}
			}
			return nullptr;
		};

		const UMenuKeyGlyphSet* GlyphSet = FindSet(Family, Settings.DefaultCustomGamepadFamily);
		const FMenuKeyGlyph* Found = GlyphSet ? GlyphSet->Find(Key) : nullptr;
		if (!bKeyboard && Family == EMenuGamepadFamily::Custom)
		{
			Family = (GlyphSet && GlyphSet->FallbackFamily != EMenuGamepadFamily::Custom) ? GlyphSet->FallbackFamily : EMenuGamepadFamily::Xbox;
			if (!Found)
			{
				const UMenuKeyGlyphSet* FallbackSet = FindSet(Family, NAME_None);
				Found = FallbackSet ? FallbackSet->Find(Key) : nullptr;
			}
		}

		FMenuKeyGlyph Out;
		Out.Key = Key;
		if (Found)
		{
			Out = *Found;
		}
		if (Out.Label.IsEmpty())
		{
			Out.Label = GetKeyLabel(Key, Family);
		}
		return Out;
	}

	EMenuInputDevice GetKeyDevice(const FKey& Key)
	{
		if (Key.IsGamepadKey())
		{
			return EMenuInputDevice::Gamepad;
		}
		if (Key.IsTouch())
		{
			return EMenuInputDevice::Touch;
		}
		return EMenuInputDevice::KeyboardMouse;
	}
}

#undef LOCTEXT_NAMESPACE
