// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "NameTBD/Buttons/Actions/ButtonAction.h"
#include "NameTBD/Cards/InfoCardWidget.h"
#include "ContextMenuTypes.h"

#include "ContextMenuWidget.generated.h"

class UBorder;
class UContextMenuButton;
class UContextMenuWidget;
class UHorizontalBox;
class UImage;
class UInfoCardAnchor;
class UPanelWidget;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UTooltipCardAnchor;
struct FMenuImage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FContextMenuEntryChosenSignature, FName, EntryId, bool, bIsChecked);

/**
 * One row of a context menu: a button row (a Context Menu Button holding the row's parts), a submenu row (the same, inside an
 * Info Card Anchor that opens the submenu), a divider, a text divider or a spacer.
 *
 * The C++ class builds its own parts. Make a Widget Blueprint from it (and pick it as the style's Entry Widget Class) to lay a
 * row out yourself; it then builds nothing and fills the parts it finds by name, all optional:
 *   RowButton (Context Menu Button: clicks, focus, states), RowHighlight (Border: the style's highlight / pressed brush),
 *   LabelText, CenterText, RightText, ShortcutText, HeaderText (Text Blocks), LeftIcon, CenterIcon, RightIcon (the first
 *   icon of each list; LeftIcon_1, LeftIcon_2... take the next ones), CheckMark, SubmenuArrow (Image, SVG Image or Context
 *   Menu Chevron - which takes the style's Submenu Arrow - shown, hidden and tinted), BindingsBox (a panel the shortcuts' keys are built into when they are drawn as art or framed; without
 *   one, ShortcutText shows them as text), DividerLine (any widget: shown for dividers), SubmenuAnchor (Info Card Anchor
 *   around RowButton, for submenus and attached menus), TooltipAnchor (Tooltip Card Anchor around RowButton, for the style's
 *   Tooltip Card row tooltips).
 * The native parts carry the same names, so a Row Button Preset's Shape and Design Targets can aim at LabelText, LeftIcon...
 * The style's alignment options (content, label / center / right / shortcut text, Text Dividers) lay out the native parts; a
 * Widget Blueprint row keeps its own layout.
 * On Menu Entry Set and On Highlight Changed are there for anything else a Widget Blueprint wants to do.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Context Menu Entry Widget"))
class NAMETBD_API UContextMenuEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/**
	 * Builds (C++ class) or fills (Widget Blueprint) the row for InEntry. Actions are the entry's actions already copied for
	 * this menu; TagActions those of its Button Tag. LeftColumnIcons: how many icons wide the left icon column is (the most
	 * any row of the menu has; 0: none), kept even when this row has fewer, so labels line up.
	 */
	void SetupEntry(const FContextMenuEntry& InEntry, const FContextMenuStyle& InStyle, UContextMenuWidget* InMenu, int32 InIndex,
		int32 LeftColumnIcons, const TArray<UButtonAction*>& InActions, const TArray<UButtonAction*>& InTagActions);

	/** The width of a left icon column InIconCount icons wide in InStyle (icon size, gaps, delimiters). */
	static float GetIconColumnWidth(const FContextMenuStyle& InStyle, int32 InIconCount);

	/** Whether a key press chooses this row: its Shortcut, More Shortcuts, Gamepad Shortcut or Shortcut Input Action's keys. */
	bool MatchesShortcut(const FInputChord& Pressed) const;

	/** The bindings this row shows (or answers to), each a list of keys: Ctrl, C. bForDisplay: the style's device rules apply. */
	void GatherBindings(TArray<TArray<FKey>>& OutBindings, bool bForDisplay) const;

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	const FContextMenuEntry& GetEntry() const { return Entry; }

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	UContextMenuWidget* GetMenu() const { return OwningMenu.Get(); }

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	int32 GetEntryIndex() const { return EntryIndex; }

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	UContextMenuButton* GetRowButton() const { return RowButton; }

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	bool IsEntryChecked() const { return bEntryChecked; }

	/** A row the player can choose now: a button or submenu, enabled. */
	UFUNCTION(BlueprintPure, Category = "Context Menu")
	bool IsSelectable() const { return Entry.IsSelectable() && RowButton != nullptr; }

	/** Chooses the row as a click would: runs its actions, flips its check, opens its submenu. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void ChooseEntry();

	/** Shows the check mark (or not) without running anything. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void SetEntryChecked(bool bNewChecked);

	/** Gives the row keyboard / gamepad focus (its highlight follows). */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void FocusEntry();

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	bool IsEntryFocused() const;

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	bool IsHighlighted() const { return bHighlighted; }

	/** Submenu rows and rows with an Attached Menu: opens it now (bFocusFirstEntry: with its first row focused, for keyboard and gamepad). */
	bool OpenSubmenu(bool bFocusFirstEntry);

	/** Submenu rows and rows with an Attached Menu: the open one, or null. */
	UContextMenuWidget* GetOpenSubmenu() const;

	/** A row that opens a menu beside it: a Submenu, or a Button with an Attached Menu. */
	bool OpensMenuBeside() const { return Entry.EntryType == EContextMenuEntryType::Submenu || Entry.HasAttachedMenu(); }

	/** The entry's own actions (copied for this menu) and those of its Button Tag. */
	const TArray<TObjectPtr<UButtonAction>>& GetRowActions() const { return RowActions; }
	const TArray<TObjectPtr<UButtonAction>>& GetRowTagActions() const { return RowTagActions; }

	/** The Card ID of a submenu row's card on its Submenu Anchor. */
	static const FName SubmenuCardId;

	/**
	 * Set by the menu every frame: whether this is the one row of its menu that may look hovered / focused (the one the mouse
	 * last moved onto, else the focused one). Rows whose hover areas overlap never both light up.
	 */
	void SetRowActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	bool IsRowActive() const { return bRowActive; }

protected:

	/** After the row is built and filled. For a Widget Blueprint row to do whatever else it wants with the entry. */
	UFUNCTION(BlueprintNativeEvent, Category = "Context Menu")
	void OnMenuEntrySet(const FContextMenuEntry& InEntry, bool bIsChecked);
	virtual void OnMenuEntrySet_Implementation(const FContextMenuEntry& InEntry, bool bIsChecked) {}

	/** Hovered or focused (true), or neither any more (false). */
	UFUNCTION(BlueprintNativeEvent, Category = "Context Menu")
	void OnHighlightChanged(bool bIsHighlighted);
	virtual void OnHighlightChanged_Implementation(bool bIsHighlighted) {}

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleRowClicked(bool bShouldCloseCard, bool bEnabling);

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UContextMenuButton> RowButton;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> RowHighlight;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CenterText;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RightText;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ShortcutText;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HeaderText;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> BindingsBox;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> LeftIcon;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CenterIcon;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> RightIcon;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CheckMark;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SubmenuArrow;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> DividerLine;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UInfoCardAnchor> SubmenuAnchor;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UTooltipCardAnchor> TooltipAnchor;

	/** The entry as shown (after overrides, Button Tag text and icons, action checks). */
	UPROPERTY(BlueprintReadOnly, Category = "Context Menu")
	FContextMenuEntry Entry;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButtonAction>> RowActions;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButtonAction>> RowTagActions;

	/** Every icon of each list, in order (the first is LeftIcon / CenterIcon / RightIcon), and the delimiters between them. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> LeftIconParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> CenterIconParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> RightIconParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> IconDelimiterParts;

	/** The shortcuts' parts built into BindingsBox: key names, delimiters and separators; key art and its own tint. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> BindingTextParts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> BindingImageParts;

	TArray<FLinearColor> BindingImageTints;

	TWeakObjectPtr<UContextMenuWidget> OwningMenu;
	FContextMenuStyle EntryStyle;
	int32 EntryIndex = INDEX_NONE;
	bool bEntryChecked = false;
	bool bHighlighted = false;
	bool bPressedLook = false;
	bool bUsesRowPreset = false;

	/** See SetRowActive. */
	bool bRowActive = false;

	/** The C++ class built this row's parts (the style's alignment options apply); false for a Widget Blueprint's own. */
	bool bBuiltNativeTree = false;

private:

	void BuildNativeTree(int32 LeftColumnIcons);
	UWidget* BuildButtonRow(int32 LeftColumnIcons);
	UWidget* BuildDividerRow();

	/** A Size Box of the style's icon size holding an Image or SVG Image for Icon (or a text glyph when Icon is unset and Glyph isn't empty). */
	UWidget* MakeIconPart(const FContextMenuIcon& Icon, FName PartName, const TCHAR* Glyph, TObjectPtr<UWidget>& OutPart);

	/** The submenu arrow's part (SubmenuArrow) in a square box of the style's Submenu Arrow Size: its image, else a drawn chevron. */
	UWidget* MakeSubmenuArrowPart();

	/** A Horizontal Box of an icon part per set icon (named Base, Base_1...), Icon Gap and Icon Delimiter between them. */
	UWidget* MakeIconList(const TArray<FContextMenuIcon>& Icons, const TCHAR* Base, TArray<TObjectPtr<UWidget>>& OutParts);

	/** A line for dividers and text dividers. */
	UWidget* MakeLine(FName PartName);

	/** A Text Block in the binding font, kept for the state colours. */
	UTextBlock* MakeBindingText(const FText& InText);

	/** An Image or SVG Image of a key's art, Glyph Height tall. */
	UWidget* MakeGlyphImage(const FMenuImage& Art);

	void FillParts();
	void FillIconParts();
	void FillBindings();
	void SetupSubmenuAnchor();
	void SetupTooltip();
	void ApplyStateLook();
	/** PartSize: the square the image is drawn at; 0: the style's Icon Size. */
	void ApplyIcon(UWidget* Part, const FContextMenuIcon& Icon, const FLinearColor& StateTint, float PartSize = 0.f) const;
	void TintIcon(UWidget* Part, const FContextMenuIcon& Icon, const FLinearColor& StateTint) const;
};

/**
 * A right-click menu: an Info Card whose payload is an Info Card Data (Context Menu). It builds a row per entry, runs their
 * actions, and handles everything a desktop menu does - keyboard and gamepad navigation (Up / Down, wrapping), Right / Left
 * into and out of submenus, Escape, Enter, typing a letter to jump to a row, an entry's shortcut while open - on top of what
 * every info card does (placement at the cursor with flipping and clamping, click-away, sounds, animations).
 *
 * The C++ class builds its own frame (a rounded panel, a width-limited size box, a scroll box past Max Height). A Widget
 * Blueprint made from it (the style's Menu Widget Class) lays the frame out itself; the rows go into the panel named
 * EntriesBox (a Vertical Box is usual), and MenuPanel (Border), MenuSizeBox (Size Box) and MenuScroll (Scroll Box) are filled
 * when present.
 */
UCLASS(meta = (DisplayName = "Context Menu Widget"))
class NAMETBD_API UContextMenuWidget : public UInfoCardWidget
{
	GENERATED_BODY()

public:

	UContextMenuWidget(const FObjectInitializer& ObjectInitializer);

	virtual bool Initialize() override;
	virtual void ApplyPayloads_Implementation(const TArray<UNameTBD_Payload*>& InPayloads) override;

	/** Rebuilds every row from the payload (after Set Entry... on the anchor, say). */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void RebuildMenu();

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	TArray<UContextMenuEntryWidget*> GetEntryWidgets() const;

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	UContextMenuEntryWidget* FindEntryWidget(FName EntryId) const;

	/** Focuses the first row that can be chosen. False when there is none. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	bool FocusFirstEntry();

	/** Closes this menu and every menu it was opened from: the whole chain, submenus included. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void CloseWholeMenu();

	/** The first menu of the chain this one is in (itself, unless it is a submenu). */
	UFUNCTION(BlueprintPure, Category = "Context Menu")
	UContextMenuWidget* GetRootMenu() const;

	/** The anchor that opened the chain (for a submenu, the first menu's), or null. */
	UFUNCTION(BlueprintPure, Category = "Context Menu")
	UInfoCardAnchor* GetMenuAnchor() const;

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	UContextMenuAsset* GetMenuAsset() const;

	const FContextMenuStyle& GetMenuStyle() const { return MenuStyle; }

	/** The style asset in use, or null for the built-in style. */
	UContextMenuStyleAsset* GetMenuStyleAsset() const { return MenuStyleAsset; }

	/** Every action payload (Context Button Payload) this card carries, in order. */
	TArray<UContextButtonPayload*> GetActionPayloads() const;

	/** The context an action on EntryId runs with. */
	FContextMenuActionContext MakeActionContext(FName EntryId, UContextMenuButton* Button) const;

	/** A row was chosen (clicked, Enter, a shortcut). What every row calls; runs the whole choice. */
	void HandleEntryChosen(UContextMenuEntryWidget* Row, bool bFromKeyboard);

	/** This menu, or the first menu of its chain, is playing its exit: its rows stay idle until it is gone. */
	bool IsMenuChainClosing() const;

	/** The row of this menu that may look highlighted now (see UContextMenuEntryWidget::SetRowActive), or null. */
	UFUNCTION(BlueprintPure, Category = "Context Menu")
	UContextMenuEntryWidget* GetActiveRow() const { return ActiveRow.Get(); }

	/** A row was chosen: its Entry ID, and its checked state after the choice (false for rows that aren't check boxes / radios). */
	UPROPERTY(BlueprintAssignable, Category = "Context Menu")
	FContextMenuEntryChosenSignature OnEntryChosen;

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyChar(const FGeometry& InGeometry, const FCharacterEvent& InCharEvent) override;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> MenuPanel;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> MenuSizeBox;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> MenuScroll;

	UPROPERTY(BlueprintReadOnly, Category = "Context Menu", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> EntriesBox;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UContextMenuEntryWidget>> EntryWidgets;

	UPROPERTY(Transient)
	TObjectPtr<UInfoCardPayload_ContextMenu> MenuPayload;

	UPROPERTY(Transient)
	TObjectPtr<UContextMenuStyleAsset> MenuStyleAsset;

	FContextMenuStyle MenuStyle;

	/** The style's Animations and Menu Sounds are taken once, on the first payload; a re-delivered payload leaves them. */
	bool bAppliedOpenLook = false;

	/** Focus is given on the first tick: during construction the menu isn't in a window yet. */
	bool bPendingInitialFocus = false;

	/** The menu took keyboard focus when it opened, so it hands it back to its anchor when it goes. */
	bool bRestoreFocusOnClose = false;

private:

	void BuildNativeFrame();
	void ApplyFrameStyle();
	void BuildRows();

	/** The focused row's index in EntryWidgets, or INDEX_NONE. */
	int32 GetFocusedRowIndex() const;

	/** Moves focus Step rows from the focused one (or from the top / bottom when none is), skipping rows that can't be chosen. */
	bool MoveFocus(int32 Step);

	/** For a submenu: focuses the row that opened it, then closes it. */
	void CloseSubmenuLevel();

	/**
	 * Picks the one row that may look highlighted: the row the mouse moved onto (taking focus, so the keyboard carries on from
	 * there), else the focused row (the keyboard moved it), else a row the resting cursor is over. Where hover areas overlap,
	 * the row whose own box holds the cursor wins.
	 */
	void UpdateActiveRow();

	TWeakObjectPtr<UContextMenuEntryWidget> ActiveRow;
	FVector2D LastCursorPos = FVector2D::ZeroVector;

	bool IsSubmenu() const;
};
