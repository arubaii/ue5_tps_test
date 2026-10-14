// Copyright AR GameSystems 2026. All rights reserved.

#include "MediaBoxTransportBar.h"

#include "MediaBox.h"
#include "MediaBoxClip.h"
#include "MediaBoxCue.h"
#include "MediaBoxLibrary.h"

#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Misc/ConfigCacheIni.h"

#define LOCTEXT_NAMESPACE "MediaBoxTransportBar"

namespace MediaBoxTransportBarPrivate
{
	/** Where Remember Volume keeps the volumes, in the game's user settings. */
	const TCHAR* VolumeSection = TEXT("/Script/NameTBDMedia.MediaBoxVolume");

	/** The scrub bar let go this near its start counts as at the start (Pause When Scrubbed To Start). */
	constexpr float StartOfBar = 0.005f;
	constexpr float StartSeconds = 0.1f;

	bool HasTimePlaceholder(const FString& Text)
	{
		return Text.Contains(TEXT("{StartTime}")) || Text.Contains(TEXT("{EndTime}")) || Text.Contains(TEXT("{Remaining}"));
	}
}

UMediaBoxTransportBar::UMediaBoxTransportBar(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TimeFormat = LOCTEXT("DefaultTimeFormat", "{StartTime} / {EndTime}");
	PlaybackSpeedFormat = LOCTEXT("DefaultSpeedFormat", "{Speed}x");
	NormalSpeedText = LOCTEXT("NormalSpeed", "Normal");
	PlaybackSpeeds = { 0.25f, 0.5f, 0.75f, 1.f, 1.25f, 1.5f, 1.75f, 2.f };
}

void UMediaBoxTransportBar::NativeConstruct()
{
	Super::NativeConstruct();

	if (PlayPauseButton)
	{
		PlayPauseButton->OnClicked.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandlePlayPauseClicked);
	}
	if (StopButton)
	{
		StopButton->OnClicked.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandleStopClicked);
	}
	if (MuteButton)
	{
		MuteButton->OnClicked.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandleMuteClicked);
	}
	if (SubtitlesButton)
	{
		SubtitlesButton->OnClicked.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandleSubtitlesClicked);
	}
	if (SubtitleLanguageButton)
	{
		SubtitleLanguageButton->OnClicked.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandleSubtitleLanguageClicked);
	}
	if (PlaybackSpeedButton)
	{
		PlaybackSpeedButton->OnClicked.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandlePlaybackSpeedClicked);
	}
	if (FullscreenButton)
	{
		FullscreenButton->OnClicked.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandleFullscreenClicked);
	}
	if (ScrubBar)
	{
		ScrubBar->OnMouseCaptureBegin.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandleScrubBegin);
		ScrubBar->OnMouseCaptureEnd.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandleScrubEnd);
		ScrubBar->OnControllerCaptureBegin.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandleScrubBegin);
		ScrubBar->OnControllerCaptureEnd.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandleScrubEnd);
		ScrubBar->OnValueChanged.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandleScrubChanged);
	}
	if (VolumeSlider)
	{
		VolumeSlider->OnValueChanged.AddUniqueDynamic(this, &UMediaBoxTransportBar::HandleVolumeChanged);
	}
	CaptureTimeTemplate();
	bReportedOnce = false;
}

void UMediaBoxTransportBar::NativeDestruct()
{
	if (bScrubbing)
	{
		HandleScrubEnd();
	}
	if (bVolumeSaveDirty && GConfig)
	{
		GConfig->Flush(/*bRemoveFromCache=*/false, GGameUserSettingsIni);
		bVolumeSaveDirty = false;
	}
	Super::NativeDestruct();
}

void UMediaBoxTransportBar::SetMediaBox(UMediaBox* NewMediaBox)
{
	DrivenBox = NewMediaBox;
	bBoxSetByHand = NewMediaBox != nullptr;
	bReportedOnce = false;
}

UMediaBox* UMediaBoxTransportBar::FindDrivenBox() const
{
	if (!DrivenBox.IsValid() && !bBoxSetByHand)
	{
		DrivenBox = MediaBoxFind::Find(const_cast<UMediaBoxTransportBar*>(this), nullptr, MediaBoxName);
	}
	return DrivenBox.Get();
}

void UMediaBoxTransportBar::CaptureTimeTemplate()
{
	if (bCapturedTimeTemplate || !TimeText)
	{
		return;
	}
	bCapturedTimeTemplate = true;
	const FText Authored = TimeText->GetText();
	bHasTimeTemplate = MediaBoxTransportBarPrivate::HasTimePlaceholder(Authored.ToString());
	if (bHasTimeTemplate)
	{
		TimeTemplate = Authored;
	}
}

FText UMediaBoxTransportBar::FormatVideoTime(float Seconds) const
{
	return FText::FromString(TimeDisplay.Format(Seconds));
}

FText UMediaBoxTransportBar::MakeTimeText(const UMediaBox& Box) const
{
	const float Duration = Box.GetDuration();
	const float Elapsed = Box.GetTime();
	const EMediaBoxStatus Status = Box.GetStatus();
	const bool bNotStarted = (Status == EMediaBoxStatus::Stopped || Status == EMediaBoxStatus::Empty || Status == EMediaBoxStatus::Loading)
		&& Elapsed <= 0.f;

	// TimeText's own text, else Time Format; before it starts, Not Started Time Format when there is one.
	FText Format = bHasTimeTemplate ? TimeTemplate : TimeFormat;
	if (bNotStarted && !NotStartedTimeFormat.IsEmpty())
	{
		Format = NotStartedTimeFormat;
	}
	else if (bElapsedTimeOnly || (Duration <= 0.f && !bHasTimeTemplate))
	{
		// Its length not known yet (or Elapsed Time Only): the time alone, as it always read.
		Format = INVTEXT("{StartTime}");
	}

	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("StartTime"), FormatVideoTime(Elapsed));
	Arguments.Add(TEXT("EndTime"), FormatVideoTime(Duration));
	Arguments.Add(TEXT("Remaining"), FormatVideoTime(FMath::Max(0.f, Duration - Elapsed)));
	return FText::Format(Format, Arguments);
}

void UMediaBoxTransportBar::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshFromMediaBox(InDeltaTime);
}

void UMediaBoxTransportBar::RefreshFromMediaBox(float DeltaTime)
{
	// Once a frame, whoever asks (its own tick, the Media Box With Controls).
	if (LastRefreshFrame == GFrameCounter)
	{
		return;
	}
	LastRefreshFrame = GFrameCounter;

	UMediaBox* Box = FindDrivenBox();
	if (!Box)
	{
		return;
	}
	CaptureTimeTemplate();
	ApplySavedVolume(*Box);

	const float Duration = Box->GetDuration();
	const float Elapsed = Box->GetTime();

	if (ScrubBar && !bScrubbing)
	{
		ScrubBar->SetValue(Duration > 0.f ? FMath::Clamp(Elapsed / Duration, 0.f, 1.f) : 0.f);
	}

	if (TimeText)
	{
		const FText Shown = MakeTimeText(*Box);
		const FString ShownString = Shown.ToString();
		if (ShownString != LastTimeString)
		{
			LastTimeString = ShownString;
			TimeText->SetText(Shown);
		}
	}

	const bool bSubtitlesOn = UMediaBoxLibrary::AreMediaBoxSubtitlesEnabled();
	if (SubtitleText && (!Box->GetCurrentSubtitle().EqualTo(LastSubtitle) || bSubtitlesOn != bWasSubtitlesEnabled))
	{
		LastSubtitle = Box->GetCurrentSubtitle();
		SubtitleText->SetText(bSubtitlesOn ? LastSubtitle : FText::GetEmpty());
	}

	const float VolumeNow = Box->GetVolume();
	if (VolumeSlider && (!bReportedOnce || !FMath::IsNearlyEqual(VolumeNow, LastVolume)))
	{
		VolumeSlider->SetValue(FMath::Clamp(VolumeNow, 0.f, 1.f));
	}

	const float SpeedNow = Box->GetPlaybackRate();
	if (PlaybackSpeedText && (!bReportedOnce || !FMath::IsNearlyEqual(SpeedNow, LastSpeed)))
	{
		PlaybackSpeedText->SetText(GetVideoPlaybackSpeedText(SpeedNow));
	}

	const FString LanguageNow = Box->GetSubtitleLanguage();
	const bool bLanguageChanged = LanguageNow != LastLanguage;
	if (SubtitleLanguageText && (!bReportedOnce || bLanguageChanged))
	{
		SubtitleLanguageText->SetText(GetSubtitleLanguageName());
	}

	// The events, as each thing changes (all of them once, as it first finds its box).
	const bool bPlayingNow = Box->IsPlaying() || Box->GetStatus() == EMediaBoxStatus::Loading || Box->WillPlayAfterScrub();
	if (bPlayingNow != bWasPlaying || !bReportedOnce)
	{
		bWasPlaying = bPlayingNow;
		OnPlayingChanged(bPlayingNow);
	}
	if (Box->IsMuted() != bWasMuted || !bReportedOnce)
	{
		bWasMuted = Box->IsMuted();
		OnMutedChanged(bWasMuted);
	}
	if (!FMath::IsNearlyEqual(VolumeNow, LastVolume) || !bReportedOnce)
	{
		LastVolume = VolumeNow;
		OnVolumeChanged(VolumeNow);
	}
	if (!FMath::IsNearlyEqual(SpeedNow, LastSpeed) || !bReportedOnce)
	{
		LastSpeed = SpeedNow;
		OnPlaybackSpeedChanged(SpeedNow);
	}
	if (Box->IsFullscreen() != bWasFullscreen || !bReportedOnce)
	{
		bWasFullscreen = Box->IsFullscreen();
		OnFullscreenChanged(bWasFullscreen);
	}
	if (bSubtitlesOn != bWasSubtitlesEnabled || !bReportedOnce)
	{
		bWasSubtitlesEnabled = bSubtitlesOn;
		OnSubtitlesEnabledChanged(bSubtitlesOn);
	}
	if (bLanguageChanged || !bReportedOnce)
	{
		LastLanguage = LanguageNow;
		OnSubtitleLanguageChanged(LanguageNow, GetSubtitleLanguageName());
	}
	bReportedOnce = true;

	SaveVolumeIfChanged(*Box);
}

// ---------------------------------------------------------------------------------------------
// Remember Volume
// ---------------------------------------------------------------------------------------------

void UMediaBoxTransportBar::ApplySavedVolume(UMediaBox& Box)
{
	if (!bRememberVolume || IsDesignTime() || !GConfig || VolumeAppliedTo.Get() == &Box)
	{
		return;
	}
	VolumeAppliedTo = &Box;

	const FString Key = VolumeSaveName.ToString();
	float Saved = Box.GetVolume();
	bool bSavedMute = Box.IsMuted();
	const bool bHasVolume = GConfig->GetFloat(MediaBoxTransportBarPrivate::VolumeSection, *(Key + TEXT(".Volume")), Saved, GGameUserSettingsIni);
	const bool bHasMute = GConfig->GetBool(MediaBoxTransportBarPrivate::VolumeSection, *(Key + TEXT(".Muted")), bSavedMute, GGameUserSettingsIni);
	if (bHasVolume)
	{
		Box.SetVolume(Saved);
	}
	if (bHasMute)
	{
		Box.SetMuted(bSavedMute);
	}
	SavedVolume = Box.GetVolume();
	bSavedMuted = Box.IsMuted();
}

void UMediaBoxTransportBar::SaveVolumeIfChanged(const UMediaBox& Box)
{
	if (!bRememberVolume || IsDesignTime() || !GConfig || VolumeAppliedTo.Get() != &Box)
	{
		return;
	}
	if (FMath::IsNearlyEqual(Box.GetVolume(), SavedVolume) && Box.IsMuted() == bSavedMuted)
	{
		return;
	}
	SavedVolume = Box.GetVolume();
	bSavedMuted = Box.IsMuted();
	const FString Key = VolumeSaveName.ToString();
	GConfig->SetFloat(MediaBoxTransportBarPrivate::VolumeSection, *(Key + TEXT(".Volume")), SavedVolume, GGameUserSettingsIni);
	GConfig->SetBool(MediaBoxTransportBarPrivate::VolumeSection, *(Key + TEXT(".Muted")), bSavedMuted, GGameUserSettingsIni);

	// Written to disk as the bar goes (and when the volume slider is let go): not on every step of a drag.
	bVolumeSaveDirty = true;
}

// ---------------------------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------------------------

void UMediaBoxTransportBar::TogglePlayPause()
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->TogglePlayPause();
	}
}

void UMediaBoxTransportBar::PlayVideo()
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->Play();
	}
}

void UMediaBoxTransportBar::PauseVideo()
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->Pause();
	}
}

void UMediaBoxTransportBar::StopVideo()
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->Stop();
	}
}

bool UMediaBoxTransportBar::IsVideoPlaying() const
{
	const UMediaBox* Box = FindDrivenBox();
	return Box && Box->IsPlaying();
}

void UMediaBoxTransportBar::SeekTo(float Seconds)
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		const float Duration = Box->GetDuration();
		Box->Seek(Duration > 0.f ? FMath::Clamp(Seconds, 0.f, Duration) : FMath::Max(0.f, Seconds));
	}
}

void UMediaBoxTransportBar::SeekBy(float Seconds)
{
	SeekTo(GetVideoTime() + Seconds);
}

float UMediaBoxTransportBar::GetVideoTime() const
{
	const UMediaBox* Box = FindDrivenBox();
	return Box ? Box->GetTime() : 0.f;
}

float UMediaBoxTransportBar::GetVideoDuration() const
{
	const UMediaBox* Box = FindDrivenBox();
	return Box ? Box->GetDuration() : 0.f;
}

void UMediaBoxTransportBar::ToggleMute()
{
	SetMuted(!IsMuted());
}

void UMediaBoxTransportBar::SetMuted(bool bNewMuted)
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->SetMuted(bNewMuted);
	}
}

bool UMediaBoxTransportBar::IsMuted() const
{
	const UMediaBox* Box = FindDrivenBox();
	return Box && Box->IsMuted();
}

void UMediaBoxTransportBar::SetVolume(float NewVolume)
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->SetVolume(NewVolume);
		if (Box->IsMuted() && NewVolume > 0.f)
		{
			Box->SetMuted(false);
		}
	}
}

float UMediaBoxTransportBar::GetVolume() const
{
	const UMediaBox* Box = FindDrivenBox();
	return Box ? Box->GetVolume() : 0.f;
}

void UMediaBoxTransportBar::ToggleSubtitles()
{
	SetSubtitlesEnabled(!AreSubtitlesEnabled());
}

void UMediaBoxTransportBar::SetSubtitlesEnabled(bool bNewEnabled)
{
	UMediaBoxLibrary::SetMediaBoxSubtitlesEnabled(bNewEnabled);
	LastSubtitle = FText::GetEmpty();
	if (SubtitleText)
	{
		SubtitleText->SetText(FText::GetEmpty());
	}
}

bool UMediaBoxTransportBar::AreSubtitlesEnabled() const
{
	return UMediaBoxLibrary::AreMediaBoxSubtitlesEnabled();
}

void UMediaBoxTransportBar::CycleSubtitleLanguage()
{
	TArray<FString> Languages;
	TArray<FText> LanguageNames;
	GetSubtitleLanguages(Languages, LanguageNames);
	if (Languages.Num() == 0)
	{
		return;
	}
	const FString Current = GetSubtitleLanguage();
	const int32 CurrentIndex = Languages.IndexOfByPredicate([&Current](const FString& Candidate) { return Candidate.Equals(Current, ESearchCase::IgnoreCase); });
	SetSubtitleLanguage(Languages[(CurrentIndex + 1) % Languages.Num()]);
}

void UMediaBoxTransportBar::SetSubtitleLanguage(const FString& NewLanguage)
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->SetSubtitleLanguage(NewLanguage);
	}
}

FString UMediaBoxTransportBar::GetSubtitleLanguage() const
{
	const UMediaBox* Box = FindDrivenBox();
	return Box ? Box->GetSubtitleLanguage() : FString();
}

FText UMediaBoxTransportBar::GetSubtitleLanguageName() const
{
	const UMediaBox* Box = FindDrivenBox();
	const UMediaBoxClip* Clip = Box ? Box->GetClip() : nullptr;
	if (!Clip)
	{
		return FText::GetEmpty();
	}
	// The language the clip shows for the one asked (fr-CA asked, fr shown).
	FString Shown;
	Clip->GetSubtitleCues(Box->GetSubtitleLanguage(), &Shown);
	return Clip->GetSubtitleLanguageName(Shown);
}

void UMediaBoxTransportBar::GetSubtitleLanguages(TArray<FString>& Languages, TArray<FText>& LanguageNames) const
{
	Languages.Reset();
	LanguageNames.Reset();
	if (const UMediaBox* Box = FindDrivenBox())
	{
		Box->GetSubtitleLanguages(Languages, LanguageNames);
	}
}

void UMediaBoxTransportBar::SetVideoPlaybackSpeed(float NewSpeed)
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->SetPlaybackRate(NewSpeed);
	}
}

float UMediaBoxTransportBar::GetVideoPlaybackSpeed() const
{
	const UMediaBox* Box = FindDrivenBox();
	return Box ? Box->GetPlaybackRate() : 1.f;
}

void UMediaBoxTransportBar::CycleVideoPlaybackSpeed()
{
	if (PlaybackSpeeds.Num() == 0)
	{
		return;
	}
	// The next speed above the current one, round to the slowest.
	TArray<float> Sorted = PlaybackSpeeds;
	Sorted.Sort();
	const float Current = GetVideoPlaybackSpeed();
	float Next = Sorted[0];
	for (const float Candidate : Sorted)
	{
		if (Candidate > Current + KINDA_SMALL_NUMBER)
		{
			Next = Candidate;
			break;
		}
	}
	SetVideoPlaybackSpeed(Next);
}

FText UMediaBoxTransportBar::GetVideoPlaybackSpeedText(float Speed) const
{
	if (FMath::IsNearlyEqual(Speed, 1.f) && !NormalSpeedText.IsEmpty())
	{
		return NormalSpeedText;
	}
	FNumberFormattingOptions Digits;
	Digits.MinimumFractionalDigits = 0;
	Digits.MaximumFractionalDigits = 2;
	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("Speed"), FText::AsNumber(Speed, &Digits));
	return FText::Format(PlaybackSpeedFormat, Arguments);
}

void UMediaBoxTransportBar::ToggleFullscreen()
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->ToggleFullscreen();
	}
}

void UMediaBoxTransportBar::SetFullscreen(bool bNewFullscreen)
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->SetFullscreen(bNewFullscreen);
	}
}

bool UMediaBoxTransportBar::IsFullscreen() const
{
	const UMediaBox* Box = FindDrivenBox();
	return Box && Box->IsFullscreen();
}

// ---------------------------------------------------------------------------------------------
// Named parts
// ---------------------------------------------------------------------------------------------

void UMediaBoxTransportBar::HandlePlayPauseClicked()
{
	TogglePlayPause();
}

void UMediaBoxTransportBar::HandleStopClicked()
{
	StopVideo();
}

void UMediaBoxTransportBar::HandleMuteClicked()
{
	ToggleMute();
}

void UMediaBoxTransportBar::HandleSubtitlesClicked()
{
	ToggleSubtitles();
}

void UMediaBoxTransportBar::HandleSubtitleLanguageClicked()
{
	CycleSubtitleLanguage();
}

void UMediaBoxTransportBar::HandlePlaybackSpeedClicked()
{
	CycleVideoPlaybackSpeed();
}

void UMediaBoxTransportBar::HandleFullscreenClicked()
{
	ToggleFullscreen();
}

void UMediaBoxTransportBar::HandleScrubBegin()
{
	bScrubbing = true;
	LastScrubValue = ScrubBar ? ScrubBar->GetValue() : 0.f;
	if (UMediaBox* Box = FindDrivenBox())
	{
		// Paused while dragged, so the frames follow the bar; played on after if it was playing.
		Box->BeginScrub();
	}
}

void UMediaBoxTransportBar::HandleScrubEnd()
{
	using namespace MediaBoxTransportBarPrivate;

	bScrubbing = false;
	if (UMediaBox* Box = FindDrivenBox())
	{
		// Let go at the very start (Pause When Scrubbed To Start): stays paused there.
		const bool bAtStart = LastScrubValue <= StartOfBar || LastScrubValue * Box->GetDuration() <= StartSeconds;
		Box->EndScrub(/*bPlayOn=*/!(bPauseWhenScrubbedToStart && bAtStart));
	}
}

void UMediaBoxTransportBar::HandleScrubChanged(float NewValue)
{
	// Only the player's own moves of the bar (the bar following the video is set from here, never seeks).
	if (!bScrubbing)
	{
		return;
	}
	LastScrubValue = NewValue;
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->ScrubTo(NewValue * Box->GetDuration());
	}
}

void UMediaBoxTransportBar::HandleVolumeChanged(float NewValue)
{
	SetVolume(NewValue);
}

#undef LOCTEXT_NAMESPACE
