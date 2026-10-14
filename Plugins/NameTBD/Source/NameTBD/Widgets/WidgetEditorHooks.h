// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UClass;
class UObject;
class UWidget;

/**
 * Small editor services the runtime module's widgets use while being designed, without depending on the editor modules:
 * finding the Widget Blueprint's own widget behind a designer preview copy (so an edit made through the preview - a preset
 * picked - reaches what is saved and compiled), and making a new asset with the editor's Save Asset As window (the editor
 * module hands that in at startup). Both do nothing in a game.
 */
namespace WidgetEditorHooks
{
	/**
	 * The Widget Blueprint's own widget (its template) that a designer preview widget is a copy of, found by name in the
	 * Widget Blueprint the preview was made from. Null for anything else - a template itself, a widget in a game.
	 */
	NAMETBD_API UWidget* FindDesignerTemplate(const UWidget* PreviewWidget);

	/** Makes a new asset of Class through the Save Asset As window, DefaultName suggested. Null when cancelled. */
	using FAssetCreator = TFunction<UObject*(UClass* /*Class*/, const FString& /*DefaultName*/)>;

	/** Set by the editor module at startup (and cleared at shutdown). */
	NAMETBD_API void SetAssetCreator(FAssetCreator InCreator);

	/** Makes a new asset of Class (see FAssetCreator); null in a game, or when cancelled. */
	NAMETBD_API UObject* CreateAsset(UClass* Class, const FString& DefaultName);
}
