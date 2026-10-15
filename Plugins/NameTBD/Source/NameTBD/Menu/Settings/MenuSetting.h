// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "MenuSettingTypes.h"

#include "MenuSetting.generated.h"

class APlayerController;
class ULocalPlayer;
class UMenuSettingTarget;
class UMenuSettingsSubsystem;
class UWorld;

/**
 * What a setting's target and formatting see: whose settings these are. In the designer (a settings screen previewed in a
 * Widget Blueprint) there is no player: Local Player and Subsystem are null and targets only read.
 */
struct NAMETBD_API FMenuSettingContext
{
	ULocalPlayer* LocalPlayer = nullptr;
	UMenuSettingsSubsystem* Subsystem = nullptr;

	UWorld* GetWorld() const;
	APlayerController* GetPlayerController() const;

	/** Another setting's current value (empty with no subsystem or no such setting). */
	FString GetValue(FName SettingId) const;

	bool IsPreview() const { return Subsystem == nullptr; }
};

/** How a Choice is drawn. */
UENUM(BlueprintType)
enum class EMenuChoiceDisplay : uint8
{
	Auto UMETA(ToolTip = "A selector with arrows (< High >) for a short list; a dropdown past the style's Selector Max Options (resolutions, languages)."),
	Selector UMETA(ToolTip = "< Option > with arrows and pips: left / right (or the arrows) steps through. The console-friendly default."),
	Dropdown UMETA(ToolTip = "A dropdown list: Accept opens it."),
	Buttons UMETA(ToolTip = "One button per option side by side (Off | Low | High), for two to four short options.")
};

/** What an Action setting's button does. */
UENUM(BlueprintType)
enum class EMenuSettingActionKind : uint8
{
	Event UMETA(ToolTip = "Only fires the settings subsystem's On Setting Action (with this setting's Id) for the game to handle."),
	AutoDetectGraphics UMETA(DisplayName = "Auto-Detect Graphics", ToolTip = "Runs the engine's hardware benchmark, applies the quality levels it recommends and the best upscaler for this graphics card."),
	ResetPage UMETA(ToolTip = "Puts every setting of the page it is on back to its default."),
	ResetAll UMETA(ToolTip = "Puts every setting back to its default (key bindings included)."),
	ResetKeyBindings UMETA(ToolTip = "Puts every key binding back to the game's defaults."),
	RunBenchmark UMETA(ToolTip = "Measures performance for Benchmark Seconds (the screen stays open; close it to measure the game) and reports the average fps, 1% low and frame times in a notification and On Benchmark Finished.")
};

/**
 * One setting: what it is called, the kind of value, where the value goes (its Target), when it applies, and when it is shown
 * or enabled. Settings are definitions (on a Settings Schema asset, or made in code); every player's values are kept by their
 * Menu Settings Subsystem.
 *
 * Make your own kind by subclassing a setting class in C++; send a value anywhere by giving it a Target (a console variable,
 * the engine's Game User Settings, a sound class, a Blueprint of yours) - or none, and read it from Blueprint (Get Setting
 * Value) or with a Setting Binder component.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class NAMETBD_API UMenuSetting : public UObject
{
	GENERATED_BODY()

public:

	// -------------------------------------------------------------------------------------------------
	// Setting
	// -------------------------------------------------------------------------------------------------

	/** Unique name; what the value is saved under and what code and conditions use ("Video.VSync", "Gameplay.FieldOfView"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting")
	FText DisplayName;

	/** Shown in the settings screen's description panel while the setting is focused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting", meta = (MultiLine = true))
	FText Description;

	/** More words search finds it by ("fps", "tearing" for VSync). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting")
	TArray<FString> Keywords;

	/** An image for the description panel (a Choice's option previews win while one is picked). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setting")
	FMenuImage Preview;

	// -------------------------------------------------------------------------------------------------
	// Value
	// -------------------------------------------------------------------------------------------------

	/** Where the value goes. None: it is only saved - read it with Get Setting Value / On Setting Changed or a Setting Binder. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Value")
	TObjectPtr<UMenuSettingTarget> Target;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	EMenuSettingScope Scope = EMenuSettingScope::User;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	EMenuSettingApplyMode ApplyMode = EMenuSettingApplyMode::Default;

	/**
	 * After it is applied the player is asked to keep it, and it goes back by itself unless kept within the confirmation time
	 * (Project Settings > Plugins > Menus) - for display changes that can leave the screen unreadable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	bool bRequiresConfirmation = false;

	/** Takes effect after the game restarts: the screen says so, and the subsystem reports Is Restart Required. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	bool bRequiresRestart = false;

	/** Written again whenever one of these settings changes (Resolution Scale after the upscaler is turned off). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	TArray<FName> ReapplyWhenChanged;

	// -------------------------------------------------------------------------------------------------
	// Conditions
	// -------------------------------------------------------------------------------------------------

	/** Listed only while all of these hold (HDR Brightness while HDR is on). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	TArray<FMenuSettingCondition> ShownWhen;

	/** Can be changed only while all of these hold; greyed out otherwise, with Disabled Reason. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	TArray<FMenuSettingCondition> EnabledWhen;

	/** Why it is greyed out ("Set by the upscaler's mode"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	FText DisabledReason;

	/** Only on these platforms ("Windows", "Desktop", "Console", "Mobile"...). Empty: everywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	TArray<FString> Platforms;

	/** When its target can't be used here (a plugin not installed, a GPU that can't). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Conditions")
	EMenuUnavailableBehavior WhenUnavailable = EMenuUnavailableBehavior::Hide;

	// -------------------------------------------------------------------------------------------------
	// What a kind of setting says about itself
	// -------------------------------------------------------------------------------------------------

	virtual EMenuSettingKind GetKind() const { return EMenuSettingKind::Info; }

	/** Its default: the target's (the engine's own) when it has one, else the setting's. */
	FString GetDefaultValue(const FMenuSettingContext& Context) const;

	/** The default the setting itself declares. */
	virtual FString GetOwnDefaultValue() const { return FString(); }

	/** A value made valid: clamped, snapped, an option that exists. */
	virtual FString Sanitize(const FString& Value, const FMenuSettingContext& Context) const { return Value; }

	/** The value as the player reads it ("On", "75%", "High"). */
	virtual FText FormatValue(const FString& Value, const FMenuSettingContext& Context) const { return FText::FromString(Value); }

	/** The value one step along (left / right): a Bool flips, a Number moves a Step, a Choice moves an option. */
	virtual FString StepValue(const FString& Value, int32 Direction, const FMenuSettingContext& Context) const { return Value; }

	/** Its value is kept by the subsystem (key bindings, actions and info lines are not). */
	virtual bool HoldsValue() const { return true; }

	/** Can it be used here (platform, target)? OutReason says why not. */
	bool IsAvailable(const FMenuSettingContext& Context, FText& OutReason) const;

	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	EMenuSettingKind GetSettingKind() const { return GetKind(); }

	/** Display Name, else the Id. */
	UFUNCTION(BlueprintPure, Category = "Menu|Settings")
	FText GetLabel() const;

	/** Search: Display Name, Keywords, Description and Id contain Text (any case). */
	bool MatchesSearch(const FString& Text) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

/** On / off. */
UCLASS(meta = (DisplayName = "Bool Setting"))
class NAMETBD_API UMenuSettingBool : public UMenuSetting
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	bool bDefaultValue = false;

	/** What On reads ("On" when empty). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	FText OnText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	FText OffText;

	virtual EMenuSettingKind GetKind() const override { return EMenuSettingKind::Bool; }
	virtual FString GetOwnDefaultValue() const override;
	virtual FString Sanitize(const FString& Value, const FMenuSettingContext& Context) const override;
	virtual FText FormatValue(const FString& Value, const FMenuSettingContext& Context) const override;
	virtual FString StepValue(const FString& Value, int32 Direction, const FMenuSettingContext& Context) const override;
};

/** A number in a range. */
UCLASS(meta = (DisplayName = "Number Setting"))
class NAMETBD_API UMenuSettingNumber : public UMenuSetting
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	float DefaultValue = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	float MinValue = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	float MaxValue = 1.f;

	/** What one press of left / right moves, and what values snap to. 0: no snapping, a twentieth of the range per press. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value", meta = (ClampMin = "0"))
	float Step = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display")
	EMenuNumberFormat Format = EMenuNumberFormat::Number;

	/** Decimals shown (Percent: of the percentage). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display", meta = (ClampMin = "0", ClampMax = "4"))
	int32 Decimals = 0;

	/** After the number ("°", "cm", " fps"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display")
	FText Units;

	/** Shown instead of the number at the minimum ("Off", "Unlimited"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display")
	FText MinText;

	/** Shown instead of the number at the maximum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display")
	FText MaxText;

	virtual EMenuSettingKind GetKind() const override { return EMenuSettingKind::Number; }
	virtual FString GetOwnDefaultValue() const override;
	virtual FString Sanitize(const FString& Value, const FMenuSettingContext& Context) const override;
	virtual FText FormatValue(const FString& Value, const FMenuSettingContext& Context) const override;
	virtual FString StepValue(const FString& Value, int32 Direction, const FMenuSettingContext& Context) const override;

	/** Value as 0..1 across the range (a slider's position). */
	float ToFraction(const FString& Value) const;
	FString FromFraction(float Fraction) const;
};

/** One of a list. */
UCLASS(meta = (DisplayName = "Choice Setting"))
class NAMETBD_API UMenuSettingChoice : public UMenuSetting
{
	GENERATED_BODY()

public:

	/** The options. A target that lists its own (resolutions, languages, upscalers found) replaces these. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	TArray<FMenuSettingOption> Options;

	/** The default option's Value. Empty: the first option. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	FString DefaultValue;

	/** Left from the first option goes to the last (and back). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display")
	bool bWrap = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Display")
	EMenuChoiceDisplay Display = EMenuChoiceDisplay::Auto;

	virtual EMenuSettingKind GetKind() const override { return EMenuSettingKind::Choice; }
	virtual FString GetOwnDefaultValue() const override;
	virtual FString Sanitize(const FString& Value, const FMenuSettingContext& Context) const override;
	virtual FText FormatValue(const FString& Value, const FMenuSettingContext& Context) const override;
	virtual FString StepValue(const FString& Value, int32 Direction, const FMenuSettingContext& Context) const override;

	/** The options as shown now: the target's, else Options. Hidden ones only when they are Current. */
	void GetOptions(const FMenuSettingContext& Context, TArray<FMenuSettingOption>& OutOptions, const FString& Current = FString()) const;

	/** Index of Value among Out (from GetOptions), or INDEX_NONE. */
	static int32 FindOption(const TArray<FMenuSettingOption>& InOptions, const FString& Value);
};

/**
 * The keys bound to one Enhanced Input player mappable key (the Mapping Name of an Input Action's Player Mappable Key Settings,
 * or of a mapping's own). Key bindings sections list one per mapping by themselves; make these to place one by hand.
 */
UCLASS(meta = (DisplayName = "Key Binding Setting"))
class NAMETBD_API UMenuSettingKeyBinding : public UMenuSetting
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	FName MappingName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	EMenuKeyBindingDevice Device = EMenuKeyBindingDevice::KeyboardMouse;

	/** The heading it is listed under ("Movement", "Combat"): its mapping's Display Category. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	FText DisplayCategory;

	/** Keys shown side by side (primary, secondary). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value", meta = (ClampMin = "1", ClampMax = "3"))
	int32 SlotCount = 2;

	virtual EMenuSettingKind GetKind() const override { return EMenuSettingKind::KeyBinding; }
	virtual bool HoldsValue() const override { return false; }
	virtual FText FormatValue(const FString& Value, const FMenuSettingContext& Context) const override;
};

/** A button. */
UCLASS(meta = (DisplayName = "Action Setting"))
class NAMETBD_API UMenuSettingAction : public UMenuSetting
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action")
	EMenuSettingActionKind ActionKind = EMenuSettingActionKind::Event;

	/** On the button. Empty: the Display Name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action")
	FText ButtonText;

	/** Asked first ("Reset all settings to their defaults?"). Empty: done at once. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action", meta = (MultiLine = true))
	FText ConfirmText;

	/** Run Benchmark: how long it measures. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Action", meta = (ClampMin = "1", UIMax = "120", Units = "s", EditCondition = "ActionKind == EMenuSettingActionKind::RunBenchmark", EditConditionHides))
	float BenchmarkSeconds = 15.f;

	virtual EMenuSettingKind GetKind() const override { return EMenuSettingKind::Action; }
	virtual bool HoldsValue() const override { return false; }
	virtual FText FormatValue(const FString& Value, const FMenuSettingContext& Context) const override;
};

/** A read-only line; its target gives the text (System Info: the graphics card, the driver). */
UCLASS(meta = (DisplayName = "Info Setting"))
class NAMETBD_API UMenuSettingInfo : public UMenuSetting
{
	GENERATED_BODY()

public:

	/** Shown when the target gives nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value")
	FText Text;

	virtual EMenuSettingKind GetKind() const override { return EMenuSettingKind::Info; }
	virtual bool HoldsValue() const override { return false; }
	virtual FText FormatValue(const FString& Value, const FMenuSettingContext& Context) const override;
};
