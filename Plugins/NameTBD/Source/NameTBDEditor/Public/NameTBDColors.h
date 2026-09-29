#pragma once

#include "CoreMinimal.h"


namespace NameTBD
{
	namespace Colors
	{
		const FLinearColor AnimationOrange_Normal   = FLinearColor(FColor(235, 168, 52));
		const FLinearColor AnimationOrange_Hovered  = FLinearColor(FColor(250, 196, 107));
		const FLinearColor AnimationOrange_Pressed  = FLinearColor(FColor(235, 196, 148));
		const FLinearColor AnimationOrange_Disabled = FLinearColor(FColor(186, 117, 52));
		
		namespace WidgetAnimEditor
		{
			// -------------------------------------------------------------------------------------------------
			// Layer types - the section bars in the Track Editor, and the accent on their tooltips.
			// Vivid on purpose: they are the loudest colors in the editor. Everything else stays muted around them.
			//
			// These are the defaults. Each user can change them in Editor Preferences > Plugins > Widget
			// Animations Editor > Track Colors - see UWidgetAnimEditorSettings - and the panel reads those.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor LayerFade(0.35f, 0.55f, 0.95f);
			inline const FLinearColor LayerScale(0.35f, 0.80f, 0.45f);
			inline const FLinearColor LayerSlide(0.20f, 0.75f, 0.80f);
			inline const FLinearColor LayerRotate(0.75f, 0.45f, 0.90f);
			inline const FLinearColor LayerPulse(0.90f, 0.45f, 0.60f);
			inline const FLinearColor LayerMaterialScalar(0.85f, 0.75f, 0.30f);
			inline const FLinearColor LayerMaterialVector(0.85f, 0.60f, 0.30f);
			inline const FLinearColor LayerTransform(AnimationOrange_Normal);
			// Every registered track type - the text tracks and any a game adds - shares this one, as they share the one
			// Registered Type slot; their names tell them apart.
			inline const FLinearColor LayerCustom(0.90f, 0.85f, 0.85f);
			// Every Blueprint track type that doesn't set a Track Color of its own. Blueprint blue, so a type a
			// designer made reads apart from the C++ ones at a glance.
			inline const FLinearColor LayerBlueprintType(0.20f, 0.50f, 0.95f);
			inline const FLinearColor LayerUnknown(0.50f, 0.50f, 0.50f);

			// -------------------------------------------------------------------------------------------------
			// Layer groups - the band down the left of a group's rows, and its summary bar.
			//
			// Not a fixed list: group N's color is generated, stepping around the hue circle by the golden angle
			// from GroupHueStart, so any number of groups stay distinct from their neighbors. Kept low in
			// saturation and value so a group reads as a quiet grouping cue and never competes with the layer
			// colors above. Hue is 0-1 around the circle.
			// -------------------------------------------------------------------------------------------------

			inline constexpr float GroupHueStart = 0.58f;
			inline constexpr float GroupSaturation = 0.32f;
			inline constexpr float GroupValue = 0.62f;

			// -------------------------------------------------------------------------------------------------
			// Sections (layer bars)
			// -------------------------------------------------------------------------------------------------

			/**
			 * A section is drawn like a graph node's title: a dark rounded frame, the node gloss, the layer color
			 * spilled over it, and a sheen along the top. The frame shows as a lip along the bottom: SectionLipRatio
			 * of the section's height, kept between SectionLipMin and SectionLipMax pixels.
			 */
			/** Part transparent, as a graph node's frame is, so a selected section's orange outline bleeds into its edge. */
			inline const FLinearColor SectionFrame(0.00f, 0.00f, 0.00f, 1.0f);
			inline constexpr float SectionFrameThickness = 0.01f;
			inline constexpr float SectionLipRatio = 0.25f;
			inline constexpr float SectionLipMin = 3.0f;
			inline constexpr float SectionLipMax = 6.0f;

			/** How tall the highlight sheen along a section's top is drawn, in pixels. */
			inline constexpr float SectionSheenHeight = 20.0f;

			/** How much a hovered section's color is lifted. */
			inline constexpr float SectionHoverLift = 1.15f;

			/**
			 * Section text uses the editor's own graph node title style ("Graph.Node.NodeTitle" - font, color and
			 * shadow), so it matches a macro node's title exactly. Only these are ours: how far text sits in from the
			 * section's ends, and how much softer the easing name on the right is than the layer name on the left.
			 */
			inline constexpr float SectionTextPadding = 12.0f;
			inline constexpr float SectionSecondaryTextOpacity = 0.75f;

			/**
			 * A selected section gets a selected Blueprint node's outline: a solid orange rim SectionSelectedOutlineSize
			 * pixels out around the section, under its frame - the frame's alpha (SectionFrame, below 1) is what lets
			 * the orange bleed into its edge - and a faint halo SectionSelectedHaloSize pixels further out.
			 */
			inline const FLinearColor SectionSelectedOutline = FLinearColor(FColor(236, 150, 18));
			inline const FLinearColor SectionSelectedHalo = FLinearColor(FColor(236, 150, 18)).CopyWithNewOpacity(0.30f);
			inline constexpr float SectionSelectedOutlineSize = 2.0f;
			inline constexpr float SectionSelectedHaloSize = 1.5f;

			/**
			 * How wide the band round a section's edge is that the orange bleeds into: the frame is opaque-backed
			 * everywhere else, so only this outermost edge lets the outline through.
			 */
			inline constexpr float SectionBleedWidth = 1.0f;
			inline const FLinearColor SectionBacking(0.00f, 0.00f, 0.00f, 1.00f);

			/**
			 * The opaque body under a section's gloss and color spill - what a graph node's title sits on. Those
			 * two textures are partly transparent, so whatever is under them shows through; this is what does, the
			 * same on every section. SectionFrame only ever shows as the section's edge and bottom lip.
			 */
			inline const FLinearColor SectionBody(0.02f, 0.02f, 0.02f, 1.00f);

			/**
			 * A track that can't be edited - locked on its own, or the whole panel locked read-only - is drawn washed
			 * out: its color moved LockedDesaturate of the way to gray, then dimmed to LockedBrightness.
			 */
			inline constexpr float LockedDesaturate = 0.75f;
			inline constexpr float LockedBrightness = 0.55f;

			/** The lock glyph drawn on a locked section, and its size in pixels. */
			inline const FLinearColor LockedGlyph(1.00f, 1.00f, 1.00f, 0.70f);
			inline constexpr float LockedGlyphSize = 11.0f;

			/** The strip lit along a section's edge while it can be dragged to resize, and while it is being dragged. */
			inline const FLinearColor SectionEdgeHandle(1.00f, 1.00f, 1.00f, 0.55f);
			inline constexpr float SectionEdgeHandleWidth = 2.0f;

			/**
			 * A value-driven track (Timing = Value) is striped: diagonal bands SectionValueStripeWidth pixels wide,
			 * every SectionValueStripeSpacing pixels, across its color. Gameplay's value decides where it is, so its
			 * length on the timeline is only how long its preview sweep takes - the stripes and "(Value Driven)" in
			 * place of its length say so at a glance. Its color spill is dimmed to SectionValueSpillOpacity under them.
			 */
			inline const FLinearColor SectionValueStripe(1.00f, 1.00f, 1.00f, 0.07f);
			inline constexpr float SectionValueStripeWidth = 3.0f;
			inline constexpr float SectionValueStripeSpacing = 9.0f;
			inline constexpr float SectionValueSpillOpacity = 0.6f;

			/**
			 * A staggered track (see WidgetAnimExpander.h) is drawn as its first item, with a tail running on to where its
			 * last item ends: the tail in the track's color at SectionStaggerTailOpacity, SectionStaggerTailHeight of the
			 * section's height, with a tick where each item after the first starts when the preview knows how many there are.
			 */
			inline constexpr float SectionStaggerTailOpacity = 0.35f;
			inline constexpr float SectionStaggerTailHeight = 0.45f;
			inline const FLinearColor SectionStaggerTick(1.00f, 1.00f, 1.00f, 0.35f);

			// -------------------------------------------------------------------------------------------------
			// Keys
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor Key(0.85f, 0.85f, 0.85f);
			inline const FLinearColor KeyHovered(1.00f, 1.00f, 1.00f);
			inline const FLinearColor KeyReadOnly(0.42f, 0.42f, 0.42f);
			inline const FLinearColor KeySelected(AnimationOrange_Normal);
			inline const FLinearColor KeySelectedGlow = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.28f);
			inline const FLinearColor KeyOutline(0.00f, 0.00f, 0.00f, 0.90f);

			/** A key diamond's size: KeySizeRatio of the row height, kept between KeySizeMin and KeySizeMax pixels. */
			inline constexpr float KeySizeRatio = 0.26f;
			inline constexpr float KeySizeMin = 5.0f;
			inline constexpr float KeySizeMax = 9.0f;

			/** The small keys summarized on a group's row. Dark-outlined, so they read on any group color. */
			inline const FLinearColor KeySummary(0.80f, 0.80f, 0.80f);
			inline const FLinearColor KeySummaryOutline(0.00f, 0.00f, 0.00f, 0.85f);

			/**
			 * A track some of whose segments - the stretches between two keys - have curves of their own names each
			 * segment's curve in small text in the middle of its stretch, instead of one easing on the right. A
			 * segment with its own curve is named at SegmentLabelOwnCurveOpacity, one left at the track's Easing at
			 * SegmentLabelTrackEasingOpacity, so what was changed stands out.
			 */
			inline constexpr float SegmentLabelOwnCurveOpacity = 0.95f;
			inline constexpr float SegmentLabelTrackEasingOpacity = 0.45f;

			/** The selection box dragged out over empty space in the Track Editor: a faint fill, and a thin outline. */
			inline const FLinearColor MarqueeFill = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.08f);
			inline const FLinearColor MarqueeOutline = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.70f);

			// -------------------------------------------------------------------------------------------------
			// Timeline
			// -------------------------------------------------------------------------------------------------

			/** The playhead - the scrubber - in the animation orange of the Play button, so it never reads as the end line. */
			inline const FLinearColor Playhead(AnimationOrange_Normal);
			inline const FLinearColor PlayheadText(0.02f, 0.02f, 0.02f);
			inline const FLinearColor RangeStart(0.20f, 0.62f, 0.24f);
			inline const FLinearColor RangeEnd(0.72f, 0.10f, 0.08f);
			inline const FLinearColor OutOfRangeShade(0.00f, 0.00f, 0.00f, 0.28f);
			inline const FLinearColor GridMajor(1.00f, 1.00f, 1.00f, 0.055f);
			inline const FLinearColor GridMinor(1.00f, 1.00f, 1.00f, 0.022f);
			inline const FLinearColor HoverTimeLine(1.00f, 1.00f, 1.00f, 0.12f);
			inline const FLinearColor RowSeparator(0.00f, 0.00f, 0.00f, 0.45f);

			/** The line drawn through every row at the time a dragged track or key has snapped to. */
			inline const FLinearColor SnapLine = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.85f);

			/** The Read Only label beside the panel's lock while it is on. */
			inline const FLinearColor ReadOnlyLabel(AnimationOrange_Normal);

			/** Behind a small inline label - a group's target. */
			inline const FLinearColor Pill(0.00f, 0.00f, 0.00f, 0.35f);

			/** How strongly the editor theme's Select and Hover colors tint a selected or hovered row. */
			inline constexpr float RowSelectedTintAlpha = 0.16f;
			inline constexpr float RowHoveredTintAlpha = 0.35f;

			/** The Animation Properties button along the bottom of the list: faint until hovered, or while the animation is selected. */
			inline const FLinearColor AnimationPropertiesText = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.55f);

			/** The thin bar down the left of a track picked in the list. */
			inline const FLinearColor ListSelectedAccent(AnimationOrange_Normal);

			/** The name of the control a pinned tooltip is for - "Ping Pong" - bold, above what it does. */
			inline const FLinearColor TooltipControlName(AnimationOrange_Normal);

			/** A warning line at the bottom of a track's tooltip - what the track needs to work - bold, italic, in this. */
			inline const FLinearColor TooltipWarning(AnimationOrange_Normal);

			/** The Value Watcher's reading while a track is changing the value it watches. */
			inline const FLinearColor WatcherChanging(0.98f, 0.30f, 0.24f);

			/** The line a dragged track would move to in the Track Editor. */
			inline const FLinearColor LineDropTarget = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.10f);

			/** The slim bar on the Track Editor's right edge showing where it is scrolled to, when its lines overflow. */
			inline const FLinearColor ScrollIndicator(1.00f, 1.00f, 1.00f, 0.22f);

			// -------------------------------------------------------------------------------------------------
			// Track tooltips - hovering a track or a key in the Track Editor
			//
			// Values at the start of a track read in the green of the animation's start line, values at its end in
			// the red of its end line - lifted a little from RangeStart / RangeEnd so they read as text.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor TooltipBackground(0.012f, 0.012f, 0.014f, 0.97f);
			inline const FLinearColor TooltipOutline(1.00f, 1.00f, 1.00f, 0.10f);
			inline const FLinearColor TooltipDivider(1.00f, 1.00f, 1.00f, 0.08f);
			inline const FLinearColor TooltipTitle(1.00f, 1.00f, 1.00f, 1.00f);
			inline const FLinearColor TooltipLabel(1.00f, 1.00f, 1.00f, 0.50f);
			inline const FLinearColor TooltipValue(1.00f, 1.00f, 1.00f, 0.90f);
			inline const FLinearColor TooltipStartValue(0.32f, 0.86f, 0.36f);
			inline const FLinearColor TooltipEndValue(0.98f, 0.30f, 0.24f);
			inline const FLinearColor TooltipTableHeader(1.00f, 1.00f, 1.00f, 0.40f);

			/** Most keys a Transform tooltip lists before it shows the first and last few and counts the rest. */
			inline constexpr int32 TooltipMaxKeyRows = 10;

			// -------------------------------------------------------------------------------------------------
			// Animation Controls - the pill of Loop / Ping Pong / Invert / Speed, and the transport
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor ControlsPill(0.00f, 0.00f, 0.00f, 0.30f);
			inline const FLinearColor ControlsDivider(1.00f, 1.00f, 1.00f, 0.14f);
			inline const FLinearColor ToggleOnBackground = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.30f);
			inline const FLinearColor ToggleOnText(1.00f, 1.00f, 1.00f);
			inline const FLinearColor ToggleOffText(1.00f, 1.00f, 1.00f, 0.55f);
			inline const FLinearColor PlayTint(AnimationOrange_Normal);
			inline const FLinearColor PauseTint(AnimationOrange_Disabled);

			// -------------------------------------------------------------------------------------------------
			// The inline track editor in a Widget Animation Component's Details - its resize grip along the bottom
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor InlineEditorGrip(1.00f, 1.00f, 1.00f, 0.10f);
			inline const FLinearColor InlineEditorGripHovered = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.70f);
			inline const FLinearColor InlineEditorBorder(0.00f, 0.00f, 0.00f, 0.35f);

			// -------------------------------------------------------------------------------------------------
			// Context tracks - the read-only tracks of the animation an additive list starts within, drawn above
			// the list's own
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor ContextRowBackground(0.00f, 0.00f, 0.00f, 0.22f);
			inline const FLinearColor ContextRowsDivider(1.00f, 1.00f, 1.00f, 0.18f);
			inline constexpr float ContextTrackOpacity = 0.55f;
			inline const FLinearColor ContextTrackText(1.00f, 1.00f, 1.00f, 0.55f);
			inline const FLinearColor StartLinkedText = FLinearColor(AnimationOrange_Normal);
		}
	}
}