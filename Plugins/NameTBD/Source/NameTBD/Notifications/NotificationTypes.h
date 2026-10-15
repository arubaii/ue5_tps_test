// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "NativeGameplayTags.h"
#include "StructUtils/InstancedStruct.h"
#include "NameTBD/Sound/UISoundTypes.h"
#include "NameTBD/Widgets/WidgetSVG.h"

#include "NotificationTypes.generated.h"

class UNotificationEntryWidget;
class UWidgetSVG;

NAMETBD_API DECLARE_LOG_CATEGORY_EXTERN(LogNameTBDNotifications, Log, All);

/**
 * The tags the notification system ships with. Channels are where a notification shows (a Notification Host shows one or
 * more channels); types are what it is (each type has its own settings in Project Settings > Plugins > Notifications). Both
 * are ordinary gameplay tags: add your own under them, or anywhere - a type's settings are found by its tag or the nearest
 * parent tag that has settings.
 */
namespace NotificationTags
{
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Toast);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Banner);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Feed);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Hint);

	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_Info);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_Success);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_Warning);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_Error);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_Reward);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_Loot);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_Elimination);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_Objective);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_Achievement);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_Social);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_System);
	NAMETBD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Type_Hint);
}

/**
 * How much it matters that the player sees a notification. Higher ones are shown first, and (where the channel allows it)
 * push lower ones off the screen early. Critical is for what the player must act on now: a lost connection, a party invite
 * about to expire.
 */
UENUM(BlueprintType)
enum class ENotificationPriority : uint8
{
	Low UMETA(ToolTip = "Nice to know: shown when there is room, the first dropped when the queue is full."),
	Normal,
	High UMETA(ToolTip = "Shown before Normal and Low ones waiting with it, and may push them off early (channel's Allow Preemption)."),
	Critical UMETA(ToolTip = "Shown first, pushing others off; with Ignore Suppression on its type, also through cutscenes and Do Not Disturb.")
};

/**
 * What kind of news it is: the built-in card's accent color and glyph (a shape for each, never color alone), and something a
 * custom entry widget can read.
 */
UENUM(BlueprintType)
enum class ENotificationSeverity : uint8
{
	Info UMETA(ToolTip = "Neutral information: an 'i' in a circle."),
	Success UMETA(ToolTip = "Something worked: a check mark."),
	Warning UMETA(ToolTip = "Attention: an exclamation mark in a triangle."),
	Error UMETA(ToolTip = "Something failed: a cross."),
	Reward UMETA(ToolTip = "Something gained: XP, loot, an achievement - a star."),
	Social UMETA(ToolTip = "From another player: an invite, a message - a speech bubble.")
};

/**
 * What happens when a notification arrives while one like it (same type and Stack Key) is still on screen or waiting.
 */
UENUM(BlueprintType)
enum class ENotificationStackPolicy : uint8
{
	None UMETA(DisplayName = "Always New", ToolTip = "Each one is its own notification (a kill feed)."),
	Merge UMETA(ToolTip = "Adds into the one there: its Count and Value grow ('Wood x3', '+150 XP'), its text and image take the new ones, its time restarts (Refresh On Stack). Loot and XP feeds."),
	Replace UMETA(ToolTip = "The one there takes the new content in place, Count and Value starting over (a hint that changes, a download's status)."),
	Ignore UMETA(ToolTip = "The new one is dropped while one like it is there ('Inventory full' pressed five times shows once).")
};

/** Why a notification went away: passed to On Notification Dismissed, kept in the history. */
UENUM(BlueprintType)
enum class ENotificationDismissReason : uint8
{
	TimedOut UMETA(ToolTip = "Its time ran out (with a Timeout Action, that action was taken first)."),
	User UMETA(ToolTip = "The player closed it: the close button, a click (Click Behavior Dismiss), a swipe, or the Dismiss key."),
	Responded UMETA(ToolTip = "The player took one of its actions."),
	Code UMETA(ToolTip = "Dismiss Notification was called."),
	Preempted UMETA(ToolTip = "Pushed off by a more important one, or to make room in a full channel (Make Room)."),
	Cancelled UMETA(ToolTip = "A notification of a type in its Cancels Types arrived ('Double Kill' takes 'Kill' away)."),
	Expired UMETA(ToolTip = "It waited in the queue longer than its type's Max Queue Time, or its channel has no host and doesn't wait for one."),
	Overflow UMETA(ToolTip = "Its channel's queue was full and it was the least important waiting."),
	Suppressed UMETA(ToolTip = "Never shown: Do Not Disturb, a muted type, or a Drop suppression. Kept in the history when its type keeps history."),
	Throttled UMETA(ToolTip = "Never shown: one like it was posted within its type's Cooldown."),
	Cleared UMETA(ToolTip = "Clear Notifications, or the player went away.")
};

/** What a suppression does to the notifications it covers (Push Notification Suppression). */
UENUM(BlueprintType)
enum class ENotificationSuppressionMode : uint8
{
	Hold UMETA(ToolTip = "Nothing new shows; those on screen leave and come back, with the time they had left, when the suppression ends. Cutscenes, loading."),
	Freeze UMETA(ToolTip = "Nothing new shows; those on screen stay, their time stopped. A menu opened over the HUD."),
	Drop UMETA(ToolTip = "New ones are not shown at all (they go to the history when their type keeps history); those on screen carry on. A boss intro that must not be talked over.")
};

/** How an action button looks on the built-in card. */
UENUM(BlueprintType)
enum class ENotificationActionStyle : uint8
{
	Primary UMETA(ToolTip = "The main choice: filled in the accent color."),
	Secondary UMETA(ToolTip = "A quiet choice: outlined."),
	Destructive UMETA(ToolTip = "A choice that removes or refuses: filled in the error color.")
};

/** An image on a notification: a texture or material, or an SVG drawn at its on-screen size. */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationIcon
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon")
	EWidgetImageSource ImageSource = EWidgetImageSource::Texture;

	/** A texture or a material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon", meta = (AllowedClasses = "/Script/Engine.Texture2D,/Script/Engine.MaterialInterface", EditCondition = "ImageSource == EWidgetImageSource::Texture", EditConditionHides))
	TObjectPtr<UObject> Image = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon", meta = (DisplayName = "SVG", EditCondition = "ImageSource == EWidgetImageSource::SVG", EditConditionHides))
	TObjectPtr<UWidgetSVG> SVG = nullptr;

	/** Multiplies the image. White keeps its colors; with an SVG drawn in white, this is its color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon")
	FLinearColor Tint = FLinearColor::White;

	bool HasImage() const { return ImageSource == EWidgetImageSource::SVG ? SVG != nullptr : Image != nullptr; }
};

/**
 * A choice on a notification: a button on the card, and optionally a key that takes it without the mouse (on the most
 * important notification on screen that has one - see the subsystem). "Accept" / "Decline" on an invite, "Equip" on loot.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationAction
{
	GENERATED_BODY()

	/** What On Notification Action reports. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	FName ActionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	ENotificationActionStyle Style = ENotificationActionStyle::Primary;

	/** The notification goes away when this is taken. Off: it stays (a "Details" that opens a screen and keeps the invite). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	bool bDismisses = true;

	/** A gamepad button that takes this action. None: no shortcut on gamepad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	FKey GamepadKey;

	/** A keyboard key that takes this action. None: no shortcut on keyboard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	FKey KeyboardKey;

	/**
	 * Seconds the key must be held to take the action (the button fills as it is held). 0: a press. Holding stops an action
	 * being taken by a press meant for the game - use it for keys the game also uses.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (ClampMin = "0", UIMax = "3", Units = "s"))
	float HoldTime = 0.f;
};

/** A named piece of text a notification's Title and Body can use as {Name}. */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationTextArg
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FName Name = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText Value;
};

/**
 * A notification to show: what gameplay code posts (Post Notification). Its Type says how it behaves (Project Settings >
 * Plugins > Notifications); everything else is its content. The Override toggles change one behavior for this one only.
 *
 * Title and Body are formatted: {Count} and {Value} are this notification's (growing as like ones merge in), and any Text
 * Args by name - "+{Value} XP", "{Player} invited you", "Wood". Localize them as any FText.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationRequest
{
	GENERATED_BODY()

	/** What it is: picks its settings (channel, priority, time on screen, stacking, sounds...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notification")
	FGameplayTag Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notification", meta = (MultiLine = true))
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notification", meta = (MultiLine = true))
	FText Body;

	/** Named text the Title and Body use as {Name}. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notification")
	TArray<FNotificationTextArg> TextArgs;

	/** No image: the type's Icon (and with none there, the built-in card's glyph for the severity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notification")
	FNotificationIcon Icon;

	/** How many (shown as "x3" from 2 up). Merging adds the new one's Count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notification", meta = (ClampMin = "0"))
	int32 Count = 1;

	/** A number it carries: XP, currency, damage. Merging adds the new one's Value. {Value} in the text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notification")
	float Value = 0.f;

	/** A progress bar on the card, 0 to 1 (a download, "3 of 5 collected"). Negative: none. Set Notification Progress moves it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notification", meta = (UIMin = "-1", UIMax = "1"))
	float Progress = -1.f;

	/**
	 * What makes two notifications "the same one" for stacking and Cooldown (an item's ID for a loot feed, a player's for their
	 * messages). None: for Merge / Replace types the type and the Title as written; otherwise the type and the whole text shown
	 * (title and body), so only an exact repeat counts as the same.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notification")
	FName StackKey = NAME_None;

	/** Buttons on it (and their keys). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actions")
	TArray<FNotificationAction> Actions;

	/** The action taken by itself when its time runs out (an invite declined). None: it just goes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actions")
	FName TimeoutActionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides", meta = (InlineEditConditionToggle))
	bool bOverrideChannel = false;

	/** Shows in this channel instead of its type's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides", meta = (EditCondition = "bOverrideChannel", Categories = "Notification.Channel"))
	FGameplayTag Channel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides", meta = (InlineEditConditionToggle))
	bool bOverridePriority = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides", meta = (EditCondition = "bOverridePriority"))
	ENotificationPriority Priority = ENotificationPriority::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides", meta = (InlineEditConditionToggle))
	bool bOverrideSeverity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides", meta = (EditCondition = "bOverrideSeverity"))
	ENotificationSeverity Severity = ENotificationSeverity::Info;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides", meta = (InlineEditConditionToggle))
	bool bOverrideDuration = false;

	/** Seconds on screen instead of its type's (still scaled by the player's Duration Scale). 0: until dismissed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overrides", meta = (EditCondition = "bOverrideDuration", ClampMin = "0", Units = "s"))
	float Duration = 5.f;

	/** Any data a custom entry widget needs (a player's rank, an item's rarity): Make Instanced Struct. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	FInstancedStruct Payload;

	/** An object it is about (the item picked up, the player who invited), for whoever handles its actions or clicks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Data")
	TObjectPtr<UObject> ContextObject = nullptr;
};

/** Names one notification, to update, dismiss or answer it later. Stays unique for the session. */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Notification")
	int32 Id = 0;

	FNotificationHandle() = default;
	explicit FNotificationHandle(int32 InId) : Id(InId) {}

	bool IsValid() const { return Id != 0; }
	bool operator==(const FNotificationHandle& Other) const { return Id == Other.Id; }
	bool operator!=(const FNotificationHandle& Other) const { return Id != Other.Id; }
	friend uint32 GetTypeHash(const FNotificationHandle& Handle) { return ::GetTypeHash(Handle.Id); }
};

/**
 * A notification as widgets see it: its text formatted, its settings resolved. Handed to entry widgets, to every event, and
 * kept in the history (which a save game can store: Get Notification History / Restore Notification History).
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	FNotificationHandle Handle;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	FGameplayTag Type;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	FGameplayTag Channel;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	ENotificationPriority Priority = ENotificationPriority::Normal;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	ENotificationSeverity Severity = ENotificationSeverity::Info;

	/** Formatted ({Count}, {Value} and the Text Args filled in). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	FText Title;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	FText Body;

	/** The request's image, else its type's. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	FNotificationIcon Icon;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	int32 Count = 1;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	float Value = 0.f;

	/** Negative: no progress bar. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	float Progress = -1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Notification")
	TArray<FNotificationAction> Actions;

	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	FInstancedStruct Payload;

	UPROPERTY(BlueprintReadOnly, Category = "Notification")
	TObjectPtr<UObject> ContextObject = nullptr;

	/** Seconds it stays on screen in all (after the player's Duration Scale). 0: until dismissed. */
	UPROPERTY(BlueprintReadOnly, Category = "Notification")
	float Duration = 0.f;

	/** The player may close it (its type's Allow Dismiss). */
	UPROPERTY(BlueprintReadOnly, Category = "Notification")
	bool bCanDismiss = true;

	/** When it was posted (UTC). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "Notification")
	FDateTime PostedAt;

	/** History: the player has seen it in the history (Mark Notifications Read), or it was shown on screen. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "History")
	bool bRead = false;

	/** History: why it went. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "History")
	ENotificationDismissReason DismissReason = ENotificationDismissReason::TimedOut;

	/** History: the action taken on it, if any. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "History")
	FName RespondedAction = NAME_None;

	bool HasProgress() const { return Progress >= 0.f; }
};

/**
 * How notifications of one type behave. Set per type tag in Project Settings > Plugins > Notifications; a type with no
 * settings of its own takes its nearest parent tag's, else the Default Type's.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationTypeSettings
{
	GENERATED_BODY()

	/** The name players see for it in a notification settings screen (Get Notification Types). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	FText DisplayName;

	/** Where it shows: a Notification Host showing this channel draws it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type", meta = (Categories = "Notification.Channel"))
	FGameplayTag Channel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	ENotificationPriority Priority = ENotificationPriority::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	ENotificationSeverity Severity = ENotificationSeverity::Info;

	/** Its image when the request brings none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	FNotificationIcon Icon;

	/** The widget it is drawn with, in any host. None: the host's Entry Widget Class, else the host's built-in card. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	TSoftClassPtr<UNotificationEntryWidget> EntryWidgetClass;

	/** Seconds on screen. 0: until dismissed (or answered). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0", UIMax = "30", Units = "s"))
	float Duration = 4.f;

	/**
	 * Adds the time it takes to read the Title and Body (Reading Speed in the settings), so a long message stays longer than a
	 * short one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (EditCondition = "Duration > 0"))
	bool bAddReadingTime = true;

	/** Seconds it may wait in the queue before it is no longer worth showing. 0: it waits as long as it takes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing", meta = (ClampMin = "0", UIMax = "60", Units = "s"))
	float MaxQueueTime = 0.f;

	/**
	 * Its time stops while the game is paused (it stays on screen, frozen). One posted while the game is paused (a pause menu's
	 * "Settings saved") shows and runs as usual. Off: its time carries on over a pause menu.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Timing")
	bool bPauseWhileGamePaused = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stacking")
	ENotificationStackPolicy StackPolicy = ENotificationStackPolicy::None;

	/**
	 * Merge / Replace / Ignore only into one updated within this many seconds. 0: any still on screen or waiting.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stacking", meta = (EditCondition = "StackPolicy != ENotificationStackPolicy::None", ClampMin = "0", UIMax = "30", Units = "s"))
	float StackWindow = 0.f;

	/** Merging or replacing restarts its time on screen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stacking", meta = (EditCondition = "StackPolicy == ENotificationStackPolicy::Merge || StackPolicy == ENotificationStackPolicy::Replace"))
	bool bRefreshOnStack = true;

	/**
	 * Drops one like an earlier one (same Stack Key) posted within this many seconds, even after the earlier one has gone - the
	 * "You can't do that" pressed ten times. 0: no cooldown. Merging ones are never dropped by it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stacking", meta = (ClampMin = "0", UIMax = "10", Units = "s"))
	float Cooldown = 0.f;

	/** When one of this type arrives, those of these types (and their child tags) go: "Double Kill" takes "Kill" away. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stacking", meta = (Categories = "Notification.Type"))
	FGameplayTagContainer CancelsTypes;

	/** Shows through cutscenes, Hold / Drop suppressions, Do Not Disturb and the player's muting. For what must be seen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior")
	bool bIgnoreSuppression = false;

	/** The player may close it (the card's close button, a swipe, the Dismiss key). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior")
	bool bAllowDismiss = true;

	/** Kept in the notification history (a notification center, a message log). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior")
	bool bKeepInHistory = false;

	/** Read out by the platform's screen reader when shown, if the player has screen reader announcements on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior")
	bool bAnnounce = true;

	/** Listed in Get Notification Types, for a settings screen where players mute types. Off for types that must not be muted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior")
	bool bPlayerCanMute = true;

	/** Played as it appears (not again when it comes back after a Hold). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sounds")
	FUISoundSlot ShownSound;

	/** Played when one merges into it or replaces it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sounds")
	FUISoundSlot StackedSound;

	/** Played when the player takes one of its actions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sounds")
	FUISoundSlot ActionSound;

	/** Played when the player closes it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sounds")
	FUISoundSlot DismissedSound;

	/** No sound of this type within this many seconds of the last (a loot feed of twenty pickups plays a few). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sounds", meta = (ClampMin = "0", UIMax = "1", Units = "s"))
	float MinTimeBetweenSounds = 0.08f;
};

/** How one channel shares out its space. Set per channel tag in Project Settings > Plugins > Notifications. */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationChannelSettings
{
	GENERATED_BODY()

	/** At most this many on screen at once (a host's own Max Visible wins). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel", meta = (ClampMin = "1", UIMax = "12"))
	int32 MaxVisible = 3;

	/** At most this many waiting; past it the least important (then the oldest) waiting one is dropped. 0: no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel", meta = (ClampMin = "0", UIMax = "100"))
	int32 MaxQueued = 20;

	/** Waiting ones show by priority, then oldest first. Off: strictly oldest first (Critical still goes first). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	bool bOrderByPriority = true;

	/** A waiting one of higher priority pushes the lowest one on screen off early (once it has shown Min Display Time). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	bool bAllowPreemption = true;

	/** A notification pushed off early goes back into the queue with the time it had left, instead of going. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel", meta = (EditCondition = "bAllowPreemption"))
	bool bRequeuePreempted = false;

	/**
	 * When full, the oldest one on screen leaves early (once it has shown Min Display Time) so a waiting one can come in - a
	 * feed that keeps moving. Off: waiting ones wait their turn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	bool bMakeRoom = false;

	/** Seconds one must have been on screen before it can be pushed off. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel", meta = (ClampMin = "0", UIMax = "5", Units = "s"))
	float MinDisplayTime = 1.f;

	/** Seconds between one leaving and the next coming in - for one-at-a-time banners, a beat between them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel", meta = (ClampMin = "0", UIMax = "2", Units = "s"))
	float Gap = 0.f;

	/** With no host showing the channel (the HUD not made yet, hidden, a loading screen), notifications wait for one. Off: they are dropped (Expired). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	bool bWaitForHost = true;
};

/**
 * One player's notification choices - what a game's settings screen edits (Set Notification Preferences). Not saved by the
 * plugin: store them with the game's other settings and set them on start.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationUserPreferences
{
	GENERATED_BODY()

	/** Nothing shows except types with Ignore Suppression; the rest go straight to the history (if kept). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications")
	bool bDoNotDisturb = false;

	/** Types (and their child tags) the player doesn't want to see. They go to the history (if kept). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications", meta = (Categories = "Notification.Type"))
	FGameplayTagContainer MutedTypes;

	/**
	 * Multiplies how long every notification stays (accessibility: up to 10x, as the Xbox Accessibility Guidelines ask for
	 * on-screen text).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications", meta = (ClampMin = "0.25", ClampMax = "10", UIMin = "0.5", UIMax = "10"))
	float DurationScale = 1.f;

	/** Nothing goes by itself: every notification stays until closed (accessibility; the other end of Duration Scale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications")
	bool bKeepUntilDismissed = false;

	/** Notifications are read out by the platform's screen reader (types with Announce on). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications")
	bool bScreenReaderAnnouncements = false;

	/** Hosts only fade notifications in and out - no sliding, popping or bouncing (accessibility). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications")
	bool bReduceMotion = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Notifications")
	bool bPlaySounds = true;
};

/** How a key is shown in an action's hint: your button prompt image, or text. */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationKeyGlyph
{
	GENERATED_BODY()

	/** Text shown for the key ("A", "LB", "E"). Empty: the key's short name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Key")
	FText Label;

	/** An image for the key (the platform's button prompt). Drawn instead of the label when set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Key")
	FNotificationIcon Image;
};

/** A notification type as a settings screen lists it (Get Notification Types). */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationTypeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Notification")
	FGameplayTag Type;

	UPROPERTY(BlueprintReadOnly, Category = "Notification")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Notification")
	bool bMuted = false;
};

/** Called once, when a notification ends (or is never shown): the action taken (None for none) and why it went. */
DECLARE_DELEGATE_ThreeParams(FOnNotificationFinishedNative, FNotificationHandle /*Handle*/, FName /*ActionId*/, ENotificationDismissReason /*Reason*/);

/** Called each time the player takes one of a notification's actions (one that doesn't dismiss may be taken again). */
DECLARE_DELEGATE_TwoParams(FOnNotificationActionNative, FNotificationHandle /*Handle*/, FName /*ActionId*/);

namespace NotificationUtils
{
	/** Request's Title / Body with {Count}, {Value} and its Text Args filled in. */
	NAMETBD_API FText FormatText(const FText& Pattern, const FNotificationRequest& Request);

	/** Words in Text (for reading time): runs of letters or digits; CJK characters count one word each per two. */
	NAMETBD_API int32 CountWords(const FString& Text);

	/** The color the built-in look gives a severity (the card's defaults). */
	NAMETBD_API FLinearColor GetDefaultSeverityColor(ENotificationSeverity Severity);
}
