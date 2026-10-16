// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "InfoCardAnchor.h"
#include "InfoCardTextWidget.h"

#include "TooltipCardAnchor.generated.h"

/**
 * An Info Card Anchor for the everyday case: wrap it around a widget, type the Tooltip Text, done.
 *
 * Everything else comes from a template - Project Settings > Info Card > Tooltips > Default Tooltip Template, or this
 * anchor's own Tooltip Template - and without either from the built-in tooltip: a Text Info Card (no Widget Blueprint
 * needed) above the widget after the project's Highlight Delay, with a short fade, hidden again by any click. Change the
 * template once and every tooltip in the project follows.
 *
 * The usual anchor fields (Source, Info Cards, Payload Defaults, the overrides, Custom Info Cards) are hidden: the card is
 * built from the fields below. The anchor's events, Blueprint functions, pinning and suppression all still apply, with the
 * card's Card ID being the template entry's (Tooltip for the built-in one).
 */
UCLASS(meta = (DisplayName = "Tooltip Card Anchor"))
class NAMETBD_API UTooltipCardAnchor : public UInfoCardAnchor
{
	GENERATED_BODY()

public:

	/** The tooltip's words. Empty Tooltip Text and Tooltip Title: no tooltip at all. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tooltip", meta = (DisplayName = "Tooltip Text", MultiLine = true))
	FText TooltipBodyText;

	/** An optional bold first line. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tooltip", meta = (DisplayName = "Tooltip Title"))
	FText TooltipTitleText;

	/**
	 * Where every setting but the text comes from: an Info Card Data Asset whose entry (Template Card ID, else its first) is
	 * this tooltip's card class, trigger, delay, placement, dismissal, sounds and animations. Empty: Project Settings > Info
	 * Card > Tooltips > Default Tooltip Template, else the built-in tooltip.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tooltip", meta = (DisplayName = "Tooltip Template"))
	TObjectPtr<UInfoCardDataAsset> TooltipTemplate;

	/** Which entry of the template. None: its first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tooltip", AdvancedDisplay, meta = (DisplayName = "Template Card ID", GetOptions = "GetTemplateCardIdOptions"))
	FName TemplateCardId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tooltip", AdvancedDisplay, meta = (InlineEditConditionToggle))
	bool bOverrideTooltipPosition = false;

	/** Where this tooltip sits around the widget, instead of the template's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tooltip", AdvancedDisplay, meta = (DisplayName = "Position", EditCondition = "bOverrideTooltipPosition"))
	EDirection2D TooltipPosition = EDirection2D::Up;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tooltip", AdvancedDisplay, meta = (InlineEditConditionToggle))
	bool bOverrideTooltipDelay = false;

	/** Seconds the cursor (or focus) rests on the widget before this tooltip shows, instead of the template's Highlight Delay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tooltip", AdvancedDisplay, meta = (DisplayName = "Delay", EditCondition = "bOverrideTooltipDelay", ClampMin = "0", UIMax = "3", Units = "s"))
	float TooltipDelay = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tooltip", AdvancedDisplay, meta = (InlineEditConditionToggle))
	bool bOverrideTooltipTextStyle = false;

	/** This tooltip's look, when its card is a Text Info Card (the built-in one is). Other card classes ignore it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tooltip", AdvancedDisplay, meta = (DisplayName = "Text Style", EditCondition = "bOverrideTooltipTextStyle"))
	FInfoCardTextStyle TooltipTextStyle;

	/** Changes the tooltip's text; a tooltip showing updates at once. Empty title and text: no tooltip. */
	UFUNCTION(BlueprintCallable, Category = "Tooltip", meta = (DisplayName = "Set Tooltip Card Text"))
	void SetTooltipCardText(FText NewText, FText NewTitle);

	UFUNCTION(BlueprintPure, Category = "Tooltip", meta = (DisplayName = "Get Tooltip Card Text"))
	FText GetTooltipCardText() const { return TooltipBodyText; }

	UFUNCTION(BlueprintPure, Category = "Tooltip", meta = (DisplayName = "Get Tooltip Card Title"))
	FText GetTooltipCardTitle() const { return TooltipTitleText; }

	/** Backs the Template Card ID dropdown. Must stay a UFUNCTION for GetOptions. */
	UFUNCTION()
	TArray<FString> GetTemplateCardIdOptions() const;

	/** The Card ID this tooltip's card has: the template entry's, or Tooltip. */
	FName GetTooltipCardId() const;

	/** The built-in tooltip's Card ID. */
	static const FName BuiltInTooltipCardId;

	/** The built-in tooltip entry, before text: what a Tooltip Card Anchor uses with no template anywhere. */
	static FInfoCardSpec MakeBuiltInTooltipSpec();

protected:

	virtual bool UsesGeneratedInfoCards() const override { return true; }
	virtual bool BuildGeneratedInfoCards(TArray<FInfoCardSpec>& OutSpecs) override;

	/** The template in force: this anchor's, else the project's (loaded on first use). */
	const UInfoCardDataAsset* ResolveTemplate() const;
};

/** Blueprint helpers for giving widgets info cards and tooltips without setting up an anchor by hand. */
UCLASS()
class NAMETBD_API UInfoCardLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Gives Widget a tooltip from code: if Widget sits inside a Tooltip Card Anchor (at any depth), that anchor's text is set;
	 * otherwise the widget's own engine tooltip becomes a Text Info Card (or Card Class) showing the text - the engine then
	 * shows it on hover after its usual delay, following the cursor. Empty title and text remove the tooltip.
	 *
	 * @return The anchor whose text was set, or null when the engine tooltip was used.
	 */
	UFUNCTION(BlueprintCallable, Category = "Info Card|Tooltip", meta = (DefaultToSelf = "Widget", Keywords = "Hint Help Description"))
	static UTooltipCardAnchor* SetWidgetTooltipCard(UWidget* Widget, FText Text, FText Title, TSubclassOf<UInfoCardWidget> CardClass = nullptr);

	/** The nearest Info Card Anchor around Widget (Widget itself counts), or null. Crosses into the User Widgets that hold it. */
	UFUNCTION(BlueprintPure, Category = "Info Card", meta = (DefaultToSelf = "Widget"))
	static UInfoCardAnchor* FindInfoCardAnchorAround(UWidget* Widget);

	/** The nearest Tooltip Card Anchor around Widget, or null. */
	UFUNCTION(BlueprintPure, Category = "Info Card|Tooltip", meta = (DefaultToSelf = "Widget"))
	static UTooltipCardAnchor* FindTooltipAnchorAround(UWidget* Widget);
};
