// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/**
 * The Details of a Flow List Item Style. Before they are laid out, the component's Selected and Unselected looks are laid
 * out for the widget it is on - that widget's design settings, taken from how it is authored - so they list exactly what
 * that kind of widget can change, and start the same as it. Shown again once laid out.
 */
class FFlowListItemStyleDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
