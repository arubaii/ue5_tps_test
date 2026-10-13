// Copyright AR GameSystems 2026. All rights reserved.

#include "MediaBoxPool.h"

#include "MediaBox.h"
#include "MediaBoxClip.h"
#include "MediaBoxLog.h"
#include "MediaBoxSettings.h"

#include "Components/AudioComponent.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Stats/Stats.h"
#include "UObject/StrongObjectPtr.h"

DECLARE_STATS_GROUP(TEXT("MediaBox"), STATGROUP_MediaBox, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Pool Tick"), STAT_MediaBoxPoolTick, STATGROUP_MediaBox);
DECLARE_DWORD_COUNTER_STAT(TEXT("Players"), STAT_MediaBoxPlayers, STATGROUP_MediaBox);
DECLARE_DWORD_COUNTER_STAT(TEXT("Warm Players"), STAT_MediaBoxWarmPlayers, STATGROUP_MediaBox);
DECLARE_DWORD_COUNTER_STAT(TEXT("Leased Players"), STAT_MediaBoxLeasedPlayers, STATGROUP_MediaBox);
DECLARE_DWORD_COUNTER_STAT(TEXT("Media Boxes"), STAT_MediaBoxBoxes, STATGROUP_MediaBox);

namespace MediaBoxPoolPrivate
{
	/** Frames a step (priming, settling) may take before it is given up as done - so a player that never reports can't hang. */
	constexpr int32 MaxPrimeFrames = 45;
	constexpr int32 MaxSettleFrames = 30;

	/** How far back from Loop End a range loop seeks back - about one frame early, so the frame after Loop End is never seen. */
	constexpr float LoopEndLead = 1.f / 120.f;

	/** How often a Separate Sound is checked against the video. */
	constexpr double SyncCheckInterval = 0.25;

	/** The editor's pool: for widgets in worlds without a game instance - the designer, editor worlds. */
	TStrongObjectPtr<UMediaBoxPool>& GetEditorPool()
	{
		static TStrongObjectPtr<UMediaBoxPool> EditorPool;
		return EditorPool;
	}

	FTimespan ToTimespan(float Seconds)
	{
		return FTimespan::FromSeconds(FMath::Max(0.0, static_cast<double>(Seconds)));
	}

	FString DescribeClip(const UMediaBoxClip* ForClip)
	{
		return ForClip ? ForClip->GetName() : TEXT("(none)");
	}
}

// =====================================================================================================
// UMediaBoxPlayer
// =====================================================================================================

void UMediaBoxPlayer::Initialize()
{
	MediaPlayer = NewObject<UMediaPlayer>(this, NAME_None, RF_Transient);
	MediaPlayer->PlayOnOpen = false;
	MediaPlayer->OnMediaOpened.AddDynamic(this, &UMediaBoxPlayer::HandleMediaOpened);
	MediaPlayer->OnMediaOpenFailed.AddDynamic(this, &UMediaBoxPlayer::HandleMediaOpenFailed);
	MediaPlayer->OnEndReached.AddDynamic(this, &UMediaBoxPlayer::HandleEndReached);
	MediaPlayer->OnSeekCompleted.AddDynamic(this, &UMediaBoxPlayer::HandleSeekCompleted);

	// Keeps its last frame while paused - a warm player's first frame stays in it until the hover. A new clip's frames are never
	// shown before its own first one is in (IsFrameReady), so what a previous clip left behind is never seen.
	MediaTexture = NewObject<UMediaTexture>(this, NAME_None, RF_Transient);
	MediaTexture->AutoClear = false;
	MediaTexture->ClearColor = FLinearColor::Transparent;
	MediaTexture->SetMediaPlayer(MediaPlayer);
	MediaTexture->UpdateResource();
}

bool UMediaBoxPlayer::Open(UMediaBoxClip* NewClip)
{
	Close();

	Clip = NewClip;
	if (!Clip || !Clip->MediaSource)
	{
		Fail(TEXT("the clip has no Media Source"));
		return false;
	}

	// Its sound is loaded and its first chunk cached now, with the video, so it starts on the frame the video does.
	if (Clip->Sound && Clip->GetResolvedAudio() == EMediaBoxAudio::SeparateSound)
	{
		UGameplayStatics::PrimeSound(Clip->Sound);
	}

	MediaPlayer->PlayOnOpen = false;
	MediaPlayer->SetDesiredPlayerName(Clip->GetPlayerName());

	// Opening may report back at once, inside OpenSource: the state says so first.
	State = EMediaBoxPlayerState::Opening;
	StepFrames = 0;
	MEDIABOX_LOG(TEXT("Media Box: opening %s"), *Clip->GetName());
	if (!MediaPlayer->OpenSource(Clip->MediaSource))
	{
		Fail(FString::Printf(TEXT("the player couldn't open %s (is its player plugin enabled for this platform?)"), *Clip->MediaSource->GetUrl()));
		return false;
	}
	return true;
}

void UMediaBoxPlayer::Close()
{
	StopAudio(0.f);
	if (MediaPlayer && State != EMediaBoxPlayerState::Free)
	{
		MediaPlayer->Close();
	}
	if (Clip)
	{
		MEDIABOX_LOG(TEXT("Media Box: closed %s"), *Clip->GetName());
	}

	State = EMediaBoxPlayerState::Free;
	Clip = nullptr;
	Error.Reset();
	bFrameReady = false;
	bPlayRequested = false;
	StepFrames = 0;
	LastTime = 0.f;
	LoopCount = 0;
	EndCount = 0;
	Rate = 1.f;
}

void UMediaBoxPlayer::Fail(const FString& Reason)
{
	State = EMediaBoxPlayerState::Failed;
	Error = Reason;
	bPlayRequested = false;
	StopAudio(0.f);
	UE_LOG(LogMediaBox, Warning, TEXT("Media Box: %s - %s"), *MediaBoxPoolPrivate::DescribeClip(Clip), *Reason);
}

void UMediaBoxPlayer::HandleMediaOpened(FString OpenedUrl)
{
	if (State != EMediaBoxPlayerState::Opening || !Clip)
	{
		return;
	}

	// The whole video loops in the player itself (seamless); a range loops by seeking back (see Tick).
	MediaPlayer->SetLooping(Clip->LoopsWholeVideo());

	if (Clip->StartTime > 0.f && MediaPlayer->SupportsSeeking())
	{
		MediaPlayer->Seek(MediaBoxPoolPrivate::ToTimespan(Clip->StartTime));
	}

	// Played silently (no audio started) until its first frame is in the texture; then paused back on its start - warm - or,
	// if a Media Box is already waiting for it, played on from there with its audio.
	PrimeFrom = Clip->StartTime;
	LastTime = PrimeFrom;
	MediaPlayer->Play();
	State = EMediaBoxPlayerState::Priming;
	StepFrames = 0;
}

void UMediaBoxPlayer::HandleMediaOpenFailed(FString FailedUrl)
{
	Fail(FString::Printf(TEXT("couldn't open %s"), *FailedUrl));
}

void UMediaBoxPlayer::HandleEndReached()
{
	if (State != EMediaBoxPlayerState::Playing || !Clip)
	{
		return;
	}

	// A whole-video loop is the player's own; its loops are counted as time going back (Tick).
	if (MediaPlayer->IsLooping())
	{
		return;
	}

	if (Clip->bLoop)
	{
		// A range whose end is the video's end: back to Loop Start.
		const float LoopFrom = Clip->LoopStart;
		MediaPlayer->Seek(MediaBoxPoolPrivate::ToTimespan(LoopFrom));
		MediaPlayer->Play();
		++LoopCount;
		LastTime = LoopFrom;
		if (bAudioPlaying && IsValid(SeparateSound) && WantsSeparateSound())
		{
			SeparateSound->Play(LoopFrom);
			AudioStartMediaTime = LoopFrom;
			AudioStartWallTime = FPlatformTime::Seconds();
		}
		return;
	}

	State = EMediaBoxPlayerState::Ended;
	++EndCount;
	StopAudio(0.05f);
}

void UMediaBoxPlayer::HandleSeekCompleted()
{
	if (State == EMediaBoxPlayerState::Settling)
	{
		State = EMediaBoxPlayerState::Ready;
		MEDIABOX_LOG(TEXT("Media Box: %s is warm"), *MediaBoxPoolPrivate::DescribeClip(Clip));
		if (bPlayRequested)
		{
			StartPlaying();
		}
	}
}

void UMediaBoxPlayer::Play(UWorld* InAudioWorld, bool bWithAudio)
{
	bAudioWanted = bWithAudio;
	if (bWithAudio)
	{
		EnsureAudio(InAudioWorld);
	}

	switch (State)
	{
	case EMediaBoxPlayerState::Ready:
	case EMediaBoxPlayerState::Paused:
	case EMediaBoxPlayerState::Ended:
		StartPlaying();
		break;
	case EMediaBoxPlayerState::Opening:
	case EMediaBoxPlayerState::Priming:
	case EMediaBoxPlayerState::Settling:
		bPlayRequested = true;
		break;
	case EMediaBoxPlayerState::Playing:
		// Already playing; a fade out in progress (a quick unhover and hover again) is called off.
		if (bAudioFadingOut)
		{
			bAudioFadingOut = false;
			AudioFade = 1.f;
			ApplyAudioVolume();
		}
		break;
	default:
		break;
	}
}

void UMediaBoxPlayer::StartPlaying()
{
	if (!Clip)
	{
		return;
	}
	if (State == EMediaBoxPlayerState::Ended)
	{
		MediaPlayer->Seek(MediaBoxPoolPrivate::ToTimespan(Clip->StartTime));
		LastTime = Clip->StartTime;
	}

	MediaPlayer->Play();
	if (!FMath::IsNearlyEqual(Rate, 1.f))
	{
		MediaPlayer->SetRate(Rate);
	}
	State = EMediaBoxPlayerState::Playing;
	bPlayRequested = false;
	StartAudio();
}

void UMediaBoxPlayer::Pause(float FadeOut)
{
	bPlayRequested = false;
	if (State == EMediaBoxPlayerState::Playing)
	{
		MediaPlayer->Pause();
		State = EMediaBoxPlayerState::Paused;
	}
	StopAudio(FadeOut);
}

void UMediaBoxPlayer::Rewind(float FadeOut)
{
	bPlayRequested = false;
	StopAudio(FadeOut);

	switch (State)
	{
	case EMediaBoxPlayerState::Playing:
	case EMediaBoxPlayerState::Paused:
	case EMediaBoxPlayerState::Ended:
	case EMediaBoxPlayerState::Ready:
		if (MediaPlayer->SupportsSeeking() && Clip)
		{
			MediaPlayer->Pause();
			MediaPlayer->Seek(MediaBoxPoolPrivate::ToTimespan(Clip->StartTime));
			LastTime = Clip->StartTime;
			State = EMediaBoxPlayerState::Settling;
			StepFrames = 0;
		}
		else
		{
			// Can't seek (a live stream): opened again from the start, warm again once it has its frame.
			UMediaBoxClip* ReopenClip = Clip;
			Open(ReopenClip);
		}
		break;
	default:
		// Opening or priming: it carries on to Ready by itself, now that nobody waits for it to play.
		break;
	}
}

void UMediaBoxPlayer::Seek(float Seconds)
{
	if (!IsOpened())
	{
		return;
	}
	MediaPlayer->Seek(MediaBoxPoolPrivate::ToTimespan(Seconds));
	LastTime = Seconds;
	if (State == EMediaBoxPlayerState::Ended)
	{
		State = EMediaBoxPlayerState::Paused;
	}
	if (State == EMediaBoxPlayerState::Playing && bAudioPlaying && IsValid(SeparateSound) && WantsSeparateSound())
	{
		SeparateSound->Play(Seconds);
		AudioStartMediaTime = Seconds;
		AudioStartWallTime = FPlatformTime::Seconds();
	}
}

void UMediaBoxPlayer::SetRate(float NewRate)
{
	Rate = FMath::Max(0.01f, NewRate);
	if (State == EMediaBoxPlayerState::Playing)
	{
		MediaPlayer->SetRate(Rate);
	}
	if (IsValid(SeparateSound))
	{
		SeparateSound->SetPitchMultiplier(Rate);
		AudioStartMediaTime = GetTime();
		AudioStartWallTime = FPlatformTime::Seconds();
	}
}

void UMediaBoxPlayer::SetVolume(float NewVolume)
{
	Volume = FMath::Max(0.f, NewVolume);
	ApplyAudioVolume();
}

float UMediaBoxPlayer::GetTime() const
{
	return MediaPlayer ? static_cast<float>(MediaPlayer->GetTime().GetTotalSeconds()) : 0.f;
}

float UMediaBoxPlayer::GetDuration() const
{
	return MediaPlayer && IsOpened() ? static_cast<float>(MediaPlayer->GetDuration().GetTotalSeconds()) : 0.f;
}

bool UMediaBoxPlayer::WantsSeparateSound() const
{
	return Clip && Clip->Sound && Clip->GetResolvedAudio() == EMediaBoxAudio::SeparateSound;
}

bool UMediaBoxPlayer::WantsEmbeddedSound() const
{
	return Clip && Clip->GetResolvedAudio() == EMediaBoxAudio::Embedded;
}

void UMediaBoxPlayer::EnsureAudio(UWorld* InAudioWorld)
{
	if (!InAudioWorld)
	{
		return;
	}
	if (AudioWorld.Get() != InAudioWorld)
	{
		DestroyAudio();
		AudioWorld = InAudioWorld;
	}

	USoundClass* MediaSoundClass = UMediaBoxSettings::Get().SoundClass.LoadSynchronous();

	if (WantsSeparateSound())
	{
		if (!IsValid(SeparateSound))
		{
			// 2D, kept across level changes with the pool, never destroyed when it stops: started again on the next play.
			SeparateSound = UGameplayStatics::CreateSound2D(InAudioWorld, Clip->Sound, 1.f, 1.f, 0.f, nullptr, /*bPersistAcrossLevelTransition=*/true, /*bAutoDestroy=*/false);
			if (SeparateSound)
			{
				SeparateSound->bIsUISound = true;
				if (MediaSoundClass)
				{
					SeparateSound->SoundClassOverride = MediaSoundClass;
				}
			}
		}
		else if (SeparateSound->Sound != Clip->Sound)
		{
			SeparateSound->Stop();
			SeparateSound->SetSound(Clip->Sound);
		}
	}

	if (WantsEmbeddedSound() && !IsValid(EmbeddedSound))
	{
		// Ownerless, registered with the world so it ticks and reaches the audio device; 2D, a UI sound.
		EmbeddedSound = NewObject<UMediaSoundComponent>(this, NAME_None, RF_Transient);
		EmbeddedSound->bIsUISound = true;
		EmbeddedSound->bAllowSpatialization = false;
		if (MediaSoundClass)
		{
			EmbeddedSound->SoundClass = MediaSoundClass;
		}
		EmbeddedSound->SetMediaPlayer(MediaPlayer);
		EmbeddedSound->RegisterComponentWithWorld(InAudioWorld);
	}
}

void UMediaBoxPlayer::DestroyAudio()
{
	if (IsValid(SeparateSound))
	{
		SeparateSound->Stop();
		SeparateSound->DestroyComponent();
	}
	SeparateSound = nullptr;

	if (IsValid(EmbeddedSound))
	{
		EmbeddedSound->Stop();
		EmbeddedSound->SetMediaPlayer(nullptr);
		EmbeddedSound->DestroyComponent();
	}
	EmbeddedSound = nullptr;

	bAudioPlaying = false;
	bAudioFadingOut = false;
	AudioFade = 1.f;
}

void UMediaBoxPlayer::StartAudio()
{
	if (!bAudioWanted)
	{
		return;
	}

	bAudioFadingOut = false;
	AudioFade = 1.f;

	if (WantsSeparateSound() && IsValid(SeparateSound))
	{
		const float AtTime = GetTime();
		SeparateSound->SetPitchMultiplier(Rate);
		SeparateSound->Play(AtTime);
		AudioStartMediaTime = AtTime;
		AudioStartWallTime = FPlatformTime::Seconds();
		NextSyncCheck = AudioStartWallTime + MediaBoxPoolPrivate::SyncCheckInterval;
		bAudioPlaying = true;
	}
	else if (WantsEmbeddedSound() && IsValid(EmbeddedSound))
	{
		if (!EmbeddedSound->IsPlaying())
		{
			EmbeddedSound->Start();
		}
		bAudioPlaying = true;
	}
	ApplyAudioVolume();
}

void UMediaBoxPlayer::StopAudio(float FadeOut)
{
	if (!bAudioPlaying)
	{
		return;
	}
	if (FadeOut > 0.f)
	{
		bAudioFadingOut = true;
		AudioFadeDuration = FadeOut;
		return;
	}

	if (IsValid(SeparateSound))
	{
		SeparateSound->Stop();
	}
	if (IsValid(EmbeddedSound))
	{
		EmbeddedSound->Stop();
	}
	bAudioPlaying = false;
	bAudioFadingOut = false;
	AudioFade = 1.f;
}

void UMediaBoxPlayer::ApplyAudioVolume()
{
	const float ClipVolume = Clip ? Clip->Volume : 1.f;
	const float Level = Volume * ClipVolume * AudioFade;
	if (IsValid(SeparateSound))
	{
		SeparateSound->SetVolumeMultiplier(Level);
	}
	if (IsValid(EmbeddedSound))
	{
		EmbeddedSound->SetVolumeMultiplier(Level);
	}
}

void UMediaBoxPlayer::Tick(float DeltaTime)
{
	using namespace MediaBoxPoolPrivate;

	// Audio fading out: down to silence, then stopped.
	if (bAudioFadingOut)
	{
		AudioFade -= DeltaTime / FMath::Max(AudioFadeDuration, KINDA_SMALL_NUMBER);
		if (AudioFade <= 0.f)
		{
			AudioFade = 0.f;
			bAudioFadingOut = false;
			StopAudio(0.f);
		}
		else
		{
			ApplyAudioVolume();
		}
	}

	switch (State)
	{
	case EMediaBoxPlayerState::Opening:
		break;

	case EMediaBoxPlayerState::Priming:
	{
		// The first frame is in once the video's time has moved on from where it started (a frame later, to be sure the
		// texture has it), or after a while whatever it says.
		++StepFrames;
		const bool bAdvanced = GetTime() > PrimeFrom + 0.0005f;
		if (bAdvanced && !bFrameReady)
		{
			bFrameReady = true;
			break;
		}
		if (!bFrameReady && StepFrames <= MaxPrimeFrames)
		{
			break;
		}
		bFrameReady = true;

		if (bPlayRequested)
		{
			// A Media Box waits for it: plays on from here - a cold start, a frame or so in.
			if (!FMath::IsNearlyEqual(Rate, 1.f))
			{
				MediaPlayer->SetRate(Rate);
			}
			State = EMediaBoxPlayerState::Playing;
			bPlayRequested = false;
			StartAudio();
			break;
		}

		// Nobody waits: paused, and back on its very first frame - warm.
		MediaPlayer->Pause();
		if (Clip && MediaPlayer->SupportsSeeking() && GetTime() > Clip->StartTime + 1.f / 60.f)
		{
			MediaPlayer->Seek(ToTimespan(Clip->StartTime));
			LastTime = Clip->StartTime;
			State = EMediaBoxPlayerState::Settling;
			StepFrames = 0;
		}
		else
		{
			State = EMediaBoxPlayerState::Ready;
			MEDIABOX_LOG(TEXT("Media Box: %s is warm"), *DescribeClip(Clip));
		}
		break;
	}

	case EMediaBoxPlayerState::Settling:
		if (++StepFrames > MaxSettleFrames)
		{
			HandleSeekCompleted();
		}
		break;

	case EMediaBoxPlayerState::Playing:
	{
		if (MediaPlayer->IsClosed())
		{
			Fail(TEXT("the player closed while playing"));
			break;
		}

		const float Now = GetTime();

		// The player's own loop wrapping round: time went back.
		if (MediaPlayer->IsLooping() && Now + 0.25f < LastTime)
		{
			++LoopCount;
			if (bAudioPlaying && WantsSeparateSound() && IsValid(SeparateSound))
			{
				SeparateSound->Play(Now);
				AudioStartMediaTime = Now;
				AudioStartWallTime = FPlatformTime::Seconds();
			}
		}
		// A range loop: back to Loop Start just before Loop End.
		else if (Clip && Clip->bLoop && !Clip->LoopsWholeVideo() && Clip->LoopEnd > 0.f && Now >= Clip->LoopEnd - LoopEndLead)
		{
			const float LoopFrom = Clip->LoopStart;
			MediaPlayer->Seek(ToTimespan(LoopFrom));
			++LoopCount;
			LastTime = LoopFrom;
			if (bAudioPlaying && WantsSeparateSound() && IsValid(SeparateSound))
			{
				SeparateSound->Play(LoopFrom);
				AudioStartMediaTime = LoopFrom;
				AudioStartWallTime = FPlatformTime::Seconds();
			}
			break;
		}

		// A Separate Sound kept in step: started again at the video's time when it has drifted too far.
		if (bAudioPlaying && !bAudioFadingOut && WantsSeparateSound() && IsValid(SeparateSound))
		{
			const double WallNow = FPlatformTime::Seconds();
			if (WallNow >= NextSyncCheck)
			{
				NextSyncCheck = WallNow + SyncCheckInterval;
				const float Expected = AudioStartMediaTime + static_cast<float>(WallNow - AudioStartWallTime) * Rate;
				if (FMath::Abs(Expected - Now) > UMediaBoxSettings::Get().AudioResyncThreshold)
				{
					SeparateSound->Play(Now);
					AudioStartMediaTime = Now;
					AudioStartWallTime = WallNow;
				}
			}
		}

		LastTime = Now;
		break;
	}

	default:
		break;
	}
}

// =====================================================================================================
// UMediaBoxPool
// =====================================================================================================

UMediaBoxPool* UMediaBoxPool::Get(const UObject* WorldContext)
{
	const UWorld* World = (WorldContext && GEngine) ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr)
	{
		if (const UMediaBoxSubsystem* Subsystem = GameInstance->GetSubsystem<UMediaBoxSubsystem>())
		{
			if (UMediaBoxPool* GamePool = Subsystem->GetPool())
			{
				return GamePool;
			}
		}
	}

	TStrongObjectPtr<UMediaBoxPool>& EditorPool = MediaBoxPoolPrivate::GetEditorPool();
	if (!EditorPool.IsValid())
	{
		EditorPool.Reset(NewObject<UMediaBoxPool>(GetTransientPackage(), TEXT("MediaBoxEditorPool"), RF_Transient));
		EditorPool->Startup();
	}
	return EditorPool.Get();
}

void UMediaBoxPool::ShutdownEditorPool()
{
	TStrongObjectPtr<UMediaBoxPool>& EditorPool = MediaBoxPoolPrivate::GetEditorPool();
	if (EditorPool.IsValid())
	{
		EditorPool->Shutdown();
		EditorPool.Reset();
	}
}

void UMediaBoxPool::Startup()
{
	if (!TickHandle.IsValid())
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMediaBoxPool::Tick));
	}
}

void UMediaBoxPool::Shutdown()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	for (UMediaBoxPlayer* PooledPlayer : Players)
	{
		if (PooledPlayer)
		{
			PooledPlayer->Close();
			PooledPlayer->DestroyAudio();
		}
	}
	Players.Reset();
	Groups.Reset();
	Pinned.Reset();
	Boxes.Reset();
}

void UMediaBoxPool::BeginDestroy()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	Super::BeginDestroy();
}

UMediaBoxPlayer* UMediaBoxPool::MakePlayer()
{
	UMediaBoxPlayer* NewPlayer = NewObject<UMediaBoxPlayer>(this, NAME_None, RF_Transient);
	NewPlayer->Initialize();
	Players.Add(NewPlayer);
	return NewPlayer;
}

UMediaBoxPlayer* UMediaBoxPool::Acquire(UMediaBoxClip* NewClip, UObject* Owner)
{
	if (!NewClip)
	{
		return nullptr;
	}

	// The clip's own warm player - Ready plays at once - else one opening it already.
	UMediaBoxPlayer* Found = nullptr;
	for (UMediaBoxPlayer* PooledPlayer : Players)
	{
		if (PooledPlayer && !PooledPlayer->IsLeased() && PooledPlayer->GetClip() == NewClip && !PooledPlayer->HasFailed())
		{
			if (!Found || PooledPlayer->IsReady())
			{
				Found = PooledPlayer;
			}
		}
	}

	if (Found)
	{
		if (Found->IsReady())
		{
			++WarmStarts;
			MEDIABOX_LOG(TEXT("Media Box: warm start of %s"), *NewClip->GetName());
		}
		else
		{
			++ColdStarts;
			MEDIABOX_LOG(TEXT("Media Box: start of %s while it was still warming"), *NewClip->GetName());
		}
	}
	else
	{
		TArray<UMediaBoxClip*> Wanted;
		RankWanted(Wanted);
		Found = FindPlayerToReuse(Wanted, /*bAllowNew=*/false);
		if (!Found)
		{
			Found = MakePlayer();
		}
		Found->Open(NewClip);
		++ColdStarts;
		MEDIABOX_LOG(TEXT("Media Box: cold start of %s (it wasn't warm)"), *NewClip->GetName());
	}

	Found->LeaseOwner = Owner;
	Found->bHasLease = true;
	Found->LoopCount = 0;
	Found->EndCount = 0;
	Found->LastWantedTime = FPlatformTime::Seconds();
	return Found;
}

void UMediaBoxPool::Release(UMediaBoxPlayer* PooledPlayer, bool bRewind, float AudioFadeOut)
{
	if (!PooledPlayer)
	{
		return;
	}
	PooledPlayer->LeaseOwner.Reset();
	PooledPlayer->bHasLease = false;
	PooledPlayer->LastWantedTime = FPlatformTime::Seconds();
	PooledPlayer->SetRate(1.f);

	if (PooledPlayer->HasFailed())
	{
		PooledPlayer->Close();
	}
	else if (bRewind)
	{
		PooledPlayer->Rewind(AudioFadeOut);
	}
	else
	{
		PooledPlayer->Pause(AudioFadeOut);
	}
}

void UMediaBoxPool::RegisterBox(UMediaBox* Box)
{
	Boxes.AddUnique(Box);
}

void UMediaBoxPool::UnregisterBox(UMediaBox* Box)
{
	Boxes.RemoveSingleSwap(Box);
}

void UMediaBoxPool::AddToGroup(FName Group, UWidget* Widget, UMediaBoxClip* TargetClip)
{
	if (Group.IsNone() || !Widget)
	{
		return;
	}
	FMediaBoxWarmGroup& WarmGroup = Groups.FindOrAdd(Group);
	for (FMediaBoxWarmTarget& Target : WarmGroup.Targets)
	{
		if (Target.Widget.Get() == Widget)
		{
			Target.Clip = TargetClip;
			return;
		}
	}
	FMediaBoxWarmTarget& Added = WarmGroup.Targets.AddDefaulted_GetRef();
	Added.Widget = Widget;
	Added.Clip = TargetClip;
}

void UMediaBoxPool::RemoveFromGroup(FName Group, UWidget* Widget)
{
	if (FMediaBoxWarmGroup* WarmGroup = Groups.Find(Group))
	{
		WarmGroup->Targets.RemoveAll([Widget](const FMediaBoxWarmTarget& Target) { return Target.Widget.Get() == Widget; });
		if (WarmGroup->Targets.Num() == 0)
		{
			Groups.Remove(Group);
		}
	}
}

void UMediaBoxPool::ClearGroup(FName Group)
{
	Groups.Remove(Group);
}

void UMediaBoxPool::SetGroupFocus(FName Group, UWidget* Widget)
{
	if (FMediaBoxWarmGroup* WarmGroup = Groups.Find(Group))
	{
		WarmGroup->Focus = Widget;
		WarmGroup->FocusTime = FPlatformTime::Seconds();
	}
}

void UMediaBoxPool::SetGroupFocusByClip(FName Group, UMediaBoxClip* FocusClip)
{
	if (FMediaBoxWarmGroup* WarmGroup = Groups.Find(Group))
	{
		for (const FMediaBoxWarmTarget& Target : WarmGroup->Targets)
		{
			if (Target.Clip == FocusClip && Target.Widget.IsValid())
			{
				WarmGroup->Focus = Target.Widget;
				WarmGroup->FocusTime = FPlatformTime::Seconds();
				return;
			}
		}
	}
}

void UMediaBoxPool::PinClip(UMediaBoxClip* TargetClip, bool bKeepWarm)
{
	if (!TargetClip)
	{
		return;
	}
	if (bKeepWarm)
	{
		++Pinned.FindOrAdd(TargetClip);
	}
	else if (int32* Count = Pinned.Find(TargetClip))
	{
		if (--(*Count) <= 0)
		{
			Pinned.Remove(TargetClip);
		}
	}
}

int32 UMediaBoxPool::GetNumWarm() const
{
	int32 Count = 0;
	for (const UMediaBoxPlayer* PooledPlayer : Players)
	{
		Count += (PooledPlayer && PooledPlayer->IsReady() && !PooledPlayer->IsLeased()) ? 1 : 0;
	}
	return Count;
}

int32 UMediaBoxPool::GetNumLeased() const
{
	int32 Count = 0;
	for (const UMediaBoxPlayer* PooledPlayer : Players)
	{
		Count += (PooledPlayer && PooledPlayer->IsLeased()) ? 1 : 0;
	}
	return Count;
}

void UMediaBoxPool::UpdatePointer(float DeltaTime)
{
	if (!FSlateApplication::IsInitialized())
	{
		bHasPointer = false;
		return;
	}

	const FVector2f Position = FVector2f(FVector2D(FSlateApplication::Get().GetCursorPos()));
	if (bHasPointer && DeltaTime > 0.f)
	{
		const FVector2f Moved = Position - PointerPosition;
		if (Moved.SizeSquared() > 0.25f)
		{
			PointerMoveTime = FPlatformTime::Seconds();
		}
		// Smoothed, so one jittery frame doesn't send the guess across the screen.
		PointerVelocity = FMath::Lerp(PointerVelocity, Moved / DeltaTime, 0.35f);
	}
	PointerPosition = Position;
	bHasPointer = true;
}

void UMediaBoxPool::RankWanted(TArray<UMediaBoxClip*>& OutClips)
{
	OutClips.Reset();
	const UMediaBoxSettings& Settings = UMediaBoxSettings::Get();
	const int32 Capacity = Settings.GetMaxWarmPlayers();
	if (Capacity <= 0)
	{
		return;
	}

	// (clip, score): lower is wanted more. Pinned clips first, then each group's ranks.
	TMap<UMediaBoxClip*, float> Scores;
	for (const TPair<TObjectPtr<UMediaBoxClip>, int32>& Pin : Pinned)
	{
		if (Pin.Key)
		{
			Scores.Add(Pin.Key, 0.f);
		}
	}

	const FVector2f Predicted = PointerPosition + (Settings.bHoverIntent ? PointerVelocity * Settings.HoverIntentLookAhead : FVector2f::ZeroVector);

	for (auto GroupIt = Groups.CreateIterator(); GroupIt; ++GroupIt)
	{
		FMediaBoxWarmGroup& WarmGroup = GroupIt.Value();
		WarmGroup.Targets.RemoveAll([](const FMediaBoxWarmTarget& Target) { return !Target.Widget.IsValid() || !Target.Clip; });
		if (WarmGroup.Targets.Num() == 0)
		{
			GroupIt.RemoveCurrent();
			continue;
		}

		// Ranked around the focused target when it was focused after the pointer last moved (a gamepad), else around where the
		// pointer is heading.
		const UWidget* FocusWidget = WarmGroup.Focus.Get();
		const bool bFromFocus = FocusWidget && (!bHasPointer || WarmGroup.FocusTime >= PointerMoveTime);
		FVector2f Anchor = Predicted;
		if (bFromFocus)
		{
			Anchor = FVector2f(FVector2D(FocusWidget->GetCachedGeometry().GetAbsolutePositionAtCoordinates(FVector2D(0.5, 0.5))));
		}

		TArray<TPair<float, UMediaBoxClip*>, TInlineAllocator<32>> Ranked;
		for (const FMediaBoxWarmTarget& Target : WarmGroup.Targets)
		{
			const UWidget* TargetWidget = Target.Widget.Get();
			const FGeometry& Geometry = TargetWidget->GetCachedGeometry();
			float Distance = TNumericLimits<float>::Max();
			if (TargetWidget == FocusWidget)
			{
				Distance = -1.f;
			}
			else if (!FVector2D(Geometry.GetLocalSize()).IsNearlyZero() && (bFromFocus || bHasPointer))
			{
				// Distance to its edge, not its middle: a big card the pointer is already over is nearest.
				const FVector2f TopLeft = FVector2f(FVector2D(Geometry.GetAbsolutePositionAtCoordinates(FVector2D(0.0, 0.0))));
				const FVector2f BottomRight = FVector2f(FVector2D(Geometry.GetAbsolutePositionAtCoordinates(FVector2D(1.0, 1.0))));
				const FVector2f Closest(FMath::Clamp(Anchor.X, FMath::Min(TopLeft.X, BottomRight.X), FMath::Max(TopLeft.X, BottomRight.X)),
					FMath::Clamp(Anchor.Y, FMath::Min(TopLeft.Y, BottomRight.Y), FMath::Max(TopLeft.Y, BottomRight.Y)));
				Distance = FVector2f::Distance(Anchor, Closest);
			}
			Ranked.Emplace(Distance, Target.Clip.Get());
		}
		Ranked.StableSort([](const TPair<float, UMediaBoxClip*>& A, const TPair<float, UMediaBoxClip*>& B) { return A.Key < B.Key; });

		int32 Taken = 0;
		TArray<UMediaBoxClip*, TInlineAllocator<16>> Seen;
		for (const TPair<float, UMediaBoxClip*>& Entry : Ranked)
		{
			if (Taken >= Settings.WarmPerGroup)
			{
				break;
			}
			if (Seen.Contains(Entry.Value))
			{
				continue;
			}
			Seen.Add(Entry.Value);
			const float Score = 1.f + static_cast<float>(Taken) + FMath::Min(FMath::Max(Entry.Key, 0.f), 100000.f) * 1.e-6f;
			float& Existing = Scores.FindOrAdd(Entry.Value, Score);
			Existing = FMath::Min(Existing, Score);
			++Taken;
		}
	}

	Scores.ValueSort([](float A, float B) { return A < B; });
	for (const TPair<UMediaBoxClip*, float>& Entry : Scores)
	{
		if (OutClips.Num() >= Capacity)
		{
			break;
		}
		if (Entry.Key->MediaSource)
		{
			OutClips.Add(Entry.Key);
		}
	}
}

UMediaBoxPlayer* UMediaBoxPool::FindPlayerToReuse(TConstArrayView<UMediaBoxClip*> Keep, bool bAllowNew)
{
	UMediaBoxPlayer* Best = nullptr;
	for (UMediaBoxPlayer* PooledPlayer : Players)
	{
		if (!PooledPlayer || PooledPlayer->IsLeased())
		{
			continue;
		}
		if (PooledPlayer->IsFree())
		{
			return PooledPlayer;
		}
		if (Keep.Contains(PooledPlayer->GetClip()))
		{
			continue;
		}
		if (!Best || PooledPlayer->LastWantedTime < Best->LastWantedTime)
		{
			Best = PooledPlayer;
		}
	}

	// A new one only while there is room; otherwise the longest unwanted one is taken over.
	if (bAllowNew)
	{
		return MakePlayer();
	}
	return Best;
}

void UMediaBoxPool::Rebalance()
{
	TArray<UMediaBoxClip*> Wanted;
	RankWanted(Wanted);

	const int32 Capacity = UMediaBoxSettings::Get().GetMaxWarmPlayers();
	const double Now = FPlatformTime::Seconds();

	auto CountUnleasedOpen = [this]()
	{
		int32 Count = 0;
		for (const UMediaBoxPlayer* PooledPlayer : Players)
		{
			Count += (PooledPlayer && !PooledPlayer->IsLeased() && !PooledPlayer->IsFree()) ? 1 : 0;
		}
		return Count;
	};

	for (UMediaBoxClip* WantedClip : Wanted)
	{
		UMediaBoxPlayer* Existing = nullptr;
		for (UMediaBoxPlayer* PooledPlayer : Players)
		{
			if (PooledPlayer && PooledPlayer->GetClip() == WantedClip)
			{
				Existing = PooledPlayer;
				break;
			}
		}
		if (Existing)
		{
			Existing->LastWantedTime = Now;
			continue;
		}

		UMediaBoxPlayer* Slot = FindPlayerToReuse(Wanted, CountUnleasedOpen() < Capacity);
		if (!Slot)
		{
			break;
		}
		Slot->Open(WantedClip);
		Slot->LastWantedTime = Now;
	}

	// Over capacity: the longest unwanted ones are closed. Players beyond what is open and leased are let go of.
	int32 Open = CountUnleasedOpen();
	while (Open > Capacity)
	{
		UMediaBoxPlayer* Oldest = nullptr;
		for (UMediaBoxPlayer* PooledPlayer : Players)
		{
			if (PooledPlayer && !PooledPlayer->IsLeased() && !PooledPlayer->IsFree() && !Wanted.Contains(PooledPlayer->GetClip())
				&& (!Oldest || PooledPlayer->LastWantedTime < Oldest->LastWantedTime))
			{
				Oldest = PooledPlayer;
			}
		}
		if (!Oldest)
		{
			break;
		}
		Oldest->Close();
		--Open;
	}

	const int32 MaxPlayers = FMath::Max(Capacity, 0) + GetNumLeased();
	for (int32 Index = Players.Num() - 1; Index >= 0 && Players.Num() > MaxPlayers; --Index)
	{
		UMediaBoxPlayer* PooledPlayer = Players[Index];
		if (!PooledPlayer || (PooledPlayer->IsFree() && !PooledPlayer->IsLeased()))
		{
			if (PooledPlayer)
			{
				PooledPlayer->DestroyAudio();
			}
			Players.RemoveAtSwap(Index);
		}
	}
}

bool UMediaBoxPool::Tick(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_MediaBoxPoolTick);

	UpdatePointer(DeltaTime);

	// The Media Boxes first: they lease and release players.
	Boxes.RemoveAll([](const TWeakObjectPtr<UMediaBox>& Box) { return !Box.IsValid(); });
	const TArray<TWeakObjectPtr<UMediaBox>> BoxesNow = Boxes;
	for (const TWeakObjectPtr<UMediaBox>& Box : BoxesNow)
	{
		if (UMediaBox* LiveBox = Box.Get())
		{
			LiveBox->TickMediaBox(DeltaTime);
		}
	}

	// Leases whose Media Box has gone without giving them back (garbage collected): taken back, rewound, warm again.
	for (UMediaBoxPlayer* PooledPlayer : Players)
	{
		if (PooledPlayer && PooledPlayer->bHasLease && !PooledPlayer->LeaseOwner.IsValid())
		{
			Release(PooledPlayer, /*bRewind=*/true, 0.f);
		}
	}

	Rebalance();

	for (UMediaBoxPlayer* PooledPlayer : Players)
	{
		if (PooledPlayer)
		{
			PooledPlayer->Tick(DeltaTime);
		}
	}

	SET_DWORD_STAT(STAT_MediaBoxPlayers, Players.Num());
	SET_DWORD_STAT(STAT_MediaBoxWarmPlayers, GetNumWarm());
	SET_DWORD_STAT(STAT_MediaBoxLeasedPlayers, GetNumLeased());
	SET_DWORD_STAT(STAT_MediaBoxBoxes, Boxes.Num());
	return true;
}

// =====================================================================================================
// UMediaBoxSubsystem
// =====================================================================================================

void UMediaBoxSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Pool = NewObject<UMediaBoxPool>(this, TEXT("MediaBoxPool"), RF_Transient);
	Pool->Startup();
}

void UMediaBoxSubsystem::Deinitialize()
{
	if (Pool)
	{
		Pool->Shutdown();
		Pool = nullptr;
	}
	Super::Deinitialize();
}
