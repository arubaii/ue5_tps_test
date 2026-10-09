// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UNiagaraUIWidget;
struct FSlateBrush;

/**
 * What every Niagara Particles widget shares, started and stopped with the module:
 *
 *   - One ticker for them all, before the world ticks each frame: Pause When Hidden, and whether each repaints every frame
 *     (see UNiagaraUIWidget::TickRuntime). Only widgets that have made a simulation are on it.
 *   - Letting go of their simulations when the world those are registered with is cleaned up (a level change, the end of
 *     Play In Editor), so no world is kept alive by a widget that outlives it.
 *   - The brushes particles draw with: one per User Interface material, shared by every widget, and the soft dot drawn
 *     when a renderer's material can't draw in UI. A brush nothing has drawn with for a while is let go.
 *   - The console variables NiagaraUI.Enable and NiagaraUI.Log, and warnings logged once.
 */
namespace NiagaraUIRuntime
{
	void Startup();
	void Shutdown();

	/** Ticks Widget from the next frame on. Called once it has a simulation. */
	void Track(UNiagaraUIWidget* Widget);

	/** Stops ticking Widget. Not during garbage collection: a widget being destroyed simply drops off. */
	void Untrack(UNiagaraUIWidget* Widget);

	/** NiagaraUI.Enable: 0 stops every Niagara Particles widget simulating and drawing, for profiling. */
	bool IsEnabled();

	/** NiagaraUI.Log: 1 logs simulations made, let go, paused and resumed. */
	bool IsLogging();

	/** Logs Message as a warning the first time it is seen this run; the same message never again. */
	void WarnOnce(const FString& Message);

	/** Whether Material can be drawn by Slate: its Material Domain is User Interface. */
	bool IsUIMaterial(const UMaterialInterface* Material);

	/** The shared brush drawing with Material (a User Interface material). Null after shutdown. */
	TSharedPtr<FSlateBrush> GetMaterialBrush(UMaterialInterface* Material);

	/** The shared soft round dot, white, drawn when a renderer's material can't draw in UI. Null after shutdown. */
	TSharedPtr<FSlateBrush> GetDefaultParticleBrush();
}
