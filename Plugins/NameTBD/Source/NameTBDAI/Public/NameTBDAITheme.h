// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "NameTBDAITheme.generated.h"

class UObject;

/** One set of colors the AI Assistant builds with: what a card, a title, a glow or a button is painted in. */
USTRUCT(BlueprintType)
struct NAMETBDAI_API FNameTBDAIPalette
{
	GENERATED_BODY()

	/** Behind everything: a screen's backdrop. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Background = FLinearColor::Black;

	/** Cards, panels and dialogs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Surface = FLinearColor::Black;

	/** A surface on a surface: a row in a card, a field, a hovered item. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor SurfaceRaised = FLinearColor::Black;

	/** Hairline outlines and dividers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Outline = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Text = FLinearColor::White;

	/** Secondary text: captions, hints, values beside a label. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor TextMuted = FLinearColor::Gray;

	/** The one color that says "act here": primary buttons, the selected item, progress. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Accent = FLinearColor::White;

	/** A second accent for contrast with the first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor AccentAlt = FLinearColor::White;

	/** Glow colors, in order, for auroras, soft glows and particles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Glow1 = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Glow2 = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Glow3 = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Success = FLinearColor::Green;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Warning = FLinearColor::Yellow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Danger = FLinearColor::Red;
};

/** Font sizes, from the largest. */
USTRUCT(BlueprintType)
struct NAMETBDAI_API FNameTBDAITypeScale
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type", meta = (ClampMin = "4"))
	int32 Display = 44;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type", meta = (ClampMin = "4"))
	int32 Title = 32;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type", meta = (ClampMin = "4"))
	int32 Heading = 24;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type", meta = (ClampMin = "4"))
	int32 Body = 16;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type", meta = (ClampMin = "4"))
	int32 Caption = 13;

	/** The font every role uses. None: the engine's default (Roboto). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type", meta = (AllowedClasses = "/Script/Engine.Font"))
	TObjectPtr<UObject> Font = nullptr;

	/** The typeface names of Font used for strong (titles) and regular (body) text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	FName StrongTypeface = TEXT("Bold");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	FName RegularTypeface = TEXT("Regular");
};

/**
 * The AI Assistant's house style: palettes, type scale, spacing, corner rounding and motion. Every tool that builds or styles
 * reads it, so a first result already looks finished and every result looks like the others. The model picks intent - "card",
 * "title", "accent", "soft glow" - and the theme turns it into values.
 *
 * Make one (Content Browser > Miscellaneous > Data Asset > NameTBD AI Theme) and pick it in Editor Preferences > Plugins >
 * NameTBD AI > Theme to make it yours; without one the built-in defaults below are used.
 */
UCLASS(BlueprintType, meta = (DisplayName = "NameTBD AI Theme"))
class NAMETBDAI_API UNameTBDAITheme : public UDataAsset
{
	GENERATED_BODY()

public:

	UNameTBDAITheme();

	/** Which palette is used when a request doesn't say. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	bool bDarkByDefault = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FNameTBDAIPalette Dark;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FNameTBDAIPalette Light;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Type")
	FNameTBDAITypeScale Type;

	/** The spacing scale, in Slate units, from the smallest: paddings and gaps are picked from it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spacing")
	TArray<float> Spacing;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Corners", meta = (ClampMin = "0"))
	float RadiusSmall = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Corners", meta = (ClampMin = "0"))
	float RadiusMedium = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Corners", meta = (ClampMin = "0"))
	float RadiusLarge = 24.f;

	/** The ready-made buttons' Motion every button gets: Smooth, Snappy, Bouncy, Gentle or Instant. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	FName ButtonMotion = TEXT("Smooth");

	/** How long an entrance takes, and the gap between items entering one after another. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (ClampMin = "0", Units = "s"))
	float EntranceDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (ClampMin = "0", Units = "s"))
	float Stagger = 0.06f;

	/** A spring preset name (Smooth, Quick, Snappy, Gentle, Bouncy...) springs default to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	FName SpringPreset = TEXT("Smooth");

	/** The size a new card or panel gets when none is asked for. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	FVector2D CardSize = FVector2D(600.0, 360.0);

	/** The theme in use: the one picked in the settings, else this class's defaults. Never null. */
	static const UNameTBDAITheme& Get();

	const FNameTBDAIPalette& GetPalette(bool bDark) const { return bDark ? Dark : Light; }

	/** A spacing step by index, held to the scale (0 = the smallest). */
	float GetSpacing(int32 Step) const;
};
