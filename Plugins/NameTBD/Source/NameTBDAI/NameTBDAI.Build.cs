// Copyright AR GameSystems 2026. All rights reserved.

using System.IO;
using UnrealBuildTool;

// The AI Assistant: one tool layer (widgets, Widget Animations, shapes, buttons, materials, Niagara UI systems, data
// binding, docs), shared by an in-editor chat panel (Window > NameTBD AI) and a local MCP server for external assistants.
// Editor-only and opt-in: never in a packaged game, and it sends nothing anywhere until the user gives it their own
// provider key or starts the MCP server. See NameTBDAISettings.h.
public class NameTBDAI : ModuleRules
{
	public NameTBDAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"ApplicationCore",      // the clipboard: Copy MCP Config
			"UnrealEd",
			"EditorFramework",
			"ToolMenus",
			"WorkspaceMenuStructure", // Window > NameTBD AI
			"PropertyEditor",
			"Settings",             // opening Editor Preferences > Plugins > NameTBD AI from the chat
			"Projects",             // the plugin's own folders (Resources/AI)
			"Json",
			"JsonUtilities",
			"HTTP",                 // the provider adapters
			"HTTPServer",           // the MCP server, on 127.0.0.1 only
			"Sockets",              // FInternetAddr, to refuse anything but loopback callers
			"AssetTools",
			"AssetRegistry",
			"ContentBrowser",
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",
			"UMG",
			"UMGEditor",
			"MaterialEditor",       // UMaterialEditingLibrary: UI materials built node by node
			"Niagara",
			"NiagaraCore",
			"NiagaraEditor",        // making a Niagara UI system from an emitter template
			"ImageCore",
			"ImageWrapper",
			"RenderCore",
			"RHI",                  // the feature level a material's compile errors are read at
			"NameTBD",
			"NameTBDEditor",        // the track scripting layer (WidgetAnimScripting) and track presets
			"NameTBDUncooked",
			"NameTBDMVVM",
			"NameTBDNiagara",
		});

		// The scripting layer and track presets are the editor module's own (Private) headers, exported for this module.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "NameTBDEditor", "Private"));

		// ...and their generated headers, in case this engine doesn't put a dependency's private UHT output on the path.
		PrivateIncludePaths.Add(Path.Combine(PluginDirectory, "Intermediate", "Build", Target.Platform.ToString(), "UnrealEditor", "Inc", "NameTBDEditor", "UHT"));
	}
}
