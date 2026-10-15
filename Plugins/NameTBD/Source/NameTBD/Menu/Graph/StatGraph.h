// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/SLeafWidget.h"
#include "StatGraphSources.h"

#include "StatGraph.generated.h"

class SStatGraph;
class UStatGraph;

/** How a line is drawn. */
UENUM(BlueprintType)
enum class EStatGraphStyle : uint8
{
	Line,
	Area UMETA(ToolTip = "A line with the area under it filled faintly."),
	Bars UMETA(ToolTip = "One bar per sample (frame time spikes stand out).")
};

/** How the vertical range is found. */
UENUM(BlueprintType)
enum class EStatGraphRange : uint8
{
	AutoFromZero UMETA(ToolTip = "From 0 to a little above the highest value shown, rounded to a readable number."),
	Auto UMETA(ToolTip = "From a little under the lowest to a little over the highest value shown."),
	Fixed UMETA(ToolTip = "Minimum to Maximum.")
};

/** A starting setup. */
UENUM(BlueprintType)
enum class EStatGraphPreset : uint8
{
	Custom UMETA(ToolTip = "Your own series and look."),
	CompactFPS UMETA(DisplayName = "Compact FPS", ToolTip = "\"87 FPS\" with a small graph: an fps counter."),
	FPS UMETA(DisplayName = "FPS", ToolTip = "Frame rate graph with average, 1% low and 60 / 30 fps lines."),
	FrameTimes UMETA(ToolTip = "Frame, game thread, render thread and GPU times graphed (ms), with 16.7 / 33.3 ms lines: where the frame goes."),
	Detailed UMETA(ToolTip = "Frame times graphed, with fps, 1% low and memory listed."),
	Memory UMETA(ToolTip = "Memory used, graphed."),
	Network UMETA(ToolTip = "Ping graphed.")
};

/** One line of a graph. */
USTRUCT(BlueprintType)
struct NAMETBD_API FStatGraphSeries
{
	GENERATED_BODY()

	/** Its name in the legend. Empty: the source's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Series")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Series")
	TObjectPtr<UStatGraphSource> Source;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Series")
	FLinearColor Color = FLinearColor(0.25f, 0.8f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Series")
	EStatGraphStyle Style = EStatGraphStyle::Line;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Series", meta = (ClampMin = "0.5", UIMax = "6"))
	float Thickness = 1.5f;

	/** Drawn on the graph (off: only listed - fps next to a frame time graph). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Series")
	bool bGraphed = true;

	/** Listed in the legend with its readout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Series")
	bool bListed = true;

	/** Decimals in its readout; -1: the graph's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Series", meta = (ClampMin = "-1", ClampMax = "3"))
	int32 Decimals = -1;
};

/** A horizontal line at a value ("60 fps", "16.7 ms"). */
USTRUCT(BlueprintType)
struct NAMETBD_API FStatGraphLine
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line")
	float Value = 16.67f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line")
	FText Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line")
	FLinearColor Color = FLinearColor(1.f, 1.f, 1.f, 0.25f);
};

/** A series' numbers over what the graph holds. */
USTRUCT(BlueprintType)
struct NAMETBD_API FStatGraphStats
{
	GENERATED_BODY()

	/** Smoothed over Readout Seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Graph")
	float Current = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Graph")
	float Average = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Graph")
	float Min = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Graph")
	float Max = 0.f;

	/** The worst 1%: the mean of the lowest 1% where higher is better (fps), of the highest 1% otherwise (ms). */
	UPROPERTY(BlueprintReadOnly, Category = "Graph")
	float Worst1Percent = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Graph")
	int32 Samples = 0;
};

/** A series' samples, oldest first (a ring). */
struct FStatGraphBuffer
{
	TArray<float> Data;
	int32 Start = 0;
	int32 Count = 0;
	float Accumulated = 0.f;
	int32 AccumulatedFrames = 0;

	void Push(float Value, int32 Capacity);
	float Get(int32 Index) const { return Data[(Start + Index) % Data.Num()]; }
	void Reset() { Data.Reset(); Start = 0; Count = 0; }
};

/** The Slate side: draws its Stat Graph's buffers. */
class NAMETBD_API SStatGraph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SStatGraph) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UStatGraph* InOwner);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	EActiveTimerReturnType HandleTimer(double InCurrentTime, float InDeltaTime);

	TWeakObjectPtr<UStatGraph> Owner;
};

/**
 * Stat Graph (palette NameTBD): a live graph of numbers over time - frame rate, frame / game / render / GPU times, memory,
 * ping, any console variable, or values of your own (Push Value, or a Blueprint source) - with a headline value, a legend with
 * average / min / max / 1% low, reference lines (60 fps, 16.7 ms) and threshold colours. Modular: each line is a series with a
 * source; pick a Preset for the usual benchmarking overlays. Real time, so it keeps running paused; live in the designer.
 */
UCLASS(meta = (DisplayName = "Stat Graph"))
class NAMETBD_API UStatGraph : public UWidget
{
	GENERATED_BODY()

public:

	UStatGraph();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Graph")
	EStatGraphPreset Preset = EStatGraphPreset::FPS;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Graph")
	TArray<FStatGraphSeries> Series;

	// ---- Sampling ----

	/** Seconds the graph spans. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sampling", meta = (ClampMin = "0.5", UIMax = "120", Units = "s"))
	float HistorySeconds = 6.f;

	/** Seconds between samples (each sample averages the frames in it). 0: every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sampling", meta = (ClampMin = "0", UIMax = "1", Units = "s"))
	float SampleInterval = 0.f;

	/** Seconds the readouts average over, and how often they change (readable, not flickering). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sampling", meta = (ClampMin = "0.05", UIMax = "2", Units = "s"))
	float ReadoutSeconds = 0.25f;

	/** Stops taking samples (the graph holds still). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sampling")
	bool bPaused = false;

	// ---- Range ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range")
	EStatGraphRange Range = EStatGraphRange::AutoFromZero;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range")
	float Minimum = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range")
	float Maximum = 100.f;

	/** An auto range never goes under this top (a quiet graph doesn't blow tiny noise up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range")
	float MinimumSpan = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Range")
	TArray<FStatGraphLine> ReferenceLines;

	// ---- Readout ----

	/** The first listed series' value large at the top ("87 FPS"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Readout")
	bool bShowHeadline = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Readout")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Readout")
	bool bShowLegend = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Readout")
	bool bShowAverage = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Readout")
	bool bShowMinMax = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Readout")
	bool bShowWorst1Percent = true;

	/** Values at the grid lines, on the right. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Readout")
	bool bShowAxisLabels = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Readout", meta = (ClampMin = "0", ClampMax = "3"))
	int32 Decimals = 1;

	// ---- Thresholds ----

	/** The headline and legend values turn Warning / Bad colours past the thresholds (fps under 60 / 30, ms over 16.7 / 33.3). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thresholds")
	bool bColorByThreshold = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thresholds", meta = (EditCondition = "bColorByThreshold"))
	float WarningThreshold = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thresholds", meta = (EditCondition = "bColorByThreshold"))
	float BadThreshold = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thresholds", meta = (EditCondition = "bColorByThreshold"))
	FLinearColor GoodColor = FLinearColor(0.35f, 0.95f, 0.45f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thresholds", meta = (EditCondition = "bColorByThreshold"))
	FLinearColor WarningColor = FLinearColor(1.f, 0.78f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Thresholds", meta = (EditCondition = "bColorByThreshold"))
	FLinearColor BadColor = FLinearColor(1.f, 0.32f, 0.3f, 1.f);

	// ---- Appearance ----

	/** The graph area (inside the padding, under the headline and legend). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FVector2D GraphSize = FVector2D(260.f, 80.f);

	/** No graph: only the headline and legend (a plain fps counter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	bool bShowGraph = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FMargin Padding = FMargin(10.f, 8.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor BackgroundColor = FLinearColor(0.02f, 0.02f, 0.03f, 0.72f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0", UIMax = "24"))
	float CornerRadius = 8.f;

	/** Horizontal grid lines across the graph. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "0", ClampMax = "10"))
	int32 GridLines = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor GridColor = FLinearColor(1.f, 1.f, 1.f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor TextColor = FLinearColor(1.f, 1.f, 1.f, 0.95f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor MutedTextColor = FLinearColor(1.f, 1.f, 1.f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FSlateFontInfo HeadlineFont;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FSlateFontInfo LabelFont;

	// ---- Blueprint ----

	/** Sets every value of a preset (series, range, lines, readouts, size). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Graph")
	void ApplyPreset(EStatGraphPreset InPreset);

	/** Gives a series with a Manual source a value (averaged with others pushed before its next sample). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Graph")
	void PushValue(int32 SeriesIndex, float Value);

	/** Adds a series; returns its index. */
	UFUNCTION(BlueprintCallable, Category = "Menu|Graph")
	int32 AddSeries(const FStatGraphSeries& NewSeries);

	UFUNCTION(BlueprintCallable, Category = "Menu|Graph")
	void ClearGraph();

	UFUNCTION(BlueprintCallable, Category = "Menu|Graph")
	void SetGraphPaused(bool bInPaused);

	UFUNCTION(BlueprintPure, Category = "Menu|Graph")
	FStatGraphStats GetSeriesStats(int32 SeriesIndex) const;

	// ---- Native: what SStatGraph reads ----

	void TickSamples(float DeltaTime);
	int32 GetCapacity() const;
	const TArray<FStatGraphBuffer>& GetBuffers() const { return Buffers; }
	const FStatGraphStats& GetCachedStats(int32 SeriesIndex) const;
	FText FormatValue(int32 SeriesIndex, float Value) const;
	FLinearColor GetThresholdColor(int32 SeriesIndex, float Value) const;
	void GetDisplayRange(float& OutMin, float& OutMax) const { OutMin = DisplayMin; OutMax = DisplayMax; }

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void OnCreationFromPalette() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	FStatGraphStats ComputeStats(int32 SeriesIndex) const;
	void UpdateRange(float DeltaTime);

	TSharedPtr<SStatGraph> GraphWidget;
	TArray<FStatGraphBuffer> Buffers;
	TArray<FStatGraphStats> CachedStats;
	float SinceSample = 0.f;
	int32 FramesSinceSample = 0;
	float SinceReadout = 0.f;
	float AverageSampleSeconds = 1.f / 60.f;
	float DisplayMin = 0.f;
	float DisplayMax = 1.f;
};
