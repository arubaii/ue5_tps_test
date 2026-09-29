// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Fonts/ShapedTextFwd.h"
#include "Framework/Text/TextLayout.h"
#include "Layout/Margin.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimParts.h"
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextHost.h"

/** How one stretch of a text is drawn: a rich text's style, or a plain text's only style. */
struct NAMETBD_API FWidgetAnimTextRunStyle
{
	/** Where it is in the drawn string, [Begin, End). */
	int32 Begin = 0;
	int32 End = 0;

	FSlateFontInfo Font;
	FSlateColor Color = FSlateColor(FLinearColor::White);
	FVector2D ShadowOffset = FVector2D(1.0, 1.0);
	FLinearColor ShadowColor = FLinearColor::Transparent;

	/** Whether Other would lay out differently - another range or font. */
	bool ChangesLayout(const FWidgetAnimTextRunStyle& Other) const;

	/** Whether Other would only draw differently - another color or shadow. */
	bool ChangesPaint(const FWidgetAnimTextRunStyle& Other) const;
};

/**
 * Everything an SWidgetAnimText draws: its string and how it looks. Read off the widget that owns it - the same
 * settings a Text Block or Rich Text Block has.
 */
struct NAMETBD_API FWidgetAnimTextStyle
{
	/** The string as drawn: style tags taken out, Transform Policy (upper or lower case) applied. */
	FString Text;

	/**
	 * Styled stretches of Text, covering all of it in order - a rich text's runs. Empty: all of it in the default style
	 * below.
	 */
	TArray<FWidgetAnimTextRunStyle> Runs;

	/** The default style: a plain text's only one. */
	FWidgetAnimTextRunStyle Default;

	ETextJustify::Type Justification = ETextJustify::Left;
	bool bAutoWrap = false;
	float WrapTextAt = 0.f;
	FMargin Margin;
	float LineHeight = 1.f;
	float MinDesiredWidth = 0.f;

	/** Every stretch of Text with its style: Runs, or all of Text in Default. */
	void GetResolvedRuns(TArray<FWidgetAnimTextRunStyle>& OutRuns) const;

	/** Whether Other would be laid out differently: other lines, other letter positions, another size. */
	bool ChangesLayout(const FWidgetAnimTextStyle& Other) const;

	/** Whether Other would only be drawn differently, in the same place: a color, a shadow, the alignment of lines. */
	bool ChangesPaint(const FWidgetAnimTextStyle& Other) const;
};

/**
 * The Slate side of every animated text widget: a text - plain or in styled runs - drawn letter by letter, so each
 * letter, word and line can be posed on its own about its own middle, and the text tracks' effects (see
 * WidgetAnimTextHost.h) can reveal, scramble, color, wave, shake and space letters without the text around them moving.
 *
 * It lays its text out itself: each paragraph's runs shaped once, lines broken at white space to fit its width (or Wrap
 * Text At), a word too long for a line broken between letters, runs of different sizes sharing a baseline, lines
 * aligned by Justification. Nothing is laid out again while it animates. With no effect set and nothing posed, each
 * line is drawn whole, as cheaply as a Text Block draws it.
 *
 * Not done: inline decorators (images) in rich text, overflow ellipsis, strike-through, the finer wrapping policies.
 * Right-to-left text is drawn correctly while nothing moves its letters; animated letter by letter, it is placed in
 * reading order, left to right.
 */
class NAMETBD_API SWidgetAnimText : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimText) {}
		/**
		 * Asked every frame for the owner's current text and style, so a change made any way - a Blueprint setting a
		 * color, a binding, a Common UI style - is picked up without the owner having to push it.
		 */
		SLATE_ARGUMENT(TFunction<void(FWidgetAnimTextStyle&)>, StyleSource)

		/** The effects the text tracks write, shared with the owner. May be null. */
		SLATE_ARGUMENT(TSharedPtr<const FWidgetAnimTextEffects>, Effects)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Takes a new text and style now, laying out again only if they need it. */
	void SetStyle(const FWidgetAnimTextStyle& InStyle);

	/** The effects have changed: draws them. */
	void NotifyEffectsChanged();

	/** Every part's pose for this frame, replacing the last frame's. See IWidgetAnimPartHost::SetWidgetAnimPartPoses. */
	void SetPartPoses(TConstArrayView<FWidgetAnimPartPose> InPoses);

	/** Every part back at rest. */
	void ClearPartPoses();

	/** How many lines the text is laid out on at its current width. */
	int32 GetLineCount() const;

	/** Where line LineIndex is in the drawn string, [OutBegin, OutEnd). False past the last line. */
	bool GetLineRange(int32 LineIndex, int32& OutBegin, int32& OutEnd) const;

	/** Whether it has been given a width yet - before that, a wrapping text's lines are only a guess. */
	bool HasBeenArranged() const { return bHasBeenArranged; }

	// SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual bool ComputeVolatility() const override;

private:

	/**
	 * The text laid out at one scale and width, in pixels at that scale. Two are kept - one measured at the layout
	 * scale for the desired size, one at the scale it is drawn at - so neither lays the text out again every frame.
	 */
	struct FLayout
	{
		float Scale = 0.f;
		float WrapWidth = -1.f;
		uint32 Version = 0;
		bool bValid = false;
		bool bWithGlyphs = false;

		/** The widest line, and every line's height together. */
		FVector2f Size = FVector2f::ZeroVector;

		/** One run's glyphs on one line, where they start on it and how wide they are. */
		struct FPiece
		{
			int32 Run = 0;
			float X = 0.f;
			float Width = 0.f;
			float Top = 0.f;
			float Height = 0.f;
			FShapedGlyphSequencePtr Glyphs;
		};

		struct FLine
		{
			int32 Begin = 0;
			int32 End = 0;
			float Width = 0.f;
			float Top = 0.f;
			float Height = 0.f;
			TArray<FPiece> Pieces;
			int32 NumLetters = 0;
		};
		TArray<FLine> Lines;

		struct FLetter
		{
			int32 Line = 0;
			int32 Run = 0;
			/** Its place among its line's letters - what Letter Spacing pushes it by. */
			int32 InLine = 0;
			float X = 0.f;
			float Width = 0.f;
			float Top = 0.f;
			float Height = 0.f;
			FShapedGlyphSequencePtr Glyphs;
		};
		TArray<FLetter> Letters;

		/** Scramble's stand-in glyphs, shaped as needed: by character and run. */
		TMap<TPair<TCHAR, int32>, FShapedGlyphSequencePtr> StandIns;
	};

	void EnsureLayout(FLayout& Layout, float Scale, float WrapWidth, bool bWithGlyphs) const;
	float GetWrapWidth() const;
	void RebuildParts();
	void RebuildPoseIndex() const;

	/** Scramble's stand-in for letter LetterIndex, shaped with its run's font at the layout's scale. */
	FShapedGlyphSequencePtr GetStandIn(FLayout& Layout, int32 LetterIndex) const;

	TFunction<void(FWidgetAnimTextStyle&)> StyleSource;
	FWidgetAnimTextStyle Style;
	TArray<FWidgetAnimTextRunStyle> ResolvedRuns;
	TSharedPtr<const FWidgetAnimTextEffects> Effects;

	uint32 LayoutVersion = 1;
	mutable FLayout MeasureLayout;
	mutable FLayout PaintLayout;

	TArray<WidgetAnimTextParts::FRange> Letters;
	TArray<int32> WordOfLetter;
	int32 NumWords = 0;

	TArray<FWidgetAnimPartPose> Poses;
	mutable bool bPoseIndexDirty = true;
	mutable TArray<int32> LetterPose;
	mutable TArray<int32> WordPose;
	mutable TArray<int32> LinePose;

	float ArrangedWidth = -1.f;
	bool bHasBeenArranged = false;
	bool bWasVolatile = false;

	/** Scramble's seed for the frame being drawn: its own seed moved on by its rate. Set as painting starts. */
	mutable uint32 ScrambleSeedNow = 0;
};
