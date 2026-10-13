// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class IPropertyHandle;

/**
 * A Diegetic Screen's Details: its own settings as they are, plus Screen Material - Copy To Project, which copies the plugin's
 * screen material the screens are drawn with (DiegeticScreenMaterial.h) into the project and gives it to the selected screens
 * (Material Custom) through the panel's handles, so it is one undo step and reaches a Blueprint's component template as any
 * Details edit does.
 */
class FDiegeticScreenDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	FReply OnCreateScreenMaterial();

	TArray<TWeakObjectPtr<UObject>> Objects;
	TSharedPtr<IPropertyHandle> MaterialsHandle;
	TSharedPtr<IPropertyHandle> MaterialModeHandle;
};
