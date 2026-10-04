// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimStateMachineLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "WidgetAnimationComponent.h"
#include "WidgetAnimStateMachine.h"

namespace WidgetAnimStateMachineSearch
{
	/** Widget's component and every one under it, in tree order, until Visit returns false. False if it was stopped. */
	static bool ForEachComponentOnAndUnder(UWidget* Widget, TFunctionRef<bool(UWidgetAnimationComponent*)> Visit, int32 Depth = 0)
	{
		static constexpr int32 MaxDepth = 128;
		if (!Widget || Depth > MaxDepth)
		{
			return true;
		}

		if (UWidgetAnimationComponent* Component = UWidgetAnimationComponent::FindWidgetAnimationComponent(Widget))
		{
			if (!Visit(Component))
			{
				return false;
			}
		}

		if (const UUserWidget* AsUserWidget = Cast<UUserWidget>(Widget))
		{
			return !AsUserWidget->WidgetTree || ForEachComponentOnAndUnder(AsUserWidget->WidgetTree->RootWidget, Visit, Depth + 1);
		}

		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (!ForEachComponentOnAndUnder(Panel->GetChildAt(Index), Visit, Depth + 1))
				{
					return false;
				}
			}
		}
		return true;
	}

	static bool Matches(const UWidgetAnimationComponent* Component, const UWidgetAnimStateMachineAsset* StateMachine, FName Input)
	{
		if (!Component)
		{
			return false;
		}
		if (StateMachine && !Component->PlaysStateMachine(StateMachine))
		{
			return false;
		}
		if (!StateMachine && Component->StateMachines.Num() == 0)
		{
			return false;
		}
		return Input.IsNone() || Component->HasStateMachineInput(StateMachine, Input);
	}
}

void UWidgetAnimStateMachineLibrary::FindComponents(UObject* Target, const UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bSearchParentWidgets, bool bAll, TArray<UWidgetAnimationComponent*>& OutComponents)
{
	OutComponents.Reset();

	if (UWidgetAnimationComponent* Direct = Cast<UWidgetAnimationComponent>(Target))
	{
		OutComponents.Add(Direct);
		return;
	}

	UWidget* Widget = Cast<UWidget>(Target);
	if (!Widget)
	{
		return;
	}

	// On the target and inside it: a call aimed at a screen reaches the buttons in it.
	WidgetAnimStateMachineSearch::ForEachComponentOnAndUnder(Widget, [&OutComponents, StateMachine, Input, bAll](UWidgetAnimationComponent* Component)
	{
		if (WidgetAnimStateMachineSearch::Matches(Component, StateMachine, Input))
		{
			OutComponents.AddUnique(Component);
			return bAll;
		}
		return true;
	});

	if (OutComponents.Num() > 0 || !bSearchParentWidgets)
	{
		return;
	}

	// Outwards: a button's own graph aimed at a widget inside it reaches the button's machine.
	static constexpr int32 MaxSearchDepth = 64;
	UObject* Search = Widget->GetOuter();
	for (int32 Depth = 0; Search && Depth < MaxSearchDepth; ++Depth, Search = Search->GetOuter())
	{
		UWidgetAnimationComponent* Component = Cast<UWidget>(Search) ? UWidgetAnimationComponent::FindWidgetAnimationComponent(Cast<UWidget>(Search)) : nullptr;
		if (WidgetAnimStateMachineSearch::Matches(Component, StateMachine, Input))
		{
			OutComponents.Add(Component);
			return;
		}
	}
}

bool UWidgetAnimStateMachineLibrary::SetStateMachineBool(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bValue, bool bSearchParentWidgets, bool bSetAllMatches)
{
	TArray<UWidgetAnimationComponent*> Components;
	FindComponents(Target, StateMachine, Input, bSearchParentWidgets, bSetAllMatches, Components);
	bool bTaken = false;
	for (UWidgetAnimationComponent* Component : Components)
	{
		bTaken |= Component->SetStateMachineBool(Input, bValue, StateMachine);
	}
	return bTaken;
}

bool UWidgetAnimStateMachineLibrary::SetStateMachineNumber(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, double Value, bool bSearchParentWidgets, bool bSetAllMatches)
{
	TArray<UWidgetAnimationComponent*> Components;
	FindComponents(Target, StateMachine, Input, bSearchParentWidgets, bSetAllMatches, Components);
	bool bTaken = false;
	for (UWidgetAnimationComponent* Component : Components)
	{
		bTaken |= Component->SetStateMachineNumber(Input, static_cast<float>(Value), StateMachine);
	}
	return bTaken;
}

bool UWidgetAnimStateMachineLibrary::FireStateMachineTrigger(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bSearchParentWidgets, bool bSetAllMatches)
{
	TArray<UWidgetAnimationComponent*> Components;
	FindComponents(Target, StateMachine, Input, bSearchParentWidgets, bSetAllMatches, Components);
	bool bTaken = false;
	for (UWidgetAnimationComponent* Component : Components)
	{
		bTaken |= Component->FireStateMachineTrigger(Input, StateMachine);
	}
	return bTaken;
}

bool UWidgetAnimStateMachineLibrary::GetStateMachineBool(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bSearchParentWidgets)
{
	TArray<UWidgetAnimationComponent*> Components;
	FindComponents(Target, StateMachine, Input, bSearchParentWidgets, /*bAll=*/false, Components);
	return Components.Num() > 0 && Components[0]->GetStateMachineBool(Input, StateMachine);
}

float UWidgetAnimStateMachineLibrary::GetStateMachineNumber(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Input, bool bSearchParentWidgets)
{
	TArray<UWidgetAnimationComponent*> Components;
	FindComponents(Target, StateMachine, Input, bSearchParentWidgets, /*bAll=*/false, Components);
	return Components.Num() > 0 ? Components[0]->GetStateMachineNumber(Input, StateMachine) : 0.f;
}

bool UWidgetAnimStateMachineLibrary::IsWidgetInState(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName State, FName Layer, bool bSearchParentWidgets)
{
	TArray<UWidgetAnimationComponent*> Components;
	FindComponents(Target, StateMachine, NAME_None, bSearchParentWidgets, /*bAll=*/false, Components);
	return Components.Num() > 0 && Components[0]->IsInState(State, StateMachine, Layer);
}

FName UWidgetAnimStateMachineLibrary::GetCurrentStateOfLayer(UObject* Target, UWidgetAnimStateMachineAsset* StateMachine, FName Layer, bool bSearchParentWidgets)
{
	TArray<UWidgetAnimationComponent*> Components;
	FindComponents(Target, StateMachine, NAME_None, bSearchParentWidgets, /*bAll=*/false, Components);
	return Components.Num() > 0 ? Components[0]->GetCurrentStateOfLayer(StateMachine, Layer) : FName();
}
