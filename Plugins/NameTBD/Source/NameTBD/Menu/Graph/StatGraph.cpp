// Copyright AR GameSystems 2026. All rights reserved.

#include "StatGraph.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "StatGraph"

namespace StatGraphLocal
{
	/** A readable number at or above Value (1, 2, 2.5, 5 x 10^n). */
	float NiceCeiling(float Value)
	{
		if (Value <= 0.f)
		{
			return 1.f;
		}
		const float Exponent = FMath::Floor(FMath::LogX(10.f, Value));
		const float Base = FMath::Pow(10.f, Exponent);
		for (const float Step : { 1.f, 2.f, 2.5f, 5.f, 10.f })
		{
			if (Value <= Step * Base + KINDA_SMALL_NUMBER)
			{
				return Step * Base;
			}
		}
		return 10.f * Base;
	}

	float LineHeight(const FSlateFontInfo& Font)
	{
		return static_cast<float>(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->GetMaxCharacterHeight(Font));
	}

	FVector2f Measure(const FText& Text, const FSlateFontInfo& Font)
	{
		return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
	}

	template <typename TSource>
	TSource* MakeSource(UObject* Outer)
	{
		return NewObject<TSource>(Outer, NAME_None, RF_Transactional);
	}

	FStatGraphSeries MakeSeries(UStatGraphSource* Source, const FLinearColor& Color, EStatGraphStyle Style, bool bGraphed = true)
	{
		FStatGraphSeries Series;
		Series.Source = Source;
		Series.Color = Color;
		Series.Style = Style;
		Series.bGraphed = bGraphed;
		return Series;
	}

	const FLinearColor FrameColor(0.95f, 0.95f, 1.f, 1.f);
	const FLinearColor GameColor(0.35f, 0.8f, 1.f, 1.f);
	const FLinearColor RenderColor(0.75f, 0.5f, 1.f, 1.f);
	const FLinearColor GpuColor(1.f, 0.62f, 0.25f, 1.f);
	const FLinearColor FpsColor(0.35f, 0.95f, 0.55f, 1.f);
}

// =====================================================================================================
//  Buffer
// =====================================================================================================

void FStatGraphBuffer::Push(float Value, int32 Capacity)
{
	Capacity = FMath::Max(2, Capacity);
	if (Data.Num() != Capacity)
	{
		// The capacity changed (history or rate edited): keep the newest samples.
		TArray<float> Kept;
		const int32 Keep = FMath::Min(Count, Capacity);
		for (int32 Index = Count - Keep; Index < Count; ++Index)
		{
			Kept.Add(Get(Index));
		}
		Data.SetNumZeroed(Capacity);
		for (int32 Index = 0; Index < Kept.Num(); ++Index)
		{
			Data[Index] = Kept[Index];
		}
		Start = 0;
		Count = Kept.Num();
	}
	if (Count < Capacity)
	{
		Data[(Start + Count) % Capacity] = Value;
		++Count;
	}
	else
	{
		Data[Start] = Value;
		Start = (Start + 1) % Capacity;
	}
}

// =====================================================================================================
//  Widget
// =====================================================================================================

UStatGraph::UStatGraph()
{
	HeadlineFont = FCoreStyle::GetDefaultFontStyle("Bold", 20);
	LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	// The preset's series (sources are objects: not made in a constructor) come with the palette, or when first shown.
}

#if WITH_EDITOR
const FText UStatGraph::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}

void UStatGraph::OnCreationFromPalette()
{
	Super::OnCreationFromPalette();
	ApplyPreset(Preset);
}

void UStatGraph::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UStatGraph, Preset) && Preset != EStatGraphPreset::Custom)
	{
		ApplyPreset(Preset);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UStatGraph::ApplyPreset(EStatGraphPreset InPreset)
{
	using namespace StatGraphLocal;
	Preset = InPreset;
	if (InPreset == EStatGraphPreset::Custom)
	{
		return;
	}

	Series.Reset();
	ReferenceLines.Reset();
	Title = FText::GetEmpty();
	bShowGraph = true;
	bShowLegend = true;
	bShowHeadline = true;
	bShowAverage = true;
	bShowMinMax = false;
	bShowWorst1Percent = true;
	bShowAxisLabels = true;
	bColorByThreshold = true;
	Range = EStatGraphRange::AutoFromZero;
	SampleInterval = 0.f;
	HistorySeconds = 6.f;
	Decimals = 1;
	GraphSize = FVector2D(260.f, 80.f);

	auto AddLine = [this](float Value, const FText& LineLabel)
	{
		FStatGraphLine& Line = ReferenceLines.AddDefaulted_GetRef();
		Line.Value = Value;
		Line.Label = LineLabel;
	};

	switch (InPreset)
	{
	case EStatGraphPreset::CompactFPS:
		Series.Add(MakeSeries(MakeSource<UStatGraphSource_FrameRate>(this), FpsColor, EStatGraphStyle::Area));
		bShowLegend = false;
		bShowAxisLabels = false;
		GraphSize = FVector2D(120.f, 26.f);
		Decimals = 0;
		WarningThreshold = 60.f;
		BadThreshold = 30.f;
		HistorySeconds = 4.f;
		break;
	case EStatGraphPreset::FPS:
		Series.Add(MakeSeries(MakeSource<UStatGraphSource_FrameRate>(this), FpsColor, EStatGraphStyle::Area));
		AddLine(60.f, LOCTEXT("Line60", "60"));
		AddLine(30.f, LOCTEXT("Line30", "30"));
		Decimals = 0;
		WarningThreshold = 60.f;
		BadThreshold = 30.f;
		break;
	case EStatGraphPreset::FrameTimes:
	case EStatGraphPreset::Detailed:
	{
		Series.Add(MakeSeries(MakeSource<UStatGraphSource_FrameTime>(this), FrameColor, EStatGraphStyle::Area));
		UStatGraphSource_ThreadTime* Game = MakeSource<UStatGraphSource_ThreadTime>(this);
		Game->Thread = EStatGraphThread::Game;
		Series.Add(MakeSeries(Game, GameColor, EStatGraphStyle::Line));
		UStatGraphSource_ThreadTime* Render = MakeSource<UStatGraphSource_ThreadTime>(this);
		Render->Thread = EStatGraphThread::Render;
		Series.Add(MakeSeries(Render, RenderColor, EStatGraphStyle::Line));
		UStatGraphSource_ThreadTime* Gpu = MakeSource<UStatGraphSource_ThreadTime>(this);
		Gpu->Thread = EStatGraphThread::GPU;
		Series.Add(MakeSeries(Gpu, GpuColor, EStatGraphStyle::Line));
		AddLine(16.67f, LOCTEXT("Line16", "16.7"));
		AddLine(33.33f, LOCTEXT("Line33", "33.3"));
		// For frame times lower is better: warn past a 60 fps frame, bad past a 30 fps one.
		WarningThreshold = 16.67f;
		BadThreshold = 33.33f;
		MinimumSpan = 20.f;
		if (InPreset == EStatGraphPreset::Detailed)
		{
			// Listed, not graphed: a different scale. The first listed is the headline: fps.
			Series.Insert(MakeSeries(MakeSource<UStatGraphSource_FrameRate>(this), FpsColor, EStatGraphStyle::Line, false), 0);
			Series[0].Decimals = 0;
			Series.Add(MakeSeries(MakeSource<UStatGraphSource_Memory>(this), FLinearColor(0.8f, 0.8f, 0.85f, 1.f), EStatGraphStyle::Line, false));
			Series.Last().Decimals = 0;
			bShowMinMax = true;
			GraphSize = FVector2D(300.f, 90.f);
		}
		else
		{
			bShowHeadline = false;
		}
		break;
	}
	case EStatGraphPreset::Memory:
		Series.Add(MakeSeries(MakeSource<UStatGraphSource_Memory>(this), FLinearColor(0.8f, 0.85f, 1.f, 1.f), EStatGraphStyle::Area));
		Decimals = 0;
		HistorySeconds = 60.f;
		SampleInterval = 0.5f;
		bColorByThreshold = false;
		bShowWorst1Percent = false;
		bShowMinMax = true;
		MinimumSpan = 256.f;
		break;
	case EStatGraphPreset::Network:
		Series.Add(MakeSeries(MakeSource<UStatGraphSource_Ping>(this), FLinearColor(0.4f, 0.85f, 1.f, 1.f), EStatGraphStyle::Line));
		Decimals = 0;
		HistorySeconds = 30.f;
		SampleInterval = 0.25f;
		WarningThreshold = 80.f;
		BadThreshold = 150.f;
		MinimumSpan = 50.f;
		break;
	default:
		break;
	}
	ClearGraph();
}

int32 UStatGraph::GetCapacity() const
{
	// Every-frame sampling sizes for 120 fps; past that the graph shows a little less time.
	const float Rate = SampleInterval > 0.f ? 1.f / SampleInterval : 120.f;
	return FMath::Clamp(FMath::CeilToInt(HistorySeconds * Rate), 8, 4096);
}

void UStatGraph::ClearGraph()
{
	Buffers.Reset();
	CachedStats.Reset();
	SinceSample = 0.f;
	FramesSinceSample = 0;
	SinceReadout = 0.f;
	DisplayMin = Range == EStatGraphRange::Fixed ? Minimum : 0.f;
	DisplayMax = Range == EStatGraphRange::Fixed ? Maximum : FMath::Max(MinimumSpan, 1.f);
}

void UStatGraph::SetGraphPaused(bool bInPaused)
{
	bPaused = bInPaused;
}

int32 UStatGraph::AddSeries(const FStatGraphSeries& NewSeries)
{
	Preset = EStatGraphPreset::Custom;
	return Series.Add(NewSeries);
}

void UStatGraph::PushValue(int32 SeriesIndex, float Value)
{
	if (Series.IsValidIndex(SeriesIndex))
	{
		if (UStatGraphSource_Manual* Manual = Cast<UStatGraphSource_Manual>(Series[SeriesIndex].Source))
		{
			Manual->Push(Value);
		}
	}
}

void UStatGraph::TickSamples(float DeltaTime)
{
	if (bPaused || DeltaTime <= 0.f)
	{
		return;
	}
	SinceSample += DeltaTime;
	++FramesSinceSample;
	if (SinceSample < SampleInterval)
	{
		return;
	}

	FStatGraphSampleContext Context;
	Context.DeltaTime = SinceSample;
	Context.Frames = FramesSinceSample;
	if (!IsDesignTime())
	{
		Context.World = GetWorld();
		Context.Player = GetOwningPlayer();
		if (!Context.Player && Context.World)
		{
			Context.Player = Context.World->GetFirstPlayerController();
		}
	}
	AverageSampleSeconds = FMath::Lerp(AverageSampleSeconds, SinceSample, 0.1f);
	SinceSample = 0.f;
	FramesSinceSample = 0;

	Buffers.SetNum(Series.Num());
	CachedStats.SetNum(Series.Num());
	const int32 Capacity = GetCapacity();
	for (int32 Index = 0; Index < Series.Num(); ++Index)
	{
		float Value = 0.f;
		if (Series[Index].Source && Series[Index].Source->SampleValue(Context, Value))
		{
			Buffers[Index].Push(Value, Capacity);
		}
	}

	SinceReadout += Context.DeltaTime;
	if (SinceReadout >= ReadoutSeconds || CachedStats.Num() == 0)
	{
		SinceReadout = 0.f;
		for (int32 Index = 0; Index < Series.Num(); ++Index)
		{
			CachedStats[Index] = ComputeStats(Index);
		}
	}
	UpdateRange(Context.DeltaTime);
}

FStatGraphStats UStatGraph::ComputeStats(int32 SeriesIndex) const
{
	FStatGraphStats Stats;
	if (!Buffers.IsValidIndex(SeriesIndex) || Buffers[SeriesIndex].Count == 0)
	{
		return Stats;
	}
	const FStatGraphBuffer& Buffer = Buffers[SeriesIndex];
	TArray<float> Sorted;
	Sorted.Reserve(Buffer.Count);
	double Sum = 0.0;
	Stats.Min = TNumericLimits<float>::Max();
	Stats.Max = TNumericLimits<float>::Lowest();
	for (int32 Index = 0; Index < Buffer.Count; ++Index)
	{
		const float Value = Buffer.Get(Index);
		Sorted.Add(Value);
		Sum += Value;
		Stats.Min = FMath::Min(Stats.Min, Value);
		Stats.Max = FMath::Max(Stats.Max, Value);
	}
	Stats.Samples = Buffer.Count;
	Stats.Average = static_cast<float>(Sum / Buffer.Count);

	// The current value: the mean over the last Readout Seconds.
	const int32 Recent = FMath::Clamp(FMath::RoundToInt(ReadoutSeconds / FMath::Max(AverageSampleSeconds, 0.001f)), 1, Buffer.Count);
	double RecentSum = 0.0;
	for (int32 Index = Buffer.Count - Recent; Index < Buffer.Count; ++Index)
	{
		RecentSum += Buffer.Get(Index);
	}
	Stats.Current = static_cast<float>(RecentSum / Recent);

	Sorted.Sort();
	const bool bHigherBetter = Series.IsValidIndex(SeriesIndex) && Series[SeriesIndex].Source && Series[SeriesIndex].Source->IsHigherBetter();
	const int32 Worst = FMath::Max(1, FMath::CeilToInt(Sorted.Num() * 0.01f));
	double WorstSum = 0.0;
	for (int32 Index = 0; Index < Worst; ++Index)
	{
		WorstSum += bHigherBetter ? Sorted[Index] : Sorted[Sorted.Num() - 1 - Index];
	}
	Stats.Worst1Percent = static_cast<float>(WorstSum / Worst);
	return Stats;
}

void UStatGraph::UpdateRange(float DeltaTime)
{
	if (Range == EStatGraphRange::Fixed)
	{
		DisplayMin = Minimum;
		DisplayMax = FMath::Max(Maximum, Minimum + KINDA_SMALL_NUMBER);
		return;
	}
	float Low = TNumericLimits<float>::Max();
	float High = TNumericLimits<float>::Lowest();
	for (int32 Index = 0; Index < Series.Num() && Index < Buffers.Num(); ++Index)
	{
		if (!Series[Index].bGraphed)
		{
			continue;
		}
		const FStatGraphBuffer& Buffer = Buffers[Index];
		for (int32 Sample = 0; Sample < Buffer.Count; ++Sample)
		{
			Low = FMath::Min(Low, Buffer.Get(Sample));
			High = FMath::Max(High, Buffer.Get(Sample));
		}
	}
	if (High < Low)
	{
		return;
	}
	float TargetMin = Range == EStatGraphRange::AutoFromZero ? 0.f : Low - (High - Low) * 0.1f;
	float TargetMax = StatGraphLocal::NiceCeiling(FMath::Max(High * 1.1f, TargetMin + MinimumSpan));
	if (Range == EStatGraphRange::Auto)
	{
		TargetMax = FMath::Max(High + (High - Low) * 0.1f, TargetMin + MinimumSpan);
	}
	// Eased, so a spike doesn't make the whole graph jump.
	const float Alpha = 1.f - FMath::Exp(-DeltaTime * 4.f);
	DisplayMin = FMath::Lerp(DisplayMin, TargetMin, Alpha);
	DisplayMax = TargetMax > DisplayMax ? TargetMax : FMath::Lerp(DisplayMax, TargetMax, Alpha);
}

const FStatGraphStats& UStatGraph::GetCachedStats(int32 SeriesIndex) const
{
	static const FStatGraphStats Empty;
	return CachedStats.IsValidIndex(SeriesIndex) ? CachedStats[SeriesIndex] : Empty;
}

FStatGraphStats UStatGraph::GetSeriesStats(int32 SeriesIndex) const
{
	return ComputeStats(SeriesIndex);
}

FText UStatGraph::FormatValue(int32 SeriesIndex, float Value) const
{
	const int32 Places = Series.IsValidIndex(SeriesIndex) && Series[SeriesIndex].Decimals >= 0 ? Series[SeriesIndex].Decimals : Decimals;
	FNumberFormattingOptions Options;
	Options.MinimumFractionalDigits = Places;
	Options.MaximumFractionalDigits = Places;
	Options.UseGrouping = false;
	return FText::AsNumber(Value, &Options);
}

FLinearColor UStatGraph::GetThresholdColor(int32 SeriesIndex, float Value) const
{
	if (!bColorByThreshold || !Series.IsValidIndex(SeriesIndex) || !Series[SeriesIndex].Source)
	{
		return TextColor;
	}
	if (Series[SeriesIndex].Source->IsHigherBetter())
	{
		return Value >= WarningThreshold ? GoodColor : Value >= BadThreshold ? WarningColor : BadColor;
	}
	// Lower is better (ms): only series in the thresholds' units are coloured - memory isn't.
	if (!Series[SeriesIndex].Source->GetUnitsText().EqualTo(LOCTEXT("MsUnits", "ms")))
	{
		return TextColor;
	}
	return Value <= WarningThreshold ? GoodColor : Value <= BadThreshold ? WarningColor : BadColor;
}

TSharedRef<SWidget> UStatGraph::RebuildWidget()
{
	if (Series.Num() == 0 && Preset != EStatGraphPreset::Custom)
	{
		// Made in code (CreateWidget / NewObject) without a preset applied: the preset's series now.
		ApplyPreset(Preset);
	}
	GraphWidget = SNew(SStatGraph, this);
	return GraphWidget.ToSharedRef();
}

void UStatGraph::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (GraphWidget.IsValid())
	{
		GraphWidget->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void UStatGraph::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	GraphWidget.Reset();
}

// =====================================================================================================
//  Slate
// =====================================================================================================

void SStatGraph::Construct(const FArguments& InArgs, UStatGraph* InOwner)
{
	Owner = InOwner;
	SetCanTick(false);
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SStatGraph::HandleTimer));
}

EActiveTimerReturnType SStatGraph::HandleTimer(double InCurrentTime, float InDeltaTime)
{
	if (UStatGraph* Graph = Owner.Get())
	{
		Graph->TickSamples(FMath::Min(InDeltaTime, 1.f));
		Invalidate(EInvalidateWidgetReason::Paint);
		return EActiveTimerReturnType::Continue;
	}
	return EActiveTimerReturnType::Stop;
}

FVector2D SStatGraph::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const UStatGraph* Graph = Owner.Get();
	if (!Graph)
	{
		return FVector2D(100.f, 40.f);
	}
	using namespace StatGraphLocal;
	double Width = Graph->bShowGraph ? Graph->GraphSize.X : 0.0;
	double Height = Graph->bShowGraph ? Graph->GraphSize.Y : 0.0;
	const float Line = LineHeight(Graph->LabelFont);
	if (Graph->bShowHeadline || !Graph->Title.IsEmpty())
	{
		Height += FMath::Max(Graph->bShowHeadline ? LineHeight(Graph->HeadlineFont) : 0.f, Line) + 4.f;
		Width = FMath::Max(Width, 110.0);
	}
	if (Graph->bShowLegend)
	{
		int32 Listed = 0;
		for (const FStatGraphSeries& Entry : Graph->Series)
		{
			Listed += Entry.bListed ? 1 : 0;
		}
		Height += Listed * (Line + 2.f) + (Listed > 0 ? 4.f : 0.f);
		Width = FMath::Max(Width, 240.0);
	}
	return FVector2D(Width + Graph->Padding.GetTotalSpaceAlong<Orient_Horizontal>(), Height + Graph->Padding.GetTotalSpaceAlong<Orient_Vertical>());
}

int32 SStatGraph::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const UStatGraph* Graph = Owner.Get();
	if (!Graph)
	{
		return LayerId;
	}
	using namespace StatGraphLocal;
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const ESlateDrawEffect Effect = ESlateDrawEffect::None;

	// Background.
	if (Graph->BackgroundColor.A > 0.f)
	{
		const FSlateRoundedBoxBrush Background(Graph->BackgroundColor, Graph->CornerRadius);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &Background, Effect, Tint);
	}
	const int32 TextLayer = LayerId + 3;

	float Y = Graph->Padding.Top;
	const float Left = Graph->Padding.Left;
	const float Right = Size.X - Graph->Padding.Right;
	const float Line = LineHeight(Graph->LabelFont);

	auto DrawText = [&](const FText& Text, const FSlateFontInfo& Font, const FVector2f& At, const FLinearColor& Color)
	{
		const FVector2f TextSize = Measure(Text, Font);
		FSlateDrawElement::MakeText(OutDrawElements, TextLayer, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(At)), Text, Font, Effect, Color * Tint);
		return TextSize;
	};

	// Headline: the first listed series' value, large, with its units; the title on the right.
	int32 FirstListed = INDEX_NONE;
	for (int32 Index = 0; Index < Graph->Series.Num(); ++Index)
	{
		if (Graph->Series[Index].bListed)
		{
			FirstListed = Index;
			break;
		}
	}
	if (Graph->bShowHeadline || !Graph->Title.IsEmpty())
	{
		float RowHeight = Line;
		if (Graph->bShowHeadline && FirstListed != INDEX_NONE)
		{
			const FStatGraphStats& Stats = Graph->GetCachedStats(FirstListed);
			const UStatGraphSource* Source = Graph->Series[FirstListed].Source;
			const FVector2f ValueSize = DrawText(Graph->FormatValue(FirstListed, Stats.Current), Graph->HeadlineFont, FVector2f(Left, Y), Graph->GetThresholdColor(FirstListed, Stats.Current));
			const FText Units = Source ? Source->GetUnitsText() : FText::GetEmpty();
			DrawText(Units, Graph->LabelFont, FVector2f(Left + ValueSize.X + 4.f, Y + ValueSize.Y - Line - 2.f), Graph->MutedTextColor);
			RowHeight = FMath::Max(RowHeight, ValueSize.Y);
		}
		if (!Graph->Title.IsEmpty())
		{
			const FVector2f TitleSize = Measure(Graph->Title, Graph->LabelFont);
			DrawText(Graph->Title, Graph->LabelFont, FVector2f(Right - TitleSize.X, Y), Graph->MutedTextColor);
		}
		Y += RowHeight + 4.f;
	}

	// Legend: a swatch, the name, the value and the stats asked for.
	if (Graph->bShowLegend)
	{
		const FSlateBrush* White = FCoreStyle::Get().GetBrush("GenericWhiteBox");
		for (int32 Index = 0; Index < Graph->Series.Num(); ++Index)
		{
			const FStatGraphSeries& Entry = Graph->Series[Index];
			if (!Entry.bListed)
			{
				continue;
			}
			const FStatGraphStats& Stats = Graph->GetCachedStats(Index);
			const FText Name = !Entry.Label.IsEmpty() ? Entry.Label : (Entry.Source ? Entry.Source->GetDefaultLabel() : FText::GetEmpty());
			const FText Units = Entry.Source ? Entry.Source->GetUnitsText() : FText::GetEmpty();
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2f(8.f, 3.f), FSlateLayoutTransform(FVector2f(Left, Y + Line * 0.5f - 1.5f))), White, Effect, Entry.Color * Tint);
			DrawText(Name, Graph->LabelFont, FVector2f(Left + 12.f, Y), Graph->MutedTextColor);

			TArray<FText> Parts;
			Parts.Add(FText::Format(LOCTEXT("ValueUnits", "{0} {1}"), Graph->FormatValue(Index, Stats.Current), Units));
			if (Graph->bShowAverage)
			{
				Parts.Add(FText::Format(LOCTEXT("Avg", "avg {0}"), Graph->FormatValue(Index, Stats.Average)));
			}
			if (Graph->bShowMinMax)
			{
				Parts.Add(FText::Format(LOCTEXT("MinMax", "{0}-{1}"), Graph->FormatValue(Index, Stats.Min), Graph->FormatValue(Index, Stats.Max)));
			}
			if (Graph->bShowWorst1Percent && Entry.Source && (Entry.Source->IsHigherBetter() || Entry.Source->GetUnitsText().EqualTo(LOCTEXT("MsUnits", "ms"))))
			{
				Parts.Add(FText::Format(Entry.Source->IsHigherBetter() ? LOCTEXT("Low", "1% low {0}") : LOCTEXT("High", "1% high {0}"), Graph->FormatValue(Index, Stats.Worst1Percent)));
			}
			const FText Readout = FText::Join(LOCTEXT("Separator", "  "), Parts);
			const FVector2f ReadoutSize = Measure(Readout, Graph->LabelFont);
			DrawText(Readout, Graph->LabelFont, FVector2f(Right - ReadoutSize.X, Y), Graph->GetThresholdColor(Index, Stats.Current));
			Y += Line + 2.f;
		}
		Y += 4.f;
	}

	if (!Graph->bShowGraph)
	{
		return TextLayer;
	}

	// The graph area.
	const float Bottom = Size.Y - Graph->Padding.Bottom;
	const float Top = Y;
	const float Height = FMath::Max(4.f, Bottom - Top);
	const float Width = FMath::Max(4.f, Right - Left);
	float RangeMin = 0.f;
	float RangeMax = 1.f;
	Graph->GetDisplayRange(RangeMin, RangeMax);
	const float Span = FMath::Max(RangeMax - RangeMin, KINDA_SMALL_NUMBER);
	auto ToY = [&](float Value) { return Bottom - FMath::Clamp((Value - RangeMin) / Span, 0.f, 1.f) * Height; };

	// Grid and reference lines.
	TArray<FVector2f> Points;
	for (int32 Grid = 1; Grid <= Graph->GridLines; ++Grid)
	{
		const float Value = RangeMin + Span * Grid / static_cast<float>(Graph->GridLines + 1);
		const float LineY = ToY(Value);
		Points = { FVector2f(Left, LineY), FVector2f(Right, LineY) };
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Points, Effect, Graph->GridColor * Tint, false, 1.f);
		if (Graph->bShowAxisLabels)
		{
			const FText Label = Graph->FormatValue(INDEX_NONE, Value);
			const FVector2f LabelSize = Measure(Label, Graph->LabelFont);
			DrawText(Label, Graph->LabelFont, FVector2f(Right - LabelSize.X, LineY - LabelSize.Y), Graph->MutedTextColor.CopyWithNewOpacity(Graph->MutedTextColor.A * 0.7f));
		}
	}
	for (const FStatGraphLine& Reference : Graph->ReferenceLines)
	{
		if (Reference.Value < RangeMin || Reference.Value > RangeMax)
		{
			continue;
		}
		const float LineY = ToY(Reference.Value);
		for (float DashX = Left; DashX < Right; DashX += 7.f)
		{
			Points = { FVector2f(DashX, LineY), FVector2f(FMath::Min(DashX + 4.f, Right), LineY) };
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Points, Effect, Reference.Color * Tint, false, 1.f);
		}
		if (!Reference.Label.IsEmpty())
		{
			const FVector2f LabelSize = Measure(Reference.Label, Graph->LabelFont);
			DrawText(Reference.Label, Graph->LabelFont, FVector2f(Left + 2.f, LineY - LabelSize.Y), Reference.Color);
		}
	}

	// The series, newest at the right.
	const int32 Capacity = Graph->GetCapacity();
	const float StepX = Width / static_cast<float>(FMath::Max(1, Capacity - 1));
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	const TArray<FStatGraphBuffer>& Buffers = Graph->GetBuffers();
	for (int32 Index = 0; Index < Graph->Series.Num() && Index < Buffers.Num(); ++Index)
	{
		const FStatGraphSeries& Entry = Graph->Series[Index];
		const FStatGraphBuffer& Buffer = Buffers[Index];
		if (!Entry.bGraphed || Buffer.Count < 2)
		{
			continue;
		}
		const FLinearColor Color = Entry.Color * Tint;
		Points.Reset(Buffer.Count);
		for (int32 Sample = 0; Sample < Buffer.Count; ++Sample)
		{
			const float X = Right - (Buffer.Count - 1 - Sample) * StepX;
			Points.Add(FVector2f(X, ToY(Buffer.Get(Sample))));
		}
		if (Entry.Style == EStatGraphStyle::Area || Entry.Style == EStatGraphStyle::Bars)
		{
			const FLinearColor Fill = Entry.Style == EStatGraphStyle::Area ? Color.CopyWithNewOpacity(Color.A * 0.22f) : Color.CopyWithNewOpacity(Color.A * 0.8f);
			const float BarWidth = Entry.Style == EStatGraphStyle::Bars ? FMath::Max(1.f, StepX - 1.f) : FMath::Max(1.f, StepX + 0.5f);
			for (const FVector2f& Point : Points)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
					AllottedGeometry.ToPaintGeometry(FVector2f(BarWidth, FMath::Max(0.f, Bottom - Point.Y)), FSlateLayoutTransform(FVector2f(Point.X - BarWidth, Point.Y))),
					White, Effect, Fill);
			}
		}
		if (Entry.Style != EStatGraphStyle::Bars)
		{
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Points, Effect, Color, true, Entry.Thickness);
		}
	}
	return TextLayer;
}

#undef LOCTEXT_NAMESPACE
