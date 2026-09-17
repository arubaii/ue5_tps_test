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
 * Preview Animation, which is then played instead of Layers.
 *
 * Edit it either through Class Defaults in WidgetAnimationBuilder itself, or by placing it in any other
 * Widget Blueprint and selecting it. Either way the preview follows every edit, including a
 * value still being dragged.
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
	UPROPERTY(EditAnywhere, Category = "Widget Animation", meta = (DisplayName = "Animation Name", EditCondition = "PreviewAnimation == nullptr"))
	FString AnimationName = TEXT("NewWidgetAnimation");

	
	/** What this animation is for, in words. Copied to the asset, where it is the tooltip wherever the asset is picked. */
	UPROPERTY(EditAnywhere, Category = "Widget Animation", meta = (MultiLine = true, EditCondition = "PreviewAnimation == nullptr"))
	FText Description;

	/**
	 * Content folder the asset is created in. Empty puts it beside the Widget Blueprint this was edited in.
	 */
	UPROPERTY(EditAnywhere, Category = "Widget Animation", meta = (DisplayName = "Save Folder", EditCondition = "PreviewAnimation == nullptr", ContentDir))
	FDirectoryPath SaveFolder;
	
	/**
	 * The layers, exactly as a Widget Animation asset holds them. They combine rather than overwrite:
	 * opacity and scale multiply, translation and angle add, and Delay staggers them.
	 */
	UPROPERTY(EditAnywhere, Category = "Widget Animation", meta = (TitleProperty = "AnimType", EditCondition = "PreviewAnimation == nullptr"))
	TArray<FWidgetAnimLayer> Layers;

	/**
	 * An existing Widget Animation to play instead of Layers. View Animation Preview on the asset sets this;
	 * clear it to go back to building. Edits made to the asset show up here as they are made.
	 */
	UPROPERTY(EditAnywhere, Category = "Widget Animation", meta = (DisplayName = "Preview Animation"))
	TObjectPtr<UWidgetAnimPresetAsset> PreviewAnimation;
	
	/**
	 * Saves Layers and Description as a new WA_ Widget Animation asset, and selects it in the Content Browser.
	 * Refused while Preview Animation is set, since what is on screen then is not what would be saved.
	 *
	 * Not CallInEditor: the button is built by FWidgetAnimBuilderDetails instead, which is what lets it
	 * be coloured. The function stays public so that customization can call it.
	 */
	void CreateWidgetAnimation();

	// -------------------------------------------------------------------------------------------------
	// Preview - never written to the asset
	// -------------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, Category = "Widget Animation|Preview", meta = (DisplayName = "Animation Preview", ShowOnlyInnerProperties))
	FWidgetAnimPreviewSettings Preview;

	/** Tries the animation looped, as an entry with Loop set would play it. */
	UPROPERTY(EditAnywhere, Category = "Widget Animation|Preview Playback", meta = (DisplayName = "Loop"))
	bool bPreviewLoop = false;

	UPROPERTY(EditAnywhere, Category = "Widget Animation|Preview Playback", meta = (DisplayName = "Ping Pong", EditCondition = "bPreviewLoop", EditConditionHides))
	bool bPreviewPingPong = true;

	/** How many times it plays through. 0 loops forever. */
	UPROPERTY(EditAnywhere, Category = "Widget Animation|Preview Playback", meta = (DisplayName = "Loop Count", EditCondition = "bPreviewLoop", EditConditionHides, ClampMin = "0"))
	int32 PreviewLoopCount = 0;

	UPROPERTY(EditAnywhere, Category = "Widget Animation|Preview Playback", meta = (DisplayName = "Invert Animation"))
	bool bPreviewInvertAnimation = false;

	/** As an entry's Playback Speed. Preview > Playback Speed multiplies on top, and only affects the preview clock. */
	UPROPERTY(EditAnywhere, Category = "Widget Animation|Preview Playback", meta = (DisplayName = "Playback Speed", ClampMin = "0.01", UIMin = "0.1", UIMax = "4.0"))
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
	void TogglePreviewPaused() { SetPreviewPaused(!bPreviewPaused); }

	/**
	 * Seconds into whichever stack is currently showing - Anim In while playing in or shown, Anim Out
	 * while playing out or hidden - on the same clock GetPreviewTotalSeconds measures. 0 before anything
	 * has played, including right after Reset. What the toolbar's length readout shows as the numerator.
	 */
	float GetPreviewElapsedSeconds() const;

	/**
	 * Seconds one full pass of whichever stack is currently showing takes to play through once. -1 if a
	 * layer in it loops forever, since it then never finishes on its own. What the toolbar's length
	 * readout shows as the denominator.
	 */
	float GetPreviewTotalSeconds() const;

	/**
	 * Whether GetPreviewTotalSeconds's answer is decided by a looping layer, and if so, how long one
	 * visible repeat of it takes - doubled in OutLoopSeconds when OutPingPong is set, since a round trip
	 * is there-and-back. See UWidgetAnimLibrary::GetStackDrivingLoopInfo.
	 */
	bool GetPreviewLoopInfo(bool& OutPingPong, float& OutLoopSeconds) const;

	// UWidget / UObject
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
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

	/** Re-reads Animation in every live builder previewing it. Called when the asset is edited. */
	static void NotifyAnimationAssetEdited(const UWidgetAnimPresetAsset* Animation, bool bRestart);

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

protected:

	/** The Image the animation plays on. The WidgetAnimPreview Widget Blueprint must have one with this name. */
	UPROPERTY(BlueprintReadOnly, Category = "Widget Animation", meta = (BindWidget))
	TObjectPtr<UImage> PreviewImage;

	virtual void OnWidgetRebuilt() override;

private:

	/**
	 * Re-reads the stacks and poses the preview now. With bRestart the animation starts over; without it,
	 * whatever is playing carries on with the new values, which is what a value being dragged wants.
	 * With bAnimate false, the widget is left exactly where Synchronize puts it - its authored, resting
	 * look - and no animation pose is applied on top; that is what Reset wants to show.
	 * No-op outside the designer.
	 */
	void RefreshPreview(bool bRestart, bool bAnimate = true);

#if WITH_EDITOR
	/** Copies everything the preview depends on from Source, so a live preview can follow its template. */
	void CopyAuthoredStateFrom(const UWidgetAnimBuilder& Source);

	/** Refreshes this object if it is itself on screen, then every live preview built from it. */
	void RefreshLivePreviews(bool bRestart, bool bCopyAuthoredState, bool bAnimate = true);

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

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> FlattenedIn;

	/** The same layers again. An asset has no direction; the preview's exit is simply the animation played backwards. */
	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> FlattenedOut;

	/**
	 * Set by the Play/Pause button in the toolbar. Stops the preview clock advancing, nothing else.
	 * Starts true so a freshly opened preview sits still until Play is pressed.
	 */
	UPROPERTY(Transient)
	bool bPreviewPaused = true;

	UPROPERTY(Transient)
	FWidgetAnimPreviewState PreviewState;

	UPROPERTY(Transient)
	FWidgetAnimBaselineSet PreviewBaselines;
};