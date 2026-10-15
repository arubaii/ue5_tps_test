// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class APlayerController;
class ULocalPlayer;
class UWorld;

/** Finding whose menus these are, shared by the menu subsystems. */
namespace MenuPlayerUtils
{
	/**
	 * Player's local player; else the one owning Context (a widget's owning player, a controller, a pawn's controller, a
	 * component's owner, a local player itself); else the context world's first local player.
	 */
	NAMETBD_API ULocalPlayer* ResolveLocalPlayer(const UObject* Context, const APlayerController* Player = nullptr);

	/**
	 * The world a console command acts on: World when it has a local player, else the playing game's (a command typed in the
	 * editor's Output Log gets the editor world).
	 */
	NAMETBD_API UWorld* FindPlayingWorld(UWorld* World);

	/** Says on screen and in the log what a command did. */
	NAMETBD_API void Report(const FString& Message);
}
