// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWidgetAnimTimeline;
class SVerticalBox;
class SWidgetAnimGraphCanvas;
struct FWidgetAnimGraphState;

/**
 * The Widget Animations panel's Graph Editor: the tracks picked in the panel drawn as curves - value over time - in place
 * of the Track Editor, with their keys moved in time and value, keys added and deleted, and the motion between two keys
 * shaped by dragging its handles. Opened from the Track Editor's strip or a track's menu (see SWidgetAnimTimeline::OpenGraph);
 * the strip's breadcrumb leads back. While it is open the previews play only the tracks it shows.
 *
 * A key holds several values at once - a Transform key a whole render transform, a Material Vector key a color - and a
 * graph has one value axis. Production editors settle this the same two ways, and this one does both:
 *
 * - Values: each value a key holds is a channel of its own - Translation X, Scale Y, Angle; R, G, B, A - with its own curve,
 *   as Unreal's Curve Editor, Maya's Graph Editor and Blender's F-Curves show a transform. The channel list beside the graph
 *   shows and hides them; a key dragged up or down changes that one channel's value only, and dragged sideways moves the whole
 *   key - every channel - since its moment is shared. Channels of very different sizes (a translation of 200, a scale of 1)
 *   are drawn normalized, each fitted to the same height, as Blender's Normalize and Maya's Normalized view do; with one channel
 *   shown it is drawn in its own units.
 *
 * - Ease: the timing every channel of a stretch shares, drawn as After Effects' Speed Graph, Rive's interpolation editor and
 *   CSS's cubic-bezier() draw it - each stretch between two keys rising from 0 to 1 - because in this plugin, as in those, how
 *   a stretch eases belongs to the stretch, not to one value: a Transform key's translation, scale and angle all arrive
 *   together, along one curve. Its two handles are dragged here (or on any channel's curve in Values) and the segment becomes a
 *   Bezier segment (EWidgetAnimSegmentEasing::Bezier); every channel of that stretch follows.
 *
 * Every curve is drawn by the runtime's own sampling - the same easing, overshoot and spring code that plays the track - so
 * what the graph shows is what plays. Curves are drawn in their track's color; a track's channels are told apart by shade and
 * by name at the curve's end.
 */
class SWidgetAnimGraphEditor : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimGraphEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<SWidgetAnimTimeline> InOwner);

	/** The panel opened the graph on other tracks: their keys, unselected, and the whole of them in view. */
	void OnLayersShown();

	/** The graph's own keys selected, and whether any are - Escape clears them before it leaves the graph. */
	bool HasSelectedKeys() const;
	void ClearSelectedKeys();

	/** The whole of every shown curve in view - F, or the Fit button. */
	void FitView();

	/** The graph itself, to take the keyboard when it opens - so F, Delete and Escape are its own. */
	TSharedPtr<SWidget> GetFocusTarget() const;

	/** The Graph Editor's icon: the editor's own Curve Editor icon where this engine version has one, else the plugin's. */
	static const FSlateBrush* GetGraphIcon();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:

	/** The channel list beside the graph: every shown track, and under it each value its keys hold. */
	void RebuildChannelList();

	/** What the channel list was built from: the tracks shown, and which channels each has. A change rebuilds it. */
	uint32 ComputeChannelSignature() const;

	/** The strip across the top of the graph: Values or Ease, Normalize, the interpolation of the selected keys, and Fit. */
	TSharedRef<SWidget> MakeToolbar();

	TWeakPtr<SWidgetAnimTimeline> Owner;
	TSharedPtr<FWidgetAnimGraphState> State;
	TSharedPtr<SWidgetAnimGraphCanvas> Canvas;
	TSharedPtr<SVerticalBox> ChannelList;
	uint32 ChannelSignature = 0;
};
