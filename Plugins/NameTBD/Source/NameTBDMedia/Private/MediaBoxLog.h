// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The Media Box's log category, shared by its files. Declared once here and defined once, in NameTBDMediaModule.cpp: a static
 * category in each .cpp is two definitions of one type as soon as a unity build puts the two files together.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogMediaBox, Log, All);

namespace MediaBoxRuntime
{
	/** MediaBox.Log 1: players opened, warmed, leased, closed; warm and cold starts. */
	bool IsLogging();

	/** MediaBox.MaxWarmPlayers: below 0 (the default) the project setting is used. */
	int32 GetMaxWarmPlayersOverride();

	/** Set Media Box Subtitles Enabled - starts at the project setting. */
	bool AreSubtitlesEnabled();
	void SetSubtitlesEnabled(bool bEnabled);
}

#define MEDIABOX_LOG(Format, ...) \
	do { if (MediaBoxRuntime::IsLogging()) { UE_LOG(LogMediaBox, Log, Format, ##__VA_ARGS__); } } while (0)
