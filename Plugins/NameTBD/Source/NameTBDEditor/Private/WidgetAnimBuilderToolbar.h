// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Play / Pause and Restart Preview in the Widget Blueprint editor's toolbar, beside Compile and Diff.
 *
 * Extends AssetEditorToolbar.CommonActions, which every asset editor's toolbar builds from, with a dynamic
 * entry: dynamic because the section is filled per editor window, and the buttons are only wanted on the
 * ones editing a Widget Blueprint that derives from UWidgetAnimBuilder. Every other asset editor sees
 * nothing added.
 *
 * This replaced a designer extension doing the same job. An extension can only hang controls off a
 * selected widget's own geometry, so the row moved with whatever the animation was doing to the image and
 * vanished when nothing was selected - and the root row in the Hierarchy is not a widget selection at
 * all, so it could never be triggered from there.
 *
 * The buttons act on the live preview instance the designer built, found through
 * UWidgetAnimBuilder::ForEachLivePreview, never on the class defaults.
 */
class FWidgetAnimBuilderToolbar
{
public:

	/** Call from the module's UToolMenus startup callback. */
	static void Register();
};