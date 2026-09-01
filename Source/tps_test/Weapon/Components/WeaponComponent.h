// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/Structs/InventoryEnums.h"
#include "Inventory/Structs/Definition.h"
#include "Inventory/Structs/Instance.h"
#include "Inventory/Structs/Rows.h"
#include "Weapon/Fragment/WeaponItemFragment.h"
#include "Weapon/Components/WeaponRecoilComponent.h"

#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundBase.h"

#include "WeaponComponent.generated.h"

class UInventoryComponent;
class USkeletalMeshComponent;
class UCameraShakeBase;
class ACharacter;

// Dispatchers — parity with the BP dispatchers. Rebind UI/anim listeners to these.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponLoaded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponEquipped);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAmmoChanged, int32, AmmoInMag, int32, ReserveAmmo);

/**
 * Server-replicated weapon component. Direct C++ port of BPC_WeaponManager with
 * server-authoritative firing (Option C: projectile spawned from the muzzle toward the
 * crosshair target point).
 *
 * ============================== HOW TO SET THIS UP ==============================
 * 1. Create a Blueprint subclass (BP_WeaponComponent) of this class — the cosmetic
 *    hooks (recoil visuals, sounds, montages) are BlueprintNativeEvents you implement
 *    there. Add BP_WeaponComponent to your character (one per weapon slot: Primary,
 *    Secondary, ...).
 * 2. In the character's BeginPlay, call InitializeWeaponComponent(WeaponSlotInventory,
 *    BackpackInventory) on each weapon component. This binds equip-on-change and
 *    performs the initial equip (auto-equips if the slot already has a weapon).
 * 3. Wire input on the character:
 *      IA_FireWeapon Started   -> ActiveWeapon->RequestStartFiring()
 *      IA_FireWeapon Completed -> ActiveWeapon->RequestStopFiring()
 *      IA_Reload    Triggered  -> ActiveWeapon->RequestReload()
 *      Scroll wheel            -> FindNextOccupiedWeapon(...) -> RequestEquipAsActive()
 * 4. Implement the cosmetic hooks in BP_WeaponComponent (each is a few nodes you
 *    already have from BPC_WeaponManager): SetRecoilParametersFX, StartShootingFX,
 *    StopShootingFX, ShotRecoilFX, PlayFireSound, PlayReloadEffects, ...
 *
 * WEAPON MESH REQUIREMENT: every weapon skeletal mesh MUST have a socket named
 * "muzzle" (configurable via MuzzleSocketName) — projectiles and muzzle flash spawn
 * from it. The character mesh must have the attach socket (default "weaponsocket_r").
 *
 * REQUIRED INVENTORY ADDITIONS: this component calls three small public functions that
 * must be added to UInventoryComponent (SetItemInstance, GetTotalQuantityByRowName,
 * ConsumeItemsByRowName) — code provided alongside this file.
 *
 * AMMO MODEL (pooled): AmmoInMag lives on the equipped weapon's FItemInstance.
 * Reserve ammo is the POOL of items matching FWeaponRow::AmmoType in the ammo-source
 * (backpack) inventory. Reload consumes pool items stack-by-stack (first found, then
 * next; partial reload if the pool can't fill the mag). After every ammo change the
 * instance's AmmoCount.AmmoTotal is refreshed to the pool total so UI can read one
 * struct. Mag hitting zero auto-reloads (parity with the BP's UpdateAmmoCount).
 *
 * AUTHORITY MODEL: all firing/reload/equip state changes run on the server. The
 * Request* entry points route automatically (direct with authority, Server RPC from
 * clients). Cosmetic feedback (muzzle flash, recoil kick, camera shake, sounds) plays
 * on clients via Multicast_PlayFireFX -> the BlueprintNativeEvent hooks.
 *
 * SPREAD: affects the real shot. The server keeps its own heat model driven by the
 * weapon's FWeaponRecoil curves (ShotToHeat / HeatToSpreadAngle / HeatToCooldown),
 * scaled by movement multipliers (RecoilMulti_Moving / RecoilMulti_InAir active;
 * Crouched/ADS hooks present but inactive until you provide those states). The
 * CrystalRecoil component remains the client-side VISUAL recoil — driven through the
 * FX hooks, exactly like the BP did.
 *
 * DAMAGE: deferred (your future health/shield component). The spawned projectile
 * receives the weapon's damage + instigator through ConfigureProjectile — implement
 * that hook when the damage system exists.
 */
UCLASS(ClassGroup=(Weapons), Blueprintable, meta=(BlueprintSpawnableComponent))
class TPS_TEST_API UWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// =================================================================================
	// Configuration
	// =================================================================================
	
	
	/** Slot index inside the weapon-source inventory this component reads its weapon from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config")
	int32 WeaponSlotIndex = 0;

	/** REQUIRED: every weapon skeletal mesh MUST have a socket with this name — the
	    projectile and muzzle flash spawn from it. Default "muzzle". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config",
		meta = (ToolTip = "Weapon skeletal meshes MUST have a socket with this name. Projectiles and muzzle flash spawn from it."))
	FName MuzzleSocketName = TEXT("muzzle");

	/** Character mesh socket the weapon attaches to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config")
	FName WeaponAttachSocketName = TEXT("weaponsocket_r");

	/** Parity: minimum time between semi-auto shots regardless of fire rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config")
	float FullAutoTapDelaySeconds = 0.2f;

	/** How far the camera->crosshair aim trace looks for a target point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config")
	float AimTraceRange = 20000.f;

	/** Speed (cm/s, 2D) above which the shooter counts as "moving" for RecoilMulti_Moving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config")
	float MovingSpeedThreshold = 10.f;

	/** Seconds the reload takes before ammo is applied (match your reload montage length). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config")
	float ReloadDuration = 2.0f;

	/** If true and this slot holds a weapon at initialization, this component becomes the
	    active weapon when no sibling weapon component is active yet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config")
	bool bAutoEquipIfFirst = true;

	/** Optional camera shake class played per shot on the owning client, scaled by
	    FWeaponRecoil::CameraShakeCurve (heat -> intensity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config|FX")
	TSubclassOf<UCameraShakeBase> CameraShakeClass;

	// =================================================================================
	// Setup — call from the character
	// =================================================================================

	/**
	 * REQUIRED SETUP — call from the character's BeginPlay on this component:
	 * binds to the weapon-source inventory's OnInventoryChanged (equips/unequips as the
	 * slot changes) and performs the initial equip. Call on all machines (server does
	 * the authoritative parts internally).
	 * @param InWeaponSourceInventory the inventory holding this component's weapon (e.g. the 1-slot primary weapon inventory)
	 * @param InAmmoSourceInventory   the inventory holding ammo items (the backpack)
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Setup")
	void InitializeWeaponComponent(UInventoryComponent* InWeaponSourceInventory, UInventoryComponent* InAmmoSourceInventory);

	/** The inventory this component's weapon lives in. Set via InitializeWeaponComponent. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Setup")
	TObjectPtr<UInventoryComponent> WeaponSourceInventory = nullptr;

	/** The inventory ammo items are pooled from (backpack). Set via InitializeWeaponComponent. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon|Setup")
	TObjectPtr<UInventoryComponent> AmmoSourceInventory = nullptr;

	// =================================================================================
	// Dispatchers (parity)
	// =================================================================================

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponLoaded OnWeaponLoaded;

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnWeaponEquipped OnWeaponEquipped;

	/** Broadcast whenever mag or reserve changes (server + on replication). Handy for UI. */
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Events")
	FOnAmmoChanged OnAmmoChanged;

	// =================================================================================
	// Input entry points — call on the ACTIVE weapon component from the character.
	// Route automatically: direct with authority, Server RPC from clients.
	// =================================================================================

	UFUNCTION(BlueprintCallable, Category = "Weapon|Actions")
	void RequestStartFiring();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Actions")
	void RequestStopFiring();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Actions")
	void RequestReload();

	/** Make this the active (drawn) weapon; holsters sibling weapon components. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Actions")
	void RequestEquipAsActive();

	// =================================================================================
	// Weapon switching helpers (pure; use from the character's scroll-wheel input)
	// =================================================================================

	/** The currently active weapon among the given components (nullptr if none). */
	UFUNCTION(BlueprintPure, Category = "Weapon|Switching")
	static UWeaponComponent* GetActiveWeapon(const TArray<UWeaponComponent*>& Weapons);

	/**
	 * Next weapon to switch to, cycling forward/backward from the active one and
	 * SKIPPING EMPTY SLOTS. Returns nullptr if no other occupied weapon exists.
	 * Character: scroll -> FindNextOccupiedWeapon(List, forward) -> RequestEquipAsActive().
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Switching")
	static UWeaponComponent* FindNextOccupiedWeapon(const TArray<UWeaponComponent*>& Weapons, bool bForward);

	/** Does this component's weapon slot currently hold a weapon? */
	UFUNCTION(BlueprintPure, Category = "Weapon|Switching")
	bool IsSlotOccupied() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Switching")
	bool IsActiveWeapon() const { return IsWeaponActive; }

	// =================================================================================
	// State queries (UI-facing)
	// =================================================================================

	UFUNCTION(BlueprintPure, Category = "Weapon|State")
	bool GetIsArmed() const { return IsArmed; }

	UFUNCTION(BlueprintPure, Category = "Weapon|State")
	bool GetIsReloading() const { return IsReloading; }

	/** The equipped weapon's item instance, resolved live from the weapon-source inventory. */
	UFUNCTION(BlueprintPure, Category = "Weapon|State", meta = (ReturnDisplayName = "Found"))
	bool GetEquippedInstance(FItemInstance& OutInstance) const;

	/** The equipped weapon's full row (stats, recoil, design), resolved via the item tables. */
	UFUNCTION(BlueprintPure, Category = "Weapon|State", meta = (ReturnDisplayName = "Found"))
	bool GetEquippedWeaponRow(FWeaponRow& OutRow) const;

	UFUNCTION(BlueprintPure, Category = "Weapon|State")
	int32 GetMagAmmo() const;

	/** Reserve = pooled AmmoType items in the ammo-source inventory. */
	UFUNCTION(BlueprintPure, Category = "Weapon|State")
	int32 GetReserveAmmo() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|State")
	EFireMode GetCurrentFireMode() const { return CurrentFireMode; }

	/** The runtime weapon mesh component (created locally on every machine). May be null before equip. */
	UFUNCTION(BlueprintPure, Category = "Weapon|State")
	USkeletalMeshComponent* GetWeaponMesh() const { return WeaponMesh; }

	// =================================================================================
	// Parity gates (server-side queries; exposed for BP extension/debugging)
	// =================================================================================

	/**
	 * Can this weapon fire at all right now? Native checks: active + armed + not
	 * reloading + mesh ready. OVERRIDE IN YOUR BP SUBCLASS to add the player-state
	 * gameplay-tag gate from the original (HasAnyTags(NoFire tags) -> Not) — call the
	 * parent first, then AND your tag check.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Weapon|Gates")
	bool IsValidFireState() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Gates")
	bool CanFireSemiAuto() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Gates")
	bool CanFireBurst() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Gates")
	bool HasAmmo() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Gates")
	bool HasAmmoInMag() const;

	// =================================================================================
	// Cosmetic hooks — BlueprintNativeEvents. Implement in your BP_WeaponComponent
	// subclass. Native defaults: muzzle flash + camera shake are implemented; the rest
	// are stubs. Each maps to nodes you already have from BPC_WeaponManager.
	// =================================================================================

	/** Owning client, when firing starts. IMPLEMENT: push recoil params into your
	    CRRecoilSpreadComponent (SetRecoilPattern, SetRecoveryEnabled, the three curves —
	    the exact nodes from the BP's SetRecoilParameters) then CRRecoil StartShooting. */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|FX")
	void OnStartFiringFX(const FWeaponRecoil& Recoil);

	/** Owning client, when firing stops. IMPLEMENT: CRRecoil StopShooting. */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|FX")
	void OnStopFiringFX();

	/** Owning client, per shot. IMPLEMENT: CRRecoil ApplyShot (the visual kick). */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|FX")
	void ShotRecoilFX();

	/** All clients, per shot. Native default spawns Design.MuzzleFlash at the muzzle socket. */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|FX")
	void PlayMuzzleFlash();

	/** All clients, per shot. STUB — implement when you do sound design. */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|FX")
	void PlayFireSound();

	/** Owning client, per shot. Native default: CameraShakeClass scaled by CameraShakeCurve(heat). */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|FX")
	void PlayCameraShake();

	/** All clients, per shot. STUB — future weapon smoke on sustained fire. */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|FX")
	void PlayWeaponSmoke();

	/** Owning client, when firing is attempted with an empty mag. STUB (dry-fire click). */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|FX")
	void OnDryFire();

	/** All clients, when a reload starts. IMPLEMENT: play the reload montage + add/remove
	    your reloading gameplay tag (the BP's montage + tag flow). */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|FX")
	void PlayReloadEffects();

	/**
	 * Server, right after the projectile is spawned (deferred) and before FinishSpawning.
	 * IMPLEMENT when your damage system exists: hand the damage/instigator to the
	 * projectile (set its exposed variables). Native default: nothing.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|Combat")
	void ConfigureProjectile(AActor* Projectile, float Damage);

	/** Per-attachment visual hook: attach the attachment's mesh to the weapon, etc. */
	UFUNCTION(BlueprintNativeEvent, Category = "Weapon|Attachments")
	void OnAttachmentEquipped(ESlotType AttachmentSlot, const FItemInstance& AttachmentInstance, const FAttachmentRow& AttachmentRow);

	// =================================================================================
	// Attachments (parity: SetEquippedAttachment / SetAttachmentDetails)
	// =================================================================================

	/** Resolve one attachment on the equipped weapon and fire OnAttachmentEquipped. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Attachments")
	void SetEquippedAttachment(ESlotType AttachmentSlot, FGuid AttachmentItemId);

	/** Run SetEquippedAttachment for every entry in the equipped instance's WeaponAttachments. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Attachments")
	void RefreshAttachments();
	
	
	/** The owning character's recoil component (local cosmetic; found on BeginPlay). */
	UFUNCTION(BlueprintPure, Category = "Weapon|Recoil")
	UWeaponRecoilComponent* GetRecoilComponent() const { return RecoilComponent; }
 
	/** Re-push the equipped weapon's recoil profile. Call after an attachment or
		modifier changes ModifiedWeaponRecoil. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Recoil")
	void RefreshRecoilProfile();
	
	/** Draw the aim trace (cyan), the projectile direction (red) and the aim point
		(yellow) for two seconds per shot. Debug only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config")
	bool bDebugDrawFireTrace = false;
 
	/**
	 * Recompute the crosshair world point from THIS machine's camera and push it to the
	 * server. Called automatically while firing; also safe to call manually (e.g. from
	 * the character's Tick if you want the server's aim fresh at all times).
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Actions")
	void UpdateAimPoint();
	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;

protected:
	
	UFUNCTION(Server, Unreliable)
	void Server_SetAimPoint(FVector_NetQuantize AimPoint);
	
	bool ComputeLocalAimPoint(FVector& OutPoint) const;
	
	/** Last aim point received from the owning client (server) / computed locally. */
	FVector CachedAimPoint = FVector::ZeroVector;
	bool bHasCachedAim = false;
	
	/** Movement/stance scalar for recoil: RecoilMulti_Moving / _InAir (Crouched/ADS later). */
	float GetRecoilStateMultiplier() const;
	/** Movement/airborne scalar applied to SPREAD at fire time. */
	float GetSpreadStateBonusDeg() const;
	
	UPROPERTY(Transient)
	TObjectPtr<UWeaponRecoilComponent> RecoilComponent = nullptr;
	
	// =================================================================================
	// Replicated state
	// =================================================================================

	/** Armed = this slot holds a weapon (equipped data valid). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_EquipState)
	bool IsArmed = false;

	/** Active = this is the drawn weapon among the character's weapon components. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_EquipState)
	bool IsWeaponActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_IsReloading)
	bool IsReloading = false;

	/** Guid of the equipped item inside the weapon-source inventory's store. */
	UPROPERTY(Replicated)
	FGuid EquippedItemId;

	/** Row name of the equipped weapon (lets every machine resolve mesh/row locally). */
	UPROPERTY(ReplicatedUsing = OnRep_EquipState)
	FName EquippedRowName = NAME_None;

	UFUNCTION()
	void OnRep_EquipState();

	UFUNCTION()
	void OnRep_IsReloading();

	// =================================================================================
	// Server RPC carriers
	// =================================================================================

	UFUNCTION(Server, Reliable)
	void Server_StartFiring();

	UFUNCTION(Server, Reliable)
	void Server_StopFiring();

	UFUNCTION(Server, Reliable)
	void Server_Reload();

	UFUNCTION(Server, Reliable)
	void Server_EquipAsActive();

	// =================================================================================
	// FX multicast
	// =================================================================================

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayFireFX();

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayReloadFX();
	
	// --- Impact FX ---

	UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category="Weapon|FX")
	void Multicast_SpawnImpactEffect(FVector_NetQuantize ImpactLocation, FVector_NetQuantizeNormal ImpactNormal, UPhysicalMaterial* PhysMaterial);

	UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category="Weapon|FX")
	void Multicast_PlayImpactSound(FVector_NetQuantize ImpactLocation, UPhysicalMaterial* PhysMaterial);

	UPROPERTY(EditDefaultsOnly, Category="Weapon|FX")
	TMap<TEnumAsByte<EPhysicalSurface>, TSoftObjectPtr<UMaterialInterface>> ImpactDecalsBySurface;

	UPROPERTY(EditDefaultsOnly, Category="Weapon|FX")
	TSoftObjectPtr<UMaterialInterface> DefaultImpactDecal;

	UPROPERTY(EditDefaultsOnly, Category="Weapon|FX")
	TMap<TEnumAsByte<EPhysicalSurface>, TSoftObjectPtr<USoundBase>> ImpactSoundsBySurface;

	UPROPERTY(EditDefaultsOnly, Category="Weapon|FX")
	TSoftObjectPtr<USoundBase> DefaultImpactSound;

	UMaterialInterface* ResolveDecalMaterialForSurface(UPhysicalMaterial* PhysMaterial) const;
	USoundBase* ResolveImpactSoundForSurface(UPhysicalMaterial* PhysMaterial) const;
	bool ResolveEquippedAmmoRow(FAmmoRow& OutAmmoRow) const;

	// =================================================================================
	// Server-side internals
	// =================================================================================

	// --- Equip ---
	void HandleWeaponSourceChanged(int32 ChangedSlotIndex);
	UFUNCTION()
	void OnWeaponSourceInventoryChanged(int32 InventoryIndex);
	void SetEquippedWeapon();       // parity: read slot -> instance -> row -> state
	void HandleEquipAsActive();     // server: holster siblings, activate self
	void Holster();                 // server: deactivate (stop fire/reload, hide)
	void ApplyEquippedVisuals();    // every machine: mesh + attach + visibility
	void EnsureWeaponMesh();        // create the runtime USkeletalMeshComponent locally

	// --- Fire ---
	void HandleStartFiring();
	void HandleStopFiring();
	void TryFire();                 // parity: the BP Fire function (server)
	void ClearShooting();           // parity: FullAuto timer cleared (only for FullAuto), firing flag off
	void ClearBurst();              // parity: reset burst counter + clear burst timer
	void SpawnProjectile(const FVector& MuzzleLocation, const FVector& Direction, const FWeaponRow& Row);
	bool ComputeAimTargetPoint(FVector& OutTargetPoint) const; // camera->crosshair trace (server)
	float ComputeSpreadAngleDeg(const FWeaponRecoil& Recoil);  // heat model x movement multipliers
	void DecayHeat(const FWeaponRecoil& Recoil);

	// --- Ammo / reload ---
	void UpdateAmmoCount(const FAmmoCount& NewAmmo); // parity: write instance back (+auto-reload on empty)
	void HandleReload();
	void FinishReload();
	void RefreshInstanceReserveMirror(FItemInstance& Instance) const; // AmmoTotal <- pool

	// --- Helpers ---
	ACharacter* GetOwningCharacter() const;
	bool ResolveEquipped(FItemInstance& OutInstance, FWeaponRow& OutRow, FWeaponItemFragment& OutState) const;
	static float EvalCurve(const FRuntimeFloatCurve& Curve, float Time, float DefaultValue);
	bool HasAuthorityLocal() const;
	static void ScreenLog(const FString& Message, bool bIsError = true);

	// =================================================================================
	// Transient runtime state
	// =================================================================================

	/** Runtime weapon mesh (created locally per machine; cosmetic, not replicated). */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> WeaponMesh = nullptr;

	/** Derived from the equipped instance's stats on every machine. */
	UPROPERTY(Transient)
	EFireMode CurrentFireMode = EFireMode::SemiAuto;

	// Server-only firing state (parity variables)
	FTimerHandle FullAutoFireRateTimer;
	FTimerHandle BurstFireRateTimer;
	FTimerHandle ReloadTimer;
	float LastFireTime = -1000.f;
	float LastBurstBulletFireTime = -1000.f;
	int32 BurstBulletCounter = 0;
	bool bFiringHeld = false;       // parity: FiringWeaponStarted

	// Server spread heat model
	float ServerHeat = 0.f;
	float LastHeatUpdateTime = 0.f;

	// Owning-client cosmetic heat mirror (drives camera shake scale)
	float LocalShakeHeat = 0.f;
	float LastLocalShakeTime = 0.f;
	
	/** Residual kick/recovery below this (degrees) counts as fully settled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Config")
	float RecoilSettleTolerance = 0.01f;

	/** True when no kick is pending and nothing is left to recover. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Recoil")
	bool IsRecoilFullySettled() const;
};