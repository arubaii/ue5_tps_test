// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"

#include "MediaBoxTypes.h"

#include "MediaBox.generated.h"

class SMediaBox;
class UDiegeticScreenComponent;
class UMediaBox;
class UMediaBoxClip;
class UMediaBoxPlayer;
class UMediaBoxPool;
class UMediaPlayer;
class UMediaTexture;
class UWidgetAnimStateMachineAsset;

/**
 * The state machine inputs a Media Box sets as it plays - on the state machines played on it, in it or around it (as Set State
 * Machine Input aims). Each is the name of an input of the machine; None sets nothing.
 */
USTRUCT(BlueprintType)
struct NAMETBDMEDIA_API FMediaBoxMachineInputs
{
	GENERATED_BODY()

	/** Only this machine's inputs. None: any machine found with an input of the name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Machine", meta = (DisplayName = "State Machine"))
	TObjectPtr<UWidgetAnimStateMachineAsset> StateMachine = nullptr;

	/** A Bool input: true while the video plays. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Machine", meta = (DisplayName = "Playing (Bool)"))
	FName Playing;

	/** A Number input: how far through the video, 0 - 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Machine", meta = (DisplayName = "Progress (Number)"))
	FName Progress;

	/** A Trigger: fired as the video's first frame shows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Machine", meta = (DisplayName = "First Frame (Trigger)"))
	FName FirstFrame;

	/** A Trigger: fired each time it loops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Machine", meta = (DisplayName = "Looped (Trigger)"))
	FName Looped;

	/** A Trigger: fired as it reaches its end (a clip that doesn't loop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Machine", meta = (DisplayName = "Ended (Trigger)"))
	FName Ended;

	/** A Trigger: fired when it can't be opened or played. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Machine", meta = (DisplayName = "Error (Trigger)"))
	FName Error;

	bool IsEmpty() const { return Playing.IsNone() && Progress.IsNone() && FirstFrame.IsNone() && Looped.IsNone() && Ended.IsNone() && Error.IsNone(); }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMediaBoxEvent, UMediaBox*, MediaBox);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMediaBoxError, UMediaBox*, MediaBox, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMediaBoxSubtitle, UMediaBox*, MediaBox, const FText&, Subtitle);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMediaBoxStatus, UMediaBox*, MediaBox, EMediaBoxStatus, NewStatus);

/**
 * Media Box: a video in UI that starts the instant it is asked to - a skill hovered plays its preview clip, with audio, on that
 * frame. See the note at the top of MediaBoxTypes.h.
 *
 * Give it a Media Box Clip (the video, its poster, its audio) and say when it plays (Play When): Hovered Or Focused for a
 * preview inside a button, Visible for one inside an Info Card, Manual for Blueprint and Media cues. The clip's poster shows
 * at once and the video fades in over it as its first frame arrives - at once when the clip was warm.
 *
 * Warm: give the Media Boxes of a screen one Warm Group name and the pool keeps the one hovered or focused and its neighbours
 * open and paused on their first frames - the next hover starts at once. A button that only changes what one shared Media
 * Box plays joins the group with Add To Media Warm Group.
 *
 * Every Media Box has a player of its own while it plays (never a shared Media Player asset): any number can be on screen.
 * It pauses while not drawn (Pause When Hidden), its audio is a 2D UI sound, and on a Diegetic Screen it redraws the screen
 * only when a new frame is in.
 */
UCLASS(meta = (DisplayName = "Media Box", PrioritizeCategories = "Media Playback Appearance Audio Subtitles"))
class NAMETBDMEDIA_API UMediaBox : public UWidget
{
	GENERATED_BODY()

public:
	UMediaBox();

	// ---------------------------------------------------------------------------------------------
	// Media
	// ---------------------------------------------------------------------------------------------

	/** What it plays: the video, its poster, its audio, where it starts and loops, its subtitles. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media", meta = (DisplayName = "Clip"))
	TObjectPtr<UMediaBoxClip> MediaClip = nullptr;

	/**
	 * The Warm Group it is in - one name for every Media Box (and Add To Media Warm Group button) of a screen. The pool keeps the
	 * one hovered or focused and those nearest it warm, so the next one starts at once. None: in no group (still warm while
	 * it plays and for a while after).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media", meta = (DisplayName = "Warm Group"))
	FName WarmGroup;

	/** Plays in the Widget Blueprint designer too, as it would in the game (silently, unless Project Settings > Media Box > Audio In Designer). Off: the poster. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Media", meta = (DisplayName = "Preview In Designer"))
	bool bPreviewInDesigner = true;

	// ---------------------------------------------------------------------------------------------
	// Playback
	// ---------------------------------------------------------------------------------------------

	/** When it plays on its own. Play, Pause and Stop work whatever is picked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (DisplayName = "Play When"))
	EMediaBoxTrigger PlayWhen = EMediaBoxTrigger::Manual;

	/**
	 * The widget whose hover or focus plays it, by name in the Widget Blueprint (or set with Set Hover Target). None: the button
	 * it is in (the nearest Button or Context Menu Button around it), else itself.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (DisplayName = "Hover Target", GetOptions = "GetHoverTargetOptions", EditCondition = "PlayWhen == EMediaBoxTrigger::Hovered || PlayWhen == EMediaBoxTrigger::Focused || PlayWhen == EMediaBoxTrigger::HoveredOrFocused"))
	FName HoverTargetName;

	/** Seconds the hover (or focus) has to last before it plays - so sweeping the pointer across a row doesn't start every clip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (DisplayName = "Hover Delay", ClampMin = "0.0", UIMax = "1.0", Units = "s", EditCondition = "PlayWhen != EMediaBoxTrigger::Manual"))
	float HoverDelay = 0.f;

	/** What happens when what started it ends: unhovered, unfocused, hidden. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (DisplayName = "After Trigger", EditCondition = "PlayWhen != EMediaBoxTrigger::Manual"))
	EMediaBoxAfterTrigger AfterTrigger = EMediaBoxAfterTrigger::StopAndRewind;

	/** Pauses while it isn't drawn - collapsed, scrolled away, on a screen not shown - and plays on when it is drawn again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (DisplayName = "Pause When Hidden"))
	bool bPauseWhenHidden = true;

	/** Playback speed: 1 as recorded, 0.5 half speed. A Separate Sound is pitched with it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", meta = (DisplayName = "Playback Rate", ClampMin = "0.05", UIMin = "0.25", UIMax = "2.0"))
	float PlaybackRate = 1.f;

	/** The state machine inputs it sets as it plays: Playing, Progress, First Frame, Looped, Ended, Error. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playback", AdvancedDisplay, meta = (DisplayName = "State Machine Inputs"))
	FMediaBoxMachineInputs MachineInputs;

	// ---------------------------------------------------------------------------------------------
	// Appearance
	// ---------------------------------------------------------------------------------------------

	/** How the video (and poster) is laid in the box: all of it (Contain), filling it (Cover), or stretched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	EMediaBoxFit Fit = EMediaBoxFit::Contain;

	/** Contain: where the video sits in the box when its shape differs - 0.5, 0.5 the middle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (DisplayName = "Alignment", EditCondition = "Fit == EMediaBoxFit::Contain"))
	FVector2D ContentAlignment = FVector2D(0.5, 0.5);

	/** Cover: which part of the video stays when it is cropped - 0.5, 0.5 the middle, 0.5, 0 its top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (DisplayName = "Crop Focus", EditCondition = "Fit == EMediaBoxFit::Cover"))
	FVector2D CropFocus = FVector2D(0.5, 0.5);

	/** Fills the whole box behind the video - the bars around a Contain fit. Transparent by default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (DisplayName = "Letterbox Color"))
	FLinearColor LetterboxColor = FLinearColor::Transparent;

	/** Multiplies the video and poster. Render Opacity and the parents' colors apply too. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FLinearColor Tint = FLinearColor::White;

	/** How big it asks to be. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (DisplayName = "Desired Size"))
	FVector2D DesiredSize = FVector2D(320.0, 180.0);

	/** Seconds the video takes to fade in over the poster. Below 0: Project Settings > Media Box > Video Fade In. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", AdvancedDisplay, meta = (DisplayName = "Video Fade In", UIMin = "-1.0", UIMax = "1.0", Units = "s"))
	float VideoFadeIn = -1.f;

	/** Seconds the video takes to fade back to the poster as it stops. Below 0: Project Settings > Media Box > Video Fade Out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", AdvancedDisplay, meta = (DisplayName = "Video Fade Out", UIMin = "-1.0", UIMax = "1.0", Units = "s"))
	float VideoFadeOut = -1.f;

	/** Shown over the poster when the video can't be played (On Error says why). None: just the poster. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (DisplayName = "Error Image"))
	FSlateBrush ErrorImage;

	// ---------------------------------------------------------------------------------------------
	// Audio
	// ---------------------------------------------------------------------------------------------

	/** Its audio's volume, times the clip's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (DisplayName = "Volume", ClampMin = "0.0", UIMax = "2.0"))
	float AudioVolume = 1.f;

	/** Plays without audio. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (DisplayName = "Muted"))
	bool bMuted = false;

	// ---------------------------------------------------------------------------------------------
	// Subtitles
	// ---------------------------------------------------------------------------------------------

	/** Draws the clip's subtitles at the bottom of the box, while subtitles are on (Set Media Box Subtitles Enabled). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles", meta = (DisplayName = "Show Subtitles"))
	bool bShowSubtitles = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles", meta = (DisplayName = "Font", EditCondition = "bShowSubtitles"))
	FSlateFontInfo SubtitleFont;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles", meta = (DisplayName = "Text Color", EditCondition = "bShowSubtitles"))
	FLinearColor SubtitleColor = FLinearColor::White;

	/** Behind each line. Transparent for none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles", meta = (DisplayName = "Background", EditCondition = "bShowSubtitles"))
	FLinearColor SubtitleBackground = FLinearColor(0.f, 0.f, 0.f, 0.6f);

	/** Space from the bottom of the box to the last line, and around each line's text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Subtitles", meta = (DisplayName = "Margin", EditCondition = "bShowSubtitles"))
	FVector2D SubtitleMargin = FVector2D(6.0, 8.0);

	// ---------------------------------------------------------------------------------------------
	// Events
	// ---------------------------------------------------------------------------------------------

	/** Its player has opened the video. */
	UPROPERTY(BlueprintAssignable, Category = "Media Box|Events")
	FOnMediaBoxEvent OnOpened;

	/** The video's first frame is showing (fading in over the poster). */
	UPROPERTY(BlueprintAssignable, Category = "Media Box|Events")
	FOnMediaBoxEvent OnFirstFrame;

	/** It looped back to Loop Start. */
	UPROPERTY(BlueprintAssignable, Category = "Media Box|Events")
	FOnMediaBoxEvent OnLooped;

	/** It reached its end (a clip that doesn't loop). */
	UPROPERTY(BlueprintAssignable, Category = "Media Box|Events")
	FOnMediaBoxEvent OnEnded;

	/** It couldn't open or play the video - why, in Reason. The poster (and Error Image) stay. */
	UPROPERTY(BlueprintAssignable, Category = "Media Box|Events")
	FOnMediaBoxError OnError;

	/** The subtitle line changed - empty when none shows. Fired whether or not Show Subtitles draws them. */
	UPROPERTY(BlueprintAssignable, Category = "Media Box|Events")
	FOnMediaBoxSubtitle OnSubtitleChanged;

	/** Its status changed: loading, playing, paused, stopped, ended, error. */
	UPROPERTY(BlueprintAssignable, Category = "Media Box|Events")
	FOnMediaBoxStatus OnStatusChanged;

	// ---------------------------------------------------------------------------------------------
	// Blueprint
	// ---------------------------------------------------------------------------------------------

	/** Plays (from where it is, or from the start once stopped). Plays until Pause or Stop, whatever Play When says. */
	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void Play();

	/** Pauses where it is. */
	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void Pause();

	/** Stops: fades back to the poster and goes back to the start - warm, so the next Play is at once. */
	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void Stop();

	/** Play when paused or stopped, Pause when playing. */
	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void TogglePlayPause();

	/** Jumps to Seconds into the video. */
	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void Seek(float Seconds);

	/**
	 * Plays another clip: what plays now stops at once and NewClip's poster shows; bPlayNow plays it (at once when warm). For one
	 * shared preview that hovered buttons change - call it from each button's On Hovered, and Stop from On Unhovered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void SetClip(UMediaBoxClip* NewClip, bool bPlayNow = true);

	UFUNCTION(BlueprintPure, Category = "Media Box")
	UMediaBoxClip* GetClip() const { return MediaClip; }

	/** The widget whose hover or focus plays it - over Hover Target's name. None: back to Hover Target. */
	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void SetHoverTarget(UWidget* NewHoverTarget);

	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void SetPlaybackRate(float NewRate);

	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void SetVolume(float NewVolume);

	UFUNCTION(BlueprintCallable, Category = "Media Box")
	void SetMuted(bool bNewMuted);

	UFUNCTION(BlueprintPure, Category = "Media Box")
	EMediaBoxStatus GetStatus() const { return Status; }

	/** Whether the video is playing (not loading, paused or stopped). */
	UFUNCTION(BlueprintPure, Category = "Media Box")
	bool IsPlaying() const;

	/** Seconds into the video. 0 while stopped. */
	UFUNCTION(BlueprintPure, Category = "Media Box")
	float GetTime() const;

	/** The video's length in seconds. 0 until it has been opened. */
	UFUNCTION(BlueprintPure, Category = "Media Box")
	float GetDuration() const;

	/** How far through the video, 0 - 1. */
	UFUNCTION(BlueprintPure, Category = "Media Box")
	float GetProgress() const;

	/** The subtitle line showing now. Empty when none. */
	UFUNCTION(BlueprintPure, Category = "Media Box")
	FText GetCurrentSubtitle() const { return CurrentSubtitle; }

	/** The Media Player it plays through now - a pooled one, only while it plays. None when stopped. Don't keep it. */
	UFUNCTION(BlueprintPure, Category = "Media Box")
	UMediaPlayer* GetMediaPlayer() const;

	/** The Media Texture its video is in now - for a material of your own. None when stopped. Don't keep it. */
	UFUNCTION(BlueprintPure, Category = "Media Box")
	UMediaTexture* GetMediaTexture() const;

	// ---------------------------------------------------------------------------------------------

	/** Advances it: triggers, fades, events. Called by its pool every frame. */
	void TickMediaBox(float DeltaTime);

	//~ UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void ValidateCompiledDefaults(class IWidgetCompilerLog& CompileLog) const override;
#endif

protected:
	//~ UWidget
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UFUNCTION()
	TArray<FString> GetHoverTargetOptions() const;

private:

	friend class SMediaBox;

	/** Whether it plays here: always in a game; in the designer with Preview In Designer. */
	bool CanPlayHere() const;

	UMediaBoxPool* GetPool() const;

	/** Leases a player when it has none and plays it - an ended clip from its start only when bRestartIfEnded (a new hover, Play). */
	void StartPlayback(bool bRestartIfEnded);
	void BeginStop(bool bFade);
	void ReleasePlayer(bool bRewind);
	void UpdateStatus();
	void UpdateBrushes();
	void UpdateGroupMembership();
	void UpdateSubtitle();
	void UpdateMachineInputs(bool bForce);
	void FireMachineTrigger(FName Input) const;

	UWidget* ResolveHoverTarget() const;
	bool IsTriggerHeld();
	bool IsHiddenNow();

	float GetFadeIn() const;
	float GetFadeOut() const;
	float GetEffectiveVolume() const;

	/** Called by SMediaBox as it paints: when it was last drawn. */
	void NotifyPainted() { LastPaintFrame = GFrameCounter; }

	TSharedPtr<SMediaBox> MyMediaBox;

	UPROPERTY(Transient)
	TObjectPtr<UMediaBoxPlayer> LeasedPlayer = nullptr;

	UPROPERTY(Transient)
	FSlateBrush PosterBrush;

	UPROPERTY(Transient)
	FSlateBrush VideoBrush;

	TWeakObjectPtr<UWidget> HoverTargetOverride;
	mutable TWeakObjectPtr<UWidget> CachedHoverTarget;
	mutable FName CachedHoverTargetName;

	TWeakObjectPtr<UDiegeticScreenComponent> DiegeticScreen;
	double NextScreenLookup = 0.0;

	/** The group and target it last joined, so it can leave them again. */
	FName JoinedGroup;
	TWeakObjectPtr<UWidget> JoinedTarget;

	/** The pool it registered with. */
	TWeakObjectPtr<UMediaBoxPool> RegisteredPool;

	EMediaBoxStatus Status = EMediaBoxStatus::Empty;
	FText CurrentSubtitle;
	int32 SubtitleIndex = INDEX_NONE;

	/** 0 the poster, 1 the video. */
	float VideoAlpha = 0.f;

	uint64 LastPaintFrame = 0;

	/** Play was called (or a trigger that ends in Keep Playing): plays whatever the trigger says. */
	bool bPlayRequested = false;
	/** Pause was called while a trigger holds it: stays paused until the trigger starts again. */
	bool bHeldPaused = false;
	bool bTriggerActive = false;
	bool bTriggerHeldLastFrame = false;
	float TriggerHeldFor = 0.f;
	bool bPausedByHidden = false;
	bool bStopping = false;

	/** Seconds to seek to once it has a player (Seek while stopped). Below 0: none. */
	float PendingSeek = -1.f;

	/** Events already sent for this lease. */
	bool bSentOpened = false;
	bool bSentFirstFrame = false;
	bool bSentError = false;
	int32 SeenLoops = 0;
	int32 SeenEnds = 0;

	/** Last values given to the state machine inputs. */
	bool bMachinePlaying = false;
	float MachineProgress = -1.f;
};
