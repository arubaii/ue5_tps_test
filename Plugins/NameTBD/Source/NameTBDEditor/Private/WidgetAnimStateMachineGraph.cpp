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

FVector2f FWidgetAnimMachineWire::Evaluate(float T) const
{
	// Hermite, as Slate draws a spline from two points and their tangents.
	const float T2 = T * T;
	const float T3 = T2 * T;
	return Start * (2.f * T3 - 3.f * T2 + 1.f)
		+ StartTangent * (T3 - 2.f * T2 + T)
		+ End * (-2.f * T3 + 3.f * T2)
		+ EndTangent * (T3 - T2);
}

FVector2f FWidgetAnimMachineWire::Derivative(float T) const
{
	const float T2 = T * T;
	return Start * (6.f * T2 - 6.f * T)
		+ StartTangent * (3.f * T2 - 4.f * T + 1.f)
		+ End * (-6.f * T2 + 6.f * T)
		+ EndTangent * (3.f * T2 - 2.f * T);
}

namespace WidgetAnimMachineWires
{
	FWidgetAnimMachineWire Compute(const FBox2f& First, const FBox2f& Second, int32 Lane, int32 LaneCount, bool bReversed)
	{
		FWidgetAnimMachineWire Wire;
		const FVector2f FirstCenter = First.GetCenter();
		const FVector2f SecondCenter = Second.GetCenter();
		const FVector2f Delta = SecondCenter - FirstCenter;

		// Along the way the two lie - side by side, or one above the other - leaving the facing edges square to them, as a
		// Blueprint wire leaves a pin: an S between two states offset across, a straight line between two in line.
		const bool bHorizontal = FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y);
		const float Sign = (bHorizontal ? Delta.X : Delta.Y) >= 0.f ? 1.f : -1.f;
		const FVector2f Axis = bHorizontal ? FVector2f(Sign, 0.f) : FVector2f(0.f, Sign);
		const FVector2f Across = bHorizontal ? FVector2f(0.f, 1.f) : FVector2f(1.f, 0.f);

		// Lanes: every wire between the two states in one of its own, parallel, centered on the states' middles - closer
		// together when there are more of them than fit the shorter of the two facing edges.
		const float FirstEdge = bHorizontal ? First.GetSize().Y : First.GetSize().X;
		const float SecondEdge = bHorizontal ? Second.GetSize().Y : Second.GetSize().X;
		const float Room = FMath::Max(FMath::Min(FirstEdge, SecondEdge) - 12.f, 0.f);
		const float Spacing = LaneCount > 1 ? FMath::Min(LaneSpacing, Room / (LaneCount - 1)) : 0.f;
		const float LaneOffset = (Lane - (LaneCount - 1) * 0.5f) * Spacing;

		auto EdgePoint = [bHorizontal](const FBox2f& Box, float Side, const FVector2f& Center)
		{
			// The middle of the edge facing Side.
			return bHorizontal
				? FVector2f(Side > 0.f ? Box.Max.X : Box.Min.X, Center.Y)
				: FVector2f(Center.X, Side > 0.f ? Box.Max.Y : Box.Min.Y);
		};
		const FVector2f FirstPoint = EdgePoint(First, Sign, FirstCenter) + Across * LaneOffset;
		const FVector2f SecondPoint = EdgePoint(Second, -Sign, SecondCenter) + Across * LaneOffset;

		// The tangent as long as the stretch it spans along the axis - what gives a Blueprint wire its S - and never so
		// short that the wire kinks between states that nearly touch.
		const float Span = FMath::Abs(FVector2f::DotProduct(SecondPoint - FirstPoint, Axis));
		const FVector2f Tangent = Axis * FMath::Max(Span, 40.f);

		// Where the transition node sits: halfway. Lanes closer than a node is wide stagger their nodes along the wire,
		// so no two nodes land on each other however many wires there are.
		float NodeT = 0.5f;
		const float Chord = FMath::Max((SecondPoint - FirstPoint).Size(), 1.f);
		if (LaneCount > 1 && Spacing < NodeDiameter + 2.f)
		{
			const float Needed = NodeDiameter + 2.f;
			const float Step = FMath::Sqrt(FMath::Max(Needed * Needed - Spacing * Spacing, 0.f)) / Chord;
			NodeT = FMath::Clamp(0.5f + (Lane - (LaneCount - 1) * 0.5f) * Step, 0.12f, 0.88f);
		}

		Wire.Start = FirstPoint;
		Wire.StartTangent = Tangent;
		Wire.End = SecondPoint;
		Wire.EndTangent = Tangent;
		Wire.NodeT = NodeT;

		// Running the other way: the same curve, traced from its far end.
		if (bReversed)
		{
			Wire.Start = SecondPoint;
			Wire.End = FirstPoint;
			Wire.StartTangent = -Tangent;
			Wire.EndTangent = -Tangent;
			Wire.NodeT = 1.f - NodeT;
		}

		Wire.NodePoint = Wire.Evaluate(Wire.NodeT);
		const FVector2f Direction = Wire.Derivative(Wire.NodeT);
		Wire.NodeDirection = Direction.IsNearlyZero() ? (bReversed ? -Axis : Axis) : Direction.GetSafeNormal();
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
		if (!bSamePair)
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
	case EWidgetAnimMachineBlend::Spring:
		// About: a spring settles when it settles.
		return TEXT("~") + WidgetAnimMachineStyle::FormatSeconds(Transition->GetBlendSeconds());
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
			.BorderImage(WidgetAnimMachineStyle::RoundedBrush(8.f))
			.BorderBackgroundColor(this, &SWidgetAnimMachineOutputPin::GetPinBorderColor)
			.OnMouseButtonDown(this, &SWidgetAnimMachineOutputPin::OnPinMouseDown)
			.Cursor(this, &SWidgetAnimMachineOutputPin::GetPinCursor));
	}

protected:

	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		return SNew(STextBlock);
	}

	FSlateColor GetPinBorderColor() const
	{
		return IsHovered() ? MachineColors::StateEdgeHovered : MachineColors::StateEdge;
	}
};

/** A bar rising left to right across what it sits on, clipped to its share of it - a state's progress. Never takes the mouse. */
class SWidgetAnimMachineFill : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineFill)
		: _Alpha(0.f)
		, _Color(FLinearColor::White)
		, _Brush(nullptr)
	{}
		SLATE_ATTRIBUTE(float, Alpha)
		SLATE_ATTRIBUTE(FLinearColor, Color)
		SLATE_ARGUMENT(const FSlateBrush*, Brush)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Alpha = InArgs._Alpha;
		Color = InArgs._Color;
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
		const FGeometry Filled = AllottedGeometry.MakeChild(FVector2f(Size.X * Amount, Size.Y), FSlateLayoutTransform());
		OutDrawElements.PushClip(FSlateClippingZone(Filled));
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Brush, ESlateDrawEffect::None, Color.Get() * InWidgetStyle.GetColorAndOpacityTint());
		OutDrawElements.PopClip();
		return LayerId + 1;
	}

private:

	TAttribute<float> Alpha;
	TAttribute<FLinearColor> Color;
	const FSlateBrush* Brush = nullptr;
};

/**
 * A state node: rounded, its name in the middle and its animation under it. Its ring is where a transition is dragged from; its
 * colored interior - all of it - takes clicks, so it is selected and moved from anywhere inside. While the watched widget is in
 * it, it is lit, and its animation's progress rises across it.
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

					// The edge: where transitions are dragged from.
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SAssignNew(RightNodeBox, SVerticalBox)
					]

					// The name, and the animation under it. The whole colored part takes the mouse - a click anywhere in it
					// selects and drags the node; only the edge round it starts a transition.
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(10.f)
					[
						SNew(SBorder)
						.BorderImage(FindBrush({ TEXT("Graph.StateNode.ColorSpill") }))
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
								.Brush(FindBrush({ TEXT("Graph.StateNode.ColorSpill") }))
								.Alpha(this, &SWidgetAnimMachineStateNode::GetProgress)
								.Color(MachineColors::StateProgress)
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

	/** The state's animation's progress while the watched widget is in it - and nothing while it isn't, or it holds still. */
	float GetProgress() const
	{
		FWidgetAnimMachineLayerDebug Debug;
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		if (!Node || !GetDebug(Debug) || Debug.CurrentState != Node->StateId || !Debug.bStatePlaying)
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
		Tip.Row(LOCTEXT("TipPlays", "Plays"), State->Animation ? FText::FromString(State->Animation->GetName()) : LOCTEXT("TipNoAnimation", "nothing - it poses nothing"));
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
 * plain light gray it always was, a Spring mint, a Cut dark gray, a transition animation the animation orange with the slanted
 * stripes a value-driven track wears in the Widget Animations panel - and, in place of its double-arrow icon, a chevron
 * pointing the way the transition goes along its wire. While the watched widget takes it, its color rises across it like a
 * progress bar.
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
		else if (IsHovered() || (IsNodeSelected && IsNodeSelected()))
		{
			Color = FMath::Lerp(Color, FLinearColor::White, 0.35f);
		}

		// The disc: dimmed under its progress while it plays, its full color rising across it.
		const FLinearColor Base = bPlaying ? (Color * MachineColors::TransitionIdleDim).CopyWithNewOpacity(1.f) : Color;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Disc, ESlateDrawEffect::None, Base);
		if (bPlaying)
		{
			const float Alpha = FMath::Clamp(Debug.TransitionAlpha, 0.f, 1.f);
			const FGeometry Filled = AllottedGeometry.MakeChild(FVector2f(Size.X * Alpha, Size.Y), FSlateLayoutTransform());
			OutDrawElements.PushClip(FSlateClippingZone(Filled));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Disc, ESlateDrawEffect::None, Color);
			OutDrawElements.PopClip();
		}

		// A transition animation is striped, as a value-driven track is.
		if (Transition->Blend == EWidgetAnimMachineBlend::Animation)
		{
			WidgetAnimMachineStyle::DrawStripesInCircle(OutDrawElements, LayerId + 2, AllottedGeometry, Center, Radius - 2.f, MachineColors::TransitionStripe,
				MachineColors::TransitionStripeWidth, MachineColors::TransitionStripeSpacing);
		}

		// Selected: a thin white ring round it.
		if (IsNodeSelected && IsNodeSelected())
		{
			const FVector2f RingSize = Size + FVector2f(4.f, 4.f);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3,
				AllottedGeometry.ToPaintGeometry(RingSize, FSlateLayoutTransform(FVector2f(-2.f, -2.f))),
				WidgetAnimMachineStyle::RingBrush(RingSize.X, 1.5f), ESlateDrawEffect::None, MachineColors::TransitionSelected);
		}

		// The chevron, pointing the way the transition goes along its wire.
		FVector2f Direction = GetDirection ? GetDirection() : FVector2f(1.f, 0.f);
		Direction = Direction.IsNearlyZero() ? FVector2f(1.f, 0.f) : Direction.GetSafeNormal();
		const FVector2f Side(-Direction.Y, Direction.X);
		const float Arm = Radius * 0.36f;
		const FVector2f Tip = Center + Direction * (Arm * 0.7f);
		TArray<FVector2f> Chevron;
		Chevron.Add(Tip - Direction * Arm + Side * Arm);
		Chevron.Add(Tip);
		Chevron.Add(Tip - Direction * Arm - Side * Arm);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 4, AllottedGeometry.ToPaintGeometry(), Chevron, ESlateDrawEffect::None, MachineColors::TransitionArrow, /*bAntialias=*/true, 2.5f);
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

	virtual bool RequiresSecondPassLayout() const override
	{
		return true;
	}

	/** On its wire, where WidgetAnimMachineWires::Compute puts it - the same place the wire is drawn through. */
	virtual void PerformSecondPassLayout(const TMap<UObject*, TSharedRef<SNodePanel::SNode>>& NodeToWidgetLookup) const override
	{
		const UWidgetAnimMachineTransitionNode* Node = Cast<UWidgetAnimMachineTransitionNode>(GraphNode);
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

		int32 Lane = 0;
		int32 Count = 1;
		bool bReversed = false;
		Graph->GetLane(Node, Lane, Count, bReversed);
		const FWidgetAnimMachineWire Wire = WidgetAnimMachineWires::Compute(BoxOf(First, FirstWidget), BoxOf(Second, SecondWidget), Lane, Count, bReversed);
		Direction = Wire.NodeDirection;

		const FVector2f MySize = FVector2f(FVector2D(GetDesiredSize()));
		const FVector2f Position = Wire.NodePoint - MySize * 0.5f;
		GraphNode->NodePosX = FMath::RoundToInt32(Position.X);
		GraphNode->NodePosY = FMath::RoundToInt32(Position.Y);
	}

private:

	/** The way its wire runs through it - set as it is placed, read as it is drawn. */
	mutable FVector2f Direction = FVector2f(1.f, 0.f);

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
		if (Transition->Blend == EWidgetAnimMachineBlend::Animation)
		{
			Blend = Transition->TransitionAnimation
				? FText::Format(LOCTEXT("TipBlendAnimation", "Plays {0}"), FText::FromString(Transition->TransitionAnimation->GetName()))
				: LOCTEXT("TipBlendNoAnimation", "Play Transition Animation (none picked)");
		}
		else if (Transition->Blend == EWidgetAnimMachineBlend::Tween)
		{
			Blend = FText::Format(LOCTEXT("TipBlendTween", "Tween, {0}"), StaticEnum<EWidgetAnimEasing>()->GetDisplayNameTextByValue(static_cast<int64>(Transition->Easing)));
		}
		Tip.Row(LOCTEXT("TipBlend", "Blend"), Blend, GetBlendColor(Transition->Blend));
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
		Tip.Hint(MachineTips::TransitionNodeHint());
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
			FWidgetAnimMachineWire Wire = WidgetAnimMachineWires::Compute(*FirstBox, *SecondBox, Lane, Count, bReversed);

			// On screen: the same curve, scaled.
			Wire.Start = ToScreen(Wire.Start);
			Wire.End = ToScreen(Wire.End);
			Wire.StartTangent *= Scale;
			Wire.EndTangent *= Scale;
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

			FSlateDrawElement::MakeDrawSpaceSpline(DrawElementsList, WireLayerID, Wire.Start, Wire.StartTangent, Wire.End, Wire.EndTangent, Thickness * Scale, ESlateDrawEffect::None, Color);

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

		const float Spacing = MachineColors::WireBubbleSpacing * Scale;
		const float Travel = FMath::Fmod(static_cast<float>(Now) * MachineColors::WireBubbleSpeed * Scale, Spacing);
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
