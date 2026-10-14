// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h" // ECollisionChannel

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

/** Where a Media Box's audio is heard from. */
UENUM(BlueprintType)
enum class EMediaBoxAudioSpace : uint8
{
	UI UMETA(DisplayName = "2D (UI)", ToolTip = "Heard the same everywhere, as UI sound - wherever the Media Box is."),

	World UMETA(DisplayName = "World (On Diegetic Screen)", ToolTip = "On a Diegetic Screen: heard from the screen in the world - quieter with distance, from its direction, muffled through walls (World Audio). Not on a screen: 2D, as UI.")
};

/**
 * How a Media Box on a Diegetic Screen is heard in the world (Audio Space World). Every player hears it from their own camera,
 * so it works the same in single player, split screen and multiplayer.
 */
USTRUCT(BlueprintType)
struct NAMETBDMEDIA_API FMediaBoxWorldAudio
{
	GENERATED_BODY()

	/** A Sound Attenuation asset to use instead of the values below - for a project's own falloff curves, reverb, focus... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Audio")
	TObjectPtr<class USoundAttenuation> Attenuation = nullptr;

	/** Full volume within this distance of the screen (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Audio", meta = (DisplayName = "Inner Radius", ClampMin = "0.0", UIMax = "2000.0", Units = "cm", EditCondition = "Attenuation == nullptr"))
	float InnerRadius = 300.f;

	/** Fades to silence over this distance beyond Inner Radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Audio", meta = (DisplayName = "Falloff Distance", ClampMin = "1.0", UIMax = "10000.0", Units = "cm", EditCondition = "Attenuation == nullptr"))
	float FalloffDistance = 2500.f;

	/** Muffled and quieter when something blocks the line from the listener to the screen - a wall, a door. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Audio", meta = (DisplayName = "Occlusion", EditCondition = "Attenuation == nullptr"))
	bool bOcclusion = true;

	/** What blocks it: the collision channel traced from the listener to the screen. The screen's own actor never blocks it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Audio", meta = (DisplayName = "Occlusion Channel", EditCondition = "Attenuation == nullptr && bOcclusion"))
	TEnumAsByte<ECollisionChannel> OcclusionChannel = ECC_Visibility;

	/** How muffled it is behind a wall: a low-pass filter at this frequency (Hz). Lower is more muffled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Audio", meta = (DisplayName = "Occluded Low Pass", ClampMin = "100.0", ClampMax = "20000.0", Units = "Hz", EditCondition = "Attenuation == nullptr && bOcclusion"))
	float OccludedLowPass = 1500.f;

	/** How loud it is behind a wall, 0 - 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Audio", meta = (DisplayName = "Occluded Volume", ClampMin = "0.0", ClampMax = "1.0", EditCondition = "Attenuation == nullptr && bOcclusion"))
	float OccludedVolume = 0.4f;

	/** Sent to the level's reverb (Audio Volumes), so it sounds like it is in the room. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World Audio", meta = (DisplayName = "Reverb", EditCondition = "Attenuation == nullptr"))
	bool bReverb = true;
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

/**
 * Which values a Media Box With Controls' controls bar follows for showing and hiding. YouTube: the YouTube desktop player's
 * (the values below are those, greyed out). Custom: the values below as set.
 */
UENUM(BlueprintType)
enum class EMediaBoxControlsPreset : uint8
{
	YouTube UMETA(DisplayName = "YouTube", ToolTip = "Like the YouTube desktop player: shown while the pointer moves over the video, hidden 3 s after it stops moving (or as soon as it leaves) while playing, always shown while paused or stopped, faded in and out quickly, the pointer hidden with the controls, a click on the video plays / pauses."),

	Custom UMETA(ToolTip = "The values below, as set here.")
};

/**
 * When a Media Box With Controls shows and hides its controls bar. Starts at the YouTube values (see YouTube()); Preset YouTube
 * uses those whatever the fields say.
 */
USTRUCT(BlueprintType)
struct NAMETBDMEDIA_API FMediaBoxControlsBehavior
{
	GENERATED_BODY()

	/** YouTube: the YouTube desktop player's values (greyed out below). Custom: the values below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls")
	EMediaBoxControlsPreset Preset = EMediaBoxControlsPreset::YouTube;

	/** Hides the controls while the video plays and nobody is using them. Off: they are always shown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Auto Hide", EditCondition = "Preset == EMediaBoxControlsPreset::Custom"))
	bool bAutoHide = true;

	/** While playing with the pointer over the video: hidden this many seconds after the pointer last moved. 0: not hidden while the pointer is over it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Hide After Idle", ClampMin = "0.0", UIMax = "10.0", Units = "s", EditCondition = "Preset == EMediaBoxControlsPreset::Custom && bAutoHide"))
	float HideAfterIdle = 3.f;

	/** While playing: hidden this many seconds after the pointer leaves the video. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Hide After Unhovered", ClampMin = "0.0", UIMax = "5.0", Units = "s", EditCondition = "Preset == EMediaBoxControlsPreset::Custom && bAutoHide"))
	float HideAfterUnhovered = 0.1f;

	/** Keeps the controls shown while the video is paused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Show While Paused", EditCondition = "Preset == EMediaBoxControlsPreset::Custom && bAutoHide"))
	bool bShowWhilePaused = true;

	/** Keeps the controls shown while stopped, ended or loading - before the video plays. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Show While Stopped", EditCondition = "Preset == EMediaBoxControlsPreset::Custom && bAutoHide"))
	bool bShowWhileStopped = true;

	/** Keeps them shown while the pointer is over the controls themselves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Show While Over Controls", EditCondition = "Preset == EMediaBoxControlsPreset::Custom && bAutoHide"))
	bool bShowWhileOverControls = true;

	/** Keeps them shown while one of them has keyboard / gamepad focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Show While Focused", EditCondition = "Preset == EMediaBoxControlsPreset::Custom && bAutoHide"))
	bool bShowWhileFocused = true;

	/** Shows them for Hide After Idle when the video starts, pauses or plays on - so the change is seen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Show On Play / Pause", EditCondition = "Preset == EMediaBoxControlsPreset::Custom && bAutoHide"))
	bool bShowOnPlaybackChange = true;

	/** Seconds the controls take to fade in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Fade In", ClampMin = "0.0", UIMax = "1.0", Units = "s", EditCondition = "Preset == EMediaBoxControlsPreset::Custom"))
	float FadeInTime = 0.1f;

	/** Seconds the controls take to fade out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Fade Out", ClampMin = "0.0", UIMax = "1.0", Units = "s", EditCondition = "Preset == EMediaBoxControlsPreset::Custom"))
	float FadeOutTime = 0.25f;

	/** Hides the mouse pointer over the video while the controls are hidden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Hide Pointer With Controls", EditCondition = "Preset == EMediaBoxControlsPreset::Custom"))
	bool bHidePointer = true;

	/** A click on the video (not on the controls) plays or pauses it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Click To Play / Pause", EditCondition = "Preset == EMediaBoxControlsPreset::Custom"))
	bool bClickToPlayPause = true;

	/** Raises the subtitles above the controls while they are shown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Controls", meta = (DisplayName = "Lift Subtitles", EditCondition = "Preset == EMediaBoxControlsPreset::Custom"))
	bool bLiftSubtitles = true;

	/** The YouTube desktop player's values. */
	static FMediaBoxControlsBehavior YouTube() { return FMediaBoxControlsBehavior(); }

	/** The values to follow: YouTube()'s for Preset YouTube, else these. */
	FMediaBoxControlsBehavior Resolve() const
	{
		if (Preset == EMediaBoxControlsPreset::YouTube)
		{
			return YouTube();
		}
		return *this;
	}
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

/**
 * A clip's subtitles in one more language: its cues, and the file / text they were read from. The clip's own Subtitles are its
 * first language (Subtitle Language, English by default).
 */
USTRUCT(BlueprintType)
struct NAMETBDMEDIA_API FMediaBoxSubtitleTrack
{
	GENERATED_BODY()

	/** The language, as a culture code: en, fr, de, es-MX, pt-BR, ja... Matched to the language asked for (fr-CA finds fr). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles")
	FString Language;

	/** Its name in menus: Français, Deutsch... Empty: the culture's own name for Language. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles", meta = (DisplayName = "Display Name"))
	FText DisplayName;

#if WITH_EDITORONLY_DATA
	/** A SubRip (.srt) or WebVTT (.vtt) file in this language, read into Subtitle Text and Cues as it is picked (Reload Subtitle File reads it again). */
	UPROPERTY(EditAnywhere, Category = "Subtitles", meta = (DisplayName = "Subtitle File", FilePathFilter = "Subtitles (*.srt;*.vtt)|*.srt;*.vtt"))
	FFilePath SubtitleFile;
#endif

	/** The subtitle file's text (SRT / VTT) - filled by Subtitle File, or pasted; read into Cues when changed. */
	UPROPERTY(EditAnywhere, Category = "Subtitles", meta = (DisplayName = "Subtitle Text (SRT / VTT)", MultiLine = true))
	FString SubtitleText;

	/** The lines, filled from Subtitle Text, or edited here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles")
	TArray<FMediaBoxSubtitleCue> Cues;
};

/** How a Media Box goes full screen and back (Project Settings > Media Box > Fullscreen Transition). */
UENUM(BlueprintType)
enum class EMediaBoxFullscreenTransition : uint8
{
	None UMETA(ToolTip = "At once."),

	Fade UMETA(ToolTip = "The full-screen video fades in over the screen, and out again."),

	Zoom UMETA(ToolTip = "The video grows from where it is on screen to fill it, and shrinks back.")
};

/**
 * How a Media Box Transport Bar writes a time. The defaults read 0:05, 1:02:03.
 */
USTRUCT(BlueprintType)
struct NAMETBDMEDIA_API FMediaBoxTimeDisplay
{
	GENERATED_BODY()

	/** An hour or more shows as hours: 1:02:03. Off: minutes go on counting - 62:03. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (DisplayName = "Wrap Minutes Into Hours"))
	bool bWrapMinutesIntoHours = true;

	/** Hours always show, even at 0: 0:00:05. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (DisplayName = "Always Show Hours", EditCondition = "bWrapMinutesIntoHours"))
	bool bAlwaysShowHours = false;

	/** The leading part has two digits: 00:05 rather than 0:05. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (DisplayName = "Pad Leading Digits"))
	bool bPadLeading = false;

	/** Shows parts of a second: 0:05.25. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (DisplayName = "Show Milliseconds"))
	bool bShowMilliseconds = false;

	/** How many digits of the parts of a second: 1 (tenths) to 3 (milliseconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (DisplayName = "Millisecond Digits", ClampMin = "1", ClampMax = "3", EditCondition = "bShowMilliseconds"))
	int32 MillisecondDigits = 2;

	/** Between hours, minutes and seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (DisplayName = "Delimiter"))
	FString Delimiter = TEXT(":");

	/** Between the seconds and their parts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time", meta = (DisplayName = "Millisecond Delimiter", EditCondition = "bShowMilliseconds"))
	FString MillisecondDelimiter = TEXT(".");

	/** Seconds written this way. */
	FString Format(float Seconds) const;
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
