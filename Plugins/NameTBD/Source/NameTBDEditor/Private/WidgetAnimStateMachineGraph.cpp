// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimStateMachineGraph.h"

#include "ConnectionDrawingPolicy.h"
#include "EdGraph/EdGraphPin.h"
#include "Framework/Commands/GenericCommands.h"
#include "NameTBD/WidgetAnimation/WidgetAnimCue.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBDColors.h"
#include "NameTBDTooltips.h"
#include "Rendering/DrawElements.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"
#include "SGraphPin.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimStateMachineGraph"

namespace MachineColors = NameTBD::Colors::StateMachineEditor;
namespace MachineTips = NameTBD::Tooltips::StateMachineEditor;

const FName UWidgetAnimMachineGraphSchema::PC_Transition(TEXT("Transition"));

namespace
{
	/** A brush from the app style, or the first of the fallbacks it has - see PickBrush in WidgetAnimBuilderToolbar.cpp. */
	const FSlateBrush* FindBrush(std::initializer_list<FName> Candidates)
	{
		const ISlateStyle& Style = FAppStyle::Get();
		for (const FName Candidate : Candidates)
		{
			if (const FSlateBrush* Brush = Style.GetOptionalBrush(Candidate, nullptr, nullptr))
			{
				return Brush;
			}
		}
		return FAppStyle::GetBrush(TEXT("WhiteBrush"));
	}

	/** Finishes an edit: names and ids made right, the machine marked changed, and everyone told. */
	void FinishEdit(UWidgetAnimStateMachineAsset* Machine)
	{
		Machine->Normalize();
		Machine->MarkPackageDirty();
		Machine->NotifyMachineChanged();
	}

	FWidgetAnimMachineLayer* GetLayerForEdit(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex)
	{
		return Machine && Machine->Layers.IsValidIndex(LayerIndex) ? &Machine->Layers[LayerIndex] : nullptr;
	}

	FText DescribeCondition(const FWidgetAnimMachineCondition& Condition)
	{
		const FText InputName = Condition.Input.IsNone() ? LOCTEXT("NoInput", "(no input)") : FText::FromName(Condition.Input);
		switch (Condition.Compare)
		{
		case EWidgetAnimMachineCompare::IsTrue:  return FText::Format(LOCTEXT("CondTrue", "{0}"), InputName);
		case EWidgetAnimMachineCompare::IsFalse: return FText::Format(LOCTEXT("CondFalse", "not {0}"), InputName);
		case EWidgetAnimMachineCompare::Fired:   return FText::Format(LOCTEXT("CondFired", "{0} fired"), InputName);
		default:
			return FText::Format(LOCTEXT("CondCompare", "{0} {1} {2}"), InputName,
				StaticEnum<EWidgetAnimMachineCompare>()->GetDisplayNameTextByValue(static_cast<int64>(Condition.Compare)),
				FText::AsNumber(Condition.Value));
		}
	}
}

// =====================================================================================================
// Edits
// =====================================================================================================

namespace WidgetAnimMachineEdits
{
	FGuid AddState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, FName Name, const FVector2D& Position, UWidgetAnimPresetAsset* Animation)
	{
		if (!GetLayerForEdit(Machine, LayerIndex))
		{
			return FGuid();
		}

		const FScopedTransaction Transaction(LOCTEXT("AddState", "Add State"));
		Machine->Modify();
		FWidgetAnimMachineLayer& Layer = Machine->Layers[LayerIndex];

		FWidgetAnimMachineState& State = Layer.States.AddDefaulted_GetRef();
		State.Id = FGuid::NewGuid();
		State.Kind = EWidgetAnimMachineNodeKind::State;
		State.Animation = Animation;
		// Named for its animation - WA_Hover makes Hover - unless a name is given.
		const FName Wanted = !Name.IsNone() ? Name : (Animation ? FName(*WidgetAnimCues::GetEventAnimationName(Animation)) : FName(TEXT("State")));
		State.Name = UWidgetAnimStateMachineAsset::MakeUniqueStateName(Layer, Wanted, State.Id);
		State.EditorPosition = Position;
		const FGuid Id = State.Id;

		// The layer's first state is where Entry goes, so a new machine plays something as soon as it has a state.
		const int32 EntryIndex = Layer.FindSpecialIndex(EWidgetAnimMachineNodeKind::Entry);
		const bool bEntryGoesNowhere = Layer.States.IsValidIndex(EntryIndex) && !Layer.Transitions.ContainsByPredicate([&Layer, EntryIndex](const FWidgetAnimMachineTransition& Transition)
		{
			return Transition.From == Layer.States[EntryIndex].Id;
		});
		if (bEntryGoesNowhere)
		{
			FWidgetAnimMachineTransition& FromEntry = Layer.Transitions.AddDefaulted_GetRef();
			FromEntry.Id = FGuid::NewGuid();
			FromEntry.From = Layer.States[EntryIndex].Id;
			FromEntry.To = Id;
			FromEntry.Blend = EWidgetAnimMachineBlend::Cut;
		}

		FinishEdit(Machine);
		return Id;
	}

	bool CanConnect(const UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& From, const FGuid& To)
	{
		if (!Machine || !Machine->Layers.IsValidIndex(LayerIndex) || From == To)
		{
			return false;
		}
		const FWidgetAnimMachineLayer& Layer = Machine->Layers[LayerIndex];
		const FWidgetAnimMachineState* FromState = Layer.FindState(From);
		const FWidgetAnimMachineState* ToState = Layer.FindState(To);
		return FromState && ToState
			&& FromState->Kind != EWidgetAnimMachineNodeKind::Exit
			&& ToState->Kind != EWidgetAnimMachineNodeKind::Entry
			&& ToState->Kind != EWidgetAnimMachineNodeKind::AnyState;
	}

	FGuid AddTransition(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& From, const FGuid& To)
	{
		if (!CanConnect(Machine, LayerIndex, From, To))
		{
			return FGuid();
		}

		const FScopedTransaction Transaction(LOCTEXT("AddTransition", "Add Transition"));
		Machine->Modify();
		FWidgetAnimMachineLayer& Layer = Machine->Layers[LayerIndex];
		const FWidgetAnimMachineState* FromState = Layer.FindState(From);
		const FWidgetAnimMachineState* ToState = Layer.FindState(To);
		const bool bFromEntry = FromState && FromState->Kind == EWidgetAnimMachineNodeKind::Entry;
		const bool bToExit = ToState && ToState->Kind == EWidgetAnimMachineNodeKind::Exit;

		FWidgetAnimMachineTransition& Transition = Layer.Transitions.AddDefaulted_GetRef();
		Transition.Id = FGuid::NewGuid();
		Transition.From = From;
		Transition.To = To;

		// Set up for where it goes: Entry cuts straight to the first state; the way to Exit is taken as the widget closes.
		if (bFromEntry)
		{
			Transition.Blend = EWidgetAnimMachineBlend::Cut;
		}
		if (bToExit)
		{
			FWidgetAnimMachineCondition& Closing = Transition.Conditions.AddDefaulted_GetRef();
			Closing.Input = WidgetAnimMachineInputs::Closing;
			Closing.Compare = EWidgetAnimMachineCompare::IsTrue;
		}
		const FGuid Id = Transition.Id;

		FinishEdit(Machine);
		return Id;
	}

	void Delete(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& States, const TArray<FGuid>& Transitions)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || (States.Num() == 0 && Transitions.Num() == 0))
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("DeleteNodes", "Delete From State Machine"));
		Machine->Modify();
		Layer->States.RemoveAll([&States](const FWidgetAnimMachineState& State) { return State.Kind == EWidgetAnimMachineNodeKind::State && States.Contains(State.Id); });
		Layer->Transitions.RemoveAll([&Transitions](const FWidgetAnimMachineTransition& Transition) { return Transitions.Contains(Transition.Id); });

		// Normalize drops the transitions whose state went.
		FinishEdit(Machine);
	}

	void RenameState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& State, const FString& NewName)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		const int32 Index = Layer ? Layer->FindStateIndex(State) : INDEX_NONE;
		const FString Trimmed = NewName.TrimStartAndEnd();
		if (Index == INDEX_NONE || Trimmed.IsEmpty() || Layer->States[Index].Name == FName(*Trimmed))
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("RenameState", "Rename State"));
		Machine->Modify();
		Layer->States[Index].Name = UWidgetAnimStateMachineAsset::MakeUniqueStateName(*Layer, FName(*Trimmed), State);
		FinishEdit(Machine);
	}

	void MoveStates(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TMap<FGuid, FVector2D>& Positions)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || Positions.Num() == 0)
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("MoveStates", "Move State"));
		Machine->Modify();
		for (FWidgetAnimMachineState& State : Layer->States)
		{
			if (const FVector2D* Position = Positions.Find(State.Id))
			{
				State.EditorPosition = *Position;
			}
		}
		Machine->MarkPackageDirty();

		// Positions are the graph's own: nothing else needs to hear of them, and the graph already shows them.
	}

	void SetEntryState(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& State)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		const int32 EntryIndex = Layer ? Layer->FindSpecialIndex(EWidgetAnimMachineNodeKind::Entry) : INDEX_NONE;
		if (EntryIndex == INDEX_NONE || !Layer->FindState(State))
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("SetEntryState", "Set Entry State"));
		Machine->Modify();
		const FGuid EntryId = Layer->States[EntryIndex].Id;
		FWidgetAnimMachineTransition* First = Layer->Transitions.FindByPredicate([&EntryId](const FWidgetAnimMachineTransition& Transition) { return Transition.From == EntryId; });
		if (First)
		{
			First->To = State;
		}
		else
		{
			FWidgetAnimMachineTransition& Transition = Layer->Transitions.AddDefaulted_GetRef();
			Transition.Id = FGuid::NewGuid();
			Transition.From = EntryId;
			Transition.To = State;
			Transition.Blend = EWidgetAnimMachineBlend::Cut;
		}
		FinishEdit(Machine);
	}

	void MoveTransitionPriority(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& TransitionId, int32 Delta)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		const int32 Index = Layer ? Layer->FindTransitionIndex(TransitionId) : INDEX_NONE;
		if (Index == INDEX_NONE || Delta == 0)
		{
			return;
		}

		// Priority is among the transitions leaving the same node: swapped with the next one of those, either way.
		const FGuid From = Layer->Transitions[Index].From;
		int32 Other = Index;
		do
		{
			Other += Delta > 0 ? 1 : -1;
		}
		while (Layer->Transitions.IsValidIndex(Other) && Layer->Transitions[Other].From != From);

		if (!Layer->Transitions.IsValidIndex(Other))
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("MoveTransitionPriority", "Change Transition Priority"));
		Machine->Modify();
		Layer->Transitions.Swap(Index, Other);
		FinishEdit(Machine);
	}

	int32 AddLayer(UWidgetAnimStateMachineAsset* Machine)
	{
		if (!Machine)
		{
			return INDEX_NONE;
		}
		const FScopedTransaction Transaction(LOCTEXT("AddLayer", "Add State Machine Layer"));
		Machine->Modify();
		const int32 Index = Machine->Layers.Add(UWidgetAnimStateMachineAsset::MakeLayer(Machine->MakeUniqueLayerName(TEXT("Layer"))));
		FinishEdit(Machine);
		return Index;
	}

	void RemoveLayer(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex)
	{
		if (!Machine || !Machine->Layers.IsValidIndex(LayerIndex) || Machine->Layers.Num() <= 1)
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("RemoveLayer", "Remove State Machine Layer"));
		Machine->Modify();
		Machine->Layers.RemoveAt(LayerIndex);
		FinishEdit(Machine);
	}

	FName AddInput(UWidgetAnimStateMachineAsset* Machine, EWidgetAnimMachineInputType Type)
	{
		if (!Machine)
		{
			return NAME_None;
		}
		const FScopedTransaction Transaction(LOCTEXT("AddInput", "Add State Machine Input"));
		Machine->Modify();
		FWidgetAnimMachineInput& Input = Machine->Inputs.AddDefaulted_GetRef();
		Input.Type = Type;
		const TCHAR* Base = Type == EWidgetAnimMachineInputType::Number ? TEXT("Number") : Type == EWidgetAnimMachineInputType::Trigger ? TEXT("Trigger") : TEXT("Bool");
		Input.Name = Machine->MakeUniqueInputName(FName(Base), Machine->Inputs.Num() - 1);
		const FName Name = Input.Name;
		FinishEdit(Machine);
		return Name;
	}

	void RemoveInput(UWidgetAnimStateMachineAsset* Machine, FName Input)
	{
		if (!Machine || !Machine->FindInput(Input))
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("RemoveInput", "Remove State Machine Input"));
		Machine->Modify();
		Machine->Inputs.RemoveAll([Input](const FWidgetAnimMachineInput& Candidate) { return Candidate.Name == Input; });
		FinishEdit(Machine);
	}

	void SetUpNewMachine(UWidgetAnimStateMachineAsset* Machine)
	{
		if (!Machine)
		{
			return;
		}
		Machine->Layers.Reset();
		Machine->Layers.Add(UWidgetAnimStateMachineAsset::MakeLayer(TEXT("Base")));
		FWidgetAnimMachineLayer& Layer = Machine->Layers[0];

		FWidgetAnimMachineState& Idle = Layer.States.AddDefaulted_GetRef();
		Idle.Id = FGuid::NewGuid();
		Idle.Name = TEXT("Idle");
		Idle.EditorPosition = FVector2D(0.0, 0.0);

		const int32 EntryIndex = Layer.FindSpecialIndex(EWidgetAnimMachineNodeKind::Entry);
		FWidgetAnimMachineTransition& FromEntry = Layer.Transitions.AddDefaulted_GetRef();
		FromEntry.Id = FGuid::NewGuid();
		FromEntry.From = Layer.States[EntryIndex].Id;
		FromEntry.To = Idle.Id;
		FromEntry.Blend = EWidgetAnimMachineBlend::Cut;

		Machine->Normalize();
	}
}

// =====================================================================================================
// UWidgetAnimMachineGraph
// =====================================================================================================

const FWidgetAnimMachineLayer* UWidgetAnimMachineGraph::GetLayer() const
{
	return Machine && Machine->Layers.IsValidIndex(LayerIndex) ? &Machine->Layers[LayerIndex] : nullptr;
}

UWidgetAnimMachineGraphNode* UWidgetAnimMachineGraph::FindStateNode(const FGuid& StateId) const
{
	for (UEdGraphNode* Node : Nodes)
	{
		UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Node);
		if (StateNode && StateNode->StateId == StateId)
		{
			return StateNode;
		}
	}
	return nullptr;
}

bool UWidgetAnimMachineGraph::IsDebugging() const
{
	FWidgetAnimMachineLayerDebug Debug;
	return GetDebug && GetDebug(Debug);
}

void UWidgetAnimMachineGraph::RebuildFromMachine()
{
	// Every node made again: the asset is the truth, and a graph this size is quicker to make than to reconcile.
	const TArray<UEdGraphNode*> OldNodes = Nodes;
	for (UEdGraphNode* Node : OldNodes)
	{
		RemoveNode(Node);
	}

	const FWidgetAnimMachineLayer* Layer = GetLayer();
	if (!Layer)
	{
		NotifyGraphChanged();
		return;
	}

	for (const FWidgetAnimMachineState& State : Layer->States)
	{
		UWidgetAnimMachineGraphNode* Node = NewObject<UWidgetAnimMachineGraphNode>(this, NAME_None, RF_Transient);
		Node->CreateNewGuid();
		Node->StateId = State.Id;
		Node->Kind = State.Kind;
		Node->NodePosX = FMath::RoundToInt32(State.EditorPosition.X);
		Node->NodePosY = FMath::RoundToInt32(State.EditorPosition.Y);
		Node->bCanRenameNode = State.Kind == EWidgetAnimMachineNodeKind::State;
		Node->AllocateDefaultPins();
		AddNode(Node, /*bUserAction=*/false, /*bSelectNewNode=*/false);
	}

	for (const FWidgetAnimMachineTransition& Transition : Layer->Transitions)
	{
		UWidgetAnimMachineGraphNode* From = FindStateNode(Transition.From);
		UWidgetAnimMachineGraphNode* To = FindStateNode(Transition.To);
		if (!From || !To)
		{
			continue;
		}

		UWidgetAnimMachineTransitionNode* Node = NewObject<UWidgetAnimMachineTransitionNode>(this, NAME_None, RF_Transient);
		Node->CreateNewGuid();
		Node->TransitionId = Transition.Id;
		Node->FromId = Transition.From;
		Node->ToId = Transition.To;
		// Halfway, until its widget places it exactly (see SWidgetAnimMachineTransitionNode::PerformSecondPassLayout).
		Node->NodePosX = (From->NodePosX + To->NodePosX) / 2;
		Node->NodePosY = (From->NodePosY + To->NodePosY) / 2;
		Node->AllocateDefaultPins();
		AddNode(Node, /*bUserAction=*/false, /*bSelectNewNode=*/false);

		if (From->GetOutputPin() && Node->GetInputPin())
		{
			From->GetOutputPin()->MakeLinkTo(Node->GetInputPin());
		}
		if (Node->GetOutputPin() && To->GetInputPin())
		{
			Node->GetOutputPin()->MakeLinkTo(To->GetInputPin());
		}
	}

	NotifyGraphChanged();
}

// =====================================================================================================
// Nodes
// =====================================================================================================

UWidgetAnimMachineGraph* UWidgetAnimMachineGraphNode::GetMachineGraph() const
{
	return Cast<UWidgetAnimMachineGraph>(GetGraph());
}

const FWidgetAnimMachineState* UWidgetAnimMachineGraphNode::GetState() const
{
	const UWidgetAnimMachineGraph* Graph = GetMachineGraph();
	const FWidgetAnimMachineLayer* Layer = Graph ? Graph->GetLayer() : nullptr;
	return Layer ? Layer->FindState(StateId) : nullptr;
}

void UWidgetAnimMachineGraphNode::AllocateDefaultPins()
{
	// An input every node has but Entry and Any State, an output every node has but Exit; both hidden in their own way - the
	// output covers the node's edge, which is where a transition is dragged from.
	UEdGraphPin* In = CreatePin(EGPD_Input, UWidgetAnimMachineGraphSchema::PC_Transition, TEXT("In"));
	UEdGraphPin* Out = CreatePin(EGPD_Output, UWidgetAnimMachineGraphSchema::PC_Transition, TEXT("Out"));
	In->bHidden = Kind == EWidgetAnimMachineNodeKind::Entry || Kind == EWidgetAnimMachineNodeKind::AnyState;
	// Exit's edge too: nothing leaves Exit (CanConnect refuses it), but a transition has to be dropped onto it.
	Out->bHidden = false;
}

FText UWidgetAnimMachineGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const FWidgetAnimMachineState* State = GetState();
	return State ? FText::FromName(State->Name) : LOCTEXT("MissingState", "(gone)");
}

FText UWidgetAnimMachineGraphNode::GetTooltipText() const
{
	switch (Kind)
	{
	case EWidgetAnimMachineNodeKind::Entry:    return MachineTips::EntryNodeTooltip();
	case EWidgetAnimMachineNodeKind::Exit:     return MachineTips::ExitNodeTooltip();
	case EWidgetAnimMachineNodeKind::AnyState: return MachineTips::AnyStateNodeTooltip();
	default:
		break;
	}
	const FWidgetAnimMachineState* State = GetState();
	if (!State)
	{
		return FText::GetEmpty();
	}
	return FText::Format(MachineTips::StateNodeTooltip(),
		FText::FromName(State->Name),
		State->Animation ? FText::FromString(State->Animation->GetName()) : LOCTEXT("NoAnimation", "no animation"),
		StaticEnum<EWidgetAnimMachineStatePlayback>()->GetDisplayNameTextByValue(static_cast<int64>(State->Playback)));
}

void UWidgetAnimMachineGraphNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	FToolMenuSection& Section = Menu->AddSection(TEXT("WidgetAnimMachineState"), LOCTEXT("StateSection", "State"));
	if (Kind == EWidgetAnimMachineNodeKind::State)
	{
		const TWeakObjectPtr<const UWidgetAnimMachineGraphNode> WeakThis(this);
		Section.AddMenuEntry(
			TEXT("SetEntryState"),
			LOCTEXT("SetEntryStateMenu", "Set As Entry State"),
			MachineTips::SetEntryStateTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakThis]()
			{
				const UWidgetAnimMachineGraphNode* Node = WeakThis.Get();
				UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
				if (Graph)
				{
					WidgetAnimMachineEdits::SetEntryState(Graph->Machine, Graph->LayerIndex, Node->StateId);
				}
			})));
		Section.AddMenuEntry(
			TEXT("OpenStateAnimation"),
			LOCTEXT("OpenStateAnimation", "Open Animation"),
			MachineTips::OpenStateAnimationTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakThis]()
			{
				const UWidgetAnimMachineGraphNode* Node = WeakThis.Get();
				UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
				if (Graph && Graph->OnOpenState)
				{
					Graph->OnOpenState(Node->StateId);
				}
			})));
		Section.AddMenuEntry(FGenericCommands::Get().Rename);
		Section.AddMenuEntry(FGenericCommands::Get().Delete);
	}
}

UWidgetAnimMachineGraph* UWidgetAnimMachineTransitionNode::GetMachineGraph() const
{
	return Cast<UWidgetAnimMachineGraph>(GetGraph());
}

const FWidgetAnimMachineTransition* UWidgetAnimMachineTransitionNode::GetTransition() const
{
	const UWidgetAnimMachineGraph* Graph = GetMachineGraph();
	const FWidgetAnimMachineLayer* Layer = Graph ? Graph->GetLayer() : nullptr;
	const int32 Index = Layer ? Layer->FindTransitionIndex(TransitionId) : INDEX_NONE;
	return Index != INDEX_NONE ? &Layer->Transitions[Index] : nullptr;
}

void UWidgetAnimMachineTransitionNode::AllocateDefaultPins()
{
	UEdGraphPin* In = CreatePin(EGPD_Input, UWidgetAnimMachineGraphSchema::PC_Transition, TEXT("In"));
	UEdGraphPin* Out = CreatePin(EGPD_Output, UWidgetAnimMachineGraphSchema::PC_Transition, TEXT("Out"));
	In->bHidden = true;
	Out->bHidden = true;
}

FText UWidgetAnimMachineTransitionNode::DescribeRule() const
{
	const FWidgetAnimMachineTransition* Transition = GetTransition();
	if (!Transition)
	{
		return FText::GetEmpty();
	}

	TArray<FText> Lines;
	if (!Transition->bEnabled)
	{
		Lines.Add(LOCTEXT("Disabled", "Disabled"));
	}

	if (Transition->Conditions.Num() == 0)
	{
		Lines.Add(LOCTEXT("Always", "Always"));
	}
	else
	{
		TArray<FText> Conditions;
		for (const FWidgetAnimMachineCondition& Condition : Transition->Conditions)
		{
			Conditions.Add(DescribeCondition(Condition));
		}
		Lines.Add(FText::Join(LOCTEXT("And", " and "), Conditions));
	}

	switch (Transition->Blend)
	{
	case EWidgetAnimMachineBlend::Cut:
		Lines.Add(LOCTEXT("BlendCut", "Cut"));
		break;
	case EWidgetAnimMachineBlend::Spring:
		Lines.Add(FText::Format(LOCTEXT("BlendSpring", "Spring, about {0}s"), FText::AsNumber(Transition->Spring.VisualDuration)));
		break;
	case EWidgetAnimMachineBlend::Animation:
		Lines.Add(FText::Format(LOCTEXT("BlendAnimation", "Plays {0}"), Transition->TransitionAnimation ? FText::FromString(Transition->TransitionAnimation->GetName()) : LOCTEXT("NoTransitionAnimation", "(no animation)")));
		break;
	default:
		Lines.Add(FText::Format(LOCTEXT("BlendTween", "Tween {0}s"), FText::AsNumber(Transition->Duration)));
		break;
	}

	if (Transition->ExitTime == EWidgetAnimMachineExitTime::AtProgress)
	{
		Lines.Add(FText::Format(LOCTEXT("ExitAt", "After {0}% of the state"), FText::AsNumber(FMath::RoundToInt(Transition->ExitAtProgress * 100.f))));
	}
	else if (Transition->ExitTime == EWidgetAnimMachineExitTime::WhenFinished)
	{
		Lines.Add(LOCTEXT("ExitFinished", "Once the state has played"));
	}
	if (!Transition->bInterruptible)
	{
		Lines.Add(LOCTEXT("NotInterruptible", "Not interruptible"));
	}
	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

FText UWidgetAnimMachineTransitionNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const UWidgetAnimMachineGraph* Graph = GetMachineGraph();
	const FWidgetAnimMachineLayer* Layer = Graph ? Graph->GetLayer() : nullptr;
	const FWidgetAnimMachineState* From = Layer ? Layer->FindState(FromId) : nullptr;
	const FWidgetAnimMachineState* To = Layer ? Layer->FindState(ToId) : nullptr;
	return FText::Format(LOCTEXT("TransitionTitle", "{0} to {1}"),
		From ? FText::FromName(From->Name) : FText::GetEmpty(),
		To ? FText::FromName(To->Name) : FText::GetEmpty());
}

FText UWidgetAnimMachineTransitionNode::GetTooltipText() const
{
	return FText::Format(LOCTEXT("TransitionTooltip", "{0}\n{1}"), GetNodeTitle(ENodeTitleType::FullTitle), DescribeRule());
}

void UWidgetAnimMachineTransitionNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	FToolMenuSection& Section = Menu->AddSection(TEXT("WidgetAnimMachineTransition"), LOCTEXT("TransitionSection", "Transition"));
	const TWeakObjectPtr<const UWidgetAnimMachineTransitionNode> WeakThis(this);
	auto MovePriority = [WeakThis](int32 Delta)
	{
		const UWidgetAnimMachineTransitionNode* Node = WeakThis.Get();
		UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
		if (Graph)
		{
			WidgetAnimMachineEdits::MoveTransitionPriority(Graph->Machine, Graph->LayerIndex, Node->TransitionId, Delta);
		}
	};
	Section.AddMenuEntry(
		TEXT("RaisePriority"),
		LOCTEXT("RaisePriority", "Raise Priority"),
		MachineTips::RaisePriorityTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([MovePriority]() { MovePriority(-1); })));
	Section.AddMenuEntry(
		TEXT("LowerPriority"),
		LOCTEXT("LowerPriority", "Lower Priority"),
		MachineTips::LowerPriorityTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([MovePriority]() { MovePriority(1); })));
	Section.AddMenuEntry(FGenericCommands::Get().Delete);
}

// =====================================================================================================
// Schema
// =====================================================================================================

namespace
{
	/** Right-click > Add State: a state where the menu was opened - joined from the state a drag came from, if one did. */
	struct FWidgetAnimMachineAction_AddState : public FEdGraphSchemaAction
	{
		FWidgetAnimMachineAction_AddState()
			: FEdGraphSchemaAction(LOCTEXT("StatesCategory", "State Machine"), LOCTEXT("AddState", "Add State"), MachineTips::AddStateActionTooltip(), 0)
		{
		}

		UEdGraphNode* AddAt(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D& Location)
		{
			UWidgetAnimMachineGraph* Graph = Cast<UWidgetAnimMachineGraph>(ParentGraph);
			if (!Graph || !Graph->Machine)
			{
				return nullptr;
			}

			const FScopedTransaction Transaction(LOCTEXT("AddStateFromMenu", "Add State"));
			const FGuid StateId = WidgetAnimMachineEdits::AddState(Graph->Machine, Graph->LayerIndex, NAME_None, Location);
			if (const UWidgetAnimMachineGraphNode* FromNode = UWidgetAnimMachineGraphSchema::GetStateNode(FromPin))
			{
				WidgetAnimMachineEdits::AddTransition(Graph->Machine, Graph->LayerIndex, FromNode->StateId, StateId);
			}
			Graph->RequestRebuild();
			return nullptr;
		}

		// The location is a FVector2f since 5.6; the FVector2D form is deprecated.
		using FEdGraphSchemaAction::PerformAction;
		virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode = true) override
		{
			return AddAt(ParentGraph, FromPin, FVector2D(Location));
		}
	};
}

UWidgetAnimMachineGraphNode* UWidgetAnimMachineGraphSchema::GetStateNode(const UEdGraphPin* Pin)
{
	return Pin ? Cast<UWidgetAnimMachineGraphNode>(Pin->GetOwningNode()) : nullptr;
}

void UWidgetAnimMachineGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	// From a drag off Exit there is nowhere to go.
	const UWidgetAnimMachineGraphNode* FromNode = GetStateNode(ContextMenuBuilder.FromPin);
	if (FromNode && FromNode->Kind == EWidgetAnimMachineNodeKind::Exit)
	{
		return;
	}
	ContextMenuBuilder.AddAction(MakeShared<FWidgetAnimMachineAction_AddState>());
}

const FPinConnectionResponse UWidgetAnimMachineGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	const UWidgetAnimMachineGraphNode* NodeA = GetStateNode(A);
	const UWidgetAnimMachineGraphNode* NodeB = GetStateNode(B);
	if (!NodeA || !NodeB)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("OnlyStates", "Join two states"));
	}

	// Dragged onto a node anywhere - its edge is its output - the arrow goes from the one dragged from to the one dropped on.
	const UWidgetAnimMachineGraphNode* From = A->Direction == EGPD_Output ? NodeA : NodeB;
	const UWidgetAnimMachineGraphNode* To = From == NodeA ? NodeB : NodeA;
	const UWidgetAnimMachineGraph* Graph = Cast<UWidgetAnimMachineGraph>(From->GetGraph());
	if (!Graph || !WidgetAnimMachineEdits::CanConnect(Graph->Machine, Graph->LayerIndex, From->StateId, To->StateId))
	{
		if (To->Kind == EWidgetAnimMachineNodeKind::Entry || To->Kind == EWidgetAnimMachineNodeKind::AnyState)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("NotIntoSpecial", "Nothing goes into Entry or Any State"));
		}
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("CantJoin", "These two can't be joined"));
	}
	return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, LOCTEXT("AddTransitionResponse", "Add a transition"));
}

bool UWidgetAnimMachineGraphSchema::TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const
{
	if (CanCreateConnection(A, B).Response == CONNECT_RESPONSE_DISALLOW)
	{
		return false;
	}

	const UWidgetAnimMachineGraphNode* NodeA = GetStateNode(A);
	const UWidgetAnimMachineGraphNode* NodeB = GetStateNode(B);
	const UWidgetAnimMachineGraphNode* From = A->Direction == EGPD_Output ? NodeA : NodeB;
	const UWidgetAnimMachineGraphNode* To = From == NodeA ? NodeB : NodeA;
	UWidgetAnimMachineGraph* Graph = Cast<UWidgetAnimMachineGraph>(From->GetGraph());
	if (!Graph)
	{
		return false;
	}

	// Made in the machine; the graph shows it on the next tick, rebuilt - never under the drag that made it.
	WidgetAnimMachineEdits::AddTransition(Graph->Machine, Graph->LayerIndex, From->StateId, To->StateId);
	Graph->RequestRebuild();
	return true;
}

FLinearColor UWidgetAnimMachineGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return MachineColors::TransitionLine;
}

void UWidgetAnimMachineGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetContextMenuActions(Menu, Context);
}

// =====================================================================================================
// Widgets
// =====================================================================================================

/** A state's edge: what a transition is dragged from, and dropped on. Covers the whole node, lit when hovered. */
class SWidgetAnimMachineOutputPin : public SGraphPin
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineOutputPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin)
	{
		this->SetCursor(EMouseCursor::Default);
		bShowLabel = true;
		GraphPinObj = InPin;
		check(GraphPinObj != nullptr);

		SBorder::Construct(SBorder::FArguments()
			.BorderImage(this, &SWidgetAnimMachineOutputPin::GetPinBorder)
			.BorderBackgroundColor(this, &SWidgetAnimMachineOutputPin::GetPinBorderColor)
			.OnMouseButtonDown(this, &SWidgetAnimMachineOutputPin::OnPinMouseDown)
			.Cursor(this, &SWidgetAnimMachineOutputPin::GetPinCursor));
	}

protected:

	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		return SNew(STextBlock);
	}

	const FSlateBrush* GetPinBorder() const
	{
		return IsHovered()
			? FindBrush({ TEXT("Graph.StateNode.Pin.BackgroundHovered") })
			: FindBrush({ TEXT("Graph.StateNode.Pin.Background") });
	}

	FSlateColor GetPinBorderColor() const
	{
		return IsHovered() ? MachineColors::StateEdgeHovered : MachineColors::StateEdge;
	}
};

/** A state node: rounded, its name in the middle and its animation under it, lit while the widget is in it. */
class SWidgetAnimMachineStateNode : public SGraphNode
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineStateNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWidgetAnimMachineGraphNode* InNode)
	{
		GraphNode = InNode;
		SetCursor(EMouseCursor::CardinalCross);
		UpdateGraphNode();
	}

	virtual void UpdateGraphNode() override
	{
		InputPins.Empty();
		OutputPins.Empty();
		RightNodeBox.Reset();
		LeftNodeBox.Reset();

		this->ContentScale.Bind(this, &SGraphNode::GetContentScale);
		this->GetOrAddSlot(ENodeZone::Center)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FindBrush({ TEXT("Graph.StateNode.Body") }))
				.Padding(0.f)
				.BorderBackgroundColor(this, &SWidgetAnimMachineStateNode::GetBodyColor)
				.ToolTipText(this, &SWidgetAnimMachineStateNode::GetTooltip)
				[
					SNew(SOverlay)

					// The edge: where transitions are dragged from.
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SAssignNew(RightNodeBox, SVerticalBox)
					]

					// The name, and the animation under it.
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(10.f)
					[
						SNew(SBorder)
						.BorderImage(FindBrush({ TEXT("Graph.StateNode.ColorSpill") }))
						.BorderBackgroundColor(this, &SWidgetAnimMachineStateNode::GetTitleColor)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Visibility(EVisibility::SelfHitTestInvisible)
						.Padding(FMargin(8.f, 4.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.f, 0.f, 4.f, 0.f)
							[
								SNew(SImage)
								.Image(this, &SWidgetAnimMachineStateNode::GetIcon)
								.ColorAndOpacity(FSlateColor::UseForeground())
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SAssignNew(InlineEditableText, SInlineEditableTextBlock)
									.Style(FAppStyle::Get(), "Graph.StateNode.NodeTitleInlineEditableText")
									.Text(this, &SWidgetAnimMachineStateNode::GetName)
									.OnVerifyTextChanged(this, &SWidgetAnimMachineStateNode::OnVerifyNameTextChanged)
									.OnTextCommitted(this, &SWidgetAnimMachineStateNode::OnNameTextCommited)
									.IsReadOnly(this, &SWidgetAnimMachineStateNode::IsNameReadOnly)
									.IsSelected(this, &SWidgetAnimMachineStateNode::IsSelectedExclusively)
								]
								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(STextBlock)
									.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
									.ColorAndOpacity(FSlateColor::UseSubduedForeground())
									.Text(this, &SWidgetAnimMachineStateNode::GetSubtitle)
									.Visibility(this, &SWidgetAnimMachineStateNode::GetSubtitleVisibility)
								]
							]
						]
					]
				]
			];

		CreatePinWidgets();
	}

	virtual void CreatePinWidgets() override
	{
		UWidgetAnimMachineGraphNode* Node = CastChecked<UWidgetAnimMachineGraphNode>(GraphNode);
		UEdGraphPin* Out = Node->GetOutputPin();
		if (Out && !Out->bHidden)
		{
			AddPin(SNew(SWidgetAnimMachineOutputPin, Out));
		}
	}

	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override
	{
		PinToAdd->SetOwner(SharedThis(this));
		RightNodeBox->AddSlot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.FillHeight(1.f)
			[
				PinToAdd
			];
		OutputPins.Add(PinToAdd);
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override
	{
		UWidgetAnimMachineGraphNode* Node = Cast<UWidgetAnimMachineGraphNode>(GraphNode);
		UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
		if (Node && Graph && Graph->OnOpenState && Node->Kind == EWidgetAnimMachineNodeKind::State)
		{
			Graph->OnOpenState(Node->StateId);
			return FReply::Handled();
		}
		return SGraphNode::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
	}

private:

	const UWidgetAnimMachineGraphNode* GetMachineNode() const
	{
		return Cast<UWidgetAnimMachineGraphNode>(GraphNode);
	}

	/** What the watched widget's layer is doing, if anything is watched. */
	bool GetDebug(FWidgetAnimMachineLayerDebug& OutDebug) const
	{
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		const UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
		return Graph && Graph->GetDebug && Graph->GetDebug(OutDebug);
	}

	bool IsActive() const
	{
		FWidgetAnimMachineLayerDebug Debug;
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		return Node && GetDebug(Debug) && Debug.CurrentState == Node->StateId;
	}

	bool IsLeaving() const
	{
		FWidgetAnimMachineLayerDebug Debug;
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		return Node && GetDebug(Debug) && Debug.ActiveTransition.IsValid() && Debug.PreviousState == Node->StateId;
	}

	FSlateColor GetBodyColor() const
	{
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		if (!Node)
		{
			return MachineColors::StateBody;
		}
		if (IsActive())
		{
			return MachineColors::StateActive;
		}
		if (IsLeaving())
		{
			return MachineColors::StateLeaving;
		}
		switch (Node->Kind)
		{
		case EWidgetAnimMachineNodeKind::Entry:    return MachineColors::EntryBody;
		case EWidgetAnimMachineNodeKind::Exit:     return MachineColors::ExitBody;
		case EWidgetAnimMachineNodeKind::AnyState: return MachineColors::AnyStateBody;
		default:                                    return MachineColors::StateBody;
		}
	}

	FSlateColor GetTitleColor() const
	{
		return IsActive() ? MachineColors::StateTitleActive : MachineColors::StateTitle;
	}

	const FSlateBrush* GetIcon() const
	{
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		const EWidgetAnimMachineNodeKind Kind = Node ? Node->Kind : EWidgetAnimMachineNodeKind::State;
		switch (Kind)
		{
		case EWidgetAnimMachineNodeKind::Entry:    return FindBrush({ TEXT("Graph.EntryNode.Icon"), TEXT("Icons.ArrowRight"), TEXT("Graph.StateNode.Icon") });
		case EWidgetAnimMachineNodeKind::Exit:     return FindBrush({ TEXT("Icons.Stop"), TEXT("Graph.StateNode.Icon") });
		case EWidgetAnimMachineNodeKind::AnyState: return FindBrush({ TEXT("Graph.AliasNode.Icon"), TEXT("Graph.ConduitNode.Icon"), TEXT("Graph.StateNode.Icon") });
		default:                                    return FindBrush({ TEXT("Graph.StateNode.Icon") });
		}
	}

	FText GetName() const
	{
		return GraphNode ? GraphNode->GetNodeTitle(ENodeTitleType::FullTitle) : FText::GetEmpty();
	}

	FText GetTooltip() const
	{
		return GraphNode ? GraphNode->GetTooltipText() : FText::GetEmpty();
	}

	FText GetSubtitle() const
	{
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		const FWidgetAnimMachineState* State = Node ? Node->GetState() : nullptr;
		if (!State || State->Kind != EWidgetAnimMachineNodeKind::State)
		{
			return FText::GetEmpty();
		}
		if (!State->Animation)
		{
			return LOCTEXT("StateNoAnimation", "no animation");
		}
		const FText Playback = StaticEnum<EWidgetAnimMachineStatePlayback>()->GetDisplayNameTextByValue(static_cast<int64>(State->Playback));
		if (!State->ProgressInput.IsNone() && State->Animation->IsPercentTime())
		{
			return FText::Format(LOCTEXT("StateProgressSubtitle", "{0}, follows {1}"), FText::FromString(State->Animation->GetName()), FText::FromName(State->ProgressInput));
		}
		return FText::Format(LOCTEXT("StateSubtitle", "{0}, {1}"), FText::FromString(State->Animation->GetName()), Playback);
	}

	EVisibility GetSubtitleVisibility() const
	{
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		return Node && Node->Kind == EWidgetAnimMachineNodeKind::State ? EVisibility::Visible : EVisibility::Collapsed;
	}
};

/** A transition's rule icon, halfway along its arrow - see FWidgetAnimMachineConnectionDrawingPolicy, which draws the arrow. */
class SWidgetAnimMachineTransitionNode : public SGraphNode
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineTransitionNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWidgetAnimMachineTransitionNode* InNode)
	{
		GraphNode = InNode;
		UpdateGraphNode();
	}

	virtual void UpdateGraphNode() override
	{
		InputPins.Empty();
		OutputPins.Empty();
		RightNodeBox.Reset();
		LeftNodeBox.Reset();

		this->ContentScale.Bind(this, &SGraphNode::GetContentScale);
		this->GetOrAddSlot(ENodeZone::Center)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SOverlay)
				.ToolTipText(this, &SWidgetAnimMachineTransitionNode::GetTooltip)
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(FindBrush({ TEXT("Graph.TransitionNode.ColorSpill") }))
					.ColorAndOpacity(this, &SWidgetAnimMachineTransitionNode::GetColor)
				]
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(FindBrush({ TEXT("Graph.TransitionNode.Icon") }))
				]
			];
	}

	virtual void CreatePinWidgets() override
	{
		// Its pins are where the arrow goes in and out: never drawn, never dragged.
	}

	virtual bool RequiresSecondPassLayout() const override
	{
		return true;
	}

	/**
	 * Halfway between its two states - and to one side, when the two are joined both ways or more than once, so every icon
	 * of a pair sits on its own arrow.
	 */
	virtual void PerformSecondPassLayout(const TMap<UObject*, TSharedRef<SNodePanel::SNode>>& NodeToWidgetLookup) const override
	{
		const UWidgetAnimMachineTransitionNode* Node = Cast<UWidgetAnimMachineTransitionNode>(GraphNode);
		const UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
		const TSharedPtr<SGraphPanel> Panel = OwnerGraphPanelPtr.Pin();
		if (!Node || !Graph || !Panel.IsValid())
		{
			return;
		}

		const UWidgetAnimMachineGraphNode* From = Graph->FindStateNode(Node->FromId);
		const UWidgetAnimMachineGraphNode* To = Graph->FindStateNode(Node->ToId);
		const TSharedPtr<SGraphNode> FromWidget = From ? Panel->GetNodeWidgetFromGuid(From->NodeGuid) : nullptr;
		const TSharedPtr<SGraphNode> ToWidget = To ? Panel->GetNodeWidgetFromGuid(To->NodeGuid) : nullptr;
		if (!FromWidget.IsValid() || !ToWidget.IsValid())
		{
			return;
		}

		const FVector2D FromSize(FromWidget->GetDesiredSize());
		const FVector2D ToSize(ToWidget->GetDesiredSize());
		const FVector2D FromCenter = FVector2D(From->NodePosX, From->NodePosY) + FromSize * 0.5;
		const FVector2D ToCenter = FVector2D(To->NodePosX, To->NodePosY) + ToSize * 0.5;

		// Arrows between the same two nodes, whichever way: this one's place among them.
		int32 Place = 0;
		int32 Count = 0;
		for (const UEdGraphNode* Other : Graph->Nodes)
		{
			const UWidgetAnimMachineTransitionNode* Sibling = Cast<UWidgetAnimMachineTransitionNode>(Other);
			if (!Sibling)
			{
				continue;
			}
			const bool bSamePair = (Sibling->FromId == Node->FromId && Sibling->ToId == Node->ToId) || (Sibling->FromId == Node->ToId && Sibling->ToId == Node->FromId);
			if (!bSamePair)
			{
				continue;
			}
			if (Sibling == Node)
			{
				Place = Count;
			}
			++Count;
		}

		// One side for the pair, whichever way each arrow goes - measured from the same end every time - so arrows between the
		// same two states fan out side by side instead of landing on each other.
		const bool bForward = Node->FromId < Node->ToId;
		const FVector2D Delta = bForward ? ToCenter - FromCenter : FromCenter - ToCenter;
		const FVector2D Normal = FVector2D(Delta.Y, -Delta.X).GetSafeNormal();
		const double Offset = Count > 1 ? (Place - (Count - 1) * 0.5) * 18.0 : 0.0;

		const FVector2D MySize(GetDesiredSize());
		const FVector2D Position = (FromCenter + ToCenter) * 0.5 + Normal * Offset - MySize * 0.5;
		GraphNode->NodePosX = FMath::RoundToInt32(Position.X);
		GraphNode->NodePosY = FMath::RoundToInt32(Position.Y);
	}

private:

	FText GetTooltip() const
	{
		return GraphNode ? GraphNode->GetTooltipText() : FText::GetEmpty();
	}

	FSlateColor GetColor() const
	{
		const UWidgetAnimMachineTransitionNode* Node = Cast<UWidgetAnimMachineTransitionNode>(GraphNode);
		const UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
		FWidgetAnimMachineLayerDebug Debug;
		if (Graph && Graph->GetDebug && Graph->GetDebug(Debug) && Debug.ActiveTransition == Node->TransitionId)
		{
			return MachineColors::TransitionActive;
		}
		const FWidgetAnimMachineTransition* Transition = Node ? Node->GetTransition() : nullptr;
		if (Transition && !Transition->bEnabled)
		{
			return MachineColors::TransitionDisabled;
		}
		return MachineColors::TransitionIcon;
	}
};

TSharedPtr<SGraphNode> UWidgetAnimMachineGraphNode::CreateVisualWidget()
{
	return SNew(SWidgetAnimMachineStateNode, this);
}

TSharedPtr<SGraphNode> UWidgetAnimMachineTransitionNode::CreateVisualWidget()
{
	return SNew(SWidgetAnimMachineTransitionNode, this);
}

// =====================================================================================================
// Arrows
// =====================================================================================================

/**
 * Draws every transition as a straight arrow from the edge of one state to the edge of the other, through its rule icon -
 * as an Animation Blueprint's state machine does - lit while it plays. The pins themselves are never drawn.
 */
class FWidgetAnimMachineConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:

	FWidgetAnimMachineConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraph)
		: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
		, Graph(Cast<UWidgetAnimMachineGraph>(InGraph))
	{
	}

	virtual void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes) override
	{
		if (!Graph)
		{
			return;
		}

		// Where each node is on screen this frame.
		TMap<const UEdGraphNode*, FSlateRect> NodeRects;
		for (int32 Index = 0; Index < ArrangedNodes.Num(); ++Index)
		{
			const FArrangedWidget& Arranged = ArrangedNodes[Index];
			const TSharedRef<SGraphNode> NodeWidget = StaticCastSharedRef<SGraphNode>(Arranged.Widget);
			const FVector2f Position(Arranged.Geometry.GetAbsolutePosition());
			const FVector2f Size(Arranged.Geometry.GetAbsoluteSize());
			NodeRects.Add(NodeWidget->GetNodeObj(), FSlateRect(Position.X, Position.Y, Position.X + Size.X, Position.Y + Size.Y));
		}

		FWidgetAnimMachineLayerDebug Debug;
		const bool bDebugging = Graph->GetDebug && Graph->GetDebug(Debug);

		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UWidgetAnimMachineTransitionNode* Transition = Cast<UWidgetAnimMachineTransitionNode>(Node);
			if (!Transition)
			{
				continue;
			}
			const FSlateRect* FromRect = NodeRects.Find(Graph->FindStateNode(Transition->FromId));
			const FSlateRect* ToRect = NodeRects.Find(Graph->FindStateNode(Transition->ToId));
			const FSlateRect* IconRect = NodeRects.Find(Transition);
			if (!FromRect || !ToRect)
			{
				continue;
			}

			const FWidgetAnimMachineTransition* Data = Transition->GetTransition();
			FLinearColor Color = MachineColors::TransitionLine;
			float Thickness = 1.5f;
			if (bDebugging && Debug.ActiveTransition == Transition->TransitionId)
			{
				Color = MachineColors::TransitionActive;
				Thickness = 3.f;
			}
			else if (Data && !Data->bEnabled)
			{
				Color = MachineColors::TransitionDisabled;
			}

			// Through the icon, so a pair joined both ways reads as two arrows.
			const FVector2f FromCenter = RectCenter(*FromRect);
			const FVector2f ToCenter = RectCenter(*ToRect);
			const FVector2f Through = IconRect ? RectCenter(*IconRect) : (FromCenter + ToCenter) * 0.5f;
			const FVector2f Start = ClipToRect(*FromRect, Through, FromCenter);
			const FVector2f End = ClipToRect(*ToRect, Through, ToCenter);

			TArray<FVector2f> Points;
			Points.Add(Start);
			Points.Add(Through);
			Points.Add(End);
			FSlateDrawElement::MakeLines(DrawElementsList, WireLayerID, FPaintGeometry(), Points, ESlateDrawEffect::None, Color, /*bAntialias=*/true, Thickness * ZoomFactor);

			// The arrowhead, where it meets the next state.
			const FVector2f Direction = (End - Through).GetSafeNormal();
			if (!Direction.IsNearlyZero())
			{
				const FVector2f Side(-Direction.Y, Direction.X);
				const float HeadLength = 10.f * ZoomFactor;
				const float HeadWidth = 5.f * ZoomFactor;
				TArray<FVector2f> Head;
				Head.Add(End - Direction * HeadLength + Side * HeadWidth);
				Head.Add(End);
				Head.Add(End - Direction * HeadLength - Side * HeadWidth);
				FSlateDrawElement::MakeLines(DrawElementsList, WireLayerID + 1, FPaintGeometry(), Head, ESlateDrawEffect::None, Color, /*bAntialias=*/true, Thickness * ZoomFactor + 0.5f);
			}
		}
	}

private:

	UWidgetAnimMachineGraph* Graph = nullptr;

	static FVector2f RectCenter(const FSlateRect& Rect)
	{
		return FVector2f((Rect.Left + Rect.Right) * 0.5f, (Rect.Top + Rect.Bottom) * 0.5f);
	}

	/** Where the line from Outside to Center, inside Rect, crosses Rect's edge. */
	static FVector2f ClipToRect(const FSlateRect& Rect, const FVector2f& Outside, const FVector2f& Center)
	{
		const FVector2f Delta = Outside - Center;
		const float HalfWidth = (Rect.Right - Rect.Left) * 0.5f;
		const float HalfHeight = (Rect.Bottom - Rect.Top) * 0.5f;
		if (FMath::IsNearlyZero(Delta.X) && FMath::IsNearlyZero(Delta.Y))
		{
			return Center;
		}
		const float ScaleX = FMath::IsNearlyZero(Delta.X) ? TNumericLimits<float>::Max() : HalfWidth / FMath::Abs(Delta.X);
		const float ScaleY = FMath::IsNearlyZero(Delta.Y) ? TNumericLimits<float>::Max() : HalfHeight / FMath::Abs(Delta.Y);
		const float Scale = FMath::Min3(ScaleX, ScaleY, 1.f);
		return Center + Delta * Scale;
	}
};

FConnectionDrawingPolicy* UWidgetAnimMachineGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
	return new FWidgetAnimMachineConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}

#undef LOCTEXT_NAMESPACE
