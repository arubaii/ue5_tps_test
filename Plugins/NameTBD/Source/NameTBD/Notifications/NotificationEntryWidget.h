// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NotificationTypes.h"

#include "NotificationEntryWidget.generated.h"

class UButton;
class UImage;
class UNotificationActionWidget;
class UNotificationHost;
class UNotificationSubsystem;
class UPanelWidget;
class UProgressBar;
class UTextBlock;
class UWidgetSVGImage;

/**
 * The base for a notification drawn with your own widget: make a Widget Blueprint from it and set it as a Notification
 * Host's Entry Widget Class (or a type's, in Project Settings > Plugins > Notifications). Entries are pooled and reused.
 *
 * Name parts as below and they are filled in for you (all optional): Title Text, Body Text, Icon Image / Icon SVG, Count Text, Progress
 * Bar, Timer Bar, Actions Panel (one Action Widget Class per action). Or do it all yourself in On Notification Set /
 * Updated. The host animates the entry in and out; play your own animations on top from those events.
 */
UCLASS(Abstract, Blueprintable)
class NAMETBD_API UNotificationEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	/** The notification shown (its latest content). */
	UFUNCTION(BlueprintPure, Category = "Notification")
	const FNotificationView& GetNotification() const { return Notification; }

	/** Shown in a history host (no timer, no actions). */
	UFUNCTION(BlueprintPure, Category = "Notification")
	bool IsHistoryEntry() const { return bHistoryEntry; }

	/** Closes it (when its type allows it); in a history, removes it from the history. */
	UFUNCTION(BlueprintCallable, Category = "Notification")
	void Dismiss();

	/** Takes one of its actions. */
	UFUNCTION(BlueprintCallable, Category = "Notification")
	void Respond(FName ActionId);

	/** Seconds it has left. */
	UFUNCTION(BlueprintPure, Category = "Notification")
	float GetTimeRemaining() const;

	/** What is left of its time, 1 to 0 (1 for one that stays until dismissed). */
	UFUNCTION(BlueprintPure, Category = "Notification")
	float GetTimeRemainingFraction() const;

	/** How far a held key has got on an action, 0 to 1. */
	UFUNCTION(BlueprintPure, Category = "Notification")
	float GetHoldProgress(FName ActionId) const;

	/** The action keys go to this notification now (show their hints). */
	UFUNCTION(BlueprintPure, Category = "Notification")
	bool IsShortcutTarget() const;

	/** The key an action shows now (gamepad or keyboard, from what the player last used). None for none. */
	UFUNCTION(BlueprintPure, Category = "Notification")
	FKey GetActionKey(const FNotificationAction& Action) const;

	/** How a key is shown: Key Glyphs (Project Settings > Plugins > Notifications), else its short name. */
	UFUNCTION(BlueprintPure, Category = "Notification")
	static FNotificationKeyGlyph GetKeyGlyph(const FKey& Key);

	// Called by the host
	void SetNotification(const FNotificationView& InNotification, UNotificationHost* InHost, UNotificationSubsystem* InSubsystem, bool bInHistoryEntry);
	void UpdateNotification(const FNotificationView& InNotification, bool bStacked);
	void ClearNotification();
	void RefreshShortcutState();

protected:

	/** It was given a notification to show (a pooled entry is reused for another). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notification")
	void OnNotificationSet(const FNotificationView& InNotification);

	/** Its content changed. bStacked: one merged into it (pop the count). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notification")
	void OnNotificationUpdated(const FNotificationView& InNotification, bool bStacked);

	/** It left the screen and goes back to the pool. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notification")
	void OnNotificationCleared();

	/** The action keys came to it or left it (show / hide key hints). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notification")
	void OnShortcutTargetChanged(bool bIsTarget);

	virtual void NativeOnNotificationSet(bool bStacked);
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Fills the named parts from the notification. */
	void FillParts();

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BodyText;

	/** Shows the notification's image when it is a texture or material (hidden otherwise). */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UImage> IconImage;

	/** Shows the notification's image when it is an SVG (hidden otherwise) - an SVG Image next to Icon Image. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSVGImage> IconSVG;

	/** Shows the count from 2 up (Count Format), hidden otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CountText;

	/** Shows Progress, hidden when it has none. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar;

	/** Empties as its time runs out; hidden for one that stays until dismissed. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> TimerBar;

	/** Gets one Action Widget Class per action. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> ActionsPanel;

	/** The widget made for each action in the Actions Panel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notification")
	TSubclassOf<UNotificationActionWidget> ActionWidgetClass;

	/** How Count Text writes the count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notification")
	FText CountFormat = NSLOCTEXT("NotificationEntryWidget", "CountFormat", "×{Count}");

	/** Fills the named parts. Off: you fill them yourself in On Notification Set / Updated. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notification")
	bool bFillParts = true;

private:

	UPROPERTY(Transient)
	FNotificationView Notification;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UNotificationActionWidget>> ActionWidgets;

	TWeakObjectPtr<UNotificationHost> WeakHost;
	TWeakObjectPtr<UNotificationSubsystem> WeakSubsystem;
	bool bHistoryEntry = false;
	bool bWasShortcutTarget = false;
};

/**
 * One action of a notification in a custom entry: make a Widget Blueprint from it and set it as the entry's Action Widget
 * Class. Name parts Action Button (clicking it takes the action), Label Text, Key Text (the key's hint, shown while the entry
 * holds the keys), Hold Bar (fills as the key is held) - all optional.
 */
UCLASS(Abstract, Blueprintable)
class NAMETBD_API UNotificationActionWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintPure, Category = "Notification")
	const FNotificationAction& GetAction() const { return Action; }

	/** Takes the action. */
	UFUNCTION(BlueprintCallable, Category = "Notification")
	void TakeAction();

	void SetAction(const FNotificationAction& InAction, UNotificationEntryWidget* InEntry);
	void RefreshKeyHint();

protected:

	/** It was given an action. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Notification")
	void OnActionSet(const FNotificationAction& InAction, bool bShowKey, const FNotificationKeyGlyph& KeyGlyph);

	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ActionButton;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyText;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HoldBar;

private:

	UFUNCTION()
	void HandleClicked();

	UPROPERTY(Transient)
	FNotificationAction Action;

	TWeakObjectPtr<UNotificationEntryWidget> WeakEntry;
};
