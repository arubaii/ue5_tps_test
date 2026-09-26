#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Widgets/SLeafWidget.h"
#include "Styling/SlateBrush.h"
#include "Ellipsoid.generated.h"

// -------------------------------------------------------------------------
// SLATE WIDGET (SEllipsoid)
// -------------------------------------------------------------------------
class NAMETBD_API SEllipsoid : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SEllipsoid)
		: _OuterRadiusBrush(FCoreStyle::Get().GetDefaultBrush())
		, _InnerRadiusBrush(FCoreStyle::Get().GetDefaultBrush())
		, _bInnerRadiusHasBrush(false)
		, _OuterRadius(FVector2D(1.f, 1.f))
		, _InnerRadius(FVector2D::ZeroVector)
	{}
		SLATE_ATTRIBUTE(const FSlateBrush*, OuterRadiusBrush)
		SLATE_ATTRIBUTE(const FSlateBrush*, InnerRadiusBrush)
		SLATE_ATTRIBUTE(bool, bInnerRadiusHasBrush)
		SLATE_ATTRIBUTE(FVector2D, OuterRadius)
		SLATE_ATTRIBUTE(FVector2D, InnerRadius)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, 
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, 
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float) const override;

	void SetOuterRadiusBrush(const TAttribute<const FSlateBrush*>& InBrush);
	void SetInnerRadiusBrush(const TAttribute<const FSlateBrush*>& InBrush);
	void SetInnerRadiusHasBrush(const TAttribute<bool>& InHasBrush);
	void SetOuterRadius(const TAttribute<FVector2D>& InOuterRadius);
	void SetInnerRadius(const TAttribute<FVector2D>& InInnerRadius);

private:
	TAttribute<const FSlateBrush*> OuterRadiusBrush;
	TAttribute<const FSlateBrush*> InnerRadiusBrush;
	TAttribute<bool> bInnerRadiusHasBrush;
	TAttribute<FVector2D> OuterRadius;
	TAttribute<FVector2D> InnerRadius;
};

// -------------------------------------------------------------------------
// UMG WIDGET (UEllipsoid)
// -------------------------------------------------------------------------
UCLASS(meta=(DisplayName="Ellipsoid", Category="AR GameSystems UI"))
class NAMETBD_API UEllipsoid : public UWidget
{
	GENERATED_BODY()

public:
	UEllipsoid();

	// The primary brush used for the ellipsoid (or the outer ring if an inner radius is set).
	// Note: To properly size the image, the X/Y value should be 10x the size of the X/Y radii.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FSlateBrush OuterRadiusBrush;

	// If true, the hollow inner area will be filled with a secondary brush.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	bool bInnerRadiusHasBrush;

	// The secondary brush used to fill the center hole.
	// Note: To properly size the image, the X/Y value should be 10x the size of the X/Y radii.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (EditCondition = "bInnerRadiusHasBrush"))
	FSlateBrush InnerRadiusBrush;

	// Scale of the widget's layout size. 10.0 means it touches the bounding box edges.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FVector2D OuterRadius;

	// Scale for the inner hole. 0.0 is a solid shape, 5.0 creates a hole half the size of the layout.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FVector2D InnerRadius;

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SEllipsoid> MyEllipsoid;
};