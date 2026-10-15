// Copyright AR GameSystems 2026. All rights reserved.

#include "NotificationSettings.h"

#define LOCTEXT_NAMESPACE "NotificationSettings"

namespace NotificationSettingsPrivate
{
	FNotificationTypeSettings MakeType(const FText& Name, const FGameplayTag& Channel, ENotificationPriority Priority, ENotificationSeverity Severity, float Duration)
	{
		FNotificationTypeSettings Type;
		Type.DisplayName = Name;
		Type.Channel = Channel;
		Type.Priority = Priority;
		Type.Severity = Severity;
		Type.Duration = Duration;
		return Type;
	}

	FNotificationChannelSettings MakeChannel(int32 MaxVisible, int32 MaxQueued, bool bByPriority, bool bPreempt, bool bRequeue, bool bMakeRoom, float Gap)
	{
		FNotificationChannelSettings Channel;
		Channel.MaxVisible = MaxVisible;
		Channel.MaxQueued = MaxQueued;
		Channel.bOrderByPriority = bByPriority;
		Channel.bAllowPreemption = bPreempt;
		Channel.bRequeuePreempted = bRequeue;
		Channel.bMakeRoom = bMakeRoom;
		Channel.Gap = Gap;
		return Channel;
	}
}

UNotificationSettings::UNotificationSettings()
{
	using namespace NotificationSettingsPrivate;
	using namespace NotificationTags;

	DefaultChannel = Channel_Toast;
	DefaultType = MakeType(LOCTEXT("Other", "Other"), FGameplayTag(), ENotificationPriority::Normal, ENotificationSeverity::Info, 4.f);
	DefaultType.bKeepInHistory = true;

	// Toasts: a few cards, the important ones first and allowed to push others off.
	Channels.Add(Channel_Toast, MakeChannel(4, 20, true, true, false, false, 0.f));
	// Banner: one at a time with a beat between; a critical one cuts in and the cut one comes back after.
	Channels.Add(Channel_Banner, MakeChannel(1, 6, true, true, true, false, 0.25f));
	// Feed: oldest first and always moving - a full feed lets its oldest line go early.
	FNotificationChannelSettings Feed = MakeChannel(6, 30, false, false, false, true, 0.f);
	Feed.MinDisplayTime = 0.6f;
	Channels.Add(Channel_Feed, Feed);
	// Hints: one at a time, in order.
	Channels.Add(Channel_Hint, MakeChannel(1, 8, false, false, false, false, 0.3f));

	FNotificationTypeSettings Info = MakeType(LOCTEXT("Info", "Information"), Channel_Toast, ENotificationPriority::Normal, ENotificationSeverity::Info, 4.f);
	Info.StackPolicy = ENotificationStackPolicy::Ignore;
	Info.bKeepInHistory = true;
	Types.Add(Type_Info, Info);

	FNotificationTypeSettings Success = MakeType(LOCTEXT("Success", "Success"), Channel_Toast, ENotificationPriority::Normal, ENotificationSeverity::Success, 3.5f);
	Success.StackPolicy = ENotificationStackPolicy::Ignore;
	Types.Add(Type_Success, Success);

	FNotificationTypeSettings Warning = MakeType(LOCTEXT("Warning", "Warnings"), Channel_Toast, ENotificationPriority::High, ENotificationSeverity::Warning, 5.f);
	Warning.StackPolicy = ENotificationStackPolicy::Ignore;
	Warning.Cooldown = 1.5f;
	Warning.bPlayerCanMute = false;
	Types.Add(Type_Warning, Warning);

	FNotificationTypeSettings Error = MakeType(LOCTEXT("Error", "Errors"), Channel_Toast, ENotificationPriority::High, ENotificationSeverity::Error, 5.f);
	Error.StackPolicy = ENotificationStackPolicy::Ignore;
	Error.Cooldown = 1.5f;
	Error.bKeepInHistory = true;
	Error.bPlayerCanMute = false;
	Types.Add(Type_Error, Error);

	FNotificationTypeSettings Reward = MakeType(LOCTEXT("Reward", "XP and currency"), Channel_Feed, ENotificationPriority::Normal, ENotificationSeverity::Reward, 2.5f);
	Reward.StackPolicy = ENotificationStackPolicy::Merge;
	Reward.bAddReadingTime = false;
	Reward.bAnnounce = false;
	Types.Add(Type_Reward, Reward);

	FNotificationTypeSettings Loot = MakeType(LOCTEXT("Loot", "Items picked up"), Channel_Feed, ENotificationPriority::Normal, ENotificationSeverity::Reward, 3.f);
	Loot.StackPolicy = ENotificationStackPolicy::Merge;
	Loot.bAddReadingTime = false;
	Loot.bAnnounce = false;
	Types.Add(Type_Loot, Loot);

	FNotificationTypeSettings Elimination = MakeType(LOCTEXT("Elimination", "Eliminations"), Channel_Feed, ENotificationPriority::Normal, ENotificationSeverity::Info, 5.f);
	Elimination.bAddReadingTime = false;
	Elimination.MaxQueueTime = 4.f;
	Elimination.bAnnounce = false;
	Types.Add(Type_Elimination, Elimination);

	FNotificationTypeSettings Objective = MakeType(LOCTEXT("Objective", "Objectives"), Channel_Banner, ENotificationPriority::High, ENotificationSeverity::Info, 3.f);
	Objective.StackPolicy = ENotificationStackPolicy::Replace;
	Objective.bKeepInHistory = true;
	Objective.bPlayerCanMute = false;
	Types.Add(Type_Objective, Objective);

	FNotificationTypeSettings Achievement = MakeType(LOCTEXT("Achievement", "Achievements"), Channel_Toast, ENotificationPriority::High, ENotificationSeverity::Reward, 6.f);
	Achievement.bKeepInHistory = true;
	Types.Add(Type_Achievement, Achievement);

	FNotificationTypeSettings Social = MakeType(LOCTEXT("Social", "Invites and messages"), Channel_Toast, ENotificationPriority::Normal, ENotificationSeverity::Social, 20.f);
	Social.StackPolicy = ENotificationStackPolicy::Merge;
	Social.bKeepInHistory = true;
	Types.Add(Type_Social, Social);

	FNotificationTypeSettings System = MakeType(LOCTEXT("System", "System"), Channel_Toast, ENotificationPriority::Critical, ENotificationSeverity::Warning, 0.f);
	System.StackPolicy = ENotificationStackPolicy::Replace;
	System.bIgnoreSuppression = true;
	System.bPauseWhileGamePaused = false;
	System.bKeepInHistory = true;
	System.bPlayerCanMute = false;
	Types.Add(Type_System, System);

	FNotificationTypeSettings Hint = MakeType(LOCTEXT("Hint", "Tips"), Channel_Hint, ENotificationPriority::Normal, ENotificationSeverity::Info, 4.f);
	Hint.StackPolicy = ENotificationStackPolicy::Replace;
	Hint.MaxQueueTime = 15.f;
	Types.Add(Type_Hint, Hint);
}

const FNotificationTypeSettings& UNotificationSettings::FindType(const FGameplayTag& Type) const
{
	FGameplayTag Search = Type;
	while (Search.IsValid())
	{
		if (const FNotificationTypeSettings* Found = Types.Find(Search))
		{
			return *Found;
		}
		Search = Search.RequestDirectParent();
	}
	return DefaultType;
}

const FNotificationChannelSettings& UNotificationSettings::FindChannel(const FGameplayTag& Channel) const
{
	FGameplayTag Search = Channel;
	while (Search.IsValid())
	{
		if (const FNotificationChannelSettings* Found = Channels.Find(Search))
		{
			return *Found;
		}
		Search = Search.RequestDirectParent();
	}
	return DefaultChannelSettings;
}

#undef LOCTEXT_NAMESPACE
