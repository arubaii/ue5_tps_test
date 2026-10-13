// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Types/SlateEnums.h"

#include "MediaBox.h"
#include "MediaBoxTypes.h"

#include "MediaBoxWithControls.generated.h"

class SMediaBoxControlsHost;
class UMediaBoxTransportBar;

/**
 * Media Box With Controls: a Media Box with a controls bar over it - a video player. Everything a Media Box does, it does, plus
 * the controls bar (any Widget Blueprint whose parent class is Media Box Transport Bar) laid over the video and driving it,
 * shown and hidden as the player's are on YouTube: while the pointer moves over the video, hidden a few seconds after it stops
 * (or as soon as it leaves) while playing, always shown while paused. A click on the video plays / pauses it.
 *
 * The bar is its own Controls Class, else Project Settings > Plugins > Media Box > Default Controls Class. When it shows and
 * hides is Controls Behavior (Preset YouTube or Custom), else the project's Default Controls Behavior.
 *
 * The bar is made by this widget at runtime (and in the designer, for a preview), never added to the Widget Blueprint's tree.
 */
UCLASS(meta = (DisplayName = "Media Box With Controls", PrioritizeCategories = "Media Controls Playback Appearance Audio Subtitles"))
class NAMETBDMEDIA_API UMediaBoxWithControls : public UMediaBox
{
	GENERATED_BODY()

public:
	UMediaBoxWithControls();

	// ---------------------------------------------------------------------------------------------
	// Controls
	// ---------------------------------------------------------------------------------------------

	/**
	 * The controls bar: a Widget Blueprint whose parent class is Media Box Transport Bar (its parts PlayPauseButton, ScrubBar,
	 * TimeText... are wired up by name). None: Project Settings > Plugins > Media Box > Default Controls Class - and none there
	 * either: no controls bar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Controls", meta = (DisplayName = "Controls Class"))
	TSubclassOf<UMediaBoxTransportBar> ControlsClass;

	/** Uses Controls Behavior below for when the bar shows and hides. Off: Project Settings > Plugins > Media Box > Default Controls Behavior. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Controls", meta = (InlineEditConditionToggle))
	bool bOverrideControlsBehavior = false;

	/**
	 * When the bar shows and hides. Preset YouTube: like the YouTube desktop player; Custom: the values set here. Ticked off: Project
	 * Settings > Plugins > Media Box > Default Controls Behavior.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Controls", meta = (DisplayName = "Controls Behavior", EditCondition = "bOverrideControlsBehavior"))
	FMediaBoxControlsBehavior ControlsBehavior;

	/** Where the bar sits over the video: along its bottom (default) or its top, stretched across it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Controls", meta = (DisplayName = "Controls Alignment"))
	TEnumAsByte<EVerticalAlignment> ControlsAlignment = VAlign_Bottom;

	/**
	 * How tall the bar is, as a part of the video's height: 0.13 is YouTube's (a 48 px bar on a 360 px player). The bar is scaled
	 * evenly to that height and stretched across the video's width, whatever size it was designed at and whatever size the video
	 * is - so one bar fits a skill-card preview and a full-screen trailer alike. 0: the bar's own designed size, unscaled.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Controls", meta = (DisplayName = "Controls Height", ClampMin = "0.0", ClampMax = "0.5", UIMax = "0.3"))
	float ControlsHeight = 0.13f;

	/** The bar is never made shorter than this (Slate units) - so it stays usable on a small video. 0: no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Controls", meta = (DisplayName = "Controls Min Height", ClampMin = "0.0", UIMax = "64.0", EditCondition = "ControlsHeight > 0"))
	float ControlsMinHeight = 0.f;

	/** The bar is never made taller than this (Slate units) - so it doesn't grow huge on a full-screen video. 0: no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Controls", meta = (DisplayName = "Controls Max Height", ClampMin = "0.0", UIMax = "200.0", EditCondition = "ControlsHeight > 0"))
	float ControlsMaxHeight = 0.f;

	/** Space between the video's edges and the bar, in the video's units (not scaled with the bar). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Controls", meta = (DisplayName = "Controls Padding"))
	FMargin ControlsPadding;

	// ---------------------------------------------------------------------------------------------
	// Blueprint
	// ---------------------------------------------------------------------------------------------

	/** The controls bar made from Controls Class. None when there is none (or before it is first shown). */
	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	UMediaBoxTransportBar* GetControls() const { return ControlsWidget; }

	/** Shows the controls now, as a pointer movement would (they then hide as Controls Behavior says). */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Controls")
	void ShowControls();

	/** Whether the controls are showing (or fading in or out). */
	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	bool AreControlsShown() const { return ControlsAlpha > 0.f; }

	/** The values the bar follows now: Controls Behavior or the project's, YouTube's when its Preset is YouTube. */
	UFUNCTION(BlueprintPure, Category = "Media Box|Controls")
	FMediaBoxControlsBehavior GetResolvedControlsBehavior() const;

	// ---------------------------------------------------------------------------------------------

	virtual void TickMediaBox(float DeltaTime) override;

	//~ UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual void ValidateCompiledDefaults(class IWidgetCompilerLog& CompileLog) const override;
#endif

protected:
	//~ UWidget
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:

	friend class SMediaBoxControlsHost;

	/** Controls Class, else the project's default. */
	TSubclassOf<UMediaBoxTransportBar> ResolveControlsClass() const;

	/** Makes the bar (again, when its class changed). */
	void EnsureControlsWidget();

	/** A click on the video (not on the controls). True when it was used. */
	bool HandleVideoClicked();

	/** Puts the bar's opacity, visibility and the pointer as ControlsAlpha says. */
	void ApplyControlsAlpha();

	UPROPERTY(Transient)
	TObjectPtr<UMediaBoxTransportBar> ControlsWidget = nullptr;

	TSharedPtr<SMediaBoxControlsHost> MyControlsHost;

	/** 0 hidden, 1 shown. */
	float ControlsAlpha = 1.f;

	/** When the pointer last moved over the video (or the controls were last asked for), and when it last left it. */
	double LastActivityTime = 0.0;
	double LastUnhoverTime = -1000.0;
	bool bWasHovered = false;
	FVector2f LastPointerPosition = FVector2f::ZeroVector;
	EMediaBoxStatus LastSeenStatus = EMediaBoxStatus::Empty;

	/** The bar's own visibility, put back whenever it is shown. */
	ESlateVisibility ControlsShownVisibility = ESlateVisibility::SelfHitTestInvisible;
	bool bPointerHidden = false;
};
