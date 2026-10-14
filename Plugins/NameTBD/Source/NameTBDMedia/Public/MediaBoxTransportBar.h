// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "MediaBoxTypes.h"

#include "MediaBoxTransportBar.generated.h"

class UButton;
class UMediaBox;
class USlider;
class UTextBlock;

/**
 * Media Box Transport Bar: the controls under a video - play / pause, stop, a scrub bar, the time, volume, mute, subtitles and
 * their language, playback speed, full screen - as a Widget Blueprint you lay out yourself. Make a Widget Blueprint with this as
 * its parent class and add any of the named parts (each optional): PlayPauseButton, StopButton, ScrubBar (Slider), TimeText,
 * VolumeSlider, MuteButton, SubtitlesButton, SubtitleText, SubtitleLanguageButton, SubtitleLanguageText, PlaybackSpeedButton,
 * PlaybackSpeedText, FullscreenButton. Each part is wired up by its name; style them as any Button, Slider or Text (Context Menu
 * Buttons work, being Buttons). The On ... Changed events swap icons.
 *
 * Every control is also a function (Toggle Play Pause, Set Video Playback Speed / Get Video Playback Speed, Toggle Fullscreen, Cycle Subtitle
 * Language...): build your own - a right-click Info Card with the speeds, YouTube's settings menu - and call them, with or without
 * the named parts.
 *
 * Drives the Media Box named Media Box in the Widget Blueprint it is in (or any around it); None: the first one found around
 * it. Or Set Media Box.
 */
UCLASS(Abstract, meta = (DisplayName = "Media Box Transport Bar"))
class NAMETBDMEDIA_API UMediaBoxTransportBar : public UUserWidget
{
	GENERATED_BODY()

public:

	UMediaBoxTransportBar(const FObjectInitializer& ObjectInitializer);

	/** The Media Box it drives, by name around it. None: the first found around it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box", meta = (DisplayName = "Media Box"))
	FName MediaBoxName;

	// ---------------------------------------------------------------------------------------------
	// Time
	// ---------------------------------------------------------------------------------------------

	/**
	 * What TimeText reads, with {StartTime} (where the video is), {EndTime} (its length) and {Remaining}. Type them into TimeText's
	 * own text in the Widget Blueprint instead ("{StartTime} of {EndTime}") and that text is used, styled as you laid it out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box|Time", meta = (DisplayName = "Time Format"))
	FText TimeFormat;

	/**
	 * What TimeText reads before the video has started (stopped on its poster, focused or hovered but not played): e.g. {EndTime}
	 * alone for its length. Empty: Time Format.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box|Time", meta = (DisplayName = "Not Started Time Format"))
	FText NotStartedTimeFormat;

	/** How each time is written: hours, padding, milliseconds, delimiters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box|Time", meta = (DisplayName = "Time Display"))
	FMediaBoxTimeDisplay TimeDisplay;

	/** Legacy: TimeText reads only {StartTime} ("0:05"), whatever Time Format says. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box|Time", AdvancedDisplay, meta = (DisplayName = "Elapsed Time Only"))
	bool bElapsedTimeOnly = false;

	// ---------------------------------------------------------------------------------------------
	// Scrub, speed, volume
	// ---------------------------------------------------------------------------------------------

	/** The scrub bar let go at its very start pauses the video there (rather than playing on from the start). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box|Scrub", meta = (DisplayName = "Pause When Scrubbed To Start"))
	bool bPauseWhenScrubbedToStart = true;

	/** The speeds Playback Speed Button steps through (and a speed menu offers), slowest first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box|Speed", meta = (DisplayName = "Playback Speeds"))
	TArray<float> PlaybackSpeeds;

	/** What PlaybackSpeedText reads: {Speed} is the speed (1.5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box|Speed", meta = (DisplayName = "Playback Speed Format"))
	FText PlaybackSpeedFormat;

	/** What PlaybackSpeedText reads at speed 1. Empty: Playback Speed Format. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box|Speed", meta = (DisplayName = "Normal Speed Text"))
	FText NormalSpeedText;

	/**
	 * Remembers the volume and mute the player set, in the game's user settings, and starts every bar with the same Volume Save
	 * Name at them - the next video, the next session.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box|Volume", meta = (DisplayName = "Remember Volume"))
	bool bRememberVolume = false;

	/** Bars sharing this name share the remembered volume (one for trailers, one for tutorials...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box|Volume", meta = (DisplayName = "Volume Save Name", EditCondition = "bRememberVolume"))
	FName VolumeSaveName = TEXT("Default");

	// ---------------------------------------------------------------------------------------------
	// Media Box
	// ---------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void SetMediaBox(UMediaBox* NewMediaBox);

	UFUNCTION(BlueprintPure, Category = "Media Box")
	UMediaBox* GetMediaBox() const { return DrivenBox.Get(); }

	// ---------------------------------------------------------------------------------------------
	// Controls, as functions (each named part calls these)
	// ---------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void TogglePlayPause();

	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void PlayVideo();

	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void PauseVideo();

	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void StopVideo();

	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	bool IsVideoPlaying() const;

	/** Jumps to Seconds into the video. */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void SeekTo(float Seconds);

	/** Jumps Seconds on (or back, below 0) - a skip ahead 10 s button. */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void SeekBy(float Seconds);

	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	float GetVideoTime() const;

	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	float GetVideoDuration() const;

	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void ToggleMute();

	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void SetMuted(bool bNewMuted);

	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	bool IsMuted() const;

	/** The volume, 0 - 1 (above 1 louder). A volume above 0 unmutes. */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void SetVolume(float NewVolume);

	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	float GetVolume() const;

	/** Subtitles on or off - for every Media Box (the game's subtitles option). */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void ToggleSubtitles();

	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void SetSubtitlesEnabled(bool bNewEnabled);

	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	bool AreSubtitlesEnabled() const;

	/** The next language the video has subtitles in (round to the first). */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void CycleSubtitleLanguage();

	/** Shows the subtitles in NewLanguage (a culture code: en, fr...) on the Media Box. Empty: the game's language. */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void SetSubtitleLanguage(const FString& NewLanguage);

	/** The language the subtitles are in now. */
	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	FString GetSubtitleLanguage() const;

	/** Its name for a menu (English, Français). */
	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	FText GetSubtitleLanguageName() const;

	/** The languages the video has subtitles in, with their names - for a language menu. */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void GetSubtitleLanguages(TArray<FString>& Languages, TArray<FText>& LanguageNames) const;

	/** Plays at NewSpeed: 1 normal, 0.5 half, 2 double. */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void SetVideoPlaybackSpeed(float NewSpeed);

	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	float GetVideoPlaybackSpeed() const;

	/** The next of Playback Speeds (round to the slowest). */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void CycleVideoPlaybackSpeed();

	/** A speed as PlaybackSpeedText writes it ("1.5x", "Normal") - for a speed menu's entries. */
	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	FText GetVideoPlaybackSpeedText(float Speed) const;

	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void ToggleFullscreen();

	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void SetFullscreen(bool bNewFullscreen);

	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	bool IsFullscreen() const;

	/** Seconds as Time Display writes them ("1:02:03"). */
	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	FText FormatVideoTime(float Seconds) const;

	// ---------------------------------------------------------------------------------------------
	// Events (icons)
	// ---------------------------------------------------------------------------------------------

	/** The video started or stopped playing - swap the play / pause icon here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Media Box")
	void OnPlayingChanged(bool bPlaying);

	/** The Media Box was muted or unmuted - swap the speaker icon here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Media Box")
	void OnMutedChanged(bool bIsMuted);

	/** The volume changed - a speaker icon with one, two or three waves. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Media Box")
	void OnVolumeChanged(float NewVolume);

	/** The playback speed changed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Media Box")
	void OnPlaybackSpeedChanged(float NewSpeed);

	/** The video went full screen or back - swap the full-screen icon here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Media Box")
	void OnFullscreenChanged(bool bIsFullscreen);

	/** Subtitles were turned on or off. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Media Box")
	void OnSubtitlesEnabledChanged(bool bSubtitlesOn);

	/** The subtitle language changed. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Media Box")
	void OnSubtitleLanguageChanged(const FString& Language, const FText& LanguageName);

	/**
	 * Brings the parts up to date with the box: scrub bar, time, volume, speed, language, the events. Called every tick - and by a
	 * Media Box With Controls as it ticks (so the bar follows its preview in the designer too); once a frame whoever asks.
	 */
	void RefreshFromMediaBox(float DeltaTime);

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlayPauseButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UButton> StopButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<USlider> ScrubBar = nullptr;

	/** The time. Its own text may hold {StartTime} / {EndTime} / {Remaining}: it is then the format (else Time Format). */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TimeText = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<USlider> VolumeSlider = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UButton> MuteButton = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SubtitlesButton = nullptr;

	/** Shows the current subtitle line here, for subtitles drawn outside the video. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleText = nullptr;

	/** Steps to the next subtitle language the video has. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SubtitleLanguageButton = nullptr;

	/** The subtitle language's name. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleLanguageText = nullptr;

	/** Steps to the next of Playback Speeds. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlaybackSpeedButton = nullptr;

	/** The playback speed ("1.5x"). */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PlaybackSpeedText = nullptr;

	/** Full screen and back. */
	UPROPERTY(BlueprintReadOnly, Category = "Parts", meta = (BindWidgetOptional))
	TObjectPtr<UButton> FullscreenButton = nullptr;

private:

	UFUNCTION()
	void HandlePlayPauseClicked();

	UFUNCTION()
	void HandleStopClicked();

	UFUNCTION()
	void HandleMuteClicked();

	UFUNCTION()
	void HandleSubtitlesClicked();

	UFUNCTION()
	void HandleSubtitleLanguageClicked();

	UFUNCTION()
	void HandlePlaybackSpeedClicked();

	UFUNCTION()
	void HandleFullscreenClicked();

	UFUNCTION()
	void HandleScrubBegin();

	UFUNCTION()
	void HandleScrubEnd();

	UFUNCTION()
	void HandleScrubChanged(float NewValue);

	UFUNCTION()
	void HandleVolumeChanged(float NewValue);

	UMediaBox* FindDrivenBox() const;

	/** The text TimeText reads now. */
	FText MakeTimeText(const UMediaBox& Box) const;

	/** TimeText's own text as the format, when it holds a placeholder. Once. */
	void CaptureTimeTemplate();

	/** Remember Volume: the saved volume put on Box (once per box); the box's volume saved as it changes. */
	void ApplySavedVolume(UMediaBox& Box);
	void SaveVolumeIfChanged(const UMediaBox& Box);

	mutable TWeakObjectPtr<UMediaBox> DrivenBox;
	bool bBoxSetByHand = false;
	bool bScrubbing = false;
	float LastScrubValue = 0.f;
	bool bWasPlaying = false;
	bool bWasMuted = false;
	float LastVolume = -1.f;
	float LastSpeed = -1.f;
	bool bWasFullscreen = false;
	bool bWasSubtitlesEnabled = false;
	FString LastLanguage;
	bool bReportedOnce = false;
	FText LastSubtitle;
	FString LastTimeString;
	uint64 LastRefreshFrame = 0;

	/** TimeText's own text, when it is a format. */
	FText TimeTemplate;
	bool bHasTimeTemplate = false;
	bool bCapturedTimeTemplate = false;

	/** Remember Volume: the box the saved volume was put on, what was saved last, and whether the save file needs writing. */
	TWeakObjectPtr<UMediaBox> VolumeAppliedTo;
	float SavedVolume = -1.f;
	bool bSavedMuted = false;
	bool bVolumeSaveDirty = false;
};
