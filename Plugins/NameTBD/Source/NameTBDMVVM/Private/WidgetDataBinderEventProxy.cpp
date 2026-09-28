// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetDataBinderEventProxy.h"

#include "UObject/UnrealType.h"
#include "WidgetDataBinderComponent.h"

// Same name as the binder's category, so "Log LogWidgetDataBinder Verbose" turns both on.
DEFINE_LOG_CATEGORY_STATIC(LogWidgetDataBinder, Log, All);

bool UWidgetDataBinderEventProxy::Attach(UWidgetDataBinderComponent* InBinder, int32 InBindingIndex, UObject* EventOwner, const FMulticastDelegateProperty* EventProperty)
{
	Detach();

	if (!InBinder || !IsValid(EventOwner) || !EventProperty)
	{
		return false;
	}

	Binder = InBinder;
	BindingIndex = InBindingIndex;
	BoundOwner = EventOwner;
	BoundOwnerClass = EventOwner->GetClass();
	BoundProperty = EventProperty;

	FScriptDelegate Delegate;
	Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UWidgetDataBinderEventProxy, HandleEvent));
	BoundProperty->AddDelegate(MoveTemp(Delegate), EventOwner, BoundProperty->ContainerPtrToValuePtr<void>(EventOwner));
	return true;
}

void UWidgetDataBinderEventProxy::Detach()
{
	UObject* EventOwner = BoundOwner.Get();

	// The property pointer is only trusted against the class it was found on; a recompiled Blueprint class
	// has new properties, and its old event storage went with the old instance.
	if (EventOwner && BoundProperty && EventOwner->GetClass() == BoundOwnerClass.Get())
	{
		FScriptDelegate Delegate;
		Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UWidgetDataBinderEventProxy, HandleEvent));
		BoundProperty->RemoveDelegate(Delegate, EventOwner, BoundProperty->ContainerPtrToValuePtr<void>(EventOwner));
	}

	BoundOwner.Reset();
	BoundOwnerClass.Reset();
	BoundProperty = nullptr;
	Binder.Reset();
	BindingIndex = INDEX_NONE;
}

void UWidgetDataBinderEventProxy::ProcessEvent(UFunction* Function, void* Parms)
{
	// Handled here rather than by the script VM: Parms is laid out for the event's signature, not for
	// HandleEvent's (which has none), so it must not be interpreted as HandleEvent's parameters.
	if (Function && Function->GetFName() == GET_FUNCTION_NAME_CHECKED(UWidgetDataBinderEventProxy, HandleEvent))
	{
		HandleEvent();
		return;
	}

	Super::ProcessEvent(Function, Parms);
}

void UWidgetDataBinderEventProxy::HandleEvent()
{
	UWidgetDataBinderComponent* Target = Binder.Get();
	UE_LOG(LogWidgetDataBinder, Verbose, TEXT("%s: event fired for Field Binding %d, binder %s."), *GetName(), BindingIndex, *GetNameSafe(Target));

	if (Target)
	{
		Target->HandleSourceEvent(BindingIndex);
	}
}