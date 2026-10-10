// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FNameTBDAIChatSession;

namespace NameTBDAIModule
{
	/** The chat, kept by the module so it outlives its tab. */
	TSharedRef<FNameTBDAIChatSession> GetSession();
}
