// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"


#include "NameTBD/Cards/InfoCardTypes.h"
#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Actions/ButtonAction.h"
#include "NameTBD/Buttons/Actions/ContextMenuInterface.h"
#include "InfoCardDeveloperSettings.generated.h"

class UInfoCardDataAsset;
class UInfoCardWidget;
class UContextMenuStyleAsset;




/**
 * One named preset: a full set of values for every project-wide info card
 * tunable. A designer picks a preset by name on any FInfoCardConfigurableFloat
 * set to UseProjectSettings (see that struct) — Presets below starts with a
 * single "Default" entry and grows however the project needs, e.g. one named
 * "Fast Combat HUD" and another "Relaxed Menu".
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FInfoCardPreset
{
	GENERATED_BODY()

	/**
	 * What this preset is called in every card's PresetName dropdown. Keep
	 * these unique — if two presets share a name, PresetName lookups resolve
	 * to whichever one TryGetPresetValue happens to reach first. Renaming a
	 * preset breaks the link on any card already pointed at the old name
	 * (see FInfoCardConfigurableFloat::Resolve); such a card falls back to
	 * its own CustomValue rather than picking a different preset.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Info Card")
	FName PresetName = TEXT("Default");

	/** See FInfoCardSpec::HoverDelay. */
	UPROPERTY(EditAnywhere, Config, Category = "Info Card", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "3.0"))
	float HoverDelay = 0.35f;

	/** See FInfoCardSpec::KeepOpenSlack. */
	UPROPERTY(EditAnywhere, Config, Category = "Info Card", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "64.0"))
	float KeepOpenSlack = 16.f;

	/** See FInfoCardSpec::KeepOpenGracePeriod. */
	UPROPERTY(EditAnywhere, Config, Category = "Info Card", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float KeepOpenGracePeriod = 0.2f;

	/** See FInfoCardSpec::SuccessiveHoverWindow. */
	UPROPERTY(EditAnywhere, Config, Category = "Info Card", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float SuccessiveHoverWindow = 0.5f;

	/** See FInfoCardSpec::AutoDismissDuration. 0 means never. */
	UPROPERTY(EditAnywhere, Config, Category = "Info Card", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	float AutoDismissDuration = 0.f;
};

/**
 * Legacy: a named, project-wide set of state designs, from when Design Presets lived in Project Settings. Design presets are
 * Context Menu Design Preset assets now; a Design Target that still names one of these has it copied in once, on load.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuDesignPreset
{
	GENERATED_BODY()

	/** Name shown in each Design Target's Preset dropdown. Keep unique. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	FName PresetName = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	FContextMenuWidgetDesign Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	FContextMenuWidgetDesign Hovered;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	FContextMenuWidgetDesign Pressed;

	/** Used while the button's Is Enabled is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	FContextMenuWidgetDesign Disabled;

	/** Used while the button's enabled state is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	FContextMenuWidgetDesign Enabled;

	/** How fast targets using this preset blend between states. 0 snaps instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float InterpolationSpeed = 15.0f;
};

// TODO: Put into its own file and refactor so it stops getting copied everywhere
UENUM(BlueprintType)
enum class EDeveloperButtonSettingSource : uint8
{
	UseProjectSettings UMETA(DisplayName = "Use Project Settings"),
	Custom
};

UENUM(BlueprintType)
enum class EImageChoice : uint8
{
	Icon,
	Spacer,
	Divider
};


// Couldn't think of a better name
USTRUCT(BlueprintType)
struct NAMETBD_API FContextButtonImageChoice
{
	GENERATED_BODY()
	
	/* Choosing spacer or divider will place either instead of an image. Spacer and divider configurations are set in the
	 * context menu builder */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	EImageChoice IconChoice = EImageChoice::Icon;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "IconChoice == EImageChoice::Icon", EditConditionHides))
	TSoftObjectPtr<UTexture2D> Icon;
};

USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuButtonData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actions")
	TArray<TSubclassOf<UButtonAction>> Actions; 
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actions")
	TArray<TSubclassOf<UContextButtonPayload>> ExpectedPayloads;
	
	
	////////////////////////////////////////////////////////////////////////
	/// Design 
	////////////////////////////////////////////////////////////////////////
	
	/** Icons will be placed in order from Left->Right of the array */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	TArray<FContextButtonImageChoice>   LeftIcons;
	
	/** Icons will be placed in order from Left->Right of the array */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	TArray<FContextButtonImageChoice>   CenterIcons;
	
	/** Icons will be placed in order from Left->Right of the array */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	TArray<FContextButtonImageChoice>   RightIcons;
	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	FText ButtonText_Left   = FText::GetEmpty();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	FText ButtonText_Center = FText::GetEmpty();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	FText ButtonText_Right  = FText::GetEmpty();
	
	
};

/**
 * Project-wide defaults for the info card system, editable under Project
 * Settings > Game > Info Card. Presets is an open-ended, designer-named list
 * — it starts with one entry called "Default" and grows by adding more
 * FInfoCardPreset rows, each with its own name and its own values for every
 * tunable. Any FInfoCardConfigurableFloat on an InfoCards entry (HoverDelay,
 * KeepOpenSlack, KeepOpenGracePeriod, SuccessiveHoverWindow,
 * AutoDismissDuration) set to UseProjectSettings picks a preset by name and
 * tracks it live: edit a value here and every card pointed at that preset
 * picks it up immediately, with nothing on the individual cards needing to
 * be touched or resynced.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Info Card Settings"))
class NAMETBD_API UInfoCardDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UInfoCardDeveloperSettings();

	/** Convenience accessor: GetDefault<UInfoCardDeveloperSettings>(), named for what it's actually used for at every call site. */
	static const UInfoCardDeveloperSettings* Get();

	/**
	 * Every named preset available to a card's {Use Project Settings, Custom}
	 * dropdown. Seeded with a single "Default" entry the first time this
	 * settings object is created (see the constructor); add, rename, or
	 * remove rows freely from here on — there's no protected/hardcoded slot.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Presets")
	TArray<FInfoCardPreset> Presets;

	/**
	 * Looks up PresetName in Presets and, if found, writes the field Kind
	 * selects into OutValue and returns true. Returns false (OutValue
	 * untouched) if no preset with that name currently exists — the caller
	 * (FInfoCardConfigurableFloat::Resolve) falls back to its own
	 * CustomValue in that case.
	 */
	bool TryGetPresetValue(FName PresetName, EInfoCardConfigurableFloatKind Kind, float& OutValue) const;

	/**
	 * Backs the GetOptions dropdown on FInfoCardConfigurableFloat::PresetName
	 * — every currently-defined preset's name, as the property editor needs
	 * them (TArray<FString>). Must stay a UFUNCTION for GetOptions to be able
	 * to call it.
	 */
	UFUNCTION()
	static TArray<FString> GetPresetNameOptions();

	// ------------------------------------------------------------------
	// Successive Hover — the "Windows tooltip" speedup: once any card has
	// closed, a new Hover-type card triggered again shortly after shows
	// instantly, skipping its own Highlight Delay, on the assumption the
	// player is still "in the middle of" browsing rather than starting a
	// fresh hover. This on/off switch is global and not per-preset — the
	// window LENGTH a card uses while it's on is FInfoCardSpec's own
	// SuccessiveHoverWindow (a FInfoCardConfigurableFloat, resolved the same
	// way as the other three tunables above), so it can still come from a
	// named preset or be authored per-card; only whether the feature runs at
	// all lives here.
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, Config, Category = "Successive Hover", meta = (ToolTip = "If true, a Hover-type card shows immediately, ignoring its own Highlight Delay, when it's triggered within that card's own Successive Hover Window of any card (any anchor) having just closed. Turn off to disable the speedup project-wide regardless of what any card's Successive Hover Window says."))
	bool bEnableSuccessiveHover = true;

	// ------------------------------------------------------------------
	// Dismissal / Placement defaults for FInfoCardConfigurableBool fields.
	// Unlike the float presets above, these are single flat project-wide
	// bools, not per-preset — see FInfoCardConfigurableBool's own comment
	// for why a behavioural on/off switch doesn't have the same "different
	// profile per use case" motivation a numeric tuning value does.
	// ------------------------------------------------------------------

	/** See FInfoCardSpec::DismissClickGetsConsumed. Project-wide default used when that field's Source is UseProjectSettings. */
	UPROPERTY(EditAnywhere, Config, Category = "Dismissal", meta = (ToolTip = "Project-wide default for FInfoCardSpec::DismissClickGetsConsumed when that field is set to Use Project Settings."))
	bool bDismissClickGetsConsumedByDefault = false;

	/** See FInfoCardSpec::FlipToAvoidClipping. Project-wide default used when that field's Source is UseProjectSettings. */
	UPROPERTY(EditAnywhere, Config, Category = "Placement", meta = (ToolTip = "Project-wide default for FInfoCardSpec::FlipToAvoidClipping when that field is set to Use Project Settings."))
	bool bFlipToAvoidClippingByDefault = true;
	
	// ------------------------------------------------------------------
	// Context Menu Button Defaults
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults")
	EExecutePressAction DefaultPressAction = EExecutePressAction::OnRelease;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults")
	EExecuteHoldAction DefaultHoldAction = EExecuteHoldAction::Immediate;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	float DefaultPressDuration = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "5.0"))
	float DefaultActionExecutionFrequency = 0.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float DefaultActionExecutionFrequencyAcceleration = 0.05f;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults", meta = (ClampMin = "0.01", UIMin = "0.01", UIMax = "5.0"))
	float DefaultActionExecutionFrequencyCap = 0.1f;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults")
	TEnumAsByte<EMouseCursor::Type> DefaultHoverCursor = EMouseCursor::Default;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults")
	TEnumAsByte<EMouseCursor::Type> DefaultPressCursor = EMouseCursor::Default;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float DefaultTransformInterpolationSpeed = 15.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults")
	EContextMenuInterpolationCurve DefaultTransformInterpolationCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults", meta = (EditCondition = "DefaultTransformInterpolationCurve == EContextMenuInterpolationCurve::Click", EditConditionHides))
	float DefaultTransformOvershoot = 1.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float DefaultSizeInterpolationSpeed = 15.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults")
	EContextMenuInterpolationCurve DefaultSizeInterpolationCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults", meta = (EditCondition = "DefaultSizeInterpolationCurve == EContextMenuInterpolationCurve::Click", EditConditionHides))
	float DefaultSizeOvershoot = 1.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Context Menu Button Defaults", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float DefaultDesignInterpolationSpeed = 15.0f;

	// ------------------------------------------------------------------
	// Context Menu Button Shape Presets
	// ------------------------------------------------------------------

	/**
	 * Legacy, hidden. Shape presets are Context Menu Shape Preset assets now (Save As Preset on a button). Kept, still read from
	 * the config, only so Shape Targets saved naming one of these get it copied into their own states once, on load. Seeded with
	 * the "Default" entry new targets used to pick (see the constructor).
	 */
	UPROPERTY(Config)
	TArray<FContextMenuShapePreset> ContextMenuShapePresets;

	// ------------------------------------------------------------------
	// Context Menu Button Design Presets
	// ------------------------------------------------------------------

	/**
	 * Legacy, hidden. Design presets are Context Menu Design Preset assets now (Save As Preset on a button). Kept, still read from
	 * the config, only so Design Targets saved naming one of these get it copied into their own states once, on load.
	 */
	UPROPERTY(Config)
	TArray<FContextMenuDesignPreset> ContextMenuDesignPresets;
	
	// ------------------------------------------------------------------
	// Tooltips (Tooltip Card Anchor)
	// ------------------------------------------------------------------

	/**
	 * The card every Tooltip Card Anchor uses unless it picks its own Tooltip Template: an Info Card Data Asset whose first
	 * entry (or the one named by the anchor's Template Card ID) supplies the card class, trigger, delay, placement, sounds
	 * and animations. The anchor only adds its text. Empty: the built-in tooltip - Default Tooltip Card Class above the
	 * widget, after the Highlight Delay preset, with a short fade.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Tooltips", meta = (DisplayName = "Default Tooltip Template"))
	TSoftObjectPtr<UInfoCardDataAsset> DefaultTooltipTemplate;

	/** The card class of the built-in tooltip (no template anywhere). Empty: Text Info Card, which needs no Widget Blueprint. */
	UPROPERTY(EditAnywhere, Config, Category = "Tooltips", meta = (DisplayName = "Default Tooltip Card Class"))
	TSoftClassPtr<UInfoCardWidget> DefaultTooltipCardClass;

	// ------------------------------------------------------------------
	// Context menus (Context Menu Anchor / Context Menu asset)
	// ------------------------------------------------------------------

	/**
	 * The look every right-click menu uses unless its Context Menu asset or its anchor picks another Context Menu Style.
	 * Empty: the built-in style (a dark rounded panel with highlighted rows).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Context Menus", meta = (DisplayName = "Default Context Menu Style"))
	TSoftObjectPtr<UContextMenuStyleAsset> DefaultContextMenuStyle;

	/** Set to BlueprintReadOnly for queries */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Config, Category = "Button Actions", meta = (Categories = "UIContext.Button"))
	TMap<FGameplayTag, FContextMenuButtonData> ButtonActions; 

	/** Returns the shape preset named PresetName, or nullptr if none currently has that name (caller falls back to its own Custom shapes). */
	const FContextMenuShapePreset* FindContextMenuShapePreset(FName PresetName) const;

	/** Returns the design preset named PresetName, or nullptr if none currently has that name (caller falls back to its own Custom designs). */
	const FContextMenuDesignPreset* FindContextMenuDesignPreset(FName PresetName) const;

	/** Backs the GetOptions dropdown on UContextMenuButton::ShapePresetName. Must stay a UFUNCTION. */
	UFUNCTION()
	static TArray<FString> GetContextMenuShapePresetNameOptions();

	/** Backs the GetOptions dropdown on FContextMenuDesignTarget::PresetName. Must stay a UFUNCTION. */
	UFUNCTION()
	static TArray<FString> GetContextMenuDesignPresetNameOptions();
};