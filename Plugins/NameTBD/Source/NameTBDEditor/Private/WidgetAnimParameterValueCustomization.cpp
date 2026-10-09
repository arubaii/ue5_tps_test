// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimParameterValueCustomization.h"

#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#include "NameTBD/WidgetAnimation/WidgetAnimParameters.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "WidgetAnimTypePill.h"

#define LOCTEXT_NAMESPACE "WidgetAnimParameterValueCustomization"

namespace WidgetAnimParameterValueCustomizationLocal
{
	/** The animation of the entry whose Parameters the row is in - null when it isn't in one. */
	const UWidgetAnimPresetAsset* FindEntryAnimation(const TSharedRef<IPropertyHandle>& Handle)
	{
		const TSharedPtr<IPropertyHandle> Array = Handle->GetParentHandle();
		const TSharedPtr<IPropertyHandle> Entry = Array.IsValid() ? Array->GetParentHandle() : nullptr;
		if (!Array.IsValid() || !Entry.IsValid() || !Array->GetProperty() || Array->GetProperty()->GetFName() != GET_MEMBER_NAME_CHECKED(FWidgetAnimEntryBase, Parameters))
		{
			return nullptr;
		}
		const TSharedPtr<IPropertyHandle> AnimationHandle = Entry->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimEntryBase, Animation));
		UObject* Object = nullptr;
		if (!AnimationHandle.IsValid() || AnimationHandle->GetValue(Object) != FPropertyAccess::Success)
		{
			return nullptr;
		}
		return Cast<UWidgetAnimPresetAsset>(Object);
	}

	WidgetAnimTypePill::EKind GetKind(EWidgetAnimParameterType Type)
	{
		switch (Type)
		{
		case EWidgetAnimParameterType::Integer:  return WidgetAnimTypePill::EKind::Integer;
		case EWidgetAnimParameterType::Bool:     return WidgetAnimTypePill::EKind::Bool;
		case EWidgetAnimParameterType::Color:    return WidgetAnimTypePill::EKind::Color;
		case EWidgetAnimParameterType::Vector2D: return WidgetAnimTypePill::EKind::Vector2D;
		case EWidgetAnimParameterType::Choice:   return WidgetAnimTypePill::EKind::Choice;
		default:                                 return WidgetAnimTypePill::EKind::Float;
		}
	}

	FName GetValueFieldName(EWidgetAnimParameterType Type)
	{
		switch (Type)
		{
		case EWidgetAnimParameterType::Integer:  return GET_MEMBER_NAME_CHECKED(FWidgetAnimParameterValue, Integer);
		case EWidgetAnimParameterType::Bool:     return GET_MEMBER_NAME_CHECKED(FWidgetAnimParameterValue, bBool);
		case EWidgetAnimParameterType::Color:    return GET_MEMBER_NAME_CHECKED(FWidgetAnimParameterValue, Color);
		case EWidgetAnimParameterType::Vector2D: return GET_MEMBER_NAME_CHECKED(FWidgetAnimParameterValue, Vector2D);
		case EWidgetAnimParameterType::Choice:   return GET_MEMBER_NAME_CHECKED(FWidgetAnimParameterValue, Option);
		default:                                 return GET_MEMBER_NAME_CHECKED(FWidgetAnimParameterValue, Float);
		}
	}
}

TSharedRef<IPropertyTypeCustomization> FWidgetAnimParameterValueCustomization::MakeInstance()
{
	return MakeShared<FWidgetAnimParameterValueCustomization>();
}

void FWidgetAnimParameterValueCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	using namespace WidgetAnimParameterValueCustomizationLocal;

	const UWidgetAnimPresetAsset* Animation = FindEntryAnimation(PropertyHandle);
	const TSharedPtr<IPropertyHandle> NameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimParameterValue, Name));
	const TSharedPtr<IPropertyHandle> OverrideHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimParameterValue, bOverride));
	FName ParameterName;
	if (NameHandle.IsValid())
	{
		NameHandle->GetValue(ParameterName);
	}
	const FWidgetAnimParameter* Declared = Animation ? Animation->FindParameter(ParameterName) : nullptr;
	bInEntry = Declared && OverrideHandle.IsValid();
	if (!bInEntry)
	{
		// Anywhere else: its fields, as they are.
		HeaderRow.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		];
		return;
	}

	const EWidgetAnimParameterType Type = Declared->Type;
	const TSharedPtr<IPropertyHandle> ValueHandle = PropertyHandle->GetChildHandle(GetValueFieldName(Type));
	auto IsSetHere = [OverrideHandle]()
	{
		bool bSet = false;
		return OverrideHandle.IsValid() && OverrideHandle->GetValue(bSet) == FPropertyAccess::Success && bSet;
	};

	// Changing the value sets it here - nobody edits a value meaning to leave the default.
	if (ValueHandle.IsValid())
	{
		ValueHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([OverrideHandle]()
		{
			bool bSet = false;
			if (OverrideHandle.IsValid() && OverrideHandle->GetValue(bSet) == FPropertyAccess::Success && !bSet)
			{
				OverrideHandle->SetValue(true);
			}
		}));
	}

	TSharedRef<SWidget> ValueWidget = SNullWidget::NullWidget;
	if (Type == EWidgetAnimParameterType::Choice)
	{
		const TArray<FName> Options = Declared->Options;
		ValueWidget = SNew(SComboButton)
			.OnGetMenuContent_Lambda([ValueHandle, Options]()
			{
				FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
				for (const FName Option : Options)
				{
					Menu.AddMenuEntry(FText::FromName(Option), FText::GetEmpty(), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([ValueHandle, Option]()
						{
							if (ValueHandle.IsValid())
							{
								ValueHandle->SetValue(Option);
							}
						})));
				}
				return Menu.MakeWidget();
			})
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(CustomizationUtils.GetRegularFont())
				.Text_Lambda([ValueHandle]()
				{
					FName Option;
					return ValueHandle.IsValid() && ValueHandle->GetValue(Option) == FPropertyAccess::Success ? FText::FromName(Option) : FText::GetEmpty();
				})
			];
	}
	else if (ValueHandle.IsValid())
	{
		ValueWidget = WidgetAnimTypePill::MakeValueEditor(ValueHandle.ToSharedRef());
	}

	const FText ToolTip = Declared->Description.IsEmpty()
		? LOCTEXT("ParameterValueTip", "Ticked: this use of the animation plays this value. Not ticked: the animation's default. A Play call or Set Widget Animation Parameter wins over both.")
		: FText::FromString(Declared->Description);

	HeaderRow
		.NameContent()
		[
			SNew(SHorizontalBox)
			.ToolTipText(ToolTip)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([IsSetHere]() { return IsSetHere() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([OverrideHandle](ECheckBoxState State)
				{
					if (OverrideHandle.IsValid())
					{
						OverrideHandle->SetValue(State == ECheckBoxState::Checked);
					}
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(CustomizationUtils.GetRegularFont())
				.Text(FText::FromName(ParameterName))
			]
		]
		.ValueContent()
		.MinDesiredWidth(WidgetAnimTypePill::TypeColumnWidth + WidgetAnimTypePill::ValueColumnWidth)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				SNew(SBox)
				.WidthOverride(WidgetAnimTypePill::TypeColumnWidth)
				[
					WidgetAnimTypePill::MakePill(GetKind(Type))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.IsEnabled_Lambda([IsSetHere]() { return IsSetHere(); })
				[
					ValueWidget
				]
			]
		];
}

void FWidgetAnimParameterValueCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// An entry's row is all in its header.
	if (bInEntry)
	{
		return;
	}

	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		const TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(Index);
		if (Child.IsValid() && Child->GetProperty() && Child->GetProperty()->GetFName() != GET_MEMBER_NAME_CHECKED(FWidgetAnimParameterValue, bOverride))
		{
			ChildBuilder.AddProperty(Child.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE
