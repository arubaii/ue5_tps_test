// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MenuSetting.h"
#include "MenuSettingTypes.h"

#include "MenuSettingTargets.generated.h"

class USoundClass;
class USoundMix;

/**
 * Where a setting's value goes, and where it is read back from. A setting without one only keeps its value (Saved Value).
 *
 * Built in: Console Variable (any cvar - Render Distance is r.ViewDistanceScale), Game User Settings (the engine's display and
 * scalability settings), Upscaler (DLSS / FSR / XeSS / TSR, frame generation, Reflex - see MenuUpscalers.h), Sound Class
 * (volumes), Built-in (gamma, UI scale, colour blindness, subtitles, vibration, reduce motion...), Notifications (the
 * plugin's notification preferences), Language, System Info, and Blueprint (your own, in a Blueprint).
 *
 * Your own in C++: subclass this, override Read / Write (and Get Options / Is Available as needed).
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class NAMETBD_API UMenuSettingTarget : public UObject
{
	GENERATED_BODY()

public:

	/** Can it be used here (a plugin's console variable exists, the GPU supports it)? OutReason says why not. */
	virtual bool IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const { return true; }

	/** The value as it is now in the engine (what was applied, maybe by something else). False: nothing to read. */
	virtual bool Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const { return false; }

	/** Puts Value into effect. */
	virtual void Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const {}

	/** The engine's own default, when it has one (the desktop resolution). False: the setting's default. */
	virtual bool GetDefault(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const { return false; }

	/** Options it lists itself for a Choice (resolutions, languages, the upscalers found). False: the setting's own. */
	virtual bool GetOptions(const UMenuSetting& Setting, const FMenuSettingContext& Context, TArray<FMenuSettingOption>& OutOptions) const { return false; }

	/**
	 * The value is kept in the plugin's save file and written again when the game starts (console variables, sound). False: the
	 * engine keeps it itself (Game User Settings saves its own ini) and it is only read.
	 */
	virtual bool UsesSavedValue() const { return true; }

	/** Written again after each map load and pawn change (what they reset - sound mixes, force feedback, the camera). */
	virtual bool ReapplyAfterMapLoad() const { return false; }

	/** Its default is written on start even when the player never changed it (the upscaler: Auto must take effect). */
	virtual bool WritesDefaultOnStart(const UMenuSetting& Setting) const { return false; }

	/** Saving: the engine's own save (Game User Settings::SaveSettings). Called once per save for each kind of target used. */
	virtual void Save(const FMenuSettingContext& Context) const {}
};

/** Only saved: the game reads it (Get Setting Value, On Setting Changed, a Setting Binder component, an input modifier). */
UCLASS(meta = (DisplayName = "Saved Value"))
class NAMETBD_API UMenuSettingTarget_Saved : public UMenuSettingTarget
{
	GENERATED_BODY()
};

/** A setting value given to a console variable as something else ("High" -> "3"). */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuCVarValueMap
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Console Variable")
	FString SettingValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Console Variable")
	FString CVarValue;
};

/**
 * A console variable: any engine, plugin or game cvar. Set with Game Setting priority (above scalability and device
 * profiles). Unavailable (hidden) while the cvar doesn't exist - a plugin's cvar with the plugin off.
 */
UCLASS(meta = (DisplayName = "Console Variable"))
class NAMETBD_API UMenuSettingTarget_ConsoleVariable : public UMenuSettingTarget
{
	GENERATED_BODY()

public:

	/** "r.ViewDistanceScale", "r.MotionBlurQuality". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console Variable")
	FString Name;

	/** Also given the same value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console Variable")
	TArray<FString> AlsoSet;

	/** A Number is multiplied by this on its way to the cvar (a 0..1 setting into a 0..100 cvar: 100). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console Variable")
	float Scale = 1.f;

	/** A Bool is written the other way round (a "Film Grain" setting into r.FilmGrain.Disable). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console Variable")
	bool bInvert = false;

	/** Setting values given to the cvar as something else; Bools are "1" / "0" by default, mapped "true" / "false". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console Variable")
	TArray<FMenuCVarValueMap> ValueMap;

	virtual bool IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const override;
	virtual bool Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const override;
	virtual void Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const override;
};

/** The engine's display and scalability settings (UGameUserSettings) a target can drive. */
UENUM(BlueprintType)
enum class EMenuGameUserSetting : uint8
{
	WindowMode UMETA(ToolTip = "Choice: \"Fullscreen\", \"WindowedFullscreen\" (borderless), \"Windowed\"."),
	Resolution UMETA(ToolTip = "Choice listing the monitor's resolutions (\"1920x1080\"); the desktop's in Windowed Fullscreen."),
	VSync UMETA(DisplayName = "VSync", ToolTip = "Bool."),
	FrameRateLimit UMETA(ToolTip = "Number or Choice of frames per second; 0 = unlimited."),
	OverallQuality UMETA(ToolTip = "Choice 0..4 (Low..Cinematic); \"-1\" when the levels differ (shown as Custom). Setting it sets every quality below."),
	ViewDistanceQuality,
	AntiAliasingQuality,
	ShadowQuality,
	GlobalIlluminationQuality,
	ReflectionQuality,
	PostProcessQuality,
	TextureQuality,
	EffectsQuality,
	FoliageQuality,
	ShadingQuality,
	ResolutionScale UMETA(ToolTip = "Number in percent of the output resolution the scene renders at."),
	DynamicResolution UMETA(ToolTip = "Bool: the engine lowers the resolution under load (where the platform supports it)."),
	HDROutput UMETA(DisplayName = "HDR Output", ToolTip = "Bool: HDR display output (unavailable on displays that can't)."),
	HDRNits UMETA(DisplayName = "HDR Brightness (nits)", ToolTip = "Number: the display's peak brightness for HDR output."),
	AudioQuality UMETA(ToolTip = "Choice of the audio quality levels (takes effect after a restart).")
};

/** UGameUserSettings: window mode, resolution, VSync, frame rate limit, the scalability groups, HDR. Saved by the engine. */
UCLASS(meta = (DisplayName = "Game User Settings"))
class NAMETBD_API UMenuSettingTarget_GameUserSettings : public UMenuSettingTarget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game User Settings")
	EMenuGameUserSetting Field = EMenuGameUserSetting::VSync;

	virtual bool IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const override;
	virtual bool Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const override;
	virtual void Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const override;
	virtual bool GetDefault(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const override;
	virtual bool GetOptions(const UMenuSetting& Setting, const FMenuSettingContext& Context, TArray<FMenuSettingOption>& OutOptions) const override;
	virtual bool UsesSavedValue() const override { return false; }
	virtual void Save(const FMenuSettingContext& Context) const override;
};

/**
 * A volume (0..1) on a sound class through a sound mix. With none set here, Channel finds them in Project Settings > Plugins >
 * Menus > Audio Channels; the Master channel with nothing assigned sets the audio device's own volume, so it always works.
 * Another channel with nothing assigned is unavailable (hidden) until you assign its sound class.
 */
UCLASS(meta = (DisplayName = "Sound Volume"))
class NAMETBD_API UMenuSettingTarget_SoundVolume : public UMenuSettingTarget
{
	GENERATED_BODY()

public:

	/** "Master", "Music", "Effects", "Dialogue", "UI", "Voice" - looked up in the project's Audio Channels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	FName Channel = TEXT("Master");

	/** Overrides the channel's sound class. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TSoftObjectPtr<USoundClass> SoundClass;

	/** Overrides the project's settings sound mix. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TSoftObjectPtr<USoundMix> SoundMix;

	virtual bool IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const override;
	virtual void Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const override;
	virtual bool ReapplyAfterMapLoad() const override { return true; }

private:
	USoundClass* ResolveClass() const;
	USoundMix* ResolveMix() const;
};

/** Engine and plugin settings without a console variable. */
UENUM(BlueprintType)
enum class EMenuBuiltInSetting : uint8
{
	DisplayGamma UMETA(ToolTip = "Number: the display gamma (brightness), 2.2 standard."),
	UIScale UMETA(DisplayName = "UI Scale", ToolTip = "Number multiplying every UI's DPI scale (1 = as designed)."),
	ColorBlindMode UMETA(ToolTip = "Choice: \"None\", \"Deuteranope\", \"Protanope\", \"Tritanope\" - Slate's colour vision correction over the whole screen."),
	ColorBlindStrength UMETA(ToolTip = "Number 0..10: how strongly Color Blind Mode corrects."),
	Subtitles UMETA(ToolTip = "Bool: the engine's subtitles (sound waves' and dialogue's)."),
	BackgroundVolume UMETA(ToolTip = "Number 0..1: the volume while the game window isn't focused (0 mutes in the background)."),
	Vibration UMETA(ToolTip = "Bool: controller force feedback for the player."),
	VibrationStrength UMETA(ToolTip = "Number 0..1 (or more): multiplies force feedback."),
	ReduceMotion UMETA(ToolTip = "Bool: the plugin's menus and notifications only fade - no sliding or bouncing. Read it anywhere with Is Reduce Motion Enabled."),
	HighContrast UMETA(ToolTip = "Bool: settings screens use their style's high contrast colours. Read it anywhere with Is High Contrast Enabled."),
	MenuNarration UMETA(ToolTip = "Bool: settings screens read the focused setting and its value out through the screen reader."),
	ControllerButtonStyle UMETA(ToolTip = "Choice: \"Auto\", \"Xbox\", \"PlayStation\", \"Switch\", \"Generic\" - which button prompts show."),
	FieldOfView UMETA(ToolTip = "Number in degrees: the player camera manager's default field of view and the view target's active camera component (set again after each map load and pawn change).")
};

/** Engine and plugin settings: gamma, UI scale, colour blindness, subtitles, background volume, vibration, accessibility flags. */
UCLASS(meta = (DisplayName = "Built-in"))
class NAMETBD_API UMenuSettingTarget_BuiltIn : public UMenuSettingTarget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Built-in")
	EMenuBuiltInSetting Field = EMenuBuiltInSetting::DisplayGamma;

	virtual bool Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const override;
	virtual void Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const override;
	virtual bool ReapplyAfterMapLoad() const override;
};

/** The plugin's notification preferences (see Notifications.md) a setting can drive. */
UENUM(BlueprintType)
enum class EMenuNotificationPreference : uint8
{
	DurationScale UMETA(ToolTip = "Number 0.25..10: how long notifications stay."),
	KeepUntilDismissed UMETA(ToolTip = "Bool: notifications never go by themselves."),
	ScreenReader UMETA(ToolTip = "Bool: notifications are read out."),
	ReduceMotion UMETA(ToolTip = "Bool: notifications only fade."),
	PlaySounds UMETA(ToolTip = "Bool."),
	DoNotDisturb UMETA(ToolTip = "Bool: only critical notifications show.")
};

/** One of the player's notification preferences (the plugin's Notification Subsystem). */
UCLASS(meta = (DisplayName = "Notification Preference"))
class NAMETBD_API UMenuSettingTarget_Notifications : public UMenuSettingTarget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notifications")
	EMenuNotificationPreference Field = EMenuNotificationPreference::DurationScale;

	virtual bool IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const override;
	virtual bool Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const override;
	virtual void Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const override;
};

/**
 * The game's language: a Choice listing the cultures the game is localized to (in their own language: "Deutsch"). Saved by the
 * engine (the game's config), so it holds from the first frame next time.
 */
UCLASS(meta = (DisplayName = "Language"))
class NAMETBD_API UMenuSettingTarget_Language : public UMenuSettingTarget
{
	GENERATED_BODY()

public:

	/** Only offer these cultures ("en", "de", "ja"). Empty: every culture with the game's localization. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Language")
	TArray<FString> Cultures;

	virtual bool Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const override;
	virtual void Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const override;
	virtual bool GetDefault(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const override;
	virtual bool GetOptions(const UMenuSetting& Setting, const FMenuSettingContext& Context, TArray<FMenuSettingOption>& OutOptions) const override;
	virtual bool UsesSavedValue() const override { return false; }
};

/** What a System Info line shows. */
UENUM(BlueprintType)
enum class EMenuSystemInfo : uint8
{
	GraphicsCard,
	GraphicsDriver,
	Processor,
	Memory UMETA(ToolTip = "The machine's physical memory."),
	GraphicsAPI UMETA(DisplayName = "Graphics API", ToolTip = "DirectX 12, Vulkan, Metal..."),
	DisplayResolution UMETA(ToolTip = "The desktop resolution."),
	GameVersion UMETA(ToolTip = "The project's version (Project Settings > Description).")
};

/** Read-only facts about the machine, for Info settings. */
UCLASS(meta = (DisplayName = "System Info"))
class NAMETBD_API UMenuSettingTarget_SystemInfo : public UMenuSettingTarget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "System Info")
	EMenuSystemInfo Field = EMenuSystemInfo::GraphicsCard;

	virtual bool Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const override;
	virtual bool UsesSavedValue() const override { return false; }
};

/**
 * Your own target, in a Blueprint: make a Blueprint of this class, implement Write Value (and Read Value if the value lives
 * somewhere it can be read back from), and pick it as a setting's target.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Blueprint Target"))
class NAMETBD_API UMenuSettingTarget_Blueprint : public UMenuSettingTarget
{
	GENERATED_BODY()

public:

	/** Puts Value into effect for Player (null in the designer's preview: do nothing then). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Menu|Settings")
	void WriteValue(const UMenuSetting* Setting, APlayerController* Player, const FString& Value);

	/** The value as it is now; return false when there is nothing to read (the saved value is used). */
	UFUNCTION(BlueprintNativeEvent, Category = "Menu|Settings")
	bool ReadValue(const UMenuSetting* Setting, APlayerController* Player, FString& OutValue) const;

	/** False hides (or greys out) the setting; Reason says why. */
	UFUNCTION(BlueprintNativeEvent, Category = "Menu|Settings")
	bool CanBeUsed(const UMenuSetting* Setting, APlayerController* Player, FText& Reason) const;

	/** Keep the value in the plugin's save file and write it again on start (off: your Write keeps it somewhere itself). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu|Settings")
	bool bUseSavedValue = true;

	/** Written again after each map loads. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu|Settings")
	bool bReapplyAfterMapLoad = false;

	virtual bool IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const override;
	virtual bool Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const override;
	virtual void Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const override;
	virtual bool UsesSavedValue() const override { return bUseSavedValue; }
	virtual bool ReapplyAfterMapLoad() const override { return bReapplyAfterMapLoad; }
};

/** Global state the Built-in target keeps (the plugin's accessibility flags), readable anywhere. */
namespace MenuAccessibility
{
	NAMETBD_API bool IsReduceMotionEnabled();
	NAMETBD_API bool IsHighContrastEnabled();
	NAMETBD_API bool IsMenuNarrationEnabled();

	NAMETBD_API void SetReduceMotion(bool bEnabled);
	NAMETBD_API void SetHighContrast(bool bEnabled);
	NAMETBD_API void SetMenuNarration(bool bEnabled);

	/** Fired when one of the flags changes. */
	NAMETBD_API FSimpleMulticastDelegate& OnChanged();
}
