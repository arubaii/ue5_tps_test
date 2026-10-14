// Copyright AR GameSystems 2026. All rights reserved.

#include "MediaBox.h"

#include "MediaBoxClip.h"
#include "MediaBoxLog.h"
#include "MediaBoxPool.h"
#include "MediaBoxSettings.h"
#include "SMediaBox.h"
#include "SMediaBoxFullscreen.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Engine/Texture.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "NameTBD/Diegetic/DiegeticScreenComponent.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachineLibrary.h"
#include "Styling/CoreStyle.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Editor/WidgetCompilerLog.h"
#endif

#define LOCTEXT_NAMESPACE "MediaBox"

namespace MediaBoxPrivate
{
	/** Frames it may go undrawn before it counts as hidden - one frame of slack for a skipped paint. */
	constexpr uint64 HiddenAfterFrames = 2;

	/** How often the Diegetic Screen it is on is looked up again, and its Warm Group place checked. */
	constexpr double LookupInterval = 0.5;

	/** Progress changes smaller than this aren't passed to the state machine. */
	constexpr float ProgressStep = 0.005f;

	/** Keep Warm While Shown lets go of its clip once hidden this long (not for a frame or two of not being drawn). */
	constexpr float UnpinAfterHidden = 1.f;
}

UMediaBox::UMediaBox()
{
	// A preview is decoration: hover and clicks go through it to the button it is in.
	SetVisibilityInternal(ESlateVisibility::HitTestInvisible);

	if (!IsRunningDedicatedServer())
	{
		SubtitleFont = FCoreStyle::GetDefaultFontStyle("Regular", 14);
	}
}

// ---------------------------------------------------------------------------------------------
// UWidget
// ---------------------------------------------------------------------------------------------

TSharedRef<SWidget> UMediaBox::RebuildWidget()
{
	MyMediaBox = SNew(SMediaBox).OwnerBox(this);

	if (UMediaBoxPool* Pool = GetPool())
	{
		Pool->RegisterBox(this);
		RegisteredPool = Pool;
	}
	UpdateBrushes();
	UpdateGroupMembership();

	// Its place in the layout: what it shows can go full screen and come back to it.
	MyFullscreenSlot = SNew(SMediaBoxFullscreenSlot)
	[
		BuildBoxContent(MyMediaBox.ToSharedRef())
	];
	return MyFullscreenSlot.ToSharedRef();
}

void UMediaBox::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// A clip changed in the Details (or a Blueprint's construction): what played stops, the new poster shows.
	if (LeasedPlayer && LeasedPlayer->GetClip() != MediaClip)
	{
		ReleasePlayer(/*bRewind=*/true);
	}
	if (LeasedPlayer)
	{
		LeasedPlayer->SetRate(PlaybackRate);
		LeasedPlayer->SetVolume(GetEffectiveVolume());
	}
	UpdateBrushes();
	UpdateGroupMembership();
	UpdateStatus();

	if (MyMediaBox.IsValid())
	{
		MyMediaBox->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

void UMediaBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	RemoveFullscreenLayerNow();
	MyFullscreenSlot.Reset();
	MyMediaBox.Reset();

	// The warm pin always (the pool counts pins; a box gone by garbage collection would hold one for good).
	ReleaseWarmPin();

	// Not while being garbage collected: the pool takes back a lease whose box has gone by itself.
	if (!IsUnreachable())
	{
		ReleasePlayer(/*bRewind=*/true);
		if (UMediaBoxPool* Pool = RegisteredPool.Get())
		{
			if (!JoinedGroup.IsNone())
			{
				Pool->RemoveFromGroup(JoinedGroup, JoinedTarget.Get());
			}
			Pool->UnregisterBox(this);
		}
	}
	JoinedGroup = NAME_None;
	JoinedTarget.Reset();
	RegisteredPool.Reset();
	LeasedPlayer = nullptr;
	VideoAlpha = 0.f;
	bScrubbing = false;
	bResumeAfterScrub = false;
}

#if WITH_EDITOR
const FText UMediaBox::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}

void UMediaBox::ValidateCompiledDefaults(IWidgetCompilerLog& CompileLog) const
{
	Super::ValidateCompiledDefaults(CompileLog);

	const FText WidgetName = FText::FromString(GetName());
	if (!MediaClip)
	{
		if (PlayWhen != EMediaBoxTrigger::Manual)
		{
			CompileLog.Note(FText::Format(LOCTEXT("NoClipNote", "{0}: no Clip - it plays nothing until Set Clip gives it one."), WidgetName));
		}
		return;
	}
	if (!MediaClip->MediaSource)
	{
		CompileLog.Warning(FText::Format(LOCTEXT("NoSourceWarning", "{0}: its Clip {1} has no Media Source."), WidgetName, FText::FromString(MediaClip->GetName())));
	}
	if (!MediaClip->Poster)
	{
		CompileLog.Note(FText::Format(LOCTEXT("NoPosterNote", "{0}: its Clip {1} has no Poster - the box is empty until the first frame arrives. Generate Poster on the clip makes one."), WidgetName, FText::FromString(MediaClip->GetName())));
	}
	if (PlayWhen != EMediaBoxTrigger::Manual && PlayWhen != EMediaBoxTrigger::Visible && WarmGroup.IsNone())
	{
		CompileLog.Note(FText::Format(LOCTEXT("NoGroupNote", "{0}: plays on hover or focus but is in no Warm Group - give the previews of this screen one Warm Group name so the next hover starts at once."), WidgetName));
	}
}
#endif

TArray<FString> UMediaBox::GetHoverTargetOptions() const
{
	TArray<FString> Options;
	Options.Add(TEXT("None"));
	if (const UWidgetTree* Tree = GetTypedOuter<UWidgetTree>())
	{
		Tree->ForEachWidget([this, &Options](UWidget* Widget)
		{
			if (Widget && Widget != this)
			{
				Options.Add(Widget->GetName());
			}
		});
	}
	return Options;
}

// ---------------------------------------------------------------------------------------------
// Blueprint
// ---------------------------------------------------------------------------------------------

void UMediaBox::Play()
{
	bPlayRequested = true;
	bHeldPaused = false;
	if (CanPlayHere() && MediaClip)
	{
		if (!WarmGroup.IsNone())
		{
			if (UMediaBoxPool* Pool = GetPool())
			{
				Pool->SetGroupFocusByClip(WarmGroup, MediaClip);
			}
		}
		StartPlayback(/*bRestartIfEnded=*/true);
	}
	UpdateStatus();
}

void UMediaBox::Pause()
{
	bPlayRequested = false;
	if (bTriggerActive)
	{
		bHeldPaused = true;
	}
	if (LeasedPlayer)
	{
		LeasedPlayer->Pause(UMediaBoxSettings::Get().AudioFadeOut);
	}
	UpdateStatus();
}

void UMediaBox::Stop()
{
	if (bTriggerActive)
	{
		bHeldPaused = true;
	}
	BeginStop(/*bFade=*/true);
	UpdateStatus();
}

void UMediaBox::TogglePlayPause()
{
	if (IsPlaying() || (LeasedPlayer && !bStopping && Status == EMediaBoxStatus::Loading))
	{
		Pause();
	}
	else
	{
		Play();
	}
}

void UMediaBox::Seek(float Seconds)
{
	if (LeasedPlayer && !bStopping)
	{
		LeasedPlayer->Seek(Seconds);
	}
	else
	{
		PendingSeek = FMath::Max(0.f, Seconds);
	}
}

void UMediaBox::BeginScrub()
{
	if (bScrubbing)
	{
		return;
	}
	bScrubbing = true;
	bResumeAfterScrub = IsPlaying() || (Status == EMediaBoxStatus::Loading && (bPlayRequested || (bTriggerActive && !bHeldPaused)));

	// Paused while dragged: the frames follow the bar. Played on (or not) by End Scrub.
	bPlayRequested = false;
	if (bTriggerActive)
	{
		bHeldPaused = true;
	}
	if (LeasedPlayer && !bStopping)
	{
		LeasedPlayer->Pause(0.f);
	}
	UpdateStatus();
}

void UMediaBox::ScrubTo(float Seconds)
{
	if (!bScrubbing)
	{
		Seek(Seconds);
		return;
	}
	// A stopped box (its poster showing) opens its video, so the frame under the bar shows.
	if ((!LeasedPlayer || bStopping) && CanPlayHere() && MediaClip)
	{
		// Fading back to its poster: called back, and held still for the scrub.
		const bool bWasStopping = bStopping && LeasedPlayer;
		bStopping = false;
		if (EnsureLease() && bWasStopping)
		{
			LeasedPlayer->Pause(0.f);
		}
	}
	if (LeasedPlayer && LeasedPlayer->IsOpened())
	{
		LeasedPlayer->Scrub(Seconds);
	}
	else
	{
		PendingSeek = FMath::Max(0.f, Seconds);
	}
}

void UMediaBox::EndScrub(bool bPlayOn)
{
	if (!bScrubbing)
	{
		return;
	}
	bScrubbing = false;

	// Lands exactly where the bar was let go (a scrub may have stopped on the frame before it).
	if (LeasedPlayer && LeasedPlayer->IsOpened())
	{
		LeasedPlayer->Seek(LeasedPlayer->GetTime());
	}
	const bool bPlayAgain = bPlayOn && bResumeAfterScrub;
	bResumeAfterScrub = false;
	if (bPlayAgain)
	{
		Play();
	}
	UpdateStatus();
}

void UMediaBox::SetSubtitleLanguage(const FString& NewLanguage)
{
	SubtitleLanguage = NewLanguage;
	UpdateSubtitle();
}

FString UMediaBox::GetSubtitleLanguage() const
{
	return SubtitleLanguage.IsEmpty() ? MediaBoxRuntime::GetSubtitleLanguage() : SubtitleLanguage;
}

void UMediaBox::GetSubtitleLanguages(TArray<FString>& Languages, TArray<FText>& LanguageNames) const
{
	Languages.Reset();
	LanguageNames.Reset();
	if (MediaClip)
	{
		MediaClip->GetSubtitleLanguages(Languages, LanguageNames);
	}
}

// ---------------------------------------------------------------------------------------------
// Fullscreen
// ---------------------------------------------------------------------------------------------

void UMediaBox::ToggleFullscreen()
{
	SetFullscreen(!bFullscreen);
}

void UMediaBox::SetFullscreen(bool bNewFullscreen)
{
	if (bNewFullscreen == bFullscreen || IsDesignTime())
	{
		return;
	}
	const UMediaBoxSettings& Settings = UMediaBoxSettings::Get();

	if (!bNewFullscreen)
	{
		if (!FullscreenLayer.IsValid())
		{
			return;
		}
		bFullscreen = false;
		const FSlateRect BackTo = MyFullscreenSlot.IsValid() ? MyFullscreenSlot->GetDesktopRect() : FSlateRect();
		TWeakObjectPtr<UMediaBox> WeakBox(this);
		FullscreenLayer->BeginExit(BackTo, /*bInstant=*/false, [WeakBox]()
		{
			if (UMediaBox* Box = WeakBox.Get())
			{
				Box->FinishExitFullscreen();
			}
		});
		OnFullscreenChanged.Broadcast(this, false);
		return;
	}

	// Going back out still: comes back in from where it is.
	if (FullscreenLayer.IsValid())
	{
		bFullscreen = true;
		FullscreenLayer->BeginEnter();
		OnFullscreenChanged.Broadcast(this, true);
		return;
	}

	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!Viewport || !MyFullscreenSlot.IsValid())
	{
		return;
	}
	const FSlateRect From = MyFullscreenSlot->GetDesktopRect();
	const TSharedPtr<SWidget> Content = MyFullscreenSlot->LendContent();
	if (!Content.IsValid())
	{
		return;
	}

	FullscreenLayer = SNew(SMediaBoxFullscreenLayer)
		.OwnerBox(this)
		.From(From)
		.Transition(Settings.FullscreenTransition)
		.TransitionTime(Settings.FullscreenTransitionTime)
		.Backdrop(Settings.FullscreenBackdrop)
		.bEscapeLeaves(Settings.bEscapeLeavesFullscreen)
		[
			Content.ToSharedRef()
		];
	Viewport->AddViewportWidgetContent(FullscreenLayer.ToSharedRef(), Settings.FullscreenZOrder);
	FullscreenViewport = Viewport;

	// Escape reaches it.
	if (Settings.bEscapeLeavesFullscreen && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(FullscreenLayer, EFocusCause::SetDirectly);
	}

	bFullscreen = true;
	OnFullscreenChanged.Broadcast(this, true);
}

void UMediaBox::FinishExitFullscreen()
{
	// Asked to go full screen again meanwhile: stays.
	if (bFullscreen || !FullscreenLayer.IsValid())
	{
		return;
	}
	RemoveFullscreenLayerNow();
}

void UMediaBox::RemoveFullscreenLayerNow()
{
	if (!FullscreenLayer.IsValid())
	{
		return;
	}
	const TSharedPtr<SWidget> Content = FullscreenLayer->TakeContent();
	if (UGameViewportClient* Viewport = FullscreenViewport.Get())
	{
		Viewport->RemoveViewportWidgetContent(FullscreenLayer.ToSharedRef());
	}
	FullscreenLayer.Reset();
	FullscreenViewport.Reset();
	if (Content.IsValid() && MyFullscreenSlot.IsValid())
	{
		MyFullscreenSlot->ReturnContent(Content.ToSharedRef());
	}
	if (bFullscreen)
	{
		bFullscreen = false;
		if (!IsUnreachable())
		{
			OnFullscreenChanged.Broadcast(this, false);
		}
	}
}

void UMediaBox::SetClip(UMediaBoxClip* NewClip, bool bPlayNow)
{
	if (NewClip != MediaClip)
	{
		// What played stops at once - no fade: the new clip's poster is shown on this frame.
		ReleaseWarmPin();
		ReleasePlayer(/*bRewind=*/true);
		MediaClip = NewClip;
		PendingSeek = -1.f;
		bPlayRequested = false;
		UpdateBrushes();
		UpdateGroupMembership();
	}
	if (bPlayNow)
	{
		Play();
	}
	UpdateStatus();
	if (MyMediaBox.IsValid())
	{
		MyMediaBox->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UMediaBox::SetHoverTarget(UWidget* NewHoverTarget)
{
	HoverTargetOverride = NewHoverTarget;
	UpdateGroupMembership();
}

void UMediaBox::SetPlaybackRate(float NewRate)
{
	PlaybackRate = FMath::Max(0.05f, NewRate);
	if (LeasedPlayer)
	{
		LeasedPlayer->SetRate(PlaybackRate);
	}
}

void UMediaBox::SetVolume(float NewVolume)
{
	AudioVolume = FMath::Max(0.f, NewVolume);
	if (LeasedPlayer)
	{
		LeasedPlayer->SetVolume(GetEffectiveVolume());
	}
}

void UMediaBox::SetMuted(bool bNewMuted)
{
	bMuted = bNewMuted;
	if (LeasedPlayer)
	{
		LeasedPlayer->SetVolume(GetEffectiveVolume());
	}
}

bool UMediaBox::IsPlaying() const
{
	return Status == EMediaBoxStatus::Playing;
}

float UMediaBox::GetTime() const
{
	if (LeasedPlayer && !bStopping)
	{
		return LeasedPlayer->GetTime();
	}
	return PendingSeek > 0.f ? PendingSeek : 0.f;
}

float UMediaBox::GetDuration() const
{
	const float FromPlayer = LeasedPlayer ? LeasedPlayer->GetDuration() : 0.f;
	return FromPlayer > 0.f ? FromPlayer : (MediaClip ? MediaClip->KnownDuration : 0.f);
}

float UMediaBox::GetProgress() const
{
	const float Duration = GetDuration();
	return Duration > 0.f ? FMath::Clamp(GetTime() / Duration, 0.f, 1.f) : 0.f;
}

UMediaPlayer* UMediaBox::GetMediaPlayer() const
{
	return LeasedPlayer ? LeasedPlayer->GetMediaPlayer() : nullptr;
}

UMediaTexture* UMediaBox::GetMediaTexture() const
{
	return LeasedPlayer ? LeasedPlayer->GetMediaTexture() : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Playing
// ---------------------------------------------------------------------------------------------

bool UMediaBox::CanPlayHere() const
{
	return !IsDesignTime() || bPreviewInDesigner;
}

UMediaBoxPool* UMediaBox::GetPool() const
{
	if (UMediaBoxPool* Pool = RegisteredPool.Get())
	{
		return Pool;
	}
	return UMediaBoxPool::Get(this);
}

float UMediaBox::GetFadeIn() const
{
	return VideoFadeIn >= 0.f ? VideoFadeIn : UMediaBoxSettings::Get().VideoFadeIn;
}

float UMediaBox::GetFadeOut() const
{
	return VideoFadeOut >= 0.f ? VideoFadeOut : UMediaBoxSettings::Get().VideoFadeOut;
}

float UMediaBox::GetEffectiveVolume() const
{
	return bMuted ? 0.f : AudioVolume;
}

bool UMediaBox::EnsureLease()
{
	UMediaBoxPool* Pool = GetPool();
	if (!Pool || !MediaClip)
	{
		return false;
	}
	if (!LeasedPlayer)
	{
		LeasedPlayer = Pool->Acquire(MediaClip, this);
		if (!LeasedPlayer)
		{
			return false;
		}
		bSentOpened = false;
		bSentFirstFrame = false;
		bSentError = false;
		SeenLoops = 0;
		SeenEnds = 0;
		VideoBrush.SetResourceObject(LeasedPlayer->GetMediaTexture());
		LeasedPlayer->SetRate(PlaybackRate);
		LeasedPlayer->SetVolume(GetEffectiveVolume());
	}
	return true;
}

void UMediaBox::StartPlayback(bool bRestartIfEnded)
{
	if (!GetPool() || !MediaClip || bScrubbing)
	{
		return;
	}

	const bool bWasStopping = bStopping;
	bStopping = false;

	if (!EnsureLease())
	{
		return;
	}

	if (PendingSeek >= 0.f)
	{
		LeasedPlayer->Seek(PendingSeek);
		PendingSeek = -1.f;
	}

	const EMediaBoxPlayerState PlayerState = LeasedPlayer->GetState();
	const bool bNeedsPlay = PlayerState == EMediaBoxPlayerState::Ready || PlayerState == EMediaBoxPlayerState::Paused
		|| PlayerState == EMediaBoxPlayerState::Opening || PlayerState == EMediaBoxPlayerState::Priming || PlayerState == EMediaBoxPlayerState::Settling
		|| (PlayerState == EMediaBoxPlayerState::Ended && bRestartIfEnded)
		|| (PlayerState == EMediaBoxPlayerState::Playing && bWasStopping);
	if (!bNeedsPlay)
	{
		return;
	}

	// Audio in a game; in the designer only with Audio In Designer, through the editor's world.
	UWorld* AudioWorld = GetWorld();
	bool bWithAudio = MediaClip->GetResolvedAudio() != EMediaBoxAudio::Silent;
	if (IsDesignTime())
	{
		bWithAudio = bWithAudio && UMediaBoxSettings::Get().bAudioInDesigner;
#if WITH_EDITOR
		if (bWithAudio && !AudioWorld && GEditor)
		{
			AudioWorld = GEditor->GetEditorWorldContext().World();
		}
#endif
	}
	// World Audio: heard from the Diegetic Screen it is on (looked up now if it hasn't been yet). Elsewhere: 2D.
	USceneComponent* ScreenForAudio = nullptr;
	if (AudioSpace == EMediaBoxAudioSpace::World && !IsDesignTime())
	{
		if (!DiegeticScreen.IsValid() && UDiegeticScreenComponent::AnyScreensExist())
		{
			DiegeticScreen = UDiegeticScreenComponent::FindScreenForWidget(this);
		}
		ScreenForAudio = DiegeticScreen.Get();
	}
	LeasedPlayer->Play(AudioWorld, bWithAudio && AudioWorld != nullptr, ScreenForAudio, ScreenForAudio ? &WorldAudio : nullptr, IsDesignTime() ? nullptr : GetOwningPlayer());
}

void UMediaBox::BeginStop(bool bFade)
{
	bPlayRequested = false;
	if (!LeasedPlayer)
	{
		VideoAlpha = 0.f;
		return;
	}

	if (!bFade || GetFadeOut() <= 0.f || VideoAlpha <= 0.f)
	{
		ReleasePlayer(/*bRewind=*/true);
		return;
	}

	// The video plays on under the fade back to the poster; its audio fades with it.
	bStopping = true;
	LeasedPlayer->FadeOutAudio(UMediaBoxSettings::Get().AudioFadeOut);
}

void UMediaBox::ReleasePlayer(bool bRewind)
{
	if (LeasedPlayer)
	{
		if (UMediaBoxPool* Pool = GetPool())
		{
			Pool->Release(LeasedPlayer, bRewind, UMediaBoxSettings::Get().AudioFadeOut);
		}
		LeasedPlayer = nullptr;
	}
	VideoBrush.SetResourceObject(nullptr);
	VideoAlpha = 0.f;
	bStopping = false;
	bPausedByHidden = false;

	if (SubtitleIndex != INDEX_NONE || !CurrentSubtitle.IsEmpty())
	{
		SubtitleIndex = INDEX_NONE;
		CurrentSubtitle = FText::GetEmpty();
		OnSubtitleChanged.Broadcast(this, CurrentSubtitle);
	}
	if (MyMediaBox.IsValid())
	{
		MyMediaBox->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UMediaBox::UpdateBrushes()
{
	UTexture* PosterTexture = MediaClip ? MediaClip->Poster.Get() : nullptr;
	PosterBrush.SetResourceObject(PosterTexture);
	if (PosterTexture)
	{
		PosterBrush.ImageSize = FVector2D(PosterTexture->GetSurfaceWidth(), PosterTexture->GetSurfaceHeight());
	}
	VideoBrush.SetResourceObject(LeasedPlayer ? LeasedPlayer->GetMediaTexture() : nullptr);
}

UWidget* UMediaBox::ResolveHoverTarget() const
{
	if (UWidget* Override = HoverTargetOverride.Get())
	{
		return Override;
	}
	if (UWidget* Cached = CachedHoverTarget.Get())
	{
		if (CachedHoverTargetName == HoverTargetName)
		{
			return Cached;
		}
	}

	UWidget* Found = nullptr;
	if (!HoverTargetName.IsNone())
	{
		// By name: in the Widget Blueprint it is in, then in each around it.
		for (UUserWidget* UserWidget = GetTypedOuter<UUserWidget>(); UserWidget && !Found; UserWidget = UserWidget->GetTypedOuter<UUserWidget>())
		{
			Found = UserWidget->GetWidgetFromName(HoverTargetName);
		}
	}
	if (!Found)
	{
		// The nearest button around it - through the User Widgets it is in, out to the screen.
		const UWidget* From = this;
		while (From && !Found)
		{
			for (UPanelWidget* Parent = From->GetParent(); Parent; Parent = Parent->GetParent())
			{
				if (Parent->IsA<UButton>())
				{
					Found = Parent;
					break;
				}
			}
			From = From->GetTypedOuter<UUserWidget>();
		}
	}

	CachedHoverTarget = Found ? Found : const_cast<UMediaBox*>(this);
	CachedHoverTargetName = HoverTargetName;
	return CachedHoverTarget.Get();
}

bool UMediaBox::IsHiddenNow()
{
	if (!MyMediaBox.IsValid())
	{
		return true;
	}

	// Full screen: on the viewport, whatever screen it was on.
	if (FullscreenLayer.IsValid())
	{
		return GFrameCounter > LastPaintFrame + MediaBoxPrivate::HiddenAfterFrames;
	}

	// On a Diegetic Screen: hidden while the screen is (it may draw rarely; a paused video would never ask it to draw again).
	if (const UDiegeticScreenComponent* Screen = DiegeticScreen.Get())
	{
		return !Screen->IsScreenShown() || !Screen->IsAwake();
	}
	return GFrameCounter > LastPaintFrame + MediaBoxPrivate::HiddenAfterFrames;
}

bool UMediaBox::IsTriggerHeld()
{
	switch (PlayWhen)
	{
	case EMediaBoxTrigger::Visible:
		return !IsHiddenNow();
	case EMediaBoxTrigger::Hovered:
	case EMediaBoxTrigger::Focused:
	case EMediaBoxTrigger::HoveredOrFocused:
	{
		UWidget* Target = ResolveHoverTarget();
		if (!Target || IsHiddenNow())
		{
			return false;
		}

		bool bHovered = false;
		if (PlayWhen != EMediaBoxTrigger::Focused)
		{
			if (Target == this)
			{
				// Itself: it doesn't take hover (Hit Test Invisible), so the pointer over its area counts.
				if (FSlateApplication::IsInitialized() && MyMediaBox.IsValid())
				{
					bHovered = GetCachedGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos());
				}
			}
			else
			{
				bHovered = Target->IsHovered();
			}
		}
		const bool bFocused = PlayWhen != EMediaBoxTrigger::Hovered && (Target->HasAnyUserFocus() || Target->HasFocusedDescendants());
		return bHovered || bFocused;
	}
	default:
		return false;
	}
}

void UMediaBox::UpdateGroupMembership()
{
	UMediaBoxPool* Pool = RegisteredPool.Get();
	if (!Pool)
	{
		return;
	}

	// Manual boxes play what they are told: they say nothing about what will be hovered next.
	const bool bJoins = !WarmGroup.IsNone() && MediaClip && PlayWhen != EMediaBoxTrigger::Manual && CanPlayHere();
	UWidget* Target = bJoins ? ResolveHoverTarget() : nullptr;

	if (!JoinedGroup.IsNone() && (JoinedGroup != WarmGroup || JoinedTarget.Get() != Target || !bJoins))
	{
		Pool->RemoveFromGroup(JoinedGroup, JoinedTarget.Get());
		JoinedGroup = NAME_None;
		JoinedTarget.Reset();
	}
	if (bJoins && Target)
	{
		Pool->AddToGroup(WarmGroup, Target, MediaClip);
		JoinedGroup = WarmGroup;
		JoinedTarget = Target;
	}
}

void UMediaBox::TickMediaBox(float DeltaTime)
{
	using namespace MediaBoxPrivate;

	const double Now = FPlatformTime::Seconds();
	if (Now >= NextScreenLookup)
	{
		NextScreenLookup = Now + LookupInterval;
		DiegeticScreen = UDiegeticScreenComponent::AnyScreensExist() ? UDiegeticScreenComponent::FindScreenForWidget(this) : nullptr;
		UpdateGroupMembership();
	}

	UpdateWarmPin(DeltaTime);

	if (!CanPlayHere() || !MediaClip)
	{
		if (LeasedPlayer)
		{
			ReleasePlayer(/*bRewind=*/true);
		}
		UpdateStatus();
		return;
	}

	// The trigger, held for Hover Delay.
	const bool bHeld = IsTriggerHeld();
	TriggerHeldFor = bHeld ? TriggerHeldFor + DeltaTime : 0.f;
	const bool bNowActive = bHeld && TriggerHeldFor >= HoverDelay;

	if (bNowActive && !bTriggerActive)
	{
		bTriggerActive = true;
		bHeldPaused = false;
		if (!JoinedGroup.IsNone())
		{
			if (UMediaBoxPool* Pool = GetPool())
			{
				Pool->SetGroupFocus(JoinedGroup, JoinedTarget.Get());
			}
		}
		StartPlayback(/*bRestartIfEnded=*/true);
	}
	else if (!bNowActive && bTriggerActive)
	{
		bTriggerActive = false;
		bHeldPaused = false;
		if (!bPlayRequested)
		{
			switch (AfterTrigger)
			{
			case EMediaBoxAfterTrigger::StopAndRewind:
				BeginStop(/*bFade=*/true);
				break;
			case EMediaBoxAfterTrigger::Pause:
				if (LeasedPlayer)
				{
					LeasedPlayer->Pause(UMediaBoxSettings::Get().AudioFadeOut);
				}
				break;
			case EMediaBoxAfterTrigger::KeepPlaying:
				bPlayRequested = true;
				break;
			default:
				break;
			}
		}
	}

	// Playing, or paused while hidden.
	const bool bWantsPlay = (bPlayRequested || (bTriggerActive && !bHeldPaused)) && !bScrubbing;
	if (bWantsPlay && !bStopping)
	{
		if (bPauseWhenHidden && IsHiddenNow())
		{
			if (LeasedPlayer && !bPausedByHidden)
			{
				LeasedPlayer->Pause(0.f);
				bPausedByHidden = true;
			}
		}
		else if (bPausedByHidden && !bResumeWhenVisible)
		{
			// Shown again, Resume When Visible off: stays paused, as if Pause was pressed.
			bPausedByHidden = false;
			bPlayRequested = false;
			if (bTriggerActive)
			{
				bHeldPaused = true;
			}
		}
		else
		{
			bPausedByHidden = false;
			StartPlayback(/*bRestartIfEnded=*/false);
		}
	}

	if (LeasedPlayer)
	{
		// What the player has to say: opened, failed, looped, ended.
		if (!bSentOpened && LeasedPlayer->IsOpened())
		{
			bSentOpened = true;
			OnOpened.Broadcast(this);
		}
		if (!bSentError && LeasedPlayer->HasFailed())
		{
			bSentError = true;
			bPlayRequested = false;
			OnError.Broadcast(this, LeasedPlayer->GetError());
			FireMachineTrigger(MachineInputs.Error);
		}
		while (LeasedPlayer && SeenLoops < LeasedPlayer->GetLoopCount())
		{
			++SeenLoops;
			OnLooped.Broadcast(this);
			FireMachineTrigger(MachineInputs.Looped);
		}
		while (LeasedPlayer && SeenEnds < LeasedPlayer->GetEndCount())
		{
			++SeenEnds;
			bPlayRequested = false;
			OnEnded.Broadcast(this);
			FireMachineTrigger(MachineInputs.Ended);
		}
	}

	// The video over the poster: fading in once its first frame is in, back out as it stops.
	const float PreviousAlpha = VideoAlpha;
	if (LeasedPlayer)
	{
		const EMediaBoxPlayerState PlayerState = LeasedPlayer->GetState();
		const bool bShowsVideo = LeasedPlayer->IsFrameReady() && (PlayerState == EMediaBoxPlayerState::Playing
			|| PlayerState == EMediaBoxPlayerState::Paused || PlayerState == EMediaBoxPlayerState::Ended);

		if (bStopping)
		{
			VideoAlpha -= DeltaTime / FMath::Max(GetFadeOut(), KINDA_SMALL_NUMBER);
			if (VideoAlpha <= 0.f)
			{
				ReleasePlayer(/*bRewind=*/true);
			}
		}
		else if (bShowsVideo && VideoAlpha < 1.f)
		{
			const float FadeIn = GetFadeIn();
			VideoAlpha = FadeIn > 0.f ? FMath::Min(1.f, VideoAlpha + DeltaTime / FadeIn) : 1.f;
			if (!bSentFirstFrame)
			{
				bSentFirstFrame = true;
				OnFirstFrame.Broadcast(this);
				FireMachineTrigger(MachineInputs.FirstFrame);
			}
		}
	}

	UpdateSubtitle();
	UpdateStatus();
	UpdateMachineInputs(/*bForce=*/false);

	// Repainted while the video moves; a Diegetic Screen told it has a new frame to draw.
	const bool bMoving = (LeasedPlayer && (LeasedPlayer->IsPlaying() || VideoAlpha > 0.f)) || VideoAlpha != PreviousAlpha;
	if (bMoving)
	{
		if (MyMediaBox.IsValid())
		{
			MyMediaBox->Invalidate(EInvalidateWidgetReason::Paint);
		}
		if (UDiegeticScreenComponent* Screen = DiegeticScreen.Get())
		{
			Screen->MarkContentChanged();
		}
	}
}

void UMediaBox::UpdateSubtitle()
{
	// The cues of the language asked for (else the clip's own Subtitles).
	FString CuesLanguage;
	const TConstArrayView<FMediaBoxSubtitleCue> Cues = MediaClip ? MediaClip->GetSubtitleCues(GetSubtitleLanguage(), &CuesLanguage) : TConstArrayView<FMediaBoxSubtitleCue>();
	const bool bLanguageChanged = CuesLanguage != ShownSubtitleLanguage;
	ShownSubtitleLanguage = CuesLanguage;

	int32 NewIndex = INDEX_NONE;
	if (LeasedPlayer && !bStopping && Cues.Num() > 0)
	{
		NewIndex = MediaBoxSubtitles::FindCue(Cues, LeasedPlayer->GetTime());
	}
	if (NewIndex != SubtitleIndex || bLanguageChanged)
	{
		SubtitleIndex = NewIndex;
		CurrentSubtitle = Cues.IsValidIndex(NewIndex) ? Cues[NewIndex].Text : FText::GetEmpty();
		OnSubtitleChanged.Broadcast(this, CurrentSubtitle);
		if (MyMediaBox.IsValid())
		{
			MyMediaBox->Invalidate(EInvalidateWidgetReason::Paint);
		}
	}
}

void UMediaBox::UpdateWarmPin(float DeltaTime)
{
	// Hidden a while (not just a frame undrawn): let go.
	HiddenFor = IsHiddenNow() ? HiddenFor + DeltaTime : 0.f;
	UMediaBoxClip* Wanted = (bKeepWarmWhileShown && MediaClip && MediaClip->MediaSource && CanPlayHere()
		&& HiddenFor < MediaBoxPrivate::UnpinAfterHidden) ? MediaClip.Get() : nullptr;

	// Not before it has been drawn once (LastPaintFrame 0): a box never shown warms nothing.
	if (Wanted && LastPaintFrame == 0)
	{
		Wanted = nullptr;
	}
	if (Wanted == WarmPinnedClip)
	{
		return;
	}
	ReleaseWarmPin();
	if (UMediaBoxPool* Pool = Wanted ? GetPool() : nullptr)
	{
		Pool->PinClip(Wanted, /*bKeepWarm=*/true);
		WarmPinnedClip = Wanted;
		WarmPinPool = Pool;
	}
}

void UMediaBox::ReleaseWarmPin()
{
	if (WarmPinnedClip)
	{
		if (UMediaBoxPool* Pool = WarmPinPool.Get())
		{
			Pool->PinClip(WarmPinnedClip, /*bKeepWarm=*/false);
		}
	}
	WarmPinnedClip = nullptr;
	WarmPinPool.Reset();
}

void UMediaBox::UpdateStatus()
{
	EMediaBoxStatus NewStatus = EMediaBoxStatus::Stopped;
	if (!MediaClip)
	{
		NewStatus = EMediaBoxStatus::Empty;
	}
	else if (LeasedPlayer && !bStopping)
	{
		switch (LeasedPlayer->GetState())
		{
		case EMediaBoxPlayerState::Failed:
			NewStatus = EMediaBoxStatus::Error;
			break;
		case EMediaBoxPlayerState::Playing:
			NewStatus = VideoAlpha > 0.f || LeasedPlayer->IsFrameReady() ? EMediaBoxStatus::Playing : EMediaBoxStatus::Loading;
			break;
		case EMediaBoxPlayerState::Paused:
			NewStatus = EMediaBoxStatus::Paused;
			break;
		case EMediaBoxPlayerState::Ended:
			NewStatus = EMediaBoxStatus::Ended;
			break;
		default:
			NewStatus = (bPlayRequested || (bTriggerActive && !bHeldPaused)) ? EMediaBoxStatus::Loading : EMediaBoxStatus::Stopped;
			break;
		}
	}

	if (NewStatus != Status)
	{
		Status = NewStatus;
		OnStatusChanged.Broadcast(this, Status);
	}
}

void UMediaBox::UpdateMachineInputs(bool bForce)
{
	if (MachineInputs.IsEmpty() || IsDesignTime())
	{
		return;
	}

	const bool bPlayingNow = Status == EMediaBoxStatus::Playing;
	if (!MachineInputs.Playing.IsNone() && (bForce || bPlayingNow != bMachinePlaying))
	{
		bMachinePlaying = bPlayingNow;
		UWidgetAnimStateMachineLibrary::SetStateMachineBool(this, MachineInputs.StateMachine, MachineInputs.Playing, bPlayingNow);
	}

	const float ProgressNow = GetProgress();
	if (!MachineInputs.Progress.IsNone() && (bForce || FMath::Abs(ProgressNow - MachineProgress) >= MediaBoxPrivate::ProgressStep || (ProgressNow == 0.f && MachineProgress != 0.f)))
	{
		MachineProgress = ProgressNow;
		UWidgetAnimStateMachineLibrary::SetStateMachineNumber(this, MachineInputs.StateMachine, MachineInputs.Progress, ProgressNow);
	}
}

void UMediaBox::FireMachineTrigger(FName Input) const
{
	if (!Input.IsNone() && !IsDesignTime())
	{
		UWidgetAnimStateMachineLibrary::FireStateMachineTrigger(const_cast<UMediaBox*>(this), MachineInputs.StateMachine, Input);
	}
}

#undef LOCTEXT_NAMESPACE
