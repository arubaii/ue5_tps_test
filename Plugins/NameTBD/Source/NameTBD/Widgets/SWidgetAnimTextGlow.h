// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SOverlay;
class SRetainerWidget;
class SWidgetAnimText;
struct FWidgetAnimTextEffects;
struct FWidgetAnimTextEffectLayer;
struct FWidgetAnimTextStyle;

/**
 * The material effects on one side of an animated text - under it (glows) or over it - for <glow> and every tag with
 * Glow ticked. See the note at the top of WidgetAnimTextMarkup.h.
 *
 * Slate can't run a material over what it draws, so each effect is drawn the way a Retainer Box with an effect material
 * draws: a copy of the text in Glow Only mode - only the letters of that effect's tags, each in its glow color, moving
 * exactly as the text's do - rendered into a small render target, which the effect's material turns into the effect.
 * One layer per material, side and set of material parameters the text uses: tags agreeing on all three share one.
 * The text widget builds these for itself: nothing to wrap it in.
 *
 * With no glowing tags it has no layers and costs nothing. Each layer costs one small render target, re-rendered each
 * frame. A layer whose material can't be loaded is simply not drawn.
 *
 * Each layer reaches past the text by twice its radius on every side, so the effect isn't cut off at the text's edges,
 * while taking up exactly the text's own room in the layout.
 */
class NAMETBD_API SWidgetAnimTextGlow : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimTextGlow)
		: _Over(false)
	{}
		/** The text as drawn: which layers its tags ask for. */
		SLATE_ARGUMENT(TSharedPtr<SWidgetAnimText>, Text)

		/** The effects over the text rather than under it. */
		SLATE_ARGUMENT(bool, Over)

		/** What each layer's copy of the text is made with - the same as the text's own. */
		SLATE_ARGUMENT(TFunction<void(FWidgetAnimTextStyle&)>, StyleSource)
		SLATE_ARGUMENT(TSharedPtr<const FWidgetAnimTextEffects>, Effects)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Every layer's copy of the text - for passing on what the text is given (poses, effects, a new style). */
	void ForEachLayerText(TFunctionRef<void(SWidgetAnimText&)> Visit) const;

	// SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:

	/** One effect layer: its copy of the text, the retainer running it through the material, and what was last set. */
	struct FLayer
	{
		uint32 Key = 0;
		TSharedPtr<SWidgetAnimText> Text;
		TSharedPtr<SRetainerWidget> Retainer;
		TSharedPtr<SWidget> Root;
		float Pad = 0.f;
		bool bHasMaterial = false;
		bool bParametersSet = false;
		float AppliedRadius = -1.f;
		float AppliedStrength = -1.f;
	};

	/** Makes the layer for Wanted and puts it in the overlay. */
	void AddLayer(const FWidgetAnimTextEffectLayer& Wanted);

	/** Brings a layer's room and material parameters up to Wanted, at the text's scale on screen. */
	void UpdateLayer(FLayer& Layer, const FWidgetAnimTextEffectLayer& Wanted, float Scale);

	TWeakPtr<SWidgetAnimText> Text;
	bool bOver = false;
	TFunction<void(FWidgetAnimTextStyle&)> StyleSource;
	TSharedPtr<const FWidgetAnimTextEffects> Effects;

	TSharedPtr<SOverlay> Overlay;
	TArray<TSharedRef<FLayer>> Layers;
};
