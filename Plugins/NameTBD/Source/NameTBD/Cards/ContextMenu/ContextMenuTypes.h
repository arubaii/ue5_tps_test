// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Commands/InputChord.h"
#include "Framework/Text/TextLayout.h"
#include "GameplayTagContainer.h"
#include "Layout/Margin.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Types/SlateEnums.h"

#include "NameTBD/Cards/InfoCardTextWidget.h"
#include "NameTBD/Cards/InfoCardTypes.h"
#include "NameTBD/Widgets/WidgetSVG.h"

#include "ContextMenuTypes.generated.h"

class UButtonAction;
class UContextMenuAsset;
class UContextMenuButtonPresetAsset;
class UContextMenuEntryWidget;
class UContextMenuStyleAsset;
class UContextMenuWidget;
class UInfoCardAnchor;
class UInfoCardDataAsset;
class UInputAction;
class UTexture2D;

/** What one line of a context menu is. */
UENUM(BlueprintType)
enum class EContextMenuEntryType : uint8
{
	Button UMETA(ToolTip = "A row that runs its actions when clicked (or chosen with Enter, a gamepad's confirm, or its shortcut)."),
	Submenu UMETA(ToolTip = "A row that opens another Context Menu beside it: on hover after the style's Submenu Delay, on click, or with Right."),
	Divider UMETA(ToolTip = "A line between groups of rows."),
	Header UMETA(DisplayName = "Text Divider", ToolTip = "A label for the rows below it, aligned left, centre or right, with an optional line: Category -------"),
	Spacer UMETA(ToolTip = "Empty space.")
};

/** Whether a row is a check box or a radio button, showing a mark while checked. */
UENUM(BlueprintType)
enum class EContextMenuCheckType : uint8
{
	None,
	CheckBox UMETA(DisplayName = "Check Box", ToolTip = "Clicking flips it on and off."),
	Radio UMETA(DisplayName = "Radio Button", ToolTip = "Clicking turns it on and every other row with the same Radio Group off.")
};

/** How a row's label is coloured. */
UENUM(BlueprintType)
enum class EContextMenuEntryEmphasis : uint8
{
	Normal,
	Destructive UMETA(ToolTip = "Drawn in the style's Destructive Label Color: Delete, Remove, Leave Party.")
};

/** Where an anchor's extra entry goes among the menu asset's. */
UENUM(BlueprintType)
enum class EContextMenuInsertPosition : uint8
{
	AtEnd UMETA(DisplayName = "At End"),
	AtStart UMETA(DisplayName = "At Start"),
	AfterEntry UMETA(DisplayName = "After Entry"),
	BeforeEntry UMETA(DisplayName = "Before Entry")
};

/** Where a Text Divider's text sits: the style's Text Divider Alignment, or this entry's own. */
UENUM(BlueprintType)
enum class EContextMenuTextDividerAlignment : uint8
{
	StyleDefault UMETA(DisplayName = "Style Default", ToolTip = "The style's Text Divider Alignment."),
	Left,
	Center,
	Right
};

/** How a row's shortcuts are drawn. */
UENUM(BlueprintType)
enum class EContextMenuBindingDisplay : uint8
{
	Text UMETA(ToolTip = "Key names as text: Ctrl+Shift+T."),
	Image UMETA(ToolTip = "Each key's art from Project Settings > Menus > Key Glyph Sets, for the device the player is using. A key with no art there shows its name.")
};

/** How one row draws its shortcuts: as the style says, or its own way. */
UENUM(BlueprintType)
enum class EContextMenuEntryBindingDisplay : uint8
{
	StyleDefault UMETA(DisplayName = "Style Default", ToolTip = "The style's Bindings > Display."),
	Text UMETA(ToolTip = "Key names as text: Ctrl+Shift+T."),
	Image UMETA(ToolTip = "Each key's art from Project Settings > Menus > Key Glyph Sets. A key with no art there shows its name.")
};

/** Where the frame (the style's Frame Brush) goes on a row's shortcuts. */
UENUM(BlueprintType)
enum class EContextMenuBindingFrame : uint8
{
	None UMETA(ToolTip = "No frame."),
	EachKey UMETA(DisplayName = "Each Key", ToolTip = "A frame around every key. The delimiters between keys stay outside the frames: [Ctrl] + [C]."),
	WholeBinding UMETA(DisplayName = "Whole Binding", ToolTip = "One frame around all the keys of a binding, delimiters inside: [Ctrl + C].")
};

/** How a row shows its Description when the cursor rests on it. */
UENUM(BlueprintType)
enum class EContextMenuTooltipMode : uint8
{
	EngineTooltip UMETA(DisplayName = "Engine Tooltip", ToolTip = "The engine's plain tooltip, following the cursor."),
	TooltipCard UMETA(DisplayName = "Tooltip Card", ToolTip = "An info card tooltip (a Tooltip Card Anchor around the row): its template, text style, delay and positioning set here.")
};

/** What opens the menu attached to a Button row. */
UENUM(BlueprintType)
enum class EContextMenuAttachedMenuTrigger : uint8
{
	Hover UMETA(ToolTip = "The cursor resting on the row (after the style's Submenu Delay), or Right on the keyboard. Clicking the row still runs it and closes the menu as usual."),
	Press UMETA(ToolTip = "Clicking the row (or Enter / a gamepad's confirm, or Right): its actions run, the attached menu opens beside it, and the menu the row is in stays open.")
};

/** An icon on a menu row: a texture or an SVG (drawn sharp at the size it is shown). */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuIcon
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon", meta = (DisplayName = "Image Source"))
	EWidgetImageSource ImageSource = EWidgetImageSource::Texture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon", meta = (EditCondition = "ImageSource == EWidgetImageSource::Texture", EditConditionHides))
	TObjectPtr<UTexture2D> Texture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon", meta = (DisplayName = "SVG", EditCondition = "ImageSource == EWidgetImageSource::SVG", EditConditionHides))
	TObjectPtr<UWidgetSVG> SVG = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon", meta = (InlineEditConditionToggle))
	bool bOverrideTint = false;

	/** This icon's colour, instead of the style's icon tint for the row's state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon", meta = (EditCondition = "bOverrideTint"))
	FLinearColor Tint = FLinearColor::White;

	bool IsSet() const { return ImageSource == EWidgetImageSource::SVG ? SVG != nullptr : Texture != nullptr; }
};

/** How one text part of a row sits: its justification, and its alignment in the space the row gives it. */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuTextAlignment
{
	GENERATED_BODY()

	FContextMenuTextAlignment() = default;
	FContextMenuTextAlignment(ETextJustify::Type InJustification, EHorizontalAlignment InHorizontal)
		: Justification(InJustification), HorizontalAlignment(InHorizontal)
	{
	}

	/** How the lines of the text line up inside the text's own box (which is as wide as its space when Horizontal is Fill). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
	TEnumAsByte<ETextJustify::Type> Justification = ETextJustify::Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment", meta = (DisplayName = "Horizontal"))
	TEnumAsByte<EHorizontalAlignment> HorizontalAlignment = HAlign_Fill;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment", meta = (DisplayName = "Vertical"))
	TEnumAsByte<EVerticalAlignment> VerticalAlignment = VAlign_Center;
};

/**
 * How rows show their Description (the row's tooltip): the engine's plain tooltip, or an info card tooltip with every setting
 * a Tooltip Card Anchor has - template, text style, delay, and the Info Card Anchor's positioning.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuTooltipStyle
{
	GENERATED_BODY()

	FContextMenuTooltipStyle();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tooltips")
	EContextMenuTooltipMode Mode = EContextMenuTooltipMode::EngineTooltip;

	/**
	 * Where the tooltip's card class, trigger, delay, placement, sounds and animations come from: an Info Card Data Asset's
	 * entry (Template Card ID, else its first). Empty: Project Settings > Info Card > Tooltips > Default Tooltip Template,
	 * else the built-in tooltip (a Text Info Card).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tooltips", meta = (DisplayName = "Tooltip Template", EditCondition = "Mode == EContextMenuTooltipMode::TooltipCard", EditConditionHides))
	TObjectPtr<UInfoCardDataAsset> Template = nullptr;

	/** Which entry of the template. None: its first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tooltips", meta = (DisplayName = "Template Card ID", EditCondition = "Mode == EContextMenuTooltipMode::TooltipCard", EditConditionHides))
	FName TemplateCardId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tooltips", meta = (DisplayName = "Override Text Style", EditCondition = "Mode == EContextMenuTooltipMode::TooltipCard", EditConditionHides))
	bool bOverrideTextStyle = false;

	/** The tooltip's look when its card is a Text Info Card (the built-in one is): panel, padding, width, fonts, colours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tooltips", meta = (EditCondition = "Mode == EContextMenuTooltipMode::TooltipCard && bOverrideTextStyle", EditConditionHides))
	FInfoCardTextStyle TextStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tooltips", meta = (DisplayName = "Override Delay", EditCondition = "Mode == EContextMenuTooltipMode::TooltipCard", EditConditionHides))
	bool bOverrideDelay = true;

	/** Seconds the cursor rests on a row before its tooltip shows, instead of the template's Highlight Delay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tooltips", meta = (EditCondition = "Mode == EContextMenuTooltipMode::TooltipCard && bOverrideDelay", EditConditionHides, ClampMin = "0", UIMax = "3", Units = "s"))
	float Delay = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tooltips", meta = (DisplayName = "Override Positioning", EditCondition = "Mode == EContextMenuTooltipMode::TooltipCard", EditConditionHides))
	bool bOverridePositioning = true;

	/**
	 * Where the tooltip opens, instead of the template's: every placement an Info Card Anchor's entry has. Default: below and
	 * to the right of the cursor, clear of a submenu opening beside the row.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tooltips", meta = (EditCondition = "Mode == EContextMenuTooltipMode::TooltipCard && bOverridePositioning", EditConditionHides))
	FInfoCardPositioning Positioning;
};

/** How rows show their shortcuts (Shortcut, More Shortcuts, Gamepad Shortcut, Shortcut Input Action). */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuBindingStyle
{
	GENERATED_BODY()

	FContextMenuBindingStyle();

	/** Text: key names. Image: the keys' art from the project's Key Glyph Sets (no picking images per row). A row can choose its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings")
	EContextMenuBindingDisplay Display = EContextMenuBindingDisplay::Text;

	/** Between the keys of one binding: Ctrl+C. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings")
	FText KeyDelimiter;

	/** Between a row's bindings when it shows more than one: Ctrl+C, Ctrl+Insert. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings")
	FText BindingSeparator;

	/** Space on each side of a delimiter and a separator. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings", meta = (ClampMin = "0", UIMax = "16", ForceUnits = "px"))
	float KeyGap = 0.f;

	/** A frame around each key, or around each whole binding (Frame Brush). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings")
	EContextMenuBindingFrame Frame = EContextMenuBindingFrame::None;

	/** The frame's look: a rounded box with an outline is a key cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings", meta = (EditCondition = "Frame != EContextMenuBindingFrame::None", EditConditionHides))
	FSlateBrush FrameBrush;

	/** Space between the frame and what it holds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings", meta = (EditCondition = "Frame != EContextMenuBindingFrame::None", EditConditionHides))
	FMargin FramePadding = FMargin(5.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings", meta = (InlineEditConditionToggle))
	bool bOverrideFont = false;

	/** The keys' font, instead of the style's Shortcut Font. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings", meta = (EditCondition = "bOverrideFont"))
	FSlateFontInfo Font;

	/** Height of a key's art (Image); wide art keeps its proportions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings", meta = (ClampMin = "4", UIMax = "48", ForceUnits = "px"))
	float GlyphHeight = 16.f;

	/** At most this many bindings per row, in order (Shortcut first). 0: all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings", meta = (ClampMin = "0", UIMax = "4"))
	int32 MaxBindings = 0;

	/** Rows' Gamepad Shortcuts (and Input Actions' gamepad keys) show only while the player uses a gamepad. Off: always. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings")
	bool bGamepadBindingsOnlyOnGamepad = true;

	/** While the player uses a gamepad, keyboard shortcuts aren't shown (a row with a Gamepad Shortcut shows that). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings")
	bool bHideKeyboardBindingsOnGamepad = false;
};

/**
 * One line of a context menu: a button, a submenu, a divider, a text divider or a spacer. Rows fill the menu top-down in
 * array order.
 *
 * The editor module groups the fields (Entry, Content, Action, State, Look) and only shows the ones the Entry Type uses.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Entry Type"))
	EContextMenuEntryType EntryType = EContextMenuEntryType::Button;

	/**
	 * Names this entry for anchors' Entry Overrides, Extra Entries' Before / After, Blueprint (Set Entry Enabled...) and the
	 * On Menu Entry Clicked event. Filled from the label when left empty; keep it once anchors use it, as renaming breaks
	 * their overrides.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Entry ID"))
	FName EntryId = NAME_None;

	// Content ------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	FText Label;

	/** Shown as the row's tooltip on hover - what it does, or why it is disabled. The style's Row Tooltips decide its look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (MultiLine = true, EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	FText Description;

	/**
	 * Before everything else, in the icon column, in order (the style's Icon Gap and Icon Delimiter between them). The column
	 * is as wide as the row with the most left icons, so every label in the menu starts at the same place. A check box or
	 * radio row shows its check mark there instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Left Icons", EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	TArray<FContextMenuIcon> LeftIcons;

	/** Right before the label, in order. With no label, an icon-only row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Center Icons", EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	TArray<FContextMenuIcon> CenterIcons;

	/** On the right, after Right Text and the shortcut, in order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Right Icons", EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	TArray<FContextMenuIcon> RightIcons;

	/** Text in the middle of the row, between the label (the left text) and Right Text, aligned by the style's Center Text Alignment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Center Text", EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	FText CenterText;

	/** Free text on the right, in the shortcut's font: a count, a value, a hint. Aligned by the style's Right Text Alignment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Right Text", EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	FText RightText;

	// Shortcut -----------------------------------------------------------

	/**
	 * A keyboard shortcut, shown on the right ("Ctrl+C"). While the menu is open, pressing it chooses this row (the menu
	 * asset's Shortcuts Work While Open). Showing it doesn't bind it anywhere else in the game.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shortcut", meta = (EditCondition = "EntryType == EContextMenuEntryType::Button", EditConditionHides))
	FInputChord Shortcut;

	/** Other shortcuts for the same row (Ctrl+Insert as well as Ctrl+C): shown after Shortcut, and working while open too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shortcut", meta = (DisplayName = "More Shortcuts", EditCondition = "EntryType == EContextMenuEntryType::Button", EditConditionHides))
	TArray<FInputChord> MoreShortcuts;

	/** A gamepad button for this row: shown while the player uses a gamepad (the style's Bindings decide), working while open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shortcut", meta = (DisplayName = "Gamepad Shortcut", EditCondition = "EntryType == EContextMenuEntryType::Button", EditConditionHides))
	FKey GamepadShortcut;

	/**
	 * An Enhanced Input action whose keys this row shows: the key the player has it on for keyboard and for gamepad, their
	 * rebinding included (Menu Input Subsystem). Those keys choose the row while the menu is open.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shortcut", meta = (DisplayName = "Shortcut Input Action", EditCondition = "EntryType == EContextMenuEntryType::Button", EditConditionHides))
	TObjectPtr<UInputAction> ShortcutAction = nullptr;

	/** Text or the keys' art (Key Glyph Sets), instead of the style's Bindings > Display. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shortcut", meta = (DisplayName = "Shortcut Display", EditCondition = "EntryType == EContextMenuEntryType::Button", EditConditionHides))
	EContextMenuEntryBindingDisplay ShortcutDisplay = EContextMenuEntryBindingDisplay::StyleDefault;

	// Content (Text Divider, Spacer) -------------------------------------

	/** Text Divider: its text. Empty: a plain divider line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Text", EditCondition = "EntryType == EContextMenuEntryType::Header", EditConditionHides))
	FText HeaderText;

	/** Text Divider: where the text sits (the line fills the rest): the style's Text Divider Alignment, or this one's own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Text Alignment", EditCondition = "EntryType == EContextMenuEntryType::Header", EditConditionHides))
	EContextMenuTextDividerAlignment TextDividerAlignment = EContextMenuTextDividerAlignment::StyleDefault;

	/** Text Divider: draw the line beside the text. Off: the text alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Show Line", EditCondition = "EntryType == EContextMenuEntryType::Header", EditConditionHides))
	bool bShowDividerLine = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Height", EditCondition = "EntryType == EContextMenuEntryType::Spacer", EditConditionHides, ClampMin = "0", UIMax = "48", ForceUnits = "px"))
	float SpacerHeight = 8.f;

	// Action -------------------------------------------------------------

	/**
	 * What choosing this row does, in order. Pick an action class and fill it in right here (Run Console Command, Copy Text,
	 * Call Function, Open URL, Play Sound, or any Button Action subclass of your own). Each open menu gets its own copies,
	 * so an action may keep state for as long as its menu is open.
	 *
	 * Nothing here at all is fine too: the anchor's On Menu Entry Clicked fires with this Entry ID either way.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Action", meta = (EditCondition = "EntryType == EContextMenuEntryType::Button", EditConditionHides))
	TArray<TObjectPtr<UButtonAction>> Actions;

	/**
	 * Also runs the actions of this entry of Project Settings > Info Card > Button Actions (handed the payloads it expects,
	 * by class, from the menu's card), and takes its text and icons when this entry has none of its own.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (DisplayName = "Button Tag", Categories = "UIContext.Button", EditCondition = "EntryType == EContextMenuEntryType::Button", EditConditionHides))
	FGameplayTag ButtonTag;

	/** Choosing this row closes the whole menu (submenus included). Off: it stays open - a toggle the player flips several of. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (DisplayName = "Close Menu On Click", EditCondition = "EntryType == EContextMenuEntryType::Button", EditConditionHides))
	bool bCloseMenuOnClick = true;

	/** Submenu: the menu it opens. Its own Style is ignored - a submenu always looks like its parent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (EditCondition = "EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	TObjectPtr<UContextMenuAsset> Submenu = nullptr;

	// Attached Menu ------------------------------------------------------

	/**
	 * A menu of its own beside this Button row, opened on hover or on press (Open On): a split button - "Open" runs, its arrow
	 * of choices opens beside it - or a nested menu on a row that still does something when clicked.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attached Menu", meta = (DisplayName = "Attached Menu", EditCondition = "EntryType == EContextMenuEntryType::Button", EditConditionHides))
	TObjectPtr<UContextMenuAsset> AttachedMenu = nullptr;

	/** The attached menu's look. Empty: the menu this row is in (as a submenu). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attached Menu", meta = (DisplayName = "Style", EditCondition = "EntryType == EContextMenuEntryType::Button && AttachedMenu != nullptr", EditConditionHides))
	TObjectPtr<UContextMenuStyleAsset> AttachedMenuStyle = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attached Menu", meta = (DisplayName = "Open On", EditCondition = "EntryType == EContextMenuEntryType::Button && AttachedMenu != nullptr", EditConditionHides))
	EContextMenuAttachedMenuTrigger AttachedMenuOpenOn = EContextMenuAttachedMenuTrigger::Hover;

	/** Where it opens. Default: beside the row, on its right (flipping left at the screen's edge), as a hover dropdown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attached Menu", meta = (DisplayName = "Positioning", EditCondition = "EntryType == EContextMenuEntryType::Button && AttachedMenu != nullptr", EditConditionHides))
	FInfoCardPositioning AttachedMenuPositioning = FInfoCardPositioning::MakeBeside(EDirection2D::Right, 4.f);

	// State --------------------------------------------------------------

	/** Off: shown greyed out, can't be chosen, and keyboard / gamepad navigation skips it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (DisplayName = "Enabled", EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (DisplayName = "Check Type", EditCondition = "EntryType == EContextMenuEntryType::Button", EditConditionHides))
	EContextMenuCheckType CheckType = EContextMenuCheckType::None;

	/** Checked until the player (or Set Entry Checked) changes it; an anchor remembers the player's choice for its next opening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (DisplayName = "Checked", EditCondition = "EntryType == EContextMenuEntryType::Button && CheckType != EContextMenuCheckType::None", EditConditionHides))
	bool bChecked = false;

	/** Radio buttons with the same group in the same menu turn each other off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (DisplayName = "Radio Group", EditCondition = "EntryType == EContextMenuEntryType::Button && CheckType == EContextMenuCheckType::Radio", EditConditionHides))
	FName RadioGroup = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State", meta = (EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	EContextMenuEntryEmphasis Emphasis = EContextMenuEntryEmphasis::Normal;

	// Look ---------------------------------------------------------------

	/** This row's button look, instead of the style's Row Button Preset (a red Delete, a highlighted default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (DisplayName = "Row Button Preset", EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	TObjectPtr<UContextMenuButtonPresetAsset> RowButtonPreset = nullptr;

#if WITH_EDITORONLY_DATA
	// Before rows had icon lists and Text Dividers a Style Default: loaded through CoreRedirects (Config/DefaultNameTBD.ini),
	// moved into LeftIcons... / TextDividerAlignment by PostSerialize, never saved again (CPF_Deprecated).
	UPROPERTY()
	FContextMenuIcon LeftIcon_DEPRECATED;

	UPROPERTY()
	FContextMenuIcon CenterIcon_DEPRECATED;

	UPROPERTY()
	FContextMenuIcon RightIcon_DEPRECATED;

	UPROPERTY()
	TEnumAsByte<EHorizontalAlignment> HeaderAlignment_DEPRECATED = HAlign_Left;
#endif

	/** A row the player can choose (a button or a submenu that is enabled). */
	bool IsSelectable() const
	{
		return bEnabled && (EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu);
	}

	/** A Button row with an Attached Menu. */
	bool HasAttachedMenu() const { return EntryType == EContextMenuEntryType::Button && AttachedMenu != nullptr; }

	/** Left Icons that are set (an empty element in the list is skipped). */
	int32 CountLeftIcons() const;

	/** The text the Entry ID is filled from when empty. */
	FString GetIdSourceText() const;

	void PostSerialize(const FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FContextMenuEntry> : public TStructOpsTypeTraitsBase2<FContextMenuEntry>
{
	enum
	{
		WithPostSerialize = true
	};
};

/**
 * Everything about how context menus look and behave, kept in a Context Menu Style asset so one look serves every menu that
 * picks it (and Project Settings > Info Card > Context Menus > Default Context Menu Style serves every other menu).
 *
 * The rows are Context Menu Buttons: give the style a Row Button Preset (style a button, Save As Preset) and every row gets
 * that button's shapes, design targets, sounds and animations - row parts are named for targets (see UContextMenuEntryWidget).
 * Without one, the native look below draws the rows.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuStyle
{
	GENERATED_BODY()

	FContextMenuStyle();

	// Panel ------------------------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel")
	FSlateBrush PanelBackground;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel")
	FMargin PanelPadding = FMargin(4.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel", meta = (ClampMin = "0", UIMax = "600", ForceUnits = "px"))
	float MinWidth = 180.f;

	/** 0: no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel", meta = (ClampMin = "0", UIMax = "1000", ForceUnits = "px"))
	float MaxWidth = 420.f;

	/** Past this the rows scroll. 0: no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel", meta = (ClampMin = "0", UIMax = "1600", ForceUnits = "px"))
	float MaxHeight = 640.f;

	// Rows -------------------------------------------------------------------------------------------------------------

	/**
	 * Every row's button look: a Context Menu Button Preset (shapes, design targets per state, sounds, animations). The
	 * native highlight below is then off, since the preset draws the states. Per entry: Row Button Preset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	TObjectPtr<UContextMenuButtonPresetAsset> RowButtonPreset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows", meta = (ClampMin = "0", UIMax = "64", ForceUnits = "px"))
	float RowHeight = 28.f;

	/** Between a row's highlight and its parts (the label starts this far inside the highlight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	FMargin RowPadding = FMargin(8.f, 2.f);

	/**
	 * Between a row's edges and its highlight (or its Row Button Preset's button), on top of the Panel Padding: highlights
	 * that stop short of the panel's sides, or gaps between neighbouring highlights. 0: the highlight is as wide as the menu
	 * less Panel Padding.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows", meta = (DisplayName = "Highlight Inset"))
	FMargin RowHighlightInset = FMargin(0.f);

	/** Where a row's parts sit across the row. Fill: the label takes the room left, so shortcuts line up on the right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows", meta = (DisplayName = "Content Horizontal Alignment"))
	TEnumAsByte<EHorizontalAlignment> RowContentHorizontalAlignment = HAlign_Fill;

	/** Where a row's parts sit in its height (Row Height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows", meta = (DisplayName = "Content Vertical Alignment"))
	TEnumAsByte<EVerticalAlignment> RowContentVerticalAlignment = VAlign_Center;

	/** Drawn behind a row while hovered or focused (no Row Button Preset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	FSlateBrush RowHighlight;

	/** Drawn behind a row while pressed (no Row Button Preset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	FSlateBrush RowPressed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows", meta = (ClampMin = "4", UIMax = "48", ForceUnits = "px"))
	float IconSize = 16.f;

	/** How rows show their Description: the engine's plain tooltip, or an info card tooltip set up here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows", meta = (DisplayName = "Row Tooltips"))
	FContextMenuTooltipStyle RowTooltips;

	/** Space between a row's parts (icon, label, right text, shortcut). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows", meta = (ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float PartGap = 8.f;

	/** Keep the icon column (one icon wide) even when no row has a left icon or check mark, so labels line up across menus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	bool bAlwaysReserveIconColumn = false;

	// Text -------------------------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FSlateFontInfo LabelFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FSlateColor LabelColor = FSlateColor(FLinearColor(0.9f, 0.91f, 0.93f, 1.f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FSlateColor HighlightedLabelColor = FSlateColor(FLinearColor::White);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FSlateColor DisabledLabelColor = FSlateColor(FLinearColor(0.9f, 0.91f, 0.93f, 0.35f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FSlateColor DestructiveLabelColor = FSlateColor(FLinearColor(0.96f, 0.38f, 0.35f, 1.f));

	/** Shortcuts and Right Text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FSlateFontInfo ShortcutFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FSlateColor ShortcutColor = FSlateColor(FLinearColor(0.9f, 0.91f, 0.93f, 0.5f));

	/** The label (a row's left text): left by default, taking the room the row's other parts leave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Label Alignment"))
	FContextMenuTextAlignment LabelAlignment = FContextMenuTextAlignment(ETextJustify::Left, HAlign_Fill);

	/** Center Text (label font and colours): in the middle of the room between the label and the right-hand parts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Center Text Alignment"))
	FContextMenuTextAlignment CenterTextAlignment = FContextMenuTextAlignment(ETextJustify::Center, HAlign_Center);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Right Text Alignment"))
	FContextMenuTextAlignment RightTextAlignment = FContextMenuTextAlignment(ETextJustify::Right, HAlign_Right);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (DisplayName = "Shortcut Alignment"))
	FContextMenuTextAlignment ShortcutAlignment = FContextMenuTextAlignment(ETextJustify::Right, HAlign_Right);

	// Bindings ---------------------------------------------------------------------------------------------------------

	/** How shortcuts are shown: text or the keys' art, the delimiter between keys (+), frames around them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bindings")
	FContextMenuBindingStyle Bindings;

	// Icons ------------------------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FLinearColor IconTint = FLinearColor(0.85f, 0.86f, 0.9f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FLinearColor HighlightedIconTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FLinearColor DisabledIconTint = FLinearColor(0.85f, 0.86f, 0.9f, 0.35f);

	/** Space between the icons of a row's Left / Center / Right Icons (on each side of Icon Delimiter, when there is one). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons", meta = (ClampMin = "0", UIMax = "16", ForceUnits = "px"))
	float IconGap = 4.f;

	/** Drawn between a row's icons, in the shortcut font and colours ("|", "·"). Empty: just Icon Gap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FText IconDelimiter;

	/** Shown on submenu rows. Unset: a chevron drawn as text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FContextMenuIcon SubmenuArrow;

	/** Shown on checked Check Box rows. Unset: a check drawn as text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FContextMenuIcon CheckMark;

	/** Shown on checked Radio Button rows. Unset: a dot drawn as text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FContextMenuIcon RadioMark;

	// Dividers ---------------------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dividers")
	FLinearColor DividerColor = FLinearColor(1.f, 1.f, 1.f, 0.1f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dividers", meta = (ClampMin = "0.5", UIMax = "8", ForceUnits = "px"))
	float DividerThickness = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dividers")
	FMargin DividerPadding = FMargin(8.f, 4.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dividers", meta = (DisplayName = "Text Divider Font"))
	FSlateFontInfo HeaderFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dividers", meta = (DisplayName = "Text Divider Color"))
	FSlateColor HeaderColor = FSlateColor(FLinearColor(0.9f, 0.91f, 0.93f, 0.55f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dividers", meta = (DisplayName = "Text Divider Padding"))
	FMargin HeaderPadding = FMargin(8.f, 6.f, 8.f, 2.f);

	/** Text Dividers in capitals ("INVENTORY"), as many games' menus do. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dividers", meta = (DisplayName = "Uppercase Text Dividers"))
	bool bUppercaseHeaders = false;

	/**
	 * Text Dividers whose Text Alignment is Style Default: Horizontal is where the text sits (the line fills the rest; Fill
	 * reads as Left), Justification lines up text that wraps or stands alone, Vertical places it in the row.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dividers", meta = (DisplayName = "Text Divider Alignment"))
	FContextMenuTextAlignment TextDividerAlignment = FContextMenuTextAlignment(ETextJustify::Left, HAlign_Left);

	// Positioning ------------------------------------------------------------------------------------------------------

	/**
	 * Where menus in this style open: at the cursor (the default - opened from a key, beside the widget), beside the widget
	 * (a dropdown, a flyout) or at a corner of the screen. A Context Menu Anchor can override it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Positioning")
	FInfoCardPositioning Positioning;

	// Behaviour --------------------------------------------------------------------------------------------------------

	/** Seconds the cursor rests on a submenu row before its submenu opens. Clicking or pressing Right opens it at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior", meta = (ClampMin = "0", UIMax = "1", Units = "s"))
	float SubmenuDelay = 0.2f;

	/** Opening a menu closes every other info card (tooltips and other menus) - one context menu at a time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bCloseOtherCardsOnOpen = true;

	/** Typing a letter moves to the next row whose label starts with it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bTypeToSelect = true;

	/** Up / Down at the last / first row go round to the other end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior")
	bool bWrapNavigation = true;

	/** The menu's entrance and exit, on top of the menu class's own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior", meta = (DisplayName = "Open Animations"))
	FInfoCardAdditiveAnimations OpenAnimations;

	/** Sounds as the menu opens (Shown) and closes (Hidden). Row sounds belong to the Row Button Preset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behavior", meta = (DisplayName = "Menu Sounds", TitleProperty = "Settings"))
	TArray<FInfoCardSoundSet> MenuSounds;

	// Custom widgets ---------------------------------------------------------------------------------------------------

	/** The menu itself: a Widget Blueprint made from Context Menu Widget for a frame of your own. Empty: the native one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Widgets")
	TSubclassOf<UContextMenuWidget> MenuWidgetClass;

	/** Every row: a Widget Blueprint made from Context Menu Entry Widget (see that class for the part names it fills). Empty: the native one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Widgets")
	TSubclassOf<UContextMenuEntryWidget> EntryWidgetClass;

	/** Dividers and Text Dividers only, instead of Entry Widget Class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Widgets")
	TSubclassOf<UContextMenuEntryWidget> DividerWidgetClass;

	/** Replaces the engine fonts of a style made in code with the engine's font asset. UObject constructors only. */
	void UseDefaultFontAssets();
};

/** A Context Menu Style: how menus look and behave. Pick it on Context Menu assets, on anchors, or as the project default. */
UCLASS(BlueprintType, meta = (DisplayName = "Context Menu Style"))
class NAMETBD_API UContextMenuStyleAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	UContextMenuStyleAsset();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ShowOnlyInnerProperties))
	FContextMenuStyle Style;

	/** The style a menu uses: Override, else the menu's own, else the project default, else the built-in one. */
	static const FContextMenuStyle& Resolve(const UContextMenuStyleAsset* Override, const UContextMenuAsset* Menu);

	/** The asset Resolve picked, or null for the built-in style. */
	static const UContextMenuStyleAsset* ResolveAsset(const UContextMenuStyleAsset* Override, const UContextMenuAsset* Menu);
};

/**
 * A right-click menu: its rows, top-down, and the style it is drawn in. Open it with a Context Menu Anchor (wrap a widget,
 * pick this asset), or put an Info Card Data (Context Menu) payload on any Info Card entry whose Card Class is a Context Menu
 * Widget.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Context Menu"))
class NAMETBD_API UContextMenuAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu", meta = (TitleProperty = "EntryId"))
	TArray<FContextMenuEntry> Entries;

	/** How this menu looks. Empty: Project Settings > Info Card > Context Menus > Default Context Menu Style. An anchor can still pick another. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu")
	TObjectPtr<UContextMenuStyleAsset> Style = nullptr;

	/** While open, pressing an entry's Shortcut chooses it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu", meta = (DisplayName = "Shortcuts Work While Open"))
	bool bShortcutsWorkWhileOpen = true;

	/** Every Entry ID, in order (entries without one skipped). */
	TArray<FName> GetEntryIds() const;

	/**
	 * Gives every entry without an Entry ID one from its label (Copy, Copy_1...), and makes duplicates unique. A button with
	 * no label yet is skipped unless bEvenWithoutText.
	 */
	void AssignMissingEntryIds(bool bEvenWithoutText);

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

/**
 * One anchor's change to one entry of its menu, kept by Entry ID. Synced from the menu asset; untouched means no change.
 *
 * Each field's EditCondition names its override toggle alone: an inline toggle only shows (and can be ticked) for an
 * EditCondition of a single bool. Remove greys the fields out through the editor's row customization instead.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuEntryOverride
{
	GENERATED_BODY()

	/** Which entry. Kept in sync automatically; shown as the element's title. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Entry ID", EditCondition = "false"))
	FName EntryId = NAME_None;

	/**
	 * Leaves this entry out of this anchor's menu (shown as a check box beside the Entry ID; the fields below grey out while it
	 * is on). To keep the row but grey it out, override Enabled instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Remove"))
	bool bRemove = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (InlineEditConditionToggle))
	bool bOverrideEnabled = false;

	/** Off: the row stays in the menu, greyed out, and can't be chosen (it isn't removed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Enabled", EditCondition = "bOverrideEnabled"))
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (InlineEditConditionToggle))
	bool bOverrideLabel = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Label", EditCondition = "bOverrideLabel"))
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (InlineEditConditionToggle))
	bool bOverrideChecked = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Checked", EditCondition = "bOverrideChecked"))
	bool bChecked = false;

	bool IsUntouched() const { return !bRemove && !bOverrideEnabled && !bOverrideLabel && !bOverrideChecked; }
};

/** An entry an anchor adds to its menu, and where. */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuExtraEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extra Entry")
	EContextMenuInsertPosition Position = EContextMenuInsertPosition::AtEnd;

	/** Before / After which of the menu's entries. One the menu doesn't have: at the end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extra Entry", meta = (DisplayName = "Relative To", GetOptions = "GetMenuEntryIdOptions", EditCondition = "Position == EContextMenuInsertPosition::AfterEntry || Position == EContextMenuInsertPosition::BeforeEntry", EditConditionHides))
	FName RelativeToEntryId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Extra Entry")
	FContextMenuEntry Entry;
};

/** What a Context Menu Anchor (or Blueprint) changed at runtime for one entry: Set Entry Enabled / Hidden / Label / Checked. */
struct FContextMenuEntryRuntimeState
{
	TOptional<bool> bEnabled;
	TOptional<bool> bHidden;
	TOptional<FText> Label;
	TOptional<bool> bChecked;
};

/**
 * The menu an info card shows: put on an Info Card entry whose Card Class is a Context Menu Widget. A Context Menu Anchor
 * makes one for you.
 */
UCLASS(DisplayName = "Info Card Data (Context Menu)")
class NAMETBD_API UInfoCardPayload_ContextMenu : public UInfoCardPayload
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu")
	TObjectPtr<UContextMenuAsset> Menu = nullptr;

	/** Instead of the menu's own Style. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu", meta = (DisplayName = "Style Override"))
	TObjectPtr<UContextMenuStyleAsset> StyleOverride = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu", meta = (TitleProperty = "EntryId"))
	TArray<FContextMenuEntryOverride> EntryOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu")
	TArray<FContextMenuExtraEntry> ExtraEntries;

	/** Runtime changes from the anchor, by Entry ID. Not saved. */
	TMap<FName, FContextMenuEntryRuntimeState> RuntimeStates;

	/** The anchor whose menu this is (for its events and remembered check states). Set by the anchor; null for a plain Info Card entry. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInfoCardAnchor> MenuAnchor;

	/** Focus the first row when the menu opens (opened from a key or a gamepad). */
	UPROPERTY(Transient)
	bool bFocusFirstEntry = false;

	/** The rows this menu shows: the asset's, with overrides, runtime changes and extra entries applied, removed ones gone. */
	void BuildEntries(TArray<FContextMenuEntry>& OutEntries) const;

	/** Backs the Relative To dropdown on Extra Entries. Must stay a UFUNCTION for GetOptions. */
	UFUNCTION()
	TArray<FString> GetMenuEntryIdOptions() const;
};
