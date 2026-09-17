// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/**
 * Draws Create Widget Animation in the Details panel, so that it can be styled.
 *
 * A UFUNCTION marked CallInEditor gets a button for free, but the property editor builds it with its own
 * fixed style and nothing in the UFUNCTION's metadata reaches it. Building the row here is the way to own
 * the button's colours - see CreateButtonStyle in the .cpp, which is the only place they live.
 */
class FWidgetAnimBuilderDetails : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

	/** The objects the panel is editing. A multi-selection creates on each of them. */
	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;

	FReply OnCreateClicked();
	bool IsCreateEnabled() const;
	FText GetCreateTooltip() const;
};