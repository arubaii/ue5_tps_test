// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Interactable/Types/InteractionTypes.h"
#include "InteractableComponent.generated.h"

class UMaterialInterface;
class USceneComponent;
class UInteractionWidgetDataAsset;

USTRUCT(BlueprintType)
struct TPS_TEST_API FInteractionProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	EInteractionMethod Method = EInteractionMethod::Press;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction",
		meta = (EditCondition = "Method == EInteractionMethod::Hold", ClampMin = "0.0"))
	float HoldDuration = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "0.0"))
	float MaxInteractionDistance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "0.0"))
	float AmbientRange = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	bool bSingleUse = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction", meta = (ClampMin = "0.0"))
	float Cooldown = 0.2f;
};

UCLASS(ClassGroup = (Interaction), meta = (BlueprintSpawnableComponent),
	HideCategories = (Variable, Tags, ComponentTick, ComponentReplication, Activation, Cooking, AssetUserData, Collision))
class TPS_TEST_API UInteractableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractableComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Interaction")
	FInteractionProfile Profile;

	/** Data Asset containing reusable widget specs for this interactable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Presentation")
	TObjectPtr<UInteractionWidgetDataAsset> WidgetSpecDataAsset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Presentation")
	TArray<FInteractionWidgetSpec> WidgetSpecs;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Presentation")
	FName AttachSocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Presentation")
	FVector FocusPointOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Presentation")
	bool bHighlightWhenFocused = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Presentation",
		meta = (EditCondition = "bHighlightWhenFocused"))
	FLinearColor HighlightColor = FLinearColor(0.07f, 1.f, 0.06f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Presentation",
		meta = (EditCondition = "bHighlightWhenFocused"))
	TSoftObjectPtr<UMaterialInterface> HighlightMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Interaction")
	FGameplayTagContainer InteractionTags;

	UPROPERTY(ReplicatedUsing = OnRep_Available, BlueprintReadOnly, Category = "Interaction|State")
	bool bAvailable = true;

	/**
	 * Emits setup warnings for this interactable at BeginPlay — currently the ambient
	 * widget/range mismatch. Off by default so a level full of interactables stays quiet.
	 * Editor and development builds only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Debug")
	bool bDebugInteractable = false;

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
	FOnInteractionPerformed OnInteractionPerformed;

	UPROPERTY(BlueprintAssignable, Category = "Interaction|Events")
	FOnInteractableAvailabilityChanged OnAvailabilityChanged;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Interaction")
	void SetAvailable(bool bNewAvailable);

	UFUNCTION(BlueprintPure, Category = "Interaction")
	bool CanBeInteractedWith(AActor* Interactor) const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	EInteractionMethod GetResolvedMethod() const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	float GetResolvedHoldDuration() const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	float GetResolvedMaxDistance(float InteractorFallbackRadius) const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	FVector GetFocusPoint() const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	USceneComponent* ResolveAttachComponent() const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	UObject* GetWidgetPayload() const;

	UFUNCTION(BlueprintPure, Category = "Interaction")
	bool WantsAmbientPresentation() const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Interaction")
	bool PerformInteraction(AActor* Interactor);

protected:
	UFUNCTION()
	void OnRep_Available();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_InteractionPerformed(AActor* Interactor);

private:
	double LastInteractionTime = TNumericLimits<double>::Lowest();
};