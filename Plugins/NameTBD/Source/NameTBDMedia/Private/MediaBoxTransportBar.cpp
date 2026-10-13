// Copyright AR GameSystems 2026. All rights reserved.

#include "MediaBoxTransportBar.h"

#include "MediaBox.h"
#include "MediaBoxCue.h"
#include "MediaBoxLibrary.h"

#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"

#define LOCTEXT_NAMESPACE "MediaBoxTransportBar"

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
	bReportedOnce = false;
}

void UMediaBoxTransportBar::NativeDestruct()
{
	if (bScrubbing)
	{
		HandleScrubEnd();
	}
	Super::NativeDestruct();
}

void UMediaBoxTransportBar::SetMediaBox(UMediaBox* NewMediaBox)
{
	DrivenBox = NewMediaBox;
	bBoxSetByHand = NewMediaBox != nullptr;
	bReportedOnce = false;
}

UMediaBox* UMediaBoxTransportBar::FindDrivenBox()
{
	if (!DrivenBox.IsValid() && !bBoxSetByHand)
	{
		DrivenBox = MediaBoxFind::Find(this, nullptr, MediaBoxName);
	}
	return DrivenBox.Get();
}

FText UMediaBoxTransportBar::FormatTime(float Seconds)
{
	const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
	const int32 Hours = Whole / 3600;
	const int32 Minutes = (Whole / 60) % 60;
	const int32 Rest = Whole % 60;
	return Hours > 0
		? FText::FromString(FString::Printf(TEXT("%d:%02d:%02d"), Hours, Minutes, Rest))
		: FText::FromString(FString::Printf(TEXT("%d:%02d"), Minutes, Rest));
}

void UMediaBoxTransportBar::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	UMediaBox* Box = FindDrivenBox();
	if (!Box)
	{
		return;
	}

	const float Duration = Box->GetDuration();
	const float Elapsed = Box->GetTime();

	if (ScrubBar && !bScrubbing)
	{
		ScrubBar->SetValue(Duration > 0.f ? FMath::Clamp(Elapsed / Duration, 0.f, 1.f) : 0.f);
	}

	if (TimeText)
	{
		const int32 ShownSeconds = FMath::FloorToInt(Elapsed);
		const int32 ShownDuration = FMath::FloorToInt(Duration);
		if (ShownSeconds != LastShownSeconds || ShownDuration != LastShownDuration)
		{
			LastShownSeconds = ShownSeconds;
			LastShownDuration = ShownDuration;
			TimeText->SetText(bElapsedTimeOnly || Duration <= 0.f
				? FormatTime(Elapsed)
				: FText::Format(LOCTEXT("TimeOfDuration", "{0} / {1}"), FormatTime(Elapsed), FormatTime(Duration)));
		}
	}

	if (SubtitleText && !Box->GetCurrentSubtitle().EqualTo(LastSubtitle))
	{
		LastSubtitle = Box->GetCurrentSubtitle();
		SubtitleText->SetText(UMediaBoxLibrary::AreMediaBoxSubtitlesEnabled() ? LastSubtitle : FText::GetEmpty());
	}

	if (VolumeSlider && !bReportedOnce)
	{
		VolumeSlider->SetValue(FMath::Clamp(Box->AudioVolume, 0.f, 1.f));
	}

	const bool bPlayingNow = Box->IsPlaying() || Box->GetStatus() == EMediaBoxStatus::Loading;
	if (bPlayingNow != bWasPlaying || !bReportedOnce)
	{
		bWasPlaying = bPlayingNow;
		OnPlayingChanged(bPlayingNow);
	}
	if (Box->bMuted != bWasMuted || !bReportedOnce)
	{
		bWasMuted = Box->bMuted;
		OnMutedChanged(bWasMuted);
	}
	bReportedOnce = true;
}

void UMediaBoxTransportBar::HandlePlayPauseClicked()
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->TogglePlayPause();
	}
}

void UMediaBoxTransportBar::HandleStopClicked()
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->Stop();
	}
}

void UMediaBoxTransportBar::HandleMuteClicked()
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->SetMuted(!Box->bMuted);
	}
}

void UMediaBoxTransportBar::HandleSubtitlesClicked()
{
	UMediaBoxLibrary::SetMediaBoxSubtitlesEnabled(!UMediaBoxLibrary::AreMediaBoxSubtitlesEnabled());
	LastSubtitle = FText::GetEmpty();
	if (SubtitleText)
	{
		SubtitleText->SetText(FText::GetEmpty());
	}
}

void UMediaBoxTransportBar::HandleScrubBegin()
{
	bScrubbing = true;
	if (UMediaBox* Box = FindDrivenBox())
	{
		// Paused while dragged, so the frames follow the bar; played on after if it was playing.
		bResumeAfterScrub = Box->IsPlaying();
		if (bResumeAfterScrub)
		{
			Box->Pause();
		}
	}
}

void UMediaBoxTransportBar::HandleScrubEnd()
{
	bScrubbing = false;
	if (UMediaBox* Box = FindDrivenBox())
	{
		if (bResumeAfterScrub)
		{
			Box->Play();
		}
	}
	bResumeAfterScrub = false;
}

void UMediaBoxTransportBar::HandleScrubChanged(float NewValue)
{
	if (!bScrubbing)
	{
		return;
	}
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->Seek(NewValue * Box->GetDuration());
	}
}

void UMediaBoxTransportBar::HandleVolumeChanged(float NewValue)
{
	if (UMediaBox* Box = FindDrivenBox())
	{
		Box->SetVolume(NewValue);
		if (Box->bMuted && NewValue > 0.f)
		{
			Box->SetMuted(false);
		}
	}
}

#undef LOCTEXT_NAMESPACE
