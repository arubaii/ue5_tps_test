using UnrealBuildTool;

public class tps_testEditor : ModuleRules
{
	public tps_testEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public dependencies should only be things that other modules 
		// inheriting from this editor module absolutely must see in headers.
		PublicDependencyModuleNames.AddRange(new string[] { 
			"Core", 
			"CoreUObject", 
			"Engine", 
		});

		// Keep editor-only workflows and your runtime game module private
		PrivateDependencyModuleNames.AddRange(new string[] { 
			"tps_test", // Moved here to prevent cyclic dependency issues
			"UnrealEd", 
			"BlueprintGraph", 
			"KismetCompiler", 
			"GraphEditor", 
			"Slate", 
			"SlateCore", 
			"InputCore", 
			"PropertyEditor" 
		});
	}
}