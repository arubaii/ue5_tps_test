// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/WidgetAnimation/WidgetAnimCue.h"
#include "NameTBD/WidgetAnimation/WidgetAnimInput.h"

#include "MediaBoxCue.generated.h"

class UMediaBox;
class UMediaBoxClip;
class UWidget;

namespace MediaBoxFind
{
	/**
	 * The Media Box something plays on: the one named BoxName in the User Widgets around From (or Host); unnamed, From itself
	 * or the first inside it, else the first in each User Widget around it, nearest first. Null when none.
	 */
	NAMETBDMEDIA_API UMediaBox* Find(UWidget* From, UWidget* Host, FName BoxName);
}

/** What a Media cue does when it fires. */
UENUM(BlueprintType)
enum class EWidgetAnimMediaCueAction : uint8
{
	Play UMETA(ToolTip = "Plays the Media Box (at once when its clip is warm)."),

	Pause UMETA(ToolTip = "Pauses it where it is."),

	Stop UMETA(ToolTip = "Stops it: back to the poster and the start."),

	TogglePlayPause UMETA(DisplayName = "Toggle Play / Pause"),

	Seek UMETA(ToolTip = "Jumps to Seek Time."),

	SetClip UMETA(DisplayName = "Set Clip", ToolTip = "Gives it the cue's Clip (and plays it, with Play Clip).")
};

/**
 * Media: plays, pauses, stops, seeks or swaps the clip of a Media Box at a moment of the animation - a trailer starting as a
 * panel finishes opening, stopping as it closes.
 *
 * Acts on the Media Box named in Media Box; else the track's widget itself when it is one (aim the track's Track Group at it),
 * or the first inside it; else the nearest around it. Seen in the editor's previews where that box has Preview In Designer on.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Media"))
struct NAMETBDMEDIA_API FWidgetAnimMediaCueParams : public FWidgetAnimCueParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media")
	EWidgetAnimMediaCueAction Action = EWidgetAnimMediaCueAction::Play;

	/** The clip Set Clip gives the Media Box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media", meta = (DisplayName = "Clip", EditCondition = "Action == EWidgetAnimMediaCueAction::SetClip", EditConditionHides))
	TObjectPtr<UMediaBoxClip> CueClip = nullptr;

	/** Set Clip plays the new clip too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media", meta = (DisplayName = "Play Clip", EditCondition = "Action == EWidgetAnimMediaCueAction::SetClip", EditConditionHides))
	bool bPlayClip = true;

	/** Where Seek jumps to, in seconds into the video. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media", meta = (DisplayName = "Seek Time", ClampMin = "0.0", Units = "s", EditCondition = "Action == EWidgetAnimMediaCueAction::Seek", EditConditionHides))
	float SeekTime = 0.f;

	/**
	 * The Media Box to act on, by its name in the Widget Blueprint the track's widget is in or any around it. None: the track's
	 * widget when it is one, else the first inside it, else the nearest around it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Media", meta = (DisplayName = "Media Box"))
	FName MediaBoxName;
};

class NAMETBDMEDIA_API FWidgetAnimMediaCueEvaluator : public TWidgetAnimCueEvaluator<FWidgetAnimMediaCueParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetTrackSummary(const FInstancedStruct& Params) const override;
	virtual bool FiresAtDesignTime(const FInstancedStruct& Params) const override { return true; }
	virtual void Fire(const FWidgetAnimCueContext& Context) const override;
};

/** What a Media Progress input gives. */
UENUM(BlueprintType)
enum class EMediaBoxInputMeasure : uint8
{
	Progress UMETA(ToolTip = "How far through the video, 0 - 1."),

	Seconds UMETA(ToolTip = "Seconds into the video."),

	Playing UMETA(ToolTip = "1 while the video plays, 0 while it doesn't.")
};

/**
 * Where a Media Box's video is: a progress bar under a trailer, motion timed to a clip, a glow while it plays. Reads the Media
 * Box named in Media Box, else the track's widget when it is one, the first inside it, else the nearest around it.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Media Progress"))
struct NAMETBDMEDIA_API FWidgetAnimInput_MediaProgress : public FWidgetAnimInputSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	EMediaBoxInputMeasure Measure = EMediaBoxInputMeasure::Progress;

	/** The Media Box to read, by name. None: the track's widget, inside it, or around it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input", meta = (DisplayName = "Media Box"))
	FName MediaBoxName;

	virtual bool Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue) override;
	virtual FString GetSummary() const override;

private:
	TWeakObjectPtr<UMediaBox> Found;
	TWeakObjectPtr<UWidget> FoundFor;
};
