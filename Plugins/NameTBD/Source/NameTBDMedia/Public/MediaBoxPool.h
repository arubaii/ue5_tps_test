// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"

#include "MediaBoxTypes.h"

#include "MediaBoxPool.generated.h"

class AActor;
class UAudioComponent;
class USceneComponent;
class USoundBase;
class UMediaBox;
class UMediaBoxClip;
class UMediaPlayer;
class UMediaSoundComponent;
class UMediaTexture;
class UWidget;
class UWorld;

/** Where a pooled player is. Not reflected: the pool's own bookkeeping. */
enum class EMediaBoxPlayerState : uint8
{
	/** Nothing open. */
	Free,
	/** OpenSource called, waiting for the player to open it. */
	Opening,
	/** Open, playing silently until the first frame is in the texture. */
	Priming,
	/** Paused, sought back to the clip's start, waiting for the seek to land. */
	Settling,
	/** Paused on the clip's first frame, ready to play at once - "warm". */
	Ready,
	Playing,
	Paused,
	/** Played to the end (no loop). */
	Ended,
	/** Couldn't open or play the clip. */
	Failed
};

/**
 * One pooled player: a Media Player, the Media Texture it draws into and the audio that goes with it, opened on one clip at a
 * time. Made per pool, never shared assets - two Media Boxes never fight over one player. A Media Box leases one while it plays;
 * the rest are kept warm by the pool. See MediaBoxTypes.h.
 */
UCLASS(Transient)
class NAMETBDMEDIA_API UMediaBoxPlayer : public UObject
{
	GENERATED_BODY()

public:

	/** Makes the Media Player and Media Texture. Called once, by the pool. */
	void Initialize();

	/** Opens NewClip, warming it: open, first frame, back to its start, paused. False when it couldn't even start opening. */
	bool Open(UMediaBoxClip* NewClip);

	/** Closes what is open and lets go of its audio. */
	void Close();

	/**
	 * Plays from where it is: at once when warm, else as soon as its first frame is in. Its audio plays in InAudioWorld - 2D
	 * (held by LocalOwner, the Media Box's player controller, when given), or, with ScreenComponent and WorldAudio, from that
	 * Diegetic Screen in the world.
	 */
	void Play(UWorld* InAudioWorld, bool bWithAudio, USceneComponent* ScreenComponent = nullptr, const FMediaBoxWorldAudio* WorldAudio = nullptr, AActor* LocalOwner = nullptr);

	/** Pauses (audio fades out over FadeOut seconds). */
	void Pause(float FadeOut);

	/** Back to the start, paused, warm again for the next Play (audio fades out over FadeOut seconds). */
	void Rewind(float FadeOut);

	void Seek(float Seconds);
	void SetRate(float NewRate);

	/** Fades the audio out over FadeOut seconds (then stops it), the video playing on - a Media Box fading back to its poster. */
	void FadeOutAudio(float FadeOut) { StopAudio(FadeOut); }

	/** The volume the audio plays at (the Media Box's, muted or not, times the clip's). */
	void SetVolume(float NewVolume);

	/** Advances opening, priming, loops, audio fades and audio sync. Called by the pool every frame. */
	void Tick(float DeltaTime);

	EMediaBoxPlayerState GetState() const { return State; }
	UMediaBoxClip* GetClip() const { return Clip; }
	UMediaPlayer* GetMediaPlayer() const { return MediaPlayer; }
	UMediaTexture* GetMediaTexture() const { return MediaTexture; }

	bool IsFree() const { return State == EMediaBoxPlayerState::Free; }
	bool IsReady() const { return State == EMediaBoxPlayerState::Ready; }
	bool HasFailed() const { return State == EMediaBoxPlayerState::Failed; }
	bool IsPlaying() const { return State == EMediaBoxPlayerState::Playing; }

	/** Whether the texture holds a frame of this clip - the video can be shown over the poster. */
	bool IsFrameReady() const { return bFrameReady; }

	/** Whether it has been opened (On Opened). */
	bool IsOpened() const { return State != EMediaBoxPlayerState::Free && State != EMediaBoxPlayerState::Opening && State != EMediaBoxPlayerState::Failed; }

	/** Seconds into the video. */
	float GetTime() const;

	/** The video's length in seconds; 0 until opened (or for a live stream). */
	float GetDuration() const;

	/** Why it failed, when it has. */
	const FString& GetError() const { return Error; }

	/** Counted for the lease: loops done, ends reached. */
	int32 GetLoopCount() const { return LoopCount; }
	int32 GetEndCount() const { return EndCount; }

	/** The Media Box using it, if any. */
	UObject* GetLeaseOwner() const { return LeaseOwner.Get(); }
	bool IsLeased() const { return LeaseOwner.IsValid(); }

	/** When it was last leased or last asked to be warm - the pool closes the longest unwanted first. */
	double LastWantedTime = 0.0;

private:

	friend class UMediaBoxPool;

	UFUNCTION()
	void HandleMediaOpened(FString OpenedUrl);

	UFUNCTION()
	void HandleMediaOpenFailed(FString FailedUrl);

	UFUNCTION()
	void HandleEndReached();

	UFUNCTION()
	void HandleSeekCompleted();

	void Fail(const FString& Reason);

	/** Plays from the state it is in now (Ready, Paused, Ended, or straight out of priming). */
	void StartPlaying();

	/** Starts the clip's audio at the video's time (Separate Sound) or the Media Sound Component (Embedded). */
	void StartAudio();
	void StopAudio(float FadeOut);
	void ApplyAudioVolume();
	bool WantsSeparateSound() const;
	bool WantsEmbeddedSound() const;

	/**
	 * The audio components, made (again) when they don't exist yet where they are wanted: owned by a hidden local actor of
	 * InAudioWorld (2D), or by ScreenComponent's actor and attached to it (World Audio). Owned components tick and activate as
	 * any do - the Media Sound Component needs its tick to receive the video's audio.
	 */
	void EnsureAudio(UWorld* InAudioWorld, USceneComponent* ScreenComponent, const FMediaBoxWorldAudio* WorldAudio, AActor* LocalOwner);

	/** Puts 2D or world settings (attenuation, occlusion) on the audio components. */
	void ConfigureAudioSpace(const FMediaBoxWorldAudio* WorldAudio);
	void DestroyAudio();

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> MediaPlayer = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> MediaTexture = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMediaSoundComponent> EmbeddedSound = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> SeparateSound = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMediaBoxClip> Clip = nullptr;

	TWeakObjectPtr<UObject> LeaseOwner;
	TWeakObjectPtr<UWorld> AudioWorld;

	/** The actor owning the audio components now, the screen they are attached to (World Audio), and which audio they are for. */
	TWeakObjectPtr<AActor> AudioOwner;
	TWeakObjectPtr<USceneComponent> AudioScreen;
	TWeakObjectPtr<USoundBase> AudioForSound;
	EMediaBoxAudio AudioForKind = EMediaBoxAudio::Auto;

	/**
	 * Whether the Embedded Sound component existed when the clip was opened. A warm player is opened with no audio listener; one
	 * added later is given the player's audio by a seek to where it is, as the audio starts (StartAudio).
	 */
	bool bEmbeddedSinkAtOpen = false;

	/** Leased and not yet given back - a lease whose owner has gone (garbage collected) is taken back by the pool. */
	bool bHasLease = false;

	EMediaBoxPlayerState State = EMediaBoxPlayerState::Free;
	FString Error;

	bool bFrameReady = false;
	bool bPlayRequested = false;
	bool bAudioWanted = true;

	/** Frames spent priming / settling - each step gives up waiting after a while rather than hang. */
	int32 StepFrames = 0;
	float PrimeFrom = 0.f;

	/** The video time last frame - a native loop wrapping round shows as time going back. */
	float LastTime = 0.f;

	int32 LoopCount = 0;
	int32 EndCount = 0;

	float Rate = 1.f;
	float Volume = 1.f;

	/** The audio fade: 1 full, falling to 0 over AudioFadeDuration, then the audio stops. */
	float AudioFade = 1.f;
	float AudioFadeDuration = 0.f;
	bool bAudioFadingOut = false;
	bool bAudioPlaying = false;

	/** Separate Sound clock: the video time it was started at, and when (platform seconds). For keeping it in step. */
	float AudioStartMediaTime = 0.f;
	double AudioStartWallTime = 0.0;
	double NextSyncCheck = 0.0;
};

/**
 * Holds the 2D audio components of the Media Boxes playing in one world: a hidden, transient, local actor (never replicated, never
 * saved), made on first use. Owned components tick and activate normally.
 */
UCLASS(Transient, NotPlaceable, NotBlueprintable)
class NAMETBDMEDIA_API AMediaBoxAudioActor : public AActor
{
	GENERATED_BODY()

public:
	AMediaBoxAudioActor();

	/** The world's audio actor, made on first use. Null in a world that has no audio (a dedicated server). */
	static AMediaBoxAudioActor* Get(UWorld* World);
};

/** One widget in a Warm Group and the clip it plays (or shows) when hovered or focused. */
USTRUCT()
struct NAMETBDMEDIA_API FMediaBoxWarmTarget
{
	GENERATED_BODY()

	TWeakObjectPtr<UWidget> Widget;

	UPROPERTY(Transient)
	TObjectPtr<UMediaBoxClip> Clip = nullptr;
};

/** The clips of one screen - a skill tree - from which the pool keeps the likeliest next hovers warm. */
USTRUCT()
struct NAMETBDMEDIA_API FMediaBoxWarmGroup
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<FMediaBoxWarmTarget> Targets;

	/** The target last focused or hovered (Set Warm Group Focus, or a Media Box of the group starting) and when. */
	TWeakObjectPtr<UWidget> Focus;
	double FocusTime = 0.0;
};

/**
 * The warm pool: every pooled player of one game instance (or of the editor, for the designer), the Warm Groups and pinned
 * clips that say which clips should be warm, and the Media Boxes, which it ticks. Each frame it ranks what is wanted - pinned
 * clips, then each group's focused target and those nearest it (or nearest where the pointer is heading) - and keeps the top
 * Max Warm Players of them open and paused on their first frame; a Media Box asking for one of those plays it at once.
 */
UCLASS(Transient)
class NAMETBDMEDIA_API UMediaBoxPool : public UObject
{
	GENERATED_BODY()

public:

	/** The pool for WorldContext's game instance; the editor's own for a world without one (the designer, editor worlds). */
	static UMediaBoxPool* Get(const UObject* WorldContext);

	/** The editor's pool, if made - for module shutdown. */
	static void ShutdownEditorPool();

	void Startup();
	void Shutdown();

	virtual void BeginDestroy() override;

	/**
	 * A player for NewClip, leased to Owner until Release: the warm one when there is one (it plays at once), else one opened
	 * now. Never null for a clip with a Media Source.
	 */
	UMediaBoxPlayer* Acquire(UMediaBoxClip* NewClip, UObject* Owner);

	/** Gives a leased player back. bRewind puts it back on its first frame, warm for the next hover. */
	void Release(UMediaBoxPlayer* PooledPlayer, bool bRewind, float AudioFadeOut);

	void RegisterBox(UMediaBox* Box);
	void UnregisterBox(UMediaBox* Box);

	// Warm Groups -------------------------------------------------------------------------------

	void AddToGroup(FName Group, UWidget* Widget, UMediaBoxClip* TargetClip);
	void RemoveFromGroup(FName Group, UWidget* Widget);
	void ClearGroup(FName Group);
	void SetGroupFocus(FName Group, UWidget* Widget);

	/** Focuses the group's target that plays FocusClip (the first one) - a shared Media Box told to play it. */
	void SetGroupFocusByClip(FName Group, UMediaBoxClip* FocusClip);

	/** Keeps TargetClip warm whatever the groups say (bKeepWarm), or stops doing so. Counted: as many offs as ons. */
	void PinClip(UMediaBoxClip* TargetClip, bool bKeepWarm);

	// Stats -------------------------------------------------------------------------------------

	int32 GetNumWarm() const;
	int32 GetNumLeased() const;
	int32 WarmStarts = 0;
	int32 ColdStarts = 0;

private:

	bool Tick(float DeltaTime);

	/** Which clips should be warm, likeliest first (at most the pool's capacity). */
	void RankWanted(TArray<UMediaBoxClip*>& OutClips);

	/** Opens what RankWanted asks for, closing what nobody wants once over capacity. */
	void Rebalance();

	/** An unleased player to open a clip on: a free one, else the longest unwanted one not in Keep, else a new one (bAllowNew). */
	UMediaBoxPlayer* FindPlayerToReuse(TConstArrayView<UMediaBoxClip*> Keep, bool bAllowNew);

	UMediaBoxPlayer* MakePlayer();

	void UpdatePointer(float DeltaTime);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMediaBoxPlayer>> Players;

	UPROPERTY(Transient)
	TMap<FName, FMediaBoxWarmGroup> Groups;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UMediaBoxClip>, int32> Pinned;

	TArray<TWeakObjectPtr<UMediaBox>> Boxes;

	FTSTicker::FDelegateHandle TickHandle;

	/** The pointer, in desktop space, its smoothed speed, and when it last moved - for guessing the next hover. */
	FVector2f PointerPosition = FVector2f::ZeroVector;
	FVector2f PointerVelocity = FVector2f::ZeroVector;
	bool bHasPointer = false;
	double PointerMoveTime = 0.0;
};

/** Holds the warm pool of a game instance, so warm players outlive level changes. */
UCLASS()
class NAMETBDMEDIA_API UMediaBoxSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UMediaBoxPool* GetPool() const { return Pool; }

private:

	UPROPERTY(Transient)
	TObjectPtr<UMediaBoxPool> Pool = nullptr;
};
