// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAITheme.h"

#include "NameTBDAISettings.h"

namespace
{
	FLinearColor Hex(const TCHAR* Text)
	{
		return FLinearColor(FColor::FromHex(Text));
	}
}

UNameTBDAITheme::UNameTBDAITheme()
{
	// Dark: deep blue-violet surfaces, soft white text, a violet accent, aurora glows.
	Dark.Background = Hex(TEXT("0B0D1AFF"));
	Dark.Surface = Hex(TEXT("15182BFF"));
	Dark.SurfaceRaised = Hex(TEXT("1F2340FF"));
	Dark.Outline = Hex(TEXT("FFFFFF1F"));
	Dark.Text = Hex(TEXT("F5F7FFFF"));
	Dark.TextMuted = Hex(TEXT("A9B0D0FF"));
	Dark.Accent = Hex(TEXT("7C5CFFFF"));
	Dark.AccentAlt = Hex(TEXT("2EE6C5FF"));
	Dark.Glow1 = Hex(TEXT("2EE6C5FF"));
	Dark.Glow2 = Hex(TEXT("4ADE80FF"));
	Dark.Glow3 = Hex(TEXT("8B5CF6FF"));
	Dark.Success = Hex(TEXT("34D399FF"));
	Dark.Warning = Hex(TEXT("FBBF24FF"));
	Dark.Danger = Hex(TEXT("F87171FF"));

	// Light: paper surfaces, ink text, the same hues a step deeper so they hold contrast on white.
	Light.Background = Hex(TEXT("F4F5FAFF"));
	Light.Surface = Hex(TEXT("FFFFFFFF"));
	Light.SurfaceRaised = Hex(TEXT("EEF0F8FF"));
	Light.Outline = Hex(TEXT("0F172A1F"));
	Light.Text = Hex(TEXT("0F172AFF"));
	Light.TextMuted = Hex(TEXT("5B6478FF"));
	Light.Accent = Hex(TEXT("5B3DF5FF"));
	Light.AccentAlt = Hex(TEXT("0EA5A0FF"));
	Light.Glow1 = Hex(TEXT("14B8A6FF"));
	Light.Glow2 = Hex(TEXT("22C55EFF"));
	Light.Glow3 = Hex(TEXT("8B5CF6FF"));
	Light.Success = Hex(TEXT("059669FF"));
	Light.Warning = Hex(TEXT("D97706FF"));
	Light.Danger = Hex(TEXT("DC2626FF"));

	Spacing = { 4.f, 8.f, 12.f, 16.f, 24.f, 32.f, 48.f, 64.f };
}

const UNameTBDAITheme& UNameTBDAITheme::Get()
{
	if (UNameTBDAITheme* Picked = UNameTBDAISettings::Get().Theme.LoadSynchronous())
	{
		return *Picked;
	}
	return *GetDefault<UNameTBDAITheme>();
}

float UNameTBDAITheme::GetSpacing(int32 Step) const
{
	if (Spacing.Num() == 0)
	{
		return 8.f * FMath::Max(Step, 0);
	}
	return Spacing[FMath::Clamp(Step, 0, Spacing.Num() - 1)];
}
