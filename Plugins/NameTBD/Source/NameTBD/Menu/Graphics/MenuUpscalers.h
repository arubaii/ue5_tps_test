// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NameTBD/Menu/Settings/MenuSettingTargets.h"
#include "NameTBD/Menu/Settings/MenuSettingTypes.h"

#include "MenuUpscalers.generated.h"

/**
 * Vendor graphics technology - NVIDIA DLSS (super resolution, DLAA, frame generation, Reflex), AMD FSR (super resolution,
 * frame generation, Anti-Lag), Intel XeSS (super resolution, frame generation, XeLL) - and the engine's own TSR / TAA / FXAA,
 * as data. Each vendor ships its technology as an Unreal plugin that is driven by console variables; the plugin reads which
 * are installed by whether their console variables exist, and which this graphics card supports by asking the vendor
 * plugin's Blueprint library through reflection - so there is no build dependency on any of them, the menu offers exactly what
 * the player can use, and a new version (or a technology not listed) is a row in Project Settings > Plugins > Menus >
 * Graphics Features, not code.
 */

/** What a graphics feature is for (one choice per kind is active at a time). */
UENUM(BlueprintType)
enum class EMenuGraphicsFeatureKind : uint8
{
	Upscaler UMETA(ToolTip = "Anti-aliasing and upscaling: DLSS, FSR, XeSS, TSR, TAA, FXAA, off. One at a time."),
	FrameGeneration UMETA(ToolTip = "Generated frames between rendered ones: DLSS Frame Generation, FSR Frame Generation, XeSS Frame Generation."),
	LatencyReduction UMETA(ToolTip = "Lower input latency: NVIDIA Reflex, AMD Anti-Lag, Intel XeLL.")
};

/** Whose graphics cards a feature needs. */
UENUM(BlueprintType)
enum class EMenuGPUVendor : uint8
{
	Any,
	NVIDIA,
	AMD,
	Intel
};

/** One mode of a feature ("Quality", "Performance", "On + Boost"). */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuGraphicsFeatureMode
{
	GENERATED_BODY()

	/** What is saved ("Quality"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mode")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mode")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mode", meta = (MultiLine = true))
	FText Description;

	/** Set when this mode is chosen (after the feature's Enable Variables). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mode")
	TArray<FMenuCVarAssignment> Variables;

	/**
	 * The screen percentage the scene renders at in this mode, written to r.ScreenPercentage (66.7 for Quality: 1.5x per axis).
	 * 0: left alone (the Resolution Scale setting decides).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mode", meta = (ClampMin = "0", ClampMax = "200", Units = "Percent"))
	float ScreenPercentage = 0.f;
};

/** One technology. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuGraphicsFeature
{
	GENERATED_BODY()

	/** What is saved ("DLSS", "FSR", "TSR", "DLSSG", "Reflex"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature")
	EMenuGraphicsFeatureKind Kind = EMenuGraphicsFeatureKind::Upscaler;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature", meta = (MultiLine = true))
	FText Description;

	/** Whose graphics cards it runs on (DLSS: NVIDIA RTX). Any: every card that passes the Support Function. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	EMenuGPUVendor Vendor = EMenuGPUVendor::Any;

	/** Auto prefers it on this vendor's cards (+50 Auto Priority): XeSS on Intel, FSR on AMD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	EMenuGPUVendor PreferredOn = EMenuGPUVendor::Any;

	/** A console variable that only exists while the technology's plugin is enabled (r.NGX.DLSS.Enable). Empty: built into the engine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	FString DetectVariable;

	/**
	 * A static Blueprint function returning bool that says whether this machine supports it, as "/Script/Module.ClassName:FunctionName"
	 * ("/Script/DLSSBlueprint.DLSSLibrary:IsDLSSSupported"). Called through reflection when the class exists. Empty: supported
	 * wherever the plugin is (and the vendor matches).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	FString SupportFunction;

	/** Needs another feature active (FSR Frame Generation needs FSR upscaling). None: works with any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Detection")
	FName RequiresFeature;

	/** Set whenever this feature is chosen (any mode). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variables")
	TArray<FMenuCVarAssignment> EnableVariables;

	/** Set when another feature of the same kind (or Off) is chosen: what turns it off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variables")
	TArray<FMenuCVarAssignment> DisableVariables;

	/** The modes offered. None: the feature is on or off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variables")
	TArray<FMenuGraphicsFeatureMode> Modes;

	/** The mode picked when it is first chosen. None: the first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variables")
	FName DefaultMode;

	/** Its sharpening strength (0..1): a Sharpness setting drives this while the feature is active. Empty: no sharpening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variables")
	FString SharpnessVariable;

	/** Changing to or from it takes a restart (the plugin reads it at start). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variables")
	bool bRequiresRestart = false;

	/** Picked by Auto: the highest found and supported wins (DLSS 100, FSR 80, XeSS 70, TSR 50; +50 on its Preferred On vendor). Negative: never picked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feature")
	int32 AutoPriority = 0;

	const FMenuGraphicsFeatureMode* FindMode(FName ModeId) const;
	FName GetDefaultModeId() const;
};

/** Is a feature usable here, and why not. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuGraphicsFeatureStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Graphics")
	FName Id;

	UPROPERTY(BlueprintReadOnly, Category = "Graphics")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Graphics")
	EMenuGraphicsFeatureKind Kind = EMenuGraphicsFeatureKind::Upscaler;

	/** Its plugin is enabled (or it is built into the engine). */
	UPROPERTY(BlueprintReadOnly, Category = "Graphics")
	bool bInstalled = false;

	/** Installed, and this graphics card runs it. */
	UPROPERTY(BlueprintReadOnly, Category = "Graphics")
	bool bSupported = false;

	/** Why not ("Needs an NVIDIA RTX graphics card", "The DLSS plugin isn't enabled"). */
	UPROPERTY(BlueprintReadOnly, Category = "Graphics")
	FText Reason;
};

/** Which part of the graphics features a setting drives. */
UENUM(BlueprintType)
enum class EMenuGraphicsFeatureField : uint8
{
	Upscaler UMETA(ToolTip = "Choice of the upscalers found and supported, plus \"Auto\" (the best for this card)."),
	UpscalerMode UMETA(ToolTip = "Choice of the active upscaler's modes (Quality, Balanced, Performance...)."),
	FrameGeneration UMETA(ToolTip = "Choice: \"Off\" and the frame generation found and supported (with what it needs active)."),
	LatencyReduction UMETA(ToolTip = "Choice: \"Off\" and each mode of the latency reduction found (Reflex On, Reflex On + Boost...)."),
	Sharpness UMETA(ToolTip = "Number 0..1 for the active upscaler's sharpening (unavailable when it has none)."),
	ResolutionScale UMETA(ToolTip = "Number in percent (r.ScreenPercentage): unavailable while the active upscaler's mode sets the resolution itself (written again when it stops).")
};

/** Drives the graphics features (Project Settings > Plugins > Menus > Graphics Features). */
UCLASS(meta = (DisplayName = "Graphics Feature"))
class NAMETBD_API UMenuSettingTarget_GraphicsFeature : public UMenuSettingTarget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Graphics")
	EMenuGraphicsFeatureField Field = EMenuGraphicsFeatureField::Upscaler;

	virtual bool IsAvailable(const UMenuSetting& Setting, const FMenuSettingContext& Context, FText& OutReason) const override;
	virtual bool GetOptions(const UMenuSetting& Setting, const FMenuSettingContext& Context, TArray<FMenuSettingOption>& OutOptions) const override;
	virtual void Write(const UMenuSetting& Setting, const FMenuSettingContext& Context, const FString& Value) const override;
	virtual bool GetDefault(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const override;
	virtual bool WritesDefaultOnStart(const UMenuSetting& Setting) const override;
	virtual bool Read(const UMenuSetting& Setting, const FMenuSettingContext& Context, FString& OutValue) const override;
};

/** The graphics features, from anywhere. */
UCLASS(meta = (ScriptName = "MenuGraphicsLibrary"))
class NAMETBD_API UMenuGraphicsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** NVIDIA, AMD, Intel or Any (another, or unknown) - the graphics card the game renders with. */
	UFUNCTION(BlueprintPure, Category = "Menu|Graphics")
	static EMenuGPUVendor GetGPUVendor();

	/** The graphics card's name ("NVIDIA GeForce RTX 4080"). */
	UFUNCTION(BlueprintPure, Category = "Menu|Graphics")
	static FString GetGPUName();

	/** Every feature in the project's table, with whether it is installed and supported here. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Graphics")
	static TArray<FMenuGraphicsFeatureStatus> GetGraphicsFeatures();

	UFUNCTION(BlueprintCallable, Category = "Menu|Graphics")
	static bool IsGraphicsFeatureSupported(FName FeatureId);

	/** The supported upscaler Auto picks here (highest Auto Priority). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Graphics")
	static FName GetRecommendedUpscaler();

	/**
	 * Makes Feature (of its kind) the active one in Mode: the others of its kind get their Disable Variables, this one its Enable
	 * Variables and the mode's. "Off" (or None) turns the kind off. False when it isn't supported here.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu|Graphics")
	static bool ApplyGraphicsFeature(EMenuGraphicsFeatureKind Kind, FName FeatureId, FName ModeId);

	/** The feature of Kind made active last (None: off / never set). */
	UFUNCTION(BlueprintPure, Category = "Menu|Graphics")
	static FName GetActiveGraphicsFeature(EMenuGraphicsFeatureKind Kind);

	/** The mode of the active feature of Kind (None when it has none). */
	UFUNCTION(BlueprintPure, Category = "Menu|Graphics")
	static FName GetActiveGraphicsMode(EMenuGraphicsFeatureKind Kind);

	/** Gives the active upscaler's Sharpness Variable Value (0..1). False when it has none. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Graphics")
	static bool SetUpscalerSharpness(float Value);

	/** Native: the table row. */
	static const FMenuGraphicsFeature* FindFeature(FName FeatureId);
	static FMenuGraphicsFeatureStatus GetStatus(const FMenuGraphicsFeature& Feature);

	/** Sets a console variable by name with Game Setting priority. False when it doesn't exist. */
	static bool SetConsoleVariable(const FString& Name, const FString& Value);
};
