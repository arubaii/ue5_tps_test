// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"

/**
 * Details layouts for info cards and context menus:
 *
 *   - FInfoCardGroupedStructCustomization: a struct's fields grouped by their Category (Card, Trigger, Placement...), in a fixed
 *     order, only some groups starting expanded - for an Info Card entry (FInfoCardSpec) and a menu entry (FContextMenuEntry),
 *     which were one long wall of fields. Rows hidden by name (a Card ID shown as the element's title) never appear.
 *   - FInfoCardPayloadDefaultCustomization: an anchor's Payload Defaults element as its Card ID, the payload's type as plain
 *     text, and the payload's own fields directly under it (Title, Body) - instead of Card ID row > class dropdown > object.
 *   - FContextMenuEntryOverrideCustomization: an anchor's Entry Overrides element as its Entry ID with the Remove check box
 *     beside it; the element's fields grey out while it is removed.
 *   - FInfoCardGeneratedAnchorDetails: the Tooltip Card Anchor and Context Menu Anchor, which build their cards from their own
 *     fields, with the generic anchor's sources hidden and their own category first.
 */
class FInfoCardGroupedStructCustomization : public IPropertyTypeCustomization
{
public:

	struct FGroupLayout
	{
		/** Groups in display order; a category not listed goes after them, collapsed. */
		TArray<FName> Order;
		TSet<FName> StartExpanded;
		/** Categories whose fields are shown directly, not in a group (an entry's type and ID). */
		TSet<FName> Ungrouped;
		/** Child rows never shown (by property name). */
		TSet<FName> Hidden;
		/** A struct child whose own children are shown in its place, grouped (Settings of a Settings Overrides element). */
		FName FlattenChild = NAME_None;
	};

	explicit FInfoCardGroupedStructCustomization(const FGroupLayout& InLayout) : Layout(InLayout) {}

	static TSharedRef<IPropertyTypeCustomization> MakeSpecInstance();
	static TSharedRef<IPropertyTypeCustomization> MakeMenuEntryInstance();
	static TSharedRef<IPropertyTypeCustomization> MakeSettingsOverrideInstance();
	static TSharedRef<IPropertyTypeCustomization> MakeAnimationOverrideInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:

	FGroupLayout Layout;
};

class FInfoCardPayloadDefaultCustomization : public IPropertyTypeCustomization
{
public:

	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
};

class FContextMenuEntryOverrideCustomization : public IPropertyTypeCustomization
{
public:

	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;

private:

	/** The element's fields can be edited: it isn't removed (or several anchors disagree). */
	bool IsKept() const;

	TSharedPtr<IPropertyHandle> RemoveHandle;
};

class FInfoCardGeneratedAnchorDetails : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};

namespace InfoCardDetails
{
	/** Registers everything above with the property editor. */
	void Register();

	/** Unregisters by name (safe at shutdown). */
	void Unregister();
}
