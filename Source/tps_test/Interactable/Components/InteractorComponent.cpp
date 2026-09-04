// Copyright arubaii. All rights reserved.

#include "InteractorComponent.h"

#include "InteractableComponent.h"
#include "Interactable/Interfaces/InteractableInterface.h"
#include "Interactable/DeveloperSettings/InteractionSettings.h"
#include "Interactable/Utils/InteractionTags.h"
#include "Interactable/Types/InteractionWidgetDataAsset.h"
#include "Interactable/Interfaces/InteractionWidgetInterface.h"

#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/MeshComponent.h"
#include "Components/WidgetComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTagAssetInterface.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace InteractionPrivate
{
	template<typename T>
	static TArray<T> Difference(const TArray<T>& A, const TArray<T>& B)
	{
		TArray<T> Result;
		Result.Reserve(A.Num());
		for (const T& Element : A)
		{
			if (!B.Contains(Element))
			{
				Result.Add(Element);
			}
		}
		return Result;
	}
}

UInteractorComponent::UInteractorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
	SuppressionTags.AddTag(InteractionTags::TAG_State_UI_Menu);
}

void UInteractorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UInteractorComponent, FocusedActor, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UInteractorComponent, bIsInteracting, COND_SkipOwner);
}

void UInteractorComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalInteractor())
	{
		return;
	}

	BindInput();

	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		OwnerPawn->ReceiveRestartedDelegate.AddDynamic(this, &UInteractorComponent::HandlePawnRestarted);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PollTimerHandle, this, &UInteractorComponent::PollInteraction,
			FMath::Max(0.005f, FocusPollRate), true);
	}
}

void UInteractorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
	}

	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		OwnerPawn->ReceiveRestartedDelegate.RemoveDynamic(this, &UInteractorComponent::HandlePawnRestarted);
	}

	ClearAllPresentation();
	Super::EndPlay(EndPlayReason);
}

bool UInteractorComponent::IsLocalInteractor() const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return false;

	if (const APawn* OwnerPawn = Cast<APawn>(Owner))
	{
		return OwnerPawn->IsLocallyControlled();
	}
	if (const AController* OwnerController = Cast<AController>(Owner))
	{
		return OwnerController->IsLocalController();
	}

	return Owner->GetNetMode() != NM_DedicatedServer && Owner->GetLocalRole() == ROLE_Authority;
}

AController* UInteractorComponent::GetOwningController() const
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return nullptr;

	if (APawn* OwnerPawn = Cast<APawn>(Owner))
	{
		return OwnerPawn->GetController();
	}
	if (AController* AsController = Cast<AController>(Owner))
	{
		return AsController;
	}
	return Owner->GetInstigatorController();
}

APlayerController* UInteractorComponent::GetOwningPlayerController() const
{
	return Cast<APlayerController>(GetOwningController());
}

FVector UInteractorComponent::GetInteractorLocation() const
{
	const AActor* Owner = GetOwner();
	return IsValid(Owner) ? Owner->GetActorLocation() : FVector::ZeroVector;
}

FVector UInteractorComponent::GetViewLocation() const
{
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		return ViewLocation;
	}
	return GetInteractorLocation();
}

void UInteractorComponent::PushSuppressionTag(FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;
	int32& Count = SuppressionRefCounts.FindOrAdd(Tag);
	++Count;
	SuppressionTags.AddTag(Tag);
}

void UInteractorComponent::PopSuppressionTag(FGameplayTag Tag)
{
	if (!Tag.IsValid()) return;
	if (int32* Count = SuppressionRefCounts.Find(Tag))
	{
		*Count = FMath::Max(0, *Count - 1);
		if (*Count == 0)
		{
			SuppressionRefCounts.Remove(Tag);
			if (!GetClass()->GetDefaultObject<UInteractorComponent>()->SuppressionTags.HasTagExact(Tag))
			{
				SuppressionTags.RemoveTag(Tag);
			}
		}
	}
}

void UInteractorComponent::SetSuppressionTagActive(FGameplayTag Tag, bool bActive)
{
	bActive ? PushSuppressionTag(Tag) : PopSuppressionTag(Tag);
}

bool UInteractorComponent::IsSuppressed() const
{
	if (SuppressionRefCounts.Num() > 0) return true;
	return QueryOwnerHasAnyTag(SuppressionTags);
}

bool UInteractorComponent::QueryOwnerHasAnyTag(const FGameplayTagContainer& Tags) const
{
	if (Tags.IsEmpty()) return false;

	auto QueryObject = [&Tags](const UObject* Object) -> bool
	{
		if (const IGameplayTagAssetInterface* TagInterface = Cast<IGameplayTagAssetInterface>(Object))
		{
			FGameplayTagContainer Owned;
			TagInterface->GetOwnedGameplayTags(Owned);
			return Owned.HasAny(Tags);
		}
		return false;
	};

	const AActor* Owner = GetOwner();
	if (QueryObject(Owner)) return true;

	const AController* Controller = GetOwningController();
	if (QueryObject(Controller)) return true;

	if (Controller && QueryObject(Controller->PlayerState)) return true;

	return false;
}

void UInteractorComponent::PollInteraction()
{
	if (!IsLocalInteractor()) return;

	const bool bSuppressed = IsSuppressed();
	if (bSuppressed != bWasSuppressed)
	{
		bWasSuppressed = bSuppressed;

		if (bSuppressed)
		{
			if (bIsInteracting) CancelInteract(EInteractionAbortReason::Suppressed);
			ClearAllPresentation();
		}
	}

	if (bSuppressed) return;

	PruneStaleEntries();
	TickFocus();

	if (AmbientRadius > 0.f && (PollCounter % FMath::Max(1, AmbientPollInterval)) == 0)
	{
		TickAmbient();
	}

	++PollCounter;
}

void UInteractorComponent::TickFocus()
{
	TArray<AActor*> Candidates;
	SweepInteractables(FocusRadius, Candidates);

	AActor* NewFocus = SelectFocusTarget(Candidates);

	if (NewFocus != FocusedActor)
	{
		SetFocus(NewFocus);
	}
	else if (IsValid(FocusedActor))
	{
		if (const UInteractableComponent* Interactable = GetFocusedInteractable())
		{
			if (Interactable->bHighlightWhenFocused && HighlightedActor != FocusedActor)
			{
				ApplyHighlight(FocusedActor, *Interactable);
			}
		}
	}

	if (IsValid(FocusedActor))
	{
		if (const UInteractableComponent* Interactable = GetFocusedInteractable())
		{
			if (FInteractionWidgetSet* Set = WidgetSets.Find(FocusedActor))
			{
				UpdateAmbientOpacity(FocusedActor, *Interactable, *Set);
			}
		}
	}
}

void UInteractorComponent::TickAmbient()
{
	TArray<AActor*> Swept;
	SweepInteractables(AmbientRadius, Swept);

	TArray<AActor*> NewAmbient;
	NewAmbient.Reserve(Swept.Num());

	for (AActor* Actor : Swept)
	{
		UInteractableComponent* Interactable = IInteractableInterface::FindInteractableComponent(Actor);
		if (!Interactable || !Interactable->WantsAmbientPresentation() || Interactable->Profile.AmbientRange <= 0.f) continue;

		const float DistanceSq = FVector::DistSquared(GetInteractorLocation(), Interactable->GetFocusPoint());
		if (DistanceSq > FMath::Square(Interactable->Profile.AmbientRange)) continue;

		if (!Interactable->CanBeInteractedWith(GetOwner())) continue;

		NewAmbient.Add(Actor);
	}

	TArray<AActor*> CurrentAmbient;
	CurrentAmbient.Reserve(AmbientActors.Num());
	for (const TObjectPtr<AActor>& Actor : AmbientActors)
	{
		if (IsValid(Actor)) CurrentAmbient.Add(Actor);
	}

	const TArray<AActor*> Entered = InteractionPrivate::Difference(NewAmbient, CurrentAmbient);
	const TArray<AActor*> Exited = InteractionPrivate::Difference(CurrentAmbient, NewAmbient);

	for (AActor* Actor : Exited)
	{
		if (Actor != FocusedActor) ApplyPresentation(Actor, EInteractionPresentation::None);
	}

	for (AActor* Actor : Entered)
	{
		if (Actor != FocusedActor) ApplyPresentation(Actor, EInteractionPresentation::Ambient);
	}

	AmbientActors.Reset(NewAmbient.Num());
	for (AActor* Actor : NewAmbient)
	{
		AmbientActors.Add(Actor);
	}

	for (AActor* Actor : NewAmbient)
	{
		if (Actor == FocusedActor) continue;

		UInteractableComponent* Interactable = IInteractableInterface::FindInteractableComponent(Actor);
		FInteractionWidgetSet* Set = WidgetSets.Find(Actor);
		if (!Interactable || !Set) continue;

		if (bAmbientLineOfSightEnabled)
		{
			const bool bVisible = HasLineOfSightTo(*Interactable, NewAmbient);
			if (bVisible != Set->bInLineOfSight)
			{
				Set->bInLineOfSight = bVisible;
				UpdateWidgetVisibility(*Set, Set->Presentation, bVisible);
			}
		}

		UpdateAmbientOpacity(Actor, *Interactable, *Set);
	}
}

void UInteractorComponent::UpdateTickState()
{
	bool bNeedsTick = (bIsInteracting && HoldRequired > 0.f);

	if (!bNeedsTick)
	{
		for (auto& Pair : WidgetSets)
		{
			if (Pair.Value.Presentation != EInteractionPresentation::None)
			{
				for (const FInteractionWidgetInstance& Instance : Pair.Value.Widgets)
				{
					// bWantsScaleTick / bWantsBillboardTick are resolved once, at build, from
					// the authored spec. Asking the live UWidgetComponent for its space here
					// is what let this disagree with the pass it was supposed to be gating.
					if ((Instance.bWantsScaleTick || Instance.bWantsBillboardTick) && IsValid(Instance.Component))
					{
						bNeedsTick = true;
						break;
					}
				}
			}
			if (bNeedsTick) break;
		}
	}

	SetComponentTickEnabled(bNeedsTick);
}

void UInteractorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickHold(DeltaTime);
	TickBillboards();
	TickWidgetScale();
}

void UInteractorComponent::TickHold(float DeltaTime)
{
	if (!bIsInteracting || HoldRequired <= 0.f) return;

	if (!IsValid(FocusedActor))
	{
		CancelInteract(EInteractionAbortReason::FocusLost);
		return;
	}

	HoldElapsed = FMath::Min(HoldElapsed + DeltaTime, HoldRequired);
	const float Progress = HoldElapsed / HoldRequired;

	OnInteractionProgress.Broadcast(FocusedActor, Progress);
	PushProgressToWidgets(FocusedActor, Progress);

	if (HoldElapsed >= HoldRequired)
	{
		EndInteract();
	}
}

void UInteractorComponent::TickBillboards()
{
	FVector ViewLoc = GetViewLocation();
	
	for (auto& Pair : WidgetSets)
	{
		if (Pair.Value.Presentation == EInteractionPresentation::None) continue;

		for (FInteractionWidgetInstance& Instance : Pair.Value.Widgets)
		{
			if (!Instance.bWantsBillboardTick || !IsValid(Instance.Component)) continue;

			FVector StartLoc = Instance.Component->GetComponentLocation();
			FRotator TargetRot = (ViewLoc - StartLoc).Rotation();
			FRotator CurrentRot = Instance.Component->GetComponentRotation();

			switch (Instance.Billboard)
			{
				case EWidgetBillboard::Horizontal:
					TargetRot.Pitch = 0.f;
					TargetRot.Roll = 0.f;
					break;
				case EWidgetBillboard::Vertical:
					TargetRot.Yaw = CurrentRot.Yaw;
					TargetRot.Roll = 0.f;
					break;
				case EWidgetBillboard::Both:
					TargetRot.Roll = 0.f;
					break;
				default:
					break;
			}

			Instance.Component->SetWorldRotation(TargetRot);
		}
	}
}

float UInteractorComponent::GetViewHalfFOVTangent() const
{
	float FOV = ConstantSizeReferenceFOV;

	if (const APlayerController* PC = GetOwningPlayerController())
	{
		if (const APlayerCameraManager* CameraManager = PC->PlayerCameraManager)
		{
			FOV = CameraManager->GetFOVAngle();
		}
	}

	return FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FOV, 5.f, 170.f) * 0.5f));
}

void UInteractorComponent::ApplyConstantScale(FInteractionWidgetInstance& Instance, const FVector& ViewLocation, float HalfFOVTangent)
{
	// World space is left strictly alone. A widget sitting in the level is meant to fall
	// off with distance, and its scale stays exactly as authored on the spec's
	// RelativeTransform. bWantsScaleTick is only ever set for Screen space specs.
	if (!Instance.bWantsScaleTick || !IsValid(Instance.Component) || !IsValid(Instance.Widget))
	{
		return;
	}

	const float ReferenceTangent = FMath::Tan(
		FMath::DegreesToRadians(FMath::Clamp(ConstantSizeReferenceFOV, 5.f, 170.f) * 0.5f));

	const float FOVTerm = ReferenceTangent > KINDA_SMALL_NUMBER
		? (HalfFOVTangent / ReferenceTangent)
		: 1.f;

	const float Distance = FVector::Dist(ViewLocation, Instance.Component->GetComponentLocation());
	const float ReferenceDistance = FMath::Max(1.f, Instance.ConstantSizeReferenceDistance);

	const float Multiplier = FMath::Max((Distance / ReferenceDistance) * FOVTerm, KINDA_SMALL_NUMBER);

	// A Screen space widget component is placed by the world widget screen layer at a flat
	// DrawSize in pixels, so component scale never reaches it — the UUserWidget's render
	// transform is the only lever that changes its size. Multiply the scale the designer
	// set in UMG rather than stamping over it.
	const FVector2D Desired = Instance.BaseRenderScale * Multiplier;

	if (!Instance.Widget->GetRenderTransform().Scale.Equals(Desired, SMALL_NUMBER))
	{
		Instance.Widget->SetRenderScale(Desired);
	}

	#if !UE_BUILD_SHIPPING
	if (bDebugWidgetScaling && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			/*Key=*/ (uint64)Instance.Component->GetUniqueID(), 0.f, FColor::Yellow,
			FString::Printf(TEXT("[Interactor] %s constant-size: dist=%.0f ref=%.0f base=%s x%.3f -> %s"),
				*GetNameSafe(Instance.Component->GetOwner()),
				Distance, ReferenceDistance, *Instance.BaseRenderScale.ToString(),
				Multiplier, *Desired.ToString()));
	}
	#endif
}

void UInteractorComponent::TickWidgetScale()
{
	const FVector ViewLoc = GetViewLocation();
	const float HalfFOVTangent = GetViewHalfFOVTangent();

	for (auto& Pair : WidgetSets)
	{
		if (Pair.Value.Presentation == EInteractionPresentation::None) continue;

		for (FInteractionWidgetInstance& Instance : Pair.Value.Widgets)
		{
			ApplyConstantScale(Instance, ViewLoc, HalfFOVTangent);
		}
	}
}

void UInteractorComponent::GatherObjectTypes(TArray<TEnumAsByte<EObjectTypeQuery>>& OutTypes) const
{
	OutTypes = InteractableObjectTypesOverride.Num() > 0
		? InteractableObjectTypesOverride
		: UInteractionSettings::Get().InteractableObjectTypes;
}

void UInteractorComponent::SweepInteractables(float Radius, TArray<AActor*>& OutActors) const
{
	OutActors.Reset();

	const UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !IsValid(Owner) || Radius <= 0.f) return;

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	GatherObjectTypes(ObjectTypes);
	if (ObjectTypes.Num() == 0) return;

	FCollisionObjectQueryParams ObjectParams;
	for (const TEnumAsByte<EObjectTypeQuery>& Type : ObjectTypes)
	{
		ObjectParams.AddObjectTypesToQuery(UEngineTypes::ConvertToCollisionChannel(Type));
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(InteractionSweep), false, Owner);
	QueryParams.bReturnPhysicalMaterial = false;

	const FVector Origin = GetInteractorLocation();

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, ObjectParams,
		FCollisionShape::MakeSphere(Radius), QueryParams);

	TSet<AActor*> Unique;
	Unique.Reserve(Overlaps.Num());

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (!IsValid(Actor) || Actor == Owner || Unique.Contains(Actor)) continue;
		if (!IInteractableInterface::FindInteractableComponent(Actor)) continue;

		Unique.Add(Actor);
		OutActors.Add(Actor);
	}

	OutActors.Sort([Origin](const AActor& A, const AActor& B)
	{
		const UInteractableComponent* CompA = A.FindComponentByClass<UInteractableComponent>();
		const UInteractableComponent* CompB = B.FindComponentByClass<UInteractableComponent>();
		const FVector PointA = CompA ? CompA->GetFocusPoint() : A.GetActorLocation();
		const FVector PointB = CompB ? CompB->GetFocusPoint() : B.GetActorLocation();
		return FVector::DistSquared(Origin, PointA) < FVector::DistSquared(Origin, PointB);
	});
}

AActor* UInteractorComponent::SelectFocusTarget(const TArray<AActor*>& Candidates) const
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return nullptr;

	if (Candidates.Num() == 0) return nullptr;

	TArray<AActor*> ValidCandidates;
	ValidCandidates.Reserve(Candidates.Num());

	for (AActor* Candidate : Candidates)
	{
		UInteractableComponent* Interactable = IInteractableInterface::FindInteractableComponent(Candidate);
		if (Interactable && Interactable->CanBeInteractedWith(Owner))
		{
			ValidCandidates.Add(Candidate);
		}
	}

	if (ValidCandidates.Num() == 0) return nullptr;

	AActor* TraceResult = nullptr;
	bool bTraceRan = false;

	if (bLineTraceWhenMultipleInteractablesInFocusRange && ValidCandidates.Num() > 1)
	{
		if (const UWorld* World = GetWorld())
		{
			FVector ViewLocation;
			FRotator ViewRotation;

			if (APlayerController* PC = GetOwningPlayerController())
			{
				PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			}
			else
			{
				ViewLocation = GetInteractorLocation();
				ViewRotation = Owner->GetActorRotation();
			}

			// Trace entirely through the interaction bubble from the view camera
			const float TraceDistance = FocusRadius + FVector::Distance(ViewLocation, GetInteractorLocation()) + 200.f;
			const FVector TraceEnd = ViewLocation + (ViewRotation.Vector() * TraceDistance);

			TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
			GatherObjectTypes(ObjectTypes);

			FCollisionObjectQueryParams ObjectParams;
			for (const TEnumAsByte<EObjectTypeQuery>& Type : ObjectTypes)
			{
				ObjectParams.AddObjectTypesToQuery(UEngineTypes::ConvertToCollisionChannel(Type));
			}

			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FocusLineTrace), false, Owner);
			TArray<FHitResult> HitResults;

			bTraceRan = true;

			if (World->LineTraceMultiByObjectType(HitResults, ViewLocation, TraceEnd, ObjectParams, QueryParams))
			{
				for (const FHitResult& Hit : HitResults)
				{
					if (AActor* HitActor = Hit.GetActor())
					{
						if (ValidCandidates.Contains(HitActor))
						{
							TraceResult = HitActor;
							break;
						}
					}
				}
			}

#if !UE_BUILD_SHIPPING
			if (bDebugFocusSelection)
			{
				// Green only when the trace resolved to an actual candidate.
				DrawDebugLine(World, ViewLocation, TraceEnd,
					TraceResult ? FColor::Green : FColor::Red,
					false, FMath::Max(0.05f, FocusPollRate * 2.f), 0, 1.f);

				FString HitNames;
				for (const FHitResult& Hit : HitResults)
				{
					// Component name matters here: a hit on an oversized trigger/interaction
					// volume resolves to the same actor as a hit on the mesh, but means
					// something completely different for "what am I looking at".
					HitNames += FString::Printf(TEXT("%s.%s(%.0fcm) "),
						*GetNameSafe(Hit.GetActor()),
						*GetNameSafe(Hit.GetComponent()),
						Hit.Distance);
				}

				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(
						/*Key=*/ 8101, FMath::Max(0.05f, FocusPollRate * 2.f),
						TraceResult ? FColor::Green : FColor::Red,
						FString::Printf(TEXT("[Interactor] Trace hits: [%s] -> %s"),
							HitNames.IsEmpty() ? TEXT("none") : *HitNames,
							TraceResult ? *GetNameSafe(TraceResult) : TEXT("no candidate matched")));
				}
			}
#endif
		}
	}

	AActor* Result = TraceResult ? TraceResult : ValidCandidates[0];

#if !UE_BUILD_SHIPPING
	if (bDebugFocusSelection)
	{
		const FVector Origin = GetInteractorLocation();

		FString Order;
		for (AActor* Candidate : ValidCandidates)
		{
			const UInteractableComponent* Interactable = IInteractableInterface::FindInteractableComponent(Candidate);
			const FVector Point = Interactable ? Interactable->GetFocusPoint() : Candidate->GetActorLocation();

			Order += FString::Printf(TEXT("%s(%.0f) "), *GetNameSafe(Candidate), FVector::Dist(Origin, Point));

			DrawDebugPoint(GetWorld(), Point, 12.f,
				Candidate == Result ? FColor::Green : FColor::Silver,
				false, FMath::Max(0.05f, FocusPollRate * 2.f));
		}

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				/*Key=*/ 8102, FMath::Max(0.05f, FocusPollRate * 2.f), FColor::White,
				FString::Printf(TEXT("[Interactor] Swept=%d Valid=%d TraceRan=%d | Focus=%s (%s)"),
					Candidates.Num(), ValidCandidates.Num(), bTraceRan ? 1 : 0,
					*GetNameSafe(Result), TraceResult ? TEXT("trace") : TEXT("nearest fallback")));

			GEngine->AddOnScreenDebugMessage(
				/*Key=*/ 8103, FMath::Max(0.05f, FocusPollRate * 2.f), FColor::Cyan,
				FString::Printf(TEXT("[Interactor] Nearest first: %s"), *Order));
		}
	}
#endif

	return Result;
}

bool UInteractorComponent::HasLineOfSightTo(const UInteractableComponent& Interactable, const TArray<AActor*>& IgnoredActors) const
{
	const UWorld* World = GetWorld();
	if (!World) return true;

	const AActor* Target = Interactable.GetOwner();
	if (!IsValid(Target)) return false;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(InteractionLOS), false, GetOwner());
	Params.AddIgnoredActor(Target);
	for (AActor* Ignored : IgnoredActors) Params.AddIgnoredActor(Ignored);

	const ECollisionChannel Channel = UEngineTypes::ConvertToCollisionChannel(UInteractionSettings::Get().LineOfSightChannel);
	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(
		Hit, GetViewLocation(), Interactable.GetFocusPoint(), Channel, Params);

	return !bBlocked;
}

UInteractableComponent* UInteractorComponent::GetFocusedInteractable() const
{
	return IInteractableInterface::FindInteractableComponent(FocusedActor);
}

void UInteractorComponent::SetFocus(AActor* NewFocus)
{
	AActor* PreviousFocus = FocusedActor;
	if (PreviousFocus == NewFocus) return;

	if (bIsInteracting && IsValid(PreviousFocus))
	{
		CancelInteract(EInteractionAbortReason::FocusLost);
	}

	if (IsValid(PreviousFocus))
	{
		ClearHighlight(PreviousFocus);
		const bool bStillAmbient = AmbientActors.Contains(PreviousFocus);
		ApplyPresentation(PreviousFocus,
			bStillAmbient ? EInteractionPresentation::Ambient : EInteractionPresentation::None);
	}

	FocusedActor = NewFocus;

	if (IsValid(NewFocus))
	{
		if (UInteractableComponent* Interactable = IInteractableInterface::FindInteractableComponent(NewFocus))
		{
			ApplyPresentation(NewFocus, EInteractionPresentation::Focused);
			if (Interactable->bHighlightWhenFocused) ApplyHighlight(NewFocus, *Interactable);

			if (Interactable->GetResolvedMethod() == EInteractionMethod::Automatic)
			{
				BeginInteract();
				EndInteract();
			}
		}
	}

	if (const AActor* Owner = GetOwner(); IsValid(Owner) && !Owner->HasAuthority())
	{
		Server_ReportFocus(FocusedActor);
	}

	OnFocusChanged.Broadcast(FocusedActor, PreviousFocus);
}

void UInteractorComponent::OnRep_FocusedActor(AActor* PreviousFocus)
{
	OnFocusChanged.Broadcast(FocusedActor, PreviousFocus);
}

bool UInteractorComponent::BuildWidgetsFor(AActor* Actor, UInteractableComponent& Interactable)
{
	FInteractionWidgetSet& Set = WidgetSets.FindOrAdd(Actor);
	if (Set.bBuilt) return true;

	APlayerController* OwningPC = GetOwningPlayerController();
	if (!OwningPC) return false;

	USceneComponent* AttachTo = Interactable.ResolveAttachComponent();
	if (!AttachTo) return false;

	UObject* Payload = Interactable.GetWidgetPayload();
	const EInteractionMethod Method = Interactable.GetResolvedMethod();

	TArray<FInteractionWidgetSpec> AllSpecs;
	
	if (Interactable.WidgetSpecDataAsset)
	{
		AllSpecs = Interactable.WidgetSpecDataAsset->WidgetSpecs;
	}

	for (const FInteractionWidgetSpec& Spec : Interactable.WidgetSpecs)
	{
		bool bOverridden = false;
		for (FInteractionWidgetSpec& Existing : AllSpecs)
		{
			if (Existing.WidgetClass == Spec.WidgetClass)
			{
				Existing = Spec;
				bOverridden = true;
				break;
			}
		}
		if (!bOverridden)
		{
			AllSpecs.Add(Spec);
		}
	}

	for (const FInteractionWidgetSpec& Spec : AllSpecs)
	{
		if (!Spec.IsValidSpec()) continue;

		UClass* WidgetClass = Spec.WidgetClass.LoadSynchronous();
		if (!WidgetClass) continue;

		UWidgetComponent* WidgetComponent = NewObject<UWidgetComponent>(Actor, UWidgetComponent::StaticClass(), NAME_None, RF_Transient);
		if (!WidgetComponent) continue;

		WidgetComponent->SetWidgetSpace(Spec.WidgetSpace);
		WidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WidgetComponent->SetGenerateOverlapEvents(false);
		WidgetComponent->SetIsReplicated(false);
		WidgetComponent->RegisterComponent();
		WidgetComponent->AttachToComponent(AttachTo, FAttachmentTransformRules::KeepRelativeTransform);

		// A spec pasted from a viewport widget component occasionally arrives with a zero
		// scale on one axis. Left alone that produces an invisible widget and a very
		// confusing bug report, and it would poison the constant-size multiplier below.
		FTransform SpecTransform = Spec.RelativeTransform;
		FVector AuthoredScale = SpecTransform.GetScale3D();
		if (AuthoredScale.IsNearlyZero())
		{
			AuthoredScale = FVector::OneVector;
			SpecTransform.SetScale3D(AuthoredScale);
		}

		WidgetComponent->SetRelativeTransform(SpecTransform);

		UUserWidget* Widget = CreateWidget<UUserWidget>(OwningPC, WidgetClass);
		if (!Widget)
		{
			WidgetComponent->DestroyComponent();
			continue;
		}

		WidgetComponent->SetWidget(Widget);

		FVector2D DrawSize = FVector2D(64.f, 64.f);
		if (Widget->Implements<UInteractionWidgetInterface>())
		{
			IInteractionWidgetInterface::Execute_SetInteractionMethod(Widget, Method);
			IInteractionWidgetInterface::Execute_SetInteractionPayload(Widget, Payload);
			
			FVector2D InterfaceSize = IInteractionWidgetInterface::Execute_GetInteractionWidgetSize(Widget);
			if (!InterfaceSize.IsZero())
			{
				DrawSize = InterfaceSize;
			}
		}
		
		WidgetComponent->SetDrawSize(DrawSize * Spec.DrawSizeScale);

		FInteractionWidgetInstance Instance;
		Instance.Widget = Widget;
		Instance.Component = WidgetComponent;
		Instance.Billboard = Spec.Billboard;
		Instance.bIgnoresLineOfSight = Spec.bIgnoresLineOfSight;

		// ------------------------------------------------------------ constant size
		// Everything the per-frame pass needs is resolved here, once, from the authored
		// spec. Nothing downstream re-reads the spec or interrogates the live component.
		//
		// Constant size is Screen space only. World space widgets are never touched by
		// the scale pass — they keep the scale authored on RelativeTransform, and the
		// setting is inert on them by design.
		Instance.bConstantWidgetSize = Spec.bConstantWidgetSize;
		Instance.WidgetSpace = Spec.WidgetSpace;
		Instance.ConstantSizeReferenceDistance = FMath::Max(1.f, Spec.ConstantSizeReferenceDistance);

		// A widget that never had a render transform authored in UMG reports a scale of
		// (0,0) here, not (1,1) — the CDO is zero-initialised and FWidgetTransform's
		// constructor default never lands on it. Multiplying that by anything gives zero
		// and the prompt vanishes, so treat a degenerate baseline as unit scale.
		const FVector2D AuthoredRenderScale = Widget->GetRenderTransform().Scale;
		Instance.BaseRenderScale = AuthoredRenderScale.IsNearlyZero()
			? FVector2D(1.f, 1.f)
			: AuthoredRenderScale;

		Instance.bWantsScaleTick = Spec.bConstantWidgetSize && Spec.WidgetSpace == EWidgetSpace::Screen;
		Instance.bWantsBillboardTick = Spec.Billboard != EWidgetBillboard::Disabled
			&& Spec.WidgetSpace == EWidgetSpace::World;

		#if !UE_BUILD_SHIPPING
		if (Spec.bConstantWidgetSize && Spec.WidgetSpace != EWidgetSpace::Screen)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Interaction] %s has a World space widget spec marked bConstantWidgetSize; ")
				TEXT("that setting only applies to Screen space widgets and is ignored here."),
				*GetNameSafe(Actor));
		}
		#endif

		if (Spec.DisplayPreset == EWidgetDisplayPreset::Focus)
		{
			Instance.bShowWhenAmbient = false;
			Instance.bShowWhenFocused = true;
			Instance.bFadeWithDistance = false;
			Instance.FadeCurve = Spec.FadeCurve; 
		}
		else if (Spec.DisplayPreset == EWidgetDisplayPreset::Ambient)
		{
			Instance.bShowWhenAmbient = true;
			Instance.bShowWhenFocused = false;
			Instance.bFadeWithDistance = true;
			Instance.FadeCurve = EWidgetFadeCurve::Linear;
		}
		else
		{
			Instance.bShowWhenAmbient = Spec.bShowWhenAmbient;
			Instance.bShowWhenFocused = Spec.bShowWhenFocused;
			Instance.bFadeWithDistance = Spec.bFadeWithDistance;
			Instance.FadeCurve = Spec.FadeCurve;
		}

		Set.Widgets.Add(MoveTemp(Instance));
	}

	Set.bBuilt = true;
	Set.Presentation = EInteractionPresentation::None;
	UpdateWidgetVisibility(Set, EInteractionPresentation::None, Set.bInLineOfSight);
	return true;
}

void UInteractorComponent::ApplyPresentation(AActor* Actor, EInteractionPresentation Presentation)
{
	if (!IsValid(Actor)) return;

	UInteractableComponent* Interactable = IInteractableInterface::FindInteractableComponent(Actor);
	if (!Interactable) return;

	if (Presentation != EInteractionPresentation::None && !BuildWidgetsFor(Actor, *Interactable)) return;

	FInteractionWidgetSet* Set = WidgetSets.Find(Actor);
	if (!Set) return;
	if (Set->Presentation == Presentation) return;

	Set->Presentation = Presentation;
	UpdateWidgetVisibility(*Set, Presentation, Set->bInLineOfSight);

	UpdateAmbientOpacity(Actor, *Interactable, *Set);
}

void UInteractorComponent::UpdateWidgetVisibility(FInteractionWidgetSet& Set, EInteractionPresentation Presentation, bool bInLineOfSight)
{
	const FVector ViewLoc = GetViewLocation();
	const float HalfFOVTangent = GetViewHalfFOVTangent();

	for (FInteractionWidgetInstance& Instance : Set.Widgets)
	{
		if (!Instance.IsValidInstance()) continue;

		bool bShouldShow = false;
		switch (Presentation)
		{
		case EInteractionPresentation::Focused:  bShouldShow = Instance.bShowWhenFocused; break;
		case EInteractionPresentation::Ambient:  bShouldShow = Instance.bShowWhenAmbient; break;
		default:                                 bShouldShow = false;                     break;
		}

		if (bShouldShow && !bInLineOfSight && !Instance.bIgnoresLineOfSight
			&& Presentation != EInteractionPresentation::Focused)
		{
			bShouldShow = false;
		}

		const bool bIsShowing = Instance.Widget->GetVisibility() != ESlateVisibility::Collapsed;
		if (bIsShowing == bShouldShow)
		{
			// Already in the right visibility state, but a constant-size widget still wants
			// its scale correct right now rather than after the next tick.
			if (bShouldShow) ApplyConstantScale(Instance, ViewLoc, HalfFOVTangent);
			continue;
		}

		// Size it before it is shown, otherwise it pops in at its authored scale for a frame.
		if (bShouldShow) ApplyConstantScale(Instance, ViewLoc, HalfFOVTangent);

		Instance.Widget->SetVisibility(bShouldShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		Instance.Component->SetVisibility(bShouldShow);

		if (Instance.Widget->Implements<UInteractionWidgetInterface>())
		{
			bShouldShow
				? IInteractionWidgetInterface::Execute_OnInteractionWidgetShown(Instance.Widget)
				: IInteractionWidgetInterface::Execute_OnInteractionWidgetHidden(Instance.Widget);
		}
	}
	
	UpdateTickState();
}

void UInteractorComponent::UpdateAmbientOpacity(AActor* Actor, const UInteractableComponent& Interactable, FInteractionWidgetSet& Set)
{
	const float Range = Interactable.Profile.AmbientRange;
	float Normalized = 1.f;

	if (Range > 0.f)
	{
		const FVector A = GetInteractorLocation();
		const FVector B = Interactable.GetFocusPoint();
		const float Distance = FVector2D::Distance(FVector2D(A.X, A.Y), FVector2D(B.X, B.Y));
		Normalized = FMath::Clamp(1.f - (Distance / Range), 0.f, 1.f);
	}

	for (const FInteractionWidgetInstance& Instance : Set.Widgets)
	{
		if (Instance.bFadeWithDistance && Instance.IsValidInstance()
			&& Instance.Widget->Implements<UInteractionWidgetInterface>())
		{
			float Opacity = 1.f;
			switch (Instance.FadeCurve)
			{
				case EWidgetFadeCurve::Linear:
					Opacity = Normalized;
					break;
				case EWidgetFadeCurve::Quadratic:
					Opacity = 1.f - FMath::Pow(1.f - Normalized, 2.f);
					break;
				case EWidgetFadeCurve::Cubic:
					Opacity = FMath::Pow(Normalized, 3.f);
					break;
				case EWidgetFadeCurve::Exponential:
					Opacity = Normalized * Normalized * (3.f - 2.f * Normalized);
					break;
			}

			IInteractionWidgetInterface::Execute_SetInteractionOpacity(Instance.Widget, Opacity);
		}
	}
}

void UInteractorComponent::PushProgressToWidgets(AActor* Actor, float Progress)
{
	if (const FInteractionWidgetSet* Set = WidgetSets.Find(Actor))
	{
		for (const FInteractionWidgetInstance& Instance : Set->Widgets)
		{
			if (Instance.IsValidInstance() && Instance.Widget->Implements<UInteractionWidgetInterface>())
			{
				IInteractionWidgetInterface::Execute_SetInteractionProgress(Instance.Widget, Progress);
			}
		}
	}
}

void UInteractorComponent::ClearPresentationFor(AActor* Actor, bool bDestroyWidgets)
{
	FInteractionWidgetSet* Set = WidgetSets.Find(Actor);
	if (!Set) return;

	UpdateWidgetVisibility(*Set, EInteractionPresentation::None, Set->bInLineOfSight);
	Set->Presentation = EInteractionPresentation::None;

	if (!bDestroyWidgets) return;

	for (FInteractionWidgetInstance& Instance : Set->Widgets)
	{
		if (IsValid(Instance.Widget)) Instance.Widget->RemoveFromParent();
		if (IsValid(Instance.Component))
		{
			Instance.Component->SetWidget(nullptr);
			Instance.Component->DestroyComponent();
		}
	}
	
	WidgetSets.Remove(Actor);
	UpdateTickState();
}

void UInteractorComponent::ClearAllPresentation()
{
	TArray<TObjectPtr<AActor>> Actors;
	WidgetSets.GetKeys(Actors);

	for (const TObjectPtr<AActor>& Actor : Actors)
	{
		ClearPresentationFor(Actor, true);
	}

	WidgetSets.Reset();
	AmbientActors.Reset();

	if (IsValid(HighlightedActor)) ClearHighlight(HighlightedActor);

	if (IsValid(FocusedActor))
	{
		AActor* Previous = FocusedActor;
		FocusedActor = nullptr;
		OnFocusChanged.Broadcast(nullptr, Previous);
	}
	
	UpdateTickState();
}

void UInteractorComponent::ResetPresentation()
{
	ClearAllPresentation();
	PollCounter = 0;
}

void UInteractorComponent::PruneStaleEntries()
{
	for (auto It = WidgetSets.CreateIterator(); It; ++It)
	{
		if (!IsValid(It.Key()))
		{
			for (FInteractionWidgetInstance& Instance : It.Value().Widgets)
			{
				if (IsValid(Instance.Widget)) Instance.Widget->RemoveFromParent();
			}
			It.RemoveCurrent();
		}
	}

	AmbientActors.RemoveAll([](const TObjectPtr<AActor>& Actor) { return !IsValid(Actor); });

	for (auto It = OriginalOverlayMaterials.CreateIterator(); It; ++It)
	{
		if (!IsValid(It.Key())) It.RemoveCurrent();
	}
}

void UInteractorComponent::ApplyHighlight(AActor* Actor, const UInteractableComponent& Interactable)
{
	if (IsValid(HighlightedActor) && HighlightedActor != Actor) ClearHighlight(HighlightedActor);

	UMaterialInterface* Source = Interactable.HighlightMaterialOverride.IsNull()
		? UInteractionSettings::Get().HighlightMaterial.LoadSynchronous()
		: Interactable.HighlightMaterialOverride.LoadSynchronous();

	if (!Source) return;

	TInlineComponentArray<UMeshComponent*> Meshes(Actor);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh)) continue;

		if (!OriginalOverlayMaterials.Contains(Mesh))
		{
			OriginalOverlayMaterials.Add(Mesh, Mesh->GetOverlayMaterial());
		}

		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Source, this);
		if (MID)
		{
			MID->SetVectorParameterValue(UInteractionSettings::Get().HighlightColorParameter, Interactable.HighlightColor);
			Mesh->SetOverlayMaterial(MID);
		}
	}
	HighlightedActor = Actor;
}

void UInteractorComponent::ClearHighlight(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		HighlightedActor = nullptr;
		return;
	}

	TInlineComponentArray<UMeshComponent*> Meshes(Actor);
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh)) continue;

		if (const TObjectPtr<UMaterialInterface>* Original = OriginalOverlayMaterials.Find(Mesh))
		{
			Mesh->SetOverlayMaterial(*Original);
		}
		else
		{
			Mesh->SetOverlayMaterial(nullptr);
		}
		OriginalOverlayMaterials.Remove(Mesh);
	}

	if (HighlightedActor == Actor) HighlightedActor = nullptr;
}

float UInteractorComponent::GetInteractionProgress() const
{
	return HoldRequired > 0.f ? FMath::Clamp(HoldElapsed / HoldRequired, 0.f, 1.f) : 0.f;
}

void UInteractorComponent::BeginInteract()
{
	if (!IsLocalInteractor() || IsSuppressed() || bIsInteracting) return;

	UInteractableComponent* Interactable = GetFocusedInteractable();
	if (!Interactable || !Interactable->CanBeInteractedWith(GetOwner())) return;

	bIsInteracting = true;
	HoldElapsed = 0.f;
	HoldRequired = (Interactable->GetResolvedMethod() == EInteractionMethod::Hold)
		? Interactable->GetResolvedHoldDuration()
		: 0.f;

	UpdateTickState();

	const AActor* Owner = GetOwner();
	if (IsValid(Owner) && Owner->HasAuthority())
	{
		Server_BeginInteract_Implementation(FocusedActor);
	}
	else
	{
		Server_BeginInteract(FocusedActor);
	}

	if (HoldRequired > 0.f)
	{
		OnInteractionProgress.Broadcast(FocusedActor, 0.f);
		PushProgressToWidgets(FocusedActor, 0.f);
	}
}

void UInteractorComponent::EndInteract()
{
	if (!IsLocalInteractor() || !bIsInteracting) return;

	AActor* Target = FocusedActor;

	if (HoldRequired > 0.f && HoldElapsed < HoldRequired)
	{
		CancelInteract(EInteractionAbortReason::Released);
		return;
	}

	bIsInteracting = false;
	UpdateTickState();

	PushProgressToWidgets(Target, 0.f);
	OnInteractionProgress.Broadcast(Target, 0.f);

	HoldElapsed = 0.f;
	HoldRequired = 0.f;

	if (!IsValid(Target)) return;

	const AActor* Owner = GetOwner();
	if (IsValid(Owner) && Owner->HasAuthority())
	{
		Server_CompleteInteract_Implementation(Target);
	}
	else
	{
		Server_CompleteInteract(Target);
	}
}

void UInteractorComponent::CancelInteract(EInteractionAbortReason Reason)
{
	if (!bIsInteracting) return;

	AActor* Target = FocusedActor;
	bIsInteracting = false;
	HoldElapsed = 0.f;
	HoldRequired = 0.f;
	
	UpdateTickState();

	PushProgressToWidgets(Target, 0.f);
	OnInteractionProgress.Broadcast(Target, 0.f);
	OnInteractionAborted.Broadcast(Target, Reason);

	if (IsLocalInteractor())
	{
		const AActor* Owner = GetOwner();
		if (IsValid(Owner) && Owner->HasAuthority())
		{
			Server_CancelInteract_Implementation();
		}
		else
		{
			Server_CancelInteract();
		}
	}
}

void UInteractorComponent::Server_BeginInteract_Implementation(AActor* Target)
{
	if (!IsValid(Target)) return;

	if (!ValidateInteraction(Target, false))
	{
		Client_InteractionRejected(Target);
		return;
	}

	ServerHoldTarget = Target;
	ServerHoldStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	FocusedActor = Target;
	bIsInteracting = true;
}

void UInteractorComponent::Server_CompleteInteract_Implementation(AActor* Target)
{
	bIsInteracting = false;

	if (!IsValid(Target) || !ValidateInteraction(Target, true))
	{
		ServerHoldTarget = nullptr;
		ServerHoldStartTime = -1.0;
		Client_InteractionRejected(Target);
		return;
	}

	UInteractableComponent* Interactable = IInteractableInterface::FindInteractableComponent(Target);
	const bool bPerformed = Interactable && Interactable->PerformInteraction(GetOwner());

	ServerHoldTarget = nullptr;
	ServerHoldStartTime = -1.0;

	if (bPerformed)
	{
		if (!IsLocalInteractor())
		{
			OnInteractionPerformed.Broadcast(Target, GetOwner());
		}
		Client_InteractionConfirmed(Target);
	}
	else
	{
		Client_InteractionRejected(Target);
	}
}

void UInteractorComponent::Server_ReportFocus_Implementation(AActor* NewFocus)
{
	FocusedActor = NewFocus;
}

void UInteractorComponent::Server_CancelInteract_Implementation()
{
	bIsInteracting = false;
	ServerHoldTarget = nullptr;
	ServerHoldStartTime = -1.0;
}

bool UInteractorComponent::ValidateInteraction(AActor* Target, bool bRequireHoldComplete) const
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !IsValid(Target)) return false;

	UInteractableComponent* Interactable = IInteractableInterface::FindInteractableComponent(Target);
	if (!Interactable || !Interactable->CanBeInteractedWith(Owner)) return false;

	const UInteractionSettings& Settings = UInteractionSettings::Get();
	const float MaxDistance = Interactable->GetResolvedMaxDistance(FocusRadius) * Settings.ServerDistanceTolerance;
	const float DistanceSq = FVector::DistSquared(Owner->GetActorLocation(), Interactable->GetFocusPoint());
	if (DistanceSq > FMath::Square(MaxDistance)) return false;

	if (!bRequireHoldComplete) return true;

	if (Interactable->GetResolvedMethod() == EInteractionMethod::Hold)
	{
		const float Required = Interactable->GetResolvedHoldDuration();
		if (Required > 0.f)
		{
			if (ServerHoldTarget != Target || ServerHoldStartTime < 0.0) return false;

			const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			const double Held = Now - ServerHoldStartTime;
			const double Allowed = Required * (1.0 - Settings.ServerHoldTolerance);

			if (Held < Allowed) return false;
		}
	}

	return true;
}

void UInteractorComponent::Client_InteractionConfirmed_Implementation(AActor* Target)
{
	OnInteractionPerformed.Broadcast(Target, GetOwner());
}

void UInteractorComponent::Client_InteractionRejected_Implementation(AActor* Target)
{
	bIsInteracting = false;
	HoldElapsed = 0.f;
	HoldRequired = 0.f;
	
	UpdateTickState();
	
	PushProgressToWidgets(Target, 0.f);
	OnInteractionAborted.Broadcast(Target, EInteractionAbortReason::Rejected);
}

void UInteractorComponent::HandlePawnRestarted(APawn* Pawn)
{
	bInputBound = false;
	BindInput();
}

void UInteractorComponent::BindInput()
{
	if (bInputBound || !InteractAction || !IsLocalInteractor()) return;

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	UInputComponent* InputComponent = OwnerPawn ? OwnerPawn->InputComponent : nullptr;
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent);
	
	if (!EnhancedInput) return;

	EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &UInteractorComponent::HandleInteractStarted);
	EnhancedInput->BindAction(InteractAction, ETriggerEvent::Completed, this, &UInteractorComponent::HandleInteractCompleted);
	EnhancedInput->BindAction(InteractAction, ETriggerEvent::Canceled, this, &UInteractorComponent::HandleInteractCanceled);

	bInputBound = true;
}

void UInteractorComponent::HandleInteractStarted()   { BeginInteract(); }
void UInteractorComponent::HandleInteractCompleted() { EndInteract(); }
void UInteractorComponent::HandleInteractCanceled()  { CancelInteract(EInteractionAbortReason::Released); }