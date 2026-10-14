// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Core/NameTBDTypes.h"
#include "SNameTBDDropShadow.generated.h"

/**
 * Drop shadow style presets from simple classic drop shadow to sleek modern dual-layer elevation.
 */
UENUM(BlueprintType)
enum class ENameTBDShadowStylePreset : uint8
{
	SimpleDrop UMETA(DisplayName = "Simple Drop Shadow"),
	ModernElevation UMETA(DisplayName = "Sleek Modern Elevation (Ambient + Key Dual-Layer)"),
	ColoredNeonGlow UMETA(DisplayName = "Colored Ambient / Neon Glow"),
	ContactFloorShadow UMETA(DisplayName = "Contact Floor Shadow"),
	SoftFloat UMETA(DisplayName = "Soft Floating Card Shadow")
};

/**
 * Configuration struct for Drop Shadow rendering.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FNameTBDDropShadowStyle
{
	GENERATED_BODY()

	/** Preset controlling single-layer vs sleek dual-layer (ambient + key) shadow composition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow")
	ENameTBDShadowStylePreset Preset = ENameTBDShadowStylePreset::ModernElevation;

	/** Primary directional key shadow offset in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow")
	FVector2D ShadowOffset = FVector2D(0.0, 8.0);

	/** Soft blur falloff radius in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow", meta = (ClampMin = "0.0", ClampMax = "128.0"))
	float BlurRadius = 22.0f;

	/** Expansion/contraction of the shadow footprint before blur falloff. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow", meta = (ClampMin = "-32.0", ClampMax = "64.0"))
	float Spread = 0.0f;

	/** Primary shadow color and opacity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow")
	FLinearColor ShadowColor = FLinearColor(0.0f, 0.0f, 0.02f, 0.34f);

	/** Secondary tight ambient occlusion shadow color (used in ModernElevation mode). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow|Dual Layer", meta = (EditCondition = "Preset == ENameTBDShadowStylePreset::ModernElevation"))
	FLinearColor AmbientShadowColor = FLinearColor(0.0f, 0.0f, 0.02f, 0.16f);

	/** Corner radius of the shadow caster matching the target widget (TL, TR, BR, BL). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow")
	FVector4 CornerRadii = FVector4(12.0, 12.0, 12.0, 12.0);

	/** Number of smooth penumbra rings used to render the analytical soft shadow [4..24]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow|Quality", meta = (ClampMin = "4", ClampMax = "24"))
	int32 QualityRings = 12;
};

/**
 * Slate widget that renders simple or sleek modern multi-layered analytical SDF drop shadows
 * behind any child or target widget.
 */
class NAMETBD_API SNameTBDDropShadow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNameTBDDropShadow)
		: _ShadowStyle()
		, _MotionSettings()
	{}
		SLATE_ARGUMENT(FNameTBDDropShadowStyle, ShadowStyle)
		SLATE_ARGUMENT(FNameTBDMotionSettings, MotionSettings)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetShadowStyle(const FNameTBDDropShadowStyle& InStyle);
	const FNameTBDDropShadowStyle& GetShadowStyle() const { return TargetStyle; }

	void SetMotionSettings(const FNameTBDMotionSettings& InMotion);
	void SetContent(const TSharedRef<SWidget>& InContent);

	/** Smoothly transitions the elevation factor (e.g., 1.0 = normal, 1.5 = lifted on hover). */
	void SetElevationMultiplier(float InMultiplier);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void PaintSoftShadowLayer(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FVector2D& Offset, float InBlurRadius, float InSpread, const FLinearColor& Color, const FVector4& Radii, int32 Rings) const;

private:
	FNameTBDDropShadowStyle TargetStyle;
	FNameTBDMotionSettings MotionSettings;
	float CurrentElevationMultiplier = 1.0f;
	float TargetElevationMultiplier = 1.0f;
};
