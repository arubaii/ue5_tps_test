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
			"UnrealEd",
			"PropertyEditor",
			"BlueprintGraph",   // UK2Node_FunctionEntry, for adding the dispatcher's parameters
			"KismetCompiler",
			"UMG",
			"UMGEditor",        // UWidgetBlueprint
			"Projects",       
			"AssetTools",
		});
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"AssetTools",
					"AssetRegistry"
				}
			);
		}
	}
}