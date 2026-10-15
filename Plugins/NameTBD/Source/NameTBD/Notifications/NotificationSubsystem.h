// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/StreamableManager.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "NotificationTypes.h"

#include "NotificationSubsystem.generated.h"

class APlayerController;
class FNotificationInputProcessor;
class SWidget;
class UNotificationHost;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotificationEvent, const FNotificationView&, Notification);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNotificationDismissedEvent, const FNotificationView&, Notification, ENotificationDismissReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNotificationActionEvent, const FNotificationView&, Notification, FName, ActionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNotificationHistoryChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotificationUnreadChanged, int32, UnreadCount);

/** Where a notification is in its life. */
enum class ENotificationState : uint8
{
	/** Waiting: no room, no host, held, paused. */
	Pending,
	/** On screen, its time running (unless frozen). */
	Visible,
	/** Its host is playing it out; gone when that ends. */
	Leaving
};

/** One live notification (the subsystem's own record). */
struct FNotificationRecord
{
	int32 Id = 0;
	FNotificationRequest Request;
	FNotificationTypeSettings TypeSettings;
	FGameplayTag Channel;
	ENotificationPriority Priority = ENotificationPriority::Normal;
	ENotificationSeverity Severity = ENotificationSeverity::Info;
	FName StackKey;

	ENotificationState State = ENotificationState::Pending;

	/** Seconds on screen in all, 0: until dismissed. */
	float Duration = 0.f;
	float Remaining = 0.f;

	/** Seconds on screen so far (only while not frozen) - for Min Display Time. */
	float ShownFor = 0.f;

	/** Real seconds (FPlatformTime) when posted, when last merged / replaced, when it last started waiting. */
	double PostTime = 0.0;
	double StackTime = 0.0;
	double QueuedSince = 0.0;
	double LeavingSince = 0.0;
	FDateTime PostedAt;

	/** Posting order, for oldest-first. */
	int64 Sequence = 0;

	TWeakObjectPtr<UNotificationHost> Host;

	/** It has been on screen once (no Shown sound or announcement again after a Hold). */
	bool bShownOnce = false;

	/** Posted while the game was paused (from a pause menu): the pause doesn't stop its time. Cleared when the game runs again. */
	bool bPostedWhilePaused = false;

	/** Why it is leaving (reported when it has gone). */
	ENotificationDismissReason LeaveReason = ENotificationDismissReason::TimedOut;
	FName RespondedAction;

	/** Native callbacks (Post Notification With Callbacks); a post merged into this one adds its own. */
	TArray<FOnNotificationActionNative> ActionCallbacks;
	TArray<FOnNotificationFinishedNative> FinishedCallbacks;

	FNotificationView MakeView() const;
};

/**
 * The notification system of one local player: everything posted goes through here, and here is decided what shows, where,
 * when and for how long. Notification Hosts (widgets placed in the HUD) register with it and draw what it hands them.
 *
 * - Post: a request is resolved against its type's settings (Project Settings > Plugins > Notifications), throttled by
 *   Cooldown, stacked into a like one (Merge / Replace / Ignore), checked against the player's Do Not Disturb, muted types
 *   and the active suppressions, then queued on its channel.
 * - Schedule (every frame, real time): per channel, the host showing it gets the waiting notifications by priority (or oldest
 *   first) while it has room; higher priority ones push lower ones off (Preemption), a full feed lets its oldest go (Make
 *   Room); waiting ones too old are dropped (Max Queue Time); no host - they wait (or are dropped).
 * - Time: real seconds, stopped while the player hovers / focuses the host, while frozen, while the game is paused (types that
 *   pause), scaled by the player's Duration Scale; Timeout Action is taken when it runs out.
 * - Actions: buttons, and keys (taken by the most important notification on screen that has keys; Hold Time to hold).
 * - History: notifications of types that keep history, read / unread; storable in a save game.
 *
 * One per local player (split screen gets one each); kept across map changes. Ticks on the core ticker, so a paused game
 * still drives it (types decide whether their time stops).
 *
 * Console: NameTBD.Notify.Test, NameTBD.Notify.Post <Type> <Title>, NameTBD.Notify.Clear, NameTBD.Notify.Dump,
 * NameTBD.Notify.DND 0|1.
 */
UCLASS()
class NAMETBD_API UNotificationSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:

	/**
	 * The subsystem of Player's local player; with no Player, of the player that owns WorldContext (a widget's owner), else of
	 * the world's first local player. Null on a dedicated server.
	 */
	static UNotificationSubsystem* Get(const UObject* WorldContext, const APlayerController* Player = nullptr);

	// USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Keeps what live notifications point at (images, context objects, payload objects) loaded. */
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	// -------------------------------------------------------------------------------------------------
	// Posting
	// -------------------------------------------------------------------------------------------------

	/**
	 * Shows a notification (see FNotificationRequest). Returns its handle - the one it merged into when it stacked. Invalid
	 * only when it was dropped outright (throttled); a suppressed one gets a handle that is no longer active.
	 */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	FNotificationHandle PostNotification(const FNotificationRequest& Request);

	/** The same, with callbacks: OnAction each time an action is taken, OnFinished once when it has gone. */
	FNotificationHandle PostNotificationWithCallbacks(const FNotificationRequest& Request, FOnNotificationActionNative OnAction, FOnNotificationFinishedNative OnFinished);

	/** Changes a notification's content in place (Count and Value too), keeping its place; bRestartTime gives it its full time again. */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	bool UpdateNotification(FNotificationHandle Handle, const FNotificationRequest& Request, bool bRestartTime = false);

	/** Moves its progress bar (0 to 1; negative hides it). */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	bool SetNotificationProgress(FNotificationHandle Handle, float InProgress);

	/** Changes its Title and Body (formatted as when posted). Empty keeps what it has. */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	bool SetNotificationText(FNotificationHandle Handle, const FText& InTitle, const FText& InBody);

	/** Removes it (played out on screen). */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	bool DismissNotification(FNotificationHandle Handle);

	/** Takes one of its actions as if the player had (On Notification Action; it goes when the action dismisses). */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	bool RespondToNotification(FNotificationHandle Handle, FName ActionId);

	/** Removes every notification, or those of these types / channels (and their child tags) when given. */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	void ClearNotifications(FGameplayTagContainer TypesOrChannels, bool bIncludeQueued = true);

	/** On screen or waiting. */
	UFUNCTION(BlueprintPure, Category = "Notifications")
	bool IsNotificationActive(FNotificationHandle Handle) const;

	/** On screen now. */
	UFUNCTION(BlueprintPure, Category = "Notifications")
	bool IsNotificationVisible(FNotificationHandle Handle) const;

	/** A notification as widgets see it (false when it is no longer active). */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	bool GetNotification(FNotificationHandle Handle, FNotificationView& OutNotification) const;

	/** Every active notification (on screen first), optionally only those of these types / channels. */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	TArray<FNotificationView> GetActiveNotifications(FGameplayTagContainer TypesOrChannels) const;

	/** How many are waiting for room in the channels Channels matches (a host's "+3 more"). */
	UFUNCTION(BlueprintPure, Category = "Notifications")
	int32 GetQueuedCount(FGameplayTagContainer InChannels) const;

	/** Seconds it has left (0 for one that stays until dismissed). */
	UFUNCTION(BlueprintPure, Category = "Notifications")
	float GetTimeRemaining(FNotificationHandle Handle) const;

	/** What is left of its time, 1 to 0 (1 for one that stays until dismissed) - a timer bar. */
	UFUNCTION(BlueprintPure, Category = "Notifications")
	float GetTimeRemainingFraction(FNotificationHandle Handle) const;

	// -------------------------------------------------------------------------------------------------
	// Suppression and preferences
	// -------------------------------------------------------------------------------------------------

	/**
	 * Holds, freezes or drops notifications (see ENotificationSuppressionMode) until Pop Notification Suppression - for a
	 * cutscene, a menu, a boss intro. Only those of these types / channels (and their child tags) when given; types with Ignore
	 * Suppression are never held. Returns an id to pop it with; suppressions add up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	int32 PushNotificationSuppression(ENotificationSuppressionMode Mode, FGameplayTagContainer TypesOrChannels);

	UFUNCTION(BlueprintCallable, Category = "Notifications")
	void PopNotificationSuppression(int32 SuppressionId);

	UFUNCTION(BlueprintCallable, Category = "Notifications")
	void SetNotificationPreferences(const FNotificationUserPreferences& InPreferences);

	UFUNCTION(BlueprintPure, Category = "Notifications")
	const FNotificationUserPreferences& GetNotificationPreferences() const { return Preferences; }

	/** Every type in the settings players may mute, with its name and whether it is muted - a settings screen's list. */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	TArray<FNotificationTypeInfo> GetNotificationTypes() const;

	/** Mutes or unmutes a type (Muted Types in the preferences). */
	UFUNCTION(BlueprintCallable, Category = "Notifications")
	void SetNotificationTypeMuted(FGameplayTag Type, bool bMuted);

	// -------------------------------------------------------------------------------------------------
	// History
	// -------------------------------------------------------------------------------------------------

	/** The history, newest first. */
	UFUNCTION(BlueprintPure, Category = "Notifications|History")
	const TArray<FNotificationView>& GetNotificationHistory() const { return History; }

	UFUNCTION(BlueprintPure, Category = "Notifications|History")
	int32 GetUnreadCount() const;

	/** Marks every history entry read (a notification center opened). */
	UFUNCTION(BlueprintCallable, Category = "Notifications|History")
	void MarkNotificationsRead();

	UFUNCTION(BlueprintCallable, Category = "Notifications|History")
	void RemoveFromHistory(FNotificationHandle Handle);

	UFUNCTION(BlueprintCallable, Category = "Notifications|History")
	void ClearNotificationHistory();

	/** Puts back a history saved earlier (Get Notification History into a save game), newest first, as it was. */
	UFUNCTION(BlueprintCallable, Category = "Notifications|History")
	void RestoreNotificationHistory(const TArray<FNotificationView>& SavedHistory);

	// -------------------------------------------------------------------------------------------------
	// Events
	// -------------------------------------------------------------------------------------------------

	/** Posted (before it shows; also when it is suppressed straight away). Not for merges: see On Notification Updated. */
	UPROPERTY(BlueprintAssignable, Category = "Notifications")
	FOnNotificationEvent OnNotificationPosted;

	/** It came on screen (the first time). */
	UPROPERTY(BlueprintAssignable, Category = "Notifications")
	FOnNotificationEvent OnNotificationShown;

	/** Its content changed: merged into, replaced, updated, progress. */
	UPROPERTY(BlueprintAssignable, Category = "Notifications")
	FOnNotificationEvent OnNotificationUpdated;

	/** The player (or Respond To Notification) took one of its actions. */
	UPROPERTY(BlueprintAssignable, Category = "Notifications")
	FOnNotificationActionEvent OnNotificationAction;

	/** The player clicked it (not on a button) - open the screen it is about. */
	UPROPERTY(BlueprintAssignable, Category = "Notifications")
	FOnNotificationEvent OnNotificationClicked;

	/** It has gone (after its exit played), or was never shown. Once per notification. */
	UPROPERTY(BlueprintAssignable, Category = "Notifications")
	FOnNotificationDismissedEvent OnNotificationDismissed;

	UPROPERTY(BlueprintAssignable, Category = "Notifications|History")
	FOnNotificationHistoryChanged OnHistoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "Notifications|History")
	FOnNotificationUnreadChanged OnUnreadCountChanged;

	// -------------------------------------------------------------------------------------------------
	// For hosts and entry widgets
	// -------------------------------------------------------------------------------------------------

	void RegisterHost(UNotificationHost* Host);
	void UnregisterHost(UNotificationHost* Host);

	/** Host has finished playing Id out (or dropped it). */
	void NotifyHostEntryGone(UNotificationHost* Host, int32 Id);

	/** The player closed it (close button, swipe, Dismiss key). Ignored for types that don't allow it. */
	void DismissByPlayer(FNotificationHandle Handle);

	/** The player clicked it. */
	void NotifyClicked(FNotificationHandle Handle);

	/** True for the notification the action keys go to now. */
	bool IsShortcutTarget(FNotificationHandle Handle) const { return Handle.IsValid() && Handle.Id == ShortcutTargetId; }

	/** How far a held action key has got, 0 to 1 (0 when not held). */
	float GetHoldProgress(FNotificationHandle Handle, FName ActionId) const;

	/** The key an action shows in its hint now: gamepad or keyboard, from what the player last used. */
	FKey GetDisplayKey(const FNotificationAction& Action) const;

	/** How a key is shown (Key Glyphs, else its short name). */
	static FNotificationKeyGlyph GetKeyGlyph(const FKey& Key);

	/** The player last pressed a gamepad button (rather than a key or the mouse). */
	bool IsUsingGamepad() const { return bUsingGamepad; }

	/** Called by the input processor; true when the key was taken. */
	bool HandleKey(const FKey& Key, int32 UserIndex, bool bDown, bool bRepeat);

	/** Records which device was used last (any input). */
	void NoteInputDevice(bool bGamepad);

	/** The Slate user index of this local player (-1 when unknown). */
	int32 GetSlateUserIndex() const;

	/** The record of Id. */
	const FNotificationRecord* FindRecord(int32 Id) const;

	/** Lists every record (console Dump). */
	void DumpToLog() const;

private:

	bool Tick(float DeltaTime);

	FNotificationRecord* FindRecordMutable(int32 Id);
	TSharedPtr<FNotificationRecord> FindStackTarget(const FNotificationRecord& Incoming) const;

	/** True when a suppression of Mode (or a stronger one for Hold) covers Record. */
	bool IsSuppressed(const FNotificationRecord& Record, ENotificationSuppressionMode Mode) const;

	/** The player's choices hide it (Do Not Disturb, muted). */
	bool IsBlockedByPlayer(const FNotificationRecord& Record) const;

	/** Nothing of Record's may show now (held, cinematic, paused). */
	bool IsHeld(const FNotificationRecord& Record, bool bGamePaused) const;

	/** Its time doesn't run now. */
	bool IsFrozen(const FNotificationRecord& Record, bool bGamePaused) const;

	/** The host that shows Channel (the highest Host Priority of the live ones that show it). */
	UNotificationHost* FindHostFor(const FGameplayTag& Channel) const;

	void ScheduleChannel(const FGameplayTag& Channel, bool bGamePaused, double Now);
	void Show(FNotificationRecord& Record, UNotificationHost* Host);

	/** Sends a shown one back to waiting (its host went, a Hold, a requeued preemption), keeping its time. */
	void ReturnToQueue(FNotificationRecord& Record);

	/** Starts it going: played out by its host, or at once when not on screen. */
	void BeginDismiss(int32 Id, ENotificationDismissReason Reason);

	/** It has gone: out of the records, then ReportGone. */
	void FinishRemoval(int32 Id);

	/** History (when kept), On Notification Dismissed, the finished callbacks - for one removed, or never added (suppressed). */
	void ReportGone(const TSharedRef<FNotificationRecord>& Record);

	/** Takes an action on Id: events, callbacks, then dismissal when the action dismisses (always, on a timeout). */
	bool RespondInternal(int32 Id, FName ActionId, bool bTimeout);

	/** Shown ones that must leave the screen now (a Hold suppression, cinematic mode). */
	bool ShouldHide(const FNotificationRecord& Record) const;

	bool IsCinematic() const;

	void AddToHistory(const FNotificationView& View, bool bRead);
	void BroadcastUnreadIfChanged();

	/** Seconds Record should stay on screen (type, text, player), 0: until dismissed. */
	float ResolveDuration(const FNotificationRecord& Record) const;

	/** Fills Record's resolved fields (type settings, channel, priority, severity, stack key) from its request. */
	void ResolveRecord(FNotificationRecord& Record) const;

	void PushViewToHost(const FNotificationRecord& Record, bool bStacked);
	void PlaySound(const FNotificationRecord& Record, const FUISoundSlot& Slot);
	void Announce(const FNotificationRecord& Record);
	void TickAnnouncements(float DeltaTime);

	/** The notification the action keys go to: the most important on screen with a keyed action, newest first. */
	void UpdateShortcutTarget();
	void TickHold(float DeltaTime);

	/** Drops waiting ones over the channel's Max Queued. */
	void TrimQueue(const FGameplayTag& Channel);

	bool MatchesFilter(const FNotificationRecord& Record, const FGameplayTagContainer& TypesOrChannels) const;

	/**
	 * Channel has waiting notifications and no live host: after Default Host Delay, puts a default host for it on the player's
	 * screen (Create Default Hosts, and no host of the game's has ever shown it), else warns once.
	 */
	void HandleNoHost(const FGameplayTag& Channel, double Now);
	void AddDefaultHost(const FGameplayTag& Channel);

	/** Takes default hosts off the screen: those no longer on it (a map change cleared the viewport), or (bAll) every one. */
	void RemoveDefaultHosts(bool bAll, const FGameplayTagContainer* CoveredChannels = nullptr);

	UWorld* GetPlayerWorld() const;
	APlayerController* GetPlayerController() const;

	TArray<TSharedRef<FNotificationRecord>> Records;
	TArray<TWeakObjectPtr<UNotificationHost>> Hosts;

	/** Hosts the subsystem made itself (Create Default Hosts), and the Slate it put on the player's screen for each. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNotificationHost>> DefaultHosts;
	TArray<TSharedPtr<SWidget>> DefaultHostWidgets;

	/** Every channel a host of the game's has been made for: those never get a default host. */
	FGameplayTagContainer GameHostChannels;

	/** Channel -> real seconds it started waiting with no host; channels already warned about. */
	TMap<FGameplayTag, double> NoHostSince;
	TSet<FGameplayTag> WarnedNoHost;

	struct FSuppression
	{
		int32 Id = 0;
		ENotificationSuppressionMode Mode = ENotificationSuppressionMode::Hold;
		FGameplayTagContainer Filter;
	};
	TArray<FSuppression> Suppressions;
	int32 NextSuppressionId = 1;

	FNotificationUserPreferences Preferences;

	UPROPERTY(Transient)
	TArray<FNotificationView> History;
	int32 LastBroadcastUnread = -1;

	/** Stack Key -> real seconds it was last posted (Cooldown). */
	TMap<FName, double> LastPostTimes;

	/** Channel -> real seconds one last left it (Gap). */
	TMap<FGameplayTag, double> LastLeaveTimes;

	/** Sound bookkeeping per type, so a type's Min Time Between Sounds holds. */
	TMap<FGameplayTag, FUISoundRuntime> SoundRuntimes;

	TArray<FText> PendingAnnouncements;
	float AnnounceWait = 0.f;

	int32 ShortcutTargetId = 0;
	FKey HeldKey;
	int32 HeldRecordId = 0;
	FName HeldActionId;
	float HeldFor = 0.f;
	float HeldNeeded = 0.f;
	bool bUsingGamepad = false;

	TSharedPtr<FNotificationInputProcessor> InputProcessor;

	/** Loads every type's Entry Widget Class ahead, so the first notification of a type doesn't wait for it. */
	FStreamableManager Streamable;
	TSharedPtr<FStreamableHandle> PreloadHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	int64 NextSequence = 0;

	/** Ids are unique across every player's subsystem for the session. */
	static int32 NextId;
};
