// Copyright Bilcash. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IDetailGroup;

/**
 * Generic details-panel customization that turns the Category metadata already written on
 * a struct's members into real collapsible groups.
 *
 * WHY THIS EXISTS:
 * Category = "Recoil|Jitter" groups properties correctly on a UCLASS, but Unreal ignores
 * it for a USTRUCT displayed NESTED (a Blueprint variable, a component property, a
 * DataTable row) and renders one flat list instead. See UE-32252. This customization
 * rebuilds the grouping the panel refuses to build.
 *
 * ENROLLING A STRUCT:
 * Add the metadata to the struct declaration, nothing else:
 *
 *     USTRUCT(BlueprintType, meta = (UseCategoryGroups))
 *     struct FWeaponRow : public FTableRowBase { ... };
 *
 * No registration code or module edits. The plugin scans for this tag automatically on
 * startup. Members group by whatever Category they already carry.
 *
 * REMOVING THE OPT-IN (apply to ALL structs, not just tagged ones)
 * In StructCategoryGroupsEditorModule.cpp, delete the block inside RegisterOptedInStructs()
 * that checks HasMetaData(s_UseCategoryGroupsMetaKey) (and the s_UseCategoryGroupsMetaKey
 * declaration above it). Every USTRUCT in the project will then be grouped automatically,
 * with no meta tag required. Note this also affects engine and third-party plugin structs,
 * since the scan isn't limited to your own project's structs — test carefully before
 * relying on this globally.
 *
 * BEHAVIOUR
 *  - Members with no Category (or a Category matching the struct's own name) stay
 *    ungrouped at the top, so the important fields remain visible without expanding.
 *  - A leading path segment matching the struct name is dropped, so "Recoil|Jitter"
 *    inside FWeaponRecoil yields one "Jitter" group rather than "Recoil > Jitter".
 *  - Deeper paths nest: "Recoil|Jitter|Advanced" yields Jitter > Advanced.
 *  - Group order follows first appearance in declaration order.
 *  - EditCondition / EditConditionHides keep working: rows are added through
 *    AddPropertyRow, so the property editor still evaluates them normally.
 */


class FStructCategoryGroups : public IPropertyTypeCustomization
{
public:
	/**
	 * When true (default), only structs whose USTRUCT() carries meta = (UseCategoryGroups)
	 * are enrolled. When false, StructCategoryGroupsEditorModule enrolls every USTRUCT it
	 * finds, opt-in tag or not — including engine and third-party plugin structs. Flip this
	 * to false for a quick global test; leave it true for normal, scoped use.
	 */
	static constexpr bool bRequireOptInTag = true;

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