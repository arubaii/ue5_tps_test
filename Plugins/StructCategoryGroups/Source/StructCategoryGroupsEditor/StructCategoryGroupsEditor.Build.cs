// Copyright Bilcash. All rights reserved.

using UnrealBuildTool;

public class StructCategoryGroupsEditor : ModuleRules
{
	public StructCategoryGroupsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"PropertyEditor",
		});
	}
}