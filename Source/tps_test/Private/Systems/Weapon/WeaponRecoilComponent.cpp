// Copyright arubaii. All rights reserved.

#include "Public/Systems/Weapon/WeaponRecoilComponent.h"
#include "Data/CRRecoilPattern.h"

void UWeaponRecoilComponent::SetSpreadCurves(const FRuntimeFloatCurve& InShotToHeat,
                                             const FRuntimeFloatCurve& InHeatToSpreadAngle,
                                             const FRuntimeFloatCurve& InHeatToCooldownPerSecond)
{
	// Protected in UCRRecoilSpreadComponent — reachable here because we derive from it.
	ShotToHeatCurve = InShotToHeat;
	HeatToSpreadAngleCurve = InHeatToSpreadAngle;
	HeatToCooldownPerSecondCurve = InHeatToCooldownPerSecond;
}

void UWeaponRecoilComponent::ResetRecoilHeat()
{
	SetRecoilHeat(0.f); // protected setter, accessible to the subclass
}

bool UWeaponRecoilComponent::IsRecoilProfileReady() const
{
	return ReadyToCalculateRecoil();
}

void UWeaponRecoilComponent::BeginFireSequence(bool bRewindPattern)
{
	if (bRewindPattern)
	{
		// Full restart: rewinds the pattern to index 0 and clears the per-press state.
		StartShooting();
		return;
	}

	// Continue the pattern where the last press left off, but still clear the accumulated
	// aim input. StartShooting() is normally responsible for that reset; skipping it lets
	// input accrue across taps until it trips RecoveryCancelThreshold, at which point the
	// plugin abandons the remaining recovery and the camera parks above centre.
	AccumulatedInputDuringFire = FRotator::ZeroRotator;
	if (RecoilPattern)
	{
		bTrackingInputDuringFire = RecoilPattern->RecoveryDelay > 0.f
			&& RecoilPattern->RecoveryCancelThreshold > 0.f;
		SetComponentTickEnabled(true);
	}
}

void UWeaponRecoilComponent::ApplyRecoilProfile(const FWeaponRecoil& Profile, float StateMultiplier)
{
	// ---------------------------------------------------------------------------------
	// THE single place weapon data meets the recoil plugin. Add new FWeaponRecoil fields
	// here and nowhere else.
	// ---------------------------------------------------------------------------------

	const float Scale = (StateMultiplier > 0.f) ? StateMultiplier : 1.f;

	// --- Pattern + recovery -----------------------------------------------------------
	if (Profile.RecoilPattern)
	{
		SetRecoilPattern(Profile.RecoilPattern);
	}
	SetRecoveryEnabled(Profile.bRecoveryEnabled);

	// --- Spread curves (protected in the plugin; this subclass is the way in) ---------
	SetSpreadCurves(Profile.ShotToHeatCurve, Profile.HeatToSpreadAngleCurve, Profile.HeatToCooldownPerSecondCurve);

	// --- Strength knobs ---------------------------------------------------------------
	// A value <= 0 is treated as "not configured" (1.0) so an unfilled row can't silently
	// zero out all recoil. StateMultiplier is the STANCE scalar (crouched / ADS) and
	// applies to the camera kick only.
	auto Knob = [](float Value, float Default) { return (Value > 0.f) ? Value : Default; };

	SetRecoilStrength(Knob(Profile.RecoilStrength, 1.f) * Scale);
	SetHorizontalRecoilStrength(Knob(Profile.HorizontalRecoilStrength, 1.f));
	SetVerticalRecoilStrength(Knob(Profile.VerticalRecoilStrength, 1.f));
	SetRecoverySpeedMultiplier(Knob(Profile.RecoverySpeedMultiplier, 1.f));

	// --- Jitter -----------------------------------------------------------------------
	// Jitter may legitimately be 0 (no jitter), so it is passed through unguarded.
	SetHorizontalJitter(FMath::Max(0.f, Profile.HorizontalJitter), Profile.bScaleHorizontalJitter);
	SetVerticalJitter(FMath::Max(0.f, Profile.VerticalJitter), Profile.bScaleVerticalJitter);
	JitterScaleFactor = FMath::Max(0.f, Profile.JitterScaleFactor);

	// --- Spread / heat tuning ---------------------------------------------------------
	// NO stance scalar here: stance tightens the kick, not the cone. The movement spread
	// scalar is applied per shot in UWeaponComponent::TryFire, where it can change as the
	// player starts and stops moving mid-burst.
	SetSpreadAngleMultiplier(Knob(Profile.SpreadAngleMultiplier, 1.f));
	SetPerShotHeatMultiplier(Knob(Profile.PerShotHeatMultiplier, 1.f));
	SetMaxRecoilHeat(Knob(Profile.MaxRecoilHeat, 100.f));
	SetRecoilHeatCoolDownDelay(FMath::Max(0.f, Profile.RecoilHeatCooldownDelay));
}

void UWeaponRecoilComponent::FireShotRecoil()
{
	ApplyShot(); // accessible here — we derive from the class that declares it protected
}