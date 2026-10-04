// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"

/**
 * A transition's condition (FWidgetAnimMachineCondition) in the Details: only what fits the input picked is offered, in its
 * words, and only the fields that mean something for it are shown.
 *  - Bool and the built-in inputs: Is True / Is False. Trigger and Clicked: Fired. Press And Hold: Is Held / Is Let Go / Completed.
 *  - Float and Integer: == != < <= > >= against a value or another input; Increased / Decreased / Changed.
 *  - Choice: Is / Is Not one of its options, or another Choice; Changed.
 *  - Vector 2D: Points (a direction, past a dead zone); its Length, X or Y compared; Increased / Decreased / Moved.
 * Picking another input puts Compare right for it.
 */
class FWidgetAnimMachineConditionCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	/** The comparisons a condition on Input can make, in order: all of them for an input the machine doesn't have. */
	static TArray<EWidgetAnimMachineCompare> GetCompares(const UWidgetAnimStateMachineAsset* Machine, FName Input);

	/** What a comparison reads as on Input: "Is Held" on a Press And Hold, "Is" on a Choice, "Is True" on a Bool. */
	static FText GetCompareLabel(const UWidgetAnimStateMachineAsset* Machine, FName Input, EWidgetAnimMachineCompare Compare);

	/** The condition in a few words - "Hovered", "Charge >= 0.5", "Rarity is Epic", "Stick points Left" - for the graph and the Details. */
	static FText Describe(const UWidgetAnimStateMachineAsset* Machine, const FWidgetAnimMachineCondition& Condition);

	/** Whether Other can be what Input is compared against: a number with a number (a Vector 2D's part among them), a Choice with a Choice. */
	static bool CanCompareInputs(const UWidgetAnimStateMachineAsset* Machine, FName Input, FName Other);
};
