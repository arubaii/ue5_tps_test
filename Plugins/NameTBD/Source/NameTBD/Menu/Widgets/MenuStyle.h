// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "NameTBD/Menu/Input/MenuKeyPrompt.h"
#include "NameTBD/Menu/Settings/MenuSettingTypes.h"
#include "NameTBD/Sound/UISoundTypes.h"
#include "NameTBD/Widgets/WidgetMotion.h"

#include "MenuStyle.generated.h"

class UMenuSettingRowWidget;

/** A ready-made look. */
UENUM(BlueprintType)
enum class EMenuStylePreset : uint8
{
	Custom UMETA(ToolTip = "Your own values (editing any value keeps them)."),
	Simple UMETA(ToolTip = "Dark and flat: charcoal panels, one blue accent, rounded rows. Reads well on any game."),
	Light UMETA(ToolTip = "Light panels, dark text, one accent."),
	Glass UMETA(ToolTip = "Frosted translucent panels over a blurred game, soft outlines."),
	Minimal UMETA(ToolTip = "No panels: text on a dark scrim, an accent line on the focused row. Cinematic."),
	Console UMETA(ToolTip = "Large rows and type for a TV at a distance (10-foot UI), strong focus."),
	HighContrast UMETA(ToolTip = "Black and white with a yellow focus outline (accessibility).")
};

/** How the page tabs look. */
UENUM(BlueprintType)
enum class EMenuTabStyle : uint8
{
	Underline UMETA(ToolTip = "Text tabs, the current one underlined in the accent."),
	Pills UMETA(ToolTip = "The current tab filled in the accent, fully rounded."),
	Boxed UMETA(ToolTip = "Every tab boxed, the current one filled.")
};

/** How a Bool shows. */
UENUM(BlueprintType)
enum class EMenuBoolDisplay : uint8
{
	Switch UMETA(ToolTip = "An on / off switch."),
	Selector UMETA(ToolTip = "< On > like any choice (console style).")
};

/** The colours of a menu look. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuStylePalette
{
	GENERATED_BODY()

	/** Behind everything (a scrim over the game; alpha lets the game show through). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Background = FLinearColor(0.012f, 0.014f, 0.02f, 0.92f);

	/** Dialogs, the description panel, dropdown lists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Surface = FLinearColor(0.04f, 0.045f, 0.06f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Row = FLinearColor(1.f, 1.f, 1.f, 0.03f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor RowHovered = FLinearColor(1.f, 1.f, 1.f, 0.07f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor RowFocused = FLinearColor(0.18f, 0.42f, 1.f, 0.18f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Accent = FLinearColor(0.2f, 0.5f, 1.f, 1.f);

	/** Text on the accent (a selected pill tab, a primary button). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor AccentText = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Text = FLinearColor(0.94f, 0.95f, 0.97f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor MutedText = FLinearColor(0.62f, 0.65f, 0.72f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor DisabledText = FLinearColor(1.f, 1.f, 1.f, 0.3f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Divider = FLinearColor(1.f, 1.f, 1.f, 0.08f);

	/** Slider tracks, a switch that is off, pips. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Track = FLinearColor(1.f, 1.f, 1.f, 0.14f);

	/** The focused row's outline and accent bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor FocusOutline = FLinearColor(0.2f, 0.5f, 1.f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Warning = FLinearColor(1.f, 0.72f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Negative = FLinearColor(1.f, 0.35f, 0.32f, 1.f);

	/** Behind a dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor Scrim = FLinearColor(0.f, 0.f, 0.f, 0.6f);
};

/** The look of settings screens and menu dialogs: colours, type, sizes, motion and sounds. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuStyle
{
	GENERATED_BODY()

	FMenuStyle();

	/** A preset's values. */
	static FMenuStyle Make(EMenuStylePreset Preset);

	/** The palette in use: High Contrast Colors while the player has High Contrast Menus on. */
	const FMenuStylePalette& GetPalette() const;

	// ---- Colours ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FMenuStylePalette Colors;

	/** Used instead while Accessibility > High Contrast Menus is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FMenuStylePalette HighContrastColors;

	// ---- Type ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	FSlateFontInfo TitleFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	FSlateFontInfo TabFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	FSlateFontInfo SectionFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	FSlateFontInfo LabelFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	FSlateFontInfo ValueFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	FSlateFontInfo DescriptionTitleFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	FSlateFontInfo DescriptionFont;

	/** Footer hints, small labels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	FSlateFontInfo HintFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	FSlateFontInfo ButtonFont;

	// ---- Layout ----

	/** Around the whole screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FMargin ScreenPadding = FMargin(64.f, 48.f);

	/** The rows and description together never get wider than this (centered). 0: no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0"))
	float ContentMaxWidth = 1400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMax = "32"))
	float CornerRadius = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "24", UIMax = "120"))
	float RowHeight = 52.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FMargin RowPadding = FMargin(18.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMax = "24"))
	float RowSpacing = 4.f;

	/** Space above each section heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMax = "64"))
	float SectionSpacing = 24.f;

	/** The width of a row's control (selector, slider, switch, keys). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "120", UIMax = "600"))
	float ValueWidth = 340.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMax = "800"))
	float DescriptionWidth = 420.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMax = "64"))
	float TabSpacing = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMax = "4"))
	float OutlineWidth = 1.f;

	/** The focused row's outline (0: only its colour and accent bar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMax = "6"))
	float FocusOutlineWidth = 1.5f;

	// ---- Look ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	EMenuTabStyle TabStyle = EMenuTabStyle::Underline;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bShowTabIcons = true;

	/** Tabs show their icon only (pages without an icon still show their name). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bTabIconsOnly = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	EMenuBoolDisplay BoolDisplay = EMenuBoolDisplay::Switch;

	/** A Choice set to Auto shows as a dropdown past this many options. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "2", UIMax = "30"))
	int32 SelectorMaxOptions = 8;

	/** Dots under a selector's value, one per option. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bShowPips = true;

	/** The panel at the side describing the focused setting (hidden on narrow screens below 1100 px). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bShowDescriptionPanel = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bShowSearch = true;

	/** Key prompts in the footer (Back, Reset, Apply, change tabs) for the device in use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bShowFooterHints = true;

	/** A dot before settings changed from their default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bShowModifiedDot = true;

	/** A thin accent bar at the focused row's left edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	bool bFocusBar = true;

	/** An image behind everything (a texture, material or SVG, stretched). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FMenuImage BackgroundImage;

	/** Blurs the game behind the menu (0: none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", UIMax = "50"))
	float BackgroundBlur = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	FMenuKeyCapStyle KeyCap;

	// ---- Motion ----

	/** Hover and focus highlights, switches, page changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	EWidgetMotion Motion = EWidgetMotion::Smooth;

	/** Pages slide a little as they change (off, or with Reduce Motion: they only fade). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	bool bSlidePages = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0", UIMax = "1", Units = "s"))
	float PageTransitionSeconds = 0.22f;

	// ---- Sounds ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
	FUISoundSlot NavigateSound;

	/** A value changed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
	FUISoundSlot ChangeSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
	FUISoundSlot AcceptSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
	FUISoundSlot BackSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
	FUISoundSlot TabSound;

	/** A change that couldn't be made (a disabled setting, a reserved key). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sounds")
	FUISoundSlot ErrorSound;

	// ---- Custom rows ----

	/** Your own Widget Blueprint (a Menu Setting Row Widget) for every setting of a kind, instead of the built-in row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Rows")
	TMap<EMenuSettingKind, TSoftClassPtr<UMenuSettingRowWidget>> RowWidgetClasses;
};

/**
 * A menu look as an asset: settings screens and menu dialogs given this asset take every colour, font, size, motion and sound
 * from it - edit it and every screen using it changes (live, in the editor and in game with Notify Style Changed). Pick a Preset
 * to start from.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Menu Style"))
class NAMETBD_API UMenuStyleAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	/** Picking one sets every value below. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	EMenuStylePreset Preset = EMenuStylePreset::Simple;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FMenuStyle Style;

	UMenuStyleAsset();

	/** Puts a preset's values in (keeping nothing of the old). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Style")
	void ApplyPreset(EMenuStylePreset InPreset);

	/** Tells every screen using this style to redraw (after changing Style at run time). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Style")
	void NotifyStyleChanged();

	/** Fired when any style asset changes (edited, or Notify Style Changed). */
	static TMulticastDelegate<void(const UMenuStyleAsset*)>& OnStyleChanged();

	/** The asset's style, else the project's Default Style, else Simple. */
	static const FMenuStyle& Resolve(const UMenuStyleAsset* Asset);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
