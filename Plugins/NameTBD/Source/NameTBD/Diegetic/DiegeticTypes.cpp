// Copyright AR GameSystems 2026. All rights reserved.

#include "DiegeticTypes.h"

// (The Diegetic Screen Widget interface's events need no bodies here: the engine generates their empty defaults.)

FDiegeticScreenLook FDiegeticScreenLook::MakePreset(EDiegeticLookPreset InPreset)
{
	FDiegeticScreenLook Look;
	Look.Preset = InPreset;

	switch (InPreset)
	{
	case EDiegeticLookPreset::TerminalGreen:
	case EDiegeticLookPreset::TerminalAmber:
		Look.PhosphorColor = InPreset == EDiegeticLookPreset::TerminalGreen ? FLinearColor(0.25f, 1.f, 0.45f) : FLinearColor(1.f, 0.62f, 0.15f);
		Look.Monochrome = 1.f;
		Look.Brightness = 1.6f;
		Look.Glow = 0.6f;
		Look.GlowRadius = 3.f;
		Look.ScanlineIntensity = 0.35f;
		Look.ScanlineCount = 480.f;
		Look.RollingBar = 0.12f;
		Look.Curvature = 0.06f;
		Look.ChromaticAberration = 0.002f;
		Look.Vignette = 0.45f;
		Look.Flicker = 0.03f;
		Look.Noise = 0.06f;
		break;

	case EDiegeticLookPreset::Hologram:
		Look.PhosphorColor = FLinearColor(0.35f, 0.85f, 1.f);
		Look.Monochrome = 0.6f;
		Look.Brightness = 2.f;
		Look.Additive = 1.f;
		Look.Glow = 0.8f;
		Look.GlowRadius = 5.f;
		Look.ScanlineIntensity = 0.25f;
		Look.ScanlineCount = 300.f;
		Look.ScanlineSpeed = 12.f;
		Look.Glitch = 0.15f;
		Look.EdgeFade = 0.6f;
		Look.Flicker = 0.06f;
		Look.Noise = 0.03f;
		break;

	case EDiegeticLookPreset::LCDPanel:
		Look.PixelGrid = 0.35f;
		Look.Glow = 0.15f;
		Look.Vignette = 0.1f;
		Look.Brightness = 1.2f;
		break;

	default:
		// Clean, and Custom's starting point: exactly as UMG draws it.
		break;
	}
	return Look;
}

FDiegeticGlass FDiegeticGlass::MakePreset(EDiegeticGlassPreset InPreset)
{
	FDiegeticGlass Glass;
	Glass.Preset = InPreset;

	switch (InPreset)
	{
	case EDiegeticGlassPreset::Clear:
		Glass.Refraction = 8.f;
		Glass.BevelWidth = 24.f;
		Glass.ChromaticAberration = 1.f;
		Glass.Sheen = 0.3f;
		Glass.EdgeHighlight = FLinearColor(1.f, 1.f, 1.f, 0.25f);
		break;

	case EDiegeticGlassPreset::Frosted:
		Glass.Blur = 20.f;
		Glass.Frost = 1.5f;
		Glass.Refraction = 6.f;
		Glass.BevelWidth = 20.f;
		Glass.ChromaticAberration = 1.f;
		Glass.Tint = FLinearColor(0.92f, 0.96f, 1.f, 0.3f);
		Glass.Brightness = 1.05f;
		Glass.Sheen = 0.25f;
		Glass.EdgeHighlight = FLinearColor(1.f, 1.f, 1.f, 0.2f);
		break;

	case EDiegeticGlassPreset::HeavyFrost:
		Glass.Blur = 40.f;
		Glass.Frost = 3.f;
		Glass.Refraction = 3.f;
		Glass.BevelWidth = 20.f;
		Glass.ChromaticAberration = 0.5f;
		Glass.Tint = FLinearColor(0.9f, 0.95f, 1.f, 0.35f);
		Glass.Brightness = 1.1f;
		Glass.Sheen = 0.2f;
		Glass.EdgeHighlight = FLinearColor(1.f, 1.f, 1.f, 0.15f);
		break;

	case EDiegeticGlassPreset::Smoked:
		Glass.Blur = 4.f;
		Glass.Refraction = 5.f;
		Glass.BevelWidth = 20.f;
		Glass.ChromaticAberration = 0.75f;
		Glass.Tint = FLinearColor(0.3f, 0.32f, 0.35f, 1.f);
		Glass.Sheen = 0.45f;
		Glass.EdgeHighlight = FLinearColor(1.f, 1.f, 1.f, 0.2f);
		break;

	case EDiegeticGlassPreset::SciFi:
		Glass.Blur = 8.f;
		Glass.Refraction = 14.f;
		Glass.BevelWidth = 16.f;
		Glass.ChromaticAberration = 3.f;
		Glass.Tint = FLinearColor(0.7f, 0.9f, 1.f, 0.35f);
		Glass.Brightness = 1.1f;
		Glass.Sheen = 0.5f;
		Glass.EdgeHighlight = FLinearColor(0.4f, 0.85f, 1.f, 1.5f);
		break;

	default:
		// No Glass, and Custom's starting point: a pane that changes nothing.
		break;
	}
	return Glass;
}
