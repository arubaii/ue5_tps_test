// Copyright AR GameSystems 2026. All rights reserved.

using UnrealBuildTool;

public class NameTBDEditor : ModuleRules
{
	public NameTBDEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"NameTBD"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"ApplicationCore",   // the clipboard, for cutting, copying and pasting tracks
			"UnrealEd",
			"PropertyEditor",
			"StatusBar",
			"ToolWidgets",       // FWidgetDrawerConfig, for the status bar drawers
			"BlueprintGraph",   // UK2Node_FunctionEntry, for adding the dispatcher's parameters
			"KismetCompiler",
			"UMG",
			"UMGEditor",        // UWidgetBlueprint, and the tab / layout hooks Dock in Layout uses
			"Kismet",           // FWidgetBlueprintApplicationMode's base, for registering the panel's tab
			"Projects",       
			"AssetTools",
			"ToolMenus",
			"DeveloperSettings", // UWidgetAnimEditorSettings - track colors and track controls
			"GraphEditor",       // SGraphPin, for the Set Widget Animation Value node's dropdown pins
			"NameTBDUncooked",   // that node
			"WebBrowser",        // the Script area's code editor (Monaco), shown in the editor's web browser
			"StructUtilsEditor", // FInstancedStructDataDetails, for a registered type's settings laid out field by field
			"AppFramework",      // the color picker, for the Text Editor's Color menu
		});
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"AssetTools",
					"AssetRegistry",
				}
			);
		}
	}
}