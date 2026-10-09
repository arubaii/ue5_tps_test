// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "DesignerExtension.h"
#include "IHasDesignerExtensibility.h"
#include "WidgetReference.h"

/**
 * Motion paths: where a widget travels as the animation in the Widget Animations panel plays, drawn over the UMG designer's
 * canvas - so a bounce, an arc or an overshoot is seen whole while it is keyed, instead of found by playing it again and again.
 *
 * Read the way production animation tools draw theirs (After Effects, Rive, Blender, Animate):
 *  - a line through every moment of the animation, through the widget's pivot (its Render Transform Pivot, the middle by default);
 *  - a dot at every frame - Dots Per Second, or the panel's Frame Rate while it shows frames - so their spacing is the speed:
 *    close together where the widget eases in or out, far apart where it moves fast;
 *  - a diamond where each key lands, in the Track Editor's own key colors (hovered and picked alike) - so where the next key
 *    will put the widget is seen before it is played. Click one to pick its key (Ctrl / Shift add to the keys picked); drag a
 *    Transform key's diamond to move the key there (Shift keeps it on one axis), one undo step per drag;
 *  - curved paths: a Transform key's Path is Corner (straight in and out), Smooth (a curve through it, worked out from its
 *    neighbours) or Bezier (its own two handles). A picked key with a curve shows its handles - drag one to bend the path (the
 *    other mirrors it; Alt breaks them apart); Alt-drag a diamond to pull fresh handles out of it. Right-click a diamond for
 *    its Path, every key's Path, and Easing Across (each segment, or the whole track as one ease through every key);
 *  - a dashed line, without dots, where the widget jumps without travelling (a Hold, a step);
 *  - the stretch already played drawn fainter than what is ahead of the playhead, and - while the panel poses the preview -
 *    a ring where the widget is now; key times, when shown, sit beside the path, never on it.
 *
 * Shown for every widget the open animation moves - each layer group's Target Widget with a Transform, Slide or Shake track - by
 * itself: a layer is laid over each designer whose Widget Blueprint has a Widget Animations panel with an animation open, with
 * nothing selected (WidgetAnimMotionPaths::Register). Where that layer can't be laid, the designer extension below draws it
 * while any widget is selected. The panel need not be on screen. The path is worked out from exactly what the preview plays
 * (UWidgetAnimLibrary::EvaluateExpandedStackAtTime on the panel's own settings and baselines), so every easing, segment curve,
 * spring, overshoot, stagger and delay is in it, and nothing is moved to find it. It is mapped onto the canvas through the
 * widgets' layout alone - never their render transforms, which the preview changes every frame - so the path and its keys stay
 * perfectly still while the animation plays or a key is dragged. Settings: Editor Preferences > Widget Animations Editor >
 * Motion Paths, and the panel's Motion Path toggle.
 *
 * The layer takes no clicks of its own - only its diamonds and handles do - so the designer's own selection and handles work
 * through it everywhere else.
 */
class FWidgetAnimMotionPathExtension : public FDesignerExtension
{
public:
	FWidgetAnimMotionPathExtension();

	// FDesignerExtension
	virtual bool CanExtendSelection(const TArray<FWidgetReference>& Selection) const override;
	virtual void ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements) override;
};

/** Hands the designer a fresh motion path extension per designer opened. */
class FWidgetAnimMotionPathFactory : public IDesignerExtensionFactory, public TSharedFromThis<FWidgetAnimMotionPathFactory>
{
public:
	virtual ~FWidgetAnimMotionPathFactory() = default;

	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override;
};

namespace WidgetAnimMotionPaths
{
	/** Starts laying the motion path layer over every open designer whose Widget Blueprint has a Widget Animations panel open. */
	void Register();

	/** Takes every layer laid back off its designer and stops looking for more. */
	void Unregister();
}
