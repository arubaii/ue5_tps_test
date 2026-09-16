// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Border.h"

#include "WidgetAnimLibrary.h"
#include "WidgetAnimPreset.h"
#include "WidgetAnimHost.h"

#include "WidgetAnimPreviewBox.generated.h"

/**
 * A Border that animates itself, live, in the UMG designer.
 *
 * The scratchpad for authoring Widget Animation assets. Drop one into any Widget Blueprint, size and
 * colour it like any other Border, add an entry pointing at your animation and set Preview Mode to Loop
 * In and Out. It runs in the designer viewport - no compile, no PIE - so you can sit with the animation
 * asset open in one tab and watch the change land in the other. Scrub works while you drag.
 *
 * It is a real Border, not an editor-only stand-in: Preview Mode works identically in a packaged build,
 * which is why Play At Runtime is off by default. A looping preview left switched on can't ship by
 * accident.
 *
 * The animation runs over a captured baseline, so whatever you authored on the Border - opacity, a render
 * transform nudge, a rotation - is animated relative to rather than flattened, and switching Preview Mode
 * back to Off restores it exactly.
 */
UCLASS(meta = (DisplayName = "Widget Anim Preview Box"))
class NAMETBD_API UWidgetAnimPreviewBox : public UBorder, public IWidgetAnimHost
{
	GENERATED_BODY()

public:

	UWidgetAnimPreviewBox();

	/**
	 * The animations being previewed. Several at once is the point: this is how you check that "fade"
	 * plus "pop" plus "slide up" read as one motion rather than three arguing.
	 *
	 * Leave an entry's Target Widget empty to animate this box. Set it to animate something else in the
	 * same Widget Blueprint instead, which is how you preview an animation against real content.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation Preview", meta = (DisplayName = "Anim In", TitleProperty = "Animation"))
	TArray<FWidgetAnimEntry> AnimIn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation Preview", meta = (DisplayName = "Anim Out", TitleProperty = "Animation"))
	TArray<FWidgetAnimEntry> AnimOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation Preview", meta = (DisplayName = "Preview", ShowOnlyInnerProperties))
	FWidgetAnimPreviewSettings Preview;

	/** Whether the preview runs in the UMG designer. The only reason to switch this off is a busy screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation Preview", meta = (DisplayName = "Preview In Designer"))
	bool bPreviewInDesigner = true;

	/**
	 * Whether the preview also runs in game. Off by default: this widget exists to author with, and a box
	 * left looping in a shipped menu is almost always an accident rather than a decision.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation Preview", meta = (DisplayName = "Play At Runtime"))
	bool bPlayAtRuntime = false;

	/** Starts the preview over from the beginning. The replay button for the Play Once modes. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Animation Preview", meta = (DisplayName = "Restart Preview"))
	void RestartPreview();

	/** Rebuilds the flattened In / Out stacks from the entries. Called for you on every rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Animation Preview")
	void RebuildEffectiveAnimations();

	/** Backs the Target Widget dropdown on every entry. Must stay a UFUNCTION for GetOptions. */
	UFUNCTION()
	TArray<FString> GetAnimTargetWidgetOptions() const;

	// IWidgetAnimHost
	virtual void PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying) override;
	virtual void StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart) override;
	virtual bool AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const override;

protected:

	virtual void SynchronizeProperties() override;
	virtual void OnWidgetRebuilt() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual const FText GetPaletteCategory() override;
#endif

private:

	/** Whether the harness should be running at all right now, given design time and the two toggles. */
	bool ShouldPreview() const;

	void StartPreviewTimer();
	EActiveTimerReturnType TickPreview(double InCurrentTime, float InDeltaTime);

	/** Puts every animated widget back as authored, then re-reads the poses the animation will build on. */
	void RecaptureBaselines();

	/** The widget an entry's Target Widget name refers to, or null to mean this box. */
	UWidget* ResolveAnimTarget(FName TargetWidgetName) const;

	UPROPERTY(Transient)
	TArray<FWidgetAnimEntry> EffectiveInEntries;

	UPROPERTY(Transient)
	TArray<FWidgetAnimEntry> EffectiveOutEntries;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> FlattenedIn;

	UPROPERTY(Transient)
	TArray<FWidgetAnimSettings> FlattenedOut;

	UPROPERTY(Transient)
	FWidgetAnimPreviewState PreviewState;

	UPROPERTY(Transient)
	FWidgetAnimBaselineSet PreviewBaselines;

	UPROPERTY(Transient)
	FWidgetAnimPlayer AnimPlayer;

	bool bIsPreviewTimerActive = false;
};