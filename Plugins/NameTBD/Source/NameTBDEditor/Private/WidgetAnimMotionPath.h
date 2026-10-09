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
 * itself, with nothing selected, whenever the Widget Blueprint's Widget Animations panel has an animation open. The designer
 * ticks every extension each frame it is on screen, selection or not (FDesignerExtension::Tick): from there the extension
 * keeps one layer over its designer - in the overlay of the designer's window, kept to the designer's own area, and drawing
 * nothing on a frame the designer isn't drawn (another tab in front, the Graph mode). Only where no window could be found is
 * the layer handed to the designer for a selection instead. The panel need not be on screen. The path is worked out from exactly what the preview plays
 * (UWidgetAnimLibrary::EvaluateExpandedStackAtTime on the panel's own settings and baselines), so every easing, segment curve,
 * spring, overshoot, stagger and delay is in it, and nothing is moved to find it. It is mapped onto the canvas through the
 * widgets' layout alone - never their render transforms, which the preview changes every frame - so the path and its keys stay
 * perfectly still while the animation plays or a key is dragged. Settings: Editor Preferences > Widget Animations Editor >
 * Motion Paths, and the panel's Motion Path toggle.
 *
 * The layer takes no clicks of its own - only its diamonds and handles do - so the designer's own selection and handles work
 * through it everywhere else. The diamonds and handles are placed as they are laid out, from where the path was worked out
 * that same frame - never through bound attributes, which Slate reads before the frame's path is worked out.
 */
class SWindow;
struct FWidgetAnimMotionPathDesignerFrame;

class FWidgetAnimMotionPathExtension : public FDesignerExtension
{
public:
	FWidgetAnimMotionPathExtension();
	virtual ~FWidgetAnimMotionPathExtension();

	// FDesignerExtension
	virtual bool CanExtendSelection(const TArray<FWidgetReference>& Selection) const override;
	virtual void ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements) override;

	/** Each frame the designer is on screen: where it is, and the layer kept over it. */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** Lays the layer over the designer's window - again when the designer moved to another window. */
	void UpdateHost();

	/** Takes the layer back off its window. */
	void RemoveHost();

	/** Where the designer is and the frame it was last drawn - shared with the layer, which reads it as it draws. */
	TSharedPtr<FWidgetAnimMotionPathDesignerFrame> DesignerFrame;

	TSharedPtr<SWidget> HostCanvas;
	TWeakPtr<SWindow> HostWindow;
	double NextHostCheck = 0.0;
};

/** Hands the designer a fresh motion path extension per designer opened. */
class FWidgetAnimMotionPathFactory : public IDesignerExtensionFactory, public TSharedFromThis<FWidgetAnimMotionPathFactory>
{
public:
	virtual ~FWidgetAnimMotionPathFactory() = default;

	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override;
};
