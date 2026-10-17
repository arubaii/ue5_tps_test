// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuInputSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "HAL/PlatformProperties.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "NameTBD/Menu/MenuPlayerUtils.h"
#include "NameTBD/Menu/Settings/MenuProjectSettings.h"
#include "NameTBD/Menu/Settings/MenuSettingsSubsystem.h"

/** Sees every input before widgets do, never takes it: only notes which device it came from. */
class FMenuInputProcessor : public IInputProcessor
{
public:
	explicit FMenuInputProcessor(UMenuInputSubsystem* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (UMenuInputSubsystem* Subsystem = Owner.Get())
		{
			Subsystem->NoteKey(InKeyEvent.GetKey(), static_cast<int32>(InKeyEvent.GetUserIndex()));
		}
		return false;
	}

	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
	{
		if (UMenuInputSubsystem* Subsystem = Owner.Get())
		{
			Subsystem->NoteAnalog(InAnalogInputEvent.GetKey(), InAnalogInputEvent.GetAnalogValue(), static_cast<int32>(InAnalogInputEvent.GetUserIndex()));
		}
		return false;
	}

	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		if (UMenuInputSubsystem* Subsystem = Owner.Get())
		{
			if (MouseEvent.IsTouchEvent())
			{
				Subsystem->NoteTouch(static_cast<int32>(MouseEvent.GetUserIndex()));
			}
			else
			{
				Subsystem->NoteMouseMove(MouseEvent.GetCursorDelta(), static_cast<int32>(MouseEvent.GetUserIndex()));
			}
		}
		return false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		if (UMenuInputSubsystem* Subsystem = Owner.Get())
		{
			if (MouseEvent.IsTouchEvent())
			{
				Subsystem->NoteTouch(static_cast<int32>(MouseEvent.GetUserIndex()));
			}
			else
			{
				Subsystem->NoteKey(MouseEvent.GetEffectingButton(), static_cast<int32>(MouseEvent.GetUserIndex()));
			}
		}
		return false;
	}

	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent) override
	{
		if (UMenuInputSubsystem* Subsystem = Owner.Get())
		{
			Subsystem->NoteKey(EKeys::MouseWheelAxis, static_cast<int32>(InWheelEvent.GetUserIndex()));
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("NameTBDMenuInput"); }

private:
	TWeakObjectPtr<UMenuInputSubsystem> Owner;
};

UMenuInputSubsystem* UMenuInputSubsystem::Get(const UObject* WorldContext, const APlayerController* Player)
{
	ULocalPlayer* LocalPlayer = MenuPlayerUtils::ResolveLocalPlayer(WorldContext, Player);
	return LocalPlayer ? LocalPlayer->GetSubsystem<UMenuInputSubsystem>() : nullptr;
}

bool UMenuInputSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && !IsRunningDedicatedServer();
}

void UMenuInputSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UMenuProjectSettings& Settings = UMenuProjectSettings::Get();
	DetectedFamily = Settings.DefaultGamepadFamily;
	DetectedCustomFamily = Settings.DefaultGamepadFamily == EMenuGamepadFamily::Custom ? Settings.DefaultCustomGamepadFamily : NAME_None;

	// Consoles: the platform's own pads.
	const FString Platform = FPlatformProperties::IniPlatformName();
	if (Platform.StartsWith(TEXT("PS")))
	{
		DetectedFamily = EMenuGamepadFamily::PlayStation;
	}
	else if (Platform.Contains(TEXT("Switch")))
	{
		DetectedFamily = EMenuGamepadFamily::Switch;
	}
	else if (Platform.StartsWith(TEXT("XSX")) || Platform.StartsWith(TEXT("XboxOne")))
	{
		DetectedFamily = EMenuGamepadFamily::Xbox;
	}
	if (!PLATFORM_DESKTOP)
	{
		CurrentDevice = (PLATFORM_ANDROID || PLATFORM_IOS) ? EMenuInputDevice::Touch : EMenuInputDevice::Gamepad;
	}

	for (const TSoftObjectPtr<UMenuKeyGlyphSet>& Entry : Settings.KeyGlyphSets)
	{
		if (UMenuKeyGlyphSet* Set = Entry.LoadSynchronous())
		{
			GlyphSets.Add(Set);
		}
	}

	if (FSlateApplication::IsInitialized())
	{
		Processor = MakeShared<FMenuInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(Processor);
	}
}

void UMenuInputSubsystem::Deinitialize()
{
	if (Processor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
	}
	Processor.Reset();
	Super::Deinitialize();
}

int32 UMenuInputSubsystem::GetSlateUserIndex() const
{
	if (const ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (const TSharedPtr<const FSlateUser> User = LocalPlayer->GetSlateUser())
		{
			return User->GetUserIndex();
		}
	}
	return 0;
}

void UMenuInputSubsystem::SwitchTo(EMenuInputDevice Device)
{
	MouseTravel = 0.f;
	if (Device == CurrentDevice)
	{
		return;
	}
	CurrentDevice = Device;
	OnInputDeviceChanged.Broadcast(Device);
	DeviceChangedNative.Broadcast(Device);
}

void UMenuInputSubsystem::SetCurrentDevice(EMenuInputDevice Device)
{
	SwitchTo(Device);
}

void UMenuInputSubsystem::NoteGamepadHardware()
{
	const FInputDeviceScope* Scope = FInputDeviceScope::GetCurrent();
	if (!Scope)
	{
		return;
	}
	const FString Hardware = Scope->HardwareDeviceIdentifier.IsEmpty() ? Scope->InputDeviceName.ToString() : Scope->HardwareDeviceIdentifier;
	if (Hardware.IsEmpty() || Hardware == GamepadHardware)
	{
		return;
	}
	GamepadHardware = Hardware;
	const FString Device = Scope->InputDeviceName.ToString();

	auto Detect = [this](EMenuGamepadFamily Family, FName CustomFamily)
	{
		const bool bChanged = DetectedFamily != Family || DetectedCustomFamily != CustomFamily;
		DetectedFamily = Family;
		DetectedCustomFamily = CustomFamily;
		if (bChanged && CurrentDevice == EMenuInputDevice::Gamepad)
		{
			// Same device kind, other prompts: listeners refresh their glyphs.
			OnInputDeviceChanged.Broadcast(CurrentDevice);
			DeviceChangedNative.Broadcast(CurrentDevice);
		}
	};

	// The project's own families first: an Elite pad also reports as an Xbox pad.
	const UMenuProjectSettings& Settings = UMenuProjectSettings::Get();
	for (const TPair<FString, FName>& Pair : Settings.CustomGamepadHardware)
	{
		if (!Pair.Value.IsNone() && (Hardware.Contains(Pair.Key) || Device.Contains(Pair.Key)))
		{
			Detect(EMenuGamepadFamily::Custom, Pair.Value);
			return;
		}
	}
	for (const TPair<FString, EMenuGamepadFamily>& Pair : Settings.GamepadHardware)
	{
		if (Hardware.Contains(Pair.Key) || Device.Contains(Pair.Key))
		{
			Detect(Pair.Value, NAME_None);
			return;
		}
	}
}

void UMenuInputSubsystem::NoteKey(const FKey& Key, int32 SlateUserIndex)
{
	if (SlateUserIndex != GetSlateUserIndex() || !Key.IsValid())
	{
		return;
	}
	const EMenuInputDevice Device = MenuInputUtils::GetKeyDevice(Key);
	if (Device == EMenuInputDevice::Gamepad)
	{
		NoteGamepadHardware();
	}
	SwitchTo(Device);
}

void UMenuInputSubsystem::NoteAnalog(const FKey& Key, float Value, int32 SlateUserIndex)
{
	if (SlateUserIndex != GetSlateUserIndex() || !Key.IsGamepadKey())
	{
		return;
	}
	if (FMath::Abs(Value) >= UMenuProjectSettings::Get().StickSwitchThreshold)
	{
		NoteGamepadHardware();
		SwitchTo(EMenuInputDevice::Gamepad);
	}
}

void UMenuInputSubsystem::NoteMouseMove(const FVector2D& Delta, int32 SlateUserIndex)
{
	if (SlateUserIndex != GetSlateUserIndex() || CurrentDevice == EMenuInputDevice::KeyboardMouse)
	{
		return;
	}
	MouseTravel += static_cast<float>(Delta.Size());
	if (MouseTravel >= UMenuProjectSettings::Get().MouseSwitchDistance)
	{
		SwitchTo(EMenuInputDevice::KeyboardMouse);
	}
}

void UMenuInputSubsystem::NoteTouch(int32 SlateUserIndex)
{
	if (SlateUserIndex == GetSlateUserIndex())
	{
		SwitchTo(EMenuInputDevice::Touch);
	}
}

EMenuGamepadFamily UMenuInputSubsystem::GetGamepadFamily() const
{
	if (!FamilyOverride.IsEmpty() && !FamilyOverride.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
	{
		const UEnum* Enum = StaticEnum<EMenuGamepadFamily>();
		const int64 Value = Enum ? Enum->GetValueByNameString(FamilyOverride) : INDEX_NONE;
		if (Value != INDEX_NONE && static_cast<EMenuGamepadFamily>(Value) != EMenuGamepadFamily::Custom)
		{
			return static_cast<EMenuGamepadFamily>(Value);
		}
		// A Custom family's name.
		if (FindCustomGlyphSet(FName(*FamilyOverride)))
		{
			return EMenuGamepadFamily::Custom;
		}
	}
	return DetectedFamily;
}

FName UMenuInputSubsystem::GetCustomGamepadFamily() const
{
	if (GetGamepadFamily() != EMenuGamepadFamily::Custom)
	{
		return NAME_None;
	}
	const FName OverrideName = FamilyOverride.IsEmpty() ? NAME_None : FName(*FamilyOverride);
	return (!OverrideName.IsNone() && FindCustomGlyphSet(OverrideName)) ? OverrideName : DetectedCustomFamily;
}

void UMenuInputSubsystem::SetGamepadFamilyOverride(const FString& Family)
{
	if (FamilyOverride != Family)
	{
		FamilyOverride = Family;
		OnInputDeviceChanged.Broadcast(CurrentDevice);
		DeviceChangedNative.Broadcast(CurrentDevice);
	}
}

FKey UMenuInputSubsystem::GetKeyForAction(const UInputAction* Action, EMenuInputDevice Device) const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input || !Action)
	{
		return EKeys::Invalid;
	}
	const bool bWantGamepad = Device == EMenuInputDevice::Gamepad;
	for (const FKey& Key : Input->QueryKeysMappedToAction(Action))
	{
		if (Key.IsGamepadKey() == bWantGamepad && !Key.IsTouch())
		{
			return Key;
		}
	}
	return EKeys::Invalid;
}

FKey UMenuInputSubsystem::GetCurrentKeyForAction(const UInputAction* Action) const
{
	return GetKeyForAction(Action, CurrentDevice);
}

FKey UMenuInputSubsystem::GetKeyForActionInContext(const UInputAction* Action, const UInputMappingContext* Context, EMenuInputDevice Device) const
{
	if (!Context)
	{
		return GetKeyForAction(Action, Device);
	}

	const bool bGamepad = Device == EMenuInputDevice::Gamepad;
	FName MappingName;
	const FKey ContextKey = MenuInputUtils::GetContextKey(Context, Action, bGamepad, &MappingName);

	// The player's rebinding of that mapping (Enhanced Input user settings), when it is player-mappable.
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UMenuSettingsSubsystem* MenuSettings = LocalPlayer ? LocalPlayer->GetSubsystem<UMenuSettingsSubsystem>() : nullptr;
	if (!MappingName.IsNone() && MenuSettings && MenuSettings->CanRebindKeys())
	{
		const FKey Bound = MenuSettings->GetBoundKey(MappingName, bGamepad ? EMenuKeyBindingDevice::Gamepad : EMenuKeyBindingDevice::KeyboardMouse, 0);
		if (Bound.IsValid())
		{
			return Bound;
		}
	}
	return ContextKey;
}

const UMenuKeyGlyphSet* UMenuInputSubsystem::FindCustomGlyphSet(FName CustomFamily) const
{
	if (CustomFamily.IsNone())
	{
		return nullptr;
	}
	for (const UMenuKeyGlyphSet* Set : GlyphSets)
	{
		if (Set && !Set->bForKeyboard && Set->Family == EMenuGamepadFamily::Custom && Set->CustomFamilyName == CustomFamily)
		{
			return Set;
		}
	}
	return nullptr;
}

const UMenuKeyGlyphSet* UMenuInputSubsystem::FindGlyphSet(bool bKeyboard, EMenuGamepadFamily Family, FName CustomFamily) const
{
	if (!bKeyboard && Family == EMenuGamepadFamily::Custom)
	{
		if (const UMenuKeyGlyphSet* Custom = FindCustomGlyphSet(CustomFamily))
		{
			return Custom;
		}
	}

	const UMenuKeyGlyphSet* Fallback = nullptr;
	for (const UMenuKeyGlyphSet* Set : GlyphSets)
	{
		if (!Set || Set->bForKeyboard != bKeyboard)
		{
			continue;
		}
		if (bKeyboard || (Set->Family == Family && Family != EMenuGamepadFamily::Custom))
		{
			return Set;
		}
		if (Set->Family == EMenuGamepadFamily::Generic || (!Fallback && Set->Family == EMenuGamepadFamily::Xbox))
		{
			Fallback = Set;
		}
	}
	return Fallback;
}

FMenuKeyGlyph UMenuInputSubsystem::GetKeyGlyph(FKey Key) const
{
	EMenuGamepadFamily Family = GetGamepadFamily();
	const bool bKeyboard = !Key.IsGamepadKey();
	FMenuKeyGlyph Out;
	Out.Key = Key;

	const UMenuKeyGlyphSet* Set = FindGlyphSet(bKeyboard, Family, GetCustomGamepadFamily());
	const FMenuKeyGlyph* Found = Set ? Set->Find(Key) : nullptr;

	// A Custom family's set holds only what differs (an Elite pad's paddles): everything else, and the key cap labels, come
	// from its Fallback Family.
	if (!bKeyboard && Set && Set->Family == EMenuGamepadFamily::Custom)
	{
		Family = Set->FallbackFamily == EMenuGamepadFamily::Custom ? EMenuGamepadFamily::Xbox : Set->FallbackFamily;
		if (!Found)
		{
			const UMenuKeyGlyphSet* FallbackSet = FindGlyphSet(false, Family);
			Found = FallbackSet ? FallbackSet->Find(Key) : nullptr;
		}
	}

	if (Found)
	{
		Out = *Found;
	}
	if (Out.Label.IsEmpty())
	{
		Out.Label = MenuInputUtils::GetKeyLabel(Key, Family);
	}
	return Out;
}
