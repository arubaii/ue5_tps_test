// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetDataBinderComponent.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ActorComponent.h"
#include "Components/Widget.h"
#include "GameFramework/Actor.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"
#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "UObject/UnrealType.h"
#include "View/MVVMView.h"
#include "WidgetDataBinderEventProxy.h"
#include "WidgetDataBinderLog.h"

#if WITH_EDITORONLY_DATA
#include "Engine/Blueprint.h"
#endif

DEFINE_LOG_CATEGORY(LogWidgetDataBinder);

TMap<TObjectKey<UWidget>, TWeakObjectPtr<UWidgetDataBinderComponent>> UWidgetDataBinderComponent::BindersByWidget;

// =====================================================================================================
// Lifecycle
// =====================================================================================================

#if UE_VERSION_OLDER_THAN(5, 6, 0)
bool UWidgetDataBinderComponent::OnInitialize()
{
	SetBinderOwner(GetOwner().Get());
	return true;
}
#else
void UWidgetDataBinderComponent::OnInitialize()
{
	SetBinderOwner(GetOwner().Get());
}

void UWidgetDataBinderComponent::OnPreConstruct(bool bIsDesignTime)
{
	// Some owners are initialized before the component system hands us the widget; pick it up here too.
	if (!BinderOwner.IsValid())
	{
		SetBinderOwner(GetOwner().Get());
	}

	if (bIsDesignTime)
	{
		ApplyDesignTimeMock();
	}
}
#endif

void UWidgetDataBinderComponent::OnConstruct()
{
	if (!BinderOwner.IsValid())
	{
		SetBinderOwner(GetOwner().Get());
	}

	UWidget* OwnerWidget = BinderOwner.Get();
	if (!OwnerWidget || OwnerWidget->IsDesignTime())
	{
		return;
	}

	bOwnerConstructed = true;
	BindAll(/*bFromConstruct=*/true);
}

void UWidgetDataBinderComponent::OnDestruct()
{
	// The source and an explicitly set ViewModel are kept: a widget removed and added again is constructed
	// again, and should pick up where it was.
	bOwnerConstructed = false;
	UnbindAll();
}

void UWidgetDataBinderComponent::BeginDestroy()
{
	UnbindAll();

	if (UWidget* OwnerWidget = BinderOwner.Get())
	{
		const TWeakObjectPtr<UWidgetDataBinderComponent>* Registered = BindersByWidget.Find(TObjectKey<UWidget>(OwnerWidget));
		if (Registered && Registered->Get() == this)
		{
			BindersByWidget.Remove(TObjectKey<UWidget>(OwnerWidget));
		}
	}

	Super::BeginDestroy();
}

void UWidgetDataBinderComponent::SetBinderOwner(UWidget* NewOwner)
{
	if (!NewOwner || BinderOwner.Get() == NewOwner)
	{
		return;
	}

	BinderOwner = NewOwner;

	// Only live instances are looked up; the designer's archetypes and class defaults never are.
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		BindersByWidget.Add(TObjectKey<UWidget>(NewOwner), this);
	}
}

// =====================================================================================================
// Runtime API
// =====================================================================================================

void UWidgetDataBinderComponent::SetSource(UObject* NewSource)
{
	if (DataSource.Get() == NewSource && (NewSource != nullptr) == bHadSource)
	{
		return;
	}

	UnbindSource();
	DataSource = NewSource;
	bHadSource = NewSource != nullptr;

	UE_LOG(LogWidgetDataBinder, Verbose, TEXT("%s: source set to %s (constructed: %d, ViewModel: %s)."),
		*DescribeForLog(), *GetNameSafe(NewSource), bOwnerConstructed ? 1 : 0, *GetNameSafe(BoundViewModel.Get()));

	if (bOwnerConstructed && BoundViewModel.IsValid())
	{
		BindSource();
	}
}

void UWidgetDataBinderComponent::ClearSource()
{
	SetSource(nullptr);
}

void UWidgetDataBinderComponent::SetViewModel(UObject* NewViewModel)
{
	if (NewViewModel && !Cast<INotifyFieldValueChanged>(NewViewModel))
	{
		UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: Set ViewModel was given %s, which isn't Field Notify (a ViewModel). Ignored."), *DescribeForLog(), *GetNameSafe(NewViewModel));
		return;
	}

	ExplicitViewModel = NewViewModel;

	// Let View Bindings see it too.
	if (NewViewModel && !ViewModelName.IsNone())
	{
		PushViewModelToView(NewViewModel, /*bWarnOnFailure=*/bOwnerConstructed);
	}

	if (bOwnerConstructed)
	{
		UnbindAll();
		BindAll(/*bFromConstruct=*/false);
	}
}

void UWidgetDataBinderComponent::RefreshFromSource()
{
	for (int32 Index = 0; Index < BindingStates.Num(); ++Index)
	{
		PushBinding(Index);
	}
}

UWidgetDataBinderComponent* UWidgetDataBinderComponent::FindWidgetDataBinder(const UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}

	const TWeakObjectPtr<UWidgetDataBinderComponent>* Found = BindersByWidget.Find(TObjectKey<UWidget>(Widget));
	return Found ? Found->Get() : nullptr;
}

int32 UWidgetDataBinderComponent::SetWidgetDataSource(UWidget* Widget, UObject* NewSource)
{
	int32 Count = 0;

	auto Visit = [&Count, NewSource](UWidget* Candidate)
	{
		if (UWidgetDataBinderComponent* Binder = FindWidgetDataBinder(Candidate))
		{
			Binder->SetSource(NewSource);
			++Count;
		}
	};

	if (!Widget)
	{
		return 0;
	}

	Visit(Widget);

	// Its own hierarchy only. ForEachWidget doesn't enter nested User Widgets, which is what we want: a nested
	// row describes its own object and gets its own call.
	if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
	{
		if (UserWidget->WidgetTree)
		{
			UserWidget->WidgetTree->ForEachWidget([&Visit](UWidget* Child) { Visit(Child); });
		}
	}

	return Count;
}

// =====================================================================================================
// Binding
// =====================================================================================================

void UWidgetDataBinderComponent::BindAll(bool bFromConstruct)
{
	if (!bOwnerConstructed)
	{
		return;
	}

	UObject* ViewModel = ExplicitViewModel.Get();

	if (!ViewModel && !ViewModelName.IsNone())
	{
		ViewModel = FindViewModelInView();

		// The view may create its own ViewModel during this same construct, after us (extensions construct in
		// their own order). Look again next frame; if it made one, that one wins and we switch to it.
		if (bFromConstruct)
		{
			bVerifyViewModelNextTick = true;
		}
	}

	if (!ViewModel && bCreateViewModelIfMissing)
	{
		ViewModel = CreateViewModel();
	}

	if (!ViewModel)
	{
		if (!bVerifyViewModelNextTick && !bWarnedNoViewModel)
		{
			bWarnedNoViewModel = true;
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: no ViewModel to bind. Set ViewModel Name to one in the widget's Viewmodels panel, or set ViewModel Class with Create ViewModel If Missing on."), *DescribeForLog());
		}
	}
	else
	{
		AttachViewModel(ViewModel);
		BindSource();
	}

	if (bVerifyViewModelNextTick)
	{
		EnsureTicker();
	}
}

void UWidgetDataBinderComponent::UnbindAll()
{
	UnbindSource();
	DetachViewModel();
	bVerifyViewModelNextTick = false;
	StopTicker();
}

void UWidgetDataBinderComponent::AttachViewModel(UObject* ViewModel, bool bKeepTriggerStates)
{
	TArray<bool> PreviousActive;
	if (bKeepTriggerStates && TriggerStates.Num() == Triggers.Num())
	{
		for (const FTriggerState& Previous : TriggerStates)
		{
			PreviousActive.Add(Previous.bActive);
		}
	}

	DetachViewModel();

	INotifyFieldValueChanged* Notifier = Cast<INotifyFieldValueChanged>(ViewModel);
	if (!Notifier)
	{
		return;
	}

	BoundViewModel = ViewModel;
	UClass* ViewModelType = ViewModel->GetClass();
	const UE::FieldNotification::IClassDescriptor& Descriptor = Notifier->GetFieldNotificationDescriptor();

	// Where each binding writes. The scratch value is reused for every conversion, so polling allocates nothing.
	BindingStates.Reset();
	BindingStates.SetNum(FieldBindings.Num());
	for (int32 Index = 0; Index < FieldBindings.Num(); ++Index)
	{
		const FWidgetFieldBinding& Binding = FieldBindings[Index];
		FBindingState& State = BindingStates[Index];
		State.SourcePath.Set(Binding.SourcePath);
		State.Mode = Binding.Update;

		if (Binding.ViewModelField.IsNone())
		{
			continue;
		}

		State.TargetProperty = FindFProperty<FProperty>(ViewModelType, Binding.ViewModelField);
		if (!State.TargetProperty)
		{
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: Field Binding %d writes '%s', which %s doesn't have."), *DescribeForLog(), Index, *Binding.ViewModelField.ToString(), *ViewModelType->GetName());
			continue;
		}

		if (!Descriptor.GetField(ViewModelType, Binding.ViewModelField).IsValid())
		{
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: Field Binding %d writes '%s', which isn't Field Notify on %s, so View Bindings and triggers won't hear about it."), *DescribeForLog(), Index, *Binding.ViewModelField.ToString(), *ViewModelType->GetName());
		}

		State.Scratch = MakeUnique<FWidgetDataScratch>(State.TargetProperty);
	}

	// One subscription per watched field, however many triggers watch it.
	TriggerStates.Reset();
	TriggerStates.SetNum(Triggers.Num());
	for (int32 Index = 0; Index < Triggers.Num(); ++Index)
	{
		const FName Field = Triggers[Index].ViewModelField;
		TriggerStates[Index].FieldPath.Set(Field.ToString());
		TriggerStates[Index].bActive = PreviousActive.IsValidIndex(Index) && PreviousActive[Index];

		if (Field.IsNone())
		{
			continue;
		}

		const UE::FieldNotification::FFieldId FieldId = Descriptor.GetField(ViewModelType, Field);
		if (!FieldId.IsValid())
		{
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: trigger %d watches '%s', which isn't a Field Notify field of %s. It will never act."), *DescribeForLog(), Index, *Field.ToString(), *ViewModelType->GetName());
			continue;
		}

		const bool bAlreadyWatched = ViewModelHandles.ContainsByPredicate([&FieldId](const TPair<UE::FieldNotification::FFieldId, FDelegateHandle>& Entry)
		{
			return Entry.Key == FieldId;
		});

		if (!bAlreadyWatched)
		{
			const FDelegateHandle Handle = Notifier->AddFieldValueChangedDelegate(FieldId,
				INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &UWidgetDataBinderComponent::HandleViewModelFieldChanged));
			ViewModelHandles.Emplace(FieldId, Handle);
		}
	}
}

void UWidgetDataBinderComponent::DetachViewModel()
{
	if (INotifyFieldValueChanged* Notifier = Cast<INotifyFieldValueChanged>(BoundViewModel.Get()))
	{
		for (const TPair<UE::FieldNotification::FFieldId, FDelegateHandle>& Entry : ViewModelHandles)
		{
			Notifier->RemoveFieldValueChangedDelegate(Entry.Key, Entry.Value);
		}
	}

	ViewModelHandles.Reset();
	BoundViewModel.Reset();
	BindingStates.Reset();
	TriggerStates.Reset();
}

void UWidgetDataBinderComponent::BindSource()
{
	UnbindSource();

	UObject* ViewModel = BoundViewModel.Get();
	if (!ViewModel)
	{
		return;
	}

	UObject* Source = DataSource.Get();

	UE_LOG(LogWidgetDataBinder, Verbose, TEXT("%s: binding source %s to ViewModel %s."), *DescribeForLog(), *GetNameSafe(Source), *GetNameSafe(ViewModel));

	// Writes made while binding don't act on triggers one by one; the single evaluation afterwards decides
	// against the settled values, with Evaluate On Bind applied. View Bindings still hear every write.
	{
		TGuardValue<bool> SuppressGuard(bSuppressTriggers, true);

		if (!Source)
		{
			if (bUseMockDataWithoutSource)
			{
				ApplyMockValues(ViewModel);
			}
		}
		else
		{
			// Guarding against a destroyed target: an actor source, or a component's actor, tells us when it goes.
			AActor* ActorToWatch = Cast<AActor>(Source);
			if (!ActorToWatch)
			{
				if (const UActorComponent* Component = Cast<UActorComponent>(Source))
				{
					ActorToWatch = Component->GetOwner();
				}
			}

			if (ActorToWatch)
			{
				ActorToWatch->OnDestroyed.AddUniqueDynamic(this, &UWidgetDataBinderComponent::HandleSourceActorDestroyed);
				WatchedActor = ActorToWatch;
			}

			for (int32 Index = 0; Index < BindingStates.Num(); ++Index)
			{
				const FWidgetFieldBinding& Binding = FieldBindings[Index];
				FBindingState& State = BindingStates[Index];
				State.Mode = Binding.Update;
				State.PollElapsed = 0.f;

				if (!State.TargetProperty || State.SourcePath.IsEmpty())
				{
					continue;
				}

				if (State.Mode == EWidgetDataUpdateMode::FieldNotify)
				{
					FWidgetDataValue Value;
					FString Error;
					bool bSubscribed = false;

					if (FWidgetDataValue::Resolve(Source, State.SourcePath, Value, &Error))
					{
						if (INotifyFieldValueChanged* SourceNotifier = Cast<INotifyFieldValueChanged>(Value.OwnerObject))
						{
							const UE::FieldNotification::FFieldId FieldId = SourceNotifier->GetFieldNotificationDescriptor().GetField(Value.OwnerObject->GetClass(), State.SourcePath.GetLastSegment());
							if (FieldId.IsValid())
							{
								State.NotifyObject = Value.OwnerObject;
								State.NotifyField = FieldId;
								State.NotifyHandle = SourceNotifier->AddFieldValueChangedDelegate(FieldId,
									INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &UWidgetDataBinderComponent::HandleSourceFieldChanged, Index));
								bSubscribed = true;
							}
						}

						if (!bSubscribed)
						{
							UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: Field Binding %d ('%s') is On Source Field Notify, but %s has no Field Notify field '%s'. Polling it every frame instead."),
								*DescribeForLog(), Index, *State.SourcePath.ToString(), *GetNameSafe(Value.OwnerObject), *State.SourcePath.GetLastSegment().ToString());
						}
					}

					if (!bSubscribed)
					{
						State.Mode = EWidgetDataUpdateMode::Poll;
					}
				}
				else if (State.Mode == EWidgetDataUpdateMode::Event)
				{
					FWidgetDataPath EventPath(Binding.EventPath);
					FWidgetDataValue Event;
					FString Error;
					bool bAttached = false;

					if (EventPath.IsEmpty())
					{
						Error = TEXT("Event Path is empty");
					}
					else if (FWidgetDataValue::Resolve(Source, EventPath, Event, &Error, /*bCallGetters=*/false))
					{
						const FMulticastDelegateProperty* EventProperty = CastField<FMulticastDelegateProperty>(Event.Property);
						if (!EventProperty)
						{
							Error = TEXT("it isn't an event dispatcher");
						}
						else if (!Event.OwnerObject || !Event.bDirectStorage)
						{
							Error = TEXT("it isn't directly on an object");
						}
						else
						{
							UWidgetDataBinderEventProxy* Proxy = NewObject<UWidgetDataBinderEventProxy>(this);
							bAttached = Proxy->Attach(this, Index, Event.OwnerObject, EventProperty);
							if (bAttached)
							{
								EventProxies.Add(Proxy);
								UE_LOG(LogWidgetDataBinder, Verbose, TEXT("%s: Field Binding %d listens to %s on %s (listener %s)."),
									*DescribeForLog(), Index, *EventProperty->GetName(), *GetNameSafe(Event.OwnerObject), *GetNameSafe(Proxy));
							}
						}
					}

					if (!bAttached)
					{
						UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: Field Binding %d can't listen to Event Path '%s' (%s). It is read once now, and again on Refresh From Source."),
							*DescribeForLog(), Index, *Binding.EventPath, *Error);
						State.Mode = EWidgetDataUpdateMode::Manual;
					}
				}

				PushBinding(Index);
			}
		}
	}

	bAnyPolling = false;
	if (Source)
	{
		for (const FBindingState& State : BindingStates)
		{
			bAnyPolling |= State.Mode == EWidgetDataUpdateMode::Poll && State.TargetProperty != nullptr;
		}
	}

	EvaluateAllTriggers(/*bInitial=*/true);

	if (bAnyPolling)
	{
		EnsureTicker();
	}
}

void UWidgetDataBinderComponent::UnbindSource()
{
	for (FBindingState& State : BindingStates)
	{
		if (INotifyFieldValueChanged* SourceNotifier = Cast<INotifyFieldValueChanged>(State.NotifyObject.Get()))
		{
			SourceNotifier->RemoveFieldValueChangedDelegate(State.NotifyField, State.NotifyHandle);
		}

		State.NotifyObject.Reset();
		State.NotifyField = UE::FieldNotification::FFieldId();
		State.NotifyHandle.Reset();
	}

	for (UWidgetDataBinderEventProxy* Proxy : EventProxies)
	{
		if (Proxy)
		{
			Proxy->Detach();
		}
	}
	EventProxies.Reset();

	if (AActor* Actor = WatchedActor.Get())
	{
		Actor->OnDestroyed.RemoveDynamic(this, &UWidgetDataBinderComponent::HandleSourceActorDestroyed);
	}
	WatchedActor.Reset();

	// The ticker is not stopped here: this can run from inside it (a source lost while polling), and it ends
	// itself on its next tick once nothing is left to poll.
	bAnyPolling = false;
}

void UWidgetDataBinderComponent::PushBinding(int32 BindingIndex)
{
	if (!BindingStates.IsValidIndex(BindingIndex))
	{
		UE_LOG(LogWidgetDataBinder, Verbose, TEXT("%s: Field Binding %d has no state (%d states), not read."), *DescribeForLog(), BindingIndex, BindingStates.Num());
		return;
	}

	FBindingState& State = BindingStates[BindingIndex];
	UObject* ViewModel = BoundViewModel.Get();
	UObject* Source = DataSource.Get();
	if (!ViewModel || !Source || !State.TargetProperty || !State.Scratch)
	{
		UE_LOG(LogWidgetDataBinder, Verbose, TEXT("%s: Field Binding %d not read (ViewModel: %s, source: %s, target field: %s)."),
			*DescribeForLog(), BindingIndex, *GetNameSafe(ViewModel), *GetNameSafe(Source), State.TargetProperty ? *State.TargetProperty->GetName() : TEXT("none"));
		return;
	}

	FWidgetDataValue Value;
	FString Error;
	if (!FWidgetDataValue::Resolve(Source, State.SourcePath, Value, &Error))
	{
		// A path through something empty (no weapon equipped yet) is normal and may fill in later, so this is
		// said once, not every frame.
		if (!State.bWarnedRead)
		{
			State.bWarnedRead = true;
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: Field Binding %d can't read '%s' from %s: %s."), *DescribeForLog(), BindingIndex, *State.SourcePath.ToString(), *GetNameSafe(Source), *Error);
		}
		return;
	}
	State.bWarnedRead = false;

	if (!WidgetDataValues::Convert(Value.Property, Value.ValuePtr, State.TargetProperty, State.Scratch->Get()))
	{
		if (!State.bWarnedConvert)
		{
			State.bWarnedConvert = true;
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: Field Binding %d can't put '%s' (%s) into '%s' (%s). Numbers convert into numbers and bools; anything else needs the same type."),
				*DescribeForLog(), BindingIndex, *State.SourcePath.ToString(), *Value.Property->GetCPPType(), *State.TargetProperty->GetName(), *State.TargetProperty->GetCPPType());
		}
		return;
	}

	const bool bWritten = WidgetDataValues::WriteField(ViewModel, State.TargetProperty, State.Scratch->Get());

	if (UE_LOG_ACTIVE(LogWidgetDataBinder, Verbose))
	{
		FString ValueText;
		State.TargetProperty->ExportTextItem_Direct(ValueText, State.Scratch->Get(), nullptr, nullptr, PPF_None);
		UE_LOG(LogWidgetDataBinder, Verbose, TEXT("%s: Field Binding %d read '%s' = %s (%s)."),
			*DescribeForLog(), BindingIndex, *State.SourcePath.ToString(), *ValueText, bWritten ? TEXT("written") : TEXT("unchanged"));
	}
}

// =====================================================================================================
// Mock data
// =====================================================================================================

void UWidgetDataBinderComponent::ApplyMockValues(UObject* ViewModel)
{
	if (!ViewModel)
	{
		return;
	}

	for (const FWidgetDataMockValue& Mock : MockValues)
	{
		const FProperty* Field = Mock.ViewModelField.IsNone() ? nullptr : FindFProperty<FProperty>(ViewModel->GetClass(), Mock.ViewModelField);
		if (!Field)
		{
			continue;
		}

		FWidgetDataScratch Parsed(Field);
		if (!Field->ImportText_Direct(*Mock.Value, Parsed.Get(), ViewModel, PPF_None))
		{
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: Mock Value '%s' isn't a valid %s for '%s'."), *DescribeForLog(), *Mock.Value, *Field->GetCPPType(), *Mock.ViewModelField.ToString());
			continue;
		}

		WidgetDataValues::WriteField(ViewModel, Field, Parsed.Get());
	}
}

void UWidgetDataBinderComponent::ApplyDesignTimeMock()
{
	if (!bUseMockDataInDesigner || MockValues.IsEmpty() || !ViewModelClass)
	{
		return;
	}

	UObject* ViewModel = FindViewModelInView();
	if (!ViewModel)
	{
		ViewModel = CreateViewModel();
	}

	ApplyMockValues(ViewModel);
}

// =====================================================================================================
// Source changes
// =====================================================================================================

void UWidgetDataBinderComponent::HandleSourceFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId, int32 BindingIndex)
{
	if (!DataSource.IsValid())
	{
		HandleSourceLost();
		return;
	}

	PushBinding(BindingIndex);
}

void UWidgetDataBinderComponent::HandleSourceEvent(int32 BindingIndex)
{
	UE_LOG(LogWidgetDataBinder, Verbose, TEXT("%s: event for Field Binding %d received (source valid: %d)."),
		*DescribeForLog(), BindingIndex, DataSource.IsValid() ? 1 : 0);

	if (!DataSource.IsValid())
	{
		HandleSourceLost();
		return;
	}

	PushBinding(BindingIndex);
}

void UWidgetDataBinderComponent::HandleSourceActorDestroyed(AActor* DestroyedActor)
{
	HandleSourceLost();
}

void UWidgetDataBinderComponent::HandleSourceLost()
{
	if (!bHadSource)
	{
		return;
	}

	UnbindSource();
	DataSource.Reset();
	bHadSource = false;

	UObject* ViewModel = BoundViewModel.Get();
	if (ViewModel && OnSourceLost == EWidgetDataSourceLost::ResetToDefaults)
	{
		// Written normally, not suppressed, so triggers see the reset as the change it is.
		const UObject* Defaults = ViewModel->GetClass()->GetDefaultObject();
		for (const FBindingState& State : BindingStates)
		{
			if (!State.TargetProperty)
			{
				continue;
			}

			FWidgetDataScratch DefaultValue(State.TargetProperty);
			State.TargetProperty->GetValue_InContainer(Defaults, DefaultValue.Get());
			WidgetDataValues::WriteField(ViewModel, State.TargetProperty, DefaultValue.Get());
		}
	}

	if (ViewModel && bUseMockDataWithoutSource)
	{
		ApplyMockValues(ViewModel);
	}

	OnSourceLostEvent.Broadcast(this);
}

// =====================================================================================================
// Triggers
// =====================================================================================================

void UWidgetDataBinderComponent::HandleViewModelFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId)
{
	if (bSuppressTriggers)
	{
		return;
	}

	const FName Field = FieldId.GetName();
	for (int32 Index = 0; Index < Triggers.Num(); ++Index)
	{
		if (Triggers[Index].ViewModelField == Field)
		{
			EvaluateTrigger(Index, /*bInitial=*/false);
		}
	}
}

void UWidgetDataBinderComponent::EvaluateAllTriggers(bool bInitial)
{
	for (int32 Index = 0; Index < Triggers.Num(); ++Index)
	{
		EvaluateTrigger(Index, bInitial);
	}
}

void UWidgetDataBinderComponent::EvaluateTrigger(int32 TriggerIndex, bool bInitial)
{
	if (!Triggers.IsValidIndex(TriggerIndex) || !TriggerStates.IsValidIndex(TriggerIndex))
	{
		return;
	}

	const FWidgetFieldTrigger& Trigger = Triggers[TriggerIndex];
	FTriggerState& State = TriggerStates[TriggerIndex];

	UObject* ViewModel = BoundViewModel.Get();
	if (!ViewModel || State.FieldPath.IsEmpty())
	{
		return;
	}

	// Through the path reader rather than a plain property lookup, so a Field Notify getter function on a C++
	// ViewModel is watched the same way as a variable.
	FWidgetDataValue Value;
	FString Error;
	if (!FWidgetDataValue::Resolve(ViewModel, State.FieldPath, Value, &Error))
	{
		if (!State.bWarned)
		{
			State.bWarned = true;
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: trigger %d can't read '%s': %s."), *DescribeForLog(), TriggerIndex, *Trigger.ViewModelField.ToString(), *Error);
		}
		return;
	}

	if (Trigger.Condition == EWidgetDataTriggerCondition::Changed)
	{
		FString Text;
		Value.Property->ExportTextItem_Direct(Text, Value.ValuePtr, nullptr, nullptr, PPF_None);

		const bool bChanged = State.bHasLastText && Text != State.LastText;
		State.LastText = MoveTemp(Text);
		State.bHasLastText = true;

		// A rebind is not a change of the value, even when the new source differs from the old one.
		if (bChanged && !bInitial)
		{
			FireTrigger(TriggerIndex, /*bBecameActive=*/true);
		}
		return;
	}

	const bool bNowActive = EvaluateCondition(Trigger, Value, State.bActive);
	if (bNowActive == State.bActive)
	{
		return;
	}

	State.bActive = bNowActive;

	if (bNowActive)
	{
		if (!bInitial || Trigger.bEvaluateOnBind)
		{
			FireTrigger(TriggerIndex, /*bBecameActive=*/true);
		}
	}
	else
	{
		FireTrigger(TriggerIndex, /*bBecameActive=*/false);
	}
}

bool UWidgetDataBinderComponent::EvaluateCondition(const FWidgetFieldTrigger& Trigger, const FWidgetDataValue& Value, bool bCurrentlyActive) const
{
	switch (Trigger.Condition)
	{
	case EWidgetDataTriggerCondition::IsTrue:
		return WidgetDataValues::IsTruthy(Value.Property, Value.ValuePtr);

	case EWidgetDataTriggerCondition::IsFalse:
		return !WidgetDataValues::IsTruthy(Value.Property, Value.ValuePtr);

	case EWidgetDataTriggerCondition::Compare:
	{
		double Number = 0.0;
		if (!WidgetDataValues::ReadNumber(Value.Property, Value.ValuePtr, Number))
		{
			return false;
		}

		// While active, the threshold moves by Hysteresis in the direction the value has to travel to leave, so
		// a value sitting on the threshold doesn't flicker the animation on and off.
		double Limit = Trigger.Threshold;
		const double Band = bCurrentlyActive ? FMath::Max(Trigger.Hysteresis, 0.0) : 0.0;

		switch (Trigger.Comparison)
		{
		case EWidgetDataComparison::Less:           Limit += Band; return Number < Limit;
		case EWidgetDataComparison::LessOrEqual:    Limit += Band; return Number <= Limit;
		case EWidgetDataComparison::Greater:        Limit -= Band; return Number > Limit;
		case EWidgetDataComparison::GreaterOrEqual: Limit -= Band; return Number >= Limit;
		case EWidgetDataComparison::Equal:          return FMath::IsNearlyEqual(Number, Limit, 1e-6);
		case EWidgetDataComparison::NotEqual:       return !FMath::IsNearlyEqual(Number, Limit, 1e-6);
		default: return false;
		}
	}

	default:
		return false;
	}
}

void UWidgetDataBinderComponent::FireTrigger(int32 TriggerIndex, bool bBecameActive)
{
	const FWidgetFieldTrigger& Trigger = Triggers[TriggerIndex];

	switch (Trigger.Action)
	{
	case EWidgetDataTriggerAction::PlayWhileTrue:
		// Changed has no "while", so its every change is a start.
		PlayOrStop(Trigger, bBecameActive || Trigger.Condition == EWidgetDataTriggerCondition::Changed);
		break;

	case EWidgetDataTriggerAction::Play:
		if (bBecameActive)
		{
			PlayOrStop(Trigger, /*bPlay=*/true);
		}
		break;

	case EWidgetDataTriggerAction::Stop:
		if (bBecameActive)
		{
			PlayOrStop(Trigger, /*bPlay=*/false);
		}
		break;
	}

	OnTriggerChanged.Broadcast(Trigger.TriggerName, bBecameActive);
}

void UWidgetDataBinderComponent::PlayOrStop(const FWidgetFieldTrigger& Trigger, bool bPlay)
{
	UWidget* Start = ResolveAnimationWidget(Trigger.AnimationWidget);
	if (!Start)
	{
		return;
	}

	// Outwards from the picked widget until something owns animations: a Widget Animation Component on a
	// widget, or a widget that is an animation host itself. The same search Play Widget Animation makes.
	static constexpr int32 MaxSearchDepth = 64;

	UObject* Search = Start;
	for (int32 Depth = 0; Search && Depth < MaxSearchDepth; ++Depth)
	{
		if (const UWidget* SearchWidget = Cast<UWidget>(Search))
		{
			if (UWidgetAnimationComponent* Component = UWidgetAnimationComponent::FindWidgetAnimationComponent(SearchWidget))
			{
				if (bPlay)
				{
					Component->PlayWidgetAnimationsById(Trigger.AnimIds, Trigger.Direction == EWidgetAnimPlayDirection::Reverse,
						FMath::Max(Trigger.PlaybackSpeed, 0.01f), Trigger.bHoldFinalPose, Trigger.bRestartIfPlaying);
				}
				else
				{
					Component->StopWidgetAnimationsById(Trigger.AnimIds, Trigger.bResetToStartOnStop);
				}
				return;
			}
		}

		if (IWidgetAnimHost* Host = Cast<IWidgetAnimHost>(Search))
		{
			if (bPlay)
			{
				Host->PlayWidgetAnimationsById(Trigger.AnimIds, Trigger.Direction == EWidgetAnimPlayDirection::Reverse,
					FMath::Max(Trigger.PlaybackSpeed, 0.01f), Trigger.bHoldFinalPose, Trigger.bRestartIfPlaying);
			}
			else
			{
				Host->StopWidgetAnimationsById(Trigger.AnimIds, Trigger.bResetToStartOnStop);
			}
			return;
		}

		Search = Search->GetOuter();
	}

	UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: trigger '%s' found no Widget Animation Component at or above %s to play its Anim IDs on."),
		*DescribeForLog(), *Trigger.TriggerName.ToString(), *GetNameSafe(Start));
}

// =====================================================================================================
// ViewModel lookup
// =====================================================================================================

UUserWidget* UWidgetDataBinderComponent::FindViewModelHost() const
{
	UWidget* OwnerWidget = BinderOwner.Get();
	if (!OwnerWidget)
	{
		return nullptr;
	}

	UUserWidget* Selected = Cast<UUserWidget>(OwnerWidget);
	UUserWidget* Containing = OwnerWidget->GetTypedOuter<UUserWidget>();

	// The selected widget when it is a User Widget that has this ViewModel (a binder on a row placed inside a
	// screen, feeding the row's own ViewModel); otherwise the Widget Blueprint the binder was added in. The
	// ViewModel plugin adds each ViewModel to the widget class as a variable of the same name.
	if (Selected && !ViewModelName.IsNone() && FindFProperty<FObjectPropertyBase>(Selected->GetClass(), ViewModelName))
	{
		return Selected;
	}

	return Containing ? Containing : Selected;
}

UObject* UWidgetDataBinderComponent::FindViewModelInView() const
{
	if (ViewModelName.IsNone())
	{
		return nullptr;
	}

	UUserWidget* Host = FindViewModelHost();
	if (!Host)
	{
		return nullptr;
	}

	if (UMVVMView* View = Host->GetExtension<UMVVMView>())
	{
		if (UObject* InView = View->GetViewModel(ViewModelName).GetObject())
		{
			return InView;
		}
	}

	// No view (yet): the variable the ViewModel plugin adds may still hold it.
	if (const FObjectPropertyBase* Variable = FindFProperty<FObjectPropertyBase>(Host->GetClass(), ViewModelName))
	{
		UObject* Held = Variable->GetObjectPropertyValue_InContainer(Host);
		if (Cast<INotifyFieldValueChanged>(Held))
		{
			return Held;
		}
	}

	return nullptr;
}

UObject* UWidgetDataBinderComponent::CreateViewModel()
{
	if (!ViewModelClass)
	{
		return nullptr;
	}

	// Reused across construct/destruct, so a widget removed and added back keeps its values.
	if (!OwnedViewModel || OwnedViewModel->GetClass() != ViewModelClass.Get())
	{
		UUserWidget* Host = FindViewModelHost();
		UObject* Outer = Host ? static_cast<UObject*>(Host) : static_cast<UObject*>(this);
		OwnedViewModel = NewObject<UObject>(Outer, ViewModelClass.Get(), NAME_None, RF_Transient);
	}

	// A Manual ViewModel of this name in the view is waiting for exactly this. The view may not be ready yet
	// during construct, so a failure here is quiet; the next-frame check retries it and warns if it still fails.
	if (!ViewModelName.IsNone())
	{
		PushViewModelToView(OwnedViewModel, /*bWarnOnFailure=*/false);
	}

	return OwnedViewModel;
}

bool UWidgetDataBinderComponent::PushViewModelToView(UObject* ViewModel, bool bWarnOnFailure)
{
	if (!ViewModel || ViewModelName.IsNone())
	{
		return false;
	}

	UUserWidget* Host = FindViewModelHost();
	UMVVMView* View = Host ? Host->GetExtension<UMVVMView>() : nullptr;

	const bool bSet = View && View->SetViewModel(ViewModelName, TScriptInterface<INotifyFieldValueChanged>(ViewModel));
	const bool bInView = bSet && View->GetViewModel(ViewModelName).GetObject() == ViewModel;

	if (!bInView && bWarnOnFailure)
	{
		UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: couldn't give ViewModel %s to the view of %s as '%s' (%s). The binder still writes to it, but View Bindings won't show it. Check that ViewModel Name matches the Viewmodels panel and that its Creation Type is Manual."),
			*DescribeForLog(), *GetNameSafe(ViewModel), *GetNameSafe(Host), *ViewModelName.ToString(),
			!Host ? TEXT("no User Widget found") : !View ? TEXT("the widget has no ViewModel view") : !bSet ? TEXT("the view refused it") : TEXT("the view kept a different one"));
	}

	UE_LOG(LogWidgetDataBinder, Verbose, TEXT("%s: gave ViewModel %s to the view of %s: %s."), *DescribeForLog(), *GetNameSafe(ViewModel), *GetNameSafe(Host), bInView ? TEXT("ok") : TEXT("failed"));
	return bInView;
}

UWidget* UWidgetDataBinderComponent::ResolveAnimationWidget(FName WidgetPath) const
{
	UWidget* OwnerWidget = BinderOwner.Get();
	if (!OwnerWidget || WidgetPath.IsNone())
	{
		return OwnerWidget;
	}

	UUserWidget* Root = OwnerWidget->GetTypedOuter<UUserWidget>();
	if (!Root)
	{
		Root = Cast<UUserWidget>(OwnerWidget);
	}

	UWidget* Found = Root ? UWidgetAnimLibrary::FindWidgetByPathInTree(Root, WidgetPath) : nullptr;
	return Found ? Found : OwnerWidget;
}

// =====================================================================================================
// Ticking
// =====================================================================================================

void UWidgetDataBinderComponent::EnsureTicker()
{
	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UWidgetDataBinderComponent::TickBinder));
	}
}

void UWidgetDataBinderComponent::StopTicker()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

bool UWidgetDataBinderComponent::TickBinder(float DeltaTime)
{
	if (bVerifyViewModelNextTick)
	{
		bVerifyViewModelNextTick = false;

		// The view has finished constructing now. If it owns a ViewModel we aren't bound to, it is the one its
		// View Bindings read, so bind to that instead.
		if (bOwnerConstructed && !ExplicitViewModel.IsValid())
		{
			UObject* InView = FindViewModelInView();
			if (InView && InView != BoundViewModel.Get())
			{
				AttachViewModel(InView, /*bKeepTriggerStates=*/true);
				BindSource();
			}
			else if (!InView && BoundViewModel.IsValid() && !ViewModelName.IsNone())
			{
				// The view wasn't ready to take our ViewModel during construct and hasn't made one of its own,
				// so it is showing nothing. Hand it over now; without this the binder writes to a ViewModel no
				// View Binding reads.
				PushViewModelToView(BoundViewModel.Get(), /*bWarnOnFailure=*/true);
			}
			else if (!InView && !BoundViewModel.IsValid() && !bWarnedNoViewModel)
			{
				bWarnedNoViewModel = true;
				UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: the widget has no ViewModel named '%s'. Check ViewModel Name, or turn on Create ViewModel If Missing."), *DescribeForLog(), *ViewModelName.ToString());
			}
		}
	}

	if (bAnyPolling)
	{
		if (!DataSource.IsValid())
		{
			HandleSourceLost();
		}
		else
		{
			for (int32 Index = 0; Index < BindingStates.Num(); ++Index)
			{
				FBindingState& State = BindingStates[Index];
				if (State.Mode != EWidgetDataUpdateMode::Poll)
				{
					continue;
				}

				const float Interval = FieldBindings.IsValidIndex(Index) ? FieldBindings[Index].PollInterval : 0.f;
				State.PollElapsed += DeltaTime;
				if (Interval <= 0.f || State.PollElapsed >= Interval)
				{
					State.PollElapsed = Interval > 0.f ? FMath::Fmod(State.PollElapsed, Interval) : 0.f;
					PushBinding(Index);
				}
			}
		}
	}

	const bool bKeepTicking = bAnyPolling || bVerifyViewModelNextTick;
	if (!bKeepTicking)
	{
		TickerHandle.Reset();
	}
	return bKeepTicking;
}

// =====================================================================================================
// Dropdowns
// =====================================================================================================

UUserWidget* UWidgetDataBinderComponent::GetOptionsRoot() const
{
	if (UWidget* OwnerWidget = BinderOwner.Get())
	{
		if (UUserWidget* Containing = OwnerWidget->GetTypedOuter<UUserWidget>())
		{
			return Containing;
		}
	}

	// The Details panel edits the component ARCHETYPE the Widget Blueprint keeps, which has no owner widget to
	// ask. Find the Widget Blueprint it belongs to and use that class's default instead.
	if (const UWidgetBlueprintGeneratedClass* GeneratedClass = GetTypedOuter<UWidgetBlueprintGeneratedClass>())
	{
		return GeneratedClass->GetDefaultObject<UUserWidget>();
	}

#if WITH_EDITORONLY_DATA
	if (const UBlueprint* Blueprint = GetTypedOuter<UBlueprint>())
	{
		if (Blueprint->GeneratedClass)
		{
			return Cast<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject());
		}
	}
#endif

	return nullptr;
}

TArray<FString> UWidgetDataBinderComponent::GetViewModelFieldOptions() const
{
	TArray<FString> Options;
	Options.Add(TEXT("None"));

	UClass* ViewModelType = ViewModelClass.Get();
	if (!ViewModelType)
	{
		return Options;
	}

	if (const INotifyFieldValueChanged* Notifier = Cast<INotifyFieldValueChanged>(ViewModelType->GetDefaultObject()))
	{
		Notifier->GetFieldNotificationDescriptor().ForEachField(ViewModelType, [&Options](UE::FieldNotification::FFieldId FieldId)
		{
			Options.AddUnique(FieldId.GetName().ToString());
			return true;
		});
	}

	return Options;
}

namespace WidgetDataBinderOptions
{
	static bool IsListedValue(const FProperty* Property)
	{
		return !CastField<FMulticastDelegateProperty>(Property) && !CastField<FDelegateProperty>(Property);
	}

	/**
	 * Blueprint-visible properties of Scope (and getters, when it is a class), prefixed; one level into
	 * components and structs, which is where gameplay values usually sit (Health Component, a Stats struct).
	 */
	static void Gather(const UStruct* Scope, const FString& Prefix, int32 DepthLeft, bool bEvents, TArray<FString>& Out)
	{
		if (!Scope)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(Scope); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintAssignable))
			{
				continue;
			}

			const FString PropertyPath = Prefix + Property->GetName();

			if (bEvents)
			{
				if (CastField<FMulticastDelegateProperty>(Property))
				{
					Out.AddUnique(PropertyPath);
				}
			}
			else if (IsListedValue(Property))
			{
				Out.AddUnique(PropertyPath);
			}

			if (DepthLeft <= 0)
			{
				continue;
			}

			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				if (ObjectProperty->PropertyClass && ObjectProperty->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
				{
					Gather(ObjectProperty->PropertyClass, PropertyPath + TEXT("."), DepthLeft - 1, bEvents, Out);
				}
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (!bEvents)
				{
					Gather(StructProperty->Struct, PropertyPath + TEXT("."), DepthLeft - 1, bEvents, Out);
				}
			}
		}

		if (bEvents)
		{
			return;
		}

		if (const UClass* ScopeClass = Cast<UClass>(Scope))
		{
			for (TFieldIterator<UFunction> It(ScopeClass); It; ++It)
			{
				const UFunction* Function = *It;
				if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure) && WidgetDataValues::IsGetterFunction(Function))
				{
					Out.AddUnique(Prefix + Function->GetName());
				}
			}
		}
	}
}

TArray<FString> UWidgetDataBinderComponent::GetSourcePathOptions() const
{
	TArray<FString> Options;
	WidgetDataBinderOptions::Gather(SourceClass.Get(), FString(), /*DepthLeft=*/1, /*bEvents=*/false, Options);
	return Options;
}

TArray<FString> UWidgetDataBinderComponent::GetEventPathOptions() const
{
	TArray<FString> Options;
	WidgetDataBinderOptions::Gather(SourceClass.Get(), FString(), /*DepthLeft=*/1, /*bEvents=*/true, Options);
	return Options;
}

TArray<FString> UWidgetDataBinderComponent::GetAnimationWidgetOptions() const
{
	TArray<FString> Options;

	if (UUserWidget* Root = GetOptionsRoot())
	{
		UWidgetAnimLibrary::GatherTargetableWidgetPathsInTree(Root, Options);
	}

	// First, so there is a way back to "the widget this binder is on" once something has been picked.
	Options.Insert(TEXT("None"), 0);
	return Options;
}

FString UWidgetDataBinderComponent::DescribeForLog() const
{
	const UWidget* OwnerWidget = BinderOwner.Get();
	const UUserWidget* Containing = OwnerWidget ? OwnerWidget->GetTypedOuter<UUserWidget>() : nullptr;
	return FString::Printf(TEXT("Widget Data Binder on %s in %s"), *GetNameSafe(OwnerWidget), *GetNameSafe(Containing ? Containing->GetClass() : nullptr));
}