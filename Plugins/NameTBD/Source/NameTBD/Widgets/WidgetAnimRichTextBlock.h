// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/RichTextBlock.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"
#include "NameTBD/WidgetAnimation/WidgetAnimParts.h"
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextHost.h"
#include "WidgetAnimTextWidgetCore.h"

#include "WidgetAnimRichTextBlock.generated.h"

class UWidgetAnimationComponent;

/**
 * Animated Rich Text Block: a Rich Text Block that animates itself, letter by letter - Animated Text Block for rich text.
 *
 * Its text is marked up as a Rich Text Block's is - <Emphasis>this</> in the Emphasis row of its Text Style Set - and
 * each styled run is drawn in its own font and color, sharing a baseline. Every text track and every Each Letter, Word
 * or Line stagger works on it as on an Animated Text Block, counting the letters as drawn, tags left out: a colored
 * word types out in step with the words around it.
 *
 * It has a Widget Animation of its own - Anim In when shown, Anim Out when hidden - playing the project's defaults for
 * Animated Rich Text until it is given entries of its own.
 *
 * Styles only: decorators (inline images, links, anything a Decorator Class draws) are left out, and so is their text.
 * Keep a plain Rich Text Block for text that needs them.
 */
UCLASS(meta = (DisplayName = "Animated Rich Text Block"))
class NAMETBD_API UWidgetAnimRichTextBlock : public URichTextBlock, public IWidgetAnimHost, public IWidgetAnimPartHost, public IWidgetAnimTextHost
{
	GENERATED_BODY()

public:

	UWidgetAnimRichTextBlock(const FObjectInitializer& ObjectInitializer);

	/** Anim In / Anim Out, their flags and the designer preview, applied to this text. */
	UPROPERTY(EditAnywhere, Instanced, NoClear, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Widget Animation", ShowOnlyInnerProperties))
	TObjectPtr<UWidgetAnimationComponent> WidgetAnimationComponent;

	/**
	 * While its own Anim In and Anim Out are both empty, plays the project's default animations for Animated Rich Text
	 * (Project Settings > Plugins > Widget Animations > Text Widgets). Changing a default there changes every text using
	 * it. Off: only its own entries play.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Use Project Default Animations"))
	bool bUseProjectDefaultAnimations = true;

	/**
	 * A part of a Text Reveal has just appeared - a letter, word or line, by the reveal's unit - with its index and its
	 * text. Fires once per part, in order, however many appear in one frame. For a sound per letter, as a dialogue box
	 * blips: skip white space, or every few letters, as suits.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Text")
	FWidgetAnimTextPartRevealed OnTextPartRevealed;

	/** Every part of a Text Reveal has appeared - also when Finish Text Reveal skipped to its end. */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Text")
	FWidgetAnimTextRevealFinished OnTextRevealFinished;

	/** Shows the rest of a Text Reveal under way at once, as a dialogue box does when a line is skipped. */
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
