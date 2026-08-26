// Copyright Bilcash 2026. All rights reserved.

/*
	HOW TO USE:
	1. Add meta = (UseCategoryGroups) to the struct declaration itself:
	     USTRUCT(..., meta = (UseCategoryGroups))
	2. Use Category = "Foo" on your UPROPERTY fields as normal.
	   Use | for sub-groups, e.g. Category = "Foo|Bar". Nest as deep as you like.
	3. To color a group header, add meta = (ColoredCategory) to ANY one UPROPERTY
	   inside that category. You only need it on a single property; the whole
	   group is colored. A color dropdown then appears on that group's header in
	   the Details panel, letting you pick from the palette defined in
	   Project Settings -> Plugins -> Struct Category Groups.
 */


#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IDetailGroup;
class IDetailChildrenBuilder;

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
	/**
	 * Walks a category path, creating/looking up nested IDetailGroups as needed.
	 * Any path present in colored_paths gets a color dropdown added to its header.
	 */
	IDetailGroup* ResolveGroup(IDetailChildrenBuilder& child_builder,
							   const TArray<FString>& path,
							   TMap<FString, IDetailGroup*>& groups_by_path,
							   const TSet<FString>& colored_paths);

	/** Builds the colored header (label + palette dropdown) for a single group. */
	void ApplyColoredHeader(IDetailGroup& group, const FString& category_path, const FString& leaf_label);

	/**
	 * Builds the header widget for a colored (or not-yet-colored) path: the
	 * fill-style content if assigned, or the stock bold label if not, always
	 * wrapped in the right-click palette zone. Used for both category group
	 * headers and the struct's own root header. extra_tooltip, if provided, is
	 * shown alongside the "right-click to set color" hint.
	 */
	TSharedRef<SWidget> BuildColoredHeaderContent(const FString& category_path,
												  const FString& leaf_label,
												  const FText& extra_tooltip = FText::GetEmpty());

	TSharedRef<SWidget> BuildPaletteMenuContent(const FString& category_path);
	
	/** Cached so the header dropdown can force a panel rebuild when the assignment changes. */
	TSharedPtr<class IPropertyUtilities> m_PropertyUtilities;

	FString m_StructDisplayName;
};