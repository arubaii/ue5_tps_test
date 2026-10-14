// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Widgets/SLeafWidget.h"
#include "WidgetSVGBrush.h"

#include "WidgetSVGImage.generated.h"

class UWidgetSVG;

/** How an SVG Image lays its SVG in its box. */
UENUM(BlueprintType)
enum class EWidgetSVGFit : uint8
{
	Contain UMETA(ToolTip = "As big as fits, keeping the SVG's proportions, centred."),

	Fill UMETA(ToolTip = "Stretched to the whole box, as an Image is.")
};

/** The Slate side of an SVG Image: draws an SVG asset at the size it is shown. */
class NAMETBD_API SWidgetSVGImage : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetSVGImage) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetSVG(UWidgetSVG* InSVG);
	void SetTint(const FLinearColor& InTint);
	void SetFit(EWidgetSVGFit InFit);
	void SetDesiredSizeOverride(const TOptional<FVector2D>& InSize);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:

	TWeakObjectPtr<UWidgetSVG> SVG;
	FLinearColor Tint = FLinearColor::White;
	EWidgetSVGFit Fit = EWidgetSVGFit::Contain;
	TOptional<FVector2D> DesiredSizeOverride;

	mutable FWidgetSVGBrush SVGBrush;
};

/**
 * An image drawn from an SVG asset, at the size it is shown on screen: sharp at any size, any DPI, under any scale
 * animation. Use it where you would use an Image with an icon.
 *
 * Its size is the SVG's own (its width / height) unless Override Size is on. Color And Opacity tints it: with the SVG's
 * Draw In White on, it is the image's colour.
 */
UCLASS(meta = (DisplayName = "SVG Image"))
class NAMETBD_API UWidgetSVGImage : public UWidget
{
	GENERATED_BODY()

public:

	/** The SVG to draw. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (DisplayName = "SVG"))
	TObjectPtr<UWidgetSVG> SVG;

	/** Multiplies the SVG's colours (white leaves them as they are). With the SVG's Draw In White on, this is its colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Appearance")
	FLinearColor ColorAndOpacity = FLinearColor::White;

	/** How the SVG is laid in the widget's box. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	EWidgetSVGFit Fit = EWidgetSVGFit::Contain;

	/** Ask for Image Size instead of the SVG's own size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (InlineEditConditionToggle))
	bool bOverrideImageSize = false;

	/** The size the widget asks for, in Slate units, when Override Image Size is on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance", meta = (EditCondition = "bOverrideImageSize"))
	FVector2D ImageSize = FVector2D(32.0, 32.0);

	UFUNCTION(BlueprintCallable, Category = "SVG Image")
	void SetSVG(UWidgetSVG* InSVG);

	UFUNCTION(BlueprintCallable, Category = "SVG Image")
	void SetColorAndOpacity(FLinearColor InColorAndOpacity);

	UFUNCTION(BlueprintCallable, Category = "SVG Image")
	void SetFit(EWidgetSVGFit InFit);

	/** Asks for InImageSize (Override Image Size on). */
	UFUNCTION(BlueprintCallable, Category = "SVG Image")
	void SetImageSize(FVector2D InImageSize);

	/** Back to the SVG's own size (Override Image Size off). */
	UFUNCTION(BlueprintCallable, Category = "SVG Image")
	void ClearImageSize();

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:

	virtual TSharedRef<SWidget> RebuildWidget() override;

	TSharedPtr<SWidgetSVGImage> MySVGImage;
};
