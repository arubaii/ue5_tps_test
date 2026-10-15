// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "NameTBD/Menu/Settings/MenuSettingsSchema.h"
#include "NameTBD/Sound/UISoundTypes.h"
#include "SMenuControls.h"
#include "SMenuSettingRow.h"

class SMenuImage;
class SMenuKeyPrompt;
class SOverlay;
class SScrollBox;
class SHorizontalBox;
class SVerticalBox;
class ULocalPlayer;
class UMenuSettingsSubsystem;

/** The keys a settings screen answers (each list: keyboard and gamepad keys alike; the first of the device in use is shown). */
struct FMenuPanelKeys
{
	TArray<FKey> PreviousTab;
	TArray<FKey> NextTab;
	TArray<FKey> Apply;
	TArray<FKey> Reset;
	TArray<FKey> Back;
};

/** The words a settings screen uses (empty: its own). */
struct FMenuPanelTexts
{
	FText Apply;
	FText Revert;
	FText Reset;
	FText Back;
	FText Search;
};

DECLARE_DELEGATE_OneParam(FOnMenuPanelPage, FName /*PageId*/);
DECLARE_DELEGATE_OneParam(FOnMenuPanelSetting, FName /*SettingId*/);

/**
 * A whole settings screen, in Slate: title and search, page tabs (shoulder buttons / Q E switch them), the page's sections and
 * rows in a scrolling list, a description panel for the focused setting, a footer with key prompts and Apply / Revert / Reset /
 * Back, and dialogs (keep display changes with a countdown, unsaved changes, confirmations, dropdown lists). Everything reads
 * its look from the style holder at paint time.
 */
class NAMETBD_API SMenuSettingsPanel : public SCompoundWidget, public IMenuSettingsRowHost
{
public:
	SLATE_BEGIN_ARGS(SMenuSettingsPanel)
		: _ApplyButton(false)
		, _ConfirmUnsavedOnBack(true)
		, _ShowBackButton(true)
		, _ShowResetButton(true)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<UMenuSettingsSubsystem>, Settings)
		SLATE_ARGUMENT(TWeakObjectPtr<ULocalPlayer>, LocalPlayer)
		/** The pages when there is no subsystem (the designer). */
		SLATE_ARGUMENT(TArray<FMenuSettingsPage>, PreviewPages)
		/** Pages shown, in this order. Empty: all. */
		SLATE_ARGUMENT(TArray<FName>, PageFilter)
		SLATE_ARGUMENT(FName, InitialPage)
		SLATE_ATTRIBUTE(FText, Title)
		SLATE_ARGUMENT(bool, ApplyButton)
		SLATE_ARGUMENT(bool, ConfirmUnsavedOnBack)
		SLATE_ARGUMENT(bool, ShowBackButton)
		SLATE_ARGUMENT(bool, ShowResetButton)
		SLATE_ARGUMENT(FMenuPanelKeys, Keys)
		SLATE_ARGUMENT(FMenuPanelTexts, Texts)
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, SoundContext)
		/** Your own row for a setting (a Menu Setting Row Widget); null: the built-in row. */
		SLATE_ARGUMENT(TFunction<TSharedPtr<SWidget>(UMenuSetting*)>, MakeCustomRow)
		SLATE_EVENT(FSimpleDelegate, OnBack)
		SLATE_EVENT(FOnMenuPanelPage, OnPageChanged)
		SLATE_EVENT(FOnMenuPanelSetting, OnSettingFocused)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle);
	virtual ~SMenuSettingsPanel() override;

	// ---- What the UMG widget drives ----

	void SetPage(int32 PageIndex, bool bAnimate = true);
	bool SetPageById(FName PageId);
	void StepPage(int32 Direction);
	FName GetCurrentPageId() const;

	/** Back: closes what is open inside (a dialog, a key wait, the search), asks about unapplied changes, else fires On Back. True when it was handled inside. */
	bool RequestBack();

	void ApplyChanges();
	void RevertChanges();

	/** Asks, then puts the current page's settings back to their defaults. */
	void ResetCurrentPage();

	void FocusFirstRow();
	void FocusSetting(FName SettingId);
	void SetSearchText(const FText& Text);

	/** Builds everything again (pages changed, style structure changed). */
	void RebuildAll();

	/** Shows a dialog on top; Buttons[Cancel] on Back. Choice gets the button pressed. */
	void ShowDialog(const FText& DialogTitle, const TAttribute<FText>& Body, const TArray<FMenuDialogButton>& Buttons, int32 DefaultButton, int32 CancelButton, TFunction<void(int32)> Choice);
	void CloseDialog();
	bool IsDialogOpen() const { return ActiveDialog.IsValid(); }

	// ---- IMenuSettingsRowHost ----

	virtual UMenuSettingsSubsystem* GetSettings() const override;
	virtual FMenuSettingContext GetContext() const override;
	virtual FMenuStyleRef GetStyle() const override { return StyleHolder; }
	virtual int32 GetUserIndex() const override;
	virtual FString GetPreviewValue(const UMenuSetting& Setting) const override;
	virtual void SetPreviewValue(const UMenuSetting& Setting, const FString& Value) override;
	virtual void OnRowFocused(const TSharedRef<SMenuSettingRow>& Row) override;
	virtual void OnRowHovered(const TSharedRef<SMenuSettingRow>& Row, bool bHovered) override;
	virtual bool OpenOptionList(const TSharedRef<SMenuSettingRow>& Row) override;
	virtual void Confirm(const FText& DialogTitle, const FText& Body, TFunction<void()> OnYes) override;
	virtual void PlaySound(EMenuSoundEvent Event) override;
	virtual void Announce(const FText& Text) override;
	virtual void OnKeyCaptureChanged(const TSharedRef<SMenuSettingRow>& Row, bool bCapturing) override;

	// ---- SWidget ----

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	const TArray<FMenuSettingsPage>& GetAllPages() const;
	void ComputeVisiblePages();
	bool IsSettingShownNow(const UMenuSetting& Setting) const;
	bool AreConditionsMetNow(const TArray<FMenuSettingCondition>& Conditions) const;
	void BuildTabs();
	void BuildRows();
	TArray<FName> CollectShownIds() const;
	void AddRow(UMenuSetting* Setting);
	void AddHeader(const FText& Text, bool bMinor);
	void RefreshRows();
	void UpdateFooterPrompts();
	void UpdateDescription(UMenuSetting* Setting);
	bool MatchesKey(const TArray<FKey>& Keys, const FKey& Key) const;
	FKey KeyForDevice(const TArray<FKey>& Keys) const;
	bool IsTyping() const;
	void Back();
	void ShowKeepChangesDialog();
	EActiveTimerReturnType HandleTick(double InCurrentTime, float InDeltaTime);
	void HandleSettingChanged(FName SettingId, const FString& Value);
	void HandleStyleChanged();

	TSharedRef<SWidget> BuildFooter();
	TSharedRef<SWidget> BuildDescription();
	TSharedRef<SWidget> MakePrompt(const TArray<FKey>& Keys, const FText& Label, FSimpleDelegate OnClicked);

	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	TWeakObjectPtr<UMenuSettingsSubsystem> Settings;
	TWeakObjectPtr<ULocalPlayer> LocalPlayer;
	TWeakObjectPtr<UObject> SoundContext;
	TArray<FMenuSettingsPage> PreviewPages;
	TMap<FName, FString> PreviewValues;
	TArray<FName> PageFilter;
	TArray<int32> VisiblePages;
	int32 CurrentPage = 0;
	bool bApplyButton = false;
	bool bConfirmUnsaved = true;
	bool bShowBack = true;
	bool bShowReset = true;
	FMenuPanelKeys Keys;
	FMenuPanelTexts Texts;
	TAttribute<FText> TitleAttribute;
	TFunction<TSharedPtr<SWidget>(UMenuSetting*)> MakeCustomRow;
	FSimpleDelegate BackDelegate;
	FOnMenuPanelPage PageChangedDelegate;
	FOnMenuPanelSetting SettingFocusedDelegate;

	FText SearchText;
	TArray<TSharedPtr<SMenuSettingRow>> Rows;
	TArray<FName> ShownIds;
	TSharedPtr<SVerticalBox> RowsBox;
	TSharedPtr<SScrollBox> ScrollBox;
	TSharedPtr<SHorizontalBox> TabsBox;
	TSharedPtr<SOverlay> DialogLayer;
	TSharedPtr<SWidget> ActiveDialog;
	TWeakPtr<SWidget> FocusBeforeDialog;
	TSharedPtr<SMenuImage> PreviewImage;
	TSharedPtr<SMenuImage> BackgroundImage;

	struct FPromptEntry
	{
		TSharedPtr<SMenuKeyPrompt> Prompt;
		TArray<FKey> Keys;
	};
	TArray<FPromptEntry> Prompts;

	TSharedPtr<class SEditableText> SearchWidget;
	TUniquePtr<FSlateBrush> SearchBrush;
	TUniquePtr<FSlateBrush> PanelBrush;
	TFunction<void(int32)> ActiveDialogChoice;
	int32 ActiveDialogCancel = INDEX_NONE;
	int32 QueuedDialogChoice = INDEX_NONE;
	bool bDialogChoiceQueued = false;

	/** The description panel's texts, made when the described setting or a value changes. */
	FText DescTitle;
	FText DescValueLine;
	FText DescBody;
	FText DescOption;
	FText DescWarning;
	FText DescNote;
	bool bRefreshQueued = false;

	TWeakObjectPtr<UMenuSetting> DescribedSetting;
	TWeakPtr<SMenuSettingRow> FocusedRow;
	bool bCapturingKey = false;
	bool bKeepDialogShown = false;
	bool bRebuildQueued = false;

	FWidgetMotionValue PageFade;
	FWidgetMotionValue PageSlide;

	TArray<FUISoundRuntime> SoundRuntimes;
	double LastSoundTime = 0.0;

	FDelegateHandle SettingChangedHandle;
	FDelegateHandle PagesChangedHandle;
	FDelegateHandle KeysChangedHandle;
	FDelegateHandle DeviceChangedHandle;
	FDelegateHandle AccessibilityHandle;
};
