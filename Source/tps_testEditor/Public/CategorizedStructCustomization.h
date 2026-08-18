// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class IDetailGroup;

/**
 * Generic details-panel customization that turns the Category metadata already written on
 * a struct's members into real collapsible groups.
 *
 * WHY THIS EXISTS
 * Category = "Recoil|Jitter" groups properties correctly on a UCLASS, but Unreal ignores
 * it for a USTRUCT displayed NESTED (a Blueprint variable, a component property, a
 * DataTable row) and renders one flat list instead. See UE-32252. This customization
 * rebuilds the grouping the panel refuses to build.
 *
 * ENROLLING A STRUCT
 * Add the metadata to the struct declaration, nothing else:
 *
 *     USTRUCT(BlueprintType, meta = (UseCategoryGroups))
 *     struct FWeaponRecoil { ... };
 *
 * Then add ONE registration line in FTpsTestEditorModule::StartupModule. Members group by
 * whatever Category they already carry; new members need no code changes anywhere.
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
class FCategorizedStructCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle,
	                             FDetailWidgetRow& HeaderRow,
	                             IPropertyTypeCustomizationUtils& CustomizationUtils) override;

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle,
	                               IDetailChildrenBuilder& ChildBuilder,
	                               IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:
	/** Resolve (creating as needed) the group for a "A|B|C" path, nesting one level per segment. */
	static IDetailGroup* ResolveGroup(IDetailChildrenBuilder& ChildBuilder,
									  const TArray<FString>& Path,
									  TMap<FString, IDetailGroup*>& GroupsByPath);

	/** Struct name without the F prefix, used to strip a redundant leading category segment. */
	FString StructDisplayName;
};
