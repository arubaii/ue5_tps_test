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

	case EDiegeticLookPreset::FrostedGlass:
		Look.BackgroundBlur = 24.f;
		Look.GlassTint = FLinearColor(0.6f, 0.7f, 0.8f, 0.25f);
		Look.BlurBrightness = 0.85f;
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
