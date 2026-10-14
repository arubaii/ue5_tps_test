// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Styling/SlateBrush.h"
#include "Curves/CurveFloat.h"
#include "NameTBDTypes.generated.h"

/**
 * Easing functions used across NameTBD animated widgets.
 */
UENUM(BlueprintType)
enum class ENameTBDEasingType : uint8
{
	Linear UMETA(DisplayName = "Linear"),
	EaseInQuad UMETA(DisplayName = "Ease In (Quad)"),
	EaseOutQuad UMETA(DisplayName = "Ease Out (Quad)"),
	EaseInOutQuad UMETA(DisplayName = "Ease In Out (Quad)"),
	EaseOutCubic UMETA(DisplayName = "Ease Out (Cubic)"),
	EaseInOutCubic UMETA(DisplayName = "Ease In Out (Cubic)"),
	EaseOutQuart UMETA(DisplayName = "Ease Out (Quart)"),
	EaseInOutQuart UMETA(DisplayName = "Ease In Out (Quart)"),
	EaseOutBack UMETA(DisplayName = "Ease Out (Overshoot / Back)"),
	Spring UMETA(DisplayName = "Spring Damper")
};

/**
 * Procedural or brush-based shape primitives supported by runtime design targets.
 */
UENUM(BlueprintType)
enum class ENameTBDShapePrimitive : uint8
{
	RoundedRect UMETA(DisplayName = "Rounded Rectangle"),
	Circle UMETA(DisplayName = "Circle / Pill"),
	Square UMETA(DisplayName = "Sharp Square / Box"),
	LinearBar UMETA(DisplayName = "Linear Bar"),
	CircularRing UMETA(DisplayName = "Circular Ring / Arc"),
	HybridBar UMETA(DisplayName = "Hybrid Linear + Circular"),
	CustomBrush UMETA(DisplayName = "Custom Brush / SVG Icon")
};

/**
 * Motion preset for interactive widgets and buttons.
 * Every motion field is immediately followed by EnterDuration and ExitDuration.
 */
UENUM(BlueprintType)
enum class ENameTBDMotionType : uint8
{
	Instant UMETA(DisplayName = "Instant (No Interpolation)"),
	SmoothLerp UMETA(DisplayName = "Smooth Eased Interpolation"),
	ElevationLift UMETA(DisplayName = "Elevation Lift & Scale"),
	SpringPop UMETA(DisplayName = "Spring Pop"),
	Flip3D UMETA(DisplayName = "3D Perspective Flip")
};

/**
 * Standardized motion configuration ensuring EnterDuration and ExitDuration
 * immediately follow the Motion field whenever motion is not Instant.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FNameTBDMotionSettings
{
	GENERATED_BODY()

	/** Motion behavior when entering or exiting the state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	ENameTBDMotionType Motion = ENameTBDMotionType::SmoothLerp;

	/** Duration (in seconds) when entering/activating the motion state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	float EnterDuration = 0.22f;

	/** Duration (in seconds) when exiting/deactivating the motion state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	float ExitDuration = 0.18f;

	/** Easing curve applied to the motion transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	ENameTBDEasingType Easing = ENameTBDEasingType::EaseOutCubic;

	/** Optional scale multiplier applied on hover/active state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (ClampMin = "0.5", ClampMax = "2.0", EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	float TargetScale = 1.03f;

	/** Optional vertical elevation offset (in slate units) when active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "Motion != ENameTBDMotionType::Instant", EditConditionHides))
	FVector2D TargetTranslationOffset = FVector2D(0.0, -2.0);
};

/**
 * A runtime-switchable shape/design target.
 * Allows widgets to switch between icons, shapes (e.g. Circle to Square when recording),
 * or color/corner presets at runtime by TargetID or normalized value range.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FNameTBDDesignTarget
{
	GENERATED_BODY()

	/** Optional identifier used to activate this design target at runtime (e.g., "Muted", "HalfVolume", "Recording"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Target")
	FName TargetID = NAME_None;

	/** Shape primitive to render when this target is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Target")
	ENameTBDShapePrimitive ShapePrimitive = ENameTBDShapePrimitive::RoundedRect;

	/** Optional brush/SVG icon for this target state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Target")
	FSlateBrush TargetBrush;

	/** Primary tint/fill color override when this target is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Target")
	FLinearColor PrimaryColor = FLinearColor::White;

	/** Corner radius (4D: TopLeft, TopRight, BottomRight, BottomLeft) for morphing shapes (e.g., Circle -> Square). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Target")
	FVector4 CornerRadii = FVector4(8.0, 8.0, 8.0, 8.0);

	/** Minimum normalized value [0..1] for automatic value-based target selection (e.g., volume slider thresholds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Target", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinValueThreshold = 0.0f;

	/** Maximum normalized value [0..1] for automatic value-based target selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design Target", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxValueThreshold = 1.0f;
};

UINTERFACE(MinimalAPI, Blueprintable)
class UNameTBDDesignTargetInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Public runtime API for querying and switching Shape/Design Targets on any supporting NameTBD widget.
 */
class NAMETBD_API INameTBDDesignTargetInterface
{
	GENERATED_BODY()

public:
	/** Switches the active shape/design target by its optional TargetID. Returns true if found. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "NameTBD|Design Targets")
	bool SetActiveDesignTargetByID(FName InTargetID, bool bAnimateTransition = true);

	/** Selects the first design target whose [MinValueThreshold, MaxValueThreshold] encompasses NormalizedValue. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "NameTBD|Design Targets")
	bool SetActiveDesignTargetByValue(float NormalizedValue, bool bAnimateTransition = true);

	/** Returns the currently active design target ID (or NAME_None if using default design). */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "NameTBD|Design Targets")
	FName GetActiveDesignTargetID() const;
};

/**
 * Math & Brush helper functions for easing and procedural rounded-box rendering.
 */
namespace NameTBDMath
{
	FORCEINLINE float EvaluateEasing(ENameTBDEasingType Easing, float Alpha)
	{
		const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
		switch (Easing)
		{
		case ENameTBDEasingType::Linear:
			return T;
		case ENameTBDEasingType::EaseInQuad:
			return T * T;
		case ENameTBDEasingType::EaseOutQuad:
			return 1.0f - (1.0f - T) * (1.0f - T);
		case ENameTBDEasingType::EaseInOutQuad:
			return T < 0.5f ? 2.0f * T * T : 1.0f - FMath::Pow(-2.0f * T + 2.0f, 2.0f) * 0.5f;
		case ENameTBDEasingType::EaseOutCubic:
			return 1.0f - FMath::Pow(1.0f - T, 3.0f);
		case ENameTBDEasingType::EaseInOutCubic:
			return T < 0.5f ? 4.0f * T * T * T : 1.0f - FMath::Pow(-2.0f * T + 2.0f, 3.0f) * 0.5f;
		case ENameTBDEasingType::EaseOutQuart:
			return 1.0f - FMath::Pow(1.0f - T, 4.0f);
		case ENameTBDEasingType::EaseInOutQuart:
			return T < 0.5f ? 8.0f * T * T * T * T : 1.0f - FMath::Pow(-2.0f * T + 2.0f, 4.0f) * 0.5f;
		case ENameTBDEasingType::EaseOutBack:
		{
			constexpr float C1 = 1.70158f;
			constexpr float C3 = C1 + 1.0f;
			return 1.0f + C3 * FMath::Pow(T - 1.0f, 3.0f) + C1 * FMath::Pow(T - 1.0f, 2.0f);
		}
		case ENameTBDEasingType::Spring:
		{
			return 1.0f - FMath::Exp(-6.0f * T) * FMath::Cos(10.0f * T);
		}
		default:
			return T;
		}
	}

	/** Signed distance to a 2D rounded box with per-corner radii (TL, TR, BR, BL). */
	FORCEINLINE float SdRoundedBox(const FVector2D& Point, const FVector2D& HalfExtents, const FVector4& CornerRadii)
	{
		double Radius = (Point.X > 0.0)
			? ((Point.Y > 0.0) ? CornerRadii.Z : CornerRadii.Y)
			: ((Point.Y > 0.0) ? CornerRadii.W : CornerRadii.X);

		Radius = FMath::Min(Radius, FMath::Min(HalfExtents.X, HalfExtents.Y));
		const FVector2D Q = FVector2D(FMath::Abs(Point.X), FMath::Abs(Point.Y)) - HalfExtents + FVector2D(Radius, Radius);
		const FVector2D MaxQ(FMath::Max(Q.X, 0.0), FMath::Max(Q.Y, 0.0));
		return static_cast<float>(MaxQ.Size() + FMath::Min(FMath::Max(Q.X, Q.Y), 0.0) - Radius);
	}

	/** Multi-stop color gradient evaluation for t in [0..1]. */
	FORCEINLINE FLinearColor SampleGradient(const TArray<FLinearColor>& Stops, float Alpha)
	{
		if (Stops.Num() == 0)
		{
			return FLinearColor::White;
		}
		if (Stops.Num() == 1)
		{
			return Stops[0];
		}
		const float Clamped = FMath::Clamp(Alpha, 0.0f, 1.0f);
		const float Scaled = Clamped * static_cast<float>(Stops.Num() - 1);
		const int32 IndexA = FMath::FloorToInt(Scaled);
		const int32 IndexB = FMath::Min(IndexA + 1, Stops.Num() - 1);
		const float Frac = Scaled - static_cast<float>(IndexA);
		return FLinearColor::LerpUsingHSV(Stops[IndexA], Stops[IndexB], Frac);
	}

	/** Creates an anti-aliased hardware RoundedBox FSlateBrush for the given 4-corner radii. */
	FORCEINLINE FSlateBrush MakeRoundedBoxBrush(const FVector4& InCornerRadii, float InOutlineWidth = 0.0f, const FLinearColor& InOutlineColor = FLinearColor::Transparent)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(FLinearColor::White);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		Brush.OutlineSettings.CornerRadii = InCornerRadii;
		Brush.OutlineSettings.Width = InOutlineWidth;
		Brush.OutlineSettings.Color = FSlateColor(InOutlineColor);
		return Brush;
	}
}
