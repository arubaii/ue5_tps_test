// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Core/NameTBDTypes.h"
#include "SNameTBDBlurWidget.generated.h"

/**
 * Modern blur and glassmorphism visual mode.
 */
UENUM(BlueprintType)
enum class ENameTBDBlurMode : uint8
{
	StandardGaussian UMETA(DisplayName = "Standard Gaussian Blur"),
	FrostedGlass UMETA(DisplayName = "Frosted Glass (Glassmorphism)"),
	AcrylicLayered UMETA(DisplayName = "Acrylic Layered (Tint + Frost + Rim)"),
	ChromaticEdgeGlass UMETA(DisplayName = "Chromatic Iridescent Glass")
};

/**
 * Light or Dark theme preset for Glassmorphism contrast & legibility.
 */
UENUM(BlueprintType)
enum class ENameTBDGlassThemeMode : uint8
{
	Custom UMETA(DisplayName = "Custom Settings"),
	LightMode UMETA(DisplayName = "Light Theme Glass (Crisp Boundary, Soft White Film)"),
	DarkMode UMETA(DisplayName = "Dark Theme Glass (Subtle Rim, Deep Charcoal Film)")
};

/**
 * Complete Glassmorphism & Better Blur style settings.
 * Works across all graphics APIs supported by Slate native blur (D3D11, D3D12, Vulkan, Metal).
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FNameTBDGlassBlurStyle
{
	GENERATED_BODY()

	/** Visual blur mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glassmorphism")
	ENameTBDBlurMode BlurMode = ENameTBDBlurMode::FrostedGlass;

	/** Optional Light/Dark theme preset for contrast and boundary tuning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glassmorphism")
	ENameTBDGlassThemeMode ThemePreset = ENameTBDGlassThemeMode::Custom;

	/** Blur radius in pixels (10-30px sweet spot for glassmorphism; 4-15px for mobile/buttons). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float BlurStrength = 16.0f;

	/** Optional explicit low-pass downsample override (0 = auto-computed from BlurStrength). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (ClampMin = "0", ClampMax = "64"))
	int32 OverrideBlurRadius = 0;

	/** Semi-transparent solid overlay tint (10-30% opacity recommended to preserve WCAG text contrast). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Surface")
	FLinearColor SurfaceTint = FLinearColor(0.92f, 0.96f, 1.0f, 0.18f);

	/** Optional gradient stops for vibrant glass surface wash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Surface")
	TArray<FLinearColor> SurfaceGradientStops;

	/** Per-corner rounding radii (TopLeft, TopRight, BottomRight, BottomLeft). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Surface")
	FVector4 CornerRadii = FVector4(14.0, 14.0, 14.0, 14.0);

	/** Subtle frost micro-diffusion intensity [0..1] for acrylic/frosted texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glass Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FrostLuminance = 0.08f;

	/** Enables directional specular rim lighting along the glass border. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rim Lighting")
	bool bEnableSpecularRim = true;

	/** Thickness of the glass rim border in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rim Lighting", meta = (ClampMin = "0.0", ClampMax = "8.0", EditCondition = "bEnableSpecularRim"))
	float RimThickness = 1.25f;

	/** Directional light angle in degrees (e.g., -45 = top-left light source). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rim Lighting", meta = (ClampMin = "-180.0", ClampMax = "180.0", EditCondition = "bEnableSpecularRim"))
	float LightDirectionDegrees = -45.0f;

	/** Highlight color on the edge facing the light source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rim Lighting", meta = (EditCondition = "bEnableSpecularRim"))
	FLinearColor RimHighlightColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.55f);

	/** Subtle shadow/ambient color on the edge opposite the light source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rim Lighting", meta = (EditCondition = "bEnableSpecularRim"))
	FLinearColor RimLowlightColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.10f);

	/** Non-blur fallback color when blur strength is 0 or disabled for low-spec hardware. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fallback")
	FLinearColor LowSpecFallbackColor = FLinearColor(0.12f, 0.14f, 0.18f, 0.85f);
};

/**
 * Slate Glassmorphic Blur Widget that works on all graphics APIs supported by native Slate blur
 * while adding frosted tint films, directional specular rim borders, and interactive elevation modulation.
 */
class NAMETBD_API SNameTBDBlurWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNameTBDBlurWidget)
		: _Style()
		, _Padding(FMargin(12.0f))
		, _HAlign(HAlign_Fill)
		, _VAlign(VAlign_Fill)
	{}
		SLATE_ARGUMENT(FNameTBDGlassBlurStyle, Style)
		SLATE_ATTRIBUTE(FMargin, Padding)
		SLATE_ARGUMENT(EHorizontalAlignment, HAlign)
		SLATE_ARGUMENT(EVerticalAlignment, VAlign)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetStyle(const FNameTBDGlassBlurStyle& InStyle);
	const FNameTBDGlassBlurStyle& GetStyle() const { return Style; }

	void SetContent(const TSharedRef<SWidget>& InContent);
	void SetContentPadding(TAttribute<FMargin> InPadding);

	/** Interactive modulation used by UNameTBDBlurButton (0 = idle, 1 = hovered, 2 = pressed). */
	void SetInteractiveModulation(float InBlurMultiplier, float InTintBrightnessOffset, float InRimBoost);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void PaintSpecularRimBorder(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FLinearColor& WidgetTint, const FNameTBDGlassBlurStyle& EffectiveStyle) const;
	FNameTBDGlassBlurStyle ResolveEffectiveStyle() const;
	void SyncInternalBlurWidget();

private:
	FNameTBDGlassBlurStyle Style;
	TSharedPtr<class SBackgroundBlur> InternalRHIBlur;
	TSharedPtr<class SBox> ContentHostBox;
	FSlateBrush FallbackRoundedBrush;

	float InteractiveBlurMultiplier = 1.0f;
	float InteractiveTintOffset = 0.0f;
	float InteractiveRimBoost = 0.0f;
};
