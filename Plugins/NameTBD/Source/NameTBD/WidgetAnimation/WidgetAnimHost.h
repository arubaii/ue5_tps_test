// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetAnimHost.generated.h"

UENUM(BlueprintType)
enum class EWidgetAnimPlayDirection : uint8
{
	Forward UMETA(ToolTip = "Plays the widget's Anim In entries."),
	Reverse UMETA(ToolTip = "Plays the widget's Anim Out entries, which includes anything in Anim In marked Mirror In Out.")
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UWidgetAnimHost : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything that owns Anim In / Anim Out entries and can play them by name.
 *
 * Implemented by UUserWidget_Animatable, UContextMenuButton and UWidgetAnimPreviewBox. The point of the
 * interface is that Play Widget Animation is ONE node rather than three, and a Blueprint calling it does
 * not have to know or care which of the three it is pointing at.
 */
class NAMETBD_API IWidgetAnimHost
{
	GENERATED_BODY()

public:

	/** An empty ID list plays every entry in the chosen direction. */
	virtual void PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying) = 0;

	virtual void StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart) = 0;

	virtual bool AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const = 0;
};

UCLASS()
class NAMETBD_API UWidgetAnimPlayLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Plays the animations with these Anim IDs on Target.
	 *
	 * Anim ID is the optional name on each Anim In / Anim Out entry. Leave Anim IDs empty to play the
	 * whole stack, which is the same thing the widget does for itself on construct.
	 *
	 * Hold Final Pose is off by default, so a named one-shot - a press bounce, an alert flash - returns
	 * the widget to normal when it ends. Turn it on for anything that is meant to leave the widget
	 * somewhere else, which is what an entrance does.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation", meta = (DefaultToSelf = "Target", AutoCreateRefTerm = "AnimIds", AdvancedDisplay = "PlaybackSpeed,bHoldFinalPose,bRestartIfPlaying,bSearchParentWidgets"))
	static void PlayWidgetAnimation(
		UObject* Target,
		const TArray<FName>& AnimIds,
		EWidgetAnimPlayDirection Direction = EWidgetAnimPlayDirection::Forward,
		float PlaybackSpeed = 1.f,
		bool bHoldFinalPose = false,
		bool bRestartIfPlaying = true,
		bool bSearchParentWidgets = true);

	/**
	 * Stops the animations with these Anim IDs. An empty list stops everything on Target.
	 *
	 * Reset To Start snaps the affected widgets back to where they were before the animation began;
	 * leaving it off holds them wherever the animation had got to.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation", meta = (DefaultToSelf = "Target", AutoCreateRefTerm = "AnimIds", AdvancedDisplay = "bSearchParentWidgets"))
	static void StopWidgetAnimation(UObject* Target, const TArray<FName>& AnimIds, bool bResetToStart = false, bool bSearchParentWidgets = true);

	/** True while any animation with these Anim IDs is still running. An empty list asks about all of them. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation", meta = (DefaultToSelf = "Target", AutoCreateRefTerm = "AnimIds"))
	static bool IsWidgetAnimationPlaying(UObject* Target, const TArray<FName>& AnimIds, bool bSearchParentWidgets = true);

private:

	/**
	 * Target as an animation host.
	 *
	 * With bSearchParentWidgets, a target that isn't one is walked outwards until a widget that is turns
	 * up. That makes the node work when it's called with Self from inside a plain child widget, which is
	 * the shape of most Blueprint graphs that want it.
	 */
	static IWidgetAnimHost* ResolveHost(UObject* Target, bool bSearchParentWidgets);
};