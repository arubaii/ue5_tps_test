#pragma once

#include "CoreMinimal.h"
#include "WeaponTypes.generated.h"


class UCRRecoilPattern;
class UNiagaraSystem; 

/**
 * Weapon firing mode.
 * Replaces the Blueprint enum E_FireMode. Order preserved exactly.
 * Display names keep the spacing the Blueprint asset used ("Semi Auto", etc.).
 */
UENUM(BlueprintType)
enum class EFireMode : uint8
{
	SemiAuto UMETA(DisplayName = "Semi Auto"),
	Burst    UMETA(DisplayName = "Burst"),
	FullAuto UMETA(DisplayName = "Full Auto")
};


/**
 * Weapon fire stats. These are INSTANCE state: they are authored as the defaults inside a
 * FWeaponItemFragment on the weapon's row, copied onto each new instance, and then mutated
 * there by attachments and modifiers. Nothing reads them off the row directly.
 * Replaces Blueprint struct S_WeaponStats.
 *
 * NOTE: You mentioned you'll add per-instance jitter tunables here yourself
 * (HorizontalJitter / VerticalJitter). Add them below when ready.
 */
USTRUCT(BlueprintType)
struct FWeaponStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Damage = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MagSize = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ToolTip = "In RPM."))
	float FireRate_RPM = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EFireMode FireMode = EFireMode::SemiAuto;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NumBurstBullets = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float TimeBetweenEachBurstBullet_Seconds = 0.f;
};


/**
 * Weapon recoil profile. Like FWeaponStats this is INSTANCE state carried by the weapon's
 * FWeaponItemFragment, so attachments and modifiers can tune a single copy without
 * touching every other weapon of the same type.
 */
USTRUCT(BlueprintType, meta = (UseCategoryGroups))
struct FWeaponRecoil
{
	GENERATED_BODY()

	// CrystalRecoil pattern data asset (UCRRecoilPattern). Object reference only —
	// the asset exists identically on all machines, so this is safe/cheap if ever copied.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<UCRRecoilPattern> RecoilPattern = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bRecoveryEnabled = true;

	/** Overall recoil magnitude. 1 = full. Attachments/mods scale this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength")
	float RecoilStrength = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength")
	float HorizontalRecoilStrength = 1.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strength")
	float VerticalRecoilStrength = 1.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Multiplier")
	float RecoilMulti_ADS = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "State Multiplier")
	float RecoilMulti_Crouched = 1.f;
	
	/** Extra spread (degrees) added while moving. Independent of heat, so it applies
			even to a first shot with a zero base spread. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float SpreadAdd_Moving = 0.f;

	/** Extra spread (degrees) added while airborne. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float SpreadAdd_InAir = 0.f;
	
 
	/** Max random yaw (deg) added per shot. 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jitter")
	float HorizontalJitter = 0.f;
 
	/** Max random pitch (deg) added per shot. 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jitter")
	float VerticalJitter = 0.f;
 
	/** Ramp horizontal jitter with sustained fire (by shot index). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jitter")
	bool bScaleHorizontalJitter = false;
 
	/** Ramp vertical jitter with sustained fire (by shot index). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jitter")
	bool bScaleVerticalJitter = false;
 
	/** How fast jitter ramps (linearly) per shot when the ramp flags are on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jitter")
	float JitterScaleFactor = 0.1f;
 
	/** Max shot dispersion: scales the whole spread cone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float SpreadAngleMultiplier = 1.f;
 
	/** Per-shot dispersion: scales how much heat each shot adds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float PerShotHeatMultiplier = 1.f;
 
	/** Heat ceiling (spread cap). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float MaxRecoilHeat = 100.f;
 
	/** Seconds after the last shot before heat starts cooling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread")
	float RecoilHeatCooldownDelay = 0.5f;
 
	/** Recovery (camera return) speed scalar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recovery")
	float RecoverySpeedMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread",
		meta = (ToolTip = "Heat added per shot based on current heat; controls how quickly accuracy degrades: (Current heat, Heat added by shot)"))
	FRuntimeFloatCurve ShotToHeatCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread",
		meta = (ToolTip = "Spread angle corresponding to the current heat; controls the strength of heat with continued fire: (Current heat, Spread angle)"))
	FRuntimeFloatCurve HeatToSpreadAngleCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spread",
		meta = (ToolTip = "Heat lost per second based on current heat. Applied each tick after RecoilHeatCooldownDelay since the last shot; controls accuracy recovery: (Current heat, heat lost per second)"))
	FRuntimeFloatCurve HeatToCooldownPerSecondCurve;
	
	/** Camera shake intensity over continued fire (heat/shot count -> shake magnitude). Per-weapon feel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera Shake")
	FRuntimeFloatCurve CameraShakeCurve;
};

/**
 * Weapon presentation & spawn design — the cosmetic/spawnable assets a weapon uses
 */
USTRUCT(BlueprintType, meta = (UseCategoryGroups))
struct FWeaponDesign
{
	GENERATED_BODY()

	/** Projectile actor spawned from the muzzle on each shot (BP_ProjectileBase or a subclass).
		The projectile handles its own travel and collision; damage is passed to it on spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<AActor> ProjectileClass;

	/** Muzzle flash VFX spawned at the muzzle socket on each shot (played on all clients via multicast). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual FX")
	TSoftObjectPtr<UNiagaraSystem> MuzzleFlash;
	
	/** Sound played on fire. All clients, per shot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	TSoftObjectPtr<USoundBase> ShotSound;
};

USTRUCT(BlueprintType, meta = (UseCategoryGroups))
struct FProjectileImpact
{
	GENERATED_BODY()

	/** Multiplies the decal's world size (bigger caliber = bigger hole). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture")
	FVector DecalSizeMultiplier = FVector(1.f);

	/** 0-1, fed into the decal material's normal-intensity param (fake depth). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture")
	float ImpactDepth = 1.f;
	
	/** dB offset applied on top of the surface's base impact sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	float ImpactVolumeDb = 0.f;

	/** Optional pitch multiplier for a heavier/lighter caliber feel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	float ImpactPitchMultiplier = 1.f;
};

USTRUCT(BlueprintType)
struct FSuppressorAudioModifier
{
	GENERATED_BODY()

	/** Negative dB — how much quieter the shot sounds with this suppressor on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float VolumeReductionDb = 15.f;

	/** Low-pass filter cutoff — suppressors muffle high frequencies (the "crack"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float LowPassCutoffHz = 4000.f;

	/** Optional: a single shared "suppressed tail" layer, NOT per-weapon — plays on
		top of the muffled base shot instead of the weapon's normal loud tail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<USoundBase> SharedSuppressedTail;
};


