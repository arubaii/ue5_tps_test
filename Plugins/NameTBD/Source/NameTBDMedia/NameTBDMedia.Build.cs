// Copyright AR GameSystems 2026. All rights reserved.

using UnrealBuildTool;

// Video in UI: the Media Box widget, its clips, the warm player pool that lets a clip start the instant it is hovered, the
// Media cue and Media Progress input of the Widget Animations, and a transport bar base class. Kept out of the core NameTBD
// module so the library itself never depends on the Media Framework; only this module does.
public class NameTBDMedia : ModuleRules
{
	public NameTBDMedia(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UMG",
				"MediaAssets",
				"DeveloperSettings",
				"NameTBD"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"InputCore",    // EKeys, for Media Box With Controls' click to play / pause
				"Media",        // the Media Framework's interfaces under UMediaPlayer
				"AudioMixer",   // UMediaSoundComponent's base (USynthComponent)
				"RenderCore",   // FlushRenderingCommands, for Generate Poster
				"RHI"           // GMaxRHIFeatureLevel, for Generate Poster
			}
		);

		// Editor-only: Generate Poster saves a texture asset next to the clip. Kept out of game builds, where that code is
		// compiled out by WITH_EDITOR.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"AssetRegistry"
				}
			);
		}
	}
}
