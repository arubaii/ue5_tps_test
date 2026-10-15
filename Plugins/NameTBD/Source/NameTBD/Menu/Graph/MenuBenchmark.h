// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "MenuBenchmark.generated.h"

/** A benchmark run's results: what reviewers and players compare (averages, lows, frame times per thread). */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuBenchmarkResult
{
	GENERATED_BODY()

	/** Seconds measured (warm-up excluded). */
	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	float Seconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	int32 Frames = 0;

	/** Frames divided by seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	float AverageFPS = 0.f;

	/** The average fps of the slowest 1% of frames. */
	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	float OnePercentLowFPS = 0.f;

	/** The average fps of the slowest 0.1% of frames. */
	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	float PointOnePercentLowFPS = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	float MinFPS = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	float MaxFPS = 0.f;

	/** Milliseconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	float AverageFrameTime = 0.f;

	/** The 99th percentile frame time (ms): 99% of frames were faster. */
	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	float FrameTimeP99 = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	float AverageGameThreadTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	float AverageRenderThreadTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	float AverageGPUTime = 0.f;

	/** Frames over 2.5x the average frame time (visible hitches). */
	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	int32 Hitches = 0;

	/** The CSV written (every frame), or empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Benchmark")
	FString CsvPath;

	/** "87 fps avg, 1% low 61 fps, 11.5 ms". */
	FText ToText() const;
};

/** Reads the engine's per-thread frame times (the numbers stat unit shows), in milliseconds. */
namespace MenuFrameTimes
{
	/** The last frame's game thread, render thread, RHI thread and GPU times (ms). 0 where the platform doesn't report it. */
	NAMETBD_API float GetGameThreadMs();
	NAMETBD_API float GetRenderThreadMs();
	NAMETBD_API float GetRHIThreadMs();
	NAMETBD_API float GetGPUMs();
}

/**
 * One benchmark run, in real time on the core ticker (works paused, in menus, in any world). Native; Blueprint uses Run
 * Benchmark (async) or the settings subsystem's Start Benchmark.
 */
class NAMETBD_API FMenuBenchmarkRun : public TSharedFromThis<FMenuBenchmarkRun>
{
public:
	DECLARE_DELEGATE_OneParam(FOnFinished, const FMenuBenchmarkResult&);

	/** Starts measuring after WarmupSeconds, for Seconds. bWriteCsv: Saved/<Benchmark Folder>/Benchmark_<date>.csv. */
	static TSharedRef<FMenuBenchmarkRun> Start(float Seconds, float WarmupSeconds, bool bWriteCsv, FOnFinished OnFinished);

	~FMenuBenchmarkRun();

	void Cancel();
	bool IsRunning() const { return TickerHandle.IsValid(); }

	/** 0..1 through the measured part. */
	float GetProgress() const;

	/** The results of a list of frame times (ms). */
	static FMenuBenchmarkResult Summarize(const TArray<float>& FrameMs, const TArray<float>& GameMs, const TArray<float>& RenderMs, const TArray<float>& GpuMs);

private:
	bool Tick(float DeltaTime);
	void Finish();

	float Duration = 15.f;
	float Warmup = 2.f;
	float Elapsed = 0.f;
	bool bCsv = true;
	double LastTime = 0.0;
	TArray<float> FrameTimes;
	TArray<float> GameTimes;
	TArray<float> RenderTimes;
	TArray<float> GpuTimes;
	FOnFinished Finished;
	FTSTicker::FDelegateHandle TickerHandle;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuBenchmarkResult, const FMenuBenchmarkResult&, Result);

/** Run Benchmark: measures performance for a while and reports the results (Blueprint async node). */
UCLASS(meta = (DisplayName = "Run Benchmark"))
class NAMETBD_API UAsyncAction_RunMenuBenchmark : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:

	/**
	 * Measures frame rate and frame times for Seconds (after Warmup Seconds), then reports average fps, 1% and 0.1% lows,
	 * frame time percentiles, per-thread averages and hitches. Write CSV keeps every frame in Saved/Benchmarks.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu|Benchmark", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UAsyncAction_RunMenuBenchmark* RunBenchmark(UObject* WorldContextObject, float Seconds = 15.f, float WarmupSeconds = 2.f, bool bWriteCsv = true);

	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FOnMenuBenchmarkResult OnFinished;

private:
	float RunSeconds = 15.f;
	float RunWarmup = 2.f;
	bool bRunCsv = true;
	TSharedPtr<FMenuBenchmarkRun> Run;
};
