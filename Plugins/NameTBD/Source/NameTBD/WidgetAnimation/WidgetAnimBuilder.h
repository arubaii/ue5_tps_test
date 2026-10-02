// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/EngineTypes.h"

#include "WidgetAnimDesignerPreview.h"
#include "WidgetAnimLibrary.h"
#include "WidgetAnimPreset.h"

#include "WidgetAnimBuilder.generated.h"

class UBlueprint;
class UImage;

/**
 * Where Widget Animation assets are made, and where existing ones are looked at.
 *
 * The base class for the WidgetAnimationBuilder Widget Blueprint, whose whole tree is one Image called
 * PreviewImage. Build the animation here exactly as it would be built in a Widget Animation asset - a
 * stack of layers - and it plays on PreviewImage live in the UMG designer: no compile, no PIE. When it
 * looks right, press Create Widget Animation and it is saved as a WA_ asset.
 *
 * View Animation Preview on a Widget Animation asset opens WidgetAnimationBuilder with that asset set as
 * Preview Animation: the preview plays it, and the Widget Animations panel opens on Edit Existing Asset with
 * it picked.
 *
 * Edit it in the Widget Animations panel - the drawer on the Widget Blueprint editor's status bar, or the
 * tab it docks into - which edits these Class Defaults and is where every property below lives. They are
 * EditDefaultsOnly, so the designer's Details panel no longer shows them for the builder's root row: edited
 * there they went to the designer's preview instance, which UMG rebuilds - and the whole Details panel with
 * it - on every committed edit. The preview follows every edit made in the panel, mid-drag included.
 *
 * The Preview Playback options are how the animation is tried out - looped, ping-ponged, slowed down.
 * They are decisions about one USE of an animation, so they stay here and are not written to the asset;
 * set them again on the entry that uses it.
 *
 * Design time only. At runtime this is an Image and nothing more.
 */
UCLASS(Abstract,  meta = (DisplayName = "Widget Anim Builder"))
class NAMETBD_API UWidgetAnimBuilder : public UUserWidget, public IWidgetAnimDesignerPreviewHost
{
	GENERATED_BODY()

public:

	UWidgetAnimBuilder(const FObjectInitializer& ObjectInitializer);

	// -------------------------------------------------------------------------------------------------
	// The animation being built - written to the asset
	// -------------------------------------------------------------------------------------------------

	/**
	 * Name of the asset Create Widget Animation makes. WA_ is added for you if it is missing, and a number
	 * is appended if an asset with that name already exists.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Widget Animation", meta = (DisplayName = "Animation Name", EditCondition = "PreviewAnimation == nullptr"))
	FString AnimationName = TEXT("NewWidgetAnimation");

	
	/** What this animation is for, in words. Copied to the asset, where it is the tooltip wherever the asset is picked. */
	UPROPERTY(EditDefaultsOnly, Category = "Widget Animation", meta = (MultiLine = true, EditCondition = "PreviewAnimation == nullptr"))
	FText Description;

	/**
	 * Content folder the asset is created in. Empty uses the project's Default Save Folder (Project Settings > Plugins >
	 * Widget Animations), or - with none set - puts it beside the Widget Blueprint this was edited in.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Widget Animation", meta = (DisplayName = "Save Folder", EditCondition = "PreviewAnimation == nullptr", ContentDir))
	FDirectoryPath SaveFolder;
	
	/**
	 * The widgets the animation being built moves, one group each, exactly as a Widget Animation asset holds
	 * them. Here, a group's Preview Target is a widget inside this builder - PreviewImage when it names none -
	 * so a generic animation can be built and watched on the preview image before it is used anywhere.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Widget Animation", meta = (TitleProperty = "GroupName", EditCondition = "PreviewAnimation == nullptr"))
	TArray<FWidgetAnimLayerGroup> LayerGroups;

	/**
	 * The layers, exactly as a Widget Animation asset holds them: one sequence, each layer in one of the
	 * Layer Groups. They combine rather than overwrite - opacity and scale multiply, translation and angle
	 * add - and Starts and Delay sequence them, across groups as freely as within one.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Widget Animation", meta = (TitleProperty = "AnimType", EditCondition = "PreviewAnimation == nullptr"))
	TArray<FWidgetAnimLayer> Layers;

	/** How fast the whole animation plays, saved with it - see UWidgetAnimPresetAsset::AnimationSpeedMultiplier. */
	UPROPERTY(EditDefaultsOnly, Category = "Widget Animation", meta = (DisplayName = "Animation Speed Multiplier", ClampMin = "0.01", UIMin = "0.1", UIMax = "4.0", EditCondition = "PreviewAnimation == nullptr"))
	float AnimationSpeedMultiplier = 1.f;

	/** How long one pass of the animation runs at the least, saved with it - see UWidgetAnimPresetAsset::MinimumLength. */
	UPROPERTY(EditDefaultsOnly, Category = "Widget Animation", meta = (DisplayName = "Minimum Length", ClampMin = "0", UIMin = "0", Units = "s", EditCondition = "PreviewAnimation == nullptr"))
	float MinimumLength = 0.f;

	/**
	 * The asset the Widget Animations panel's Edit Existing Asset is editing in place, played instead of the
	 * builder's own layers. View Animation Preview on the asset sets it; switching the panel to New Animation
	 * clears it.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Widget Animation", meta = (DisplayName = "Preview Animation"))
	TObjectPtr<UWidgetAnimPresetAsset> PreviewAnimation;
	
	/**
	 * Saves Layers, LayerGroups and Description as a new WA_ Widget Animation asset, selects it in the Content
	 * Browser, and returns it - null if nothing was created. The Widget Animations panel's Save as Animation Asset.
	 * Refused while Preview Animation is set, since what is on screen then is not what would be saved.
	 *
	 * Not CallInEditor: the button is built by FWidgetAnimBuilderDetails instead, which is what lets it
	 * be coloured. The function stays public so that customization can call it.
	 */
	UWidgetAnimPresetAsset* CreateWidgetAnimation();

	// -------------------------------------------------------------------------------------------------
	// Preview - never written to the asset
	// -------------------------------------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, Category = "Widget Animation|Preview", meta = (DisplayName = "Animation Preview", ShowOnlyInnerProperties))
	FWidgetAnimPreviewSettings Preview;

	/**
	 * Tries the animation looped, as an entry with Loop set would play it - endlessly, since the preview
	 * is something to watch while judging the animation and a loop that stops is just a slow way of not
	 * looping.
	 *
	 * Driven by the Loop toggle in the toolbar rather than the Details panel - the toolbar's is on screen
	 * whatever is selected, and two ways to set one value is one too many. Still a UPROPERTY, so it is
	 * saved on the Widget Blueprint the same as before.
	 */
	UPROPERTY()
	bool bPreviewLoop = false;

	/** The Ping Pong toggle in the toolbar, which is only live while Loop is on. See bPreviewLoop. */
	UPROPERTY()
	bool bPreviewPingPong = true;

	/** The Invert toggle in the toolbar. See bPreviewLoop for why it is not in the Details panel. */
	UPROPERTY()
	bool bPreviewInvertAnimation = false;

	/**
	 * As an entry's Playback Speed. Preview > Playback Speed multiplies on top, and only affects the
	 * preview clock. The Speed box in the toolbar; see bPreviewLoop.
	 */
	UPROPERTY()
	float PreviewPlaybackSpeed = 1.f;

	/**
	 * Starts the preview over from the beginning. The Restart button in the toolbar.
	 *
	 * Not CallInEditor any more: the same button in the Details panel was a second way to do one thing,
	 * and the toolbar's is always on screen rather than only while this object is what is selected.
	 */
	void ResetPreview();

	/**
	 * Freezes the preview on the frame it is showing, and lets it go again from there. What the Play/Pause
	 * button in the toolbar calls. Per instance and never saved: it is a way of looking at the animation,
	 * not part of it, so it is deliberately left out of CopyAuthoredStateFrom - pausing a live preview
	 * survives the next edit made in the Details panel.
	 */
	void SetPreviewPaused(bool bPaused);

	/**
	 * Plays the animation as authored, first frame to last: its entrance, once - or for as long as Loop is
	 * on. What Play means everywhere a Play button calls it, whatever Animation Preview > Mode was left on.
	 *
	 * The Mode is set to Play In to get there. Loop In and Out, Play Out and the rest each run some of the
	 * animation backwards on purpose - its exit - and a Play button that did that whenever one of them was
	 * left selected looked like an animation playing in reverse. Starts from the top when frozen on a
	 * scrubbed frame, a key or the end; carries on from where it was paused otherwise. Stops on the last
	 * frame, as a timeline does.
	 */
	void PlayAnimation();

	/** Play if frozen, freeze if playing. What the toolbar's and the panel's Play/Pause buttons call. */
	void TogglePreviewPaused()
	{
		if (bPreviewPaused)
		{
			PlayAnimation();
		}
		else
		{
			SetPreviewPaused(true);
		}
	}

	// -------------------------------------------------------------------------------------------------
	// Playback controls - what the toolbar's Loop, Invert, Speed and Scrub drive
	//
	// Each setter writes the value on this object AND on the template it was built from, so the choice
	// outlives the designer throwing this preview instance away, and rebuilds the stacks so the change is
	// visible on the frame it is made.
	// -------------------------------------------------------------------------------------------------

	bool GetPreviewLoop() const { return bPreviewLoop; }
	void SetPreviewLoop(bool bLoop);

	bool GetPreviewInvertAnimation() const { return bPreviewInvertAnimation; }
	void SetPreviewInvertAnimation(bool bInvert);

	bool GetPreviewPingPong() const { return bPreviewPingPong; }
	void SetPreviewPingPong(bool bPingPong);

	float GetPreviewPlaybackSpeed() const { return PreviewPlaybackSpeed; }
	void SetPreviewPlaybackSpeed(float Speed);

	/**
	 * Where the scrub slider sits, as a fraction of one pass: the alpha last scrubbed to (or the key being
	 * edited), or - when nothing has been scrubbed - where the animation on screen currently is, so the
	 * slider doubles as a progress bar. Same coordinate as GetPreviewElapsedSeconds.
	 */
	float GetPreviewScrubAlpha() const;

	/**
	 * Poses the animation at Alpha - a fraction of one pass of the entrance, as it plays - and freezes it
	 * there. What the toolbar's slider calls.
	 *
	 * The clock is deliberately not moved with it: scrubbing is a look at one frame, so Play starts the
	 * animation from the top rather than from wherever the slider was left.
	 */
	void SetPreviewScrubAlpha(float Alpha);

	/**
	 * Pauses and scrubs to key KeyIndex of flattened layer LayerIndex on the entrance stack. What editing a
	 * keyframe does, and what selecting one in the Widget Animations panel does, so the readout, the
	 * slider and the pose all jump to the moment that key controls.
	 *
	 * The key stays focused - later refreshes re-pose on it, following it if its time moves - until the
	 * slider is dragged, Play is pressed, or the animation starts over.
	 */
	void ScrubToTransformKey(int32 LayerIndex, int32 KeyIndex);

	/**
	 * Where the animation on screen is, in seconds along one pass of the entrance as it plays. 0 before
	 * anything has played, including right after Reset. The toolbar readout's numerator, and the same
	 * coordinate the scrub slider, a key's time and SetPreviewScrubAlpha all use.
	 *
	 * A position, not a stopwatch. The exit is the entrance run backwards, so while the exit plays this
	 * counts back down towards 0 - it used to count up, which put the slider on the opposite side from the
	 * pose and made scrubbing out of a paused exit jump to the mirrored frame. Looping is measured the same
	 * way: the number walks up to the total and starts again, or back down it with Ping Pong on.
	 */
	float GetPreviewElapsedSeconds() const;

	/**
	 * Seconds one full pass of whichever stack is currently showing takes to play through once - the same
	 * number whether or not Loop is on, since looping does not make the animation itself any longer. -1
	 * only if a layer inside it loops forever on its own. The readout's denominator.
	 */
	float GetPreviewTotalSeconds() const;

	// UWidget / UObject
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void BeginDestroy() override;

	/** Fits LayerGroups to Layers after loading - see UWidgetAnimLibrary::NormalizeLayerGroups. */
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Where editing a keyframe is recognised - the property chain says which layer and which key. */
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

	virtual const FText GetPaletteCategory() override;
#endif

#if WITH_EDITOR
	/** Name of the Widget Blueprint View Animation Preview opens. It must derive from this class. */
	static const FName BuilderBlueprintName;

	/**
	 * Opens the WidgetAnimationBuilder Widget Blueprint with Animation as its Preview Animation.
	 * What View Animation Preview on a Widget Animation asset calls.
	 */
	static void OpenPreviewFor(UWidgetAnimPresetAsset* Animation);

	/**
	 * Re-reads Animation in every live builder previewing it. Called when the asset is edited.
	 *
	 * A builder starts over by itself when the animation's shape changed - a layer added or removed, a type
	 * changed, a key added. bForceRestart starts it over even when only values changed.
	 */
	static void NotifyAnimationAssetEdited(const UWidgetAnimPresetAsset* Animation, bool bForceRestart);

	/**
	 * Takes every live builder previewing Animation to key KeyIndex of layer LayerIndex, paused there.
	 * Called when a keyframe's values are edited on the asset, so the edit is seen at the moment it affects.
	 */
	static void NotifyAnimationAssetKeyEdited(const UWidgetAnimPresetAsset* Animation, int32 LayerIndex, int32 KeyIndex);

	/**
	 * Visits every builder of BuilderClass that is currently on screen in a designer.
	 *
	 * How the toolbar reaches the thing being previewed. It only has the Widget Blueprint being edited,
	 * and the object that matters is the preview instance the designer built from that Blueprint's class -
	 * not the class defaults, which have no clock running and are not what is painted.
	 */
	static void ForEachLivePreview(const UClass* BuilderClass, TFunctionRef<void(UWidgetAnimBuilder&)> Func);

	/** True when a live preview of BuilderClass exists and is frozen. What the toolbar's icon reads. */
	static bool IsLivePreviewPaused(const UClass* BuilderClass);

	/** True when there is a live preview of BuilderClass at all, so the toolbar knows to enable itself. */
	static bool HasLivePreview(const UClass* BuilderClass);
#endif

	// IWidgetAnimDesignerPreviewHost
	virtual UWidget* GetPreviewOwnerWidget() override { return this; }
	virtual UWidget* GetPreviewAnimationTarget() override;
	virtual FWidgetAnimPreviewSettings& GetPreviewSettings() override { return Preview; }
	virtual FWidgetAnimPreviewState& GetPreviewState() override { return PreviewState; }
	virtual FWidgetAnimBaselineSet& GetPreviewBaselines() override { return PreviewBaselines; }
	virtual const TArray<FWidgetAnimSettings>& GetPreviewAnimIn() const override { return FlattenedIn; }
	virtual const TArray<FWidgetAnimSettings>& GetPreviewAnimOut() const override { return FlattenedOut; }
	virtual bool IsPreviewPaused() const override { return bPreviewPaused; }
	virtual void RebuildPreviewAnimations() override;
	virtual void OnPreviewFinished() override;

	/**
	 * Starts the harness's clock only while playing. A frozen preview has no time to advance, so it gets a
	 * single repaint instead of a timer that would tick every frame for as long as it sits paused.
	 */
	virtual void EnsurePreviewTimer() override;

protected:

	/** The Image the animation plays on. The WidgetAnimPreview Widget Blueprint must have one with this name. */
	UPROPERTY(BlueprintReadOnly, Category = "Widget Animation", meta = (BindWidget))
	TObjectPtr<UImage> PreviewImage;

	virtual void OnWidgetRebuilt() override;

private:

	/**
	 * Re-reads the stacks and poses the preview now. No-op outside the designer.
	 *
	 * Whether the animation starts over is decided here, from what changed rather than from how it was
	 * changed: a different shape of animation - a layer added or removed, a type changed, a key added,
	 * loop / ping-pong / invert toggled, another Preview Animation - starts over; anything else is only new
	 * values, and whatever is on screen carries on with them. Playing keeps playing; paused on a key stays
	 * paused on that key. Deciding by event type instead restarted on every committed checkbox, and threw
	 * away the key the preview had just jumped to. bForceRestart starts over regardless.
	 *
	 * With bAnimate false the widget is left at its authored, resting look with no pose on top, which is
	 * what Reset shows.
	 */
	void RefreshPreview(bool bForceRestart, bool bAnimate = true);

	/**
	 * Applies one playback edit to this object and to the template it was built from, then re-reads the
	 * stacks. bRestart is for changes that alter the shape of the animation - loop, invert - rather than
	 * only its pace, which a preview that is already running should carry on with.
	 */
	void SetPlaybackOption(TFunctionRef<void(UWidgetAnimBuilder&)> Apply, bool bRestart);

	/** Whether the exit stack is the one on screen, which decides what the readouts and Scrub measure. */
	bool IsShowingOutStack() const;

	/** Freezes the preview at Alpha along one pass of the entrance, and poses it there. */
	void BeginScrubAt(float Alpha);

	/**
	 * Where key KeyIndex of flattened layer LayerIndex sits as a fraction of one pass of the entrance, as it
	 * plays - mirrored within its layer when Invert is on. -1 if there is no such Transform key.
	 */
	float GetTransformKeyScrubAlpha(int32 LayerIndex, int32 KeyIndex) const;

	/**
	 * Puts on screen whatever the preview says it is showing: the scrub (or focused key) pose, the harness's
	 * current frame, or - after Reset - nothing on top of the authored look. Called after anything that puts
	 * the widget back as authored, which UWidgetAnimDesignerPreview::Synchronize always does; without it a
	 * paused preview lost its pose on every designer refresh while the readout still claimed it.
	 */
	void ApplyCurrentPose();

	/** Starts the harness over and drops any scrub or focused key. */
	void RestartPreviewClock();

	/** Everything that makes a change "a different animation" rather than new values. See RefreshPreview. */
	uint32 ComputeStructureSignature() const;

	/** One repaint of the designer, with no clock left running afterwards. */
	void RequestDesignerRepaint();

	/**
	 * The designer throws preview instances away and rebuilds them - on compile, and on some edits. What
	 * the preview was showing (paused, scrubbed, focused on a key) is kept per builder class so the new
	 * instance carries on exactly there, rather than every rebuild snapping back to the first frame.
	 */
	void SaveSession() const;

	/**
	 * Adopts the session a previous instance of this class left, once per instance and only in the
	 * designer. Also takes this instance's first structure signature, which the session is checked against.
	 */
	void RestoreSession();

#if WITH_EDITOR
	/**
	 * Copies everything the preview depends on from Source, so a live preview can follow its template.
	 *
	 * Property by property, and only what differs. The object the Details panel is showing is very often one
	 * of these previews, and rewriting its arrays with identical copies - every edit, from inside the
	 * panel's own change notification - freed and reallocated the memory the panel and the inline curve
	 * editor were reading. Returns whether anything was copied.
	 */
	bool CopyAuthoredStateFrom(const UWidgetAnimBuilder& Source);

	/** Refreshes this object if it is itself on screen, then every live preview built from it. */
	void RefreshLivePreviews(bool bForceRestart, bool bCopyAuthoredState, bool bAnimate = true);

	/** Runs Func on this object, then on every live preview built from it. */
	void ForThisAndLivePreviews(TFunctionRef<void(UWidgetAnimBuilder&)> Func);

	/** Hooks HandleTransformAnimsSynced up the first time it is needed. */
	static void EnsureSyncListener();

	/**
	 * A deferred key sync changed an object's keys. A builder's previews - and any builder previewing a
	 * changed asset - copied those keys before the sync ran, so they are brought up to date here.
	 */
	static void HandleTransformAnimsSynced(UObject* Object, bool bResized);

	/** Save Folder as a long package path, or the folder of the Widget Blueprint this belongs to. */
	FString GetSavePackagePath() const;

	/** Animation Name, sanitized and with the WA_ prefix. */
	FString GetPrefixedAssetName() const;

	/** The WidgetAnimationBuilder Widget Blueprint, or null if there isn't one deriving from this class. */
	static UBlueprint* FindBuilderBlueprint();
#endif

	/**
	 * Holds a copy of Layers when there is no Preview Animation, so the preview is built by the same FWidgetAnimEntryBase::BuildSettings every
	 * real use of an asset goes through. Never saved.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UWidgetAnimPresetAsset> PreviewAsset;

	/** A group's Preview Target inside this builder: that widget, or PreviewImage when it names none or nothing matches. */
	UWidget* ResolvePreviewGroupTarget(FName Path);

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> FlattenedIn;

	/** The same layers again. An asset has no direction; the preview's exit is simply the animation played backwards. */
	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> FlattenedOut;

	/**
	 * How long one pass of the stack takes while Loop is on, measured off the same use built without it.
	 * 0 when Loop is off, where the stacks can answer for themselves. See RebuildPreviewAnimations.
	 */
	UPROPERTY(Transient)
	float PreviewPassSeconds = 0.f;

	/**
	 * Set by the Play/Pause button in the toolbar. Stops the preview clock advancing, nothing else.
	 * Starts true so a freshly opened preview sits still until Play is pressed.
	 */
	UPROPERTY(Transient)
	bool bPreviewPaused = true;

	/**
	 * Set while the toolbar's Scrub slider is what put the widget where it is, so the slider reports the
	 * alpha it was dragged to instead of the clock's position, and so Play knows to start over.
	 */
	UPROPERTY(Transient)
	bool bPreviewScrubbing = false;

	UPROPERTY(Transient)
	float PreviewScrubAlpha = 0.f;

	/**
	 * The keyframe being edited, which the scrub follows. INDEX_NONE when the scrub is only a slider
	 * position. See ScrubToTransformKey.
	 */
	int32 FocusedLayerIndex = INDEX_NONE;
	int32 FocusedKeyIndex = INDEX_NONE;

	/** Set by Reset: the widget shows its authored look with no pose on top until Play, a scrub or a key edit. */
	bool bShowingRestPose = false;

	/** ComputeStructureSignature as of the last refresh. A different value means a different animation. */
	uint32 StructureSignature = 0;

	/** Whether this instance has picked up the session a previous instance left. See RestoreSession. */
	bool bSessionRestored = false;

	/** A one-shot repaint timer is already registered. */
	bool bRepaintQueued = false;

	UPROPERTY(Transient)
	FWidgetAnimPreviewState PreviewState;

	UPROPERTY(Transient)
	FWidgetAnimBaselineSet PreviewBaselines;
};