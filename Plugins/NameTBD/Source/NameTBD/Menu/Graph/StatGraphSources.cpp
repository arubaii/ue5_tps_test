// Copyright AR GameSystems 2026. All rights reserved.

#include "StatGraphSources.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "MenuBenchmark.h"

#define LOCTEXT_NAMESPACE "StatGraphSources"

bool UStatGraphSource::SampleValue(const FStatGraphSampleContext& Context, float& OutValue)
{
	return ReceiveSample(Context, OutValue);
}

bool UStatGraphSource_FrameRate::SampleValue(const FStatGraphSampleContext& Context, float& OutValue)
{
	if (Context.DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	OutValue = static_cast<float>(FMath::Max(1, Context.Frames)) / Context.DeltaTime;
	return true;
}

FText UStatGraphSource_FrameRate::GetDefaultUnits() const { return LOCTEXT("Fps", "fps"); }
FText UStatGraphSource_FrameRate::GetDefaultLabel() const { return LOCTEXT("FrameRate", "FPS"); }

bool UStatGraphSource_FrameTime::SampleValue(const FStatGraphSampleContext& Context, float& OutValue)
{
	if (Context.DeltaTime <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	OutValue = Context.DeltaTime * 1000.f / static_cast<float>(FMath::Max(1, Context.Frames));
	return true;
}

FText UStatGraphSource_FrameTime::GetDefaultUnits() const { return LOCTEXT("Ms", "ms"); }
FText UStatGraphSource_FrameTime::GetDefaultLabel() const { return LOCTEXT("Frame", "Frame"); }

bool UStatGraphSource_ThreadTime::SampleValue(const FStatGraphSampleContext& Context, float& OutValue)
{
	switch (Thread)
	{
	case EStatGraphThread::Game: OutValue = MenuFrameTimes::GetGameThreadMs(); break;
	case EStatGraphThread::Render: OutValue = MenuFrameTimes::GetRenderThreadMs(); break;
	case EStatGraphThread::RHI: OutValue = MenuFrameTimes::GetRHIThreadMs(); break;
	default: OutValue = MenuFrameTimes::GetGPUMs(); break;
	}
	return OutValue > 0.f;
}

FText UStatGraphSource_ThreadTime::GetDefaultUnits() const { return LOCTEXT("Ms", "ms"); }

FText UStatGraphSource_ThreadTime::GetDefaultLabel() const
{
	switch (Thread)
	{
	case EStatGraphThread::Game: return LOCTEXT("Game", "Game");
	case EStatGraphThread::Render: return LOCTEXT("Render", "Render");
	case EStatGraphThread::RHI: return LOCTEXT("RHI", "RHI");
	default: return LOCTEXT("GPU", "GPU");
	}
}

bool UStatGraphSource_Memory::SampleValue(const FStatGraphSampleContext& Context, float& OutValue)
{
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	const double Bytes = Figure == EStatGraphMemory::UsedVirtual ? static_cast<double>(Stats.UsedVirtual)
		: Figure == EStatGraphMemory::AvailablePhysical ? static_cast<double>(Stats.AvailablePhysical) : static_cast<double>(Stats.UsedPhysical);
	OutValue = static_cast<float>(Bytes / (1024.0 * 1024.0));
	return true;
}

FText UStatGraphSource_Memory::GetDefaultUnits() const { return LOCTEXT("MB", "MB"); }

FText UStatGraphSource_Memory::GetDefaultLabel() const
{
	return Figure == EStatGraphMemory::AvailablePhysical ? LOCTEXT("MemoryFree", "Free Memory") : LOCTEXT("Memory", "Memory");
}

bool UStatGraphSource_ConsoleVariable::SampleValue(const FStatGraphSampleContext& Context, float& OutValue)
{
	IConsoleVariable* Variable = Name.IsEmpty() ? nullptr : IConsoleManager::Get().FindConsoleVariable(*Name);
	if (!Variable)
	{
		return false;
	}
	OutValue = Variable->GetFloat();
	return true;
}

FText UStatGraphSource_ConsoleVariable::GetDefaultLabel() const
{
	return FText::FromString(Name);
}

bool UStatGraphSource_Ping::SampleValue(const FStatGraphSampleContext& Context, float& OutValue)
{
	const APlayerState* State = Context.Player ? Context.Player->PlayerState.Get() : nullptr;
	if (!State || Context.Player->GetNetMode() == NM_Standalone)
	{
		return false;
	}
	OutValue = State->GetPingInMilliseconds();
	return true;
}

FText UStatGraphSource_Ping::GetDefaultUnits() const { return LOCTEXT("Ms", "ms"); }
FText UStatGraphSource_Ping::GetDefaultLabel() const { return LOCTEXT("Ping", "Ping"); }

void UStatGraphSource_Manual::Push(float Value)
{
	Sum += Value;
	++Count;
}

bool UStatGraphSource_Manual::SampleValue(const FStatGraphSampleContext& Context, float& OutValue)
{
	if (Count == 0)
	{
		return false;
	}
	OutValue = Sum / static_cast<float>(Count);
	Sum = 0.f;
	Count = 0;
	return true;
}

#undef LOCTEXT_NAMESPACE
