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
			// Vivid on purpose: they are the loudest colours in the editor. Everything else stays muted around them.
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
			inline const FLinearColor LayerUnknown(0.50f, 0.50f, 0.50f);

			// -------------------------------------------------------------------------------------------------
			// Layer groups - the band down the left of a group's rows, and its summary bar.
			//
			// Not a fixed list: group N's colour is generated, stepping round the hue circle by the golden angle
			// from GroupHueStart, so any number of groups stay distinct from their neighbours. Kept low in
			// saturation and value so a group reads as a quiet grouping cue and never competes with the layer
			// colours above. Hue is 0-1 around the circle.
			// -------------------------------------------------------------------------------------------------

			inline constexpr float GroupHueStart = 0.58f;
			inline constexpr float GroupSaturation = 0.32f;
			inline constexpr float GroupValue = 0.62f;

			// -------------------------------------------------------------------------------------------------
			// Sections (layer bars)
			// -------------------------------------------------------------------------------------------------

			/**
			 * A section is drawn like a graph node's title: a dark rounded frame, the node gloss, the layer colour
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

			/** How much a hovered section's colour is lifted. */
			inline constexpr float SectionHoverLift = 1.15f;

			/**
			 * Section text uses the editor's own graph node title style ("Graph.Node.NodeTitle" - font, colour and
			 * shadow), so it matches a macro node's title exactly. Only these are ours: how far text sits in from the
			 * section's ends, and how much softer the easing name on the right is than the layer name on the left.
			 */
			inline constexpr float SectionTextPadding = 12.0f;
			inline constexpr float SectionSecondaryTextOpacity = 0.75f;

			/**
			 * A selected section gets a selected Blueprint node's outline: a solid orange rim SectionSelectedOutlineSize
			 * pixels out round the section, under its frame - the frame's alpha (SectionFrame, below 1) is what lets
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
			 * The opaque body under a section's gloss and colour spill - what a graph node's title sits on. Those
			 * two textures are partly transparent, so whatever is under them shows through; this is what does, the
			 * same on every section. SectionFrame only ever shows as the section's edge and bottom lip.
			 */
			inline const FLinearColor SectionBody(0.02f, 0.02f, 0.02f, 1.00f);

			/**
			 * A track that can't be edited - locked on its own, or the whole panel locked read-only - is drawn washed
			 * out: its colour moved LockedDesaturate of the way to grey, then dimmed to LockedBrightness.
			 */
			inline constexpr float LockedDesaturate = 0.75f;
			inline constexpr float LockedBrightness = 0.55f;

			/** The lock glyph drawn on a locked section, and its size in pixels. */
			inline const FLinearColor LockedGlyph(1.00f, 1.00f, 1.00f, 0.70f);
			inline constexpr float LockedGlyphSize = 11.0f;

			/** The strip lit along a section's edge while it can be dragged to resize, and while it is being dragged. */
			inline const FLinearColor SectionEdgeHandle(1.00f, 1.00f, 1.00f, 0.55f);
			inline constexpr float SectionEdgeHandleWidth = 2.0f;

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

			/** The small keys summarised on a group's row. Dark-outlined, so they read on any group colour. */
			inline const FLinearColor KeySummary(0.80f, 0.80f, 0.80f);
			inline const FLinearColor KeySummaryOutline(0.00f, 0.00f, 0.00f, 0.85f);

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

			/** How strongly the editor theme's Select and Hover colours tint a selected or hovered row. */
			inline constexpr float RowSelectedTintAlpha = 0.16f;
			inline constexpr float RowHoveredTintAlpha = 0.35f;

			/** The Animation Properties button along the bottom of the list: faint until hovered, or while the animation is selected. */
			inline const FLinearColor AnimationPropertiesText = FLinearColor(AnimationOrange_Normal).CopyWithNewOpacity(0.55f);

			/** The thin bar down the left of a track picked in the list. */
			inline const FLinearColor ListSelectedAccent(AnimationOrange_Normal);

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
		}
	}
}