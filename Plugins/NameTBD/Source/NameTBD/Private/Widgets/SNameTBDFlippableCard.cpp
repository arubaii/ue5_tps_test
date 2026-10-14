// Copyright NameTBD Plugin. All Rights Reserved.

#include "Widgets/SNameTBDFlippableCard.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SNameTBDFlippableCard::Construct(const FArguments& InArgs)
{
	bTargetShowBack = InArgs._bInitiallyShowBack;
	FlipAxis = InArgs._FlipAxis;
	MotionSettings = InArgs._MotionSettings;
	MidFlipElevationScale = InArgs._MidFlipElevationScale;
	PerspectiveSkewStrength = InArgs._PerspectiveSkewStrength;
	bFlipOnClick = InArgs._bFlipOnClick;
	OnFaceChanged = InArgs._OnFaceChanged;

	RawFlipProgress = bTargetShowBack ? 1.0f : 0.0f;
	CurrentFlipAlpha = RawFlipProgress;

	SetCanTick(true);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(FrontSlotBox, SBox)
			[
				InArgs._FrontContent.Widget
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(BackSlotBox, SBox)
			[
				InArgs._BackContent.Widget
			]
		]
	];

	UpdateActiveFaceVisibilityAndTransform();
}

void SNameTBDFlippableCard::FlipToBack(bool bAnimate)
{
	if (!bTargetShowBack)
	{
		bTargetShowBack = true;
		if (!bAnimate || MotionSettings.Motion == ENameTBDMotionType::Instant)
		{
			RawFlipProgress = 1.0f;
			CurrentFlipAlpha = 1.0f;
			UpdateActiveFaceVisibilityAndTransform();
		}
		OnFaceChanged.ExecuteIfBound(bTargetShowBack);
	}
}

void SNameTBDFlippableCard::FlipToFront(bool bAnimate)
{
	if (bTargetShowBack)
	{
		bTargetShowBack = false;
		if (!bAnimate || MotionSettings.Motion == ENameTBDMotionType::Instant)
		{
			RawFlipProgress = 0.0f;
			CurrentFlipAlpha = 0.0f;
			UpdateActiveFaceVisibilityAndTransform();
		}
		OnFaceChanged.ExecuteIfBound(bTargetShowBack);
	}
}

void SNameTBDFlippableCard::ToggleFlip(bool bAnimate)
{
	if (bTargetShowBack)
	{
		FlipToFront(bAnimate);
	}
	else
	{
		FlipToBack(bAnimate);
	}
}

void SNameTBDFlippableCard::SetFrontContent(const TSharedRef<SWidget>& InFront)
{
	if (FrontSlotBox.IsValid())
	{
		FrontSlotBox->SetContent(InFront);
	}
}

void SNameTBDFlippableCard::SetBackContent(const TSharedRef<SWidget>& InBack)
{
	if (BackSlotBox.IsValid())
	{
		BackSlotBox->SetContent(InBack);
	}
}

void SNameTBDFlippableCard::SetMotionSettings(const FNameTBDMotionSettings& InMotion)
{
	MotionSettings = InMotion;
}

void SNameTBDFlippableCard::SetFlipAxis(ENameTBDCardFlipAxis InAxis)
{
	FlipAxis = InAxis;
	UpdateActiveFaceVisibilityAndTransform();
}

FReply SNameTBDFlippableCard::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bFlipOnClick && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		ToggleFlip(true);
		return FReply::Handled();
	}
	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

void SNameTBDFlippableCard::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const float Goal = bTargetShowBack ? 1.0f : 0.0f;
	if (!FMath::IsNearlyEqual(RawFlipProgress, Goal, KINDA_SMALL_NUMBER))
	{
		if (MotionSettings.Motion == ENameTBDMotionType::Instant)
		{
			RawFlipProgress = Goal;
		}
		else
		{
			const float Duration = bTargetShowBack
				? FMath::Max(MotionSettings.EnterDuration, 0.001f)
				: FMath::Max(MotionSettings.ExitDuration, 0.001f);

			RawFlipProgress = FMath::FInterpConstantTo(RawFlipProgress, Goal, InDeltaTime, 1.0f / Duration);
		}

		CurrentFlipAlpha = FMath::Clamp(NameTBDMath::EvaluateEasing(MotionSettings.Easing, RawFlipProgress), 0.0f, 1.0f);
		UpdateActiveFaceVisibilityAndTransform();
	}
}

void SNameTBDFlippableCard::UpdateActiveFaceVisibilityAndTransform()
{
	const bool bDisplayBackNow = (CurrentFlipAlpha >= 0.5f);

	if (FrontSlotBox.IsValid())
	{
		FrontSlotBox->SetVisibility(bDisplayBackNow ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
	}
	if (BackSlotBox.IsValid())
	{
		BackSlotBox->SetVisibility(bDisplayBackNow ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
	}

	const float Theta = CurrentFlipAlpha * PI;
	const float AxialScale = FMath::Max(0.02f, FMath::Abs(FMath::Cos(Theta)));
	const float SinTheta = FMath::Sin(Theta);
	const float LiftScale = FMath::Lerp(1.0f, MidFlipElevationScale, SinTheta);
	const float SkewFactor = FMath::Sin(Theta * 2.0f) * PerspectiveSkewStrength;

	FMatrix2x2 TransformMatrix(1.0f, 0.0f, 0.0f, 1.0f);
	if (FlipAxis == ENameTBDCardFlipAxis::Horizontal)
	{
		TransformMatrix = FMatrix2x2(AxialScale * LiftScale, SkewFactor, 0.0f, LiftScale);
	}
	else if (FlipAxis == ENameTBDCardFlipAxis::Vertical)
	{
		TransformMatrix = FMatrix2x2(LiftScale, 0.0f, SkewFactor, AxialScale * LiftScale);
	}
	else
	{
		const float DiagScale = FMath::Lerp(AxialScale, 1.0f, 0.35f) * LiftScale;
		TransformMatrix = FMatrix2x2(DiagScale, SkewFactor, SkewFactor, DiagScale);
	}

	SetRenderTransformPivot(FVector2D(0.5, 0.5));
	SetRenderTransform(FSlateRenderTransform(TransformMatrix, FVector2D(0.0, -SinTheta * 6.0)));
}

int32 SNameTBDFlippableCard::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const float SinTheta = FMath::Sin(CurrentFlipAlpha * PI);
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");

	if (SinTheta > 0.01f)
	{
		const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
		const FVector2D ShadowOffset(0.0, 6.0 + static_cast<double>(SinTheta) * 14.0);
		const FLinearColor ShadowColor(0.0f, 0.0f, 0.02f, 0.28f * SinTheta);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(LocalSize, FSlateLayoutTransform(ShadowOffset)),
			WhiteBrush,
			ESlateDrawEffect::None,
			ShadowColor * InWidgetStyle.GetColorAndOpacityTint()
		);
		++LayerId;
	}

	int32 OutLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	if (SinTheta > 0.02f)
	{
		++OutLayer;
		const bool bApproachingEdge = (CurrentFlipAlpha < 0.5f);
		const FLinearColor SheenColor = bApproachingEdge
			? FLinearColor(0.0f, 0.0f, 0.0f, SinTheta * 0.22f)
			: FLinearColor(1.0f, 1.0f, 1.0f, SinTheta * 0.16f);

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			OutLayer,
			AllottedGeometry.ToPaintGeometry(),
			WhiteBrush,
			ESlateDrawEffect::None,
			SheenColor * InWidgetStyle.GetColorAndOpacityTint()
		);
	}

	return OutLayer;
}
