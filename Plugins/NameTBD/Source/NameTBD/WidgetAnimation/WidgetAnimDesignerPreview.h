// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Widgets/SWidget.h"

#include "WidgetAnimLibrary.h"

#include "WidgetAnimDesignerPreview.generated.h"

class UWidget;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UWidgetAnimDesignerPreviewHost : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything that wants its animations played in the UMG designer.
 *
 * This used to be three copies of the same fifty lines - one on UUserWidget_Animatable, one on
 * UContextMenuButton, one on UWidgetAnimPreviewBox - which is why fixing anything about the preview
 * meant fixing it three times and why the Info Card anchor, which has animations of its own to show,
 * simply never got one. The harness is the functionality; a host is just the six things the harness
 * cannot work out for itself.
 *
 * Implementing it is small: expose the settings struct the user edits, the transient state and baseline
 * set the harness writes into, and the two flattened stacks to play. Then call Synchronize from
 * SynchronizeProperties, EnsurePreviewTimer from OnWidgetRebuilt, Shutdown from ReleaseSlateResources
 * and BeginDestroy, and NotifyPropertyChanged from PostEditChangeProperty.
 */
class NAMETBD_API IWidgetAnimDesignerPreviewHost
{
	GENERATED_BODY()

public:

	/**
	 * The widget the Slate active timer registers against - the one actually in the designer's tree.
	 *
	 * Usually the host itself. It has to be a real widget with a cached SWidget, because registering an
	 * active timer is what forces the viewport to repaint; nothing else in the editor will.
	 */
	virtual UWidget* GetPreviewOwnerWidget() = 0;

	/**
	 * The widget an animation layer with no Target Widget animates.
	 *
	 * Defaults to the owner. An Info Card anchor overrides it to return its content, because the thing
	 * being previewed there is a card that does not exist until the game runs.
	 */
	virtual UWidget* GetPreviewAnimationTarget() { return GetPreviewOwnerWidget(); }

	/** The FWidgetAnimPreviewSettings the user is editing in the Details panel. */
	virtual FWidgetAnimPreviewSettings& GetPreviewSettings() = 0;

	/** Transient clock. Must survive between frames; must not be saved. */
	virtual FWidgetAnimPreviewState& GetPreviewState() = 0;

	/** What the animated widgets looked like before the preview touched them, so Off can put them back. */
	virtual FWidgetAnimBaselineSet& GetPreviewBaselines() = 0;

	virtual const TArray<FWidgetAnimSettings>& GetPreviewAnimIn() const = 0;
	virtual const TArray<FWidgetAnimSettings>& GetPreviewAnimOut() const = 0;

	/** Rebuild whatever the two stacks above are derived from. Called before every baseline recapture. */
	virtual void RebuildPreviewAnimations() {}

	/**
	 * Register whatever drives preview frames.
	 *
	 * The default registers a Slate active timer on the owner widget, which is what any host that has no
	 * clock of its own wants. A host that already runs a timer for its runtime playback - the context
	 * menu button does - overrides this to start that one instead, so the preview never ends up being
	 * ticked twice a frame by two separate timers.
	 */
	virtual void EnsurePreviewTimer();
	
	/**
	 * Whether the preview clock is frozen. Posing still happens (a zero-delta TickFrame poses the current
	 * frame); only time stops. While this is true the harness's active timer stops itself, so a host that
	 * un-pauses must call EnsurePreviewTimer to start the clock again.
	 */
	virtual bool IsPreviewPaused() const { return false; }
	
	/**
	 * Called once the harness has run a playing mode to its end - not for a held pose or a scrub frame,
	 * which stop for a different reason. A host that tracks a paused/playing state of its own uses this
	 * to put itself back to the start rather than sitting on the last frame still calling itself playing.
	 */
	virtual void OnPreviewFinished() {}
};

/**
 * Drives an IWidgetAnimDesignerPreviewHost.
 *
 * Every function is a no-op outside the designer, so hosts can call them unconditionally rather than
 * wrapping each one in IsDesignTime() and WITH_EDITOR.
 */
UCLASS()
class NAMETBD_API UWidgetAnimDesignerPreview : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** True when the host is at design time, has a mode selected, and has something to play. */
	static bool ShouldRun(IWidgetAnimDesignerPreviewHost* Host);

	/**
	 * The whole design-time pass: rebuild the stacks, re-read the baselines, and either start the timer
	 * or put everything back. Call this from SynchronizeProperties; it returns immediately at runtime.
	 */
	static void Synchronize(IWidgetAnimDesignerPreviewHost* Host);

	/** Restores, clears, then re-reads the baseline set. Run whenever the stacks change. */
	static void RecaptureBaselines(IWidgetAnimDesignerPreviewHost* Host);

	/** Registers the Slate active timer, unless one is already running. The default EnsurePreviewTimer. */
	static void EnsureTimer(IWidgetAnimDesignerPreviewHost* Host);

	/**
	 * One preview frame. True while more are needed.
	 *
	 * A paused host asked for a frame with time in it gets false and no pose - there is nothing to advance.
	 * Asked with a zero delta it is posed as usual, which is how the frozen frame is shown.
	 *
	 * Does not touch the timer flag, so a host driving this from a timer it owns keeps its own bookkeeping.
	 */
	static bool TickFrame(IWidgetAnimDesignerPreviewHost* Host, float DeltaTime);

	/** TickFrame wrapped for a Slate active timer, including stopping itself when there is nothing to do. */
	static EActiveTimerReturnType Tick(IWidgetAnimDesignerPreviewHost* Host, float DeltaTime);

	/** Puts every animated widget back as authored and resets the harness. What switching Mode to Off does. */
	static void Stop(IWidgetAnimDesignerPreviewHost* Host);

	/** Call from ReleaseSlateResources and BeginDestroy. The Slate widget the timer was on is going away. */
	static void Shutdown(IWidgetAnimDesignerPreviewHost* Host);

#if WITH_EDITOR
	/**
	 * Pushes a mid-edit change from the object the Details panel is editing to the widgets actually being
	 * painted, and poses them immediately.
	 *
	 * Call unconditionally from PostEditChangeProperty; it decides for itself whether the change is one
	 * the ordinary commit-time rebuild would miss. See WidgetAnimDesignerSync for why this is needed at
	 * all: the panel edits a template, the viewport paints a preview instance, and the two only reconcile
	 * when an edit is committed.
	 */
	static void NotifyPropertyChanged(UObject* Template, const FPropertyChangedEvent& PropertyChangedEvent);
#endif
};