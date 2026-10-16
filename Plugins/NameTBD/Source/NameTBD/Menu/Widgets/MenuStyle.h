// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Types/SlateEnums.h"
#include "NameTBD/Menu/Input/MenuKeyPrompt.h"
#include "NameTBD/Menu/Settings/MenuSetting.h"
#include "NameTBD/Menu/Settings/MenuSettingTypes.h"
#include "NameTBD/Sound/UISoundTypes.h"
#include "NameTBD/Widgets/WidgetEffectImage.h"
#include "NameTBD/Widgets/WidgetMotion.h"

#include "MenuStyle.generated.h"

class UMenuDescriptionWidget;
class UMenuDialogWidget;
class UMenuFooterPromptWidget;
class UMenuHeadingWidget;
class UMenuOptionListWidget;
class UMenuSearchWidget;
class UMenuSettingRowWidget;
class UMenuTabWidget;

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

/**
 * One part of a settings screen drawn as art: an image (a texture, a material or an SVG) for each state, drawn instead of the
 * built-in shape (or over it, with Keep Built In). A state left off uses Normal. Which state shows: Disabled, else Pressed, else
 * Selected, else Focused, else Hovered, else Normal.
 *
 * A material gets its own dynamic instance per part and these live inputs each time it is drawn (scalar parameters unless said):
 * Hover and Focus (0..1, eased with the style's motion), Pressed, Selected, Disabled (0 / 1), Value (a slider's or switch's 0..1, a
 * selector's option as 0..1; -1 where there is none), Time (real seconds), Width and Height (the part's size), and the vectors
 * PaletteColor (the colour the built-in look gives this part now) and Accent - so one material can do every state itself.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuPartSkin
{
	GENERATED_BODY()

	/** The part at rest. Nothing set: the built-in shape in the palette's colours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	FWidgetEffectImage Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (InlineEditConditionToggle))
	bool bHoveredImage = false;

	/** Under the pointer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (EditCondition = "bHoveredImage"))
	FWidgetEffectImage Hovered;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (InlineEditConditionToggle))
	bool bFocusedImage = false;

	/** Keyboard / gamepad focus on it (a row, a button). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (EditCondition = "bFocusedImage"))
	FWidgetEffectImage Focused;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (InlineEditConditionToggle))
	bool bPressedImage = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (EditCondition = "bPressedImage"))
	FWidgetEffectImage Pressed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (InlineEditConditionToggle))
	bool bSelectedImage = false;

	/** The current tab, the picked segment or list option, a switch that is on, the current pip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (EditCondition = "bSelectedImage"))
	FWidgetEffectImage Selected;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (InlineEditConditionToggle))
	bool bDisabledImage = false;

	/** A setting that can't be changed now, a button that can't be used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (EditCondition = "bDisabledImage"))
	FWidgetEffectImage Disabled;

	/**
	 * Multiplies the image by the colour the built-in look gives this part now (the palette, High Contrast included), so one white
	 * image follows every palette and state colour. Off: the image's own colours (times Tint).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	bool bTintWithPalette = false;

	/** Multiplies the image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	FLinearColor Tint = FLinearColor::White;

	/** Grows the drawn image past the part's box on each side (a glow, a frame, a shadow round it); negative shrinks it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	FMargin Outset = FMargin(0.f);

	/** Also draws the built-in shape, under the image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	bool bKeepBuiltIn = false;

	/** Whether any image is set. */
	bool IsSet() const;

	/** Whether the built-in shape is drawn (nothing set, or Keep Built In). */
	bool DrawsBuiltIn() const { return !IsSet() || bKeepBuiltIn; }

	/** The image for a state (the state's own when switched on and set, else Normal). */
	const FWidgetEffectImage& ForState(bool bDisabled, bool bPressed, bool bSelected, bool bFocused, bool bHovered) const;

	/** Whether any image is a material (it may animate: the screen repaints every frame). */
	bool UsesMaterial() const;
};

/**
 * Art for every part of a settings screen and menu dialog, for an art-directed look: frames, brushed panels, glowing focus bars,
 * custom slider knobs. Each part takes an image per state (texture, material or SVG); parts left empty keep the built-in drawing.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuStyleSkin
{
	GENERATED_BODY()

	// ---- Rows ----

	/** Behind each setting row (states: hovered, focused, disabled). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	FMenuPartSkin Row;

	/** The bar at the focused row's left edge (with the style's Focus Bar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	FMenuPartSkin FocusBar;

	/** The dot before a setting changed from its default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	FMenuPartSkin ModifiedDot;

	/** Behind each section heading ("DISPLAY"). Built-in: nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	FMenuPartSkin SectionHeading;

	/** Behind each key binding category heading ("Movement"). Built-in: nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	FMenuPartSkin CategoryHeading;

	// ---- Tabs ----

	/** Behind the row of tabs. Built-in: nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tabs")
	FMenuPartSkin TabBar;

	/** Each tab (selected: the current page). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tabs")
	FMenuPartSkin Tab;

	/** The current tab's underline (Underline tab style). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tabs")
	FMenuPartSkin TabIndicator;

	// ---- Controls ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin SwitchTrack;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin SwitchKnob;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin SliderTrack;

	/** The filled part of a slider's track: drawn over the whole track and cut at the value, so an image isn't squashed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin SliderFill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin SliderThumb;

	/** Behind a selector (< High >). Built-in: nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin SelectorBox;

	/** A selector's arrow, drawn pointing left; the right one is it turned round (pressed: the step just made). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin SelectorArrow;

	/** One option's pip under a selector (selected: the current option). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin Pip;

	/** Choices shown as buttons side by side (selected: the picked one). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin Segment;

	/** A dropdown's box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin Dropdown;

	/** A dropdown's arrow, drawn pointing down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin DropdownArrow;

	/** Each key slot of a key binding row (selected: waiting for a key). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	FMenuPartSkin KeySlot;

	// ---- Buttons ----

	/** Filled buttons: dialogs' main choice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FMenuPartSkin PrimaryButton;

	/** Outlined buttons: action rows, dialogs' other choices. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FMenuPartSkin SecondaryButton;

	/** Text buttons: Revert Changes, a dialog's Cancel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FMenuPartSkin GhostButton;

	/** Behind each footer prompt ([B] Back). Built-in: nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FMenuPartSkin FooterPrompt;

	// ---- Panels ----

	/** Behind the search box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels")
	FMenuPartSkin SearchBox;

	/** The description panel at the side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels")
	FMenuPartSkin DescriptionPanel;

	/** Behind the footer. Built-in: nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels")
	FMenuPartSkin Footer;

	/** A dialog's card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels")
	FMenuPartSkin DialogCard;

	/** A dropdown list's card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels")
	FMenuPartSkin ListCard;

	/** One option of a dropdown list (selected: the current value). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels")
	FMenuPartSkin ListOption;

	/** Over the screen behind a dialog or list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panels")
	FMenuPartSkin Scrim;

	// ---- Scroll bar (a texture or material only: SVGs and states other than hovered / pressed don't apply) ----

	/** The scroll bar's thumb (hovered; pressed: dragged). Built-in: the palette's Track colour, rounded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar")
	FMenuPartSkin ScrollThumb;

	/** Behind the thumb. Built-in: nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scroll Bar")
	FMenuPartSkin ScrollTrack;

	/** Whether any part uses a material. */
	bool UsesMaterial() const;
};

/** What a widget rule replaces. */
UENUM(BlueprintType)
enum class EMenuWidgetRulePart : uint8
{
	Row UMETA(ToolTip = "The whole row: your widget is the row (its name, its control, its highlight). The screen still gives it focus, the description panel, narration, sounds and the footer actions."),
	Control UMETA(ToolTip = "Only the control on the right of the built-in row (the switch, slider, selector, keys...): the row keeps its name, highlight, focus bar, modified dot and messages.")
};

/**
 * Your own UMG widget (a Menu Setting Row Widget, or a Menu Setting Control Widget for Part Control) for the settings a rule picks.
 * Every filled-in test must pass; empty tests pass. The first rule that matches a setting wins, before Custom Rows by kind.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuWidgetRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	EMenuWidgetRulePart Part = EMenuWidgetRulePart::Row;

	/** Setting Ids, with wildcards ("Video.*", "Audio.*Volume"). Empty: any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	TArray<FString> SettingIds;

	/** Section Ids (or titles). Empty: any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	TArray<FName> Sections;

	/** Page Ids. Empty: any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	TArray<FName> Pages;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule", meta = (InlineEditConditionToggle))
	bool bMatchKind = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule", meta = (EditCondition = "bMatchKind"))
	EMenuSettingKind Kind = EMenuSettingKind::Choice;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule", meta = (InlineEditConditionToggle))
	bool bMatchChoiceDisplay = false;

	/** How a Choice shows, Auto worked out first (a selector, or a dropdown past Selector Max Options). Only Choices match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule", meta = (EditCondition = "bMatchChoiceDisplay"))
	EMenuChoiceDisplay ChoiceDisplay = EMenuChoiceDisplay::Selector;

	/** The widget made for each matching setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	TSoftClassPtr<UMenuSettingRowWidget> WidgetClass;

	/** Whether the rule takes this setting (ResolvedDisplay: its Choice display with Auto worked out). */
	bool Matches(const UMenuSetting& Setting, FName PageId, FName SectionId, const FText& SectionTitle, EMenuChoiceDisplay ResolvedDisplay) const;
};

/**
 * Your own UMG widgets for the parts of a settings screen, each a Widget Blueprint of the part's class: the screen makes one per
 * tab / heading / prompt..., fills it through its events, and keeps doing the work (input, focus, sounds, narration). Empty: the
 * built-in part.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuStyleWidgets
{
	GENERATED_BODY()

	/** Each page tab (Menu Tab Widget). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widgets")
	TSoftClassPtr<UMenuTabWidget> Tab;

	/** Section and key binding category headings (Menu Heading Widget). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widgets")
	TSoftClassPtr<UMenuHeadingWidget> Heading;

	/** Each footer prompt and the tab-change prompts (Menu Footer Prompt Widget). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widgets")
	TSoftClassPtr<UMenuFooterPromptWidget> FooterPrompt;

	/** The description panel (Menu Description Widget). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widgets")
	TSoftClassPtr<UMenuDescriptionWidget> Description;

	/** The search box (Menu Search Widget). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widgets")
	TSoftClassPtr<UMenuSearchWidget> Search;

	/** Dialogs: keep display changes, unsaved changes, confirmations (Menu Dialog Widget). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widgets")
	TSoftClassPtr<UMenuDialogWidget> Dialog;

	/** Dropdown lists (Menu Option List Widget). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widgets")
	TSoftClassPtr<UMenuOptionListWidget> OptionList;

	/** Rows or controls for chosen settings: by Id, section, page, kind or how a Choice shows. First match wins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widgets", meta = (TitleProperty = "WidgetClass"))
	TArray<FMenuWidgetRule> Rules;

	/** The class of the first rule matching a setting, null when none does. */
	TSoftClassPtr<UMenuSettingRowWidget> FindRule(EMenuWidgetRulePart Part, const UMenuSetting& Setting, FName PageId, FName SectionId, const FText& SectionTitle,
		EMenuChoiceDisplay ResolvedDisplay) const;
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

	/** Where the title sits in its line (left; centred for a symmetric layout). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	TEnumAsByte<EHorizontalAlignment> TitleAlignment = HAlign_Fill;

	/** Where the tabs sit in their line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	TEnumAsByte<EHorizontalAlignment> TabsAlignment = HAlign_Left;

	/** Around each tab's name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FMargin TabPadding = FMargin(14.f, 8.f);

	/** Around a button's text (dialogs, action rows). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FMargin ButtonPadding = FMargin(18.f, 10.f);

	/** The switch's size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FVector2D SwitchSize = FVector2D(52.0, 28.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "1", UIMax = "40"))
	float SliderTrackHeight = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMax = "64"))
	float SliderThumbSize = 16.f;

	/** Half the height of a selector's arrows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "2", UIMax = "32"))
	float SelectorArrowSize = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "1", UIMax = "16"))
	float PipHeight = 3.f;

	/** A dialog's width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "200", UIMax = "1200"))
	float DialogWidth = 520.f;

	/** The scroll bar's thickness. 0: the engine's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMax = "24"))
	float ScrollBarThickness = 0.f;

	// ---- Text ----

	/** Tab names in capitals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	bool bUppercaseTabs = false;

	/** Section headings in capitals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	bool bUppercaseSections = true;

	/** Setting names in capitals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	bool bUppercaseLabels = false;

	/** Button and footer prompt texts in capitals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Type")
	bool bUppercaseButtons = false;

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

	// ---- Skin ----

	/** Art for each part and state (textures, materials, SVGs) instead of the built-in shapes. Parts left empty keep the built-in look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin")
	FMenuStyleSkin Skin;

	/** Your own UMG widgets for tabs, headings, prompts, the description panel, search, dialogs, lists, and rows or controls by rule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Widgets")
	FMenuStyleWidgets Widgets;

	// ---- Custom rows ----

	/** Your own Widget Blueprint (a Menu Setting Row Widget) for every setting of a kind, instead of the built-in row (Custom Widgets > Rules come first). */
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
