// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h" // FFilePath

#include "MediaBoxTypes.h"

#include "MediaBoxClip.generated.h"

class UMediaSource;
class USoundBase;
class UTexture;

/**
 * Media Box Clip: one video as a Media Box plays it - the video, the still shown before its first frame (Poster), its audio,
 * where it starts and loops, its subtitles. One per skill preview, tutorial video or menu backdrop; the Media Box, Warm
 * Groups and Media cues all take clips.
 *
 * For a clip that starts at once: give it a Poster (Generate Poster makes one from the video), give previews a Separate
 * Sound, and encode the video for a fast start (see Docs/Design/MediaBox.md - a keyframe on the first frame, the MP4 index
 * at the front; Data Validation warns about the index).
 */
UCLASS(BlueprintType, meta = (DisplayName = "Media Box Clip"))
class NAMETBDMEDIA_API UMediaBoxClip : public UDataAsset
{
	GENERATED_BODY()

public:

	// ---------------------------------------------------------------------------------------------
	// Video
	// ---------------------------------------------------------------------------------------------

	/** The video: a File Media Source (an .mp4, .bk2...), Img Media Source (an image sequence), Stream Media Source... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Video", meta = (DisplayName = "Media Source"))
	TObjectPtr<UMediaSource> MediaSource = nullptr;

	/**
	 * Shown the instant the Media Box is asked to play, until the video's first frame is in, and whenever it is stopped - so the
	 * box is never black and never waits. Make it the video's first frame (Generate Poster) and the start is seamless.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Video")
	TObjectPtr<UTexture> Poster = nullptr;

	/** Where in the video it plays from (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Video", meta = (DisplayName = "Start Time", ClampMin = "0.0", Units = "s"))
	float StartTime = 0.f;

	/** Plays again from Loop Start each time it reaches Loop End. Off: it stops at its end (On Ended). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Video", meta = (DisplayName = "Loop"))
	bool bLoop = true;

	/**
	 * Where each loop starts again (seconds) - after an intro that plays once. Looping the whole video (Loop Start 0, Loop End
	 * 0) uses the player's own seamless loop; a range loops by seeking back, which a short-GOP encode makes hitch-free.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Video", meta = (DisplayName = "Loop Start", EditCondition = "bLoop", ClampMin = "0.0", Units = "s"))
	float LoopStart = 0.f;

	/** Where each loop ends (seconds). 0: the video's end. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Video", meta = (DisplayName = "Loop End", EditCondition = "bLoop", ClampMin = "0.0", Units = "s"))
	float LoopEnd = 0.f;

	/** The media player plugin to open it with (ElectraPlayer, WmfMedia, BinkMedia, ImgMedia...). None: Project Settings > Media Box, else the engine's pick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Video", AdvancedDisplay, meta = (DisplayName = "Player Name"))
	FName PlayerName;

	// ---------------------------------------------------------------------------------------------
	// Audio
	// ---------------------------------------------------------------------------------------------

	/** Where the audio comes from: the video file's own track, the Sound below, or none. Auto: the Sound when set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	EMediaBoxAudio Audio = EMediaBoxAudio::Auto;

	/**
	 * The clip's audio as a sound asset, played in step with the video (Separate Sound) - loaded and primed with the clip, so it
	 * starts on the very frame the video does. Export the video's audio track to a .wav and import it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<USoundBase> Sound = nullptr;

	/** The clip's own volume, under the Media Box's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float Volume = 1.f;

	// ---------------------------------------------------------------------------------------------
	// Subtitles
	// ---------------------------------------------------------------------------------------------

#if WITH_EDITORONLY_DATA
	/**
	 * A SubRip (.srt) or WebVTT (.vtt) file to take the subtitles from. Picking it reads it at once into Subtitle Text and
	 * Subtitles, so the lines are kept inside this clip and ship with it - the file itself isn't needed in the game. Edited the
	 * file since? Reload Subtitle File.
	 */
	UPROPERTY(EditAnywhere, Category = "Subtitles", meta = (DisplayName = "Subtitle File", FilePathFilter = "Subtitles (*.srt;*.vtt)|*.srt;*.vtt"))
	FFilePath SubtitleFile;
#endif

	/**
	 * The subtitle file's text (SRT / VTT) - filled by Subtitle File, or pasted here; it is read into Subtitles below as soon as
	 * it is changed. Kept so it can be edited again.
	 */
	UPROPERTY(EditAnywhere, Category = "Subtitles", meta = (DisplayName = "Subtitle Text (SRT / VTT)", MultiLine = true))
	FString SubtitleText;

	/** The clip's subtitle lines. Filled from Subtitle Text, or edited here. A Media Box draws them (Show Subtitles) and reports them (On Subtitle Changed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles")
	TArray<FMediaBoxSubtitleCue> Subtitles;

	/** The language of Subtitles above, as a culture code (en, fr, de, es-MX...): English unless set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles", meta = (DisplayName = "Subtitle Language"))
	FString SubtitleLanguage = TEXT("en");

	/** Its name in menus. Empty: the culture's own name for Subtitle Language. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles", meta = (DisplayName = "Subtitle Language Name"))
	FText SubtitleLanguageName;

	/**
	 * The subtitles in other languages, each from its own .srt / .vtt. A Media Box shows the language asked for (its own Subtitle
	 * Language, else Set Media Box Subtitle Language, else Project Settings > Media Box > Subtitle Language) - fr-CA finds fr - and
	 * Subtitles above when the clip has no such language.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles", meta = (DisplayName = "Other Languages", TitleProperty = "Language"))
	TArray<FMediaBoxSubtitleTrack> OtherSubtitleTracks;

	/** The video's length in seconds, once any player has opened it this session (0 before) - for showing the length before it plays. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Video")
	float KnownDuration = 0.f;

	// ---------------------------------------------------------------------------------------------
	// Editor
	// ---------------------------------------------------------------------------------------------

	/** Where in the video Generate Poster takes its still (seconds). Below 0: Start Time. */
	UPROPERTY(EditAnywhere, Category = "Video", AdvancedDisplay, meta = (DisplayName = "Poster Time", Units = "s"))
	float PosterTime = -1.f;

#if WITH_EDITOR
	/**
	 * Opens the video, takes the frame at Poster Time and saves it as a texture next to this clip (<clip>_Poster), set as
	 * Poster. Takes a moment; a notification says when it is done. Needs the clip's video player plugin enabled in the editor.
	 */
	UFUNCTION(CallInEditor, Category = "Video", meta = (DisplayName = "Generate Poster"))
	void GeneratePoster();

	/** Reads Subtitle File again into Subtitle Text and Subtitles - after the .srt / .vtt was edited - and each other language's file. */
	UFUNCTION(CallInEditor, Category = "Subtitles", meta = (DisplayName = "Reload Subtitle File"))
	void ReloadSubtitleFile();

	/** Reads one other language's Subtitle File into its text and cues. False when it couldn't. */
	bool ReadSubtitleTrackFile(FMediaBoxSubtitleTrack& Track);
#endif

	// ---------------------------------------------------------------------------------------------

	/** Where its audio comes from, Auto resolved. */
	EMediaBoxAudio GetResolvedAudio() const;

	/** Whether its loop is the whole video, which the player loops itself (seamless). */
	bool LoopsWholeVideo() const { return bLoop && LoopStart <= 0.f && LoopEnd <= 0.f; }

	/** The player plugin to ask for: Player Name, else the project's. */
	FName GetPlayerName() const;

	/**
	 * The subtitle cues for Language: the track of that language, else one sharing its main part (fr-CA and fr), else Subtitles.
	 * OutLanguage: the language of the cues returned.
	 */
	TConstArrayView<FMediaBoxSubtitleCue> GetSubtitleCues(const FString& Language, FString* OutLanguage = nullptr) const;

	/** The languages it has subtitles in - Subtitle Language first (only when it has Subtitles) - with their names for menus. */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Subtitles")
	void GetSubtitleLanguages(TArray<FString>& Languages, TArray<FText>& LanguageNames) const;

	/** A language's name for menus: the name given here, else the culture's own name (English, Français), else the code. */
	FText GetSubtitleLanguageName(const FString& Language) const;

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
