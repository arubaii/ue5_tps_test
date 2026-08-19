// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/CRRecoilSpreadComponent.h"
#include "Item/Structs/Definition.h"
#include "WeaponRecoilComponent.generated.h"

/**
 * Thin C++ subclass of UCRRecoilSpreadComponent that lets native code drive the whole
 * recoil profile from data.
 *
 * WHY THIS EXISTS: the three spread curves (ShotToHeatCurve, HeatToSpreadAngleCurve,
 * HeatToCooldownPerSecondCurve) are PROTECTED in the plugin — BlueprintReadWrite, but
 * unreachable from outside C++. A subclass can touch them, so this class is the seam
 * that turns an FWeaponRecoil struct into plugin state without forking the plugin.
 *
 * USAGE: add this component to BP_ThirdPersonCharacter INSTEAD of the plain
 * "Recoil Component (with Spread)". It is a drop-in — everything the base class does
 * still works. UWeaponComponent finds it automatically on the owning character.
 *
 * EXTENDING: when you add a field to FWeaponRecoil, push it in ApplyRecoilProfile.
 * That one function is the only place row/instance recoil data meets the plugin.
 *
 * NOT REPLICATED: recoil is a local-player camera effect. The plugin already early-outs
 * unless the target controller is a local player controller.
 */
UCLASS(ClassGroup = (Weapons), meta = (BlueprintSpawnableComponent), DisplayName = "Weapon Recoil Component")
class TPS_TEST_API UWeaponRecoilComponent : public UCRRecoilSpreadComponent
{
	GENERATED_BODY()

public:
	/**
	 * Push a complete recoil profile into the plugin. Call on equip, whenever the
	 * instance's ModifiedWeaponRecoil changes (attachment/modifier applied), and before
	 * StartShooting so movement-state multipliers are current.
	 *
	 * @param profile          the equipped weapon's FWeaponRecoil (instance-modified copy)
	 * @param state_multiplier  movement/stance scalar (moving, in-air, ADS, crouched...).
	 *                         Scales visual kick and spread so the feel matches the
	 *                         server's authoritative spread.
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon Recoil")
	void ApplyRecoilProfile(const FWeaponRecoil& profile, float state_multiplier = 1.f);

	/** Set the three spread curves directly (the reason this subclass exists). */
	UFUNCTION(BlueprintCallable, Category = "Weapon Recoil")
	void SetSpreadCurves(const FRuntimeFloatCurve& in_shot_to_heat,
	                     const FRuntimeFloatCurve& in_heat_to_spread_angle,
	                     const FRuntimeFloatCurve& in_heat_to_cooldown_per_second);

	/** Zero the accumulated heat (weapon swap, respawn, etc.). */
	UFUNCTION(BlueprintCallable, Category = "Weapon Recoil")
	void ResetRecoilHeat();

	/** True once all three curves are set — spread queries are meaningless before that. */
	UFUNCTION(BlueprintPure, Category = "Weapon Recoil")
	bool IsRecoilProfileReady() const;
	
	/** Public passthrough: UCRRecoilSpreadComponent re-declares ApplyShot as protected,
	so outside callers (UWeaponComponent) can't reach it. Fire one shot's recoil. */
	UFUNCTION(BlueprintCallable, Category = "Weapon Recoil")
	void FireShotRecoil();
	
	/**
	 * Begin a firing sequence. bRewindPattern = start the pattern from index 0;
	 * false = continue the pattern, but still refresh the per-press recovery-tracking
	 * state (which StartShooting would otherwise be responsible for clearing).
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon Recoil")
	void BeginFireSequence(bool b_rewind_pattern);
};