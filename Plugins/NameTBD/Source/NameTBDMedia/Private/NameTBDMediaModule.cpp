// Copyright AR GameSystems 2026. All rights reserved.

#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
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

		// The editor's pool is closed while everything is still alive - its players' audio components live in worlds the engine
		// tears down before modules shut down.
		PreExitHandle = FCoreDelegates::OnEnginePreExit.AddStatic(&UMediaBoxPool::ShutdownEditorPool);
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::OnEnginePreExit.Remove(PreExitHandle);
		FWidgetAnimEvaluatorRegistry::Get().Unregister(FWidgetAnimMediaCueParams::StaticStruct());

		// A module unloaded while the editor runs (hot reload) closes its pool here; at exit, pre-exit already did.
		UMediaBoxPool::ShutdownEditorPool();
	}

private:
	FDelegateHandle PreExitHandle;
};

IMPLEMENT_MODULE(FNameTBDMediaModule, NameTBDMedia)
