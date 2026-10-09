// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

/**
 * A parameter value (FWidgetAnimParameterValue) in the Details. In an animation entry's Parameters - a Widget Animation
 * Component's, a button's - one row per parameter of the animation: a tick to set it here, its name, and its value as its
 * type takes it (a Choice as a dropdown of its options), greyed while not set here, when it plays the animation's default.
 * Anywhere else, its fields.
 */
class FWidgetAnimParameterValueCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	/** Whether the row is one of an entry's Parameters, laid out as such in the header alone. */
	bool bInEntry = false;
};
