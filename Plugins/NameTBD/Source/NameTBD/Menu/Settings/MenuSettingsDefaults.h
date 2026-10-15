// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UMenuSettingsSchema;

/**
 * The plugin's built-in pages - Gameplay, Video, Audio, Controls, Accessibility - made in code (so they are there with no asset
 * and stay up to date with the plugin). Every setting and its Id is listed in MenuSystem.md; projects hide, replace (same Id)
 * or add to them with schemas.
 */
namespace MenuSettingsDefaults
{
	/** A new schema (owned by Outer) holding the built-in pages. */
	NAMETBD_API UMenuSettingsSchema* MakeBuiltInSchema(UObject* Outer);
}
