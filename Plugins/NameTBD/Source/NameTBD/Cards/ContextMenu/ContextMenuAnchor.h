// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "NameTBD/Cards/InfoCardAnchor.h"
#include "ContextMenuTypes.h"

#include "ContextMenuAnchor.generated.h"

class UContextButtonPayload;
class UContextMenuWidget;

/** What opens a Context Menu Anchor's menu. */
UENUM(BlueprintType)
enum class EContextMenuOpenTrigger : uint8
{
	RightClick UMETA(DisplayName = "Right Click"),
	LeftClick UMETA(DisplayName = "Left Click", ToolTip = "A dropdown or an \"options\" button. Pair it with Override Positioning: To Icon, Down."),
	AnyClick UMETA(DisplayName = "Right Or Left Click"),
	Manual UMETA(DisplayName = "Only From Blueprint", ToolTip = "Nothing opens it but Open Context Menu - from a gamepad button, a key, a long press.")
};

/** Where a Context Menu Anchor's menu opened before positioning moved to the style (Positioning, Override Positioning). Kept to load old anchors. */
UENUM(BlueprintType)
enum class EContextMenuPlacement : uint8
{
	AtCursor UMETA(DisplayName = "At Cursor", ToolTip = "Where the click landed, down and to the right, flipping at the screen's edges. Opened from a key or a gamepad (Open Context Menu with Focus First Entry), it opens below the widget instead."),
	BelowWidget UMETA(DisplayName = "Below Widget", ToolTip = "Under the widget, as a dropdown."),
	RightOfWidget UMETA(DisplayName = "Right Of Widget", ToolTip = "Beside the widget, as a flyout.")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FContextMenuAnchorEntrySignature, FName, EntryId, bool, bIsChecked);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FContextMenuAnchorOpeningSignature);

/**
 * Wrap a widget in this and pick a Context Menu asset: right-clicking the widget opens the menu at the cursor. That's all a
 * right-click menu needs.
 *
 * The menu asset is shared; this anchor tailors it to this one widget without touching the asset:
 *   - Entry Overrides: one per entry of the menu (kept in sync with it) - remove it here, grey it out, relabel it, start it
 *     checked.
 *   - Extra Entries: rows only this widget's menu has, at the start, the end, or before / after a named entry.
 *   - Style Override: another Context Menu Style for this widget's menu.
 *   - Override Positioning: where this widget's menu opens, instead of its style's Positioning.
 *   - Action Payloads: the data this widget's rows act on (the item in this slot), handed to every row's actions.
 *
 * When a row is chosen, its actions run and On Menu Entry Clicked fires with its Entry ID - so a menu can be wired up in the
 * Widget Blueprint's graph with one event and a Switch on Name, no action classes at all. On Context Menu Opening fires just
 * before the menu is built, for Set Entry Enabled / Hidden / Label / Checked from the state of the game.
 *
 * Built on the info card system: the menu is an info card (Context Menu Widget), so pinning, suppression tags, Hide All Cards
 * and the anchor's events all apply; its Card ID is ContextMenu. The usual anchor fields are hidden.
 */
UCLASS(meta = (DisplayName = "Context Menu Anchor"))
class NAMETBD_API UContextMenuAnchor : public UInfoCardAnchor
{
	GENERATED_BODY()

public:

	/** The menu this widget opens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu")
	TObjectPtr<UContextMenuAsset> Menu = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu", meta = (DisplayName = "Open With"))
	EContextMenuOpenTrigger OpenWith = EContextMenuOpenTrigger::RightClick;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu", meta = (InlineEditConditionToggle))
	bool bOverridePositioning = false;

	/**
	 * Where this widget's menu opens, instead of its style's Positioning: beside the widget for a dropdown (To Icon, Down) or
	 * a flyout (To Icon, Right), at the cursor, at a corner of the screen. Off: shows the style's.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu", meta = (DisplayName = "Override Positioning", EditCondition = "bOverridePositioning"))
	FInfoCardPositioning Positioning;

	/** This widget's menu in another Context Menu Style than the menu asset's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu", meta = (DisplayName = "Style Override"))
	TObjectPtr<UContextMenuStyleAsset> StyleOverride = nullptr;

	/**
	 * One per entry of the menu, by Entry ID, kept in sync with the asset (entries can't be added or removed here - see Extra
	 * Entries). Untouched means as the asset has it.
	 */
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadOnly, Category = "Context Menu", meta = (DisplayName = "Entry Overrides", TitleProperty = "EntryId"))
	TArray<FContextMenuEntryOverride> EntryOverrides;

	/** Rows only this widget's menu has. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu", meta = (DisplayName = "Extra Entries"))
	TArray<FContextMenuExtraEntry> ExtraEntries;

	/** What the rows' actions act on: handed to every action in the menu (Execute Menu Action > Payloads). Submenus get them too. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Context Menu", meta = (DisplayName = "Action Payloads"))
	TArray<TObjectPtr<UContextButtonPayload>> ActionPayloads;

	/** A row was chosen, in this menu or one of its submenus. bIsChecked: its state after the click (check boxes and radios). */
	UPROPERTY(BlueprintAssignable, Category = "Context Menu")
	FContextMenuAnchorEntrySignature OnMenuEntryClicked;

	/** The menu is about to open (before its rows are built): change entries for this opening here. */
	UPROPERTY(BlueprintAssignable, Category = "Context Menu")
	FContextMenuAnchorOpeningSignature OnContextMenuOpening;

	/**
	 * Opens the menu now, wherever the trigger would. bFocusFirstEntry: for a key or a gamepad - the first row is focused, and
	 * a menu positioned To Cursor opens beside the widget instead (Positioning > From Keyboard).
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void OpenContextMenu(bool bFocusFirstEntry = false);

	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void CloseContextMenu();

	UFUNCTION(BlueprintPure, Category = "Context Menu")
	bool IsContextMenuOpen() const;

	/** The open menu (the first one, not a submenu), or null. */
	UFUNCTION(BlueprintPure, Category = "Context Menu")
	UContextMenuWidget* GetOpenContextMenu() const;

	/** Switches the menu this widget opens. An open menu shows the new one at once. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void SetContextMenu(UContextMenuAsset* NewMenu);

	/** Replaces the Action Payloads (an item moved into this slot). An open menu's actions get the new ones. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void SetActionPayloads(const TArray<UContextButtonPayload*>& NewActionPayloads);

	/** Greys a row out, or back in, from now on (this menu and its submenus, by Entry ID). Open menus follow at once. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void SetEntryEnabled(FName EntryId, bool bEntryEnabled);

	/** Leaves a row out, or brings it back. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void SetEntryHidden(FName EntryId, bool bEntryHidden);

	/** Relabels a row ("Equip" / "Unequip"). */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void SetEntryLabel(FName EntryId, FText NewLabel);

	/** Checks or unchecks a check box / radio row. Remembered, as the player's own clicks are. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void SetEntryChecked(FName EntryId, bool bEntryChecked);

	/** Whether a check box / radio row is checked: the player's last choice, else Set Entry Checked, else the menu's. */
	UFUNCTION(BlueprintPure, Category = "Context Menu")
	bool IsEntryChecked(FName EntryId) const;

	/** Forgets every Set Entry... change and remembered check: the rows are as authored again. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu")
	void ClearEntryChanges();

	/** Backs the Relative To dropdown on Extra Entries. Must stay a UFUNCTION for GetOptions. */
	UFUNCTION()
	TArray<FString> GetMenuEntryIdOptions() const;

	/** The changes Set Entry... made, by Entry ID. Read by the menus while they build their rows. */
	const TMap<FName, FContextMenuEntryRuntimeState>& GetEntryRuntimeStates() const { return EntryRuntimeStates; }

	/** Called by the menu: a check / radio row's state after a click. Kept for the next opening. */
	void RememberEntryChecked(FName EntryId, bool bNowChecked);

	/** Called by the menu once a row's actions have run. */
	void NotifyMenuEntryChosen(FName EntryId, bool bNowChecked);

	/** Where this anchor's menu opens: Override Positioning when on, else its style's. */
	const FInfoCardPositioning& GetEffectivePositioning() const;

	virtual void PostLoad() override;

	/** The card IDs of the menu (Right Click, and Any Click's right) and of Any Click's left-click twin. */
	static const FName ContextMenuCardId;
	static const FName LeftClickContextMenuCardId;

protected:

	virtual bool UsesGeneratedInfoCards() const override { return true; }
	virtual bool BuildGeneratedInfoCards(TArray<FInfoCardSpec>& OutSpecs) override;
	virtual bool ShouldReopenOnRepeatClick(int32 Index) const override { return true; }
	virtual void SynchronizeProperties() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Entry Overrides made to match the menu: one per entry with an ID, in the menu's order; set ones kept. */
	void SyncEntryOverridesFromMenu();
#endif

	UFUNCTION()
	void HandleClickOpening();

	/**
	 * Left Click / Any Click on a button: the button takes the click, so it never reaches the anchor. The anchor listens to
	 * the wrapped button instead - a press while the menu is open closes it, a click otherwise opens it.
	 */
	UFUNCTION()
	void HandleContentButtonPressed();

	UFUNCTION()
	void HandleContentButtonClicked();

	bool bMenuOpenAtButtonPress = false;

	/** Rebuilds the rows of an open menu (and its submenus) next tick, after a Set Entry... change. */
	void RequestOpenMenuRefresh();

	TMap<FName, FContextMenuEntryRuntimeState> EntryRuntimeStates;

	/** The next opening was asked for from a key or a gamepad (see Open Context Menu). */
	bool bOpeningFromKeyboard = false;

	bool bOpenMenuRefreshQueued = false;

#if WITH_EDITORONLY_DATA
	// Where the menu opened before positioning moved to the style. Loaded from old anchors through CoreRedirects
	// (Config/DefaultNameTBD.ini), turned into Override Positioning in PostLoad, never saved again (CPF_Deprecated).
	UPROPERTY()
	EContextMenuPlacement Placement_DEPRECATED = EContextMenuPlacement::AtCursor;
#endif
};
