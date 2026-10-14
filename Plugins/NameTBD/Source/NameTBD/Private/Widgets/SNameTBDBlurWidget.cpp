// Copyright NameTBD Plugin. All Rights Reserved.

#include "Widgets/SNameTBDBlurWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"

void SNameTBDBlurWidget::Construct(const FArguments& InArgs)
{
	Style = InArgs._Style;
	FallbackRoundedBrush = NameTBDMath::MakeRoundedBoxBrush(Style.CornerRadii);
	FallbackRoundedBrush.TintColor = FSlateColor(Style.LowSpecFallbackColor);

	ChildSlot
	[
		SAssignNew(InternalRHIBlur, SBackgroundBlur)
		.BlurStrength(Style.BlurStrength)
		.CornerRadius(Style.CornerRadii)
		.LowQualityFallbackBrush(&FallbackRoundedBrush)
		.Padding(FMargin(0.0f))
		.HAlign(InArgs._HAlign)
		.VAlign(InArgs._VAlign)
		[
			SAssignNew(ContentHostBox, SBox)
			.Padding(InArgs._Padding)
			[
				InArgs._Content.Widget
			]
		]
	];

	SyncInternalBlurWidget();
}

void SNameTBDBlurWidget::SyncInternalBlurWidget()
{
	if (!InternalRHIBlur.IsValid())
	{
		return;
	}

	const FNameTBDGlassBlurStyle Effective = ResolveEffectiveStyle();
	FallbackRoundedBrush = NameTBDMath::MakeRoundedBoxBrush(Effective.CornerRadii);
	FallbackRoundedBrush.TintColor = FSlateColor(Effective.LowSpecFallbackColor);

	InternalRHIBlur->SetBlurStrength(Effective.BlurStrength);
	InternalRHIBlur->SetCornerRadius(Effective.CornerRadii);
	if (Effective.OverrideBlurRadius > 0)
	{
		InternalRHIBlur->SetBlurRadius(Effective.OverrideBlurRadius);
	}
	else
	{
		InternalRHIBlur->SetBlurRadius(TOptional<int32>());
	}
}

void SNameTBDBlurWidget::SetStyle(const FNameTBDGlassBlurStyle& InStyle)
{
	Style = InStyle;
	SyncInternalBlurWidget();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SNameTBDBlurWidget::SetContent(const TSharedRef<SWidget>& InContent)
{
	if (ContentHostBox.IsValid())
	{
		ContentHostBox->SetContent(InContent);
	}
}

void SNameTBDBlurWidget::SetContentPadding(TAttribute<FMargin> InPadding)
{
	if (ContentHostBox.IsValid())
	{
		ContentHostBox->SetPadding(MoveTemp(InPadding));
	}
}

void SNameTBDBlurWidget::SetInteractiveModulation(float InBlurMultiplier, float InTintBrightnessOffset, float InRimBoost)
{
	InteractiveBlurMultiplier = InBlurMultiplier;
	InteractiveTintOffset = InTintBrightnessOffset;
	InteractiveRimBoost = InRimBoost;
	SyncInternalBlurWidget();
	Invalidate(EInvalidateWidgetReason::Paint);
}

FNameTBDGlassBlurStyle SNameTBDBlurWidget::ResolveEffectiveStyle() const
{
	FNameTBDGlassBlurStyle Effective = Style;
	if (Effective.ThemePreset == ENameTBDGlassThemeMode::LightMode)
	{
		Effective.SurfaceTint = FLinearColor(0.98f, 0.99f, 1.0f, 0.28f);
		Effective.RimHighlightColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.72f);
		Effective.RimLowlightColor = FLinearColor(0.75f, 0.80f, 0.88f, 0.25f);
	}
	else if (Effective.ThemePreset == ENameTBDGlassThemeMode::DarkMode)
	{
		Effective.SurfaceTint = FLinearColor(0.06f, 0.08f, 0.12f, 0.32f);
		Effective.RimHighlightColor = FLinearColor(0.78f, 0.84f, 0.96f, 0.35f);
		Effective.RimLowlightColor = FLinearColor(0.20f, 0.24f, 0.32f, 0.12f);
	}

	Effective.BlurStrength = FMath::Max(0.0f, Effective.BlurStrength * InteractiveBlurMultiplier);
	Effective.SurfaceTint.R = FMath::Clamp(Effective.SurfaceTint.R + InteractiveTintOffset, 0.0f, 1.0f);
	Effective.SurfaceTint.G = FMath::Clamp(Effective.SurfaceTint.G + InteractiveTintOffset, 0.0f, 1.0f);
	Effective.SurfaceTint.B = FMath::Clamp(Effective.SurfaceTint.B + InteractiveTintOffset, 0.0f, 1.0f);
	Effective.RimHighlightColor.A = FMath::Clamp(Effective.RimHighlightColor.A + InteractiveRimBoost, 0.0f, 1.0f);
	return Effective;
}

int32 SNameTBDBlurWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FNameTBDGlassBlurStyle Effective = ResolveEffectiveStyle();
	const FSlateBrush RoundedSurfaceBrush = NameTBDMath::MakeRoundedBoxBrush(Effective.CornerRadii);

	// 1. Paint underlying SBackgroundBlur (executes RHI Gaussian blur on D3D11/D3D12/Vulkan/Metal)
	// plus child content at high layer
	int32 PostBlurLayerId = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bEnabled);

	// 2. Semi-Opaque Glass Tint Film (drawn right above blur layer so text remains crisp and readable)
	FLinearColor FilmColor = Effective.SurfaceTint;
	if (Effective.SurfaceGradientStops.Num() >= 2)
	{
		FilmColor = NameTBDMath::SampleGradient(Effective.SurfaceGradientStops, 0.5f);
	}
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId + 1,
		AllottedGeometry.ToPaintGeometry(),
		&RoundedSurfaceBrush,
		DrawEffects,
		FilmColor * WidgetTint
	);

	// 3. Acrylic / Frosted Micro-Luminance Diffusion Layer
	if (Effective.BlurMode != ENameTBDBlurMode::StandardGaussian && Effective.FrostLuminance > KINDA_SMALL_NUMBER)
	{
		const FLinearColor FrostOverlay(1.0f, 1.0f, 1.0f, Effective.FrostLuminance * 0.35f);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(),
			&RoundedSurfaceBrush,
			DrawEffects,
			FrostOverlay * WidgetTint
		);
	}

	// 4. Directional Specular Rim Lighting Border on top of glass surface
	if (Effective.bEnableSpecularRim && Effective.RimThickness > 0.0f)
	{
		++PostBlurLayerId;
		PaintSpecularRimBorder(AllottedGeometry, OutDrawElements, PostBlurLayerId, WidgetTint, Effective);
	}

	return PostBlurLayerId;
}

void SNameTBDBlurWidget::PaintSpecularRimBorder(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FLinearColor& WidgetTint, const FNameTBDGlassBlurStyle& EffectiveStyle) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	if (LocalSize.X <= 2.0 || LocalSize.Y <= 2.0)
	{
		return;
	}

	const float LightRad = FMath::DegreesToRadians(EffectiveStyle.LightDirectionDegrees);
	const FVector2D LightDir(FMath::Cos(LightRad), FMath::Sin(LightRad));
	const FSlateRenderTransform& RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();

	// Construct perimeter quad strip with directional rim specular falloff
	constexpr int32 PerimeterSteps = 32;
	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;
	Vertices.Reserve((PerimeterSteps + 1) * 2);
	Indices.Reserve(PerimeterSteps * 6);

	const FVector2D HalfSize = LocalSize * 0.5;
	const double RawAvgRadius = (EffectiveStyle.CornerRadii.X + EffectiveStyle.CornerRadii.Y + EffectiveStyle.CornerRadii.Z + EffectiveStyle.CornerRadii.W) * 0.25;
	const float AvgRadius = static_cast<float>(FMath::Clamp(RawAvgRadius, 0.0, FMath::Min(HalfSize.X, HalfSize.Y)));

	for (int32 i = 0; i <= PerimeterSteps; ++i)
	{
		const float T = static_cast<float>(i) / static_cast<float>(PerimeterSteps);
		const float Angle = T * 2.0f * PI;
		const FVector2D UnitNormal(FMath::Cos(Angle), FMath::Sin(Angle));

		const FVector2D InnerBox = HalfSize - FVector2D(AvgRadius, AvgRadius);
		const FVector2D CornerCenter(
			FMath::Sign(UnitNormal.X) * InnerBox.X,
			FMath::Sign(UnitNormal.Y) * InnerBox.Y
		);
		const FVector2D OuterPt = HalfSize + CornerCenter + UnitNormal * AvgRadius;
		const FVector2D InnerPt = OuterPt - UnitNormal * EffectiveStyle.RimThickness;

		const float SpecularFactor = static_cast<float>(FMath::Clamp((FVector2D::DotProduct(UnitNormal, LightDir) + 1.0) * 0.5, 0.0, 1.0));
		FLinearColor EdgeColor = FLinearColor::LerpUsingHSV(EffectiveStyle.RimLowlightColor, EffectiveStyle.RimHighlightColor, SpecularFactor);
		if (EffectiveStyle.BlurMode == ENameTBDBlurMode::ChromaticEdgeGlass)
		{
			EdgeColor.R *= (0.85f + 0.3f * SpecularFactor);
			EdgeColor.B *= (1.15f - 0.2f * SpecularFactor);
		}
		const FColor PackedColor = (EdgeColor * WidgetTint).ToFColor(true);

		Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, FVector2f(InnerPt), FVector2f(T, 0.0f), PackedColor));
		Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, FVector2f(OuterPt), FVector2f(T, 1.0f), PackedColor));

		if (i < PerimeterSteps)
		{
			const int32 Base = i * 2;
			Indices.Add(Base);
			Indices.Add(Base + 1);
			Indices.Add(Base + 2);

			Indices.Add(Base + 1);
			Indices.Add(Base + 3);
			Indices.Add(Base + 2);
		}
	}

	const FSlateResourceHandle& Handle = FCoreStyle::Get().GetBrush("GenericWhiteBox")->GetRenderingResource();
	FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, Handle, Vertices, Indices, nullptr, 0, 0);
}
