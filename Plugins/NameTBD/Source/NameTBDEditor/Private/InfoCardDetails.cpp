// Copyright AR GameSystems 2026. All rights reserved.

#include "InfoCardDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "NameTBD/Cards/InfoCardAnchor.h"
#include "NameTBD/Cards/InfoCardTypes.h"
#include "NameTBD/Cards/TooltipCardAnchor.h"
#include "NameTBD/Cards/ContextMenu/ContextMenuAnchor.h"
#include "NameTBD/Cards/ContextMenu/ContextMenuTypes.h"

#define LOCTEXT_NAMESPACE "InfoCardDetails"

namespace InfoCardDetailsPrivate
{
	/** A field's group: the last part of its Category ("Info Card|Placement" is Placement). */
	FName GroupOf(const TSharedRef<IPropertyHandle>& Handle)
	{
		FString Category = Handle->GetMetaData(TEXT("Category"));
		int32 Bar = INDEX_NONE;
		if (Category.FindLastChar(TEXT('|'), Bar))
		{
			Category.RightChopInline(Bar + 1);
		}
		return Category.IsEmpty() ? NAME_None : FName(*Category);
	}
}

// =================================================================================================
// Grouped structs
// =================================================================================================

TSharedRef<IPropertyTypeCustomization> FInfoCardGroupedStructCustomization::MakeSpecInstance()
{
	FGroupLayout GroupLayout;
	GroupLayout.Order = { TEXT("Card"), TEXT("Trigger"), TEXT("Placement"), TEXT("Dismissal"), TEXT("Payloads"), TEXT("Animation"), TEXT("Sound"), TEXT("Suppression") };
	GroupLayout.StartExpanded = { TEXT("Card"), TEXT("Trigger"), TEXT("Payloads") };
	return MakeShared<FInfoCardGroupedStructCustomization>(GroupLayout);
}

TSharedRef<IPropertyTypeCustomization> FInfoCardGroupedStructCustomization::MakeMenuEntryInstance()
{
	FGroupLayout GroupLayout;
	GroupLayout.Order = { TEXT("Entry"), TEXT("Content"), TEXT("Shortcut"), TEXT("Action"), TEXT("Attached Menu"), TEXT("State"), TEXT("Look") };
	GroupLayout.StartExpanded = { TEXT("Content"), TEXT("Action") };
	// The type and ID sit directly under the entry's title: they decide what the rest of it is.
	GroupLayout.Ungrouped = { TEXT("Entry") };
	return MakeShared<FInfoCardGroupedStructCustomization>(GroupLayout);
}

TSharedRef<IPropertyTypeCustomization> FInfoCardGroupedStructCustomization::MakeSettingsOverrideInstance()
{
	FGroupLayout GroupLayout;
	GroupLayout.Order = { TEXT("Info Card"), TEXT("Trigger"), TEXT("Placement"), TEXT("Offset"), TEXT("Tick"), TEXT("Dismissal"), TEXT("Sound"), TEXT("Card Transform") };
	GroupLayout.Ungrouped = { TEXT("Info Card") };
	GroupLayout.Hidden = { GET_MEMBER_NAME_CHECKED(FInfoCardSettingsOverrideElement, CardId) };
	GroupLayout.FlattenChild = GET_MEMBER_NAME_CHECKED(FInfoCardSettingsOverrideElement, Settings);
	return MakeShared<FInfoCardGroupedStructCustomization>(GroupLayout);
}

TSharedRef<IPropertyTypeCustomization> FInfoCardGroupedStructCustomization::MakeAnimationOverrideInstance()
{
	FGroupLayout GroupLayout;
	GroupLayout.Order = { TEXT("Info Card") };
	GroupLayout.Ungrouped = { TEXT("Info Card") };
	GroupLayout.Hidden = { GET_MEMBER_NAME_CHECKED(FInfoCardAdditiveAnimationsOverride, CardId) };
	return MakeShared<FInfoCardGroupedStructCustomization>(GroupLayout);
}

void FInfoCardGroupedStructCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// The name widget carries the array's Title Property (the Card ID, the Entry ID); the value widget its element buttons.
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			PropertyHandle->CreatePropertyValueWidget()
		];
}

void FInfoCardGroupedStructCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TArray<TSharedRef<IPropertyHandle>> Fields;

	auto Gather = [this, &Fields](const TSharedRef<IPropertyHandle>& Parent, auto& Self) -> void
	{
		uint32 ChildCount = 0;
		Parent->GetNumChildren(ChildCount);
		for (uint32 Index = 0; Index < ChildCount; ++Index)
		{
			const TSharedPtr<IPropertyHandle> Child = Parent->GetChildHandle(Index);
			if (!Child.IsValid() || !Child->GetProperty())
			{
				continue;
			}

			const FName ChildName = Child->GetProperty()->GetFName();
			if (Layout.Hidden.Contains(ChildName))
			{
				continue;
			}
			if (ChildName == Layout.FlattenChild)
			{
				Self(Child.ToSharedRef(), Self);
				continue;
			}
			Fields.Add(Child.ToSharedRef());
		}
	};
	Gather(PropertyHandle, Gather);

	TMap<FName, TArray<TSharedRef<IPropertyHandle>>> ByGroup;
	TArray<FName> GroupOrder = Layout.Order;
	for (const TSharedRef<IPropertyHandle>& Field : Fields)
	{
		const FName Group = InfoCardDetailsPrivate::GroupOf(Field);
		ByGroup.FindOrAdd(Group).Add(Field);
		GroupOrder.AddUnique(Group);
	}

	for (const FName Group : GroupOrder)
	{
		const TArray<TSharedRef<IPropertyHandle>>* GroupFields = ByGroup.Find(Group);
		if (!GroupFields || GroupFields->IsEmpty())
		{
			continue;
		}

		if (Group.IsNone() || Layout.Ungrouped.Contains(Group))
		{
			for (const TSharedRef<IPropertyHandle>& Field : *GroupFields)
			{
				ChildBuilder.AddProperty(Field);
			}
			continue;
		}

		IDetailGroup& DetailGroup = ChildBuilder.AddGroup(Group, FText::FromName(Group));
		for (const TSharedRef<IPropertyHandle>& Field : *GroupFields)
		{
			DetailGroup.AddPropertyRow(Field);
		}
		if (Layout.StartExpanded.Contains(Group))
		{
			DetailGroup.ToggleExpansion(true);
		}
	}
}

// =================================================================================================
// Entry Overrides
// =================================================================================================

TSharedRef<IPropertyTypeCustomization> FContextMenuEntryOverrideCustomization::MakeInstance()
{
	return MakeShared<FContextMenuEntryOverrideCustomization>();
}

bool FContextMenuEntryOverrideCustomization::IsKept() const
{
	bool bRemoved = false;
	return !RemoveHandle.IsValid() || RemoveHandle->GetValue(bRemoved) != FPropertyAccess::Success || !bRemoved;
}

void FContextMenuEntryOverrideCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	RemoveHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FContextMenuEntryOverride, bRemove));

	// The Entry ID is the element's title (the array's Title Property); Remove sits right beside it.
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				RemoveHandle.IsValid() ? RemoveHandle->CreatePropertyValueWidget() : SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.f, 0.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RemoveEntry", "Remove"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.ToolTipText(LOCTEXT("RemoveEntryTooltip", "Leaves this entry out of this widget's menu. To keep the row but grey it out, override Enabled instead."))
			]
		];
}

void FContextMenuEntryOverrideCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TAttribute<bool> KeptAttribute = TAttribute<bool>::CreateSP(this, &FContextMenuEntryOverrideCustomization::IsKept);

	uint32 ChildCount = 0;
	PropertyHandle->GetNumChildren(ChildCount);
	for (uint32 Index = 0; Index < ChildCount; ++Index)
	{
		const TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(Index);
		if (!Child.IsValid() || !Child->GetProperty())
		{
			continue;
		}

		// The ID is the title and Remove is in the header; every other field (each with its own override toggle) greys out
		// while the entry is removed.
		const FName ChildName = Child->GetProperty()->GetFName();
		if (ChildName == GET_MEMBER_NAME_CHECKED(FContextMenuEntryOverride, EntryId) || ChildName == GET_MEMBER_NAME_CHECKED(FContextMenuEntryOverride, bRemove))
		{
			continue;
		}
		ChildBuilder.AddProperty(Child.ToSharedRef()).IsEnabled(KeptAttribute);
	}
}

// =================================================================================================
// Payload Defaults
// =================================================================================================

TSharedRef<IPropertyTypeCustomization> FInfoCardPayloadDefaultCustomization::MakeInstance()
{
	return MakeShared<FInfoCardPayloadDefaultCustomization>();
}

void FInfoCardPayloadDefaultCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> PayloadHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FInfoCardPayloadDefault, Payload));

	UObject* Payload = nullptr;
	if (PayloadHandle.IsValid())
	{
		PayloadHandle->GetValue(Payload);
	}

	// The payload's type as text, as the array index shows the Card ID: it follows the Data Asset and can't be changed here.
	const FText TypeText = Payload ? Payload->GetClass()->GetDisplayNameText() : LOCTEXT("NoPayload", "No payload");

	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(200.f)
		[
			SNew(STextBlock)
			.Text(TypeText)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.ToolTipText(LOCTEXT("PayloadTypeTooltip", "The payload this card's Data Asset entry has. Add or remove payloads on the Data Asset; change its fields here, for this anchor."))
		];
}

void FInfoCardPayloadDefaultCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> PayloadHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FInfoCardPayloadDefault, Payload));
	if (!PayloadHandle.IsValid())
	{
		return;
	}

	UObject* Payload = nullptr;
	PayloadHandle->GetValue(Payload);
	if (!Payload)
	{
		return;
	}

	// The payload's own fields, straight under the element: Payload Defaults > [Card ID] > Title, Body.
	bool bAddedAny = false;
	for (TFieldIterator<FProperty> It(Payload->GetClass()); It; ++It)
	{
		const FProperty* Field = *It;
		if (!Field->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		// Found by name anywhere under the object's node, and only taken when it really is this field (a nested struct
		// could have a member of the same name).
		const TSharedPtr<IPropertyHandle> FieldHandle = PayloadHandle->GetChildHandle(Field->GetFName(), /*bRecurse=*/true);
		if (FieldHandle.IsValid() && FieldHandle->GetProperty() == Field)
		{
			ChildBuilder.AddProperty(FieldHandle.ToSharedRef());
			bAddedAny = true;
		}
	}

	// Nothing reachable by name (the object's rows not built under this node): the payload as before, so nothing is lost.
	if (!bAddedAny)
	{
		ChildBuilder.AddProperty(PayloadHandle.ToSharedRef());
	}
}

// =================================================================================================
// Tooltip Card Anchor / Context Menu Anchor
// =================================================================================================

TSharedRef<IDetailCustomization> FInfoCardGeneratedAnchorDetails::MakeInstance()
{
	return MakeShared<FInfoCardGeneratedAnchorDetails>();
}

void FInfoCardGeneratedAnchorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// These anchors build their cards from their own fields; the generic sources would do nothing here.
	const UClass* AnchorClass = UInfoCardAnchor::StaticClass();
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UInfoCardAnchor, SourceMode), AnchorClass);
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UInfoCardAnchor, InfoCards_DA), AnchorClass);
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UInfoCardAnchor, PayloadDefaults), AnchorClass);
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UInfoCardAnchor, SettingsOverrides), AnchorClass);
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UInfoCardAnchor, AdditiveAnimationOverrides), AnchorClass);
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UInfoCardAnchor, CustomInfoCards), AnchorClass);

	// Their own fields first, above everything a Border has.
	DetailBuilder.EditCategory(TEXT("Tooltip"), FText::GetEmpty(), ECategoryPriority::Important);
	DetailBuilder.EditCategory(TEXT("Context Menu"), FText::GetEmpty(), ECategoryPriority::Important);
}

// =================================================================================================
// Registration
// =================================================================================================

namespace InfoCardDetails
{
	void Register()
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyModule.RegisterCustomPropertyTypeLayout(FName(TEXT("InfoCardSpec")),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FInfoCardGroupedStructCustomization::MakeSpecInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FName(TEXT("ContextMenuEntry")),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FInfoCardGroupedStructCustomization::MakeMenuEntryInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FName(TEXT("InfoCardSettingsOverrideElement")),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FInfoCardGroupedStructCustomization::MakeSettingsOverrideInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FName(TEXT("InfoCardAdditiveAnimationsOverride")),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FInfoCardGroupedStructCustomization::MakeAnimationOverrideInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FName(TEXT("ContextMenuEntryOverride")),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FContextMenuEntryOverrideCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FName(TEXT("InfoCardPayloadDefault")),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FInfoCardPayloadDefaultCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(FName(TEXT("TooltipCardAnchor")),
			FOnGetDetailCustomizationInstance::CreateStatic(&FInfoCardGeneratedAnchorDetails::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(FName(TEXT("ContextMenuAnchor")),
			FOnGetDetailCustomizationInstance::CreateStatic(&FInfoCardGeneratedAnchorDetails::MakeInstance));

		PropertyModule.NotifyCustomizationModuleChanged();
	}

	void Unregister()
	{
		FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor");
		if (!PropertyModule)
		{
			return;
		}

		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("InfoCardSpec"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("ContextMenuEntry"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("InfoCardSettingsOverrideElement"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("InfoCardAdditiveAnimationsOverride"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("ContextMenuEntryOverride"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("InfoCardPayloadDefault"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("TooltipCardAnchor"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("ContextMenuAnchor"));
	}
}

#undef LOCTEXT_NAMESPACE
