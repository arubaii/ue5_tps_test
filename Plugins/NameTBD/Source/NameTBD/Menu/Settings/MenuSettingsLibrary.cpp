// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuSettingsLibrary.h"

#include "Engine/LocalPlayer.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "MenuSettingTargets.h"
#include "MenuSettingsSubsystem.h"
#include "NameTBD/Menu/Input/MenuInputSubsystem.h"
#include "NameTBD/Menu/MenuPlayerUtils.h"

// =====================================================================================================
//  Library
// =====================================================================================================

UMenuSettingsSubsystem* UMenuSettingsLibrary::GetMenuSettings(const UObject* WorldContextObject, APlayerController* Player)
{
	return UMenuSettingsSubsystem::Get(WorldContextObject, Player);
}

FString UMenuSettingsLibrary::GetSettingValue(const UObject* WorldContextObject, FName SettingId)
{
	const UMenuSettingsSubsystem* Settings = UMenuSettingsSubsystem::Get(WorldContextObject);
	return Settings ? Settings->GetSettingValue(SettingId) : FString();
}

bool UMenuSettingsLibrary::GetSettingBool(const UObject* WorldContextObject, FName SettingId, bool bDefault)
{
	const UMenuSettingsSubsystem* Settings = UMenuSettingsSubsystem::Get(WorldContextObject);
	return Settings ? Settings->GetSettingBool(SettingId, bDefault) : bDefault;
}

float UMenuSettingsLibrary::GetSettingNumber(const UObject* WorldContextObject, FName SettingId, float DefaultValue)
{
	const UMenuSettingsSubsystem* Settings = UMenuSettingsSubsystem::Get(WorldContextObject);
	return Settings ? Settings->GetSettingNumber(SettingId, DefaultValue) : DefaultValue;
}

bool UMenuSettingsLibrary::IsReduceMotionEnabled()
{
	return MenuAccessibility::IsReduceMotionEnabled();
}

bool UMenuSettingsLibrary::IsHighContrastEnabled()
{
	return MenuAccessibility::IsHighContrastEnabled();
}

bool UMenuSettingsLibrary::IsMenuNarrationEnabled()
{
	return MenuAccessibility::IsMenuNarrationEnabled();
}

void UMenuSettingsLibrary::NarrateMenuText(const FText& Text)
{
	if (MenuAccessibility::IsMenuNarrationEnabled() && !Text.IsEmpty())
	{
		UGameplayStatics::AnnounceAccessibleString(Text.ToString());
	}
}

EMenuInputDevice UMenuSettingsLibrary::GetMenuInputDevice(const UObject* WorldContextObject, APlayerController* Player)
{
	const UMenuInputSubsystem* Input = UMenuInputSubsystem::Get(WorldContextObject, Player);
	return Input ? Input->GetCurrentDevice() : EMenuInputDevice::KeyboardMouse;
}

UMenuInputSubsystem* UMenuSettingsLibrary::GetMenuInput(const UObject* WorldContextObject, APlayerController* Player)
{
	return UMenuInputSubsystem::Get(WorldContextObject, Player);
}

// =====================================================================================================
//  Setting Binder
// =====================================================================================================

UMenuSettingBinderComponent::UMenuSettingBinderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UMenuSettingsSubsystem* UMenuSettingBinderComponent::FindSettings() const
{
	const AActor* OwnerActor = GetOwner();
	const APlayerController* Player = Cast<APlayerController>(OwnerActor);
	if (!Player)
	{
		if (const APawn* Pawn = Cast<APawn>(OwnerActor))
		{
			Player = Cast<APlayerController>(Pawn->GetController());
		}
	}
	if (!Player && OwnerActor)
	{
		Player = Cast<APlayerController>(OwnerActor->GetOwner());
	}
	if (Player && !Player->IsLocalController())
	{
		// Another player's pawn on this machine: settings are this machine's player's to apply to their own view only.
		return nullptr;
	}
	return UMenuSettingsSubsystem::Get(this, Player);
}

void UMenuSettingBinderComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UMenuSettingsSubsystem* Settings = FindSettings())
	{
		BoundSettings = Settings;
		ChangedHandle = Settings->OnSettingChangedNative().AddUObject(this, &UMenuSettingBinderComponent::HandleSettingChanged);
		ApplyAllBindings();
	}
}

void UMenuSettingBinderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMenuSettingsSubsystem* Settings = BoundSettings.Get())
	{
		Settings->OnSettingChangedNative().Remove(ChangedHandle);
	}
	BoundSettings.Reset();
	Super::EndPlay(EndPlayReason);
}

void UMenuSettingBinderComponent::ApplyAllBindings()
{
	const UMenuSettingsSubsystem* Settings = BoundSettings.Get();
	if (!Settings)
	{
		return;
	}
	for (const FMenuSettingBinding& Binding : Bindings)
	{
		const FString Value = Settings->GetSettingValue(Binding.SettingId);
		if (!Value.IsEmpty() && ApplyBinding(Binding, Value))
		{
			OnBoundSettingChanged.Broadcast(Binding.SettingId, Value);
		}
	}
}

void UMenuSettingBinderComponent::HandleSettingChanged(FName SettingId, const FString& Value)
{
	for (const FMenuSettingBinding& Binding : Bindings)
	{
		if (Binding.SettingId == SettingId && ApplyBinding(Binding, Value))
		{
			OnBoundSettingChanged.Broadcast(SettingId, Value);
		}
	}
}

bool UMenuSettingBinderComponent::ApplyBinding(const FMenuSettingBinding& Binding, const FString& Value)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || Binding.PropertyName.IsNone())
	{
		return false;
	}
	UObject* Target = OwnerActor;
	if (Binding.ComponentClass)
	{
		Target = OwnerActor->FindComponentByClass(Binding.ComponentClass);
	}
	if (!Target)
	{
		return false;
	}
	FProperty* Property = Target->GetClass()->FindPropertyByName(Binding.PropertyName);
	if (!Property)
	{
		UE_LOG(LogNameTBDSettings, Warning, TEXT("Setting Binder on %s: %s has no property %s."), *OwnerActor->GetName(), *Target->GetClass()->GetName(), *Binding.PropertyName.ToString());
		return false;
	}
	void* Address = Property->ContainerPtrToValuePtr<void>(Target);
	if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property); Numeric && !Numeric->IsEnum())
	{
		const double Number = MenuSettingValues::ToNumber(Value) * Binding.Scale;
		if (Numeric->IsFloatingPoint())
		{
			Numeric->SetFloatingPointPropertyValue(Address, Number);
		}
		else
		{
			Numeric->SetIntPropertyValue(Address, static_cast<int64>(FMath::RoundToDouble(Number)));
		}
	}
	else if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
	{
		Bool->SetPropertyValue(Address, MenuSettingValues::ToBool(Value));
	}
	else if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		TextProperty->SetPropertyValue(Address, FText::FromString(Value));
	}
	else
	{
		// Enums by name, names, strings, structs in text form.
		if (!Property->ImportText_Direct(*Value, Address, Target, PPF_None))
		{
			return false;
		}
	}

	if (!Binding.ThenCall.IsNone())
	{
		if (UFunction* Function = Target->FindFunction(Binding.ThenCall); Function && Function->NumParms == 0)
		{
			Target->ProcessEvent(Function, nullptr);
		}
	}
	return true;
}
