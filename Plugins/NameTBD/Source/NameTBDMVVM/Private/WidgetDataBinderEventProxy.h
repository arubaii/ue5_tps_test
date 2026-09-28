// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "WidgetDataBinderEventProxy.generated.h"

class FMulticastDelegateProperty;
class UWidgetDataBinderComponent;

/**
 * Listens to one event dispatcher on a data source for a Widget Data Binder, whatever the event's parameters.
 *
 * A dynamic delegate can only call a UFUNCTION, and normally only one whose parameters match the event's. The
 * binder can't declare a function for every possible signature, so this object binds its one parameterless
 * function and intercepts the call in ProcessEvent before any parameters are looked at: the event's own
 * parameter block is never read, and the binder simply re-reads the value it cares about from its Source Path.
 */
UCLASS(Transient)
class UWidgetDataBinderEventProxy : public UObject
{
	GENERATED_BODY()

public:

	/** Binds to EventProperty, whose storage is on EventOwner. Returns false if it can't. */
	bool Attach(UWidgetDataBinderComponent* InBinder, int32 InBindingIndex, UObject* EventOwner, const FMulticastDelegateProperty* EventProperty);

	/** Unbinds, if the event's owner is still around. */
	void Detach();

	virtual void ProcessEvent(UFunction* Function, void* Parms) override;

private:

	UFUNCTION()
	void HandleEvent();

	TWeakObjectPtr<UWidgetDataBinderComponent> Binder;
	int32 BindingIndex = INDEX_NONE;

	TWeakObjectPtr<UObject> BoundOwner;
	TWeakObjectPtr<UClass> BoundOwnerClass;
	const FMulticastDelegateProperty* BoundProperty = nullptr;
};
