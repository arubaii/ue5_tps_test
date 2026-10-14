// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Components/NamedSlotInterface.h"
#include "Core/NameTBDTypes.h"
#include "Widgets/SNameTBDFoldableDropdown.h"
#include "NameTBDFoldableDropdown.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNameTBDFoldableExpansionChanged, bool, bIsExpanded);

/**
 * Better Dropdown (Foldable) UMG Widget with smooth collapse and open animations.
 * Provides two named slots (HeaderContent and BodyContent), Enter/Exit durations right after Motion,
 * animated chevron indicator, and optional accordion group auto-collapse.
 */
UCLASS(meta = (DisplayName = "NameTBD Foldable Dropdown"))
class NAMETBD_API UNameTBDFoldableDropdown : public UWidget, public INamedSlotInterface, public INameTBDDesignTargetInterface
{
	GENERATED_BODY()

public:
	UNameTBDFoldableDropdown();

	/** Whether the foldable dropdown starts in the expanded/open state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foldable")
	bool bIsExpanded = false;

	/** Fold/Unfold visual animation mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foldable")
	ENameTBDFoldAnimationMode AnimationMode = ENameTBDFoldAnimationMode::SlideAndFade;

	/** Motion preset for opening and collapsing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	ENameTBDMotionType Motion = ENameTBDMotionType::SmoothLerp;

	/** Duration (in seconds) for the open / expand animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	float EnterDuration = 0.26f;

	/** Duration (in seconds) for the collapse / close animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	float ExitDuration = 0.20f;

	/** Easing curve for the open and collapse animations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	ENameTBDEasingType Easing = ENameTBDEasingType::EaseOutCubic;

	/** Whether to draw the animated chevron arrow in the header. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foldable")
	bool bShowHeaderChevron = true;

	/** Optional accordion group name. Expanding one foldable in a group automatically collapses sibling foldables with the same group name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foldable")
	FName AccordionGroupName = NAME_None;

	/** Optional runtime shape/design targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Targets")
	TArray<FNameTBDDesignTarget> DesignTargets;

	/** Currently active design target ID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Design Targets")
	FName ActiveDesignTargetID = NAME_None;

	/** Fired when the dropdown is expanded or collapsed. */
	UPROPERTY(BlueprintAssignable, Category = "Foldable|Events")
	FOnNameTBDFoldableExpansionChanged OnExpansionChanged;

protected:
	UPROPERTY()
	TObjectPtr<UWidget> HeaderContent;

	UPROPERTY()
	TObjectPtr<UWidget> BodyContent;

public:
	/** Expands or collapses the dropdown with optional smooth animation. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Foldable Dropdown")
	void SetExpanded(bool bInExpanded, bool bAnimate = true);

	/** Toggles the current expanded/collapsed state. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Foldable Dropdown")
	void ToggleExpanded(bool bAnimate = true);

	/** Returns true if currently expanded (or animating toward expanded). */
	UFUNCTION(BlueprintPure, Category = "NameTBD|Foldable Dropdown")
	bool IsExpanded() const { return bIsExpanded; }

	/** Returns the current normalized expansion alpha [0..1]. */
	UFUNCTION(BlueprintPure, Category = "NameTBD|Foldable Dropdown")
	float GetExpansionAlpha() const;

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
	void HandleSlateExpansionChanged(bool bNewExpanded);
	FNameTBDMotionSettings BuildMotionStruct() const;

protected:
	TSharedPtr<SNameTBDFoldableDropdown> MyFoldable;
	static const FName HeaderSlotName;
	static const FName BodySlotName;
};
