// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Extensions/UIComponent.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/ObjectKey.h"
#include "Blueprint/UserWidget.h"
#include "WidgetAnimLibrary.h"
#include "WidgetAnimPreset.h"
#include "WidgetAnimationComponent.generated.h"

class SWidget;
class FActiveTimerHandle;
class UWidgetAnimationComponent;

// =====================================================================================================
// Versioning (UUserWidget_Animatable migration)
// =====================================================================================================

/**
 * Stamped into packages saved by UUserWidget_Animatable. Before this version Anim In / Anim Out and
 * their flags were properties of the widget itself; UUserWidget_Animatable::PostLoad moves them into
 * its component once.
 */
struct NAMETBD_API FWidgetAnimationComponentVersion
{
	enum Type : int32
	{
		BeforeCustomVersionWasAdded = 0,

		/** Anim In / Anim Out and their flags live on UWidgetAnimationComponent instead of the widget. */
		MovedIntoComponent,

		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static const FGuid GUID;

	FWidgetAnimationComponentVersion() = delete;
};

/** Everything a widget authored about its animations before they moved into the component. */
struct NAMETBD_API FWidgetAnimationLegacySettings
{
	bool bPlayOnlyOnConstructAndDestruct = false;
	bool bPlayInAnimationsOnConstruct = true;
	bool bPlayAnimationsOnVisibilityHidden = false;
	TArray<FWidgetAnimEntry> AnimIn;
	TArray<FWidgetAnimEntry> AnimOut;
	bool bPreserveAuthoredTransforms = false;
	FWidgetAnimPreviewSettings DesignerPreview;
};

/**
 * How the designer toolbar plays a selected widget's animations, in place of its entries' own flags: the same
 * four choices an entry makes about one use of an animation. Editor-only in use; plain data.
 */
struct NAMETBD_API FWidgetAnimDesignerPlayback
{
	bool bLoop = false;
	bool bPingPong = true;
	bool bInvert = false;
	float Speed = 1.f;
};

/**
 * When a component's Additive Anim Layers start: right away, with Anim In or Anim Out (unset) - or within one of its
 * entries, as part of it: on its clock, from its start, whenever it plays - its own Play In, a trigger, a Play By ID
 * of its Anim ID - with its Loop, Ping Pong, Invert and Playback Speed. Set from the component's inline track editor.
 *
 * The entry is remembered by where it is authored - which list, and its place there - and by its animation, so it is
 * found again after the list is reordered. An entry that can no longer be found - removed, or its animation swapped -
 * leaves the layers starting right away again.
 */
USTRUCT()
struct NAMETBD_API FWidgetAnimAdditiveStart
{
	GENERATED_BODY()

	/** The entry is in Anim Out (true) or Anim In. One from the other list plays in this list's direction by Mirror In Out. */
	UPROPERTY()
	bool bInAnimOut = false;

	/** Its place in that list when it was picked. */
	UPROPERTY()
	int32 EntryIndex = INDEX_NONE;

	/** Its animation: what tells it apart, and finds it again at another place. */
	UPROPERTY()
	TObjectPtr<UWidgetAnimPresetAsset> Animation = nullptr;

	bool IsSet() const { return EntryIndex != INDEX_NONE && Animation != nullptr; }
	void Reset() { *this = FWidgetAnimAdditiveStart(); }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWidgetAnimationComponentEvent, UWidgetAnimationComponent*, Component);
DECLARE_MULTICAST_DELEGATE_OneParam(FWidgetAnimationComponentNativeEvent, UWidgetAnimationComponent* /*Component*/);

// =====================================================================================================
// Component
// =====================================================================================================

/**
 * Anim In / Anim Out for any widget, added from the UMG designer's Add Component button.
 *
 * Select a widget in the hierarchy (a Border, a Button, a nested User Widget - anything), press Add
 * Component, pick Widget Animation Component. The component then plays its Anim In when the User Widget
 * it lives in is constructed, and everything else UUserWidget_Animatable used to do: mirroring, the
 * track player, Play In / Play Out with the cascade into nested components, play-by-ID, authored
 * transform baselines, extra layers handed over at runtime, and a designer preview.
 *
 * The widget the component is added to is the one it animates. An entry with no Target Widget animates
 * it; a Target Widget path is looked up in the same Widget Blueprint the component was added in.
 *
 * Things that are different from a widget-based host, because a UI Component is not a widget:
 *  - It keeps its own clock (the core ticker) instead of riding on NativeTick, so it animates whether or
 *    not the owning widget ticks, and poses are applied before Slate lays out the frame.
 *  - It cannot intercept SetVisibility on its owner. With Play Animations On Visibility Hidden on,
 *    becoming visible still plays Anim In automatically; hiding WITH an exit animation goes through
 *    Set Visibility Animated. (UUserWidget_Animatable routes its own SetVisibility there, so it keeps
 *    the old behaviour.)
 *
 * Code can also create one and attach it by hand with BindToWidget - UInfoCardWidget and
 * UUserWidget_Animatable both do - in which case the code that bound it also calls
 * NotifyOwnerConstructed / NotifyOwnerDestructed.
 */
UCLASS(DefaultToInstanced, EditInlineNew, BlueprintType, meta = (DisplayName = "Widget Animation Component"))
class NAMETBD_API UWidgetAnimationComponent : public UUIComponent
{
	GENERATED_BODY()

public:

	// -------------------------------------------------------------------------------------------------
	// Authored settings
	// -------------------------------------------------------------------------------------------------

	/**
	 * Plays every Anim In entry on construct and ignores visibility, whatever each entry's own Play In Animation On
	 * Construct and Play Animation On Visibility Hidden say.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bPlayOnlyOnConstructAndDestruct = false;

	/** Moved onto each Anim In entry as Play In Animation On Construct. Read once on load, then cleared into the entries. */
	UPROPERTY()
	bool bPlayInAnimationsOnConstruct_DEPRECATED = true;

	/** Moved onto each entry as Play Animation On Visibility Hidden. Read once on load, then cleared into the entries. */
	UPROPERTY()
	bool bPlayAnimationsOnVisibilityHidden_DEPRECATED = false;

	/**
	 * The animations this widget plays as it appears.
	 *
	 * Each entry points at a Widget Animation asset - which holds the layers, the timing and the curves -
	 * and decides the handful of things that are about THIS use of it: whether it loops, whether it also
	 * plays backwards as the exit, whether it runs inverted, and which widget it moves.
	 *
	 * Give an entry an Anim ID to fire it from Blueprint later with Play Widget Animations By Id.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Anim In", TitleProperty = "Animation"))
	TArray<FWidgetAnimEntry> AnimIn;

	/** The animations this widget plays as it closes. Anything in Anim In marked Mirror In Out is included automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Anim Out", TitleProperty = "Animation"))
	TArray<FWidgetAnimEntry> AnimOut;

	/**
	 * Plain animation layers played on top of Anim In, every time it plays.
	 *
	 * For a touch that belongs to this one widget and isn't worth a Widget Animation asset of its own. The
	 * layers animate the widget this component is on, have no Anim ID (so Play Widget Animations By Id only
	 * includes them when it is asked to play everything), and are not mirrored into Anim Out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Additive Anim Layers In", TitleProperty = "AnimType"))
	TArray<FWidgetAnimLayer> AdditiveAnimLayersIn;

	/** Plain animation layers played on top of Anim Out, every time it plays. See Additive Anim Layers In. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Additive Anim Layers Out", TitleProperty = "AnimType"))
	TArray<FWidgetAnimLayer> AdditiveAnimLayersOut;

	/** When Additive Anim Layers In start: right away with Anim In, or within one of its entries. See FWidgetAnimAdditiveStart. */
	UPROPERTY()
	FWidgetAnimAdditiveStart AdditiveAnimLayersInStart;

	/** When Additive Anim Layers Out start: right away with Anim Out, or within one of its entries. See FWidgetAnimAdditiveStart. */
	UPROPERTY()
	FWidgetAnimAdditiveStart AdditiveAnimLayersOutStart;

	/**
	 * Every Value Name the value-driven tracks in this component's animations use (Timing = Value in the Widget
	 * Animations editor), one row each, listed for you: set them with Set Widget Animation Value. Tick a row to give
	 * this widget its own range, start value and limits for it, in place of the tracks' own. How the tracks travel to
	 * a value - their smoothing - is part of the animation and stays there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation", meta = (DisplayName = "Value Tracks", EditFixedSize, TitleProperty = "ValueName"))
	TArray<FWidgetAnimValueOverride> ValueOverrides;

	/**
	 * Whether animations compose over whatever each target was authored as, instead of replacing it.
	 *
	 * Off (the default): a Fade ending at 1 means opacity 1, full stop, even on a widget authored at 0.5.
	 * On: opacity and scale multiply, translation and angle add - so that same Fade settles at 0.5, and a
	 * Slide returns to wherever the widget was nudged to rather than to zero.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Preserve Authored Transforms"))
	bool bPreserveAuthoredTransforms = false;

	/**
	 * Plays these animations in the UMG designer, so they can be judged without compiling and running.
	 * Nothing here is saved into the widget's authored state, and it has no effect at runtime.
	 *
	 * Requires Unreal Engine 5.6 or newer: older UI Components are never told they are in the designer.
	 */
	UPROPERTY(EditAnywhere, Category = "Animation|Designer Preview", meta = (DisplayName = "Designer Preview", ShowOnlyInnerProperties))
	FWidgetAnimPreviewSettings DesignerPreview;

	/** Fired when the exit animation finishes, after any visibility deferred by Set Visibility Animated has been applied. */
	UPROPERTY(BlueprintAssignable, Category = "Animation")
	FWidgetAnimationComponentEvent OnOutAnimationFinished;

	/** Native twin of OnOutAnimationFinished, broadcast just after it. */
	FWidgetAnimationComponentNativeEvent OnOutAnimationFinishedNative;

	// -------------------------------------------------------------------------------------------------
	// Playback
	// -------------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void PlayIn(bool bCascadeToChildren = true);

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void PlayOut(bool bCascadeToChildren = true);

	UFUNCTION(BlueprintPure, Category = "Animation")
	bool IsAnimClosing() const;

	UFUNCTION(BlueprintPure, Category = "Animation")
	EWidgetAnimPhase GetAnimPhase() const { return AnimPlayer.Phase; }

	/**
	 * True if any track going in this direction is still running. An endlessly looping track counts as
	 * running until its loop is ended, even after the rest of its direction has landed.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation")
	bool IsPlayingInDirection(bool bIsOut) const;

	/**
	 * Rebuilds the stacks the component plays from Anim In / Anim Out and the additive layers. Call it after
	 * changing any of those at runtime.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void RebuildEffectiveAnimations();

	/**
	 * Extra layers merged on top of Anim In / Anim Out (after the additive layers), for callers that build
	 * FWidgetAnimSettings themselves - the info card additive animations being the case this exists for.
	 * A null Target Widget on a layer animates the component's owner. Replaces whatever was set before.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetExtraAnimSettings(const TArray<FWidgetAnimSettings>& InExtraAnimIn, const TArray<FWidgetAnimSettings>& InExtraAnimOut);

	/**
	 * Sets the owner's visibility, with animation: hiding plays Anim Out first and applies the
	 * visibility when it finishes; showing applies it immediately and plays Anim In. Without Play
	 * Animations On Visibility Hidden this is a plain SetVisibility.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetVisibilityAnimated(ESlateVisibility InVisibility);

	/**
	 * Advances every running animation by DeltaTime and applies the result. Returns true on the frame the
	 * exit animation finishes. The component already calls this itself every frame while its owner is
	 * constructed; only call it for a component you are clocking by hand.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	bool TickAnimationPlayback(float DeltaTime);

	// -------------------------------------------------------------------------------------------------
	// Play by ID
	// -------------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Animation", meta = (AutoCreateRefTerm = "AnimIds"))
	void PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse = false, float PlaybackSpeed = 1.f, bool bHoldFinalPose = true, bool bRestartIfPlaying = true);

	UFUNCTION(BlueprintCallable, Category = "Animation", meta = (AutoCreateRefTerm = "AnimIds"))
	void StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart = false);

	UFUNCTION(BlueprintPure, Category = "Animation", meta = (AutoCreateRefTerm = "AnimIds"))
	bool AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const;

	/**
	 * Ends the looping of the running animation with this Anim ID, in either direction.
	 *
	 * Soft Settle on: the animation finishes the loop it is in, then stops on its end pose. A ping-pong
	 * loop caught on its way back plays forward once more so it settles rather than snapping.
	 * Soft Settle off: the animation ends immediately, jumping to its end pose.
	 *
	 * Every running entry with this ID is ended together, the same way they start together. Does nothing
	 * if AnimId is None, is not playing, or is conflicting (two entries with this ID on the same widget in
	 * the same direction) - the last case also puts an error on screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void EndLoopingAnimation(FName AnimId, bool bSoftSettle = true);

	/**
	 * Anim IDs used by more than one entry on the same widget going the same direction (mirrored copies
	 * included), as of the last Rebuild Effective Animations. Sharing an ID across different widgets is
	 * intended and is not listed here.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation")
	TArray<FName> GetConflictingAnimationIds() const { return ConflictingAnimIds; }

	// -------------------------------------------------------------------------------------------------
	// Value-driven tracks
	// -------------------------------------------------------------------------------------------------

	/**
	 * Sets the value of the value-driven tracks with this Value Name (Timing = Value in the Widget Animations editor).
	 * Each track maps it onto itself through its Value At Start and Value At End - 0 and 1 by default, so 0.5 is
	 * halfway along - and travels there by its own smoothing. Call it as often as the value changes.
	 *
	 * Snap jumps straight to the value, and may go back even where a track never goes backwards - for resetting. Start
	 * If Not Playing starts the Anim In entries that have such a track but haven't played yet, so no Play call is needed
	 * first; an entry already playing is never restarted. Set before the widget is constructed, the value is where the
	 * tracks start. Returns whether a running track took the value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Animation|Value", meta = (AdvancedDisplay = "bSnap,bStartIfNotPlaying"))
	bool SetWidgetAnimationValue(FName ValueName, float Value, bool bSnap = false, bool bStartIfNotPlaying = true);

	/** Set Widget Animation Value, for a track whose Value Type is Integer. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Value", meta = (DisplayName = "Set Widget Animation Value (Integer)", AdvancedDisplay = "bSnap,bStartIfNotPlaying"))
	bool SetWidgetAnimationValueInteger(FName ValueName, int32 Value, bool bSnap = false, bool bStartIfNotPlaying = true);

	/** Set Widget Animation Value, for a track whose Value Type is Vector 2D: X and Y each move their own axis. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Value", meta = (DisplayName = "Set Widget Animation Value (Vector 2D)", AdvancedDisplay = "bSnap,bStartIfNotPlaying"))
	bool SetWidgetAnimationValueVector2D(FName ValueName, FVector2D Value, bool bSnap = false, bool bStartIfNotPlaying = true);

	/** Set Widget Animation Value, for a track whose Value Type is Colour: R, G, B and A each move their own channel. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Value", meta = (DisplayName = "Set Widget Animation Value (Colour)", AdvancedDisplay = "bSnap,bStartIfNotPlaying"))
	bool SetWidgetAnimationValueColor(FName ValueName, FLinearColor Value, bool bSnap = false, bool bStartIfNotPlaying = true);

	/** Any of the four, with the value raw - up to four channels. See WidgetAnimValue::FromFloat and the others. */
	bool SetWidgetAnimationValues(FName ValueName, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying);

	/**
	 * The value the first value-driven track with this Value Name shows now - or, with Target Value, the one it is
	 * heading to (the last one set) - in the value's own units. 0 when nothing has this name and no value was set.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation|Value")
	float GetWidgetAnimationValue(FName ValueName, bool bTargetValue = false) const;

	UFUNCTION(BlueprintPure, Category = "Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Integer)"))
	int32 GetWidgetAnimationValueInteger(FName ValueName, bool bTargetValue = false) const;

	UFUNCTION(BlueprintPure, Category = "Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Vector 2D)"))
	FVector2D GetWidgetAnimationValueVector2D(FName ValueName, bool bTargetValue = false) const;

	UFUNCTION(BlueprintPure, Category = "Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Colour)"))
	FLinearColor GetWidgetAnimationValueColor(FName ValueName, bool bTargetValue = false) const;

	/** The raw value behind the four getters. */
	FVector4 GetWidgetAnimationValues(FName ValueName, bool bTargetValue) const;

	/**
	 * Lists every Value Name the component's animations and additive layers use in Value Tracks - one row each, in the
	 * order first found - keeping the rows already overridden and dropping names no longer used. Rows not overridden
	 * show their tracks' settings. Run on load and on every edit. True if anything changed.
	 */
	bool RefreshValueOverrides();

	/**
	 * True while Play Out is waiting for a value-driven track - here, or in a component it cascades into - to arrive
	 * before the exit starts (its On Exit is Wait For Value or Complete First). The phase is already Playing Out.
	 */
	UFUNCTION(BlueprintPure, Category = "Animation")
	bool IsExitWaitingForValues() const { return PendingExit != EPendingExit::None; }

	/**
	 * Whether Play Out would move anything here: an exit to play, or a value-driven track it would wait for first.
	 * What a screen closing asks before deciding it has an exit to wait for.
	 */
	bool HasExitToPlay() const;

	// -------------------------------------------------------------------------------------------------
	// Owner and lookup
	// -------------------------------------------------------------------------------------------------

	/** The widget this component animates. */
	UFUNCTION(BlueprintPure, Category = "Animation")
	UWidget* GetAnimatedWidget() const { return AnimOwner.Get(); }

	/** The Widget Animation Component on Widget, or null. Works for designer-added and code-bound components alike. */
	UFUNCTION(BlueprintPure, Category = "Animation")
	static UWidgetAnimationComponent* FindWidgetAnimationComponent(const UWidget* Widget);

	/**
	 * Calls Visit for the nearest components below Widget (Widget's own component is not included): a branch that
	 * has its own component is handed to it and not walked further, because that component cascades into its own
	 * children in turn. Steps into nested User Widgets. The same walk Play In / Play Out cascade along, so code that
	 * drives a whole widget from outside - a screen transition waiting on every exit - sees the components the
	 * cascade reaches.
	 */
	static void ForEachNearestNestedComponent(UWidget* Widget, TFunctionRef<void(UWidgetAnimationComponent*)> Visit);

	/**
	 * Attaches this component to InWidget by hand, for code that creates the component itself rather than
	 * having it added in the designer. Paths are then resolved inside InWidget if it is a User Widget.
	 * The caller also becomes responsible for NotifyOwnerConstructed / NotifyOwnerDestructed.
	 */
	void BindToWidget(UWidget* InWidget);

	/** Owner lifecycle. Both are idempotent, so being told twice (designer system and host code) is harmless. */
	void NotifyOwnerConstructed();
	void NotifyOwnerDestructed();

	/** At design time, (re)starts the designer preview from the widget's authored pose. No-op at runtime. */
	void NotifyOwnerDesignTimeRefresh();

	/** Registers the designer preview's active timer if the preview needs frames and it isn't registered yet. */
	void EnsureDesignerPreviewTimer();

	/**
	 * For the designer toolbar shown when an animated widget is selected: plays this component's animations in
	 * the designer - the entrance, or the exit from the settled pose - with the toolbar's Loop, Ping Pong,
	 * Invert and Speed in place of its entries' own, whatever its Designer Preview is set to. Lasts until
	 * Reset, or until the designer rebuilds the widget and the preview is Designer Preview's again.
	 */
	void PlayDesignerPreviewOverride(bool bOut, const FWidgetAnimDesignerPlayback& Options);

	/** Freezes the toolbar's preview at Alpha of one pass - of the exit if the last play was Play Out. */
	void ScrubDesignerPreviewOverride(float Alpha, const FWidgetAnimDesignerPlayback& Options);

	/**
	 * The toolbar's Loop / Ping Pong / Invert / Speed changed. Applied to what is showing where it stands - no
	 * restart, so flipping Invert or dragging Speed keeps the preview where it is. Nothing to do while the
	 * toolbar isn't driving this component.
	 */
	void UpdateDesignerPreviewOptions(const FWidgetAnimDesignerPlayback& Options);

	/** Puts the widget back as authored and holds it there. The toolbar's Reset. */
	void ResetDesignerPreviewOverride();

	/** The toolbar's readouts: where the preview is in one pass, in seconds and as a fraction, and how long a pass is. */
	float GetDesignerPreviewSeconds() const;
	float GetDesignerPreviewDuration() const;
	float GetDesignerPreviewAlpha() const;

	/** Whether the toolbar's preview is running - Play pressed, not yet finished, frozen or reset. */
	bool IsDesignerPreviewPlaying() const;

	/** Whether there is anything for the designer toolbar to play. */
	bool HasDesignerPreviewAnimations() const { return FlattenedIn.Num() > 0 || FlattenedOut.Num() > 0; }

	/** True while this component is applying a visibility itself. A SetVisibility override that routes into the component must pass the call straight through while this is set. */
	bool IsApplyingVisibility() const { return bApplyingVisibility; }

	/** The widget an entry's Target Widget path refers to, or null to mean "the animated widget". */
	UWidget* ResolveAnimTarget(FName TargetWidgetName) const;

	// -------------------------------------------------------------------------------------------------
	// When the additive layers start
	// -------------------------------------------------------------------------------------------------

	/**
	 * The entry an Additive Anim Layers list starts within - bOutList picks Out or In - as its place in Anim Out
	 * (bOutInAnimOut) or Anim In. INDEX_NONE when the list starts right away, or its entry can't be found.
	 */
	int32 ResolveAdditiveStart(bool bOutList, bool& bOutInAnimOut) const;

	/**
	 * Whether an authored entry can host an Additive Anim Layers list: it has an animation, and plays in the list's
	 * direction - in its own list, or mirrored into it by Mirror In Out.
	 */
	bool CanHostAdditiveList(bool bOutList, bool bInAnimOut, int32 EntryIndex) const;

	/** Points each list's start at its entry's current place, and clears one whose entry is gone. True if either changed. */
	bool RefreshAdditiveStarts();

	/** Backs the Target Widget dropdown on every entry. Must stay a UFUNCTION for GetOptions. */
	UFUNCTION()
	TArray<FString> GetAnimTargetWidgetOptions() const;

	const TArray<FWidgetAnimSettings>& GetFlattenedAnimIn() const { return FlattenedIn; }
	const TArray<FWidgetAnimSettings>& GetFlattenedAnimOut() const { return FlattenedOut; }

	// -------------------------------------------------------------------------------------------------
	// Migration
	// -------------------------------------------------------------------------------------------------

	/**
	 * Takes settings that were saved on the owning widget before they moved here. With ArchetypeLegacy
	 * null everything is taken; with it set, only fields that differ from it are.
	 */
	void ImportLegacySettings(const FWidgetAnimationLegacySettings& Legacy, const FWidgetAnimationLegacySettings* ArchetypeLegacy);

	/** Whether an owner's legacy fields should be compared against Archetype's (see ImportLegacySettings). */
	static bool ShouldDiffLegacyAgainstArchetype(const UObject* Archetype);

	virtual void BeginDestroy() override;

	/** Brings every entry's Target Widgets in line with its animation's Layer Groups - see SyncEntryGroupTargets. */
	virtual void PostLoad() override;

#if WITH_EDITOR
	/** Keeps every Transform layer's Keys in step with its curve, and every entry's Target Widgets in step with its animation's groups. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	 * Sizes and labels each Anim In / Anim Out entry's Target Widgets to its animation's Layer Groups. Run on
	 * load and on every edit, so the Details panel lists exactly the groups the picked animation has; the
	 * built stacks never depend on it - an entry that is behind resolves missing groups to their defaults.
	 */
	bool SyncEntryGroupTargets();

protected:

	// UUIComponent
#if UE_VERSION_OLDER_THAN(5, 6, 0)
	virtual bool OnInitialize() override;
#else
	virtual void OnInitialize() override;
	virtual void OnPreConstruct(bool bIsDesignTime) override;
#endif
	virtual void OnConstruct() override;
	virtual void OnDestruct() override;

private:

	/** The widget being animated. Set from the UI Component owner, or by BindToWidget. */
	TWeakObjectPtr<UWidget> AnimOwner;

	/** True when BindToWidget attached this rather than the designer's component system. */
	bool bBoundByCode = false;

	bool bOwnerConstructed = false;
	bool bApplyingVisibility = false;
	bool bLastSeenVisible = false;

	UPROPERTY(Transient)
	TArray<FWidgetAnimEntry> EffectiveInEntries;

	UPROPERTY(Transient)
	TArray<FWidgetAnimEntry> EffectiveOutEntries;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> FlattenedIn;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> FlattenedOut;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> ExtraAnimIn;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> ExtraAnimOut;

	/** Additive Anim Layers followed by the extra layers: everything without an Anim ID, played as one track. */
	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> UnnamedAnimIn;

	/** An Additive Anim Layers list that starts within an entry, placed, and that entry's place among the effective entries. */
	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> LinkedAdditiveIn;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> LinkedAdditiveOut;

	int32 LinkedEffectiveIn = INDEX_NONE;
	int32 LinkedEffectiveOut = INDEX_NONE;

	/** Where an authored entry is among the effective entries of a direction - see AppendEntriesWithMirroring. */
	int32 ToEffectiveIndex(bool bOutDirection, bool bInAnimOut, int32 EntryIndex) const;

	/**
	 * Appends the Additive Anim Layers list that starts within effective entry EffectiveIndex - if one does - to that
	 * entry's settings, EntrySettings from First on: stamped with the entry's own per-use flags and its clock, so it
	 * plays as part of it.
	 */
	void AppendLinkedAdditive(int32 EffectiveIndex, bool bOut, TArray<FWidgetAnimSettings>& EntrySettings, int32 First) const;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> UnnamedAnimOut;

	UPROPERTY(Transient)
	FWidgetAnimPlayer AnimPlayer;

	UPROPERTY(Transient)
	bool bIsDeferringVisibility = false;

	UPROPERTY(Transient)
	ESlateVisibility DeferredVisibility = ESlateVisibility::Hidden;

	/** Authored pose of every animated widget. Captured at design time always, at runtime only for Preserve Authored Transforms. */
	UPROPERTY(Transient)
	FWidgetAnimBaselineSet AnimBaselines;

	/**
	 * The authored pose again, captured at runtime when Preserve Authored Transforms is off and AnimBaselines
	 * therefore isn't. Only for putting widgets back - Stop with Reset To Start - never composed over, so animations
	 * still replace the authored pose exactly as that setting says.
	 */
	UPROPERTY(Transient)
	FWidgetAnimBaselineSet ResetPoseBaselines;

	/** Whether any Anim In or Anim Out entry plays on a visibility change - what the visibility watch is for. */
	bool bAnyVisibilityTrigger = false;

	/**
	 * An exit asked for while a value-driven animation still had a way to go, waiting for it to arrive - see
	 * FWidgetAnimValueOptions::OnExit. Which exit it was, and whether it cascades; started from TickAnimationPlayback
	 * once every value it waits on has arrived. Cleared by anything that plays the entrance instead.
	 */
	enum class EPendingExit : uint8
	{
		None,
		PlayOut,
		VisibilityTrigger
	};

	EPendingExit PendingExit = EPendingExit::None;
	bool bPendingExitCascade = true;

	/** Set while a waiting exit is being started, so it doesn't wait again. */
	bool bStartingPendingExit = false;

	/**
	 * Sends this component's value-driven animations - and, with bCascade, those of every component the exit cascades
	 * into - their exit values (Complete First goes to 1), and answers whether any still has a way to go.
	 */
	bool PrepareValuesForExit(bool bCascade);

	/** Starts the waiting exit if every value it waits on has arrived. True once it has started. */
	bool TryStartPendingExit();

	/** Puts this widget's Value Tracks overrides onto the value-driven layers in Settings. */
	void ApplyValueOverrides(TArray<FWidgetAnimSettings>& Settings) const;

	/**
	 * What every call starting an entry does to that entry's settings before its track starts: the Additive Anim Layers
	 * list that starts within it, and this widget's value overrides.
	 */
	void PrepareEntrySettings(int32 EffectiveIndex, bool bOut, TArray<FWidgetAnimSettings>& EntrySettings) const;

	UPROPERTY(Transient)
	FWidgetAnimPreviewState DesignerPreviewState;

	/** The designer toolbar's mode, while it is overriding Designer Preview. Never saved. */
	TOptional<EWidgetAnimPreviewMode> DesignerPreviewModeOverride;

	/** The toolbar's playback options, and where its scrub stands - which stack, and how far along. */
	FWidgetAnimDesignerPlayback DesignerPreviewOptions;
	float DesignerPreviewScrubAlpha = 0.f;
	bool bDesignerPreviewScrubOut = false;

	/** Anim In / Anim Out with the toolbar's options stamped on - what plays while the toolbar is driving. */
	TArray<FWidgetAnimSettings> DesignerOverrideIn;
	TArray<FWidgetAnimSettings> DesignerOverrideOut;

	/** Designer Preview, with the toolbar's mode, speed and scrub in place of its own while it is driving. */
	FWidgetAnimPreviewSettings GetEffectiveDesignerPreview() const;

	/** Restamps DesignerOverrideIn/Out from the built stacks and DesignerPreviewOptions. */
	void RebuildDesignerOverrideStacks();

	/** The stacks the designer preview plays: the toolbar's stamped copies while it is driving, else the built ones. */
	const TArray<FWidgetAnimSettings>& GetDesignerPreviewIn() const { return DesignerPreviewModeOverride.IsSet() ? DesignerOverrideIn : FlattenedIn; }
	const TArray<FWidgetAnimSettings>& GetDesignerPreviewOut() const { return DesignerPreviewModeOverride.IsSet() ? DesignerOverrideOut : FlattenedOut; }

	/** Whether the preview is on the exit: playing it, or holding after it. */
	bool IsDesignerPreviewShowingOut() const;

	/** Runtime clock: the core ticker, registered while the owner is constructed. */
	FTSTicker::FDelegateHandle RuntimeTickHandle;

	/** Designer clock: an active timer on the owner's Slate widget, which is also what keeps the designer repainting. */
	TWeakPtr<FActiveTimerHandle> PreviewTimerHandle;
	TWeakPtr<SWidget> PreviewTimerWidget;

	/** Anim IDs two effective entries use on the same widget in the same direction. Rebuilt with the stacks. */
	TArray<FName> ConflictingAnimIds;

	/** Retries EnsureDesignerPreviewTimer until the owner's Slate widget exists. */
	FTSTicker::FDelegateHandle PreviewRetryHandle;
	int32 PreviewRetriesLeft = 0;

	/** Widget -> component, for FindWidgetAnimationComponent. Game thread only. */
	static TMap<TObjectKey<UWidget>, TWeakObjectPtr<UWidgetAnimationComponent>> ComponentsByWidget;

	void SetAnimOwner(UWidget* InWidget, bool bByCode);
	void Unregister();

	/** The User Widget whose tree Target Widget paths are resolved in. */
	UUserWidget* GetPathRoot() const;

	void CascadeAnimationToChildren(bool bPlayingIn);
	void EnsureRuntimeBaselines();
	void ApplyCurrentAnimPose();

	/** What starts a filtered Play In / Play Out: which of each entry's triggers it answers to. */
	enum class EEntryTrigger : uint8
	{
		Construct,
		Visibility
	};

	/**
	 * Play In or Play Out for one trigger: only the entries set to play on it. The unnamed layers (Additive Anim
	 * Layers and extra layers) join whenever an entry does - or, on construct, when there are no entries at all, as
	 * they always have. False when nothing was started.
	 */
	bool PlayForTrigger(EEntryTrigger Trigger, bool bOut);

	/** Marks the entries' trigger settings visible on this component's own entries, and notes whether any watches visibility. */
	void RefreshEntryTriggers();

	/** Moves the two trigger settings saved on the component before they moved onto entries onto every entry. */
	void MigrateTriggersIntoEntries();
	void ApplyVisibilityToOwner(ESlateVisibility InVisibility);
	void HandleOutAnimationFinished();

	void StartRuntimeTicking();
	void StopRuntimeTicking();
	void RuntimeTick(float DeltaTime);

	void StopDesignerPreview();

	/** True while any track in this direction still has a layer that is going to end - endless loops excluded. Drives Phase. */
	bool IsSettlingInDirection(bool bIsOut) const;

	/** Recomputes ConflictingAnimIds from the effective stacks, reporting any conflict that is new. */
	void RefreshAnimIdConflicts();

	/** Puts the conflict error for AnimId on screen and in the log. */
	void ReportAnimIdConflict(FName AnimId) const;

	/** Reports each of AnimIds that is conflicting. Returns true if any was. */
	bool ReportConflictsIn(const TArray<FName>& AnimIds) const;

	static bool IsVisibleState(ESlateVisibility Visibility);
};