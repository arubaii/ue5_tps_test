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
			inline const FLinearColor LayerShake(0.95f, 0.55f, 0.35f);
			inline const FLinearColor LayerMaterialScalar(0.85f, 0.75f, 0.30f);
			inline const FLinearColor LayerMaterialVector(0.85f, 0.60f, 0.30f);
			inline const FLinearColor LayerTransform(AnimationOrange_Normal);
			// The keyframed shape tracks: a Shape Box track's outline, and a Shape track's shape settings.
			inline const FLinearColor LayerShapeBox(0.95f, 0.75f, 0.40f);
			inline const FLinearColor LayerShape(0.40f, 0.85f, 0.70f);
			inline const FLinearColor LayerDesign(0.88f, 0.52f, 0.86f);
			// Every registered track type - the text tracks and any a game adds - shares this one, as they share the one
			// Registered Type slot; their names tell them apart.
			inline const FLinearColor LayerCustom(0.90f, 0.85f, 0.85f);
			// Every Blueprint track type that doesn't set a Track Color of its own. Blueprint blue, so a type a
			// designer made reads apart from the C++ ones at a glance.
			inline const FLinearColor LayerBlueprintType(0.20f, 0.50f, 0.95f);
			// Every cue track - Event, Sound, Play Animation, Blueprint cues - that doesn't set a color of its own. A warm
			// red, apart from every type that moves something: a cue only makes something happen.
			inline const FLinearColor LayerCue(0.92f, 0.36f, 0.30f);
			inline const FLinearColor LayerUnknown(0.50f, 0.50f, 0.50f);
			// An animation track - another Widget Animation inserted whole as one track. Not a setting: the animation
			// orange is what says "a whole animation" everywhere in the panel.
			inline const FLinearColor LayerAnimation(AnimationOrange_Normal);

			// -------------------------------------------------------------------------------------------------
			// An animation track opened up - the inserted animation's own groups and tracks, shown under it, read-only.
			// Tinted with the animation orange so it reads as another animation's, not this one's:
			//   NestedTint: laid over the rows it shows, in the Track Editor and in the list.
			//   NestedHeaderTint: over its groups' rows, a little stronger, as a group's row is a header.
			//   NestedEdge: the strip down the left of those rows, where a group's band would be, NestedEdgeWidth pixels wide.
			//   NestedTrackOpacity: how strongly its tracks are drawn - plainly there, plainly not editable here.
			// -------------------------------------------------------------------------------------------------
			inline const FLinearColor NestedTint = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.06f);
			inline const FLinearColor NestedHeaderTint = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.12f);
			inline const FLinearColor NestedEdge = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.55f);
			inline constexpr float NestedEdgeWidth = 3.0f;
			inline constexpr float NestedTrackOpacity = 0.8f;

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
			// The macro node's half-tone line: part way down a track's body, everything below it is a shade darker,
			// most at the right end and fading out toward the left, so the line disappears as it runs left. The defaults
			// are measured off the macro node: the line 61% down, the right half of the width darkened, from nothing at
			// its middle to about a quarter darker at its right end, a little slower at first than an even fade.
			//   SectionHalfTone: the shade laid over the body below the line; alpha is how dark at the right end (0 turns it off).
			//   SectionHalfToneLine: where the line sits, as a share of the body's height from its top.
			//   SectionHalfToneReach: how far left it reaches before it has faded out, as a share of the track's width.
			//   SectionHalfToneMaxReach: a cap on that in pixels; 0 for none, which is how the macro node does it.
			//   SectionHalfToneFalloff: how it fades - 1 evenly, higher keeps it to the right end.
			inline const FLinearColor SectionHalfTone(0.00f, 0.00f, 0.00f, 0.26f);
			inline constexpr float SectionHalfToneLine = 0.61f;
			inline constexpr float SectionHalfToneReach = 0.50f;
			inline constexpr float SectionHalfToneMaxReach = 0.0f;
			inline constexpr float SectionHalfToneFalloff = 1.2f;

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

			/**
			 * A cue track (see WidgetAnimCue.h) marks each point it fires at with a line down through the section - drawn
			 * under the key - CueMarkerWidth pixels wide; a point it doesn't fire at (its end with Fire At End off) has no
			 * line and a faded key, CueSilentKeyOpacity.
			 */
			inline const FLinearColor CueMarker(1.00f, 0.95f, 0.85f, 0.85f);
			inline constexpr float CueMarkerWidth = 2.0f;
			inline constexpr float CueSilentKeyOpacity = 0.35f;

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
			// The animation's start and end: bright, so they read at once against the tracks and apart from the playhead's
			// orange. Each is a line through the tracks and a flag in the ruler's bottom strip - where it is dragged by, below
			// the playhead's tag, so the two never sit in each other's way. RangeLine is how strongly the line is drawn.
			// A group's line in the Track Editor: the stretch its tracks run as a quiet band of its color - GroupBandHeightRatio of
			// the line's height, filled at GroupBandFillOpacity, a hairline along its top and bottom at GroupBandEdgeOpacity, and
			// a solid cap GroupBandCapWidth pixels wide at each end.
			inline constexpr float GroupBandHeightRatio = 0.42f;
			inline constexpr float GroupBandFillOpacity = 0.28f;
			inline constexpr float GroupBandEdgeOpacity = 0.70f;
			inline constexpr float GroupBandCapWidth = 2.0f;

			inline const FLinearColor RangeStart(0.18f, 1.00f, 0.32f);
			inline const FLinearColor RangeEnd(1.00f, 0.16f, 0.12f);
			inline constexpr float RangeLineOpacity = 0.80f;

			// The playhead's line under the ruler: strongest at the top and fading down the tracks to PlayheadFadeTo of its
			// color, PlayheadTopWidth pixels wide where it leaves its tag, tapering to one pixel by PlayheadTaper of the way down.
			inline constexpr float PlayheadFadeTo = 0.10f;
			inline constexpr float PlayheadTopWidth = 2.0f;
			inline constexpr float PlayheadTaper = 0.30f;
			inline constexpr float PlayheadGlowOpacity = 0.22f;
			inline const FLinearColor OutOfRangeShade(0.00f, 0.00f, 0.00f, 0.28f);

			// On a percentage timeline with tracks reaching past 100%: what lies beyond it, which a progress of 1 never
			// reaches - shaded like outside the animation, and its 100% line in a quieter red than the end's.
			inline const FLinearColor PercentPastEndShade(0.00f, 0.00f, 0.00f, 0.20f);
			inline const FLinearColor PercentEndLine(0.72f, 0.10f, 0.08f, 0.55f);
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

			/**
			 * A folder of groups in the Animation Tracks list (see FWidgetAnimLayerGroup::EditorFolder): the strip down the left
			 * of the groups in it, and its icon and menu heading.
			 */
			inline const FLinearColor ListFolderStrip(0.00f, 0.00f, 0.00f, 0.22f);
			inline const FLinearColor ListFolderAccent(0.86f, 0.72f, 0.40f, 1.00f);

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

			// -------------------------------------------------------------------------------------------------
			// Context menu icons - Add Animation Track in green, the Delete entries in red. Brighter than they
			// show: the menu dims its icons at rest (see FNameTBDStyle::Create).
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor MenuAddIconTint(0.35f, 0.95f, 0.45f);
			inline const FLinearColor MenuDeleteIconTint(1.00f, 0.32f, 0.30f);

			// -------------------------------------------------------------------------------------------------
			// Track text - the name, length, easing and segment curves drawn on a track (and the labels on stagger
			// tails and context tracks), with a slight drop shadow under them for legibility.
			// TrackTextShadowOffset: how far the shadow sits down and to the right, in pixels.
			// TrackTextShadow: its color; the alpha is how strong it is (0 turns it off).
			// -------------------------------------------------------------------------------------------------

			inline constexpr float TrackTextShadowOffset = 1.0f;
			// How far a hidden track's text leans when its font has no italic typeface and it is slanted instead:
			// pixels right per pixel up.
			inline constexpr float TrackTextSlant = 0.2f;
			inline const FLinearColor TrackTextShadow(0.00f, 0.00f, 0.00f, 0.55f);
			// The smallest a track's text is shrunk to, zoomed out, before it is left out - as a share of its size.
			inline constexpr float TrackTextMinShrink = 0.5f;

			// -------------------------------------------------------------------------------------------------
			// The Track Editor's strip - Save / Update Animation Asset, drawn in the animation orange with black text
			// (see the AnimationOrange colors above), and grayed out while there is nothing to save; and the faint chip
			// behind Fit, so it reads as a button to press rather than a toggle.
			// -------------------------------------------------------------------------------------------------

			//   SaveButtonNormal: at rest, a dimmed animation orange so it doesn't shout; SaveButtonHovered: the full orange
			//   under the mouse; SaveButtonPressed: a touch darker while pressed.
			inline const FLinearColor SaveButtonNormal = FLinearColor(AnimationOrange_Normal * 0.62f).CopyWithNewOpacity(1.f);
			inline const FLinearColor SaveButtonHovered(AnimationOrange_Normal);
			inline const FLinearColor SaveButtonPressed = FLinearColor(AnimationOrange_Normal * 0.82f).CopyWithNewOpacity(1.f);
			inline const FLinearColor SaveButtonDisabled(0.16f, 0.13f, 0.09f);
			inline const FLinearColor SaveButtonDisabledText(1.00f, 1.00f, 1.00f, 0.35f);
			inline const FLinearColor FitButtonChip(1.00f, 1.00f, 1.00f, 0.07f);
			inline const FLinearColor FitButtonChipHovered(1.00f, 1.00f, 1.00f, 0.14f);

			// -------------------------------------------------------------------------------------------------
			// Collapsed tracks - tracks folded into one, as a Blueprint collapses part of a graph.
			//   LayerCollapsed: the collapsed track's own color - its section, its row's accent, its name.
			//   CollapsedListTint: laid over its row in the Animation Tracks list, and its tracks' rows while open.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor LayerCollapsed(0.55f, 0.62f, 0.72f);
			inline const FLinearColor CollapsedListTint(0.55f, 0.62f, 0.72f, 0.06f);

			// A collapsed track's section is a facsimile of the Blueprint collapsed graph node: a dark blank body inside an even
			// frame, rounded at every corner, with a soft gray glow in its middle fading out toward its edges.
			//   CollapsedNodeFrame / CollapsedNodeBorder / CollapsedNodeRadius: the frame, its width all round, its corners.
			//   CollapsedNodeBody: the body. CollapsedNodeGlow: the glow at its strongest (its alpha is how strong).
			//   CollapsedNodeGlowReach: the glow's fade-in step from each end, as a share of the width, up to
			//   CollapsedNodeGlowMaxReach pixels (it is full 2.2 steps in). CollapsedNodeGlowFalloff: how quickly it fades toward the top and bottom.
			//   CollapsedNodeText: its name and track count, in italics, as a collapsed node's name is.
			inline const FLinearColor CollapsedNodeFrame = FLinearColor(FColor(14, 15, 14));
			inline constexpr float CollapsedNodeBorder = 2.0f;
			inline constexpr float CollapsedNodeRadius = 5.0f;
			inline const FLinearColor CollapsedNodeBody = FLinearColor(FColor(23, 24, 22));
			inline const FLinearColor CollapsedNodeGlow = FLinearColor(FColor(72, 73, 70));
			inline constexpr float CollapsedNodeGlowReach = 0.2f;
			inline constexpr float CollapsedNodeGlowMaxReach = 170.0f;
			inline constexpr float CollapsedNodeGlowFalloff = 1.7f;
			inline const FLinearColor CollapsedNodeText = FLinearColor(FColor(172, 171, 152));

			// A collapsed track open on its own: the Track Editor's edges darkened, ScopeVignetteWidth pixels in, to
			// ScopeVignette at the very edge (its alpha is how dark; 0 turns it off) - a nested editor - inside a thin
			// ScopeFrame line of the collapsed track's color.
			inline const FLinearColor ScopeVignette(0.00f, 0.00f, 0.00f, 0.38f);
			inline constexpr float ScopeVignetteWidth = 56.0f;
			inline const FLinearColor ScopeFrame(0.55f, 0.62f, 0.72f, 0.55f);

			// -------------------------------------------------------------------------------------------------
			// Pinned tracks - drawn at the top of the Track Editor, under the ruler, where they stay while the rest scroll.
			//   PinnedLineTint: laid over their lines. PinnedDivider: the line under the last of them.
			//   ListPinnedIcon: a pinned track's pin in the list. ListQuietIcon: the list's eye while its track plays -
			//   quiet, so a column of open eyes doesn't shout.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor PinnedLineTint(1.00f, 1.00f, 1.00f, 0.03f);
			inline const FLinearColor PinnedDivider = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.45f);
			inline const FLinearColor ListPinnedIcon(AnimationOrange_Normal);
			inline const FLinearColor ListQuietIcon(1.00f, 1.00f, 1.00f, 0.22f);

			// -------------------------------------------------------------------------------------------------
			// Playing backward - an inverted track, or every track while the preview's Invert is on.
			//   InvertChevron: the chevrons pointing back along an inverted track, InvertChevronSpacing pixels apart.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor InvertChevron(1.00f, 1.00f, 1.00f, 0.22f);
			inline constexpr float InvertChevronSpacing = 14.0f;

			// -------------------------------------------------------------------------------------------------
			// The rest of this pass's odds and ends.
			//   RangeHandleLit: the animation's start or end handle on the ruler, hovered or dragged.
			//   SoundFadeLine: a Sound cue's fade in and fade out, drawn as ramps over its track.
			//   PickingTargetText: a group's target while a widget is being picked for it in the designer.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor RangeHandleLit(AnimationOrange_Hovered);
			inline const FLinearColor SoundFadeLine(1.00f, 1.00f, 1.00f, 0.75f);
			inline const FLinearColor PickingTargetText(AnimationOrange_Normal);
			// Behind the name of the widget under the eyedropper, beside the cursor, while a target is being picked.
			inline const FLinearColor PickHoverBackground(0.02f, 0.02f, 0.02f, 0.90f);

			// How much further in from its end an animation track's right-hand text sits than any other track's, in
			// pixels - its "Widget Animation, N tracks" otherwise touches the section's rounded end.
			inline constexpr float AnimationTrackTextRightPadding = 6.0f;

			// -------------------------------------------------------------------------------------------------
			// Animation timelines - a Widget Blueprint's own animations.
			//   NoTimelineText: the timeline picker's "No timeline" while the Widget Blueprint has none - the orange of
			//     the one thing to click.
			//   ScriptTargetName: the name of the timeline or asset a script runs on, in the Script area's strip.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor NoTimelineText(AnimationOrange_Normal);
			inline const FLinearColor ScriptTargetName(AnimationOrange_Normal);

			// -------------------------------------------------------------------------------------------------
			// The Script area under the Track Editor - Python that makes tracks. See SWidgetAnimScriptPanel.
			//   ScriptPlain ... ScriptOperator: the code, colored as Python - quiet for the plain words, brighter for what
			//     shapes the code (keywords), and the animation orange for the scripting API's own names (anim, TrackType),
			//     so what a script can reach stands out.
			//   ScriptCodeBackground / ScriptOutputBackground: behind the code and behind what the last run printed.
			//   ScriptOutputInfo / Warning / Error: printed lines, warnings, errors and tracebacks.
			//   ScriptStatusDone / ScriptStatusFailed: the last line of a run - how it went.
			//   ScriptToggleOn: the Script button in the Track Editor's strip while the area is open.
			//   ScriptUnavailableText: what to do when Python isn't there.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor ScriptPlain(0.84f, 0.85f, 0.86f);
			inline const FLinearColor ScriptKeyword(0.78f, 0.52f, 0.92f);
			inline const FLinearColor ScriptConstant(0.40f, 0.64f, 1.00f);
			inline const FLinearColor ScriptString(0.62f, 0.82f, 0.45f);
			inline const FLinearColor ScriptComment(0.46f, 0.53f, 0.47f);
			inline const FLinearColor ScriptNumber(0.95f, 0.72f, 0.45f);
			inline const FLinearColor ScriptBuiltin(0.40f, 0.80f, 0.86f);
			inline const FLinearColor ScriptApi(AnimationOrange_Normal);
			inline const FLinearColor ScriptDefinition(0.98f, 0.86f, 0.45f);
			inline const FLinearColor ScriptOperator(0.66f, 0.68f, 0.72f);
			inline const FLinearColor ScriptCodeBackground(0.012f, 0.012f, 0.014f);
			inline const FLinearColor ScriptOutputBackground(0.020f, 0.020f, 0.022f);
			inline const FLinearColor ScriptOutputInfo(0.80f, 0.80f, 0.80f);
			inline const FLinearColor ScriptOutputWarning(0.98f, 0.80f, 0.30f);
			inline const FLinearColor ScriptOutputError(1.00f, 0.42f, 0.38f);
			inline const FLinearColor ScriptStatusDone(0.45f, 0.86f, 0.52f);
			inline const FLinearColor ScriptStatusFailed(1.00f, 0.42f, 0.38f);
			inline const FLinearColor ScriptToggleOn(AnimationOrange_Normal);
			inline const FLinearColor ScriptUnavailableText(0.85f, 0.85f, 0.85f);

			// The code editor (Monaco) in its Unreal theme - everything else it takes from the Script* colors above:
			//   ScriptLineHighlight: behind the line the cursor is on. ScriptLineNumber / ScriptLineNumberActive: the gutter's
			//   numbers, and the cursor line's. ScriptSelection: behind selected text. ScriptCursor: the cursor itself.
			inline const FLinearColor ScriptLineHighlight(0.020f, 0.020f, 0.024f);
			inline const FLinearColor ScriptLineNumber(0.10f, 0.10f, 0.11f);
			inline const FLinearColor ScriptLineNumberActive(0.36f, 0.36f, 0.38f);
			inline const FLinearColor ScriptSelection(0.03f, 0.08f, 0.18f);
			inline const FLinearColor ScriptCursor(AnimationOrange_Normal);

			// -------------------------------------------------------------------------------------------------
			// The Graph Editor - the selected tracks as curves, in place of the Track Editor (see SWidgetAnimGraphEditor).
			// Curves and keys are in their track's own color; these are everything around them:
			//   GraphTrackRunTint: how strongly each track's run is shaded in its color behind its curves.
			//   GraphChannelShadeStep: how much lighter or darker each further channel of a track is drawn than the first.
			//   GraphHoldOpacity: the faint lines holding a curve's first and last values out to the view's edges.
			//   GraphGutter / GraphRulerTick / GraphRulerText: the value gutter down the left, the ruler's ticks and numbers.
			//   GraphZeroLine: the value grid's line at 0, a little stronger than the rest.
			//   GraphHandle / GraphHandleLine: a segment's Bezier handles and the lines to their keys.
			// -------------------------------------------------------------------------------------------------
			inline constexpr float GraphTrackRunTint = 0.045f;
			inline constexpr float GraphChannelShadeStep = 0.22f;
			inline constexpr float GraphHoldOpacity = 0.28f;
			inline const FLinearColor GraphGutter(0.00f, 0.00f, 0.00f, 0.22f);
			inline const FLinearColor GraphRulerTick(1.00f, 1.00f, 1.00f, 0.30f);
			inline const FLinearColor GraphRulerText(0.62f, 0.62f, 0.62f);
			inline const FLinearColor GraphZeroLine(1.00f, 1.00f, 1.00f, 0.14f);
			inline const FLinearColor GraphHandle(0.95f, 0.95f, 0.95f);
			inline const FLinearColor GraphHandleLine(1.00f, 1.00f, 1.00f, 0.45f);

			// A track's Part of the Text that isn't in its text: the Selection panel's note under it, in a warning color.
			inline const FLinearColor TextTargetMissing(0.98f, 0.62f, 0.30f);
		}

		// =================================================================================================
		// The Widget Animation State Machine editor - see SWidgetAnimStateMachineEditor and WidgetAnimStateMachineGraph.
		// Laid out like an Animation Blueprint's state machine, so it reads the same at a glance.
		// =================================================================================================
		namespace StateMachineEditor
		{
			// -------------------------------------------------------------------------------------------------
			// Transitions - the wires between states.
			//   TransitionLine: a wire at rest. TransitionDisabled: one switched off (Enabled unticked).
			//   TransitionActive / TransitionIcon: no longer drawn - a wire in play runs with WireBubble dots now, and the
			//     transition node's colors are further down (TransitionTween and on). Kept for anything reading them.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor TransitionLine(0.78f, 0.78f, 0.80f, 0.90f);
			inline const FLinearColor TransitionActive(AnimationOrange_Normal);
			inline const FLinearColor TransitionDisabled(0.30f, 0.30f, 0.32f, 0.70f);
			inline const FLinearColor TransitionIcon(0.86f, 0.86f, 0.88f);

			// -------------------------------------------------------------------------------------------------
			// States.
			//   StateEdge / StateEdgeHovered: the ring round a state that a transition is dragged from - brighter under the
			//     mouse, so it reads as the thing to drag.
			//   StateBody: a state at rest. StateActive / StateLeaving: no longer used by the graph (the state in play wears the
			//     track's stripes instead - see ActiveTrack*); kept for anything else that lights a state.
			//   EntryBody / ExitBody / AnyStateBody: the three special nodes - green for where it starts, red for where it
			//     ends (Anim Out), blue for the transitions that can be taken from anywhere.
			//   StateTitle / StateTitleActive: a state's name, and the name of the one in play.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor StateEdge(0.12f, 0.12f, 0.13f);
			inline const FLinearColor StateEdgeHovered(0.55f, 0.55f, 0.58f);
			inline const FLinearColor StateBody(0.06f, 0.06f, 0.065f);
			inline const FLinearColor StateActive(AnimationOrange_Normal * FLinearColor(0.55f, 0.55f, 0.55f, 1.0f));
			inline const FLinearColor StateLeaving(0.22f, 0.16f, 0.07f);
			inline const FLinearColor EntryBody(0.06f, 0.22f, 0.08f);
			inline const FLinearColor ExitBody(0.26f, 0.05f, 0.05f);
			inline const FLinearColor AnyStateBody(0.05f, 0.12f, 0.26f);
			inline const FLinearColor StateTitle(0.90f, 0.90f, 0.90f);
			inline const FLinearColor StateTitleActive(1.0f, 1.0f, 1.0f);

			// -------------------------------------------------------------------------------------------------
			// The Inputs panel - each input's type as a small colored dot, matching the Blueprint pin colors of the
			// same types (Boolean red, Float green, an exec-like white for a Trigger), so they read as what they are.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor InputBool(0.30f, 0.0f, 0.0f);
			inline const FLinearColor InputNumber(0.357667f, 1.0f, 0.06f);
			inline const FLinearColor InputTrigger(1.0f, 1.0f, 1.0f);
			// A Press And Hold: the animation orange - it fills as it is held, as a hold ring would.
			inline const FLinearColor InputHold(AnimationOrange_Normal);
			// Integer: the Blueprint integer pin's teal. Choice: an enum's green, lighter so its label reads. Vector 2D: the
			// Blueprint vector pin's gold.
			inline const FLinearColor InputInteger(0.013575f, 0.770000f, 0.429609f);
			inline const FLinearColor InputChoice(0.10f, 0.55f, 0.45f);
			inline const FLinearColor InputVector2D(1.0f, 0.74f, 0.10f);

			// -------------------------------------------------------------------------------------------------
			// Wires - Blueprint splines from state to state.
			//   WireActive: the wire of the transition being taken - the dots' own orange, and thicker (WireActiveThickness);
			//     WireBubble: the dots that run along it, as they run along a wire in Blueprint simulation - bigger than the wire
			//     is thick, so they read as beads on it. WireBubbleSize / WireBubbleSpacing / WireBubbleSpeed:
			//     their size, how far apart they run and how fast, in graph units (and per second). WireLinger: seconds the effect
			//     fades out over once the transition is done, so a short blend is still seen to run.
			//   WireThickness / WireActiveThickness: in graph units.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor WireActive(AnimationOrange_Hovered);
			inline const FLinearColor WireBubble(AnimationOrange_Hovered);
			inline constexpr float WireBubbleSize = 9.0f;
			inline constexpr float WireBubbleSpacing = 32.0f;
			inline constexpr float WireBubbleSpeed = 90.0f;
			inline constexpr float WireLinger = 0.2f;
			inline constexpr float WireThickness = 1.5f;
			inline constexpr float WireActiveThickness = 4.0f;

			// -------------------------------------------------------------------------------------------------
			// The transition node - the Animation Blueprint's round transition node, tinted by how it blends:
			//   TransitionTween: the light gray it always was. TransitionSpring: mint. TransitionAnimation: the animation orange.
			//   TransitionCut: dark gray.
			//   TransitionArrow: the arrowhead pointing the way it goes (the state machine wires' own, Graph.Arrow), and
			//   TransitionArrowSize its size in graph units. TransitionSelected: the ring round it while selected.
			//   TransitionIdleDim: how much of its color it shows under its progress while it plays - the full color rises
			//   across it like a progress bar.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor TransitionTween(0.86f, 0.86f, 0.88f);
			inline const FLinearColor TransitionSpring(0.45f, 0.88f, 0.70f);
			inline const FLinearColor TransitionAnimation(AnimationOrange_Normal);
			inline const FLinearColor TransitionCut(0.42f, 0.42f, 0.45f);
			inline const FLinearColor TransitionArrow(0.03f, 0.03f, 0.035f, 0.90f);
			inline constexpr float TransitionArrowSize = 11.0f;
			inline const FLinearColor TransitionSelected(1.0f, 1.0f, 1.0f, 0.95f);
			
			// A selected transition node, as the graph editor shows a selected node - measured off a selected state - but round:
			// a crisp solid band TransitionSelectedWidth thick, starting TransitionSelectedOverlap inside the disc's edge (so no
			// gap shows), shading from TransitionSelectedTop at the top to TransitionSelectedBottom at the bottom. No glow.
			// In graph units - it grows with the zoom, as the editor's selection does: 3 units is the 5 pixels a selected state's
			// band measures at the zoom the screenshots were taken at.
			
			inline constexpr float TransitionSelectedWidth = 3.0f;
			inline constexpr float TransitionSelectedOverlap = 0.75f;
			inline const FLinearColor TransitionSelectedTop = FLinearColor(FColor(223, 159, 0));
			inline const FLinearColor TransitionSelectedBottom = FLinearColor(FColor(210, 116, 0));
			inline constexpr float TransitionIdleDim = 0.45f;

			// -------------------------------------------------------------------------------------------------
			// The length label beside a transition node: how long the transition takes. Text, and the dark pill behind it.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor LengthLabelText(1.0f, 1.0f, 1.0f, 0.78f);
			inline const FLinearColor LengthLabelBackground(0.015f, 0.015f, 0.018f, 0.78f);

			// -------------------------------------------------------------------------------------------------
			// States, in more detail.
			//   StateProgress: the fill rising across a state's middle while its animation plays, in the debugger - for a state
			//     with its own Node Color, that color brightened instead. StateProgressOpacity: how much of it shows (the name
			//     stays readable over it). StateProgressEdge / StateProgressEdgeWidth: the bright line at its front.
			//   StateProgressMinBrightness: how bright (HSV value) a Node Color is lifted to for its fill and its lit body.
			//   ActiveTrack*: the state the watched widget is in wears a value-driven track's diagonal bands from the Widget
			//     Animations panel - the track's width and spacing (WidgetAnimEditor::SectionValueStripe*) - over its own colors,
			//     scrolling right at ActiveTrackStripeSpeed graph units a second. ActiveTrackStripe: a touch stronger than the
			//     track's, which sit on a bright color. Mid-transition no state wears them.
			//     ActiveTrackRadius: the state's corners, which they stay inside.
			//   StateSelected: the outline of a selected state.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor StateProgress(AnimationOrange_Normal);
			inline constexpr float StateProgressOpacity = 0.72f;
			inline const FLinearColor StateProgressEdge(1.0f, 0.93f, 0.80f, 0.95f);
			inline constexpr float StateProgressEdgeWidth = 2.0f;
			inline constexpr float StateProgressMinBrightness = 0.80f;
			inline const FLinearColor ActiveTrackStripe(1.0f, 1.0f, 1.0f, 0.10f);
			inline constexpr float ActiveTrackStripeSpeed = 8.0f;
			inline constexpr float ActiveTrackStripeWidth = WidgetAnimEditor::SectionValueStripeWidth;
			inline constexpr float ActiveTrackStripeSpacing = WidgetAnimEditor::SectionValueStripeSpacing;
			inline constexpr float ActiveTrackRadius = 10.0f;

			// The glow round the state in play: ActiveRing, a line ActiveRingWidth thick laid over the node's outer rim
			// (ActiveRingInset: its middle that far out from the node's edge - the body brush draws its rim a little past the
			// node's box, so the line covers it entirely), following the edge's rounded corners (ActiveRingRadius, measured off
			// the state body brush), and a soft glow fading out beyond it over ActiveRingHaloReach in ActiveRingHaloSteps thin
			// lines, ActiveRingHaloOpacity at its brightest. It breathes
			// between ActiveRingMin and ActiveRingMax every ActiveRingPeriod seconds. Mid-transition no state glows.
			inline const FLinearColor ActiveRing(AnimationOrange_Normal);
			inline constexpr float ActiveRingWidth = 3.0f;
			inline constexpr float ActiveRingInset = 0.5f;
			inline constexpr float ActiveRingRadius = 12.0f;
			inline constexpr float ActiveRingHaloReach = 6.0f;
			inline constexpr int32 ActiveRingHaloSteps = 6;
			inline constexpr float ActiveRingHaloOpacity = 0.35f;
			inline constexpr float ActiveRingMin = 0.55f;
			inline constexpr float ActiveRingMax = 1.0f;
			inline constexpr float ActiveRingPeriod = 3.0f;
			// How far toward white the line on the rim of a glowing state goes while the mouse is over its edge (the edge's
			// own hover look is held back then: its corners would poke out past the glow).
			inline constexpr float ActiveRingHoverLift = 0.45f;

			// A state's middle while its animation's progress fills it: square-cornered, near black - the rounded one's dark.
			inline const FLinearColor StateTitleSharp(0.010f, 0.010f, 0.011f, 1.0f);
			inline const FLinearColor StateSelected(1.0f, 1.0f, 1.0f, 0.85f);

			// A selected state's outline while it glows (in play), drawn on its own rounded edge just outside the glow line: the
			// graph editor's selection orange, and how thick. A selected state not in play keeps the editor's own selection glow.
			inline const FLinearColor StateSelectedOutline = FLinearColor(FColor(242, 177, 0));
			inline constexpr float StateSelectedOutlineWidth = 2.5f;

			// -------------------------------------------------------------------------------------------------
			// The Inputs panel. InputRowSelected: behind the input whose settings the Details show. BuiltInUnused: a built-in
			// input the machine doesn't read.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor InputRowSelected = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.16f);
			inline const FLinearColor InputRowHovered(1.0f, 1.0f, 1.0f, 0.04f);
			inline const FLinearColor BuiltInUnused(1.0f, 1.0f, 1.0f, 0.32f);

			// -------------------------------------------------------------------------------------------------
			// Picking a preview target in the designer: the label beside the eyedropper.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor PickLabelBackground(0.02f, 0.02f, 0.02f, 0.92f);
			inline const FLinearColor PickLabelText(AnimationOrange_Normal);
		}

		namespace ShapeEditor
		{
			// -------------------------------------------------------------------------------------------------
			// Editing a Custom Shape's path on the designer's canvas. The outline is drawn twice - a dark line under a light
			// one - so it reads over any background the widget sits on. Selection is the animation orange, as everywhere
			// else in the plugin's editors.
			// -------------------------------------------------------------------------------------------------

			inline const FLinearColor PathUnderlay(0.0f, 0.0f, 0.0f, 0.75f);
			inline const FLinearColor PathLine(0.92f, 0.92f, 0.92f, 1.0f);
			inline const FLinearColor PathHovered(AnimationOrange_Hovered);
			inline const FLinearColor Selected(AnimationOrange_Normal);
			inline const FLinearColor VertexFill(1.0f, 1.0f, 1.0f, 1.0f);
			inline const FLinearColor VertexOutline(0.05f, 0.05f, 0.05f, 1.0f);
			inline const FLinearColor HandleLine(AnimationOrange_Normal.CopyWithNewOpacity(0.85f));
			inline const FLinearColor HandleFill(1.0f, 1.0f, 1.0f, 1.0f);
			inline const FLinearColor PenPreview(AnimationOrange_Normal.CopyWithNewOpacity(0.7f));
			inline const FLinearColor CloseRing(AnimationOrange_Hovered);
			inline const FLinearColor SnapGuide(1.0f, 0.25f, 0.55f, 0.9f);
			inline const FLinearColor MarqueeFill(AnimationOrange_Normal.CopyWithNewOpacity(0.08f));
			inline const FLinearColor MarqueeOutline(AnimationOrange_Normal.CopyWithNewOpacity(0.8f));
			inline const FLinearColor ToolbarBackground(0.02f, 0.02f, 0.02f, 0.92f);
			inline const FLinearColor ToolbarOutline(0.25f, 0.25f, 0.25f, 1.0f);
			inline const FLinearColor ToolActive(AnimationOrange_Normal);
			inline const FLinearColor ToolText(0.85f, 0.85f, 0.85f, 1.0f);
			inline const FLinearColor ToolTextActive(0.0f, 0.0f, 0.0f, 1.0f);

			// The toolbar's and the Details row's buttons ("chips"): a dimmed animation orange while a tool or kind is the
			// active one, with dark text; a faint fill otherwise. Each a little brighter under the mouse.
			inline const FLinearColor ChipActive(AnimationOrange_Normal.R * 0.78f, AnimationOrange_Normal.G * 0.78f, AnimationOrange_Normal.B * 0.78f, 1.0f);
			inline const FLinearColor ChipActiveHovered(AnimationOrange_Normal);
			inline const FLinearColor ChipIdle(1.0f, 1.0f, 1.0f, 0.07f);
			inline const FLinearColor ChipIdleHovered(1.0f, 1.0f, 1.0f, 0.16f);
			inline const FLinearColor ChipText(0.86f, 0.86f, 0.86f, 1.0f);
			inline const FLinearColor ChipTextActive(0.04f, 0.04f, 0.04f, 1.0f);
			inline constexpr float ChipDisabledOpacity = 0.35f;
			inline constexpr float ChipHeight = 22.0f;

			inline constexpr float VertexSize = 9.0f;
			inline constexpr float HandleSize = 7.0f;
			inline constexpr float PathThickness = 1.5f;
			inline constexpr float HitRadius = 7.0f;
		}
	}
}