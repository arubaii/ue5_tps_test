using System.IO;
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
				"EnhancedInput",
				"RHI",              // menus: the GPU vendor and name, frame times (benchmark, stat graph), ray tracing support
				"EngineSettings",   // menus: the project version for System Info
				"ApplicationCore"   // menus: FInputDeviceScope (which gamepad family sent the input)
			}
		);

		// nanosvg (Source/ThirdParty/nanosvg, header-only): SVGs are drawn with it (WidgetSVGRaster.cpp), in games too.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ThirdParty", "nanosvg"));

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