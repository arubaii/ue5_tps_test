// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Components/NamedSlotInterface.h"
#include "Core/NameTBDTypes.h"
#include "Widgets/SNameTBDFlippableCard.h"
#include "NameTBDFlippableCard.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNameTBDFlippableCardFlipped, bool, bIsShowingBack);

/**
 * Two-Sided Flippable Card UMG Widget with 3D perspective flip animation.
 * Exposes FrontContent and BackContent named slots, EnterDuration and ExitDuration right after Motion,
 * configurable flip axis, and runtime Design Target support.
 */
UCLASS(meta = (DisplayName = "NameTBD Flippable Card"))
class NAMETBD_API UNameTBDFlippableCard : public UWidget, public INamedSlotInterface, public INameTBDDesignTargetInterface
{
	GENERATED_BODY()

public:
	UNameTBDFlippableCard();

	/** Whether the card starts flipped to the back face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flippable Card")
	bool bShowBackFace = false;

	/** Axis of rotation for the 3D card flip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flippable Card")
	ENameTBDCardFlipAxis FlipAxis = ENameTBDCardFlipAxis::Horizontal;

	/** If true, clicking anywhere on the card automatically toggles between Front and Back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flippable Card")
	bool bFlipOnClick = false;

	/** Motion preset for the card flip transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	ENameTBDMotionType Motion = ENameTBDMotionType::Flip3D;

	/** Duration (in seconds) when flipping from Front to Back (entering flipped state). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.5", EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	float EnterDuration = 0.42f;

	/** Duration (in seconds) when flipping from Back to Front (exiting flipped state). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.5", EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	float ExitDuration = 0.38f;

	/** Easing curve applied to the flip rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	ENameTBDEasingType Easing = ENameTBDEasingType::EaseInOutCubic;

	/** Scale boost at the 90-degree midpoint of the flip to simulate 3D camera proximity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "1.0", ClampMax = "1.5"))
	float MidFlipElevationScale = 1.08f;

	/** Perspective skew intensity during the flip rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float PerspectiveSkewStrength = 0.14f;

	/** Optional runtime shape/design targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Targets")
	TArray<FNameTBDDesignTarget> DesignTargets;

	/** Currently active design target ID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Design Targets")
	FName ActiveDesignTargetID = NAME_None;

	/** Broadcast whenever the card is flipped to Front or Back. */
	UPROPERTY(BlueprintAssignable, Category = "Flippable Card|Events")
	FOnNameTBDFlippableCardFlipped OnCardFlipped;

protected:
	UPROPERTY()
	TObjectPtr<UWidget> FrontContent;

	UPROPERTY()
	TObjectPtr<UWidget> BackContent;

public:
	/** Flips the card to show the BackContent slot. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Flippable Card")
	void FlipToBack(bool bAnimate = true);

	/** Flips the card to show the FrontContent slot. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Flippable Card")
	void FlipToFront(bool bAnimate = true);

	/** Toggles between FrontContent and BackContent. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Flippable Card")
	void ToggleFlip(bool bAnimate = true);

	/** Returns true if the card is currently targeting or showing the back face. */
	UFUNCTION(BlueprintPure, Category = "NameTBD|Flippable Card")
	bool IsShowingBackFace() const { return bShowBackFace; }

	/** Returns the normalized flip progress [0 = Front, 0.5 = 90deg edge, 1 = Back]. */
	UFUNCTION(BlueprintPure, Category = "NameTBD|Flippable Card")
	float GetFlipProgress() const;

	// INamedSlotInterface
	virtual void GetSlotNames(TArray<FName>& SlotNames) const override;
	virtual UWidget* GetContentForSlot(FName SlotName) const override;
	virtual void SetContentForSlot(FName SlotName, UWidget* Content) override;

	// INameTBDDesignTargetInterface
	virtual bool SetActiveDesignTargetByID_Implementation(FName InTargetID, bool bAnimateTransition = true) override;
	virtual bool SetActiveDesignTargetByValue_Implementation(float NormalizedValue, bool bAnimateTransition = true) override;
	virtual FName GetActiveDesignTargetID_Implementation() const override { return ActiveDesignTargetID; }

	// UWidget interface
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	void HandleSlateFaceChanged(bool bNewShowingBack);
	FNameTBDMotionSettings BuildMotionStruct() const;

protected:
	TSharedPtr<SNameTBDFlippableCard> MyFlippableCard;
	static const FName FrontSlotName;
	static const FName BackSlotName;
};
