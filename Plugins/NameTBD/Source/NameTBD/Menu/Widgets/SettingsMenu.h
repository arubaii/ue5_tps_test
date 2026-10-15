// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "InputCoreTypes.h"
#include "NameTBD/Menu/MenuScreenInterface.h"
#include "NameTBD/Menu/Settings/MenuSetting.h"
#include "SMenuControls.h"

#include "SettingsMenu.generated.h"

class SMenuSettingRow;
class SMenuSettingsPanel;
class UMenuSettingRowWidget;
class UMenuSettingsSchema;
class UMenuSettingsSubsystem;
class UMenuStyleAsset;
class UTextBlock;
class FMenuStandaloneRowHost;

/** When a settings screen's changes take effect. */
UENUM(BlueprintType)
enum class EMenuApplyPolicy : uint8
{
	Instant UMETA(ToolTip = "As they change (display changes still ask to be kept). Saved when the screen closes. Console style."),
	ApplyButton UMETA(ToolTip = "Changes wait for Apply (a key and a footer prompt); Back asks about unapplied changes. PC style.")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSettingsMenuBack);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSettingsMenuName, FName, Id);

/**
 * Settings Menu (palette NameTBD): a complete, production settings screen - page tabs, sections, every kind of setting row,
 * search, a description panel, key prompts for the device in use, Apply / Revert / Reset to Defaults, the keep-display-changes
 * countdown, key rebinding, narration and sounds - built from the player's settings (the built-in Gameplay / Video / Audio /
 * Controls / Accessibility pages and your schemas). Its whole look comes from a Menu Style asset.
 *
 * Use it as the content of a screen (Menu Settings Screen does the wiring with the Menu Manager), filter it to some pages for a
 * focused screen ("Controls" only), or place Setting Controls one by one in your own layout instead.
 */
UCLASS(meta = (DisplayName = "Settings Menu"))
class NAMETBD_API USettingsMenu : public UWidget
{
	GENERATED_BODY()

public:

	USettingsMenu();

	/** The look (colours, type, sizes, motion, sounds). Empty: the project's Default Style, else Simple. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings Menu")
	TObjectPtr<UMenuStyleAsset> StyleAsset;

	/** The pages shown, in this order ("Controls", "Video", "Audio", "Accessibility"). Empty: every page. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings Menu")
	TArray<FName> Pages;

	/** The page it opens on. None: the first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings Menu")
	FName InitialPage;

	/** Your settings for this menu (merged over the built-in pages while it is shown). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings Menu")
	TArray<TObjectPtr<UMenuSettingsSchema>> ExtraSchemas;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings Menu")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings Menu")
	EMenuApplyPolicy ApplyPolicy = EMenuApplyPolicy::Instant;

	/** Back with unapplied changes asks: Apply, Discard or Cancel (Apply Button policy). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings Menu")
	bool bConfirmUnsavedOnBack = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings Menu")
	bool bShowBackPrompt = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings Menu")
	bool bShowResetPrompt = true;

	/** Focus goes to the first setting when it is shown (pads need focus to start). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Settings Menu")
	bool bFocusOnShow = true;

	// ---- Keys ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Keys")
	TArray<FKey> PreviousTabKeys;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Keys")
	TArray<FKey> NextTabKeys;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Keys")
	TArray<FKey> ApplyKeys;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Keys")
	TArray<FKey> ResetKeys;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Keys")
	TArray<FKey> BackKeys;

	// ---- Words (empty: the built-in ones) ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FText ApplyText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FText RevertText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FText ResetText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FText BackText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Text")
	FText SearchHint;

	// ---- Events ----

	/** The player backed out with nothing left to confirm: close the screen. */
	UPROPERTY(BlueprintAssignable, Category = "Settings Menu")
	FOnSettingsMenuBack OnBack;

	UPROPERTY(BlueprintAssignable, Category = "Settings Menu")
	FOnSettingsMenuName OnPageChanged;

	UPROPERTY(BlueprintAssignable, Category = "Settings Menu")
	FOnSettingsMenuName OnSettingFocused;

	// ---- Blueprint ----

	/** Starts an editing session (what Revert returns to). Shown by itself; call it when reopening a kept widget. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void OpenSession();

	/** Ends the session and saves (bApplyPending: applies what waits first, else it is dropped). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void CloseSession(bool bApplyPending = false);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	bool SetPage(FName PageId);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void NextPage();

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void PreviousPage();

	UFUNCTION(BlueprintPure, Category = "Menu|Settings Menu")
	FName GetCurrentPage() const;

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void ApplyChanges();

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void RevertChanges();

	/** Asks, then resets the current page. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void ResetCurrentPage();

	/** Back as the player pressed it. True: handled inside (a dialog, the search, unapplied changes asked about); false: On Back fired. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	bool RequestBack();

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void FocusFirstSetting();

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void FocusSetting(FName SettingId);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void SetStyleAsset(UMenuStyleAsset* InStyleAsset);

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void SetSearchText(FText InText);

	/** Builds the pages again (after registering settings). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void RefreshSettingsMenu();

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UMenuSettingsSubsystem* FindSettings() const;
	void HandleStyleChanged(const UMenuStyleAsset* Changed);
	TSharedPtr<SWidget> MakeCustomRow(UMenuSetting* Setting);

	TSharedPtr<SMenuSettingsPanel> Panel;
	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	FDelegateHandle StyleHandle;
	TWeakObjectPtr<UMenuSettingsSubsystem> SessionSettings;
	bool bSessionOpen = false;

	/** The designer's pages (no player there). */
	UPROPERTY(Transient)
	TObjectPtr<UMenuSettingsSchema> PreviewSchema;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMenuSettingRowWidget>> CustomRows;
};

/**
 * Setting Control (palette NameTBD): one setting's row in the built-in look - put single settings anywhere in your own layout
 * (a Brightness slider on a first-launch screen, Subtitles on a pause menu).
 */
UCLASS(meta = (DisplayName = "Setting Control"))
class NAMETBD_API UMenuSettingControl : public UWidget
{
	GENERATED_BODY()

public:

	/** The setting's Id ("Video.Brightness"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting Control")
	FName SettingId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting Control")
	TObjectPtr<UMenuStyleAsset> StyleAsset;

	UFUNCTION(BlueprintCallable, Category = "Menu|Settings Menu")
	void SetSettingId(FName InSettingId);

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<FMenuStandaloneRowHost> Host;
	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();

	UPROPERTY(Transient)
	TObjectPtr<UMenuSettingsSchema> PreviewSchema;
};

/**
 * Your own look for a setting row, in UMG: make a Widget Blueprint from this, give it the parts you want (Label Text, Value Text,
 * Description Text are filled for you when present), drive it from On Setting Assigned / On Setting Refreshed, and change the value
 * with Set Value / Step / Reset To Default / Run Action. Use it in a Menu Style's Custom Rows (every row of a kind), or place it
 * yourself with a Setting Id.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Menu Setting Row Widget"))
class NAMETBD_API UMenuSettingRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** The setting shown when placed by hand (a Settings Menu's custom rows are given theirs). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting", meta = (ExposeOnSpawn = "true"))
	FName SettingId;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Setting")
	TObjectPtr<UMenuSetting> Setting;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ValueText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

	/** Gives the row its setting (and refreshes it). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Setting Row")
	void InitializeRow(UMenuSetting* InSetting);

	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Setting Row")
	void OnSettingAssigned(UMenuSetting* AssignedSetting);

	/** The value (or whether it can change) changed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Setting Row")
	void OnSettingRefreshed(const FString& CurrentValue, const FText& DisplayValue, bool bEnabled, const FText& DisabledReason);

	UFUNCTION(BlueprintPure, Category = "Menu|Setting Row")
	FString GetValue() const;

	UFUNCTION(BlueprintPure, Category = "Menu|Setting Row")
	FText GetDisplayValue() const;

	UFUNCTION(BlueprintPure, Category = "Menu|Setting Row")
	TArray<FMenuSettingOption> GetOptions() const;

	UFUNCTION(BlueprintPure, Category = "Menu|Setting Row")
	bool CanChange(FText& Reason) const;

	UFUNCTION(BlueprintPure, Category = "Menu|Setting Row")
	UMenuSettingsSubsystem* GetSettings() const;

	UFUNCTION(BlueprintCallable, Category = "Menu|Setting Row")
	void SetValue(const FString& NewValue);

	/** Left / right: Direction -1 or 1. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Setting Row")
	void Step(int32 Direction);

	UFUNCTION(BlueprintCallable, Category = "Menu|Setting Row")
	void ResetToDefault();

	UFUNCTION(BlueprintCallable, Category = "Menu|Setting Row")
	void RunAction();

	/** Reads the value again and fires On Setting Refreshed. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Setting Row")
	void RefreshRow();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void HandleSettingChanged(FName ChangedId, const FString& NewValue);
	FDelegateHandle ChangedHandle;
};

/**
 * A ready settings screen for the Menu Manager: make a Widget Blueprint from this with a Settings Menu named SettingsMenu in it
 * (the AI Assistant's create_settings_menu does), register it as a screen. Opening it starts an editing session and focuses the
 * first setting; Back goes through the menu (dialogs, unapplied changes) and closes the screen; closing it saves.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Menu Settings Screen"))
class NAMETBD_API UMenuSettingsScreen : public UUserWidget, public IMenuScreenInterface
{
	GENERATED_BODY()

public:

	UMenuSettingsScreen(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<USettingsMenu> SettingsMenu;

	virtual void InitializeScreen_Implementation(UObject* MenuContext) override;
	virtual void OnScreenOpened_Implementation(UObject* MenuContext) override;
	virtual void OnScreenClosed_Implementation() override;
	virtual bool OnBackRequested_Implementation() override;

protected:
	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent) override;

private:
	UFUNCTION()
	void HandleMenuBack();
};
