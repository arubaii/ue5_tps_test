// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Core/NameTBDTypes.h"
#include "Widgets/SNameTBDProgressBar.h"
#include "NameTBDProgressBar.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNameTBDProgressBarValueChanged, float, NewPercent, float, OldPercent);

/**
 * UMG Extended/Improved Progress Bar supporting Linear, Circular, and Hybrid (Both) shapes,
 * automatic damage/heal ghost bar animations, multi-stop gradients, segments, smoothness,
 * and runtime shape/design target switching.
 */
UCLASS(meta = (DisplayName = "NameTBD Progress Bar"))
class NAMETBD_API UNameTBDProgressBar : public UWidget, public INameTBDDesignTargetInterface
{
	GENERATED_BODY()

public:
	UNameTBDProgressBar();

	/** Current progress value in [0..1]. Compatible with vanilla ProgressBar SetPercent/GetPercent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progress", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Percent = 0.75f;

	/** Optional bindable delegate for Percent. */
	UPROPERTY()
	FGetFloat PercentDelegate;

	/** Complete visual and geometric style of the progress bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FNameTBDProgressBarStyle BarStyle;

	/** Motion and Enter/Exit duration settings for value changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	FNameTBDMotionSettings ValueChangeMotion;

	/** Optional runtime shape/design targets (e.g., changing bar shape or color states by ID or threshold). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Targets")
	TArray<FNameTBDDesignTarget> DesignTargets;

	/** Currently active design target ID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Design Targets")
	FName ActiveDesignTargetID = NAME_None;

	/** Fired whenever SetPercent changes the target percent. */
	UPROPERTY(BlueprintAssignable, Category = "Progress|Events")
	FOnNameTBDProgressBarValueChanged OnPercentChanged;

public:
	/** Sets the current progress bar fill percent [0..1] (vanilla UMG ProgressBar compatible). */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Progress Bar")
	void SetPercent(float InPercent);

	/** Gets the current target progress bar percent [0..1] (vanilla UMG ProgressBar compatible). */
	UFUNCTION(BlueprintPure, Category = "NameTBD|Progress Bar")
	float GetPercent() const { return Percent; }

	/** Gets the currently animated/displayed primary percent [0..1]. */
	UFUNCTION(BlueprintPure, Category = "NameTBD|Progress Bar")
	float GetDisplayedPercent() const;

	/** Sets the bar shape (Linear, Circular, or Both) at runtime. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Progress Bar")
	void SetBarShape(ENameTBDProgressBarShape InShape);

	/** Sets the number of segments and gap size at runtime. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Progress Bar")
	void SetSegments(int32 InSegmentCount, float InSegmentGap = 3.0f);

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
	void ApplyDesignTarget(const FNameTBDDesignTarget& Target);
	float HandleGetPercent() const;

protected:
	TSharedPtr<SNameTBDProgressBar> MyProgressBar;
};
