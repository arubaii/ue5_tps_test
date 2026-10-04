// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimMachineConditionCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
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
		Number,
		Trigger,
		Hold
	};

	EConditionInputKind GetInputKind(const UWidgetAnimStateMachineAsset* Machine, FName Input)
	{
		// The built-in inputs - Hovered, Pressed, Closing and the rest - are all true or false.
		if (WidgetAnimMachineInputs::IsBuiltIn(Input))
		{
			return EConditionInputKind::Bool;
		}
		const FWidgetAnimMachineInput* Definition = Machine && !Input.IsNone() ? Machine->FindInput(Input) : nullptr;
		if (!Definition)
		{
			return EConditionInputKind::Unknown;
		}
		switch (Definition->Type)
		{
		case EWidgetAnimMachineInputType::Number:  return EConditionInputKind::Number;
		case EWidgetAnimMachineInputType::Trigger: return EConditionInputKind::Trigger;
		case EWidgetAnimMachineInputType::Hold:    return EConditionInputKind::Hold;
		default:                                    return EConditionInputKind::Bool;
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

	/** The input a condition tests, or None while several conditions with different inputs are shown at once. */
	FName GetInput(const TSharedPtr<IPropertyHandle>& InputHandle)
	{
		FName Input;
		return InputHandle.IsValid() && InputHandle->GetValue(Input) == FPropertyAccess::Success ? Input : FName();
	}

	bool GetCompare(const TSharedPtr<IPropertyHandle>& CompareHandle, EWidgetAnimMachineCompare& OutCompare)
	{
		uint8 Value = 0;
		if (!CompareHandle.IsValid() || CompareHandle->GetValue(Value) != FPropertyAccess::Success)
		{
			return false;
		}
		OutCompare = static_cast<EWidgetAnimMachineCompare>(Value);
		return true;
	}

	/** What a comparison does, for its tooltip in the list - on a Press And Hold, in its own words. */
	FText GetCompareTooltip(EConditionInputKind Kind, EWidgetAnimMachineCompare Compare)
	{
		if (Kind == EConditionInputKind::Hold)
		{
			switch (Compare)
			{
			case EWidgetAnimMachineCompare::IsTrue:
				return LOCTEXT("HeldTip", "While it is being held, until it has been held for its Hold Time: the way into the state that plays as it is held.");
			case EWidgetAnimMachineCompare::IsFalse:
				return LOCTEXT("LetGoTip", "Once it is let go before its Hold Time - or isn't held at all: the way back, the hold not completed. Never the frame it completes.");
			case EWidgetAnimMachineCompare::Fired:
				return LOCTEXT("CompletedTip", "The frame it has been held for its whole Hold Time: the way on, the hold done.");
			default:
				break;
			}
		}
		return StaticEnum<EWidgetAnimMachineCompare>()->GetToolTipTextByIndex(static_cast<int32>(Compare));
	}
}

TSharedRef<IPropertyTypeCustomization> FWidgetAnimMachineConditionCustomization::MakeInstance()
{
	return MakeShared<FWidgetAnimMachineConditionCustomization>();
}

TArray<EWidgetAnimMachineCompare> FWidgetAnimMachineConditionCustomization::GetCompares(const UWidgetAnimStateMachineAsset* Machine, FName Input)
{
	switch (GetInputKind(Machine, Input))
	{
	case EConditionInputKind::Bool:
		return { EWidgetAnimMachineCompare::IsTrue, EWidgetAnimMachineCompare::IsFalse };
	case EConditionInputKind::Trigger:
		return { EWidgetAnimMachineCompare::Fired };
	case EConditionInputKind::Hold:
		return { EWidgetAnimMachineCompare::IsTrue, EWidgetAnimMachineCompare::IsFalse, EWidgetAnimMachineCompare::Fired };
	case EConditionInputKind::Number:
		return { EWidgetAnimMachineCompare::Equal, EWidgetAnimMachineCompare::NotEqual, EWidgetAnimMachineCompare::Less,
			EWidgetAnimMachineCompare::LessOrEqual, EWidgetAnimMachineCompare::Greater, EWidgetAnimMachineCompare::GreaterOrEqual };
	default:
	{
		TArray<EWidgetAnimMachineCompare> All;
		const UEnum* Enum = StaticEnum<EWidgetAnimMachineCompare>();
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			All.Add(static_cast<EWidgetAnimMachineCompare>(Enum->GetValueByIndex(Index)));
		}
		return All;
	}
	}
}

FText FWidgetAnimMachineConditionCustomization::GetCompareLabel(const UWidgetAnimStateMachineAsset* Machine, FName Input, EWidgetAnimMachineCompare Compare)
{
	if (GetInputKind(Machine, Input) == EConditionInputKind::Hold)
	{
		switch (Compare)
		{
		case EWidgetAnimMachineCompare::IsTrue:  return LOCTEXT("IsHeld", "Is Held");
		case EWidgetAnimMachineCompare::IsFalse: return LOCTEXT("IsLetGo", "Is Let Go");
		case EWidgetAnimMachineCompare::Fired:   return LOCTEXT("Completed", "Completed");
		default: break;
		}
	}
	return StaticEnum<EWidgetAnimMachineCompare>()->GetDisplayNameTextByValue(static_cast<int64>(Compare));
}

void FWidgetAnimMachineConditionCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// The name, and the condition in a few words beside it - "Hold Is Held", "Charge >= 0.5" - so a folded list reads.
	const TSharedPtr<IPropertyHandle> InputHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Input));
	const TSharedPtr<IPropertyHandle> CompareHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Compare));
	const TSharedPtr<IPropertyHandle> ValueHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Value));
	const TWeakObjectPtr<const UWidgetAnimStateMachineAsset> WeakMachine(FindMachine(PropertyHandle));

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
		.Text_Lambda([InputHandle, CompareHandle, ValueHandle, WeakMachine]()
		{
			const FName Input = GetInput(InputHandle);
			EWidgetAnimMachineCompare Compare = EWidgetAnimMachineCompare::IsTrue;
			if (Input.IsNone() || !GetCompare(CompareHandle, Compare))
			{
				return FText::GetEmpty();
			}
			const FText Label = GetCompareLabel(WeakMachine.Get(), Input, Compare);
			if (GetInputKind(WeakMachine.Get(), Input) == EConditionInputKind::Number)
			{
				float Value = 0.f;
				if (ValueHandle.IsValid())
				{
					ValueHandle->GetValue(Value);
				}
				return FText::Format(LOCTEXT("SummaryNumber", "{0} {1} {2}"), FText::FromName(Input), Label, FText::AsNumber(Value));
			}
			return FText::Format(LOCTEXT("Summary", "{0} {1}"), FText::FromName(Input), Label);
		})
	];
}

void FWidgetAnimMachineConditionCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> InputHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Input));
	const TSharedPtr<IPropertyHandle> CompareHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Compare));
	const TSharedPtr<IPropertyHandle> ValueHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimMachineCondition, Value));
	if (!InputHandle.IsValid() || !CompareHandle.IsValid())
	{
		return;
	}
	const TWeakObjectPtr<const UWidgetAnimStateMachineAsset> WeakMachine(FindMachine(PropertyHandle));

	// Input: as it is - its dropdown of the machine's inputs and the built-in ones. Picked, Compare is put right for it: the
	// one it had, if it still applies, else the first that does.
	ChildBuilder.AddProperty(InputHandle.ToSharedRef());
	const TWeakPtr<IPropertyHandle> WeakInput = InputHandle;
	const TWeakPtr<IPropertyHandle> WeakCompare = CompareHandle;
	InputHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakInput, WeakCompare, WeakMachine]()
	{
		const TSharedPtr<IPropertyHandle> Input = WeakInput.Pin();
		const TSharedPtr<IPropertyHandle> Compare = WeakCompare.Pin();
		const FName InputName = GetInput(Input);
		EWidgetAnimMachineCompare Current = EWidgetAnimMachineCompare::IsTrue;
		if (!Compare.IsValid() || InputName.IsNone() || !GetCompare(Compare, Current))
		{
			return;
		}
		const TArray<EWidgetAnimMachineCompare> Allowed = GetCompares(WeakMachine.Get(), InputName);
		if (Allowed.Num() > 0 && !Allowed.Contains(Current))
		{
			Compare->SetValue(static_cast<uint8>(Allowed[0]));
		}
	}));

	// Compare: only what the input can be asked, in its words.
	IDetailPropertyRow& CompareRow = ChildBuilder.AddProperty(CompareHandle.ToSharedRef());
	CompareRow.CustomWidget()
	.NameContent()
	[
		CompareHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(140.f)
	[
		SNew(SComboButton)
		.OnGetMenuContent_Lambda([InputHandle, CompareHandle, WeakMachine]()
		{
			FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
			const FName Input = GetInput(InputHandle);
			const EConditionInputKind Kind = GetInputKind(WeakMachine.Get(), Input);
			for (const EWidgetAnimMachineCompare Compare : GetCompares(WeakMachine.Get(), Input))
			{
				Menu.AddMenuEntry(
					GetCompareLabel(WeakMachine.Get(), Input, Compare),
					GetCompareTooltip(Kind, Compare),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([CompareHandle, Compare]() { CompareHandle->SetValue(static_cast<uint8>(Compare)); }),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda([CompareHandle, Compare]()
						{
							EWidgetAnimMachineCompare Current = EWidgetAnimMachineCompare::IsTrue;
							return GetCompare(CompareHandle, Current) && Current == Compare;
						})),
					NAME_None,
					EUserInterfaceActionType::RadioButton);
			}
			return Menu.MakeWidget();
		})
		.ButtonContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text_Lambda([InputHandle, CompareHandle, WeakMachine]()
			{
				EWidgetAnimMachineCompare Current = EWidgetAnimMachineCompare::IsTrue;
				if (!GetCompare(CompareHandle, Current))
				{
					return LOCTEXT("MultipleCompares", "Multiple Values");
				}
				return GetCompareLabel(WeakMachine.Get(), GetInput(InputHandle), Current);
			})
		]
	];

	// Value: as it is - shown only for a comparison of numbers (its own EditCondition).
	if (ValueHandle.IsValid())
	{
		ChildBuilder.AddProperty(ValueHandle.ToSharedRef());
	}
}

#undef LOCTEXT_NAMESPACE
