// Copyright AR GameSystems 2026. All rights reserved.

using UnrealBuildTool;

// Registers the plugin's Shaders folder as the virtual shader path /Plugin/NameTBDAI, so the Custom nodes of the AI's UI
// materials can include NameTBDUI.ush (safe maths, noise, shapes). A mapping must be added while the engine starts
// (LoadingPhase PostConfigInit), before the AI module loads, so it is its own small module. UncookedOnly: shaders are
// compiled by the editor and the cooker, never by a packaged game.
public class NameTBDShaders : ModuleRules
{
	public NameTBDShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Projects",    // IPluginManager - where the plugin is
				"RenderCore"   // AddShaderSourceDirectoryMapping
			}
		);
	}
}
