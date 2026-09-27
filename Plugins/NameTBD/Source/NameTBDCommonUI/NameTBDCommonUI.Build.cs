// Copyright AR GameSystems 2026. All rights reserved.

using UnrealBuildTool;

// The Common UI integration: screens on an activatable-widget stack that play their Widget Animations as they
// open, return and close. Kept out of the core NameTBD module so the library itself never depends on Common UI;
// only this module does.
public class NameTBDCommonUI : ModuleRules
{
	public NameTBDCommonUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UMG",
				"CommonUI",
				"NameTBD"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore"
			}
		);
	}
}
