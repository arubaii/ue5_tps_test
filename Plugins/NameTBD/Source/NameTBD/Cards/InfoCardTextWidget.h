// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Text/TextLayout.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"

#include "InfoCardTypes.h"
#include "InfoCardWidget.h"

#include "InfoCardTextWidget.generated.h"

class UBorder;
class USizeBox;
class UTextBlock;
class UVerticalBox;

/** How a Text Info Card looks: its panel, padding, width limit and the two lines of text. */
USTRUCT(BlueprintType)
struct NAMETBD_API FInfoCardTextStyle
{
	GENERATED_BODY()

	FInfoCardTextStyle();

	/** The panel behind the text. Rounded Box with an outline by default; any brush works (a texture, a material, a 9-slice box). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Card")
	FSlateBrush Background;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Card")
	FMargin Padding = FMargin(12.f, 8.f);

	/** Text wraps at this width (the panel's padding not included). 0: no limit, one line per paragraph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Card", meta = (ClampMin = "0", UIMax = "800", ForceUnits = "px"))
	float MaxWidth = 320.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Card")
	FSlateFontInfo TitleFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Card")
	FSlateColor TitleColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 1.f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Card")
	FSlateFontInfo BodyFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Card")
	FSlateColor BodyColor = FSlateColor(FLinearColor(0.78f, 0.79f, 0.82f, 1.f));

	/** Space between the title and the body, when both show. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Card", meta = (ClampMin = "0", UIMax = "24", ForceUnits = "px"))
	float LineGap = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Card")
	TEnumAsByte<ETextJustify::Type> Justification = ETextJustify::Left;
};

/**
 * A Text Info Card's look for one card, carried as a payload so an anchor (Tooltip Card Anchor > Text Style) can restyle the
 * card it shows without a card class of its own. A card that isn't a Text Info Card ignores it.
 */
UCLASS(DisplayName = "Info Card Data (Text Style)")
class NAMETBD_API UInfoCardPayload_TextStyle : public UInfoCardPayload
{
	GENERATED_BODY()

public:

	UInfoCardPayload_TextStyle();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Style", meta = (ShowOnlyInnerProperties))
	FInfoCardTextStyle TextStyle;
};

/**
 * The ready-made card for text: a title and a body on a rounded panel, built in C++ so it works with no Widget Blueprint at
 * all. It is what a Tooltip Card Anchor shows by default, and any Info Card entry can use it as its Card Class.
 *
 * Reads Info Card Data (Text) for its words (an empty Title hides the title line; an empty Body hides the body) and Info Card
 * Data (Text Style), when present, for its look; otherwise Text Style below.
 *
 * Make a Widget Blueprint from it to lay it out yourself: the class then builds nothing, and fills whichever of these it
 * finds by name - TitleText and BodyText (Text Blocks), CardBackground (a Border: the Background brush and Padding) and
 * CardWidthBox (a Size Box: Max Width). Animate it as any card: a Widget Animation Component in the Widget Blueprint, or the
 * entry's Additive Animations.
 */
UCLASS(meta = (DisplayName = "Text Info Card"))
class NAMETBD_API UInfoCardTextWidget : public UInfoCardWidget
{
	GENERATED_BODY()

public:

	UInfoCardTextWidget(const FObjectInitializer& ObjectInitializer);

	/** The card's look unless an Info Card Data (Text Style) payload brings another. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Card", meta = (DisplayName = "Text Style"))
	FInfoCardTextStyle TextCardStyle;

	/** Sets the two lines directly (what the text payload does). An empty Title hides the title line. */
	UFUNCTION(BlueprintCallable, Category = "Text Card")
	void SetCardTexts(FText InTitle, FText InBody);

	/** Restyles the card now. */
	UFUNCTION(BlueprintCallable, Category = "Text Card")
	void SetTextCardStyle(const FInfoCardTextStyle& NewTextStyle);

	virtual bool Initialize() override;
	virtual void ApplyPayloads_Implementation(const TArray<UNameTBD_Payload*>& InPayloads) override;

protected:

	UPROPERTY(BlueprintReadOnly, Category = "Text Card", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> CardBackground;

	UPROPERTY(BlueprintReadOnly, Category = "Text Card", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> CardWidthBox;

	UPROPERTY(BlueprintReadOnly, Category = "Text Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Text Card", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BodyText;

	/** The style in use: Text Style, or the payload's. */
	FInfoCardTextStyle ActiveTextStyle;

	/** The built-in layout, for the C++ class itself (never for a Widget Blueprint made from it). */
	void BuildDefaultTree();

	/** Pushes ActiveTextStyle onto whichever parts exist. */
	void ApplyActiveTextStyle();
};
