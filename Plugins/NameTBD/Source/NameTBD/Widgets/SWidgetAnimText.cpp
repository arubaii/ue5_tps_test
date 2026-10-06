// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimText.h"

#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Clipping.h"
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextMarkup.h"
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
	// New tags are drawn in place: they never move a letter in the layout.
	if (Default.ChangesPaint(Other.Default) || Justification != Other.Justification || Markup != Other.Markup)
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
		// A final mix, so neighboring indices don't give neighboring numbers.
		uint32 Mixed = Hash * 0x9E3779B1u;
		Mixed ^= Mixed >> 15;
		Mixed *= 0x85EBCA77u;
		Mixed ^= Mixed >> 13;
		return (static_cast<float>(Mixed & 0xFFFFu) / 65535.f) * 2.f - 1.f;
	}

	/** Where a character coming into a change (bEnter) or going out of it is drawn, relative to its place. */
	struct FChangePose
	{
		FVector2f Offset = FVector2f::ZeroVector;
		float Opacity = 1.f;
		float Scale = 1.f;
		float Blur = 0.f;
	};

	/**
	 * The pose of a character coming in or going out, Progress (0 to 1, eased) of the way through a change of Style.
	 * Extent is the character's box: how far a roll carries it. Sign: +1 comes in from below or the right.
	 */
	FChangePose GetChangePose(EWidgetAnimTextChangeStyle Style, bool bEnter, float Progress, int32 Sign, const FVector2f& Extent)
	{
		FChangePose Pose;
		const float Done = FMath::Clamp(Progress, 0.f, 1.f);
		const float ToGo = 1.f - Done;
		const float Direction = Sign >= 0 ? 1.f : -1.f;
		switch (Style)
		{
		case EWidgetAnimTextChangeStyle::SlotMachine:
			Pose.Offset.Y = bEnter ? Direction * Extent.Y * ToGo : -Direction * Extent.Y * Done;
			Pose.Opacity = bEnter ? FMath::Lerp(0.35f, 1.f, Done) : FMath::Lerp(1.f, 0.35f, Done);
			break;

		case EWidgetAnimTextChangeStyle::Roulette:
			Pose.Offset.X = bEnter ? Direction * Extent.X * ToGo : -Direction * Extent.X * Done;
			Pose.Opacity = bEnter ? FMath::Lerp(0.35f, 1.f, Done) : FMath::Lerp(1.f, 0.35f, Done);
			break;

		case EWidgetAnimTextChangeStyle::Fade:
			Pose.Opacity = bEnter ? Done : ToGo;
			break;

		case EWidgetAnimTextChangeStyle::Blend:
			// Glyph outlines can't be morphed into one another, so one melts into the next: the old one softening and
			// shrinking away while the new one sharpens and settles, both in the same place.
			Pose.Opacity = bEnter ? Done : ToGo;
			Pose.Blur = (bEnter ? ToGo : Done) * Extent.Y * 0.18f;
			Pose.Scale = bEnter ? FMath::Lerp(1.12f, 1.f, Done) : FMath::Lerp(1.f, 0.88f, Done);
			break;

		default:
			Pose.Opacity = bEnter ? 1.f : 0.f;
			break;
		}
		return Pose;
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

	// Told only of changes after the first text: that one is simply what it was made with.
	OnTextChanged = InArgs._OnTextChanged;
	bGlowOnly = InArgs._GlowOnly;
	EffectKeyFilter = InArgs._EffectKey;

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
	const bool bMarkupChanged = Style.Markup != InStyle.Markup;
	Style = InStyle;
	Style.GetResolvedRuns(ResolvedRuns);

	if (bTextChanged)
	{
		RebuildParts();

		// While a Change Style is set, a new text is drawn as a change from the letters last drawn - once there are some
		// to change from.
		if (Effects.IsValid() && Effects->ChangeStyle != EWidgetAnimTextChangeStyle::Instant && DrawnScale > 0.f
			&& !DrawnText.Equals(Style.Text, ESearchCase::CaseSensitive))
		{
			BeginChange(GetNow());
		}
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

	// Tags that move on their own need drawing every frame; a text without them doesn't.
	if (bMarkupChanged)
	{
		UpdateVolatility();
	}

	if (bTextChanged && OnTextChanged)
	{
		OnTextChanged();
	}
}

void SWidgetAnimText::NotifyEffectsChanged()
{
	// Only what moves on its own - a blinking cursor, a traveling wave, a shake or scramble keeping its own time - needs
	// a frame every frame; everything else is drawn when a track changes it.
	UpdateVolatility();
}

void SWidgetAnimText::UpdateVolatility()
{
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
	return (Effects.IsValid() && Effects->IsLive()) || (Style.Markup.IsValid() && Style.Markup->bLiveEffects);
}

double SWidgetAnimText::GetNow()
{
	return FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : FPlatformTime::Seconds();
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

void SWidgetAnimText::PullStyle()
{
	if (StyleSource)
	{
		FWidgetAnimTextStyle Current;
		StyleSource(Current);
		SetStyle(Current);
	}
}

const TArray<FWidgetAnimTextEffectLayer>& SWidgetAnimText::GetEffectLayers() const
{
	// A text's tags are read once and shared, so its layers only need working out again for another text.
	// Held, not just compared by address, so a new text read into the same memory is never taken for the old one.
	const FWidgetAnimTextMarkup* Markup = Style.Markup.Get();
	if (Markup == EffectLayersFor.Get() && EffectLayersFor.IsValid() == Style.Markup.IsValid())
	{
		return EffectLayers;
	}
	EffectLayersFor = Style.Markup;
	EffectLayers.Reset();

	if (Markup)
	{
		for (const FWidgetAnimTextSpan& Span : Markup->Spans)
		{
			if (Span.EffectKey == 0)
			{
				continue;
			}
			FWidgetAnimTextEffectLayer* Layer = EffectLayers.FindByPredicate([&Span](const FWidgetAnimTextEffectLayer& Existing) { return Existing.Key == Span.EffectKey; });
			if (!Layer)
			{
				Layer = &EffectLayers.AddDefaulted_GetRef();
				Layer->Key = Span.EffectKey;
				Layer->Material = Span.EffectMaterial;
				Layer->bOver = Span.Tag.bGlowOverText;
				Layer->ScalarParameters = Span.Tag.GlowScalarParameters;
				Layer->VectorParameters = Span.Tag.GlowVectorParameters;
			}
			Layer->Radius = FMath::Max(Layer->Radius, Span.Tag.GlowRadius);
			Layer->Strength = FMath::Max(Layer->Strength, Span.Tag.GlowStrength);
		}
	}
	return EffectLayers;
}

float SWidgetAnimText::GetEffectStrength(uint32 Key) const
{
	for (const FWidgetAnimTextEffectLayer& Layer : GetEffectLayers())
	{
		if (Layer.Key == Key)
		{
			return Layer.Strength;
		}
	}
	return 0.f;
}

void SWidgetAnimText::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	PullStyle();

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

	// A change keeps its own time: drawn every frame while it moves, once more when it lands, and then let go.
	if (Change.Start >= 0.0)
	{
		if (Change.IsActive(GetNow()))
		{
			bChangeWasActive = true;
			Invalidate(EInvalidateWidgetReason::Paint);
		}
		else if (bChangeWasActive)
		{
			bChangeWasActive = false;
			Change = FChange();
			Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
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
				FLayout::FPiece& DrawnItemText = Line.Pieces.AddDefaulted_GetRef();
				DrawnItemText.Run = Piece.Run;
				DrawnItemText.X = Measure(Range.Key, Low);
				DrawnItemText.Width = Measure(Low, High);
				DrawnItemText.Top = Ascent - RunExtents[Piece.Run].Key;
				DrawnItemText.Height = RunExtents[Piece.Run].Key + RunExtents[Piece.Run].Value;
				if (bWithGlyphs)
				{
					DrawnItemText.Glyphs = Piece.Glyphs->GetSubSequence(Low - Piece.Begin, High - Piece.Begin);
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
	SpanPoses.Reset();

	for (int32 PoseIndex = 0; PoseIndex < Poses.Num(); ++PoseIndex)
	{
		const FWidgetAnimPartKey& Part = Poses[PoseIndex].Part;

		// A run of letters can overlap any other, so each is kept as it came, not by place.
		if (Part.Unit == WidgetAnimPartUnits::Span)
		{
			if (Part.Index >= 0 && Part.Count > 0)
			{
				SpanPoses.Add(PoseIndex);
			}
			continue;
		}
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
	if (Charset.IsEmpty() || !Layout.Letters.IsValidIndex(LetterIndex))
	{
		return nullptr;
	}

	const float Pick = (WidgetAnimTextDraw::Random(LetterIndex, ScrambleSeedNow, 0x5CA3u) + 1.f) * 0.5f;
	const TCHAR Char = Charset[FMath::Clamp(FMath::FloorToInt32(Pick * Charset.Len()), 0, Charset.Len() - 1)];
	return GetCharGlyphs(Layout, Char, Layout.Letters[LetterIndex].Run);
}

FShapedGlyphSequencePtr SWidgetAnimText::GetCharGlyphs(FLayout& Layout, TCHAR Char, int32 Run) const
{
	if (!FSlateApplication::IsInitialized())
	{
		return nullptr;
	}

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

// =====================================================================================================
// Changes
// =====================================================================================================

float SWidgetAnimText::FChange::GetProgress(double Now) const
{
	if (Start < 0.0 || Time <= 0.f)
	{
		return 1.f;
	}
	const float Alpha = FMath::Clamp(static_cast<float>((Now - Start) / Time), 0.f, 1.f);
	return WidgetAnimEasing::Evaluate(Easing, nullptr, Alpha);
}

void SWidgetAnimText::BeginChange(double Now)
{
	const FWidgetAnimTextEffects& Fx = *Effects;

	// Reels still turning from the change before carry on from where they have got to, so a fast count keeps them
	// spinning rather than snapping back each time.
	const float LastProgress = Change.Start >= 0.0 ? Change.GetProgress(Now) : 1.f;
	const TMap<int32, FChangeWheel> TurningWheels = MoveTemp(Change.Wheels);

	FChange Next;
	Next.Start = Now;
	Next.Time = FMath::Max(Fx.ChangeTime, 0.01f);
	Next.Easing = Fx.ChangeEasing;
	Next.Style = Fx.ChangeStyle;
	Next.Old = Drawn;
	Next.OldScale = DrawnScale;

	// Which way the new characters come in: as asked, or the way the number went - told by the Count, or else read off
	// the two texts.
	int32 Sign = Fx.ChangeFrom != 0 ? Fx.ChangeFrom : Fx.ChangeTrend;
	if (Sign == 0)
	{
		double OldNumber = 0.0;
		double NewNumber = 0.0;
		const bool bBothNumbers = WidgetAnimText::ParseNumber(DrawnText, OldNumber) && WidgetAnimText::ParseNumber(Style.Text, NewNumber);
		Sign = bBothNumbers && NewNumber < OldNumber ? -1 : 1;
	}
	Next.Sign = Sign > 0 ? 1 : -1;

	const int32 NumOld = Next.Old.Num();
	const int32 NumNew = Letters.Num();
	TArray<FString, TInlineAllocator<16>> New;
	New.Reserve(NumNew);
	for (const WidgetAnimTextParts::FRange& Range : Letters)
	{
		New.Add(Style.Text.Mid(Range.Begin, Range.End - Range.Begin));
	}

	auto IsDigit = [](const FString& Chars) { return Chars.Len() == 1 && FChar::IsDigit(Chars[0]); };
	const bool bWhole = Fx.ChangeScope == EWidgetAnimTextChangeScope::WholeNumber;
	const bool bReels = Fx.bChangeThroughDigits
		&& (Next.Style == EWidgetAnimTextChangeStyle::SlotMachine || Next.Style == EWidgetAnimTextChangeStyle::Roulette);

	// What the two texts share at their start and end stays put: "Gold: " and " XP" around a number, or - counting
	// changed digits only - the digits that didn't change too. For the whole number, digits are never shared, so the
	// number moves as one while the words around it stay.
	int32 Prefix = 0;
	while (Prefix < NumOld && Prefix < NumNew && Next.Old[Prefix].Chars.Equals(New[Prefix], ESearchCase::CaseSensitive)
		&& !(bWhole && IsDigit(New[Prefix])))
	{
		++Prefix;
	}
	int32 Suffix = 0;
	while (Suffix < NumOld - Prefix && Suffix < NumNew - Prefix
		&& Next.Old[NumOld - 1 - Suffix].Chars.Equals(New[NumNew - 1 - Suffix], ESearchCase::CaseSensitive)
		&& !(bWhole && IsDigit(New[NumNew - 1 - Suffix])))
	{
		++Suffix;
	}

	Next.Letters.SetNum(NumNew);
	for (int32 Index = 0; Index < Prefix; ++Index)
	{
		Next.Letters[Index].Old = Index;
	}
	for (int32 Index = 0; Index < Suffix; ++Index)
	{
		Next.Letters[NumNew - 1 - Index].Old = NumOld - 1 - Index;
	}

	const int32 MidNew = NumNew - Suffix - Prefix;
	const int32 MidOld = NumOld - Suffix - Prefix;
	if (bWhole)
	{
		for (int32 Index = Prefix; Index < NumNew - Suffix; ++Index)
		{
			Next.Letters[Index].Kind = EChangeKind::Arrive;
		}
		for (int32 Index = Prefix; Index < NumOld - Suffix; ++Index)
		{
			Next.Leaving.Add(Index);
		}
	}
	else
	{
		// Lined up from the right, as digits are: the ones under the ones, so 999 to 1,000 rolls each 9 over to a 0 and
		// brings the 1 and the comma in beside them.
		for (int32 Step = 0; Step < FMath::Max(MidNew, MidOld); ++Step)
		{
			const int32 NewIndex = NumNew - Suffix - 1 - Step;
			const int32 OldIndex = NumOld - Suffix - 1 - Step;
			const bool bHasNew = Step < MidNew;
			const bool bHasOld = Step < MidOld;

			if (bHasNew && bHasOld)
			{
				FChangeLetter& Letter = Next.Letters[NewIndex];
				Letter.Old = OldIndex;
				if (Next.Old[OldIndex].Chars.Equals(New[NewIndex], ESearchCase::CaseSensitive))
				{
					Letter.Kind = EChangeKind::Stay;
				}
				else if (bReels && IsDigit(Next.Old[OldIndex].Chars) && IsDigit(New[NewIndex]))
				{
					Letter.Kind = EChangeKind::Wheel;
					Letter.WheelKey = NumNew - 1 - NewIndex;

					// Forward through the digits in the way it comes in: up from 7 to 2 passes 8, 9, 0 and 1.
					const int32 OldDigit = Next.Old[OldIndex].Chars[0] - TEXT('0');
					const int32 NewDigit = New[NewIndex][0] - TEXT('0');
					const int32 Delta = Next.Sign > 0 ? (NewDigit - OldDigit + 10) % 10 : -((OldDigit - NewDigit + 10) % 10);

					FChangeWheel Wheel;
					Wheel.From = OldDigit;
					Wheel.To = OldDigit + Delta;
					if (const FChangeWheel* Turning = TurningWheels.Find(Letter.WheelKey))
					{
						const int64 TurningTo = static_cast<int64>(FMath::RoundToDouble(Turning->To));
						if (((TurningTo % 10) + 10) % 10 == OldDigit)
						{
							Wheel.From = FMath::Lerp(Turning->From, Turning->To, static_cast<double>(LastProgress));
							Wheel.To = Turning->To + Delta;
						}
					}
					Next.Wheels.Add(Letter.WheelKey, Wheel);
				}
				else
				{
					Letter.Kind = EChangeKind::Swap;
				}
			}
			else if (bHasNew)
			{
				Next.Letters[NewIndex].Kind = EChangeKind::Arrive;
			}
			else if (bHasOld)
			{
				Next.Leaving.Add(OldIndex);
			}
		}
	}

	Change = MoveTemp(Next);
	bChangeWasActive = true;
	Invalidate(EInvalidateWidgetReason::Paint);
}


int32 SWidgetAnimText::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Shaped at the scale it is drawn at, so the letters are as sharp as a Text Block's.
	EnsureLayout(PaintLayout, AllottedGeometry.Scale, GetWrapWidth(), /*bWithGlyphs=*/true);
	FLayout& Layout = PaintLayout;

	// While a Change Style is set, the letters as drawn are kept for the next change to leave from.
	const bool bKeepDrawn = Effects.IsValid() && Effects->ChangeStyle != EWidgetAnimTextChangeStyle::Instant;
	if (!bKeepDrawn && Drawn.Num() > 0)
	{
		Drawn.Reset();
		DrawnText.Reset();
		DrawnScale = 0.f;
	}

	if (Layout.Lines.Num() == 0 || ResolvedRuns.Num() == 0)
	{
		if (bKeepDrawn)
		{
			// Nothing to draw is something to change from too: a number appearing in an empty text rolls in.
			Drawn.Reset();
			DrawnText = Style.Text;
			DrawnScale = Layout.Scale;
		}
		return LayerId;
	}

	const FWidgetAnimTextEffects* Fx = Effects.IsValid() && !Effects->IsEmpty() ? Effects.Get() : nullptr;
	const FWidgetAnimTextMarkup* Markup = Style.Markup.IsValid() && Style.Markup->bDrawsEffects ? Style.Markup.Get() : nullptr;

	// A glow layer draws only what glows into it; with nothing, nothing.
	const float GlowStrengthScale = bGlowOnly ? GetEffectStrength(EffectKeyFilter) : 0.f;
	if (bGlowOnly && (!Markup || GlowStrengthScale <= 0.f))
	{
		return LayerId;
	}
	const double Now = GetNow();
	const bool bChanging = Change.IsActive(Now) && Change.Letters.Num() == Letters.Num();

	const float InverseScale = 1.f / Layout.Scale;
	const FVector2f LocalSize = AllottedGeometry.GetLocalSize();
	const float Available = LocalSize.X - Style.Margin.Left - Style.Margin.Right;

	const ESlateDrawEffect DrawEffects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	// Text Color limited to part of the text colors those letters one by one, below; otherwise the whole text, here.
	const bool bTintLetters = Fx && Fx->bTint && Fx->TintLetters.Num() > 0;
	const bool bReplaceColor = Fx && Fx->bTint && Fx->bTintReplaces && !bTintLetters;
	if (Fx && Fx->bTint && !bReplaceColor && !bTintLetters)
	{
		Tint *= Fx->Tint;
	}

	// What moves on its own, at this moment: the host keeps the time, so a wave travels and a shake jitters with no
	// track writing every frame.
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
		if (!bGlowOnly && Shadow.A > 0.f && !RunStyle.ShadowOffset.IsNearlyZero())
		{
			Shadow.A *= Opacity;
			FSlateDrawElement::MakeShapedText(OutDrawElements, LayerId, MakeGeometry(Offset + FVector2f(RunStyle.ShadowOffset)).ToPaintGeometry(), Glyphs.ToSharedRef(), DrawEffects, Shadow, Shadow);
		}

		FLinearColor Color = ColorOverride ? *ColorOverride : RunColors[Run];
		Color.A *= Opacity;
		// The glow layer's outline glows with the letter: it is part of its shape.
		FLinearColor Outline = bGlowOnly ? Color : RunOutlines[Run];
		Outline.A *= bGlowOnly ? 1.f : Opacity;
		FSlateDrawElement::MakeShapedText(OutDrawElements, LayerId + 1, MakeGeometry(Offset).ToPaintGeometry(), Glyphs.ToSharedRef(), DrawEffects, Color, Outline);
	};

	// No effect, no tag, nothing posed and nothing changing: each line whole, a run at a time, as a Text Block draws it.
	if (!Fx && !Markup && Poses.Num() == 0 && !bChanging && !bKeepDrawn)
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

	/**
	 * Glyphs drawn at Origin, moved by Composed (in the widget's own units) when bPosed - and softened by Blur, and cut
	 * to Clip, when those are given.
	 */
	auto DrawPosed = [&](const FShapedGlyphSequencePtr& Glyphs, int32 Run, const FVector2f& Origin, const FVector2f& Size, bool bPosed,
		const FSlateRenderTransform& Composed, float Opacity, const FLinearColor* ColorOverride, float Blur, const FBox2f* Clip)
	{
		if (!Glyphs.IsValid() || Opacity <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		if (Clip)
		{
			OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry.MakeChild(Clip->GetSize(), FSlateLayoutTransform(Clip->Min))));
		}

		// The poses were worked out in the widget's own units; the glyphs are drawn in their own, from their top left -
		// so the transform is carried over to there.
		auto DrawOne = [&](const FVector2f& At, float OneOpacity)
		{
			if (!bPosed)
			{
				Draw(Glyphs, Run, At, Size, nullptr, OneOpacity, ColorOverride);
				return;
			}
			const FSlateRenderTransform InGlyphs = Concatenate(Concatenate(FSlateRenderTransform(At), Composed), FSlateRenderTransform(-At));
			Draw(Glyphs, Run, At, Size, &InGlyphs, OneOpacity, ColorOverride);
		};

		if (Blur > 0.35f)
		{
			// Out of focus: drawn nine times, in place and all around it, each faint enough that where all nine overlap
			// it is as opaque as asked - so its middle holds and its edges soften.
			static const FVector2f Taps[] =
			{
				FVector2f(0.f, 0.f), FVector2f(1.f, 0.f), FVector2f(-1.f, 0.f), FVector2f(0.f, 1.f), FVector2f(0.f, -1.f),
				FVector2f(0.7071f, 0.7071f), FVector2f(-0.7071f, 0.7071f), FVector2f(0.7071f, -0.7071f), FVector2f(-0.7071f, -0.7071f)
			};
			const float TapOpacity = 1.f - FMath::Pow(1.f - FMath::Min(Opacity, 0.999f), 1.f / 9.f);
			for (const FVector2f& Tap : Taps)
			{
				DrawOne(Origin + Tap * Blur, TapOpacity);
			}
		}
		else
		{
			DrawOne(Origin, Opacity);
		}

		if (Clip)
		{
			OutDrawElements.PopClip();
		}
	};

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

	// The box around each run of letters posed as one - a track's Part of the Text - whose middle it is turned and scaled about.
	TArray<FBox2f, TInlineAllocator<4>> SpanBoxes;
	SpanBoxes.Init(FBox2f(ForceInit), SpanPoses.Num());
	for (int32 SpanAt = 0; SpanAt < SpanPoses.Num(); ++SpanAt)
	{
		const FWidgetAnimPartKey& SpanPart = Poses[SpanPoses[SpanAt]].Part;
		for (int32 LetterIndex = FMath::Max(SpanPart.Index, 0); LetterIndex < FMath::Min(SpanPart.Index + SpanPart.Count, NumLetters); ++LetterIndex)
		{
			SpanBoxes[SpanAt] += LetterBoxes[LetterIndex];
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

	// Where each tag's letters are - its first and how many - so a gradient, a wave or a shimmer runs along them.
	TArray<int32, TInlineAllocator<8>> SpanFirst;
	TArray<int32, TInlineAllocator<8>> SpanCount;
	if (Markup)
	{
		SpanFirst.Init(INDEX_NONE, Markup->Spans.Num());
		SpanCount.Init(0, Markup->Spans.Num());
		for (int32 LetterIndex = 0; LetterIndex < NumLetters; ++LetterIndex)
		{
			const int32 At = Letters[LetterIndex].Begin;
			for (int32 SpanIndex = 0; SpanIndex < Markup->Spans.Num(); ++SpanIndex)
			{
				if (Markup->Spans[SpanIndex].Covers(At))
				{
					if (SpanFirst[SpanIndex] == INDEX_NONE)
					{
						SpanFirst[SpanIndex] = LetterIndex;
					}
					++SpanCount[SpanIndex];
				}
			}
		}
	}

	// A change of text under way: how far along it is, and whether the old letters' glyphs can still be drawn - shaped at
	// another scale they would come out the wrong size.
	const float ChangeProgress = bChanging ? Change.GetProgress(Now) : 1.f;
	const bool bOldGlyphsUsable = bChanging && Change.OldScale == Layout.Scale;
	const bool bRollingChange = Change.Style == EWidgetAnimTextChangeStyle::SlotMachine || Change.Style == EWidgetAnimTextChangeStyle::Roulette;

	/** The window a rolling character is seen through: its own row of a reel (Slot Machine) or column (Roulette). */
	auto RollClip = [&](const FBox2f& At, float Width)
	{
		if (Change.Style == EWidgetAnimTextChangeStyle::SlotMachine)
		{
			const float PadX = Width * 0.5f;
			return FBox2f(FVector2f(At.Min.X - PadX, At.Min.Y), FVector2f(At.Max.X + PadX, At.Max.Y));
		}
		const float PadY = At.GetSize().Y * 0.5f;
		return FBox2f(FVector2f(At.Min.X, At.Min.Y - PadY), FVector2f(At.Min.X + Width, At.Max.Y + PadY));
	};

	/** Glyphs coming in or going out in a change, at At, posed as Pose says on top of whatever else moves the letter. */
	auto DrawChangeGlyphs = [&](const FShapedGlyphSequencePtr& Glyphs, int32 Run, const FVector2f& At, const FVector2f& Size,
		const WidgetAnimTextDraw::FChangePose& Pose, bool bBasePosed, const FSlateRenderTransform& BaseComposed, float BaseOpacity,
		const FLinearColor* ColorOverride, const FBox2f* Clip)
	{
		const FVector2f DrawAt = At + Pose.Offset;
		bool bPosedHere = bBasePosed;
		FSlateRenderTransform ComposedHere = BaseComposed;
		if (Pose.Scale != 1.f)
		{
			ComposedHere = Concatenate(WidgetAnimTextDraw::PoseAbout(FVector2D::ZeroVector, FVector2D(Pose.Scale, Pose.Scale), FVector2D::ZeroVector, 0.f, DrawAt + Size * 0.5f), BaseComposed);
			bPosedHere = true;
		}
		DrawPosed(Glyphs, Run, DrawAt, Size, bPosedHere, ComposedHere, BaseOpacity * Pose.Opacity, ColorOverride, Pose.Blur, Clip);
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

		// Text Color on part of the text: its letters tinted - or, replacing, its color at the run's own opacity.
		if (bTintLetters && WidgetAnimTextParts::Covers(Fx->TintLetters, LetterIndex))
		{
			if (Fx->bTintReplaces)
			{
				FLinearColor Replaced = Fx->Tint * Tint;
				Replaced.A *= ResolvedRuns.IsValidIndex(Letter.Run) ? ResolvedRuns[Letter.Run].Color.GetColor(InWidgetStyle).A : 1.f;
				Color = Replaced;
			}
			else
			{
				Color *= Fx->Tint;
			}
			bRecolored = true;
		}

		float Blur = 0.f;
		FBox2f Clip(ForceInit);
		bool bClip = false;

		// The glow a tag gives the letter - the innermost glowing tag around it wins. Only the glow layer reads it.
		const FWidgetAnimTextTag* GlowTag = nullptr;

		// A change of text: where this letter was before it, so it slides from there to its new place as the change goes.
		const FChangeLetter* ChangeLetter = bChanging ? &Change.Letters[LetterIndex] : nullptr;
		const FDrawnLetter* OldLetter = ChangeLetter && Change.Old.IsValidIndex(ChangeLetter->Old) ? &Change.Old[ChangeLetter->Old] : nullptr;
		const FVector2f Glide = OldLetter ? (OldLetter->Box.Min - Box.Min) * (1.f - ChangeProgress) : FVector2f::ZeroVector;
		const bool bChangedHere = ChangeLetter && ChangeLetter->Kind != EChangeKind::Stay;
		if (!bChangedHere)
		{
			Offset += FVector2D(Glide);
		}

		if (Fx)
		{
			// The letter's own effects, about its own middle: a wave, a shake, spacing.
			if (Fx->bWave && Fx->WaveLength > KINDA_SMALL_NUMBER && WidgetAnimTextParts::Covers(Fx->WaveLetters, LetterIndex))
			{
				const float Wave = FMath::Sin(2.f * PI * (WavePhase - LetterIndex / Fx->WaveLength));
				switch (Fx->WaveMotion)
				{
				case EWidgetAnimTextWaveMotion::Sway:   Offset.X += Wave * Fx->WaveAmplitude; break;
				case EWidgetAnimTextWaveMotion::Rotate: EffectAngle += Wave * Fx->WaveAmplitude; break;
				case EWidgetAnimTextWaveMotion::Scale:  EffectScale *= 1.f + Fx->WaveAmplitude * (0.5f + 0.5f * Wave); break;
				case EWidgetAnimTextWaveMotion::Hop:
					Offset.Y -= FMath::Abs(FMath::Sin(PI * (WavePhase - LetterIndex / Fx->WaveLength))) * Fx->WaveAmplitude;
					break;
				case EWidgetAnimTextWaveMotion::Bob:
				default:                                Offset.Y -= Wave * Fx->WaveAmplitude; break;
				}
			}
			if (Fx->bShake && WidgetAnimTextParts::Covers(Fx->ShakeLetters, LetterIndex))
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
		}

		// The text's tags on this letter: each tag around it adds its motion and look, an inner tag's color over an outer's.
		if (Markup)
		{
			const int32 At = Letters[LetterIndex].Begin;
			for (int32 SpanIndex = 0; SpanIndex < Markup->Spans.Num(); ++SpanIndex)
			{
				const FWidgetAnimTextSpan& Span = Markup->Spans[SpanIndex];
				if (!Span.Tag.DrawsEffect() || !Span.Covers(At))
				{
					continue;
				}
				const FWidgetAnimTextTag& TextTag = Span.Tag;
				const float InSpan = static_cast<float>(LetterIndex - FMath::Max(SpanFirst[SpanIndex], 0));
				const int32 SpanLetters = FMath::Max(SpanCount[SpanIndex], 1);

				if (TextTag.bSize)
				{
					EffectScale *= FMath::Max(TextTag.Size, 0.f);
				}
				if (bGlowOnly && Span.EffectKey != 0 && Span.EffectKey == EffectKeyFilter)
				{
					GlowTag = &TextTag;
				}
				if (TextTag.bWave && TextTag.WaveLength > KINDA_SMALL_NUMBER)
				{
					const float Phase = static_cast<float>(FMath::Fmod(Now * TextTag.WaveSpeed, 1.0)) - InSpan / TextTag.WaveLength;
					const float Wave = FMath::Sin(2.f * PI * Phase);
					switch (TextTag.WaveMotion)
					{
					case EWidgetAnimTextWaveMotion::Sway:   Offset.X += Wave * TextTag.WaveAmplitude; break;
					case EWidgetAnimTextWaveMotion::Rotate: EffectAngle += Wave * TextTag.WaveAmplitude; break;
					case EWidgetAnimTextWaveMotion::Scale:  EffectScale *= 1.f + TextTag.WaveAmplitude * (0.5f + 0.5f * Wave); break;
					case EWidgetAnimTextWaveMotion::Hop:    Offset.Y -= FMath::Abs(FMath::Sin(PI * Phase)) * TextTag.WaveAmplitude; break;
					case EWidgetAnimTextWaveMotion::Bob:
					default:                                Offset.Y -= Wave * TextTag.WaveAmplitude; break;
					}
				}
				if (TextTag.bShake)
				{
					const uint32 TagSeed = static_cast<uint32>(FMath::FloorToDouble(Now * FMath::Max(TextTag.ShakeRate, 1.f))) + static_cast<uint32>(SpanIndex) * 7919u;
					Offset.X += WidgetAnimTextDraw::Random(LetterIndex, TagSeed, 0x1A2Bu) * TextTag.ShakeDistance;
					Offset.Y += WidgetAnimTextDraw::Random(LetterIndex, TagSeed, 0x3C4Du) * TextTag.ShakeDistance;
					EffectAngle += WidgetAnimTextDraw::Random(LetterIndex, TagSeed, 0x5E6Fu) * TextTag.ShakeAngle;
				}
				if (TextTag.bPulse)
				{
					const float Phase = static_cast<float>(FMath::Fmod(Now * TextTag.PulseSpeed, 1.0)) - InSpan * 0.08f;
					EffectScale *= 1.f + TextTag.PulseAmount * (0.5f + 0.5f * FMath::Sin(2.f * PI * Phase));
				}
				if (TextTag.bBlink)
				{
					const float Phase = static_cast<float>(FMath::Fmod(Now * TextTag.BlinkSpeed, 1.0));
					Opacity *= FMath::Lerp(FMath::Clamp(TextTag.BlinkOpacity, 0.f, 1.f), 1.f, 0.5f + 0.5f * FMath::Cos(2.f * PI * Phase));
				}

				// Colors: over the run's own, keeping its opacity.
				auto Recolor = [&Color, &bRecolored, &Tint](const FLinearColor& NewColor)
				{
					FLinearColor Tinted = NewColor * Tint;
					Tinted.A = Color.A * NewColor.A;
					Color = Tinted;
					bRecolored = true;
				};
				if (TextTag.bColor && TextTag.Colors.Num() > 0)
				{
					FLinearColor Picked = TextTag.Colors[0];
					if (TextTag.Colors.Num() > 1)
					{
						// A gradient from the first letter to the last.
						const float Along = SpanLetters > 1 ? InSpan / static_cast<float>(SpanLetters - 1) : 0.f;
						const float Scaled = Along * static_cast<float>(TextTag.Colors.Num() - 1);
						const int32 Lower = FMath::Clamp(FMath::FloorToInt32(Scaled), 0, TextTag.Colors.Num() - 2);
						Picked = FMath::Lerp(TextTag.Colors[Lower], TextTag.Colors[Lower + 1], Scaled - static_cast<float>(Lower));
					}
					Recolor(Picked);
				}
				if (TextTag.bRainbow)
				{
					const float Hue = FMath::Frac(static_cast<float>(FMath::Fmod(Now * TextTag.RainbowSpeed, 1.0)) - InSpan / FMath::Max(TextTag.RainbowLength, 1.f));
					Recolor(FLinearColor(Hue * 360.f, 0.7f, 1.f).HSVToLinearRGB());
				}
				if (TextTag.bShimmer)
				{
					// A sweep in the first half of every Shimmer Every, and rest in the second.
					const float Sweep = static_cast<float>(FMath::Fmod(Now / FMath::Max(TextTag.ShimmerEvery, 0.1f), 1.0)) * 2.f;
					if (Sweep <= 1.f)
					{
						constexpr float Softness = 2.f;
						const float Reach = FMath::Max(TextTag.ShimmerWidth, 0.f) + Softness + 0.5f;
						const float BandCenter = FMath::Lerp(-Reach, static_cast<float>(SpanLetters - 1) + Reach, Sweep);
						const float Distance = FMath::Abs(InSpan - BandCenter);
						const float Weight = FMath::Clamp(1.f - FMath::Max(Distance - TextTag.ShimmerWidth, 0.f) / Softness, 0.f, 1.f) * FMath::Clamp(TextTag.ShimmerStrength, 0.f, 1.f);
						if (Weight > 0.f)
						{
							FLinearColor Band = TextTag.ShimmerColor * Tint;
							Band.A = Color.A;
							Color = FMath::Lerp(Color, Band, Weight);
							bRecolored = true;
						}
					}
				}
			}
		}

		if (!Offset.IsZero() || EffectScale != 1.f || EffectAngle != 0.f)
		{
			Composed = Concatenate(Composed, WidgetAnimTextDraw::PoseAbout(Offset, FVector2D(EffectScale, EffectScale), FVector2D::ZeroVector, EffectAngle, Center));
			bPosed = true;
		}

		if (Fx)
		{
			// A reveal: not drawn before its part is reached - or drawn faintly, as the text still to come - and on its
			// way in, by its appearance, while it arrives.
			if (Fx->bReveal)
			{
				const int32 Place = RevealPlace(LetterIndex, Word, Letter.Line);
				const float Alpha = Fx->RevealAlphas.IsValidIndex(Place) ? FMath::Clamp(Fx->RevealAlphas[Place], 0.f, 1.f) : 0.f;

				// The text still to come, faint and in its place, moving as the letters around it do.
				auto DrawGhost = [&](float GhostShare)
				{
					if (bGlowOnly || !Fx->bRevealGhost || GhostShare <= 0.f)
					{
						return;
					}
					FLinearColor GhostColor = Color;
					if (Fx->bGhostColor)
					{
						GhostColor = Fx->GhostColor * Tint;
						GhostColor.A = Color.A * Fx->GhostColor.A;
					}
					DrawPosed(Glyphs, Letter.Run, Box.Min, Box.GetSize(), bPosed, Composed, Opacity * FMath::Clamp(Fx->GhostOpacity, 0.f, 1.f) * GhostShare, &GhostColor, 0.f, nullptr);
				};

				if (Alpha <= 0.f)
				{
					bAllShown = false;
					DrawGhost(1.f);
					continue;
				}
				LastShownLetter = LetterIndex;
				bAllShown = bAllShown && Alpha >= 1.f;

				if (Alpha < 1.f)
				{
					// A tag's own reveal look wins over the reveal's.
					const FWidgetAnimTextSpan* LookSpan = Style.Markup.IsValid() ? Style.Markup->FindRevealLookAt(Letters[LetterIndex].Begin) : nullptr;
					const FWidgetAnimTextAppear& Appear = LookSpan ? LookSpan->Appear : Fx->Appear;
					const float Eased = Appear.Ease(Alpha);
					const float Arrived = FMath::Clamp(Eased, 0.f, 1.f);
					const float Remaining = 1.f - Eased;

					DrawGhost(1.f - Arrived);

					Opacity *= FMath::Lerp(FMath::Clamp(Appear.Opacity, 0.f, 1.f), 1.f, Arrived);
					const float AppearScale = FMath::Lerp(Appear.Scale, 1.f, Eased);
					const FVector2D AppearStretch = FMath::Lerp(Appear.Stretch, FVector2D(1.0, 1.0), static_cast<double>(Eased));
					FVector2D AppearOffset = Appear.Offset * Remaining;
					if (Appear.Scatter > 0.f)
					{
						const float Theta = WidgetAnimTextDraw::Random(Place, 0x5CA7u, 0x77u) * PI;
						AppearOffset += FVector2D(FMath::Cos(Theta), FMath::Sin(Theta)) * (Appear.Scatter * Remaining);
					}
					Blur = FMath::Max(Appear.Blur, 0.f) * FMath::Max(Remaining, 0.f);

					const FBox2f PartBox = RevealBox(LetterIndex, Word, Letter.Line);
					Composed = Concatenate(Composed, WidgetAnimTextDraw::PoseAbout(AppearOffset, AppearStretch * AppearScale, FVector2D::ZeroVector, Appear.Angle * Remaining, PartBox.GetCenter()));
					bPosed = true;

					if (Appear.bWipe)
					{
						// Uncovered from the left of its part, with room above and below for anything that overhangs.
						const float Pad = PartBox.GetSize().Y;
						Clip = FBox2f(FVector2f(PartBox.Min.X - 2.f, PartBox.Min.Y - Pad), FVector2f(PartBox.Min.X + PartBox.GetSize().X * Arrived, PartBox.Max.Y + Pad));
						bClip = true;
					}
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
			if (Fx->bShimmer && WidgetAnimTextParts::Covers(Fx->ShimmerLetters, LetterIndex))
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
		for (int32 SpanAt = 0; SpanAt < SpanPoses.Num(); ++SpanAt)
		{
			const FWidgetAnimPartKey& SpanPart = Poses[SpanPoses[SpanAt]].Part;
			if (LetterIndex >= SpanPart.Index && LetterIndex < SpanPart.Index + SpanPart.Count && SpanBoxes[SpanAt].bIsValid)
			{
				ApplyPose(SpanPoses[SpanAt], SpanBoxes[SpanAt]);
			}
		}
		if (LinePose.IsValidIndex(Letter.Line) && LineBoxes.IsValidIndex(Letter.Line))
		{
			ApplyPose(LinePose[Letter.Line], LineBoxes[Letter.Line]);
		}

		if (Opacity <= KINDA_SMALL_NUMBER || !Glyphs.IsValid())
		{
			continue;
		}

		const FLinearColor* ColorOverride = bRecolored ? &Color : nullptr;

		// The glow layer: the letter in its glow color - the tag's, or its own as drawn - at its tag's share of the
		// strongest glow in the text (the material scales the rest), dipping as its glow pulses. Everything else about
		// it - where it is, how it moves, how far it has arrived - is as the text draws it.
		if (bGlowOnly)
		{
			if (!GlowTag)
			{
				continue;
			}
			FLinearColor GlowColor = Color;
			if (!GlowTag->bGlowInTextColor)
			{
				GlowColor = GlowTag->GlowColor * Tint;
				GlowColor.A = GlowTag->GlowColor.A * Color.A;
			}
			float GlowShare = GlowTag->GlowStrength / FMath::Max(GlowStrengthScale, KINDA_SMALL_NUMBER);
			if (GlowTag->GlowPulse > 0.f)
			{
				const float Phase = static_cast<float>(FMath::Fmod(Now * GlowTag->GlowPulseSpeed, 1.0));
				GlowShare *= 1.f - FMath::Clamp(GlowTag->GlowPulse, 0.f, 1.f) * (0.5f - 0.5f * FMath::Cos(2.f * PI * Phase));
			}
			GlowColor.A *= FMath::Clamp(GlowShare, 0.f, 1.f);
			const FVector2f GlowAt = Box.Min + (bChangedHere ? Glide : FVector2f::ZeroVector);
			DrawPosed(Glyphs, Letter.Run, GlowAt, Box.GetSize(), bPosed, Composed, Opacity, &GlowColor, Blur, bClip ? &Clip : nullptr);
			continue;
		}

		// A changed character: the old one leaving and the new one coming in, or a digit turning on its reel.
		if (bChangedHere && !bStandIn)
		{
			const FVector2f At = Box.Min + Glide;
			const float Width = OldLetter ? FMath::Max(Box.GetSize().X, OldLetter->Box.GetSize().X) : Box.GetSize().X;
			const FVector2f Extent(Width, Box.GetSize().Y);
			const FBox2f Window = RollClip(FBox2f(At, At + Box.GetSize()), Width);
			const FBox2f* ChangeClip = bRollingChange ? &Window : nullptr;

			if (ChangeLetter->Kind == EChangeKind::Wheel)
			{
				const FChangeWheel* Wheel = Change.Wheels.Find(ChangeLetter->WheelKey);
				const double Position = Wheel ? FMath::Lerp(Wheel->From, Wheel->To, static_cast<double>(ChangeProgress)) : 0.0;
				const double Base = FMath::FloorToDouble(Position);
				const float Fraction = static_cast<float>(Position - Base);
				const bool bSlot = Change.Style == EWidgetAnimTextChangeStyle::SlotMachine;

				// Higher digits sit below (or to the right) on the reel, so turning it up brings the next one in from there.
				for (int32 Step = 0; Step < 2; ++Step)
				{
					if (Step == 1 && Fraction <= KINDA_SMALL_NUMBER)
					{
						break;
					}
					const int64 Unwrapped = static_cast<int64>(Base) + Step;
					const TCHAR DigitChar = static_cast<TCHAR>(TEXT('0') + static_cast<int32>(((Unwrapped % 10) + 10) % 10));
					const FShapedGlyphSequencePtr DigitGlyphs = GetCharGlyphs(Layout, DigitChar, Letter.Run);
					if (!DigitGlyphs.IsValid())
					{
						continue;
					}

					const float Along = static_cast<float>(Step) - Fraction;
					WidgetAnimTextDraw::FChangePose Pose;
					Pose.Offset = bSlot ? FVector2f(0.f, Along * Extent.Y) : FVector2f(Along * Extent.X, 0.f);
					Pose.Opacity = 1.f - 0.6f * FMath::Min(FMath::Abs(Along), 1.f);

					const float DigitWidth = WidgetAnimTextDraw::MeasureRange(*DigitGlyphs, 0, 1) * InverseScale;
					const FVector2f DigitAt(At.X + (Box.GetSize().X - DigitWidth) * 0.5f, At.Y);
					DrawChangeGlyphs(DigitGlyphs, Letter.Run, DigitAt, FVector2f(DigitWidth, Box.GetSize().Y), Pose, bPosed, Composed, Opacity, ColorOverride, ChangeClip);
				}
				continue;
			}

			// The old character going, when there was one and its glyphs are still good to draw.
			if (ChangeLetter->Kind == EChangeKind::Swap && OldLetter && bOldGlyphsUsable)
			{
				const WidgetAnimTextDraw::FChangePose Leaving = WidgetAnimTextDraw::GetChangePose(Change.Style, /*bEnter=*/false, ChangeProgress, Change.Sign, Extent);
				DrawChangeGlyphs(OldLetter->Glyphs, OldLetter->Run, At, OldLetter->Box.GetSize(), Leaving, bPosed, Composed, Opacity, ColorOverride, ChangeClip);
			}

			const WidgetAnimTextDraw::FChangePose Entering = WidgetAnimTextDraw::GetChangePose(Change.Style, /*bEnter=*/true, ChangeProgress, Change.Sign, Extent);
			DrawChangeGlyphs(Glyphs, Letter.Run, At, Box.GetSize(), Entering, bPosed, Composed, Opacity, ColorOverride, ChangeClip);
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

		DrawPosed(Glyphs, Letter.Run, Origin, Size, bPosed, Composed, Opacity, ColorOverride, Blur, bClip ? &Clip : nullptr);
	}

	// Old characters with nothing to go to - a number that lost a digit - leave where they were. Not glowing.
	if (!bGlowOnly && bChanging && bOldGlyphsUsable)
	{
		for (const int32 OldIndex : Change.Leaving)
		{
			if (!Change.Old.IsValidIndex(OldIndex))
			{
				continue;
			}
			const FDrawnLetter& Old = Change.Old[OldIndex];
			const FVector2f OldSize = Old.Box.GetSize();
			const FBox2f Window = RollClip(Old.Box, OldSize.X);
			const WidgetAnimTextDraw::FChangePose Leaving = WidgetAnimTextDraw::GetChangePose(Change.Style, /*bEnter=*/false, ChangeProgress, Change.Sign, OldSize);
			DrawChangeGlyphs(Old.Glyphs, Old.Run, Old.Box.Min, OldSize, Leaving, /*bBasePosed=*/false, FSlateRenderTransform(), 1.f, nullptr, bRollingChange ? &Window : nullptr);
		}
	}

	// A typing cursor just after the last letter shown, while there is more to come - blinking, twice a second.
	if (Fx && Fx->bReveal && !Fx->Cursor.IsEmpty() && !bAllShown && FSlateApplication::IsInitialized())
	{
		const bool bBlinkOn = !Fx->bCursorBlink || FMath::Fmod(Now, 1.0) < 0.5;
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

	// The letters at rest, for the next change to leave from.
	if (bKeepDrawn)
	{
		Drawn.SetNum(NumLetters);
		for (int32 LetterIndex = 0; LetterIndex < NumLetters; ++LetterIndex)
		{
			FDrawnLetter& Kept = Drawn[LetterIndex];
			Kept.Chars = Style.Text.Mid(Letters[LetterIndex].Begin, Letters[LetterIndex].End - Letters[LetterIndex].Begin);
			Kept.Box = LetterBoxes[LetterIndex];
			Kept.Glyphs = Layout.Letters[LetterIndex].Glyphs;
			Kept.Run = Layout.Letters[LetterIndex].Run;
		}
		DrawnText = Style.Text;
		DrawnScale = Layout.Scale;
	}

	return LayerId + 1;
}
