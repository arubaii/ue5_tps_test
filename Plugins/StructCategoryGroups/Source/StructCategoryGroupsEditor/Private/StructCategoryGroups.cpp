// Copyright Bilcash. All rights reserved.

#include "StructCategoryGroups.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "StructCategoryGroups"

TSharedRef<IPropertyTypeCustomization> FStructCategoryGroups::MakeInstance()
{
	return MakeShareable(new FStructCategoryGroups());
}

void FStructCategoryGroups::CustomizeHeader(TSharedRef<IPropertyHandle> property_handle,
                                            FDetailWidgetRow& header_row,
                                            IPropertyTypeCustomizationUtils& customization_utils)
{
	header_row
		.NameContent()
		[
			property_handle->CreatePropertyNameWidget()
		];

	m_StructDisplayName.Reset();
	if (const FStructProperty* structProperty = CastField<FStructProperty>(property_handle->GetProperty()))
	{
		if (structProperty->Struct)
		{
			m_StructDisplayName = structProperty->Struct->GetName();
		}
	}
}

IDetailGroup* FStructCategoryGroups::ResolveGroup(IDetailChildrenBuilder& child_builder,
                                                   const TArray<FString>& path,
                                                   TMap<FString, IDetailGroup*>& groups_by_path)
{
	IDetailGroup* current = nullptr;
	FString accumulated;

	for (const FString& segment : path)
	{
		accumulated = accumulated.IsEmpty() ? segment : (accumulated + TEXT("|") + segment);

		if (IDetailGroup** existing = groups_by_path.Find(accumulated))
		{
			current = *existing;
			continue;
		}

		IDetailGroup& newGroup = current
			? current->AddGroup(FName(*accumulated), FText::FromString(segment))
			: child_builder.AddGroup(FName(*accumulated), FText::FromString(segment));

		groups_by_path.Add(accumulated, &newGroup);
		current = &newGroup;
	}

	return current;
}

void FStructCategoryGroups::CustomizeChildren(TSharedRef<IPropertyHandle> property_handle,
                                              IDetailChildrenBuilder& child_builder,
                                              IPropertyTypeCustomizationUtils& customization_utils)
{
	uint32 numChildren = 0;
	property_handle->GetNumChildren(numChildren);

	TMap<FString, IDetailGroup*> groupsByPath;

	for (uint32 index = 0; index < numChildren; ++index)
	{
		TSharedPtr<IPropertyHandle> childHandle = property_handle->GetChildHandle(index);
		if (!childHandle.IsValid() || !childHandle->GetProperty())
		{
			continue;
		}

		FString category = childHandle->GetMetaData(TEXT("Category"));

		TArray<FString> path;
		if (!category.IsEmpty())
		{
			category.ParseIntoArray(path, TEXT("|"), /*InCullEmpty*/ true);

			for (FString& segment : path)
			{
				segment.TrimStartAndEndInline();
			}

			if (path.Num() > 0 && !m_StructDisplayName.IsEmpty()
				&& path[0].Equals(m_StructDisplayName, ESearchCase::IgnoreCase))
			{
				path.RemoveAt(0);
			}
		}

		if (path.Num() == 0)
		{
			child_builder.AddProperty(childHandle.ToSharedRef());
			continue;
		}

		if (IDetailGroup* group = ResolveGroup(child_builder, path, groupsByPath))
		{
			group->AddPropertyRow(childHandle.ToSharedRef());
		}
		else
		{
			child_builder.AddProperty(childHandle.ToSharedRef());
		}
	}
}

#undef LOCTEXT_NAMESPACE