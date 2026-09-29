// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimText.h"

#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Slate/WidgetTransform.h"

// =====================================================================================================
// Styles
// =====================================================================================================

bool FWidgetAnimTextRunStyle::ChangesLayout(const FWidgetAnimTextRunStyle& Other) const
{
	return Begin != Other.Begin || End != Other.End || !Font.IsIdenticalTo(Other.Font);
}

bool FWidgetAnimTextRunStyle::ChangesPaint(const FWidgetAnimTextRunStyle& Other) const
{
	return Color != Other.Color || ShadowOffset != Other.ShadowOffset || ShadowColor != Other.ShadowColor;
}

void FWidgetAnimTextStyle::GetResolvedRuns(TArray<FWidgetAnimTextRunStyle>& OutRuns) const
{
	OutRuns.Reset();
	if (Runs.Num() == 0)
	{
		FWidgetAnimTextRunStyle& Whole = OutRuns.Add_GetRef(Default);
		Whole.Begin = 0;
		Whole.End = Text.Len();
		return;
	}

	// Covering the whole string, in order, whatever was handed over: gaps take the default style.
	int32 Cursor = 0;
	for (const FWidgetAnimTextRunStyle& Run : Runs)
	{
		const int32 Begin = FMath::Clamp(Run.Begin, Cursor, Text.Len());
		const int32 End = FMath::Clamp(Run.End, Begin, Text.Len());
		if (Begin > Cursor)
		{
			FWidgetAnimTextRunStyle& Gap = OutRuns.Add_GetRef(Default);
			Gap.Begin = Cursor;
			Gap.End = Begin;
		}
		if (End > Begin)
		{
			FWidgetAnimTextRunStyle& Copy = OutRuns.Add_GetRef(Run);
			Copy.Begin = Begin;
			Copy.End = End;
		}
		Cursor = FMath::Max(Cursor, End);
	}
	if (Cursor < Text.Len() || OutRuns.Num() == 0)
	{
		FWidgetAnimTextRunStyle& Tail = OutRuns.Add_GetRef(Default);
		Tail.Begin = Cursor;
		Tail.End = Text.Len();
	}
}

bool FWidgetAnimTextStyle::ChangesLayout(const FWidgetAnimTextStyle& Other) const
{
	if (!Text.Equals(Other.Text, ESearchCase::CaseSensitive)
		|| Default.ChangesLayout(Other.Default)
		|| Runs.Num() != Other.Runs.Num()
		|| bAutoWrap != Other.bAutoWrap
		|| WrapTextAt != Other.WrapTextAt
		|| Margin != Other.Margin
		|| LineHeight != Other.LineHeight
		|| MinDesiredWidth != Other.MinDesiredWidth)
	{
		return true;
	}
	for (int32 Index = 0; Index < Runs.Num(); ++Index)
	{
		if (Runs[Index].ChangesLayout(Other.Runs[Index]))
		{
			return true;
		}
	}
	return false;
}

bool FWidgetAnimTextStyle::ChangesPaint(const FWidgetAnimTextStyle& Other) const
{
	if (Default.ChangesPaint(Other.Default) || Justification != Other.Justification)
	{
		return true;
	}
	for (int32 Index = 0; Index < Runs.Num() && Index < Other.Runs.Num(); ++Index)
	{
		if (Runs[Index].ChangesPaint(Other.Runs[Index]))
		{
			return true;
		}
	}
	return false;
}

// =====================================================================================================
// Helpers
// =====================================================================================================

namespace WidgetAnimTextDraw
{
	/**
	 * A pose as a render transform about Pivot, in the text's own units - the same transform a widget's Render
	 * Transform makes of the same values, turned about the part's middle instead of the widget's pivot.
	 */
	FSlateRenderTransform PoseAbout(const FVector2D& Translation, FVector2D Scale, const FVector2D& Shear, float Angle, const FVector2f& Pivot)
	{
		// Never quite 0 on an axis: a letter scaled to nothing together with a turn has no inverse, and Slate draws
		// that frame with a garbage one. See MakeInvertibleScale in the library.
		constexpr double MinScale = 0.0001;
		Scale.X = FMath::Abs(Scale.X) < MinScale ? (Scale.X < 0.0 ? -MinScale : MinScale) : Scale.X;
		Scale.Y = FMath::Abs(Scale.Y) < MinScale ? (Scale.Y < 0.0 ? -MinScale : MinScale) : Scale.Y;

		const FWidgetTransform Transform(Translation, Scale, Shear, Angle);
		const FSlateRenderTransform AtOrigin = Concatenate(FSlateRenderTransform(-Pivot), Transform.ToSlateRenderTransform());
		return Concatenate(AtOrigin, FSlateRenderTransform(Pivot));
	}

	FSlateRenderTransform PoseAbout(const FWidgetAnimPartPose& Pose, const FVector2f& Pivot)
	{
		return PoseAbout(Pose.Translation, Pose.Scale, Pose.Shear, Pose.Angle, Pivot);
	}

	/** A range of glyphs' measured width - 0 for nothing. */
	float MeasureRange(const FShapedGlyphSequence& Glyphs, int32 Begin, int32 End)
	{
		if (End <= Begin)
		{
			return 0.f;
		}
		const auto Width = Glyphs.GetMeasuredWidth(Begin, End, /*InIncludeKerningWithPrecedingGlyph=*/true);
		return Width.IsSet() ? static_cast<float>(Width.GetValue()) : 0.f;
	}

	/** A number in [-1, 1] picked by Index and Seed, the same every time for the same two. */
	float Random(int32 Index, uint32 Seed, uint32 Salt)
	{
		const uint32 Hash = HashCombineFast(HashCombineFast(GetTypeHash(Index), Seed), Salt);
		// A final mix, so neighbouring indices don't give neighbouring numbers.
		uint32 Mixed = Hash * 0x9E3779B1u;
		Mixed ^= Mixed >> 15;
		Mixed *= 0x85EBCA77u;
		Mixed ^= Mixed >> 13;
		return (static_cast<float>(Mixed & 0xFFFFu) / 65535.f) * 2.f - 1.f;
	}
}

// =====================================================================================================
// SWidgetAnimText
// =====================================================================================================

void SWidgetAnimText::Construct(const FArguments& InArgs)
{
	StyleSource = InArgs._StyleSource;
	Effects = InArgs._Effects;

	if (StyleSource)
	{
		FWidgetAnimTextStyle Initial;
		StyleSource(Initial);
		SetStyle(Initial);
	}

	// Ticks to notice its owner's changes - see StyleSource - and to learn the width it wraps at.
	SetCanTick(true);
}

void SWidgetAnimText::SetStyle(const FWidgetAnimTextStyle& InStyle)
{
	const bool bLayout = Style.ChangesLayout(InStyle);
	const bool bPaint = bLayout || Style.ChangesPaint(InStyle);
	if (!bPaint)
	{
		return;
	}

	const bool bTextChanged = !Style.Text.Equals(InStyle.Text, ESearchCase::CaseSensitive);
	Style = InStyle;
	Style.GetResolvedRuns(ResolvedRuns);

	if (bTextChanged)
	{
		RebuildParts();
	}

	if (bLayout)
	{
		++LayoutVersion;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
	else
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SWidgetAnimText::NotifyEffectsChanged()
{
	// Only what moves on its own - a blinking cursor, a traveling wave, a shake or scramble keeping its own time - needs
	// a frame every frame; everything else is drawn when a track changes it.
	const bool bVolatile = ComputeVolatility();
	if (bVolatile != bWasVolatile)
	{
		bWasVolatile = bVolatile;
		Invalidate(EInvalidateWidgetReason::PaintAndVolatility);
	}
	else
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

bool SWidgetAnimText::ComputeVolatility() const
{
	return Effects.IsValid() && Effects->IsLive();
}

void SWidgetAnimText::RebuildParts()
{
	WidgetAnimTextParts::GetLetters(Style.Text, Letters);

	TArray<WidgetAnimTextParts::FRange> Words;
	WidgetAnimTextParts::GetWords(Letters, Words, &WordOfLetter);
	NumWords = Words.Num();

	bPoseIndexDirty = true;
}

void SWidgetAnimText::SetPartPoses(TConstArrayView<FWidgetAnimPartPose> InPoses)
{
	// Nothing to redraw when nothing was posed and nothing is: the frames a staggered track is at rest.
	if (InPoses.Num() == 0 && Poses.Num() == 0)
	{
		return;
	}

	Poses.Reset(InPoses.Num());
	for (const FWidgetAnimPartPose& Pose : InPoses)
	{
		// A part at rest needs no pose; leaving it out keeps the cheap whole-line drawing for as long as it can.
		if (!Pose.IsIdentity())
		{
			Poses.Add(Pose);
		}
	}

	bPoseIndexDirty = true;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SWidgetAnimText::ClearPartPoses()
{
	SetPartPoses(TConstArrayView<FWidgetAnimPartPose>());
}

int32 SWidgetAnimText::GetLineCount() const
{
	// Laid out at the scale it was last drawn at if it has been drawn, else at 1 - the number of lines is the same at
	// any scale unless a line is right at its edge.
	FLayout& Layout = PaintLayout.bValid ? PaintLayout : MeasureLayout;
	EnsureLayout(Layout, Layout.bValid ? Layout.Scale : 1.f, GetWrapWidth(), Layout.bWithGlyphs);
	return Layout.Lines.Num();
}

bool SWidgetAnimText::GetLineRange(int32 LineIndex, int32& OutBegin, int32& OutEnd) const
{
	FLayout& Layout = PaintLayout.bValid ? PaintLayout : MeasureLayout;
	EnsureLayout(Layout, Layout.bValid ? Layout.Scale : 1.f, GetWrapWidth(), Layout.bWithGlyphs);
	if (!Layout.Lines.IsValidIndex(LineIndex))
	{
		return false;
	}
	OutBegin = Layout.Lines[LineIndex].Begin;
	OutEnd = Layout.Lines[LineIndex].End;
	return true;
}

float SWidgetAnimText::GetWrapWidth() const
{
	if (Style.WrapTextAt > 0.f)
	{
		return Style.WrapTextAt;
	}

	if (Style.bAutoWrap && ArrangedWidth > 0.f)
	{
		return FMath::Max(ArrangedWidth - Style.Margin.Left - Style.Margin.Right, 1.f);
	}

	return 0.f;
}

void SWidgetAnimText::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (StyleSource)
	{
		FWidgetAnimTextStyle Current;
		StyleSource(Current);
		SetStyle(Current);
	}

	// The width it is given decides where an Auto Wrap Text breaks its lines - known only once it has been arranged.
	const float Width = AllottedGeometry.GetLocalSize().X;
	if (Width != ArrangedWidth)
	{
		ArrangedWidth = Width;
		if (Style.bAutoWrap && Style.WrapTextAt <= 0.f)
		{
			Invalidate(EInvalidateWidgetReason::Layout);
		}
	}
	bHasBeenArranged = bHasBeenArranged || Width > 0.f;
}

void SWidgetAnimText::EnsureLayout(FLayout& Layout, float Scale, float WrapWidth, bool bWithGlyphs) const
{
	Scale = FMath::Max(Scale, KINDA_SMALL_NUMBER);
	if (Layout.bValid && Layout.Version == LayoutVersion && Layout.Scale == Scale && Layout.WrapWidth == WrapWidth && (Layout.bWithGlyphs || !bWithGlyphs))
	{
		return;
	}

	Layout = FLayout();
	Layout.Scale = Scale;
	Layout.WrapWidth = WrapWidth;
	Layout.Version = LayoutVersion;
	Layout.bWithGlyphs = bWithGlyphs;
	Layout.bValid = true;

	if (!FSlateApplication::IsInitialized() || ResolvedRuns.Num() == 0)
	{
		return;
	}

	const TSharedRef<FSlateFontCache> FontCache = FSlateApplication::Get().GetRenderer()->GetFontCache();
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float WrapPixels = WrapWidth > 0.f ? WrapWidth * Scale : 0.f;
	const float LinePitchScale = FMath::Max(Style.LineHeight, 0.f);

	// How far above and below its baseline each run's font reaches, so runs of different sizes on one line share a
	// baseline, and each line is as tall as its tallest run.
	TArray<TPair<float, float>, TInlineAllocator<8>> RunExtents;
	RunExtents.Reserve(ResolvedRuns.Num());
	for (const FWidgetAnimTextRunStyle& Run : ResolvedRuns)
	{
		const float Height = static_cast<float>(FontMeasure->GetMaxCharacterHeight(Run.Font, Scale));
		const float Baseline = static_cast<float>(FontMeasure->GetBaseline(Run.Font, Scale));
		RunExtents.Add(TPair<float, float>(Height + Baseline, -Baseline));
	}
	auto RunAt = [this](int32 Index)
	{
		for (int32 RunIndex = 0; RunIndex < ResolvedRuns.Num(); ++RunIndex)
		{
			if (Index < ResolvedRuns[RunIndex].End || RunIndex == ResolvedRuns.Num() - 1)
			{
				return RunIndex;
			}
		}
		return 0;
	};

	Layout.Letters.SetNum(Letters.Num());
	int32 NextLetter = 0;
	float LineTop = 0.f;

	const FString& Text = Style.Text;
	int32 ParagraphBegin = 0;
	while (ParagraphBegin <= Text.Len())
	{
		// One paragraph: up to the next line break, a "\r\n" counting as one.
		int32 ParagraphEnd = ParagraphBegin;
		while (ParagraphEnd < Text.Len() && Text[ParagraphEnd] != TEXT('\n'))
		{
			++ParagraphEnd;
		}
		const int32 NextParagraph = ParagraphEnd + 1;
		if (ParagraphEnd > ParagraphBegin && Text[ParagraphEnd - 1] == TEXT('\r'))
		{
			--ParagraphEnd;
		}

		// Each run's stretch of the paragraph, shaped once, at the scale it is drawn at. Indices are the string's own.
		struct FShaped
		{
			int32 Begin = 0;
			int32 End = 0;
			int32 Run = 0;
			FShapedGlyphSequencePtr Glyphs;
		};
		TArray<FShaped, TInlineAllocator<4>> Shaped;
		for (int32 RunIndex = 0; RunIndex < ResolvedRuns.Num(); ++RunIndex)
		{
			const int32 Begin = FMath::Max(ResolvedRuns[RunIndex].Begin, ParagraphBegin);
			const int32 End = FMath::Min(ResolvedRuns[RunIndex].End, ParagraphEnd);
			if (End > Begin)
			{
				FShaped& Piece = Shaped.AddDefaulted_GetRef();
				Piece.Begin = Begin;
				Piece.End = End;
				Piece.Run = RunIndex;
				Piece.Glyphs = FontCache->ShapeBidirectionalText(Text.Mid(Begin, End - Begin), ResolvedRuns[RunIndex].Font, Scale, TextBiDi::ETextDirection::LeftToRight, ETextShapingMethod::Auto);
			}
		}

		auto Measure = [&Shaped](int32 Begin, int32 End)
		{
			float Width = 0.f;
			for (const FShaped& Piece : Shaped)
			{
				const int32 Low = FMath::Max(Begin, Piece.Begin);
				const int32 High = FMath::Min(End, Piece.End);
				if (High > Low)
				{
					Width += WidgetAnimTextDraw::MeasureRange(*Piece.Glyphs, Low - Piece.Begin, High - Piece.Begin);
				}
			}
			return Width;
		};

		// Lines: greedily as many words as fit, broken at white space; a word too long for a line on its own broken
		// between characters. Spaces at the end of a line hang past it and aren't counted in its width.
		TArray<TPair<int32, int32>, TInlineAllocator<8>> ParagraphLines;
		{
			int32 LineBegin = ParagraphBegin;
			int32 LineEnd = ParagraphBegin;
			bool bLineHasWord = false;
			int32 Cursor = ParagraphBegin;

			auto SplitOverlong = [&](int32& Begin, int32 End)
			{
				// Characters from Begin that fit, at least one - and never half of a surrogate pair.
				while (WrapPixels > 0.f && End - Begin > 1 && Measure(Begin, End) > WrapPixels)
				{
					int32 Cut = Begin + 1;
					while (Cut < End && Measure(Begin, Cut + 1) <= WrapPixels)
					{
						++Cut;
					}
					if (Cut < End && Text[Cut] >= 0xDC00 && Text[Cut] <= 0xDFFF && Cut > Begin + 1)
					{
						--Cut;
					}
					ParagraphLines.Add(TPair<int32, int32>(Begin, Cut));
					Begin = Cut;
				}
			};

			while (Cursor < ParagraphEnd)
			{
				while (Cursor < ParagraphEnd && FChar::IsWhitespace(Text[Cursor]))
				{
					++Cursor;
				}
				if (Cursor >= ParagraphEnd)
				{
					break;
				}
				const int32 WordBegin = Cursor;
				while (Cursor < ParagraphEnd && !FChar::IsWhitespace(Text[Cursor]))
				{
					++Cursor;
				}
				const int32 WordEnd = Cursor;

				if (!bLineHasWord || WrapPixels <= 0.f || Measure(LineBegin, WordEnd) <= WrapPixels)
				{
					LineEnd = WordEnd;
					bLineHasWord = true;
				}
				else
				{
					ParagraphLines.Add(TPair<int32, int32>(LineBegin, LineEnd));
					LineBegin = WordBegin;
					LineEnd = WordEnd;
				}

				SplitOverlong(LineBegin, LineEnd);
			}
			ParagraphLines.Add(TPair<int32, int32>(LineBegin, LineEnd));
		}

		for (const TPair<int32, int32>& Range : ParagraphLines)
		{
			const int32 LineIndex = Layout.Lines.Num();
			FLayout::FLine& Line = Layout.Lines.AddDefaulted_GetRef();
			Line.Begin = Range.Key;
			Line.End = Range.Value;
			Line.Width = Measure(Range.Key, Range.Value);

			// As tall as its tallest run - or, empty, as the run it sits in.
			float Ascent = 0.f;
			float Descent = 0.f;
			bool bAnyPiece = false;
			for (const FShaped& Piece : Shaped)
			{
				if (FMath::Min(Range.Value, Piece.End) > FMath::Max(Range.Key, Piece.Begin))
				{
					Ascent = FMath::Max(Ascent, RunExtents[Piece.Run].Key);
					Descent = FMath::Max(Descent, RunExtents[Piece.Run].Value);
					bAnyPiece = true;
				}
			}
			if (!bAnyPiece)
			{
				const int32 Run = RunAt(Range.Key);
				Ascent = RunExtents[Run].Key;
				Descent = RunExtents[Run].Value;
			}
			Line.Height = Ascent + Descent;
			Line.Top = LineTop;
			LineTop += Line.Height * LinePitchScale;

			for (const FShaped& Piece : Shaped)
			{
				const int32 Low = FMath::Max(Range.Key, Piece.Begin);
				const int32 High = FMath::Min(Range.Value, Piece.End);
				if (High <= Low)
				{
					continue;
				}
				FLayout::FPiece& Drawn = Line.Pieces.AddDefaulted_GetRef();
				Drawn.Run = Piece.Run;
				Drawn.X = Measure(Range.Key, Low);
				Drawn.Width = Measure(Low, High);
				Drawn.Top = Ascent - RunExtents[Piece.Run].Key;
				Drawn.Height = RunExtents[Piece.Run].Key + RunExtents[Piece.Run].Value;
				if (bWithGlyphs)
				{
					Drawn.Glyphs = Piece.Glyphs->GetSubSequence(Low - Piece.Begin, High - Piece.Begin);
				}
			}

			// Every letter on this line: letters are in order, and never white space, so each falls inside one line.
			while (NextLetter < Letters.Num() && Letters[NextLetter].Begin < Line.End)
			{
				const int32 Begin = Letters[NextLetter].Begin;
				FLayout::FLetter& Letter = Layout.Letters[NextLetter];
				Letter.Line = LineIndex;
				Letter.InLine = Line.NumLetters++;

				for (const FShaped& Piece : Shaped)
				{
					if (Begin >= Piece.Begin && Begin < Piece.End)
					{
						const int32 End = FMath::Min(Letters[NextLetter].End, Piece.End);
						Letter.Run = Piece.Run;
						Letter.X = Measure(Range.Key, Begin);
						Letter.Width = Measure(Begin, End);
						Letter.Top = Ascent - RunExtents[Piece.Run].Key;
						Letter.Height = RunExtents[Piece.Run].Key + RunExtents[Piece.Run].Value;
						if (bWithGlyphs && End > Begin)
						{
							Letter.Glyphs = Piece.Glyphs->GetSubSequence(Begin - Piece.Begin, End - Piece.Begin);
						}
						break;
					}
				}
				++NextLetter;
			}
		}

		ParagraphBegin = NextParagraph;
	}

	for (const FLayout::FLine& Line : Layout.Lines)
	{
		Layout.Size.X = FMath::Max(Layout.Size.X, Line.Width);
	}

	// The last line is as tall as its glyphs, or its whole pitch when the lines are spaced out further than that.
	if (Layout.Lines.Num() > 0)
	{
		const FLayout::FLine& Last = Layout.Lines.Last();
		Layout.Size.Y = Last.Top + FMath::Max(Last.Height, Last.Height * LinePitchScale);
	}
}

FVector2D SWidgetAnimText::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	EnsureLayout(MeasureLayout, LayoutScaleMultiplier, GetWrapWidth(), /*bWithGlyphs=*/false);

	const float InverseScale = 1.f / MeasureLayout.Scale;
	const FVector2f Content = MeasureLayout.Size * InverseScale;
	return FVector2D(
		FMath::Max(Content.X, Style.MinDesiredWidth) + Style.Margin.Left + Style.Margin.Right,
		Content.Y + Style.Margin.Top + Style.Margin.Bottom);
}

void SWidgetAnimText::RebuildPoseIndex() const
{
	if (!bPoseIndexDirty)
	{
		return;
	}
	bPoseIndexDirty = false;

	LetterPose.Init(INDEX_NONE, Letters.Num());
	WordPose.Init(INDEX_NONE, NumWords);
	LinePose.Reset();

	for (int32 PoseIndex = 0; PoseIndex < Poses.Num(); ++PoseIndex)
	{
		const FWidgetAnimPartKey& Part = Poses[PoseIndex].Part;
		TArray<int32>* Index = Part.Unit == WidgetAnimPartUnits::Letter ? &LetterPose
			: Part.Unit == WidgetAnimPartUnits::Word ? &WordPose
			: Part.Unit == WidgetAnimPartUnits::Line ? &LinePose
			: nullptr;
		if (!Index || Part.Index < 0)
		{
			continue;
		}

		// Lines are only known once laid out, so their index grows as poses name them.
		if (Index == &LinePose && !LinePose.IsValidIndex(Part.Index))
		{
			const int32 OldNum = LinePose.Num();
			LinePose.SetNum(Part.Index + 1);
			for (int32 Fill = OldNum; Fill < LinePose.Num(); ++Fill)
			{
				LinePose[Fill] = INDEX_NONE;
			}
		}

		if (Index->IsValidIndex(Part.Index) && (*Index)[Part.Index] == INDEX_NONE)
		{
			(*Index)[Part.Index] = PoseIndex;
		}
	}
}

FShapedGlyphSequencePtr SWidgetAnimText::GetStandIn(FLayout& Layout, int32 LetterIndex) const
{
	const FString& Charset = Effects->ScrambleCharset;
	if (Charset.IsEmpty() || !Layout.Letters.IsValidIndex(LetterIndex) || !FSlateApplication::IsInitialized())
	{
		return nullptr;
	}

	const float Pick = (WidgetAnimTextDraw::Random(LetterIndex, ScrambleSeedNow, 0x5CA3u) + 1.f) * 0.5f;
	const TCHAR Char = Charset[FMath::Clamp(FMath::FloorToInt32(Pick * Charset.Len()), 0, Charset.Len() - 1)];
	const int32 Run = Layout.Letters[LetterIndex].Run;

	const TPair<TCHAR, int32> Key(Char, Run);
	if (const FShapedGlyphSequencePtr* Found = Layout.StandIns.Find(Key))
	{
		return *Found;
	}

	const TSharedRef<FSlateFontCache> FontCache = FSlateApplication::Get().GetRenderer()->GetFontCache();
	const FSlateFontInfo& Font = ResolvedRuns.IsValidIndex(Run) ? ResolvedRuns[Run].Font : Style.Default.Font;
	const FShapedGlyphSequencePtr Shaped = FontCache->ShapeBidirectionalText(FString(1, &Char), Font, Layout.Scale, TextBiDi::ETextDirection::LeftToRight, ETextShapingMethod::Auto);
	Layout.StandIns.Add(Key, Shaped);
	return Shaped;
}

int32 SWidgetAnimText::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Shaped at the scale it is drawn at, so the letters are as sharp as a Text Block's.
	EnsureLayout(PaintLayout, AllottedGeometry.Scale, GetWrapWidth(), /*bWithGlyphs=*/true);
	FLayout& Layout = PaintLayout;
	if (Layout.Lines.Num() == 0 || ResolvedRuns.Num() == 0)
	{
		return LayerId;
	}

	const FWidgetAnimTextEffects* Fx = Effects.IsValid() && !Effects->IsEmpty() ? Effects.Get() : nullptr;

	const float InverseScale = 1.f / Layout.Scale;
	const FVector2f LocalSize = AllottedGeometry.GetLocalSize();
	const float Available = LocalSize.X - Style.Margin.Left - Style.Margin.Right;

	const ESlateDrawEffect DrawEffects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const bool bReplaceColor = Fx && Fx->bTint && Fx->bTintReplaces;
	if (Fx && Fx->bTint && !bReplaceColor)
	{
		Tint *= Fx->Tint;
	}

	// What moves on its own, at this moment: the host keeps the time, so a wave travels and a shake jitters with no
	// track writing every frame.
	const double Now = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : 0.0;
	float WavePhase = 0.f;
	uint32 ShakeSeed = 0;
	ScrambleSeedNow = 0;
	if (Fx)
	{
		WavePhase = Fx->WavePhase + static_cast<float>(FMath::Fmod(Now * Fx->WaveSpeed, 1.0));
		ShakeSeed = Fx->ShakeSeed + (Fx->ShakeRate > 0.f ? static_cast<uint32>(FMath::FloorToDouble(Now * Fx->ShakeRate)) : 0u);
		ScrambleSeedNow = Fx->ScrambleSeed + (Fx->ScrambleRate > 0.f ? static_cast<uint32>(FMath::FloorToDouble(Now * Fx->ScrambleRate)) : 0u);
	}

	// Each run's colors, tinted - or, with a color that replaces them, that color at each run's own opacity.
	TArray<FLinearColor, TInlineAllocator<8>> RunColors;
	TArray<FLinearColor, TInlineAllocator<8>> RunOutlines;
	TArray<FLinearColor, TInlineAllocator<8>> RunShadows;
	for (const FWidgetAnimTextRunStyle& Run : ResolvedRuns)
	{
		FLinearColor RunColor = Run.Color.GetColor(InWidgetStyle) * Tint;
		if (bReplaceColor)
		{
			// The inherited opacity is in Tint already; the run's own comes from its color.
			FLinearColor Replaced = Fx->Tint * Tint;
			Replaced.A *= Run.Color.GetColor(InWidgetStyle).A;
			RunColor = Replaced;
		}
		RunColors.Add(RunColor);
		RunOutlines.Add(Run.Font.OutlineSettings.OutlineColor * Tint);
		RunShadows.Add(Run.ShadowColor * Tint);
	}

	// Where a line starts, in the widget's own units: its alignment within the room left between the margins.
	auto LineLeft = [&](const FLayout::FLine& Line)
	{
		const float Width = Line.Width * InverseScale;
		float Offset = 0.f;
		switch (Style.Justification)
		{
		case ETextJustify::Center:         Offset = (Available - Width) * 0.5f; break;
		case ETextJustify::Right:
		case ETextJustify::InvariantRight: Offset = Available - Width; break;
		default:                           break;
		}
		return Style.Margin.Left + Offset;
	};
	auto LineTop = [&](const FLayout::FLine& Line) { return Style.Margin.Top + Line.Top * InverseScale; };

	auto Draw = [&](const FShapedGlyphSequencePtr& Glyphs, int32 Run, const FVector2f& Offset, const FVector2f& Size, const FSlateRenderTransform* Transform, float Opacity, const FLinearColor* ColorOverride)
	{
		if (!Glyphs.IsValid() || !ResolvedRuns.IsValidIndex(Run))
		{
			return;
		}

		auto MakeGeometry = [&](const FVector2f& At)
		{
			return Transform
				? AllottedGeometry.MakeChild(Size, FSlateLayoutTransform(At), *Transform, FVector2f::ZeroVector)
				: AllottedGeometry.MakeChild(Size, FSlateLayoutTransform(At));
		};

		const FWidgetAnimTextRunStyle& RunStyle = ResolvedRuns[Run];
		FLinearColor Shadow = RunShadows[Run];
		if (Shadow.A > 0.f && !RunStyle.ShadowOffset.IsNearlyZero())
		{
			Shadow.A *= Opacity;
			FSlateDrawElement::MakeShapedText(OutDrawElements, LayerId, MakeGeometry(Offset + FVector2f(RunStyle.ShadowOffset)).ToPaintGeometry(), Glyphs.ToSharedRef(), DrawEffects, Shadow, Shadow);
		}

		FLinearColor Color = ColorOverride ? *ColorOverride : RunColors[Run];
		Color.A *= Opacity;
		FLinearColor Outline = RunOutlines[Run];
		Outline.A *= Opacity;
		FSlateDrawElement::MakeShapedText(OutDrawElements, LayerId + 1, MakeGeometry(Offset).ToPaintGeometry(), Glyphs.ToSharedRef(), DrawEffects, Color, Outline);
	};

	// No effect, nothing posed: each line whole, a run at a time, as a Text Block draws it.
	if (!Fx && Poses.Num() == 0)
	{
		for (const FLayout::FLine& Line : Layout.Lines)
		{
			const float Left = LineLeft(Line);
			const float Top = LineTop(Line);
			for (const FLayout::FPiece& Piece : Line.Pieces)
			{
				Draw(Piece.Glyphs, Piece.Run, FVector2f(Left + Piece.X * InverseScale, Top + Piece.Top * InverseScale), FVector2f(Piece.Width, Piece.Height) * InverseScale, nullptr, 1.f, nullptr);
			}
		}
		return LayerId + 1;
	}

	RebuildPoseIndex();

	// Where each letter sits, in the widget's own units - and the box around each word and line, whose middle a word or
	// line is turned and scaled about.
	const int32 NumLetters = FMath::Min(Letters.Num(), Layout.Letters.Num());
	TArray<FBox2f, TInlineAllocator<64>> LetterBoxes;
	LetterBoxes.SetNumUninitialized(NumLetters);
	TArray<FBox2f, TInlineAllocator<16>> WordBoxes;
	WordBoxes.Init(FBox2f(ForceInit), NumWords);
	TArray<FBox2f, TInlineAllocator<8>> LineBoxes;
	LineBoxes.Init(FBox2f(ForceInit), Layout.Lines.Num());

	for (int32 LineIndex = 0; LineIndex < Layout.Lines.Num(); ++LineIndex)
	{
		const FLayout::FLine& Line = Layout.Lines[LineIndex];
		const FVector2f Min(LineLeft(Line), LineTop(Line));
		LineBoxes[LineIndex] = FBox2f(Min, Min + FVector2f(Line.Width, Line.Height) * InverseScale);
	}
	for (int32 LetterIndex = 0; LetterIndex < NumLetters; ++LetterIndex)
	{
		const FLayout::FLetter& Letter = Layout.Letters[LetterIndex];
		const FLayout::FLine& Line = Layout.Lines[FMath::Clamp(Letter.Line, 0, Layout.Lines.Num() - 1)];
		const FVector2f Min(LineLeft(Line) + Letter.X * InverseScale, LineTop(Line) + Letter.Top * InverseScale);
		LetterBoxes[LetterIndex] = FBox2f(Min, Min + FVector2f(Letter.Width, Letter.Height) * InverseScale);

		if (WordOfLetter.IsValidIndex(LetterIndex) && WordBoxes.IsValidIndex(WordOfLetter[LetterIndex]))
		{
			WordBoxes[WordOfLetter[LetterIndex]] += LetterBoxes[LetterIndex];
		}
	}

	// A reveal counts in letters, words or lines: which one a letter belongs to, and that one's box.
	auto RevealPlace = [&](int32 LetterIndex, int32 Word, int32 LineIndex)
	{
		return Fx->RevealUnit == WidgetAnimPartUnits::Word ? Word
			: Fx->RevealUnit == WidgetAnimPartUnits::Line ? LineIndex
			: LetterIndex;
	};
	auto RevealBox = [&](int32 LetterIndex, int32 Word, int32 LineIndex)
	{
		if (Fx->RevealUnit == WidgetAnimPartUnits::Word && WordBoxes.IsValidIndex(Word))
		{
			return WordBoxes[Word];
		}
		if (Fx->RevealUnit == WidgetAnimPartUnits::Line && LineBoxes.IsValidIndex(LineIndex))
		{
			return LineBoxes[LineIndex];
		}
		return LetterBoxes[LetterIndex];
	};

	int32 LastShownLetter = INDEX_NONE;
	bool bAllShown = true;

	for (int32 LetterIndex = 0; LetterIndex < NumLetters; ++LetterIndex)
	{
		const FLayout::FLetter& Letter = Layout.Letters[LetterIndex];
		const int32 Word = WordOfLetter.IsValidIndex(LetterIndex) ? WordOfLetter[LetterIndex] : INDEX_NONE;
		const FBox2f& Box = LetterBoxes[LetterIndex];
		const FVector2f Center = Box.GetCenter();

		float Opacity = 1.f;
		FVector2D Offset = FVector2D::ZeroVector;
		float EffectScale = 1.f;
		float EffectAngle = 0.f;
		FSlateRenderTransform Composed;
		bool bPosed = false;
		FShapedGlyphSequencePtr Glyphs = Letter.Glyphs;
		bool bStandIn = false;
		FLinearColor Color = RunColors.IsValidIndex(Letter.Run) ? RunColors[Letter.Run] : FLinearColor::White;
		bool bRecolored = false;

		if (Fx)
		{
			// The letter's own effects, about its own middle: a wave, a shake, spacing.
			if (Fx->bWave && Fx->WaveLength > KINDA_SMALL_NUMBER)
			{
				const float Wave = FMath::Sin(2.f * PI * (WavePhase - LetterIndex / Fx->WaveLength));
				switch (Fx->WaveMotion)
				{
				case EWidgetAnimTextWaveMotion::Sway:   Offset.X += Wave * Fx->WaveAmplitude; break;
				case EWidgetAnimTextWaveMotion::Rotate: EffectAngle += Wave * Fx->WaveAmplitude; break;
				case EWidgetAnimTextWaveMotion::Scale:  EffectScale *= 1.f + Fx->WaveAmplitude * (0.5f + 0.5f * Wave); break;
				case EWidgetAnimTextWaveMotion::Bob:
				default:                                Offset.Y -= Wave * Fx->WaveAmplitude; break;
				}
			}
			if (Fx->bShake)
			{
				Offset.X += WidgetAnimTextDraw::Random(LetterIndex, ShakeSeed, 0x1A2Bu) * Fx->ShakeAmount;
				Offset.Y += WidgetAnimTextDraw::Random(LetterIndex, ShakeSeed, 0x3C4Du) * Fx->ShakeAmount;
				EffectAngle += WidgetAnimTextDraw::Random(LetterIndex, ShakeSeed, 0x5E6Fu) * Fx->ShakeAngle;
			}
			if (Fx->ExtraSpacing != 0.f && Layout.Lines.IsValidIndex(Letter.Line))
			{
				const int32 OnLine = Layout.Lines[Letter.Line].NumLetters;
				Offset.X += (Letter.InLine - (OnLine - 1) * 0.5f) * Fx->ExtraSpacing;
			}
			if (!Offset.IsZero() || EffectScale != 1.f || EffectAngle != 0.f)
			{
				Composed = Concatenate(Composed, WidgetAnimTextDraw::PoseAbout(Offset, FVector2D(EffectScale, EffectScale), FVector2D::ZeroVector, EffectAngle, Center));
				bPosed = true;
			}

			// A reveal: not drawn before its part is reached, and on its way in from Appear while it arrives.
			if (Fx->bReveal)
			{
				const int32 Place = RevealPlace(LetterIndex, Word, Letter.Line);
				const float Alpha = Fx->RevealAlphas.IsValidIndex(Place) ? FMath::Clamp(Fx->RevealAlphas[Place], 0.f, 1.f) : 0.f;
				if (Alpha <= 0.f)
				{
					bAllShown = false;
					continue;
				}
				LastShownLetter = LetterIndex;
				bAllShown = bAllShown && Alpha >= 1.f;

				if (Alpha < 1.f)
				{
					const FWidgetAnimTextAppear& Appear = Fx->Appear;
					const float Eased = WidgetAnimEasing::Evaluate(Appear.Easing, nullptr, Alpha);
					Opacity *= FMath::Lerp(Appear.Opacity, 1.f, Eased);
					const float Remaining = 1.f - Eased;
					const float AppearScale = FMath::Lerp(Appear.Scale, 1.f, Eased);
					Composed = Concatenate(Composed, WidgetAnimTextDraw::PoseAbout(Appear.Offset * Remaining, FVector2D(AppearScale, AppearScale), FVector2D::ZeroVector, Appear.Angle * Remaining, RevealBox(LetterIndex, Word, Letter.Line).GetCenter()));
					bPosed = true;
				}
			}

			// A scramble: settled letters as themselves, the next ones as stand-ins, any past those not yet.
			if (Fx->bScramble && LetterIndex >= Fx->ScrambleSettled)
			{
				if (Fx->ScrambleAhead >= 0 && LetterIndex >= Fx->ScrambleSettled + Fx->ScrambleAhead)
				{
					continue;
				}
				Glyphs = GetStandIn(Layout, LetterIndex);
				bStandIn = true;
			}

			// A shimmer: the band's color over the letters it passes, fading at its edges.
			if (Fx->bShimmer)
			{
				const float Distance = FMath::Abs(LetterIndex - Fx->ShimmerCenter);
				const float Weight = FMath::Clamp(1.f - FMath::Max(Distance - Fx->ShimmerWidth, 0.f) / FMath::Max(Fx->ShimmerSoftness, KINDA_SMALL_NUMBER), 0.f, 1.f) * Fx->ShimmerStrength;
				if (Weight > 0.f)
				{
					FLinearColor Band = Fx->ShimmerColor * Tint;
					Band.A = Color.A;
					Color = FMath::Lerp(Color, Band, FMath::Clamp(Weight, 0.f, 1.f));
					bRecolored = true;
				}
			}
		}

		// The tracks' own poses of the letter, its word and its line - each about its own middle, the smaller inside the
		// larger, so a word can bounce while its letters each spin.
		auto ApplyPose = [&](int32 PoseIndex, const FBox2f& PoseBox)
		{
			if (!Poses.IsValidIndex(PoseIndex))
			{
				return;
			}
			const FWidgetAnimPartPose& Pose = Poses[PoseIndex];
			Opacity *= Pose.Opacity;
			Composed = Concatenate(Composed, WidgetAnimTextDraw::PoseAbout(Pose, PoseBox.GetCenter()));
			bPosed = true;
		};
		ApplyPose(LetterPose.IsValidIndex(LetterIndex) ? LetterPose[LetterIndex] : INDEX_NONE, Box);
		if (WordPose.IsValidIndex(Word) && WordBoxes.IsValidIndex(Word))
		{
			ApplyPose(WordPose[Word], WordBoxes[Word]);
		}
		if (LinePose.IsValidIndex(Letter.Line) && LineBoxes.IsValidIndex(Letter.Line))
		{
			ApplyPose(LinePose[Letter.Line], LineBoxes[Letter.Line]);
		}

		if (Opacity <= KINDA_SMALL_NUMBER || !Glyphs.IsValid())
		{
			continue;
		}

		FVector2f Origin = Box.Min;
		FVector2f Size = Box.GetSize();
		if (bStandIn)
		{
			// Centered in the letter's own box, so the line doesn't move as the stand-ins change.
			const float StandInWidth = WidgetAnimTextDraw::MeasureRange(*Glyphs, 0, 1) * InverseScale;
			Origin.X += (Size.X - StandInWidth) * 0.5f;
			Size.X = StandInWidth;
		}

		const FLinearColor* ColorOverride = bRecolored ? &Color : nullptr;
		if (!bPosed)
		{
			Draw(Glyphs, Letter.Run, Origin, Size, nullptr, Opacity, ColorOverride);
			continue;
		}

		// The poses were worked out in the widget's own units; the letter is drawn in its own, from its top left - so the
		// transform is carried over to there.
		const FSlateRenderTransform InLetter = Concatenate(Concatenate(FSlateRenderTransform(Origin), Composed), FSlateRenderTransform(-Origin));
		Draw(Glyphs, Letter.Run, Origin, Size, &InLetter, Opacity, ColorOverride);
	}

	// A typing cursor just after the last letter shown, while there is more to come - blinking, twice a second.
	if (Fx && Fx->bReveal && !Fx->Cursor.IsEmpty() && !bAllShown && FSlateApplication::IsInitialized())
	{
		const bool bBlinkOn = !Fx->bCursorBlink || FMath::Fmod(FSlateApplication::Get().GetCurrentTime(), 1.0) < 0.5;
		if (bBlinkOn)
		{
			const int32 Run = LastShownLetter != INDEX_NONE ? Layout.Letters[LastShownLetter].Run : 0;
			const FSlateFontInfo& Font = ResolvedRuns.IsValidIndex(Run) ? ResolvedRuns[Run].Font : Style.Default.Font;
			const FShapedGlyphSequencePtr Cursor = FSlateApplication::Get().GetRenderer()->GetFontCache()->ShapeBidirectionalText(
				Fx->Cursor, Font, Layout.Scale, TextBiDi::ETextDirection::LeftToRight, ETextShapingMethod::Auto);
			const float Width = WidgetAnimTextDraw::MeasureRange(*Cursor, 0, Fx->Cursor.Len()) * InverseScale;

			FVector2f At;
			float Height = 0.f;
			if (LastShownLetter != INDEX_NONE)
			{
				At = FVector2f(LetterBoxes[LastShownLetter].Max.X, LetterBoxes[LastShownLetter].Min.Y);
				Height = LetterBoxes[LastShownLetter].GetSize().Y;
			}
			else
			{
				At = FVector2f(LineLeft(Layout.Lines[0]), LineTop(Layout.Lines[0]));
				Height = Layout.Lines[0].Height * InverseScale;
			}
			Draw(Cursor, Run, At, FVector2f(Width, Height), nullptr, 1.f, nullptr);
		}
	}

	return LayerId + 1;
}
