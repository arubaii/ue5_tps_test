// Copyright NameTBD Plugin. All Rights Reserved.

#include "Widgets/SNameTBDProgressBar.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"

void SNameTBDProgressBar::Construct(const FArguments& InArgs)
{
	PercentAttribute = InArgs._Percent;
	Style = InArgs._Style;
	MotionSettings = InArgs._MotionSettings;

	const float Initial = FMath::Clamp(PercentAttribute.Get(0.75f), 0.0f, 1.0f);
	TargetPercent = Initial;
	DisplayedPrimaryPercent = Initial;
	DisplayedGhostPercent = Initial;

	SetCanTick(true);
}

void SNameTBDProgressBar::SetPercent(TAttribute<float> InPercent)
{
	PercentAttribute = MoveTemp(InPercent);
	const float NewTarget = FMath::Clamp(PercentAttribute.Get(TargetPercent), 0.0f, 1.0f);
	if (!FMath::IsNearlyEqual(NewTarget, TargetPercent, KINDA_SMALL_NUMBER))
	{
		bLastChangeWasDamage = (NewTarget < TargetPercent);
		TargetPercent = NewTarget;
		GhostDelayTimer = Style.GhostHoldDelay;

		if (MotionSettings.Motion == ENameTBDMotionType::Instant)
		{
			DisplayedPrimaryPercent = TargetPercent;
			DisplayedGhostPercent = TargetPercent;
		}
	}
}

void SNameTBDProgressBar::SetStyle(const FNameTBDProgressBarStyle& InStyle)
{
	Style = InStyle;
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

void SNameTBDProgressBar::SetMotionSettings(const FNameTBDMotionSettings& InMotion)
{
	MotionSettings = InMotion;
}

FVector2D SNameTBDProgressBar::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	if (Style.Shape == ENameTBDProgressBarShape::Circular)
	{
		return FVector2D(Style.BarLength, Style.BarLength);
	}
	if (Style.Shape == ENameTBDProgressBarShape::Both)
	{
		const float CircleDiameter = Style.BarWidth * 2.4f;
		return FVector2D(Style.BarLength + CircleDiameter, FMath::Max(Style.BarWidth, CircleDiameter));
	}
	const bool bVertical = (Style.FillDirection == ENameTBDLinearFillDirection::BottomToTop || Style.FillDirection == ENameTBDLinearFillDirection::TopToBottom);
	return bVertical ? FVector2D(Style.BarWidth, Style.BarLength) : FVector2D(Style.BarLength, Style.BarWidth);
}

void SNameTBDProgressBar::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (PercentAttribute.IsBound())
	{
		SetPercent(PercentAttribute.Get());
	}

	if (MotionSettings.Motion == ENameTBDMotionType::Instant)
	{
		DisplayedPrimaryPercent = TargetPercent;
		DisplayedGhostPercent = TargetPercent;
		return;
	}

	// Use EnterDuration when increasing (healing/filling) and ExitDuration when decreasing (damage/draining)
	const float PrimaryDuration = bLastChangeWasDamage
		? FMath::Max(MotionSettings.ExitDuration, 0.001f)
		: FMath::Max(MotionSettings.EnterDuration, 0.001f);

	const float PrimarySpeed = 1.0f / PrimaryDuration;
	if (bLastChangeWasDamage)
	{
		// Primary bar drops fast on damage, ghost bar trails behind after GhostHoldDelay
		DisplayedPrimaryPercent = FMath::FInterpConstantTo(DisplayedPrimaryPercent, TargetPercent, InDeltaTime, PrimarySpeed * 2.0f);
		if (Style.bEnableGhostBar)
		{
			if (GhostDelayTimer > 0.0f)
			{
				GhostDelayTimer -= InDeltaTime;
			}
			else
			{
				DisplayedGhostPercent = FMath::FInterpTo(DisplayedGhostPercent, TargetPercent, InDeltaTime, PrimarySpeed * 3.5f);
			}
		}
		else
		{
			DisplayedGhostPercent = DisplayedPrimaryPercent;
		}
	}
	else
	{
		// Ghost bar jumps ahead on heal to preview gain, primary bar smoothly fills up to meet it
		DisplayedGhostPercent = FMath::FInterpTo(DisplayedGhostPercent, TargetPercent, InDeltaTime, PrimarySpeed * 5.0f);
		if (GhostDelayTimer > 0.0f)
		{
			GhostDelayTimer -= InDeltaTime;
		}
		else
		{
			DisplayedPrimaryPercent = FMath::FInterpTo(DisplayedPrimaryPercent, TargetPercent, InDeltaTime, PrimarySpeed * 3.0f);
		}
	}
}

FLinearColor SNameTBDProgressBar::ResolveFillColor(float NormalizedU) const
{
	if (Style.GradientColors.Num() >= 2)
	{
		const float LookupAlpha = Style.bSampleGradientByValue ? DisplayedPrimaryPercent : NormalizedU;
		return NameTBDMath::SampleGradient(Style.GradientColors, LookupAlpha);
	}
	return Style.FillColor;
}

int32 SNameTBDProgressBar::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();

	if (Style.Shape == ENameTBDProgressBarShape::Linear)
	{
		PaintLinearBar(AllottedGeometry, OutDrawElements, LayerId, WidgetTint, FVector2D::ZeroVector, LocalSize);
	}
	else if (Style.Shape == ENameTBDProgressBarShape::Circular)
	{
		const float OuterRadius = static_cast<float>(FMath::Min(LocalSize.X, LocalSize.Y) * 0.5);
		PaintCircularBar(AllottedGeometry, OutDrawElements, LayerId, WidgetTint, LocalSize * 0.5, OuterRadius, Style.BarWidth);
	}
	else // ENameTBDProgressBarShape::Both
	{
		const float CircleDiameter = static_cast<float>(FMath::Min(LocalSize.Y, static_cast<double>(Style.BarWidth * 2.4f)));
		const float CircleRadius = CircleDiameter * 0.5f;
		const FVector2D CircleCenter(CircleRadius, LocalSize.Y * 0.5);
		PaintCircularBar(AllottedGeometry, OutDrawElements, LayerId, WidgetTint, CircleCenter, CircleRadius, Style.BarWidth * 0.45f);

		const FVector2D LinearOrigin(CircleDiameter + 4.0, (LocalSize.Y - Style.BarWidth) * 0.5);
		const FVector2D LinearSize(FMath::Max(0.0, LocalSize.X - CircleDiameter - 4.0), Style.BarWidth);
		PaintLinearBar(AllottedGeometry, OutDrawElements, LayerId, WidgetTint, LinearOrigin, LinearSize);
	}

	if (Style.OpacityMaskBrush.GetResourceObject() != nullptr)
	{
		++LayerId;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(),
			&Style.OpacityMaskBrush,
			ESlateDrawEffect::None,
			FLinearColor(1.0f, 1.0f, 1.0f, Style.OpacityMaskBrush.TintColor.GetSpecifiedColor().A)
		);
	}

	return LayerId;
}

void SNameTBDProgressBar::PaintLinearBar(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
	int32& InOutLayerId, const FLinearColor& Tint, const FVector2D& LocalOrigin, const FVector2D& LocalSize) const
{
	const FVector4 Radii(Style.CornerRadius, Style.CornerRadius, Style.CornerRadius, Style.CornerRadius);
	const FSlateBrush RoundedBrush = NameTBDMath::MakeRoundedBoxBrush(Radii);
	const FSlateBrush* BrushToUse = (Style.CornerRadius > 0.0f) ? &RoundedBrush : FCoreStyle::Get().GetBrush("GenericWhiteBox");

	const int32 NumSegments = FMath::Max(1, Style.SegmentCount);
	const bool bVertical = (Style.FillDirection == ENameTBDLinearFillDirection::BottomToTop || Style.FillDirection == ENameTBDLinearFillDirection::TopToBottom);
	const float TotalMainAxis = static_cast<float>(bVertical ? LocalSize.Y : LocalSize.X);
	const float TotalGap = (NumSegments > 1) ? Style.SegmentGap * static_cast<float>(NumSegments - 1) : 0.0f;
	const float SegmentMainSize = FMath::Max(1.0f, (TotalMainAxis - TotalGap) / static_cast<float>(NumSegments));

	const float LowPercent = FMath::Min(DisplayedPrimaryPercent, DisplayedGhostPercent);
	const float HighPercent = FMath::Max(DisplayedPrimaryPercent, DisplayedGhostPercent);
	const FLinearColor GhostColor = (bLastChangeWasDamage ? Style.DamageGhostColor : Style.HealGhostColor) * Tint;
	const FLinearColor TrackColor = Style.BackgroundColor * Tint;

	for (int32 SegIdx = 0; SegIdx < NumSegments; ++SegIdx)
	{
		const float SegStartNorm = static_cast<float>(SegIdx) / static_cast<float>(NumSegments);
		const float SegEndNorm = static_cast<float>(SegIdx + 1) / static_cast<float>(NumSegments);
		const float OffsetAlongMain = static_cast<float>(SegIdx) * (SegmentMainSize + Style.SegmentGap);

		FVector2D SegPos = LocalOrigin;
		FVector2D SegSize = LocalSize;
		if (bVertical)
		{
			SegPos.Y += (Style.FillDirection == ENameTBDLinearFillDirection::BottomToTop)
				? (TotalMainAxis - OffsetAlongMain - SegmentMainSize)
				: OffsetAlongMain;
			SegSize.Y = SegmentMainSize;
		}
		else
		{
			SegPos.X += (Style.FillDirection == ENameTBDLinearFillDirection::RightToLeft)
				? (TotalMainAxis - OffsetAlongMain - SegmentMainSize)
				: OffsetAlongMain;
			SegSize.X = SegmentMainSize;
		}

		// 1. Background Track Segment
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			InOutLayerId,
			AllottedGeometry.ToPaintGeometry(SegSize, FSlateLayoutTransform(SegPos)),
			BrushToUse,
			ESlateDrawEffect::None,
			TrackColor
		);

		// 2. Ghost Segment Fill
		if (Style.bEnableGhostBar && HighPercent > SegStartNorm)
		{
			const float LocalGhostFill = FMath::Clamp((HighPercent - SegStartNorm) / (SegEndNorm - SegStartNorm), 0.0f, 1.0f);
			FVector2D GhostSize = SegSize;
			FVector2D GhostPos = SegPos;
			if (bVertical)
			{
				GhostSize.Y *= LocalGhostFill;
				if (Style.FillDirection == ENameTBDLinearFillDirection::BottomToTop)
				{
					GhostPos.Y = SegPos.Y + (SegSize.Y - GhostSize.Y);
				}
			}
			else
			{
				GhostSize.X *= LocalGhostFill;
				if (Style.FillDirection == ENameTBDLinearFillDirection::RightToLeft)
				{
					GhostPos.X = SegPos.X + (SegSize.X - GhostSize.X);
				}
			}

			FSlateDrawElement::MakeBox(
				OutDrawElements,
				InOutLayerId + 1,
				AllottedGeometry.ToPaintGeometry(GhostSize, FSlateLayoutTransform(GhostPos)),
				BrushToUse,
				ESlateDrawEffect::None,
				GhostColor
			);
		}

		// 3. Primary Fill Segment (with multi-stop gradient support)
		const float PrimaryFillTarget = bLastChangeWasDamage ? LowPercent : DisplayedPrimaryPercent;
		if (PrimaryFillTarget > SegStartNorm)
		{
			const float LocalFill = FMath::Clamp((PrimaryFillTarget - SegStartNorm) / (SegEndNorm - SegStartNorm), 0.0f, 1.0f);
			FVector2D FillSize = SegSize;
			FVector2D FillPos = SegPos;

			if (bVertical)
			{
				FillSize.Y *= LocalFill;
				if (Style.FillDirection == ENameTBDLinearFillDirection::BottomToTop)
				{
					FillPos.Y = SegPos.Y + (SegSize.Y - FillSize.Y);
				}
			}
			else if (Style.FillDirection == ENameTBDLinearFillDirection::CenterOutward)
			{
				FillSize.X *= LocalFill;
				FillPos.X = SegPos.X + (SegSize.X - FillSize.X) * 0.5;
			}
			else
			{
				FillSize.X *= LocalFill;
				if (Style.FillDirection == ENameTBDLinearFillDirection::RightToLeft)
				{
					FillPos.X = SegPos.X + (SegSize.X - FillSize.X);
				}
			}

			const FLinearColor SegColor = ResolveFillColor((SegStartNorm + SegEndNorm) * 0.5f * LocalFill) * Tint;
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				InOutLayerId + 2,
				AllottedGeometry.ToPaintGeometry(FillSize, FSlateLayoutTransform(FillPos)),
				BrushToUse,
				ESlateDrawEffect::None,
				SegColor
			);
		}
	}

	InOutLayerId += 3;
}

void SNameTBDProgressBar::PaintCircularBar(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
	int32& InOutLayerId, const FLinearColor& Tint, const FVector2D& Center, float OuterRadius, float Thickness) const
{
	const float InnerRadius = FMath::Max(1.0f, OuterRadius - Thickness);
	const int32 NumSegments = FMath::Max(1, Style.SegmentCount);
	const float TotalGapDeg = (NumSegments > 1) ? (Style.SegmentGap * static_cast<float>(NumSegments)) : 0.0f;
	const float AvailableSweep = FMath::Max(1.0f, Style.CircularSweepAngle - TotalGapDeg);
	const float SegSweep = AvailableSweep / static_cast<float>(NumSegments);
	const float DirSign = Style.bClockwise ? 1.0f : -1.0f;

	const float LowPercent = FMath::Min(DisplayedPrimaryPercent, DisplayedGhostPercent);
	const float HighPercent = FMath::Max(DisplayedPrimaryPercent, DisplayedGhostPercent);
	const FLinearColor GhostColor = (bLastChangeWasDamage ? Style.DamageGhostColor : Style.HealGhostColor) * Tint;
	const FLinearColor TrackColor = Style.BackgroundColor * Tint;

	for (int32 SegIdx = 0; SegIdx < NumSegments; ++SegIdx)
	{
		const float SegStartNorm = static_cast<float>(SegIdx) / static_cast<float>(NumSegments);
		const float SegEndNorm = static_cast<float>(SegIdx + 1) / static_cast<float>(NumSegments);
		const float SegStartDeg = Style.CircularStartAngle + DirSign * static_cast<float>(SegIdx) * (SegSweep + (NumSegments > 1 ? Style.SegmentGap : 0.0f));
		const float SegEndDeg = SegStartDeg + DirSign * SegSweep;

		// 1. Background Track Arc
		AppendArcRibbon(OutDrawElements, InOutLayerId, AllottedGeometry, Center, InnerRadius, OuterRadius, SegStartDeg, SegEndDeg, TrackColor, TrackColor, Style.EdgeSmoothness);

		// 2. Ghost Arc
		if (Style.bEnableGhostBar && HighPercent > SegStartNorm)
		{
			const float LocalGhost = FMath::Clamp((HighPercent - SegStartNorm) / (SegEndNorm - SegStartNorm), 0.0f, 1.0f);
			const float GhostEndDeg = FMath::Lerp(SegStartDeg, SegEndDeg, LocalGhost);
			AppendArcRibbon(OutDrawElements, InOutLayerId + 1, AllottedGeometry, Center, InnerRadius, OuterRadius, SegStartDeg, GhostEndDeg, GhostColor, GhostColor, Style.EdgeSmoothness);
		}

		// 3. Primary Fill Arc
		const float PrimaryFillTarget = bLastChangeWasDamage ? LowPercent : DisplayedPrimaryPercent;
		if (PrimaryFillTarget > SegStartNorm)
		{
			const float LocalFill = FMath::Clamp((PrimaryFillTarget - SegStartNorm) / (SegEndNorm - SegStartNorm), 0.0f, 1.0f);
			const float FillEndDeg = FMath::Lerp(SegStartDeg, SegEndDeg, LocalFill);
			const FLinearColor CStart = ResolveFillColor(SegStartNorm) * Tint;
			const FLinearColor CEnd = ResolveFillColor(FMath::Lerp(SegStartNorm, SegEndNorm, LocalFill)) * Tint;
			AppendArcRibbon(OutDrawElements, InOutLayerId + 2, AllottedGeometry, Center, InnerRadius, OuterRadius, SegStartDeg, FillEndDeg, CStart, CEnd, Style.EdgeSmoothness);
		}
	}

	InOutLayerId += 3;
}

void SNameTBDProgressBar::AppendArcRibbon(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& AllottedGeometry,
	const FVector2D& Center, float InnerR, float OuterR, float StartDeg, float EndDeg,
	const FLinearColor& ColorStart, const FLinearColor& ColorEnd, float FeatherWidth) const
{
	(void)FeatherWidth;
	const float ArcSpan = FMath::Abs(EndDeg - StartDeg);
	if (ArcSpan <= 0.05f)
	{
		return;
	}

	const int32 Steps = FMath::Clamp(FMath::CeilToInt(ArcSpan / 4.0f), 4, 96);
	TArray<FSlateVertex> Vertices;
	TArray<SlateIndex> Indices;
	Vertices.Reserve((Steps + 1) * 2);
	Indices.Reserve(Steps * 6);

	const FSlateRenderTransform& RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();

	for (int32 i = 0; i <= Steps; ++i)
	{
		const float Alpha = static_cast<float>(i) / static_cast<float>(Steps);
		const float AngleRad = FMath::DegreesToRadians(FMath::Lerp(StartDeg, EndDeg, Alpha) - 90.0f);
		const FVector2D Dir(FMath::Cos(AngleRad), FMath::Sin(AngleRad));

		const FVector2D InnerPt = Center + Dir * InnerR;
		const FVector2D OuterPt = Center + Dir * OuterR;
		const FColor StepColor = FLinearColor::LerpUsingHSV(ColorStart, ColorEnd, Alpha).ToFColor(true);

		Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, FVector2f(InnerPt), FVector2f(Alpha, 0.0f), StepColor));
		Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RenderTransform, FVector2f(OuterPt), FVector2f(Alpha, 1.0f), StepColor));

		if (i < Steps)
		{
			const int32 BaseIdx = i * 2;
			Indices.Add(BaseIdx);
			Indices.Add(BaseIdx + 1);
			Indices.Add(BaseIdx + 2);

			Indices.Add(BaseIdx + 1);
			Indices.Add(BaseIdx + 3);
			Indices.Add(BaseIdx + 2);
		}
	}

	const FSlateResourceHandle& ResourceHandle = FCoreStyle::Get().GetBrush("GenericWhiteBox")->GetRenderingResource();
	FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, ResourceHandle, Vertices, Indices, nullptr, 0, 0);
}
