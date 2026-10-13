// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DiegeticUILibrary.generated.h"

class AActor;
class APlayerController;
class UDiegeticInteractorComponent;
class UDiegeticScreenComponent;
class UWidget;

/** Diegetic UI from a widget's own Blueprint, or from gameplay. See DiegeticTypes.h. */
UCLASS()
class NAMETBD_API UDiegeticUILibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** The Diegetic Screen this widget is shown on. None on the HUD. */
	UFUNCTION(BlueprintPure, Category = "Diegetic UI", meta = (DefaultToSelf = "OnScreen"))
	static UDiegeticScreenComponent* GetDiegeticScreen(const UWidget* OnScreen);

	/** Whether this widget is shown on a Diegetic Screen (in the world) rather than on the HUD. */
	UFUNCTION(BlueprintPure, Category = "Diegetic UI", meta = (DefaultToSelf = "OnScreen"))
	static bool IsOnDiegeticScreen(const UWidget* OnScreen);

	/**
	 * Something on the widget's screen changed that it can't see for itself - text set, an image swapped: a When Changed
	 * screen redraws. Does nothing on the HUD, so a widget used in both places can always call it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Diegetic UI", meta = (DefaultToSelf = "OnScreen"))
	static void MarkDiegeticContentChanged(const UWidget* OnScreen);

	/** Where a widget on a Diegetic Screen is, in the world - to spawn an effect at a button. Its own place on the HUD: zero. */
	UFUNCTION(BlueprintPure, Category = "Diegetic UI", meta = (DefaultToSelf = "OnScreen"))
	static FVector GetDiegeticWidgetWorldLocation(const UWidget* OnScreen);

	/** The player's Diegetic Interactor - on their pawn, added on its own unless Project Settings > Diegetic UI says otherwise. */
	UFUNCTION(BlueprintPure, Category = "Diegetic UI")
	static UDiegeticInteractorComponent* GetDiegeticInteractor(const APlayerController* Player);

	/** Has Player use Screen, as a click on a Focus View screen does. */
	UFUNCTION(BlueprintCallable, Category = "Diegetic UI")
	static bool BeginUsingDiegeticScreen(APlayerController* Player, UDiegeticScreenComponent* Screen);

	/** Stops Player using whatever screen they use. */
	UFUNCTION(BlueprintCallable, Category = "Diegetic UI")
	static void EndUsingDiegeticScreen(APlayerController* Player);

	/** The screen Player uses now. None when they use none. */
	UFUNCTION(BlueprintPure, Category = "Diegetic UI")
	static UDiegeticScreenComponent* GetUsedDiegeticScreen(const APlayerController* Player);

	/**
	 * The object whose data this widget's screen shows - Set Screen Context's, else its Context Actor, else the screen's own actor
	 * (the weapon an ammo counter is on, the door a panel opens). None on the HUD.
	 */
	UFUNCTION(BlueprintPure, Category = "Diegetic UI", meta = (DefaultToSelf = "OnScreen"))
	static UObject* GetDiegeticScreenContext(const UWidget* OnScreen);

	/** The actor this widget's screen is on. None on the HUD. */
	UFUNCTION(BlueprintPure, Category = "Diegetic UI", meta = (DefaultToSelf = "OnScreen"))
	static AActor* GetDiegeticScreenOwner(const UWidget* OnScreen);

	/**
	 * Opens a screen as a menu: shows it (its widget plays in) and has Player use it - a hologram menu in front of the player, a
	 * wrist device. Close Diegetic Menu, or Back, ends it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Diegetic UI")
	static bool OpenDiegeticMenu(APlayerController* Player, UDiegeticScreenComponent* Screen);

	/** Closes a screen opened with Open Diegetic Menu: stops using it and hides it (its widget plays out first). */
	UFUNCTION(BlueprintCallable, Category = "Diegetic UI")
	static void CloseDiegeticMenu(UDiegeticScreenComponent* Screen);
};
