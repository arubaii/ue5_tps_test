// Copyright Bilcash. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IDetailGroup;

class FStructCategoryGroups : public IPropertyTypeCustomization
{
public:
	
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> property_handle,
								 FDetailWidgetRow& header_row,
								 IPropertyTypeCustomizationUtils& customization_utils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> property_handle,
								   IDetailChildrenBuilder& child_builder,
								   IPropertyTypeCustomizationUtils& customization_utils) override;

private:
	static IDetailGroup* ResolveGroup(IDetailChildrenBuilder& child_builder,
									  const TArray<FString>& path,
									  TMap<FString, IDetailGroup*>& groups_by_path);

	FString m_StructDisplayName;
};