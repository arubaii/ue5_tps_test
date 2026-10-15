// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuSetting.h"

#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MenuProjectSettings.h"
#include "MenuSettingTargets.h"
#include "MenuSettingsSubsystem.h"
#include "Misc/DataValidation.h"
#include "NameTBD/Menu/Input/MenuInputTypes.h"

#define LOCTEXT_NAMESPACE "MenuSetting"

// =====================================================================================================
//  Context
// =====================================================================================================

UWorld* FMenuSettingContext::GetWorld() const
{
	return LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
}

APlayerController* FMenuSettingContext::GetPlayerController() const
{
	return LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr;
}

FString FMenuSettingContext::GetValue(FName SettingId) const
{
	return Subsystem ? Subsystem->GetSettingValue(SettingId) : FString();
}

// =====================================================================================================
//  Setting
// =====================================================================================================

FString UMenuSetting::GetDefaultValue(const FMenuSettingContext& Context) const
{
	if (const FString* Override = UMenuProjectSettings::Get().FindDefaultOverride(Id))
	{
		return Sanitize(*Override, Context);
	}
	FString FromTarget;
	if (Target && Target->GetDefault(*this, Context, FromTarget))
	{
		return Sanitize(FromTarget, Context);
	}
	return Sanitize(GetOwnDefaultValue(), Context);
}

bool UMenuSetting::IsAvailable(const FMenuSettingContext& Context, FText& OutReason) const
{
	if (!MenuSettingValues::IsCurrentPlatformIn(Platforms))
	{
		OutReason = LOCTEXT("NotOnPlatform", "Not available on this platform.");
		return false;
	}
	if (Target && !Target->IsAvailable(*this, Context, OutReason))
	{
		if (OutReason.IsEmpty())
		{
			OutReason = LOCTEXT("NotAvailable", "Not available on this system.");
		}
		return false;
	}
	return true;
}

FText UMenuSetting::GetLabel() const
{
	return DisplayName.IsEmpty() ? FText::FromName(Id) : DisplayName;
}

bool UMenuSetting::MatchesSearch(const FString& Text) const
{
	if (Text.IsEmpty())
	{
		return true;
	}
	if (GetLabel().ToString().Contains(Text) || Description.ToString().Contains(Text) || Id.ToString().Contains(Text))
	{
		return true;
	}
	for (const FString& Keyword : Keywords)
	{
		if (Keyword.Contains(Text))
		{
			return true;
		}
	}
	return false;
}

#if WITH_EDITOR
EDataValidationResult UMenuSetting::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Id.IsNone() && GetKind() != EMenuSettingKind::KeyBinding)
	{
		Context.AddError(FText::Format(LOCTEXT("NoId", "A {0} has no Id: its value can't be saved or found."), GetClass()->GetDisplayNameText()));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

// =====================================================================================================
//  Bool
// =====================================================================================================

FString UMenuSettingBool::GetOwnDefaultValue() const
{
	return MenuSettingValues::FromBool(bDefaultValue);
}

FString UMenuSettingBool::Sanitize(const FString& Value, const FMenuSettingContext& Context) const
{
	return MenuSettingValues::FromBool(MenuSettingValues::ToBool(Value));
}

FText UMenuSettingBool::FormatValue(const FString& Value, const FMenuSettingContext& Context) const
{
	if (MenuSettingValues::ToBool(Value))
	{
		return OnText.IsEmpty() ? LOCTEXT("On", "On") : OnText;
	}
	return OffText.IsEmpty() ? LOCTEXT("Off", "Off") : OffText;
}

FString UMenuSettingBool::StepValue(const FString& Value, int32 Direction, const FMenuSettingContext& Context) const
{
	return MenuSettingValues::FromBool(!MenuSettingValues::ToBool(Value));
}

// =====================================================================================================
//  Number
// =====================================================================================================

FString UMenuSettingNumber::GetOwnDefaultValue() const
{
	return MenuSettingValues::FromNumber(DefaultValue);
}

FString UMenuSettingNumber::Sanitize(const FString& Value, const FMenuSettingContext& Context) const
{
	const double Low = FMath::Min(MinValue, MaxValue);
	const double High = FMath::Max(MinValue, MaxValue);
	double Number = MenuSettingValues::ToNumber(Value, DefaultValue);
	if (Step > 0.f)
	{
		Number = Low + FMath::RoundToDouble((Number - Low) / Step) * Step;
	}
	return MenuSettingValues::FromNumber(FMath::Clamp(Number, Low, High));
}

FText UMenuSettingNumber::FormatValue(const FString& Value, const FMenuSettingContext& Context) const
{
	const double Number = MenuSettingValues::ToNumber(Value, DefaultValue);
	if (!MinText.IsEmpty() && FMath::IsNearlyEqual(Number, static_cast<double>(MinValue), 1e-4))
	{
		return MinText;
	}
	if (!MaxText.IsEmpty() && FMath::IsNearlyEqual(Number, static_cast<double>(MaxValue), 1e-4))
	{
		return MaxText;
	}

	FNumberFormattingOptions Options;
	Options.MinimumFractionalDigits = Decimals;
	Options.MaximumFractionalDigits = Decimals;
	Options.UseGrouping = false;

	switch (Format)
	{
	case EMenuNumberFormat::Percent:
		return FText::Format(LOCTEXT("PercentFormat", "{0}%"), FText::AsNumber(Number * 100.0, &Options));
	case EMenuNumberFormat::PercentValue:
		return FText::Format(LOCTEXT("PercentFormat", "{0}%"), FText::AsNumber(Number, &Options));
	case EMenuNumberFormat::Multiplier:
	{
		FNumberFormattingOptions MultiplierOptions = Options;
		MultiplierOptions.MinimumFractionalDigits = FMath::Min(Decimals, 1);
		MultiplierOptions.MaximumFractionalDigits = FMath::Max(Decimals, 2);
		return FText::Format(LOCTEXT("MultiplierFormat", "{0}x"), FText::AsNumber(Number, &MultiplierOptions));
	}
	default:
		break;
	}
	if (Units.IsEmpty())
	{
		return FText::AsNumber(Number, &Options);
	}
	return FText::Format(LOCTEXT("UnitsFormat", "{0}{1}"), FText::AsNumber(Number, &Options), Units);
}

FString UMenuSettingNumber::StepValue(const FString& Value, int32 Direction, const FMenuSettingContext& Context) const
{
	const double Increment = Step > 0.f ? Step : FMath::Abs(MaxValue - MinValue) / 20.0;
	const double Number = MenuSettingValues::ToNumber(Value, DefaultValue) + Increment * FMath::Sign(Direction);
	return Sanitize(MenuSettingValues::FromNumber(Number), Context);
}

float UMenuSettingNumber::ToFraction(const FString& Value) const
{
	const double Range = MaxValue - MinValue;
	if (FMath::IsNearlyZero(Range))
	{
		return 0.f;
	}
	return FMath::Clamp(static_cast<float>((MenuSettingValues::ToNumber(Value, DefaultValue) - MinValue) / Range), 0.f, 1.f);
}

FString UMenuSettingNumber::FromFraction(float Fraction) const
{
	return Sanitize(MenuSettingValues::FromNumber(MinValue + (MaxValue - MinValue) * FMath::Clamp(Fraction, 0.f, 1.f)), FMenuSettingContext());
}

// =====================================================================================================
//  Choice
// =====================================================================================================

FString UMenuSettingChoice::GetOwnDefaultValue() const
{
	if (!DefaultValue.IsEmpty())
	{
		return DefaultValue;
	}
	return Options.Num() > 0 ? Options[0].Value : FString();
}

void UMenuSettingChoice::GetOptions(const FMenuSettingContext& Context, TArray<FMenuSettingOption>& OutOptions, const FString& Current) const
{
	OutOptions.Reset();
	TArray<FMenuSettingOption> Source;
	if (!Target || !Target->GetOptions(*this, Context, Source))
	{
		Source = Options;
	}
	for (FMenuSettingOption& Option : Source)
	{
		if (!Option.bHiddenUnlessCurrent || Option.Value.Equals(Current, ESearchCase::IgnoreCase))
		{
			OutOptions.Add(MoveTemp(Option));
		}
	}
}

int32 UMenuSettingChoice::FindOption(const TArray<FMenuSettingOption>& InOptions, const FString& Value)
{
	for (int32 Index = 0; Index < InOptions.Num(); ++Index)
	{
		if (InOptions[Index].Value.Equals(Value, ESearchCase::IgnoreCase))
		{
			return Index;
		}
	}
	// "2" and "2.0" are the same option.
	if (MenuSettingValues::IsNumber(Value))
	{
		const double Number = MenuSettingValues::ToNumber(Value);
		for (int32 Index = 0; Index < InOptions.Num(); ++Index)
		{
			if (MenuSettingValues::IsNumber(InOptions[Index].Value) && FMath::IsNearlyEqual(MenuSettingValues::ToNumber(InOptions[Index].Value), Number, 1e-4))
			{
				return Index;
			}
		}
	}
	return INDEX_NONE;
}

FString UMenuSettingChoice::Sanitize(const FString& Value, const FMenuSettingContext& Context) const
{
	TArray<FMenuSettingOption> Current;
	GetOptions(Context, Current, Value);
	if (Current.Num() == 0)
	{
		return Value;
	}
	const int32 Index = FindOption(Current, Value);
	if (Index != INDEX_NONE)
	{
		return Current[Index].Value;
	}
	// Not an option (any more): the target's default if it is one, else the setting's, else the first.
	FString FromTarget;
	if (Target && Target->GetDefault(*this, Context, FromTarget))
	{
		const int32 TargetIndex = FindOption(Current, FromTarget);
		if (TargetIndex != INDEX_NONE)
		{
			return Current[TargetIndex].Value;
		}
	}
	const int32 DefaultIndex = FindOption(Current, GetOwnDefaultValue());
	return Current[DefaultIndex != INDEX_NONE ? DefaultIndex : 0].Value;
}

FText UMenuSettingChoice::FormatValue(const FString& Value, const FMenuSettingContext& Context) const
{
	TArray<FMenuSettingOption> Current;
	GetOptions(Context, Current, Value);
	const int32 Index = FindOption(Current, Value);
	return Index != INDEX_NONE ? Current[Index].GetLabel() : FText::FromString(Value);
}

FString UMenuSettingChoice::StepValue(const FString& Value, int32 Direction, const FMenuSettingContext& Context) const
{
	TArray<FMenuSettingOption> Current;
	GetOptions(Context, Current);
	if (Current.Num() == 0)
	{
		return Value;
	}
	int32 Index = FindOption(Current, Value);
	if (Index == INDEX_NONE)
	{
		// A hidden value (Custom): left goes to the last, right to the first.
		return Current[Direction < 0 ? Current.Num() - 1 : 0].Value;
	}
	Index += FMath::Sign(Direction);
	if (bWrap)
	{
		Index = (Index + Current.Num()) % Current.Num();
	}
	return Current[FMath::Clamp(Index, 0, Current.Num() - 1)].Value;
}

// =====================================================================================================
//  Key binding, action, info
// =====================================================================================================

FText UMenuSettingKeyBinding::FormatValue(const FString& Value, const FMenuSettingContext& Context) const
{
	if (!Context.Subsystem)
	{
		return FText::GetEmpty();
	}
	TArray<FText> Keys;
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		const FKey Key = Context.Subsystem->GetBoundKey(MappingName, Device, SlotIndex);
		if (Key.IsValid())
		{
			Keys.Add(Key.GetDisplayName(false));
		}
	}
	return Keys.Num() > 0 ? FText::Join(LOCTEXT("KeySeparator", ", "), Keys) : LOCTEXT("Unbound", "Unbound");
}

FText UMenuSettingAction::FormatValue(const FString& Value, const FMenuSettingContext& Context) const
{
	return ButtonText.IsEmpty() ? GetLabel() : ButtonText;
}

FText UMenuSettingInfo::FormatValue(const FString& Value, const FMenuSettingContext& Context) const
{
	FString Read;
	if (Target && Target->Read(*this, Context, Read) && !Read.IsEmpty())
	{
		return FText::FromString(Read);
	}
	return Text;
}

#undef LOCTEXT_NAMESPACE
