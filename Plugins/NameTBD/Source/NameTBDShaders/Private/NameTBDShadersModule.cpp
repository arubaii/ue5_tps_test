// Copyright AR GameSystems 2026. All rights reserved.

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

namespace
{
	const TCHAR* VirtualShaderFolder = TEXT("/Plugin/NameTBDAI");
}

/** Maps /Plugin/NameTBDAI to the plugin's Shaders folder (NameTBDUI.ush), once, if the folder is there. */
class FNameTBDShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD"));
		if (!Plugin.IsValid())
		{
			return;
		}
		const FString Folder = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		// Adding a path twice is a check() failure; without the folder the AI's materials just don't use the helpers.
		if (FPaths::DirectoryExists(Folder) && !AllShaderSourceDirectoryMappings().Contains(VirtualShaderFolder))
		{
			AddShaderSourceDirectoryMapping(VirtualShaderFolder, Folder);
		}
	}
};

IMPLEMENT_MODULE(FNameTBDShadersModule, NameTBDShaders)
