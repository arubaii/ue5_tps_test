// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "DesignerExtension.h"
#include "IHasDesignerExtensibility.h"
#include "WidgetReference.h"

/**
 * The Widget Gizmo: moves, scales, rotates and shears the widgets selected in the UMG designer, the way a 2D tool's transform
 * gizmo does (Unreal's viewport, Blender, Figma, After Effects, Rive) - not a widget, nothing to place: it shows over every
 * widget clicked in the designer.
 *
 * It edits the widget's Render Transform (Translation, Scale, Shear, Angle) about its Render Transform Pivot, and sits on that
 * pivot. Its modes, each with its own handles:
 *  - Move: an X arrow and a Y arrow pointing up (along the widget's parent's axes, which is what Translation moves along) and a
 *    square between them that moves freely. Shift on the square keeps the move to the axis moved along most.
 *  - Scale: handles on a frame a little outside the widget's box (clear of the designer's own outline and handles): corners
 *    scale both ways in proportion (Shift: freely), edges one way (Shift: both, in proportion); always about the pivot.
 *  - Rotate: a ring about the pivot. Shift turns in steps of Rotation Snap.
 *  - Shear: a grip on each edge of the frame; the top and bottom slide sideways, the sides up and down. Shift: Shear Snap steps.
 * In every mode the gizmo itself - its middle, the pivot - can be dragged: that moves the Render Transform Pivot, and the
 * Translation is made up for it, so the widget stays exactly where it is. The pivot snaps to the box's corners, edge middles and
 * middle (Ctrl held: free). Right-click a handle to reset what it edits (Pivot to Center, Position / X / Y, Scale, Rotation,
 * Shear, or the whole Render Transform). Escape or a right click during a drag puts everything back. Every edit is one undo step.
 *
 * Keys, as Blender's (Editor Preferences > Plugins > Widget Gizmo): G grabs - the widgets follow the mouse without a button held
 * until a click or Enter (X / Y keep it to an axis); S, R and Z switch to Scale, Rotate and Shear. After any of them a number
 * typed sets the value: R 90 Enter, S 1.5 Enter (both axes), S X 1.5 Y 1.25 Enter, G X 100 Y -20 Enter, Z X 10 Enter - X / Y /
 * Tab pick the axis, Backspace and minus edit, Escape puts it back. A bar under the chips shows what is typed. S / R / Z again
 * with nothing typed goes back to Move; H hides the gizmo and shows it again. The keys act only while the designer has the
 * keyboard, or the mouse is over it and no text is being typed, and a widget is selected.
 *
 * Keying: while the Widget Animations panel poses its preview at the playhead (an animation open and the panel on screen), the
 * gizmo shows the widget as posed there, and what it does is keyed - see SWidgetAnimTimeline::BeginGizmoKeying: each channel goes
 * to a track on the widget that holds it (Transform: all four; Scale: Scale; Rotate: Angle) with a key at the playhead - the more
 * directed one when both have a key - else the more directed one gets a key there. A channel no track takes, and the pivot, go
 * onto the widget as authored. The Auto Key chip says which keys are keyed.
 *
 * Drawn and hit-tested the way the motion paths are (see WidgetAnimMotionPath.h): the designer calls each extension's Paint
 * every frame it draws, after its preview, so the gizmo is part of the designer's own drawing, in its space and clipped to it;
 * the mouse and keys come through an input preprocessor that hit-tests what was drawn - only where the designer itself is
 * under the mouse - before the designer sees them. It is asked first, so on a widget with a motion path the gizmo's handles
 * win over a diamond under them (H hides the gizmo to reach it).
 *
 * Edits go to the Widget Blueprint's own widget (the template, which is saved) and to the designer's preview (so they show at
 * once). The gizmo is placed from the pose shown over the preview's layout - never the preview's render transform as last
 * drawn, which lags a frame behind a drag and would make the gizmo shake.
 */
namespace WidgetGizmoImpl
{
	class FGizmoLayer;
}

class FWidgetGizmoExtension : public FDesignerExtension
{
public:
	FWidgetGizmoExtension();
	virtual ~FWidgetGizmoExtension();

	// FDesignerExtension
	/** Every frame the designer is drawn, after its preview: the gizmo over each selected widget. */
	virtual void Paint(const TSet<FWidgetReference>& Selection, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId) const override;

private:
	/** This designer's gizmo - made on its first paint. */
	mutable TSharedPtr<WidgetGizmoImpl::FGizmoLayer> Layer;
};

/** Hands the designer a fresh gizmo extension per designer opened. */
class FWidgetGizmoFactory : public IDesignerExtensionFactory, public TSharedFromThis<FWidgetGizmoFactory>
{
public:
	virtual ~FWidgetGizmoFactory() = default;

	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override;
};
