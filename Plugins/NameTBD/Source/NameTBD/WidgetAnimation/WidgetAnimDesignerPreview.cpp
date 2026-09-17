// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimDesignerPreview.h"

#include "Components/Widget.h"

void IWidgetAnimDesignerPreviewHost::EnsurePreviewTimer()
{
	UWidgetAnimDesignerPreview::EnsureTimer(this);
}

bool UWidgetAnimDesignerPreview::ShouldRun(IWidgetAnimDesignerPreviewHost* Host)
{
	if (!Host)
	{
		return false;
	}

	const UWidget* Owner = Host->GetPreviewOwnerWidget();
	if (!Owner || !Owner->IsDesignTime())
	{
		return false;
	}

	if (Host->GetPreviewSettings().Mode == EWidgetAnimPreviewMode::Off)
	{
		return false;
	}

	// Nothing to play is the same as Off as far as the harness is concerned, and saying so here stops a
	// timer being registered every frame for a widget whose animation list is still empty.
	return Host->GetPreviewAnimIn().Num() > 0 || Host->GetPreviewAnimOut().Num() > 0;
}

void UWidgetAnimDesignerPreview::RecaptureBaselines(IWidgetAnimDesignerPreviewHost* Host)
{
	if (!Host)
	{
		return;
	}

	FWidgetAnimBaselineSet& Baselines = Host->GetPreviewBaselines();

	// Restored before re-capturing, or the pose recorded as "authored" is really whatever frame the last
	// animation left behind, and every rebuild drifts a little further from what the Details panel says.
	UWidgetAnimLibrary::RestoreWidgetAnimBaselines(Baselines);
	Baselines.ResetBaselines();
	UWidgetAnimLibrary::CaptureWidgetAnimBaselines(
		Host->GetPreviewAnimationTarget(),
		Host->GetPreviewAnimIn(),
		Host->GetPreviewAnimOut(),
		Baselines);
}

void UWidgetAnimDesignerPreview::Synchronize(IWidgetAnimDesignerPreviewHost* Host)
{
	if (!Host)
	{
		return;
	}

	UWidget* Owner = Host->GetPreviewOwnerWidget();
	if (!Owner || !Owner->IsDesignTime())
	{
		return;
	}

#if WITH_EDITOR
	// So a mid-drag edit on the template can reach this preview. See WidgetAnimDesignerSync.
	WidgetAnimDesignerSync::RegisterPreview(Owner);
#endif

	// Design time only. At runtime the stacks are built when the host constructs, which is the point at
	// which any Blueprint that wanted to change them first had a chance to.
	Host->RebuildPreviewAnimations();
	RecaptureBaselines(Host);

	if (ShouldRun(Host))
	{
		Host->EnsurePreviewTimer();
	}
	else
	{
		// Covers switching Mode back to Off: the timer stops on its own, but the widgets have to be put
		// back now or they stay frozen part-way through whatever was playing.
		Stop(Host);
	}
}

void UWidgetAnimDesignerPreview::EnsureTimer(IWidgetAnimDesignerPreviewHost* Host)
{
	if (!Host)
	{
		return;
	}

	FWidgetAnimPreviewState& State = Host->GetPreviewState();
	if (State.bTimerActive)
	{
		return;
	}

	UWidget* Owner = Host->GetPreviewOwnerWidget();
	if (!Owner)
	{
		return;
	}

	// SynchronizeProperties can run before the Slate widget exists, and an active timer needs one to
	// register against. OnWidgetRebuilt is the second chance.
	TSharedPtr<SWidget> SlateWidget = Owner->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return;
	}

	State.bTimerActive = true;

	// An active timer rather than a tick: it keeps the widget ticking AND forces the repaint, which is
	// what makes this visible in the designer viewport rather than only when something else redraws it.
	//
	// The host is reached back through a weak pointer rather than captured directly, because design-time
	// widgets are thrown away and rebuilt constantly and the timer outlives some of them.
	TWeakObjectPtr<UWidget> WeakOwner(Owner);
	SlateWidget->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
		[WeakOwner](double, float InDeltaTime) -> EActiveTimerReturnType
		{
			UWidget* LiveOwner = WeakOwner.Get();
			IWidgetAnimDesignerPreviewHost* LiveHost = LiveOwner ? Cast<IWidgetAnimDesignerPreviewHost>(LiveOwner) : nullptr;
			if (!LiveHost)
			{
				return EActiveTimerReturnType::Stop;
			}

			return UWidgetAnimDesignerPreview::Tick(LiveHost, InDeltaTime);
		}));
}

bool UWidgetAnimDesignerPreview::TickFrame(IWidgetAnimDesignerPreviewHost* Host, float DeltaTime)
{
	if (!Host)
	{
		return false;
	}
	
	if (Host && Host->IsPreviewPaused() && DeltaTime > 0.f)
	{
		return true;
	}

	return UWidgetAnimLibrary::TickWidgetAnimPreview(
		Host->GetPreviewAnimationTarget(),
		Host->GetPreviewAnimIn(),
		Host->GetPreviewAnimOut(),
		Host->GetPreviewState(),
		Host->GetPreviewSettings(),
		Host->GetPreviewBaselines(),
		DeltaTime);
}

EActiveTimerReturnType UWidgetAnimDesignerPreview::Tick(IWidgetAnimDesignerPreviewHost* Host, float DeltaTime)
{
	if (!Host)
	{
		return EActiveTimerReturnType::Stop;
	}

	UWidget* Owner = Host->GetPreviewOwnerWidget();
	if (!Owner || !Owner->GetCachedWidget().IsValid())
	{
		Host->GetPreviewState().bTimerActive = false;
		return EActiveTimerReturnType::Stop;
	}

	if (!ShouldRun(Host))
	{
		Stop(Host);
		return EActiveTimerReturnType::Stop;
	}

	if (!TickFrame(Host, DeltaTime))
	{
		// A held pose, or a Play Once that has finished. Any property edit restarts the timer, so nothing
		// is lost by stopping here. Scrub deliberately never reaches this branch - see TickWidgetAnimPreview.
		Host->GetPreviewState().bTimerActive = false;
		return EActiveTimerReturnType::Stop;
	}

	return EActiveTimerReturnType::Continue;
}

void UWidgetAnimDesignerPreview::Stop(IWidgetAnimDesignerPreviewHost* Host)
{
	if (!Host)
	{
		return;
	}

	UWidgetAnimLibrary::RestoreWidgetAnimBaselines(Host->GetPreviewBaselines());

	FWidgetAnimPreviewState& State = Host->GetPreviewState();
	UWidgetAnimLibrary::RestartWidgetAnimPreview(State);

	// Recorded so the harness does not then treat the next frame as a mode change and restart itself a
	// second time.
	State.LastMode = Host->GetPreviewSettings().Mode;
	State.bTimerActive = false;
}

void UWidgetAnimDesignerPreview::Shutdown(IWidgetAnimDesignerPreviewHost* Host)
{
	if (!Host)
	{
		return;
	}

	Host->GetPreviewState().bTimerActive = false;

#if WITH_EDITOR
	if (UWidget* Owner = Host->GetPreviewOwnerWidget())
	{
		WidgetAnimDesignerSync::UnregisterPreview(Owner);
	}
#endif
}

#if WITH_EDITOR

namespace
{
	/** Hands one live preview the edited settings and poses it there and then. */
	void PushSettingsTo(IWidgetAnimDesignerPreviewHost* Host, const FWidgetAnimPreviewSettings& Settings)
	{
		if (!Host)
		{
			return;
		}

		Host->GetPreviewSettings() = Settings;
		Host->EnsurePreviewTimer();

		// Posed immediately rather than waiting for the next timer tick: the held modes stop their own
		// timer as soon as they have drawn a frame, so there may not be a next tick to wait for.
		if (UWidgetAnimDesignerPreview::ShouldRun(Host))
		{
			UWidgetAnimDesignerPreview::TickFrame(Host, 0.f);
		}
	}
}

void UWidgetAnimDesignerPreview::NotifyPropertyChanged(UObject* Template, const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!Template || !WidgetAnimDesignerSync::IsInteractiveChange(PropertyChangedEvent))
	{
		return;
	}

	IWidgetAnimDesignerPreviewHost* TemplateHost = Cast<IWidgetAnimDesignerPreviewHost>(Template);
	if (!TemplateHost)
	{
		return;
	}

	const FWidgetAnimPreviewSettings SettingsToPush = TemplateHost->GetPreviewSettings();

	// The edited object can itself be the one on screen - a widget selected inside its own Widget
	// Blueprint, rather than a placement of it in some parent. ShouldRun screens out the case where it
	// is only a template, so this costs nothing when it does not apply.
	PushSettingsTo(TemplateHost, SettingsToPush);

	WidgetAnimDesignerSync::ForEachPreviewOf(Template, [&SettingsToPush](UObject* PreviewObject)
	{
		PushSettingsTo(Cast<IWidgetAnimDesignerPreviewHost>(PreviewObject), SettingsToPush);
	});
}

#endif // WITH_EDITOR