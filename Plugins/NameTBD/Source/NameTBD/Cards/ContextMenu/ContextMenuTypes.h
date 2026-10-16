// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Commands/InputChord.h"
#include "GameplayTagContainer.h"
#include "Layout/Margin.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Types/SlateEnums.h"

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

	/** Shown as the row's tooltip on hover - what it does, or why it is disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (MultiLine = true, EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	FText Description;

	/** Before everything else, in the icon column (so labels line up whether or not a row has one). A checked row shows its check mark here instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Left Icon", EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	FContextMenuIcon LeftIcon;

	/** Right before the label. With no label, an icon-only row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Center Icon", EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	FContextMenuIcon CenterIcon;

	/** On the right, after Right Text and the shortcut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Right Icon", EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	FContextMenuIcon RightIcon;

	/** Free text on the right, in the shortcut's font: a count, a value, a hint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Right Text", EditCondition = "EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu", EditConditionHides))
	FText RightText;

	/**
	 * A keyboard shortcut, shown on the right ("Ctrl+C"). While the menu is open, pressing it chooses this row (the menu
	 * asset's Shortcuts Work While Open). Showing it doesn't bind it anywhere else in the game.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (EditCondition = "EntryType == EContextMenuEntryType::Button", EditConditionHides))
	FInputChord Shortcut;

	/** Text Divider: its text. Empty: a plain divider line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Text", EditCondition = "EntryType == EContextMenuEntryType::Header", EditConditionHides))
	FText HeaderText;

	/** Text Divider: where the text sits; the line fills the rest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (DisplayName = "Text Alignment", EditCondition = "EntryType == EContextMenuEntryType::Header", EditConditionHides))
	TEnumAsByte<EHorizontalAlignment> HeaderAlignment = HAlign_Left;

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

	/** A row the player can choose (a button or a submenu that is enabled). */
	bool IsSelectable() const
	{
		return bEnabled && (EntryType == EContextMenuEntryType::Button || EntryType == EContextMenuEntryType::Submenu);
	}

	/** The text the Entry ID is filled from when empty. */
	FString GetIdSourceText() const;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	FMargin RowPadding = FMargin(8.f, 2.f);

	/** Drawn behind a row while hovered or focused (no Row Button Preset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	FSlateBrush RowHighlight;

	/** Drawn behind a row while pressed (no Row Button Preset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows")
	FSlateBrush RowPressed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows", meta = (ClampMin = "4", UIMax = "48", ForceUnits = "px"))
	float IconSize = 16.f;

	/** Space between a row's parts (icon, label, right text, shortcut). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rows", meta = (ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float PartGap = 8.f;

	/** Keep the icon column even when no row has a left icon or check mark, so labels line up across menus. */
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

	// Icons ------------------------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FLinearColor IconTint = FLinearColor(0.85f, 0.86f, 0.9f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FLinearColor HighlightedIconTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FLinearColor DisabledIconTint = FLinearColor(0.85f, 0.86f, 0.9f, 0.35f);

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

/** One anchor's change to one entry of its menu, kept by Entry ID. Synced from the menu asset; untouched means no change. */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuEntryOverride
{
	GENERATED_BODY()

	/** Which entry. Kept in sync automatically; shown as the element's title. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Entry ID", EditCondition = "false"))
	FName EntryId = NAME_None;

	/** Leaves this entry out of this anchor's menu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Remove"))
	bool bRemove = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (InlineEditConditionToggle))
	bool bOverrideEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Enabled", EditCondition = "bOverrideEnabled && !bRemove"))
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (InlineEditConditionToggle))
	bool bOverrideLabel = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Label", EditCondition = "bOverrideLabel && !bRemove"))
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (InlineEditConditionToggle))
	bool bOverrideChecked = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entry", meta = (DisplayName = "Checked", EditCondition = "bOverrideChecked && !bRemove"))
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
