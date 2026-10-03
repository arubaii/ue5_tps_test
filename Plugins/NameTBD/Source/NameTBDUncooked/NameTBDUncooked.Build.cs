// Copyright AR GameSystems 2026. All rights reserved.

using UnrealBuildTool;

// Blueprint nodes of the library: the Set Widget Animation Value node, with its animation and value dropdowns. Nodes
// must live in an UncookedOnly module - loaded by the editor and the cooker, never shipped - so they are here, apart
// from the runtime NameTBD module their calls compile down to. Their dropdown pins are drawn by NameTBDEditor.
//
// Also the Widget Blueprint extension holding a Widget Blueprint's animation timelines (WidgetAnimTimelines.h): data
// saved with the Widget Blueprint, so its class must load wherever the Widget Blueprint does.
public class NameTBDUncooked : ModuleRules
{
	public NameTBDUncooked(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"BlueprintGraph",
				"NameTBD",
				"UMG",
				"UMGEditor"    // UWidgetBlueprint and its widget tree, and UWidgetBlueprintExtension - what the timelines are kept in
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"KismetCompiler",
				"UnrealEd",
				"Slate",
				"SlateCore"
			}
		);
	}
}
