// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimPreviewBox.h"

#include "Widgets/SWidget.h"

#define LOCTEXT_NAMESPACE "WidgetAnimPreviewBox"

UWidgetAnimPreviewBox::UWidgetAnimPreviewBox()
{
	Preview.Mode = EWidgetAnimPreviewMode::LoopInOut;
}

UWidget* UWidgetAnimPreviewBox::ResolveAnimTarget(FName TargetWidgetName) const
{
	// None means "this box", and the animation library already falls back to the widget it was given when
	// an entry has no target - so null is the right answer, not an error.
	if (TargetWidgetName.IsNone())
	{
		return nullptr;
	}

	// Rooted at the owning User Widget rather than at this box, because the interesting case is previewing
	// an animation against real content that sits beside the box, not inside it.
	if (UUserWidget* Owner = GetTypedOuter<UUserWidget>())
	{
		if (UWidget* Found = UWidgetAnimLibrary::FindWidgetByPathInTree(Owner, TargetWidgetName))
		{
			return Found;
		}
	}

	return UWidgetAnimLibrary::FindWidgetByPath(const_cast<UWidgetAnimPreviewBox*>(this), TargetWidgetName);
}

TArray<FString> UWidgetAnimPreviewBox::GetAnimTargetWidgetOptions() const
{
	TArray<FString> Options;

	if (UUserWidget* Owner = GetTypedOuter<UUserWidget>())
	{
		UWidgetAnimLibrary::GatherTargetableWidgetPathsInTree(Owner, Options);
	}

	// First, so there is a way back to "animate the box itself" once something has been picked.
	Options.Insert(TEXT("None"), 0);
	return Options;
}

void UWidgetAnimPreviewBox::RebuildEffectiveAnimations()
{
	EffectiveInEntries.Reset();
	EffectiveOutEntries.Reset();

	UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(AnimIn, EffectiveInEntries, EffectiveOutEntries);
	UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(AnimOut, EffectiveOutEntries, EffectiveInEntries);

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	FlattenedIn.Reset();
	FlattenedOut.Reset();
	UWidgetAnimEntryLibrary::FlattenEntries(EffectiveInEntries, Resolver, FlattenedIn);
	UWidgetAnimEntryLibrary::FlattenEntries(EffectiveOutEntries, Resolver, FlattenedOut);
}

bool UWidgetAnimPreviewBox::ShouldPreview() const
{
	if (Preview.Mode == EWidgetAnimPreviewMode::Off)
	{
		return false;
	}

	return IsDesignTime() ? bPreviewInDesigner : bPlayAtRuntime;
}

void UWidgetAnimPreviewBox::RecaptureBaselines()
{
	// Restore before re-capturing, or the "authored" pose we record is really the last frame the animation
	// happened to leave behind, and every rebuild drifts a little further from what the Details panel says.
	UWidgetAnimLibrary::RestoreWidgetAnimBaselines(PreviewBaselines);
	PreviewBaselines.ResetBaselines();
	UWidgetAnimLibrary::CaptureWidgetAnimBaselines(this, FlattenedIn, FlattenedOut, PreviewBaselines);
}

void UWidgetAnimPreviewBox::RestartPreview()
{
	UWidgetAnimLibrary::RestartWidgetAnimPreview(PreviewState);

	// Cleared as well, so the next tick treats this as a fresh start rather than a continuation.
	PreviewState.LastMode = EWidgetAnimPreviewMode::Off;

	RebuildEffectiveAnimations();
	RecaptureBaselines();
	StartPreviewTimer();
}

void UWidgetAnimPreviewBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();

#if WITH_EDITOR
	if (IsDesignTime())
	{
		// So a mid-drag edit on the template can reach this preview. See WidgetAnimDesignerSync.
		WidgetAnimDesignerSync::RegisterPreview(this);
	}
#endif

	RebuildEffectiveAnimations();
	RecaptureBaselines();

	if (ShouldPreview())
	{
		StartPreviewTimer();
	}
	else
	{
		// Covers switching Preview Mode to Off, or clearing Preview In Designer: the timer would stop on
		// its own next tick, but the widget has to be put back now or it stays frozen mid-animation.
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(PreviewBaselines);
		UWidgetAnimLibrary::RestartWidgetAnimPreview(PreviewState);
		PreviewState.LastMode = Preview.Mode;
	}
}

void UWidgetAnimPreviewBox::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	// SynchronizeProperties can run before the Slate widget exists, and an active timer needs one to
	// register against. This is the pass where it definitely does.
	if (ShouldPreview())
	{
		StartPreviewTimer();
	}
}

void UWidgetAnimPreviewBox::ReleaseSlateResources(bool bReleaseChildren)
{
	bIsPreviewTimerActive = false;
	Super::ReleaseSlateResources(bReleaseChildren);
}

void UWidgetAnimPreviewBox::BeginDestroy()
{
#if WITH_EDITOR
	WidgetAnimDesignerSync::UnregisterPreview(this);
#endif
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UWidgetAnimPreviewBox::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// A committed edit rebuilds the preview on its own and this would be wasted work. An interactive one -
	// a slider still being dragged - never reaches the preview at all, which is what made Scrub useless:
	// nothing moved until the mouse came up, so it read as a broken control rather than a slow one.
	if (!WidgetAnimDesignerSync::IsInteractiveChange(PropertyChangedEvent))
	{
		return;
	}

	const FWidgetAnimPreviewSettings SettingsToPush = Preview;

	WidgetAnimDesignerSync::ForEachPreviewOf(this, [&SettingsToPush](UObject* PreviewObject)
	{
		if (UWidgetAnimPreviewBox* Box = Cast<UWidgetAnimPreviewBox>(PreviewObject))
		{
			Box->Preview = SettingsToPush;
			Box->StartPreviewTimer();

			// Posed immediately rather than waiting for the next timer tick: the held modes stop their own
			// timer as soon as they've drawn a frame, so there may not be a next tick to wait for. This is
			// the call that actually makes dragging the Scrub slider move something.
			if (Box->ShouldPreview())
			{
				UWidgetAnimLibrary::TickWidgetAnimPreview(
					Box,
					Box->FlattenedIn,
					Box->FlattenedOut,
					Box->PreviewState,
					Box->Preview,
					Box->PreviewBaselines,
					0.f);
			}
		}
	});
}

const FText UWidgetAnimPreviewBox::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "Animation");
}
#endif

void UWidgetAnimPreviewBox::StartPreviewTimer()
{
	if (bIsPreviewTimerActive)
	{
		return;
	}

	TSharedPtr<SWidget> SlateWidget = GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return;
	}

	// An active timer rather than a tick: it keeps the widget being ticked AND forces the repaint that
	// makes the animation visible, which is the whole reason this works in the designer viewport at all.
	bIsPreviewTimerActive = true;
	SlateWidget->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateUObject(this, &UWidgetAnimPreviewBox::TickPreview));
}

EActiveTimerReturnType UWidgetAnimPreviewBox::TickPreview(double InCurrentTime, float InDeltaTime)
{
	if (!GetCachedWidget().IsValid())
	{
		bIsPreviewTimerActive = false;
		return EActiveTimerReturnType::Stop;
	}

	// Two independent things can want frames here: the designer preview harness, and any track started by
	// Play Widget Animation. They never overlap in practice - the harness is a design-time authoring aid
	// and the node is a runtime call - so each is ticked on its own terms and the timer keeps running
	// while either still has something to do.
	bool bNeedsMoreFrames = false;

	if (ShouldPreview())
	{
		bNeedsMoreFrames |= UWidgetAnimLibrary::TickWidgetAnimPreview(this, FlattenedIn, FlattenedOut, PreviewState, Preview, PreviewBaselines, InDeltaTime);
	}
	else if (PreviewState.LastMode != EWidgetAnimPreviewMode::Off)
	{
		// Restored here rather than by ticking the harness with Mode Off: Preview In Designer / Play At
		// Runtime can switch the preview off while Mode is still set to something, and the harness would
		// have no idea anything had changed.
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(PreviewBaselines);
		UWidgetAnimLibrary::RestartWidgetAnimPreview(PreviewState);
		PreviewState.LastMode = EWidgetAnimPreviewMode::Off;
	}

	if (AnimPlayer.Tracks.Num() > 0)
	{
		TArray<FName> JustFinished;
		bNeedsMoreFrames |= UWidgetAnimLibrary::TickWidgetAnimPlayer(this, AnimPlayer, PreviewBaselines, InDeltaTime, JustFinished);
	}

	if (!bNeedsMoreFrames)
	{
		// A held pose (Hold Start / Hold End / Scrub, or a Play Once that has finished) needs no further
		// frames. Editing any property re-synchronizes the widget, which starts the timer again.
		bIsPreviewTimerActive = false;
		return EActiveTimerReturnType::Stop;
	}

	return EActiveTimerReturnType::Continue;
}

// =====================================================================================================
// Play by ID
// =====================================================================================================

void UWidgetAnimPreviewBox::PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying)
{
	UWidgetAnimLibrary::CaptureWidgetAnimBaselines(this, FlattenedIn, FlattenedOut, PreviewBaselines);

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	UWidgetAnimEntryLibrary::StartEntriesById(
		AnimPlayer,
		bReverse ? EffectiveOutEntries : EffectiveInEntries,
		Resolver,
		AnimIds,
		bReverse,
		PlaybackSpeed,
		bRestartIfPlaying,
		bHoldFinalPose);

	StartPreviewTimer();
}

void UWidgetAnimPreviewBox::StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart)
{
	UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, AnimIds, /*bRemoveTracks=*/bResetToStart);

	if (bResetToStart)
	{
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(PreviewBaselines);
	}
}

bool UWidgetAnimPreviewBox::AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const
{
	return UWidgetAnimLibrary::AreWidgetAnimTracksPlaying(AnimPlayer, AnimIds);
}

#undef LOCTEXT_NAMESPACE