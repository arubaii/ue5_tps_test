// Copyright AR GameSystems 2026. All rights reserved.

#include "NotificationTypes.h"

DEFINE_LOG_CATEGORY(LogNameTBDNotifications);

namespace NotificationTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Toast, "Notification.Channel.Toast", "Cards in a corner, a few at a time: messages, achievements, invites.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Banner, "Notification.Channel.Banner", "One big line across the screen at a time: objective updated, area discovered, level up.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Feed, "Notification.Channel.Feed", "A running list of short lines: loot, XP, eliminations.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Hint, "Notification.Channel.Hint", "One tip at a time near the bottom: tutorial prompts, controls.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_Info, "Notification.Type.Info", "Neutral information.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_Success, "Notification.Type.Success", "Something worked: saved, crafted, joined.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_Warning, "Notification.Type.Warning", "Attention: low ammo, inventory nearly full.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_Error, "Notification.Type.Error", "Something failed: can't do that, purchase failed.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_Reward, "Notification.Type.Reward", "Numbers gained that add up: XP, currency.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_Loot, "Notification.Type.Loot", "Items picked up, merged by Stack Key into 'Wood x3'.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_Elimination, "Notification.Type.Elimination", "A kill feed line.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_Objective, "Notification.Type.Objective", "Quest / objective updates, shown as a banner.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_Achievement, "Notification.Type.Achievement", "Achievements, trophies, unlocks.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_Social, "Notification.Type.Social", "Invites, friend requests, messages - usually with actions.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_System, "Notification.Type.System", "Connection lost, controller disconnected: Critical, until resolved.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Type_Hint, "Notification.Type.Hint", "Tutorial tips and control prompts.");
}

namespace NotificationUtils
{
	FText FormatText(const FText& Pattern, const FNotificationRequest& Request)
	{
		if (Pattern.IsEmpty())
		{
			return Pattern;
		}

		// Only a pattern with an argument in it is formatted: plain text stays the same FText (and keeps its history).
		const FString& Source = Pattern.ToString();
		if (!Source.Contains(TEXT("{")))
		{
			return Pattern;
		}

		FFormatNamedArguments Args;
		Args.Add(TEXT("Count"), FText::AsNumber(Request.Count));
		FNumberFormattingOptions ValueFormat;
		ValueFormat.MaximumFractionalDigits = 2;
		Args.Add(TEXT("Value"), FText::AsNumber(Request.Value, &ValueFormat));
		for (const FNotificationTextArg& Arg : Request.TextArgs)
		{
			if (!Arg.Name.IsNone())
			{
				Args.Add(Arg.Name.ToString(), Arg.Value);
			}
		}
		return FText::Format(FTextFormat(Pattern), Args);
	}

	int32 CountWords(const FString& Text)
	{
		int32 Words = 0;
		int32 WideCharacters = 0;
		bool bInWord = false;
		for (const TCHAR Character : Text)
		{
			const uint32 Code = static_cast<uint32>(Character);
			const bool bWide = (Code >= 0x3040 && Code <= 0x30FF) || (Code >= 0x3400 && Code <= 0x9FFF) || (Code >= 0xAC00 && Code <= 0xD7AF);
			if (bWide)
			{
				// Japanese, Chinese and Korean run without spaces: about two characters read as one word.
				++WideCharacters;
				bInWord = false;
				continue;
			}
			const bool bWordCharacter = FChar::IsAlnum(Character) || Character == TEXT('\'');
			if (bWordCharacter && !bInWord)
			{
				++Words;
			}
			bInWord = bWordCharacter;
		}
		return Words + (WideCharacters + 1) / 2;
	}

	FLinearColor GetDefaultSeverityColor(ENotificationSeverity Severity)
	{
		switch (Severity)
		{
		case ENotificationSeverity::Success: return FLinearColor::FromSRGBColor(FColor(61, 214, 140));
		case ENotificationSeverity::Warning: return FLinearColor::FromSRGBColor(FColor(255, 159, 28));
		case ENotificationSeverity::Error: return FLinearColor::FromSRGBColor(FColor(255, 90, 95));
		case ENotificationSeverity::Reward: return FLinearColor::FromSRGBColor(FColor(255, 210, 74));
		case ENotificationSeverity::Social: return FLinearColor::FromSRGBColor(FColor(139, 124, 255));
		default: return FLinearColor::FromSRGBColor(FColor(76, 155, 255));
		}
	}
}
