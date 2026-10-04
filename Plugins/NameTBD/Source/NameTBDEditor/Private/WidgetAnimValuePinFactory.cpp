// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimValuePinFactory.h"

#include "EdGraphSchema_K2.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformTime.h"
#include "K2Node_SetWidgetAnimationValue.h"
#include "K2Node_SetWidgetAnimationProgress.h"
#include "K2Node_SetStateMachineInput.h"
#include "Misc/PackageName.h"
#include "NameTBDTooltips.h"
#include "SGraphPin.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimValuePinFactory"

namespace WidgetAnimValuePin
{
	/** How long a pin's label is trusted before it is worked out again. Working it out walks the Widget Blueprint's
	 *  objects, too much to do every frame the graph paints, but a track renamed in the Widget Animations editor
	 *  should still show here without the node being touched. */
	constexpr double LabelLifetimeSeconds = 1.0;

	FText GetKindText(EWidgetAnimValueKind Kind)
	{
		return StaticEnum<EWidgetAnimValueKind>()->GetDisplayNameTextByValue(static_cast<int64>(Kind));
	}

	FText MakeChoiceLabel(const FWidgetAnimValueChoice& Choice)
	{
		return FText::Format(LOCTEXT("ChoiceLabel", "{0}  ({1})"), FText::FromName(Choice.ValueName), GetKindText(Choice.Kind));
	}

	/** A source's values as one line, for its menu entry's tooltip. */
	FText DescribeValues(const FWidgetAnimValueSource& Source)
	{
		if (Source.Values.Num() == 0)
		{
			return NameTBD::Tooltips::WidgetAnimValuePins::SourceHasNoValues();
		}

		TArray<FText> Labels;
		for (const FWidgetAnimValueChoice& Choice : Source.Values)
		{
			Labels.Add(MakeChoiceLabel(Choice));
		}
		return FText::Format(NameTBD::Tooltips::WidgetAnimValuePins::SourceValues(), FText::Join(FText::FromString(TEXT(", ")), Labels));
	}

	/** Sets Pin's default as if typed into it, so the node hears of it and it can be undone. */
	void SetPinDefault(UEdGraphPin* Pin, const FString& NewValue)
	{
		if (!Pin || Pin->WasTrashed() || Pin->DefaultValue == NewValue)
		{
			return;
		}

		Pin->Modify();
		Pin->GetSchema()->TrySetDefaultValue(*Pin, NewValue);
	}
}

/**
 * The Set Widget Animation Value node's Animation or Value Name pin, as a dropdown. The node itself works out what
 * can be picked - GetValueSources and GetValueChoices - so this only shows it and writes the pick back.
 */
class SWidgetAnimValuePin : public SGraphPin
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimValuePin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
	{
		bAnimationPin = InGraphPinObj && InGraphPinObj->PinName == UK2Node_SetWidgetAnimationValue::AnimationPinName;
		SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
	}

protected:

	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		return SNew(SComboButton)
			.Visibility(this, &SWidgetAnimValuePin::GetDefaultValueVisibility)
			.IsEnabled(this, &SWidgetAnimValuePin::GetDefaultValueIsEditable)
			.ContentPadding(FMargin(2.0f, 0.0f))
			.ToolTipText(this, &SWidgetAnimValuePin::GetButtonTooltip)
			.OnGetMenuContent(this, &SWidgetAnimValuePin::MakeMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
				.Text(this, &SWidgetAnimValuePin::GetButtonText)
			];
	}

private:

	/** Which of the node's two dropdown pins this is: the Animation, or the Value Name. */
	bool bAnimationPin = false;

	// The button's label and tooltip, worked out now and then - see LabelLifetimeSeconds - or when the pick changes.
	mutable FString CachedFor;
	mutable double CachedAt = -1.0;
	mutable FText CachedText;
	mutable FText CachedTooltip;

	/** The pick last reported as not on the Target, so the search is logged once per pick rather than every refresh. */
	mutable FString LoggedMissingFor;

	UK2Node_SetWidgetAnimationValue* GetNode() const
	{
		return GraphPinObj && !GraphPinObj->WasTrashed() ? Cast<UK2Node_SetWidgetAnimationValue>(GraphPinObj->GetOwningNodeUnchecked()) : nullptr;
	}

	/** Whether the node is Set Widget Animation Progress, which picks only percentage animations. */
	bool IsProgressNode() const
	{
		return Cast<UK2Node_SetWidgetAnimationProgress>(GetNode()) != nullptr;
	}

	FString GetAnimationKey() const
	{
		const UK2Node_SetWidgetAnimationValue* Node = GetNode();
		const UEdGraphPin* AnimationPin = Node ? Node->FindPin(UK2Node_SetWidgetAnimationValue::AnimationPinName) : nullptr;
		return AnimationPin ? AnimationPin->DefaultValue : FString();
	}

	FText GetButtonText() const
	{
		RefreshLabel();
		return CachedText;
	}

	FText GetButtonTooltip() const
	{
		RefreshLabel();
		return CachedTooltip;
	}

	void RefreshLabel() const
	{
		const UK2Node_SetWidgetAnimationValue* Node = GetNode();
		if (!Node)
		{
			CachedText = FText::GetEmpty();
			CachedTooltip = FText::GetEmpty();
			return;
		}

		const FString AnimationKey = GetAnimationKey();
		const FString Current = GraphPinObj->DefaultValue;
		const FString CacheKey = bAnimationPin ? Current : AnimationKey + TEXT("|") + Current;
		const double Now = FPlatformTime::Seconds();
		if (CacheKey == CachedFor && CachedAt >= 0.0 && Now - CachedAt < WidgetAnimValuePin::LabelLifetimeSeconds)
		{
			return;
		}
		CachedFor = CacheKey;
		CachedAt = Now;

		TArray<FWidgetAnimValueSource> Sources;
		Node->GetValueSources(Sources);

		if (bAnimationPin)
		{
			CachedTooltip = IsProgressNode() ? NameTBD::Tooltips::WidgetAnimValuePins::ProgressAnimationPicker() : NameTBD::Tooltips::WidgetAnimValuePins::AnimationPicker();
			if (Current.IsEmpty())
			{
				CachedText = LOCTEXT("PickAnimation", "Pick an Animation");
			}
			else if (const FWidgetAnimValueSource* Source = Sources.FindByPredicate([&Current](const FWidgetAnimValueSource& Candidate) { return Candidate.Key == Current; }))
			{
				CachedText = Source->Label;
			}
			else
			{
				CachedText = FText::Format(LOCTEXT("AnimationMissing", "{0}  (not on the Target)"), FText::FromString(FPackageName::ObjectPathToObjectName(Current)));
				CachedTooltip = NameTBD::Tooltips::WidgetAnimValuePins::AnimationMissing();

				// Where it looked and what it found there, in the Output Log, so a missed animation can be told apart
				// from one that really isn't reachable - once per pick.
				if (LoggedMissingFor != Current)
				{
					LoggedMissingFor = Current;
					Node->LogValueSourceSearch();
				}
			}
			return;
		}

		CachedTooltip = NameTBD::Tooltips::WidgetAnimValuePins::ValuePicker();
		const FName Name(*Current);
		if (Name.IsNone())
		{
			CachedText = LOCTEXT("PickValue", "Pick a Value");
			return;
		}

		// With an animation picked, only its values count; with none, the name is set wherever it is, so any will do.
		const FWidgetAnimValueChoice* Found = nullptr;
		for (const FWidgetAnimValueSource& Source : Sources)
		{
			if (!AnimationKey.IsEmpty() && Source.Key != AnimationKey)
			{
				continue;
			}
			Found = Source.Values.FindByPredicate([Name](const FWidgetAnimValueChoice& Choice) { return Choice.ValueName == Name; });
			if (Found)
			{
				break;
			}
		}

		if (Found)
		{
			CachedText = WidgetAnimValuePin::MakeChoiceLabel(*Found);
		}
		else
		{
			CachedText = FText::Format(LOCTEXT("ValueMissing", "{0}  (not found)"), FText::FromName(Name));
			CachedTooltip = NameTBD::Tooltips::WidgetAnimValuePins::ValueMissing();
		}
	}

	TSharedRef<SWidget> MakeMenu()
	{
		FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);

		const UK2Node_SetWidgetAnimationValue* Node = GetNode();
		if (!Node)
		{
			return Menu.MakeWidget();
		}

		TArray<FWidgetAnimValueSource> Sources;
		Node->GetValueSources(Sources);

		if (bAnimationPin)
		{
			MakeAnimationMenu(Menu, Sources);
		}
		else
		{
			MakeValueMenu(Menu, Sources);
		}
		return Menu.MakeWidget();
	}

	void MakeAnimationMenu(FMenuBuilder& Menu, const TArray<FWidgetAnimValueSource>& Sources)
	{
		// The animations first, then the additive tracks - which are always there, even with none authored yet.
		Menu.BeginSection(NAME_None, LOCTEXT("AnimationsSection", "Animations"));
		bool bAnyAnimation = false;
		for (const FWidgetAnimValueSource& Source : Sources)
		{
			if (Source.Key == UK2Node_SetWidgetAnimationValue::AdditiveTracksKey)
			{
				continue;
			}
			bAnyAnimation = true;
			AddAnimationEntry(Menu, Source);
		}
		if (!bAnyAnimation)
		{
			if (IsProgressNode())
			{
				AddNote(Menu, LOCTEXT("NoPercentAnimations", "No percentage animations"), NameTBD::Tooltips::WidgetAnimValuePins::NoPercentAnimations());
			}
			else
			{
				AddNote(Menu, LOCTEXT("NoAnimations", "No animations"), NameTBD::Tooltips::WidgetAnimValuePins::NoAnimations());
			}

			// Where it looked, in the Output Log, so an empty list can be told apart from a missed one.
			if (const UK2Node_SetWidgetAnimationValue* Node = GetNode())
			{
				Node->LogValueSourceSearch();
			}
		}
		Menu.EndSection();

		// The additive tracks, when the node offers them - Set Widget Animation Progress doesn't.
		const FWidgetAnimValueSource* Additive = Sources.FindByPredicate([](const FWidgetAnimValueSource& Source) { return Source.Key == UK2Node_SetWidgetAnimationValue::AdditiveTracksKey; });
		if (Additive)
		{
			Menu.BeginSection(NAME_None, LOCTEXT("AdditiveSection", "This Widget"));
			AddAnimationEntry(Menu, *Additive);
			Menu.EndSection();
		}
	}

	void AddAnimationEntry(FMenuBuilder& Menu, const FWidgetAnimValueSource& Source)
	{
		Menu.AddMenuEntry(
			Source.Label,
			IsProgressNode() ? NameTBD::Tooltips::WidgetAnimValuePins::ProgressAnimationPicker() : WidgetAnimValuePin::DescribeValues(Source),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SWidgetAnimValuePin::PickAnimation, Source.Key),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SWidgetAnimValuePin::IsAnimationPicked, Source.Key)),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}

	void MakeValueMenu(FMenuBuilder& Menu, const TArray<FWidgetAnimValueSource>& Sources)
	{
		// With an animation picked, its values. With none, every source's, under its name - and picking one picks
		// its animation too, since a value is known by the two together.
		const FString AnimationKey = GetAnimationKey();
		bool bAnyValue = false;
		for (const FWidgetAnimValueSource& Source : Sources)
		{
			if (!AnimationKey.IsEmpty() && Source.Key != AnimationKey)
			{
				continue;
			}
			if (AnimationKey.IsEmpty() && Source.Values.Num() == 0)
			{
				continue;
			}

			Menu.BeginSection(NAME_None, Source.Label);
			for (const FWidgetAnimValueChoice& Choice : Source.Values)
			{
				bAnyValue = true;
				Menu.AddMenuEntry(
					WidgetAnimValuePin::MakeChoiceLabel(Choice),
					NameTBD::Tooltips::WidgetAnimValuePins::ValuePicker(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &SWidgetAnimValuePin::PickValue, Source.Key, Choice.ValueName),
						FCanExecuteAction(),
						FIsActionChecked::CreateSP(this, &SWidgetAnimValuePin::IsValuePicked, Source.Key, Choice.ValueName)),
					NAME_None,
					EUserInterfaceActionType::RadioButton);
			}
			Menu.EndSection();
		}

		if (!bAnyValue)
		{
			AddNote(Menu, LOCTEXT("NoValues", "No value-driven tracks"), NameTBD::Tooltips::WidgetAnimValuePins::NoValues());
		}
	}

	/** A grayed-out line that says why there is nothing to pick. */
	static void AddNote(FMenuBuilder& Menu, const FText& Label, const FText& Tooltip)
	{
		Menu.AddMenuEntry(
			Label,
			Tooltip,
			FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	}

	bool IsAnimationPicked(FString Key) const
	{
		return GraphPinObj && !GraphPinObj->WasTrashed() && GraphPinObj->DefaultValue == Key;
	}

	bool IsValuePicked(FString SourceKey, FName Name) const
	{
		return GraphPinObj && !GraphPinObj->WasTrashed() && GetAnimationKey() == SourceKey && FName(*GraphPinObj->DefaultValue) == Name;
	}

	void PickAnimation(FString Key)
	{
		const FScopedTransaction Transaction(LOCTEXT("PickAnimationTransaction", "Pick Widget Animation"));
		WidgetAnimValuePin::SetPinDefault(GraphPinObj, Key);
		CachedAt = -1.0;
	}

	void PickValue(FString SourceKey, FName Name)
	{
		UK2Node_SetWidgetAnimationValue* Node = GetNode();
		if (!Node)
		{
			return;
		}

		// The animation first: picking it keeps the name only if it has one by that name, which is then set anyway.
		const FScopedTransaction Transaction(LOCTEXT("PickValueTransaction", "Pick Widget Animation Value"));
		WidgetAnimValuePin::SetPinDefault(Node->FindPin(UK2Node_SetWidgetAnimationValue::AnimationPinName), SourceKey);
		WidgetAnimValuePin::SetPinDefault(GraphPinObj, Name.ToString());
		CachedAt = -1.0;
	}
};

/**
 * The Set State Machine Input node's State Machine or Input pin, as a dropdown: the machines the Widget Animation Components
 * on the Target and inside it play, and the picked machine's inputs, each with its type. See UK2Node_SetStateMachineInput.
 */
class SWidgetAnimMachinePin : public SGraphPin
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimMachinePin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
	{
		bMachinePin = InGraphPinObj && InGraphPinObj->PinName == UK2Node_SetStateMachineInput::StateMachinePinName;
		SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
	}

protected:

	virtual TSharedRef<SWidget> GetDefaultValueWidget() override
	{
		return SNew(SComboButton)
			.Visibility(this, &SWidgetAnimMachinePin::GetDefaultValueVisibility)
			.IsEnabled(this, &SWidgetAnimMachinePin::GetDefaultValueIsEditable)
			.ContentPadding(FMargin(2.0f, 0.0f))
			.ToolTipText(this, &SWidgetAnimMachinePin::GetButtonTooltip)
			.OnGetMenuContent(this, &SWidgetAnimMachinePin::MakeMenu)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
				.Text(this, &SWidgetAnimMachinePin::GetButtonText)
			];
	}

private:

	/** Which of the node's two dropdown pins this is: the State Machine, or the Input. */
	bool bMachinePin = false;

	mutable FString CachedFor;
	mutable double CachedAt = -1.0;
	mutable FText CachedText;
	mutable FText CachedTooltip;

	UK2Node_SetStateMachineInput* GetNode() const
	{
		return GraphPinObj && !GraphPinObj->WasTrashed() ? Cast<UK2Node_SetStateMachineInput>(GraphPinObj->GetOwningNodeUnchecked()) : nullptr;
	}

	FString GetMachineKey() const
	{
		const UK2Node_SetStateMachineInput* Node = GetNode();
		const UEdGraphPin* MachinePin = Node ? Node->FindPin(UK2Node_SetStateMachineInput::StateMachinePinName) : nullptr;
		return MachinePin ? MachinePin->DefaultValue : FString();
	}

	static FText GetTypeText(EWidgetAnimMachineInputType Type)
	{
		return StaticEnum<EWidgetAnimMachineInputType>()->GetDisplayNameTextByValue(static_cast<int64>(Type));
	}

	static FText MakeInputLabel(FName Name, EWidgetAnimMachineInputType Type)
	{
		return FText::Format(LOCTEXT("MachineInputLabel", "{0}  ({1})"), FText::FromName(Name), GetTypeText(Type));
	}

	FText GetButtonText() const
	{
		RefreshLabel();
		return CachedText;
	}

	FText GetButtonTooltip() const
	{
		RefreshLabel();
		return CachedTooltip;
	}

	void RefreshLabel() const
	{
		const UK2Node_SetStateMachineInput* Node = GetNode();
		if (!Node)
		{
			CachedText = FText::GetEmpty();
			CachedTooltip = FText::GetEmpty();
			return;
		}

		const FString MachineKey = GetMachineKey();
		const FString Current = GraphPinObj->DefaultValue;
		const FString CacheKey = bMachinePin ? Current : MachineKey + TEXT("|") + Current;
		const double Now = FPlatformTime::Seconds();
		if (CacheKey == CachedFor && CachedAt >= 0.0 && Now - CachedAt < WidgetAnimValuePin::LabelLifetimeSeconds)
		{
			return;
		}
		CachedFor = CacheKey;
		CachedAt = Now;

		if (bMachinePin)
		{
			CachedTooltip = NameTBD::Tooltips::WidgetAnimValuePins::StateMachinePicker();
			if (Current.IsEmpty())
			{
				CachedText = LOCTEXT("PickMachine", "Pick a State Machine");
				return;
			}
			TArray<FWidgetAnimMachineChoice> Choices;
			Node->GetMachineChoices(Choices);
			if (const FWidgetAnimMachineChoice* Choice = Choices.FindByPredicate([&Current](const FWidgetAnimMachineChoice& Candidate) { return Candidate.Key == Current; }))
			{
				CachedText = Choice->Label;
			}
			else
			{
				CachedText = FText::Format(LOCTEXT("MachineMissing", "{0}  (not on the Target)"), FText::FromString(FPackageName::ObjectPathToObjectName(Current)));
				CachedTooltip = NameTBD::Tooltips::WidgetAnimValuePins::StateMachineMissing();
			}
			return;
		}

		CachedTooltip = NameTBD::Tooltips::WidgetAnimValuePins::MachineInputPicker();
		const FName Name(*Current);
		if (Name.IsNone())
		{
			CachedText = LOCTEXT("PickInput", "Pick an Input");
			return;
		}
		TArray<TPair<FName, EWidgetAnimMachineInputType>> Inputs;
		Node->GetInputChoices(Inputs);
		if (const TPair<FName, EWidgetAnimMachineInputType>* Found = Inputs.FindByPredicate([Name](const TPair<FName, EWidgetAnimMachineInputType>& Input) { return Input.Key == Name; }))
		{
			CachedText = MakeInputLabel(Found->Key, Found->Value);
		}
		else
		{
			CachedText = FText::Format(LOCTEXT("InputMissing", "{0}  (not found)"), FText::FromName(Name));
			CachedTooltip = NameTBD::Tooltips::WidgetAnimValuePins::MachineInputMissing();
		}
	}

	TSharedRef<SWidget> MakeMenu()
	{
		FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
		const UK2Node_SetStateMachineInput* Node = GetNode();
		if (!Node)
		{
			return Menu.MakeWidget();
		}

		if (bMachinePin)
		{
			TArray<FWidgetAnimMachineChoice> Choices;
			Node->GetMachineChoices(Choices);
			Menu.BeginSection(NAME_None, LOCTEXT("MachinesSection", "State Machines"));
			for (const FWidgetAnimMachineChoice& Choice : Choices)
			{
				TArray<FText> InputLabels;
				for (const TPair<FName, EWidgetAnimMachineInputType>& Input : Choice.Inputs)
				{
					InputLabels.Add(MakeInputLabel(Input.Key, Input.Value));
				}
				const FText Tooltip = InputLabels.Num() > 0
					? FText::Format(NameTBD::Tooltips::WidgetAnimValuePins::StateMachineInputs(), FText::Join(FText::FromString(TEXT(", ")), InputLabels))
					: NameTBD::Tooltips::WidgetAnimValuePins::StateMachineHasNoInputs();
				Menu.AddMenuEntry(
					Choice.Label,
					Tooltip,
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &SWidgetAnimMachinePin::Pick, Choice.Key),
						FCanExecuteAction(),
						FIsActionChecked::CreateSP(this, &SWidgetAnimMachinePin::IsPicked, Choice.Key)),
					NAME_None,
					EUserInterfaceActionType::RadioButton);
			}
			if (Choices.Num() == 0)
			{
				AddNote(Menu, LOCTEXT("NoMachines", "No state machines"), NameTBD::Tooltips::WidgetAnimValuePins::NoStateMachines());
			}
			Menu.EndSection();
			return Menu.MakeWidget();
		}

		TArray<TPair<FName, EWidgetAnimMachineInputType>> Inputs;
		Node->GetInputChoices(Inputs);
		Menu.BeginSection(NAME_None, LOCTEXT("InputsSection", "Inputs"));
		for (const TPair<FName, EWidgetAnimMachineInputType>& Input : Inputs)
		{
			const FString Key = Input.Key.ToString();
			Menu.AddMenuEntry(
				MakeInputLabel(Input.Key, Input.Value),
				NameTBD::Tooltips::WidgetAnimValuePins::MachineInputPicker(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SWidgetAnimMachinePin::Pick, Key),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &SWidgetAnimMachinePin::IsPicked, Key)),
				NAME_None,
				EUserInterfaceActionType::RadioButton);
		}
		if (Inputs.Num() == 0)
		{
			AddNote(Menu, GetMachineKey().IsEmpty() ? LOCTEXT("PickMachineFirst", "Pick a State Machine first") : LOCTEXT("NoInputs", "No inputs"),
				NameTBD::Tooltips::WidgetAnimValuePins::NoMachineInputs());
		}
		Menu.EndSection();
		return Menu.MakeWidget();
	}

	static void AddNote(FMenuBuilder& Menu, const FText& Label, const FText& Tooltip)
	{
		Menu.AddMenuEntry(Label, Tooltip, FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	}

	bool IsPicked(FString Key) const
	{
		return GraphPinObj && !GraphPinObj->WasTrashed() && GraphPinObj->DefaultValue == Key;
	}

	void Pick(FString Key)
	{
		const FScopedTransaction Transaction(bMachinePin ? LOCTEXT("PickMachineTransaction", "Pick State Machine") : LOCTEXT("PickInputTransaction", "Pick State Machine Input"));
		if (UK2Node_SetStateMachineInput* Node = GetNode())
		{
			Node->Modify();
		}
		WidgetAnimValuePin::SetPinDefault(GraphPinObj, Key);
		CachedAt = -1.0;
	}
};

TSharedPtr<SGraphPin> FWidgetAnimValuePinFactory::CreatePin(UEdGraphPin* Pin) const
{
	if (Pin && Pin->Direction == EGPD_Input && Cast<UK2Node_SetStateMachineInput>(Pin->GetOwningNodeUnchecked())
		&& (Pin->PinName == UK2Node_SetStateMachineInput::StateMachinePinName || Pin->PinName == UK2Node_SetStateMachineInput::InputPinName))
	{
		return SNew(SWidgetAnimMachinePin, Pin);
	}

	if (!Pin || Pin->Direction != EGPD_Input || !Cast<UK2Node_SetWidgetAnimationValue>(Pin->GetOwningNodeUnchecked()))
	{
		return nullptr;
	}

	if (Pin->PinName == UK2Node_SetWidgetAnimationValue::AnimationPinName || Pin->PinName == UK2Node_SetWidgetAnimationValue::ValueNamePinName)
	{
		return SNew(SWidgetAnimValuePin, Pin);
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
