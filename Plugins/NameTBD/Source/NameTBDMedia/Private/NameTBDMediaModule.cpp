// Copyright AR GameSystems 2026. All rights reserved.

#include "HAL/IConsoleManager.h"
#include "MediaBoxCue.h"
#include "MediaBoxLog.h"
#include "MediaBoxPool.h"
#include "MediaBoxSettings.h"
#include "Modules/ModuleManager.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEvaluator.h"

DEFINE_LOG_CATEGORY(LogMediaBox);

namespace MediaBoxRuntime
{
	static TAutoConsoleVariable<int32> CVarLog(
		TEXT("MediaBox.Log"),
		0,
		TEXT("1: log the Media Box warm pool - players opened, warmed, leased and closed, and every warm (instant) or cold start."));

	static TAutoConsoleVariable<int32> CVarMaxWarmPlayers(
		TEXT("MediaBox.MaxWarmPlayers"),
		-1,
		TEXT("How many players the Media Box warm pool keeps warm. Below 0: Project Settings > Plugins > Media Box > Max Warm Players."));

	static TOptional<bool> SubtitlesOverride;

	bool IsLogging()
	{
		return CVarLog.GetValueOnGameThread() != 0;
	}

	int32 GetMaxWarmPlayersOverride()
	{
		return CVarMaxWarmPlayers.GetValueOnGameThread();
	}

	bool AreSubtitlesEnabled()
	{
		return SubtitlesOverride.Get(UMediaBoxSettings::Get().bSubtitlesEnabled);
	}

	void SetSubtitlesEnabled(bool bEnabled)
	{
		SubtitlesOverride = bEnabled;
	}
}

/**
 * The Media Box module's setup: the Media cue, registered with the Widget Animations as any registered type is - listed under
 * Cues in the Add Track menu. The core library never depends on the Media Framework; it only meets this cue through the
 * registry. The Media Progress value input needs no registration (see WidgetAnimInput.h).
 */
class FNameTBDMediaModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		FWidgetAnimEvaluatorRegistry::Get().Register(MakeShared<FWidgetAnimMediaCueEvaluator>());
	}

	virtual void ShutdownModule() override
	{
		FWidgetAnimEvaluatorRegistry::Get().Unregister(FWidgetAnimMediaCueParams::StaticStruct());
		UMediaBoxPool::ShutdownEditorPool();
	}
};

IMPLEMENT_MODULE(FNameTBDMediaModule, NameTBDMedia)
