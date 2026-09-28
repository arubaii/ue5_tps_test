// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"

/**
 * Draws the Set Widget Animation Value node's Animation and Value Name pins as dropdowns: the animations the widget's
 * Widget Animation Components play, and the value-driven tracks in the picked one. Every other pin is left to the
 * engine's own factories.
 */
class FWidgetAnimValuePinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<class SGraphPin> CreatePin(class UEdGraphPin* Pin) const override;
};
