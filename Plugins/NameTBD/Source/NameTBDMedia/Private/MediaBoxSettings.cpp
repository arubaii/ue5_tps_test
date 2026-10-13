// Copyright AR GameSystems 2026. All rights reserved.

#include "MediaBoxSettings.h"

#include "MediaBoxLog.h"

#define LOCTEXT_NAMESPACE "MediaBoxSettings"

#if WITH_EDITOR
FText UMediaBoxSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "Media Box");
}
#endif

int32 UMediaBoxSettings::GetMaxWarmPlayers() const
{
	const int32 Override = MediaBoxRuntime::GetMaxWarmPlayersOverride();
	return Override >= 0 ? Override : FMath::Max(0, MaxWarmPlayers);
}

#undef LOCTEXT_NAMESPACE
