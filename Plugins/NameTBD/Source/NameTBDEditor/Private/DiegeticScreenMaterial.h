// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/Diegetic/DiegeticScreenComponent.h"

class UMaterial;
class UMaterialInterface;

/**
 * The plugin's screen materials (a Diegetic Screen's Material Automatic), made by the editor - nothing to author, nothing in
 * the patch that only the editor can make:
 *
 * - M_DiegeticScreen (Panel): surface, unlit, AlphaComposite (premultiplied - what Slate draws into the widget's render target,
 *   so a widget's opacity is only opacity, as in UMG), two-sided with the back face hidden unless the screen is Two Sided,
 *   Output Depth and Velocity (pixels at least 0.9 opaque write depth: particles and other translucency behind its solid parts
 *   stay behind them, and temporal anti-aliasing moves them with the screen), before depth of field (the pass most translucency
 *   is drawn in), responsive anti-aliasing, no fog. The scene's exposure undone (Eye Adaptation), by IgnoreSceneExposure. The
 *   sample Screen Looks: phosphor monochrome, glow, scan lines, rolling bar, curvature, colour fringing, vignette, flicker, grain,
 *   LCD pixel grid, hologram (additive, edge fade, glitch); pixel grid and scan lines fade out where finer than the screen's
 *   pixels.
 * - M_DiegeticScreen_Glass: the same, with the world behind seen through glass under the widget's see-through parts - on the
 *   whole pane (GlassPane; corners rounded by GlassCornerRadius) and over the Background Blur widgets' areas (BlurRegion0..7,
 *   rounded rectangles, each with its own radius). 24 Scene Color taps (8 a channel) on a Vogel disk in the pane's own plane,
 *   turned per pixel and per frame so temporal anti-aliasing smooths them; bent by the bevel (GlassRefraction, GlassBevelWidth),
 *   ripples (GlassRipple) and frost (GlassFrost); red and blue split (GlassChromaticAberration); tinted (GlassTint), brightened
 *   (GlassBrightness), a sheen at grazing angles (GlassSheen) and a lit rim (GlassEdgeHighlight). No responsive anti-aliasing.
 * - M_DiegeticScreen_Opaque: opaque, for screens with Blend Mode Opaque.
 *
 * Built node by node (UMaterialEditingLibrary), the effects in Custom nodes of plain HLSL with no includes; the widget is
 * sampled with SampleLevel 0 (the widget's render target has no mips). Made in /NameTBD/Diegetic (the plugin's content), or
 * /Game/NameTBD/Diegetic when that can't be written, saved, and rebuilt in place when made by an older version (scalar
 * parameter DiegeticMaterialVersion).
 */
namespace DiegeticScreenMaterial
{
	/** The plugin's material for a variant, current: loaded - or, missing or out of date, queued to be made next tick (null now). */
	UMaterialInterface* Get(EDiegeticMaterialVariant Variant);

	/** Makes (or rebuilds) every variant that is missing or out of date, now; saves them; tells every screen. */
	void MakeMissing();

	/** Duplicates the plugin's material for a variant into Folder (a long package path), for a project to change. */
	UMaterial* CopyToProject(EDiegeticMaterialVariant Variant, const FString& Folder, FText& OutError);
}
