// Copyright AR GameSystems 2026. All rights reserved.

using UnrealBuildTool;

// The MVVM integration: the Widget Data Binder component, which copies values from a gameplay object into a
// widget's ViewModel and plays Widget Animations when ViewModel fields cross a condition. Kept out of the core
// NameTBD module so the library itself never depends on the ModelViewViewModel plugin; only this module does.
public class NameTBDMVVM : ModuleRules
{
	public NameTBDMVVM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UMG",
				"FieldNotification",
				"ModelViewViewModel",
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
