// Copyright AR GameSystems 2026. All rights reserved.

#include "InfoCardDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "NameTBD/Cards/InfoCardAnchor.h"
#include "NameTBD/Cards/InfoCardTypes.h"
#include "NameTBD/Cards/TooltipCardAnchor.h"
#include "NameTBD/Cards/ContextMenu/ContextMenuAnchor.h"
#include "NameTBD/Cards/ContextMenu/ContextMenuTypes.h"
#include "NameTBD/DeveloperSettings/InfoCardDeveloperSettings.h"
#include "NameTBD/Menu/Input/MenuKeyPrompt.h"

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

	/** A field marked ActionFromMappingContext gets an Input Action picker narrowed to its sibling context's actions. */
	void NarrowActionPicker(IDetailPropertyRow& Row, const TSharedRef<IPropertyHandle>& Field, const TSharedPtr<FAssetThumbnailPool>& ThumbnailPool)
	{
		static const FName ContextMeta(TEXT("ActionFromMappingContext"));
		if (!Field->HasMetaData(ContextMeta))
		{
			return;
		}
		const TSharedPtr<IPropertyHandle> Parent = Field->GetParentHandle();
		const TSharedPtr<IPropertyHandle> Context = Parent.IsValid() ? Parent->GetChildHandle(FName(*Field->GetMetaData(ContextMeta))) : nullptr;
		if (Context.IsValid())
		{
			InfoCardDetails::MakeActionFromContextRow(Row, Field, Context, ThumbnailPool);
		}
	}

	/** Holds a Widget Blueprint class (which lays its own parts out), not a C++ one (which builds the native parts). */
	bool HoldsBlueprintClass(const TSharedPtr<IPropertyHandle>& ClassHandle)
	{
		UObject* Value = nullptr;
		if (!ClassHandle.IsValid() || ClassHandle->GetValue(Value) != FPropertyAccess::Success)
		{
			return false;
		}
		return Cast<UBlueprintGeneratedClass>(Value) != nullptr;
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
				InfoCardDetailsPrivate::NarrowActionPicker(ChildBuilder.AddProperty(Field), Field, CustomizationUtils.GetThumbnailPool());
			}
			continue;
		}

		IDetailGroup& DetailGroup = ChildBuilder.AddGroup(Group, FText::FromName(Group));
		for (const TSharedRef<IPropertyHandle>& Field : *GroupFields)
		{
			InfoCardDetailsPrivate::NarrowActionPicker(DetailGroup.AddPropertyRow(Field), Field, CustomizationUtils.GetThumbnailPool());
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
// Context Menu Style asset
// =================================================================================================

TSharedRef<IDetailCustomization> FContextMenuStyleAssetDetails::MakeInstance()
{
	return MakeShared<FContextMenuStyleAssetDetails>();
}

void FContextMenuStyleAssetDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	using namespace InfoCardDetailsPrivate;

	const TSharedRef<IPropertyHandle> StyleHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UContextMenuStyleAsset, Style));
	if (!StyleHandle->IsValidHandle())
	{
		return;
	}
	DetailBuilder.HideProperty(StyleHandle);

	// A Widget Blueprint picked as the row (or divider) class lays its parts out itself: these only arrange the native ones.
	const TSharedPtr<IPropertyHandle> EntryClassHandle = StyleHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FContextMenuStyle, EntryWidgetClass));
	const TSharedPtr<IPropertyHandle> DividerClassHandle = StyleHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FContextMenuStyle, DividerWidgetClass));
	const TSharedPtr<IPropertyHandle> CustomSubmenuHandle = StyleHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FContextMenuStyle, bCustomSubmenuPlacement));

	const TAttribute<bool> NativeRows = TAttribute<bool>::CreateLambda([EntryClassHandle]()
	{
		return !HoldsBlueprintClass(EntryClassHandle);
	});
	const TAttribute<bool> NativeDividers = TAttribute<bool>::CreateLambda([EntryClassHandle, DividerClassHandle]()
	{
		// Dividers use Entry Widget Class while Divider Widget Class is empty.
		UObject* DividerClass = nullptr;
		const bool bDividerSet = DividerClassHandle.IsValid() && DividerClassHandle->GetValue(DividerClass) == FPropertyAccess::Success && DividerClass;
		return !HoldsBlueprintClass(bDividerSet ? DividerClassHandle : EntryClassHandle);
	});
	const TAttribute<bool> CustomSubmenu = TAttribute<bool>::CreateLambda([CustomSubmenuHandle]()
	{
		bool bCustom = false;
		return CustomSubmenuHandle.IsValid() && CustomSubmenuHandle->GetValue(bCustom) == FPropertyAccess::Success && bCustom;
	});

	static const TSet<FName> NativeRowFields = {
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, RowContentHorizontalAlignment),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, RowContentVerticalAlignment),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, PartGap),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, bAlwaysReserveIconColumn),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, LabelAlignment),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, CenterTextAlignment),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, RightTextAlignment),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, ShortcutAlignment),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, IconGap),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, IconDelimiter),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, SubmenuArrowSize),
	};
	static const TSet<FName> NativeDividerFields = {
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, DividerThickness),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, DividerPadding),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, HeaderPadding),
		GET_MEMBER_NAME_CHECKED(FContextMenuStyle, TextDividerAlignment),
	};

	// Every field of the style straight into its category (no Style row to open), in declaration order.
	uint32 ChildCount = 0;
	StyleHandle->GetNumChildren(ChildCount);
	for (uint32 Index = 0; Index < ChildCount; ++Index)
	{
		const TSharedPtr<IPropertyHandle> Child = StyleHandle->GetChildHandle(Index);
		if (!Child.IsValid() || !Child->GetProperty())
		{
			continue;
		}

		const FName Category = GroupOf(Child.ToSharedRef());
		IDetailPropertyRow& Row = DetailBuilder.EditCategory(Category.IsNone() ? FName(TEXT("Style")) : Category).AddProperty(Child);

		const FName ChildName = Child->GetProperty()->GetFName();
		if (NativeRowFields.Contains(ChildName))
		{
			Row.IsEnabled(NativeRows);
		}
		else if (NativeDividerFields.Contains(ChildName))
		{
			Row.IsEnabled(NativeDividers);
		}
		else if (ChildName == GET_MEMBER_NAME_CHECKED(FContextMenuStyle, SubmenuPlacement))
		{
			Row.IsEnabled(CustomSubmenu);
		}
	}

	// What it does first, what it looks like after (FContextMenuStyle::GetCategoryOrder); anything else after those.
	DetailBuilder.SortCategories([](const TMap<FName, IDetailCategoryBuilder*>& Categories)
	{
		const TArray<FName>& Order = FContextMenuStyle::GetCategoryOrder();
		for (const TPair<FName, IDetailCategoryBuilder*>& Pair : Categories)
		{
			const int32 Position = Order.IndexOfByKey(Pair.Key);
			Pair.Value->SetSortOrder(Position == INDEX_NONE ? Order.Num() + 100 : Position);
		}
	});
}

// =================================================================================================
// Placement Presets (Project Settings)
// =================================================================================================

TSharedRef<IPropertyTypeCustomization> FInfoCardPlacementPresetCustomization::MakeInstance()
{
	return MakeShared<FInfoCardPlacementPresetCustomization>();
}

void FInfoCardPlacementPresetCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// The array's Title Property (the preset's name) and its element buttons.
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

void FInfoCardPlacementPresetCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	if (const TSharedPtr<IPropertyHandle> NameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FInfoCardPlacementPreset, PresetName)))
	{
		ChildBuilder.AddProperty(NameHandle.ToSharedRef());
	}

	// The placement's fields directly under the name. A preset is always its own fields: no Placement Source / Preset here.
	const TSharedPtr<IPropertyHandle> PlacementHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FInfoCardPlacementPreset, Placement));
	if (!PlacementHandle.IsValid())
	{
		return;
	}
	uint32 ChildCount = 0;
	PlacementHandle->GetNumChildren(ChildCount);
	for (uint32 Index = 0; Index < ChildCount; ++Index)
	{
		const TSharedPtr<IPropertyHandle> Child = PlacementHandle->GetChildHandle(Index);
		if (!Child.IsValid() || !Child->GetProperty())
		{
			continue;
		}
		const FName ChildName = Child->GetProperty()->GetFName();
		if (ChildName == GET_MEMBER_NAME_CHECKED(FInfoCardPositioning, Source) || ChildName == GET_MEMBER_NAME_CHECKED(FInfoCardPositioning, PresetName))
		{
			continue;
		}
		ChildBuilder.AddProperty(Child.ToSharedRef());
	}
}

// =================================================================================================
// Input Action pickers narrowed to a mapping context
// =================================================================================================

TSharedRef<IPropertyTypeCustomization> FContextMenuShortcutInputCustomization::MakeInstance()
{
	return MakeShared<FContextMenuShortcutInputCustomization>();
}

void FContextMenuShortcutInputCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
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

void FContextMenuShortcutInputCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> ContextHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FContextMenuShortcutInput, MappingContext));
	const TSharedPtr<IPropertyHandle> ActionHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FContextMenuShortcutInput, Action));

	// The context first: picking it narrows the action picker below to the actions it maps.
	if (ContextHandle.IsValid())
	{
		ChildBuilder.AddProperty(ContextHandle.ToSharedRef());
	}
	if (ActionHandle.IsValid())
	{
		InfoCardDetails::MakeActionFromContextRow(ChildBuilder.AddProperty(ActionHandle.ToSharedRef()), ActionHandle.ToSharedRef(), ContextHandle, CustomizationUtils.GetThumbnailPool());
	}
}

TSharedRef<IDetailCustomization> FMenuKeyPromptDetails::MakeInstance()
{
	return MakeShared<FMenuKeyPromptDetails>();
}

void FMenuKeyPromptDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const TSharedRef<IPropertyHandle> ActionHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMenuKeyPrompt, InputAction));
	const TSharedRef<IPropertyHandle> ContextHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMenuKeyPrompt, InputMappingContext));
	if (IDetailPropertyRow* Row = DetailBuilder.EditDefaultProperty(ActionHandle))
	{
		InfoCardDetails::MakeActionFromContextRow(*Row, ActionHandle, ContextHandle, DetailBuilder.GetThumbnailPool());
	}
}

// =================================================================================================
// Registration
// =================================================================================================

namespace InfoCardDetails
{
	void MakeActionFromContextRow(IDetailPropertyRow& Row, const TSharedRef<IPropertyHandle>& ActionHandle, const TSharedPtr<IPropertyHandle>& ContextHandle,
		const TSharedPtr<FAssetThumbnailPool>& ThumbnailPool)
	{
		// The actions the context maps, by path (filtering runs for every asset the picker lists).
		auto ShouldFilter = [ContextHandle](const FAssetData& Asset) -> bool
		{
			UObject* ContextObject = nullptr;
			if (!ContextHandle.IsValid() || ContextHandle->GetValue(ContextObject) != FPropertyAccess::Success)
			{
				return false;
			}
			const UInputMappingContext* Context = Cast<UInputMappingContext>(ContextObject);
			if (!Context)
			{
				return false; // no context: every action
			}
			const FSoftObjectPath AssetPath = Asset.GetSoftObjectPath();
			for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
			{
				if (Mapping.Action && FSoftObjectPath(Mapping.Action.Get()) == AssetPath)
				{
					return false;
				}
			}
			return true;
		};

		Row.CustomWidget()
			.NameContent()
			[
				ActionHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MinDesiredWidth(250.f)
			.MaxDesiredWidth(0.f)
			[
				SNew(SObjectPropertyEntryBox)
				.PropertyHandle(ActionHandle)
				.AllowedClass(UInputAction::StaticClass())
				.ThumbnailPool(ThumbnailPool)
				.OnShouldFilterAsset_Lambda(ShouldFilter)
			];
	}

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

		PropertyModule.RegisterCustomClassLayout(FName(TEXT("ContextMenuStyleAsset")),
			FOnGetDetailCustomizationInstance::CreateStatic(&FContextMenuStyleAssetDetails::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FName(TEXT("InfoCardPlacementPreset")),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FInfoCardPlacementPresetCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FName(TEXT("ContextMenuShortcutInput")),
			FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FContextMenuShortcutInputCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(FName(TEXT("MenuKeyPrompt")),
			FOnGetDetailCustomizationInstance::CreateStatic(&FMenuKeyPromptDetails::MakeInstance));

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
		PropertyModule->UnregisterCustomClassLayout(TEXT("ContextMenuStyleAsset"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("InfoCardPlacementPreset"));
		PropertyModule->UnregisterCustomPropertyTypeLayout(TEXT("ContextMenuShortcutInput"));
		PropertyModule->UnregisterCustomClassLayout(TEXT("MenuKeyPrompt"));
	}
}

#undef LOCTEXT_NAMESPACE
