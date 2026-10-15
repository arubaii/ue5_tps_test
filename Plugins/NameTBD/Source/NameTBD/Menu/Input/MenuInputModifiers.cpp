// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuInputModifiers.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "NameTBD/Menu/Settings/MenuSettingsSubsystem.h"

namespace MenuInputModifiersLocal
{
	const UMenuSettingsSubsystem* FindSettings(const UEnhancedPlayerInput* PlayerInput)
	{
		const APlayerController* Controller = PlayerInput ? PlayerInput->GetOuterAPlayerController() : nullptr;
		const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
		return LocalPlayer ? LocalPlayer->GetSubsystem<UMenuSettingsSubsystem>() : nullptr;
	}
}

FInputActionValue UInputModifierScaleBySetting::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	if (CurrentValue.GetValueType() == EInputActionValueType::Boolean)
	{
		return CurrentValue;
	}
	const UMenuSettingsSubsystem* Settings = MenuInputModifiersLocal::FindSettings(PlayerInput);
	FVector Scale = BaseScale;
	if (Settings)
	{
		const float ScaleX = ScaleSetting.IsNone() ? 1.f : Settings->GetSettingNumber(ScaleSetting, 1.f);
		const float ScaleY = ScaleSettingY.IsNone() ? ScaleX : Settings->GetSettingNumber(ScaleSettingY, 1.f);
		Scale.X *= ScaleX;
		Scale.Y *= ScaleY;
		if (!InvertXSetting.IsNone() && Settings->GetSettingBool(InvertXSetting))
		{
			Scale.X = -Scale.X;
		}
		if (!InvertYSetting.IsNone() && Settings->GetSettingBool(InvertYSetting))
		{
			Scale.Y = -Scale.Y;
		}
	}
	return FInputActionValue(CurrentValue.GetValueType(), CurrentValue.Get<FVector>() * Scale);
}

FInputActionValue UInputModifierDeadZoneBySetting::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	if (CurrentValue.GetValueType() == EInputActionValueType::Boolean)
	{
		return CurrentValue;
	}
	const UMenuSettingsSubsystem* Settings = MenuInputModifiersLocal::FindSettings(PlayerInput);
	const float Lower = FMath::Clamp(Settings && !DeadZoneSetting.IsNone() ? Settings->GetSettingNumber(DeadZoneSetting, DefaultDeadZone) : DefaultDeadZone, 0.f, 0.99f);
	const float Upper = FMath::Max(UpperThreshold, Lower + 0.01f);

	auto Rescale = [Lower, Upper](float AxisValue)
	{
		return FMath::Min(1.f, FMath::Max(0.f, FMath::Abs(AxisValue) - Lower) / (Upper - Lower)) * FMath::Sign(AxisValue);
	};

	FVector Value = CurrentValue.Get<FVector>();
	switch (Type)
	{
	case EDeadZoneType::Axial:
		Value.X = Rescale(static_cast<float>(Value.X));
		Value.Y = Rescale(static_cast<float>(Value.Y));
		Value.Z = Rescale(static_cast<float>(Value.Z));
		break;
	case EDeadZoneType::Radial:
		if (Value.IsNearlyZero())
		{
			Value = FVector::ZeroVector;
		}
		else
		{
			Value = Value.GetSafeNormal() * Rescale(static_cast<float>(Value.Size()));
		}
		break;
	default:
		if (Value.Size() < Lower)
		{
			Value = FVector::ZeroVector;
		}
		break;
	}
	return FInputActionValue(CurrentValue.GetValueType(), Value);
}
