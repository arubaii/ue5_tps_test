// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NotificationTypes.h"

#include "NotificationSettings.generated.h"

/**
 * Project Settings > Plugins > Notifications: how each notification type behaves and how each channel shares its space. The
 * defaults cover the usual game notifications out of the box (toasts, a banner, a loot / XP / kill feed, hints) - see
 * NotificationTypes.h for the tags.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Notifications"))
class NAMETBD_API UNotificationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UNotificationSettings();

	static const UNotificationSettings& Get() { return *GetDefault<UNotificationSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Type's settings: its own, else its nearest parent tag's, else Default Type. */
	const FNotificationTypeSettings& FindType(const FGameplayTag& Type) const;

	/** Channel's settings: its own, else its nearest parent tag's, else Default Channel Settings. */
	const FNotificationChannelSettings& FindChannel(const FGameplayTag& Channel) const;

	// -------------------------------------------------------------------------------------------------
	// Types
	// -------------------------------------------------------------------------------------------------

	/** Settings per type tag. Child tags take their parent's settings unless they have their own. */
	UPROPERTY(Config, EditAnywhere, Category = "Types", meta = (ForceInlineRow, Categories = "Notification.Type"))
	TMap<FGameplayTag, FNotificationTypeSettings> Types;

	/** For a notification whose type has no settings (or no type). */
	UPROPERTY(Config, EditAnywhere, Category = "Types")
	FNotificationTypeSettings DefaultType;

	// -------------------------------------------------------------------------------------------------
	// Channels
	// -------------------------------------------------------------------------------------------------

	UPROPERTY(Config, EditAnywhere, Category = "Channels", meta = (ForceInlineRow, Categories = "Notification.Channel"))
	TMap<FGameplayTag, FNotificationChannelSettings> Channels;

	/** For a channel with no settings. */
	UPROPERTY(Config, EditAnywhere, Category = "Channels")
	FNotificationChannelSettings DefaultChannelSettings;

	/** Where a notification shows when neither it nor its type names a channel. */
	UPROPERTY(Config, EditAnywhere, Category = "Channels", meta = (Categories = "Notification.Channel"))
	FGameplayTag DefaultChannel;

	// -------------------------------------------------------------------------------------------------
	// Hosts
	// -------------------------------------------------------------------------------------------------

	/**
	 * When a notification has nowhere to show - no Notification Host for its channel has been placed yet - one is put on the
	 * player's screen by itself (Toasts top right, Feed top left, Banner top center, Hints bottom center; any other channel as
	 * toasts), so notifications work with nothing set up. A host of your own for the channel replaces it as soon as it is made.
	 * Off: notifications wait for a host of yours (a warning is logged once per channel).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Hosts")
	bool bCreateDefaultHosts = true;

	/** Seconds a notification waits for a host of yours before a default one is made (a HUD made a moment after the first post). */
	UPROPERTY(Config, EditAnywhere, Category = "Hosts", meta = (EditCondition = "bCreateDefaultHosts", ClampMin = "0", UIMax = "5", Units = "s"))
	float DefaultHostDelay = 0.5f;

	// -------------------------------------------------------------------------------------------------
	// Timing
	// -------------------------------------------------------------------------------------------------

	/** Words a minute a player reads at, for types with Add Reading Time (subtitle guidance sits near 160-180). */
	UPROPERTY(Config, EditAnywhere, Category = "Timing", meta = (ClampMin = "60", UIMax = "400"))
	float ReadingWordsPerMinute = 180.f;

	/** No notification that goes by itself stays less than this, whatever its type says. */
	UPROPERTY(Config, EditAnywhere, Category = "Timing", meta = (ClampMin = "0.5", UIMax = "5", Units = "s"))
	float MinimumDuration = 2.f;

	/** A notification with actions that goes by itself stays at least this long, so there is time to answer it. */
	UPROPERTY(Config, EditAnywhere, Category = "Timing", meta = (ClampMin = "0", UIMax = "60", Units = "s"))
	float MinimumActionDuration = 10.f;

	/** Nothing new shows (and those on screen wait, hidden) while the player's controller is in cinematic mode. */
	UPROPERTY(Config, EditAnywhere, Category = "Timing")
	bool bHoldDuringCinematics = true;

	// -------------------------------------------------------------------------------------------------
	// History
	// -------------------------------------------------------------------------------------------------

	/** The most notifications the history keeps; the oldest go first. 0: no history. */
	UPROPERTY(Config, EditAnywhere, Category = "History", meta = (ClampMin = "0", UIMax = "500"))
	int32 MaxHistory = 100;

	// -------------------------------------------------------------------------------------------------
	// Input
	// -------------------------------------------------------------------------------------------------

	/** Closes the notification that holds the action shortcuts (the most important one with actions), on gamepad. */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	FKey DismissGamepadKey;

	/** The same on keyboard. */
	UPROPERTY(Config, EditAnywhere, Category = "Input")
	FKey DismissKeyboardKey;

	/** How keys are shown in action hints: your platform's button prompts. Keys not here show their short name. */
	UPROPERTY(Config, EditAnywhere, Category = "Input", meta = (ForceInlineRow))
	TMap<FKey, FNotificationKeyGlyph> KeyGlyphs;

	// -------------------------------------------------------------------------------------------------
	// Player
	// -------------------------------------------------------------------------------------------------

	/** What each player starts with (Set Notification Preferences changes them at run time). */
	UPROPERTY(Config, EditAnywhere, Category = "Player")
	FNotificationUserPreferences DefaultPreferences;
};
