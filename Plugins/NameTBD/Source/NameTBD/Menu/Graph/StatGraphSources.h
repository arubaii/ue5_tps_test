// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "StatGraphSources.generated.h"

class APlayerController;
class UWorld;

/** What a source sees when it is asked for a value. */
USTRUCT(BlueprintType)
struct NAMETBD_API FStatGraphSampleContext
{
	GENERATED_BODY()

	/** Real seconds since the last sample (this frame's length when sampling every frame). */
	UPROPERTY(BlueprintReadOnly, Category = "Graph")
	float DeltaTime = 0.f;

	/** Frames since the last sample. */
	UPROPERTY(BlueprintReadOnly, Category = "Graph")
	int32 Frames = 1;

	/** The graph's world and player (null in the designer). */
	UPROPERTY(BlueprintReadOnly, Category = "Graph")
	TObjectPtr<UWorld> World = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Graph")
	TObjectPtr<APlayerController> Player = nullptr;
};

/**
 * Where a graph line's values come from. Built in: Frame Rate, Frame Time, Thread Time (game, render, RHI, GPU), Memory,
 * Console Variable, Ping, Manual (values you push). Your own: a Blueprint of this class implementing Sample, or a C++ subclass
 * overriding SampleValue.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, Blueprintable, BlueprintType)
class NAMETBD_API UStatGraphSource : public UObject
{
	GENERATED_BODY()

public:

	/** Shown after values ("fps", "ms"). Empty: the source's own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Source")
	FText Units;

	/** The value now. False: no value this time (the line keeps its last). */
	virtual bool SampleValue(const FStatGraphSampleContext& Context, float& OutValue);

	virtual FText GetDefaultUnits() const { return FText::GetEmpty(); }
	virtual FText GetDefaultLabel() const { return FText::GetEmpty(); }

	/** Higher values are better (frame rate) - for threshold colours and which end the "1% low" is taken from. */
	virtual bool IsHigherBetter() const { return false; }

	FText GetUnitsText() const { return Units.IsEmpty() ? GetDefaultUnits() : Units; }

	/** A Blueprint source: return the value (and true), or false for none this time. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Graph", meta = (DisplayName = "Sample"))
	bool ReceiveSample(const FStatGraphSampleContext& Context, float& Value);
};

/** Frames per second. */
UCLASS(meta = (DisplayName = "Frame Rate"))
class NAMETBD_API UStatGraphSource_FrameRate : public UStatGraphSource
{
	GENERATED_BODY()

public:
	virtual bool SampleValue(const FStatGraphSampleContext& Context, float& OutValue) override;
	virtual FText GetDefaultUnits() const override;
	virtual FText GetDefaultLabel() const override;
	virtual bool IsHigherBetter() const override { return true; }
};

/** The whole frame's time in milliseconds. */
UCLASS(meta = (DisplayName = "Frame Time"))
class NAMETBD_API UStatGraphSource_FrameTime : public UStatGraphSource
{
	GENERATED_BODY()

public:
	virtual bool SampleValue(const FStatGraphSampleContext& Context, float& OutValue) override;
	virtual FText GetDefaultUnits() const override;
	virtual FText GetDefaultLabel() const override;
};

/** Which part of the frame. */
UENUM(BlueprintType)
enum class EStatGraphThread : uint8
{
	Game UMETA(ToolTip = "The game thread: gameplay, Blueprints, animation, UI."),
	Render UMETA(ToolTip = "The render thread: preparing what the GPU draws."),
	RHI UMETA(DisplayName = "RHI", ToolTip = "The RHI thread: talking to the graphics driver."),
	GPU UMETA(DisplayName = "GPU", ToolTip = "The graphics card's time for the frame.")
};

/** Game, render, RHI or GPU time in milliseconds (what stat unit shows). */
UCLASS(meta = (DisplayName = "Thread Time"))
class NAMETBD_API UStatGraphSource_ThreadTime : public UStatGraphSource
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Source")
	EStatGraphThread Thread = EStatGraphThread::Game;

	virtual bool SampleValue(const FStatGraphSampleContext& Context, float& OutValue) override;
	virtual FText GetDefaultUnits() const override;
	virtual FText GetDefaultLabel() const override;
};

/** Which memory figure. */
UENUM(BlueprintType)
enum class EStatGraphMemory : uint8
{
	UsedPhysical UMETA(ToolTip = "Physical memory the game uses (MB)."),
	UsedVirtual UMETA(ToolTip = "Virtual memory the game uses (MB)."),
	AvailablePhysical UMETA(ToolTip = "Physical memory left on the machine (MB).")
};

/** Memory in megabytes. */
UCLASS(meta = (DisplayName = "Memory"))
class NAMETBD_API UStatGraphSource_Memory : public UStatGraphSource
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Source")
	EStatGraphMemory Figure = EStatGraphMemory::UsedPhysical;

	virtual bool SampleValue(const FStatGraphSampleContext& Context, float& OutValue) override;
	virtual FText GetDefaultUnits() const override;
	virtual FText GetDefaultLabel() const override;
};

/** Any console variable's value (r.ScreenPercentage under dynamic resolution, a game's own). */
UCLASS(meta = (DisplayName = "Console Variable"))
class NAMETBD_API UStatGraphSource_ConsoleVariable : public UStatGraphSource
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Source")
	FString Name;

	virtual bool SampleValue(const FStatGraphSampleContext& Context, float& OutValue) override;
	virtual FText GetDefaultLabel() const override;
};

/** The player's ping to the server in milliseconds (nothing in standalone). */
UCLASS(meta = (DisplayName = "Ping"))
class NAMETBD_API UStatGraphSource_Ping : public UStatGraphSource
{
	GENERATED_BODY()

public:
	virtual bool SampleValue(const FStatGraphSampleContext& Context, float& OutValue) override;
	virtual FText GetDefaultUnits() const override;
	virtual FText GetDefaultLabel() const override;
};

/** Values you give it (Stat Graph's Push Value): anything the game measures. */
UCLASS(meta = (DisplayName = "Manual"))
class NAMETBD_API UStatGraphSource_Manual : public UStatGraphSource
{
	GENERATED_BODY()

public:
	/** Takes Value at the next sample. */
	void Push(float Value);

	virtual bool SampleValue(const FStatGraphSampleContext& Context, float& OutValue) override;

private:
	float Sum = 0.f;
	int32 Count = 0;
};
