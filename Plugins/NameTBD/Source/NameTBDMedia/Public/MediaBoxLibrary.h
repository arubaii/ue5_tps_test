// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "MediaBoxLibrary.generated.h"

class UMediaBoxClip;
class UWidget;

/**
 * The Media Box warm pool from Blueprint: Warm Groups for buttons that change what a shared Media Box plays, clips kept warm by
 * hand, the subtitles option, and the pool's numbers. See MediaBoxTypes.h.
 */
UCLASS(meta = (DisplayName = "Media Box Library"))
class NAMETBDMEDIA_API UMediaBoxLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Puts Widget (a skill button) in Warm Group with the clip it plays when hovered or focused - for a screen whose buttons all
	 * play into one shared Media Box (Set Clip from On Hovered). The pool keeps the clips of the one focused and those nearest it
	 * (or nearest where the pointer is heading) warm. Media Boxes that play on their own hover join their Warm Group by themselves.
	 * Widget leaves the group when it is destroyed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Warm", meta = (DefaultToSelf = "Widget", Keywords = "video preload prewarm"))
	static void AddToMediaWarmGroup(UWidget* Widget, FName Group, UMediaBoxClip* Clip);

	UFUNCTION(BlueprintCallable, Category = "Media Box|Warm", meta = (DefaultToSelf = "Widget"))
	static void RemoveFromMediaWarmGroup(UWidget* Widget, FName Group);

	/** Empties Group - a screen closing. */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Warm", meta = (WorldContext = "WorldContextObject"))
	static void ClearMediaWarmGroup(UObject* WorldContextObject, FName Group);

	/** Tells Group that Widget is hovered or focused now, so its clip and its neighbours' are warmed first. Media Boxes do this themselves. */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Warm", meta = (DefaultToSelf = "Widget"))
	static void SetMediaWarmGroupFocus(UWidget* Widget, FName Group);

	/**
	 * Keeps Clip warm whatever the Warm Groups say (bKeepWarm), or stops doing so - the clip a menu plays as it opens, warmed
	 * during the loading screen. Counted: call it as many times off as on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Warm", meta = (WorldContext = "WorldContextObject", Keywords = "video preload prewarm"))
	static void KeepMediaClipWarm(UObject* WorldContextObject, UMediaBoxClip* Clip, bool bKeepWarm = true);

	/** Turns the subtitles every Media Box (with Show Subtitles) draws on or off - the game's subtitles option. Starts at Project Settings > Media Box. */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Subtitles")
	static void SetMediaBoxSubtitlesEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Media Box|Subtitles")
	static bool AreMediaBoxSubtitlesEnabled();

	/**
	 * The warm pool's numbers: players warm (ready to play at once) and playing now, and how many starts were warm (instant)
	 * or cold (waited for the video) since the game started. Cold starts that keep climbing: raise Max Warm Players, or put the
	 * screen's previews in one Warm Group.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media Box|Warm", meta = (WorldContext = "WorldContextObject"))
	static void GetMediaBoxPoolStats(UObject* WorldContextObject, int32& WarmPlayers, int32& PlayingPlayers, int32& WarmStarts, int32& ColdStarts);
};
