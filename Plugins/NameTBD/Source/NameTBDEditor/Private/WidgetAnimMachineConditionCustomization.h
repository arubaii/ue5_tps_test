// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"

/**
 * A transition's condition (FWidgetAnimMachineCondition) in the Details: Compare offers only what makes sense for the input
 * picked, in its words - Is True / Is False for a Bool or a built-in input, Fired for a Trigger, the comparisons for a Number,
 * Is Held / Is Let Go / Completed for a Press And Hold. Picking another input puts Compare right for it.
 */
class FWidgetAnimMachineConditionCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	/** The comparisons a condition on Input can make, in order: all of them for an input the machine doesn't have. */
	static TArray<EWidgetAnimMachineCompare> GetCompares(const UWidgetAnimStateMachineAsset* Machine, FName Input);

	/** What a comparison reads as on Input: "Is Held" on a Press And Hold, "Is True" on a Bool. */
	static FText GetCompareLabel(const UWidgetAnimStateMachineAsset* Machine, FName Input, EWidgetAnimMachineCompare Compare);
};
