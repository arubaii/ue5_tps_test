// Copyright arubaii. All rights reserved.

#include "InteractionSettings.h"

UInteractionSettings::UInteractionSettings()
{
	// WorldDynamic is a deliberately conservative default: it works in a fresh project
	// with zero setup, and the tooltip tells you to narrow it. Shipping with this is a
	// perf mistake, not a correctness one.
	InteractableObjectTypes.Add(ObjectTypeQuery2);
}

const UInteractionSettings& UInteractionSettings::Get()
{
	const UInteractionSettings* Settings = GetDefault<UInteractionSettings>();
	check(Settings);
	return *Settings;
}
