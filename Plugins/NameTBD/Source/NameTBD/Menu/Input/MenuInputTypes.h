// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputCoreTypes.h"
#include "NameTBD/Menu/Settings/MenuSettingTypes.h"

#include "MenuInputTypes.generated.h"

/** What the player last used. */
UENUM(BlueprintType)
enum class EMenuInputDevice : uint8
{
	KeyboardMouse UMETA(DisplayName = "Keyboard & Mouse"),
	Gamepad,
	Touch
};

/** Whose button prompts a gamepad shows. */
UENUM(BlueprintType)
enum class EMenuGamepadFamily : uint8
{
	Generic UMETA(ToolTip = "Neutral prompts (labels like \"A\", \"LB\" without a brand's art)."),
	Xbox,
	PlayStation,
	Switch UMETA(DisplayName = "Nintendo Switch"),
	SteamDeck UMETA(DisplayName = "Steam Deck")
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
 * Button prompt art for one device family: Xbox, PlayStation, Switch, Steam Deck, or keyboard and mouse (Family Generic with
 * For Keyboard on). List them in Project Settings > Plugins > Menus > Key Glyph Sets. A key without art here shows as a key
 * cap with its name (or the set's label), so prompts always read.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Key Glyph Set"))
class NAMETBD_API UMenuKeyGlyphSet : public UDataAsset
{
	GENERATED_BODY()

public:

	/** The gamepads this art is for. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glyphs")
	EMenuGamepadFamily Family = EMenuGamepadFamily::Xbox;

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
}
