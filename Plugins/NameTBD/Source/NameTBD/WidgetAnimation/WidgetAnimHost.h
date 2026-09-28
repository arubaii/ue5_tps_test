// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetAnimHost.generated.h"

class UWidgetAnimationComponent;

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

	/**
	 * Sets the value of the value-driven tracks with this Value Name - see WidgetAnimDriver.h - starting the entries that
	 * have one but haven't played yet when bStartIfNotPlaying. Value is raw, up to four channels. True if a running
	 * track took it. Not pure, so a host with no value-driven tracks needn't implement it: the default takes nothing.
	 */
	virtual bool SetWidgetAnimationValues(FName /*ValueName*/, const FVector4& /*Value*/, bool /*bSnap*/, bool /*bStartIfNotPlaying*/) { return false; }

	/** The raw value a value-driven track shows now, or with bTargetValue the one it is heading to. */
	virtual FVector4 GetWidgetAnimationValues(FName /*ValueName*/, bool /*bTargetValue*/) const { return FVector4(0.0, 0.0, 0.0, 0.0); }
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

	/**
	 * Sets the value of the value-driven tracks with this Value Name (Timing = Value in the Widget Animations editor).
	 * Each track maps it onto itself through its Value At Start and Value At End - 0 and 1 by default - and travels
	 * there by its own smoothing. Call it whenever the value changes.
	 *
	 * Target is a widget with a Widget Animation Component, the component itself, or any animation host (a button, a
	 * card, an animatable widget); from a plain child widget the search goes outwards until it finds one. Snap jumps
	 * straight to the value (and may go back even where a track never goes backwards - for resetting). Start If Not
	 * Playing starts the entries that have such a track but haven't played yet, so no Play call is needed. True if a
	 * running track took the value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (DefaultToSelf = "Target", AdvancedDisplay = "bSnap,bStartIfNotPlaying,bSearchParentWidgets"))
	static bool SetWidgetAnimationValue(UObject* Target, FName ValueName, float Value, bool bSnap = false, bool bStartIfNotPlaying = true, bool bSearchParentWidgets = true);

	/** Set Widget Animation Value, for tracks whose Value Type is Integer. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (DisplayName = "Set Widget Animation Value (Integer)", DefaultToSelf = "Target", AdvancedDisplay = "bSnap,bStartIfNotPlaying,bSearchParentWidgets"))
	static bool SetWidgetAnimationValueInteger(UObject* Target, FName ValueName, int32 Value, bool bSnap = false, bool bStartIfNotPlaying = true, bool bSearchParentWidgets = true);

	/** Set Widget Animation Value, for tracks whose Value Type is Vector 2D: X and Y each move their own axis. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (DisplayName = "Set Widget Animation Value (Vector 2D)", DefaultToSelf = "Target", AdvancedDisplay = "bSnap,bStartIfNotPlaying,bSearchParentWidgets"))
	static bool SetWidgetAnimationValueVector2D(UObject* Target, FName ValueName, FVector2D Value, bool bSnap = false, bool bStartIfNotPlaying = true, bool bSearchParentWidgets = true);

	/** Set Widget Animation Value, for tracks whose Value Type is Colour: R, G, B and A each move their own channel. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (DisplayName = "Set Widget Animation Value (Colour)", DefaultToSelf = "Target", AdvancedDisplay = "bSnap,bStartIfNotPlaying,bSearchParentWidgets"))
	static bool SetWidgetAnimationValueColor(UObject* Target, FName ValueName, FLinearColor Value, bool bSnap = false, bool bStartIfNotPlaying = true, bool bSearchParentWidgets = true);

	/** The value a value-driven track shows now - or, with Target Value, the one it is heading to - in its own units. Found like Set Widget Animation Value. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Value", meta = (DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static float GetWidgetAnimationValue(UObject* Target, FName ValueName, bool bTargetValue = false, bool bSearchParentWidgets = true);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Integer)", DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static int32 GetWidgetAnimationValueInteger(UObject* Target, FName ValueName, bool bTargetValue = false, bool bSearchParentWidgets = true);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Vector 2D)", DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static FVector2D GetWidgetAnimationValueVector2D(UObject* Target, FName ValueName, bool bTargetValue = false, bool bSearchParentWidgets = true);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Colour)", DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static FLinearColor GetWidgetAnimationValueColor(UObject* Target, FName ValueName, bool bTargetValue = false, bool bSearchParentWidgets = true);

	/** The four setters' shared path, raw: whatever owns Target's animations takes Value. */
	static bool SetValueOn(UObject* Target, FName ValueName, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets);

	/** The four getters' shared path, raw. */
	static FVector4 GetValueFrom(UObject* Target, FName ValueName, bool bTargetValue, bool bSearchParentWidgets);

private:

	/**
	 * Target as an animation host.
	 *
	 * With bSearchParentWidgets, a target that isn't one is walked outwards until a widget that is turns
	 * up. That makes the node work when it's called with Self from inside a plain child widget, which is
	 * the shape of most Blueprint graphs that want it.
	 */
	static IWidgetAnimHost* ResolveHost(UObject* Target, bool bSearchParentWidgets);

	/**
	 * Whatever owns Target's animations, for the value calls: the component itself, a widget's Widget Animation Component,
	 * or an animation host - checked in that order on Target and then, with bSearchParentWidgets, outwards. One of the
	 * two outputs is set when it returns true.
	 */
	static bool ResolveValueOwner(UObject* Target, bool bSearchParentWidgets, UWidgetAnimationComponent*& OutComponent, IWidgetAnimHost*& OutHost);
};