// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "Interactable/Types/InteractionTypes.h"
#include "InteractorComponent.generated.h"

class AController;
class APlayerController;
class UInputAction;
class UInteractableComponent;
class UMaterialInstanceDynamic;
class UMeshComponent;
class UUserWidget;

UCLASS(ClassGroup = (Interaction), meta = (BlueprintSpawnableComponent),
	HideCategories = (Variable, Tags, ComponentTick, ComponentReplication, Activation, Cooking, AssetUserData, Collision))
class TPS_TEST_API UInteractorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractorComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Detection", meta = (ClampMin = "1.0"))
	float FocusRadius = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Detection", meta = (ClampMin = "0.005"))
	float FocusPollRate = 0.025f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Detection", meta = (ClampMin = "0.0"))
	float AmbientRadius = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Detection", meta = (ClampMin = "1"))
	int32 AmbientPollInterval = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Detection")
	bool bAmbientLineOfSightEnabled = true;

	/** 
	 * When true and multiple available interactables are within FocusRadius, a line trace from the camera is cast
	 * to determine which one the player is actively looking at. If the trace misses, it falls back to the nearest item.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Detection")
	bool bLineTraceWhenMultipleInteractablesInFocusRange = false;

	/**
	 * Field of view, in degrees, that FInteractionWidgetSpec::ConstantSizeReferenceDistance
	 * was authored against.
	 *
	 * Constant-size widgets weigh the camera's current FOV against this so they track the
	 * scene through a zoom: narrowing the FOV magnifies everything in view, and a prompt
	 * that is pretending to sit in the world has to magnify with it. Without this term an
	 * ADS would pull the crate closer and leave its prompt behind at the hip-fire size.
	 *
	 * Leave it at your default camera FOV.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation",
		meta = (ClampMin = "5.0", ClampMax = "170.0"))
	float ConstantSizeReferenceFOV = 90.f;

	/**
	 * Logs focus selection each poll and draws the trace: green when it resolved to a candidate,
	 * red when it did not. Also draws each candidate's focus point, green for the winner.
	 * Editor and development builds only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Debug")
	bool bDebugFocusSelection = false;

	/**
	 * Reports, on screen, every widget the constant-size pass is driving and the scale it
	 * is applying. Only Screen space widgets ever appear here — if one you marked
	 * bConstantWidgetSize is missing, check that its spec is Screen space.
	 * Editor and development builds only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Debug")
	bool bDebugWidgetScaling = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Detection")
	TArray<TEnumAsByte<EObjectTypeQuery>> InteractableObjectTypesOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Suppression")
	FGameplayTagContainer SuppressionTags;

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
	FOnInteractionFocusChanged OnFocusChanged;

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
	FOnInteractionProgress OnInteractionProgress;

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
	FOnInteractionPerformed OnInteractionPerformed;

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
	FOnInteractionAborted OnInteractionAborted;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	AActor* GetFocusedActor() const { return FocusedActor; }

	UFUNCTION(BlueprintPure, Category = "Interaction")
	UInteractableComponent* GetFocusedInteractable() const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	bool IsInteracting() const { return bIsInteracting; }

	UFUNCTION(BlueprintPure, Category = "Interaction")
	float GetInteractionProgress() const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	bool IsLocalInteractor() const;

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void BeginInteract();

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void EndInteract();

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void CancelInteract(EInteractionAbortReason Reason = EInteractionAbortReason::Released);

	UFUNCTION(BlueprintCallable, Category = "Interaction|Suppression")
	void PushSuppressionTag(FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "Interaction|Suppression")
	void PopSuppressionTag(FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "Interaction|Suppression")
	void SetSuppressionTagActive(FGameplayTag Tag, bool bActive);

	UFUNCTION(BlueprintPure, Category = "Interaction|Suppression")
	bool IsSuppressed() const;

	UFUNCTION(BlueprintCallable, Category = "Interaction")
	void ResetPresentation();

protected:
	UPROPERTY(ReplicatedUsing = OnRep_FocusedActor)
	TObjectPtr<AActor> FocusedActor = nullptr;

	UPROPERTY(Replicated)
	bool bIsInteracting = false;

	UFUNCTION()
	void OnRep_FocusedActor(AActor* PreviousFocus);

	UFUNCTION(Server, Reliable)
	void Server_BeginInteract(AActor* Target);

	UFUNCTION(Server, Reliable)
	void Server_CompleteInteract(AActor* Target);

	UFUNCTION(Server, Reliable)
	void Server_CancelInteract();

	UFUNCTION(Server, Unreliable)
	void Server_ReportFocus(AActor* NewFocus);

	UFUNCTION(Client, Reliable)
	void Client_InteractionRejected(AActor* Target);

	UFUNCTION(Client, Reliable)
	void Client_InteractionConfirmed(AActor* Target);

private:
	void PollInteraction();
	void TickFocus();
	void TickAmbient();
	void TickHold(float DeltaTime);
	void TickBillboards();
	void TickWidgetScale();
	void UpdateTickState();

	void SweepInteractables(float Radius, TArray<AActor*>& OutActors) const;
	AActor* SelectFocusTarget(const TArray<AActor*>& Candidates) const;
	bool HasLineOfSightTo(const UInteractableComponent& Interactable, const TArray<AActor*>& IgnoredActors) const;

	void SetFocus(AActor* NewFocus);
	void ApplyPresentation(AActor* Actor, EInteractionPresentation Presentation);
	void ClearPresentationFor(AActor* Actor, bool bDestroyWidgets);
	void ClearAllPresentation();

	bool BuildWidgetsFor(AActor* Actor, UInteractableComponent& Interactable);

	/** Tangent of the camera's half-FOV, used to keep constant-size widgets stable under zoom. */
	float GetViewHalfFOVTangent() const;

	/**
	 * Drives one constant-size widget. Safe to call on any instance — it returns
	 * immediately for widgets that did not ask for it.
	 */
	void ApplyConstantScale(FInteractionWidgetInstance& Instance, const FVector& ViewLocation, float HalfFOVTangent);

	void UpdateWidgetVisibility(FInteractionWidgetSet& Set, EInteractionPresentation Presentation, bool bInLineOfSight);
	void UpdateAmbientOpacity(AActor* Actor, const UInteractableComponent& Interactable, FInteractionWidgetSet& Set);
	void PushProgressToWidgets(AActor* Actor, float Progress);

	void ApplyHighlight(AActor* Actor, const UInteractableComponent& Interactable);
	void ClearHighlight(AActor* Actor);

	void PruneStaleEntries();

	APlayerController* GetOwningPlayerController() const;
	AController* GetOwningController() const;

	FVector GetInteractorLocation() const;
	FVector GetViewLocation() const;

	void GatherObjectTypes(TArray<TEnumAsByte<EObjectTypeQuery>>& OutTypes) const;
	bool QueryOwnerHasAnyTag(const FGameplayTagContainer& Tags) const;

	void BindInput();

	UFUNCTION()
	void HandlePawnRestarted(APawn* Pawn);

	void HandleInteractStarted();
	void HandleInteractCompleted();
	void HandleInteractCanceled();

	bool ValidateInteraction(AActor* Target, bool bRequireHoldComplete) const;

	UPROPERTY(Transient)
	TMap<TObjectPtr<AActor>, FInteractionWidgetSet> WidgetSets;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> AmbientActors;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UMeshComponent>, TObjectPtr<UMaterialInterface>> OriginalOverlayMaterials;

	UPROPERTY(Transient)
	TObjectPtr<AActor> HighlightedActor = nullptr;

	float HoldElapsed = 0.f;
	float HoldRequired = 0.f;

	double ServerHoldStartTime = -1.0;
	UPROPERTY(Transient)
	TObjectPtr<AActor> ServerHoldTarget = nullptr;

	TMap<FGameplayTag, int32> SuppressionRefCounts;

	bool bWasSuppressed = false;
	int32 PollCounter = 0;
	bool bInputBound = false;

	FTimerHandle PollTimerHandle;
};