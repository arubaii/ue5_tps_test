// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuBenchmark.h"

#include "HAL/PlatformTime.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "DynamicRHI.h"
#include "RHI.h"
#include "NameTBD/Menu/Settings/MenuProjectSettings.h"

#define LOCTEXT_NAMESPACE "MenuBenchmark"

namespace MenuFrameTimes
{
	float GetGameThreadMs() { return static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime)); }
	float GetRenderThreadMs() { return static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime)); }
	float GetRHIThreadMs() { return static_cast<float>(FPlatformTime::ToMilliseconds(GRHIThreadTime)); }
	float GetGPUMs() { return static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0))); }
}

FText FMenuBenchmarkResult::ToText() const
{
	FNumberFormattingOptions Whole;
	Whole.MaximumFractionalDigits = 0;
	FNumberFormattingOptions OneDecimal;
	OneDecimal.MinimumFractionalDigits = 1;
	OneDecimal.MaximumFractionalDigits = 1;
	return FText::Format(LOCTEXT("Summary", "{0} fps average, 1% low {1} fps, 0.1% low {2} fps. Frame {3} ms (99%: {4} ms), {5} hitches."),
		FText::AsNumber(AverageFPS, &Whole), FText::AsNumber(OnePercentLowFPS, &Whole), FText::AsNumber(PointOnePercentLowFPS, &Whole),
		FText::AsNumber(AverageFrameTime, &OneDecimal), FText::AsNumber(FrameTimeP99, &OneDecimal), FText::AsNumber(Hitches));
}

TSharedRef<FMenuBenchmarkRun> FMenuBenchmarkRun::Start(float Seconds, float WarmupSeconds, bool bWriteCsv, FOnFinished OnFinished)
{
	TSharedRef<FMenuBenchmarkRun> Run = MakeShared<FMenuBenchmarkRun>();
	Run->Duration = FMath::Max(1.f, Seconds);
	Run->Warmup = FMath::Max(0.f, WarmupSeconds);
	Run->bCsv = bWriteCsv;
	Run->Finished = MoveTemp(OnFinished);
	Run->LastTime = FPlatformTime::Seconds();
	const int32 Expected = FMath::CeilToInt(Run->Duration * 144.f);
	Run->FrameTimes.Reserve(Expected);
	Run->GameTimes.Reserve(Expected);
	Run->RenderTimes.Reserve(Expected);
	Run->GpuTimes.Reserve(Expected);
	Run->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(Run, &FMenuBenchmarkRun::Tick));
	return Run;
}

FMenuBenchmarkRun::~FMenuBenchmarkRun()
{
	Cancel();
}

void FMenuBenchmarkRun::Cancel()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

float FMenuBenchmarkRun::GetProgress() const
{
	return FMath::Clamp((Elapsed - Warmup) / Duration, 0.f, 1.f);
}

bool FMenuBenchmarkRun::Tick(float DeltaTime)
{
	// The real frame length (the ticker's delta can be clamped by the engine's max delta time).
	const double Now = FPlatformTime::Seconds();
	const float FrameSeconds = static_cast<float>(Now - LastTime);
	LastTime = Now;
	Elapsed += FrameSeconds;
	if (Elapsed > Warmup && FrameSeconds > 0.f)
	{
		FrameTimes.Add(FrameSeconds * 1000.f);
		GameTimes.Add(MenuFrameTimes::GetGameThreadMs());
		RenderTimes.Add(MenuFrameTimes::GetRenderThreadMs());
		GpuTimes.Add(MenuFrameTimes::GetGPUMs());
	}
	if (Elapsed >= Warmup + Duration)
	{
		TickerHandle.Reset();
		Finish();
		return false;
	}
	return true;
}

FMenuBenchmarkResult FMenuBenchmarkRun::Summarize(const TArray<float>& FrameMs, const TArray<float>& GameMs, const TArray<float>& RenderMs, const TArray<float>& GpuMs)
{
	FMenuBenchmarkResult Result;
	Result.Frames = FrameMs.Num();
	if (Result.Frames == 0)
	{
		return Result;
	}
	auto Mean = [](const TArray<float>& Values)
	{
		double Sum = 0.0;
		for (const float Value : Values)
		{
			Sum += Value;
		}
		return Values.Num() > 0 ? static_cast<float>(Sum / Values.Num()) : 0.f;
	};

	double TotalMs = 0.0;
	for (const float Ms : FrameMs)
	{
		TotalMs += Ms;
	}
	Result.Seconds = static_cast<float>(TotalMs / 1000.0);
	Result.AverageFrameTime = static_cast<float>(TotalMs / Result.Frames);
	Result.AverageFPS = TotalMs > 0.0 ? static_cast<float>(Result.Frames * 1000.0 / TotalMs) : 0.f;

	TArray<float> Sorted = FrameMs;
	Sorted.Sort([](float A, float B) { return A > B; }); // slowest first
	auto WorstFps = [&Sorted](float Share)
	{
		const int32 Count = FMath::Max(1, FMath::CeilToInt(Sorted.Num() * Share));
		double Sum = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Sum += Sorted[Index];
		}
		const double MeanMs = Sum / Count;
		return MeanMs > 0.0 ? static_cast<float>(1000.0 / MeanMs) : 0.f;
	};
	Result.OnePercentLowFPS = WorstFps(0.01f);
	Result.PointOnePercentLowFPS = WorstFps(0.001f);
	Result.MinFPS = Sorted[0] > 0.f ? 1000.f / Sorted[0] : 0.f;
	Result.MaxFPS = Sorted.Last() > 0.f ? 1000.f / Sorted.Last() : 0.f;
	Result.FrameTimeP99 = Sorted[FMath::Clamp(FMath::FloorToInt(Sorted.Num() * 0.01f), 0, Sorted.Num() - 1)];

	for (const float Ms : FrameMs)
	{
		Result.Hitches += Ms > Result.AverageFrameTime * 2.5f ? 1 : 0;
	}
	Result.AverageGameThreadTime = Mean(GameMs);
	Result.AverageRenderThreadTime = Mean(RenderMs);
	Result.AverageGPUTime = Mean(GpuMs);
	return Result;
}

void FMenuBenchmarkRun::Finish()
{
	FMenuBenchmarkResult Result = Summarize(FrameTimes, GameTimes, RenderTimes, GpuTimes);

	if (bCsv && FrameTimes.Num() > 0)
	{
		FString Csv = TEXT("frame,frame_ms,game_ms,render_ms,gpu_ms\n");
		for (int32 Index = 0; Index < FrameTimes.Num(); ++Index)
		{
			Csv += FString::Printf(TEXT("%d,%.3f,%.3f,%.3f,%.3f\n"), Index, FrameTimes[Index], GameTimes[Index], RenderTimes[Index], GpuTimes[Index]);
		}
		Csv += FString::Printf(TEXT("\n# frames,%d\n# seconds,%.2f\n# average_fps,%.2f\n# 1pct_low_fps,%.2f\n# 0.1pct_low_fps,%.2f\n# avg_frame_ms,%.3f\n# p99_frame_ms,%.3f\n# hitches,%d\n# gpu,%s\n"),
			Result.Frames, Result.Seconds, Result.AverageFPS, Result.OnePercentLowFPS, Result.PointOnePercentLowFPS, Result.AverageFrameTime, Result.FrameTimeP99, Result.Hitches,
			*GRHIAdapterName.Replace(TEXT(","), TEXT(" ")));
		const FString Path = FPaths::ProjectSavedDir() / UMenuProjectSettings::Get().BenchmarkFolder / FString::Printf(TEXT("Benchmark_%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		if (FFileHelper::SaveStringToFile(Csv, *Path))
		{
			Result.CsvPath = FPaths::ConvertRelativePathToFull(Path);
		}
	}
	UE_LOG(LogNameTBDSettings, Display, TEXT("Benchmark: %s%s"), *Result.ToText().ToString(), Result.CsvPath.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Result.CsvPath));

	// Last: whoever is told may let go of this run.
	const FOnFinished Callback = Finished;
	Finished.Unbind();
	Callback.ExecuteIfBound(Result);
}

// =====================================================================================================
//  Async node
// =====================================================================================================

UAsyncAction_RunMenuBenchmark* UAsyncAction_RunMenuBenchmark::RunBenchmark(UObject* WorldContextObject, float Seconds, float WarmupSeconds, bool bWriteCsv)
{
	UAsyncAction_RunMenuBenchmark* Action = NewObject<UAsyncAction_RunMenuBenchmark>();
	Action->RunSeconds = Seconds;
	Action->RunWarmup = WarmupSeconds;
	Action->bRunCsv = bWriteCsv;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UAsyncAction_RunMenuBenchmark::Activate()
{
	Run = FMenuBenchmarkRun::Start(RunSeconds, RunWarmup, bRunCsv, FMenuBenchmarkRun::FOnFinished::CreateWeakLambda(this, [this](const FMenuBenchmarkResult& Result)
	{
		OnFinished.Broadcast(Result);
		SetReadyToDestroy();
	}));
}

#undef LOCTEXT_NAMESPACE
