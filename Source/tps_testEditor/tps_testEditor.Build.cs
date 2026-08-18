using UnrealBuildTool;

public class tps_testEditor : ModuleRules
{
	public tps_testEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"tps_test"        // the runtime module whose structs we customize
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"BlueprintGraph",
			"KismetCompiler",
			"GraphEditor",
			"Slate",
			"SlateCore",
			"InputCore",
			"PropertyEditor"   // FCategorizedStructCustomization needs this
		});
	}
}