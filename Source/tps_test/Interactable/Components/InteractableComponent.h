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

	/**
	 * Hold every Screen space widget on this interactable at a constant size IN THE
	 * WORLD, so it recedes with distance the way anything else in the level does.
	 *
	 * One switch for the whole interactable rather than per widget spec, because a
	 * player looking at an object should get one consistent read on how far away it is —
	 * there's no good reason for one prompt on a crate to grow while another on the same
	 * crate holds steady. It also means a spec sitting in a shared
	 * UInteractionWidgetDataAsset behaves according to whichever interactable is showing
	 * it, not a value baked into the shared asset.
	 *
	 * Screen space only, and it exists because Screen space does not do this on its own. A
	 * screen widget is drawn by the world widget screen layer at a flat DrawSize in
	 * pixels, projected to wherever its component happens to be: it keeps its full size at
	 * fifty metres, so it swells over the object it belongs to as you back away. Turning
	 * this on gives it the 1/distance falloff the screen layer never applies, and you get
	 * an in-world-looking prompt that is still drawn on the screen layer — always on top,
	 * never clipped by geometry, never depth-sorted against the level.
	 *
	 * OFF is the engine behaviour: pixel-locked, same size at any range. That is the right
	 * choice for objective markers and offscreen indicators.
	 *
	 * A World space widget is a real quad in the level and already falls off on its own;
	 * the interactor leaves those completely alone, authored scale in, authored scale out.
	 * Setting this on an interactable whose specs are World space does nothing for them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation")
	bool bConstantWidgetSize = false;

	/**
	 * The distance, in cm from the camera, at which a constant-size widget draws at its
	 * natural size — its authored UMG render scale, multiplier of exactly 1. Closer than
	 * this it grows, further away it shrinks, both in proportion to distance.
	 *
	 * Left at 0 (the default), this is derived automatically per-object from
	 * GetResolvedMaxDistance() — the interaction range this interactable already has, or
	 * the interactor's FocusRadius if none is set. That range is "as close as being right
	 * next to it gets" for something you interact with at arm's length, so the widget
	 * reads at its natural size right up to the edge of interaction range without you
	 * authoring anything per object.
	 *
	 * Set a positive value only to override that per this interactable — e.g. a prompt you
	 * want to read as natural-sized from further away than you can actually interact with.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation",
		meta = (EditCondition = "bConstantWidgetSize", ClampMin = "0.0"))
	float ConstantSizeReferenceDistance = 0.f;

	/**
	 * Floor on the constant-size multiplier, so a prompt at the far edge of its ambient
	 * range stays legible instead of collapsing into a smear of pixels.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation",
		meta = (EditCondition = "bConstantWidgetSize", ClampMin = "0.01"))
	float ConstantSizeMinScale = 0.15f;

	/**
	 * Ceiling on the constant-size multiplier.
	 *
	 * This one earns its keep: reference distance is authored for the range you read the
	 * prompt from, and interacting means walking well inside it. At a 300cm reference,
	 * standing 40cm from a crate asks for 7.5x. The ceiling is what stops the last metre
	 * of every approach from filling the screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction|Presentation",
		meta = (EditCondition = "bConstantWidgetSize", ClampMin = "0.01"))
	float ConstantSizeMaxScale = 2.f;

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

	/** True if any widget spec on this interactable (own array or data asset) is World space. */
	UFUNCTION(BlueprintPure, Category = "Interaction")
	bool HasWorldSpaceWidgetSpec() const;

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