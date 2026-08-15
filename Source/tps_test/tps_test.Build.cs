// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class tps_test : ModuleRules
{
	public tps_test(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"DeveloperSettings",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"NetCore",	
			"GameplayTags",		// FGameplayTag in FModifierPayload
			"CrystalRecoil"		// UCRRecoilPattern in FWeaponRecoil
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"tps_test",
			"tps_test/Variant_Platforming",
			"tps_test/Variant_Platforming/Animation",
			"tps_test/Variant_Combat",
			"tps_test/Variant_Combat/AI",
			"tps_test/Variant_Combat/Animation",
			"tps_test/Variant_Combat/Gameplay",
			"tps_test/Variant_Combat/Interfaces",
			"tps_test/Variant_Combat/UI",
			"tps_test/Variant_SideScrolling",
			"tps_test/Variant_SideScrolling/AI",
			"tps_test/Variant_SideScrolling/Gameplay",
			"tps_test/Variant_SideScrolling/Interfaces",
			"tps_test/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
