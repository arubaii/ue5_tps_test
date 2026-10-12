// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimMachineConditionCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "NameTBDTooltips.h"
#include "PropertyHandle.h"
#include "SWidgetAnimStateMachineEditor.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimMachineConditionCustomization"

namespace
{
	/** What kind of value a condition's input holds - which comparisons it can make. */
	enum class EConditionInputKind : uint8
	{
		Unknown,
		Bool,
		Float,
		Integer,
		Trigger,
		Hold,
		Choice,
		Vector2D
	};

	EConditionInputKind GetInputKind(const UWidgetAnimStateMachineAsset* Machine, FName Input)
	{
		// The built-in inputs are true or false - or pulses (Clicked, the Events, the Gamepad buttons), as a Trigger is. A machine's
		// own input of the same name wins over a built-in, as at runtime.
		const FWidgetAnimMachineInput* Definition = Machine && !Input.IsNone() ? Machine->FindInput(Input) : nullptr;
		if (!Definition && WidgetAnimMachineInputs::IsBuiltIn(Input))
		{
			return WidgetAnimMachineInputs::IsBuiltInPulse(Input) ? EConditionInputKind::Trigger : EConditionInputKind::Bool;
		}
		if (!Definition)
		{
			return EConditionInputKind::Unknown;
		}
		switch (Definition->Type)
		{
		case EWidgetAnimMachineInputType::Number:   return EConditionInputKind::Float;
		case EWidgetAnimMachineInputType::Integer:  return EConditionInputKind::Integer;
		case EWidgetAnimMachineInputType::Trigger:  return EConditionInputKind::Trigger;
		case EWidgetAnimMachineInputType::Hold:     return EConditionInputKind::Hold;
		case EWidgetAnimMachineInputType::Choice:   return EConditionInputKind::Choice;
		case EWidgetAnimMachineInputType::Vector2D: return EConditionInputKind::Vector2D;
		default:                                     return EConditionInputKind::Bool;
		}
	}

	bool IsNumeric(EConditionInputKind Kind)
	{
		return Kind == EConditionInputKind::Float || Kind == EConditionInputKind::Integer || Kind == EConditionInputKind::Vector2D;
	}

	/** == != < <= > >=: what is compared against a value or another input. */
	bool IsRelational(EWidgetAnimMachineCompare Compare)
	{
		switch (Compare)
		{
		case EWidgetAnimMachineCompare::Equal:
		case EWidgetAnimMachineCompare::NotEqual:
		case EWidgetAnimMachineCompare::Less:
		case EWidgetAnimMachineCompare::LessOrEqual:
		case EWidgetAnimMachineCompare::Greater:
		case EWidgetAnimMachineCompare::GreaterOrEqual:
			return true;
		default:
			return false;
		}
	}

	/** The machine a condition belongs to: the State Machine editor's transition, or the asset's own Details. */
	const UWidgetAnimStateMachineAsset* FindMachine(const TSharedRef<IPropertyHandle>& Handle)
	{
		TArray<UObject*> Outers;
		Handle->GetOuterObjects(Outers);
		for (const UObject* Outer : Outers)
		{
			if (const UWidgetAnimMachineTransitionProxy* Proxy = Cast<UWidgetAnimMachineTransitionProxy>(Outer))
			{
				if (const UWidgetAnimStateMachineAsset* Machine = Proxy->Machine.Get())
				{
					return Machine;
				}
			}
			if (const UWidgetAnimStateMachineAsset* Asset = Cast<UWidgetAnimStateMachineAsset>(Outer))
			{
				return Asset;
			}
		}
		return nullptr;
	}

	/** The layer a condition's transition is in - INDEX_NONE on the asset's own Details, which don't say. */
	int32 FindOwnLayerIndex(const TSharedRef<IPropertyHandle>& Handle)
	{
		TArray<UObject*> Outers;
		Handle->GetOuterObjects(Outers);
		for (const UObject* Outer : Outers)
		{
			if (const UWidgetAnimMachineTransitionProxy* Proxy = Cast<UWidgetAnimMachineTransitionProxy>(Outer))
			{
				return Proxy->LayerIndex;
			}
		}
		return INDEX_NONE;
	}

	/** A name field's value, or None while several conditions with different values are shown at once. */
	FName GetName(const TSharedPtr<IPropertyHandle>& Handle)
	{
		FName Value;
		return Handle.IsValid() && Handle->GetValue(Value) == FPropertyAccess::Success ? Value : FName();
	}

	/** An enum field's value. False while several conditions with different values are shown at once. */
	template <typename TEnum>
	bool GetEnum(const TSharedPtr<IPropertyHandle>& Handle, TEnum& OutValue)
	{
		uint8 Value = 0;
		if (!Handle.IsValid() || Handle->GetValue(Value) != FPropertyAccess::Success)
		{
			return false;
		}
		OutValue = static_cast<TEnum>(Value);
		return true;
	}

	FText GetEnumText(const UEnum* Enum, int64 Value)
	{
		return Enum->GetDisplayNameTextByValue(Value);
	}

	/** What a comparison does, for its tooltip in the list - in the words of the kind of input it is on. */
	FText GetCompareTooltip(EConditionInputKind Kind, EWidgetAnimMachineCompare Compare)
	{
		if (Kind == EConditionInputKind::Hold)
		{
			switch (Compare)
			{
			case EWidgetAnimMachineCompare::IsTrue:
				return LOCTEXT("HeldTip", "While it is being held, until it has been held for its Hold Time: the way into the state that plays as it is held.");
			case EWidgetAnimMachineCompare::IsFalse:
				return LOCTEXT("LetGoTip", "The frame it is let go - once, as a Trigger fires, not for as long as nothing is pressed: the way back, before or after its Hold Time. Never the frame it completes.");
			case EWidgetAnimMachineCompare::Fired:
				return LOCTEXT("CompletedTip", "The frame it has been held for its whole Hold Time: the way on, the hold done.");
			default:
				break;
			}
		}
		if (Kind == EConditionInputKind::Choice)
		{
			switch (Compare)
			{
			case EWidgetAnimMachineCompare::Equal:    return LOCTEXT("IsTip", "The option it has is this one - or the one another Choice has.");
			case EWidgetAnimMachineCompare::NotEqual: return LOCTEXT("IsNotTip", "The option it has is any but this one.");
			case EWidgetAnimMachineCompare::Changed:  return LOCTEXT("ChoiceChangedTip", "The frame it switches to another option - whichever.");
			default: break;
			}
		}
		if (Kind == EConditionInputKind::Vector2D && Compare == EWidgetAnimMachineCompare::Changed)
		{
			return LOCTEXT("MovedTip", "The frame it moves at all.");
		}
		return StaticEnum<EWidgetAnimMachineCompare>()->GetToolTipTextByIndex(static_cast<int32>(Compare));
	}

	/** One input in the Input picker - picked, it is the condition's input. */
	void AddInputEntry(FMenuBuilder& Menu, const TSharedPtr<IPropertyHandle>& InputHandle, FName Input, const FText& Tooltip)
	{
		Menu.AddMenuEntry(FText::FromName(Input), Tooltip, FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([InputHandle, Input]() { InputHandle->SetValue(Input); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([InputHandle, Input]() { return GetName(InputHandle) == Input; })),
			NAME_None, EUserInterfaceActionType::RadioButton);
	}

	/** The built-in inputs of one group (and sub-group, for the Gamepad's) - those a machine's own input doesn't stand in for. */
	TArray<FName> GetBuiltInsIn(const UWidgetAnimStateMachineAsset* Machine, FName Group, FName SubGroup)
	{
		return WidgetAnimMachineInputs::GetBuiltIns().FilterByPredicate([Machine, Group, SubGroup](FName BuiltIn)
		{
			return WidgetAnimMachineInputs::GetBuiltInGroup(BuiltIn) == Group && WidgetAnimMachineInputs::GetBuiltInSubGroup(BuiltIn) == SubGroup
				&& !(Machine && Machine->FindInput(BuiltIn));
		});
	}

	/**
	 * The Input picker: the machine's own inputs, then the Widget Inputs as the Inputs panel lists them - the widget's states and
	 * Clicked, then Events and Gamepad (by its groups) in sub-menus of their own.
	 */
	void FillInputMenu(FMenuBuilder& Menu, const TSharedPtr<IPropertyHandle>& InputHandle, const UWidgetAnimStateMachineAsset* Machine)
	{
		Menu.BeginSection(NAME_None, LOCTEXT("MachineInputsSection", "Inputs"));
		if (!Machine || Machine->Inputs.Num() == 0)
		{
			Menu.AddMenuEntry(LOCTEXT("NoMachineInputs", "No inputs yet"), LOCTEXT("NoMachineInputsTip", "Add one in the Inputs panel."),
				FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
		}
		else
		{
			for (const FWidgetAnimMachineInput& Candidate : Machine->Inputs)
			{
				if (!Candidate.Name.IsNone())
				{
					const FText TypeName = StaticEnum<EWidgetAnimMachineInputType>()->GetDisplayNameTextByValue(static_cast<int64>(Candidate.Type));
					AddInputEntry(Menu, InputHandle, Candidate.Name, Candidate.Description.IsEmpty()
						? TypeName
						: FText::Format(LOCTEXT("MachineInputEntryTip", "{0}: {1}"), TypeName, FText::FromString(Candidate.Description)));
				}
			}
		}
		Menu.EndSection();

		static const FName EventsGroup(TEXT("Events"));
		static const FName GamepadGroup(TEXT("Gamepad"));
		Menu.BeginSection(NAME_None, LOCTEXT("WidgetInputsSection", "Widget Inputs"));
		for (const FName BuiltIn : GetBuiltInsIn(Machine, NAME_None, NAME_None))
		{
			AddInputEntry(Menu, InputHandle, BuiltIn, WidgetAnimMachineInputs::GetBuiltInDescription(BuiltIn));
		}
		const TWeakObjectPtr<const UWidgetAnimStateMachineAsset> WeakMachine(Machine);
		Menu.AddSubMenu(LOCTEXT("EventsSubMenu", "Events"), NameTBD::Tooltips::StateMachineEditor::BuiltInEventsGroupTooltip(),
			FNewMenuDelegate::CreateLambda([InputHandle, WeakMachine](FMenuBuilder& SubMenu)
			{
				for (const FName BuiltIn : GetBuiltInsIn(WeakMachine.Get(), EventsGroup, NAME_None))
				{
					AddInputEntry(SubMenu, InputHandle, BuiltIn, WidgetAnimMachineInputs::GetBuiltInDescription(BuiltIn));
				}
			}));
		Menu.AddSubMenu(LOCTEXT("GamepadSubMenu", "Gamepad"), NameTBD::Tooltips::StateMachineEditor::BuiltInGamepadGroupTooltip(),
			FNewMenuDelegate::CreateLambda([InputHandle, WeakMachine](FMenuBuilder& SubMenu)
			{
				// A section for each kind of button, in the order they are listed.
				TArray<FName> SubGroups;
				for (const FName BuiltIn : WidgetAnimMachineInputs::GetBuiltIns())
				{
					if (WidgetAnimMachineInputs::GetBuiltInGroup(BuiltIn) == GamepadGroup)
					{
						SubGroups.AddUnique(WidgetAnimMachineInputs::GetBuiltInSubGroup(BuiltIn));
					}
				}
				for (const FName SubGroup : SubGroups)
				{
					SubMenu.BeginSection(NAME_None, FText::FromName(SubGroup));
					for (const FName BuiltIn : GetBuiltInsIn(WeakMachine.Get(), GamepadGroup, SubGroup))
					{
						AddInputEntry(SubMenu, InputHandle, BuiltIn, WidgetAnimMachineInputs::GetBuiltInDescription(BuiltIn));
					}
					SubMenu.EndSection();
				}
			}));
		Menu.EndSection();
	}

	/** A number as a condition shows it: whole for an Integer. */
	FText FormatValue(EConditionInputKind Kind, float Value)
	{
		return Kind == EConditionInputKind::Integer ? FText::AsNumber(FMath::RoundToInt32(Value)) : FText::AsNumber(Value);
	}
}

TSharedRef<IPropertyTypeCustomization> FWidgetAnimMachineConditionCustomization::MakeInstance()
{
	return MakeShared<FWidgetAnimMachineConditionCustomization>();
}

TArray<EWidgetAnimMachineCompare> FWidgetAnimMachineConditionCustomization::GetCompares(const UWidgetAnimStateMachineAsset* Machine, FName Input)
{
	using C = EWidgetAnimMachineCompare;
	switch (GetInputKind(Machine, Input))
	{
	case EConditionInputKind::Bool:     return { C::IsTrue, C::IsFalse };
	case EConditionInputKind::Trigger:  return { C::Fired };
	case EConditionInputKind::Hold:     return { C::IsTrue, C::IsFalse, C::Fired };
	case EConditionInputKind::Float:
	case EConditionInputKind::Integer:  return { C::Equal, C::NotEqual, C::Less, C::LessOrEqual, C::Greater, C::GreaterOrEqual, C::Increased, C::Decreased, C::Changed };
	case EConditionInputKind::Choice:   return { C::Equal, C::NotEqual, C::Changed };
	case EConditionInputKind::Vector2D: return { C::Points, C::Equal, C::NotEqual, C::Less, C::LessOrEqual, C::Greater, C::GreaterOrEqual, C::Increased, C::Decreased, C::Changed };
	default:
	{
		TArray<C> All;
		const UEnum* Enum = StaticEnum<C>();
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			All.Add(static_cast<C>(Enum->GetValueByIndex(Index)));
		}
		return All;
	}
	}
}

FText FWidgetAnimMachineConditionCustomization::GetCompareLabel(const UWidgetAnimStateMachineAsset* Machine, FName Input, EWidgetAnimMachineCompare Compare)
{
	const EConditionInputKind Kind = GetInputKind(Machine, Input);
	if (Kind == EConditionInputKind::Hold)
	{
		switch (Compare)
		{
		case EWidgetAnimMachineCompare::IsTrue:  return LOCTEXT("IsHeld", "Is Held");
		case EWidgetAnimMachineCompare::IsFalse: return LOCTEXT("IsLetGo", "Is Let Go");
		case EWidgetAnimMachineCompare::Fired:   return LOCTEXT("Completed", "Completed");
		default: break;
		}
	}
	if (Kind == EConditionInputKind::Choice)
	{
		switch (Compare)
		{
		case EWidgetAnimMachineCompare::Equal:    return LOCTEXT("ChoiceIs", "Is");
		case EWidgetAnimMachineCompare::NotEqual: return LOCTEXT("ChoiceIsNot", "Is Not");
		default: break;
		}
	}
	if (Kind == EConditionInputKind::Vector2D && Compare == EWidgetAnimMachineCompare::Changed)
	{
		return LOCTEXT("Moved", "Moved");
	}
	return GetEnumText(StaticEnum<EWidgetAnimMachineCompare>(), static_cast<int64>(Compare));
}

bool FWidgetAnimMachineConditionCustomization::CanCompareInputs(const UWidgetAnimStateMachineAsset* Machine, FName Input, FName Other)
{
	if (Other.IsNone() || Other == Input)
	{
		return false;
	}
	const EConditionInputKind Mine = GetInputKind(Machine, Input);
	const EConditionInputKind Theirs = GetInputKind(Machine, Other);
	if (Mine == EConditionInputKind::Choice)
	{
		return Theirs == EConditionInputKind::Choice;
	}
	return IsNumeric(Mine) && IsNumeric(Theirs);
}

FText FWidgetAnimMachineConditionCustomization::Describe(const UWidgetAnimStateMachineAsset* Machine, const FWidgetAnimMachineCondition& Condition)
{
	using C = EWidgetAnimMachineCompare;

	// Another layer's state: "Press is Down", "Badge entered Shown".
	if (Condition.TestsLayerState())
	{
		const FText LayerText = Condition.Layer.IsNone() ? LOCTEXT("NoLayer", "(no layer)") : FText::FromName(Condition.Layer);
		const FText StateText = Condition.State.IsNone() ? LOCTEXT("NoState", "(no state)") : FText::FromName(Condition.State);
		switch (Condition.LayerTest)
		{
		case EWidgetAnimMachineLayerStateTest::IsNotIn: return FText::Format(LOCTEXT("CondLayerNotIn", "{0} isn't {1}"), LayerText, StateText);
		case EWidgetAnimMachineLayerStateTest::Entered: return FText::Format(LOCTEXT("CondLayerEntered", "{0} entered {1}"), LayerText, StateText);
		case EWidgetAnimMachineLayerStateTest::Exited:  return FText::Format(LOCTEXT("CondLayerExited", "{0} left {1}"), LayerText, StateText);
		default:                                         return FText::Format(LOCTEXT("CondLayerIn", "{0} is {1}"), LayerText, StateText);
		}
	}

	const FText Name = Condition.Input.IsNone() ? LOCTEXT("NoInput", "(no input)") : FText::FromName(Condition.Input);
	const EConditionInputKind Kind = GetInputKind(Machine, Condition.Input);
	const bool bAgainstInput = Condition.CompareTo == EWidgetAnimMachineCompareTo::Input;
	const UEnum* Parts = StaticEnum<EWidgetAnimMachineVectorPart>();

	// The left side: a Vector 2D's part, named with it - "Stick length".
	const FText Left = Kind == EConditionInputKind::Vector2D
		? FText::Format(LOCTEXT("VectorSide", "{0} {1}"), Name, GetEnumText(Parts, static_cast<int64>(Condition.Part)).ToLower())
		: Name;

	// The right side: the value or option typed, or the other input (its part, for a Vector 2D).
	auto Right = [&]() -> FText
	{
		if (bAgainstInput)
		{
			const FText Other = Condition.OtherInput.IsNone() ? LOCTEXT("NoOtherInput", "(no input)") : FText::FromName(Condition.OtherInput);
			return GetInputKind(Machine, Condition.OtherInput) == EConditionInputKind::Vector2D
				? FText::Format(LOCTEXT("OtherVectorSide", "{0} {1}"), Other, GetEnumText(Parts, static_cast<int64>(Condition.OtherPart)).ToLower())
				: Other;
		}
		if (Kind == EConditionInputKind::Choice)
		{
			return Condition.Option.IsNone() ? LOCTEXT("NoOption", "(no option)") : FText::FromName(Condition.Option);
		}
		return FormatValue(Kind, Condition.Value);
	};

	switch (Kind)
	{
	case EConditionInputKind::Hold:
		switch (Condition.Compare)
		{
		case C::IsTrue:  return FText::Format(LOCTEXT("CondHolding", "{0} held"), Name);
		case C::IsFalse: return FText::Format(LOCTEXT("CondLetGo", "{0} let go"), Name);
		case C::Fired:
		{
			const FWidgetAnimMachineInput* Hold = Machine ? Machine->FindInput(Condition.Input) : nullptr;
			return Hold
				? FText::Format(LOCTEXT("CondHoldDone", "{0} held {1}s"), Name, FText::AsNumber(Hold->HoldTime))
				: FText::Format(LOCTEXT("CondHoldDoneNoTime", "{0} held"), Name);
		}
		default: break;
		}
		break;

	case EConditionInputKind::Choice:
		switch (Condition.Compare)
		{
		case C::Equal:    return FText::Format(LOCTEXT("CondChoiceIs", "{0} is {1}"), Name, Right());
		case C::NotEqual: return FText::Format(LOCTEXT("CondChoiceIsNot", "{0} isn't {1}"), Name, Right());
		case C::Changed:  return FText::Format(LOCTEXT("CondChoiceChanged", "{0} changed"), Name);
		default: break;
		}
		break;

	case EConditionInputKind::Vector2D:
		if (Condition.Compare == C::Points)
		{
			return FText::Format(LOCTEXT("CondPoints", "{0} points {1}"), Name, GetEnumText(StaticEnum<EWidgetAnimMachineDirection>(), static_cast<int64>(Condition.Direction)));
		}
		if (Condition.Compare == C::Changed)
		{
			return FText::Format(LOCTEXT("CondMoved", "{0} moved"), Name);
		}
		break;

	default:
		break;
	}

	switch (Condition.Compare)
	{
	case C::IsTrue:    return FText::Format(LOCTEXT("CondTrue", "{0}"), Name);
	case C::IsFalse:   return FText::Format(LOCTEXT("CondFalse", "not {0}"), Name);
	case C::Fired:     return FText::Format(LOCTEXT("CondFired", "{0} fired"), Name);
	case C::Increased: return FText::Format(LOCTEXT("CondIncreased", "{0} went up"), Left);
	case C::Decreased: return FText::Format(LOCTEXT("CondDecreased", "{0} went down"), Left);
	case C::Changed:   return FText::Format(LOCTEXT("CondChanged", "{0} changed"), Left);
	case C::Points:    return FText::Format(LOCTEXT("CondPointsUnknown", "{0} points"), Left);
	default:
		return FText::Format(LOCTEXT("CondCompare", "{0} {1} {2}"), Left, GetEnumText(StaticEnum<C>(), static_cast<int64>(Condition.Compare)), Right());
	}
}

void FWidgetAnimMachineConditionCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// The name, and the condition in a few words beside it - "Hold held", "Charge >= 0.5", "Rarity is Epic" - so a folded list reads.
	const TWeakObjectPtr<const UWidgetAnimStateMachineAsset> WeakMachine(FindMachine(PropertyHandle));
	const TSharedRef<IPropertyHandle> Handle = PropertyHandle;

	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(200.f)
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Text_Lambda([Handle, WeakMachine]()
		{
			TArray<void*> RawData;
			Handle->AccessRawData(RawData);
			if (RawData.Num() != 1 || !RawData[0])
			{
				return FText::GetEmpty();
			}
			const FWidgetAnimMachineCondition& Condition = *static_cast<const FWidgetAnimMachineCondition*>(RawData[0]);
			return Condition.Input.IsNone() && !Condition.TestsLayerState() ? FText::GetEmpty() : Describe(WeakMachine.Get(), Condition);
		})
	];
}

void FWidgetAnimMachineConditionCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	auto Child = [&PropertyHandle](FName Name) { return PropertyHandle->GetChildHandle(Name); };
	const TSharedPtr<IPropertyHandle> InputHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Input));
	const TSharedPtr<IPropertyHandle> CompareHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Compare));
	const TSharedPtr<IPropertyHandle> PartHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Part));
	const TSharedPtr<IPropertyHandle> DirectionHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Direction));
	const TSharedPtr<IPropertyHandle> DeadZoneHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, DeadZone));
	const TSharedPtr<IPropertyHandle> CompareToHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, CompareTo));
	const TSharedPtr<IPropertyHandle> ValueHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Value));
	const TSharedPtr<IPropertyHandle> OptionHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Option));
	const TSharedPtr<IPropertyHandle> OtherHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, OtherInput));
	const TSharedPtr<IPropertyHandle> OtherPartHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, OtherPart));
	const TSharedPtr<IPropertyHandle> SourceHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Source));
	const TSharedPtr<IPropertyHandle> LayerHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Layer));
	const TSharedPtr<IPropertyHandle> LayerTestHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, LayerTest));
	const TSharedPtr<IPropertyHandle> StateHandle = Child(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, State));
	if (!InputHandle.IsValid() || !CompareHandle.IsValid() || !CompareToHandle.IsValid() || !OptionHandle.IsValid() || !OtherHandle.IsValid()
		|| !SourceHandle.IsValid() || !LayerHandle.IsValid() || !LayerTestHandle.IsValid() || !StateHandle.IsValid())
	{
		return;
	}
	const TWeakObjectPtr<const UWidgetAnimStateMachineAsset> WeakMachine(FindMachine(PropertyHandle));
	const int32 OwnLayer = FindOwnLayerIndex(PropertyHandle);

	// Test: an input, or another layer's state. Everything below the input rows is shown only for an input.
	auto TestsLayer = [SourceHandle]()
	{
		EWidgetAnimMachineConditionSource Value = EWidgetAnimMachineConditionSource::Input;
		GetEnum(SourceHandle, Value);
		return Value == EWidgetAnimMachineConditionSource::LayerState;
	};
	ChildBuilder.AddProperty(SourceHandle.ToSharedRef());

	// What each row is shown for - worked out each time it is drawn, so a row appears the moment what it depends on is picked.
	auto Kind = [InputHandle, WeakMachine]() { return GetInputKind(WeakMachine.Get(), GetName(InputHandle)); };
	auto Compare = [CompareHandle]()
	{
		EWidgetAnimMachineCompare Value = EWidgetAnimMachineCompare::IsTrue;
		GetEnum(CompareHandle, Value);
		return Value;
	};
	auto AgainstInput = [CompareToHandle]()
	{
		EWidgetAnimMachineCompareTo Value = EWidgetAnimMachineCompareTo::Value;
		GetEnum(CompareToHandle, Value);
		return Value == EWidgetAnimMachineCompareTo::Input;
	};
	// Compared against something: a number's relations, or a Choice's Is / Is Not.
	auto Comparative = [Kind, Compare]()
	{
		const EConditionInputKind K = Kind();
		const EWidgetAnimMachineCompare C = Compare();
		return K == EConditionInputKind::Choice ? (C == EWidgetAnimMachineCompare::Equal || C == EWidgetAnimMachineCompare::NotEqual)
			: (IsNumeric(K) || K == EConditionInputKind::Unknown) && IsRelational(C);
	};
	// Rows for an input are hidden while the condition tests another layer's state.
	auto Show = [TestsLayer](bool bShown) { return bShown && !TestsLayer() ? EVisibility::Visible : EVisibility::Collapsed; };
	auto ShowForLayer = [TestsLayer]() { return TestsLayer() ? EVisibility::Visible : EVisibility::Collapsed; };

	// Another layer's state: the layer (any but the transition's own), what is asked, and the state - the picked layer's.
	auto MakeNameCombo = [](TFunction<FText()> GetLabel, TFunction<void(FMenuBuilder&)> FillMenu) -> TSharedRef<SWidget>
	{
		return SNew(SComboButton)
			.OnGetMenuContent_Lambda([FillMenu]()
			{
				FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
				FillMenu(Menu);
				return Menu.MakeWidget();
			})
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text_Lambda([GetLabel]() { return GetLabel(); })
			];
	};
	IDetailPropertyRow& LayerRow = ChildBuilder.AddProperty(LayerHandle.ToSharedRef());
	LayerRow.Visibility(TAttribute<EVisibility>::CreateLambda(ShowForLayer));
	LayerRow.CustomWidget()
	.NameContent()
	[
		LayerHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(140.f)
	[
		MakeNameCombo(
			[LayerHandle]()
			{
				const FName LayerName = GetName(LayerHandle);
				return LayerName.IsNone() ? LOCTEXT("PickLayer", "Pick a layer") : FText::FromName(LayerName);
			},
			[LayerHandle, StateHandle, WeakMachine, OwnLayer](FMenuBuilder& Menu)
			{
				const UWidgetAnimStateMachineAsset* Machine = WeakMachine.Get();
				bool bAny = false;
				for (int32 Index = 0; Machine && Index < Machine->Layers.Num(); ++Index)
				{
					if (Index == OwnLayer)
					{
						continue;
					}
					bAny = true;
					const FName LayerName = Machine->Layers[Index].Name;
					Menu.AddMenuEntry(FText::FromName(LayerName), FText::GetEmpty(), FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([LayerHandle, StateHandle, LayerName]()
							{
								if (GetName(LayerHandle) != LayerName)
								{
									LayerHandle->SetValue(LayerName);
									StateHandle->SetValue(FName());
								}
							}),
							FCanExecuteAction(),
							FIsActionChecked::CreateLambda([LayerHandle, LayerName]() { return GetName(LayerHandle) == LayerName; })),
						NAME_None, EUserInterfaceActionType::RadioButton);
				}
				if (!bAny)
				{
					Menu.AddMenuEntry(LOCTEXT("NoOtherLayers", "The machine has no other layer"), LOCTEXT("NoOtherLayersTip", "Add a layer with + beside the layer tabs."),
						FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
				}
			})
	];
	ChildBuilder.AddProperty(LayerTestHandle.ToSharedRef()).Visibility(TAttribute<EVisibility>::CreateLambda(ShowForLayer));
	IDetailPropertyRow& StateRow = ChildBuilder.AddProperty(StateHandle.ToSharedRef());
	StateRow.Visibility(TAttribute<EVisibility>::CreateLambda(ShowForLayer));
	StateRow.CustomWidget()
	.NameContent()
	[
		StateHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(140.f)
	[
		MakeNameCombo(
			[StateHandle]()
			{
				const FName StateName = GetName(StateHandle);
				return StateName.IsNone() ? LOCTEXT("PickState", "Pick a state") : FText::FromName(StateName);
			},
			[LayerHandle, StateHandle, WeakMachine](FMenuBuilder& Menu)
			{
				const UWidgetAnimStateMachineAsset* Machine = WeakMachine.Get();
				const int32 LayerIndex = Machine ? Machine->FindLayerIndex(GetName(LayerHandle)) : INDEX_NONE;
				if (!Machine || !Machine->Layers.IsValidIndex(LayerIndex))
				{
					Menu.AddMenuEntry(LOCTEXT("PickLayerFirst", "Pick the layer first"), FText::GetEmpty(),
						FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
					return;
				}
				for (const FWidgetAnimMachineState& Candidate : Machine->Layers[LayerIndex].States)
				{
					// States inside sub-machines too; the layer's own Exit, never a sub-machine's.
					if (Candidate.Kind != EWidgetAnimMachineNodeKind::State && (Candidate.Kind != EWidgetAnimMachineNodeKind::Exit || Candidate.ParentMachine.IsValid()))
					{
						continue;
					}
					const FName StateName = Candidate.Name;
					Menu.AddMenuEntry(FText::FromName(StateName), FText::GetEmpty(), FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([StateHandle, StateName]() { StateHandle->SetValue(StateName); }),
							FCanExecuteAction(),
							FIsActionChecked::CreateLambda([StateHandle, StateName]() { return GetName(StateHandle) == StateName; })),
						NAME_None, EUserInterfaceActionType::RadioButton);
				}
			})
	];

	// Input: the machine's inputs, then the built-in ones - Events and Gamepad in sub-menus, as the Inputs panel folds them. Picked,
	// Compare is put right for it: the one it had, if it still applies, else the first that does.
	IDetailPropertyRow& InputRow = ChildBuilder.AddProperty(InputHandle.ToSharedRef());
	InputRow.Visibility(TAttribute<EVisibility>::CreateLambda([Show]() { return Show(true); }));
	InputRow.CustomWidget()
	.NameContent()
	[
		InputHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(140.f)
	[
		MakeNameCombo(
			[InputHandle]()
			{
				FName Picked;
				if (InputHandle->GetValue(Picked) == FPropertyAccess::MultipleValues)
				{
					return LOCTEXT("MultipleInputs", "Multiple Values");
				}
				return Picked.IsNone() ? LOCTEXT("PickInput", "Pick an input") : FText::FromName(Picked);
			},
			[InputHandle, WeakMachine](FMenuBuilder& Menu)
			{
				FillInputMenu(Menu, InputHandle, WeakMachine.Get());
			})
	];
	const TWeakPtr<IPropertyHandle> WeakInput = InputHandle;
	const TWeakPtr<IPropertyHandle> WeakCompare = CompareHandle;
	InputHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakInput, WeakCompare, WeakMachine]()
	{
		const TSharedPtr<IPropertyHandle> PinnedInput = WeakInput.Pin();
		const TSharedPtr<IPropertyHandle> PinnedCompare = WeakCompare.Pin();
		const FName InputName = GetName(PinnedInput);
		EWidgetAnimMachineCompare Current = EWidgetAnimMachineCompare::IsTrue;
		if (!PinnedCompare.IsValid() || InputName.IsNone() || !GetEnum(PinnedCompare, Current))
		{
			return;
		}
		const TArray<EWidgetAnimMachineCompare> Allowed = GetCompares(WeakMachine.Get(), InputName);
		if (Allowed.Num() > 0 && !Allowed.Contains(Current))
		{
			PinnedCompare->SetValue(static_cast<uint8>(Allowed[0]));
		}
	}));

	// A combo of named entries, for the rows laid out by hand below.
	auto MakeCombo = [](TFunction<FText()> GetLabel, TFunction<void(FMenuBuilder&)> FillMenu) -> TSharedRef<SWidget>
	{
		return SNew(SComboButton)
			.OnGetMenuContent_Lambda([FillMenu]()
			{
				FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
				FillMenu(Menu);
				return Menu.MakeWidget();
			})
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text_Lambda([GetLabel]() { return GetLabel(); })
			];
	};
	auto AddRadio = [](FMenuBuilder& Menu, const FText& Label, const FText& Tooltip, TFunction<void()> Pick, TFunction<bool()> IsPicked)
	{
		Menu.AddMenuEntry(Label, Tooltip, FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Pick]() { Pick(); }), FCanExecuteAction(), FIsActionChecked::CreateLambda([IsPicked]() { return IsPicked(); })),
			NAME_None, EUserInterfaceActionType::RadioButton);
	};

	// Part: which number of a Vector 2D is compared - before the comparison, as it reads: "Stick Length > 0.5".
	if (PartHandle.IsValid())
	{
		ChildBuilder.AddProperty(PartHandle.ToSharedRef())
			.Visibility(TAttribute<EVisibility>::CreateLambda([Kind, Compare, Show]()
			{
				const EWidgetAnimMachineCompare C = Compare();
				return Show(Kind() == EConditionInputKind::Vector2D && C != EWidgetAnimMachineCompare::Points && C != EWidgetAnimMachineCompare::Changed);
			}));
	}

	// Compare: only what the input can be asked, in its words.
	IDetailPropertyRow& CompareRow = ChildBuilder.AddProperty(CompareHandle.ToSharedRef());
	CompareRow.Visibility(TAttribute<EVisibility>::CreateLambda([Show]() { return Show(true); }));
	CompareRow.CustomWidget()
	.NameContent()
	[
		CompareHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(140.f)
	[
		MakeCombo(
			[InputHandle, CompareHandle, WeakMachine]()
			{
				EWidgetAnimMachineCompare Current = EWidgetAnimMachineCompare::IsTrue;
				return GetEnum(CompareHandle, Current) ? GetCompareLabel(WeakMachine.Get(), GetName(InputHandle), Current) : LOCTEXT("MultipleValues", "Multiple Values");
			},
			[InputHandle, CompareHandle, WeakMachine, AddRadio](FMenuBuilder& Menu)
			{
				const FName Input = GetName(InputHandle);
				const EConditionInputKind InputKind = GetInputKind(WeakMachine.Get(), Input);
				for (const EWidgetAnimMachineCompare Option : GetCompares(WeakMachine.Get(), Input))
				{
					AddRadio(Menu, GetCompareLabel(WeakMachine.Get(), Input, Option), GetCompareTooltip(InputKind, Option),
						[CompareHandle, Option]() { CompareHandle->SetValue(static_cast<uint8>(Option)); },
						[CompareHandle, Option]()
						{
							EWidgetAnimMachineCompare Current = EWidgetAnimMachineCompare::IsTrue;
							return GetEnum(CompareHandle, Current) && Current == Option;
						});
				}
			})
	];

	// Points: which way, and past how far.
	for (const TSharedPtr<IPropertyHandle>& PointsHandle : { DirectionHandle, DeadZoneHandle })
	{
		if (PointsHandle.IsValid())
		{
			ChildBuilder.AddProperty(PointsHandle.ToSharedRef())
				.Visibility(TAttribute<EVisibility>::CreateLambda([Kind, Compare, Show]()
				{
					return Show(Kind() == EConditionInputKind::Vector2D && Compare() == EWidgetAnimMachineCompare::Points);
				}));
		}
	}

	// Compare To: a value typed here, or another input.
	ChildBuilder.AddProperty(CompareToHandle.ToSharedRef())
		.Visibility(TAttribute<EVisibility>::CreateLambda([Comparative, Show]() { return Show(Comparative()); }));

	// Value: a number typed here.
	if (ValueHandle.IsValid())
	{
		ChildBuilder.AddProperty(ValueHandle.ToSharedRef())
			.Visibility(TAttribute<EVisibility>::CreateLambda([Kind, Comparative, AgainstInput, Show]()
			{
				return Show(Comparative() && !AgainstInput() && Kind() != EConditionInputKind::Choice);
			}));
	}

	// Option: one of a Choice's own options.
	IDetailPropertyRow& OptionRow = ChildBuilder.AddProperty(OptionHandle.ToSharedRef());
	OptionRow.Visibility(TAttribute<EVisibility>::CreateLambda([Kind, Comparative, AgainstInput, Show]()
	{
		return Show(Kind() == EConditionInputKind::Choice && Comparative() && !AgainstInput());
	}));
	OptionRow.CustomWidget()
	.NameContent()
	[
		OptionHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(140.f)
	[
		MakeCombo(
			[OptionHandle]()
			{
				const FName Option = GetName(OptionHandle);
				return Option.IsNone() ? LOCTEXT("PickOption", "Pick an option") : FText::FromName(Option);
			},
			[InputHandle, OptionHandle, WeakMachine, AddRadio](FMenuBuilder& Menu)
			{
				const UWidgetAnimStateMachineAsset* Machine = WeakMachine.Get();
				const FWidgetAnimMachineInput* Input = Machine ? Machine->FindInput(GetName(InputHandle)) : nullptr;
				if (!Input || Input->Options.Num() == 0)
				{
					Menu.AddMenuEntry(LOCTEXT("NoOptions", "The Choice has no options"), LOCTEXT("NoOptionsTip", "Give the input its Options in its Details."),
						FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
					return;
				}
				for (const FName Option : Input->Options)
				{
					AddRadio(Menu, FText::FromName(Option), FText::GetEmpty(),
						[OptionHandle, Option]() { OptionHandle->SetValue(Option); },
						[OptionHandle, Option]() { return GetName(OptionHandle) == Option; });
				}
			})
	];

	// Other Input: one it can be compared with - a number with a number, a Choice with a Choice.
	IDetailPropertyRow& OtherRow = ChildBuilder.AddProperty(OtherHandle.ToSharedRef());
	OtherRow.Visibility(TAttribute<EVisibility>::CreateLambda([Comparative, AgainstInput, Show]() { return Show(Comparative() && AgainstInput()); }));
	OtherRow.CustomWidget()
	.NameContent()
	[
		OtherHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(140.f)
	[
		MakeCombo(
			[OtherHandle]()
			{
				const FName Other = GetName(OtherHandle);
				return Other.IsNone() ? LOCTEXT("PickOtherInput", "Pick an input") : FText::FromName(Other);
			},
			[InputHandle, OtherHandle, WeakMachine, AddRadio](FMenuBuilder& Menu)
			{
				const UWidgetAnimStateMachineAsset* Machine = WeakMachine.Get();
				const FName Input = GetName(InputHandle);
				bool bAny = false;
				if (Machine)
				{
					for (const FWidgetAnimMachineInput& Candidate : Machine->Inputs)
					{
						if (!CanCompareInputs(Machine, Input, Candidate.Name))
						{
							continue;
						}
						bAny = true;
						const FName Other = Candidate.Name;
						AddRadio(Menu,
							FText::Format(LOCTEXT("OtherInputEntry", "{0}  ({1})"), FText::FromName(Other), GetEnumText(StaticEnum<EWidgetAnimMachineInputType>(), static_cast<int64>(Candidate.Type))),
							FText::FromString(Candidate.Description),
							[OtherHandle, Other]() { OtherHandle->SetValue(Other); },
							[OtherHandle, Other]() { return GetName(OtherHandle) == Other; });
					}
				}
				if (!bAny)
				{
					Menu.AddMenuEntry(LOCTEXT("NoOtherInputs", "No input to compare with"),
						LOCTEXT("NoOtherInputsTip", "A number compares with another Float, Integer or Vector 2D; a Choice with another Choice. Add one in the Inputs panel."),
						FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
				}
			})
	];

	// Other Part: which number of the other input, when it is a Vector 2D.
	if (OtherPartHandle.IsValid())
	{
		ChildBuilder.AddProperty(OtherPartHandle.ToSharedRef())
			.Visibility(TAttribute<EVisibility>::CreateLambda([Comparative, AgainstInput, OtherHandle, WeakMachine, Show]()
			{
				return Show(Comparative() && AgainstInput() && GetInputKind(WeakMachine.Get(), GetName(OtherHandle)) == EConditionInputKind::Vector2D);
			}));
	}
}

#undef LOCTEXT_NAMESPACE
