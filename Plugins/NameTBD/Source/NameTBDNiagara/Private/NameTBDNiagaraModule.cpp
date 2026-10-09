// Copyright AR GameSystems 2026. All rights reserved.

#include "Modules/ModuleManager.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEvaluator.h"
#include "NiagaraUILog.h"
#include "NiagaraUIRuntime.h"
#include "WidgetAnimParticleCue.h"

DEFINE_LOG_CATEGORY(LogNiagaraUI);

/**
 * The Niagara module's setup: what every Niagara Particles widget shares (see NiagaraUIRuntime.h), and the Particles cue,
 * registered with the Widget Animations as any registered type is - listed under Cues in the Add Track menu. The core
 * library never depends on Niagara; it only meets this cue through the registry.
 */
class FNameTBDNiagaraModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		NiagaraUIRuntime::Startup();
		FWidgetAnimEvaluatorRegistry::Get().Register(MakeShared<FWidgetAnimParticleCueEvaluator>());
	}

	virtual void ShutdownModule() override
	{
		FWidgetAnimEvaluatorRegistry::Get().Unregister(FWidgetAnimParticleCueParams::StaticStruct());
		NiagaraUIRuntime::Shutdown();
	}
};

IMPLEMENT_MODULE(FNameTBDNiagaraModule, NameTBDNiagara)
