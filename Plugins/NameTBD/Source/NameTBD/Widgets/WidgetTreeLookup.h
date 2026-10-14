// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UWidget;
class UWidgetTree;

/**
 * Finding widgets by name in the tree something lives in - for settings that name another widget (a Widget Path's nodes,
 * a constraint's target), whether that something is a widget in the tree, a UI Component on one, or an object inside a
 * component. Offered as dropdowns in the Details panel (GetOptions) and resolved by name at runtime.
 *
 * The tree is found through the object's outers: a Widget Tree, a User Widget's tree, or - in the editor, where a component
 * lives on its Widget Blueprint - any outer holding an object property named WidgetTree (read by reflection, so the runtime
 * module needn't know the editor's Widget Blueprint class).
 */
namespace WidgetTreeLookup
{
	/** The widget tree From belongs to, or null. */
	NAMETBD_API UWidgetTree* FindWidgetTree(const UObject* From);

	/** Every named widget in From's tree, sorted, without Exclude - what a widget dropdown lists. "None" first when bWithNone. */
	NAMETBD_API void GetWidgetNames(const UObject* From, TArray<FString>& OutNames, const UWidget* Exclude = nullptr, bool bWithNone = true);

	/** The widget named Name in From's tree, or null (also for None). */
	NAMETBD_API UWidget* FindWidget(const UObject* From, FName Name);
}
