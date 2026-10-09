// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "IDetailCustomization.h"

class UWidgetDataBinderComponent;
struct FPropertyChangedEvent;

/**
 * The Details of a Widget Data Binder, with a Data Preview at the top: the binder's data, live in the designer, without
 * Play In Editor - the way Rive's data panel or a design tool's variable panel lets a component be tried with its data.
 *
 *  - Sample Set: previous / next / play through the binder's Sample Sets, or back to its Mock Values. Each step is written
 *    into the designer's preview as a change, so the widget reacts as in game: triggers play their animations, counts run,
 *    List Bindings add, remove and move their rows.
 *  - A row per ViewModel field with its value in the preview now (counts tick in it); type a value to change it. A list's
 *    row has Add Row, Remove Row, Overtake, Shuffle and Reverse, to try its entrances, exits and moves.
 *  - Edits to the binder's own settings (Sample Sets, Triggers, List Bindings) are taken into the preview at once.
 *
 * Acts on the binder's designer copies (UWidgetDataBinderComponent::FindDesignerPreviews), bound there when Run Triggers In
 * Designer is on. Nothing it does is saved.
 */
class FWidgetDataBinderDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual ~FWidgetDataBinderDetails() override;

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** The designer copies of the binder shown, looked up again at most twice a second. */
	TArray<UWidgetDataBinderComponent*> GetPreviews() const;
	UWidgetDataBinderComponent* GetFirstPreview() const;

	void ShowSet(int32 SetIndex);
	void Step(int32 Delta);
	void TogglePlay();
	bool TickPlay(float DeltaTime);
	void Rebind();
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	FText GetSetLabel() const;

	TWeakObjectPtr<UWidgetDataBinderComponent> Template;
	FTSTicker::FDelegateHandle PlayHandle;
	FTSTicker::FDelegateHandle RebindHandle;
	FDelegateHandle PropertyChangedHandle;
	float PlayElapsed = 0.f;

	mutable TArray<TWeakObjectPtr<UWidgetDataBinderComponent>> CachedPreviews;
	mutable double CachedAt = -1.0;
};
