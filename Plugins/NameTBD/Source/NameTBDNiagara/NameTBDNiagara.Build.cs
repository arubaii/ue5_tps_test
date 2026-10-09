// Copyright AR GameSystems 2026. All rights reserved.

using UnrealBuildTool;

// Niagara particles drawn in UI: the Niagara Particles widget, which simulates a Niagara System and draws its CPU
// sprites and ribbons straight into Slate, and the Particles cue that fires them from a Widget Animation. Kept out of
// the core NameTBD module so the library itself never depends on Niagara; only this module does.
public class NameTBDNiagara : ModuleRules
{
	public NameTBDNiagara(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"UMG",
				"Niagara",
				"NameTBD"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"NiagaraCore"   // FNiagaraID, for telling a ribbon emitter's ribbons apart
			}
		);

		// Editor-only: the Details panels are laid out again when a widget's User Parameters change rows (a system set or
		// changed). Kept out of game builds, where that code is compiled out by WITH_EDITOR.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("PropertyEditor");
		}
	}
}
