// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimStateMachine.h"

#include "Components/Widget.h"
#include "WidgetAnimLibrary.h"
#include "WidgetAnimPreset.h"
#include "WidgetAnimSpring.h"

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

int32 FWidgetAnimMachineLayer::FindSpecialIndex(EWidgetAnimMachineNodeKind Kind) const
{
	return States.IndexOfByPredicate([Kind](const FWidgetAnimMachineState& State) { return State.Kind == Kind; });
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
			if (State.Animation && State.Kind == EWidgetAnimMachineNodeKind::State)
			{
				OutAnimations.AddUnique(State.Animation);
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

	for (const FWidgetAnimMachineLayer& Layer : Layers)
	{
		if (Layer.WeightInput == InputName)
		{
			return true;
		}
		for (const FWidgetAnimMachineState& State : Layer.States)
		{
			if (State.ProgressInput == InputName)
			{
				return true;
			}
		}
		for (const FWidgetAnimMachineTransition& Transition : Layer.Transitions)
		{
			if (Transition.Conditions.ContainsByPredicate([InputName](const FWidgetAnimMachineCondition& Condition) { return Condition.Input == InputName; }))
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

		// Its special nodes: one each, made if missing - a layer added in the Details comes with none - and extra ones
		// dropped, a duplicated node never being anything but a mistake.
		const FWidgetAnimMachineLayer Fresh = MakeLayer(Layer.Name);
		for (const EWidgetAnimMachineNodeKind Kind : { EWidgetAnimMachineNodeKind::Entry, EWidgetAnimMachineNodeKind::AnyState, EWidgetAnimMachineNodeKind::Exit })
		{
			int32 Found = INDEX_NONE;
			for (int32 Index = Layer.States.Num() - 1; Index >= 0; --Index)
			{
				if (Layer.States[Index].Kind != Kind)
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
				Layer.States.Add(Fresh.States[Fresh.FindSpecialIndex(Kind)]);
				bChanged = true;
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
				|| Transition.From == Transition.To;
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
		// Comment boxes: an id each, as the graph knows them by.
		TSet<FGuid> SeenComments;
		for (FWidgetAnimMachineComment& Comment : Layer.Comments)
		{
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

	const TArray<FName>& GetBuiltIns()
	{
		static const TArray<FName> BuiltIns = { Hovered, Pressed, Focused, Selected, Disabled, Closing };
		return BuiltIns;
	}

	uint8 GetBuiltInBit(FName Name)
	{
		const int32 Index = GetBuiltIns().IndexOfByKey(Name);
		return Index == INDEX_NONE ? 0 : static_cast<uint8>(1u << Index);
	}

	bool IsBuiltIn(FName Name)
	{
		return GetBuiltInBit(Name) != 0;
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
			return LOCTEXT("SelectedBuiltIn", "Built in: a Common UI button is selected, or a Check Box is checked.");
		}
		if (Name == Disabled)
		{
			return LOCTEXT("DisabledBuiltIn", "Built in: the widget is disabled - itself, or one around it.");
		}
		if (Name == Closing)
		{
			return LOCTEXT("ClosingBuiltIn", "Built in: the widget is playing out - Play Out, a screen closing, or hidden with Set Visibility Animated. False again as it plays in. What transitions to Exit test.");
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

	// Rows by target name, so a target that moved in the list keeps its widget.
	TArray<FWidgetAnimGroupTarget> NewTargets;
	NewTargets.Reserve(TargetNames.Num());
	for (const FName TargetName : TargetNames)
	{
		const FString Label = TargetName.ToString();
		const FWidgetAnimGroupTarget* Existing = Targets.FindByPredicate([&Label](const FWidgetAnimGroupTarget& Row) { return Row.GroupName == Label; });

		FWidgetAnimGroupTarget& Row = NewTargets.AddDefaulted_GetRef();
		Row.GroupName = Label;
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
			if (NewTargets[Index].GroupName != Targets[Index].GroupName || NewTargets[Index].TargetWidgetName != Targets[Index].TargetWidgetName)
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
				|| A.HoldTime != B.HoldTime || A.MeasuredOnWidget != B.MeasuredOnWidget || !(A.Source == B.Source))
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
	if (const FWidgetAnimGroupTarget* Row = Targets.FindByPredicate([&Label](const FWidgetAnimGroupTarget& Candidate) { return Candidate.GroupName == Label; }))
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
