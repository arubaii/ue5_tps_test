// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimEntryCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
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

	// Progress Animation Duration only where it means something - a percentage animation played on construct or when
	// hidden - worked out from the entry each time it is drawn, so it appears the moment either trigger is ticked or the
	// animation is switched to Percent. See FWidgetAnimEntry::ShowsProgressDuration.
	const FName ProgressDurationName = GET_MEMBER_NAME_CHECKED(FWidgetAnimEntryBase, ProgressAnimationDuration);
	const TSharedRef<IPropertyHandle> EntryHandle = PropertyHandle;
	auto ProgressDurationVisibility = [EntryHandle]()
	{
		TArray<void*> RawData;
		EntryHandle->AccessRawData(RawData);
		for (const void* Raw : RawData)
		{
			if (Raw && static_cast<const FWidgetAnimEntry*>(Raw)->ShowsProgressDuration())
			{
				return EVisibility::Visible;
			}
		}
		return EVisibility::Collapsed;
	};

	for (const TSharedRef<IPropertyHandle>& Child : Children)
	{
		const FName Name = NameOf(Child);
		if (Name == ProgressDurationName)
		{
			ChildBuilder.AddProperty(Child).Visibility(TAttribute<EVisibility>::CreateLambda(ProgressDurationVisibility));
		}
		else if (Name != AnimationName && Name != GroupTargetsName)
		{
			ChildBuilder.AddProperty(Child);
		}
	}
}
