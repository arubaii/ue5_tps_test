// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NameTBD/Menu/Input/MenuInputTypes.h"

#include "MenuSettingsLibrary.generated.h"

class APlayerController;
class UMenuInputSubsystem;
class UMenuSettingsSubsystem;

/** The menu settings from anywhere: read a setting where the game uses it (FOV, aim assist, subtitles size). */
UCLASS(meta = (ScriptName = "MenuSettingsLibrary"))
class NAMETBD_API UMenuSettingsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** The player's settings (Player, else the player owning the context, else the first local player). */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static UMenuSettingsSubsystem* GetMenuSettings(const UObject* WorldContextObject, APlayerController* Player = nullptr);

	UFUNCTION(BlueprintPure, Category = "Menu|Settings", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static FString GetSettingValue(const UObject* WorldContextObject, FName SettingId);

	UFUNCTION(BlueprintPure, Category = "Menu|Settings", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static bool GetSettingBool(const UObject* WorldContextObject, FName SettingId, bool bDefault = false);

	UFUNCTION(BlueprintPure, Category = "Menu|Settings", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static float GetSettingNumber(const UObject* WorldContextObject, FName SettingId, float DefaultValue = 0.f);

	/** The menus and notifications fade instead of moving (Accessibility > Reduce Motion). Check it in your own animations. */
	UFUNCTION(BlueprintPure, Category = "Menu|Accessibility")
	static bool IsReduceMotionEnabled();

	UFUNCTION(BlueprintPure, Category = "Menu|Accessibility")
	static bool IsHighContrastEnabled();

	UFUNCTION(BlueprintPure, Category = "Menu|Accessibility")
	static bool IsMenuNarrationEnabled();

	/** Reads Text out through the platform's screen reader when Menu Narration is on (for your own menus). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Accessibility")
	static void NarrateMenuText(const FText& Text);

	/** The device the player uses now (prompts, cursor, layouts). */
	UFUNCTION(BlueprintPure, Category = "Menu|Input", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static EMenuInputDevice GetMenuInputDevice(const UObject* WorldContextObject, APlayerController* Player = nullptr);

	UFUNCTION(BlueprintPure, Category = "Menu|Input", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static UMenuInputSubsystem* GetMenuInput(const UObject* WorldContextObject, APlayerController* Player = nullptr);
};

/** One setting into one property. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuSettingBinding
{
	GENERATED_BODY()

	/** The setting's Id ("Gameplay.FieldOfView"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FName SettingId;

	/** The component of the owner holding the property (the first of this class). None: the actor itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	TSubclassOf<UActorComponent> ComponentClass;

	/** The property's name ("FieldOfView", "MaxDrawDistance", "bVisible"): numbers, bools, enums (by name) and text are set from the value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FName PropertyName;

	/** A Number setting is multiplied by this first (a 0..1 setting into a 0..100 property: 100). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	float Scale = 1.f;

	/** Called on the component (or actor) after the property is set, with no parameters ("MarkRenderStateDirty"). None: nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FName ThenCall;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBoundSettingChanged, FName, SettingId, const FString&, Value);

/**
 * Setting Binder: put the player's settings into properties of this actor and its components, when it starts and whenever they
 * change - no Blueprint. A custom Render Distance setting into a component's draw distance, Camera Shake into your camera's shake
 * scale, a Show Crosshair setting into a widget component's visibility. Reads the settings of the player who owns or controls the
 * actor (the first local player otherwise).
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent, DisplayName = "Setting Binder"))
class NAMETBD_API UMenuSettingBinderComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UMenuSettingBinderComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<FMenuSettingBinding> Bindings;

	/** A bound setting changed (and once for each at start), after its property was set. */
	UPROPERTY(BlueprintAssignable, Category = "Settings")
	FOnBoundSettingChanged OnBoundSettingChanged;

	/** Sets every bound property from its setting now. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Settings")
	void ApplyAllBindings();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleSettingChanged(FName SettingId, const FString& Value);
	bool ApplyBinding(const FMenuSettingBinding& Binding, const FString& Value);
	UMenuSettingsSubsystem* FindSettings() const;

	TWeakObjectPtr<UMenuSettingsSubsystem> BoundSettings;
	FDelegateHandle ChangedHandle;
};
