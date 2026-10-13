// Copyright AR GameSystems 2026. All rights reserved.

#include "DiegeticUISubsystem.h"

#include "Components/WidgetInteractionComponent.h"
#include "DiegeticInteractorComponent.h"
#include "DiegeticScreenComponent.h"
#include "DiegeticUISettings.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("NameTBD Diegetic UI"), STATGROUP_NameTBDDiegetic, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Schedule Screens"), STAT_DiegeticSchedule, STATGROUP_NameTBDDiegetic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Screens"), STAT_DiegeticScreens, STATGROUP_NameTBDDiegetic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Redraws Scheduled"), STAT_DiegeticRedraws, STATGROUP_NameTBDDiegetic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Waiting On Budget"), STAT_DiegeticWaiting, STATGROUP_NameTBDDiegetic);
DECLARE_DWORD_COUNTER_STAT(TEXT("Asleep"), STAT_DiegeticAsleep, STATGROUP_NameTBDDiegetic);

namespace DiegeticUISubsystemPrivate
{
	static TAutoConsoleVariable<int32> CVarDebug(
		TEXT("NameTBD.Diegetic.Debug"),
		0,
		TEXT("1: draws each Diegetic Screen's update mode, distance, redraws a second and state over it."),
		ECVF_Cheat);

	static TAutoConsoleVariable<int32> CVarBudget(
		TEXT("NameTBD.Diegetic.Budget"),
		-1,
		TEXT("Diegetic Screens redrawn a frame (By Distance and When Changed ones). -1: Project Settings > Diegetic UI > Max Redraws Per Frame. 0: no limit."),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarFreeze(
		TEXT("NameTBD.Diegetic.Freeze"),
		0,
		TEXT("1: no Diegetic Screen redraws on its schedule (Every Frame ones and ones pointed at still do): what the scheduling saves."),
		ECVF_Cheat);

	/** A screen waiting for a redraw, and how badly it wants one. */
	struct FCandidate
	{
		UDiegeticScreenComponent* Screen = nullptr;
		int32 Priority = 0;
		double Overdue = 0.0;
	};

	/** Debug colours (runtime: not NameTBDColors.h, which is the editor's). */
	const FColor DrawnColor(90, 220, 120);
	const FColor WaitingColor(240, 200, 80);
	const FColor AsleepColor(140, 140, 140);
}

UDiegeticUISubsystem* UDiegeticUISubsystem::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext && GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDiegeticUISubsystem>() : nullptr;
}

bool UDiegeticUISubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDiegeticUISubsystem::Deinitialize()
{
	for (const TWeakObjectPtr<UDiegeticScreenComponent>& Screen : Screens)
	{
		if (UDiegeticScreenComponent* Live = Screen.Get())
		{
			Live->SetScheduled(false);
		}
	}
	Screens.Reset();
	Interactors.Reset();
	CheckedPawns.Reset();
	Super::Deinitialize();
}

void UDiegeticUISubsystem::RegisterScreen(UDiegeticScreenComponent* Screen)
{
	if (Screen)
	{
		Screens.AddUnique(Screen);
		Screen->SetScheduled(true);
	}
}

void UDiegeticUISubsystem::UnregisterScreen(UDiegeticScreenComponent* Screen)
{
	Screens.Remove(Screen);
	if (Screen)
	{
		Screen->SetScheduled(false);
	}
}

void UDiegeticUISubsystem::RegisterInteractor(UDiegeticInteractorComponent* Interactor)
{
	if (Interactor)
	{
		Interactors.AddUnique(Interactor);
	}
}

void UDiegeticUISubsystem::UnregisterInteractor(UDiegeticInteractorComponent* Interactor)
{
	Interactors.Remove(Interactor);
}

UDiegeticInteractorComponent* UDiegeticUISubsystem::FindInteractor(const APlayerController* Player) const
{
	for (const TWeakObjectPtr<UDiegeticInteractorComponent>& Interactor : Interactors)
	{
		UDiegeticInteractorComponent* Live = Interactor.Get();
		if (Live && Live->GetInteractingPlayer() == Player)
		{
			return Live;
		}
	}
	return nullptr;
}

TStatId UDiegeticUISubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDiegeticUISubsystem, STATGROUP_Tickables);
}

void UDiegeticUISubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	SCOPE_CYCLE_COUNTER(STAT_DiegeticSchedule);

	Screens.RemoveAll([](const TWeakObjectPtr<UDiegeticScreenComponent>& Each) { return !Each.IsValid(); });
	Interactors.RemoveAll([](const TWeakObjectPtr<UDiegeticInteractorComponent>& Each) { return !Each.IsValid(); });
	SET_DWORD_STAT(STAT_DiegeticScreens, Screens.Num());
	if (Screens.Num() == 0)
	{
		return;
	}

	GatherViews();
	AddInteractors();
	// Real time: a paused game's screens still answer the player, as its HUD does.
	ScheduleScreens(FPlatformTime::Seconds(), static_cast<float>(FApp::GetDeltaTime()));
	DrawDebug();
}

void UDiegeticUISubsystem::GatherViews()
{
	ViewLocations.Reset();
	PawnLocations.Reset();
	bHasFirstView = false;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Player = It->Get();
		if (!Player || !Player->IsLocalController())
		{
			continue;
		}
		FVector ViewLocation;
		FRotator ViewRotation;
		Player->GetPlayerViewPoint(ViewLocation, ViewRotation);
		ViewLocations.Add(ViewLocation);
		if (!bHasFirstView)
		{
			bHasFirstView = true;
			FirstViewLocation = ViewLocation;
			FirstViewRotation = ViewRotation;
		}
		const APawn* Pawn = Player->GetPawn();
		PawnLocations.Add(Pawn ? Pawn->GetActorLocation() : ViewLocation);
	}
}

void UDiegeticUISubsystem::AddInteractors()
{
	const UDiegeticUISettings& Settings = UDiegeticUISettings::Get();
	UWorld* World = GetWorld();
	if (!Settings.bAutoAddInteractor || !World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Player = It->Get();
		APawn* Pawn = Player && Player->IsLocalController() ? Player->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		const TWeakObjectPtr<APawn>* Checked = CheckedPawns.Find(TObjectKey<APlayerController>(Player));
		if (Checked && Checked->Get() == Pawn)
		{
			continue;
		}
		CheckedPawns.Add(TObjectKey<APlayerController>(Player), Pawn);

		// A pawn that points at widgets its own way keeps its way.
		if (Pawn->FindComponentByClass<UWidgetInteractionComponent>() || Player->FindComponentByClass<UWidgetInteractionComponent>())
		{
			continue;
		}

		UDiegeticInteractorComponent* Added = NewObject<UDiegeticInteractorComponent>(Pawn,
			MakeUniqueObjectName(Pawn, UDiegeticInteractorComponent::StaticClass(), TEXT("DiegeticInteractor")), RF_Transient);
		Added->InteractionDistance = Settings.DefaultInteractionDistance;
		// Each local player its own virtual Slate user: split-screen players hover and focus apart. Set before it registers,
		// which is when the virtual user is made.
		if (const ULocalPlayer* Local = Player->GetLocalPlayer())
		{
			Added->VirtualUserIndex = Local->GetLocalPlayerIndex();
		}
		Added->SetupAttachment(Pawn->GetRootComponent());
		Added->RegisterComponent();
		Pawn->AddInstanceComponent(Added);
	}
}

void UDiegeticUISubsystem::ScheduleScreens(double Now, float RealDeltaTime)
{
	using namespace DiegeticUISubsystemPrivate;

	// Half a frame of slack, so a 20 a second screen at 60 frames a second redraws every third frame rather than drifting.
	const double HalfFrame = 0.5 * FMath::Max(static_cast<double>(RealDeltaTime), 0.0);
	const bool bFrozen = CVarFreeze.GetValueOnGameThread() != 0;

	TArray<FCandidate, TInlineAllocator<32>> Candidates;
	int32 NumDrawn = 0;
	int32 NumAsleep = 0;

	for (const TWeakObjectPtr<UDiegeticScreenComponent>& Weak : Screens)
	{
		UDiegeticScreenComponent* Screen = Weak.Get();
		if (!Screen)
		{
			continue;
		}
		Screen->SyncRegistration();
		Screen->FlushWidgetState();
		Screen->SetDrawScheduled(false);
		if (bHasFirstView && Screen->PlacementSettings.Placement != EDiegeticPlacement::AsPlaced && Screen->IsAwake())
		{
			Screen->UpdatePlacement(FirstViewLocation, FirstViewRotation, RealDeltaTime);
		}

		// Distances: to the nearest camera (redraws) and the nearest pawn (presence), from what shows it - its panel or its mesh.
		// Whether a camera is in front of its face (+X).
		const UPrimitiveComponent* Display = Screen->GetDisplayPrimitive();
		const FBoxSphereBounds& DisplayBounds = Display->Bounds;
		const FVector Center = Screen->GetDisplayCenter();
		const FVector Face = Screen->GetForwardVector();
		const bool bTwoSided = Screen->GetTwoSided() || Screen->GetGeometryMode() != EWidgetGeometryMode::Plane;
		double ViewDistanceSquared = ViewLocations.Num() > 0 ? TNumericLimits<double>::Max() : 0.0;
		bool bAnyInFront = ViewLocations.Num() == 0 || bTwoSided;
		for (const FVector& View : ViewLocations)
		{
			ViewDistanceSquared = FMath::Min(ViewDistanceSquared, DisplayBounds.ComputeSquaredDistanceFromBoxToPoint(View));
			bAnyInFront |= FVector::DotProduct(View - Center, Face) > 0.0;
		}
		double PawnDistanceSquared = PawnLocations.Num() > 0 ? TNumericLimits<double>::Max() : 0.0;
		for (const FVector& PawnLocation : PawnLocations)
		{
			PawnDistanceSquared = FMath::Min(PawnDistanceSquared, DisplayBounds.ComputeSquaredDistanceFromBoxToPoint(PawnLocation));
		}
		const float ViewDistance = static_cast<float>(FMath::Sqrt(ViewDistanceSquared));
		Screen->SetMeasurements(ViewDistance, static_cast<float>(FMath::Sqrt(PawnDistanceSquared)), bAnyInFront);
		Screen->UpdatePresence();

		if (!Screen->IsVisible() || !Screen->GetUserWidgetObject())
		{
			continue;
		}
		if (!Screen->IsAwake())
		{
			++NumAsleep;
			if (!Screen->WantsRedrawWhileAsleep(Now))
			{
				continue;
			}
		}

		const FDiegeticUpdateSettings& Updates = Screen->UpdateSettings;
		const bool bInteracting = Screen->IsAimedAt() || Screen->IsInUse();
		if (Updates.UpdateMode == EDiegeticUpdateMode::Realtime || (bInteracting && Updates.bFullRateWhileInteracting))
		{
			Screen->SetDrawScheduled(true);
			++NumDrawn;
			continue;
		}
		if (bFrozen)
		{
			continue;
		}
		if (Updates.MaxUpdateDistance > 0.f && ViewDistance > Updates.MaxUpdateDistance && !bInteracting)
		{
			continue;
		}
		if (Updates.bSkipWhenFacingAway && !bAnyInFront)
		{
			continue;
		}

		bool bHeartbeat = false;
		if (Updates.UpdateMode == EDiegeticUpdateMode::OnDemand)
		{
			const bool bChanging = Screen->ConsumeChange(Now);
			bHeartbeat = Updates.HeartbeatRate > 0.f && Now - Screen->GetLastDrawTime() >= 1.0 / Updates.HeartbeatRate - HalfFrame;
			if (!bChanging && !bHeartbeat)
			{
				continue;
			}
		}

		const float Rate = ViewDistance <= Updates.NearDistance ? Updates.NearRate : ViewDistance <= Updates.MidDistance ? Updates.MidRate : Updates.FarRate;
		const double Interval = Rate > 0.f ? 1.0 / Rate : 0.0;
		const double Since = Now - Screen->GetLastDrawTime();
		if (!bHeartbeat && Interval > 0.0 && Since < Interval - HalfFrame)
		{
			continue;
		}

		// A screen the renderer hasn't drawn lately - out of view, culled - wouldn't redraw anyway (the Widget Component's own
		// check): it takes no place in the budget. One never drawn is let through, so its first picture is never held back.
		if (Screen->GetLastDrawTime() > 0.0 && (Display != Screen || !Screen->GetTickWhenOffscreen()) && !Display->WasRecentlyRendered(0.5f))
		{
			continue;
		}

		FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Screen = Screen;
		Candidate.Priority = Updates.BudgetPriority;
		Candidate.Overdue = Since / FMath::Max(Interval, 1.0 / 120.0);
	}

	// The budget: by priority, then the longest overdue - so every screen gets its turn, however many there are.
	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		return A.Priority != B.Priority ? A.Priority > B.Priority : A.Overdue > B.Overdue;
	});
	const int32 BudgetOverride = CVarBudget.GetValueOnGameThread();
	const int32 Budget = BudgetOverride >= 0 ? BudgetOverride : UDiegeticUISettings::Get().MaxRedrawsPerFrame;
	int32 NumWaiting = 0;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Budget > 0 && Index >= Budget)
		{
			++NumWaiting;
			continue;
		}
		Candidates[Index].Screen->SetDrawScheduled(true);
		++NumDrawn;
	}

	SET_DWORD_STAT(STAT_DiegeticRedraws, NumDrawn);
	SET_DWORD_STAT(STAT_DiegeticWaiting, NumWaiting);
	SET_DWORD_STAT(STAT_DiegeticAsleep, NumAsleep);
}

void UDiegeticUISubsystem::DrawDebug() const
{
#if ENABLE_DRAW_DEBUG
	using namespace DiegeticUISubsystemPrivate;
	if (CVarDebug.GetValueOnGameThread() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	for (const TWeakObjectPtr<UDiegeticScreenComponent>& Weak : Screens)
	{
		const UDiegeticScreenComponent* Screen = Weak.Get();
		if (!Screen)
		{
			continue;
		}
		const bool bDrawnLately = Now - Screen->GetLastDrawTime() < 0.1;
		const FColor Color = !Screen->IsAwake() ? AsleepColor : bDrawnLately ? DrawnColor : WaitingColor;
		const FVector2D Extent = Screen->GetDisplayWorldSize();
		const FVector Above = Screen->GetDisplayCenter() + Screen->GetUpVector() * (Extent.Y * 0.5 + 10.0);
		DrawDebugString(World, Above, Screen->GetDebugText(), nullptr, Color, 0.f, /*bDrawShadow=*/true, 1.f);
	}
#endif
}
