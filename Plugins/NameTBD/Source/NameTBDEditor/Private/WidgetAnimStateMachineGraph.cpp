// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimStateMachineGraph.h"

#include "ConnectionDrawingPolicy.h"
#include "CoreGlobals.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditorActions.h"
#include "NameTBD/WidgetAnimation/WidgetAnimCue.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"
#include "NameTBDColors.h"
#include "WidgetAnimEditorSettings.h"
#include "NameTBDStyle.h"
#include "NameTBDTooltips.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "SCommentBubble.h"
#include "GraphEditorSettings.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"
#include "SGraphPin.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#include "WidgetAnimMachineConditionCustomization.h"
#include "WidgetAnimMachineEditorStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
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

	/** A condition in a few words, as its Details say it: "Hovered", "Charge >= 0.5", "Rarity is Epic", "Hold held 1s". */
	FText DescribeCondition(const FWidgetAnimMachineCondition& Condition, const UWidgetAnimStateMachineAsset* Machine = nullptr)
	{
		return FWidgetAnimMachineConditionCustomization::Describe(Machine, Condition);
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

	void Delete(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& States, const TArray<FGuid>& Transitions,
		const TArray<FGuid>& Comments, const TMap<FGuid, TArray<int32>>& Reroutes)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || (States.Num() == 0 && Transitions.Num() == 0 && Comments.Num() == 0 && Reroutes.Num() == 0))
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("DeleteNodes", "Delete From State Machine"));
		Machine->Modify();

		// Reroute points first, while every transition is still where its indices were counted. Last first, so the earlier
		// ones keep their places.
		for (const TPair<FGuid, TArray<int32>>& Points : Reroutes)
		{
			const int32 Index = Layer->FindTransitionIndex(Points.Key);
			if (Index == INDEX_NONE)
			{
				continue;
			}
			TArray<FVector2D>& Route = Layer->Transitions[Index].EditorReroutePoints;
			TArray<int32> Sorted = Points.Value;
			Sorted.Sort(TGreater<int32>());
			for (const int32 Point : Sorted)
			{
				if (Route.IsValidIndex(Point))
				{
					Route.RemoveAt(Point);
				}
			}
		}

		Layer->States.RemoveAll([&States](const FWidgetAnimMachineState& State) { return State.Kind == EWidgetAnimMachineNodeKind::State && States.Contains(State.Id); });
		Layer->Transitions.RemoveAll([&Transitions](const FWidgetAnimMachineTransition& Transition) { return Transitions.Contains(Transition.Id); });
		Layer->Comments.RemoveAll([&Comments](const FWidgetAnimMachineComment& Comment) { return Comments.Contains(Comment.Id); });

		// Normalize drops the transitions whose state went.
		FinishEdit(Machine);
	}

	bool AddReroute(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Transition, int32 Index, const FVector2D& Offset)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		const int32 TransitionIndex = Layer ? Layer->FindTransitionIndex(Transition) : INDEX_NONE;
		if (TransitionIndex == INDEX_NONE)
		{
			return false;
		}

		const FScopedTransaction ScopedTransaction(LOCTEXT("AddReroute", "Add Reroute Point"));
		Machine->Modify();
		TArray<FVector2D>& Route = Layer->Transitions[TransitionIndex].EditorReroutePoints;
		Route.Insert(Offset, FMath::Clamp(Index, 0, Route.Num()));
		FinishEdit(Machine);
		return true;
	}

	void ClearReroutes(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& Transitions)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || !Layer->Transitions.ContainsByPredicate([&Transitions](const FWidgetAnimMachineTransition& Transition)
		{
			return Transitions.Contains(Transition.Id) && Transition.EditorReroutePoints.Num() > 0;
		}))
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("ClearReroutes", "Remove Reroute Points"));
		Machine->Modify();
		for (FWidgetAnimMachineTransition& Transition : Layer->Transitions)
		{
			if (Transitions.Contains(Transition.Id))
			{
				Transition.EditorReroutePoints.Reset();
			}
		}
		FinishEdit(Machine);
	}

	void MoveReroutes(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TMap<FGuid, TArray<FVector2D>>& Points)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || Points.Num() == 0)
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("MoveReroutes", "Move Reroute Point"));
		Machine->Modify();
		for (FWidgetAnimMachineTransition& Transition : Layer->Transitions)
		{
			if (const TArray<FVector2D>* Route = Points.Find(Transition.Id))
			{
				Transition.EditorReroutePoints = *Route;
			}
		}
		Machine->MarkPackageDirty();
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
		const FName OldName = Layer->States[Index].Name;
		const FName NewStateName = UWidgetAnimStateMachineAsset::MakeUniqueStateName(*Layer, FName(*Trimmed), State);
		Layer->States[Index].Name = NewStateName;

		// Conditions on another layer's state that name it follow it.
		Machine->RenameLayerStateReferences(Layer->Name, OldName, NewStateName);
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
		// A collapsed group's box goes by the group's id.
		for (FWidgetAnimMachineStateGroup& Group : Layer->EditorGroups)
		{
			if (const FVector2D* Position = Positions.Find(Group.Id))
			{
				Group.Position = *Position;
			}
		}
		Machine->MarkPackageDirty();

		// Positions are the graph's own: nothing else needs to hear of them, and the graph already shows them.
	}

	void SetNodePositionQuietly(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Id, const FVector2D& Position)
	{
		if (!Machine || !Machine->Layers.IsValidIndex(LayerIndex))
		{
			return;
		}
		FWidgetAnimMachineLayer& Layer = Machine->Layers[LayerIndex];
		if (FWidgetAnimMachineState* State = Layer.States.FindByPredicate([&Id](const FWidgetAnimMachineState& Candidate) { return Candidate.Id == Id; }))
		{
			State->EditorPosition = Position;
		}
		else if (FWidgetAnimMachineStateGroup* Group = Layer.EditorGroups.FindByPredicate([&Id](const FWidgetAnimMachineStateGroup& Candidate) { return Candidate.Id == Id; }))
		{
			Group->Position = Position;
		}
		else
		{
			return;
		}
		Machine->MarkPackageDirty();
	}

	bool GetNodeSize(const UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Id, FVector2D& OutSize, float& OutZoom)
	{
		if (!Machine || !Machine->Layers.IsValidIndex(LayerIndex))
		{
			return false;
		}
		const FWidgetAnimMachineLayer& Layer = Machine->Layers[LayerIndex];
		if (const FWidgetAnimMachineState* State = Layer.States.FindByPredicate([&Id](const FWidgetAnimMachineState& Candidate) { return Candidate.Id == Id; }))
		{
			OutSize = State->EditorSize;
			OutZoom = State->EditorSizeZoom;
			return true;
		}
		if (const FWidgetAnimMachineStateGroup* Group = Layer.EditorGroups.FindByPredicate([&Id](const FWidgetAnimMachineStateGroup& Candidate) { return Candidate.Id == Id; }))
		{
			OutSize = Group->EditorSize;
			OutZoom = Group->EditorSizeZoom;
			return true;
		}
		return false;
	}

	void SetNodeSizeQuietly(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Id, const FVector2D& Size, float Zoom)
	{
		if (!Machine || !Machine->Layers.IsValidIndex(LayerIndex))
		{
			return;
		}
		FWidgetAnimMachineLayer& Layer = Machine->Layers[LayerIndex];
		if (FWidgetAnimMachineState* State = Layer.States.FindByPredicate([&Id](const FWidgetAnimMachineState& Candidate) { return Candidate.Id == Id; }))
		{
			State->EditorSize = Size;
			State->EditorSizeZoom = Zoom;
		}
		else if (FWidgetAnimMachineStateGroup* Group = Layer.EditorGroups.FindByPredicate([&Id](const FWidgetAnimMachineStateGroup& Candidate) { return Candidate.Id == Id; }))
		{
			Group->EditorSize = Size;
			Group->EditorSizeZoom = Zoom;
		}
		// Not dirtied: only how the graph keeps a node in place, saved with whatever is saved next.
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

	void SetTransitionsStraight(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& Transitions, bool bStraight)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || Transitions.Num() == 0)
		{
			return;
		}
		const FScopedTransaction Transaction(bStraight ? LOCTEXT("StraightenWires", "Straighten Wire") : LOCTEXT("CurveWires", "Curve Wire"));
		Machine->Modify();
		for (FWidgetAnimMachineTransition& Transition : Layer->Transitions)
		{
			if (Transitions.Contains(Transition.Id))
			{
				Transition.bEditorStraight = bStraight;
			}
		}
		Machine->MarkPackageDirty();
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
		const FName OldName = Machine->Layers[LayerIndex].Name;
		Machine->Layers[LayerIndex].Name = Machine->MakeUniqueLayerName(FName(*Trimmed), LayerIndex);

		// Conditions on its states, in the other layers, follow it.
		Machine->RenameLayerStateReferences(NAME_None, OldName, Machine->Layers[LayerIndex].Name);
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
		const TCHAR* Base = Type == EWidgetAnimMachineInputType::Number ? TEXT("Float")
			: Type == EWidgetAnimMachineInputType::Integer ? TEXT("Integer")
			: Type == EWidgetAnimMachineInputType::Trigger ? TEXT("Trigger")
			: Type == EWidgetAnimMachineInputType::Hold ? TEXT("Hold")
			: Type == EWidgetAnimMachineInputType::Choice ? TEXT("Choice")
			: Type == EWidgetAnimMachineInputType::Vector2D ? TEXT("Vector")
			: TEXT("Bool");
		if (Type == EWidgetAnimMachineInputType::Choice)
		{
			// Something to pick from straight away - renamed and added to in its Details.
			Input.Options = { FName(TEXT("Option1")), FName(TEXT("Option2")) };
		}
		Input.Name = Machine->MakeUniqueInputName(FName(Base), Machine->Inputs.Num() - 1);
		const FName Name = Input.Name;
		FinishEdit(Machine);
		return Name;
	}

	void SetInputCategory(UWidgetAnimStateMachineAsset* Machine, FName Input, const FString& Category)
	{
		FWidgetAnimMachineInput* Found = Machine ? Machine->Inputs.FindByPredicate([Input](const FWidgetAnimMachineInput& Candidate) { return Candidate.Name == Input; }) : nullptr;
		const FString Trimmed = Category.TrimStartAndEnd();
		if (!Found || Found->Category == Trimmed)
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("SetInputCategory", "Move State Machine Input To Category"));
		Machine->Modify();
		Found->Category = Trimmed;
		FinishEdit(Machine);
	}

	void RenameInputCategory(UWidgetAnimStateMachineAsset* Machine, const FString& Old, const FString& New)
	{
		const FString Trimmed = New.TrimStartAndEnd();
		if (!Machine || Old.IsEmpty() || Trimmed == Old)
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("RenameInputCategory", "Rename State Machine Input Category"));
		Machine->Modify();
		for (FWidgetAnimMachineInput& Input : Machine->Inputs)
		{
			if (Input.Category == Old)
			{
				Input.Category = Trimmed;
			}
		}
		FinishEdit(Machine);
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
			auto RenameActions = [Old, New](TArray<FWidgetAnimMachineAction>& Actions)
			{
				for (FWidgetAnimMachineAction& Action : Actions)
				{
					if (Action.Input == Old)
					{
						Action.Input = New;
					}
				}
			};
			for (FWidgetAnimMachineState& State : Layer.States)
			{
				for (FName* Read : { &State.ProgressInput, &State.SpeedInput, &State.BlendInput })
				{
					if (*Read == Old)
					{
						*Read = New;
					}
				}
				for (FWidgetAnimMachineParameterBinding& Binding : State.ParameterInputs)
				{
					if (Binding.Input == Old)
					{
						Binding.Input = New;
					}
				}
				RenameActions(State.OnEnter);
				RenameActions(State.OnExit);
			}
			for (FWidgetAnimMachineTransition& Transition : Layer.Transitions)
			{
				for (FWidgetAnimMachineCondition& Condition : Transition.Conditions)
				{
					if (Condition.Input == Old)
					{
						Condition.Input = New;
					}
					if (Condition.OtherInput == Old)
					{
						Condition.OtherInput = New;
					}
				}
				RenameActions(Transition.OnTaken);
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

	const FWidgetAnimMachineStateGroup* FindGroup(const FWidgetAnimMachineLayer& Layer, const FGuid& Group)
	{
		return Group.IsValid() ? Layer.EditorGroups.FindByPredicate([&Group](const FWidgetAnimMachineStateGroup& Candidate) { return Candidate.Id == Group; }) : nullptr;
	}

	FGuid CollapseToGroup(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const TArray<FGuid>& States)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer)
		{
			return FGuid();
		}
		TArray<int32> Members;
		for (const FGuid& State : States)
		{
			const int32 Index = Layer->FindStateIndex(State);
			if (Index != INDEX_NONE && Layer->States[Index].Kind == EWidgetAnimMachineNodeKind::State)
			{
				Members.Add(Index);
			}
		}
		if (Members.Num() == 0)
		{
			return FGuid();
		}

		const FScopedTransaction Transaction(LOCTEXT("CollapseToGroup", "Collapse States"));
		Machine->Modify();
		FWidgetAnimMachineStateGroup& Group = Layer->EditorGroups.AddDefaulted_GetRef();
		Group.Id = FGuid::NewGuid();
		Group.bCollapsed = true;

		// Collapsed States, unique among the layer's; its box where its states were, on average.
		const FString Base = TEXT("Collapsed States");
		FString Candidate = Base;
		for (int32 Suffix = 2; Layer->EditorGroups.ContainsByPredicate([&Candidate](const FWidgetAnimMachineStateGroup& Other) { return Other.Name == FName(*Candidate); }); ++Suffix)
		{
			Candidate = FString::Printf(TEXT("%s %d"), *Base, Suffix);
		}
		Group.Name = FName(*Candidate);
		FVector2D Sum = FVector2D::ZeroVector;
		for (const int32 Index : Members)
		{
			Layer->States[Index].EditorGroup = Group.Id;
			Sum += Layer->States[Index].EditorPosition;
		}
		Group.Position = Sum / static_cast<double>(Members.Num());
		const FGuid Id = Group.Id;
		FinishEdit(Machine);
		return Id;
	}

	void SetGroupCollapsed(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Group, bool bCollapsed)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		FWidgetAnimMachineStateGroup* Found = Layer ? Layer->EditorGroups.FindByPredicate([&Group](const FWidgetAnimMachineStateGroup& Candidate) { return Candidate.Id == Group; }) : nullptr;
		if (!Found || Found->bCollapsed == bCollapsed)
		{
			return;
		}
		const FScopedTransaction Transaction(bCollapsed ? LOCTEXT("CollapseGroup", "Collapse States") : LOCTEXT("ExpandGroup", "Expand Collapsed States"));
		Machine->Modify();
		Found->bCollapsed = bCollapsed;
		FinishEdit(Machine);
	}

	void Ungroup(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Group)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		if (!Layer || !FindGroup(*Layer, Group))
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("Ungroup", "Release Collapsed States"));
		Machine->Modify();
		for (FWidgetAnimMachineState& State : Layer->States)
		{
			if (State.EditorGroup == Group)
			{
				State.EditorGroup.Invalidate();
			}
		}
		Layer->EditorGroups.RemoveAll([&Group](const FWidgetAnimMachineStateGroup& Candidate) { return Candidate.Id == Group; });
		FinishEdit(Machine);
	}

	void RenameGroup(UWidgetAnimStateMachineAsset* Machine, int32 LayerIndex, const FGuid& Group, const FString& NewName)
	{
		FWidgetAnimMachineLayer* Layer = GetLayerForEdit(Machine, LayerIndex);
		FWidgetAnimMachineStateGroup* Found = Layer ? Layer->EditorGroups.FindByPredicate([&Group](const FWidgetAnimMachineStateGroup& Candidate) { return Candidate.Id == Group; }) : nullptr;
		const FString Trimmed = NewName.TrimStartAndEnd();
		if (!Found || Trimmed.IsEmpty() || Found->Name == FName(*Trimmed))
		{
			return;
		}
		const FScopedTransaction Transaction(LOCTEXT("RenameGroup", "Rename Collapsed States"));
		Machine->Modify();
		Found->Name = FName(*Trimmed);
		FinishEdit(Machine);
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

namespace
{
	/** Which curve of a wire of Count curves T falls on, and how far along that curve: each curve an equal share of the way. */
	int32 SplitWireT(float T, int32 Count, float& OutLocalT)
	{
		const float Scaled = FMath::Clamp(T, 0.f, 1.f) * Count;
		const int32 Segment = FMath::Clamp(FMath::FloorToInt32(Scaled), 0, Count - 1);
		OutLocalT = Scaled - Segment;
		return Segment;
	}

	/** How many points along each curve a wire is measured by - for its dots, its middle, and clicks on it. */
	constexpr int32 WireSamplesPerSegment = 24;
}

FVector2f FWidgetAnimMachineWire::Evaluate(float T) const
{
	if (Segments.Num() == 0)
	{
		return FVector2f::ZeroVector;
	}
	float Local = 0.f;
	const FSegment& Segment = Segments[SplitWireT(T, Segments.Num(), Local)];
	return HermitePoint(Segment.Start, Segment.StartTangent, Segment.End, Segment.EndTangent, Local);
}

FVector2f FWidgetAnimMachineWire::Derivative(float T) const
{
	if (Segments.Num() == 0)
	{
		return FVector2f::ZeroVector;
	}
	float Local = 0.f;
	const FSegment& Segment = Segments[SplitWireT(T, Segments.Num(), Local)];
	return HermiteDerivative(Segment.Start, Segment.StartTangent, Segment.End, Segment.EndTangent, Local) * static_cast<float>(Segments.Num());
}

void FWidgetAnimMachineWire::Transform(const FVector2f& Origin, float Scale)
{
	for (FSegment& Segment : Segments)
	{
		Segment.Start = Origin + Segment.Start * Scale;
		Segment.End = Origin + Segment.End * Scale;
		Segment.StartTangent *= Scale;
		Segment.EndTangent *= Scale;
	}
	NodePoint = Origin + NodePoint * Scale;
}

float FWidgetAnimMachineWire::GetDistance(const FVector2f& Point, int32& OutSegment, FVector2f* OutNearest) const
{
	OutSegment = INDEX_NONE;
	float Best = TNumericLimits<float>::Max();
	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		const FSegment& Segment = Segments[SegmentIndex];
		FVector2f Previous = Segment.Start;
		for (int32 Sample = 1; Sample <= WireSamplesPerSegment; ++Sample)
		{
			const FVector2f Next = HermitePoint(Segment.Start, Segment.StartTangent, Segment.End, Segment.EndTangent, static_cast<float>(Sample) / WireSamplesPerSegment);
			const FVector2f Along = Next - Previous;
			const float LengthSquared = Along.SizeSquared();
			const float Fraction = LengthSquared > KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2f::DotProduct(Point - Previous, Along) / LengthSquared, 0.f, 1.f) : 0.f;
			const FVector2f OnWire = Previous + Along * Fraction;
			const float Distance = (Point - OnWire).Size();
			if (Distance < Best)
			{
				Best = Distance;
				OutSegment = SegmentIndex;
				if (OutNearest)
				{
					*OutNearest = OnWire;
				}
			}
			Previous = Next;
		}
	}
	return Best;
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

	/**
	 * A wire routed through points of its own - reroute points, and its transition node where that was dragged: out of the
	 * side or corner of the state it leaves that faces the first point, through each point in turn, and into the side or
	 * corner of the other state that faces the last - one smooth curve from each point to the next, as through Blueprint
	 * reroute nodes. At each point the wire runs the way from the point before it to the point after it.
	 */
	FWidgetAnimMachineWire ComputeRouted(const FBox2f& From, const FBox2f& To, const FVector2f* NodePoint, TConstArrayView<FVector2f> Reroutes)
	{
		FWidgetAnimMachineWire Wire;

		// The points it runs through, the way it goes: the reroute points in their order, and the dragged transition node
		// wherever along the way it adds least - so it sits on whichever part of the wire it was dragged nearest.
		TArray<FVector2f, TInlineAllocator<8>> Points(Reroutes.GetData(), Reroutes.Num());
		TArray<bool, TInlineAllocator<8>> IsReroute;
		IsReroute.Init(true, Points.Num());
		int32 NodeIndex = INDEX_NONE;
		if (NodePoint)
		{
			float BestCost = TNumericLimits<float>::Max();
			for (int32 Index = 0; Index <= Points.Num(); ++Index)
			{
				const FVector2f Before = Index == 0 ? From.GetCenter() : Points[Index - 1];
				const FVector2f After = Index == Points.Num() ? To.GetCenter() : Points[Index];
				const float Cost = (*NodePoint - Before).Size() + (After - *NodePoint).Size() - (After - Before).Size();
				if (Cost < BestCost - KINDA_SMALL_NUMBER)
				{
					BestCost = Cost;
					NodeIndex = Index;
				}
			}
			Points.Insert(*NodePoint, NodeIndex);
			IsReroute.Insert(false, NodeIndex);
		}

		const FVector2f FromSide = GetFacingSide(From, Points[0]);
		const FVector2f ToSide = GetFacingSide(To, Points.Last());

		// Through reroute points: sharp - straight lines from point to point, out of the middle of a state's side (Q lines the
		// points up with it). Through only a dragged transition node: one smooth curve through it, as ever.
		const bool bSharp = Reroutes.Num() > 0;

		// Every point of the wire, ends and all, and which way it runs at each.
		TArray<FVector2f, TInlineAllocator<10>> Full;
		Full.Add(GetSideMiddle(From, FromSide));
		Full.Append(Points);
		Full.Add(GetSideMiddle(To, ToSide));
		TArray<FVector2f, TInlineAllocator<10>> Directions;
		Directions.SetNumZeroed(Full.Num());
		for (int32 Index = 1; Index < Full.Num() - 1; ++Index)
		{
			FVector2f Through = (Full[Index + 1] - Full[Index - 1]).GetSafeNormal();
			if (Through.IsNearlyZero())
			{
				Through = (Full[Index] - Full[Index - 1]).GetSafeNormal();
			}
			Directions[Index] = Through.IsNearlyZero() ? FVector2f(1.f, 0.f) : Through;
		}

		int32 ReroutesSoFar = 0;
		for (int32 Index = 0; Index < Full.Num() - 1; ++Index)
		{
			const float Length = FMath::Max((Full[Index + 1] - Full[Index]).Size(), 1.f);
			FWidgetAnimMachineWire::FSegment& Segment = Wire.Segments.AddDefaulted_GetRef();
			Segment.Start = Full[Index];
			Segment.End = Full[Index + 1];
			// Out of a state and into one as a Blueprint wire leaves and enters its pins; through a point at the pace of the
			// curve on either side of it, so the two meet smoothly.
			if (bSharp)
			{
				// A straight line: a Hermite curve whose tangents are both the line itself.
				Segment.StartTangent = Segment.End - Segment.Start;
				Segment.EndTangent = Segment.End - Segment.Start;
			}
			else
			{
				Segment.StartTangent = Index == 0 ? FromSide.GetSafeNormal() * FMath::Max(Length, 36.f) : Directions[Index] * Length;
				Segment.EndTangent = Index == Full.Num() - 2 ? -ToSide.GetSafeNormal() * FMath::Max(Length, 36.f) : Directions[Index + 1] * Length;
			}
			Segment.ReroutesBefore = ReroutesSoFar;
			if (IsReroute.IsValidIndex(Index) && IsReroute[Index])
			{
				++ReroutesSoFar;
			}
		}

		const int32 Count = Wire.Segments.Num();
		if (NodeIndex != INDEX_NONE)
		{
			// The dragged node: where the curve into it ends. On a sharp wire it points along the line out of it.
			Wire.NodeT = static_cast<float>(NodeIndex + 1) / Count;
			Wire.NodePoint = *NodePoint;
			const FVector2f Out = (Full[NodeIndex + 2] - Full[NodeIndex + 1]).GetSafeNormal();
			Wire.NodeDirection = bSharp && !Out.IsNearlyZero() ? Out : Directions[NodeIndex + 1];
			return Wire;
		}

		// Not dragged: halfway along the wire, measured along it - but never on a reroute point, kept clear of it by a node's width.
		TArray<float, TInlineAllocator<128>> Lengths;
		Lengths.Reserve(Count * WireSamplesPerSegment + 1);
		Lengths.Add(0.f);
		FVector2f Previous = Wire.Segments[0].Start;
		for (int32 Sample = 1; Sample <= Count * WireSamplesPerSegment; ++Sample)
		{
			const FVector2f Next = Wire.Evaluate(static_cast<float>(Sample) / (Count * WireSamplesPerSegment));
			Lengths.Add(Lengths.Last() + (Next - Previous).Size());
			Previous = Next;
		}
		const float Total = Lengths.Last();
		float Middle = Total * 0.5f;
		const float Clear = NodeDiameter * 0.5f + RerouteDiameter * 0.5f + 4.f;
		for (int32 Point = 1; Point < Count; ++Point)
		{
			const float At = Lengths[Point * WireSamplesPerSegment];
			if (FMath::Abs(Middle - At) < Clear)
			{
				Middle = Middle >= At ? At + Clear : At - Clear;
			}
		}
		Middle = Total > Clear * 2.f ? FMath::Clamp(Middle, Clear, Total - Clear) : Total * 0.5f;

		int32 Sample = 1;
		while (Sample < Lengths.Num() - 1 && Lengths[Sample] < Middle)
		{
			++Sample;
		}
		const float Span = FMath::Max(Lengths[Sample] - Lengths[Sample - 1], KINDA_SMALL_NUMBER);
		const float Fraction = FMath::Clamp((Middle - Lengths[Sample - 1]) / Span, 0.f, 1.f);
		Wire.NodeT = (Sample - 1 + Fraction) / (Count * WireSamplesPerSegment);
		Wire.NodePoint = Wire.Evaluate(Wire.NodeT);
		const FVector2f Direction = Wire.Derivative(Wire.NodeT);
		Wire.NodeDirection = Direction.IsNearlyZero() ? Directions[1] : Direction.GetSafeNormal();
		return Wire;
	}

	bool IsSlanted(const FBox2f& First, const FBox2f& Second)
	{
		const FVector2f Side = GetFacingSide(First, Second);
		const FVector2f Delta = Second.GetCenter() - First.GetCenter();
		const float Slant = FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Delta.Y), FMath::Max(FMath::Abs(Delta.X), KINDA_SMALL_NUMBER)));
		return Side.X != 0.f && Side.Y != 0.f && Slant > 25.f && Slant < 65.f;
	}

	FWidgetAnimMachineWire Compute(const FBox2f& First, const FBox2f& Second, int32 Lane, int32 LaneCount, bool bReversed, const FVector2f* NodePoint, TConstArrayView<FVector2f> Reroutes, bool bStraight)
	{
		if (NodePoint || Reroutes.Num() > 0)
		{
			return ComputeRouted(bReversed ? Second : First, bReversed ? First : Second, NodePoint, Reroutes);
		}

		FWidgetAnimMachineWire Wire;

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
		FVector2f FirstTangent = FirstSide.GetSafeNormal() * TangentLength;
		FVector2f SecondTangent = -SecondSide.GetSafeNormal() * TangentLength;

		// Lined up - straight across, straight down, or corner to corner on the diagonal (Q) - give or take a pixel's rounding:
		// a straight line, not a curve that bows by that pixel. Both ends must already point along the line, within a few degrees.
		// Drawn straight on purpose (Q on states that lie on a slant, or the wire's menu): a straight line whatever the angle.
		const FVector2f ChordDirection = (SecondPoint - FirstPoint).GetSafeNormal();
		const float StraightCos = FMath::Cos(FMath::DegreesToRadians(StraightSnapDegrees));
		if (!ChordDirection.IsNearlyZero()
			&& (bStraight
				|| (FVector2f::DotProduct(ChordDirection, FirstSide.GetSafeNormal()) >= StraightCos
					&& FVector2f::DotProduct(ChordDirection, (-SecondSide).GetSafeNormal()) >= StraightCos)))
		{
			FirstTangent = ChordDirection * TangentLength;
			SecondTangent = ChordDirection * TangentLength;
		}

		// Where the transition node sits: halfway. Lanes closer than a node is wide stagger their nodes along the wire, so no
		// two nodes land on each other however many wires there are.
		float NodeT = 0.5f;
		if (LaneCount > 1 && Spacing < NodeDiameter + 2.f)
		{
			const float Needed = NodeDiameter + 2.f;
			const float Step = FMath::Sqrt(FMath::Max(Needed * Needed - Spacing * Spacing, 0.f)) / Chord;
			NodeT = FMath::Clamp(0.5f + (Lane - (LaneCount - 1) * 0.5f) * Step, 0.12f, 0.88f);
		}

		FWidgetAnimMachineWire::FSegment& Segment = Wire.Segments.AddDefaulted_GetRef();
		Segment.Start = FirstPoint;
		Segment.StartTangent = FirstTangent;
		Segment.End = SecondPoint;
		Segment.EndTangent = SecondTangent;
		Wire.NodeT = NodeT;

		// Running the other way: the same curve, traced from its far end.
		if (bReversed)
		{
			Segment.Start = SecondPoint;
			Segment.End = FirstPoint;
			Segment.StartTangent = -SecondTangent;
			Segment.EndTangent = -FirstTangent;
			Wire.NodeT = 1.f - NodeT;
		}

		Wire.NodePoint = Wire.Evaluate(Wire.NodeT);
		const FVector2f Direction = Wire.Derivative(Wire.NodeT);
		Wire.NodeDirection = Direction.IsNearlyZero() ? (Segment.End - Segment.Start).GetSafeNormal() : Direction.GetSafeNormal();
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

namespace WidgetAnimMachineLookups
{
	/**
	 * The index in Items of the first item whose id (IdOf, unset for an item that has none) is Id: through Indices, checked
	 * against Items, and Indices made again from Items when it doesn't check out. INDEX_NONE when no item has it.
	 */
	template<typename ItemArrayType, typename IdOfType>
	int32 Find(TMap<FGuid, int32>& Indices, const ItemArrayType& Items, const FGuid& Id, IdOfType IdOf)
	{
		if (const int32* Found = Indices.Find(Id))
		{
			const TOptional<FGuid> FoundId = Items.IsValidIndex(*Found) ? IdOf(Items[*Found]) : TOptional<FGuid>();
			if (FoundId.IsSet() && FoundId.GetValue() == Id)
			{
				return *Found;
			}
		}

		Indices.Reset();
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			const TOptional<FGuid> ItemId = IdOf(Items[Index]);
			if (ItemId.IsSet() && !Indices.Contains(ItemId.GetValue()))
			{
				Indices.Add(ItemId.GetValue(), Index);
			}
		}
		const int32* Again = Indices.Find(Id);
		return Again ? *Again : INDEX_NONE;
	}
}

UWidgetAnimMachineGraphNode* UWidgetAnimMachineGraph::FindStateNode(const FGuid& StateId) const
{
	const int32 Index = WidgetAnimMachineLookups::Find(StateNodeIndices, Nodes, StateId, [](const auto& Node)
	{
		const UWidgetAnimMachineGraphNode* StateNode = Cast<UWidgetAnimMachineGraphNode>(Node);
		return StateNode ? TOptional<FGuid>(StateNode->StateId) : TOptional<FGuid>();
	});
	return Index != INDEX_NONE ? Cast<UWidgetAnimMachineGraphNode>(Nodes[Index]) : nullptr;
}

UWidgetAnimMachineTransitionNode* UWidgetAnimMachineGraph::FindTransitionNode(const FGuid& TransitionId) const
{
	const int32 Index = WidgetAnimMachineLookups::Find(TransitionNodeIndices, Nodes, TransitionId, [](const auto& Node)
	{
		const UWidgetAnimMachineTransitionNode* TransitionNode = Cast<UWidgetAnimMachineTransitionNode>(Node);
		return TransitionNode ? TOptional<FGuid>(TransitionNode->TransitionId) : TOptional<FGuid>();
	});
	return Index != INDEX_NONE ? Cast<UWidgetAnimMachineTransitionNode>(Nodes[Index]) : nullptr;
}

const FWidgetAnimMachineState* UWidgetAnimMachineGraph::FindLayerState(const FGuid& StateId) const
{
	// No id finds nothing, as the layer's own FindStateIndex has it.
	const FWidgetAnimMachineLayer* Layer = GetLayer();
	if (!Layer || !StateId.IsValid())
	{
		return nullptr;
	}
	const int32 Index = WidgetAnimMachineLookups::Find(StateIndices, Layer->States, StateId, [](const FWidgetAnimMachineState& State)
	{
		return TOptional<FGuid>(State.Id);
	});
	return Index != INDEX_NONE ? &Layer->States[Index] : nullptr;
}

const FWidgetAnimMachineTransition* UWidgetAnimMachineGraph::FindLayerTransition(const FGuid& TransitionId) const
{
	// No id finds nothing, as the layer's own FindTransitionIndex has it.
	const FWidgetAnimMachineLayer* Layer = GetLayer();
	if (!Layer || !TransitionId.IsValid())
	{
		return nullptr;
	}
	const int32 Index = WidgetAnimMachineLookups::Find(TransitionIndices, Layer->Transitions, TransitionId, [](const FWidgetAnimMachineTransition& Transition)
	{
		return TOptional<FGuid>(Transition.Id);
	});
	return Index != INDEX_NONE ? &Layer->Transitions[Index] : nullptr;
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

	// Only the transitions between the same two nodes: gathered by pair once a frame, rather than every node gone through for
	// every wire.
	if (PairNodesFrame != GFrameCounter || PairNodesCount != Nodes.Num())
	{
		PairNodes.Reset();
		for (const UEdGraphNode* Other : Nodes)
		{
			if (const UWidgetAnimMachineTransitionNode* Transition = Cast<UWidgetAnimMachineTransitionNode>(Other))
			{
				FGuid PairFirst;
				FGuid PairSecond;
				GetPairOrder(Transition, PairFirst, PairSecond);
				PairNodes.FindOrAdd(TPair<FGuid, FGuid>(PairFirst, PairSecond)).Add(Transition);
			}
		}
		PairNodesFrame = GFrameCounter;
		PairNodesCount = Nodes.Num();
	}

	static const TArray<const UWidgetAnimMachineTransitionNode*, TInlineAllocator<2>> NoSiblings;
	const TArray<const UWidgetAnimMachineTransitionNode*, TInlineAllocator<2>>* Siblings = PairNodes.Find(TPair<FGuid, FGuid>(First, Second));
	for (const UWidgetAnimMachineTransitionNode* Sibling : Siblings ? *Siblings : NoSiblings)
	{
		// A node dragged somewhere of its own, or a wire routed through reroute points, takes no lane: its wire runs through
		// its points instead.
		if ((Sibling->bPlaced || Sibling->RerouteOffsets.Num() > 0) && Sibling != Node)
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

FWidgetAnimMachineWire UWidgetAnimMachineGraph::ComputeWire(const UWidgetAnimMachineTransitionNode* Node, const FBox2f& FirstBox, const FBox2f& SecondBox) const
{
	int32 Lane = 0;
	int32 Count = 1;
	bool bReversed = false;
	GetLane(Node, Lane, Count, bReversed);

	// Its dragged node and reroute points are kept from halfway between the two nodes' corners, so they follow them.
	const FVector2f Anchor = WidgetAnimMachineWires::GetPlacementAnchor(FirstBox, SecondBox);
	const FVector2f PlacedPoint = Anchor + Node->PlacedOffset;
	TArray<FVector2f, TInlineAllocator<8>> Reroutes;
	Reroutes.Reserve(Node->RerouteOffsets.Num());
	for (const FVector2f& Offset : Node->RerouteOffsets)
	{
		Reroutes.Add(Anchor + Offset);
	}
	return WidgetAnimMachineWires::Compute(FirstBox, SecondBox, Lane, Count, bReversed, Node->bPlaced ? &PlacedPoint : nullptr, Reroutes, Node->bStraight);
}

bool UWidgetAnimMachineGraph::IsDebugging() const
{
	FWidgetAnimMachineLayerDebug Debug;
	return GetDebug && GetDebug(Debug);
}

TOptional<bool> UWidgetAnimMachineGraph::GetLayerConditionHeld(const FGuid& Transition, int32 ConditionIndex) const
{
	FWidgetAnimMachineLayerDebug Debug;
	if (!GetDebug || !GetDebug(Debug))
	{
		return TOptional<bool>();
	}
	return ReadLayerConditionHeld(Debug, Transition, ConditionIndex);
}

TOptional<bool> UWidgetAnimMachineGraph::ReadLayerConditionHeld(const FWidgetAnimMachineLayerDebug& Debug, const FGuid& Transition, int32 ConditionIndex) const
{
	const FWidgetAnimMachineLayerDebug::FLayerCondition* Entry = Debug.FindLayerCondition(Transition, ConditionIndex);
	if (!Entry)
	{
		return TOptional<bool>();
	}
	const TPair<FGuid, int32> Key(Transition, ConditionIndex);
	const double Now = FPlatformTime::Seconds();
	if (Entry->bHolds)
	{
		LayerConditionHeldTimes.Add(Key, Now);
		return true;
	}
	const double* LastHeld = LayerConditionHeldTimes.Find(Key);
	return LastHeld && Now - *LastHeld < MachineColors::LayerConditionLinger;
}

TOptional<bool> UWidgetAnimMachineGraph::GetLayerConditionsHeld(const FGuid& Transition) const
{
	const FWidgetAnimMachineLayer* Layer = Machine && Machine->Layers.IsValidIndex(LayerIndex) ? &Machine->Layers[LayerIndex] : nullptr;
	const int32 TransitionIndex = Layer ? Layer->FindTransitionIndex(Transition) : INDEX_NONE;
	FWidgetAnimMachineLayerDebug Debug;
	if (TransitionIndex == INDEX_NONE || !GetDebug || !GetDebug(Debug))
	{
		return TOptional<bool>();
	}
	TOptional<bool> All;
	const TArray<FWidgetAnimMachineCondition>& Conditions = Layer->Transitions[TransitionIndex].Conditions;
	for (int32 ConditionIndex = 0; ConditionIndex < Conditions.Num(); ++ConditionIndex)
	{
		const TOptional<bool> Held = ReadLayerConditionHeld(Debug, Transition, ConditionIndex);
		if (Held.IsSet())
		{
			All = All.Get(true) && Held.GetValue();
		}
	}
	return All;
}

void UWidgetAnimMachineGraph::RebuildFromMachine()
{
	// Nothing found before is where it was.
	PairNodes.Reset();
	PairNodesFrame = MAX_uint64;
	PairNodesCount = INDEX_NONE;

	// Each node made again takes the size the machine kept with its position (see SWidgetAnimMachineStateNode::Tick).
	KnownNodeSizes.Reset();

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

	// A group opened that is gone (undone, ungrouped): back to the whole layer.
	if (OpenGroup.IsValid() && !WidgetAnimMachineEdits::FindGroup(*Layer, OpenGroup))
	{
		OpenGroup.Invalidate();
	}

	// Collapsed groups, at the top of the layer: a box each, in place of their states.
	if (!OpenGroup.IsValid())
	{
		for (const FWidgetAnimMachineStateGroup& Group : Layer->EditorGroups)
		{
			if (!Group.bCollapsed)
			{
				continue;
			}
			UWidgetAnimMachineGraphNode* Node = NewObject<UWidgetAnimMachineGraphNode>(this, NAME_None, RF_Transient);
			Node->CreateNewGuid();
			Node->StateId = Group.Id;
			Node->Kind = EWidgetAnimMachineNodeKind::State;
			Node->bIsGroup = true;
			Node->NodePosX = FMath::RoundToInt32(Group.Position.X);
			Node->NodePosY = FMath::RoundToInt32(Group.Position.Y);
			Node->bCanRenameNode = true;
			Node->AllocateDefaultPins();
			AddNode(Node, /*bUserAction=*/false, /*bSelectNewNode=*/false);
		}
	}

	for (const FWidgetAnimMachineState& State : Layer->States)
	{
		// Only what is shown: a group opened, its states alone; else every state not in a collapsed group.
		if (GetShownNodeId(State.Id) != State.Id)
		{
			continue;
		}
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
		// Between the nodes its ends are shown as - a collapsed group's box for a state in it. Within one box it isn't drawn.
		const FGuid ShownFrom = GetShownNodeId(Transition.From);
		const FGuid ShownTo = GetShownNodeId(Transition.To);
		UWidgetAnimMachineGraphNode* From = FindStateNode(ShownFrom);
		UWidgetAnimMachineGraphNode* To = FindStateNode(ShownTo);
		if (!From || !To || From == To)
		{
			continue;
		}

		UWidgetAnimMachineTransitionNode* Node = NewObject<UWidgetAnimMachineTransitionNode>(this, NAME_None, RF_Transient);
		Node->CreateNewGuid();
		Node->TransitionId = Transition.Id;
		Node->FromId = ShownFrom;
		Node->ToId = ShownTo;
		Node->bPlaced = Transition.bEditorNodePlaced;
		Node->PlacedOffset = FVector2f(Transition.EditorNodeOffset);
		Node->bStraight = Transition.bEditorStraight;
		for (const FVector2D& Point : Transition.EditorReroutePoints)
		{
			Node->RerouteOffsets.Add(FVector2f(Point));
		}
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

		// Its wire's reroute points, each a node of its own - over the wires, as a Blueprint reroute node is.
		for (int32 RerouteIndex = 0; RerouteIndex < Node->RerouteOffsets.Num(); ++RerouteIndex)
		{
			UWidgetAnimMachineRerouteNode* Reroute = NewObject<UWidgetAnimMachineRerouteNode>(this, NAME_None, RF_Transient);
			Reroute->CreateNewGuid();
			Reroute->TransitionId = Transition.Id;
			Reroute->RerouteIndex = RerouteIndex;
			Reroute->TransitionNode = Node;
			// Where it is, until its widget places it exactly (see SWidgetAnimMachineRerouteNode::PerformSecondPassLayout).
			Reroute->NodePosX = Node->NodePosX;
			Reroute->NodePosY = Node->NodePosY;
			AddNode(Reroute, /*bUserAction=*/false, /*bSelectNewNode=*/false);
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

FGuid UWidgetAnimMachineGraph::GetShownNodeId(const FGuid& StateId) const
{
	const FWidgetAnimMachineLayer* Layer = GetLayer();
	const FWidgetAnimMachineState* State = Layer ? Layer->FindState(StateId) : nullptr;
	if (!State)
	{
		return FGuid();
	}
	if (OpenGroup.IsValid())
	{
		return State->EditorGroup == OpenGroup ? StateId : FGuid();
	}
	const FWidgetAnimMachineStateGroup* Group = WidgetAnimMachineEdits::FindGroup(*Layer, State->EditorGroup);
	return Group && Group->bCollapsed ? Group->Id : StateId;
}

const FWidgetAnimMachineStateGroup* UWidgetAnimMachineGraphNode::GetGroup() const
{
	const UWidgetAnimMachineGraph* Graph = GetMachineGraph();
	const FWidgetAnimMachineLayer* Layer = Graph ? Graph->GetLayer() : nullptr;
	return bIsGroup && Layer ? WidgetAnimMachineEdits::FindGroup(*Layer, StateId) : nullptr;
}

bool UWidgetAnimMachineGraphNode::ShowsState(const FGuid& State) const
{
	if (!bIsGroup)
	{
		return State.IsValid() && State == StateId;
	}
	const UWidgetAnimMachineGraph* Graph = GetMachineGraph();
	const FWidgetAnimMachineLayer* Layer = Graph ? Graph->GetLayer() : nullptr;
	const FWidgetAnimMachineState* Found = Layer ? Layer->FindState(State) : nullptr;
	return Found && Found->EditorGroup == StateId;
}

const FWidgetAnimMachineState* UWidgetAnimMachineGraphNode::GetState() const
{
	const UWidgetAnimMachineGraph* Graph = GetMachineGraph();
	return Graph ? Graph->FindLayerState(StateId) : nullptr;
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
	if (bIsGroup)
	{
		const FWidgetAnimMachineStateGroup* Group = GetGroup();
		return Group ? FText::FromName(Group->Name) : LOCTEXT("MissingGroup", "(gone)");
	}
	const FWidgetAnimMachineState* State = GetState();
	return State ? FText::FromName(State->Name) : LOCTEXT("MissingState", "(gone)");
}

FText UWidgetAnimMachineGraphNode::GetTooltipText() const
{
	if (bIsGroup)
	{
		return MachineTips::GroupNodeHint();
	}
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
	// Several states selected: the menu is the selection's - what acts on all of them (Collapse States, copy, cut, duplicate,
	// delete, Organize) - and nothing meant for one state: no jumping to it, opening its animation, renaming it, making it the
	// Entry state, its note, or the wires off it.
	bool bMultiple = false;
	if (const UWidgetAnimMachineGraph* OwnGraph = GetMachineGraph())
	{
		TArray<FGuid> Selected;
		if (OwnGraph->GetSelectedStates)
		{
			OwnGraph->GetSelectedStates(Selected);
		}
		bMultiple = Selected.Num() > 1;
	}

	// A wire right-clicked: the graph hands over the pin at one end of it - often a state's - so the transitions on the wires
	// at that pin are offered here, each to switch on or off, as their own node's menu does.
	if (Context && Context->Pin && !bMultiple)
	{
		TArray<const UWidgetAnimMachineTransitionNode*, TInlineAllocator<4>> OnWire;
		for (const UEdGraphPin* Linked : Context->Pin->LinkedTo)
		{
			if (const UWidgetAnimMachineTransitionNode* TransitionNode = Linked ? Cast<UWidgetAnimMachineTransitionNode>(Linked->GetOwningNode()) : nullptr)
			{
				OnWire.AddUnique(TransitionNode);
			}
		}
		if (OnWire.Num() > 0)
		{
			// Under the State section: what the state is comes first, then the wires off it.
			FToolMenuSection& WireSection = Menu->AddSection(TEXT("WidgetAnimMachineWire"), LOCTEXT("WireSection", "Transitions On This Wire"),
				FToolMenuInsert(TEXT("WidgetAnimMachineState"), EToolMenuInsertType::After));
			for (const UWidgetAnimMachineTransitionNode* TransitionNode : OnWire)
			{
				const FWidgetAnimMachineTransition* Transition = TransitionNode->GetTransition();
				UWidgetAnimMachineGraph* Graph = TransitionNode->GetMachineGraph();
				if (!Transition || !Graph)
				{
					continue;
				}
				const UWidgetAnimMachineGraphNode* FromNode = Graph->FindStateNode(TransitionNode->FromId);
				const UWidgetAnimMachineGraphNode* ToNode = Graph->FindStateNode(TransitionNode->ToId);
				const FText Route = FText::Format(LOCTEXT("WireRoute", "{0} -> {1}"),
					FromNode ? FromNode->GetNodeTitle(ENodeTitleType::ListView) : FText::GetEmpty(),
					ToNode ? ToNode->GetNodeTitle(ENodeTitleType::ListView) : FText::GetEmpty());
				const bool bEnabled = Transition->bEnabled;
				const TWeakObjectPtr<const UWidgetAnimMachineTransitionNode> WeakTransition(TransitionNode);
				// Each its route with a check box: ticked, it can be taken - quicker to read than "Enabled" / "Disabled".
				WireSection.AddMenuEntry(
					*FString::Printf(TEXT("ToggleWire_%s"), *TransitionNode->TransitionId.ToString()),
					Route,
					MachineTips::ToggleTransitionTooltip(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([WeakTransition, bEnabled]()
						{
							const UWidgetAnimMachineTransitionNode* Node = WeakTransition.Get();
							UWidgetAnimMachineGraph* NodeGraph = Node ? Node->GetMachineGraph() : nullptr;
							if (NodeGraph)
							{
								WidgetAnimMachineEdits::SetTransitionsEnabled(NodeGraph->Machine, NodeGraph->LayerIndex, { Node->TransitionId }, !bEnabled);
							}
						}),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([bEnabled]() { return bEnabled; })),
					EUserInterfaceActionType::ToggleButton);
			}
		}
	}

	const TWeakObjectPtr<const UWidgetAnimMachineGraphNode> WeakSelf(this);

	// Collapse States: the selected states (this one too), as one box - lined up with what they are wired to once it is made
	// (OnCollapsed). Expand States undoes the look; a state of expanded collapsed states folds them back.
	auto CollapseSelected = [WeakSelf]()
	{
		const UWidgetAnimMachineGraphNode* Node = WeakSelf.Get();
		UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
		if (!Graph)
		{
			return;
		}
		const FWidgetAnimMachineState* Own = Node->GetState();
		const FWidgetAnimMachineLayer* Layer = Graph->GetLayer();
		const FWidgetAnimMachineStateGroup* Expanded = Own && Layer ? WidgetAnimMachineEdits::FindGroup(*Layer, Own->EditorGroup) : nullptr;
		FGuid Made;
		if (Expanded && !Expanded->bCollapsed)
		{
			Made = Expanded->Id;
			WidgetAnimMachineEdits::SetGroupCollapsed(Graph->Machine, Graph->LayerIndex, Made, true);
		}
		else
		{
			TArray<FGuid> States;
			if (Graph->GetSelectedStates)
			{
				Graph->GetSelectedStates(States);
			}
			States.AddUnique(Node->StateId);
			Made = WidgetAnimMachineEdits::CollapseToGroup(Graph->Machine, Graph->LayerIndex, States);
		}
		if (Made.IsValid() && Graph->OnCollapsed)
		{
			Graph->OnCollapsed(Made);
		}
	};

	// Rename: its new name typed straight into the menu, as a track's is in the Widget Animations panel - a state, or
	// collapsed states. Enter renames it and closes the menu; Escape or clicking away leaves it.
	auto AddRename = [WeakSelf](FToolMenuSection& InSection)
	{
		const UWidgetAnimMachineGraphNode* Node = WeakSelf.Get();
		const UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
		const bool bEditable = !Graph || !Graph->CanEditMachine || Graph->CanEditMachine();
		const FText Current = Node ? Node->GetNodeTitle(ENodeTitleType::EditableTitle) : FText::GetEmpty();
		InSection.AddSubMenu(
			TEXT("RenameNode"),
			LOCTEXT("RenameNodeMenu", "Rename"),
			Node && Node->bIsGroup ? MachineTips::RenameCollapsedStatesTooltip() : MachineTips::RenameStateTooltip(),
			FNewMenuDelegate::CreateLambda([WeakSelf, Current, bEditable](FMenuBuilder& SubMenu)
			{
				SubMenu.AddWidget(
					SNew(SBox)
					.Padding(FMargin(8.f, 2.f, 8.f, 4.f))
					.MinDesiredWidth(200.f)
					[
						SNew(SEditableTextBox)
						.Text(Current)
						.IsReadOnly(!bEditable)
						.SelectAllTextWhenFocused(true)
						.OnTextCommitted_Lambda([WeakSelf](const FText& NewText, ETextCommit::Type CommitType)
						{
							const FString Typed = NewText.ToString().TrimStartAndEnd();
							if (CommitType != ETextCommit::OnEnter || Typed.IsEmpty())
							{
								return;
							}
							FSlateApplication::Get().DismissAllMenus();
							const UWidgetAnimMachineGraphNode* Renamed = WeakSelf.Get();
							UWidgetAnimMachineGraph* RenamedGraph = Renamed ? Renamed->GetMachineGraph() : nullptr;
							if (!RenamedGraph)
							{
								return;
							}
							if (Renamed->bIsGroup)
							{
								WidgetAnimMachineEdits::RenameGroup(RenamedGraph->Machine, RenamedGraph->LayerIndex, Renamed->StateId, Typed);
							}
							else
							{
								WidgetAnimMachineEdits::RenameState(RenamedGraph->Machine, RenamedGraph->LayerIndex, Renamed->StateId, Typed);
							}
						})
					],
					FText::GetEmpty(),
					/*bNoIndent=*/true);
			}),
			/*bInOpenSubMenuOnClick=*/false,
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GenericCommands.Rename")));
	};

	// Lining the selection up: Q, as in a Blueprint graph. A menu entry of its own - the engine's Straighten Connections
	// command straightens by pins, which a state machine's nodes don't line up by. Next to last: a state's note is last.
	auto AddOrganize = [&Menu, WeakSelf]()
	{
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
	};

	// The selection's menu.
	if (bMultiple)
	{
		FToolMenuSection& SelectionSection = Menu->AddSection(TEXT("WidgetAnimMachineSelection"), LOCTEXT("SelectionSection", "Selected States"));
		SelectionSection.AddMenuEntry(
			TEXT("CollapseToGroup"),
			LOCTEXT("CollapseToGroupMenu", "Collapse States"),
			MachineTips::CollapseToGroupTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda(CollapseSelected)));
		SelectionSection.AddMenuEntry(FGenericCommands::Get().Copy);
		SelectionSection.AddMenuEntry(FGenericCommands::Get().Cut);
		SelectionSection.AddMenuEntry(FGenericCommands::Get().Duplicate);
		SelectionSection.AddMenuEntry(FGenericCommands::Get().Delete);
		AddOrganize();
		return;
	}

	// A collapsed group's box: open it, expand it in place, take its states out of it, rename it.
	if (bIsGroup)
	{
		FToolMenuSection& GroupSection = Menu->AddSection(TEXT("WidgetAnimMachineGroup"), LOCTEXT("GroupSection", "Collapsed States"));
		auto AddGroupEntry = [&GroupSection, WeakSelf](FName EntryName, const FText& Label, const FText& Tip, TFunction<void(UWidgetAnimMachineGraph&, const FGuid&)> Act)
		{
			GroupSection.AddMenuEntry(EntryName, Label, Tip, FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakSelf, Act]()
			{
				const UWidgetAnimMachineGraphNode* Node = WeakSelf.Get();
				UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
				if (Graph)
				{
					Act(*Graph, Node->StateId);
				}
			})));
		};
		AddGroupEntry(TEXT("OpenGroup"), LOCTEXT("OpenGroupMenu", "Open"), LOCTEXT("OpenGroupTip", "Shows these states alone (or double-click the box). Back to the layer with the trail in the layer row, above the graph."),
			[](UWidgetAnimMachineGraph& Graph, const FGuid& Group) { Graph.OpenGroupView(Group); });
		AddGroupEntry(TEXT("ExpandGroup"), LOCTEXT("ExpandGroupMenu", "Expand States"), LOCTEXT("ExpandGroupTip", "Shows these states in the layer again, still together under this name; Collapse States on any of them folds them back into this box."),
			[](UWidgetAnimMachineGraph& Graph, const FGuid& Group) { WidgetAnimMachineEdits::SetGroupCollapsed(Graph.Machine, Graph.LayerIndex, Group, false); });
		AddRename(GroupSection);
		AddOrganize();
		return;
	}

	FToolMenuSection& Section = Menu->AddSection(TEXT("WidgetAnimMachineState"), LOCTEXT("StateSection", "State"));
	if (Kind == EWidgetAnimMachineNodeKind::Entry || Kind == EWidgetAnimMachineNodeKind::Exit)
	{
		// Back to the start, or out through Exit - as a double-click does.
		const bool bEntry = Kind == EWidgetAnimMachineNodeKind::Entry;
		Section.AddMenuEntry(
			TEXT("JumpToState"),
			bEntry ? LOCTEXT("JumpToEntryMenu", "Jump To Entry") : LOCTEXT("JumpToExitMenu", "Jump To Exit"),
			bEntry ? MachineTips::JumpToEntryTooltip() : MachineTips::JumpToExitTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakSelf]()
			{
				const UWidgetAnimMachineGraphNode* Node = WeakSelf.Get();
				UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
				if (Graph && Graph->OnJumpToState)
				{
					Graph->OnJumpToState(Node->StateId);
				}
			})));
	}
	if (Kind == EWidgetAnimMachineNodeKind::State)
	{
		Section.AddMenuEntry(
			TEXT("JumpToState"),
			LOCTEXT("JumpToStateMenu", "Jump To State"),
			MachineTips::JumpToStateTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakSelf]()
			{
				const UWidgetAnimMachineGraphNode* Node = WeakSelf.Get();
				UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
				if (Graph && Graph->OnJumpToState)
				{
					Graph->OnJumpToState(Node->StateId);
				}
			})));
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
		Section.AddMenuEntry(
			TEXT("CollapseToGroup"),
			LOCTEXT("CollapseToGroupMenu", "Collapse States"),
			MachineTips::CollapseToGroupTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda(CollapseSelected)));
		AddRename(Section);
		Section.AddMenuEntry(FGenericCommands::Get().Copy);
		Section.AddMenuEntry(FGenericCommands::Get().Cut);
		Section.AddMenuEntry(FGenericCommands::Get().Duplicate);
		Section.AddMenuEntry(FGenericCommands::Get().Delete);
	}

	AddOrganize();

	// A state's note, last: typed straight into the menu, as a Blueprint node's comment is.
	if (Kind == EWidgetAnimMachineNodeKind::State)
	{
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
}

UWidgetAnimMachineGraph* UWidgetAnimMachineTransitionNode::GetMachineGraph() const
{
	return Cast<UWidgetAnimMachineGraph>(GetGraph());
}

const FWidgetAnimMachineTransition* UWidgetAnimMachineTransitionNode::GetTransition() const
{
	const UWidgetAnimMachineGraph* Graph = GetMachineGraph();
	return Graph ? Graph->FindLayerTransition(TransitionId) : nullptr;
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
		Conditions.Add(DescribeCondition(Condition, GetMachineGraph() ? GetMachineGraph()->Machine.Get() : nullptr));
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

	// Selected with other transitions: the whole selection is switched, as Blueprint's Disable acts on every node selected.
	TArray<FGuid> Selected;
	if (const UWidgetAnimMachineGraph* OwnGraph = GetMachineGraph())
	{
		if (OwnGraph->GetSelectedTransitions)
		{
			OwnGraph->GetSelectedTransitions(Selected);
		}
	}
	TArray<FGuid> Targets = Selected.Contains(TransitionId) ? Selected : TArray<FGuid>{ TransitionId };
	const bool bSeveral = Targets.Num() > 1;
	Section.AddMenuEntry(
		TEXT("ToggleTransition"),
		bSeveral
			? (bEnabled ? LOCTEXT("DisableTransitions", "Disable Selected Transitions") : LOCTEXT("EnableTransitions", "Enable Selected Transitions"))
			: (bEnabled ? LOCTEXT("DisableTransition", "Disable Transition") : LOCTEXT("EnableTransition", "Enable Transition")),
		MachineTips::ToggleTransitionTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakSelf, bEnabled, Targets]()
		{
			const UWidgetAnimMachineTransitionNode* Node = WeakSelf.Get();
			UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
			if (Graph)
			{
				WidgetAnimMachineEdits::SetTransitionsEnabled(Graph->Machine, Graph->LayerIndex, Targets, !bEnabled);
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
	// Its wire dead straight from state to state, or curving as usual - when it is a plain wire: one routed through points
	// of its own runs straight between them already.
	if (!bPlaced && RerouteOffsets.Num() == 0)
	{
		const TWeakObjectPtr<UWidgetAnimMachineTransitionNode> WeakStraight(const_cast<UWidgetAnimMachineTransitionNode*>(this));
		Section.AddMenuEntry(
			TEXT("StraightWire"),
			LOCTEXT("StraightWire", "Straight Wire"),
			MachineTips::StraightWireTooltip(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakStraight]()
				{
					UWidgetAnimMachineTransitionNode* Node = WeakStraight.Get();
					UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
					if (Graph)
					{
						Node->bStraight = !Node->bStraight;
						WidgetAnimMachineEdits::SetTransitionsStraight(Graph->Machine, Graph->LayerIndex, { Node->TransitionId }, Node->bStraight);
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([WeakStraight]()
				{
					const UWidgetAnimMachineTransitionNode* Node = WeakStraight.Get();
					return Node && Node->bStraight;
				})),
			EUserInterfaceActionType::ToggleButton);
	}
	// Routed through reroute points: back to a plain wire.
	if (RerouteOffsets.Num() > 0)
	{
		Section.AddMenuEntry(
			TEXT("ClearReroutes"),
			LOCTEXT("ClearReroutes", "Remove Reroute Points"),
			MachineTips::ClearReroutesTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([WeakSelf]()
			{
				const UWidgetAnimMachineTransitionNode* Node = WeakSelf.Get();
				UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
				if (Graph)
				{
					WidgetAnimMachineEdits::ClearReroutes(Graph->Machine, Graph->LayerIndex, { Node->TransitionId });
				}
			})));
	}
	Section.AddMenuEntry(FGenericCommands::Get().Delete);
}

UWidgetAnimMachineGraph* UWidgetAnimMachineRerouteNode::GetMachineGraph() const
{
	return Cast<UWidgetAnimMachineGraph>(GetGraph());
}

FText UWidgetAnimMachineRerouteNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("RerouteTitle", "Reroute Point");
}

FText UWidgetAnimMachineRerouteNode::GetTooltipText() const
{
	return TransitionNode
		? FText::Format(LOCTEXT("RerouteTooltip", "Reroute point on {0}"), TransitionNode->GetNodeTitle(ENodeTitleType::FullTitle))
		: GetNodeTitle(ENodeTitleType::FullTitle);
}

void UWidgetAnimMachineRerouteNode::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	FToolMenuSection& Section = Menu->AddSection(TEXT("WidgetAnimMachineReroute"), LOCTEXT("RerouteSection", "Reroute Point"));
	const TWeakObjectPtr<const UWidgetAnimMachineRerouteNode> WeakSelf(this);
	Section.AddMenuEntry(
		TEXT("RemoveReroute"),
		LOCTEXT("RemoveReroute", "Remove Reroute Point"),
		MachineTips::RemoveRerouteTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakSelf]()
		{
			const UWidgetAnimMachineRerouteNode* Node = WeakSelf.Get();
			UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
			if (Graph)
			{
				TMap<FGuid, TArray<int32>> Reroutes;
				Reroutes.Add(Node->TransitionId, TArray<int32>{ Node->RerouteIndex });
				WidgetAnimMachineEdits::Delete(Graph->Machine, Graph->LayerIndex, TArray<FGuid>(), TArray<FGuid>(), TArray<FGuid>(), Reroutes);
			}
		})));
	Section.AddMenuEntry(
		TEXT("ClearReroutesOnWire"),
		LOCTEXT("ClearReroutesOnWire", "Remove All Reroute Points On Wire"),
		MachineTips::ClearReroutesTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([WeakSelf]()
		{
			const UWidgetAnimMachineRerouteNode* Node = WeakSelf.Get();
			UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
			if (Graph)
			{
				WidgetAnimMachineEdits::ClearReroutes(Graph->Machine, Graph->LayerIndex, { Node->TransitionId });
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

const FName UWidgetAnimMachineGraphSchema::HoldingPreset(TEXT("Holding"));

FText UWidgetAnimMachineGraphSchema::GetPresetTooltip(const TPair<FName, FName>& Preset)
{
	if (Preset.Key == HoldingPreset)
	{
		return MachineTips::AddHoldingStateTooltip();
	}
	return Preset.Value.IsNone()
		? MachineTips::AddIdleStateTooltip()
		: FText::Format(MachineTips::AddPresetStateTooltip(), FText::FromName(Preset.Key), FText::FromName(Preset.Value));
}

const TArray<TPair<FName, FName>>& UWidgetAnimMachineGraphSchema::GetPresetStates()
{
	static const TArray<TPair<FName, FName>> Presets =
	{
		TPair<FName, FName>(TEXT("Idle"), NAME_None),
		TPair<FName, FName>(WidgetAnimMachineInputs::Hovered, WidgetAnimMachineInputs::Hovered),
		TPair<FName, FName>(WidgetAnimMachineInputs::Pressed, WidgetAnimMachineInputs::Pressed),
		TPair<FName, FName>(HoldingPreset, NAME_None),
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

	// Holding: played while the machine's Press And Hold is held - its first, or a new one made for it.
	const bool bHolding = Preset == HoldingPreset;
	if (bHolding)
	{
		const FWidgetAnimMachineInput* Hold = Graph->Machine->Inputs.FindByPredicate([](const FWidgetAnimMachineInput& Candidate) { return Candidate.Type == EWidgetAnimMachineInputType::Hold; });
		Input = Hold ? Hold->Name : WidgetAnimMachineEdits::AddInput(Graph->Machine, EWidgetAnimMachineInputType::Hold);
	}

	const FGuid StateId = WidgetAnimMachineEdits::AddState(Graph->Machine, Graph->LayerIndex, Preset, Location);

	// Added inside an open group: the state joins it, or the group's view would not show it.
	if (StateId.IsValid() && Graph->OpenGroup.IsValid() && Graph->Machine->Layers.IsValidIndex(Graph->LayerIndex))
	{
		FWidgetAnimMachineLayer& Layer = Graph->Machine->Layers[Graph->LayerIndex];
		if (WidgetAnimMachineEdits::FindGroup(Layer, Graph->OpenGroup))
		{
			if (FWidgetAnimMachineState* Added = Layer.States.FindByPredicate([&StateId](const FWidgetAnimMachineState& Candidate) { return Candidate.Id == StateId; }))
			{
				Graph->Machine->Modify();
				Added->EditorGroup = Graph->OpenGroup;
			}
		}
	}

	if (From && StateId.IsValid())
	{
		const FGuid TransitionId = WidgetAnimMachineEdits::AddTransition(Graph->Machine, Graph->LayerIndex, From->StateId, StateId);
		if (TransitionId.IsValid() && !Input.IsNone())
		{
			WidgetAnimMachineEdits::AddCondition(Graph->Machine, Graph->LayerIndex, TransitionId, Input, EWidgetAnimMachineCompare::IsTrue);
		}

		// Holding: and back again when it is let go before its Hold Time - the hold not completed, blended back from wherever
		// the state had got to. (Not to Entry or Any State: nothing goes back into those.)
		if (bHolding && !Input.IsNone())
		{
			const FGuid BackId = WidgetAnimMachineEdits::AddTransition(Graph->Machine, Graph->LayerIndex, StateId, From->StateId);
			if (BackId.IsValid())
			{
				WidgetAnimMachineEdits::AddCondition(Graph->Machine, Graph->LayerIndex, BackId, Input, EWidgetAnimMachineCompare::IsFalse);
			}
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
		const FText ToolTip = GetPresetTooltip(Preset);
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
	if (NodeA->bIsGroup || NodeB->bIsGroup)
	{
		// A group's box is only how the graph shows its states: join one of them - open the group, or expand it.
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, LOCTEXT("NotToGroup", "Open or expand the collapsed states to join one of them"));
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
 * A collapsed group's body, as a Blueprint collapsed graph node's (and the Widget Animations panel's collapsed track) is: a
 * dark frame round a dark, blank body, rounded at every corner, with a soft gray glow in its middle fading toward its edges.
 * Colors from NameTBD::Colors::WidgetAnimEditor (CollapsedNode*), so the two editors' collapsed things look alike.
 */
class SWidgetAnimMachineCollapsedBody : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineCollapsedBody) {}
		SLATE_ATTRIBUTE(bool, Lifted)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		// Visibility is the caller's (shown on a group's box only); never set here, which would override it.
		Lifted = InArgs._Lifted;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(8.0, 8.0);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		namespace CollapsedLook = NameTBD::Colors::WidgetAnimEditor;
		const FVector2f Size = AllottedGeometry.GetLocalSize();
		const float Border = CollapsedLook::CollapsedNodeBorder;
		if (Size.X <= Border * 2.f || Size.Y <= Border * 2.f)
		{
			return LayerId;
		}
		const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
			WidgetAnimMachineStyle::RoundedBrush(CollapsedLook::CollapsedNodeRadius), ESlateDrawEffect::None, CollapsedLook::CollapsedNodeFrame * Tint);

		const FVector2f BodySize(Size.X - Border * 2.f, Size.Y - Border * 2.f);
		const FGeometry Body = AllottedGeometry.MakeChild(BodySize, FSlateLayoutTransform(FVector2f(Border, Border)));
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, Body.ToPaintGeometry(),
			WidgetAnimMachineStyle::RoundedBrush(FMath::Max(CollapsedLook::CollapsedNodeRadius - Border, 1.f)), ESlateDrawEffect::None, CollapsedLook::CollapsedNodeBody * Tint);

		// The glow, a row of pixels at a time: across each row, faded in from both ends; down the rows, strongest in the middle.
		FLinearColor Glow = CollapsedLook::CollapsedNodeGlow;
		if (Lifted.Get(false))
		{
			Glow = Glow * 1.25f;
		}
		Glow.A = CollapsedLook::CollapsedNodeGlow.A;
		OutDrawElements.PushClip(FSlateClippingZone(Body));
		const float Reach = FMath::Min(FMath::Clamp(BodySize.X * CollapsedLook::CollapsedNodeGlowReach, 4.f, CollapsedLook::CollapsedNodeGlowMaxReach), BodySize.X / 4.5f);
		const int32 NumRows = FMath::Max(FMath::CeilToInt(BodySize.Y), 1);
		for (int32 Row = 0; Row < NumRows; ++Row)
		{
			const float Down = (Row + 0.5f) / NumRows;
			const float Strength = FMath::Pow(FMath::Sin(Down * PI), CollapsedLook::CollapsedNodeGlowFalloff);
			if (Strength <= 0.01f)
			{
				continue;
			}
			auto At = [&Glow, Strength](float X, float Alpha)
			{
				return FSlateGradientStop(FVector2D(X, 0.0), Glow.CopyWithNewOpacity(Glow.A * Strength * Alpha) * FLinearColor(1.f, 1.f, 1.f, 1.f));
			};
			TArray<FSlateGradientStop> Stops;
			Stops.Add(At(0.f, 0.f));
			Stops.Add(At(Reach * 0.5f, 0.15f));
			Stops.Add(At(Reach, 0.45f));
			Stops.Add(At(Reach * 2.2f, 1.f));
			Stops.Add(At(BodySize.X - Reach * 2.2f, 1.f));
			Stops.Add(At(BodySize.X - Reach, 0.45f));
			Stops.Add(At(BodySize.X - Reach * 0.5f, 0.15f));
			Stops.Add(At(BodySize.X, 0.f));
			FSlateDrawElement::MakeGradient(OutDrawElements, LayerId + 2,
				Body.ToPaintGeometry(FVector2f(BodySize.X, 1.f), FSlateLayoutTransform(FVector2f(0.f, static_cast<float>(Row)))),
				MoveTemp(Stops), Orient_Vertical, ESlateDrawEffect::None);
		}
		OutDrawElements.PopClip();
		return LayerId + 3;
	}

private:

	TAttribute<bool> Lifted;
};

/**
 * A blend state's samples, drawn small on its node: Blend 1D as a line with a tick per sample, Blend 2D as a square with a dot per
 * sample (each as big as its weight while watched) - and, while the watched widget is in the state, a dot where its input is.
 */
class SWidgetAnimMachineBlendPlot : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineBlendPlot) {}
		/** The state's blend samples and space, read every paint. */
		SLATE_ARGUMENT(TFunction<const FWidgetAnimMachineState*()>, GetState)
		/** The live weights and input point, when watched. False otherwise. */
		SLATE_ARGUMENT(TFunction<bool(TArray<float>&, FVector2f&)>, GetLive)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		GetState = InArgs._GetState;
		GetLive = InArgs._GetLive;
		SetVisibility(EVisibility::HitTestInvisible);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const FWidgetAnimMachineState* State = GetState ? GetState() : nullptr;
		if (!State || !State->IsBlendState() || State->BlendSamples.Num() == 0)
		{
			return FVector2D::ZeroVector;
		}
		return State->BlendSpace == EWidgetAnimMachineBlendSpace::TwoD ? FVector2D(56.0, 56.0) : FVector2D(96.0, 14.0);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const FWidgetAnimMachineState* State = GetState ? GetState() : nullptr;
		if (!State || !State->IsBlendState() || State->BlendSamples.Num() == 0)
		{
			return LayerId;
		}
		TArray<float> Weights;
		FVector2f Live = FVector2f::ZeroVector;
		const bool bLive = GetLive && GetLive(Weights, Live);
		const FSlateBrush* White = FAppStyle::GetBrush(TEXT("WhiteBrush"));
		const FLinearColor Faint(1.f, 1.f, 1.f, 0.25f);
		const FLinearColor Sample(1.f, 1.f, 1.f, 0.8f);
		const FLinearColor LiveColor = MachineColors::StateProgress;
		const FVector2f Size = AllottedGeometry.GetLocalSize();
		const bool bTwoD = State->BlendSpace == EWidgetAnimMachineBlendSpace::TwoD;

		// The samples' extent - where the plot's edges are - with the live point inside it too.
		FVector2f Min(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
		FVector2f Max(-TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
		auto PointOf = [State, bTwoD](int32 Index)
		{
			const FWidgetAnimMachineBlendSample& Blend = State->BlendSamples[Index];
			return bTwoD ? FVector2f(Blend.Position2D) : FVector2f(Blend.Position, 0.f);
		};
		for (int32 Index = 0; Index < State->BlendSamples.Num(); ++Index)
		{
			Min = FVector2f::Min(Min, PointOf(Index));
			Max = FVector2f::Max(Max, PointOf(Index));
		}
		if (bLive)
		{
			Min = FVector2f::Min(Min, Live);
			Max = FVector2f::Max(Max, Live);
		}
		const FVector2f Span(FMath::Max(Max.X - Min.X, 1e-3f), FMath::Max(Max.Y - Min.Y, 1e-3f));
		const float Inset = 4.f;
		auto ToLocal = [&](const FVector2f& Point)
		{
			const float X = Inset + (Point.X - Min.X) / Span.X * (Size.X - Inset * 2.f);
			const float Y = bTwoD ? Inset + (Point.Y - Min.Y) / Span.Y * (Size.Y - Inset * 2.f) : Size.Y * 0.5f;
			return FVector2f(X, Y);
		};
		auto Dot = [&](const FVector2f& Center, float Radius, const FLinearColor& Color, int32 Layer)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, Layer,
				AllottedGeometry.ToPaintGeometry(FVector2f(Radius * 2.f, Radius * 2.f), FSlateLayoutTransform(Center - FVector2f(Radius, Radius))),
				White, ESlateDrawEffect::None, Color * InWidgetStyle.GetColorAndOpacityTint());
		};

		// The frame: a line for 1D, a box for 2D.
		if (bTwoD)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), White, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.35f) * InWidgetStyle.GetColorAndOpacityTint());
		}
		else
		{
			TArray<FVector2f> Line = { FVector2f(Inset, Size.Y * 0.5f), FVector2f(Size.X - Inset, Size.Y * 0.5f) };
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, Faint * InWidgetStyle.GetColorAndOpacityTint(), true, 1.f);
		}

		for (int32 Index = 0; Index < State->BlendSamples.Num(); ++Index)
		{
			const float Weight = bLive && Weights.IsValidIndex(Index) ? Weights[Index] : 0.f;
			Dot(ToLocal(PointOf(Index)), 1.5f + 2.5f * Weight, Weight > 0.f ? FMath::Lerp(Sample, LiveColor, Weight) : Sample, LayerId + 1);
		}
		if (bLive)
		{
			Dot(ToLocal(Live), 2.f, LiveColor, LayerId + 2);
		}
		return LayerId + 3;
	}

private:

	TFunction<const FWidgetAnimMachineState*()> GetState;
	TFunction<bool(TArray<float>&, FVector2f&)> GetLive;
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

		// Each line one closed antialiased path. Such a path dims where its two ends meet - a hairline crack across the ring -
		// so it starts at the middle of the bottom edge, where it shows least, and runs on past its start by the DEVELOPER
		// ONLY State Machine Ring Seam Overlap: its two ends overlap rather than meet.
		const float Overlap = FMath::Max(UWidgetAnimEditorSettings::Get().MachineRingSeamOverlap, 0.f);
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
			// From the middle of the bottom edge, clockwise: the bottom-left corner, the top-left, the top-right, the
			// bottom-right - each a quarter turn - and back along the bottom edge, past the start.
			constexpr int32 ArcSegments = 10;
			Points.Reset();
			const FVector2f BottomMiddle((Min.X + Max.X) * 0.5f, Max.Y);
			Points.Add(BottomMiddle);
			for (int32 Step = 0; Step < 4; ++Step)
			{
				const int32 Corner = (Step + 2) % 4;
				const float StartAngle = -UE_HALF_PI + Corner * UE_HALF_PI;
				for (int32 Segment = 0; Segment <= ArcSegments; ++Segment)
				{
					const float Angle = StartAngle + UE_HALF_PI * Segment / ArcSegments;
					Points.Add(Centers[Corner] + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
				}
			}
			const float Straight = FMath::Max((Max.X - Min.X) * 0.5f - Radius, 0.f);
			Points.Add(BottomMiddle - FVector2f(FMath::Min(Overlap, Straight), 0.f));
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

							// A collapsed group: the collapsed graph node's body in place of a state's colored title.
							+ SOverlay::Slot()
							[
								SNew(SWidgetAnimMachineCollapsedBody)
								.Visibility(MachineNode && MachineNode->bIsGroup ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
								.Lifted(this, &SWidgetAnimMachineStateNode::IsActive)
							]

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
									// Entry, Exit, Any State - a name alone: its capitals centered in the box, not its line. The
									// title's line is taller below them (descenders, the drop shadow) than above, which sat the
									// name visibly high; it is drawn that much lower, the box's size unchanged.
									+ SVerticalBox::Slot()
									.AutoHeight()
									.Padding(bIsState ? FMargin(0.f) : FMargin(0.f, MachineColors::SpecialNodeTitleDrop, 0.f, -MachineColors::SpecialNodeTitleDrop))
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.VAlign(VAlign_Center)
										[
										SAssignNew(InlineEditableText, SInlineEditableTextBlock)
										.Style(MachineNode && MachineNode->bIsGroup ? &GetGroupTitleStyle() : &FAppStyle::Get().GetWidgetStyle<FInlineEditableTextBlockStyle>("Graph.StateNode.NodeTitleInlineEditableText"))
										.Text(this, &SWidgetAnimMachineStateNode::GetName)
										.OnVerifyTextChanged(this, &SWidgetAnimMachineStateNode::OnVerifyNameTextChanged)
										.OnTextCommitted(this, &SWidgetAnimMachineStateNode::OnNameTextCommited)
										.IsReadOnly(this, &SWidgetAnimMachineStateNode::IsNameReadOnly)
										.IsSelected(this, &SWidgetAnimMachineStateNode::IsSelectedExclusively)
										]
										// Actions on entering or leaving: a small lightning badge.
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.VAlign(VAlign_Center)
										.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
										[
											SNew(SImage)
											.Image(FNameTBDStyle::FindIcon(FNameTBDStyle::StateMachineActionsIcon) ? FNameTBDStyle::FindIcon(FNameTBDStyle::StateMachineActionsIcon) : FAppStyle::GetBrush(TEXT("Icons.Lightning")))
											.ColorAndOpacity(MachineColors::StateProgress)
											.ToolTipText(MachineTips::ActionsBadgeTooltip())
											.Visibility(this, &SWidgetAnimMachineStateNode::GetActionsBadgeVisibility)
										]
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
									// A blend state's samples, and where its input is while watched. Collapsed for anything else, so
									// its gap doesn't push the name off center.
									+ SVerticalBox::Slot()
									.AutoHeight()
									.HAlign(HAlign_Center)
									.Padding(FMargin(0.f, 3.f, 0.f, 0.f))
									[
										SNew(SWidgetAnimMachineBlendPlot)
										.Visibility_Lambda([WeakNode = TWeakObjectPtr<UWidgetAnimMachineGraphNode>(Cast<UWidgetAnimMachineGraphNode>(GraphNode))]()
										{
											const UWidgetAnimMachineGraphNode* Node = WeakNode.Get();
											const FWidgetAnimMachineState* State = Node ? Node->GetState() : nullptr;
											return State && State->IsBlendState() && State->BlendSamples.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
										})
										.GetState([WeakNode = TWeakObjectPtr<UWidgetAnimMachineGraphNode>(Cast<UWidgetAnimMachineGraphNode>(GraphNode))]() -> const FWidgetAnimMachineState*
										{
											const UWidgetAnimMachineGraphNode* Node = WeakNode.Get();
											return Node ? Node->GetState() : nullptr;
										})
										.GetLive([WeakSelf = TWeakPtr<SWidgetAnimMachineStateNode>(SharedThis(this))](TArray<float>& OutWeights, FVector2f& OutPoint)
										{
											const TSharedPtr<SWidgetAnimMachineStateNode> Self = WeakSelf.Pin();
											return Self.IsValid() && Self->GetLiveBlend(OutWeights, OutPoint);
										})
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
				// A Blueprint node's comment bubble exactly: the editor's comment color (dark gray, quiet), its pin and its
				// show / hide toggle - hiding it keeps the note, as a Blueprint node's does.
				.ColorAndOpacity(GetDefault<UGraphEditorSettings>()->DefaultCommentNodeTitleColor)
				.EnableTitleBarBubble(true)
				.EnableBubbleCtrls(true)
				.AllowPinning(true)
				.GraphLOD_Lambda([this]() { return GetCurrentLOD(); })
				.IsGraphNodeHovered_Lambda([this]() { return IsHovered(); })
				.OnToggled_Lambda([WeakNode](bool bVisible)
				{
					const UWidgetAnimMachineGraphNode* Node = WeakNode.Get();
					UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
					const FWidgetAnimMachineState* State = Node ? Node->GetState() : nullptr;
					if (Graph && State)
					{
						WidgetAnimMachineEdits::SetStateComment(Graph->Machine, Graph->LayerIndex, Node->StateId, State->Comment, bVisible);
					}
				})
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
		// A collapsed group's box opens - its states shown alone.
		if (Node && Graph && Node->bIsGroup)
		{
			Graph->OpenGroupView(Node->StateId);
			return FReply::Handled();
		}

		// Entry and Exit are jumped to as well: Entry starts the preview again, Exit takes the layer out the way it closes.
		const bool bEntryOrExit = Node && (Node->Kind == EWidgetAnimMachineNodeKind::Entry || Node->Kind == EWidgetAnimMachineNodeKind::Exit);
		if (bEntryOrExit && Graph && Graph->OnJumpToState)
		{
			Graph->OnJumpToState(Node->StateId);
			return FReply::Handled();
		}
		if (Node && Graph && Node->Kind == EWidgetAnimMachineNodeKind::State)
		{
			// Into the state, as the preview's inputs would take it there. Its animation opens from the node's menu.
			if (Graph->OnJumpToState)
			{
				Graph->OnJumpToState(Node->StateId);
				return FReply::Handled();
			}
			if (Graph->OnOpenState)
			{
				Graph->OnOpenState(Node->StateId);
				return FReply::Handled();
			}
		}
		return SGraphNode::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
	}

	/**
	 * A node whose size changes - a longer animation name, a blend plot appearing - keeps its middle where it was, so the
	 * wires lined up with it (Q) stay straight: it moves by half of what it grew or shrank. The size is kept with the position
	 * in the machine (EditorSize, quietly), and a node made again by a rebuild starts from it - so after an undo or a redo,
	 * which bring back a position with the size it went with, the node lands exactly where it was.
	 */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SGraphNode::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		UWidgetAnimMachineGraphNode* Node = Cast<UWidgetAnimMachineGraphNode>(GraphNode);
		UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
		if (!Graph || !Node->StateId.IsValid() || Node->Kind != EWidgetAnimMachineNodeKind::State)
		{
			return;
		}
		const FVector2f NowSize = FVector2f(FVector2D(GetDesiredSize()));
		if (NowSize.X <= 1.f || NowSize.Y <= 1.f)
		{
			return;
		}
		// Only a size that has held two frames in a row: a node's first frames - the editor just opened, the graph just made
		// again - can measure it on the way to its real size, and moving it by that would take it off its line for good.
		if (!NowSize.Equals(LastSeenSize, 0.5f))
		{
			LastSeenSize = NowSize;
			return;
		}
		// Zoomed: whatever the zoom does to a node's size is not the node changing.
		const TSharedPtr<SGraphPanel> Panel = GetOwnerPanel();
		const float Zoom = Panel.IsValid() ? Panel->GetZoomAmount() : 1.f;
		if (!FMath::IsNearlyEqual(Zoom, Graph->KnownNodeZoom))
		{
			Graph->KnownNodeZoom = Zoom;
			Graph->KnownNodeSizes.Reset();
		}
		FVector2f* Known = Graph->KnownNodeSizes.Find(Node->StateId);
		if (!Known)
		{
			// First seen since the graph was made again or zoomed: the size kept with its position, if kept at this zoom.
			FVector2D KeptSize = FVector2D::ZeroVector;
			float KeptZoom = 0.f;
			const bool bKept = WidgetAnimMachineEdits::GetNodeSize(Graph->Machine, Graph->LayerIndex, Node->StateId, KeptSize, KeptZoom)
				&& KeptSize.X > 1.0 && KeptSize.Y > 1.0 && FMath::IsNearlyEqual(KeptZoom, Zoom);
			Known = &Graph->KnownNodeSizes.Add(Node->StateId, bKept ? FVector2f(KeptSize) : NowSize);
			if (!bKept)
			{
				WidgetAnimMachineEdits::SetNodeSizeQuietly(Graph->Machine, Graph->LayerIndex, Node->StateId, FVector2D(NowSize), Zoom);
			}
		}
		if (Known->Equals(NowSize, 0.5f))
		{
			return;
		}
		const FVector2f Shift = (*Known - NowSize) * 0.5f;
		*Known = NowSize;
		WidgetAnimMachineEdits::SetNodeSizeQuietly(Graph->Machine, Graph->LayerIndex, Node->StateId, FVector2D(NowSize), Zoom);
		const int32 ShiftX = FMath::RoundToInt32(Shift.X);
		const int32 ShiftY = FMath::RoundToInt32(Shift.Y);
		if (ShiftX == 0 && ShiftY == 0)
		{
			return;
		}
		Node->NodePosX += ShiftX;
		Node->NodePosY += ShiftY;
		WidgetAnimMachineEdits::SetNodePositionQuietly(Graph->Machine, Graph->LayerIndex, Node->StateId, FVector2D(Node->NodePosX, Node->NodePosY));
	}

private:

	/** Its size last tick - see Tick: a size is acted on once it has held two frames in a row. */
	FVector2f LastSeenSize = FVector2f::ZeroVector;

	const UWidgetAnimMachineGraphNode* GetMachineNode() const
	{
		return Cast<UWidgetAnimMachineGraphNode>(GraphNode);
	}

	/**
	 * A collapsed group's name, as the Widget Animations panel's collapsed track names itself: italic, in CollapsedNodeText, with
	 * no drop shadow. The state title's own style otherwise.
	 */
	static const FInlineEditableTextBlockStyle& GetGroupTitleStyle()
	{
		// A state's name at a state's size, drawn as a collapsed track's name is in the Widget Animations panel: in its italic,
		// in CollapsedNodeText, over the tracks' slight drop shadow.
		static const FInlineEditableTextBlockStyle Style = []()
		{
			namespace Palette = NameTBD::Colors::WidgetAnimEditor;
			FInlineEditableTextBlockStyle Made = FAppStyle::Get().GetWidgetStyle<FInlineEditableTextBlockStyle>("Graph.StateNode.NodeTitleInlineEditableText");
			FSlateFontInfo Font = Made.TextStyle.Font;
			// Its italic typeface ("Bold" -> "BoldItalic", "Regular" -> "Italic") where its family has one.
			if (const FCompositeFont* Composite = Font.GetCompositeFont())
			{
				const FString Base = Font.TypefaceFontName.IsNone() || Font.TypefaceFontName == TEXT("Regular") ? FString() : Font.TypefaceFontName.ToString();
				const FName Wanted(*(Base + TEXT("Italic")));
				for (const FTypefaceEntry& Entry : Composite->DefaultTypeface.Fonts)
				{
					if (Entry.Name == Wanted)
					{
						Font.TypefaceFontName = Wanted;
						break;
					}
				}
			}
			Made.TextStyle.SetFont(Font);
			Made.TextStyle.SetColorAndOpacity(FSlateColor(Palette::CollapsedNodeText));
			Made.TextStyle.SetShadowOffset(FVector2D(Palette::TrackTextShadowOffset, Palette::TrackTextShadowOffset));
			Made.TextStyle.SetShadowColorAndOpacity(Palette::TrackTextShadow);
			Made.EditableTextBoxStyle.TextStyle.SetFont(Font);
			return Made;
		}();
		return Style;
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
		return Node && GetDebug(Debug) && Node->ShowsState(Debug.CurrentState) && !Debug.ActiveTransition.IsValid();
	}

	/** The lightning badge: shown on a state with On Enter or On Exit actions. */
	EVisibility GetActionsBadgeVisibility() const
	{
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		const FWidgetAnimMachineState* State = Node ? Node->GetState() : nullptr;
		return State && State->HasActions() ? EVisibility::Visible : EVisibility::Collapsed;
	}

	/** A blend state's live weights and input point, while the watched widget is in it. */
	bool GetLiveBlend(TArray<float>& OutWeights, FVector2f& OutPoint) const
	{
		FWidgetAnimMachineLayerDebug Debug;
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		if (!Node || Node->bIsGroup || !GetDebug(Debug) || Debug.CurrentState != Node->StateId || Debug.BlendWeights.Num() == 0)
		{
			return false;
		}
		OutWeights = Debug.BlendWeights;
		OutPoint = Debug.BlendPoint;
		return true;
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
		// A collapsed group's body is its own (SWidgetAnimMachineCollapsedBody): nothing over it.
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();
		if (Node && Node->bIsGroup)
		{
			return FLinearColor::Transparent;
		}
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
		if (!Node || Node->bIsGroup || !GetDebug(Debug) || Debug.CurrentState != Node->StateId || !Debug.bStatePlaying || Debug.ActiveTransition.IsValid())
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
		default:
			// A collapsed group: the collapsed graph's icon, as a Blueprint's collapsed node wears.
			if (Node && Node->bIsGroup)
			{
				return FindBrush({ TEXT("GraphEditor.SubGraph_16x"), TEXT("Graph.StateNode.Icon") });
			}
			return FindBrush({ TEXT("Graph.StateNode.Icon") });
		}
	}

	FText GetName() const
	{
		return GraphNode ? GraphNode->GetNodeTitle(ENodeTitleType::FullTitle) : FText::GetEmpty();
	}

	FText GetSubtitle() const
	{
		const UWidgetAnimMachineGraphNode* Node = GetMachineNode();

		// A collapsed group: how many states it holds.
		if (Node && Node->bIsGroup)
		{
			int32 Count = 0;
			const UWidgetAnimMachineGraph* Graph = Node->GetMachineGraph();
			if (const FWidgetAnimMachineLayer* Layer = Graph ? Graph->GetLayer() : nullptr)
			{
				for (const FWidgetAnimMachineState& Member : Layer->States)
				{
					Count += Member.EditorGroup == Node->StateId ? 1 : 0;
				}
			}
			return FText::Format(LOCTEXT("GroupSubtitle", "{0} {0}|plural(one=state,other=states)"), Count);
		}

		const FWidgetAnimMachineState* State = Node ? Node->GetState() : nullptr;
		if (!State || State->Kind != EWidgetAnimMachineNodeKind::State)
		{
			return FText::GetEmpty();
		}

		// A blend state: by what, over how many.
		if (State->IsBlendState())
		{
			return FText::Format(LOCTEXT("BlendSubtitle", "{0} by {1}, {2} samples"),
				StaticEnum<EWidgetAnimMachineBlendSpace>()->GetDisplayNameTextByValue(static_cast<int64>(State->BlendSpace)),
				State->BlendInput.IsNone() ? LOCTEXT("BlendNoInput", "(no input)") : FText::FromName(State->BlendInput),
				FText::AsNumber(State->BlendSamples.Num()));
		}
		if (State->RandomVariations.Num() > 0)
		{
			// "Idle 40% + 2 random, Loop": how often the state's own animation plays, and how many others it picks from.
			float Total = 0.f;
			for (const FWidgetAnimMachineRandomVariation& Variation : State->RandomVariations)
			{
				Total += FMath::Max(Variation.Chance, 0.f);
			}
			return FText::Format(LOCTEXT("RandomSubtitle", "{0} {1} + {2} random, {3}"),
				State->Animation ? FText::FromString(State->Animation->GetName()) : LOCTEXT("RandomNoMain", "none"),
				FText::AsPercent(FMath::Clamp(1.f - Total, 0.f, 1.f)),
				FText::AsNumber(State->RandomVariations.Num()),
				StaticEnum<EWidgetAnimMachineStatePlayback>()->GetDisplayNameTextByValue(static_cast<int64>(State->Playback)));
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

		// A collapsed group's box.
		if (Node && Node->bIsGroup)
		{
			WidgetAnimMachineStyle::FTipBuilder GroupTip;
			GroupTip.Title(Node->GetNodeTitle(ENodeTitleType::FullTitle), TipPalette::TooltipControlName);
			GroupTip.Kind(LOCTEXT("GroupKind", "Collapsed States"));
			GroupTip.Paragraph(GetSubtitle());
			GroupTip.Hint(MachineTips::GroupNodeHint());
			return GroupTip.Build();
		}

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
		if (State->IsBlendState())
		{
			Tip.Row(LOCTEXT("TipBlend", "Blends"), FText::Format(LOCTEXT("TipBlendValue", "{0} samples by {1}"), FText::AsNumber(State->BlendSamples.Num()), FText::FromName(State->BlendInput)));
		}
		if (!State->SpeedInput.IsNone())
		{
			Tip.Row(LOCTEXT("TipSpeedInput", "Speed From"), FText::FromName(State->SpeedInput));
		}
		if (State->HasActions())
		{
			Tip.Row(LOCTEXT("TipActions", "Actions"), FText::Format(LOCTEXT("TipActionsValue", "{0} on enter, {1} on exit"), FText::AsNumber(State->OnEnter.Num()), FText::AsNumber(State->OnExit.Num())));
		}

		// While watched: the actions its layer ran last.
		FWidgetAnimMachineLayerDebug Debug;
		if (GetDebug(Debug) && Debug.LastActions.Num() > 0)
		{
			Tip.Row(LOCTEXT("TipLastActions", "Last Actions"), FText::FromString(FString::Join(Debug.LastActions, TEXT(", "))));
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

		// While a widget is watched: a dot at its top right for its conditions on other layers' states - green while they all
		// hold, red while one doesn't - so a transition waiting on another layer reads at a glance.
		const TOptional<bool> LayerConditionsHeld = Graph ? Graph->GetLayerConditionsHeld(TransitionNode->TransitionId) : TOptional<bool>();
		if (LayerConditionsHeld.IsSet())
		{
			const FVector2f DotSize(MachineColors::LayerConditionDot, MachineColors::LayerConditionDot);
			const FVector2f DotCenter = Center + FVector2f(Radius, -Radius) * 0.78f;
			const FLinearColor DotColor = LayerConditionsHeld.GetValue() ? MachineColors::LayerConditionHeld : MachineColors::LayerConditionNotHeld;
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 5,
				AllottedGeometry.ToPaintGeometry(DotSize, FSlateLayoutTransform(DotCenter - DotSize * 0.5f)), Round, ESlateDrawEffect::NoPixelSnapping, DotColor * Tint);
		}
		return LayerId + 6;
	}

private:

	TWeakObjectPtr<UWidgetAnimMachineTransitionNode> Node;
	TFunction<FVector2f()> GetDirection;
	TFunction<bool()> IsNodeSelected;
};

namespace
{
	/**
	 * The boxes of the two nodes a transition joins, in graph space, in its pair's order (see GetPairOrder) - where its
	 * transition node and reroute points are placed from. False until both are laid out.
	 */
	bool GetPairBoxes(const TSharedPtr<SGraphPanel>& Panel, const UWidgetAnimMachineGraph* Graph, const UWidgetAnimMachineTransitionNode* Node, FBox2f& OutFirst, FBox2f& OutSecond)
	{
		if (!Panel.IsValid() || !Graph || !Node)
		{
			return false;
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
			return false;
		}
		auto BoxOf = [](const UEdGraphNode* StateNode, const TSharedPtr<SGraphNode>& Widget)
		{
			const FVector2f Min(static_cast<float>(StateNode->NodePosX), static_cast<float>(StateNode->NodePosY));
			return FBox2f(Min, Min + FVector2f(FVector2D(Widget->GetDesiredSize())));
		};
		OutFirst = BoxOf(First, FirstWidget);
		OutSecond = BoxOf(Second, SecondWidget);
		return true;
	}

	/** How lit a transition's wire is: 1 while the watched widget takes it, fading to 0 over WireLinger after (see the wires). */
	float GetWireGlow(const UWidgetAnimMachineGraph* Graph, const FGuid& TransitionId)
	{
		if (!Graph)
		{
			return 0.f;
		}
		FWidgetAnimMachineLayerDebug Debug;
		if (Graph->GetDebug && Graph->GetDebug(Debug) && Debug.ActiveTransition == TransitionId)
		{
			return 1.f;
		}
		const double* LastActive = Graph->LastActiveTimes.Find(TransitionId);
		return LastActive ? FMath::Clamp(1.f - static_cast<float>(FPlatformTime::Seconds() - *LastActive) / MachineColors::WireLinger, 0.f, 1.f) : 0.f;
	}
}

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

		FBox2f FirstBox;
		FBox2f SecondBox;
		if (!GetPairBoxes(Panel, Graph, Node, FirstBox, SecondBox))
		{
			return;
		}
		const FVector2f Anchor = WidgetAnimMachineWires::GetPlacementAnchor(FirstBox, SecondBox);
		const FVector2f MySize = FVector2f(FVector2D(GetDesiredSize()));

		// Moved since it was put here: dragged - on its own or with others. It stays where it was dropped.
		const FIntPoint Current(GraphNode->NodePosX, GraphNode->NodePosY);
		if (bLaidOut && Current != LastLaidOut)
		{
			Node->bPlaced = true;
			Node->PlacedOffset = FVector2f(static_cast<float>(Current.X), static_cast<float>(Current.Y)) + MySize * 0.5f - Anchor;
		}

		const FWidgetAnimMachineWire Wire = Graph->ComputeWire(Node, FirstBox, SecondBox);
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

		// While a widget is watched: each condition on another layer's state, and whether it holds now.
		if (const UWidgetAnimMachineGraph* MachineGraph = Node->GetMachineGraph())
		{
			for (int32 ConditionIndex = 0; ConditionIndex < Transition->Conditions.Num(); ++ConditionIndex)
			{
				const TOptional<bool> Held = MachineGraph->GetLayerConditionHeld(Node->TransitionId, ConditionIndex);
				if (Held.IsSet())
				{
					Tip.Row(Held.GetValue() ? LOCTEXT("TipLayerConditionHolds", "Holds") : LOCTEXT("TipLayerConditionNotHeld", "Doesn't hold"),
						DescribeCondition(Transition->Conditions[ConditionIndex], MachineGraph->Machine),
						Held.GetValue() ? MachineColors::LayerConditionHeld : MachineColors::LayerConditionNotHeld);
				}
			}
		}

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

/**
 * What a reroute point draws: a dot on its wire, as a Blueprint reroute node is - in the wire's own color, lit as the wire is
 * while its transition is taken, lighter under the mouse, and ringed in the selection orange while selected.
 */
class SWidgetAnimMachineRerouteGlyph : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineRerouteGlyph) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakObjectPtr<UWidgetAnimMachineRerouteNode> InNode, TFunction<bool()> InIsSelected)
	{
		Node = InNode;
		IsNodeSelected = MoveTemp(InIsSelected);
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D(WidgetAnimMachineWires::RerouteHitSize, WidgetAnimMachineWires::RerouteHitSize);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const UWidgetAnimMachineRerouteNode* RerouteNode = Node.Get();
		const UWidgetAnimMachineTransitionNode* TransitionNode = RerouteNode ? RerouteNode->TransitionNode.Get() : nullptr;
		const FWidgetAnimMachineTransition* Transition = TransitionNode ? TransitionNode->GetTransition() : nullptr;
		if (!Transition)
		{
			return LayerId;
		}

		// The wire's own color, as the wires are drawn.
		const float Glow = GetWireGlow(RerouteNode->GetMachineGraph(), RerouteNode->TransitionId);
		FLinearColor Color = MachineColors::TransitionLine.CopyWithNewOpacity(1.f);
		if (Glow > 0.f)
		{
			Color = FMath::Lerp(Color, MachineColors::WireActive, Glow);
		}
		else if (!Transition->bEnabled)
		{
			Color = MachineColors::TransitionDisabled;
		}
		if (IsHovered())
		{
			Color = FMath::Lerp(Color, FLinearColor::White, 0.35f);
		}
		const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();

		const FVector2f Center = AllottedGeometry.GetLocalSize() * 0.5f;
		const FSlateBrush* Round = WidgetAnimMachineStyle::RoundBrush();
		auto DrawCircle = [&](int32 Layer, float Radius, const FLinearColor& CircleColor)
		{
			const FVector2f CircleSize(Radius * 2.f, Radius * 2.f);
			FSlateDrawElement::MakeBox(OutDrawElements, Layer,
				AllottedGeometry.ToPaintGeometry(CircleSize, FSlateLayoutTransform(Center - CircleSize * 0.5f)), Round, ESlateDrawEffect::NoPixelSnapping, CircleColor);
		};

		const float Radius = WidgetAnimMachineWires::RerouteDiameter * 0.5f;
		if (IsNodeSelected && IsNodeSelected())
		{
			// Selected: a band of the selection orange round it, as round a selected transition node.
			DrawCircle(LayerId, Radius + MachineColors::TransitionSelectedWidth - MachineColors::TransitionSelectedOverlap,
				FMath::Lerp(MachineColors::TransitionSelectedTop, MachineColors::TransitionSelectedBottom, 0.5f) * Tint);
		}
		else
		{
			// A thin dark rim, so it reads against the wire it sits on.
			DrawCircle(LayerId, Radius + 1.f, MachineColors::TransitionArrow * Tint);
		}
		DrawCircle(LayerId + 1, Radius, Color * Tint);
		return LayerId + 2;
	}

private:

	TWeakObjectPtr<UWidgetAnimMachineRerouteNode> Node;
	TFunction<bool()> IsNodeSelected;
};

/** A reroute point on a transition's wire: dragged like any node, its wire running through it. */
class SWidgetAnimMachineRerouteNode : public SGraphNode
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachineRerouteNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWidgetAnimMachineRerouteNode* InNode)
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

		const TWeakPtr<SWidgetAnimMachineRerouteNode> WeakSelf = SharedThis(this);
		this->ContentScale.Bind(this, &SGraphNode::GetContentScale);
		this->GetOrAddSlot(ENodeZone::Center)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SWidgetAnimMachineRerouteGlyph,
					TWeakObjectPtr<UWidgetAnimMachineRerouteNode>(Cast<UWidgetAnimMachineRerouteNode>(GraphNode)),
					[WeakSelf]() { const TSharedPtr<SWidgetAnimMachineRerouteNode> Self = WeakSelf.Pin(); return Self.IsValid() && Self->IsSelected(); })
				.ToolTip(WidgetAnimMachineStyle::MakeDynamicToolTip([WeakSelf]() -> TSharedPtr<SWidget>
				{
					const TSharedPtr<SWidgetAnimMachineRerouteNode> Self = WeakSelf.Pin();
					return Self.IsValid() ? Self->MakeToolTipContent() : nullptr;
				}))
			];
	}

	virtual void CreatePinWidgets() override
	{
		// None: the wire is drawn through it (see the wires).
	}

	/** No selection brush from the graph editor - a rounded box round a dot. The dot draws its own ring (see the glyph). */
	virtual const FSlateBrush* GetShadowBrush(bool bSelected) const override
	{
		return FAppStyle::GetNoBrush();
	}

	virtual bool RequiresSecondPassLayout() const override
	{
		return true;
	}

	/** Dragged by the graph - taken here, as the transition node takes its own (see SWidgetAnimMachineTransitionNode::MoveTo). */
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

	/** Alt+click: takes the point off its wire. */
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && MouseEvent.IsAltDown())
		{
			const UWidgetAnimMachineRerouteNode* Node = Cast<UWidgetAnimMachineRerouteNode>(GraphNode);
			UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
			if (Graph)
			{
				TMap<FGuid, TArray<int32>> Reroutes;
				Reroutes.Add(Node->TransitionId, TArray<int32>{ Node->RerouteIndex });
				WidgetAnimMachineEdits::Delete(Graph->Machine, Graph->LayerIndex, TArray<FGuid>(), TArray<FGuid>(), TArray<FGuid>(), Reroutes);
				return FReply::Handled();
			}
		}
		return SGraphNode::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	/**
	 * Where its transition keeps it, from halfway between the two nodes the transition joins - so it follows them. Dragged
	 * (moved by the graph since it was last put here), it stays where it was dropped, and its transition keeps it there.
	 */
	virtual void PerformSecondPassLayout(const TMap<UObject*, TSharedRef<SNodePanel::SNode>>& NodeToWidgetLookup) const override
	{
		UWidgetAnimMachineRerouteNode* Node = Cast<UWidgetAnimMachineRerouteNode>(GraphNode);
		UWidgetAnimMachineTransitionNode* TransitionNode = Node ? Node->TransitionNode.Get() : nullptr;
		const UWidgetAnimMachineGraph* Graph = Node ? Node->GetMachineGraph() : nullptr;
		FBox2f FirstBox;
		FBox2f SecondBox;
		if (!TransitionNode || !TransitionNode->RerouteOffsets.IsValidIndex(Node->RerouteIndex) || !GetPairBoxes(OwnerGraphPanelPtr.Pin(), Graph, TransitionNode, FirstBox, SecondBox))
		{
			return;
		}

		const FVector2f Anchor = WidgetAnimMachineWires::GetPlacementAnchor(FirstBox, SecondBox);
		const FVector2f MySize = FVector2f(FVector2D(GetDesiredSize()));
		FVector2f& PointOffset = TransitionNode->RerouteOffsets[Node->RerouteIndex];

		const FIntPoint Current(GraphNode->NodePosX, GraphNode->NodePosY);
		if (bLaidOut && Current != LastLaidOut)
		{
			PointOffset = FVector2f(static_cast<float>(Current.X), static_cast<float>(Current.Y)) + MySize * 0.5f - Anchor;
		}

		const FVector2f Position = Anchor + PointOffset - MySize * 0.5f;
		GraphNode->NodePosX = FMath::RoundToInt32(Position.X);
		GraphNode->NodePosY = FMath::RoundToInt32(Position.Y);
		LastLaidOut = FIntPoint(GraphNode->NodePosX, GraphNode->NodePosY);
		bLaidOut = true;
	}

private:

	/** Where it was last put, so a move by the graph - a drag - is told from its own placing. */
	mutable FIntPoint LastLaidOut = FIntPoint::ZeroValue;
	mutable bool bLaidOut = false;

	bool IsSelected() const
	{
		const TSharedPtr<SGraphPanel> Panel = OwnerGraphPanelPtr.Pin();
		return Panel.IsValid() && GraphNode && Panel->SelectionManager.IsNodeSelected(GraphNode);
	}

	TSharedPtr<SWidget> MakeToolTipContent() const
	{
		const UWidgetAnimMachineRerouteNode* Node = Cast<UWidgetAnimMachineRerouteNode>(GraphNode);
		if (!Node || !Node->TransitionNode)
		{
			return nullptr;
		}
		WidgetAnimMachineStyle::FTipBuilder Tip;
		Tip.Title(Node->TransitionNode->GetNodeTitle(ENodeTitleType::FullTitle), TipPalette::TooltipControlName);
		Tip.Kind(LOCTEXT("RerouteKind", "Reroute Point"));
		Tip.Paragraph(MachineTips::RerouteNodeTooltip());
		return Tip.Build();
	}
};

TSharedPtr<SGraphNode> UWidgetAnimMachineGraphNode::CreateVisualWidget()
{
	return SNew(SWidgetAnimMachineStateNode, this);
}

TSharedPtr<SGraphNode> UWidgetAnimMachineRerouteNode::CreateVisualWidget()
{
	return SNew(SWidgetAnimMachineRerouteNode, this);
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

		FWidgetAnimMachineLayerDebug Debug;
		const bool bDebugging = Graph->GetDebug && Graph->GetDebug(Debug);
		const double Now = FPlatformTime::Seconds();

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

			// On screen: the same curve, scaled.
			FWidgetAnimMachineWire Wire = Graph->ComputeWire(Transition, *FirstBox, *SecondBox);
			Wire.Transform(Origin, Scale);

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

			// Only a wire that can show: each curve lies within its four control points, so one whose points' box - with room
			// for its width and its dots - is off screen draws nothing. A big machine has most of its wires off screen.
			if (IsWireOnScreen(Wire, FMath::Max(Thickness, MachineColors::WireBubbleSize) * Scale))
			{
				// Curve by curve: through the dragged node and every reroute point, if it has any.
				for (const FWidgetAnimMachineWire::FSegment& Segment : Wire.Segments)
				{
					FSlateDrawElement::MakeDrawSpaceSpline(DrawElementsList, WireLayerID, Segment.Start, Segment.StartTangent, Segment.End, Segment.EndTangent, Thickness * Scale, ESlateDrawEffect::None, Color);
				}

				// Dots running along it, the way it goes, as along a wire in Blueprint simulation.
				if (Glow > 0.f)
				{
					DrawBubbles(Wire, Scale, Now, Glow);
				}
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

		// The labels last, once every wire is known, each where it is clearest. The nodes and the labels placed so far kept in
		// grids, so each label is weighed against what is near it - not against every node and label there is.
		FBoxGrid Obstacles(LabelGridCell * Scale);
		for (const TPair<const UEdGraphNode*, FBox2f>& Box : ScreenBoxes)
		{
			// Comment boxes are frames round nodes, not in the way of a label.
			if (!Box.Key->IsA<UEdGraphNode_Comment>())
			{
				Obstacles.Add(Box.Value);
			}
		}
		FBoxGrid Labels(LabelGridCell * Scale);
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

	/** The grids' cell, in graph units: about a label's size, so a label looks through a cell or two of each. */
	static constexpr float LabelGridCell = 64.f;

	/**
	 * Boxes kept in a coarse grid, so those near a box are found without going through them all - each once, however many of
	 * its cells it shares with the box asked about. A box too big for the grid is kept apart, and always looked at.
	 */
	class FBoxGrid
	{
	public:

		explicit FBoxGrid(float InCellSize)
			: CellSize(FMath::Max(InCellSize, 1.f))
		{
		}

		void Add(const FBox2f& Box)
		{
			const int32 Index = Boxes.Add(Box);
			Visited.Add(0);
			const bool bGridded = ForEachCell(Box, [this, Index](const FIntPoint& Cell)
			{
				Cells.FindOrAdd(Cell).Add(Index);
			});
			if (!bGridded)
			{
				Large.Add(Index);
			}
		}

		/** Every box that may overlap Box: none that does is left out. */
		template<typename VisitType>
		void ForEachNear(const FBox2f& Box, VisitType&& Visit)
		{
			++Query;
			auto VisitIndex = [this, &Visit](int32 Index)
			{
				if (Visited[Index] != Query)
				{
					Visited[Index] = Query;
					Visit(Boxes[Index]);
				}
			};

			for (const int32 Index : Large)
			{
				VisitIndex(Index);
			}
			const bool bGridded = ForEachCell(Box, [this, &VisitIndex](const FIntPoint& Cell)
			{
				if (const TArray<int32>* InCell = Cells.Find(Cell))
				{
					for (const int32 Index : *InCell)
					{
						VisitIndex(Index);
					}
				}
			});
			if (!bGridded)
			{
				for (int32 Index = 0; Index < Boxes.Num(); ++Index)
				{
					VisitIndex(Index);
				}
			}
		}

	private:

		/** Most cells one box is filed under; past it - or off any sensible scale - it isn't, and false is returned. */
		static constexpr int64 MaxCellsPerBox = 256;

		template<typename CellVisitType>
		bool ForEachCell(const FBox2f& Box, CellVisitType&& VisitCell) const
		{
			const double MinX = FMath::FloorToDouble(Box.Min.X / CellSize);
			const double MinY = FMath::FloorToDouble(Box.Min.Y / CellSize);
			const double MaxX = FMath::FloorToDouble(Box.Max.X / CellSize);
			const double MaxY = FMath::FloorToDouble(Box.Max.Y / CellSize);
			constexpr double Limit = 1.0e8;
			if (!(FMath::Abs(MinX) < Limit && FMath::Abs(MinY) < Limit && FMath::Abs(MaxX) < Limit && FMath::Abs(MaxY) < Limit)
				|| MaxX < MinX || MaxY < MinY
				|| (static_cast<int64>(MaxX - MinX) + 1) * (static_cast<int64>(MaxY - MinY) + 1) > MaxCellsPerBox)
			{
				return false;
			}

			for (int32 Y = static_cast<int32>(MinY); Y <= static_cast<int32>(MaxY); ++Y)
			{
				for (int32 X = static_cast<int32>(MinX); X <= static_cast<int32>(MaxX); ++X)
				{
					VisitCell(FIntPoint(X, Y));
				}
			}
			return true;
		}

		float CellSize = 1.f;
		TArray<FBox2f> Boxes;
		TArray<uint32> Visited;
		uint32 Query = 0;
		TMap<FIntPoint, TArray<int32>> Cells;
		TArray<int32> Large;
	};

	/** Whether any of a wire can be on screen: its curves' control points, grown by Margin, against what is drawn. */
	bool IsWireOnScreen(const FWidgetAnimMachineWire& Wire, float Margin) const
	{
		if (Wire.Segments.Num() == 0)
		{
			return false;
		}

		// A segment's Bezier control points from its ends and tangents, as the spline is drawn from them.
		FBox2f Bounds(ForceInit);
		for (const FWidgetAnimMachineWire::FSegment& Segment : Wire.Segments)
		{
			Bounds += Segment.Start;
			Bounds += Segment.End;
			Bounds += Segment.Start + Segment.StartTangent / 3.f;
			Bounds += Segment.End - Segment.EndTangent / 3.f;
		}
		const FSlateRect WireRect(Bounds.Min.X - Margin, Bounds.Min.Y - Margin, Bounds.Max.X + Margin, Bounds.Max.Y + Margin);
		return FSlateRect::DoRectanglesIntersect(WireRect, ClippingRect);
	}

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
		const int32 Segments = 32 * FMath::Max(Wire.Segments.Num(), 1);
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
	void DrawLengthLabel(const FString& Text, const FVector2f& Point, const FVector2f& Direction, float Scale, FBoxGrid& Obstacles, FBoxGrid& InOutLabels)
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
			Obstacles.ForEachNear(Box, [&Score, &Box, &Overlap](const FBox2f& Obstacle)
			{
				Score += Overlap(Box, Obstacle);
			});
			InOutLabels.ForEachNear(Box, [&Score, &Box, &Overlap](const FBox2f& Label)
			{
				Score += Overlap(Box, Label) * 2.f;
			});
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
