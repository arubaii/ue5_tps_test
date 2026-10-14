// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Components/ContentWidget.h"
#include "Core/NameTBDTypes.h"
#include "Widgets/SNameTBDDropShadow.h"
#include "NameTBDDropShadowBox.generated.h"

class UNameTBDDropShadowBox;

/**
 * Attachable Drop Shadow Widget Component that can be attached dynamically
 * at runtime or in Blueprints to ANY UWidget to provide simple or sleek modern drop shadows.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "NameTBD Drop Shadow Component"))
class NAMETBD_API UNameTBDDropShadowComponent : public UObject
{
	GENERATED_BODY()

public:
	/** Shadow configuration for the attached widget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow")
	FNameTBDDropShadowStyle ShadowStyle;

	/** Motion and Enter/Exit duration settings when animating shadow elevation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	FNameTBDMotionSettings ElevationMotion;

public:
	/** Attaches a UNameTBDDropShadowComponent to any target UWidget and returns the component instance. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Drop Shadow", meta = (DefaultToSelf = "TargetWidget"))
	static UNameTBDDropShadowComponent* AttachDropShadowToWidget(UWidget* TargetWidget, FNameTBDDropShadowStyle InStyle);

	/** Finds an existing Drop Shadow component on TargetWidget, if present. */
	UFUNCTION(BlueprintPure, Category = "NameTBD|Drop Shadow", meta = (DefaultToSelf = "TargetWidget"))
	static UNameTBDDropShadowComponent* GetDropShadowComponent(const UWidget* TargetWidget);

	/** Updates the shadow style on this component. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Drop Shadow")
	void SetShadowStyle(const FNameTBDDropShadowStyle& InStyle);

	/** Animates the shadow elevation multiplier (e.g., 1.4 when hovering or dragging a widget). */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Drop Shadow")
	void SetElevationMultiplier(float InMultiplier);

	/** Returns the UWidget this drop shadow component is attached to. */
	UFUNCTION(BlueprintPure, Category = "NameTBD|Drop Shadow")
	UWidget* GetTargetWidget() const { return AttachedWidget.Get(); }

	TSharedRef<SWidget> WrapSlateWidget(const TSharedRef<SWidget>& InChildWidget);

private:
	void BindToTargetWidget(UWidget* InTargetWidget, const FNameTBDDropShadowStyle& InStyle);

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UWidget> AttachedWidget;

	UPROPERTY(Transient)
	TObjectPtr<UNameTBDDropShadowBox> InjectedShadowBox = nullptr;

	TSharedPtr<SNameTBDDropShadow> ShadowSlateWrapper;
};

/**
 * UMG Drop Shadow wrapper widget that can wrap any widget hierarchy in the UMG Designer
 * to render simple or sleek modern dual-layer elevation shadows.
 */
UCLASS(meta = (DisplayName = "NameTBD Drop Shadow Box"))
class NAMETBD_API UNameTBDDropShadowBox : public UContentWidget, public INameTBDDesignTargetInterface
{
	GENERATED_BODY()

public:
	UNameTBDDropShadowBox();

	/** Visual style of the drop shadow (Simple, Modern Dual-Layer Elevation, Colored Glow, Contact). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop Shadow")
	FNameTBDDropShadowStyle ShadowStyle;

	/** Motion settings (with EnterDuration and ExitDuration right after Motion) for shadow elevation transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	FNameTBDMotionSettings ElevationMotion;

	/** Optional runtime design targets for switching shadow color/elevation presets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Targets")
	TArray<FNameTBDDesignTarget> DesignTargets;

	/** Currently active design target ID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Design Targets")
	FName ActiveDesignTargetID = NAME_None;

public:
	/** Updates the drop shadow configuration at runtime. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Drop Shadow")
	void SetShadowStyle(const FNameTBDDropShadowStyle& InStyle);

	/** Smoothly transitions the shadow elevation multiplier (e.g. 1.0 = normal, 1.5 = lifted). */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Drop Shadow")
	void SetElevationMultiplier(float InMultiplier);

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

protected:
	TSharedPtr<SNameTBDDropShadow> MyDropShadow;
};
