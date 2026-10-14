// Copyright NameTBD Plugin. All Rights Reserved.

#include "Widgets/SNameTBDDropShadow.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SNameTBDDropShadow::Construct(const FArguments& InArgs)
{
	TargetStyle = InArgs._ShadowStyle;
	MotionSettings = InArgs._MotionSettings;
	SetCanTick(true);

	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SNameTBDDropShadow::SetShadowStyle(const FNameTBDDropShadowStyle& InStyle)
{
	TargetStyle = InStyle;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SNameTBDDropShadow::SetMotionSettings(const FNameTBDMotionSettings& InMotion)
{
	MotionSettings = InMotion;
}

void SNameTBDDropShadow::SetContent(const TSharedRef<SWidget>& InContent)
{
	ChildSlot
	[
		InContent
	];
}

void SNameTBDDropShadow::SetElevationMultiplier(float InMultiplier)
{
	TargetElevationMultiplier = FMath::Max(0.0f, InMultiplier);
	if (MotionSettings.Motion == ENameTBDMotionType::Instant)
	{
		CurrentElevationMultiplier = TargetElevationMultiplier;
	}
}

void SNameTBDDropShadow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!FMath::IsNearlyEqual(CurrentElevationMultiplier, TargetElevationMultiplier, KINDA_SMALL_NUMBER))
	{
		const bool bEntering = TargetElevationMultiplier > CurrentElevationMultiplier;
		const float Duration = bEntering ? FMath::Max(MotionSettings.EnterDuration, 0.001f) : FMath::Max(MotionSettings.ExitDuration, 0.001f);
		CurrentElevationMultiplier = FMath::FInterpTo(CurrentElevationMultiplier, TargetElevationMultiplier, InDeltaTime, 1.0f / Duration);
	}
}

int32 SNameTBDDropShadow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const int32 Rings = FMath::Clamp(TargetStyle.QualityRings, 4, 24);
	const FVector2D ScaledOffset = TargetStyle.ShadowOffset * static_cast<double>(CurrentElevationMultiplier);
	const float ScaledBlur = TargetStyle.BlurRadius * CurrentElevationMultiplier;

	if (TargetStyle.Preset == ENameTBDShadowStylePreset::ModernElevation)
	{
		// 1. Tight Ambient Contact Layer
		PaintSoftShadowLayer(
			AllottedGeometry,
			OutDrawElements,
			LayerId,
			ScaledOffset * 0.25,
			FMath::Max(2.0f, ScaledBlur * 0.35f),
			TargetStyle.Spread * 0.5f,
			TargetStyle.AmbientShadowColor * Tint,
			TargetStyle.CornerRadii,
			FMath::Max(4, Rings / 2)
		);
		++LayerId;

		// 2. Wide Key Directional Penumbra Layer
		PaintSoftShadowLayer(
			AllottedGeometry,
			OutDrawElements,
			LayerId,
			ScaledOffset,
			ScaledBlur,
			TargetStyle.Spread,
			TargetStyle.ShadowColor * Tint,
			TargetStyle.CornerRadii,
			Rings
		);
		++LayerId;
	}
	else if (TargetStyle.Preset == ENameTBDShadowStylePreset::ContactFloorShadow)
	{
		const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
		const FVector2D FloorOffset(ScaledOffset.X, LocalSize.Y * 0.42 + ScaledOffset.Y);
		PaintSoftShadowLayer(
			AllottedGeometry,
			OutDrawElements,
			LayerId,
			FloorOffset,
			ScaledBlur * 0.8f,
			static_cast<float>(-LocalSize.Y * 0.32),
			TargetStyle.ShadowColor * Tint,
			FVector4(LocalSize.X * 0.5, LocalSize.X * 0.5, LocalSize.X * 0.5, LocalSize.X * 0.5),
			Rings
		);
		++LayerId;
	}
	else
	{
		// SimpleDrop, ColoredNeonGlow, SoftFloat
		PaintSoftShadowLayer(
			AllottedGeometry,
			OutDrawElements,
			LayerId,
			ScaledOffset,
			ScaledBlur,
			TargetStyle.Spread,
			TargetStyle.ShadowColor * Tint,
			TargetStyle.CornerRadii,
			Rings
		);
		++LayerId;
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

void SNameTBDDropShadow::PaintSoftShadowLayer(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FVector2D& Offset, float InBlurRadius, float InSpread, const FLinearColor& Color, const FVector4& Radii, int32 Rings) const
{
	if (Color.A <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector2D BaseSize = AllottedGeometry.GetLocalSize();
	if (BaseSize.X <= 0.0 || BaseSize.Y <= 0.0)
	{
		return;
	}

	const float EffectiveBlur = FMath::Max(0.5f, InBlurRadius);

	// Concentric Gaussian-weighted rounded-box penumbra shells from outermost soft edge to core shadow
	for (int32 RingIdx = Rings - 1; RingIdx >= 0; --RingIdx)
	{
		const float NormalizedDist = static_cast<float>(RingIdx) / static_cast<float>(Rings);
		const double Expansion = static_cast<double>(InSpread + NormalizedDist * EffectiveBlur);

		const float GaussianWeight = FMath::Exp(-3.5f * NormalizedDist * NormalizedDist) / static_cast<float>(Rings) * 2.2f;
		FLinearColor ShellColor = Color;
		ShellColor.A = FMath::Clamp(Color.A * GaussianWeight, 0.0f, 1.0f);

		const FVector2D ShellOrigin = Offset - FVector2D(Expansion, Expansion);
		const FVector2D ShellSize = BaseSize + FVector2D(Expansion * 2.0, Expansion * 2.0);

		if (ShellSize.X > 0.0 && ShellSize.Y > 0.0)
		{
			const FVector4 ExpandedRadii(
				FMath::Max(0.0, Radii.X + Expansion),
				FMath::Max(0.0, Radii.Y + Expansion),
				FMath::Max(0.0, Radii.Z + Expansion),
				FMath::Max(0.0, Radii.W + Expansion)
			);
			const FSlateBrush ShellRoundedBrush = NameTBDMath::MakeRoundedBoxBrush(ExpandedRadii);

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(ShellSize, FSlateLayoutTransform(ShellOrigin)),
				&ShellRoundedBrush,
				ESlateDrawEffect::None,
				ShellColor
			);
		}
	}
}
