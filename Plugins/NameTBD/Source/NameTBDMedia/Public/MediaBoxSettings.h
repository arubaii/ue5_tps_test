// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "MediaBoxTypes.h"

#include "MediaBoxSettings.generated.h"

class UMediaBoxTransportBar;
class USoundClass;

/**
 * Project Settings > Plugins > Media Box: what every Media Box in the project shares - how many players are kept warm, the
 * fades, how audio follows the video, and how the next hover is guessed. See MediaBoxTypes.h.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Media Box"))
class NAMETBDMEDIA_API UMediaBoxSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	static const UMediaBoxSettings& Get() { return *GetDefault<UMediaBoxSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif

	/** Max Warm Players, or the MediaBox.MaxWarmPlayers console value when that is 0 or more. */
	int32 GetMaxWarmPlayers() const;

	// -------------------------------------------------------------------------------------------------
	// Warm Pool
	// -------------------------------------------------------------------------------------------------

	/**
	 * How many players are kept open and paused on a clip's first frame, ready to play at once - besides those playing. Each
	 * holds a decoder and a frame of video in memory: 4 covers the clip hovered, the one fading out and the two most likely
	 * next. Consoles and phones may allow fewer hardware decoders at once; lower it there (per platform config). 0: no warm
	 * players, every clip starts cold (the poster still shows at once).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Warm Pool", meta = (DisplayName = "Max Warm Players", ClampMin = "0", UIMax = "16"))
	int32 MaxWarmPlayers = 4;

	/**
	 * Of each Warm Group, how many clips are kept warm: the focused (or hovered) one and those nearest it on screen - or nearest
	 * where the pointer is heading. The pool's Max Warm Players still caps them all.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Warm Pool", meta = (DisplayName = "Warm Per Group", ClampMin = "1", UIMax = "16"))
	int32 WarmPerGroup = 3;

	/**
	 * Guesses the next hover from where the pointer is heading (its speed, Hover Intent Look Ahead seconds ahead) and warms what
	 * is there before it arrives. Off: from where the pointer is.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Warm Pool", meta = (DisplayName = "Hover Intent"))
	bool bHoverIntent = true;

	UPROPERTY(Config, EditAnywhere, Category = "Warm Pool", meta = (DisplayName = "Hover Intent Look Ahead", EditCondition = "bHoverIntent", ClampMin = "0.0", UIMax = "0.5", Units = "s"))
	float HoverIntentLookAhead = 0.15f;

	/**
	 * The media player plugin every Media Box asks for by name - ElectraPlayer, WmfMedia, BinkMedia, ImgMedia... None: the
	 * engine picks per file and platform. A clip's own Player Name wins.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Warm Pool", meta = (DisplayName = "Player Name"))
	FName PlayerName;

	// -------------------------------------------------------------------------------------------------
	// Fades
	// -------------------------------------------------------------------------------------------------

	/** How long the video takes to fade in over the poster once its first frame is in - hides the cut. 0: a cut. */
	UPROPERTY(Config, EditAnywhere, Category = "Fades", meta = (DisplayName = "Video Fade In", ClampMin = "0.0", UIMax = "1.0", Units = "s"))
	float VideoFadeIn = 0.08f;

	/** How long the video takes to fade back to the poster as it stops (Stop And Rewind). 0: a cut. */
	UPROPERTY(Config, EditAnywhere, Category = "Fades", meta = (DisplayName = "Video Fade Out", ClampMin = "0.0", UIMax = "1.0", Units = "s"))
	float VideoFadeOut = 0.12f;

	/** How long the audio takes to fade out as the video stops or pauses, rather than being cut. */
	UPROPERTY(Config, EditAnywhere, Category = "Fades", meta = (DisplayName = "Audio Fade Out", ClampMin = "0.0", UIMax = "1.0", Units = "s"))
	float AudioFadeOut = 0.15f;

	// -------------------------------------------------------------------------------------------------
	// Audio
	// -------------------------------------------------------------------------------------------------

	/** The Sound Class every Media Box's audio plays in - the UI or voice class, for the game's volume sliders. None: the sound's own. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (DisplayName = "Sound Class"))
	TSoftObjectPtr<USoundClass> SoundClass;

	/**
	 * A Separate Sound drifting further than this from the video is put back in step (started again at the video's time). Short
	 * previews never drift that far.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (DisplayName = "Audio Resync Threshold", ClampMin = "0.01", UIMax = "0.5", Units = "s"))
	float AudioResyncThreshold = 0.08f;

	/** Plays the audio in the Widget Blueprint designer too, where a Media Box has Preview In Designer on. Off: designer previews are silent. */
	UPROPERTY(Config, EditAnywhere, Category = "Audio", meta = (DisplayName = "Audio In Designer"))
	bool bAudioInDesigner = false;

	// -------------------------------------------------------------------------------------------------
	// Controls
	// -------------------------------------------------------------------------------------------------

	/**
	 * The controls bar every Media Box With Controls shows when its own Controls Class is None - a Widget Blueprint whose parent
	 * class is Media Box Transport Bar. None here too: no controls bar.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Controls", meta = (DisplayName = "Default Controls Class"))
	TSoftClassPtr<UMediaBoxTransportBar> DefaultControlsClass;

	/**
	 * When the controls bar of a Media Box With Controls shows and hides, unless the widget overrides it (Override Controls
	 * Behavior). Preset YouTube: like the YouTube desktop player; Custom: the values set here.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Controls", meta = (DisplayName = "Default Controls Behavior"))
	FMediaBoxControlsBehavior DefaultControlsBehavior;

	// -------------------------------------------------------------------------------------------------
	// Subtitles
	// -------------------------------------------------------------------------------------------------

	/** Whether Media Boxes draw their clips' subtitles - the game's subtitles option. Set at runtime with Set Media Box Subtitles Enabled. */
	UPROPERTY(Config, EditAnywhere, Category = "Subtitles", meta = (DisplayName = "Subtitles Enabled"))
	bool bSubtitlesEnabled = true;
};
