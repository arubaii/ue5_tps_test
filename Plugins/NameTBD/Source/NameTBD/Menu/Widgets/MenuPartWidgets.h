// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NameTBD/Menu/Input/MenuInputTypes.h"
#include "NameTBD/Menu/Settings/MenuSettingTypes.h"
#include "MenuStyle.h"
#include "SMenuControls.h"

#include "MenuPartWidgets.generated.h"

class UEditableText;
class UEditableTextBox;
class UMenuSetting;
class UTextBlock;

/*
 * Your own UMG widgets for the parts of a Settings Menu (Menu Style > Custom Widgets): tabs, headings, footer prompts, the
 * description panel, the search box, dialogs and dropdown lists. Make a Widget Blueprint from the part's class, lay it out, drive
 * it from its events; the screen makes one per part, fills it, and keeps doing the work (input, focus, sounds, narration). Parts
 * named as below are filled for you when present.
 */

/** Which footer prompt a Menu Footer Prompt Widget is. */
UENUM(BlueprintType)
enum class EMenuFooterAction : uint8
{
	Back,
	Reset UMETA(ToolTip = "Reset the page to its defaults (asks first)."),
	Apply UMETA(ToolTip = "Apply the changes that wait (Apply Button policy)."),
	Revert UMETA(ToolTip = "Put back every change made since the screen opened."),
	PreviousTab UMETA(DisplayName = "Previous Tab"),
	NextTab UMETA(DisplayName = "Next Tab")
};

/** What the description panel says about the setting it describes. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuSettingDescription
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Description")
	FText Title;

	/** "Current: High    Default: Medium" (empty for actions and info). */
	UPROPERTY(BlueprintReadOnly, Category = "Description")
	FText ValueLine;

	UPROPERTY(BlueprintReadOnly, Category = "Description")
	FText CurrentValue;

	UPROPERTY(BlueprintReadOnly, Category = "Description")
	FText DefaultValue;

	/** The setting's description. */
	UPROPERTY(BlueprintReadOnly, Category = "Description")
	FText Body;

	/** The picked option's own description. */
	UPROPERTY(BlueprintReadOnly, Category = "Description")
	FText OptionText;

	/** Why it can't be changed now. */
	UPROPERTY(BlueprintReadOnly, Category = "Description")
	FText Warning;

	/** Restart / confirmation notes. */
	UPROPERTY(BlueprintReadOnly, Category = "Description")
	FText Note;

	/** The setting's or the option's preview image. */
	UPROPERTY(BlueprintReadOnly, Category = "Description")
	FMenuImage Preview;

	UPROPERTY(BlueprintReadOnly, Category = "Description")
	bool bEnabled = true;

	UPROPERTY(BlueprintReadOnly, Category = "Description")
	bool bRequiresRestart = false;
};

/** The base of a Settings Menu's custom part widgets: the style it is shown with, and a way back to the screen. */
UCLASS(Abstract, Blueprintable)
class NAMETBD_API UMenuPartWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** The style of the screen showing it (its colours, fonts, sizes). */
	UFUNCTION(BlueprintPure, Category = "Menu|Part")
	FMenuStyle GetMenuStyle() const;

	/** The palette in use (High Contrast Colors while High Contrast Menus is on). */
	UFUNCTION(BlueprintPure, Category = "Menu|Part")
	FMenuStylePalette GetMenuPalette() const;

	/** The style changed (edited, High Contrast turned on): restyle. Also called once when it is made. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Part")
	void OnMenuStyleChanged();

	/** C++: the style holder of the screen showing it. */
	void SetStyleHolder(const FMenuStyleRef& InHolder);

	/** C++: tells it the style changed. */
	void NotifyStyleChanged() { OnMenuStyleChanged(); }

protected:
	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
};

/** A page tab. Clicking anywhere on it that your widget doesn't handle selects it; a Button inside calls Select Tab. */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Menu Tab Widget"))
class NAMETBD_API UMenuTabWidget : public UMenuPartWidget
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadOnly, Category = "Tab")
	FName PageId;

	UPROPERTY(BlueprintReadOnly, Category = "Tab")
	FText PageName;

	UPROPERTY(BlueprintReadOnly, Category = "Tab")
	FMenuImage PageIcon;

	UPROPERTY(BlueprintReadOnly, Category = "Tab")
	int32 TabIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Tab")
	bool bSelected = false;

	/** Filled with the page's name (in capitals with the style's Uppercase Tabs). */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Tab")
	void OnTabAssigned(FName InPageId, const FText& InPageName, const FMenuImage& InPageIcon, int32 InTabIndex);

	/** Selected (the current page), hovered, pressed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Tab")
	void OnTabStateChanged(bool bIsSelected, bool bIsHovered, bool bIsPressed);

	/** Shows this tab's page. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Tab")
	void SelectTab();

	/** C++: filled by the screen. */
	void AssignTab(FName InPageId, const FText& InPageName, const FMenuImage& InPageIcon, int32 InTabIndex, TFunction<void()> InSelect);
	void SetTabState(bool bInSelected, bool bInHovered, bool bInPressed);

private:
	TFunction<void()> SelectFunction;
	bool bLastHovered = false;
	bool bLastPressed = false;
	bool bStateSent = false;
};

/** A section heading ("DISPLAY") or a key binding category heading ("Movement"). */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Menu Heading Widget"))
class NAMETBD_API UMenuHeadingWidget : public UMenuPartWidget
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadOnly, Category = "Heading")
	FText HeadingText;

	/** A key binding category under a section, rather than a section. */
	UPROPERTY(BlueprintReadOnly, Category = "Heading")
	bool bCategory = false;

	/** Filled with the heading (sections in capitals with the style's Uppercase Sections). */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Heading")
	void OnHeadingAssigned(const FText& InHeadingText, bool bIsCategory);

	void AssignHeading(const FText& InHeadingText, bool bInCategory);
};

/** A footer prompt ([B] Back, [Y] Reset to Defaults, Apply, Revert) or a tab-change prompt beside the tabs. Clicking it does it. */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Menu Footer Prompt Widget"))
class NAMETBD_API UMenuFooterPromptWidget : public UMenuPartWidget
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadOnly, Category = "Prompt")
	EMenuFooterAction Action = EMenuFooterAction::Back;

	/** "Back", "Reset to Defaults" (empty for the tab prompts). */
	UPROPERTY(BlueprintReadOnly, Category = "Prompt")
	FText PromptLabel;

	/** The key for the device in use (art, or a label for a key cap). */
	UPROPERTY(BlueprintReadOnly, Category = "Prompt")
	FMenuKeyGlyph Glyph;

	/** Filled with the label. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	/** Filled with the key's label ("Esc", "B"), for a key cap of your own. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Prompt")
	void OnPromptAssigned(EMenuFooterAction InAction, const FText& InLabel);

	/** The device or the binding changed: show this key. Invalid Key: none for this device (touch: still tappable). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Prompt")
	void OnGlyphChanged(const FMenuKeyGlyph& NewGlyph);

	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Prompt")
	void OnPromptStateChanged(bool bPromptEnabled, bool bIsHovered, bool bIsPressed);

	/** Does what the prompt says, as clicking it. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Prompt")
	void Press();

	void AssignPrompt(EMenuFooterAction InAction, const FText& InLabel, TFunction<void()> InPress);
	void SetGlyph(const FMenuKeyGlyph& InGlyph);
	void SetPromptState(bool bInEnabled, bool bInHovered, bool bInPressed);

private:
	TFunction<void()> PressFunction;
	bool bLastEnabled = true;
	bool bLastHovered = false;
	bool bLastPressed = false;
	bool bStateSent = false;
};

/** The description panel: what the focused (or hovered) setting does, its value, default, preview and notes. */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Menu Description Widget"))
class NAMETBD_API UMenuDescriptionWidget : public UMenuPartWidget
{
	GENERATED_BODY()

public:

	/** The setting described (none: nothing focused yet). */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Description")
	TObjectPtr<UMenuSetting> DescribedSetting;

	UPROPERTY(BlueprintReadOnly, Category = "Description")
	FMenuSettingDescription CurrentDescription;

	/** Each filled with its line and collapsed while it is empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ValueLineText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BodyText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> OptionText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WarningText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NoteText;

	/** Another setting is described, or its value changed. Setting: none when nothing is described. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Description")
	void OnDescriptionChanged(UMenuSetting* NewSetting, const FMenuSettingDescription& NewDescription);

	void Describe(UMenuSetting* InSetting, const FMenuSettingDescription& InDescription);
};

/** The search box. An Editable Text named SearchText (or an Editable Text Box named SearchTextBox) is wired up for you. */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Menu Search Widget"))
class NAMETBD_API UMenuSearchWidget : public UMenuPartWidget
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UEditableText> SearchText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> SearchTextBox;

	/** The hint the menu asks for ("Search settings"); given to the parts above. */
	UPROPERTY(BlueprintReadOnly, Category = "Search")
	FText SearchHint;

	/** Searches the menu for Text (what typing in your own box calls). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Search")
	void SetMenuSearchText(const FText& Text);

	/** The menu changed the search (cleared it on Back or a tab change): show this. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Search")
	void OnMenuSearchTextChanged(const FText& Text);

	/** C++: wired by the screen. */
	void AssignSearch(const FText& InHint, TFunction<void(const FText&)> InSetText);
	void ShowSearchText(const FText& Text);

protected:
	virtual void NativeOnInitialized() override;

private:
	UFUNCTION()
	void HandleTextChanged(const FText& Text);

	TFunction<void(const FText&)> SetTextFunction;
	bool bShowing = false;
};

/** A dialog: keep display changes (with a countdown), unsaved changes, confirmations. Back picks the cancel button. */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Menu Dialog Widget"))
class NAMETBD_API UMenuDialogWidget : public UMenuPartWidget
{
	GENERATED_BODY()

public:

	UMenuDialogWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintReadOnly, Category = "Dialog")
	FText DialogTitle;

	UPROPERTY(BlueprintReadOnly, Category = "Dialog")
	FText DialogBody;

	UPROPERTY(BlueprintReadOnly, Category = "Dialog")
	TArray<FText> ButtonLabels;

	/** Primary (filled) / Secondary / Ghost - in the built-in look; yours to read. 0 Primary, 1 Secondary, 2 Ghost. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialog")
	TArray<int32> ButtonKinds;

	UPROPERTY(BlueprintReadOnly, Category = "Dialog")
	int32 DefaultButton = 0;

	/** -1: Back does nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "Dialog")
	int32 CancelButton = INDEX_NONE;

	/** Filled with the title and the body. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BodyText;

	/** Build your buttons here; focus the default one (pads need focus). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Dialog")
	void OnDialogShown(const FText& InTitle, const FText& InBody, const TArray<FText>& InButtonLabels, int32 InDefaultButton, int32 InCancelButton);

	/** The body changed (the keep-display countdown). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Dialog")
	void OnDialogBodyChanged(const FText& InBody);

	/** The widget to focus when it opens (the default button). None: the dialog itself. */
	UFUNCTION(BlueprintNativeEvent, Category = "Menu|Dialog")
	UWidget* GetDialogFocusWidget();

	/** Answers with a button. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Dialog")
	void Choose(int32 ButtonIndex);

	/** As Back: the cancel button (if any). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Dialog")
	void CancelDialog();

	void AssignDialog(const FText& InTitle, const FText& InBody, const TArray<FText>& InLabels, const TArray<int32>& InKinds, int32 InDefault, int32 InCancel,
		TFunction<void(int32)> InChoose);
	void SetBody(const FText& InBody);

private:
	TFunction<void(int32)> ChooseFunction;
};

/** A dropdown's list of options. Back cancels. */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Menu Option List Widget"))
class NAMETBD_API UMenuOptionListWidget : public UMenuPartWidget
{
	GENERATED_BODY()

public:

	UMenuOptionListWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintReadOnly, Category = "Options")
	FText ListTitle;

	UPROPERTY(BlueprintReadOnly, Category = "Options")
	TArray<FText> OptionLabels;

	UPROPERTY(BlueprintReadOnly, Category = "Options")
	int32 CurrentOption = 0;

	/** Filled with the title (the setting's name). */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	/** Build an entry per option here; focus the current one. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Options")
	void OnOptionListShown(const FText& InTitle, const TArray<FText>& InOptionLabels, int32 InCurrentOption);

	/** The widget to focus when it opens (the current option). None: the list itself. */
	UFUNCTION(BlueprintNativeEvent, Category = "Menu|Options")
	UWidget* GetListFocusWidget();

	UFUNCTION(BlueprintCallable, Category = "Menu|Options")
	void PickOption(int32 OptionIndex);

	UFUNCTION(BlueprintCallable, Category = "Menu|Options")
	void CancelList();

	void AssignList(const FText& InTitle, const TArray<FText>& InLabels, int32 InCurrent, TFunction<void(int32)> InPick);

private:
	TFunction<void(int32)> PickFunction;
};
