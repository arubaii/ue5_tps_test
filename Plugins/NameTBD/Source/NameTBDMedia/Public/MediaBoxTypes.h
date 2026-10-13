// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "MediaBoxTypes.generated.h"

// =====================================================================================================
// Media Box
// =====================================================================================================
//
// Video in UI that starts the moment it is asked to: a skill hovered in a skill tree plays its preview clip, with audio, on
// that same frame - the way Horizon Zero Dawn's skill tree does.
//
// Nothing slow happens at the hover. The slow steps - opening the file, starting the decoder, decoding the first frame,
// loading the sound - happen beforehand, in the warm pool (UMediaBoxPool): a few players kept open and paused on each
// clip's first frame, chosen by what the player is likely to hover next (Warm Groups). The hover only calls Play on one of
// those, behind the clip's poster (a still of its first frame), which shows at once whatever happens. See
// Docs/Design/MediaBox.md.

/** How the video (and its poster) is laid in the Media Box. */
UENUM(BlueprintType)
enum class EMediaBoxFit : uint8
{
	Contain UMETA(ToolTip = "All of the video shows, as big as fits, its shape kept. The Media Box's Letterbox Color fills the rest."),

	Cover UMETA(ToolTip = "The video fills the whole Media Box, its shape kept; what overflows is cropped (Crop Focus says which part stays)."),

	Stretch UMETA(ToolTip = "The video is stretched to the Media Box's shape.")
};

/**
 * When a Media Box plays on its own. Whatever is picked, Play, Pause and Stop work too. A trigger that ends (unhovered, hidden)
 * does what After Trigger says.
 */
UENUM(BlueprintType)
enum class EMediaBoxTrigger : uint8
{
	Manual UMETA(ToolTip = "Only when told to: Play, Set Clip with Play, a Media cue."),

	Visible UMETA(ToolTip = "While the Media Box is shown - a preview in an Info Card plays as the card opens."),

	Hovered UMETA(ToolTip = "While its Hover Target (by default the button it is in, else itself) is hovered."),

	Focused UMETA(ToolTip = "While its Hover Target has keyboard / gamepad focus, or something inside it has."),

	HoveredOrFocused UMETA(DisplayName = "Hovered Or Focused", ToolTip = "While its Hover Target is hovered or focused - mouse and gamepad alike. What a skill tree wants.")
};

/** What a Media Box does when what started it ends: unhovered, unfocused, hidden. */
UENUM(BlueprintType)
enum class EMediaBoxAfterTrigger : uint8
{
	StopAndRewind UMETA(DisplayName = "Stop And Rewind", ToolTip = "Fades back to the poster and goes back to the start: the next hover plays it from the beginning, at once (its player stays warm)."),

	Pause UMETA(ToolTip = "Pauses where it is; the next time it plays, it carries on from there."),

	KeepPlaying UMETA(DisplayName = "Keep Playing", ToolTip = "Plays on until it ends (or Stop).")
};

/** Where a clip's audio comes from. */
UENUM(BlueprintType)
enum class EMediaBoxAudio : uint8
{
	Auto UMETA(ToolTip = "Separate Sound when the clip has a Sound, else the video file's own audio."),

	Embedded UMETA(ToolTip = "The video file's own audio track, played through a Media Sound Component (2D, UI sound)."),

	SeparateSound UMETA(DisplayName = "Separate Sound", ToolTip = "The clip's Sound, played in step with the video. Starts with no wait at all (it is loaded and primed with the clip), so it is the one recommended for previews."),

	Silent UMETA(ToolTip = "No audio.")
};

/** Where a Media Box is, for Blueprint. */
UENUM(BlueprintType)
enum class EMediaBoxStatus : uint8
{
	Empty UMETA(ToolTip = "No clip."),

	Loading UMETA(ToolTip = "Asked to play, waiting for its player (opening, or its first frame)."),

	Playing,

	Paused,

	Stopped UMETA(ToolTip = "At rest: the poster shows."),

	Ended UMETA(ToolTip = "Played to its end (and doesn't loop)."),

	Error UMETA(ToolTip = "Its video couldn't be opened or played - see On Error.")
};

/** One subtitle line of a clip: shown from Start to End (seconds into the video). */
USTRUCT(BlueprintType)
struct NAMETBDMEDIA_API FMediaBoxSubtitleCue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subtitles", meta = (Units = "s"))
	float Start = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subtitles", meta = (Units = "s"))
	float End = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Subtitles", meta = (MultiLine = true))
	FText Text;
};

namespace MediaBoxSubtitles
{
	/**
	 * Reads SubRip (.srt) or WebVTT (.vtt) text into cues, sorted by start: "00:00:01,500 --> 00:00:03,000" timings (a dot for
	 * the comma too, hours optional), the lines under each up to a blank line. Cue numbers, a WEBVTT header, NOTE / STYLE blocks
	 * and cue settings after the end time are skipped; <i>-style tags are taken out. Malformed blocks are skipped.
	 */
	NAMETBDMEDIA_API void Parse(const FString& Source, TArray<FMediaBoxSubtitleCue>& OutCues);

	/** The cue shown at Seconds, or INDEX_NONE. Cues are sorted by start; overlapping ones: the last that started. */
	NAMETBDMEDIA_API int32 FindCue(TConstArrayView<FMediaBoxSubtitleCue> Cues, float Seconds);
}
