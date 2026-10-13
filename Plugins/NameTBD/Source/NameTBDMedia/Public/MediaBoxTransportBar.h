// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "MediaBoxTransportBar.generated.h"

class UButton;
class UMediaBox;
class USlider;
class UTextBlock;

/**
 * Media Box Transport Bar: the controls under a video - play / pause, stop, a scrub bar, the time, volume, mute, subtitles -
 * as a Widget Blueprint you lay out yourself. Make a Widget Blueprint with this as its parent class and add any of the named
 * parts (each optional): PlayPauseButton, StopButton, ScrubBar (Slider), TimeText, VolumeSlider, MuteButton, SubtitlesButton,
 * SubtitleText. Each part is wired up by its name; style them as any Button, Slider or Text (Context Menu Buttons work, being
 * Buttons). On Playing Changed / On Muted Changed swap icons.
 *
 * Drives the Media Box named Media Box in the Widget Blueprint it is in (or any around it); None: the first one found around
 * it. Or Set Media Box.
 */
UCLASS(Abstract, meta = (DisplayName = "Media Box Transport Bar"))
class NAMETBDMEDIA_API UMediaBoxTransportBar : public UUserWidget
{
	GENERATED_BODY()

public:

	/** The Media Box it drives, by name around it. None: the first found around it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box", meta = (DisplayName = "Media Box"))
	FName MediaBoxName;

	/** How the time reads: false "0:05 / 0:12", true "0:05". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media Box", meta = (DisplayName = "Elapsed Time Only"))
	bool bElapsedTimeOnly = false;

	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void SetMediaBox(UMediaBox* NewMediaBox);

	UFUNCTION(BlueprintPure, Category = "Media Box")
	UMediaBox* GetMediaBox() const { return DrivenBox.Get(); }

	/** The video started or stopped playing - swap the play / pause icon here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Media Box")
	void OnPlayingChanged(bool bPlaying);

	/** The Media Box was muted or unmuted - swap the speaker icon here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Media Box")
	void OnMutedChanged(bool bIsMuted);

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
	void HandleScrubBegin();

	UFUNCTION()
	void HandleScrubEnd();

	UFUNCTION()
	void HandleScrubChanged(float NewValue);

	UFUNCTION()
	void HandleVolumeChanged(float NewValue);

	UMediaBox* FindDrivenBox();

	static FText FormatTime(float Seconds);

	TWeakObjectPtr<UMediaBox> DrivenBox;
	bool bBoxSetByHand = false;
	bool bScrubbing = false;
	bool bResumeAfterScrub = false;
	bool bWasPlaying = false;
	bool bWasMuted = false;
	bool bReportedOnce = false;
	FText LastSubtitle;
	int32 LastShownSeconds = -1;
	int32 LastShownDuration = -1;
};
