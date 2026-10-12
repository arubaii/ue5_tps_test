// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimationStateMachineComponent.h"

#include "Components/Widget.h"

TMap<TObjectKey<UWidget>, TWeakObjectPtr<UWidgetAnimationStateMachineComponent>> UWidgetAnimationStateMachineComponent::ComponentsByWidget;

// =====================================================================================================
// UUIComponent
// =====================================================================================================

#if UE_VERSION_OLDER_THAN(5, 6, 0)
bool UWidgetAnimationStateMachineComponent::OnInitialize()
{
	SetMachineOwner(GetOwner().Get());
	return true;
}
#else
void UWidgetAnimationStateMachineComponent::OnInitialize()
{
	SetMachineOwner(GetOwner().Get());
}

void UWidgetAnimationStateMachineComponent::OnPreConstruct(bool bIsDesignTime)
{
	// Some owners are initialized before the component system hands us the widget; pick it up here too.
	if (!MachineOwner.IsValid())
	{
		SetMachineOwner(GetOwner().Get());
	}

	// Every component on the widget is initialized by now: a Widget Animation Component beside this one plays the machines,
	// and is told of the designer itself. Only one made here is told from here.
	UWidgetAnimationComponent* Player = EnsurePlayer();
	if (bIsDesignTime && Player && Player == OwnPlayer)
	{
		Player->NotifyOwnerDesignTimeRefresh();
	}
}
#endif

void UWidgetAnimationStateMachineComponent::OnConstruct()
{
	if (!MachineOwner.IsValid())
	{
		SetMachineOwner(GetOwner().Get());
	}

	UWidgetAnimationComponent* Player = EnsurePlayer();
	if (Player && Player == OwnPlayer)
	{
		Player->NotifyOwnerConstructed();
	}
}

void UWidgetAnimationStateMachineComponent::OnDestruct()
{
	if (OwnPlayer)
	{
		OwnPlayer->NotifyOwnerDestructed();
	}
}

void UWidgetAnimationStateMachineComponent::BeginDestroy()
{
	Unregister();
	Super::BeginDestroy();
}

// =====================================================================================================
// Owner and lookup
// =====================================================================================================

void UWidgetAnimationStateMachineComponent::SetMachineOwner(UWidget* InWidget)
{
	if (MachineOwner.Get() == InWidget)
	{
		return;
	}

	Unregister();
	MachineOwner = InWidget;

	// Templates (class defaults, Widget Blueprint archetypes) never play anything, and registering them would let a lookup on
	// a live widget land on a template that happens to share its key.
	if (InWidget && !InWidget->IsTemplate() && !IsTemplate())
	{
		ComponentsByWidget.Add(TObjectKey<UWidget>(InWidget), this);
		EnsurePlayer();
	}
}

void UWidgetAnimationStateMachineComponent::Unregister()
{
	if (UWidget* Previous = MachineOwner.Get())
	{
		const TObjectKey<UWidget> Key(Previous);
		if (const TWeakObjectPtr<UWidgetAnimationStateMachineComponent>* Existing = ComponentsByWidget.Find(Key))
		{
			if (Existing->Get() == this || !Existing->IsValid())
			{
				ComponentsByWidget.Remove(Key);
			}
		}
	}
}

UWidgetAnimationStateMachineComponent* UWidgetAnimationStateMachineComponent::FindWidgetAnimationStateMachineComponent(const UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}
	const TWeakObjectPtr<UWidgetAnimationStateMachineComponent>* Found = ComponentsByWidget.Find(TObjectKey<UWidget>(Widget));
	return Found ? Found->Get() : nullptr;
}

UWidgetAnimationComponent* UWidgetAnimationStateMachineComponent::GetPlayer() const
{
	return UWidgetAnimationComponent::FindWidgetAnimationComponent(MachineOwner.Get());
}

UWidgetAnimationComponent* UWidgetAnimationStateMachineComponent::EnsurePlayer()
{
	UWidget* Widget = MachineOwner.Get();
	if (!Widget || Widget->IsTemplate() || IsTemplate())
	{
		return nullptr;
	}

	// The widget's own Widget Animation Component plays the machines, with its own animations. One made here before it turned
	// up - it is told after this one, at times - is let go: it had nothing to play yet but these, and the widget's plays them.
	UWidgetAnimationComponent* Found = UWidgetAnimationComponent::FindWidgetAnimationComponent(Widget);
	if (Found && Found != OwnPlayer)
	{
		if (OwnPlayer)
		{
			OwnPlayer->NotifyOwnerDestructed();
			OwnPlayer = nullptr;
		}
		return Found;
	}

	// None: one of its own, bound to the widget as a component added in the designer is - what every lookup on the widget finds.
	if (!OwnPlayer)
	{
		OwnPlayer = NewObject<UWidgetAnimationComponent>(this, NAME_None, RF_Transient);
		OwnPlayer->BindAsMachinePlayer(Widget, bPreserveAuthoredTransforms);
	}
	return OwnPlayer;
}

bool UWidgetAnimationStateMachineComponent::HasStateMachine(const UWidgetAnimStateMachineAsset* StateMachine) const
{
	return StateMachine && StateMachines.ContainsByPredicate([StateMachine](const FWidgetAnimStateMachineEntry& Row) { return Row.StateMachine == StateMachine; });
}

void UWidgetAnimationStateMachineComponent::GetMachinesFor(UWidgetAnimStateMachineAsset* StateMachine, TArray<UWidgetAnimStateMachineAsset*>& OutMachines) const
{
	OutMachines.Reset();
	if (StateMachine)
	{
		OutMachines.Add(StateMachine);
		return;
	}
	for (const FWidgetAnimStateMachineEntry& Row : StateMachines)
	{
		if (Row.StateMachine)
		{
			OutMachines.AddUnique(Row.StateMachine);
		}
	}
}

TArray<FString> UWidgetAnimationStateMachineComponent::GetAnimTargetWidgetOptions() const
{
	return UWidgetAnimationComponent::GatherTargetWidgetOptions(this, MachineOwner.Get(), /*bOwnTree=*/false);
}

bool UWidgetAnimationStateMachineComponent::SyncStateMachines()
{
	if (StateMachines.Num() == 0)
	{
		return false;
	}

	// New rows are checked against what this widget's tree has - see FWidgetAnimEntry::SyncGroupTargets.
	const TArray<FString> TargetOptions = GetAnimTargetWidgetOptions();
	bool bChanged = false;
	for (FWidgetAnimStateMachineEntry& Row : StateMachines)
	{
		bChanged |= Row.SyncToMachine(&TargetOptions);
	}
	return bChanged;
}

void UWidgetAnimationStateMachineComponent::PostLoad()
{
	Super::PostLoad();
	SyncStateMachines();
}

#if WITH_EDITOR
void UWidgetAnimationStateMachineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// A machine picked or swapped: its Animation Target Widgets and Inputs get a row each. Only once the edit is committed -
	// resizing an array under a drag in progress would pull the row out from under it.
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		SyncStateMachines();
	}
}
#endif

// =====================================================================================================
// Inputs and states - through what plays the machines, for this component's own
// =====================================================================================================

bool UWidgetAnimationStateMachineComponent::SetStateMachineBool(FName Input, bool bValue, UWidgetAnimStateMachineAsset* StateMachine)
{
	UWidgetAnimationComponent* Player = EnsurePlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	bool bTaken = false;
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		bTaken |= Player && Player->SetStateMachineBool(Input, bValue, Machine);
	}
	return bTaken;
}

bool UWidgetAnimationStateMachineComponent::SetStateMachineNumber(FName Input, float Value, UWidgetAnimStateMachineAsset* StateMachine)
{
	UWidgetAnimationComponent* Player = EnsurePlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	bool bTaken = false;
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		bTaken |= Player && Player->SetStateMachineNumber(Input, Value, Machine);
	}
	return bTaken;
}

bool UWidgetAnimationStateMachineComponent::SetStateMachineInteger(FName Input, int32 Value, UWidgetAnimStateMachineAsset* StateMachine)
{
	UWidgetAnimationComponent* Player = EnsurePlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	bool bTaken = false;
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		bTaken |= Player && Player->SetStateMachineInteger(Input, Value, Machine);
	}
	return bTaken;
}

bool UWidgetAnimationStateMachineComponent::SetStateMachineChoice(FName Input, FName Option, UWidgetAnimStateMachineAsset* StateMachine)
{
	UWidgetAnimationComponent* Player = EnsurePlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	bool bTaken = false;
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		bTaken |= Player && Player->SetStateMachineChoice(Input, Option, Machine);
	}
	return bTaken;
}

bool UWidgetAnimationStateMachineComponent::SetStateMachineVector2D(FName Input, FVector2D Value, UWidgetAnimStateMachineAsset* StateMachine)
{
	UWidgetAnimationComponent* Player = EnsurePlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	bool bTaken = false;
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		bTaken |= Player && Player->SetStateMachineVector2D(Input, Value, Machine);
	}
	return bTaken;
}

bool UWidgetAnimationStateMachineComponent::FireStateMachineTrigger(FName Input, UWidgetAnimStateMachineAsset* StateMachine)
{
	UWidgetAnimationComponent* Player = EnsurePlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	bool bTaken = false;
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		bTaken |= Player && Player->FireStateMachineTrigger(Input, Machine);
	}
	return bTaken;
}

bool UWidgetAnimationStateMachineComponent::GetStateMachineBool(FName Input, UWidgetAnimStateMachineAsset* StateMachine) const
{
	const UWidgetAnimationComponent* Player = GetPlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		if (Player && Player->HasStateMachineInput(Machine, Input))
		{
			return Player->GetStateMachineBool(Input, Machine);
		}
	}
	return false;
}

FName UWidgetAnimationStateMachineComponent::GetStateMachineChoice(FName Input, UWidgetAnimStateMachineAsset* StateMachine) const
{
	const UWidgetAnimationComponent* Player = GetPlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		if (Player && Player->HasStateMachineInput(Machine, Input))
		{
			return Player->GetStateMachineChoice(Input, Machine);
		}
	}
	return NAME_None;
}

FVector2D UWidgetAnimationStateMachineComponent::GetStateMachineVector2D(FName Input, UWidgetAnimStateMachineAsset* StateMachine) const
{
	const UWidgetAnimationComponent* Player = GetPlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		if (Player && Player->HasStateMachineInput(Machine, Input))
		{
			return Player->GetStateMachineVector2D(Input, Machine);
		}
	}
	return FVector2D::ZeroVector;
}

float UWidgetAnimationStateMachineComponent::GetStateMachineNumber(FName Input, UWidgetAnimStateMachineAsset* StateMachine) const
{
	const UWidgetAnimationComponent* Player = GetPlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		if (Player && Player->HasStateMachineInput(Machine, Input))
		{
			return Player->GetStateMachineNumber(Input, Machine);
		}
	}
	return 0.f;
}

bool UWidgetAnimationStateMachineComponent::IsInState(FName State, UWidgetAnimStateMachineAsset* StateMachine, FName Layer) const
{
	const UWidgetAnimationComponent* Player = GetPlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	return Player && Machines.ContainsByPredicate([Player, State, Layer](UWidgetAnimStateMachineAsset* Machine) { return Player->IsInState(State, Machine, Layer); });
}

FName UWidgetAnimationStateMachineComponent::GetCurrentStateOfLayer(UWidgetAnimStateMachineAsset* StateMachine, FName Layer) const
{
	const UWidgetAnimationComponent* Player = GetPlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		const FName Current = Player ? Player->GetCurrentStateOfLayer(Machine, Layer) : FName();
		if (!Current.IsNone())
		{
			return Current;
		}
	}
	return NAME_None;
}

TArray<float> UWidgetAnimationStateMachineComponent::GetStateMachineBlendWeights(UWidgetAnimStateMachineAsset* StateMachine, FName Layer) const
{
	const UWidgetAnimationComponent* Player = GetPlayer();
	TArray<UWidgetAnimStateMachineAsset*> Machines;
	GetMachinesFor(StateMachine, Machines);
	for (UWidgetAnimStateMachineAsset* Machine : Machines)
	{
		TArray<float> Weights = Player ? Player->GetStateMachineBlendWeights(Machine, Layer) : TArray<float>();
		if (Weights.Num() > 0)
		{
			return Weights;
		}
	}
	return TArray<float>();
}

void UWidgetAnimationStateMachineComponent::StartStateMachines()
{
	if (UWidgetAnimationComponent* Player = EnsurePlayer())
	{
		Player->StartStateMachines();
	}
}

void UWidgetAnimationStateMachineComponent::StopStateMachines()
{
	if (UWidgetAnimationComponent* Player = EnsurePlayer())
	{
		Player->StopStateMachines();
	}
}
