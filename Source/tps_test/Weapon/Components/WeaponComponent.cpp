// Copyright arubaii. All rights reserved.


#include "Weapon/Components/WeaponComponent.h"

#include "Item/Component/InventoryComponent.h"
#include "Item/ItemLibrary.h"
#include "Weapon/Fragment/WeaponItemFragment.h"

#include "Camera/CameraShakeBase.h"
#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Physics/PhysicalMaterial_Gameplay/PhysicalMaterial_Gameplay.h"


DEFINE_LOG_CATEGORY_STATIC(LogWeaponComponent, Log, All);


static TAutoConsoleVariable<int32> CVarWeaponDebugDraw(
	TEXT("weapon.DebugDraw"),
	0,
	TEXT("Draw weapon fire debug: 0 = off, 1 = projectile trace, 2 = projectile trace and aim trace."),
	ECVF_Cheat);

// =====================================================================================
//  Construction / lifecycle / replication
// =====================================================================================


UWeaponComponent::UWeaponComponent()
{
	// Tick is used ONLY on the owning client, while firing, to stream the crosshair aim
	// point to the server (the server cannot see the client's camera).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UWeaponComponent, IsArmed);
	DOREPLIFETIME(UWeaponComponent, IsWeaponActive);
	DOREPLIFETIME(UWeaponComponent, IsReloading);
	DOREPLIFETIME(UWeaponComponent, EquippedItemId);
	DOREPLIFETIME(UWeaponComponent, EquippedRowName);
}

void UWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	
	if (ACharacter* character = GetOwningCharacter())
	{
		RecoilComponent = character->FindComponentByClass<UWeaponRecoilComponent>();
		if (RecoilComponent)
			RecoilComponent->SetTargetController(character->GetController());
	}
 
	
	ApplyEquippedVisuals();
}

void UWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* world = GetWorld())
	{
		world->GetTimerManager().ClearTimer(FullAutoFireRateTimer);
		world->GetTimerManager().ClearTimer(BurstFireRateTimer);
		world->GetTimerManager().ClearTimer(ReloadTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void UWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Owning client only: keep the server's copy of the crosshair point fresh so
	// full-auto and burst shots after the first one still aim where the player looks.
	const APawn* ownerPawn = Cast<APawn>(GetOwner());
	if (ownerPawn && ownerPawn->IsLocallyControlled())
		UpdateAimPoint();
}

void UWeaponComponent::ScreenLog(const FString& Message, bool bIsError)
{
	if (bIsError)
	{
		UE_LOG(LogWeaponComponent, Warning, TEXT("%s"), *Message);
	}
	else
		UE_LOG(LogWeaponComponent, Log, TEXT("%s"), *Message);
	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, 6.f, bIsError ? FColor::Red : FColor::Green, Message);
}

bool UWeaponComponent::HasAuthorityLocal() const
{
	const AActor* owner = GetOwner();
	return owner && owner->HasAuthority();
}

ACharacter* UWeaponComponent::GetOwningCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

float UWeaponComponent::EvalCurve(const FRuntimeFloatCurve& Curve, float Time, float DefaultValue)
{
	const FRichCurve* rich = Curve.GetRichCurveConst();
	return (rich && rich->GetNumKeys() > 0) ? rich->Eval(Time) : DefaultValue;
}

// =====================================================================================
//  Setup
// =====================================================================================

void UWeaponComponent::InitializeWeaponComponent(UInventoryComponent* InWeaponSourceInventory, UInventoryComponent* InAmmoSourceInventory)
{
	if (!InWeaponSourceInventory)
	{
		ScreenLog(TEXT("[UWeaponComponent::InitializeWeaponComponent] WeaponSourceInventory is null — the component cannot function."));
		return;
	}
	if (!InAmmoSourceInventory)
		ScreenLog(TEXT("[UWeaponComponent::InitializeWeaponComponent] AmmoSourceInventory is null — reload will have no ammo pool."));

	WeaponSourceInventory = InWeaponSourceInventory;
	AmmoSourceInventory = InAmmoSourceInventory;

	// Equip-on-change parity: the BP called SetEquippedWeapon from the character when the
	// primary weapon inventory broadcast OnInventoryChanged. The binding lives here now.
	WeaponSourceInventory->OnInventoryChanged.AddUniqueDynamic(this, &UWeaponComponent::OnWeaponSourceInventoryChanged);

	if (HasAuthorityLocal())
	{
		SetEquippedWeapon();

		// Auto-equip at startup: if this slot holds a weapon and no sibling weapon
		// component is active yet, this one becomes the drawn weapon.
		if (bAutoEquipIfFirst && IsArmed && !IsWeaponActive)
		{
			bool bAnySiblingActive = false;
			if (const AActor* owner = GetOwner())
			{
				TArray<UWeaponComponent*> siblings;
				owner->GetComponents<UWeaponComponent>(siblings);
				for (const UWeaponComponent* sibling : siblings)
				{
					if (sibling && sibling->IsWeaponActive)
					{
						bAnySiblingActive = true;
						break;
					}
				}
			}
			if (!bAnySiblingActive)
				HandleEquipAsActive();
		}
	}
	else
		// Client: state may already have replicated before initialization — reflect it.
		ApplyEquippedVisuals();
}

void UWeaponComponent::OnWeaponSourceInventoryChanged(int32 InventoryIndex)
{
	HandleWeaponSourceChanged(InventoryIndex);
}

void UWeaponComponent::HandleWeaponSourceChanged(int32 ChangedSlotIndex)
{
	if (ChangedSlotIndex != WeaponSlotIndex)
		return;

	if (HasAuthorityLocal())
	{
		const FInventorySlot slot = WeaponSourceInventory->GetSlot(WeaponSlotIndex);
		if (slot.ItemID != EquippedItemId)
			SetEquippedWeapon();          // genuinely a different weapon
		else
			OnAmmoChanged.Broadcast(GetMagAmmo(), GetReserveAmmo());  // ammo-only change
	}
}

// =====================================================================================
//  Equip (parity: SetEquippedWeapon + LoadWeaponAsync + EquipWeapon)
// =====================================================================================

void UWeaponComponent::SetEquippedWeapon()
{
	if (!HasAuthorityLocal() || !WeaponSourceInventory)
		return;

	const FInventorySlot slot = WeaponSourceInventory->GetSlot(WeaponSlotIndex);
	FItemInstance instance;
	const bool bHasWeapon = slot.ItemID.IsValid() && WeaponSourceInventory->GetItem(slot.ItemID, instance);

	if (!bHasWeapon)
	{
		// Slot emptied (weapon dropped/moved) -> unequip.
		const bool bWasArmed = IsArmed;
		IsArmed = false;
		EquippedItemId.Invalidate();
		EquippedRowName = NAME_None;
		ClearShooting();
		ClearBurst();
		if (UWorld* world = GetWorld())
			world->GetTimerManager().ClearTimer(ReloadTimer);
		IsReloading = false;
		bFiringHeld = false;

		ApplyEquippedVisuals();
		if (bWasArmed)
			OnWeaponEquipped.Broadcast();
		
		if (RecoilComponent)
			RecoilComponent->ResetRecoilHeat();
		RefreshRecoilProfile();
		
		return;
	}

	// Parity: slot -> instance -> row -> equipped state.
	EquippedItemId = slot.ItemID;
	EquippedRowName = instance.RowName;
	if (const FWeaponItemFragment* liveState = instance.GetFragment<FWeaponItemFragment>())
		CurrentFireMode = liveState->Stats.FireMode;
	IsArmed = true;
	ServerHeat = 0.f;
	LastHeatUpdateTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	FWeaponRow row;
	if (!UItemLibrary::GetItemDefTyped<FWeaponRow>(EquippedRowName, row))
	{
		ScreenLog(FString::Printf(TEXT("[UWeaponComponent::SetEquippedWeapon] Could not find weapon row for '%s' (check Item Tables prefix mapping and the weapon table's row struct)."),
		                          *EquippedRowName.ToString()));
		// Stay armed with data we have; visuals will be missing the mesh.
	}
	
	ApplyEquippedVisuals();

	// Parity: the BP async-loaded the soft mesh then broadcast OnWeaponLoaded. Your
	// FWeaponRow::SkeletalMesh is a HARD reference now (loads with the row), so the
	// "load" is immediate — the broadcast timing is preserved.
	OnWeaponLoaded.Broadcast();
	OnWeaponEquipped.Broadcast();

	RefreshAttachments();
	OnAmmoChanged.Broadcast(GetMagAmmo(), GetReserveAmmo());
	
	// Auto-activate if this weapon just armed and no sibling is currently the active weapon.
	if (bAutoEquipIfFirst && IsArmed && !IsWeaponActive)
	{
		bool bAnySiblingActive = false;
		if (const AActor* owner = GetOwner())
		{
			TArray<UWeaponComponent*> siblings;
			owner->GetComponents<UWeaponComponent>(siblings);
			for (const UWeaponComponent* sibling : siblings)
			{
				if (sibling && sibling != this && sibling->IsWeaponActive)
				{
					bAnySiblingActive = true;
					break;
				}
			}
		}
		if (!bAnySiblingActive)
			HandleEquipAsActive();
	}
}

void UWeaponComponent::RequestEquipAsActive()
{
	if (HasAuthorityLocal())
		HandleEquipAsActive();
	else
		Server_EquipAsActive();
}

void UWeaponComponent::Server_EquipAsActive_Implementation()
{
	HandleEquipAsActive();
}

void UWeaponComponent::HandleEquipAsActive()
{
	if (!HasAuthorityLocal())
		return;
	if (!IsArmed)
		return; // nothing to draw

	// Holster every sibling weapon component, then activate this one.
	if (const AActor* owner = GetOwner())
	{
		TArray<UWeaponComponent*> siblings;
		owner->GetComponents<UWeaponComponent>(siblings);
		for (UWeaponComponent* sibling : siblings)
		{
			if (sibling && sibling != this && sibling->IsWeaponActive)
				sibling->Holster();
		}
	}

	IsWeaponActive = true;
	ApplyEquippedVisuals();
	OnWeaponEquipped.Broadcast();
}

void UWeaponComponent::Holster()
{
	if (!HasAuthorityLocal())
		return;

	IsWeaponActive = false;

	// Switching away cancels everything in progress: firing, burst, reload.
	bFiringHeld = false;
	if (UWorld* world = GetWorld())
	{
		world->GetTimerManager().ClearTimer(FullAutoFireRateTimer);
		world->GetTimerManager().ClearTimer(BurstFireRateTimer);
		world->GetTimerManager().ClearTimer(ReloadTimer);
	}
	BurstBulletCounter = 0;
	IsReloading = false;

	ApplyEquippedVisuals();
}

void UWeaponComponent::OnRep_EquipState()
{
	ApplyEquippedVisuals();
	OnWeaponEquipped.Broadcast();
	OnAmmoChanged.Broadcast(GetMagAmmo(), GetReserveAmmo());
}

void UWeaponComponent::OnRep_IsReloading()
{
	// Cosmetic reload feedback arrives via Multicast_PlayReloadFX; this rep exists so
	// UI can poll GetIsReloading() reliably. Intentionally no logic here.
}

void UWeaponComponent::EnsureWeaponMesh()
{
	if (WeaponMesh)
		return;

	ACharacter* character = GetOwningCharacter();
	if (!character || !character->GetMesh())
		return;

	// Runtime-created, LOCAL cosmetic component. Every machine builds its own from the
	// replicated equip state (EquippedRowName / IsWeaponActive) — the component itself
	// is deliberately not replicated.
	WeaponMesh = NewObject<USkeletalMeshComponent>(GetOwner());
	if (!WeaponMesh)
		return;
	WeaponMesh->RegisterComponent();
	WeaponMesh->AttachToComponent(character->GetMesh(),
	                              FAttachmentTransformRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false),
	                              WeaponAttachSocketName);
	WeaponMesh->SetVisibility(false);
}

void UWeaponComponent::ApplyEquippedVisuals()
{
	EnsureWeaponMesh();
	if (!WeaponMesh)
		return;

	if (!IsArmed || EquippedRowName == NAME_None)
	{
		WeaponMesh->SetSkeletalMeshAsset(nullptr);
		WeaponMesh->SetVisibility(false);
		return;
	}

	FWeaponRow row;
	if (!UItemLibrary::GetItemDefTyped<FWeaponRow>(EquippedRowName, row))
	{
		WeaponMesh->SetVisibility(false);
		return;
	}

	// Keep the derived fire mode current on every machine (instance stats win when the
	// instance is resolvable; the row is the fallback before inventory replication lands).
	FItemInstance instance;
	if (WeaponSourceInventory && EquippedItemId.IsValid() && WeaponSourceInventory->GetItem(EquippedItemId, instance))
	{
		if (const FWeaponItemFragment* liveState = instance.GetFragment<FWeaponItemFragment>())
			CurrentFireMode = liveState->Stats.FireMode;
	}
	else
	{
		// The row carries no stats any more; fall back to the definition's authored
		// default fragment until the live instance replicates in.
		if (const FWeaponItemFragment* defaultFragment = row.ItemDetails.GetDefaultFragment<FWeaponItemFragment>())
			CurrentFireMode = defaultFragment->Stats.FireMode;
	}

	WeaponMesh->SetSkeletalMeshAsset(row.SkeletalMesh);
	WeaponMesh->SetVisibility(IsWeaponActive);
	

}

// =====================================================================================
//  Switching helpers
// =====================================================================================

UWeaponComponent* UWeaponComponent::GetActiveWeapon(const TArray<UWeaponComponent*>& Weapons)
{
	for (UWeaponComponent* weapon : Weapons)
	{
		if (weapon && weapon->IsWeaponActive)
			return weapon;
	}
	return nullptr;
}

UWeaponComponent* UWeaponComponent::FindNextOccupiedWeapon(const TArray<UWeaponComponent*>& Weapons, bool bForward)
{
	const int32 num = Weapons.Num();
	if (num == 0)
		return nullptr;

	int32 activeIndex = INDEX_NONE;
	for (int32 i = 0; i < num; ++i)
	{
		if (Weapons[i] && Weapons[i]->IsWeaponActive)
		{
			activeIndex = i;
			break;
		}
	}

	const int32 step = bForward ? 1 : -1;
	const int32 start = (activeIndex == INDEX_NONE) ? (bForward ? 0 : num - 1) : activeIndex + step;

	// Walk the list once, wrapping, skipping empty slots and the current weapon.
	for (int32 offset = 0; offset < num; ++offset)
	{
		const int32 index = ((start + offset * step) % num + num) % num;
		UWeaponComponent* candidate = Weapons[index];
		if (!candidate || index == activeIndex)
			continue;
		if (candidate->IsSlotOccupied())
			return candidate;
	}
	return nullptr;
}

bool UWeaponComponent::IsSlotOccupied() const
{
	return WeaponSourceInventory && WeaponSourceInventory->GetSlot(WeaponSlotIndex).ItemID.IsValid();
}

// =====================================================================================
//  State queries
// =====================================================================================

bool UWeaponComponent::ResolveEquipped(FItemInstance& OutInstance, FWeaponRow& OutRow, FWeaponItemFragment& OutState) const
{
	OutState = FWeaponItemFragment();

	if (!IsArmed || !WeaponSourceInventory || !EquippedItemId.IsValid())
		return false;
	if (!WeaponSourceInventory->GetItem(EquippedItemId, OutInstance))
		return false;
	const FName rowName = (EquippedRowName != NAME_None) ? EquippedRowName : OutInstance.RowName;
	if (!UItemLibrary::GetItemDefTyped<FWeaponRow>(rowName, OutRow))
		return false;

	// Live weapon state lives in a fragment on the instance. An item with no weapon
	// fragment is simply not a weapon, so resolving fails rather than handing back
	// default-constructed stats that would read as a zero fire rate.
	if (const FWeaponItemFragment* state = OutInstance.GetFragment<FWeaponItemFragment>())
	{
		OutState = *state;
		return true;
	}

	// Fall back to the definition's authored defaults, which covers the window before the
	// instance has replicated to this machine.
	if (const FWeaponItemFragment* defaultFragment = OutRow.ItemDetails.GetDefaultFragment<FWeaponItemFragment>())
	{
		OutState = *defaultFragment;
		return true;
	}
	return false;
}

bool UWeaponComponent::GetEquippedInstance(FItemInstance& OutInstance) const
{
	FWeaponRow row;
	FWeaponItemFragment state;
	return ResolveEquipped(OutInstance, row, state);
}

bool UWeaponComponent::GetEquippedWeaponRow(FWeaponRow& OutRow) const
{
	FItemInstance instance;
	FWeaponItemFragment state;
	return ResolveEquipped(instance, OutRow, state);
}

int32 UWeaponComponent::GetMagAmmo() const
{
	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	return ResolveEquipped(instance, row, state) ? state.AmmoCount.AmmoInMag : 0;
}

int32 UWeaponComponent::GetReserveAmmo() const
{
	if (!AmmoSourceInventory)
		return 0;
	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (!ResolveEquipped(instance, row, state) || row.AmmoType == NAME_None)
		return 0;
	return AmmoSourceInventory->GetTotalQuantityByRowName(row.AmmoType);
}

// =====================================================================================
//  Gates (parity)
// =====================================================================================

bool UWeaponComponent::IsValidFireState_Implementation() const
{
	// Native essentials. The BP additionally gated on player-state gameplay tags
	// (HasAnyTags(no-fire tags) -> Not) — add that in your BP subclass override:
	// return Parent AND NOT HasAnyTags(GetPlayerStateTags, YourNoFireTags).
	return IsWeaponActive
		&& IsArmed
		&& !IsReloading
		&& WeaponMesh
		&& WeaponMesh->GetSkeletalMeshAsset() != nullptr;
}

bool UWeaponComponent::CanFireSemiAuto() const
{
	
	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (!ResolveEquipped(instance, row, state))
		return false;

	const float now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float sinceLast = now - LastFireTime;

	// BP parity (Select node): full-auto uses the tap-delay gate; everything else uses
	// the fire-rate interval gate. NOT both.
	if (CurrentFireMode == EFireMode::FullAuto)
		return sinceLast >= FullAutoTapDelaySeconds;

	const float rateInterval = 60.f / FMath::Max(state.Stats.FireRate_RPM, 1.f);
	return sinceLast >= rateInterval;
}

bool UWeaponComponent::CanFireBurst() const
{
	if (CurrentFireMode != EFireMode::Burst)
		return true;

	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (!ResolveEquipped(instance, row, state))
		return false;

	return BurstBulletCounter < FMath::Max(state.Stats.NumBurstBullets, 1);
}

bool UWeaponComponent::HasAmmo() const
{
	return GetReserveAmmo() > 0;
}

bool UWeaponComponent::HasAmmoInMag() const
{
	return GetMagAmmo() > 0;
}

// =====================================================================================
//  Firing — input entries
// =====================================================================================

void UWeaponComponent::RequestStartFiring()
{
	// Capture the crosshair point BEFORE the shot and start streaming it, so the server
	// fires along the player's actual view.
	const APawn* aimPawn = Cast<APawn>(GetOwner());
	if (aimPawn && aimPawn->IsLocallyControlled())
	{
		UpdateAimPoint();
		SetComponentTickEnabled(true);
	}

	// Owning-client cosmetics first (parity: the BP pushed recoil params + StartShooting
	// on press). Responsive regardless of the server round-trip.
	const APawn* ownerPawn = Cast<APawn>(GetOwner());
	if (ownerPawn && ownerPawn->IsLocallyControlled())
	{
		FItemInstance instance;
		FWeaponRow row;
		FWeaponItemFragment state;
		if (ResolveEquipped(instance, row, state))
			OnStartFiringFX(state.Recoil);
	}

	if (HasAuthorityLocal())
		HandleStartFiring();
	else
		Server_StartFiring();
}

void UWeaponComponent::RequestStopFiring()
{
	const APawn* aimPawn = Cast<APawn>(GetOwner());
	if (aimPawn && aimPawn->IsLocallyControlled())
		SetComponentTickEnabled(false);

	const APawn* ownerPawn = Cast<APawn>(GetOwner());
	if (ownerPawn && ownerPawn->IsLocallyControlled())
		OnStopFiringFX();

	if (HasAuthorityLocal())
		HandleStopFiring();
	else
		Server_StopFiring();
}

void UWeaponComponent::RequestReload()
{
	if (HasAuthorityLocal())
		HandleReload();
	else
		Server_Reload();
}

void UWeaponComponent::Server_StartFiring_Implementation()
{
	HandleStartFiring();
}

void UWeaponComponent::Server_StopFiring_Implementation()
{
	HandleStopFiring();
}

void UWeaponComponent::Server_Reload_Implementation()
{
	HandleReload();
}

// =====================================================================================
//  Firing — server logic
// =====================================================================================

void UWeaponComponent::HandleStartFiring()
{
	if (!HasAuthorityLocal() || !IsWeaponActive || !IsArmed)
		return;

	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (!ResolveEquipped(instance, row, state))
	{
		ScreenLog(TEXT("[UWeaponComponent::HandleStartFiring] Could not resolve the equipped weapon instance/row."));
		return;
	}

	// Guard: a burst still in progress finishes on its own timer; ignore re-press.
	if (CurrentFireMode == EFireMode::Burst && GetWorld() && GetWorld()->GetTimerManager().IsTimerActive(BurstFireRateTimer))
		return;
	
	bFiringHeld = true; // parity: FiringWeaponStarted

	// Parity: fire once immediately on press...
	TryFire();

	// ...then the fire mode decides the follow-up timer.
	UWorld* world = GetWorld();
	if (!world)
		return;

	switch (CurrentFireMode)
	{
	case EFireMode::FullAuto:
	{
		const float interval = 60.f / FMath::Max(state.Stats.FireRate_RPM, 1.f);
		world->GetTimerManager().SetTimer(FullAutoFireRateTimer, this, &UWeaponComponent::TryFire, interval, true);
		break;
	}
	case EFireMode::Burst:
	{
		const float interval = FMath::Max(state.Stats.TimeBetweenEachBurstBullet_Seconds, 0.01f);
		world->GetTimerManager().SetTimer(BurstFireRateTimer, this, &UWeaponComponent::TryFire, interval, true);
		break;
	}
	case EFireMode::SemiAuto:
	default:
		// Parity: no timer — the immediate shot was it.
		break;
	}
}

void UWeaponComponent::HandleStopFiring()
{
	
	if (!HasAuthorityLocal())
		return;
	bFiringHeld = false;
	ClearShooting();
	// Parity: burst is NOT cleared on release — an in-progress burst completes.
}

void UWeaponComponent::ClearShooting()
{
	// Parity: the BP's release path cleared ONLY the full-auto timer (the fire-mode
	// switch skipped it for semi and burst), then dropped the firing flag.
	if (CurrentFireMode == EFireMode::FullAuto)
	{
		if (UWorld* world = GetWorld())
			world->GetTimerManager().ClearTimer(FullAutoFireRateTimer);
	}
	bFiringHeld = false;
}

void UWeaponComponent::ClearBurst()
{
	// Parity: reset the burst counter and stop the burst timer.
	BurstBulletCounter = 0;
	if (UWorld* world = GetWorld())
		world->GetTimerManager().ClearTimer(BurstFireRateTimer);
}

void UWeaponComponent::TryFire()
{
	if (!HasAuthorityLocal())
	{
		ScreenLog(TEXT("[TryFire] RETURN: no authority (client-side call)."));
		return;
	}

	// Parity: IsValidFireState gates every shot (timers keep ticking; each tick re-checks).
	if (!IsValidFireState())
	{
		ScreenLog(FString::Printf(
			TEXT("[TryFire] RETURN: IsValidFireState FALSE — active=%d armed=%d reloading=%d mesh=%d meshAsset=%d"),
			IsWeaponActive ? 1 : 0,
			IsArmed ? 1 : 0,
			IsReloading ? 1 : 0,
			WeaponMesh ? 1 : 0,
			(WeaponMesh && WeaponMesh->GetSkeletalMeshAsset()) ? 1 : 0));
		return;
	}

	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (!ResolveEquipped(instance, row, state))
	{
		ScreenLog(FString::Printf(
			TEXT("[TryFire] RETURN: ResolveEquipped FAILED — armed=%d inv=%d itemId=%s row=%s"),
			IsArmed ? 1 : 0,
			WeaponSourceInventory ? 1 : 0,
			*EquippedItemId.ToString(),
			*EquippedRowName.ToString()));
		ClearShooting();
		ClearBurst();
		return;
	}

	// Fire-mode gates (parity: CanFireSemiAuto / CanFireBurst; full-auto's rate is its timer).
	if (CurrentFireMode == EFireMode::SemiAuto && !CanFireSemiAuto())
	{
		const float since = GetWorld() ? (GetWorld()->GetTimeSeconds() - LastFireTime) : -1.f;
		ScreenLog(FString::Printf(
			TEXT("[TryFire] RETURN: CanFireSemiAuto FALSE — since=%.3f required=%.3f (RPM=%.0f)"),
			since,
			60.f / FMath::Max(state.Stats.FireRate_RPM, 1.f),
			state.Stats.FireRate_RPM));
		return;
	}
	if (CurrentFireMode == EFireMode::Burst && !CanFireBurst())
	{
		ScreenLog(FString::Printf(
			TEXT("[TryFire] RETURN: CanFireBurst FALSE — counter=%d of %d"),
			BurstBulletCounter,
			FMath::Max(state.Stats.NumBurstBullets, 1)));
		ClearBurst(); // parity: failing the burst gate ends the burst
		return;
	}

	// Empty mag: stop, dry-fire feedback, and attempt the auto-reload (parity with the
	// BP's UpdateAmmoCount empty-mag behavior, applied on the attempt as well).
	if (state.AmmoCount.AmmoInMag <= 0)
	{
		ScreenLog(FString::Printf(TEXT("[TryFire] RETURN: mag empty (mag=%d) — auto-reloading."),
		                          state.AmmoCount.AmmoInMag));
		OnDryFire();
		ClearShooting();
		ClearBurst();
		HandleReload();
		return;
	}

	UWorld* world = GetWorld();
	if (!world)
	{
		ScreenLog(TEXT("[TryFire] RETURN: no World."));
		return;
	}
	const float now = world->GetTimeSeconds();

	// --- Aim: camera -> crosshair target point (trace 1 of Option C) ---
	FVector targetPoint;
	if (!ComputeAimTargetPoint(targetPoint))
	{
		ScreenLog(FString::Printf(TEXT("[TryFire] RETURN: ComputeAimTargetPoint FAILED — hasCachedAim=%d"),
		                          bHasCachedAim ? 1 : 0));
		return;
	}

	// --- Muzzle origin ---
	FVector muzzleLocation;
	if (WeaponMesh && WeaponMesh->DoesSocketExist(MuzzleSocketName))
		muzzleLocation = WeaponMesh->GetSocketLocation(MuzzleSocketName);
	else
	{
		muzzleLocation = WeaponMesh ? WeaponMesh->GetComponentLocation()
		                            : (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
		ScreenLog(FString::Printf(TEXT("[TryFire] Weapon mesh has no '%s' socket — firing from component origin. Add the socket to the weapon mesh."),
		                          *MuzzleSocketName.ToString()));
	}

	// --- Direction with spread (spread affects the REAL shot) ---
	FVector direction = (targetPoint - muzzleLocation).GetSafeNormal();
	if (direction.IsNearlyZero())
		direction = GetOwner() ? GetOwner()->GetActorForwardVector() : FVector::ForwardVector;

	// Spread source: the recoil component owns the heat model that the weapon's three
	// curves feed (ShotToHeat -> heat -> HeatToSpreadAngle), and it is the only thing
	// that honours SpreadAngleMultiplier / PerShotHeatMultiplier. Use it whenever it is
	// available (listen-server host, or the shooter's own machine). The standalone server
	// model remains the fallback for a dedicated server, which has no local heat.
	float spreadDeg;
	if (RecoilComponent && RecoilComponent->IsRecoilProfileReady())
		spreadDeg = RecoilComponent->GetCurrentSpreadAngle();
	else
		spreadDeg = ComputeSpreadAngleDeg(state.Recoil);

	// Movement/airborne spread is ADDITIVE, in degrees, applied outside the branch so it
	// affects both the plugin path and the server fallback. Additive rather than
	// multiplicative on purpose: a weapon whose curve starts at zero would otherwise be
	// pinpoint-accurate on a first shot taken mid-air, which is nonsense.
	spreadDeg += GetSpreadStateBonusDeg();
	

	if (spreadDeg > 0.f)
		direction = FMath::VRandCone(direction, FMath::DegreesToRadians(spreadDeg));

	// --- Spawn the projectile from the muzzle toward the target (trace 2 becomes the
	//     projectile's own physical travel: a muzzle inside a wall hits the wall) ---
	if (CVarWeaponDebugDraw.GetValueOnGameThread() == 1 || CVarWeaponDebugDraw.GetValueOnGameThread() == 2)
	{
		DrawDebugLine(world, muzzleLocation, muzzleLocation + direction * 5000.f, FColor::Red, false, 2.f, 0, 1.f);
		DrawDebugPoint(world, targetPoint, 12.f, FColor::Yellow, false, 2.f);
	}

	SpawnProjectile(muzzleLocation, direction, row);

	// --- Heat / timing bookkeeping ---
	ServerHeat += EvalCurve(state.Recoil.ShotToHeatCurve, ServerHeat, 1.f);
	LastFireTime = now;
	if (CurrentFireMode == EFireMode::Burst)
	{
		++BurstBulletCounter;
		LastBurstBulletFireTime = now;
		if (BurstBulletCounter >= FMath::Max(state.Stats.NumBurstBullets, 1))
			ClearBurst();
	}

	// --- Ammo decrement (writes the instance back through the inventory; also handles
	//     the mag-hits-zero auto-reload parity) ---
	FAmmoCount newAmmo = state.AmmoCount;
	newAmmo.AmmoInMag = FMath::Max(0, newAmmo.AmmoInMag - 1);
	UpdateAmmoCount(newAmmo);

	// --- Cosmetics on every machine ---
	Multicast_PlayFireFX();
}

bool UWeaponComponent::ComputeLocalAimPoint(FVector& OutPoint) const
{
	const APawn* pawn = Cast<APawn>(GetOwner());
	UWorld* world = GetWorld();
	if (!pawn || !world)
		return false;

	// Parity with the BP: FollowCamera location + camera forward * range, traced.
	// Only meaningful on the machine that owns the camera (the local client).
	FVector viewLocation;
	FVector viewDirection;
	if (const UCameraComponent* camera = pawn->FindComponentByClass<UCameraComponent>())
	{
		viewLocation = camera->GetComponentLocation();
		viewDirection = camera->GetForwardVector();
	}
	else
	{
		viewLocation = pawn->GetPawnViewLocation();
		viewDirection = pawn->GetControlRotation().Vector();
	}

	const FVector traceEnd = viewLocation + viewDirection * AimTraceRange;

	FCollisionQueryParams params(FName(TEXT("WeaponAimTrace")), /*bTraceComplex*/ false, GetOwner());
	FHitResult hit;
	const bool bHit = world->LineTraceSingleByChannel(hit, viewLocation, traceEnd, ECC_Visibility, params);

	OutPoint = bHit ? hit.ImpactPoint : traceEnd;

	if (CVarWeaponDebugDraw.GetValueOnGameThread() == 2)
		DrawDebugLine(world, viewLocation, OutPoint, FColor::Cyan, false, 2.f, 0, 1.f);
	return true;
}

void UWeaponComponent::UpdateAimPoint()
{
	FVector point;
	if (!ComputeLocalAimPoint(point))
		return;

	CachedAimPoint = point;
	bHasCachedAim = true;

	if (!HasAuthorityLocal())
		Server_SetAimPoint(point);
}

void UWeaponComponent::Server_SetAimPoint_Implementation(FVector_NetQuantize AimPoint)
{
	CachedAimPoint = AimPoint;
	bHasCachedAim = true;
}

bool UWeaponComponent::ComputeAimTargetPoint(FVector& OutTargetPoint) const
{
	// The authoritative shot uses the aim point the OWNING CLIENT computed from its own
	// camera. The server has no valid view of a remote client's camera, so reading the
	// camera here would aim somewhere else entirely.
	if (bHasCachedAim)
	{
		OutTargetPoint = CachedAimPoint;
		return true;
	}

	// Fallback (listen-server host firing locally before any aim update arrived).
	return ComputeLocalAimPoint(OutTargetPoint);
}

float UWeaponComponent::ComputeSpreadAngleDeg(const FWeaponRecoil& Recoil)
{
	DecayHeat(Recoil);

	float spread = EvalCurve(Recoil.HeatToSpreadAngleCurve, ServerHeat, 0.f);

	// NOTE: the movement/airborne bonus is NOT applied here. TryFire adds it once, after
	// choosing between this fallback and the plugin's heat model, so it cannot be
	// double-counted.
	return FMath::Max(spread, 0.f);
}

void UWeaponComponent::DecayHeat(const FWeaponRecoil& Recoil)
{
	UWorld* world = GetWorld();
	if (!world)
		return;
	const float now = world->GetTimeSeconds();
	const float delta = now - LastHeatUpdateTime;
	if (delta > 0.f)
	{
		const float coolPerSecond = EvalCurve(Recoil.HeatToCooldownPerSecondCurve, ServerHeat, 0.f);
		ServerHeat = FMath::Max(0.f, ServerHeat - coolPerSecond * delta);
	}
	LastHeatUpdateTime = now;
}

void UWeaponComponent::SpawnProjectile(const FVector& MuzzleLocation, const FVector& Direction, const FWeaponRow& Row)
{
	UWorld* world = GetWorld();
	if (!world)
		return;

	UClass* projectileClass = Row.Design.ProjectileClass;
	if (!projectileClass)
	{
		ScreenLog(FString::Printf(TEXT("[UWeaponComponent::SpawnProjectile] Weapon row '%s' has no Design.ProjectileClass set."),
		                          *EquippedRowName.ToString()));
		return;
	}

	const FTransform spawnTransform(Direction.Rotation(), MuzzleLocation);
	AActor* projectile = world->SpawnActorDeferred<AActor>(projectileClass, spawnTransform, GetOwner(),
	                                                       Cast<APawn>(GetOwner()),
	                                                       ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!projectile)
	{
		ScreenLog(TEXT("[UWeaponComponent::SpawnProjectile] Projectile spawn failed."));
		return;
	}

	// Damage seam: hand the weapon's damage (and anything else) to the projectile here.
	// Implement ConfigureProjectile in your BP subclass when the health/shield system exists.
	FItemInstance instance;
	FWeaponRow resolvedRow;
	FWeaponItemFragment state;

	// Live instance first; fall back to the row's authored default fragment for the window
	// before the instance has replicated in.
	float damage = 0.f;
	if (ResolveEquipped(instance, resolvedRow, state))
		damage = state.Stats.Damage;
	else if (const FWeaponItemFragment* defaultFragment = Row.ItemDetails.GetDefaultFragment<FWeaponItemFragment>())
		damage = defaultFragment->Stats.Damage;
	ConfigureProjectile(projectile, damage);

	projectile->FinishSpawning(spawnTransform);
}

// =====================================================================================
//  Ammo / reload (pooled model)
// =====================================================================================

void UWeaponComponent::UpdateAmmoCount(const FAmmoCount& NewAmmo)
{
	if (!HasAuthorityLocal() || !WeaponSourceInventory || !EquippedItemId.IsValid())
		return;

	FItemInstance instance;
	if (!WeaponSourceInventory->GetItem(EquippedItemId, instance))
	{
		ScreenLog(TEXT("[UWeaponComponent::UpdateAmmoCount] Equipped item id no longer resolves in the weapon-source inventory."));
		return;
	}

	// Mutate the fragment ON the fetched instance: writing to a copy would be discarded.
	FWeaponItemFragment* mutableState = instance.GetMutableFragment<FWeaponItemFragment>();
	if (!mutableState)
	{
		ScreenLog(TEXT("[UWeaponComponent::UpdateAmmoCount] Equipped item carries no weapon fragment."));
		return;
	}

	mutableState->AmmoCount = NewAmmo;
	RefreshInstanceReserveMirror(instance);

	// Writes through the inventory so ammo replicates and persists with the item.
	WeaponSourceInventory->SetItemInstance(EquippedItemId, instance);
	OnAmmoChanged.Broadcast(mutableState->AmmoCount.AmmoInMag, mutableState->AmmoCount.AmmoTotal);

	// Parity (BP UpdateAmmoCount): the mag hitting zero clears the burst and auto-reloads.
	if (mutableState->AmmoCount.AmmoInMag == 0)
	{
		ClearBurst();
		ClearShooting();
		HandleReload();
	}
}

void UWeaponComponent::RefreshInstanceReserveMirror(FItemInstance& Instance) const
{
	// Pool model: AmmoTotal mirrors the current pool so UI can read one struct.
	FWeaponRow row;
	if (AmmoSourceInventory && UItemLibrary::GetItemDefTyped<FWeaponRow>(Instance.RowName, row) && row.AmmoType != NAME_None)
	{
		if (FWeaponItemFragment* mirrorState = Instance.GetMutableFragment<FWeaponItemFragment>())
			mirrorState->AmmoCount.AmmoTotal = AmmoSourceInventory->GetTotalQuantityByRowName(row.AmmoType);
	}
}

void UWeaponComponent::HandleReload()
{
	if (!HasAuthorityLocal() || IsReloading || !IsArmed || !IsWeaponActive)
		return;

	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (!ResolveEquipped(instance, row, state))
		return;

	const int32 magSize = state.Stats.MagSize;
	if (state.AmmoCount.AmmoInMag >= magSize)
		return; // already full

	if (row.AmmoType == NAME_None)
	{
		ScreenLog(FString::Printf(TEXT("[UWeaponComponent::HandleReload] Weapon row '%s' has no AmmoType set — cannot reload from the pool."),
		                          *EquippedRowName.ToString()));
		return;
	}

	if (!AmmoSourceInventory || AmmoSourceInventory->GetTotalQuantityByRowName(row.AmmoType) <= 0)
		return; // no reserve — nothing to load

	// Reloading cancels firing (parity: the reload flow gated firing with the tag).
	ClearShooting();
	ClearBurst();

	IsReloading = true;
	Multicast_PlayReloadFX();

	if (UWorld* world = GetWorld())
	{
		world->GetTimerManager().SetTimer(ReloadTimer, this, &UWeaponComponent::FinishReload,
		                                  FMath::Max(ReloadDuration, 0.01f), false);
	}
}

void UWeaponComponent::FinishReload()
{
	IsReloading = false;

	if (!HasAuthorityLocal())
		return;

	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (!ResolveEquipped(instance, row, state) || !AmmoSourceInventory)
		return;

	// Pool consumption: pull what the mag needs from AmmoType stacks in the backpack —
	// first stack found, then the next, and so on; partial reload if the pool runs dry.
	const int32 magSize = state.Stats.MagSize;
	const int32 needed = magSize - state.AmmoCount.AmmoInMag;
	if (needed <= 0)
		return;

	const int32 consumed = AmmoSourceInventory->ConsumeItemsByRowName(row.AmmoType, needed);
	if (consumed <= 0)
		return;

	FAmmoCount newAmmo = state.AmmoCount;
	newAmmo.AmmoInMag += consumed;
	UpdateAmmoCount(newAmmo);
}

// =====================================================================================
//  FX
// =====================================================================================

void UWeaponComponent::Multicast_PlayFireFX_Implementation()
{
	// Local cosmetic heat mirror for camera-shake scaling (decay + per-shot heat, using
	// the same curves as the server model so the feel matches the real spread).
	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (ResolveEquipped(instance, row, state))
	{
		if (UWorld* world = GetWorld())
		{
			const float now = world->GetTimeSeconds();
			const float delta = now - LastLocalShakeTime;
			if (delta > 0.f)
			{
				const float cool = EvalCurve(state.Recoil.HeatToCooldownPerSecondCurve, LocalShakeHeat, 0.f);
				LocalShakeHeat = FMath::Max(0.f, LocalShakeHeat - cool * delta);
			}
			LastLocalShakeTime = now;
		}
		LocalShakeHeat += EvalCurve(state.Recoil.ShotToHeatCurve, LocalShakeHeat, 1.f);
	}

	PlayMuzzleFlash();
	PlayFireSound();
	PlayWeaponSmoke();

	const APawn* ownerPawn = Cast<APawn>(GetOwner());
	if (ownerPawn && ownerPawn->IsLocallyControlled())
	{
		ShotRecoilFX();
		PlayCameraShake();
	}
}

void UWeaponComponent::Multicast_PlayReloadFX_Implementation()
{
	PlayReloadEffects();
}

void UWeaponComponent::PlayMuzzleFlash_Implementation()
{
	if (!WeaponMesh || EquippedRowName == NAME_None)
		return;

	FWeaponRow row;
	if (!UItemLibrary::GetItemDefTyped<FWeaponRow>(EquippedRowName, row))
		return;

	UNiagaraSystem* flashSystem = row.Design.MuzzleFlash.LoadSynchronous();
	if (flashSystem)
	{
		UNiagaraFunctionLibrary::SpawnSystemAttached(flashSystem, WeaponMesh, MuzzleSocketName,
													 FVector::ZeroVector, FRotator::ZeroRotator,
													 EAttachLocation::SnapToTarget, /*bAutoDestroy*/ true);
	}
}

void UWeaponComponent::PlayCameraShake_Implementation()
{
	if (!CameraShakeClass)
		return;

	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	float scale = 1.f;
	if (ResolveEquipped(instance, row, state))
		scale = EvalCurve(state.Recoil.CameraShakeCurve, LocalShakeHeat, 1.f);

	const APawn* ownerPawn = Cast<APawn>(GetOwner());
	if (APlayerController* pc = ownerPawn ? Cast<APlayerController>(ownerPawn->GetController()) : nullptr)
		pc->ClientStartCameraShake(CameraShakeClass, scale);
}

// Stubs / BP-implemented hooks — native defaults intentionally empty.
void UWeaponComponent::OnStartFiringFX_Implementation(const FWeaponRecoil& Recoil)
{
	if (!RecoilComponent)
		return;

	// Push the current profile (state multipliers included) so the pattern, curves and
	// tuning knobs are live BEFORE the first shot is consumed.
	RecoilComponent->ApplyRecoilProfile(Recoil, GetRecoilStateMultiplier());

	// Rewind the pattern to index 0 only once the weapon has FULLY settled (no kick left
	// to apply, nothing left to recover). Otherwise the pattern continues where the last
	// press left off, so tap-firing walks the pattern instead of replaying its first
	// entry forever.
	//
	// Either way BeginFireSequence refreshes the per-press recovery tracking that
	// StartShooting would normally reset. Without that, accumulated aim input builds up
	// across taps until it trips the pattern's RecoveryCancelThreshold, at which point
	// the plugin abandons the remaining recovery and the camera parks above centre.
	RecoilComponent->BeginFireSequence(IsRecoilFullySettled());
}

bool UWeaponComponent::IsRecoilFullySettled() const
{
	if (!RecoilComponent)
		return true;

	// Sub-degree residue counts as settled; exact zero is never reached while interpolating.
	return RecoilComponent->GetRecoilToApply().IsNearlyZero(RecoilSettleTolerance)
		&& RecoilComponent->GetRecoilToRecover().IsNearlyZero(RecoilSettleTolerance);
}

void UWeaponComponent::OnStopFiringFX_Implementation()
{
	// Deliberately empty. The plugin has no StopShooting: recovery starts on its own
	// after RecoveryDelay, and the pattern index must SURVIVE the release so tap-firing
	// keeps walking the pattern. The rewind happens in OnStartFiringFX, gated on
	// IsRecoilFullySettled().
}

void UWeaponComponent::ShotRecoilFX_Implementation()
{
	if (RecoilComponent)
		// Plugin no-ops unless this is the local player controller.
		RecoilComponent->FireShotRecoil();
}

void UWeaponComponent::PlayFireSound_Implementation() {}
void UWeaponComponent::PlayWeaponSmoke_Implementation() {}
void UWeaponComponent::OnDryFire_Implementation() {}
void UWeaponComponent::PlayReloadEffects_Implementation() {}
void UWeaponComponent::ConfigureProjectile_Implementation(AActor* Projectile, float Damage)
{
	if (!Projectile)
		return;

	if (UFunction* func = Projectile->FindFunction(FName("SetOwningWeapon")))
	{
		struct { UWeaponComponent* Weapon; } params{ this };
		Projectile->ProcessEvent(func, &params);
	}
}
void UWeaponComponent::OnAttachmentEquipped_Implementation(ESlotType AttachmentSlot, const FItemInstance& AttachmentInstance, const FAttachmentRow& AttachmentRow) {}

// =====================================================================================
//  Attachments (parity: SetEquippedAttachment / SetAttachmentDetails)
// =====================================================================================

void UWeaponComponent::SetEquippedAttachment(ESlotType AttachmentSlot, FGuid AttachmentItemId)
{
	if (!WeaponSourceInventory || !AttachmentItemId.IsValid())
		return;

	// Attachment instances travel with the weapon in the same store (the closure).
	FItemInstance attachmentInstance;
	if (!WeaponSourceInventory->GetItem(AttachmentItemId, attachmentInstance))
	{
		ScreenLog(FString::Printf(TEXT("[UWeaponComponent::SetEquippedAttachment] Attachment id %s not found in the weapon-source inventory."),
		                          *AttachmentItemId.ToString()));
		return;
	}

	FAttachmentRow attachmentRow;
	if (!UItemLibrary::GetItemDefTyped<FAttachmentRow>(attachmentInstance.RowName, attachmentRow))
	{
		ScreenLog(FString::Printf(TEXT("[UWeaponComponent::SetEquippedAttachment] Could not find attachment row for '%s'."),
		                          *attachmentInstance.RowName.ToString()));
		return;
	}

	// Stat effects already live on the weapon instance's FWeaponItemFragment (computed
	// with the item data); this hook is the visual/extension seam (attach meshes, etc.).
	OnAttachmentEquipped(AttachmentSlot, attachmentInstance, attachmentRow);
}

void UWeaponComponent::RefreshAttachments()
{
	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (!ResolveEquipped(instance, row, state))
		return;

	for (const TPair<ESlotType, FGuid>& pair : state.Attachments)
	{
		if (pair.Value.IsValid())
			SetEquippedAttachment(pair.Key, pair.Value);
	}
	
	RefreshRecoilProfile();
	
}

void UWeaponComponent::RefreshRecoilProfile()
{
	if (!RecoilComponent)
		return;
	
	// Instance first (attachments mutate the weapon fragment), row as the
	// fallback before inventory replication lands on this machine.
	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (ResolveEquipped(instance, row, state))
		RecoilComponent->ApplyRecoilProfile(state.Recoil, GetRecoilStateMultiplier());
	else if (EquippedRowName != NAME_None
			 && UItemLibrary::GetItemDefTyped<FWeaponRow>(EquippedRowName, row))
	{
		if (const FWeaponItemFragment* defaultFragment = row.ItemDetails.GetDefaultFragment<FWeaponItemFragment>())
			RecoilComponent->ApplyRecoilProfile(defaultFragment->Recoil, GetRecoilStateMultiplier());
	}
}
 
float UWeaponComponent::GetRecoilStateMultiplier() const
{
	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (!ResolveEquipped(instance, row, state))
		return 1.f;
	const FWeaponRecoil& recoil = state.Recoil;

	// STANCE scales the camera KICK, and only the kick. Multipliers stack, so crouched
	// while aiming is tighter than either alone. A value <= 0 means "not configured".
	float multi = 1.f;

	if (const ACharacter* character = GetOwningCharacter())
	{
		if (character->bIsCrouched && recoil.RecoilMulti_Crouched > 0.f)
			multi *= recoil.RecoilMulti_Crouched;
	}

	// ADS: multiply by Recoil.RecoilMulti_ADS here once an aiming state exists.

	return multi;
}

float UWeaponComponent::GetSpreadStateBonusDeg() const
{
	FItemInstance instance;
	FWeaponRow row;
	FWeaponItemFragment state;
	if (!ResolveEquipped(instance, row, state))
		return 0.f;
	const FWeaponRecoil& recoil = state.Recoil;

	// MOVEMENT adds a flat spread floor in DEGREES on top of whatever the heat model
	// produces. In-air supersedes moving, since a falling player is always moving.
	if (const ACharacter* character = GetOwningCharacter())
	{
		if (const UCharacterMovementComponent* move = character->GetCharacterMovement())
		{
			if (move->IsFalling())
				return FMath::Max(0.f, recoil.SpreadAdd_InAir);
			if (character->GetVelocity().Size2D() > MovingSpeedThreshold)
				return FMath::Max(0.f, recoil.SpreadAdd_Moving);
		}
	}
	return 0.f;
}

// =============================================================================================================
// Impact FX
// =============================================================================================================

void UWeaponComponent::Multicast_SpawnImpactEffect_Implementation(FVector_NetQuantize ImpactLocation, FVector_NetQuantizeNormal ImpactNormal, UPhysicalMaterial* PhysMaterial)
{
	UWorld* world = GetWorld();
	if (!world)
		return;

	if (UMaterialInterface* decalMat = ResolveDecalMaterialForSurface(PhysMaterial))
	{
		const FRotator decalRotation = ImpactNormal.Rotation();
		UGameplayStatics::SpawnDecalAtLocation(world, decalMat, FVector(4.f), ImpactLocation, decalRotation, 30.f);
	}

	if (UPhysicalMaterial_Gameplay* surfaceData = UPhysicalMaterial_Gameplay::Resolve(PhysMaterial))
	{
		if (UNiagaraSystem* particle = surfaceData->ImpactParticle.LoadSynchronous())
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(world, particle, ImpactLocation, ImpactNormal.Rotation());
	}
}

void UWeaponComponent::Multicast_PlayImpactSound_Implementation(FVector_NetQuantize ImpactLocation, UPhysicalMaterial* PhysMaterial)
{
	UWorld* world = GetWorld();
	if (!world)
		return;

	if (USoundBase* impactSound = ResolveImpactSoundForSurface(PhysMaterial))
		UGameplayStatics::PlaySoundAtLocation(world, impactSound, ImpactLocation);
}

bool UWeaponComponent::ResolveEquippedAmmoRow(FAmmoRow& OutAmmoRow) const
{
	FWeaponRow weaponRow;
	if (!GetEquippedWeaponRow(weaponRow) || weaponRow.AmmoType == NAME_None)
		return false;

	return UItemLibrary::GetItemDefTyped<FAmmoRow>(weaponRow.AmmoType, OutAmmoRow);
}

UMaterialInterface* UWeaponComponent::ResolveDecalMaterialForSurface(UPhysicalMaterial* PhysMaterial) const
{
	if (UPhysicalMaterial_Gameplay* surfaceData = UPhysicalMaterial_Gameplay::Resolve(PhysMaterial))
	{
		if (UMaterialInterface* decal = surfaceData->ImpactDecal.LoadSynchronous())
			return decal;
	}
	return DefaultImpactDecal.LoadSynchronous();
}

USoundBase* UWeaponComponent::ResolveImpactSoundForSurface(UPhysicalMaterial* PhysMaterial) const
{
	if (UPhysicalMaterial_Gameplay* surfaceData = UPhysicalMaterial_Gameplay::Resolve(PhysMaterial))
	{
		if (USoundBase* sound = surfaceData->ImpactSound.LoadSynchronous())
			return sound;
	}
	return DefaultImpactSound.LoadSynchronous();
}