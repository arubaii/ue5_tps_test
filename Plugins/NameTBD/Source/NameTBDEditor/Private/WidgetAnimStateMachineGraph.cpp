// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimStateMachineGraph.h"

#include "ConnectionDrawingPolicy.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditorActions.h"
#include "NameTBD/WidgetAnimation/WidgetAnimCue.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"
#include "NameTBDColors.h"
#include "NameTBDStyle.h"
#include "NameTBDTooltips.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "SCommentBubble.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"
#include "SGraphPin.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "WidgetAnimMachineEditorStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimStateMachineGraph"

namespace MachineColors = NameTBD::Colors::StateMachineEditor;
namespace MachineTips = NameTBD::Tooltips::StateMachineEditor;
namespace TipPalette = NameTBD::Colors::WidgetAnimEditor;

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

	/** What the clipboard text starts with - so only something copied from a state machine is ever pasted into one. */
	const TCHAR* ClipboardPrefix = TEXT("WidgetAnimStateMachineClipboard:");

	/** The blend's color on its transition node. */
	FLinearColor GetBlendColor(EWidgetAnimMachineBlend Blend)
	{
		switch (Blend)
		{
		case EWidgetAnimMachineBlend::Spring:    return MachineColors::TransitionSpring;
		case EWidgetAnimMachineBlend::Animation: return MachineColors::TransitionAnimation;
		case EWidgetAnimMachineBlend::Cut:       return MachineColors::TransitionCut;
		default:                                  return MachineColors::TransitionTween;
		}
	}

	FText GetBlendName(EWidgetAnimMachineBlend Blend)
	{
		return StaticEnum<EWidgetAnimMachineBlend>()->GetDisplayNameTextByValue(static_cast<int64>(Blend));
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

	void AddCondition(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& TransitionId, FName Input, EWidgetAnimMachineCompare Compare)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		const int32 Index = Layer ? Layer->FindTransitionIndex(TransitionId) : INDEX_NONE;
		if (Index == INDEX_NONE || Input.IsNone())
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("AddCondition", "Add Transition Condition"));
		Machine->Modify();
		FWidgetAnimMachineCondition& Condition = Layer->Transitions[Index].Conditions.AddDefaulted_GetRef();
		Condition.Input = Input;
		Condition.Compare = Compare;
		FinishEdit(Machine);
	}

	void Delete(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& States, const TArray<FGuid>& Transitions, const TArray<FGuid>& Comments)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || (States.Num() == 0 && Transitions.Num() == 0 && Comments.Num() == 0))
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("DeleteNodes", "Delete From State Machine"));
		Machine->Modify();
		Layer->States.RemoveAll([&States](const FWidgetAnimMachineState& State) { return State.Kind == EWidgetAnimMachineNodeKind::State && States.Contains(State.Id); });
		Layer->Transitions.RemoveAll([&Transitions](const FWidgetAnimMachineTransition& Transition) { return Transitions.Contains(Transition.Id); });
		Layer->Comments.RemoveAll([&Comments](const FWidgetAnimMachineComment& Comment) { return Comments.Contains(Comment.Id); });

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

	void PlaceTransitions(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TMap<FGuid, TOptional<FVector2D>>& Offsets)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || Offsets.Num() == 0)
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("PlaceTransitions", "Move Transition Node"));
		Machine->Modify();
		for (FWidgetAnimMachineTransition& Transition : Layer->Transitions)
		{
			if (const TOptional<FVector2D>* Offset = Offsets.Find(Transition.Id))
			{
				Transition.bEditorNodePlaced = Offset->IsSet();
				Transition.EditorNodeOffset = Offset->IsSet() ? Offset->GetValue() : FVector2D::ZeroVector;
			}
		}
		Machine->MarkPackageDirty();
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

	void SetTransitionsEnabled(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& Transitions, bool bEnabled)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || Transitions.Num() == 0)
		{
			return;
		}
		const FScopedTransaction Transaction(bEnabled ? LOCTEXT("EnableTransitions", "Enable Transitions") : LOCTEXT("DisableTransitions", "Disable Transitions"));
		Machine->Modify();
		for (FWidgetAnimMachineTransition& Transition : Layer->Transitions)
		{
			if (Transitions.Contains(Transition.Id))
			{
				Transition.bEnabled = bEnabled;
			}
		}
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

	void RenameLayer(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FString& NewName)
	{
		const FString Trimmed = NewName.TrimStartAndEnd();
		if (!Machine || !Machine->Layers.IsValidIndex(LayerIndex) || Trimmed.IsEmpty() || Machine->Layers[LayerIndex].Name == FName(*Trimmed))
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("RenameLayer", "Rename State Machine Layer"));
		Machine->Modify();
		Machine->Layers[LayerIndex].Name = Machine->MakeUniqueLayerName(FName(*Trimmed), LayerIndex);
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

	void ReplaceInputReferences(UWidgetAnimStateMachineAsset* Machine, FName Old, FName New)
	{
		if (!Machine || Old.IsNone() || Old == New)
		{
			return;
		}
		for (FWidgetAnimMachineLayer& Layer : Machine->Layers)
		{
			if (Layer.WeightInput == Old)
			{
				Layer.WeightInput = New;
			}
			for (FWidgetAnimMachineState& State : Layer.States)
			{
				if (State.ProgressInput == Old)
				{
					State.ProgressInput = New;
				}
			}
			for (FWidgetAnimMachineTransition& Transition : Layer.Transitions)
			{
				for (FWidgetAnimMachineCondition& Condition : Transition.Conditions)
				{
					if (Condition.Input == Old)
					{
						Condition.Input = New;
					}
				}
			}
		}
	}

	FText ValidateInputName(const UWidgetAnimStateMachineAsset* Machine, FName Input, const FString& NewName)
	{
		const FString Trimmed = NewName.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return LOCTEXT("InputNameEmpty", "An input needs a name.");
		}
		const FName Wanted(*Trimmed);
		if (WidgetAnimMachineInputs::IsBuiltIn(Wanted))
		{
			return FText::Format(LOCTEXT("InputNameBuiltIn", "{0} is a built-in input."), FText::FromName(Wanted));
		}
		if (Machine && Wanted != Input && Machine->FindInput(Wanted))
		{
			return FText::Format(LOCTEXT("InputNameTaken", "The machine has an input called {0} already."), FText::FromName(Wanted));
		}
		return FText::GetEmpty();
	}

	FName RenameInput(UWidgetAnimStateMachineAsset* Machine, FName Input, const FString& NewName)
	{
		const int32 Index = Machine ? Machine->Inputs.IndexOfByPredicate([Input](const FWidgetAnimMachineInput& Candidate) { return Candidate.Name == Input; }) : INDEX_NONE;
		const FString Trimmed = NewName.TrimStartAndEnd();
		if (Index == INDEX_NONE || Trimmed.IsEmpty() || FName(*Trimmed) == Input)
		{
			return Input;
		}

		const FScopedTransaction Transaction(LOCTEXT("RenameInput", "Rename State Machine Input"));
		Machine->Modify();
		const FName Unique = Machine->MakeUniqueInputName(FName(*Trimmed), Index);
		Machine->Inputs[Index].Name = Unique;
		ReplaceInputReferences(Machine, Input, Unique);
		FinishEdit(Machine);
		return Unique;
	}

	void SetStateComment(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& State, const FString& Comment, bool bVisible)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		const int32 Index = Layer ? Layer->FindStateIndex(State) : INDEX_NONE;
		if (Index == INDEX_NONE)
		{
			return;
		}
		FWidgetAnimMachineState& Target = Layer->States[Index];
		if (Target.Comment == Comment && Target.bCommentBubbleVisible == bVisible)
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("SetStateComment", "Edit Node Comment"));
		Machine->Modify();
		Target.Comment = Comment;
		Target.bCommentBubbleVisible = bVisible;
		FinishEdit(Machine);
	}

	FGuid AddComment(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FBox2D& Bounds, const FString& Text)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer)
		{
			return FGuid();
		}
		const FScopedTransaction Transaction(LOCTEXT("AddComment", "Add Comment"));
		Machine->Modify();
		FWidgetAnimMachineComment& Comment = Layer->Comments.AddDefaulted_GetRef();
		Comment.Id = FGuid::NewGuid();
		Comment.Text = Text;
		Comment.Position = Bounds.Min;
		Comment.Size = FVector2D(FMath::Max(Bounds.GetSize().X, 64.0), FMath::Max(Bounds.GetSize().Y, 48.0));
		const FGuid Id = Comment.Id;
		FinishEdit(Machine);
		return Id;
	}

	bool UpdateComments(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FWidgetAnimMachineComment>& Comments)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || Comments.Num() == 0)
		{
			return false;
		}

		auto Differs = [](const FWidgetAnimMachineComment& A, const FWidgetAnimMachineComment& B)
		{
			return !FWidgetAnimMachineComment::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None);
		};
		const bool bAnyChange = Comments.ContainsByPredicate([Layer, &Differs](const FWidgetAnimMachineComment& Changed)
		{
			const FWidgetAnimMachineComment* Current = Layer->Comments.FindByPredicate([&Changed](const FWidgetAnimMachineComment& Candidate) { return Candidate.Id == Changed.Id; });
			return Current && Differs(*Current, Changed);
		});
		if (!bAnyChange)
		{
			return false;
		}

		const FScopedTransaction Transaction(LOCTEXT("EditComment", "Edit Comment"));
		Machine->Modify();
		for (const FWidgetAnimMachineComment& Changed : Comments)
		{
			if (FWidgetAnimMachineComment* Current = Layer->Comments.FindByPredicate([&Changed](const FWidgetAnimMachineComment& Candidate) { return Candidate.Id == Changed.Id; }))
			{
				*Current = Changed;
			}
		}
		Machine->MarkPackageDirty();
		return true;
	}

	FString CopyToText(const UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& States, const TArray<FGuid>& Comments)
	{
		const FWidgetAnimMachineLayer* Layer = Machine && Machine->Layers.IsValidIndex(LayerIndex) ? &Machine->Layers[LayerIndex] : nullptr;
		if (!Layer)
		{
			return FString();
		}

		FWidgetAnimMachineClipboard Clipboard;
		for (const FWidgetAnimMachineState& State : Layer->States)
		{
			if (State.Kind == EWidgetAnimMachineNodeKind::State && States.Contains(State.Id))
			{
				Clipboard.States.Add(State);
			}
		}
		// Only the transitions between states that were copied: a transition can't be pasted without both its ends.
		for (const FWidgetAnimMachineTransition& Transition : Layer->Transitions)
		{
			const bool bFrom = Clipboard.States.ContainsByPredicate([&Transition](const FWidgetAnimMachineState& State) { return State.Id == Transition.From; });
			const bool bTo = Clipboard.States.ContainsByPredicate([&Transition](const FWidgetAnimMachineState& State) { return State.Id == Transition.To; });
			if (bFrom && bTo)
			{
				Clipboard.Transitions.Add(Transition);
			}
		}
		for (const FWidgetAnimMachineComment& Comment : Layer->Comments)
		{
			if (Comments.Contains(Comment.Id))
			{
				Clipboard.Comments.Add(Comment);
			}
		}
		if (Clipboard.States.Num() == 0 && Clipboard.Comments.Num() == 0)
		{
			return FString();
		}

		FString Text;
		const FWidgetAnimMachineClipboard Defaults;
		FWidgetAnimMachineClipboard::StaticStruct()->ExportText(Text, &Clipboard, &Defaults, nullptr, PPF_None, nullptr);
		return FString(ClipboardPrefix) + Text;
	}

	bool CanPasteText(const FString& Text)
	{
		return Text.StartsWith(ClipboardPrefix);
	}

	TArray<FGuid> PasteText(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FString& Text, const FVector2D& Location)
	{
		TArray<FGuid> Pasted;
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || !CanPasteText(Text))
		{
			return Pasted;
		}

		FWidgetAnimMachineClipboard Clipboard;
		const FString Body = Text.RightChop(FCString::Strlen(ClipboardPrefix));
		FWidgetAnimMachineClipboard::StaticStruct()->ImportText(*Body, &Clipboard, nullptr, PPF_None, GWarn, FWidgetAnimMachineClipboard::StaticStruct()->GetName());
		if (Clipboard.States.Num() == 0 && Clipboard.Comments.Num() == 0)
		{
			return Pasted;
		}

		// The copied nodes' top left goes where they are pasted; everything keeps its place relative to it.
		FVector2D TopLeft(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		for (const FWidgetAnimMachineState& State : Clipboard.States)
		{
			TopLeft = FVector2D(FMath::Min(TopLeft.X, State.EditorPosition.X), FMath::Min(TopLeft.Y, State.EditorPosition.Y));
		}
		for (const FWidgetAnimMachineComment& Comment : Clipboard.Comments)
		{
			TopLeft = FVector2D(FMath::Min(TopLeft.X, Comment.Position.X), FMath::Min(TopLeft.Y, Comment.Position.Y));
		}
		const FVector2D Offset = Location - TopLeft;

		const FScopedTransaction Transaction(LOCTEXT("PasteStates", "Paste States"));
		Machine->Modify();

		TMap<FGuid, FGuid> NewIds;
		for (FWidgetAnimMachineState State : Clipboard.States)
		{
			const FGuid NewId = FGuid::NewGuid();
			NewIds.Add(State.Id, NewId);
			State.Id = NewId;
			State.Kind = EWidgetAnimMachineNodeKind::State;
			State.EditorPosition += Offset;
			State.Name = UWidgetAnimStateMachineAsset::MakeUniqueStateName(*Layer, State.Name, NewId);
			Layer->States.Add(State);
			Pasted.Add(NewId);
		}
		for (FWidgetAnimMachineTransition Transition : Clipboard.Transitions)
		{
			const FGuid* From = NewIds.Find(Transition.From);
			const FGuid* To = NewIds.Find(Transition.To);
			if (!From || !To)
			{
				continue;
			}
			Transition.Id = FGuid::NewGuid();
			Transition.From = *From;
			Transition.To = *To;
			Layer->Transitions.Add(Transition);
		}
		for (FWidgetAnimMachineComment Comment : Clipboard.Comments)
		{
			Comment.Id = FGuid::NewGuid();
			Comment.Position += Offset;
			Layer->Comments.Add(Comment);
			Pasted.Add(Comment.Id);
		}

		FinishEdit(Machine);
		return Pasted;
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
// Wires
// =====================================================================================================

namespace
{
	/** A point on a Hermite curve, as Slate draws a spline from two points and their tangents. */
	FVector2f HermitePoint(const FVector2f& P0, const FVector2f& T0, const FVector2f& P1, const FVector2f& T1, float T)
	{
		const float T2 = T * T;
		const float T3 = T2 * T;
		return P0 * (2.f * T3 - 3.f * T2 + 1.f)
			+ T0 * (T3 - 2.f * T2 + T)
			+ P1 * (-2.f * T3 + 3.f * T2)
			+ T1 * (T3 - T2);
	}

	FVector2f HermiteDerivative(const FVector2f& P0, const FVector2f& T0, const FVector2f& P1, const FVector2f& T1, float T)
	{
		const float T2 = T * T;
		return P0 * (6.f * T2 - 6.f * T)
			+ T0 * (3.f * T2 - 4.f * T + 1.f)
			+ P1 * (-6.f * T2 + 6.f * T)
			+ T1 * (3.f * T2 - 2.f * T);
	}
}

FVector2f FWidgetAnimMachineWire::Evaluate(float T) const
{
	if (!bThroughNode)
	{
		return HermitePoint(Start, StartTangent, End, EndTangent, T);
	}
	// Through a dragged node: the first half of the way is the curve into it, the second the curve out of it.
	return T < 0.5f
		? HermitePoint(Start, StartTangent, Mid, MidTangentIn, T * 2.f)
		: HermitePoint(Mid, MidTangentOut, End, EndTangent, T * 2.f - 1.f);
}

FVector2f FWidgetAnimMachineWire::Derivative(float T) const
{
	if (!bThroughNode)
	{
		return HermiteDerivative(Start, StartTangent, End, EndTangent, T);
	}
	return T < 0.5f
		? HermiteDerivative(Start, StartTangent, Mid, MidTangentIn, T * 2.f) * 2.f
		: HermiteDerivative(Mid, MidTangentOut, End, EndTangent, T * 2.f - 1.f) * 2.f;
}

namespace WidgetAnimMachineWires
{
	FVector2f GetFacingSide(const FBox2f& Box, const FBox2f& Toward)
	{
		const FVector2f Delta = Toward.GetCenter() - Box.GetCenter();
		const float SignX = Delta.X >= 0.f ? 1.f : -1.f;
		const float SignY = Delta.Y >= 0.f ? 1.f : -1.f;

		// The space between them each way - negative where they overlap.
		const float GapX = SignX > 0.f ? Toward.Min.X - Box.Max.X : Box.Min.X - Toward.Max.X;
		const float GapY = SignY > 0.f ? Toward.Min.Y - Box.Max.Y : Box.Min.Y - Toward.Max.Y;

		// Apart both ways - diagonal: out of the corner facing it, unless it lies far more one way than the other.
		if (GapX > 0.f && GapY > 0.f)
		{
			if (GapX > GapY * CornerRatio)
			{
				return FVector2f(SignX, 0.f);
			}
			if (GapY > GapX * CornerRatio)
			{
				return FVector2f(0.f, SignY);
			}
			return FVector2f(SignX, SignY);
		}

		// Apart one way only: straight out of that side.
		if (GapX > 0.f || GapY > 0.f)
		{
			return GapX > GapY ? FVector2f(SignX, 0.f) : FVector2f(0.f, SignY);
		}

		// On top of each other: whichever side the line between their middles crosses, for Box's own shape.
		const FVector2f HalfSize = Box.GetSize() * 0.5f;
		if (FMath::Abs(Delta.X) * FMath::Max(HalfSize.Y, 1.f) >= FMath::Abs(Delta.Y) * FMath::Max(HalfSize.X, 1.f))
		{
			return FVector2f(SignX, 0.f);
		}
		return FVector2f(0.f, SignY);
	}

	FVector2f GetFacingSide(const FBox2f& Box, const FVector2f& Toward)
	{
		return GetFacingSide(Box, FBox2f(Toward, Toward));
	}

	FVector2f GetSideMiddle(const FBox2f& Box, const FVector2f& Side)
	{
		const FVector2f HalfSize = Box.GetSize() * 0.5f;
		const bool bCorner = Side.X != 0.f && Side.Y != 0.f;
		// A corner pulled in onto the state's rounded corner, where the wire meets what is drawn.
		const FVector2f Reach = bCorner
			? FVector2f(FMath::Max(HalfSize.X - CornerInset, 0.f), FMath::Max(HalfSize.Y - CornerInset, 0.f))
			: HalfSize;
		return Box.GetCenter() + Side * Reach;
	}

	FVector2f GetAnchor(const FBox2f& Box, const FVector2f& Side, float Offset, const FVector2f& Across)
	{
		FVector2f Point = GetSideMiddle(Box, Side);
		if (FMath::IsNearlyZero(Offset))
		{
			return Point;
		}
		FVector2f Along = FVector2f(-Side.Y, Side.X).GetSafeNormal();
		if (FVector2f::DotProduct(Along, Across) < 0.f)
		{
			Along = -Along;
		}
		if (Side.X == 0.f || Side.Y == 0.f)
		{
			return Point + Along * Offset;
		}

		// A corner: off it along whichever of its two edges the offset leads onto - the step out past the other edge held on it.
		Point += Along * (Offset * UE_SQRT_2);
		Point.X = FMath::Clamp(Point.X, Box.Min.X, Box.Max.X);
		Point.Y = FMath::Clamp(Point.Y, Box.Min.Y, Box.Max.Y);
		return Point;
	}

	FVector2f GetPlacementAnchor(const FBox2f& First, const FBox2f& Second)
	{
		return (First.Min + Second.Min) * 0.5f;
	}

	FWidgetAnimMachineWire Compute(const FBox2f& First, const FBox2f& Second, int32 Lane, int32 LaneCount, bool bReversed, const FVector2f* NodePoint)
	{
		FWidgetAnimMachineWire Wire;

		if (NodePoint)
		{
			// Through where its node was dragged: out of the side or corner of the state it leaves that faces the node, into
			// the node, and on out of it into the side or corner of the other that faces it - a smooth curve through the node,
			// as through a reroute.
			const FBox2f& From = bReversed ? Second : First;
			const FBox2f& To = bReversed ? First : Second;
			const FVector2f FromSide = GetFacingSide(From, *NodePoint);
			const FVector2f ToSide = GetFacingSide(To, *NodePoint);
			const FVector2f StartPoint = GetSideMiddle(From, FromSide);
			const FVector2f EndPoint = GetSideMiddle(To, ToSide);
			const float InLength = FMath::Max((*NodePoint - StartPoint).Size(), 1.f);
			const float OutLength = FMath::Max((EndPoint - *NodePoint).Size(), 1.f);
			FVector2f Through = (EndPoint - StartPoint).GetSafeNormal();
			if (Through.IsNearlyZero())
			{
				Through = (*NodePoint - StartPoint).GetSafeNormal();
			}

			Wire.bThroughNode = true;
			Wire.Start = StartPoint;
			Wire.StartTangent = FromSide.GetSafeNormal() * FMath::Max(InLength, 36.f);
			Wire.Mid = *NodePoint;
			Wire.MidTangentIn = Through * InLength;
			Wire.MidTangentOut = Through * OutLength;
			Wire.End = EndPoint;
			Wire.EndTangent = -ToSide.GetSafeNormal() * FMath::Max(OutLength, 36.f);
			Wire.NodeT = 0.5f;
			Wire.NodePoint = *NodePoint;
			Wire.NodeDirection = Through.IsNearlyZero() ? FVector2f(1.f, 0.f) : Through;
			return Wire;
		}

		const FVector2f FirstCenter = First.GetCenter();
		const FVector2f SecondCenter = Second.GetCenter();

		// Each end leaves the side of its state that faces the other state - straight across, straight down - or, between
		// states that lie diagonally, the corner that faces it.
		const FVector2f FirstSide = GetFacingSide(First, Second);
		const FVector2f SecondSide = GetFacingSide(Second, First);

		// Lanes: every wire between the two states in one of its own, side by side along the edges, all offset the same way
		// across the line between the two states - closer together when more of them than fit the shorter edge.
		const FVector2f Between = (SecondCenter - FirstCenter).GetSafeNormal();
		const FVector2f Across = Between.IsNearlyZero() ? FVector2f(0.f, 1.f) : FVector2f(-Between.Y, Between.X);
		auto EdgeLength = [](const FBox2f& Box, const FVector2f& Side)
		{
			if (Side.X != 0.f && Side.Y != 0.f)
			{
				return FMath::Min(Box.GetSize().X, Box.GetSize().Y);
			}
			return Side.X != 0.f ? Box.GetSize().Y : Box.GetSize().X;
		};
		const float Room = FMath::Max(FMath::Min(EdgeLength(First, FirstSide), EdgeLength(Second, SecondSide)) - 12.f, 0.f);
		const float Spacing = LaneCount > 1 ? FMath::Min(LaneSpacing, Room / (LaneCount - 1)) : 0.f;
		const float LaneOffset = (Lane - (LaneCount - 1) * 0.5f) * Spacing;

		const FVector2f FirstPoint = GetAnchor(First, FirstSide, LaneOffset, Across);
		const FVector2f SecondPoint = GetAnchor(Second, SecondSide, LaneOffset, Across);

		// Out of the side or corner it leaves, into the one it arrives at - as a Blueprint wire leaves and enters its pins -
		// long enough to curve smoothly, never so short that it kinks between states that nearly touch.
		const float Chord = FMath::Max((SecondPoint - FirstPoint).Size(), 1.f);
		const float TangentLength = FMath::Max(Chord * 0.6f, 36.f);
		const FVector2f FirstTangent = FirstSide.GetSafeNormal() * TangentLength;
		const FVector2f SecondTangent = -SecondSide.GetSafeNormal() * TangentLength;

		// Where the transition node sits: halfway. Lanes closer than a node is wide stagger their nodes along the wire, so no
		// two nodes land on each other however many wires there are.
		float NodeT = 0.5f;
		if (LaneCount > 1 && Spacing < NodeDiameter + 2.f)
		{
			const float Needed = NodeDiameter + 2.f;
			const float Step = FMath::Sqrt(FMath::Max(Needed * Needed - Spacing * Spacing, 0.f)) / Chord;
			NodeT = FMath::Clamp(0.5f + (Lane - (LaneCount - 1) * 0.5f) * Step, 0.12f, 0.88f);
		}

		Wire.Start = FirstPoint;
		Wire.StartTangent = FirstTangent;
		Wire.End = SecondPoint;
		Wire.EndTangent = SecondTangent;
		Wire.NodeT = NodeT;

		// Running the other way: the same curve, traced from its far end.
		if (bReversed)
		{
			Wire.Start = SecondPoint;
			Wire.End = FirstPoint;
			Wire.StartTangent = -SecondTangent;
			Wire.EndTangent = -FirstTangent;
			Wire.NodeT = 1.f - NodeT;
		}

		Wire.NodePoint = Wire.Evaluate(Wire.NodeT);
		const FVector2f Direction = Wire.Derivative(Wire.NodeT);
		Wire.NodeDirection = Direction.IsNearlyZero() ? (Wire.End - Wire.Start).GetSafeNormal() : Direction.GetSafeNormal();
		return Wire;
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

UWidgetAnimMachineTransitionNode* UWidgetAnimMachineGraph::FindTransitionNode(const FGuid& TransitionId) const
{
	for (UEdGraphNode* Node : Nodes)
	{
		UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Node);
		if (TransitionNode && TransitionNode->TransitionId == TransitionId)
		{
			return TransitionNode;
		}
	}
	return nullptr;
}

UEdGraphNode_Comment* UWidgetAnimMachineGraph::FindCommentNode(const FGuid& CommentId) const
{
	for (UEdGraphNode* Node : Nodes)
	{
		UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node);
		if (CommentNode && CommentNode->NodeGuid == CommentId)
		{
			return CommentNode;
		}
	}
	return nullptr;
}

void UWidgetAnimMachineGraph::GetPairOrder(const UWidgetAnimMachineTransitionNode* Node, FGuid& OutFirst, FGuid& OutSecond)
{
	// Whichever way a wire runs, its pair is counted from the same end, so lanes never swap between the two directions.
	const bool bFromFirst = Node->FromId < Node->ToId;
	OutFirst = bFromFirst ? Node->FromId : Node->ToId;
	OutSecond = bFromFirst ? Node->ToId : Node->FromId;
}

void UWidgetAnimMachineGraph::GetLane(const UWidgetAnimMachineTransitionNode* Node, int32& OutLane, int32& OutCount, bool& bOutReversed) const
{
	OutLane = 0;
	OutCount = 0;
	FGuid First;
	FGuid Second;
	GetPairOrder(Node, First, Second);
	bOutReversed = Node->FromId != First;

	for (const UEdGraphNode* Other : Nodes)
	{
		const UWidgetAnimMachineTransitionNode* Sibling = Cast<UWidgetAnimMachineTransitionNode>(Other);
		if (!Sibling)
		{
			continue;
		}
		const bool bSamePair = (Sibling->FromId == First && Sibling->ToId == Second) || (Sibling->FromId == Second && Sibling->ToId == First);
		// A node dragged somewhere of its own takes no lane: its wire runs through it instead.
		if (!bSamePair || (Sibling->bPlaced && Sibling != Node))
		{
			continue;
		}
		if (Sibling == Node)
		{
			OutLane = OutCount;
		}
		++OutCount;
	}
	OutCount = FMath::Max(OutCount, 1);
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

	// Comment boxes first, so they are under everything.
	for (const FWidgetAnimMachineComment& Comment : Layer->Comments)
	{
		UEdGraphNode_Comment* Node = NewObject<UEdGraphNode_Comment>(this, NAME_None, RF_Transient);
		Node->NodeGuid = Comment.Id;
		Node->NodePosX = FMath::RoundToInt32(Comment.Position.X);
		Node->NodePosY = FMath::RoundToInt32(Comment.Position.Y);
		Node->NodeWidth = FMath::RoundToInt32(Comment.Size.X);
		Node->NodeHeight = FMath::RoundToInt32(Comment.Size.Y);
		Node->NodeComment = Comment.Text;
		Node->CommentColor = Comment.Color;
		Node->FontSize = Comment.FontSize;
		Node->MoveMode = Comment.bGroupMovement ? ECommentBoxMode::GroupMovement : ECommentBoxMode::NoGroupMovement;
		Node->bCommentBubbleVisible_InDetailsPanel = Comment.bShowBubbleWhenZoomed;
		Node->bColorCommentBubble = Comment.bColorBubble;
		Node->bCanRenameNode = true;
		AddNode(Node, /*bUserAction=*/false, /*bSelectNewNode=*/false);
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

		// Its note, in a bubble over it.
		Node->NodeComment = State.Comment;
		Node->bCommentBubbleVisible = State.bCommentBubbleVisible && !State.Comment.IsEmpty();
		Node->bCommentBubblePinned = true;

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
		Node->bPlaced = Transition.bEditorNodePlaced;
		Node->PlacedOffset = FVector2f(Transition.EditorNodeOffset);
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
	const TWeakObjectPtr<const UWidgetAnimMachineGraphNode> WeakSelf(this);
	if (Kind == EWidgetAnimMachineNodeKind::State)
	{
		Section.AddMenuEntry(
			TEXT("SetEntryState"),
			LOCTEXT("SetEntryStateMenu", "Set As Entry State"),
			MachineTips::SetEntryStateTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakSelf]()
			{
				const UWidgetAnimMachineGraphNode* Node = WeakSelf.Get();
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
			FUIAction(FExecuteAction::CreateLambda([WeakSelf]()
			{
				const UWidgetAnimMachineGraphNode* Node = WeakSelf.Get();
				UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
				if (Graph && Graph->OnOpenState)
				{
					Graph->OnOpenState(Node->StateId);
				}
			})));
		Section.AddMenuEntry(FGenericCommands::Get().Rename);
		Section.AddMenuEntry(FGenericCommands::Get().Copy);
		Section.AddMenuEntry(FGenericCommands::Get().Cut);
		Section.AddMenuEntry(FGenericCommands::Get().Duplicate);
		Section.AddMenuEntry(FGenericCommands::Get().Delete);

		// Its note: typed straight into the menu, as a Blueprint node's comment is.
		const FWidgetAnimMachineState* State = GetState();
		FToolMenuSection& CommentSection = Menu->AddSection(TEXT("WidgetAnimMachineNodeComment"), LOCTEXT("NodeCommentSection", "Node Comment"));
		CommentSection.AddEntry(FToolMenuEntry::InitWidget(
			TEXT("NodeComment"),
			SNew(SBox)
			.WidthOverride(240.f)
			.Padding(FMargin(4.f, 2.f))
			[
				SNew(SMultiLineEditableTextBox)
				.Text(FText::FromString(State ? State->Comment : FString()))
				.HintText(LOCTEXT("NodeCommentHint", "A note shown over the state"))
				.ToolTip(WidgetAnimMachineStyle::MakeToolTip(MachineTips::NodeCommentTooltip()))
				.OnTextCommitted_Lambda([WeakSelf](const FText& NewText, ETextCommit::Type CommitType)
				{
					const UWidgetAnimMachineGraphNode* Node = WeakSelf.Get();
					UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
					if (Graph)
					{
						WidgetAnimMachineEdits::SetStateComment(Graph->Machine, Graph->LayerIndex, Node->StateId, NewText.ToString(), /*bVisible=*/true);
					}
				})
			],
			FText::GetEmpty(),
			/*bNoIndent=*/true));
	}

	// Lining the selection up: Q, as in a Blueprint graph. A menu entry of its own - the engine's Straighten Connections
	// command straightens by pins, which a state machine's nodes don't line up by.
	FToolMenuSection& Organize = Menu->AddSection(TEXT("WidgetAnimMachineOrganize"), LOCTEXT("OrganizeSection", "Organize"));
	Organize.AddMenuEntry(
		TEXT("StraightenStates"),
		LOCTEXT("StraightenStates", "Straighten Connections (Q)"),
		MachineTips::StraightenTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakSelf]()
		{
			const UWidgetAnimMachineGraphNode* Node = WeakSelf.Get();
			UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
			if (Graph && Graph->OnStraighten)
			{
				Graph->OnStraighten();
			}
		})));
	Organize.AddMenuEntry(FGraphEditorCommands::Get().CreateComment);
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

FText UWidgetAnimMachineTransitionNode::DescribeConditions() const
{
	const FWidgetAnimMachineTransition* Transition = GetTransition();
	if (!Transition || Transition->Conditions.Num() == 0)
	{
		return LOCTEXT("Always", "Always");
	}
	TArray<FText> Conditions;
	for (const FWidgetAnimMachineCondition& Condition : Transition->Conditions)
	{
		Conditions.Add(DescribeCondition(Condition));
	}
	return FText::Join(LOCTEXT("And", " and "), Conditions);
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
	Lines.Add(DescribeConditions());

	switch (Transition->Blend)
	{
	case EWidgetAnimMachineBlend::Cut:
		Lines.Add(LOCTEXT("BlendCut", "Cut"));
		break;
	case EWidgetAnimMachineBlend::Spring:
		Lines.Add(FText::Format(LOCTEXT("BlendSpring", "Spring, about {0}"), FText::FromString(WidgetAnimMachineStyle::FormatSeconds(Transition->GetBlendSeconds()))));
		break;
	case EWidgetAnimMachineBlend::Animation:
		Lines.Add(FText::Format(LOCTEXT("BlendAnimation", "Plays {0}"), Transition->TransitionAnimation ? FText::FromString(Transition->TransitionAnimation->GetName()) : LOCTEXT("NoTransitionAnimation", "(no animation)")));
		break;
	default:
		Lines.Add(FText::Format(LOCTEXT("BlendTween", "Tween {0}"), FText::FromString(WidgetAnimMachineStyle::FormatSeconds(Transition->Duration))));
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

FString UWidgetAnimMachineTransitionNode::GetLengthLabel() const
{
	const FWidgetAnimMachineTransition* Transition = GetTransition();
	if (!Transition)
	{
		return FString();
	}
	switch (Transition->Blend)
	{
	case EWidgetAnimMachineBlend::Cut:
		return LOCTEXT("CutLabel", "Cut").ToString();
	case EWidgetAnimMachineBlend::Animation:
		// The animation, then the blend in and out: "0.5s + 0.1s". No animation picked: only the blend.
		if (Transition->TransitionAnimation)
		{
			return WidgetAnimMachineStyle::FormatSeconds(Transition->GetTransitionAnimationSeconds()) + TEXT(" + ")
				+ WidgetAnimMachineStyle::FormatSeconds(Transition->GetTweenSeconds());
		}
		return WidgetAnimMachineStyle::FormatSeconds(Transition->GetTweenSeconds());
	default:
		return WidgetAnimMachineStyle::FormatSeconds(Transition->GetBlendSeconds());
	}
}

void UWidgetAnimMachineTransitionNode::GetPriority(int32& OutPriority, int32& OutCount) const
{
	OutPriority = 0;
	OutCount = 0;
	const UWidgetAnimMachineGraph* Graph = GetMachineGraph();
	const FWidgetAnimMachineLayer* Layer = Graph ? Graph->GetLayer() : nullptr;
	if (!Layer)
	{
		return;
	}
	for (const FWidgetAnimMachineTransition& Transition : Layer->Transitions)
	{
		if (Transition.From != FromId)
		{
			continue;
		}
		++OutCount;
		if (Transition.Id == TransitionId)
		{
			OutPriority = OutCount;
		}
	}
}

FText UWidgetAnimMachineTransitionNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const UWidgetAnimMachineGraph* Graph = GetMachineGraph();
	const FWidgetAnimMachineLayer* Layer = Graph ? Graph->GetLayer() : nullptr;
	const FWidgetAnimMachineState* From = Layer ? Layer->FindState(FromId) : nullptr;
	const FWidgetAnimMachineState* To = Layer ? Layer->FindState(ToId) : nullptr;
	return FText::Format(LOCTEXT("TransitionTitle", "{0} → {1}"),
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
	const TWeakObjectPtr<const UWidgetAnimMachineTransitionNode> WeakSelf(this);
	auto MovePriority = [WeakSelf](int32 Delta)
	{
		const UWidgetAnimMachineTransitionNode* Node = WeakSelf.Get();
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

	const FWidgetAnimMachineTransition* Transition = GetTransition();
	const bool bEnabled = !Transition || Transition->bEnabled;
	Section.AddMenuEntry(
		TEXT("ToggleTransition"),
		bEnabled ? LOCTEXT("DisableTransition", "Disable Transition") : LOCTEXT("EnableTransition", "Enable Transition"),
		MachineTips::ToggleTransitionTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakSelf, bEnabled]()
		{
			const UWidgetAnimMachineTransitionNode* Node = WeakSelf.Get();
			UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
			if (Graph)
			{
				WidgetAnimMachineEdits::SetTransitionsEnabled(Graph->Machine, Graph->LayerIndex, { Node->TransitionId }, !bEnabled);
			}
		})));
	// Dragged somewhere of its own: back onto the middle of its wire.
	if (bPlaced)
	{
		const TWeakObjectPtr<UWidgetAnimMachineTransitionNode> WeakMutable(const_cast<UWidgetAnimMachineTransitionNode*>(this));
		Section.AddMenuEntry(
			TEXT("ResetTransitionNode"),
			LOCTEXT("ResetTransitionNode", "Reset Node Position"),
			MachineTips::ResetTransitionNodeTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakMutable]()
			{
				UWidgetAnimMachineTransitionNode* Node = WeakMutable.Get();
				UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
				if (Graph)
				{
					Node->bPlaced = false;
					Node->PlacedOffset = FVector2f::ZeroVector;
					TMap<FGuid, TOptional<FVector2D>> Offsets;
					Offsets.Add(Node->TransitionId, TOptional<FVector2D>());
					WidgetAnimMachineEdits::PlaceTransitions(Graph->Machine, Graph->LayerIndex, Offsets);
				}
			})));
	}
	Section.AddMenuEntry(FGenericCommands::Get().Delete);
}

// =====================================================================================================
// Schema
// =====================================================================================================

namespace
{
	/**
	 * Right-click > Add State: a state where the menu was opened - joined from the state a drag came from, if one did. With a
	 * Preset, one named for a built-in input (Hovered, Pressed...), its transition from the dragged state testing that input.
	 */
	struct FWidgetAnimMachineAction_AddState : public FEdGraphSchemaAction
	{
		FName Preset;

		FWidgetAnimMachineAction_AddState(FName InPreset, const FText& InCategory, const FText& InName, const FText& InToolTip, int32 InGrouping)
			: FEdGraphSchemaAction(InCategory, InName, InToolTip, InGrouping)
			, Preset(InPreset)
		{
		}

		// The location is a FVector2f since 5.6; the FVector2D form is deprecated.
		using FEdGraphSchemaAction::PerformAction;
		virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode = true) override
		{
			UWidgetAnimMachineGraph* Graph = Cast<UWidgetAnimMachineGraph>(ParentGraph);
			if (Graph && Graph->Machine)
			{
				UWidgetAnimMachineGraphSchema::AddPresetState(Graph, Preset, FVector2D(Location), UWidgetAnimMachineGraphSchema::GetStateNode(FromPin));
			}
			return nullptr;
		}
	};

	/** Right-click > Add Comment: a comment box where the menu was opened. */
	struct FWidgetAnimMachineAction_AddComment : public FEdGraphSchemaAction
	{
		FWidgetAnimMachineAction_AddComment()
			: FEdGraphSchemaAction(LOCTEXT("StatesCategory", "State Machine"), LOCTEXT("AddCommentAction", "Add Comment"), MachineTips::AddCommentTooltip(), 1)
		{
		}

		using FEdGraphSchemaAction::PerformAction;
		virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2f& Location, bool bSelectNewNode = true) override
		{
			UWidgetAnimMachineGraph* Graph = Cast<UWidgetAnimMachineGraph>(ParentGraph);
			if (Graph && Graph->Machine)
			{
				const FVector2D Min(Location);
				WidgetAnimMachineEdits::AddComment(Graph->Machine, Graph->LayerIndex, FBox2D(Min, Min + FVector2D(400.0, 200.0)), LOCTEXT("NewComment", "Comment").ToString());
				Graph->RequestRebuild();
			}
			return nullptr;
		}
	};
}

UWidgetAnimMachineGraphNode* UWidgetAnimMachineGraphSchema::GetStateNode(const UEdGraphPin* Pin)
{
	return Pin ? Cast<UWidgetAnimMachineGraphNode>(Pin->GetOwningNode()) : nullptr;
}

const TArray<TPair<FName, FName>>& UWidgetAnimMachineGraphSchema::GetPresetStates()
{
	static const TArray<TPair<FName, FName>> Presets =
	{
		TPair<FName, FName>(TEXT("Idle"), NAME_None),
		TPair<FName, FName>(WidgetAnimMachineInputs::Hovered, WidgetAnimMachineInputs::Hovered),
		TPair<FName, FName>(WidgetAnimMachineInputs::Pressed, WidgetAnimMachineInputs::Pressed),
		TPair<FName, FName>(WidgetAnimMachineInputs::Selected, WidgetAnimMachineInputs::Selected),
		TPair<FName, FName>(WidgetAnimMachineInputs::Focused, WidgetAnimMachineInputs::Focused),
		TPair<FName, FName>(WidgetAnimMachineInputs::Disabled, WidgetAnimMachineInputs::Disabled),
	};
	return Presets;
}

FGuid UWidgetAnimMachineGraphSchema::AddPresetState(UWidgetAnimMachineGraph* Graph, FName Preset, const FVector2D& Location, const UWidgetAnimMachineGraphNode* From)
{
	if (!Graph || !Graph->Machine)
	{
		return FGuid();
	}

	FName Input;
	if (const TPair<FName, FName>* Found = GetPresetStates().FindByPredicate([Preset](const TPair<FName, FName>& Candidate) { return Candidate.Key == Preset; }))
	{
		Input = Found->Value;
	}

	// One undo for all of it.
	const FScopedTransaction Transaction(LOCTEXT("AddStateFromMenu", "Add State"));
	const FGuid StateId = WidgetAnimMachineEdits::AddState(Graph->Machine, Graph->LayerIndex, Preset, Location);
	if (From && StateId.IsValid())
	{
		const FGuid TransitionId = WidgetAnimMachineEdits::AddTransition(Graph->Machine, Graph->LayerIndex, From->StateId, StateId);
		if (TransitionId.IsValid() && !Input.IsNone())
		{
			WidgetAnimMachineEdits::AddCondition(Graph->Machine, Graph->LayerIndex, TransitionId, Input, EWidgetAnimMachineCompare::IsTrue);
		}
	}
	Graph->RequestRebuild();
	return StateId;
}

void UWidgetAnimMachineGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	// From a drag off Exit there is nowhere to go.
	const UWidgetAnimMachineGraphNode* FromNode = GetStateNode(ContextMenuBuilder.FromPin);
	if (FromNode && FromNode->Kind == EWidgetAnimMachineNodeKind::Exit)
	{
		return;
	}

	ContextMenuBuilder.AddAction(MakeShared<FWidgetAnimMachineAction_AddState>(NAME_None, LOCTEXT("StatesCategory", "State Machine"), LOCTEXT("AddState", "Add State"), MachineTips::AddStateActionTooltip(), 2));

	// Ready-named states for the built-in inputs - those already in the layer left out.
	const UWidgetAnimMachineGraph* Graph = Cast<UWidgetAnimMachineGraph>(ContextMenuBuilder.CurrentGraph);
	const FWidgetAnimMachineLayer* Layer = Graph ? Graph->GetLayer() : nullptr;
	for (const TPair<FName, FName>& Preset : GetPresetStates())
	{
		if (Layer && Layer->FindStateIndexByName(Preset.Key) != INDEX_NONE)
		{
			continue;
		}
		const FText Name = FText::Format(LOCTEXT("AddPresetState", "Add {0} State"), FText::FromName(Preset.Key));
		const FText ToolTip = Preset.Value.IsNone()
			? MachineTips::AddIdleStateTooltip()
			: FText::Format(MachineTips::AddPresetStateTooltip(), FText::FromName(Preset.Key), FText::FromName(Preset.Value));
		ContextMenuBuilder.AddAction(MakeShared<FWidgetAnimMachineAction_AddState>(Preset.Key, LOCTEXT("CommonStatesCategory", "State Machine|Common States"), Name, ToolTip, 1));
	}

	if (!ContextMenuBuilder.FromPin)
	{
		ContextMenuBuilder.AddAction(MakeShared<FWidgetAnimMachineAction_AddComment>());
	}
}

const FPinConnectionResponse UWidgetAnimMachineGraphSchema::CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const
{
	const UWidgetAnimMachineGraphNode* NodeA = GetStateNode(A);
	const UWidgetAnimMachineGraphNode* NodeB = GetStateNode(B);
	if (!NodeA || !NodeB)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("OnlyStates", "Join two states"));
	}

	// Dragged onto a node anywhere - its edge is its output - the transition goes from the one dragged from to the one dropped on.
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

/** A state's edge: what a transition is dragged from, and dropped on. Under the whole node; only its ring shows - and takes the mouse. */
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

	/**
	 * While this returns true - its state glowing, in play - the edge doesn't light up under the mouse itself: its hover brush
	 * has tighter corners than the glow laid over the rim, and they would poke out past it. The glow brightens instead.
	 */
	void SetHoverShownElsewhere(TFunction<bool()> InHoverShownElsewhere)
	{
		HoverShownElsewhere = MoveTemp(InHoverShownElsewhere);
	}

protected:

	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		return SNew(STextBlock);
	}

	bool ShowsHover() const
	{
		return IsHovered() && !(HoverShownElsewhere && HoverShownElsewhere());
	}

	/** The Animation Blueprint state's own edge - see-through, so the node's color shows through it. */
	const FSlateBrush* GetPinBorder() const
	{
		return ShowsHover()
			? FindBrush({ TEXT("Graph.StateNode.Pin.BackgroundHovered") })
			: FindBrush({ TEXT("Graph.StateNode.Pin.Background") });
	}

	FSlateColor GetPinBorderColor() const
	{
		return ShowsHover() ? MachineColors::StateEdgeHovered : MachineColors::StateEdge;
	}

private:

	TFunction<bool()> HoverShownElsewhere;
};

/** A bar rising left to right across what it sits on, clipped to its share of it - a state's progress. Never takes the mouse. */
class SWidgetAnimMachineFill : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineFill)
		: _Alpha(0.f)
		, _Color(FLinearColor::White)
		, _EdgeColor(FLinearColor::Transparent)
		, _EdgeWidth(0.f)
		, _Brush(nullptr)
	{}
		SLATE_ATTRIBUTE(float, Alpha)
		SLATE_ATTRIBUTE(FLinearColor, Color)
		/** The line at its front - where it has got to. */
		SLATE_ARGUMENT(FLinearColor, EdgeColor)
		SLATE_ARGUMENT(float, EdgeWidth)
		SLATE_ARGUMENT(const FSlateBrush*, Brush)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Alpha = InArgs._Alpha;
		Color = InArgs._Color;
		EdgeColor = InArgs._EdgeColor;
		EdgeWidth = InArgs._EdgeWidth;
		Brush = InArgs._Brush;
		SetVisibility(EVisibility::HitTestInvisible);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D::ZeroVector;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const float Amount = FMath::Clamp(Alpha.Get(), 0.f, 1.f);
		const FVector2f Size = AllottedGeometry.GetLocalSize();
		if (Amount <= 0.f || !Brush || Size.X <= 0.f)
		{
			return LayerId;
		}

		// The whole shape, clipped to the part filled - so its rounded corners stay round, and its front edge is square.
		const float FrontX = Size.X * Amount;
		const FGeometry Filled = AllottedGeometry.MakeChild(FVector2f(FrontX, Size.Y), FSlateLayoutTransform());
		OutDrawElements.PushClip(FSlateClippingZone(Filled));
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Brush, ESlateDrawEffect::None, Color.Get() * InWidgetStyle.GetColorAndOpacityTint());

		// Its front: a bright line, so where it has got to reads at a glance - inside the shape, never past its round ends.
		if (EdgeWidth > 0.f && EdgeColor.A > 0.f && Amount < 1.f)
		{
			const float Inset = FMath::Min(3.f, Size.Y * 0.25f);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2f(EdgeWidth, FMath::Max(Size.Y - Inset * 2.f, 1.f)), FSlateLayoutTransform(FVector2f(FrontX - EdgeWidth, Inset))),
				FAppStyle::GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, EdgeColor * InWidgetStyle.GetColorAndOpacityTint());
		}
		OutDrawElements.PopClip();
		return LayerId + 2;
	}

private:

	TAttribute<float> Alpha;
	TAttribute<FLinearColor> Color;
	FLinearColor EdgeColor = FLinearColor::Transparent;
	float EdgeWidth = 0.f;
	const FSlateBrush* Brush = nullptr;
};

/**
 * A value-driven track's stripes from the Widget Animations panel - faint diagonal bands, the track's width and spacing - across
 * what it sits on, inside its rounded corners, scrolling slowly to the right. Over the state the watched widget is in; nothing
 * under them is recolored. Never takes the mouse.
 */
class SWidgetAnimMachineTrackPattern : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineTrackPattern)
		: _Opacity(0.f)
		, _Clock(0.0)
	{}
		SLATE_ATTRIBUTE(float, Opacity)
		/** Seconds the stripes have scrolled for: they stand still while it does (the preview paused). */
		SLATE_ATTRIBUTE(double, Clock)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Opacity = InArgs._Opacity;
		Clock = InArgs._Clock;
		SetVisibility(EVisibility::HitTestInvisible);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D::ZeroVector;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const float Amount = FMath::Clamp(Opacity.Get(), 0.f, 1.f);
		const FVector2f Size = AllottedGeometry.GetLocalSize();
		if (Amount <= 0.f || Size.X <= 0.f || Size.Y <= 0.f)
		{
			return LayerId;
		}

		const FLinearColor Stripe = MachineColors::ActiveTrackStripe;
		const FLinearColor Color = Stripe.CopyWithNewOpacity(Stripe.A * Amount) * InWidgetStyle.GetColorAndOpacityTint();
		const float Width = MachineColors::ActiveTrackStripeWidth;
		const float Spacing = MachineColors::ActiveTrackStripeSpacing;

		// The node's rounded outline, pulled in by half a band so each band's ends stay inside its corners - as a signed
		// distance: below 0 inside, above outside. On a small node its corners are no rounder than half its height.
		const float Pull = Width * 0.5f + 0.5f;
		const FVector2f Half = Size * 0.5f - FVector2f(Pull, Pull);
		if (Half.X <= 0.f || Half.Y <= 0.f)
		{
			return LayerId;
		}
		const FVector2f Middle = Size * 0.5f;
		const float Radius = FMath::Clamp(MachineColors::ActiveTrackRadius - Pull, 0.f, FMath::Min(Half.X, Half.Y));
		auto Distance = [&](const FVector2f& Point)
		{
			const FVector2f Q(FMath::Abs(Point.X - Middle.X) - (Half.X - Radius), FMath::Abs(Point.Y - Middle.Y) - (Half.Y - Radius));
			const FVector2f Outside(FMath::Max(Q.X, 0.f), FMath::Max(Q.Y, 0.f));
			return Outside.Size() + FMath::Min(FMath::Max(Q.X, Q.Y), 0.f) - Radius;
		};

		// Each band a line up to the right through (CenterX, middle), drawn antialiased - so it glides a fraction of a pixel a
		// frame rather than stepping a whole one - from exactly where it enters the outline to where it leaves it. The outline
		// is convex, so along a band its distance dips once: its lowest point is found, then each edge either side of it. A
		// band only grazing a corner is short - it fades with its length, rather than popping out.
		const FVector2f Along(UE_INV_SQRT_2, -UE_INV_SQRT_2);
		const float Reach = Size.X + Size.Y;
		const float Scroll = static_cast<float>(FMath::Fmod(Clock.Get() * static_cast<double>(MachineColors::ActiveTrackStripeSpeed), static_cast<double>(Spacing)));
		const float MiddleY = Size.Y * 0.5f;
		const float FirstX = -FMath::CeilToFloat(MiddleY / Spacing + 1.f) * Spacing + Scroll;
		const float LastX = Size.X + MiddleY + Spacing;
		TArray<FVector2f> Points;
		Points.SetNum(2);
		for (float CenterX = FirstX; CenterX < LastX; CenterX += Spacing)
		{
			const FVector2f Center(CenterX, MiddleY);
			auto DistanceAt = [&](float At) { return Distance(Center + Along * At); };

			// The deepest point along it.
			float Low = -Reach;
			float High = Reach;
			for (int32 Step = 0; Step < 40; ++Step)
			{
				const float A = Low + (High - Low) / 3.f;
				const float B = High - (High - Low) / 3.f;
				if (DistanceAt(A) < DistanceAt(B))
				{
					High = B;
				}
				else
				{
					Low = A;
				}
			}
			const float Deepest = (Low + High) * 0.5f;
			if (DistanceAt(Deepest) >= 0.f)
			{
				continue;
			}

			// Out from it each way to the outline.
			auto Edge = [&](float Outside)
			{
				float In = Deepest;
				float Out = Outside;
				for (int32 Step = 0; Step < 20; ++Step)
				{
					const float Mid = (In + Out) * 0.5f;
					(DistanceAt(Mid) < 0.f ? In : Out) = Mid;
				}
				return In;
			};
			const float Start = Edge(-Reach);
			const float End = Edge(Reach);
			const float Length = End - Start;
			if (Length <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Points[0] = Center + Along * Start;
			Points[1] = Center + Along * End;
			const float Fade = FMath::Clamp(Length / (Width * 3.f), 0.f, 1.f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
				Color.CopyWithNewOpacity(Color.A * Fade), /*bAntialias=*/true, FMath::Min(Width, FMath::Max(Length, 1.f)));
		}
		return LayerId + 1;
	}

private:

	TAttribute<float> Opacity;
	TAttribute<double> Clock;
};

/**
 * What is drawn round a state's edge, on the same rounded path as the node itself - so every line hugs it and its corners
 * match: while the watched widget is in it, a bright line laid on its rim fading out smoothly beyond it (so which state is in
 * play shows at a glance, even zoomed out); and if it is selected meanwhile, its selection outline too - drawn here rather
 * than by the graph editor, whose selection brush has corners of its own that clash with the glow's. A selected state not in
 * play keeps the graph editor's own selection glow. Drawn outside what it sits on; the node's own colors untouched. Never
 * takes the mouse.
 */
class SWidgetAnimMachineActiveRing : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineActiveRing)
		: _Strength(0.f)
		, _Selected(false)
	{}
		/** How strongly the glow shows, 0 - 1: pulsing while settled in the state, 0 otherwise. */
		SLATE_ATTRIBUTE(float, Strength)
		/** Whether the state is selected: while the glow shows, its selection outline is drawn with it. */
		SLATE_ATTRIBUTE(bool, Selected)
		/** Whether the mouse is over the state's edge - where a transition is dragged from: the line on the rim brightens. */
		SLATE_ATTRIBUTE(bool, Hovered)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Strength = InArgs._Strength;
		Selected = InArgs._Selected;
		Hovered = InArgs._Hovered;
		SetVisibility(EVisibility::HitTestInvisible);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D::ZeroVector;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const float Amount = FMath::Clamp(Strength.Get(), 0.f, 1.f);
		const FVector2f Size = AllottedGeometry.GetLocalSize();
		if (Amount <= 0.f || Size.X <= 0.f || Size.Y <= 0.f)
		{
			return LayerId;
		}
		const bool bSelected = Selected.Get();

		// Each line is the node's rounded outline pushed out by its offset - corners as arcs round the same corner radius
		// grown by the same offset, so every line is concentric with the node - drawn antialiased as one closed path, so its
		// color is exactly what is asked (a rounded box brush's outline isn't tinted).
		const float NodeRadius = FMath::Min(MachineColors::ActiveRingRadius, FMath::Min(Size.X, Size.Y) * 0.5f);
		const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
		const FLinearColor Glow = MachineColors::ActiveRing * Tint;
		const FLinearColor Selection = MachineColors::StateSelectedOutline * Tint;
		struct FLine { float Offset; float Thickness; FLinearColor Color; };
		TArray<FLine, TInlineAllocator<12>> Lines;

		// Over the node's rim: the bright line. Selected meanwhile: the selection outline just outside it, both whole.
		const float RimOffset = MachineColors::ActiveRingInset;
		const float RimWidth = MachineColors::ActiveRingWidth;
		float Outer = RimOffset + RimWidth * 0.5f;
		const float SelectionOffset = Outer + MachineColors::StateSelectedOutlineWidth * 0.5f;
		if (bSelected)
		{
			Outer += MachineColors::StateSelectedOutlineWidth;
		}

		// The glow: thin lines one after another out from the outermost line, each fainter - overlapping, they fade smoothly
		// out rather than in bands. Outermost first, so the lines over the rim are drawn last.
		const int32 HaloSteps = FMath::Max(MachineColors::ActiveRingHaloSteps, 1);
		for (int32 Step = HaloSteps; Step >= 1; --Step)
		{
			const float Out = static_cast<float>(Step) / HaloSteps;
			const float Opacity = MachineColors::ActiveRingHaloOpacity * FMath::Square(1.f - Out + 1.f / HaloSteps) * Amount;
			Lines.Add({ Outer + MachineColors::ActiveRingHaloReach * Out, MachineColors::ActiveRingHaloReach / HaloSteps + 0.75f, Glow.CopyWithNewOpacity(Glow.A * Opacity) });
		}
		if (bSelected)
		{
			Lines.Add({ SelectionOffset, MachineColors::StateSelectedOutlineWidth, Selection });
		}
		// Its edge under the mouse: the line on the rim lit up, as the edge itself lights up on a state not in play.
		const FLinearColor Rim = Hovered.Get()
			? FMath::Lerp(Glow, FLinearColor::White, MachineColors::ActiveRingHoverLift).CopyWithNewOpacity(Glow.A)
			: Glow.CopyWithNewOpacity(Glow.A * Amount);
		Lines.Add({ RimOffset, RimWidth, Rim });

		TArray<FVector2f> Points;
		for (const FLine& Line : Lines)
		{
			const float Radius = FMath::Max(NodeRadius + Line.Offset, 0.5f);
			const FVector2f Min(-Line.Offset, -Line.Offset);
			const FVector2f Max = Size + FVector2f(Line.Offset, Line.Offset);
			const FVector2f Centers[] =
			{
				FVector2f(Max.X - Radius, Min.Y + Radius),
				FVector2f(Max.X - Radius, Max.Y - Radius),
				FVector2f(Min.X + Radius, Max.Y - Radius),
				FVector2f(Min.X + Radius, Min.Y + Radius),
			};
			// Round from the top-right corner, clockwise: each corner a quarter turn starting where the last left off.
			constexpr int32 ArcSegments = 10;
			Points.Reset();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const float StartAngle = -UE_HALF_PI + Corner * UE_HALF_PI;
				for (int32 Segment = 0; Segment <= ArcSegments; ++Segment)
				{
					const float Angle = StartAngle + UE_HALF_PI * Segment / ArcSegments;
					Points.Add(Centers[Corner] + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
				}
			}
			// Closed back on its first point - copied out first: adding an array's own element to it is refused (it may move
			// as the array grows).
			const FVector2f First = Points[0];
			Points.Add(First);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
				Line.Color, /*bAntialias=*/true, Line.Thickness);
		}
		return LayerId + 1;
	}

private:

	TAttribute<float> Strength;
	TAttribute<bool> Selected;
	TAttribute<bool> Hovered;
};

/**
 * A state node: rounded, its name in the middle and its animation under it. Its ring is where a transition is dragged from; its
 * colored interior - all of it - takes clicks, so it is selected and moved from anywhere inside. While the watched widget is in
 * it, a value-driven track's stripes scroll across it (SWidgetAnimMachineTrackPattern) - its color unchanged - and its
 * animation's progress rises across its middle.
 */
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

		const UWidgetAnimMachineGraphNode* MachineNode = GetMachineNode();
		const bool bIsState = MachineNode && MachineNode->Kind == EWidgetAnimMachineNodeKind::State;

		this->ContentScale.Bind(this, &SGraphNode::GetContentScale);
		this->GetOrAddSlot(ENodeZone::Center)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(FindBrush({ TEXT("Graph.StateNode.Body") }))
				.Padding(0.f)
				.BorderBackgroundColor(this, &SWidgetAnimMachineStateNode::GetBodyColor)
				.ToolTip(WidgetAnimMachineStyle::MakeDynamicToolTip([WeakSelf = TWeakPtr<SWidgetAnimMachineStateNode>(SharedThis(this))]() -> TSharedPtr<SWidget>
				{
					const TSharedPtr<SWidgetAnimMachineStateNode> Self = WeakSelf.Pin();
					return Self.IsValid() ? Self->MakeToolTipContent() : nullptr;
				}))
				[
					SNew(SOverlay)

					// While the watched widget is in it: a soft glow just outside it, pulsing.
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SNew(SWidgetAnimMachineActiveRing)
						.Strength(this, &SWidgetAnimMachineStateNode::GetRingStrength)
						.Selected(this, &SWidgetAnimMachineStateNode::IsNodeSelected)
						.Hovered(this, &SWidgetAnimMachineStateNode::IsEdgeHovered)
					]

					// The edge: where transitions are dragged from.
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SAssignNew(RightNodeBox, SVerticalBox)
					]

					// While the watched widget is in it: a value-driven track's stripes across it, scrolling right. Over the edge,
					// under the name; the mouse goes straight through them.
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SNew(SWidgetAnimMachineTrackPattern)
						.Opacity(this, &SWidgetAnimMachineStateNode::GetTrackOpacity)
						.Clock(this, &SWidgetAnimMachineStateNode::GetStripeClock)
					]

					// The name, and the animation under it. The whole colored part takes the mouse - a click anywhere in it
					// selects and drags the node; only the edge round it starts a transition.
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(10.f)
					[
						SNew(SBorder)
						.BorderImage(this, &SWidgetAnimMachineStateNode::GetTitleBrush)
						.BorderBackgroundColor(this, &SWidgetAnimMachineStateNode::GetTitleColor)
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						.Visibility(EVisibility::Visible)
						.Padding(0.f)
						[
							SNew(SOverlay)

							// Its animation's progress, while the watched widget plays it.
							+ SOverlay::Slot()
							[
								SNew(SWidgetAnimMachineFill)
								.Brush(FAppStyle::GetBrush(TEXT("WhiteBrush")))
								.Alpha(this, &SWidgetAnimMachineStateNode::GetProgress)
								.Color(this, &SWidgetAnimMachineStateNode::GetProgressColor)
								.EdgeColor(MachineColors::StateProgressEdge)
								.EdgeWidth(MachineColors::StateProgressEdgeWidth)
							]

							+ SOverlay::Slot()
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
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
										.Visibility(bIsState ? EVisibility::Visible : EVisibility::Collapsed)
									]
								]
							]
						]
					]
				]
			];

		// Its note, in a bubble over it - as a Blueprint node's comment is. Typed into, it is the machine's (see the editor).
		if (bIsState)
		{
			const TWeakObjectPtr<UWidgetAnimMachineGraphNode> WeakNode(Cast<UWidgetAnimMachineGraphNode>(GraphNode));
			const TSharedRef<SCommentBubble> Bubble = SNew(SCommentBubble)
				.GraphNode(GraphNode)
				.Text(this, &SWidgetAnimMachineStateNode::GetNodeComment)
				.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.9f))
				.EnableTitleBarBubble(false)
				.EnableBubbleCtrls(false)
				.AllowPinning(false)
				.OnTextCommitted_Lambda([WeakNode](const FText& NewText, ETextCommit::Type)
				{
					const UWidgetAnimMachineGraphNode* Node = WeakNode.Get();
					UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
					if (Graph)
					{
						WidgetAnimMachineEdits::SetStateComment(Graph->Machine, Graph->LayerIndex, Node->StateId, NewText.ToString(), !NewText.IsEmpty());
					}
				});
			const TWeakPtr<SCommentBubble> WeakBubble = Bubble;
			GetOrAddSlot(ENodeZone::TopCenter)
				.VAlign(VAlign_Top)
				[
					Bubble
				];

			// Where it sits, and how big it is, as the bubble says - set on the slot itself, once it is made. (The node's slot
			// list is private: reached through its children, every one of which is a node slot.)
			FChildren* NodeChildren = GetChildren();
			for (int32 SlotIndex = 0; NodeChildren && SlotIndex < NodeChildren->Num(); ++SlotIndex)
			{
				FNodeSlot& BubbleSlot = const_cast<FNodeSlot&>(static_cast<const FNodeSlot&>(NodeChildren->GetSlotAt(SlotIndex)));
				if (BubbleSlot.GetZoneType() != ENodeZone::TopCenter)
				{
					continue;
				}
				BubbleSlot.SetSlotOffset(TAttribute<FVector2f>::CreateLambda([WeakBubble]()
				{
					const TSharedPtr<SCommentBubble> Pinned = WeakBubble.Pin();
					return Pinned.IsValid() ? FVector2f(Pinned->GetOffset2f()) : FVector2f::ZeroVector;
				}));
				BubbleSlot.SetSlotSize(TAttribute<FVector2f>::CreateLambda([WeakBubble]()
				{
					const TSharedPtr<SCommentBubble> Pinned = WeakBubble.Pin();
					return Pinned.IsValid() ? FVector2f(Pinned->GetSize2f()) : FVector2f::ZeroVector;
				}));
				BubbleSlot.SetAllowScaling(TAttribute<bool>::CreateLambda([WeakBubble]()
				{
					const TSharedPtr<SCommentBubble> Pinned = WeakBubble.Pin();
					return Pinned.IsValid() && Pinned->IsScalingAllowed();
				}));
			}
		}

		CreatePinWidgets();
	}

	virtual void CreatePinWidgets() override
	{
		UWidgetAnimMachineGraphNode* Node = CastChecked<UWidgetAnimMachineGraphNode>(GraphNode);
		UEdGraphPin* Out = Node->GetOutputPin();
		if (Out && !Out->bHidden)
		{
			// While it glows, the edge's hover shows on the glow (see GetRingHovered), not on the edge's own brush.
			const TSharedRef<SWidgetAnimMachineOutputPin> Pin = SNew(SWidgetAnimMachineOutputPin, Out);
			const TWeakPtr<SWidgetAnimMachineStateNode> WeakSelf = SharedThis(this);
			Pin->SetHoverShownElsewhere([WeakSelf]()
			{
				const TSharedPtr<SWidgetAnimMachineStateNode> Self = WeakSelf.Pin();
				return Self.IsValid() && Self->GetRingStrength() > 0.f;
			});
			AddPin(Pin);
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

	/**
	 * Selected while it glows (in play, or being left): no selection brush from the graph editor - its corners are its own and
	 * clash with the glow's. Its selection outline is drawn with the glow, on its own rounded edge (SWidgetAnimMachineActiveRing).
	 * Selected otherwise: the graph editor's own selection glow, as ever.
	 */
	virtual const FSlateBrush* GetShadowBrush(bool bSelected) const override
	{
		return bSelected && GetRingStrength() > 0.f ? FAppStyle::GetNoBrush() : SGraphNode::GetShadowBrush(bSelected);
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

	/**
	 * Whether the watched widget is in this state - settled in it: not while a transition plays. Mid-transition no state wears
	 * the active look - the one being left loses it the moment the transition starts, the one being entered takes it on once
	 * the transition is done; the wire shows the transition meanwhile.
	 */
	bool IsActive() const
	{
		FWidgetAnimMachineLayerDebug Debug;
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		return Node && GetDebug(Debug) && Debug.CurrentState == Node->StateId && !Debug.ActiveTransition.IsValid();
	}


	/** The node's own Node Color, if it is given one. */
	bool GetCustomColor(FLinearColor& OutColor) const
	{
#if WITH_EDITORONLY_DATA
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		const FWidgetAnimMachineState* State = Node ? Node->GetState() : nullptr;
		if (State && State->bOverrideNodeColor)
		{
			OutColor = State->NodeColor.CopyWithNewOpacity(1.f);
			return true;
		}
#endif
		return false;
	}

	/** A Node Color lifted to at least StateProgressMinBrightness: what it is lit and filled in. */
	static FLinearColor Brighten(const FLinearColor& Color)
	{
		FLinearColor HSV = Color.LinearRGBToHSV();
		HSV.B = FMath::Max(HSV.B, MachineColors::StateProgressMinBrightness);
		return HSV.HSVToLinearRGB().CopyWithNewOpacity(1.f);
	}

	FSlateColor GetBodyColor() const
	{
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		if (!Node)
		{
			return MachineColors::StateBody;
		}

		// Its own color, if given one. In play it keeps its color: the stripes over it are what show it is in play.
		FLinearColor Custom;
		if (GetCustomColor(Custom))
		{
			return Custom;
		}
		switch (Node->Kind)
		{
		case EWidgetAnimMachineNodeKind::Entry:    return MachineColors::EntryBody;
		case EWidgetAnimMachineNodeKind::Exit:     return MachineColors::ExitBody;
		case EWidgetAnimMachineNodeKind::AnyState: return MachineColors::AnyStateBody;
		default:                                    return MachineColors::StateBody;
		}
	}

	/**
	 * The glow round it: pulsing between ActiveRingMin and ActiveRingMax every ActiveRingPeriod seconds while the watched
	 * widget is settled in it (see IsActive) - on the graph's clock, so it holds still while the preview is paused - none
	 * otherwise.
	 */
	float GetRingStrength() const
	{
		if (IsActive())
		{
			const double Phase = FMath::Fmod(GetStripeClock(), static_cast<double>(MachineColors::ActiveRingPeriod)) / MachineColors::ActiveRingPeriod;
			const float Wave = 0.5f - 0.5f * FMath::Cos(static_cast<float>(Phase) * UE_TWO_PI);
			return FMath::Lerp(MachineColors::ActiveRingMin, MachineColors::ActiveRingMax, Wave);
		}
		return 0.f;
	}

	/** Whether the mouse is over its edge - the output pin under it all. */
	bool IsEdgeHovered() const
	{
		return OutputPins.Num() > 0 && OutputPins[0]->IsHovered();
	}

	bool IsNodeSelected() const
	{
		const TSharedPtr<SGraphPanel> Panel = OwnerGraphPanelPtr.Pin();
		return Panel.IsValid() && GraphNode && Panel->SelectionManager.IsNodeSelected(GraphNode);
	}

	/** The stripes: while the watched widget is settled in it (see IsActive), none otherwise. */
	float GetTrackOpacity() const
	{
		return IsActive() ? 1.f : 0.f;
	}

	/** The progress fill: the animation orange, or the node's own color brightened. */
	FLinearColor GetProgressColor() const
	{
		FLinearColor Custom;
		const FLinearColor Color = GetCustomColor(Custom) ? Brighten(Custom) : MachineColors::StateProgress;
		return Color.CopyWithNewOpacity(MachineColors::StateProgressOpacity);
	}

	/** Its middle: the Animation Blueprint state's rounded one - square-cornered while its animation's progress fills it. */
	const FSlateBrush* GetTitleBrush() const
	{
		return GetProgress() > 0.f ? WidgetAnimMachineStyle::SharpBoxBrush() : FindBrush({ TEXT("Graph.StateNode.ColorSpill") });
	}

	FSlateColor GetTitleColor() const
	{
		if (GetProgress() > 0.f)
		{
			return MachineColors::StateTitleSharp;
		}
		return IsActive() ? MachineColors::StateTitleActive : MachineColors::StateTitle;
	}

	/** How far its stripes have scrolled: the graph's own clock, which stands still while the preview is paused. */
	double GetStripeClock() const
	{
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		const UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
		return Graph ? Graph->StripeClock : 0.0;
	}

	/** The state's animation's progress while the watched widget is in it - and nothing while it isn't, or it holds still. */
	float GetProgress() const
	{
		FWidgetAnimMachineLayerDebug Debug;
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		if (!Node || !GetDebug(Debug) || Debug.CurrentState != Node->StateId || !Debug.bStatePlaying || Debug.ActiveTransition.IsValid())
		{
			return 0.f;
		}
		return Debug.StateAlpha;
	}

	const FSlateBrush* GetIcon() const
	{
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		const EWidgetAnimMachineNodeKind Kind = Node ? Node->Kind : EWidgetAnimMachineNodeKind::State;
		switch (Kind)
		{
		case EWidgetAnimMachineNodeKind::Entry:    return FindBrush({ TEXT("Graph.EntryNode.Icon"), TEXT("Icons.ArrowRight"), TEXT("Graph.StateNode.Icon") });
		case EWidgetAnimMachineNodeKind::Exit:
			// Its own: a door with the way out - not the state icon, which every state already wears.
			if (const FSlateBrush* Exit = FNameTBDStyle::Get().GetOptionalBrush(FNameTBDStyle::StateMachineExitIcon, nullptr, nullptr))
			{
				return Exit;
			}
			return FindBrush({ TEXT("Icons.Logout"), TEXT("Icons.Export"), TEXT("Icons.ArrowRight") });
		case EWidgetAnimMachineNodeKind::AnyState: return FindBrush({ TEXT("Graph.AliasNode.Icon"), TEXT("Graph.ConduitNode.Icon"), TEXT("Graph.StateNode.Icon") });
		default:                                    return FindBrush({ TEXT("Graph.StateNode.Icon") });
		}
	}

	FText GetName() const
	{
		return GraphNode ? GraphNode->GetNodeTitle(ENodeTitleType::FullTitle) : FText::GetEmpty();
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

	/** Its tooltip: in the plugin's style, what it is and plays, and how to work it. */
	TSharedPtr<SWidget> MakeToolTipContent() const
	{
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		const FWidgetAnimMachineState* State = Node ? Node->GetState() : nullptr;
		if (!Node || !State)
		{
			return nullptr;
		}

		WidgetAnimMachineStyle::FTipBuilder Tip;
		Tip.Title(FText::FromName(State->Name), TipPalette::TooltipControlName);
		if (Node->Kind != EWidgetAnimMachineNodeKind::State)
		{
			Tip.Kind(StaticEnum<EWidgetAnimMachineNodeKind>()->GetDisplayNameTextByValue(static_cast<int64>(Node->Kind)));
			Tip.Paragraph(Node->GetTooltipText());
			return Tip.Build();
		}

		Tip.Kind(LOCTEXT("StateKind", "State"));
		Tip.Row(LOCTEXT("TipAnimation", "Animation"), State->Animation ? FText::FromString(State->Animation->GetName()) : LOCTEXT("TipNoAnimation", "None"));
		if (State->Animation)
		{
			Tip.Row(LOCTEXT("TipPlayback", "Playback"), StaticEnum<EWidgetAnimMachineStatePlayback>()->GetDisplayNameTextByValue(static_cast<int64>(State->Playback)));
			if (!FMath::IsNearlyEqual(State->PlaybackSpeed, 1.f))
			{
				Tip.Row(LOCTEXT("TipSpeed", "Speed"), FText::AsNumber(State->PlaybackSpeed));
			}
			if (!State->ProgressInput.IsNone())
			{
				Tip.Row(LOCTEXT("TipProgress", "Follows"), FText::FromName(State->ProgressInput));
			}
		}

		// Where it can go from here.
		if (const UWidgetAnimMachineGraph* Graph = Node->GetMachineGraph())
		{
			if (const FWidgetAnimMachineLayer* Layer = Graph->GetLayer())
			{
				int32 Out = 0;
				int32 In = 0;
				for (const FWidgetAnimMachineTransition& Transition : Layer->Transitions)
				{
					Out += Transition.From == State->Id ? 1 : 0;
					In += Transition.To == State->Id ? 1 : 0;
				}
				Tip.Row(LOCTEXT("TipTransitions", "Transitions"), FText::Format(LOCTEXT("TipTransitionsValue", "{0} in, {1} out"), In, Out));
			}
		}
		if (!State->Comment.IsEmpty())
		{
			Tip.Divider();
			Tip.Paragraph(FText::FromString(State->Comment));
		}
		Tip.Hint(MachineTips::StateNodeHint());
		return Tip.Build();
	}
};

/**
 * What a transition node draws: the Animation Blueprint's own round transition node, tinted by how it blends - a Tween the
 * plain light gray it always was, a Spring mint, a Cut dark gray, a transition animation the animation orange - and, in place
 * of its double-arrow icon, the state machine wires' own arrowhead, turned the way the transition goes along its wire. While
 * the watched widget takes it, its color rises across it like a progress bar.
 */
class SWidgetAnimMachineTransitionGlyph : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineTransitionGlyph) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakObjectPtr<UWidgetAnimMachineTransitionNode> InNode, TFunction<FVector2f()> InGetDirection, TFunction<bool()> InIsSelected)
	{
		Node = InNode;
		GetDirection = MoveTemp(InGetDirection);
		IsNodeSelected = MoveTemp(InIsSelected);
	}

	/** The round brush - the Animation Blueprint transition node's. */
	static const FSlateBrush* GetDiscBrush()
	{
		return FindBrush({ TEXT("Graph.TransitionNode.ColorSpill") });
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		// As big as the editor draws its own transition node - never smaller than the wires' layout expects.
		const FVector2D BrushSize(GetDiscBrush()->ImageSize);
		const double Side = FMath::Max3(BrushSize.X, BrushSize.Y, static_cast<double>(WidgetAnimMachineWires::NodeDiameter));
		return FVector2D(Side, Side);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const UWidgetAnimMachineTransitionNode* TransitionNode = Node.Get();
		const FWidgetAnimMachineTransition* Transition = TransitionNode ? TransitionNode->GetTransition() : nullptr;
		if (!Transition)
		{
			return LayerId;
		}

		const FVector2f Size = AllottedGeometry.GetLocalSize();
		const FVector2f Center = Size * 0.5f;
		const float Radius = FMath::Min(Size.X, Size.Y) * 0.5f;
		const FSlateBrush* Disc = GetDiscBrush();

		// What the watched widget is doing with it.
		const UWidgetAnimMachineGraph* Graph = TransitionNode->GetMachineGraph();
		FWidgetAnimMachineLayerDebug Debug;
		const bool bPlaying = Graph && Graph->GetDebug && Graph->GetDebug(Debug) && Debug.ActiveTransition == TransitionNode->TransitionId;

		FLinearColor Color = GetBlendColor(Transition->Blend);
		if (!Transition->bEnabled)
		{
			Color = MachineColors::TransitionDisabled;
		}
		else if (IsHovered())
		{
			// Lighter under the mouse. Selected, it keeps its color - as a selected state does - and wears the selection band.
			Color = FMath::Lerp(Color, FLinearColor::White, 0.35f);
		}

		// The disc: dimmed under its progress while it plays, its full color rising across it.
		const FLinearColor Base = bPlaying ? (Color * MachineColors::TransitionIdleDim).CopyWithNewOpacity(1.f) : Color;
		const float Progress = bPlaying ? FMath::Clamp(Debug.TransitionAlpha, 0.f, 1.f) : 0.f;
		const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();

		// A true circle, Radius about the middle, at any zoom: a rounded box rounded by half its height. Not snapped to whole
		// pixels: snapped, each circle's box rounds its own way - the band's and the node's by different amounts - and the
		// band shows thicker on one side than the other, differently wherever the node sits.
		const FSlateBrush* Round = WidgetAnimMachineStyle::RoundBrush();
		auto DrawCircle = [&](int32 Layer, float CircleRadius, const FLinearColor& CircleColor)
		{
			const FVector2f CircleSize(CircleRadius * 2.f, CircleRadius * 2.f);
			FSlateDrawElement::MakeBox(OutDrawElements, Layer,
				AllottedGeometry.ToPaintGeometry(CircleSize, FSlateLayoutTransform(Center - CircleSize * 0.5f)), Round, ESlateDrawEffect::NoPixelSnapping, CircleColor);
		};
		auto ClipLeftPart = [&](float Amount)
		{
			const FGeometry Filled = AllottedGeometry.MakeChild(FVector2f(Size.X * Amount, Size.Y), FSlateLayoutTransform());
			OutDrawElements.PushClip(FSlateClippingZone(Filled));
		};

		if (IsNodeSelected && IsNodeSelected())
		{
			// Selected: the graph editor's own selection look, measured off a selected state, made round - a crisp solid band
			// of the selection orange over the node's edge and just outside it, shading from yellower at the top to deeper
			// orange at the bottom as the editor's selection brush does, with no glow beyond it. A filled circle under a
			// smaller one in the node's color - each a true circle, so the band is perfectly round, even, and whole (the graph
			// editor's own selection brush, a rounded box, is held back: see GetShadowBrush). Its shading in thin horizontal
			// strips, too fine to see as steps.
			const float BandInner = Radius - MachineColors::TransitionSelectedOverlap;
			const float BandOuter = BandInner + MachineColors::TransitionSelectedWidth;
			constexpr int32 Strips = 16;
			const float Top = Center.Y - BandOuter;
			const float StripHeight = BandOuter * 2.f / Strips;
			for (int32 Strip = 0; Strip < Strips; ++Strip)
			{
				const FGeometry StripGeometry = AllottedGeometry.MakeChild(
					FVector2f(Size.X + BandOuter * 4.f, StripHeight + 0.5f),
					FSlateLayoutTransform(FVector2f(-BandOuter * 2.f, Top + StripHeight * Strip)));
				OutDrawElements.PushClip(FSlateClippingZone(StripGeometry));
				const float Down = (Strip + 0.5f) / Strips;
				DrawCircle(LayerId, BandOuter, FMath::Lerp(MachineColors::TransitionSelectedTop, MachineColors::TransitionSelectedBottom, Down) * Tint);
				OutDrawElements.PopClip();
			}

			// The node inside the band, its own rim covered as a selected state's is.
			DrawCircle(LayerId + 1, BandInner, Base * Tint);
			if (Progress > 0.f)
			{
				ClipLeftPart(Progress);
				DrawCircle(LayerId + 2, BandInner, Color * Tint);
				OutDrawElements.PopClip();
			}
		}
		else
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Disc, ESlateDrawEffect::None, Base);
			if (Progress > 0.f)
			{
				ClipLeftPart(Progress);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Disc, ESlateDrawEffect::None, Color);
				OutDrawElements.PopClip();
			}
		}

		// The arrowhead - the one an Animation Blueprint state machine's wires end in - turned to point the way the transition
		// goes along its wire, in the middle of the node.
		FVector2f Direction = GetDirection ? GetDirection() : FVector2f(1.f, 0.f);
		Direction = Direction.IsNearlyZero() ? FVector2f(1.f, 0.f) : Direction.GetSafeNormal();
		if (const FSlateBrush* Arrow = FAppStyle::Get().GetOptionalBrush(TEXT("Graph.Arrow"), nullptr, nullptr))
		{
			const float Angle = FMath::Atan2(Direction.Y, Direction.X);
			const FVector2f ArrowSize(MachineColors::TransitionArrowSize, MachineColors::TransitionArrowSize);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 4,
				AllottedGeometry.ToPaintGeometry(ArrowSize, FSlateLayoutTransform(Center - ArrowSize * 0.5f), FSlateRenderTransform(FQuat2f(Angle)), FVector2f(0.5f, 0.5f)),
				Arrow, ESlateDrawEffect::None, MachineColors::TransitionArrow);
		}
		return LayerId + 5;
	}

private:

	TWeakObjectPtr<UWidgetAnimMachineTransitionNode> Node;
	TFunction<FVector2f()> GetDirection;
	TFunction<bool()> IsNodeSelected;
};

/** A transition's node, on its wire - see FWidgetAnimMachineConnectionDrawingPolicy, which draws the wire. */
class SWidgetAnimMachineTransitionNode : public SGraphNode
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineTransitionNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWidgetAnimMachineTransitionNode* InNode)
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

		const TWeakPtr<SWidgetAnimMachineTransitionNode> WeakSelf = SharedThis(this);
		this->ContentScale.Bind(this, &SGraphNode::GetContentScale);
		this->GetOrAddSlot(ENodeZone::Center)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SWidgetAnimMachineTransitionGlyph,
					TWeakObjectPtr<UWidgetAnimMachineTransitionNode>(Cast<UWidgetAnimMachineTransitionNode>(GraphNode)),
					[WeakSelf]() { const TSharedPtr<SWidgetAnimMachineTransitionNode> Self = WeakSelf.Pin(); return Self.IsValid() ? Self->Direction : FVector2f(1.f, 0.f); },
					[WeakSelf]() { const TSharedPtr<SWidgetAnimMachineTransitionNode> Self = WeakSelf.Pin(); return Self.IsValid() && Self->IsSelected(); })
				.ToolTip(WidgetAnimMachineStyle::MakeDynamicToolTip([WeakSelf]() -> TSharedPtr<SWidget>
				{
					const TSharedPtr<SWidgetAnimMachineTransitionNode> Self = WeakSelf.Pin();
					return Self.IsValid() ? Self->MakeToolTipContent() : nullptr;
				}))
			];
	}

	virtual void CreatePinWidgets() override
	{
		// Its pins are where the wire goes in and out: never drawn, never dragged.
	}

	/** No selection brush from the graph editor - a rounded box round a round node. The node draws its own circle (see the glyph). */
	virtual const FSlateBrush* GetShadowBrush(bool bSelected) const override
	{
		return FAppStyle::GetNoBrush();
	}

	virtual bool RequiresSecondPassLayout() const override
	{
		return true;
	}

	/**
	 * Dragged by the graph. The graph editor's own MoveTo leaves alone any node it lays out itself (as this one is, on its
	 * wire) - so this one takes the move, and PerformSecondPassLayout keeps it there from then on.
	 */
	virtual void MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override
	{
		if (!GraphNode || NodeFilter.Find(SharedThis(this)))
		{
			return;
		}
		NodeFilter.Add(SharedThis(this));
		GraphNode->NodePosX = FMath::RoundToInt32(NewPosition.X);
		GraphNode->NodePosY = FMath::RoundToInt32(NewPosition.Y);
	}

	/** Alt+click: deletes the transition - as Alt+click breaks a Blueprint wire. */
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && MouseEvent.IsAltDown())
		{
			const UWidgetAnimMachineTransitionNode* Node = Cast<UWidgetAnimMachineTransitionNode>(GraphNode);
			UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
			if (Graph)
			{
				WidgetAnimMachineEdits::Delete(Graph->Machine, Graph->LayerIndex, TArray<FGuid>(), { Node->TransitionId });
				return FReply::Handled();
			}
		}
		return SGraphNode::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	/**
	 * On its wire, where WidgetAnimMachineWires::Compute puts it - the same place the wire is drawn through. Dragged (moved by
	 * the graph since it was last put here), it stays where it was dropped, and its wire runs through it from then on.
	 */
	virtual void PerformSecondPassLayout(const TMap<UObject*, TSharedRef<SNodePanel::SNode>>& NodeToWidgetLookup) const override
	{
		UWidgetAnimMachineTransitionNode* Node = Cast<UWidgetAnimMachineTransitionNode>(GraphNode);
		const UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
		const TSharedPtr<SGraphPanel> Panel = OwnerGraphPanelPtr.Pin();
		if (!Node || !Graph || !Panel.IsValid())
		{
			return;
		}

		FGuid FirstId;
		FGuid SecondId;
		UWidgetAnimMachineGraph::GetPairOrder(Node, FirstId, SecondId);
		const UWidgetAnimMachineGraphNode* First = Graph->FindStateNode(FirstId);
		const UWidgetAnimMachineGraphNode* Second = Graph->FindStateNode(SecondId);
		const TSharedPtr<SGraphNode> FirstWidget = First ? Panel->GetNodeWidgetFromGuid(First->NodeGuid) : nullptr;
		const TSharedPtr<SGraphNode> SecondWidget = Second ? Panel->GetNodeWidgetFromGuid(Second->NodeGuid) : nullptr;
		if (!FirstWidget.IsValid() || !SecondWidget.IsValid())
		{
			return;
		}

		auto BoxOf = [](const UEdGraphNode* StateNode, const TSharedPtr<SGraphNode>& Widget)
		{
			const FVector2f Min(static_cast<float>(StateNode->NodePosX), static_cast<float>(StateNode->NodePosY));
			return FBox2f(Min, Min + FVector2f(FVector2D(Widget->GetDesiredSize())));
		};

		const FBox2f FirstBox = BoxOf(First, FirstWidget);
		const FBox2f SecondBox = BoxOf(Second, SecondWidget);
		const FVector2f Anchor = WidgetAnimMachineWires::GetPlacementAnchor(FirstBox, SecondBox);
		const FVector2f MySize = FVector2f(FVector2D(GetDesiredSize()));

		// Moved since it was put here: dragged - on its own or with others. It stays where it was dropped.
		const FIntPoint Current(GraphNode->NodePosX, GraphNode->NodePosY);
		if (bLaidOut && Current != LastLaidOut)
		{
			Node->bPlaced = true;
			Node->PlacedOffset = FVector2f(static_cast<float>(Current.X), static_cast<float>(Current.Y)) + MySize * 0.5f - Anchor;
		}

		int32 Lane = 0;
		int32 Count = 1;
		bool bReversed = false;
		Graph->GetLane(Node, Lane, Count, bReversed);
		const FVector2f PlacedPoint = Anchor + Node->PlacedOffset;
		const FWidgetAnimMachineWire Wire = WidgetAnimMachineWires::Compute(FirstBox, SecondBox, Lane, Count, bReversed, Node->bPlaced ? &PlacedPoint : nullptr);
		Direction = Wire.NodeDirection;

		const FVector2f Position = Wire.NodePoint - MySize * 0.5f;
		GraphNode->NodePosX = FMath::RoundToInt32(Position.X);
		GraphNode->NodePosY = FMath::RoundToInt32(Position.Y);
		LastLaidOut = FIntPoint(GraphNode->NodePosX, GraphNode->NodePosY);
		bLaidOut = true;
	}

private:

	/** The way its wire runs through it - set as it is placed, read as it is drawn. */
	mutable FVector2f Direction = FVector2f(1.f, 0.f);

	/** Where it was last put, so a move by the graph - a drag - is told from its own placing. */
	mutable FIntPoint LastLaidOut = FIntPoint::ZeroValue;
	mutable bool bLaidOut = false;

	bool IsSelected() const
	{
		const TSharedPtr<SGraphPanel> Panel = OwnerGraphPanelPtr.Pin();
		return Panel.IsValid() && GraphNode && Panel->SelectionManager.IsNodeSelected(GraphNode);
	}

	/** Its tooltip: from where to where, when it is taken and how it blends. */
	TSharedPtr<SWidget> MakeToolTipContent() const
	{
		const UWidgetAnimMachineTransitionNode* Node = Cast<UWidgetAnimMachineTransitionNode>(GraphNode);
		const FWidgetAnimMachineTransition* Transition = Node ? Node->GetTransition() : nullptr;
		if (!Transition)
		{
			return nullptr;
		}

		WidgetAnimMachineStyle::FTipBuilder Tip;
		Tip.Title(Node->GetNodeTitle(ENodeTitleType::FullTitle), TipPalette::TooltipControlName);
		Tip.Kind(Transition->bEnabled ? LOCTEXT("TransitionKind", "Transition") : LOCTEXT("TransitionKindDisabled", "Transition - disabled"));
		Tip.Row(LOCTEXT("TipWhen", "When"), Node->DescribeConditions());

		FText Blend = GetBlendName(Transition->Blend);
		if (Transition->Blend == EWidgetAnimMachineBlend::Animation && Transition->TransitionAnimation)
		{
			// "Animation: " and its name, in bold.
			Tip.Row(LOCTEXT("TipBlend", "Blend"), LOCTEXT("TipBlendAnimation", "Animation: "), FText::FromString(Transition->TransitionAnimation->GetName()), GetBlendColor(Transition->Blend));
		}
		else
		{
			if (Transition->Blend == EWidgetAnimMachineBlend::Animation)
			{
				Blend = LOCTEXT("TipBlendNoAnimation", "Animation: None");
			}
			else if (Transition->Blend == EWidgetAnimMachineBlend::Tween)
			{
				Blend = FText::Format(LOCTEXT("TipBlendTween", "Tween, {0}"), StaticEnum<EWidgetAnimEasing>()->GetDisplayNameTextByValue(static_cast<int64>(Transition->Easing)));
			}
			Tip.Row(LOCTEXT("TipBlend", "Blend"), Blend, GetBlendColor(Transition->Blend));
		}
		if (Transition->Blend != EWidgetAnimMachineBlend::Cut)
		{
			Tip.Row(LOCTEXT("TipLength", "Takes"), FText::FromString(Node->GetLengthLabel()));
		}
		if (Transition->ExitTime == EWidgetAnimMachineExitTime::AtProgress)
		{
			Tip.Row(LOCTEXT("TipExitTime", "Exit Time"), FText::Format(LOCTEXT("TipExitAt", "after {0}% of the state"), FText::AsNumber(FMath::RoundToInt(Transition->ExitAtProgress * 100.f))));
		}
		else if (Transition->ExitTime == EWidgetAnimMachineExitTime::WhenFinished)
		{
			Tip.Row(LOCTEXT("TipExitTime", "Exit Time"), LOCTEXT("TipExitFinished", "once the state has played"));
		}
		int32 Priority = 0;
		int32 Count = 0;
		Node->GetPriority(Priority, Count);
		if (Count > 1)
		{
			Tip.Row(LOCTEXT("TipPriority", "Priority"), FText::Format(LOCTEXT("TipPriorityValue", "{0} of {1}"), Priority, Count));
		}
		if (!Transition->bInterruptible)
		{
			Tip.Row(LOCTEXT("TipInterruptible", "Interruptible"), LOCTEXT("TipNo", "No"));
		}

		// A transition animation: what the animation is for, in its own words.
		if (Transition->Blend == EWidgetAnimMachineBlend::Animation && Transition->TransitionAnimation && !Transition->TransitionAnimation->Description.IsEmpty())
		{
			Tip.Divider();
			Tip.Paragraph(Transition->TransitionAnimation->Description);
		}
		return Tip.Build();
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
// Wires
// =====================================================================================================

/**
 * Draws every transition as a Blueprint wire from state to state, through its transition node, and beside the node how long the
 * transition takes. The transition the watched widget is taking runs with dots, as a wire does in Blueprint simulation. The pins
 * themselves are never drawn.
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

		// Every node's box, in graph space - where the transition nodes were placed from - and on screen. One scale and one
		// origin take the one to the other.
		TMap<const UEdGraphNode*, FBox2f> GraphBoxes;
		TMap<const UEdGraphNode*, FBox2f> ScreenBoxes;
		float Scale = 0.f;
		FVector2f Origin = FVector2f::ZeroVector;
		for (int32 Index = 0; Index < ArrangedNodes.Num(); ++Index)
		{
			const FArrangedWidget& Arranged = ArrangedNodes[Index];
			const TSharedRef<SGraphNode> NodeWidget = StaticCastSharedRef<SGraphNode>(Arranged.Widget);
			const UEdGraphNode* NodeObj = NodeWidget->GetNodeObj();
			if (!NodeObj)
			{
				continue;
			}
			const FVector2f Position(Arranged.Geometry.GetAbsolutePosition());
			const FVector2f Size(Arranged.Geometry.GetAbsoluteSize());
			ScreenBoxes.Add(NodeObj, FBox2f(Position, Position + Size));

			const FVector2f GraphMin(static_cast<float>(NodeObj->NodePosX), static_cast<float>(NodeObj->NodePosY));
			GraphBoxes.Add(NodeObj, FBox2f(GraphMin, GraphMin + FVector2f(FVector2D(NodeWidget->GetDesiredSize()))));
			if (Scale <= 0.f && NodeObj->IsA<UWidgetAnimMachineGraphNode>())
			{
				Scale = Arranged.Geometry.Scale;
				Origin = Position - GraphMin * Scale;
			}
		}
		if (Scale <= 0.f)
		{
			return;
		}
		auto ToScreen = [Scale, Origin](const FVector2f& GraphPoint) { return Origin + GraphPoint * Scale; };

		FWidgetAnimMachineLayerDebug Debug;
		const bool bDebugging = Graph->GetDebug && Graph->GetDebug(Debug);
		const double Now = FPlatformTime::Seconds();

		// Placed labels, so the next one keeps clear of them.
		TArray<FBox2f> Labels;
		struct FLabelJob
		{
			FString Text;
			FVector2f Point;
			FVector2f Direction;
			const UEdGraphNode* Node = nullptr;
		};
		TArray<FLabelJob> LabelJobs;

		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			const UWidgetAnimMachineTransitionNode* Transition = Cast<UWidgetAnimMachineTransitionNode>(Node);
			if (!Transition)
			{
				continue;
			}
			FGuid FirstId;
			FGuid SecondId;
			UWidgetAnimMachineGraph::GetPairOrder(Transition, FirstId, SecondId);
			const FBox2f* FirstBox = GraphBoxes.Find(Graph->FindStateNode(FirstId));
			const FBox2f* SecondBox = GraphBoxes.Find(Graph->FindStateNode(SecondId));
			if (!FirstBox || !SecondBox)
			{
				continue;
			}

			int32 Lane = 0;
			int32 Count = 1;
			bool bReversed = false;
			Graph->GetLane(Transition, Lane, Count, bReversed);
			const FVector2f PlacedPoint = WidgetAnimMachineWires::GetPlacementAnchor(*FirstBox, *SecondBox) + Transition->PlacedOffset;
			FWidgetAnimMachineWire Wire = WidgetAnimMachineWires::Compute(*FirstBox, *SecondBox, Lane, Count, bReversed, Transition->bPlaced ? &PlacedPoint : nullptr);

			// On screen: the same curve, scaled.
			Wire.Start = ToScreen(Wire.Start);
			Wire.End = ToScreen(Wire.End);
			Wire.Mid = ToScreen(Wire.Mid);
			Wire.StartTangent *= Scale;
			Wire.EndTangent *= Scale;
			Wire.MidTangentIn *= Scale;
			Wire.MidTangentOut *= Scale;
			Wire.NodePoint = ToScreen(Wire.NodePoint);

			const FWidgetAnimMachineTransition* Data = Transition->GetTransition();
			const bool bActive = bDebugging && Debug.ActiveTransition == Transition->TransitionId;

			// Taken now, or a moment ago: the effect fades out over WireLinger after the blend ends, so even a 0.2s tween is
			// seen to run.
			if (bActive)
			{
				Graph->LastActiveTimes.Add(Transition->TransitionId, Now);
			}
			const double* LastActive = Graph->LastActiveTimes.Find(Transition->TransitionId);
			const float Glow = bActive ? 1.f : (LastActive ? FMath::Clamp(1.f - static_cast<float>(Now - *LastActive) / MachineColors::WireLinger, 0.f, 1.f) : 0.f);

			FLinearColor Color = MachineColors::TransitionLine;
			float Thickness = MachineColors::WireThickness;
			if (Glow > 0.f)
			{
				Color = FMath::Lerp(MachineColors::TransitionLine, MachineColors::WireActive, Glow);
				Thickness = FMath::Lerp(MachineColors::WireThickness, MachineColors::WireActiveThickness, Glow);
			}
			else if (Data && !Data->bEnabled)
			{
				Color = MachineColors::TransitionDisabled;
			}

			if (Wire.bThroughNode)
			{
				// Into the dragged node, and on out of it.
				FSlateDrawElement::MakeDrawSpaceSpline(DrawElementsList, WireLayerID, Wire.Start, Wire.StartTangent, Wire.Mid, Wire.MidTangentIn, Thickness * Scale, ESlateDrawEffect::None, Color);
				FSlateDrawElement::MakeDrawSpaceSpline(DrawElementsList, WireLayerID, Wire.Mid, Wire.MidTangentOut, Wire.End, Wire.EndTangent, Thickness * Scale, ESlateDrawEffect::None, Color);
			}
			else
			{
				FSlateDrawElement::MakeDrawSpaceSpline(DrawElementsList, WireLayerID, Wire.Start, Wire.StartTangent, Wire.End, Wire.EndTangent, Thickness * Scale, ESlateDrawEffect::None, Color);
			}

			// Dots running along it, the way it goes, as along a wire in Blueprint simulation.
			if (Glow > 0.f)
			{
				DrawBubbles(Wire, Scale, Now, Glow);
			}

			if (Data && Scale >= 0.4f)
			{
				FLabelJob& Job = LabelJobs.AddDefaulted_GetRef();
				Job.Text = Transition->GetLengthLabel();
				Job.Point = Wire.NodePoint;
				Job.Direction = Wire.NodeDirection;
				Job.Node = Transition;
			}
		}

		// The labels last, once every wire is known, each where it is clearest.
		TArray<FBox2f> Obstacles;
		for (const TPair<const UEdGraphNode*, FBox2f>& Box : ScreenBoxes)
		{
			// Comment boxes are frames round nodes, not in the way of a label.
			if (!Box.Key->IsA<UEdGraphNode_Comment>())
			{
				Obstacles.Add(Box.Value);
			}
		}
		for (const FLabelJob& Job : LabelJobs)
		{
			DrawLengthLabel(Job.Text, Job.Point, Job.Direction, Scale, Obstacles, Labels);
		}
	}

	using FConnectionDrawingPolicy::DrawPreviewConnector;

	/**
	 * The wire being dragged out of a state: out of whichever side or corner of it faces the mouse - its bottom when dragged
	 * down, its side when dragged across, its corner when dragged off diagonally - as the wire it makes will be.
	 */
	virtual void DrawPreviewConnector(const FGeometry& PinGeometry, const FVector2f& StartPoint, const FVector2f& EndPoint, UEdGraphPin* Pin) override
	{
		// The state's edge covers the whole state: its geometry is the state's box. The mouse is whichever end isn't on it.
		const FVector2f Position(PinGeometry.GetAbsolutePosition());
		const FVector2f Size(PinGeometry.GetAbsoluteSize());
		if (Size.X <= 0.f || Size.Y <= 0.f)
		{
			FConnectionDrawingPolicy::DrawPreviewConnector(PinGeometry, StartPoint, EndPoint, Pin);
			return;
		}
		const FBox2f Box(Position, Position + Size);
		const bool bFromMouse = Pin && Pin->Direction == EGPD_Input;
		const FVector2f Mouse = bFromMouse ? StartPoint : EndPoint;

		const FVector2f Side = WidgetAnimMachineWires::GetFacingSide(Box, Mouse);
		const FVector2f Edge = WidgetAnimMachineWires::GetSideMiddle(Box, Side);
		const float Length = FMath::Max((Mouse - Edge).Size() * 0.6f, 36.f * ZoomFactor);
		const FVector2f Tangent = Side.GetSafeNormal() * Length;

		FSlateDrawElement::MakeDrawSpaceSpline(DrawElementsList, WireLayerID, Edge, Tangent, Mouse, Tangent,
			MachineColors::WireThickness * ZoomFactor, ESlateDrawEffect::None, MachineColors::TransitionLine);
	}

private:

	UWidgetAnimMachineGraph* Graph = nullptr;

	/** Paint geometry at an absolute position: Size on screen, drawn at Scale - what text is sized by. */
	static FPaintGeometry MakeScreenGeometry(const FVector2f& Position, const FVector2f& Size, float Scale)
	{
		// The render transform is what places it: the same as the layout one, here - a paint geometry built with an identity
		// render transform is drawn at the window's corner.
		const float SafeScale = FMath::Max(Scale, KINDA_SMALL_NUMBER);
		const FSlateLayoutTransform Layout(SafeScale, Position);
		return FPaintGeometry(Layout, TransformCast<FSlateRenderTransform>(Layout), Size / SafeScale, /*bInHasRenderTransform=*/false);
	}

	/** Dots at even spacing along the wire, moving from its start to its end. */
	void DrawBubbles(const FWidgetAnimMachineWire& Wire, float Scale, double Now, float Glow)
	{
		const FSlateBrush* BubbleBrush = FindBrush({ TEXT("Graph.ExecutionBubble") });

		// The wire as a polyline, measured, so the dots move at an even pace along its length.
		constexpr int32 Segments = 32;
		TArray<FVector2f> Points;
		TArray<float> Lengths;
		Points.Reserve(Segments + 1);
		Lengths.Reserve(Segments + 1);
		float Total = 0.f;
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const FVector2f Point = Wire.Evaluate(static_cast<float>(Index) / Segments);
			if (Index > 0)
			{
				Total += (Point - Points.Last()).Size();
			}
			Points.Add(Point);
			Lengths.Add(Total);
		}
		if (Total <= 1.f)
		{
			return;
		}

		// How far along the first dot is: worked out in double - the clock is far too big a number for a float to move the
		// dots smoothly (they would jump every few frames).
		const float Spacing = MachineColors::WireBubbleSpacing * Scale;
		const double TravelGraph = FMath::Fmod(Now * static_cast<double>(MachineColors::WireBubbleSpeed), static_cast<double>(MachineColors::WireBubbleSpacing));
		const float Travel = static_cast<float>(TravelGraph) * Scale;
		const float BubbleSize = FMath::Max(MachineColors::WireBubbleSize * Scale, 3.f);
		int32 Segment = 1;
		for (float Distance = Travel; Distance < Total; Distance += Spacing)
		{
			while (Segment < Lengths.Num() - 1 && Lengths[Segment] < Distance)
			{
				++Segment;
			}
			const float SegmentLength = FMath::Max(Lengths[Segment] - Lengths[Segment - 1], KINDA_SMALL_NUMBER);
			const float T = FMath::Clamp((Distance - Lengths[Segment - 1]) / SegmentLength, 0.f, 1.f);
			const FVector2f Point = FMath::Lerp(Points[Segment - 1], Points[Segment], T);
			const FVector2f TopLeft = Point - FVector2f(BubbleSize, BubbleSize) * 0.5f;
			FSlateDrawElement::MakeBox(DrawElementsList, WireLayerID + 1, MakeScreenGeometry(TopLeft, FVector2f(BubbleSize, BubbleSize), 1.f), BubbleBrush, ESlateDrawEffect::None, MachineColors::WireBubble.CopyWithNewOpacity(MachineColors::WireBubble.A * Glow));
		}
	}

	/**
	 * How long the transition takes, beside its node: off the wire's side - above a wire that runs across, to the right of one
	 * that runs down - or, if something is there, the other side, then the corners, then further out. Wherever overlaps least.
	 */
	void DrawLengthLabel(const FString& Text, const FVector2f& Point, const FVector2f& Direction, float Scale, const TArray<FBox2f>& Obstacles, TArray<FBox2f>& InOutLabels)
	{
		if (Text.IsEmpty() || !FSlateApplication::IsInitialized())
		{
			return;
		}

		const FSlateFontInfo Font = WidgetAnimMachineStyle::SmallFont();
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2f TextSize(FontMeasure->Measure(Text, Font, Scale));
		const FVector2f Padding(4.f * Scale, 1.f * Scale);
		const FVector2f BoxSize = TextSize + Padding * 2.f;

		const FVector2f Along = Direction.IsNearlyZero() ? FVector2f(1.f, 0.f) : Direction.GetSafeNormal();
		FVector2f Side(-Along.Y, Along.X);
		const bool bAcross = FMath::Abs(Along.X) >= FMath::Abs(Along.Y);
		// Above a wire that runs across; right of one that runs down.
		if ((bAcross && Side.Y > 0.f) || (!bAcross && Side.X < 0.f))
		{
			Side = -Side;
		}

		const float NodeRadius = WidgetAnimMachineWires::NodeDiameter * 0.5f * Scale;
		const float Gap = 3.f * Scale;
		auto Extent = [&BoxSize](const FVector2f& Toward)
		{
			// How far the box reaches from its center toward a direction.
			return FMath::Abs(Toward.X) * BoxSize.X * 0.5f + FMath::Abs(Toward.Y) * BoxSize.Y * 0.5f;
		};
		auto CenterToward = [&](const FVector2f& Toward, float Extra)
		{
			const FVector2f Unit = Toward.GetSafeNormal();
			return Point + Unit * (NodeRadius + Gap + Extent(Unit) + Extra);
		};

		const FVector2f Candidates[] =
		{
			CenterToward(Side, 0.f),
			CenterToward(-Side, 0.f),
			CenterToward(Side + Along, 0.f),
			CenterToward(Side - Along, 0.f),
			CenterToward(-Side + Along, 0.f),
			CenterToward(-Side - Along, 0.f),
			CenterToward(Side, NodeRadius * 1.5f),
			CenterToward(-Side, NodeRadius * 1.5f),
		};

		auto Overlap = [](const FBox2f& A, const FBox2f& B)
		{
			const float Width = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X);
			const float Height = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);
			return Width > 0.f && Height > 0.f ? Width * Height : 0.f;
		};

		FBox2f Best(Candidates[0] - BoxSize * 0.5f, Candidates[0] + BoxSize * 0.5f);
		float BestScore = TNumericLimits<float>::Max();
		for (const FVector2f& Candidate : Candidates)
		{
			const FBox2f Box(Candidate - BoxSize * 0.5f, Candidate + BoxSize * 0.5f);
			float Score = 0.f;
			for (const FBox2f& Obstacle : Obstacles)
			{
				Score += Overlap(Box, Obstacle);
			}
			for (const FBox2f& Label : InOutLabels)
			{
				Score += Overlap(Box, Label) * 2.f;
			}
			if (Score < BestScore - KINDA_SMALL_NUMBER)
			{
				BestScore = Score;
				Best = Box;
			}
			if (Score <= 0.f)
			{
				break;
			}
		}
		InOutLabels.Add(Best);

		FSlateDrawElement::MakeBox(DrawElementsList, WireLayerID + 1, MakeScreenGeometry(Best.Min, BoxSize, 1.f), WidgetAnimMachineStyle::RoundedBrush(3.f), ESlateDrawEffect::None, MachineColors::LengthLabelBackground);
		FSlateDrawElement::MakeText(DrawElementsList, WireLayerID + 2, MakeScreenGeometry(Best.Min + Padding, TextSize, Scale), Text, Font, ESlateDrawEffect::None, MachineColors::LengthLabelText);
	}
};

FConnectionDrawingPolicy* UWidgetAnimMachineGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
	return new FWidgetAnimMachineConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}

#undef LOCTEXT_NAMESPACE
