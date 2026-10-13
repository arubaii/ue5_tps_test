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
 *   so soft edges and glass panels come out as in UMG), two-sided with the back face hidden unless the screen is Two Sided,
 *   after depth of field, responsive anti-aliasing, no fog. The scene's exposure undone (Eye Adaptation), by
 *   IgnoreSceneExposure. Every Screen Look: phosphor monochrome, glow, scan lines, rolling bar, curvature, colour fringing,
 *   vignette, flicker, grain, LCD pixel grid, hologram (additive, edge fade, glitch).
 * - M_DiegeticScreen_Glass: the same, plus the world behind blurred (12 Scene Color taps on a spiral, radius in the widget's
 *   pixels turned into the view's) where the widget is transparent - Background Blur, GlassTint, BlurBrightness - and over
 *   the Background Blur widgets' areas (BlurRegion0..7, rounded rectangles, each with its own radius).
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
