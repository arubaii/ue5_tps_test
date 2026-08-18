using UnrealBuildTool;

public class EditorFontSwap : ModuleRules
{
	public EditorFontSwap(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

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
			"ApplicationCore",
			"UnrealEd",
			"DeveloperSettings", // for UDeveloperSettings-based Project Settings page
			"Projects",          // to resolve plugin base dir
		});
	}
}
