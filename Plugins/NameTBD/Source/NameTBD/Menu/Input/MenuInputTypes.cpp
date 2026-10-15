// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuInputTypes.h"

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
