// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "MenuInputTypes.h"

#include "MenuInputSubsystem.generated.h"

class FMenuInputProcessor;
class UInputAction;
class UInputMappingContext;
class UMenuKeyGlyphSet;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuInputDeviceChanged, EMenuInputDevice, Device);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMenuInputDeviceChangedNative, EMenuInputDevice /*Device*/);

/**
 * Which input the player is using right now - keyboard and mouse, a gamepad (and whose: Xbox, PlayStation, Switch), or touch -
 * and the button prompts that go with it. Watches every key, stick and mouse move (a Slate input pre-processor, so it sees
 * them whatever has focus) and switches as soon as the player picks up the other device, with thresholds so a resting mouse
 * or a drifting stick doesn't flip prompts. One per local player (split screen: each its own controller).
 *
 * Key prompts (Menu Key Prompt widget, settings screens, notifications' key hints) ask it for an action's key on the current
 * device and the glyph to draw.
 */
UCLASS(DisplayName = "Menu Input Subsystem")
class NAMETBD_API UMenuInputSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:

	static UMenuInputSubsystem* Get(const UObject* WorldContext, const APlayerController* Player = nullptr);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Menu|Input")
	EMenuInputDevice GetCurrentDevice() const { return CurrentDevice; }

	UFUNCTION(BlueprintPure, Category = "Menu|Input")
	bool IsUsingGamepad() const { return CurrentDevice == EMenuInputDevice::Gamepad; }

	/** Whose prompts a gamepad shows: the Button Prompts setting's choice, else the gamepad last used. */
	UFUNCTION(BlueprintPure, Category = "Menu|Input")
	EMenuGamepadFamily GetGamepadFamily() const;

	/** While Get Gamepad Family is Custom: which one (a Key Glyph Set's Custom Family Name). None otherwise. */
	UFUNCTION(BlueprintPure, Category = "Menu|Input")
	FName GetCustomGamepadFamily() const;

	/** The gamepad's hardware name as the platform reports it ("XInputController", "DualSense"); empty when none was seen. */
	UFUNCTION(BlueprintPure, Category = "Menu|Input")
	FString GetGamepadHardwareName() const { return GamepadHardware; }

	/**
	 * Forces whose prompts show: "Auto" (the gamepad used), "Xbox", "PlayStation", "Switch", "SteamDeck", "Generic", or a
	 * Custom family's name (a Key Glyph Set's Custom Family Name).
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu|Input")
	void SetGamepadFamilyOverride(const FString& Family);

	/** Makes Device current now (a touch screen game starting in touch). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Input")
	void SetCurrentDevice(EMenuInputDevice Device);

	/**
	 * The key Action is mapped to for Device (Keyboard & Mouse, Gamepad; Touch reads as Keyboard & Mouse) - the player's
	 * rebinding included. Invalid when it has none there.
	 */
	UFUNCTION(BlueprintPure, Category = "Menu|Input")
	FKey GetKeyForAction(const UInputAction* Action, EMenuInputDevice Device) const;

	/** The same for the current device. */
	UFUNCTION(BlueprintPure, Category = "Menu|Input")
	FKey GetCurrentKeyForAction(const UInputAction* Action) const;

	/**
	 * The key Action has in Context for Device: the player's rebinding of that mapping (when it is player-mappable), else the
	 * context's own key - whether or not the context is applied right now. No Context: Get Key For Action.
	 */
	UFUNCTION(BlueprintPure, Category = "Menu|Input")
	FKey GetKeyForActionInContext(const UInputAction* Action, const UInputMappingContext* Context, EMenuInputDevice Device) const;

	/** How Key is drawn: its art from the project's Key Glyph Sets for the current gamepad family, else a label for a key cap. */
	UFUNCTION(BlueprintPure, Category = "Menu|Input")
	FMenuKeyGlyph GetKeyGlyph(FKey Key) const;

	UPROPERTY(BlueprintAssignable, Category = "Menu|Input")
	FOnMenuInputDeviceChanged OnInputDeviceChanged;

	FOnMenuInputDeviceChangedNative& OnInputDeviceChangedNative() { return DeviceChangedNative; }

	/** From the pre-processor (Slate user Index sent it). */
	void NoteKey(const FKey& Key, int32 SlateUserIndex);
	void NoteAnalog(const FKey& Key, float Value, int32 SlateUserIndex);
	void NoteMouseMove(const FVector2D& Delta, int32 SlateUserIndex);
	void NoteTouch(int32 SlateUserIndex);

private:
	int32 GetSlateUserIndex() const;
	void SwitchTo(EMenuInputDevice Device);
	void NoteGamepadHardware();
	const UMenuKeyGlyphSet* FindGlyphSet(bool bKeyboard, EMenuGamepadFamily Family, FName CustomFamily = NAME_None) const;

	/** A Custom family's set by its name, or null. */
	const UMenuKeyGlyphSet* FindCustomGlyphSet(FName CustomFamily) const;

	TSharedPtr<FMenuInputProcessor> Processor;
	EMenuInputDevice CurrentDevice = EMenuInputDevice::KeyboardMouse;
	EMenuGamepadFamily DetectedFamily = EMenuGamepadFamily::Xbox;
	FName DetectedCustomFamily = NAME_None;
	FString FamilyOverride;
	FString GamepadHardware;
	float MouseTravel = 0.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMenuKeyGlyphSet>> GlyphSets;

	FOnMenuInputDeviceChangedNative DeviceChangedNative;
};
