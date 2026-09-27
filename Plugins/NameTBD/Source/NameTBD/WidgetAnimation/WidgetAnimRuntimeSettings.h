// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "WidgetAnimRuntimeSettings.generated.h"

/**
 * Project-wide settings for widget animations, in Project Settings > Plugins > Widget Animations.
 *
 * Project settings rather than Editor Preferences because they are the team's, not one person's: the budget a
 * Blueprint track type has to stay within is a decision about the game, and everyone profiling it should be held to
 * the same one. The editor's own look and feel stays in UWidgetAnimEditorSettings.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Widget Animations"))
class NAMETBD_API UWidgetAnimRuntimeSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	static const UWidgetAnimRuntimeSettings& Get() { return *GetDefault<UWidgetAnimRuntimeSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/**
	 * Warn when a Blueprint track type is costing more than it should - averaging over the budget below, or running
	 * Blueprint on nearly every frame it plays. Each type is warned about once per session: in the Output Log, the
	 * PIE message log and an editor notification, and on its tracks' tooltips in the Widget Animations panel.
	 *
	 * Measured in development builds only. A Shipping build never measures, so it never pays for measuring.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Types", meta = (DisplayName = "Warn About Expensive Blueprint Types"))
	bool bWarnAboutExpensiveBlueprintTypes = true;

	/**
	 * How much Blueprint time one Blueprint type may spend per frame, on average across all its tracks, before it is
	 * warned about. The default leaves room for dozens of tracks that only call Blueprint when their value changes.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Types", meta = (EditCondition = "bWarnAboutExpensiveBlueprintTypes", ClampMin = "0.001", UIMin = "0.01", UIMax = "1.0", Units = "ms"))
	float BlueprintTypeBudgetMs = 0.05f;

	/**
	 * The share of frames a Blueprint type may run Blueprint on before it is warned about, whatever those calls cost.
	 * A type that calls Blueprint on nearly every frame has no Quantize Step, or a value that never settles; it is
	 * cheap with one track and expensive with fifty, so it is caught before it gets there. 1 turns this check off.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Types", meta = (EditCondition = "bWarnAboutExpensiveBlueprintTypes", ClampMin = "0.1", ClampMax = "1.0", DisplayName = "Every-Frame Call Threshold"))
	float EveryFrameCallThreshold = 0.9f;

	/**
	 * How long a Blueprint type's cost is averaged over before it is judged. Longer ignores brief spikes - the frame a
	 * menu opens - and takes longer to notice a real problem.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Blueprint Types", meta = (EditCondition = "bWarnAboutExpensiveBlueprintTypes", ClampMin = "0.25", UIMax = "5.0", Units = "s"))
	float MeasurementWindowSeconds = 1.f;
};