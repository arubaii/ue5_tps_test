// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/TextBlock.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"
#include "NameTBD/WidgetAnimation/WidgetAnimParts.h"
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextHost.h"
#include "WidgetAnimTextWidgetCore.h"

#include "WidgetAnimTextBlock.generated.h"

class UWidgetAnimationComponent;

/**
 * Animated Text Block: a Text Block that animates itself, letter by letter.
 *
 * It draws each letter on its own, so a track with an Each Letter, Word or Line stagger (see WidgetAnimExpander.h) can
 * fade, drop, pop, turn or shear them in one after another, and every text track works on it in full: Text Reveal types
 * it out without the text re-wrapping as it goes, Count, Scramble, Text Color, Shimmer, Wave, Shake and Letter Spacing
 * all play on it at once and combine.
 *
 * It has a Widget Animation of its own, as an animatable User Widget does: Anim In plays when it is shown, Anim Out when
 * it is hidden. Until it is given entries of its own it plays the project's defaults for Animated Text (Project
 * Settings > Plugins > Widget Animations > Text Widgets) - turn off Use Project Default Animations for one that
 * shouldn't.
 *
 * For dialogue, give its Anim In an entry with a Text Reveal and tick Play When Text Changes: whatever sets its text -
 * a dialogue system, a binding, Set Text - the new line types out. Tags written in the line itself shake, wave, color
 * and pace its words (<shake>no</>, <speed=3>, <pause=0.5>, <event=Portrait>; see WidgetAnimTextMarkup.h and Project
 * Settings > Plugins > Widget Animations > Text Tags). Set Text Reveal Speed speeds one speaker up; On Text Part
 * Revealed blips per letter, On Text Event answers <event> tags, and Finish Text Reveal skips to the end.
 *
 * Everything else is a Text Block's: the same Text, Font, Color and Opacity, Shadow, Justification, Wrapping, Margin,
 * Line Height and Transform Policy, set the same ways - in the designer, or from a Blueprint at runtime, whose changes
 * it picks up the next frame. So it can replace a Text Block anywhere (Replace With in the designer).
 *
 * What it leaves out: overflow ellipsis, strike-through, and wrapping policies beyond breaking at white space. Keep
 * plain Text Blocks for text that never animates: they are cheaper to lay out.
 */
UCLASS(meta = (DisplayName = "Animated Text Block"))
class NAMETBD_API UWidgetAnimTextBlock : public UTextBlock, public IWidgetAnimHost, public IWidgetAnimPartHost, public IWidgetAnimTextHost
{
	GENERATED_BODY()

public:

	UWidgetAnimTextBlock(const FObjectInitializer& ObjectInitializer);

	/** Anim In / Anim Out, their flags and the designer preview, applied to this text. */
	UPROPERTY(EditAnywhere, Instanced, NoClear, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Widget Animation", ShowOnlyInnerProperties))
	TObjectPtr<UWidgetAnimationComponent> WidgetAnimationComponent;

	/**
	 * While its own Anim In and Anim Out are both empty, plays the project's default animations for Animated Text
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

	/**
	 * A Text Reveal has reached an <event=Name> tag in the text - with its name and the index of the part it stands
	 * before. Fires a frame after it is reached, so it may set a new text or play an animation. For a portrait change,
	 * a sound, a camera shake - whatever a line of dialogue asks for at that point.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Text")
	FWidgetAnimTextEvent OnTextEvent;

	/**
	 * Reads text tags in its text - <shake>these words</>, <speed=2>, <pause=0.5> - and draws and paces them (see
	 * Project Settings > Plugins > Widget Animations > Text Tags). Off, a tag is shown as written.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Text", meta = (DisplayName = "Read Text Tags"))
	bool bReadTextTags = true;

	/**
	 * How much faster than authored a Text Reveal paced by its Typing Speed types on this text: 2 twice as fast - a
	 * furious witness - 0.5 half as fast. Takes effect the next time a reveal starts. Multiplies the game's Global Text
	 * Reveal Speed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation|Text")
	void SetTextRevealSpeed(float Speed);

	UFUNCTION(BlueprintPure, Category = "Animation|Text")
	float GetTextRevealSpeed() const;

	/** Its text as drawn: text tags and style tags taken out, upper or lower case applied. */
	UFUNCTION(BlueprintPure, Category = "Animation|Text")
	FText GetTextWithoutTags() const;

	/** Shows the rest of a Text Reveal under way at once, as a dialogue box does when a line is skipped. */
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
