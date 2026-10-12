// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimStateMachine.h"

#include "Components/Widget.h"
#include "WidgetAnimLibrary.h"
#include "WidgetAnimPreset.h"
#include "WidgetAnimSpring.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "WidgetAnimStateMachine"

// =====================================================================================================
// Transitions and states
// =====================================================================================================

FWidgetAnimMachineTransition::FWidgetAnimMachineTransition()
{
	// A hover's spring: quick, with a little life in it.
	Spring.VisualDuration = 0.3f;
	Spring.Bounce = 0.15f;
}

float FWidgetAnimMachineTransition::GetBlendSeconds() const
{
	switch (Blend)
	{
	case EWidgetAnimMachineBlend::Cut:
		return 0.f;
	case EWidgetAnimMachineBlend::Spring:
		return UWidgetAnimSpringLibrary::GetVisualDuration(Spring);
	case EWidgetAnimMachineBlend::Animation:
		// No animation: the transition is only its blend.
		return TransitionAnimation ? GetTransitionAnimationSeconds() + GetTweenSeconds() : GetTweenSeconds();
	default:
		return GetTweenSeconds();
	}
}

float FWidgetAnimMachineTransition::GetTweenSeconds() const
{
	return FMath::Max(Blend == EWidgetAnimMachineBlend::Animation ? AnimationBlendTime : Duration, 0.f);
}

float FWidgetAnimMachineTransition::GetTransitionAnimationLength() const
{
	if (!TransitionAnimation)
	{
		return 0.f;
	}

	// One pass of it as a use plays it: every group on no widget in particular - the length doesn't depend on which.
	FWidgetAnimEntry Use;
	Use.Animation = TransitionAnimation;
	Use.PlaybackSpeed = 1.f;
	Use.ProgressAnimationDuration = 1.f;
	TArray<FWidgetAnimSettings> Settings;
	Use.BuildSettings(Settings, [](int32) -> UWidget* { return nullptr; });
	return Settings.Num() > 0 ? UWidgetAnimLibrary::GetStackPassDuration(Settings) : 0.f;
}

float FWidgetAnimMachineTransition::GetTransitionAnimationSeconds() const
{
	if (bOverrideAnimationDuration)
	{
		return FMath::Max(AnimationDuration, 0.01f);
	}
	return GetTransitionAnimationLength() / FMath::Max(TransitionAnimationSpeed, 0.01f);
}

float FWidgetAnimMachineTransition::GetTransitionAnimationSpeed() const
{
	if (!bOverrideAnimationDuration)
	{
		return FMath::Max(TransitionAnimationSpeed, 0.01f);
	}
	const float Length = GetTransitionAnimationLength();
	return Length > KINDA_SMALL_NUMBER ? FMath::Max(Length / FMath::Max(AnimationDuration, 0.01f), 0.01f) : 1.f;
}

FWidgetAnimMachineState::FWidgetAnimMachineState()
{
}

// =====================================================================================================
// FWidgetAnimMachineLayer
// =====================================================================================================

int32 FWidgetAnimMachineLayer::FindStateIndex(const FGuid& Id) const
{
	return Id.IsValid() ? States.IndexOfByPredicate([&Id](const FWidgetAnimMachineState& State) { return State.Id == Id; }) : INDEX_NONE;
}

int32 FWidgetAnimMachineLayer::FindStateIndexByName(FName StateName) const
{
	return StateName.IsNone() ? INDEX_NONE : States.IndexOfByPredicate([StateName](const FWidgetAnimMachineState& State) { return State.Name == StateName; });
}

int32 FWidgetAnimMachineLayer::FindSpecialIndex(EWidgetAnimMachineNodeKind Kind, const FGuid& ParentMachine) const
{
	return States.IndexOfByPredicate([Kind, &ParentMachine](const FWidgetAnimMachineState& State) { return State.Kind == Kind && State.ParentMachine == ParentMachine; });
}

int32 FWidgetAnimMachineLayer::FindParentIndex(int32 Index) const
{
	return States.IsValidIndex(Index) && States[Index].ParentMachine.IsValid() ? FindStateIndex(States[Index].ParentMachine) : INDEX_NONE;
}

bool FWidgetAnimMachineLayer::IsWithin(int32 Index, int32 Ancestor) const
{
	if (!States.IsValidIndex(Ancestor))
	{
		return false;
	}
	// Up through the sub-machines it is in - never further than there are states, whatever a broken asset says.
	for (int32 Step = 0; States.IsValidIndex(Index) && Step <= States.Num(); ++Step)
	{
		if (Index == Ancestor)
		{
			return true;
		}
		Index = FindParentIndex(Index);
	}
	return false;
}

int32 FWidgetAnimMachineLayer::GetDepth(int32 Index) const
{
	int32 Depth = 0;
	for (int32 Parent = FindParentIndex(Index); Parent != INDEX_NONE && Depth <= States.Num(); Parent = FindParentIndex(Parent))
	{
		++Depth;
	}
	return Depth;
}

int32 FWidgetAnimMachineLayer::FindTransitionIndex(const FGuid& Id) const
{
	return Id.IsValid() ? Transitions.IndexOfByPredicate([&Id](const FWidgetAnimMachineTransition& Transition) { return Transition.Id == Id; }) : INDEX_NONE;
}

const FWidgetAnimMachineState* FWidgetAnimMachineLayer::FindState(const FGuid& Id) const
{
	const int32 Index = FindStateIndex(Id);
	return States.IsValidIndex(Index) ? &States[Index] : nullptr;
}

bool FWidgetAnimMachineLayer::HasExitPath() const
{
	const int32 ExitIndex = FindSpecialIndex(EWidgetAnimMachineNodeKind::Exit);
	if (!States.IsValidIndex(ExitIndex))
	{
		return false;
	}
	const FGuid ExitId = States[ExitIndex].Id;
	return Transitions.ContainsByPredicate([&ExitId](const FWidgetAnimMachineTransition& Transition) { return Transition.bEnabled && Transition.To == ExitId; });
}

// =====================================================================================================
// UWidgetAnimStateMachineAsset
// =====================================================================================================

FString UWidgetAnimStateMachineAsset::GetMachineDisplayName() const
{
	FString Name = GetName();
	if (Name.StartsWith(TEXT("WASM_"), ESearchCase::IgnoreCase) && Name.Len() > 5)
	{
		Name.RightChopInline(5);
	}
	return Name;
}

const FWidgetAnimMachineInput* UWidgetAnimStateMachineAsset::FindInput(FName InputName) const
{
	return InputName.IsNone() ? nullptr : Inputs.FindByPredicate([InputName](const FWidgetAnimMachineInput& Input) { return Input.Name == InputName; });
}

int32 UWidgetAnimStateMachineAsset::FindLayerIndex(FName LayerName) const
{
	return Layers.IndexOfByPredicate([LayerName](const FWidgetAnimMachineLayer& Layer) { return Layer.Name == LayerName; });
}

void UWidgetAnimStateMachineAsset::GatherAnimations(TArray<UWidgetAnimPresetAsset*>& OutAnimations) const
{
	for (const FWidgetAnimMachineLayer& Layer : Layers)
	{
		for (const FWidgetAnimMachineState& State : Layer.States)
		{
			if (State.Kind != EWidgetAnimMachineNodeKind::State)
			{
				continue;
			}
			// What it plays: its animation and random variations, or its blend samples.
			if (!State.IsBlendState())
			{
				if (State.Animation)
				{
					OutAnimations.AddUnique(State.Animation);
				}
				for (const FWidgetAnimMachineRandomVariation& Variation : State.RandomVariations)
				{
					if (Variation.Animation)
					{
						OutAnimations.AddUnique(Variation.Animation);
					}
				}
			}
			else
			{
				for (const FWidgetAnimMachineBlendSample& Sample : State.BlendSamples)
				{
					if (Sample.Animation)
					{
						OutAnimations.AddUnique(Sample.Animation);
					}
				}
			}
		}
		for (const FWidgetAnimMachineTransition& Transition : Layer.Transitions)
		{
			if (Transition.TransitionAnimation && Transition.Blend == EWidgetAnimMachineBlend::Animation)
			{
				OutAnimations.AddUnique(Transition.TransitionAnimation);
			}
		}
	}
}

void UWidgetAnimStateMachineAsset::GetTargetNames(TArray<FName>& OutNames) const
{
	// The groups of every animation, matched by name: a "Background" group in the Hovered animation and in the Pressed one
	// move the same widget, so a use of the machine maps it once.
	TArray<UWidgetAnimPresetAsset*> Animations;
	GatherAnimations(Animations);
	for (const UWidgetAnimPresetAsset* Animation : Animations)
	{
		const int32 NumGroups = Animation->LayerGroups.Num();
		for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
		{
			OutNames.AddUnique(FName(*Animation->LayerGroups[GroupIndex].GetDisplayName(GroupIndex)));
		}
	}
}

FName UWidgetAnimStateMachineAsset::GetTargetDefault(FName TargetName) const
{
	TArray<UWidgetAnimPresetAsset*> Animations;
	GatherAnimations(Animations);
	for (const UWidgetAnimPresetAsset* Animation : Animations)
	{
		const int32 NumGroups = Animation->LayerGroups.Num();
		for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
		{
			if (FName(*Animation->LayerGroups[GroupIndex].GetDisplayName(GroupIndex)) == TargetName)
			{
				return Animation->LayerGroups[GroupIndex].PreviewTarget;
			}
		}
	}
	return NAME_None;
}

bool UWidgetAnimStateMachineAsset::HasExitPath() const
{
	return Layers.ContainsByPredicate([](const FWidgetAnimMachineLayer& Layer) { return Layer.HasExitPath(); });
}

bool UWidgetAnimStateMachineAsset::UsesInput(FName InputName) const
{
	if (InputName.IsNone())
	{
		return false;
	}

	// A Press And Hold with nothing driving it is held by the widget's own pressed state.
	if (InputName == WidgetAnimMachineInputs::Pressed && Inputs.ContainsByPredicate([](const FWidgetAnimMachineInput& Input)
	{
		return Input.Type == EWidgetAnimMachineInputType::Hold && !Input.Source.IsValid();
	}))
	{
		return true;
	}

	// Clicked is read off the widget's press, hover and focus.
	if ((InputName == WidgetAnimMachineInputs::Pressed || InputName == WidgetAnimMachineInputs::Hovered || InputName == WidgetAnimMachineInputs::Focused)
		&& InputName != WidgetAnimMachineInputs::Clicked && UsesInput(WidgetAnimMachineInputs::Clicked))
	{
		return true;
	}

	for (const FWidgetAnimMachineLayer& Layer : Layers)
	{
		if (Layer.WeightInput == InputName)
		{
			return true;
		}
		for (const FWidgetAnimMachineState& State : Layer.States)
		{
			if (State.ProgressInput == InputName || State.SpeedInput == InputName || (State.IsBlendState() && State.BlendInput == InputName))
			{
				return true;
			}
		}
		for (const FWidgetAnimMachineTransition& Transition : Layer.Transitions)
		{
			if (Transition.Conditions.ContainsByPredicate([InputName](const FWidgetAnimMachineCondition& Condition)
			{
				return !Condition.TestsLayerState()
					&& (Condition.Input == InputName || (Condition.CompareTo == EWidgetAnimMachineCompareTo::Input && Condition.OtherInput == InputName));
			}))
			{
				return true;
			}
		}
	}
	return false;
}

FName UWidgetAnimStateMachineAsset::MakeUniqueStateName(const FWidgetAnimMachineLayer& Layer, FName BaseName, const FGuid& Except)
{
	const FString Base = BaseName.IsNone() ? FString(TEXT("State")) : BaseName.GetPlainNameString();
	auto IsTaken = [&Layer, &Except](FName Candidate)
	{
		return Layer.States.ContainsByPredicate([Candidate, &Except](const FWidgetAnimMachineState& State) { return State.Name == Candidate && State.Id != Except; });
	};

	FName Candidate(*Base);
	for (int32 Suffix = 2; IsTaken(Candidate); ++Suffix)
	{
		Candidate = FName(*FString::Printf(TEXT("%s %d"), *Base, Suffix));
	}
	return Candidate;
}

FName UWidgetAnimStateMachineAsset::MakeUniqueLayerName(FName BaseName, int32 ExceptIndex) const
{
	const FString Base = BaseName.IsNone() ? FString(TEXT("Layer")) : BaseName.GetPlainNameString();
	auto IsTaken = [this, ExceptIndex](FName Candidate)
	{
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			if (Index != ExceptIndex && Layers[Index].Name == Candidate)
			{
				return true;
			}
		}
		return false;
	};

	FName Candidate(*Base);
	for (int32 Suffix = 2; IsTaken(Candidate); ++Suffix)
	{
		Candidate = FName(*FString::Printf(TEXT("%s %d"), *Base, Suffix));
	}
	return Candidate;
}

FName UWidgetAnimStateMachineAsset::MakeNewLayerName() const
{
	// Numbered from 1, so the first layer added over Base reads "Layer 1" rather than a bare "Layer" followed by "Layer 2".
	for (int32 Number = 1;; ++Number)
	{
		const FName Candidate(*FString::Printf(TEXT("Layer %d"), Number));
		if (FindLayerIndex(Candidate) == INDEX_NONE)
		{
			return Candidate;
		}
	}
}

FName UWidgetAnimStateMachineAsset::MakeUniqueInputName(FName BaseName, int32 ExceptIndex) const
{
	const FString Base = BaseName.IsNone() ? FString(TEXT("Input")) : BaseName.GetPlainNameString();
	auto IsTaken = [this, ExceptIndex](FName Candidate)
	{
		if (WidgetAnimMachineInputs::IsBuiltIn(Candidate))
		{
			return true;
		}
		for (int32 Index = 0; Index < Inputs.Num(); ++Index)
		{
			if (Index != ExceptIndex && Inputs[Index].Name == Candidate)
			{
				return true;
			}
		}
		return false;
	};

	FName Candidate(*Base);
	for (int32 Suffix = 2; IsTaken(Candidate); ++Suffix)
	{
		Candidate = FName(*FString::Printf(TEXT("%s %d"), *Base, Suffix));
	}
	return Candidate;
}

FWidgetAnimMachineLayer UWidgetAnimStateMachineAsset::MakeLayer(FName LayerName)
{
	FWidgetAnimMachineLayer Layer;
	Layer.Name = LayerName;

	// The special nodes in a column down the left, where a graph is read from.
	auto AddSpecial = [&Layer](EWidgetAnimMachineNodeKind Kind, const TCHAR* Name, const FVector2D& Position)
	{
		FWidgetAnimMachineState& Node = Layer.States.AddDefaulted_GetRef();
		Node.Id = FGuid::NewGuid();
		Node.Kind = Kind;
		Node.Name = FName(Name);
#if WITH_EDITORONLY_DATA
		Node.EditorPosition = Position;
#endif
	};
	AddSpecial(EWidgetAnimMachineNodeKind::Entry, TEXT("Entry"), FVector2D(-320.0, 0.0));
	AddSpecial(EWidgetAnimMachineNodeKind::AnyState, TEXT("Any State"), FVector2D(-320.0, -160.0));
	AddSpecial(EWidgetAnimMachineNodeKind::Exit, TEXT("Exit"), FVector2D(480.0, 0.0));
	return Layer;
}

bool UWidgetAnimStateMachineAsset::Normalize()
{
	bool bChanged = false;

	// A machine with no layer at all is given one, so it always has a graph to edit.
	if (Layers.Num() == 0)
	{
		Layers.Add(MakeLayer(TEXT("Base")));
		bChanged = true;
	}

	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		FWidgetAnimMachineLayer& Layer = Layers[LayerIndex];

		const FName UniqueLayerName = MakeUniqueLayerName(Layer.Name.IsNone() ? FName(TEXT("Layer")) : Layer.Name, LayerIndex);
		if (UniqueLayerName != Layer.Name)
		{
			Layer.Name = UniqueLayerName;
			bChanged = true;
		}

		// A node in a sub-machine that is gone (or is no sub-machine) is moved up to the layer's own graph - never lost; the
		// special nodes among them go with the extras below. A sub-machine inside itself, however deep, is cut loose the same way.
		for (FWidgetAnimMachineState& State : Layer.States)
		{
			if (!State.ParentMachine.IsValid())
			{
				continue;
			}
			const FWidgetAnimMachineState* Parent = Layer.FindState(State.ParentMachine);
			if (!Parent || !Parent->IsSubMachine() || Parent->Id == State.Id)
			{
				State.ParentMachine.Invalidate();
				bChanged = true;
			}
		}
		for (int32 Index = 0; Index < Layer.States.Num(); ++Index)
		{
			if (Layer.States[Index].ParentMachine.IsValid() && Layer.GetDepth(Index) > Layer.States.Num())
			{
				Layer.States[Index].ParentMachine.Invalidate();
				bChanged = true;
			}
		}

		// Its special nodes, and every sub-machine's: one each, made if missing - a layer added in the Details comes with none,
		// a state just made a sub-machine too - and extra ones dropped, a duplicated node never being anything but a mistake.
		const FWidgetAnimMachineLayer Fresh = MakeLayer(Layer.Name);
		TArray<FGuid> Scopes;
		Scopes.Add(FGuid());
		for (const FWidgetAnimMachineState& State : Layer.States)
		{
			if (State.IsSubMachine())
			{
				Scopes.Add(State.Id);
			}
		}
		for (const FGuid& Scope : Scopes)
		{
			for (const EWidgetAnimMachineNodeKind Kind : { EWidgetAnimMachineNodeKind::Entry, EWidgetAnimMachineNodeKind::AnyState, EWidgetAnimMachineNodeKind::Exit })
			{
				int32 Found = INDEX_NONE;
				for (int32 Index = Layer.States.Num() - 1; Index >= 0; --Index)
				{
					if (Layer.States[Index].Kind != Kind || Layer.States[Index].ParentMachine != Scope)
					{
						continue;
					}
					if (Found == INDEX_NONE)
					{
						Found = Index;
						continue;
					}
					Layer.States.RemoveAt(Found);
					Found = Index;
					bChanged = true;
				}
				if (Found == INDEX_NONE)
				{
					// Placed as a new layer's are - a sub-machine's graph is laid out like one.
					FWidgetAnimMachineState Special = Fresh.States[Fresh.FindSpecialIndex(Kind)];
					Special.Id = FGuid::NewGuid();
					Special.ParentMachine = Scope;
					Layer.States.Add(Special);
					bChanged = true;
				}
			}
		}

		// Every node an id of its own and a name unique in the layer - a duplicated layer or state copies its ids.
		TSet<FGuid> SeenIds;
		for (FWidgetAnimMachineState& State : Layer.States)
		{
			bool bSeen = false;
			if (State.Id.IsValid())
			{
				SeenIds.Add(State.Id, &bSeen);
			}
			if (!State.Id.IsValid() || bSeen)
			{
				State.Id = FGuid::NewGuid();
				SeenIds.Add(State.Id);
				bChanged = true;
			}
		}
		for (FWidgetAnimMachineState& State : Layer.States)
		{
			const FName Wanted = State.Name.IsNone() ? FName(TEXT("State")) : State.Name;
			const FName Unique = MakeUniqueStateName(Layer, Wanted, State.Id);
			if (Unique != State.Name)
			{
				State.Name = Unique;
				bChanged = true;
			}
		}

		// Transitions: an id each; those whose ends are gone - or that go nowhere they can - dropped.
		const int32 NumBefore = Layer.Transitions.Num();
		Layer.Transitions.RemoveAll([&Layer](const FWidgetAnimMachineTransition& Transition)
		{
			const FWidgetAnimMachineState* From = Layer.FindState(Transition.From);
			const FWidgetAnimMachineState* To = Layer.FindState(Transition.To);
			return !From || !To
				|| From->Kind == EWidgetAnimMachineNodeKind::Exit
				|| To->Kind == EWidgetAnimMachineNodeKind::Entry
				|| To->Kind == EWidgetAnimMachineNodeKind::AnyState
				|| Transition.From == Transition.To
				// Between two nodes of the same graph: a sub-machine's insides are reached through its Entry.
				|| From->ParentMachine != To->ParentMachine;
		});
		bChanged |= Layer.Transitions.Num() != NumBefore;

		TSet<FGuid> SeenTransitions;
		for (FWidgetAnimMachineTransition& Transition : Layer.Transitions)
		{
			bool bSeen = false;
			if (Transition.Id.IsValid())
			{
				SeenTransitions.Add(Transition.Id, &bSeen);
			}
			if (!Transition.Id.IsValid() || bSeen)
			{
				Transition.Id = FGuid::NewGuid();
				SeenTransitions.Add(Transition.Id);
				bChanged = true;
			}
		}

#if WITH_EDITORONLY_DATA
		// Comment boxes: an id each, as the graph knows them by - and in a graph that is there.
		TSet<FGuid> SeenComments;
		for (FWidgetAnimMachineComment& Comment : Layer.Comments)
		{
			if (Comment.ParentMachine.IsValid() && !Scopes.Contains(Comment.ParentMachine))
			{
				Comment.ParentMachine.Invalidate();
				bChanged = true;
			}
			bool bSeen = false;
			if (Comment.Id.IsValid())
			{
				SeenComments.Add(Comment.Id, &bSeen);
			}
			if (!Comment.Id.IsValid() || bSeen)
			{
				Comment.Id = FGuid::NewGuid();
				SeenComments.Add(Comment.Id);
				bChanged = true;
			}
		}

		// Random Variations: v1's (no chances) moved over, each an equal share with the state's own; then kept to 1 at most.
		for (FWidgetAnimMachineState& State : Layer.States)
		{
			if (State.RandomAnimations.Num() > 0)
			{
				const float Share = 1.f / static_cast<float>(State.RandomAnimations.Num() + 1);
				for (const TObjectPtr<UWidgetAnimPresetAsset>& Old : State.RandomAnimations)
				{
					FWidgetAnimMachineRandomVariation& Moved = State.RandomVariations.AddDefaulted_GetRef();
					Moved.Animation = Old;
					Moved.Chance = Share;
				}
				State.RandomAnimations.Reset();
				bChanged = true;
			}
			float Total = 0.f;
			for (FWidgetAnimMachineRandomVariation& Variation : State.RandomVariations)
			{
				Variation.Chance = FMath::Clamp(Variation.Chance, 0.f, 1.f);
				Total += Variation.Chance;
			}
			if (Total > 1.f + KINDA_SMALL_NUMBER)
			{
				for (FWidgetAnimMachineRandomVariation& Variation : State.RandomVariations)
				{
					Variation.Chance /= Total;
				}
				bChanged = true;
			}
		}

		// Collapsed groups: a state in a group that is gone is in none; a group with no state left goes.
		for (FWidgetAnimMachineState& State : Layer.States)
		{
			const bool bGroupExists = State.EditorGroup.IsValid()
				&& Layer.EditorGroups.ContainsByPredicate([&State](const FWidgetAnimMachineStateGroup& Group) { return Group.Id == State.EditorGroup; });
			if (State.EditorGroup.IsValid() && (!bGroupExists || State.Kind != EWidgetAnimMachineNodeKind::State))
			{
				State.EditorGroup.Invalidate();
				bChanged = true;
			}
		}
		const int32 NumGroupsBefore = Layer.EditorGroups.Num();
		Layer.EditorGroups.RemoveAll([&Layer](const FWidgetAnimMachineStateGroup& Group)
		{
			return !Group.Id.IsValid() || !Layer.States.ContainsByPredicate([&Group](const FWidgetAnimMachineState& State) { return State.EditorGroup == Group.Id; });
		});
		bChanged |= Layer.EditorGroups.Num() != NumGroupsBefore;
#endif
	}

	// Inputs: a name each, unique, and never one of the built-in ones.
	for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
	{
		FWidgetAnimMachineInput& Input = Inputs[InputIndex];
		const FName Unique = MakeUniqueInputName(Input.Name.IsNone() ? FName(TEXT("Input")) : Input.Name, InputIndex);
		if (Unique != Input.Name)
		{
			Input.Name = Unique;
			bChanged = true;
		}
	}

	return bChanged;
}

TArray<FString> UWidgetAnimStateMachineAsset::GetTargetNameOptions() const
{
	TArray<FString> Options;
	Options.Add(TEXT("None"));
	TArray<FName> Names;
	GetTargetNames(Names);
	for (const FName Name : Names)
	{
		Options.Add(Name.ToString());
	}
	return Options;
}

TArray<FString> UWidgetAnimStateMachineAsset::GetNumberInputOptions() const
{
	TArray<FString> Options;
	Options.Add(TEXT("None"));
	for (const FWidgetAnimMachineInput& Input : Inputs)
	{
		// A Press And Hold too: how far through its hold - a charge ring's fill.
		if ((Input.Type == EWidgetAnimMachineInputType::Number || Input.Type == EWidgetAnimMachineInputType::Hold) && !Input.Name.IsNone())
		{
			Options.Add(Input.Name.ToString());
		}
	}
	return Options;
}

const FName UWidgetAnimStateMachineAsset::MaskHostTarget(TEXT("(Widget)"));

UWidgetAnimPresetAsset* FWidgetAnimMachineState::PickRandomAnimation(float Roll) const
{
	float Reached = 0.f;
	for (const FWidgetAnimMachineRandomVariation& Variation : RandomVariations)
	{
		Reached += FMath::Max(Variation.Chance, 0.f);
		if (Variation.Animation && Roll < Reached)
		{
			return Variation.Animation;
		}
	}
	return Animation;
}

TArray<FString> UWidgetAnimStateMachineAsset::GetConditionLayerOptions() const
{
	TArray<FString> Options;
	for (const FWidgetAnimMachineLayer& Layer : Layers)
	{
		Options.Add(Layer.Name.ToString());
	}
	return Options;
}

TArray<FString> UWidgetAnimStateMachineAsset::GetConditionStateOptions() const
{
	TArray<FString> Options;
	for (const FWidgetAnimMachineLayer& Layer : Layers)
	{
		for (const FWidgetAnimMachineState& State : Layer.States)
		{
			// States inside sub-machines too; the layer's own Exit, never a sub-machine's.
			if (State.Kind == EWidgetAnimMachineNodeKind::State || (State.Kind == EWidgetAnimMachineNodeKind::Exit && !State.ParentMachine.IsValid()))
			{
				Options.AddUnique(State.Name.ToString());
			}
		}
	}
	return Options;
}

TArray<FString> UWidgetAnimStateMachineAsset::GetMaskTargetOptions() const
{
	TArray<FString> Options;
	Options.Add(FWidgetAnimGroupTarget::ThisWidget.ToString());
	TArray<FName> Names;
	GetTargetNames(Names);
	for (const FName Name : Names)
	{
		Options.Add(Name.ToString());
	}
	return Options;
}

TArray<FString> UWidgetAnimStateMachineAsset::GetWidgetInputTargetOptions() const
{
	return GetMaskTargetOptions();
}

TArray<FString> UWidgetAnimStateMachineAsset::GetInputCategoryOptions() const
{
	TArray<FString> Options;
	for (const FWidgetAnimMachineInput& Input : Inputs)
	{
		if (!Input.Category.TrimStartAndEnd().IsEmpty())
		{
			Options.AddUnique(Input.Category.TrimStartAndEnd());
		}
	}
	Options.Sort();
	return Options;
}

TArray<FString> UWidgetAnimStateMachineAsset::GetBlendInputOptions() const
{
	TArray<FString> Options;
	Options.Add(TEXT("None"));
	for (const FWidgetAnimMachineInput& Input : Inputs)
	{
		if (!Input.Name.IsNone() && (Input.Type == EWidgetAnimMachineInputType::Number || Input.Type == EWidgetAnimMachineInputType::Integer
			|| Input.Type == EWidgetAnimMachineInputType::Hold || Input.Type == EWidgetAnimMachineInputType::Vector2D))
		{
			Options.Add(Input.Name.ToString());
		}
	}
	return Options;
}

TArray<FString> UWidgetAnimStateMachineAsset::GetSettableInputOptions() const
{
	TArray<FString> Options;
	for (const FWidgetAnimMachineInput& Input : Inputs)
	{
		if (!Input.Name.IsNone())
		{
			Options.Add(Input.Name.ToString());
		}
	}
	return Options;
}

void UWidgetAnimStateMachineAsset::RenameLayerStateReferences(FName LayerName, FName OldName, FName NewName)
{
	if (OldName == NewName)
	{
		return;
	}
	for (FWidgetAnimMachineLayer& Layer : Layers)
	{
		for (FWidgetAnimMachineTransition& Transition : Layer.Transitions)
		{
			for (FWidgetAnimMachineCondition& Condition : Transition.Conditions)
			{
				if (!Condition.TestsLayerState())
				{
					continue;
				}
				if (LayerName.IsNone())
				{
					if (Condition.Layer == OldName)
					{
						Condition.Layer = NewName;
					}
				}
				else if (Condition.Layer == LayerName && Condition.State == OldName)
				{
					Condition.State = NewName;
				}
			}
		}
	}
}

TArray<FString> UWidgetAnimStateMachineAsset::GetStateParameterOptions() const
{
	TArray<FString> Options;
	for (const FWidgetAnimMachineLayer& Layer : Layers)
	{
		for (const FWidgetAnimMachineState& State : Layer.States)
		{
			// Every animation it can play: its own and variations, or its blend samples'.
			TArray<const UWidgetAnimPresetAsset*, TInlineAllocator<8>> Played;
			Played.Add(State.Animation);
			for (const FWidgetAnimMachineRandomVariation& Variation : State.RandomVariations)
			{
				Played.Add(Variation.Animation);
			}
			for (const FWidgetAnimMachineBlendSample& Sample : State.BlendSamples)
			{
				Played.Add(Sample.Animation);
			}
			for (const UWidgetAnimPresetAsset* Animation : Played)
			{
				if (Animation)
				{
					for (const FWidgetAnimParameter& Parameter : Animation->Parameters)
					{
						Options.AddUnique(Parameter.Name.ToString());
					}
				}
			}
		}
	}
	return Options;
}

TArray<FString> UWidgetAnimStateMachineAsset::GetInputNameOptions() const
{
	TArray<FString> Options;
	for (const FWidgetAnimMachineInput& Input : Inputs)
	{
		if (!Input.Name.IsNone())
		{
			Options.Add(Input.Name.ToString());
		}
	}
	for (const FName BuiltIn : WidgetAnimMachineInputs::GetBuiltIns())
	{
		Options.Add(BuiltIn.ToString());
	}
	return Options;
}

void UWidgetAnimStateMachineAsset::PostLoad()
{
	Super::PostLoad();
	Normalize();
}

void UWidgetAnimStateMachineAsset::PostInitProperties()
{
	Super::PostInitProperties();

	// A new machine starts with one layer, ready to have states added - never the class default, which stays empty.
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_WasLoaded))
	{
		Normalize();
	}
}

#if WITH_EDITOR
void UWidgetAnimStateMachineAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Only once an edit is committed: renaming under a drag in progress would pull the row out from under it.
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		Normalize();
	}
	NotifyMachineChanged();
}

void UWidgetAnimStateMachineAsset::PostEditUndo()
{
	Super::PostEditUndo();
	NotifyMachineChanged();
}

UWidgetAnimStateMachineAsset::FOnMachineChanged& UWidgetAnimStateMachineAsset::OnMachineChanged()
{
	static FOnMachineChanged Delegate;
	return Delegate;
}

void UWidgetAnimStateMachineAsset::NotifyMachineChanged()
{
	OnMachineChanged().Broadcast(this);
}

EDataValidationResult UWidgetAnimStateMachineAsset::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Result = Super::IsDataValid(Context);
	TArray<FText> Problems;
	GatherProblems(Problems, FText::FromString(GetName()));
	for (const FText& Problem : Problems)
	{
		Context.AddWarning(Problem);
	}
	return Result;
}

void UWidgetAnimStateMachineAsset::GatherProblems(TArray<FText>& OutProblems, const FText& MachineName) const
{
	for (const FWidgetAnimMachineLayer& Layer : Layers)
	{
		for (const FWidgetAnimMachineTransition& Transition : Layer.Transitions)
		{
			for (const FWidgetAnimMachineCondition& Condition : Transition.Conditions)
			{
				if (!Condition.TestsLayerState())
				{
					// An input condition naming an input that is gone (renamed or deleted by hand) never holds either.
					if (!Condition.Input.IsNone() && !FindInput(Condition.Input) && !WidgetAnimMachineInputs::IsBuiltIn(Condition.Input))
					{
						OutProblems.Add(FText::Format(NSLOCTEXT("WidgetAnimStateMachine", "MissingConditionInput", "{0}: a transition on layer {1} tests the input {2}, which the machine doesn't have - the condition never holds."),
							MachineName, FText::FromName(Layer.Name), FText::FromName(Condition.Input)));
					}
					continue;
				}
				const int32 OtherLayer = FindLayerIndex(Condition.Layer);
				if (!Layers.IsValidIndex(OtherLayer))
				{
					OutProblems.Add(FText::Format(NSLOCTEXT("WidgetAnimStateMachine", "MissingConditionLayer", "{0}: a transition on layer {1} tests the layer {2}, which the machine doesn't have - the condition never holds."),
						MachineName, FText::FromName(Layer.Name), FText::FromName(Condition.Layer)));
				}
				else if (Layers[OtherLayer].FindStateIndexByName(Condition.State) == INDEX_NONE)
				{
					OutProblems.Add(FText::Format(NSLOCTEXT("WidgetAnimStateMachine", "MissingConditionState", "{0}: a transition on layer {1} tests {2}'s state {3}, which that layer doesn't have - the condition never holds."),
						MachineName, FText::FromName(Layer.Name), FText::FromName(Condition.Layer), FText::FromName(Condition.State)));
				}
			}
		}
		for (const FWidgetAnimMachineState& State : Layer.States)
		{
			for (const FWidgetAnimMachineParameterBinding& Binding : State.ParameterInputs)
			{
				if (Binding.Parameter.IsNone())
				{
					continue;
				}
				// Any of what the state can play declaring it is enough.
				bool bDeclared = State.Animation && State.Animation->FindParameter(Binding.Parameter);
				for (const FWidgetAnimMachineRandomVariation& Variation : State.RandomVariations)
				{
					bDeclared = bDeclared || (Variation.Animation && Variation.Animation->FindParameter(Binding.Parameter));
				}
				for (const FWidgetAnimMachineBlendSample& Sample : State.BlendSamples)
				{
					bDeclared = bDeclared || (Sample.Animation && Sample.Animation->FindParameter(Binding.Parameter));
				}
				if (!bDeclared)
				{
					OutProblems.Add(FText::Format(NSLOCTEXT("WidgetAnimStateMachine", "MissingStateParameter", "{0}: state {1} sets the parameter {2} from {3}, but none of its animations has that parameter - it is ignored."),
						MachineName, FText::FromName(State.Name), FText::FromName(Binding.Parameter), FText::FromName(Binding.Input)));
				}
				if (!Binding.Input.IsNone() && !FindInput(Binding.Input) && !WidgetAnimMachineInputs::IsBuiltIn(Binding.Input))
				{
					OutProblems.Add(FText::Format(NSLOCTEXT("WidgetAnimStateMachine", "MissingStateParameterInput", "{0}: state {1} sets {2} from the input {3}, which the machine doesn't have - it is ignored."),
						MachineName, FText::FromName(State.Name), FText::FromName(Binding.Parameter), FText::FromName(Binding.Input)));
				}
			}
		}
	}
}
#endif

// =====================================================================================================
// Built-in inputs
// =====================================================================================================

namespace WidgetAnimMachineInputs
{
	const FName Hovered(TEXT("Hovered"));
	const FName Pressed(TEXT("Pressed"));
	const FName Focused(TEXT("Focused"));
	const FName Selected(TEXT("Selected"));
	const FName Disabled(TEXT("Disabled"));
	const FName Closing(TEXT("Closing"));
	const FName Clicked(TEXT("Clicked"));

	const FName HoverEntered(TEXT("Hover Entered"));
	const FName HoverExited(TEXT("Hover Exited"));
	const FName PressStarted(TEXT("Press Started"));
	const FName Released(TEXT("Released"));
	const FName FocusGained(TEXT("Focus Gained"));
	const FName FocusLost(TEXT("Focus Lost"));
	const FName DoubleClicked(TEXT("Double Clicked"));
	const FName RightClicked(TEXT("Right Clicked"));

	const FName Accept(TEXT("Accept"));
	const FName Back(TEXT("Back"));
	const FName GamepadFaceBottom(TEXT("Gamepad Face Bottom"));
	const FName GamepadFaceRight(TEXT("Gamepad Face Right"));
	const FName GamepadFaceLeft(TEXT("Gamepad Face Left"));
	const FName GamepadFaceTop(TEXT("Gamepad Face Top"));
	const FName GamepadLeftShoulder(TEXT("Gamepad Left Shoulder"));
	const FName GamepadRightShoulder(TEXT("Gamepad Right Shoulder"));
	const FName GamepadLeftTrigger(TEXT("Gamepad Left Trigger"));
	const FName GamepadRightTrigger(TEXT("Gamepad Right Trigger"));
	const FName GamepadDPadUp(TEXT("Gamepad D-Pad Up"));
	const FName GamepadDPadDown(TEXT("Gamepad D-Pad Down"));
	const FName GamepadDPadLeft(TEXT("Gamepad D-Pad Left"));
	const FName GamepadDPadRight(TEXT("Gamepad D-Pad Right"));
	const FName GamepadMenu(TEXT("Gamepad Menu"));
	const FName GamepadView(TEXT("Gamepad View"));
	const FName GamepadLeftStick(TEXT("Gamepad Left Stick Click"));
	const FName GamepadRightStick(TEXT("Gamepad Right Stick Click"));

	namespace
	{
		/** One built-in input: where it is listed, whether it is a pulse, and the gamepad button it reads. */
		struct FBuiltInEntry
		{
			FName Name;
			FName Group;
			FName SubGroup;
			bool bPulse = false;
			FKey Key;
		};

		const TArray<FBuiltInEntry>& GetEntries()
		{
			static const FName Events(TEXT("Events"));
			static const FName Gamepad(TEXT("Gamepad"));
			static const FName Navigation(TEXT("Navigation"));
			static const FName FaceButtons(TEXT("Face Buttons"));
			static const FName Shoulders(TEXT("Shoulders And Triggers"));
			static const FName DPad(TEXT("D-Pad"));
			static const FName MenuAndSticks(TEXT("Menu And Sticks"));

			// The first seven keep their bits - their order is the one they always had.
			static const TArray<FBuiltInEntry> Entries =
			{
				{ Hovered, NAME_None, NAME_None, false, EKeys::Invalid },
				{ Pressed, NAME_None, NAME_None, false, EKeys::Invalid },
				{ Focused, NAME_None, NAME_None, false, EKeys::Invalid },
				{ Selected, NAME_None, NAME_None, false, EKeys::Invalid },
				{ Disabled, NAME_None, NAME_None, false, EKeys::Invalid },
				{ Closing, NAME_None, NAME_None, false, EKeys::Invalid },
				{ Clicked, NAME_None, NAME_None, true, EKeys::Invalid },

				{ HoverEntered, Events, NAME_None, true, EKeys::Invalid },
				{ HoverExited, Events, NAME_None, true, EKeys::Invalid },
				{ PressStarted, Events, NAME_None, true, EKeys::Invalid },
				{ Released, Events, NAME_None, true, EKeys::Invalid },
				{ FocusGained, Events, NAME_None, true, EKeys::Invalid },
				{ FocusLost, Events, NAME_None, true, EKeys::Invalid },
				{ DoubleClicked, Events, NAME_None, true, EKeys::Invalid },
				{ RightClicked, Events, NAME_None, true, EKeys::Invalid },

				{ Accept, Gamepad, Navigation, true, EKeys::Invalid },
				{ Back, Gamepad, Navigation, true, EKeys::Invalid },
				{ GamepadFaceBottom, Gamepad, FaceButtons, true, EKeys::Gamepad_FaceButton_Bottom },
				{ GamepadFaceRight, Gamepad, FaceButtons, true, EKeys::Gamepad_FaceButton_Right },
				{ GamepadFaceLeft, Gamepad, FaceButtons, true, EKeys::Gamepad_FaceButton_Left },
				{ GamepadFaceTop, Gamepad, FaceButtons, true, EKeys::Gamepad_FaceButton_Top },
				{ GamepadLeftShoulder, Gamepad, Shoulders, true, EKeys::Gamepad_LeftShoulder },
				{ GamepadRightShoulder, Gamepad, Shoulders, true, EKeys::Gamepad_RightShoulder },
				{ GamepadLeftTrigger, Gamepad, Shoulders, true, EKeys::Gamepad_LeftTrigger },
				{ GamepadRightTrigger, Gamepad, Shoulders, true, EKeys::Gamepad_RightTrigger },
				{ GamepadDPadUp, Gamepad, DPad, true, EKeys::Gamepad_DPad_Up },
				{ GamepadDPadDown, Gamepad, DPad, true, EKeys::Gamepad_DPad_Down },
				{ GamepadDPadLeft, Gamepad, DPad, true, EKeys::Gamepad_DPad_Left },
				{ GamepadDPadRight, Gamepad, DPad, true, EKeys::Gamepad_DPad_Right },
				{ GamepadMenu, Gamepad, MenuAndSticks, true, EKeys::Gamepad_Special_Right },
				{ GamepadView, Gamepad, MenuAndSticks, true, EKeys::Gamepad_Special_Left },
				{ GamepadLeftStick, Gamepad, MenuAndSticks, true, EKeys::Gamepad_LeftThumbstick },
				{ GamepadRightStick, Gamepad, MenuAndSticks, true, EKeys::Gamepad_RightThumbstick },
			};
			return Entries;
		}

		const FBuiltInEntry* FindEntry(FName Name)
		{
			return Name.IsNone() ? nullptr : GetEntries().FindByPredicate([Name](const FBuiltInEntry& Entry) { return Entry.Name == Name; });
		}
	}

	const TArray<FName>& GetBuiltIns()
	{
		static const TArray<FName> BuiltIns = []()
		{
			TArray<FName> Names;
			for (const FBuiltInEntry& Entry : GetEntries())
			{
				Names.Add(Entry.Name);
			}
			// A bit each in a uint64 mask (GetBuiltInBit).
			check(Names.Num() <= 64);
			return Names;
		}();
		return BuiltIns;
	}

	uint64 GetBuiltInBit(FName Name)
	{
		const int32 Index = Name.IsNone() ? INDEX_NONE : GetBuiltIns().IndexOfByKey(Name);
		return Index == INDEX_NONE ? 0 : (uint64(1) << Index);
	}

	bool IsBuiltIn(FName Name)
	{
		return GetBuiltInBit(Name) != 0;
	}

	bool IsBuiltInPulse(FName Name)
	{
		const FBuiltInEntry* Entry = FindEntry(Name);
		return Entry && Entry->bPulse;
	}

	FName GetBuiltInGroup(FName Name)
	{
		const FBuiltInEntry* Entry = FindEntry(Name);
		return Entry ? Entry->Group : NAME_None;
	}

	FName GetBuiltInSubGroup(FName Name)
	{
		const FBuiltInEntry* Entry = FindEntry(Name);
		return Entry ? Entry->SubGroup : NAME_None;
	}

	FKey GetBuiltInKey(FName Name)
	{
		const FBuiltInEntry* Entry = FindEntry(Name);
		return Entry ? Entry->Key : EKeys::Invalid;
	}

	FText GetBuiltInDescription(FName Name)
	{
		if (Name == Hovered)
		{
			return LOCTEXT("HoveredBuiltIn", "Built in: the pointer is over the widget the machine plays on. A Common UI button's own hovered state.");
		}
		if (Name == Pressed)
		{
			return LOCTEXT("PressedBuiltIn", "Built in: the widget is held down - a Button's or Common UI button's pressed state, or the left mouse button pressed over it.");
		}
		if (Name == Focused)
		{
			return LOCTEXT("FocusedBuiltIn", "Built in: the widget, or something inside it, has keyboard or gamepad focus.");
		}
		if (Name == Selected)
		{
			return LOCTEXT("SelectedBuiltIn", "Built in: a Common UI button is selected, a Context Menu Button is enabled (toggled on, or the one picked in its group), or a Check Box is checked.");
		}
		if (Name == Disabled)
		{
			return LOCTEXT("DisabledBuiltIn", "Built in: the widget is disabled - itself, or one around it.");
		}
		if (Name == Closing)
		{
			return LOCTEXT("ClosingBuiltIn", "Built in: the widget is playing out - Play Out, a screen closing, or hidden with Set Visibility Animated. False again as it plays in. What transitions to Exit test.");
		}
		if (Name == Clicked)
		{
			return LOCTEXT("ClickedBuiltIn", "Built in, Fired the frame the widget is clicked: a Context Menu Button's action going off - on release or on press, as it is set, and every repeat of a Hold button - or any other widget let go while hovered or focused after being pressed. Not when the press is dragged off.");
		}
		if (Name == HoverEntered)
		{
			return LOCTEXT("HoverEnteredBuiltIn", "Built in, Fired the frame the widget becomes Hovered: the pointer moving onto it. Once, where Hovered stays true for as long as it is there.");
		}
		if (Name == HoverExited)
		{
			return LOCTEXT("HoverExitedBuiltIn", "Built in, Fired the frame the widget stops being Hovered: the pointer moving off it.");
		}
		if (Name == PressStarted)
		{
			return LOCTEXT("PressStartedBuiltIn", "Built in, Fired the frame the widget becomes Pressed: the press going down - a Button's or Common UI button's by mouse, keyboard or gamepad, else the left mouse button over it.");
		}
		if (Name == Released)
		{
			return LOCTEXT("ReleasedBuiltIn", "Built in, Fired the frame the widget stops being Pressed: the press let go, wherever the pointer is - Clicked is the one let go over the widget.");
		}
		if (Name == FocusGained)
		{
			return LOCTEXT("FocusGainedBuiltIn", "Built in, Fired the frame the widget becomes Focused: it, or something inside it, takes keyboard or gamepad focus.");
		}
		if (Name == FocusLost)
		{
			return LOCTEXT("FocusLostBuiltIn", "Built in, Fired the frame the widget stops being Focused: focus moving to something outside it.");
		}
		if (Name == DoubleClicked)
		{
			return LOCTEXT("DoubleClickedBuiltIn", "Built in, Fired the frame the left mouse button is double-clicked over the widget. The first click of the two is a Clicked of its own.");
		}
		if (Name == RightClicked)
		{
			return LOCTEXT("RightClickedBuiltIn", "Built in, Fired the frame the right mouse button is let go over the widget, after being pressed over it: a context menu's click.");
		}
		if (Name == Accept)
		{
			return LOCTEXT("AcceptBuiltIn", "Built in, Fired the frame the UI's confirm button is pressed while the widget, or something inside it, has focus: the gamepad's accept button (the bottom face button on most platforms, as Slate's navigation config has it), or Enter or Space.");
		}
		if (Name == Back)
		{
			return LOCTEXT("BackBuiltIn", "Built in, Fired the frame the UI's cancel button is pressed while the widget, or something inside it, has focus: the gamepad's back button (the right face button on most platforms, as Slate's navigation config has it), or Escape.");
		}
		const FKey Key = GetBuiltInKey(Name);
		if (Key.IsValid())
		{
			return FText::Format(LOCTEXT("GamepadBuiltIn", "Built in, Fired the frame {0} is pressed while the widget, or something inside it, has keyboard or gamepad focus. Not again while it is held."), Key.GetDisplayName());
		}
		return FText::GetEmpty();
	}
}

// =====================================================================================================
// Widget state providers
// =====================================================================================================

namespace WidgetAnimMachineWidgetStates
{
	namespace
	{
		struct FRegisteredProvider
		{
			int32 Handle = INDEX_NONE;
			FProvider Provider;
		};

		TArray<FRegisteredProvider>& GetProviders()
		{
			static TArray<FRegisteredProvider> Providers;
			return Providers;
		}
	}

	int32 RegisterProvider(FProvider Provider)
	{
		static int32 NextHandle = 1;
		FRegisteredProvider& Entry = GetProviders().AddDefaulted_GetRef();
		Entry.Handle = NextHandle++;
		Entry.Provider = MoveTemp(Provider);
		return Entry.Handle;
	}

	void UnregisterProvider(int32 Handle)
	{
		GetProviders().RemoveAll([Handle](const FRegisteredProvider& Entry) { return Entry.Handle == Handle; });
	}

	bool ReadFromProviders(UWidget* Widget, FWidgetAnimMachineWidgetState& OutState)
	{
		if (!Widget)
		{
			return false;
		}
		for (const FRegisteredProvider& Entry : GetProviders())
		{
			if (Entry.Provider && Entry.Provider(Widget, OutState))
			{
				return true;
			}
		}
		return false;
	}

	namespace
	{
		struct FRegisteredSelectedProvider
		{
			int32 Handle = INDEX_NONE;
			FSelectedProvider Provider;
		};

		TArray<FRegisteredSelectedProvider>& GetSelectedProviders()
		{
			static TArray<FRegisteredSelectedProvider> Providers;
			return Providers;
		}
	}

	int32 RegisterSelectedProvider(FSelectedProvider Provider)
	{
		static int32 NextHandle = 1;
		FRegisteredSelectedProvider& Entry = GetSelectedProviders().AddDefaulted_GetRef();
		Entry.Handle = NextHandle++;
		Entry.Provider = MoveTemp(Provider);
		return Entry.Handle;
	}

	void UnregisterSelectedProvider(int32 Handle)
	{
		GetSelectedProviders().RemoveAll([Handle](const FRegisteredSelectedProvider& Entry) { return Entry.Handle == Handle; });
	}

	bool IsSelectedByContainer(UWidget* Widget)
	{
		if (!Widget)
		{
			return false;
		}
		for (const FRegisteredSelectedProvider& Entry : GetSelectedProviders())
		{
			if (Entry.Provider && Entry.Provider(Widget))
			{
				return true;
			}
		}
		return false;
	}
}

// =====================================================================================================
// Events
// =====================================================================================================

namespace WidgetAnimMachineEvents
{
	namespace
	{
		/** A name's words, each capitalized, joined by one space: "pressed   twice" reads Pressed Twice. */
		FString ToWords(const FString& Source)
		{
			TArray<FString> Words;
			FString Word;
			for (const TCHAR Char : Source)
			{
				if (FChar::IsAlnum(Char) || Char == TEXT('_'))
				{
					Word.AppendChar(Word.IsEmpty() ? FChar::ToUpper(Char) : Char);
				}
				else if (!Word.IsEmpty())
				{
					Words.Add(MoveTemp(Word));
					Word.Reset();
				}
			}
			if (!Word.IsEmpty())
			{
				Words.Add(MoveTemp(Word));
			}
			return FString::Join(Words, TEXT(" "));
		}
	}

	FName GetStateEventFunctionName(const UWidgetAnimStateMachineAsset* Machine, FName State, bool bEntered)
	{
		const FString StateWords = State.IsNone() ? FString() : ToWords(State.ToString());
		if (StateWords.IsEmpty())
		{
			return NAME_None;
		}
		const FString MachineWords = Machine ? ToWords(Machine->GetMachineDisplayName()) : FString(TEXT("State Machine"));
		return FName(*FString::Printf(TEXT("On %s %s %s"), *MachineWords, bEntered ? TEXT("Entered") : TEXT("Exited"), *StateWords));
	}

	FName GetActionEventFunctionName(const UWidgetAnimStateMachineAsset* Machine, FName EventName)
	{
		const FString EventWords = EventName.IsNone() ? FString() : ToWords(EventName.ToString());
		if (EventWords.IsEmpty())
		{
			return NAME_None;
		}
		const FString MachineWords = Machine ? ToWords(Machine->GetMachineDisplayName()) : FString(TEXT("State Machine"));
		return FName(*FString::Printf(TEXT("On %s %s"), *MachineWords, *EventWords));
	}
}

// =====================================================================================================
// FWidgetAnimStateMachineEntry
// =====================================================================================================

bool FWidgetAnimStateMachineEntry::SyncToMachine(const TArray<FString>* TargetOptions)
{
	bool bChanged = false;

	// Not while the machine or its animations are still loading (an async load, a component post-loaded first): their
	// targets and inputs would read as none, and wipe what was picked for them.
	if (StateMachine)
	{
		if (StateMachine->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad))
		{
			return false;
		}
		TArray<UWidgetAnimPresetAsset*> Animations;
		StateMachine->GatherAnimations(Animations);
		for (const UWidgetAnimPresetAsset* Animation : Animations)
		{
			if (Animation && Animation->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad))
			{
				return false;
			}
		}
	}

	TArray<FName> TargetNames;
	if (StateMachine)
	{
		StateMachine->GetTargetNames(TargetNames);
	}

	// Only worth checking a target against choices that list more than (This Widget) - see FWidgetAnimEntry::SyncGroupTargets.
	const bool bCanCheck = TargetOptions && TargetOptions->Num() > 1;
	auto IsOffered = [TargetOptions](FName Name)
	{
		const FString NameString = Name.ToString();
		return TargetOptions->ContainsByPredicate([&NameString](const FString& Option) { return Option.Equals(NameString, ESearchCase::IgnoreCase); });
	};

	// Each target's animations - those with a Track Group of its name - for its row to show.
	TMap<FName, TArray<FString>> AnimationsByTarget;
	if (StateMachine)
	{
		TArray<UWidgetAnimPresetAsset*> Animations;
		StateMachine->GatherAnimations(Animations);
		for (const UWidgetAnimPresetAsset* Animation : Animations)
		{
			const int32 NumGroups = Animation ? Animation->LayerGroups.Num() : 0;
			for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
			{
				AnimationsByTarget.FindOrAdd(FName(*Animation->LayerGroups[GroupIndex].GetDisplayName(GroupIndex))).AddUnique(Animation->GetName());
			}
		}
	}

	// Rows by target name, so a target that moved in the list keeps its widget.
	TArray<FWidgetAnimGroupTarget> NewTargets;
	NewTargets.Reserve(TargetNames.Num());
	for (const FName TargetName : TargetNames)
	{
		const FString Label = TargetName.ToString();
		const FWidgetAnimGroupTarget* Existing = Targets.FindByPredicate([&Label](const FWidgetAnimGroupTarget& Row) { return Row.GroupName == Label; });
		// A row kept from before groups were Track Groups ("Layer Group 1") keeps its widget, and is relabeled.
		if (!Existing)
		{
			Existing = Targets.FindByPredicate([&Label](const FWidgetAnimGroupTarget& Row) { return FWidgetAnimLayerGroup::IsSameTargetName(Row.GroupName, Label); });
		}

		FWidgetAnimGroupTarget& Row = NewTargets.AddDefaulted_GetRef();
		Row.GroupName = Label;
		Row.bListsAnimations = true;
		if (const TArray<FString>* Listed = AnimationsByTarget.Find(TargetName))
		{
			Row.Animations = FString::Join(*Listed, TEXT(", "));
		}
		if (Existing)
		{
			Row.TargetWidgetName = Existing->TargetWidgetName;
		}
		else
		{
			FName Start = StateMachine ? StateMachine->GetTargetDefault(TargetName) : FName();
			if (bCanCheck && !FWidgetAnimGroupTarget::IsThisWidget(Start) && !IsOffered(Start))
			{
				Start = NAME_None;
			}
			Row.TargetWidgetName = Start;
		}
		if (TargetOptions && Row.TargetWidgetName.IsNone())
		{
			Row.TargetWidgetName = FWidgetAnimGroupTarget::ThisWidget;
		}
	}

	const bool bTargetsDiffer = NewTargets.Num() != Targets.Num() || [&]()
	{
		for (int32 Index = 0; Index < NewTargets.Num(); ++Index)
		{
			if (NewTargets[Index].GroupName != Targets[Index].GroupName || NewTargets[Index].TargetWidgetName != Targets[Index].TargetWidgetName
				|| NewTargets[Index].Animations != Targets[Index].Animations || NewTargets[Index].bListsAnimations != Targets[Index].bListsAnimations)
			{
				return true;
			}
		}
		return false;
	}();
	if (bTargetsDiffer)
	{
		Targets = MoveTemp(NewTargets);
		bChanged = true;
	}

	// Inputs: one row per input, by name, keeping what was overridden.
	TArray<FWidgetAnimMachineInputOverride> NewInputs;
	if (StateMachine)
	{
		for (const FWidgetAnimMachineInput& Input : StateMachine->Inputs)
		{
			const FWidgetAnimMachineInputOverride* Existing = InputOverrides.FindByPredicate([&Input](const FWidgetAnimMachineInputOverride& Row) { return Row.Input == Input.Name; });
			FWidgetAnimMachineInputOverride& Row = NewInputs.AddDefaulted_GetRef();
			if (Existing && Existing->bOverride)
			{
				Row = *Existing;
			}
			else
			{
				// Not overridden: the machine's own settings, so the row always shows what will be used.
				Row.bDefaultBool = Input.bDefaultBool;
				Row.DefaultNumber = Input.DefaultNumber;
				Row.HoldTime = Input.HoldTime;
				Row.DefaultInteger = Input.DefaultInteger;
				Row.DefaultVector2D = Input.DefaultVector2D;
				Row.Source = Input.Source;
			}
			Row.Input = Input.Name;
			Row.Type = Input.Type;
		}
	}

	const bool bInputsDiffer = NewInputs.Num() != InputOverrides.Num() || [&]()
	{
		for (int32 Index = 0; Index < NewInputs.Num(); ++Index)
		{
			const FWidgetAnimMachineInputOverride& A = NewInputs[Index];
			const FWidgetAnimMachineInputOverride& B = InputOverrides[Index];
			if (A.Input != B.Input || A.Type != B.Type || A.bOverride != B.bOverride || A.bDefaultBool != B.bDefaultBool || A.DefaultNumber != B.DefaultNumber
				|| A.HoldTime != B.HoldTime || A.DefaultInteger != B.DefaultInteger || A.DefaultVector2D != B.DefaultVector2D || A.MeasuredOnWidget != B.MeasuredOnWidget || !(A.Source == B.Source))
			{
				return true;
			}
		}
		return false;
	}();
	if (bInputsDiffer)
	{
		InputOverrides = MoveTemp(NewInputs);
		bChanged = true;
	}

	return bChanged;
}

FName FWidgetAnimStateMachineEntry::GetTargetPath(FName TargetName) const
{
	const FString Label = TargetName.ToString();
	const FWidgetAnimGroupTarget* Row = Targets.FindByPredicate([&Label](const FWidgetAnimGroupTarget& Candidate) { return Candidate.GroupName == Label; });
	if (!Row)
	{
		Row = Targets.FindByPredicate([&Label](const FWidgetAnimGroupTarget& Candidate) { return FWidgetAnimLayerGroup::IsSameTargetName(Candidate.GroupName, Label); });
	}
	if (Row)
	{
		return FWidgetAnimGroupTarget::IsThisWidget(Row->TargetWidgetName) ? FName(NAME_None) : Row->TargetWidgetName;
	}

	// A target the machine gained since this use was last synced: where a new row would start.
	return StateMachine ? StateMachine->GetTargetDefault(TargetName) : FName();
}

const FWidgetAnimMachineInputOverride* FWidgetAnimStateMachineEntry::FindInputOverride(FName InputName) const
{
	return InputOverrides.FindByPredicate([InputName](const FWidgetAnimMachineInputOverride& Row) { return Row.bOverride && Row.Input == InputName; });
}

#undef LOCTEXT_NAMESPACE
