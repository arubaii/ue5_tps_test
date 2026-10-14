// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "Core/NameTBDTypes.h"
#include "Widgets/SNameTBDBlurWidget.h"
#include "NameTBDBlurWidget.generated.h"

/**
 * Sleek, modern Glassmorphism Blur container widget.
 * Combines hardware RHI backdrop blur (on all graphics APIs supported by native Slate blur)
 * with contrast-preserving tint overlays, frosted diffusion, and directional specular rim lighting.
 */
UCLASS(meta = (DisplayName = "NameTBD Glass Blur"))
class NAMETBD_API UNameTBDBlurWidget : public UContentWidget, public INameTBDDesignTargetInterface
{
	GENERATED_BODY()

public:
	UNameTBDBlurWidget();

	/** Complete glassmorphism and backdrop blur configuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glassmorphism")
	FNameTBDGlassBlurStyle GlassStyle;

	/** Padding between the glass border and child content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FMargin ContentPadding = FMargin(16.0f);

	/** Horizontal alignment of the child slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	TEnumAsByte<EHorizontalAlignment> HorizontalAlignment = HAlign_Fill;

	/** Vertical alignment of the child slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	TEnumAsByte<EVerticalAlignment> VerticalAlignment = VAlign_Fill;

	/** Optional runtime shape/design targets for switching glass tint, corner radii, or blur presets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Targets")
	TArray<FNameTBDDesignTarget> DesignTargets;

	/** Currently active design target ID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Design Targets")
	FName ActiveDesignTargetID = NAME_None;

public:
	/** Updates the blur strength at runtime. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Glass Blur")
	void SetBlurStrength(float InBlurStrength);

	/** Updates the glass surface tint color and opacity at runtime. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Glass Blur")
	void SetSurfaceTint(FLinearColor InSurfaceTint);

	/** Switches between Custom, LightMode, and DarkMode glass presets at runtime. */
	UFUNCTION(BlueprintCallable, Category = "NameTBD|Glass Blur")
	void SetThemePreset(ENameTBDGlassThemeMode InThemePreset);

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
	TSharedPtr<SNameTBDBlurWidget> MyBlurWidget;
};
