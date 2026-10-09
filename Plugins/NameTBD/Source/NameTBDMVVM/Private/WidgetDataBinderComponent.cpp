// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetDataBinderComponent.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ActorComponent.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "GameFramework/Actor.h"
#include "Internationalization/Text.h"
#include "Misc/App.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEasing.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"
#include "NameTBD/Widgets/FlowList/FlowList.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SWidget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachineLibrary.h"
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
		BindForDesigner();
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
	else if (bDesignBound)
	{
		BindForDesigner();
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
	else if (bDesignBound)
	{
		BindForDesigner();
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
	bAnyCounting = false;
	bDesignBound = false;
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

	// One subscription per watched field, however many triggers and lists watch it.
	auto Watch = [this, Notifier, &Descriptor, ViewModelType](FName Field, const FString& Watcher)
	{
		if (Field.IsNone())
		{
			return;
		}

		const UE::FieldNotification::FFieldId FieldId = Descriptor.GetField(ViewModelType, Field);
		if (!FieldId.IsValid())
		{
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: %s watches '%s', which isn't a Field Notify field of %s. It will never act."), *DescribeForLog(), *Watcher, *Field.ToString(), *ViewModelType->GetName());
			return;
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
	};

	TriggerStates.Reset();
	TriggerStates.SetNum(Triggers.Num());
	for (int32 Index = 0; Index < Triggers.Num(); ++Index)
	{
		const FWidgetFieldTrigger& Trigger = Triggers[Index];
		FTriggerState& State = TriggerStates[Index];
		State.FieldPath.Set(Trigger.ViewModelField.ToString());
		State.bActive = PreviousActive.IsValidIndex(Index) && PreviousActive[Index];
		Watch(Trigger.ViewModelField, FString::Printf(TEXT("trigger %d"), Index));

		// Score > Best Score is worked out again when Best Score changes too.
		if (Trigger.Condition == EWidgetDataTriggerCondition::Compare && Trigger.CompareTo == EWidgetDataCompareTo::Field)
		{
			State.CompareFieldPath.Set(Trigger.CompareField.ToString());
			Watch(Trigger.CompareField, FString::Printf(TEXT("trigger %d's Compare Field"), Index));
		}

		if (Trigger.Action == EWidgetDataTriggerAction::CountTo)
		{
			State.CountProperty = Trigger.CountField.IsNone() ? nullptr : FindFProperty<FProperty>(ViewModelType, Trigger.CountField);
			if (!State.CountProperty)
			{
				UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: trigger %d counts into '%s', which %s doesn't have."), *DescribeForLog(), Index, *Trigger.CountField.ToString(), *ViewModelType->GetName());
			}
		}
	}

	// The lists keep their entries across ViewModels: a new one is shown by key, reusing what it can.
	ListStates.SetNum(ListBindings.Num());
	for (int32 Index = 0; Index < ListBindings.Num(); ++Index)
	{
		ListStates[Index].FieldPath.Set(ListBindings[Index].ViewModelField.ToString());
		ListStates[Index].bWarned = false;
		Watch(ListBindings[Index].ViewModelField, FString::Printf(TEXT("List Binding %d"), Index));
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

		// A list's element, before the source's values: an entry of a struct list has no source at all.
		ApplyItemData();

		if (!Source)
		{
			if (bUseMockDataWithoutSource && !ItemData.IsValid())
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
	ReconcileAllLists(/*bInitial=*/true);

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

void UWidgetDataBinderComponent::ApplyMockValues(UObject* ViewModel, const TSet<FName>* Skip)
{
	if (!ViewModel)
	{
		return;
	}

	for (const FWidgetDataMockValue& Mock : MockValues)
	{
		if (Skip && Skip->Contains(Mock.ViewModelField))
		{
			continue;
		}

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
		const FWidgetFieldTrigger& Trigger = Triggers[Index];
		const bool bComparedField = Trigger.Condition == EWidgetDataTriggerCondition::Compare
			&& Trigger.CompareTo == EWidgetDataCompareTo::Field && Trigger.CompareField == Field;
		if (Trigger.ViewModelField == Field || bComparedField)
		{
			EvaluateTrigger(Index, /*bInitial=*/false);
		}
	}

	for (int32 Index = 0; Index < ListBindings.Num(); ++Index)
	{
		if (ListBindings[Index].ViewModelField == Field)
		{
			ReconcileList(Index, /*bInitial=*/false);
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

	if (Trigger.Action == EWidgetDataTriggerAction::CountTo)
	{
		double Number = 0.0;
		if (!WidgetDataValues::ReadNumber(Value.Property, Value.ValuePtr, Number))
		{
			Number = WidgetDataValues::IsTruthy(Value.Property, Value.ValuePtr) ? 1.0 : 0.0;
		}

		const bool bChanged = !State.bHasLastNumber || Number != State.LastNumber;
		State.bHasLastNumber = true;
		State.LastNumber = Number;

		// At binding the number is shown where it is, never counted to: a row pointed at another player shows their score.
		// A count only starts on a change after that, from wherever the shown number is - mid-count included.
		const float Duration = FMath::Max(Trigger.CountDuration, 0.f);
		if (bInitial || !State.bHasCountShown || Duration <= 0.f)
		{
			if (!bInitial || Trigger.bEvaluateOnBind || !State.bHasCountShown)
			{
				State.bCounting = false;
				State.CountShown = Number;
				State.bHasCountShown = true;
				WriteCount(TriggerIndex, Number);
			}
			return;
		}
		if (bChanged)
		{
			State.CountFrom = State.CountShown;
			State.CountTarget = Number;
			State.CountElapsed = 0.f;
			State.bCounting = true;
			bAnyCounting = true;
			EnsureTicker();
			KeepDesignerAwake(Duration + 0.1f);
		}
		return;
	}

	if (Trigger.Condition == EWidgetDataTriggerCondition::WentUp || Trigger.Condition == EWidgetDataTriggerCondition::WentDown)
	{
		if (Trigger.Action == EWidgetDataTriggerAction::SetValue || Trigger.Action == EWidgetDataTriggerAction::SetProgress
			|| Trigger.Action == EWidgetDataTriggerAction::SetInput || Trigger.Action == EWidgetDataTriggerAction::SetParameter)
		{
			// Those act on every change below, with no condition.
		}
		else
		{
			double Number = 0.0;
			if (!WidgetDataValues::ReadNumber(Value.Property, Value.ValuePtr, Number))
			{
				Number = WidgetDataValues::IsTruthy(Value.Property, Value.ValuePtr) ? 1.0 : 0.0;
			}
			const bool bHadNumber = State.bHasLastNumber;
			const double Before = State.LastNumber;
			State.bHasLastNumber = true;
			State.LastNumber = Number;

			// As Changed: a rebind is not a change, even to another number.
			const bool bWent = Trigger.Condition == EWidgetDataTriggerCondition::WentUp ? Number > Before : Number < Before;
			if (bHadNumber && bWent && !bInitial)
			{
				FireTrigger(TriggerIndex, /*bBecameActive=*/true);
			}
			return;
		}
	}

	if (Trigger.Action == EWidgetDataTriggerAction::SetParameter)
	{
		// As Set Value: at binding only with Evaluate On Bind, after that on every change - compared as text, which covers
		// every type a parameter can take (a Name, an enum, a color).
		FString Text;
		Value.Property->ExportTextItem_Direct(Text, Value.ValuePtr, nullptr, nullptr, PPF_None);
		const bool bChanged = !State.bHasLastText || Text != State.LastText;
		State.LastText = MoveTemp(Text);
		State.bHasLastText = true;
		if (bInitial ? Trigger.bEvaluateOnBind : bChanged)
		{
			SetAnimationParameter(Trigger, Value);
		}
		return;
	}

	if (Trigger.Action == EWidgetDataTriggerAction::SetValue || Trigger.Action == EWidgetDataTriggerAction::SetProgress || Trigger.Action == EWidgetDataTriggerAction::SetInput)
	{
		// No condition and no edge: every change of the field is a value, as it is - each track maps it onto itself. A
		// Vector 2D or Linear Color field carries its channels; a number or bool (0 and 1) is one value.
		FVector4 Raw(0.0, 0.0, 0.0, 0.0);
		const FStructProperty* StructProperty = CastField<FStructProperty>(Value.Property);
		double Number = 0.0;
		if (StructProperty && StructProperty->Struct == TBaseStructure<FVector2D>::Get())
		{
			Raw = WidgetAnimValue::FromVector2D(*static_cast<const FVector2D*>(Value.ValuePtr));
		}
		else if (StructProperty && StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
		{
			Raw = WidgetAnimValue::FromColor(*static_cast<const FLinearColor*>(Value.ValuePtr));
		}
		else if (WidgetDataValues::ReadNumber(Value.Property, Value.ValuePtr, Number))
		{
			Raw = WidgetAnimValue::FromFloat(Number);
		}
		else
		{
			Raw = WidgetAnimValue::FromFloat(WidgetDataValues::IsTruthy(Value.Property, Value.ValuePtr) ? 1.0 : 0.0);
		}

		// At binding the value is shown where it is rather than traveled to - a row pointed at another pawn shows that
		// pawn's health at once - and only with Evaluate On Bind. After that, only a change sends anything.
		if (bInitial)
		{
			if (Trigger.bEvaluateOnBind)
			{
				if (Trigger.Action == EWidgetDataTriggerAction::SetInput)
				{
					SetMachineInput(Trigger, Raw.X);
				}
				else
				{
					SetAnimationValue(Trigger, Raw, /*bSnap=*/true);
				}
			}
		}
		else if (!State.bHasLastValue || State.LastValue != Raw)
		{
			if (Trigger.Action == EWidgetDataTriggerAction::SetInput)
			{
				SetMachineInput(Trigger, Raw.X);
			}
			else
			{
				SetAnimationValue(Trigger, Raw, /*bSnap=*/false);
			}
		}

		State.bHasLastValue = true;
		State.LastValue = Raw;
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
		if (Trigger.CompareTo == EWidgetDataCompareTo::Field)
		{
			// Read live: Score > Best Score holds against whatever Best Score is now.
			FWidgetDataValue Other;
			if (Trigger.CompareField.IsNone() || !FWidgetDataValue::Resolve(BoundViewModel.Get(), FWidgetDataPath(Trigger.CompareField.ToString()), Other)
				|| !WidgetDataValues::ReadNumber(Other.Property, Other.ValuePtr, Limit))
			{
				return false;
			}
		}
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
		// Changed, Went Up and Went Down have no "while", so their every change is a start.
		PlayOrStop(Trigger, bBecameActive || Trigger.Condition == EWidgetDataTriggerCondition::Changed
			|| Trigger.Condition == EWidgetDataTriggerCondition::WentUp || Trigger.Condition == EWidgetDataTriggerCondition::WentDown);
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

	default:
		// Set Value acts in EvaluateTrigger, on every change, and never gets here.
		break;
	}

	OnTriggerChanged.Broadcast(Trigger.TriggerName, bBecameActive);
}

bool UWidgetDataBinderComponent::FindAnimationOwner(const FWidgetFieldTrigger& Trigger, UWidgetAnimationComponent*& OutComponent, IWidgetAnimHost*& OutHost) const
{
	OutComponent = nullptr;
	OutHost = nullptr;

	UWidget* Start = ResolveAnimationWidget(Trigger.AnimationWidget);
	if (!Start)
	{
		return false;
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
				OutComponent = Component;
				return true;
			}
		}

		if (IWidgetAnimHost* Host = Cast<IWidgetAnimHost>(Search))
		{
			OutHost = Host;
			return true;
		}

		Search = Search->GetOuter();
	}

	UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: trigger '%s' found no Widget Animation Component at or above %s to play its Anim IDs on."),
		*DescribeForLog(), *Trigger.TriggerName.ToString(), *GetNameSafe(Start));
	return false;
}

void UWidgetDataBinderComponent::PlayOrStop(const FWidgetFieldTrigger& Trigger, bool bPlay)
{
	UWidgetAnimationComponent* Component = nullptr;
	IWidgetAnimHost* Host = nullptr;
	if (!FindAnimationOwner(Trigger, Component, Host))
	{
		return;
	}

	// In the designer (a data preview), the component plays on its runtime clock from the authored pose.
	if (bDesignBound)
	{
		if (Component)
		{
			Component->BeginDesignTimePlayback();
		}
		KeepDesignerAwake(3.f);
	}

	if (Component)
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

	if (bPlay)
	{
		Host->PlayWidgetAnimationsById(Trigger.AnimIds, Trigger.Direction == EWidgetAnimPlayDirection::Reverse,
			FMath::Max(Trigger.PlaybackSpeed, 0.01f), Trigger.bHoldFinalPose, Trigger.bRestartIfPlaying);
	}
	else
	{
		Host->StopWidgetAnimationsById(Trigger.AnimIds, Trigger.bResetToStartOnStop);
	}
}

void UWidgetDataBinderComponent::SetAnimationParameter(const FWidgetFieldTrigger& Trigger, const FWidgetDataValue& Value)
{
	UWidget* Target = ResolveAnimationWidget(Trigger.AnimationWidget);
	if (Trigger.ParameterName.IsNone() || !Target || !Value.IsValid())
	{
		return;
	}

	// The field as a parameter value of its own kind: the animation's declaration converts it on arrival if it is another.
	FWidgetAnimParameterValue Parameter;
	Parameter.Name = Trigger.ParameterName;
	const FStructProperty* StructProperty = CastField<FStructProperty>(Value.Property);
	const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Value.Property);
	const FByteProperty* ByteProperty = CastField<FByteProperty>(Value.Property);
	const UEnum* Enum = EnumProperty ? EnumProperty->GetEnum() : (ByteProperty ? ByteProperty->Enum.Get() : nullptr);
	double Number = 0.0;
	if (StructProperty && StructProperty->Struct == TBaseStructure<FVector2D>::Get())
	{
		Parameter.Type = EWidgetAnimParameterType::Vector2D;
		Parameter.Vector2D = *static_cast<const FVector2D*>(Value.ValuePtr);
	}
	else if (StructProperty && StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
	{
		Parameter.Type = EWidgetAnimParameterType::Color;
		Parameter.Color = *static_cast<const FLinearColor*>(Value.ValuePtr);
	}
	else if (StructProperty && StructProperty->Struct == TBaseStructure<FColor>::Get())
	{
		Parameter.Type = EWidgetAnimParameterType::Color;
		Parameter.Color = FLinearColor(*static_cast<const FColor*>(Value.ValuePtr));
	}
	else if (CastField<FBoolProperty>(Value.Property))
	{
		Parameter.Type = EWidgetAnimParameterType::Bool;
		Parameter.bBool = WidgetDataValues::IsTruthy(Value.Property, Value.ValuePtr);
	}
	else if (Enum)
	{
		// An enum by its option's name, as a Choice's options read.
		int64 EnumValue = 0;
		if (EnumProperty)
		{
			EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value.ValuePtr);
		}
		else
		{
			EnumValue = ByteProperty->GetPropertyValue(Value.ValuePtr);
		}
		Parameter.Type = EWidgetAnimParameterType::Choice;
		Parameter.Option = FName(*Enum->GetNameStringByValue(EnumValue));
	}
	else if (CastField<FNameProperty>(Value.Property) || CastField<FStrProperty>(Value.Property) || CastField<FTextProperty>(Value.Property))
	{
		FString Text;
		if (const FTextProperty* TextProperty = CastField<FTextProperty>(Value.Property))
		{
			Text = TextProperty->GetPropertyValue(Value.ValuePtr).ToString();
		}
		else
		{
			Value.Property->ExportTextItem_Direct(Text, Value.ValuePtr, nullptr, nullptr, PPF_None);
		}
		Parameter.Type = EWidgetAnimParameterType::Choice;
		Parameter.Option = FName(*Text);
	}
	else if (WidgetDataValues::ReadNumber(Value.Property, Value.ValuePtr, Number))
	{
		const bool bInteger = CastField<FNumericProperty>(Value.Property) && CastField<FNumericProperty>(Value.Property)->IsInteger();
		Parameter.Type = bInteger ? EWidgetAnimParameterType::Integer : EWidgetAnimParameterType::Float;
		Parameter.Float = static_cast<float>(Number);
		Parameter.Integer = static_cast<int32>(FMath::RoundToInt64(Number));
	}
	else
	{
		Parameter.Type = EWidgetAnimParameterType::Bool;
		Parameter.bBool = WidgetDataValues::IsTruthy(Value.Property, Value.ValuePtr);
	}

	// For the entries with the Anim IDs, or every animation the widget plays.
	if (Trigger.AnimIds.Num() == 0)
	{
		UWidgetAnimPlayLibrary::SetWidgetAnimationParameter(Target, Parameter, nullptr, NAME_None, /*bSearchParentWidgets=*/true);
	}
	for (const FName AnimId : Trigger.AnimIds)
	{
		UWidgetAnimPlayLibrary::SetWidgetAnimationParameter(Target, Parameter, nullptr, AnimId, /*bSearchParentWidgets=*/true);
	}
}

void UWidgetDataBinderComponent::SetAnimationValue(const FWidgetFieldTrigger& Trigger, const FVector4& Value, bool bSnap)
{
	const bool bProgress = Trigger.Action == EWidgetDataTriggerAction::SetProgress;
	UWidgetAnimationComponent* Component = nullptr;
	IWidgetAnimHost* Host = nullptr;
	if ((!bProgress && Trigger.ValueName.IsNone()) || !FindAnimationOwner(Trigger, Component, Host))
	{
		return;
	}

	if (bDesignBound && Component)
	{
		Component->BeginDesignTimePlayback();
		KeepDesignerAwake(2.f);
	}

	// By name alone: every animation's tracks with this Value Name follow the field. A progress goes to the percentage
	// animations of the entries with the Anim IDs, or of every entry.
	TArray<FWidgetAnimValueId, TInlineAllocator<2>> Ids;
	if (!bProgress)
	{
		Ids.Add(FWidgetAnimValueId::Anywhere(Trigger.ValueName));
	}
	else if (Trigger.AnimIds.Num() == 0)
	{
		Ids.Add(FWidgetAnimValueId::Progress(NAME_None));
	}
	else
	{
		for (const FName AnimId : Trigger.AnimIds)
		{
			Ids.Add(FWidgetAnimValueId::Progress(AnimId));
		}
	}

	for (const FWidgetAnimValueId& Id : Ids)
	{
		if (Component)
		{
			Component->SetWidgetAnimationValues(Id, Value, bSnap, /*bStartIfNotPlaying=*/true);
		}
		else
		{
			Host->SetWidgetAnimationValues(Id, Value, bSnap, /*bStartIfNotPlaying=*/true);
		}
	}
}

void UWidgetDataBinderComponent::SetMachineInput(const FWidgetFieldTrigger& Trigger, double Value)
{
	if (Trigger.InputName.IsNone())
	{
		return;
	}

	// The machine on the picked widget, or inside it, or outwards from it, that has the input - as Set State Machine Input
	// finds it from a graph.
	TArray<UWidgetAnimationComponent*> Components;
	UWidgetAnimStateMachineLibrary::FindComponents(ResolveAnimationWidget(Trigger.AnimationWidget), nullptr, Trigger.InputName, /*bSearchParentWidgets=*/true, /*bAll=*/false, Components);
	if (Components.Num() == 0)
	{
		UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: trigger '%s' found no state machine with an input '%s' at, in or above the Animation Widget."),
			*DescribeForLog(), *Trigger.TriggerName.ToString(), *Trigger.InputName.ToString());
		return;
	}
	for (UWidgetAnimationComponent* Component : Components)
	{
		Component->SetStateMachineInputValue(Trigger.InputName, Value);
	}
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
// Count Into Field
// =====================================================================================================

void UWidgetDataBinderComponent::WriteCount(int32 TriggerIndex, double Number)
{
	UObject* ViewModel = BoundViewModel.Get();
	if (!ViewModel || !Triggers.IsValidIndex(TriggerIndex) || !TriggerStates.IsValidIndex(TriggerIndex))
	{
		return;
	}
	const FWidgetFieldTrigger& Trigger = Triggers[TriggerIndex];
	const FProperty* Target = TriggerStates[TriggerIndex].CountProperty;
	if (!Target)
	{
		return;
	}

	FWidgetDataScratch Written(Target);
	if (WidgetDataValues::IsNumeric(Target))
	{
		WidgetDataValues::WriteNumber(Target, Written.Get(), Number);
	}
	else
	{
		// Written out as the culture writes numbers: 12,480 - or 12.480 - with the decimals asked for.
		FNumberFormattingOptions Options;
		Options.SetUseGrouping(Trigger.bCountGroupDigits);
		Options.SetMinimumFractionalDigits(FMath::Clamp(Trigger.CountDecimals, 0, 6));
		Options.SetMaximumFractionalDigits(FMath::Clamp(Trigger.CountDecimals, 0, 6));
		const FText Formatted = FText::AsNumber(Number, &Options);

		if (const FTextProperty* TextProperty = CastField<FTextProperty>(Target))
		{
			TextProperty->SetPropertyValue(Written.Get(), Formatted);
		}
		else if (const FStrProperty* StringProperty = CastField<FStrProperty>(Target))
		{
			StringProperty->SetPropertyValue(Written.Get(), Formatted.ToString());
		}
		else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Target))
		{
			NameProperty->SetPropertyValue(Written.Get(), FName(*Formatted.ToString()));
		}
		else
		{
			FTriggerState& State = TriggerStates[TriggerIndex];
			if (!State.bWarned)
			{
				State.bWarned = true;
				UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: trigger %d counts into '%s', which is a %s - only numbers, Text, String and Name can be counted into."),
					*DescribeForLog(), TriggerIndex, *Trigger.CountField.ToString(), *Target->GetCPPType());
			}
			return;
		}
	}
	WidgetDataValues::WriteField(ViewModel, Target, Written.Get());
}

bool UWidgetDataBinderComponent::TickCounts(float DeltaTime)
{
	bool bAnyStill = false;
	for (int32 Index = 0; Index < TriggerStates.Num(); ++Index)
	{
		FTriggerState& State = TriggerStates[Index];
		if (!State.bCounting || !Triggers.IsValidIndex(Index))
		{
			continue;
		}
		const FWidgetFieldTrigger& Trigger = Triggers[Index];
		State.CountElapsed += DeltaTime;
		const float Duration = FMath::Max(Trigger.CountDuration, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp(State.CountElapsed / Duration, 0.f, 1.f);
		const float Eased = WidgetAnimEasing::Evaluate(Trigger.CountEasing, nullptr, Alpha);
		State.CountShown = FMath::Lerp(State.CountFrom, State.CountTarget, static_cast<double>(Eased));
		if (Alpha >= 1.f)
		{
			State.CountShown = State.CountTarget;
			State.bCounting = false;
		}
		else
		{
			bAnyStill = true;
		}
		WriteCount(Index, State.CountShown);
	}
	return bAnyStill;
}

// =====================================================================================================
// List Bindings
// =====================================================================================================

namespace WidgetDataBinderLists
{
	/** The Widget Data Binder of an entry: on the entry itself, else the first in its own hierarchy. */
	static UWidgetDataBinderComponent* FindEntryBinder(UUserWidget* Entry)
	{
		if (UWidgetDataBinderComponent* OnEntry = UWidgetDataBinderComponent::FindWidgetDataBinder(Entry))
		{
			return OnEntry;
		}
		UWidgetDataBinderComponent* Found = nullptr;
		if (Entry && Entry->WidgetTree)
		{
			Entry->WidgetTree->ForEachWidget([&Found](UWidget* Child)
			{
				if (!Found)
				{
					Found = UWidgetDataBinderComponent::FindWidgetDataBinder(Child);
				}
			});
		}
		return Found;
	}

	/** An element's key as text: Key Field's value (struct member, or a path on an object), else what identifies it. */
	static FString MakeKey(const FProperty* Inner, const void* Element, int32 Position, FName KeyField)
	{
		FString Key;
		if (const FStructProperty* StructInner = CastField<FStructProperty>(Inner))
		{
			const FProperty* KeyProperty = KeyField.IsNone() ? nullptr : FindFProperty<FProperty>(StructInner->Struct, KeyField);
			if (!KeyProperty)
			{
				return FString::FromInt(Position);
			}
			KeyProperty->ExportTextItem_InContainer(Key, Element, nullptr, nullptr, PPF_None);
			return Key;
		}

		const FObjectPropertyBase* ObjectInner = CastField<FObjectPropertyBase>(Inner);
		UObject* Object = ObjectInner ? ObjectInner->GetObjectPropertyValue(Element) : nullptr;
		if (!Object)
		{
			return TEXT("(none)");
		}
		if (!KeyField.IsNone())
		{
			FWidgetDataValue KeyValue;
			if (FWidgetDataValue::Resolve(Object, FWidgetDataPath(KeyField.ToString()), KeyValue))
			{
				KeyValue.Property->ExportTextItem_Direct(Key, KeyValue.ValuePtr, nullptr, nullptr, PPF_None);
				return Key;
			}
		}
		return Object->GetPathName();
	}

	/** "Name=Ann, Score=1200" split at the commas outside quotes and brackets, each split at its first '='. */
	static void ParsePairs(const FString& Row, TArray<TPair<FString, FString>>& OutPairs)
	{
		OutPairs.Reset();
		TArray<FString> Parts;
		FString Current;
		int32 Depth = 0;
		bool bQuoted = false;
		for (int32 Index = 0; Index < Row.Len(); ++Index)
		{
			const TCHAR Char = Row[Index];
			if (Char == TEXT('"') && (Index == 0 || Row[Index - 1] != TEXT('\\')))
			{
				bQuoted = !bQuoted;
			}
			else if (!bQuoted && Char == TEXT('('))
			{
				++Depth;
			}
			else if (!bQuoted && Char == TEXT(')'))
			{
				--Depth;
			}
			else if (!bQuoted && Depth == 0 && Char == TEXT(','))
			{
				Parts.Add(Current);
				Current.Reset();
				continue;
			}
			Current.AppendChar(Char);
		}
		Parts.Add(Current);

		for (const FString& Part : Parts)
		{
			FString Name;
			FString Value;
			if (Part.Split(TEXT("="), &Name, &Value))
			{
				OutPairs.Emplace(Name.TrimStartAndEnd(), Value.TrimStartAndEnd());
			}
		}
	}
}

void UWidgetDataBinderComponent::ReconcileAllLists(bool bInitial)
{
	for (int32 Index = 0; Index < ListBindings.Num(); ++Index)
	{
		ReconcileList(Index, bInitial);
	}
}

void UWidgetDataBinderComponent::ReconcileList(int32 ListIndex, bool bInitial)
{
	using namespace WidgetDataBinderLists;

	if (!ListBindings.IsValidIndex(ListIndex) || !ListStates.IsValidIndex(ListIndex))
	{
		return;
	}
	const FWidgetListBinding& Binding = ListBindings[ListIndex];
	FListState& State = ListStates[ListIndex];
	UObject* ViewModel = BoundViewModel.Get();
	UWidget* OwnerWidget = BinderOwner.Get();
	if (!ViewModel || !OwnerWidget || Binding.ViewModelField.IsNone())
	{
		return;
	}

	auto Warn = [this, &State, ListIndex, &Binding](const FString& Problem)
	{
		if (!State.bWarned)
		{
			State.bWarned = true;
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: List Binding %d ('%s') %s"), *DescribeForLog(), ListIndex, *Binding.ViewModelField.ToString(), *Problem);
		}
	};

	FWidgetDataValue Value;
	FString Error;
	if (!FWidgetDataValue::Resolve(ViewModel, State.FieldPath, Value, &Error))
	{
		Warn(FString::Printf(TEXT("can't read the field: %s."), *Error));
		return;
	}
	const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Value.Property);
	const FProperty* Inner = ArrayProperty ? ArrayProperty->Inner : nullptr;
	if (!Inner || !(CastField<FStructProperty>(Inner) || CastField<FObjectPropertyBase>(Inner)))
	{
		Warn(TEXT("isn't an array of structs or objects."));
		return;
	}
	if (!Binding.EntryWidgetClass)
	{
		Warn(TEXT("has no Entry Widget Class."));
		return;
	}
	UPanelWidget* Panel = Cast<UPanelWidget>(ResolveAnimationWidget(Binding.ListWidget));
	if (!Panel)
	{
		Warn(TEXT("has no List Widget that can hold widgets (a Flow List, a Vertical Box...)."));
		return;
	}
	UFlowList* FlowList = Cast<UFlowList>(Panel);
	if (FlowList && FlowList->IsVirtual())
	{
		Warn(TEXT("fills a Flow List that is a virtual list (it has its own Entry Widget Class). Clear the Flow List's Entry Widget Class: the List Binding makes the entries."));
		return;
	}
	UUserWidget* EntryOuter = Cast<UUserWidget>(OwnerWidget);
	if (!EntryOuter)
	{
		EntryOuter = OwnerWidget->GetTypedOuter<UUserWidget>();
	}
	if (!EntryOuter)
	{
		return;
	}

	// The keys now, made unique (a second "Ann" is "Ann#2"), in order.
	FScriptArrayHelper Array(ArrayProperty, Value.ValuePtr);
	const int32 Count = Array.Num();
	TArray<FString> Keys;
	Keys.Reserve(Count);
	{
		TMap<FString, int32> Seen;
		for (int32 Position = 0; Position < Count; ++Position)
		{
			FString Key = MakeKey(Inner, Array.GetRawPtr(Position), Position, Binding.KeyField);
			int32& Times = Seen.FindOrAdd(Key);
			if (++Times > 1)
			{
				Key += FString::Printf(TEXT("#%d"), Times);
			}
			Keys.Add(MoveTemp(Key));
		}
	}

	const bool bAnimate = !bInitial && Binding.bAnimateChanges;
	auto RemoveFromList = [FlowList, Panel, bAnimate](UWidget* Child)
	{
		if (FlowList)
		{
			FlowList->RemoveItem(Child, bAnimate);
		}
		else
		{
			Panel->RemoveChild(Child);
		}
	};

	// The first time, what was placed in the designer goes: it was a layout preview.
	if (!State.bFilled)
	{
		State.bFilled = true;
		for (int32 ChildIndex = Panel->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
		{
			if (UWidget* Child = Panel->GetChildAt(ChildIndex))
			{
				if (FlowList)
				{
					FlowList->RemoveItem(Child, /*bAnimate=*/false);
				}
				else
				{
					Panel->RemoveChild(Child);
				}
			}
		}
	}

	TMap<FString, UUserWidget*> Shown;
	for (int32 Index = 0; Index < State.Keys.Num() && Index < State.Entries.Num(); ++Index)
	{
		UUserWidget* Entry = State.Entries[Index].Get();
		if (Entry && Entry->GetParent() == Panel)
		{
			Shown.Add(State.Keys[Index], Entry);
		}
	}

	// Gone: their exit plays (a Flow List keeps them while it does).
	{
		TSet<FString> Kept(Keys);
		for (const TPair<FString, UUserWidget*>& Pair : Shown)
		{
			if (!Kept.Contains(Pair.Key))
			{
				RemoveFromList(Pair.Value);
				ListEntryWidgets.Remove(Pair.Value);
			}
		}
	}

	// Kept by key, or made - given their element before they are first built, so they never show a default.
	TArray<UUserWidget*> Entries;
	Entries.Reserve(Count);
	for (int32 Position = 0; Position < Count; ++Position)
	{
		UUserWidget* Entry = Shown.FindRef(Keys[Position]);
		if (!Entry)
		{
			Entry = CreateWidget<UUserWidget>(EntryOuter, Binding.EntryWidgetClass);
			if (!Entry)
			{
				Warn(TEXT("couldn't make its Entry Widget Class."));
				return;
			}
#if WITH_EDITOR
			// In the designer the entry is part of the preview: built as designed widgets are, its own binder previewing too.
			if (OwnerWidget->IsDesignTime())
			{
				Entry->SetDesignerFlags(OwnerWidget->GetDesignerFlags());
			}
#endif
			ListEntryWidgets.Add(Entry);
		}
		PushListElement(ListIndex, Entry, Inner, Array.GetRawPtr(Position), Position);
		Entries.Add(Entry);
	}

	// In order: a Flow List moves each into place, so the ones between slide; a new one enters where it belongs.
	if (FlowList)
	{
		for (int32 Position = 0; Position < Entries.Num(); ++Position)
		{
			UUserWidget* Entry = Entries[Position];
			if (Entry->GetParent() != FlowList)
			{
				FlowList->InsertItem(Entry, Position, bAnimate);
			}
			else if (FlowList->GetChildIndex(Entry) != Position)
			{
				FlowList->MoveItem(Entry, Position);
			}
		}
	}
	else
	{
		bool bInOrder = Panel->GetChildrenCount() == Entries.Num();
		for (int32 Position = 0; bInOrder && Position < Entries.Num(); ++Position)
		{
			bInOrder = Panel->GetChildAt(Position) == Entries[Position];
		}
		if (!bInOrder)
		{
			Panel->ClearChildren();
			for (UUserWidget* Entry : Entries)
			{
				Panel->AddChild(Entry);
			}
		}
	}

	State.Keys = MoveTemp(Keys);
	State.Entries.Reset(Entries.Num());
	for (UUserWidget* Entry : Entries)
	{
		State.Entries.Add(Entry);
	}

	if (bAnimate)
	{
		KeepDesignerAwake(2.f);
	}
}

void UWidgetDataBinderComponent::PushListElement(int32 ListIndex, UUserWidget* Entry, const FProperty* Inner, const void* ElementPtr, int32 Position)
{
	const FWidgetListBinding& Binding = ListBindings[ListIndex];
	UWidgetDataBinderComponent* EntryBinder = WidgetDataBinderLists::FindEntryBinder(Entry);
	if (!EntryBinder)
	{
		FListState& State = ListStates[ListIndex];
		if (!State.bWarnedNoEntryBinder)
		{
			State.bWarnedNoEntryBinder = true;
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: List Binding %d's entry %s has no Widget Data Binder to give its element to. Add one to the entry widget."),
				*DescribeForLog(), ListIndex, *GetNameSafe(Entry ? Entry->GetClass() : nullptr));
		}
		return;
	}

	if (const FStructProperty* StructInner = CastField<FStructProperty>(Inner))
	{
		EntryBinder->SetItemData(StructInner->Struct, ElementPtr, Binding.PositionField, Position + 1);
		return;
	}

	const FObjectPropertyBase* ObjectInner = CastField<FObjectPropertyBase>(Inner);
	UObject* Element = ObjectInner ? ObjectInner->GetObjectPropertyValue(ElementPtr) : nullptr;
	if (Cast<INotifyFieldValueChanged>(Element))
	{
		// A list of ViewModels: each entry shows its own, so a change inside one reaches its entry directly.
		if (EntryBinder->ExplicitViewModel.Get() != Element)
		{
			EntryBinder->SetViewModel(Element);
		}
	}
	else if (EntryBinder->GetSource() != Element)
	{
		EntryBinder->SetSource(Element);
	}
	EntryBinder->SetItemPosition(Binding.PositionField, Position + 1);
}

// =====================================================================================================
// Item data (one element of a list)
// =====================================================================================================

void UWidgetDataBinderComponent::SetItemData(const UScriptStruct* Struct, const void* Data, FName PositionFieldName, int32 Position)
{
	if (!Struct || !Data)
	{
		ItemData.Reset();
	}
	else
	{
		ItemData.InitializeAs(Struct, static_cast<const uint8*>(Data));
	}
	ItemPositionField = PositionFieldName;
	ItemPosition = Position;

	// Written as any change once bound - the triggers see it (Rank Went Down plays Promote); before, applied as it binds.
	if (BoundViewModel.IsValid())
	{
		ApplyItemData();
	}
}

void UWidgetDataBinderComponent::SetItemPosition(FName PositionFieldName, int32 Position)
{
	ItemPositionField = PositionFieldName;
	ItemPosition = Position;
	if (BoundViewModel.IsValid())
	{
		ApplyItemData();
	}
}

void UWidgetDataBinderComponent::ApplyItemData()
{
	UObject* ViewModel = BoundViewModel.Get();
	if (!ViewModel)
	{
		return;
	}
	UClass* ViewModelType = ViewModel->GetClass();

	if (const UScriptStruct* Struct = ItemData.GetScriptStruct())
	{
		const uint8* Memory = ItemData.GetMemory();
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Member = *It;
			const FProperty* Field = FindFProperty<FProperty>(ViewModelType, Member->GetFName());
			if (!Field || !WidgetDataValues::CanConvert(Member, Field))
			{
				continue;
			}
			FWidgetDataScratch Converted(Field);
			if (WidgetDataValues::Convert(Member, Member->ContainerPtrToValuePtr<void>(Memory), Field, Converted.Get()))
			{
				WidgetDataValues::WriteField(ViewModel, Field, Converted.Get());
			}
		}
	}

	if (!ItemPositionField.IsNone() && ItemPosition != INDEX_NONE)
	{
		if (const FProperty* Field = FindFProperty<FProperty>(ViewModelType, ItemPositionField))
		{
			FWidgetDataScratch Number(Field);
			if (WidgetDataValues::WriteNumber(Field, Number.Get(), ItemPosition))
			{
				WidgetDataValues::WriteField(ViewModel, Field, Number.Get());
			}
		}
	}
}

TArray<UUserWidget*> UWidgetDataBinderComponent::GetListEntries(int32 ListIndex) const
{
	TArray<UUserWidget*> Entries;
	if (ListStates.IsValidIndex(ListIndex))
	{
		for (const TWeakObjectPtr<UUserWidget>& Entry : ListStates[ListIndex].Entries)
		{
			if (UUserWidget* Live = Entry.Get())
			{
				Entries.Add(Live);
			}
		}
	}
	return Entries;
}

// =====================================================================================================
// Sample data and the designer's Data Preview
// =====================================================================================================

bool UWidgetDataBinderComponent::WriteFieldFromText(UObject* ViewModel, FName FieldName, const FString& ValueText, bool bWarn)
{
	const FProperty* Field = (ViewModel && !FieldName.IsNone()) ? FindFProperty<FProperty>(ViewModel->GetClass(), FieldName) : nullptr;
	if (!Field)
	{
		return false;
	}
	FWidgetDataScratch Parsed(Field);
	if (!Field->ImportText_Direct(*ValueText, Parsed.Get(), ViewModel, PPF_None))
	{
		if (bWarn)
		{
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: '%s' isn't a valid %s for '%s'."), *DescribeForLog(), *ValueText, *Field->GetCPPType(), *FieldName.ToString());
		}
		return false;
	}
	WidgetDataValues::WriteField(ViewModel, Field, Parsed.Get());
	return true;
}

void UWidgetDataBinderComponent::BuildSampleArray(UObject* ViewModel, const FArrayProperty* ArrayProperty, const TArray<FString>& Rows, void* ArrayValue)
{
	using namespace WidgetDataBinderLists;

	const FProperty* Inner = ArrayProperty->Inner;
	const FStructProperty* StructInner = CastField<FStructProperty>(Inner);
	const FObjectPropertyBase* ObjectInner = CastField<FObjectPropertyBase>(Inner);

	// The key of the list that shows this field, so an object row reuses the object already there with its key - its entry
	// then sees its values change (and animates), rather than being handed a new ViewModel.
	FName KeyField;
	for (const FWidgetListBinding& Binding : ListBindings)
	{
		if (Binding.ViewModelField == ArrayProperty->GetFName())
		{
			KeyField = Binding.KeyField;
			break;
		}
	}
	TMap<FString, UObject*> Existing;
	if (ObjectInner)
	{
		FWidgetDataValue Current;
		if (FWidgetDataValue::Resolve(ViewModel, FWidgetDataPath(ArrayProperty->GetName()), Current) && Current.Property == ArrayProperty)
		{
			FScriptArrayHelper CurrentArray(ArrayProperty, Current.ValuePtr);
			for (int32 Index = 0; Index < CurrentArray.Num(); ++Index)
			{
				if (UObject* Object = ObjectInner->GetObjectPropertyValue(CurrentArray.GetRawPtr(Index)))
				{
					Existing.Add(MakeKey(Inner, CurrentArray.GetRawPtr(Index), Index, KeyField), Object);
				}
			}
		}
	}

	FScriptArrayHelper Array(ArrayProperty, ArrayValue);
	Array.EmptyValues();
	TArray<TPair<FString, FString>> Pairs;
	for (const FString& Row : Rows)
	{
		const int32 Index = Array.AddValue();
		uint8* Element = Array.GetRawPtr(Index);

		if (StructInner)
		{
			FString Text = Row.TrimStartAndEnd();
			if (!Text.StartsWith(TEXT("(")))
			{
				Text = TEXT("(") + Text + TEXT(")");
			}
			if (!StructInner->ImportText_Direct(*Text, Element, ViewModel, PPF_None))
			{
				UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: sample row '%s' isn't a valid %s."), *DescribeForLog(), *Row, *StructInner->Struct->GetName());
			}
			continue;
		}

		UClass* ElementClass = ObjectInner ? ObjectInner->PropertyClass.Get() : nullptr;
		if (!ElementClass || ElementClass->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		ParsePairs(Row, Pairs);
		UObject* Object = nullptr;
		if (!KeyField.IsNone())
		{
			for (const TPair<FString, FString>& Pair : Pairs)
			{
				if (FName(*Pair.Key) == KeyField)
				{
					// Keyed as MakeKey writes it: the value as its property exports it.
					if (const FProperty* KeyProperty = FindFProperty<FProperty>(ElementClass, KeyField))
					{
						FWidgetDataScratch KeyValue(KeyProperty);
						FString KeyText;
						if (KeyProperty->ImportText_Direct(*Pair.Value, KeyValue.Get(), nullptr, PPF_None))
						{
							KeyProperty->ExportTextItem_Direct(KeyText, KeyValue.Get(), nullptr, nullptr, PPF_None);
							Object = Existing.FindRef(KeyText);
						}
					}
					break;
				}
			}
		}
		if (!Object || !Object->IsA(ElementClass))
		{
			Object = NewObject<UObject>(ViewModel, ElementClass, NAME_None, RF_Transient);
		}
		for (const TPair<FString, FString>& Pair : Pairs)
		{
			if (const FProperty* Member = FindFProperty<FProperty>(ElementClass, FName(*Pair.Key)))
			{
				FWidgetDataScratch Parsed(Member);
				if (Member->ImportText_Direct(*Pair.Value, Parsed.Get(), Object, PPF_None))
				{
					// Through WriteField, so an element that is a ViewModel tells its entry.
					WidgetDataValues::WriteField(Object, Member, Parsed.Get());
				}
			}
		}
		ObjectInner->SetObjectPropertyValue(Element, Object);
	}
}

void UWidgetDataBinderComponent::ApplySampleSetValues(UObject* ViewModel, int32 SetIndex)
{
	if (!ViewModel || !SampleSets.IsValidIndex(SetIndex))
	{
		return;
	}
	const FWidgetDataSampleSet& Set = SampleSets[SetIndex];
	for (const FWidgetDataMockValue& Sample : Set.Values)
	{
		WriteFieldFromText(ViewModel, Sample.ViewModelField, Sample.Value, /*bWarn=*/true);
	}
	for (const FWidgetDataSampleList& List : Set.Lists)
	{
		const FArrayProperty* ArrayProperty = List.ViewModelField.IsNone() ? nullptr : FindFProperty<FArrayProperty>(ViewModel->GetClass(), List.ViewModelField);
		if (!ArrayProperty)
		{
			UE_LOG(LogWidgetDataBinder, Warning, TEXT("%s: sample set '%s' lists '%s', which isn't an array field of %s."),
				*DescribeForLog(), *Set.Name.ToString(), *List.ViewModelField.ToString(), *ViewModel->GetClass()->GetName());
			continue;
		}
		FWidgetDataScratch Built(ArrayProperty);
		BuildSampleArray(ViewModel, ArrayProperty, List.Rows, Built.Get());
		WidgetDataValues::WriteField(ViewModel, ArrayProperty, Built.Get());
	}
}

bool UWidgetDataBinderComponent::ApplySampleSet(int32 SetIndex)
{
	UObject* ViewModel = BoundViewModel.Get();
	if (!ViewModel || !SampleSets.IsValidIndex(SetIndex))
	{
		return false;
	}
	// The Mock Values first, so a set only needs what differs - each step then changes just what it says. Not those the set
	// writes itself: a list would otherwise go to the mock rows and back, playing every exit and entrance.
	const TSet<FName> SetFields = GetSampleSetFields(SetIndex);
	ApplyMockValues(ViewModel, &SetFields);
	ApplySampleSetValues(ViewModel, SetIndex);
	return true;
}

TSet<FName> UWidgetDataBinderComponent::GetSampleSetFields(int32 SetIndex) const
{
	TSet<FName> Fields;
	if (SampleSets.IsValidIndex(SetIndex))
	{
		for (const FWidgetDataMockValue& Sample : SampleSets[SetIndex].Values)
		{
			Fields.Add(Sample.ViewModelField);
		}
		for (const FWidgetDataSampleList& List : SampleSets[SetIndex].Lists)
		{
			Fields.Add(List.ViewModelField);
		}
	}
	return Fields;
}

bool UWidgetDataBinderComponent::SetViewModelFieldFromText(FName FieldName, const FString& ValueText)
{
	return WriteFieldFromText(BoundViewModel.Get(), FieldName, ValueText, /*bWarn=*/false);
}

bool UWidgetDataBinderComponent::GetViewModelFieldAsText(FName FieldName, FString& OutValueText) const
{
	UObject* ViewModel = BoundViewModel.Get();
	if (!ViewModel || FieldName.IsNone())
	{
		return false;
	}
	FWidgetDataValue Value;
	if (!FWidgetDataValue::Resolve(ViewModel, FWidgetDataPath(FieldName.ToString()), Value))
	{
		return false;
	}
	OutValueText.Reset();
	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Value.Property))
	{
		const int32 Count = FScriptArrayHelper(ArrayProperty, Value.ValuePtr).Num();
		OutValueText = FString::Printf(TEXT("%d %s"), Count, Count == 1 ? TEXT("row") : TEXT("rows"));
		return true;
	}
	Value.Property->ExportTextItem_Direct(OutValueText, Value.ValuePtr, nullptr, nullptr, PPF_None);
	return true;
}

bool UWidgetDataBinderComponent::EditListForPreview(FName FieldName, EWidgetDataListEdit Edit)
{
	using namespace WidgetDataBinderLists;

	UObject* ViewModel = BoundViewModel.Get();
	const FArrayProperty* ArrayProperty = (ViewModel && !FieldName.IsNone()) ? FindFProperty<FArrayProperty>(ViewModel->GetClass(), FieldName) : nullptr;
	if (!ArrayProperty)
	{
		return false;
	}

	FWidgetDataScratch Edited(ArrayProperty);
	ArrayProperty->CopyCompleteValue(Edited.Get(), ArrayProperty->ContainerPtrToValuePtr<void>(ViewModel));
	FScriptArrayHelper Array(ArrayProperty, Edited.Get());
	const FProperty* Inner = ArrayProperty->Inner;
	const int32 Count = Array.Num();

	switch (Edit)
	{
	case EWidgetDataListEdit::AddRow:
	{
		// A copy of the last row (or a default one), at a random place, with a key of its own.
		const int32 Added = Array.AddValue();
		if (Count > 0)
		{
			if (const FObjectPropertyBase* ObjectInner = CastField<FObjectPropertyBase>(Inner))
			{
				UObject* Last = ObjectInner->GetObjectPropertyValue(Array.GetRawPtr(Count - 1));
				ObjectInner->SetObjectPropertyValue(Array.GetRawPtr(Added), Last ? DuplicateObject<UObject>(Last, ViewModel) : nullptr);
			}
			else
			{
				Inner->CopySingleValue(Array.GetRawPtr(Added), Array.GetRawPtr(Count - 1));
			}
		}
		else if (const FObjectPropertyBase* ObjectInner = CastField<FObjectPropertyBase>(Inner))
		{
			UClass* ElementClass = ObjectInner->PropertyClass.Get();
			if (ElementClass && !ElementClass->HasAnyClassFlags(CLASS_Abstract))
			{
				ObjectInner->SetObjectPropertyValue(Array.GetRawPtr(Added), NewObject<UObject>(ViewModel, ElementClass, NAME_None, RF_Transient));
			}
		}

		FName KeyField;
		for (const FWidgetListBinding& Binding : ListBindings)
		{
			if (Binding.ViewModelField == FieldName)
			{
				KeyField = Binding.KeyField;
				break;
			}
		}
		void* Container = Array.GetRawPtr(Added);
		const UStruct* Scope = nullptr;
		if (const FStructProperty* StructInner = CastField<FStructProperty>(Inner))
		{
			Scope = StructInner->Struct;
		}
		else if (const FObjectPropertyBase* ObjectInner = CastField<FObjectPropertyBase>(Inner))
		{
			UObject* Object = ObjectInner->GetObjectPropertyValue(Container);
			Container = Object;
			Scope = Object ? Object->GetClass() : nullptr;
		}
		const FProperty* KeyProperty = (Scope && Container && !KeyField.IsNone()) ? FindFProperty<FProperty>(Scope, KeyField) : nullptr;
		if (KeyProperty)
		{
			void* KeyValue = KeyProperty->ContainerPtrToValuePtr<void>(Container);
			double Number = 0.0;
			if (WidgetDataValues::ReadNumber(KeyProperty, KeyValue, Number))
			{
				double Highest = Number;
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const void* Other = Array.GetRawPtr(Index);
					if (CastField<FObjectPropertyBase>(Inner))
					{
						Other = CastField<FObjectPropertyBase>(Inner)->GetObjectPropertyValue(Other);
					}
					double OtherNumber = 0.0;
					if (Other && WidgetDataValues::ReadNumber(KeyProperty, KeyProperty->ContainerPtrToValuePtr<void>(Other), OtherNumber))
					{
						Highest = FMath::Max(Highest, OtherNumber);
					}
				}
				WidgetDataValues::WriteNumber(KeyProperty, KeyValue, Highest + 1.0);
			}
			else if (const FStrProperty* StringKey = CastField<FStrProperty>(KeyProperty))
			{
				StringKey->SetPropertyValue(KeyValue, StringKey->GetPropertyValue(KeyValue) + FString::Printf(TEXT(" %d"), Count + 1));
			}
			else if (const FNameProperty* NameKey = CastField<FNameProperty>(KeyProperty))
			{
				NameKey->SetPropertyValue(KeyValue, FName(*FString::Printf(TEXT("%s %d"), *NameKey->GetPropertyValue(KeyValue).ToString(), Count + 1)));
			}
			else if (const FTextProperty* TextKey = CastField<FTextProperty>(KeyProperty))
			{
				TextKey->SetPropertyValue(KeyValue, FText::FromString(FString::Printf(TEXT("%s %d"), *TextKey->GetPropertyValue(KeyValue).ToString(), Count + 1)));
			}
		}

		// Somewhere in the list rather than always last, where an arrival is easiest to miss.
		const int32 Place = FMath::RandRange(0, Added);
		for (int32 Index = Added; Index > Place; --Index)
		{
			Array.SwapValues(Index, Index - 1);
		}
		break;
	}

	case EWidgetDataListEdit::RemoveRow:
		if (Count == 0)
		{
			return false;
		}
		Array.RemoveValues(FMath::RandRange(0, Count - 1), 1);
		break;

	case EWidgetDataListEdit::Shuffle:
		for (int32 Index = Count - 1; Index > 0; --Index)
		{
			Array.SwapValues(Index, FMath::RandRange(0, Index));
		}
		break;

	case EWidgetDataListEdit::Reverse:
		for (int32 Index = 0; Index < Count / 2; ++Index)
		{
			Array.SwapValues(Index, Count - 1 - Index);
		}
		break;

	case EWidgetDataListEdit::MoveOneUp:
	{
		// One row climbs one to three places - an overtake, the commonest leaderboard change.
		if (Count < 2)
		{
			return false;
		}
		const int32 From = FMath::RandRange(1, Count - 1);
		const int32 To = FMath::Max(0, From - FMath::RandRange(1, 3));
		for (int32 Index = From; Index > To; --Index)
		{
			Array.SwapValues(Index, Index - 1);
		}
		break;
	}
	}

	WidgetDataValues::WriteField(ViewModel, ArrayProperty, Edited.Get());
	return true;
}

void UWidgetDataBinderComponent::SetPreviewSampleSet(int32 SetIndex)
{
	PreviewSampleSet = SampleSets.IsValidIndex(SetIndex) ? SetIndex : INDEX_NONE;
	UObject* ViewModel = BoundViewModel.Get();
	if (!bDesignBound || !ViewModel)
	{
		return;
	}
	const TSet<FName> SetFields = GetSampleSetFields(PreviewSampleSet);
	ApplyMockValues(ViewModel, &SetFields);
	ApplySampleSetValues(ViewModel, PreviewSampleSet);
}

void UWidgetDataBinderComponent::BindForDesigner()
{
	UWidget* OwnerWidget = BinderOwner.Get();
	if (!OwnerWidget || !OwnerWidget->IsDesignTime())
	{
		return;
	}

	if (!bRunTriggersInDesigner)
	{
		// As before: the Mock Values (or this entry's element) written, nothing run.
		UnbindAll();
		if (ItemData.IsValid())
		{
			UObject* ViewModel = FindViewModelInView();
			if (!ViewModel)
			{
				ViewModel = CreateViewModel();
			}
			if (ViewModel)
			{
				TGuardValue<bool> SuppressGuard(bSuppressTriggers, true);
				AttachViewModel(ViewModel);
				ApplyItemData();
				DetachViewModel();
			}
		}
		else
		{
			ApplyDesignTimeMock();
		}
		return;
	}

	UObject* ViewModel = ExplicitViewModel.Get();
	if (!ViewModel)
	{
		ViewModel = FindViewModelInView();
	}
	if (!ViewModel && ViewModelClass)
	{
		ViewModel = CreateViewModel();
	}
	if (!ViewModel)
	{
		return;
	}

	UnbindAll();
	AttachViewModel(ViewModel);
	bDesignBound = true;

	// An entry of a list of objects: its element is its source, read as at runtime.
	if (DataSource.IsValid())
	{
		BindSource();
		return;
	}

	// Settled without acting, as at runtime: then the one evaluation on bind, and the lists shown as they are.
	{
		TGuardValue<bool> SuppressGuard(bSuppressTriggers, true);
		if (ItemData.IsValid() || ExplicitViewModel.IsValid())
		{
			// An entry of a list: its element is its data.
			ApplyItemData();
		}
		else
		{
			if (bUseMockDataInDesigner)
			{
				const TSet<FName> SetFields = GetSampleSetFields(PreviewSampleSet);
				ApplyMockValues(ViewModel, &SetFields);
			}
			ApplySampleSetValues(ViewModel, PreviewSampleSet);
		}
	}
	EvaluateAllTriggers(/*bInitial=*/true);
	ReconcileAllLists(/*bInitial=*/true);
	if (bAnyCounting)
	{
		EnsureTicker();
	}
}

void UWidgetDataBinderComponent::KeepDesignerAwake(float Seconds)
{
	UWidget* OwnerWidget = BinderOwner.Get();
	if (!bDesignBound || !OwnerWidget)
	{
		return;
	}

	const double Now = FApp::GetCurrentTime();
	const bool bRunning = DesignerAwakeUntil > Now;
	DesignerAwakeUntil = FMath::Max(DesignerAwakeUntil, Now + Seconds);
	if (bRunning)
	{
		return;
	}

	// An active timer keeps the designer's viewport drawing every frame while what the core ticker plays runs.
	const TSharedPtr<SWidget> Slate = OwnerWidget->GetCachedWidget();
	if (!Slate.IsValid())
	{
		return;
	}
	Slate->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateWeakLambda(this, [this](double, float)
	{
		return FApp::GetCurrentTime() < DesignerAwakeUntil ? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
	}));
}

#if WITH_EDITOR
TArray<UWidgetDataBinderComponent*> UWidgetDataBinderComponent::FindDesignerPreviews() const
{
	TArray<UWidgetDataBinderComponent*> Previews;
	if (const UWidget* OwnerWidget = BinderOwner.Get())
	{
		if (OwnerWidget->IsDesignTime())
		{
			Previews.Add(const_cast<UWidgetDataBinderComponent*>(this));
		}
		return Previews;
	}

	// The template the Details edit: its designer copies are the binders made from it whose widget is being designed.
	for (TObjectIterator<UWidgetDataBinderComponent> It(RF_ClassDefaultObject | RF_ArchetypeObject, /*bIncludeDerivedClasses=*/true, EInternalObjectFlags::Garbage); It; ++It)
	{
		UWidgetDataBinderComponent* Copy = *It;
		const UWidget* CopyWidget = Copy != this ? Copy->BinderOwner.Get() : nullptr;
		if (!CopyWidget || !CopyWidget->IsDesignTime())
		{
			continue;
		}
		for (const UObject* Archetype = Copy->GetArchetype(); Archetype; Archetype = Archetype->GetArchetype())
		{
			if (Archetype == this)
			{
				Previews.Add(Copy);
				break;
			}
			if (Archetype->HasAnyFlags(RF_ClassDefaultObject))
			{
				break;
			}
		}
	}
	return Previews;
}

void UWidgetDataBinderComponent::RefreshDesignerPreviewFrom(const UWidgetDataBinderComponent* Template)
{
	if (Template && Template != this)
	{
		for (TFieldIterator<FProperty> It(UWidgetDataBinderComponent::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_EditConst))
			{
				Property->CopyCompleteValue_InContainer(this, Template);
			}
		}
	}
	BindForDesigner();
}
#endif

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

	if (bAnyCounting)
	{
		bAnyCounting = TickCounts(DeltaTime);
	}

	const bool bKeepTicking = bAnyPolling || bVerifyViewModelNextTick || bAnyCounting;
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

TArray<FString> UWidgetDataBinderComponent::GetListFieldOptions() const
{
	TArray<FString> Options;
	Options.Add(TEXT("None"));
	if (const UClass* ViewModelType = ViewModelClass.Get())
	{
		for (TFieldIterator<FArrayProperty> It(ViewModelType); It; ++It)
		{
			const FProperty* Inner = It->Inner;
			if (CastField<FStructProperty>(Inner) || CastField<FObjectPropertyBase>(Inner))
			{
				Options.AddUnique(It->GetName());
			}
		}
	}
	return Options;
}

TArray<FString> UWidgetDataBinderComponent::GetListKeyFieldOptions() const
{
	TArray<FString> Options;
	Options.Add(TEXT("None"));
	if (const UClass* ViewModelType = ViewModelClass.Get())
	{
		for (TFieldIterator<FArrayProperty> It(ViewModelType); It; ++It)
		{
			const UStruct* Element = nullptr;
			if (const FStructProperty* StructInner = CastField<FStructProperty>(It->Inner))
			{
				Element = StructInner->Struct;
			}
			else if (const FObjectPropertyBase* ObjectInner = CastField<FObjectPropertyBase>(It->Inner))
			{
				Element = ObjectInner->PropertyClass;
			}
			if (!Element)
			{
				continue;
			}
			for (TFieldIterator<FProperty> MemberIt(Element); MemberIt; ++MemberIt)
			{
				const FProperty* Member = *MemberIt;
				const bool bKeyLike = WidgetDataValues::IsNumeric(Member) || CastField<FStrProperty>(Member) || CastField<FNameProperty>(Member)
					|| CastField<FTextProperty>(Member) || CastField<FStructProperty>(Member);
				if (bKeyLike && (Member->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_Edit) || CastField<FStructProperty>(It->Inner)))
				{
					Options.AddUnique(Member->GetName());
				}
			}
		}
	}
	return Options;
}

FString UWidgetDataBinderComponent::DescribeForLog() const
{
	const UWidget* OwnerWidget = BinderOwner.Get();
	const UUserWidget* Containing = OwnerWidget ? OwnerWidget->GetTypedOuter<UUserWidget>() : nullptr;
	return FString::Printf(TEXT("Widget Data Binder on %s in %s"), *GetNameSafe(OwnerWidget), *GetNameSafe(Containing ? Containing->GetClass() : nullptr));
}