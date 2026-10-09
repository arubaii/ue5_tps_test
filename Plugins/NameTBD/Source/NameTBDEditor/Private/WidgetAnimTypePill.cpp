// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTypePill.h"

#include "EdGraphSchema_K2.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "NameTBDColors.h"

#define LOCTEXT_NAMESPACE "WidgetAnimTypePill"

namespace WidgetAnimTypePillLocal
{
	using WidgetAnimTypePill::EKind;

	/** The drop-down of MakeTypePickerButton: a search box over the types' pills and names. */
	class SWidgetAnimTypePicker : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetAnimTypePicker) {}
			SLATE_ARGUMENT(TArray<EKind>, Kinds)
			SLATE_ARGUMENT(TFunction<void(EKind)>, OnPicked)
			SLATE_ARGUMENT(TFunction<FText(EKind)>, GetToolTip)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			PickedHandler = InArgs._OnPicked;
			ToolTipFor = InArgs._GetToolTip;
			for (const EKind Kind : InArgs._Kinds)
			{
				AllKinds.Add(MakeShared<EKind>(Kind));
			}
			Shown = AllKinds;

			ChildSlot
			[
				SNew(SBox)
				.MinDesiredWidth(220.f)
				.Padding(FMargin(4.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 0.f, 0.f, 4.f))
					[
						SAssignNew(SearchBox, SSearchBox)
						.OnTextChanged(this, &SWidgetAnimTypePicker::OnSearchChanged)
						.OnTextCommitted(this, &SWidgetAnimTypePicker::OnSearchCommitted)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.MaxDesiredHeight(320.f)
						[
							SAssignNew(List, SListView<TSharedPtr<EKind>>)
							.ListItemsSource(&Shown)
							.SelectionMode(ESelectionMode::Single)
							.OnGenerateRow(this, &SWidgetAnimTypePicker::MakeRow)
							.OnMouseButtonClick(this, &SWidgetAnimTypePicker::Pick)
						]
					]
				]
			];
		}

		TSharedPtr<SWidget> GetFocusWidget() const { return SearchBox; }

	private:
		TSharedRef<ITableRow> MakeRow(TSharedPtr<EKind> Item, const TSharedRef<STableViewBase>& Owner)
		{
			return SNew(STableRow<TSharedPtr<EKind>>, Owner)
				.Padding(FMargin(4.f, 3.f))
				.ToolTipText(ToolTipFor ? ToolTipFor(*Item) : FText::GetEmpty())
				[
					WidgetAnimTypePill::MakePill(*Item)
				];
		}

		void OnSearchChanged(const FText& Text)
		{
			const FString Filter = Text.ToString().TrimStartAndEnd();
			Shown.Reset();
			for (const TSharedPtr<EKind>& Kind : AllKinds)
			{
				if (Filter.IsEmpty() || WidgetAnimTypePill::GetLabel(*Kind).ToString().Contains(Filter))
				{
					Shown.Add(Kind);
				}
			}
			List->RequestListRefresh();
		}

		void OnSearchCommitted(const FText& Text, ETextCommit::Type CommitType)
		{
			if (CommitType == ETextCommit::OnEnter && Shown.Num() > 0)
			{
				Pick(Shown[0]);
			}
		}

		void Pick(TSharedPtr<EKind> Item)
		{
			if (Item.IsValid() && PickedHandler)
			{
				// Copied first: closing the menu can free this picker.
				const EKind Picked = *Item;
				const TFunction<void(EKind)> Handler = PickedHandler;
				FSlateApplication::Get().DismissAllMenus();
				Handler(Picked);
			}
		}

		TArray<TSharedPtr<EKind>> AllKinds;
		TArray<TSharedPtr<EKind>> Shown;
		TFunction<void(EKind)> PickedHandler;
		TFunction<FText(EKind)> ToolTipFor;
		TSharedPtr<SSearchBox> SearchBox;
		TSharedPtr<SListView<TSharedPtr<EKind>>> List;
	};
}

namespace WidgetAnimTypePill
{
	EKind FromParameterType(EWidgetAnimParameterType Type)
	{
		switch (Type)
		{
		case EWidgetAnimParameterType::Integer:  return EKind::Integer;
		case EWidgetAnimParameterType::Bool:     return EKind::Bool;
		case EWidgetAnimParameterType::Color:    return EKind::Color;
		case EWidgetAnimParameterType::Vector2D: return EKind::Vector2D;
		case EWidgetAnimParameterType::Choice:   return EKind::Choice;
		default:                                 return EKind::Float;
		}
	}

	FLinearColor GetColor(EKind Kind)
	{
		// Trigger and Press And Hold aren't Blueprint types: their own colors.
		if (Kind == EKind::Trigger)
		{
			return NameTBD::Colors::StateMachineEditor::InputTrigger;
		}
		if (Kind == EKind::Hold)
		{
			return NameTBD::Colors::StateMachineEditor::InputHold;
		}

		// The rest as a Blueprint variable of the type is drawn: a Float is a Real (double) in Unreal 5, a Choice an enum.
		FEdGraphPinType PinType;
		switch (Kind)
		{
		case EKind::Bool:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			break;
		case EKind::Float:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			break;
		case EKind::Integer:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			break;
		case EKind::Choice:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			break;
		case EKind::Vector2D:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
			break;
		default:
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
			break;
		}
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		return Schema ? Schema->GetPinTypeColor(PinType) : FLinearColor::White;
	}

	FText GetLabel(EKind Kind)
	{
		switch (Kind)
		{
		case EKind::Bool:     return LOCTEXT("Bool", "Bool");
		case EKind::Float:    return LOCTEXT("Float", "Float");
		case EKind::Integer:  return LOCTEXT("Integer", "Integer");
		case EKind::Choice:   return LOCTEXT("Choice", "Choice");
		case EKind::Vector2D: return LOCTEXT("Vector2D", "Vector 2D");
		case EKind::Color:    return LOCTEXT("Color", "Color");
		case EKind::Trigger:  return LOCTEXT("Trigger", "Trigger");
		default:              return LOCTEXT("Hold", "Hold");
		}
	}

	TSharedRef<SWidget> MakePill(EKind Kind, bool bMuted)
	{
		// The My Blueprint panel's variable type icon: a pill in the pin color, then the type's name in the normal text color.
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(TEXT("Kismet.VariableList.TypeIcon")))
				.ColorAndOpacity(GetColor(Kind))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(GetLabel(Kind))
				.ColorAndOpacity(bMuted ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground())
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			];
	}

	TSharedRef<SWidget> MakeTypePickerButton(const TArray<EKind>& Kinds, TFunction<void(EKind)> OnPicked, TFunction<FText(EKind)> GetToolTip,
		const TAttribute<FText>& ButtonToolTip, TAttribute<bool> IsEnabled, bool bAsAddButton)
	{
		TSharedPtr<SComboButton> Combo;
		if (bAsAddButton)
		{
			// The Widget Animations panel's "+ Add" (Add Track) exactly: a plain combo button, a plus in the accent green, Add in bold.
			SAssignNew(Combo, SComboButton)
				.HasDownArrow(false)
				.ContentPadding(FMargin(6.f, 1.f))
				.ToolTipText(ButtonToolTip)
				.IsEnabled(IsEnabled)
				.ButtonContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))
						.ColorAndOpacity(FStyleColors::AccentGreen)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.Text(LOCTEXT("AddButton", "Add"))
					]
				];
		}
		else
		{
			SAssignNew(Combo, SComboButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.HasDownArrow(false)
				.ContentPadding(FMargin(2.f))
				.ToolTipText(ButtonToolTip)
				.IsEnabled(IsEnabled)
				.ButtonContent()
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))
					.ColorAndOpacity(FSlateColor::UseForeground())
				];
		}
		const TWeakPtr<SComboButton> WeakCombo = Combo;
		Combo->SetOnGetMenuContent(FOnGetContent::CreateLambda([WeakCombo, Kinds, OnPicked, GetToolTip]() -> TSharedRef<SWidget>
		{
			const TSharedRef<WidgetAnimTypePillLocal::SWidgetAnimTypePicker> Picker = SNew(WidgetAnimTypePillLocal::SWidgetAnimTypePicker)
				.Kinds(Kinds)
				.OnPicked(OnPicked)
				.GetToolTip(GetToolTip);
			// Typing goes straight into the search, as in the Blueprint editor.
			if (const TSharedPtr<SComboButton> PinnedCombo = WeakCombo.Pin())
			{
				PinnedCombo->SetMenuContentWidgetToFocus(Picker->GetFocusWidget());
			}
			return Picker;
		}));
		return Combo.ToSharedRef();
	}

	TSharedRef<SWidget> MakeValueEditor(const TSharedRef<IPropertyHandle>& Handle)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Handle->GetProperty());
		const UScriptStruct* Struct = StructProperty ? StructProperty->Struct : nullptr;

		// A Vector 2D: X and Y, each the property editor's own number box.
		if (Struct == TBaseStructure<FVector2D>::Get())
		{
			const TSharedPtr<IPropertyHandle> XHandle = Handle->GetChildHandle(TEXT("X"));
			const TSharedPtr<IPropertyHandle> YHandle = Handle->GetChildHandle(TEXT("Y"));
			if (XHandle.IsValid() && YHandle.IsValid())
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.Padding(FMargin(0.f, 0.f, 2.f, 0.f))
					[
						XHandle->CreatePropertyValueWidget()
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.Padding(FMargin(2.f, 0.f, 0.f, 0.f))
					[
						YHandle->CreatePropertyValueWidget()
					];
			}
		}

		// A Color: a swatch; click it for the color picker.
		if (Struct == TBaseStructure<FLinearColor>::Get())
		{
			const TWeakPtr<IPropertyHandle> WeakHandle = Handle;
			auto ReadColor = [WeakHandle]()
			{
				FLinearColor Color = FLinearColor::White;
				if (const TSharedPtr<IPropertyHandle> Pinned = WeakHandle.Pin())
				{
					FString Text;
					if (Pinned->GetValueAsFormattedString(Text) == FPropertyAccess::Success)
					{
						Color.InitFromString(Text);
					}
				}
				return Color;
			};
			return SNew(SBox)
				.HeightOverride(18.f)
				.VAlign(VAlign_Center)
				[
					SNew(SColorBlock)
					.Color_Lambda(ReadColor)
					.ShowBackgroundForAlpha(true)
					.OnMouseButtonDown_Lambda([WeakHandle, ReadColor](const FGeometry&, const FPointerEvent& MouseEvent)
					{
						if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
						{
							return FReply::Unhandled();
						}
						FColorPickerArgs Args;
						Args.bUseAlpha = true;
						Args.bIsModal = false;
						Args.InitialColor = ReadColor();
						Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([WeakHandle](FLinearColor Picked)
						{
							if (const TSharedPtr<IPropertyHandle> Pinned = WeakHandle.Pin())
							{
								Pinned->SetValueFromFormattedString(FString::Printf(TEXT("(R=%f,G=%f,B=%f,A=%f)"), Picked.R, Picked.G, Picked.B, Picked.A));
							}
						});
						OpenColorPicker(Args);
						return FReply::Handled();
					})
				];
		}

		return Handle->CreatePropertyValueWidget();
	}
}

#undef LOCTEXT_NAMESPACE
