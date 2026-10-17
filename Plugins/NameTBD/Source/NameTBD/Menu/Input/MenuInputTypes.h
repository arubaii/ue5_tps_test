// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
#include "NameTBD/Menu/Settings/MenuSettingTypes.h"

#include "MenuInputTypes.generated.h"

class UInputAction;
class UInputMappingContext;

/** What the player last used. */
UENUM(BlueprintType)
enum class EMenuInputDevice : uint8
{
	KeyboardMouse UMETA(DisplayName = "Keyboard & Mouse"),
	Gamepad,
	Touch
};

/**
 * Whose button prompts a gamepad shows. Custom is a family of the project's own, named by a Key Glyph Set's Custom Family
 * Name (appended last: saved values and Blueprint byte conversions keep their meaning).
 */
UENUM(BlueprintType)
enum class EMenuGamepadFamily : uint8
{
	Generic UMETA(ToolTip = "Neutral prompts (labels like \"A\", \"LB\" without a brand's art)."),
	Xbox,
	PlayStation,
	Switch UMETA(DisplayName = "Nintendo Switch"),
	SteamDeck UMETA(DisplayName = "Steam Deck"),
	Custom UMETA(ToolTip = "A family of your own - an Xbox Elite pad, a fight stick, VR controllers - named by the glyph set's Custom Family Name. Shown for hardware listed in Project Settings > Menus > Custom Gamepad Hardware, or when the Button Prompts setting picks it.")
};

/** What happens when a key being bound is already bound to something else. */
UENUM(BlueprintType)
enum class EMenuKeyConflictPolicy : uint8
{
	Swap UMETA(ToolTip = "The other input takes this one's old key (nothing is left unbound)."),
	Unbind UMETA(ToolTip = "The other input loses the key (it shows as unbound)."),
	Allow UMETA(ToolTip = "Both keep it (both act on the key)."),
	Refuse UMETA(ToolTip = "The key isn't bound; the row says which input has it.")
};

/** One key's prompt: an image (the platform's art), or a label drawn as a key cap. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuKeyGlyph
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glyph")
	FKey Key;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glyph")
	FMenuImage Image;

	/** The key cap's text when there is no image ("LB", "Space"). Empty: the key's short name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glyph")
	FText Label;
};

/**
 * Button prompt art for one device family: Xbox, PlayStation, Switch, Steam Deck, a Custom family of your own (an Elite pad,
 * VR controllers), or keyboard and mouse (For Keyboard on). List them in Project Settings > Plugins > Menus > Key Glyph Sets.
 * A key without art here shows as a key cap with its name (or the set's label), so prompts always read.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Key Glyph Set"))
class NAMETBD_API UMenuKeyGlyphSet : public UDataAsset
{
	GENERATED_BODY()

public:

	/** The gamepads this art is for. Custom: a family of your own, named below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glyphs", meta = (EditCondition = "!bForKeyboard"))
	EMenuGamepadFamily Family = EMenuGamepadFamily::Xbox;

	/**
	 * Custom: the family's name ("XboxElite", "VR"). Project Settings > Menus > Custom Gamepad Hardware maps hardware to it,
	 * the Button Prompts setting lists it, and Set Gamepad Family Override takes it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glyphs", meta = (DisplayName = "Custom Family Name", EditCondition = "!bForKeyboard && Family == EMenuGamepadFamily::Custom", EditConditionHides))
	FName CustomFamilyName = NAME_None;

	/** Custom: its name in the Button Prompts setting. Empty: Custom Family Name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glyphs", meta = (DisplayName = "Custom Family Display Name", EditCondition = "!bForKeyboard && Family == EMenuGamepadFamily::Custom", EditConditionHides))
	FText CustomFamilyDisplayName;

	/**
	 * Custom: keys this set has no art for come from this family's set (an Elite pad: Xbox, adding only its paddles), and its
	 * key cap labels ("A" or "Cross") are used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glyphs", meta = (DisplayName = "Fallback Family", EditCondition = "!bForKeyboard && Family == EMenuGamepadFamily::Custom", EditConditionHides))
	EMenuGamepadFamily FallbackFamily = EMenuGamepadFamily::Xbox;

	/** This set is the keyboard and mouse art (Family is then ignored). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glyphs")
	bool bForKeyboard = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glyphs", meta = (TitleProperty = "Key"))
	TArray<FMenuKeyGlyph> Glyphs;

	const FMenuKeyGlyph* Find(const FKey& Key) const;
};

namespace MenuInputUtils
{
	/** A key's family-appropriate short label when there is no art: "A" / "Cross", "LB" / "L1", "Space", "LMB". */
	NAMETBD_API FText GetKeyLabel(const FKey& Key, EMenuGamepadFamily Family);

	/** The device a key belongs to (mouse keys are Keyboard & Mouse; touch keys Touch). */
	NAMETBD_API EMenuInputDevice GetKeyDevice(const FKey& Key);

	/**
	 * The key Context maps Action to for a gamepad (bGamepad) or keyboard and mouse - the context's own key, no player needed
	 * (the designer uses it). OutMappingName: that mapping's player-mappable name, for its rebinding. Invalid when none.
	 */
	NAMETBD_API FKey GetContextKey(const UInputMappingContext* Context, const UInputAction* Action, bool bGamepad, FName* OutMappingName = nullptr);

	/**
	 * A key's glyph with no player to ask (the designer): the project's Key Glyph Sets read directly - keyboard art, or the
	 * Default Gamepad Family's (a Custom family's falling back to its Fallback Family) - with the family's label.
	 */
	NAMETBD_API FMenuKeyGlyph GetProjectKeyGlyph(const FKey& Key);
}
