using UnrealBuildTool;

public class NameTBD : ModuleRules
{
	public NameTBD(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"UMG",
				"GameplayTags",
				"DeveloperSettings"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"RenderCore",
				"EnhancedInput"
			}
		);

		// Editor-only: Widget Anim Builder's Create Widget Animation button creates, saves and selects assets.
		// Kept out of game builds, where that code is compiled out by WITH_EDITOR.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"AssetTools"
				}
			);
		}
	}
}