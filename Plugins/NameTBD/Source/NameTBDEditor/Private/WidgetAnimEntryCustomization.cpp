// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimEntryCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "PropertyHandle.h"

TSharedRef<IPropertyTypeCustomization> FWidgetAnimEntryCustomization::MakeInstance()
{
	return MakeShared<FWidgetAnimEntryCustomization>();
}

void FWidgetAnimEntryCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// The name only - in an Anim In or Anim Out list it reads as the list's Title Property, the animation's name - and
	// the row's own array buttons, which the Details add to an element's header whatever lays it out.
	HeaderRow.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FWidgetAnimEntryCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const FName AnimationName = GET_MEMBER_NAME_CHECKED(FWidgetAnimEntryBase, Animation);
	const FName GroupTargetsName = GET_MEMBER_NAME_CHECKED(FWidgetAnimEntry, GroupTargets);

	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	TArray<TSharedRef<IPropertyHandle>> Children;
	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		const TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(Index);
		if (Child.IsValid() && Child->IsValidHandle())
		{
			Children.Add(Child.ToSharedRef());
		}
	}

	auto NameOf = [](const TSharedRef<IPropertyHandle>& Handle)
	{
		const FProperty* Property = Handle->GetProperty();
		return Property ? Property->GetFName() : NAME_None;
	};

	// Animation, then Target Widgets, then the rest as declared. Each keeps its own EditCondition wherever it is added.
	for (const FName First : { AnimationName, GroupTargetsName })
	{
		if (const TSharedRef<IPropertyHandle>* Found = Children.FindByPredicate([&NameOf, First](const TSharedRef<IPropertyHandle>& Handle) { return NameOf(Handle) == First; }))
		{
			ChildBuilder.AddProperty(*Found);
		}
	}

	for (const TSharedRef<IPropertyHandle>& Child : Children)
	{
		const FName Name = NameOf(Child);
		if (Name != AnimationName && Name != GroupTargetsName)
		{
			ChildBuilder.AddProperty(Child);
		}
	}
}
