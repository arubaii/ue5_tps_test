// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimCompileChecks.h"

#include "BlueprintCompilationManager.h"
#include "KismetCompiler.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"
#include "WidgetAnimTimelines.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationStateMachineComponent.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"

#define LOCTEXT_NAMESPACE "WidgetAnimCompileChecks"

namespace WidgetAnimCompileChecksPrivate
{
	/** The name a compile line gives Machine: the Widget Blueprint's own machine by its State Machine editor name, an asset by its own. */
	FText DescribeMachine(const UWidgetAnimStateMachineAsset* Machine)
	{
		if (const UWidgetAnimTimelinesExtension* Extension = Cast<UWidgetAnimTimelinesExtension>(Machine->GetOuter()))
		{
			if (!Extension->StateMachineName.IsEmpty())
			{
				return FText::Format(LOCTEXT("OwnMachineNamed", "State Machine '{0}'"), FText::FromString(Extension->StateMachineName));
			}
			return LOCTEXT("OwnMachine", "This Widget Blueprint's State Machine");
		}
		return FText::FromString(Machine->GetName());
	}

	/** The name a compile line gives Animation: a timeline by its name, an asset by its own. */
	FText DescribeAnimation(const UWidgetAnimPresetAsset* Animation)
	{
		if (const UWidgetAnimTimelinesExtension* Extension = Cast<UWidgetAnimTimelinesExtension>(Animation->GetOuter()))
		{
			const int32 Index = Extension->IndexOf(Animation);
			if (Extension->Timelines.IsValidIndex(Index))
			{
				return FText::Format(LOCTEXT("Timeline", "Timeline '{0}'"), FText::FromString(Extension->Timelines[Index].Name));
			}
		}
		return FText::FromString(Animation->GetName());
	}
}

void WidgetAnimCompileChecks::Gather(const UBlueprint* Blueprint, TArray<FText>& OutProblems)
{
	using namespace WidgetAnimCompileChecksPrivate;
	if (!Blueprint)
	{
		return;
	}

	TSet<const UObject*> Checked;
	auto CheckAnimation = [&Checked, &OutProblems](const UWidgetAnimPresetAsset* Animation)
	{
		bool bAlreadyChecked = false;
		Checked.Add(Animation, &bAlreadyChecked);
		if (Animation && !bAlreadyChecked)
		{
			Animation->GatherParameterProblems(OutProblems, DescribeAnimation(Animation));
		}
	};
	auto CheckMachine = [&Checked, &OutProblems, &CheckAnimation](const UWidgetAnimStateMachineAsset* Machine)
	{
		bool bAlreadyChecked = false;
		Checked.Add(Machine, &bAlreadyChecked);
		if (!Machine || bAlreadyChecked)
		{
			return;
		}
		Machine->GatherProblems(OutProblems, DescribeMachine(Machine));
		TArray<UWidgetAnimPresetAsset*> Animations;
		Machine->GatherAnimations(Animations);
		for (const UWidgetAnimPresetAsset* Animation : Animations)
		{
			CheckAnimation(Animation);
		}
	};

	// Everything stored inside the Widget Blueprint: its timelines' animations and its own machine (kept in the timelines
	// extension), and the Widget Animation Components and State Machine Components on its widgets. Designer preview copies are
	// never inside it.
	ForEachObjectWithOuter(Blueprint, [&CheckAnimation, &CheckMachine](UObject* Object)
	{
		if (!IsValid(Object) || Object->HasAnyFlags(RF_Transient))
		{
			return;
		}
		if (const UWidgetAnimationComponent* Component = Cast<UWidgetAnimationComponent>(Object))
		{
			for (const FWidgetAnimStateMachineEntry& Entry : Component->StateMachines)
			{
				CheckMachine(Entry.StateMachine);
			}
			for (const TArray<FWidgetAnimEntry>* Entries : { &Component->AnimIn, &Component->AnimOut })
			{
				for (const FWidgetAnimEntry& Entry : *Entries)
				{
					CheckAnimation(Entry.Animation);
				}
			}
		}
		else if (const UWidgetAnimationStateMachineComponent* MachineComponent = Cast<UWidgetAnimationStateMachineComponent>(Object))
		{
			for (const FWidgetAnimStateMachineEntry& Entry : MachineComponent->StateMachines)
			{
				CheckMachine(Entry.StateMachine);
			}
		}
		else if (const UWidgetAnimStateMachineAsset* Machine = Cast<UWidgetAnimStateMachineAsset>(Object))
		{
			CheckMachine(Machine);
		}
		else if (const UWidgetAnimPresetAsset* Animation = Cast<UWidgetAnimPresetAsset>(Object))
		{
			CheckAnimation(Animation);
		}
	}, /*bIncludeNestedObjects=*/true);
}

void UWidgetAnimCompilerExtension::ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data)
{
	Super::ProcessBlueprintCompiled(CompilationContext, Data);

	TArray<FText> Problems;
	WidgetAnimCompileChecks::Gather(CompilationContext.Blueprint, Problems);
	for (const FText& Problem : Problems)
	{
		// The log reads @@ as a token for an object; nothing of a name here should be taken for one.
		CompilationContext.MessageLog.Warning(*Problem.ToString().Replace(TEXT("@@"), TEXT("@ @")));
	}
}

void WidgetAnimCompileChecks::Register()
{
	// Kept for the editor's whole session: the compilation manager holds it from now on, and has no way to let it go.
	UWidgetAnimCompilerExtension* Extension = NewObject<UWidgetAnimCompilerExtension>(GetTransientPackage(), NAME_None, RF_Transient);
	Extension->AddToRoot();
	FBlueprintCompilationManager::RegisterCompilerExtension(UWidgetBlueprint::StaticClass(), Extension);
}

#undef LOCTEXT_NAMESPACE
