// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimEvaluator.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"

#define LOCTEXT_NAMESPACE "WidgetAnimEvaluator"

// =====================================================================================================
// FWidgetAnimEvaluator
// =====================================================================================================

FText FWidgetAnimEvaluator::GetValueLabel() const
{
	return LOCTEXT("DefaultValueLabel", "Value");
}

FText FWidgetAnimEvaluator::GetKeyColorLabel(const FInstancedStruct& Params) const
{
	return LOCTEXT("DefaultKeyColorLabel", "Color");
}

UClass* FWidgetAnimEvaluator::GetTargetClass() const
{
	return UWidget::StaticClass();
}

UObject* FWidgetAnimEvaluator::ResolveTarget(UWidget* Widget, const FInstancedStruct& Params) const
{
	return FindFirstTarget(Widget, GetTrackTargetClass(Params));
}

UObject* FWidgetAnimEvaluator::FindFirstTarget(UWidget* Widget, const UClass* TargetClass)
{
	if (!Widget || !TargetClass)
	{
		return nullptr;
	}

	if (Widget->IsA(TargetClass))
	{
		return Widget;
	}

	// Into a User Widget's own tree, so a group aimed at a nested card still finds the label inside it.
	if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
	{
		return UserWidget->WidgetTree ? FindFirstTarget(UserWidget->WidgetTree->RootWidget, TargetClass) : nullptr;
	}

	if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
		{
			if (UObject* Found = FindFirstTarget(Panel->GetChildAt(ChildIndex), TargetClass))
			{
				return Found;
			}
		}
	}

	return nullptr;
}

// =====================================================================================================
// FWidgetAnimEvaluatorRegistry
// =====================================================================================================

FWidgetAnimEvaluatorRegistry& FWidgetAnimEvaluatorRegistry::Get()
{
	static FWidgetAnimEvaluatorRegistry Registry;
	return Registry;
}

void FWidgetAnimEvaluatorRegistry::Register(const TSharedRef<FWidgetAnimEvaluator>& Evaluator)
{
	const UScriptStruct* ParamsStruct = Evaluator->GetParamsStruct();
	if (!ensureMsgf(ParamsStruct, TEXT("A widget animation evaluator was registered without a settings struct.")))
	{
		return;
	}

	// The whole point of the base struct is that every type's settings can be told apart from anything else an
	// instanced struct might hold; one that doesn't derive from it would never show up in the type picker anyway.
	ensureMsgf(ParamsStruct->IsChildOf(FWidgetAnimTypeParams::StaticStruct()),
		TEXT("%s is registered as a widget animation type but does not derive from FWidgetAnimTypeParams."), *ParamsStruct->GetName());

	Evaluators.Add(ParamsStruct, Evaluator);
	ChangedDelegate.Broadcast();
}

void FWidgetAnimEvaluatorRegistry::Unregister(const UScriptStruct* ParamsStruct)
{
	if (Evaluators.Remove(ParamsStruct) > 0)
	{
		ChangedDelegate.Broadcast();
	}
}

const FWidgetAnimEvaluator* FWidgetAnimEvaluatorRegistry::Find(const UScriptStruct* ParamsStruct) const
{
	// Up the struct's line until something is registered. A handful of pointer compares on a small map - cheap
	// enough to do per track per frame rather than cache a pointer that a hot reload could leave dangling.
	for (const UStruct* Struct = ParamsStruct; Struct; Struct = Struct->GetSuperStruct())
	{
		if (const TSharedRef<FWidgetAnimEvaluator>* Found = Evaluators.Find(static_cast<const UScriptStruct*>(Struct)))
		{
			return &Found->Get();
		}
	}

	return nullptr;
}

void FWidgetAnimEvaluatorRegistry::GetAll(TArray<const FWidgetAnimEvaluator*>& OutEvaluators) const
{
	OutEvaluators.Reset(Evaluators.Num());
	for (const TPair<const UScriptStruct*, TSharedRef<FWidgetAnimEvaluator>>& Pair : Evaluators)
	{
		OutEvaluators.Add(&Pair.Value.Get());
	}

	OutEvaluators.Sort([](const FWidgetAnimEvaluator& A, const FWidgetAnimEvaluator& B)
	{
		return A.GetDisplayName().CompareTo(B.GetDisplayName()) < 0;
	});
}

// =====================================================================================================
// Helpers
// =====================================================================================================

double WidgetAnimGetRegisteredEndpoint(const FInstancedStruct& Params, bool bStart)
{
	const FWidgetAnimEvaluator* Evaluator = FWidgetAnimEvaluatorRegistry::Get().Find(Params);
	return Evaluator ? Evaluator->GetEndpointValue(Params, bStart) : 0.0;
}

FLinearColor WidgetAnimGetRegisteredEndpointColor(const FInstancedStruct& Params, bool bStart)
{
	const FWidgetAnimEvaluator* Evaluator = FWidgetAnimEvaluatorRegistry::Get().Find(Params);
	return Evaluator && Evaluator->GetKeyContent(Params) != EWidgetAnimKeyContent::Value ? Evaluator->GetEndpointColor(Params, bStart) : FLinearColor::White;
}

EWidgetAnimKeyContent WidgetAnimGetRegisteredKeyContent(const FInstancedStruct& Params)
{
	const FWidgetAnimEvaluator* Evaluator = FWidgetAnimEvaluatorRegistry::Get().Find(Params);
	return Evaluator ? Evaluator->GetKeyContent(Params) : EWidgetAnimKeyContent::Value;
}

bool WidgetAnimIsRegisteredCue(const FInstancedStruct& Params)
{
	const FWidgetAnimEvaluator* Evaluator = Params.IsValid() ? FWidgetAnimEvaluatorRegistry::Get().Find(Params) : nullptr;
	return Evaluator && Evaluator->IsCue();
}

#undef LOCTEXT_NAMESPACE