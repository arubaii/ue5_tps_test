// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/Widgets/WidgetSVG.h"

#include "MenuSettingTypes.generated.h"

class UMaterialInterface;
class UTexture2D;
class UWidgetSVG;

NAMETBD_API DECLARE_LOG_CATEGORY_EXTERN(LogNameTBDSettings, Log, All);

/**
 * The kinds of value a setting holds - what a settings screen draws for it. Every value is kept as text (the canonical form
 * the save file, conditions and the AI use): a Bool "true" / "false", a Number "1.5", a Choice its option's Value.
 */
UENUM(BlueprintType)
enum class EMenuSettingKind : uint8
{
	Bool UMETA(ToolTip = "On / off: a switch (or an On / Off selector)."),
	Number UMETA(ToolTip = "A number in a range: a slider with its value (percent, multiplier, units)."),
	Choice UMETA(ToolTip = "One of a list of options: a selector with arrows (< High >), or a dropdown for long lists."),
	KeyBinding UMETA(ToolTip = "The keys bound to an input (Enhanced Input player mappable keys): press a key to change it."),
	Action UMETA(ToolTip = "A button that does something: auto-detect graphics, reset to defaults, run a benchmark."),
	Info UMETA(ToolTip = "A read-only line: the graphics card, the driver.")
};

/** Who a setting belongs to: the machine (display, graphics - one for everyone on it) or the player (controls, accessibility). */
UENUM(BlueprintType)
enum class EMenuSettingScope : uint8
{
	Machine UMETA(ToolTip = "Saved once for the machine: display, graphics, audio device. Every local player sees the same value."),
	User UMETA(ToolTip = "Saved for each player (split screen: each has their own): controls, accessibility, gameplay.")
};

/** When a changed setting takes effect. */
UENUM(BlueprintType)
enum class EMenuSettingApplyMode : uint8
{
	Default UMETA(ToolTip = "As the settings screen showing it says (its Apply Policy)."),
	Immediately UMETA(ToolTip = "At once, as it is changed, so the player sees it live (Revert puts it back)."),
	OnApply UMETA(ToolTip = "Only when the player presses Apply (heavy changes: resolution, ray tracing).")
};

/** How a setting that can't be used here (its plugin missing, the GPU not supporting it, the platform) shows. */
UENUM(BlueprintType)
enum class EMenuUnavailableBehavior : uint8
{
	Hide UMETA(ToolTip = "Not listed at all."),
	Disable UMETA(ToolTip = "Listed, greyed out, with why in its description (\"Needs an NVIDIA RTX graphics card\").")
};

/** How a condition compares a setting's value. Numbers compare as numbers when both sides are numbers, else as text. */
UENUM(BlueprintType)
enum class EMenuSettingCompare : uint8
{
	Equals,
	NotEquals UMETA(DisplayName = "Not Equals"),
	Greater,
	GreaterOrEqual UMETA(DisplayName = "Greater Or Equal"),
	Less,
	LessOrEqual UMETA(DisplayName = "Less Or Equal"),
	IsTrue UMETA(DisplayName = "Is True"),
	IsFalse UMETA(DisplayName = "Is False"),
	IsAnyOf UMETA(DisplayName = "Is Any Of", ToolTip = "Value is a comma separated list; true when the setting's value is one of them.")
};

/** How a Number is shown. */
UENUM(BlueprintType)
enum class EMenuNumberFormat : uint8
{
	Number UMETA(ToolTip = "The number with Decimals, then Units (\"90 °\", \"2.2\")."),
	Percent UMETA(ToolTip = "A value of 0..1 shown as 0..100% (volumes)."),
	PercentValue UMETA(DisplayName = "Percent (Value)", ToolTip = "A value already in percent shown with % (resolution scale 75 -> \"75%\")."),
	Multiplier UMETA(ToolTip = "\"1.5x\" (sensitivity, render distance).")
};

/** Which keys a key binding row edits. */
UENUM(BlueprintType)
enum class EMenuKeyBindingDevice : uint8
{
	KeyboardMouse UMETA(DisplayName = "Keyboard & Mouse"),
	Gamepad
};

/** One condition on another setting's value ("shown while VSync is off", "enabled while Upscaler is DLSS"). */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuSettingCondition
{
	GENERATED_BODY()

	/** The setting whose value is tested (its Id). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	FName SettingId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EMenuSettingCompare Compare = EMenuSettingCompare::IsTrue;

	/** What it is compared with (not used by Is True / Is False). Comma separated for Is Any Of. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	FString Value;

	FMenuSettingCondition() = default;
	FMenuSettingCondition(FName InSettingId, EMenuSettingCompare InCompare, const FString& InValue = FString())
		: SettingId(InSettingId), Compare(InCompare), Value(InValue)
	{
	}

	/** Compares Actual (the tested setting's value, as text). */
	bool Test(const FString& Actual) const;
};

/** An image shown by the menus (a tab icon, a setting's preview, an option's preview): a texture or material, or an SVG. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuImage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image")
	EWidgetImageSource ImageSource = EWidgetImageSource::Texture;

	/** A texture or a material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image", meta = (AllowedClasses = "/Script/Engine.Texture2D,/Script/Engine.MaterialInterface", EditCondition = "ImageSource == EWidgetImageSource::Texture", EditConditionHides))
	TObjectPtr<UObject> Image = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image", meta = (DisplayName = "SVG", EditCondition = "ImageSource == EWidgetImageSource::SVG", EditConditionHides))
	TObjectPtr<UWidgetSVG> SVG = nullptr;

	/** Multiplies the image. White keeps its colours; with an SVG drawn in white, this is its colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image")
	FLinearColor Tint = FLinearColor::White;

	bool HasImage() const { return ImageSource == EWidgetImageSource::SVG ? SVG != nullptr : Image != nullptr; }
};

/** One option of a Choice setting. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuSettingOption
{
	GENERATED_BODY()

	/** What is stored and handed to the setting's target ("2", "DLSS", "1920x1080"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Option")
	FString Value;

	/** What the player sees ("High"). Empty: Value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Option")
	FText Label;

	/** Shown under the setting's own description while this option is picked ("Sharpest image; costs the most"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Option", meta = (MultiLine = true))
	FText Description;

	/** A preview of this option for the description panel (a screenshot at this quality). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Option")
	FMenuImage Preview;

	/** Shown when it is the value, never offered (Overall Quality's "Custom"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Option")
	bool bHiddenUnlessCurrent = false;

	FMenuSettingOption() = default;
	FMenuSettingOption(const FString& InValue, const FText& InLabel, const FText& InDescription = FText::GetEmpty())
		: Value(InValue), Label(InLabel), Description(InDescription)
	{
	}

	FText GetLabel() const { return Label.IsEmpty() ? FText::FromString(Value) : Label; }
};

/** A console variable and the value it is given. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuCVarAssignment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Console Variable")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Console Variable")
	FString Value;

	FMenuCVarAssignment() = default;
	FMenuCVarAssignment(const FString& InName, const FString& InValue) : Name(InName), Value(InValue) {}
};

/** A setting value, by id (what Get / Set All Settings and save files carry). */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuSettingValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setting")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setting")
	FString Value;
};

namespace MenuSettingValues
{
	/** "true" / "false". Reads "1", "yes", "on" as true. */
	NAMETBD_API FString FromBool(bool bValue);
	NAMETBD_API bool ToBool(const FString& Value);

	/** A number as short text ("1.5", "60", "0.333"). */
	NAMETBD_API FString FromNumber(double Value);
	NAMETBD_API double ToNumber(const FString& Value, double Default = 0.0);
	NAMETBD_API bool IsNumber(const FString& Value);

	/** "1920x1080". */
	NAMETBD_API FString FromResolution(const FIntPoint& Resolution);
	NAMETBD_API bool ToResolution(const FString& Value, FIntPoint& OutResolution);

	/** The platform names a setting's Platforms list can use: the platform ("Windows", "Mac", "Linux"...) and its group ("Desktop", "Console", "Mobile"). */
	NAMETBD_API bool IsCurrentPlatformIn(const TArray<FString>& Platforms);
}
