// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class SDockTab;
class UWidgetBlueprint;
class UWidgetAnimPresetAsset;

/**
 * The per-editor hook for the Widget Animations panel, and for the selected widget's preview controls. The
 * class keeps its old name so the module's startup code calling Register() needs no change.
 *
 * Extends the Widget Blueprint editor's toolbar with a dynamic entry that runs once per editor window, and
 * for every Widget Blueprint's editor adds a Widget Animations drawer to the status bar, beside Animations,
 * and a Widget Animation Scripting drawer after it (the panel's Script area, docking beside the Details panel),
 * holding SWidgetAnimTimeline, and registers the tab its Dock in Layout button opens - through UMG's own tab
 * and layout hooks, so the tab is one of the Widget Blueprint editor's and docks beside Animations. In a
 * WidgetAnimationBuilder the panel builds a generic animation on the builder's working copy; in any other
 * Widget Blueprint it builds a WA_ asset against that widget's own widgets, one Track Group per widget.
 * See RegisterWidgetAnimationsDrawer and IWidgetAnimPanelSource.
 *
 * Outside a builder, it also adds Play In / Play Out / Reset, a timer, a scrubber, Loop / Ping Pong / Invert
 * and Speed to the toolbar, shown only while the widget selected in the designer has animations - a Widget
 * Animation Component on it or inside it - and acting on that widget alone: a quick look at how it moves
 * where it has been put. See FSelectedWidgetAnimations.
 *
 * Needs PropertyEditor, StatusBar, ToolWidgets, UnrealEd, UMGEditor, UMG, AssetTools, ContentBrowser, Kismet, InputCore and Slate/SlateCore in the
 * editor module's dependencies (ToolWidgets owns FWidgetDrawerConfig; StatusBarSubsystem.h only
 * forward-declares it).
 */
class FWidgetAnimBuilderToolbar
{
public:

	/** Call from the module's UToolMenus startup callback. */
	static void Register();

	/**
	 * Takes the tab and layout hooks off UMG again. Optional - call it from the module's ShutdownModule if the
	 * module can be unloaded while the editor runs; the toolbar extension is cleaned up by UToolMenus itself.
	 */
	static void Unregister();

	/**
	 * Docks the Widget Blueprint's editor's Widget Animation Scripting tab beside its Details panel - or brings it forward,
	 * open already. Null when the Widget Blueprint has no editor open, or the tab couldn't be placed.
	 */
	static TSharedPtr<SDockTab> DockScripting(const UWidgetBlueprint* WidgetBlueprint);

	/** Whether the Widget Blueprint's editor has its Widget Animation Scripting tab open. */
	static bool IsScriptingDocked(const UWidgetBlueprint* WidgetBlueprint);

	/**
	 * Opens Animation in the Widget Blueprint's editor's Widget Animations panel - Edit Existing Asset - and docks the panel,
	 * or brings it forward. What double-clicking a state in the State Machine editor does. False when the Widget Blueprint has
	 * no editor open, or its panel can't edit an asset (a builder's).
	 */
	static bool OpenAnimationInPanel(const UWidgetBlueprint* WidgetBlueprint, UWidgetAnimPresetAsset* Animation);
};