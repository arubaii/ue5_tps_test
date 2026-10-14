// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "Core/NameTBDTypes.h"
#include "Widgets/SNameTBDBlurWidget.h"
#include "NameTBDBlurButton.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNameTBDBlurButtonEvent);

/**
 * Glassmorphic Blur Button widget.
 * Combines modern frosted glass backdrop blur with interactive hover/press states,
 * EnterDuration and ExitDuration immediately following its Motion field, and runtime Design Targets.
 */
UCLASS(meta = (DisplayName = "NameTBD Glass Blur Button"))
class NAMETBD_API UNameTBDBlurButton : public UContentWidget, public INameTBDDesignTargetInterface
{
	GENERATED_BODY()

public:
	UNameTBDBlurButton();

	/** Base glassmorphism style for the button. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glassmorphism")
	FNameTBDGlassBlurStyle GlassStyle;

	/** Motion preset for hover/press transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	ENameTBDMotionType Motion = ENameTBDMotionType::ElevationLift;

	/** Duration (in seconds) when entering the hovered/active motion state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	float EnterDuration = 0.18f;

	/** Duration (in seconds) when exiting the hovered/active motion state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	float ExitDuration = 0.14f;

	/** Easing curve for the button motion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	ENameTBDEasingType MotionEasing = ENameTBDEasingType::EaseOutCubic;

	/** Blur multiplier when hovered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float HoverBlurMultiplier = 1.25f;

	/** Surface brightness boost when hovered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float HoverTintBrightnessOffset = 0.06f;

	/** Rim highlight boost when hovered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float HoverRimBoost = 0.25f;

	/** Scale applied when hovered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float HoverScale = 1.03f;

	/** Scale applied when pressed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
	float PressedScale = 0.97f;

	/** Inner padding for button content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FMargin ContentPadding = FMargin(18.0f, 10.0f);

	/** Runtime shape/design targets (e.g., volume icon states or circle-to-square recording toggle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Targets")
	TArray<FNameTBDDesignTarget> DesignTargets;

	/** Currently active design target ID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Design Targets")
	FName ActiveDesignTargetID = NAME_None;

	/** Called when the button is clicked. */
	UPROPERTY(BlueprintAssignable, Category = "Button|Event")
	FOnNameTBDBlurButtonEvent OnClicked;

	/** Called when the button is pressed. */
	UPROPERTY(BlueprintAssignable, Category = "Button|Event")
	FOnNameTBDBlurButtonEvent OnPressed;

	/** Called when the button is released. */
	UPROPERTY(BlueprintAssignable, Category = "Button|Event")
	FOnNameTBDBlurButtonEvent OnReleased;

	/** Called when the button is hovered. */
	UPROPERTY(BlueprintAssignable, Category = "Button|Event")
	FOnNameTBDBlurButtonEvent OnHovered;

	/** Called when the button is unhovered. */
	UPROPERTY(BlueprintAssignable, Category = "Button|Event")
	FOnNameTBDBlurButtonEvent OnUnhovered;

public:
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
	virtual void OnSlotAdded(UPanelSlot* InSlot) override;
	virtual void OnSlotRemoved(UPanelSlot* InSlot) override;

	FReply HandleSlateClicked();
	void HandleSlatePressed();
	void HandleSlateReleased();
	void HandleSlateHovered();
	void HandleSlateUnhovered();
	void RefreshInteractiveState();

protected:
	TSharedPtr<SNameTBDBlurWidget> MyBlurSurface;
	TSharedPtr<class SButton> MyInputButton;
	bool bIsHoveredState = false;
	bool bIsPressedState = false;
};
