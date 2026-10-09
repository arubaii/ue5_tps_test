// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetAnimDriver.h" // FWidgetAnimValueId
#include "WidgetAnimParameters.h" // FWidgetAnimParameterValue

#include "WidgetAnimHost.generated.h"

class UWidgetAnimationComponent;
class UWidgetAnimPresetAsset;

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
	 * Sets the value of the value-driven tracks Id names - see WidgetAnimDriver.h - starting the entries that have one
	 * but haven't played yet when bStartIfNotPlaying. Value is raw, up to four channels. True if a running track took
	 * it. Not pure, so a host with no value-driven tracks needn't implement it: the default takes nothing.
	 */
	virtual bool SetWidgetAnimationValues(const FWidgetAnimValueId& /*Id*/, const FVector4& /*Value*/, bool /*bSnap*/, bool /*bStartIfNotPlaying*/) { return false; }

	/** The raw value a value-driven track shows now, or with bTargetValue the one it is heading to. */
	virtual FVector4 GetWidgetAnimationValues(const FWidgetAnimValueId& /*Id*/, bool /*bTargetValue*/) const { return FVector4(0.0, 0.0, 0.0, 0.0); }
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
	 * Play Widget Animation, with values for the animations' parameters (see WidgetAnimParameters.h): each value goes to the
	 * parameter of its name in every animation started, over the entry's own and whatever Set Widget Animation Parameter set.
	 * Make them with the Make Animation Parameter nodes. Target is a widget with a Widget Animation Component, the component,
	 * or any animation host, found as Set Widget Animation Value finds it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Parameters", meta = (DefaultToSelf = "Target", AutoCreateRefTerm = "AnimIds,Parameters", AdvancedDisplay = "PlaybackSpeed,bHoldFinalPose,bRestartIfPlaying,bSearchParentWidgets"))
	static void PlayWidgetAnimationWithParameters(
		UObject* Target,
		const TArray<FName>& AnimIds,
		const TArray<FWidgetAnimParameterValue>& Parameters,
		EWidgetAnimPlayDirection Direction = EWidgetAnimPlayDirection::Forward,
		float PlaybackSpeed = 1.f,
		bool bHoldFinalPose = false,
		bool bRestartIfPlaying = true,
		bool bSearchParentWidgets = true);

	/**
	 * Sets an animation parameter on Target's Widget Animation Component (see WidgetAnimParameters.h) for the uses of Animation
	 * (every animation when none) with Anim ID (any when None), over each use's own value - taken by every animation it starts
	 * from now on, Play In / Out, Play Widget Animation and its state machines' states alike. One already playing keeps the
	 * values it started with: play it again to see the new one. True when a component took it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Parameters", meta = (DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static bool SetWidgetAnimationParameter(UObject* Target, const FWidgetAnimParameterValue& Value, UWidgetAnimPresetAsset* Animation = nullptr, FName AnimId = NAME_None, bool bSearchParentWidgets = true);

	/** Lets go of the values Set Widget Animation Parameter set on Target for Animation and Anim ID - every one when both are unset. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Parameters", meta = (DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static void ClearWidgetAnimationParameters(UObject* Target, UWidgetAnimPresetAsset* Animation = nullptr, FName AnimId = NAME_None, bool bSearchParentWidgets = true);

	/**
	 * The value an animation parameter plays with on Target: as Set Widget Animation Parameter set it, else as the entry of
	 * Animation (any when none) with Anim ID (any when None) sets it, else its default. False when no animation there has it.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Parameters", meta = (DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static bool GetWidgetAnimationParameter(UObject* Target, FName Name, FWidgetAnimParameterValue& OutValue, UWidgetAnimPresetAsset* Animation = nullptr, FName AnimId = NAME_None, bool bSearchParentWidgets = true);

	/**
	 * Stops the animations with these Anim IDs. An empty list stops everything on Target.
	 *
	 * Reset To Start snaps the affected widgets back to where they were before the animation began;
	 * leaving it off holds them wherever the animation had gotten to.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation", meta = (DefaultToSelf = "Target", AutoCreateRefTerm = "AnimIds", AdvancedDisplay = "bSearchParentWidgets"))
	static void StopWidgetAnimation(UObject* Target, const TArray<FName>& AnimIds, bool bResetToStart = false, bool bSearchParentWidgets = true);

	/** True while any animation with these Anim IDs is still running. An empty list asks about all of them. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation", meta = (DefaultToSelf = "Target", AutoCreateRefTerm = "AnimIds"))
	static bool IsWidgetAnimationPlaying(UObject* Target, const TArray<FName>& AnimIds, bool bSearchParentWidgets = true);

	/**
	 * Sets the value of the value-driven tracks with this Value Name (Timing = Value in the Widget Animations editor),
	 * in every animation that has one. The Set Widget Animation Value node from the Widget Animation category instead
	 * picks the animation and the value from dropdowns, and its Value pin takes the track's own type. Each track maps
	 * the value onto itself through its Value At Start and Value At End - 0 and 1 by default - and travels there by its
	 * own smoothing. Call it whenever the value changes.
	 *
	 * Target is a widget with a Widget Animation Component, the component itself, or any animation host (a button, a
	 * card, an animatable widget); from a plain child widget the search goes outwards until it finds one. Snap jumps
	 * straight to the value (and may go back even where a track never goes backwards - for resetting). Start If Not
	 * Playing starts the entries that have such a track but haven't played yet, so no Play call is needed. True if a
	 * running track took the value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (DisplayName = "Set Widget Animation Value By Name", DefaultToSelf = "Target", AdvancedDisplay = "bSnap,bStartIfNotPlaying,bSearchParentWidgets"))
	static bool SetWidgetAnimationValue(UObject* Target, FName ValueName, float Value, bool bSnap = false, bool bStartIfNotPlaying = true, bool bSearchParentWidgets = true);

	/** Set Widget Animation Value, for tracks whose Value Type is Integer. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (DisplayName = "Set Widget Animation Value By Name (Integer)", DefaultToSelf = "Target", AdvancedDisplay = "bSnap,bStartIfNotPlaying,bSearchParentWidgets"))
	static bool SetWidgetAnimationValueInteger(UObject* Target, FName ValueName, int32 Value, bool bSnap = false, bool bStartIfNotPlaying = true, bool bSearchParentWidgets = true);

	/** Set Widget Animation Value, for tracks whose Value Type is Vector 2D: X and Y each move their own axis. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (DisplayName = "Set Widget Animation Value By Name (Vector 2D)", DefaultToSelf = "Target", AdvancedDisplay = "bSnap,bStartIfNotPlaying,bSearchParentWidgets"))
	static bool SetWidgetAnimationValueVector2D(UObject* Target, FName ValueName, FVector2D Value, bool bSnap = false, bool bStartIfNotPlaying = true, bool bSearchParentWidgets = true);

	/** Set Widget Animation Value, for tracks whose Value Type is Color: R, G, B and A each move their own channel. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (DisplayName = "Set Widget Animation Value By Name (Color)", DefaultToSelf = "Target", AdvancedDisplay = "bSnap,bStartIfNotPlaying,bSearchParentWidgets"))
	static bool SetWidgetAnimationValueColor(UObject* Target, FName ValueName, FLinearColor Value, bool bSnap = false, bool bStartIfNotPlaying = true, bool bSearchParentWidgets = true);

	// What the Set Widget Animation Value node compiles to: the value in one animation (Animation) or in the
	// additive tracks (Animation none, bAdditiveTracks), typed, set through SetScopedValueOn. Not for calling by
	// hand - the node sets them up.
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Target"))
	static bool SetScopedValueFloat(UObject* Target, UWidgetAnimPresetAsset* Animation, bool bAdditiveTracks, FName ValueName, double Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Target"))
	static bool SetScopedValueInteger(UObject* Target, UWidgetAnimPresetAsset* Animation, bool bAdditiveTracks, FName ValueName, int32 Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Target"))
	static bool SetScopedValueVector2D(UObject* Target, UWidgetAnimPresetAsset* Animation, bool bAdditiveTracks, FName ValueName, FVector2D Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches);

	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Value", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Target"))
	static bool SetScopedValueColor(UObject* Target, UWidgetAnimPresetAsset* Animation, bool bAdditiveTracks, FName ValueName, FLinearColor Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches);

	/** The value a value-driven track shows now - or, with Target Value, the one it is heading to - in its own units. Found like Set Widget Animation Value. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Value", meta = (DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static float GetWidgetAnimationValue(UObject* Target, FName ValueName, bool bTargetValue = false, bool bSearchParentWidgets = true);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Integer)", DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static int32 GetWidgetAnimationValueInteger(UObject* Target, FName ValueName, bool bTargetValue = false, bool bSearchParentWidgets = true);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Vector 2D)", DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static FVector2D GetWidgetAnimationValueVector2D(UObject* Target, FName ValueName, bool bTargetValue = false, bool bSearchParentWidgets = true);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Value", meta = (DisplayName = "Get Widget Animation Value (Color)", DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static FLinearColor GetWidgetAnimationValueColor(UObject* Target, FName ValueName, bool bTargetValue = false, bool bSearchParentWidgets = true);

	/**
	 * Sets how far along the percentage animations played by the entries with this Anim ID are, 0 (0%) to 1 (100%) - a
	 * press-and-hold's held share, a charge level. Every track of the animation follows it, travelling there by the
	 * animation's Smoothing; call it as often as the progress changes. An Anim ID of None sets every percentage animation
	 * on Target. See EWidgetAnimTimeUnit.
	 *
	 * Target is found as Set Widget Animation Value finds it. Snap jumps straight to it, even backwards; Start If Not
	 * Playing starts the Anim In entries with that Anim ID that haven't played yet, so no Play call is needed. An entry
	 * playing over its Progress Animation Duration carries on from wherever it has got to. True if an animation took it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Progress", meta = (DisplayName = "Set Widget Animation Progress By Id", DefaultToSelf = "Target", AdvancedDisplay = "bSnap,bStartIfNotPlaying,bSearchParentWidgets"))
	static bool SetWidgetAnimationProgress(UObject* Target, FName AnimId, float Progress, bool bSnap = false, bool bStartIfNotPlaying = true, bool bSearchParentWidgets = true);

	/** How far along the percentage animation an entry with this Anim ID plays is now, 0 - 1 - or, with Target Progress, where it is heading. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Progress", meta = (DisplayName = "Get Widget Animation Progress By Id", DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static float GetWidgetAnimationProgress(UObject* Target, FName AnimId, bool bTargetProgress = false, bool bSearchParentWidgets = true);

	/**
	 * What the Set Widget Animation Progress node compiles to: the progress of the entries playing Animation (every
	 * percentage animation with none), set through SetScopedValueOn. Not for calling by hand - the node sets it up.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Progress", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "Target"))
	static bool SetScopedProgress(UObject* Target, UWidgetAnimPresetAsset* Animation, double Progress, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches);

	/**
	 * How far along Animation is on Target, 0 - 1, as its entry plays it - or, with Target Progress, where it is heading.
	 * Picked like the Set Widget Animation Progress node's animation; Get Widget Animation Progress By Id goes by Anim ID.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Progress", meta = (DefaultToSelf = "Target", AdvancedDisplay = "bSearchParentWidgets"))
	static float GetWidgetAnimationProgressOf(UObject* Target, UWidgetAnimPresetAsset* Animation, bool bTargetProgress = false, bool bSearchParentWidgets = true);

	/** The setters' shared path, raw: whatever owns Target's animations takes Value. */
	static bool SetValueOn(UObject* Target, const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets);

	/**
	 * The node's path: the Widget Animation Components on Target and nested under it (into child widgets and nested
	 * User Widgets, in tree order) that have the value take it - the first found, or with bSetAllMatches every one, for
	 * a screen holding many of the same widget. When none of them has it, SetValueOn's search: Target's own owner,
	 * then outwards with bSearchParentWidgets.
	 */
	static bool SetScopedValueOn(UObject* Target, const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying, bool bSearchParentWidgets, bool bSetAllMatches);

	/** The getters' shared path, raw. */
	static FVector4 GetValueFrom(UObject* Target, const FWidgetAnimValueId& Id, bool bTargetValue, bool bSearchParentWidgets);

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