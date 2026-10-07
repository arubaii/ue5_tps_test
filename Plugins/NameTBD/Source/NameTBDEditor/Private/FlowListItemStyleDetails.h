// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
struct FPropertyChangedEvent;

/**
 * The Details of a Flow List Item Style. Before they are laid out, the component's Selected and Unselected looks are laid
 * out for the widget it is on - that widget's design settings, taken from how it is authored - so they list exactly what
 * that kind of widget can change, and start the same as it. Shown again once laid out.
 *
 * Only what applies is shown: looks the widget has nothing for (a Shape Box's design look, any other widget's shape look),
 * material parameters on a widget with no material, and the Transition under Nearness To Focus are hidden rather than left
 * empty or greyed. A choice that changes what applies - the Trigger, a spring's mode, a material parameter's Is Color - lays
 * them out again: in the designer, rows a component's edit hides or shows aren't otherwise laid out again until reselected.
 */
class FFlowListItemStyleDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual ~FFlowListItemStyleDetails() override;

	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
	void RefreshSoon();

	TWeakPtr<IDetailLayoutBuilder> WeakBuilder;
	TArray<TWeakObjectPtr<UObject>> Customized;
	FDelegateHandle PropertyChangedHandle;
};