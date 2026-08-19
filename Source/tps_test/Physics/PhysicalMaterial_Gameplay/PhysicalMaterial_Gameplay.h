// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicalMaterial_Gameplay.generated.h"

class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;

/**
 * Physical Material subclass carrying per-surface gameplay FX data (projectile impact,
 * footsteps, and room to grow — landing, wading, etc.). Assign this class instead of the
 * base UPhysicalMaterial when creating a new Physical Material asset so its FX fields
 * show up in the Details panel alongside Friction/Restitution/Surface Type.
 *
 * Resolve() is the shared entry point: any system holding a raw UPhysicalMaterial*
 * (e.g. FHitResult::PhysMaterial) casts through it instead of duplicating the
 * Cast<>+null-check pattern at every call site.
 */
UCLASS(BlueprintType)
class TPS_TEST_API UPhysicalMaterial_Gameplay : public UPhysicalMaterial
{
	GENERATED_BODY()

public:
	/** Safe cast helper: returns nullptr if PhysMaterial is null or not this subclass. */
	static UPhysicalMaterial_Gameplay* Resolve(UPhysicalMaterial* PhysMaterial);

	// =================================================================================
	// Projectile impact
	// =================================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TSoftObjectPtr<UMaterialInterface> ImpactDecal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TSoftObjectPtr<UNiagaraSystem> ImpactParticle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TSoftObjectPtr<USoundBase> ImpactSound;

	// =================================================================================
	// Footsteps
	// =================================================================================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footsteps")
	TSoftObjectPtr<USoundBase> FootstepSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footsteps")
	TSoftObjectPtr<UNiagaraSystem> FootstepDust;

	// Room for more later: LandingSound, WadingSound, DragSound, etc.
};