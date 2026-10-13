// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "DiegeticUISubsystem.generated.h"

class APawn;
class APlayerController;
class UDiegeticInteractorComponent;
class UDiegeticScreenComponent;

/**
 * Runs the Diegetic Screens of a game world (see DiegeticTypes.h), once a frame after the world has ticked:
 *
 * - Places screens that move themselves (Face Camera, In Front Of Player) for the first local player's view.
 * - Measures each screen's distance from the local players' cameras and pawns, and whether a camera is in front of it.
 * - Wakes and sleeps them (Presence).
 * - Schedules their redraws for the next frame: Every Frame screens and screens pointed at or in use always; the rest at the
 *   rate their distance asks for (When Changed: only while something on them moves), never while not seen recently or every
 *   camera is behind them - the most overdue first, at most Max Redraws Per Frame of them (Project Settings > Diegetic UI).
 * - Gives each local player's pawn a Diegetic Interactor, when the project asks for it and the pawn has no Widget Interaction
 *   Component of its own.
 *
 * Only in game and PIE worlds: in the editor's own world a Diegetic Screen redraws as the engine has it.
 *
 * Stats: stat NameTBDDiegetic. Console: NameTBD.Diegetic.Debug 1 (each screen's mode, distance, redraws a second, state),
 * NameTBD.Diegetic.Budget N (overrides Max Redraws Per Frame; -1 = the setting), NameTBD.Diegetic.Freeze 1 (no scheduled
 * redraws at all - what the scheduling saves).
 */
UCLASS()
class NAMETBD_API UDiegeticUISubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:

	/** The subsystem of WorldContext's world. Null outside a game world. */
	static UDiegeticUISubsystem* Get(const UObject* WorldContext);

	void RegisterScreen(UDiegeticScreenComponent* Screen);
	void UnregisterScreen(UDiegeticScreenComponent* Screen);
	const TArray<TWeakObjectPtr<UDiegeticScreenComponent>>& GetScreens() const { return Screens; }

	void RegisterInteractor(UDiegeticInteractorComponent* Interactor);
	void UnregisterInteractor(UDiegeticInteractorComponent* Interactor);

	/** The Diegetic Interactor belonging to Player. */
	UDiegeticInteractorComponent* FindInteractor(const APlayerController* Player) const;

	// UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

private:

	/** The local players' camera and pawn locations this frame. */
	void GatherViews();

	/** A Diegetic Interactor for each local player's pawn that has no Widget Interaction Component (Auto Add Interactor). */
	void AddInteractors();

	/** Measures, wakes / sleeps and schedules every screen. */
	void ScheduleScreens(double Now, float RealDeltaTime);

	void DrawDebug() const;

	TArray<TWeakObjectPtr<UDiegeticScreenComponent>> Screens;
	TArray<TWeakObjectPtr<UDiegeticInteractorComponent>> Interactors;

	TArray<FVector> ViewLocations;
	TArray<FVector> PawnLocations;

	/** The first local player's view: what Face Camera and In Front Of Player follow. */
	FVector FirstViewLocation = FVector::ZeroVector;
	FRotator FirstViewRotation = FRotator::ZeroRotator;
	bool bHasFirstView = false;

	/** The pawn each local player had when last looked at for an interactor: only a new one is looked at again. */
	TMap<TObjectKey<APlayerController>, TWeakObjectPtr<APawn>> CheckedPawns;
};
