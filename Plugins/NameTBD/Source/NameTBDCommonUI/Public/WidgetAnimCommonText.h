// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonDateTimeTextBlock.h"
#include "CommonNumericTextBlock.h"
#include "CommonRichTextBlock.h"
#include "CommonTextBlock.h"
#include "NameTBD/Widgets/WidgetAnimTextWidgetCore.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"
#include "NameTBD/WidgetAnimation/WidgetAnimParts.h"
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextHost.h"

#include "WidgetAnimCommonText.generated.h"

class UWidgetAnimationComponent;

// =====================================================================================================
// Common UI's text widgets, animated
// =====================================================================================================
//
// Each is its Common UI widget - Common Text, Common Rich Text, Common Numeric Text, Common Date Time Text - drawn
// letter by letter as the Animated Text Block is: every text track and every Each Letter, Word or Line stagger works on
// it, it plays its own Anim In and Anim Out, and until it is given entries of its own it plays the project's defaults
// for its kind (Project Settings > Plugins > Widget Animations > Text Widgets). Its Common UI style applies as it
// always does - the font, colors, margin and line height come from its Style class.
//
// What they leave out, with the Animated Text Block's own omissions: Common Text's auto-scrolling of text too long for
// its box, and Common Rich Text's decorators (inline images and the like).

/** Common Animated Text: Common Text, animated letter by letter. See the note above. */
UCLASS(meta = (DisplayName = "Common Animated Text"))
class NAMETBDCOMMONUI_API UCommonAnimatedText : public UCommonTextBlock, public IWidgetAnimHost, public IWidgetAnimPartHost, public IWidgetAnimTextHost
{
	GENERATED_BODY()

public:

	UCommonAnimatedText(const FObjectInitializer& ObjectInitializer);

	/** Anim In / Anim Out, their flags and the designer preview, applied to this text. */
	UPROPERTY(EditAnywhere, Instanced, NoClear, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Widget Animation", ShowOnlyInnerProperties))
	TObjectPtr<UWidgetAnimationComponent> WidgetAnimationComponent;

	/** While its own Anim In and Anim Out are both empty, plays the project's default animations for Animated Text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Use Project Default Animations"))
	bool bUseProjectDefaultAnimations = true;

	/** A part of a Text Reveal has just appeared, with its index and its text. See UWidgetAnimTextBlock. */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Text")
	FWidgetAnimTextPartRevealed OnTextPartRevealed;

	/** Every part of a Text Reveal has appeared. */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Text")
	FWidgetAnimTextRevealFinished OnTextRevealFinished;

	/** Shows the rest of a Text Reveal under way at once. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Text")
	void FinishTextReveal();

	/** Whether a Text Reveal is under way and hasn't shown all of the text yet. */
	UFUNCTION(BlueprintPure, Category = "Animation|Text")
	bool IsTextRevealing() const;

	virtual void SetText(FText InText) override;

	WIDGETANIM_TEXT_WIDGET_BODY()

private:

	FWidgetAnimTextWidgetCore TextCore;
};

/** Common Animated Rich Text: Common Rich Text, animated letter by letter - its styles drawn, its decorators left out. */
UCLASS(meta = (DisplayName = "Common Animated Rich Text"))
class NAMETBDCOMMONUI_API UCommonAnimatedRichText : public UCommonRichTextBlock, public IWidgetAnimHost, public IWidgetAnimPartHost, public IWidgetAnimTextHost
{
	GENERATED_BODY()

public:

	UCommonAnimatedRichText(const FObjectInitializer& ObjectInitializer);

	/** Anim In / Anim Out, their flags and the designer preview, applied to this text. */
	UPROPERTY(EditAnywhere, Instanced, NoClear, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Widget Animation", ShowOnlyInnerProperties))
	TObjectPtr<UWidgetAnimationComponent> WidgetAnimationComponent;

	/** While its own Anim In and Anim Out are both empty, plays the project's default animations for Animated Rich Text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Use Project Default Animations"))
	bool bUseProjectDefaultAnimations = true;

	/** A part of a Text Reveal has just appeared, with its index and its text. See UWidgetAnimTextBlock. */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Text")
	FWidgetAnimTextPartRevealed OnTextPartRevealed;

	/** Every part of a Text Reveal has appeared. */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Text")
	FWidgetAnimTextRevealFinished OnTextRevealFinished;

	/** Shows the rest of a Text Reveal under way at once. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Text")
	void FinishTextReveal();

	/** Whether a Text Reveal is under way and hasn't shown all of the text yet. */
	UFUNCTION(BlueprintPure, Category = "Animation|Text")
	bool IsTextRevealing() const;

	virtual void SetText(const FText& InText) override;

	WIDGETANIM_TEXT_WIDGET_BODY()

private:

	FWidgetAnimTextWidgetCore TextCore;
};

/**
 * Common Animated Numeric Text: Common Numeric Text, animated letter by letter. A Count track on it hands the number to
 * the widget (with Use Widget's Format on), so it counts in the widget's own Numeric Type and formatting - a percentage,
 * seconds, a distance - and Count From Number Shown Now carries on from its current value.
 */
UCLASS(meta = (DisplayName = "Common Animated Numeric Text"))
class NAMETBDCOMMONUI_API UCommonAnimatedNumericText : public UCommonNumericTextBlock, public IWidgetAnimHost, public IWidgetAnimPartHost, public IWidgetAnimTextHost
{
	GENERATED_BODY()

public:

	UCommonAnimatedNumericText(const FObjectInitializer& ObjectInitializer);

	/** Anim In / Anim Out, their flags and the designer preview, applied to this text. */
	UPROPERTY(EditAnywhere, Instanced, NoClear, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Widget Animation", ShowOnlyInnerProperties))
	TObjectPtr<UWidgetAnimationComponent> WidgetAnimationComponent;

	/** While its own Anim In and Anim Out are both empty, plays the project's default animations for Animated Numeric Text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Use Project Default Animations"))
	bool bUseProjectDefaultAnimations = true;

	/** A part of a Text Reveal has just appeared, with its index and its text. See UWidgetAnimTextBlock. */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Text")
	FWidgetAnimTextPartRevealed OnTextPartRevealed;

	/** Every part of a Text Reveal has appeared. */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Text")
	FWidgetAnimTextRevealFinished OnTextRevealFinished;

	/** Shows the rest of a Text Reveal under way at once. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Text")
	void FinishTextReveal();

	/** Whether a Text Reveal is under way and hasn't shown all of the text yet. */
	UFUNCTION(BlueprintPure, Category = "Animation|Text")
	bool IsTextRevealing() const;

	// IWidgetAnimTextHost: the number, for a Count to set and format the widget's own way.
	virtual bool GetWidgetAnimNumber(double& OutValue) const override;
	virtual bool SetWidgetAnimNumber(double Value) override;

	WIDGETANIM_TEXT_WIDGET_BODY()

private:

	FWidgetAnimTextWidgetCore TextCore;
};

/** Common Animated Date Time Text: Common Date Time Text - a clock, a countdown - animated letter by letter. */
UCLASS(meta = (DisplayName = "Common Animated Date Time Text"))
class NAMETBDCOMMONUI_API UCommonAnimatedDateTimeText : public UCommonDateTimeTextBlock, public IWidgetAnimHost, public IWidgetAnimPartHost, public IWidgetAnimTextHost
{
	GENERATED_BODY()

public:

	UCommonAnimatedDateTimeText(const FObjectInitializer& ObjectInitializer);

	/** Anim In / Anim Out, their flags and the designer preview, applied to this text. */
	UPROPERTY(EditAnywhere, Instanced, NoClear, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Widget Animation", ShowOnlyInnerProperties))
	TObjectPtr<UWidgetAnimationComponent> WidgetAnimationComponent;

	/** While its own Anim In and Anim Out are both empty, plays the project's default animations for Animated Date Time Text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Use Project Default Animations"))
	bool bUseProjectDefaultAnimations = true;

	/** A part of a Text Reveal has just appeared, with its index and its text. See UWidgetAnimTextBlock. */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Text")
	FWidgetAnimTextPartRevealed OnTextPartRevealed;

	/** Every part of a Text Reveal has appeared. */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Text")
	FWidgetAnimTextRevealFinished OnTextRevealFinished;

	/** Shows the rest of a Text Reveal under way at once. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Text")
	void FinishTextReveal();

	/** Whether a Text Reveal is under way and hasn't shown all of the text yet. */
	UFUNCTION(BlueprintPure, Category = "Animation|Text")
	bool IsTextRevealing() const;

	WIDGETANIM_TEXT_WIDGET_BODY()

private:

	FWidgetAnimTextWidgetCore TextCore;
};
