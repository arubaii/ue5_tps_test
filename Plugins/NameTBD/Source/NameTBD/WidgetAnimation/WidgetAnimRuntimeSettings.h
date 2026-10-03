// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "UObject/SoftObjectPtr.h"
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextMarkup.h" // FWidgetAnimTextTag
#include "WidgetAnimRuntimeSettings.generated.h"

class UWidgetAnimPresetAsset;
class UMaterialInterface;

/** Which kind of animated text widget a set of project defaults is for. */
UENUM(BlueprintType)
enum class EWidgetAnimTextWidgetKind : uint8
{
	Text UMETA(ToolTip = "Animated Text Block and Common Animated Text."),

	RichText UMETA(DisplayName = "Rich Text", ToolTip = "Animated Rich Text Block and Common Animated Rich Text."),

	Numeric UMETA(ToolTip = "Common Animated Numeric Text."),

	DateTime UMETA(DisplayName = "Date Time", ToolTip = "Common Animated Date Time Text.")
};

/**
 * One default use of a Widget Animation on every text widget of a kind: the animation, its Anim ID and how it plays.
 * Every Layer Group of the animation plays on the text widget itself. Held softly, so project settings never load
 * animations before they are used.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimTextDefaultEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TSoftObjectPtr<UWidgetAnimPresetAsset> Animation;

	/** Plays it by name as well - Play Widget Animation with this ID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Anim ID"))
	FName AnimId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	bool bLoop = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (EditCondition = "bLoop", EditConditionHides))
	bool bPingPong = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (EditCondition = "bLoop", EditConditionHides, ClampMin = "0"))
	int32 LoopCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Invert Animation"))
	bool bInvertAnimation = false;

	/** Also plays backwards as the widget's exit (or entrance, for an Anim Out entry). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Mirror In Out"))
	bool bMirrorInOut = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "0.01", UIMin = "0.1", UIMax = "4.0"))
	float PlaybackSpeed = 1.f;

	/**
	 * Plays it again every time the widget is given a new text while it is on screen - a dialogue line typing out
	 * whenever a dialogue system sets the next one. Anim In only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Play When Text Changes"))
	bool bPlayOnTextChange = false;
};

/** The animations every text widget of one kind plays unless it is given its own. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimTextWidgetDefaults
{
	GENERATED_BODY()

	/** Played as the widget is constructed and shown - a line of text typing out, a number counting up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Anim In"))
	TArray<FWidgetAnimTextDefaultEntry> AnimIn;

	/** Played as the widget is hidden, or by Play Out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Anim Out"))
	TArray<FWidgetAnimTextDefaultEntry> AnimOut;
};

/**
 * Project-wide settings for widget animations, in Project Settings > Plugins > Widget Animations.
 *
 * Project settings rather than Editor Preferences because they are the team's, not one person's: the budget a
 * Blueprint track type has to stay within is a decision about the game, and everyone profiling it should be held to
 * the same one. The editor's own look and feel stays in UWidgetAnimEditorSettings.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Widget Animations"))
class NAMETBD_API UWidgetAnimRuntimeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UWidgetAnimRuntimeSettings();

	static const UWidgetAnimRuntimeSettings& Get() { return *GetDefault<UWidgetAnimRuntimeSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	 * Warn when a Blueprint track type is costing more than it should - averaging over the budget below, or running
	 * Blueprint on nearly every frame it plays. Each type is warned about once per session: in the Output Log, the
	 * PIE message log and an editor notification, and on its tracks' tooltips in the Widget Animations panel.
	 *
	 * Measured in development builds only. A Shipping build never measures, so it never pays for measuring.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Types", meta = (DisplayName = "Warn About Expensive Blueprint Types"))
	bool bWarnAboutExpensiveBlueprintTypes = true;

	/**
	 * How much Blueprint time one Blueprint type may spend per frame, on average across all its tracks, before it is
	 * warned about. The default leaves room for dozens of tracks that only call Blueprint when their value changes.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Types", meta = (EditCondition = "bWarnAboutExpensiveBlueprintTypes", ClampMin = "0.001", UIMin = "0.01", UIMax = "1.0", Units = "ms"))
	float BlueprintTypeBudgetMs = 0.05f;

	/**
	 * The share of frames a Blueprint type may run Blueprint on before it is warned about, whatever those calls cost.
	 * A type that calls Blueprint on nearly every frame has no Quantize Step, or a value that never settles; it is
	 * cheap with one track and expensive with fifty, so it is caught before it gets there. 1 turns this check off.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Types", meta = (EditCondition = "bWarnAboutExpensiveBlueprintTypes", ClampMin = "0.1", ClampMax = "1.0", DisplayName = "Every-Frame Call Threshold"))
	float EveryFrameCallThreshold = 0.9f;

	/**
	 * How long a Blueprint type's cost is averaged over before it is judged. Longer ignores brief spikes - the frame a
	 * menu opens - and takes longer to notice a real problem.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Types", meta = (EditCondition = "bWarnAboutExpensiveBlueprintTypes", ClampMin = "0.25", UIMax = "5.0", Units = "s"))
	float MeasurementWindowSeconds = 1.f;

	// New animations

#if WITH_EDITORONLY_DATA
	/**
	 * The Content folder new Widget Animation assets are saved in - made from an Animation Timeline in the Widget Animations panel,
	 * Save as Animation under a component's tracks, or a builder's Create Widget Animation. Empty saves each beside the
	 * Widget Blueprint it was made in. A builder's own Save Folder, when it sets one, comes first.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "New Animations", meta = (DisplayName = "Default Save Folder", ContentDir))
	FDirectoryPath DefaultSaveFolder;
#endif

	/** Default Save Folder as a long package path ("/Game/UI/Animations") - empty when none is set, and outside the editor. */
	FString GetDefaultSavePackagePath() const;

	/**
	 * A Content folder as typed or picked, as a long package path: "/Game/..." kept, a path relative to Content given
	 * "/Game/", backslashes and trailing slashes tidied. Empty stays empty.
	 */
	static FString NormalizeContentFolder(FString Folder);

	// Text widgets
	//
	// Every animated text widget has a Widget Animation Component of its own. Until it is given Anim In or Anim Out
	// entries of its own, it plays these - live: change them here and every text widget without its own entries plays
	// the new ones the next time it is constructed. A widget with Use Project Default Animations off plays none.

	/** Animated Text Block and Common Animated Text. */
	UPROPERTY(Config, EditAnywhere, Category = "Text Widgets", meta = (DisplayName = "Animated Text"))
	FWidgetAnimTextWidgetDefaults TextDefaults;

	/** Animated Rich Text Block and Common Animated Rich Text. */
	UPROPERTY(Config, EditAnywhere, Category = "Text Widgets", meta = (DisplayName = "Animated Rich Text"))
	FWidgetAnimTextWidgetDefaults RichTextDefaults;

	/** Common Animated Numeric Text. */
	UPROPERTY(Config, EditAnywhere, Category = "Text Widgets", meta = (DisplayName = "Animated Numeric Text"))
	FWidgetAnimTextWidgetDefaults NumericTextDefaults;

	/** Common Animated Date Time Text. */
	UPROPERTY(Config, EditAnywhere, Category = "Text Widgets", meta = (DisplayName = "Animated Date Time Text"))
	FWidgetAnimTextWidgetDefaults DateTimeTextDefaults;

	// Text tags
	//
	// The effect tags every animated text widget reads in its text - <shake>these words</> - and what each one does.
	// See WidgetAnimTextMarkup.h. A new project starts with a library of them; edit them, add your own, or remove any.

	/**
	 * Every effect tag a text can use, by the name written in the text. Built-in tags - speed, instant, pause, event,
	 * play, color, size and glow - are always there and need no entry.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Text Tags", meta = (TitleProperty = "Tag"))
	TArray<FWidgetAnimTextTag> TextTags;

	/**
	 * The material that turns <glow> - and every tag with Glow and no Glow Material of its own - into a soft glow behind
	 * its letters. A User Interface material: its Texture Object Parameter "Texture" receives the glowing letters drawn in
	 * their glow color, and its scalar parameters "Radius" (in pixels) and "TagStrength" are set from the text's tags,
	 * with any parameters the tag names. It outputs the effect alone - the letters are drawn over (or under) it. Empty,
	 * or missing, and those tags simply don't glow.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Text Tags", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	TSoftObjectPtr<UMaterialInterface> TextGlowMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/NameTBD/Materials/M_WidgetAnimTextGlow.M_WidgetAnimTextGlow")));

	/** What plain <glow> starts from - its attributes change it per use: <glow=cyan r=10 a=2 p=0.4 s=2>. */
	UPROPERTY(Config, EditAnywhere, Category = "Text Tags", meta = (DisplayName = "Glow Defaults"))
	FWidgetAnimTextGlowDefaults TextGlowDefaults;

	/** The defaults for one kind of text widget. */
	const FWidgetAnimTextWidgetDefaults& GetTextWidgetDefaults(EWidgetAnimTextWidgetKind Kind) const
	{
		switch (Kind)
		{
		case EWidgetAnimTextWidgetKind::RichText: return RichTextDefaults;
		case EWidgetAnimTextWidgetKind::Numeric:  return NumericTextDefaults;
		case EWidgetAnimTextWidgetKind::DateTime: return DateTimeTextDefaults;
		case EWidgetAnimTextWidgetKind::Text:
		default:                                  return TextDefaults;
		}
	}
};