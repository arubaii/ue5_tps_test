// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Core/NameTBDTypes.h"
#include "SNameTBDProgressBar.generated.h"

/**
 * Bar geometry shape (Linear, Circular, or Both/Hybrid).
 */
UENUM(BlueprintType)
enum class ENameTBDProgressBarShape : uint8
{
	Linear UMETA(DisplayName = "Linear"),
	Circular UMETA(DisplayName = "Circular / Radial"),
	Both UMETA(DisplayName = "Both (Linear Track + Circular Node)")
};

/**
 * Fill direction for linear mode.
 */
UENUM(BlueprintType)
enum class ENameTBDLinearFillDirection : uint8
{
	LeftToRight UMETA(DisplayName = "Left to Right"),
	RightToLeft UMETA(DisplayName = "Right to Left"),
	BottomToTop UMETA(DisplayName = "Bottom to Top"),
	TopToBottom UMETA(DisplayName = "Top to Bottom"),
	CenterOutward UMETA(DisplayName = "Center Outward")
};

/**
 * Configuration struct passed between UNameTBDProgressBar and SNameTBDProgressBar.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FNameTBDProgressBarStyle
{
	GENERATED_BODY()

	/** Shape of the bar: Linear, Circular, or Both. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	ENameTBDProgressBarShape Shape = ENameTBDProgressBarShape::Linear;

	/** Fill direction when using Linear or Both shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	ENameTBDLinearFillDirection FillDirection = ENameTBDLinearFillDirection::LeftToRight;

	/** Explicit bar length (in Slate units) for linear mode or outer diameter for circular mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "8.0"))
	float BarLength = 260.0f;

	/** Explicit bar thickness/width (in Slate units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "2.0"))
	float BarWidth = 22.0f;

	/** Corner radius for linear bar and segments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.0"))
	float CornerRadius = 8.0f;

	/** Start angle in degrees for circular mode (0 = top / 12 o'clock). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Circular", meta = (ClampMin = "-360.0", ClampMax = "360.0"))
	float CircularStartAngle = 0.0f;

	/** Sweep arc angle in degrees for circular mode (360 = full circle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Circular", meta = (ClampMin = "10.0", ClampMax = "360.0"))
	float CircularSweepAngle = 360.0f;

	/** Whether circular fill progresses clockwise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Circular")
	bool bClockwise = true;

	/** Background track color (supports transparency). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor BackgroundColor = FLinearColor(0.06f, 0.07f, 0.09f, 0.75f);

	/** Primary fill color (used when GradientColors has fewer than 2 entries). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	FLinearColor FillColor = FLinearColor(0.12f, 0.68f, 0.98f, 1.0f);

	/** Optional multi-stop gradient colors for the fill bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	TArray<FLinearColor> GradientColors;

	/** If true, gradient maps to current Percent value (e.g., low = red, high = green) instead of spatial UV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colors")
	bool bSampleGradientByValue = false;

	/** Delayed ghost/trail bar color when taking damage (value decreases). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Ghost Bar")
	FLinearColor DamageGhostColor = FLinearColor(0.95f, 0.22f, 0.26f, 0.90f);

	/** Delayed ghost/preview bar color when healing/gaining (value increases). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Ghost Bar")
	FLinearColor HealGhostColor = FLinearColor(0.20f, 0.92f, 0.52f, 0.85f);

	/** Enable delayed ghost trail bar on value changes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Ghost Bar")
	bool bEnableGhostBar = true;

	/** Hold delay (in seconds) before the ghost bar begins catching up after damage/heal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Ghost Bar", meta = (ClampMin = "0.0", EditCondition = "bEnableGhostBar"))
	float GhostHoldDelay = 0.30f;

	/** Number of segments (1 = continuous bar, >1 divides the bar into discrete parts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segments", meta = (ClampMin = "1", ClampMax = "100"))
	int32 SegmentCount = 1;

	/** Gap thickness in Slate units (or degrees for circular) between segments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Segments", meta = (ClampMin = "0.0", EditCondition = "SegmentCount > 1"))
	float SegmentGap = 3.0f;

	/** Edge anti-aliasing / smoothness feather width in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float EdgeSmoothness = 1.25f;

	/** Optional opacity mask brush multiplied over the progress bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	FSlateBrush OpacityMaskBrush;
};

/**
 * Slate implementation of the Extended/Improved Progress Bar.
 */
class NAMETBD_API SNameTBDProgressBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SNameTBDProgressBar)
		: _Percent(0.75f)
		, _Style()
		, _MotionSettings()
	{}
		SLATE_ATTRIBUTE(float, Percent)
		SLATE_ARGUMENT(FNameTBDProgressBarStyle, Style)
		SLATE_ARGUMENT(FNameTBDMotionSettings, MotionSettings)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetPercent(TAttribute<float> InPercent);
	float GetTargetPercent() const { return TargetPercent; }
	float GetDisplayedPercent() const { return DisplayedPrimaryPercent; }
	float GetGhostPercent() const { return DisplayedGhostPercent; }

	void SetStyle(const FNameTBDProgressBarStyle& InStyle);
	void SetMotionSettings(const FNameTBDMotionSettings& InMotion);

	// SWidget interface
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void PaintLinearBar(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FLinearColor& Tint, const FVector2D& LocalOrigin, const FVector2D& LocalSize) const;
	void PaintCircularBar(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32& InOutLayerId, const FLinearColor& Tint, const FVector2D& Center, float OuterRadius, float Thickness) const;
	void AppendArcRibbon(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry, const FVector2D& Center, float InnerR, float OuterR, float StartDeg, float EndDeg, const FLinearColor& ColorStart, const FLinearColor& ColorEnd, float FeatherWidth) const;
	FLinearColor ResolveFillColor(float NormalizedU) const;

private:
	TAttribute<float> PercentAttribute;
	FNameTBDProgressBarStyle Style;
	FNameTBDMotionSettings MotionSettings;

	float TargetPercent = 0.75f;
	float DisplayedPrimaryPercent = 0.75f;
	float DisplayedGhostPercent = 0.75f;
	float GhostDelayTimer = 0.0f;
	bool bLastChangeWasDamage = true;
};
