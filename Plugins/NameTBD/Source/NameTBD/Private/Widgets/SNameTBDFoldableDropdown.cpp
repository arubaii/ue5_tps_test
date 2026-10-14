// Copyright NameTBD Plugin. All Rights Reserved.

#include "Widgets/SNameTBDFoldableDropdown.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

void SNameTBDFoldableDropdown::Construct(const FArguments& InArgs)
{
	bTargetExpanded = InArgs._bInitiallyExpanded;
	AnimationMode = InArgs._AnimationMode;
	MotionSettings = InArgs._MotionSettings;
	bShowChevron = InArgs._bShowChevron;
	OnExpansionChanged = InArgs._OnExpansionChanged;

	RawProgress = bTargetExpanded ? 1.0f : 0.0f;
	CurrentAlpha = RawProgress;

	SetCanTick(true);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(0.0f))
			.OnClicked(this, &SNameTBDFoldableDropdown::HandleHeaderClicked)
			[
				SAssignNew(HeaderContainer, SBox)
				[
					InArgs._HeaderContent.Widget
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(BodyContainer, SBox)
			.Clipping(EWidgetClipping::ClipToBoundsAlways)
			[
				InArgs._BodyContent.Widget
			]
		]
	];
}

FReply SNameTBDFoldableDropdown::HandleHeaderClicked()
{
	SetExpanded(!bTargetExpanded, true);
	return FReply::Handled();
}

void SNameTBDFoldableDropdown::SetExpanded(bool bInExpanded, bool bAnimate)
{
	if (bTargetExpanded != bInExpanded)
	{
		bTargetExpanded = bInExpanded;
		if (!bAnimate || MotionSettings.Motion == ENameTBDMotionType::Instant)
		{
			RawProgress = bTargetExpanded ? 1.0f : 0.0f;
			CurrentAlpha = RawProgress;
		}
		OnExpansionChanged.ExecuteIfBound(bTargetExpanded);
		Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

void SNameTBDFoldableDropdown::SetHeaderContent(const TSharedRef<SWidget>& InHeader)
{
	if (HeaderContainer.IsValid())
	{
		HeaderContainer->SetContent(InHeader);
	}
}

void SNameTBDFoldableDropdown::SetBodyContent(const TSharedRef<SWidget>& InBody)
{
	if (BodyContainer.IsValid())
	{
		BodyContainer->SetContent(InBody);
	}
}

void SNameTBDFoldableDropdown::SetMotionSettings(const FNameTBDMotionSettings& InMotion)
{
	MotionSettings = InMotion;
}

void SNameTBDFoldableDropdown::SetAnimationMode(ENameTBDFoldAnimationMode InMode)
{
	AnimationMode = InMode;
}

void SNameTBDFoldableDropdown::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const float Goal = bTargetExpanded ? 1.0f : 0.0f;
	if (!FMath::IsNearlyEqual(RawProgress, Goal, KINDA_SMALL_NUMBER))
	{
		if (MotionSettings.Motion == ENameTBDMotionType::Instant)
		{
			RawProgress = Goal;
		}
		else
		{
			const float Duration = bTargetExpanded
				? FMath::Max(MotionSettings.EnterDuration, 0.001f)
				: FMath::Max(MotionSettings.ExitDuration, 0.001f);

			RawProgress = FMath::FInterpConstantTo(RawProgress, Goal, InDeltaTime, 1.0f / Duration);
		}

		CurrentAlpha = FMath::Clamp(NameTBDMath::EvaluateEasing(MotionSettings.Easing, RawProgress), 0.0f, 1.0f);

		if (BodyContainer.IsValid())
		{
			BodyContainer->SetVisibility(CurrentAlpha <= 0.001f ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
			if (AnimationMode == ENameTBDFoldAnimationMode::Scale3DUnfold)
			{
				BodyContainer->SetRenderTransformPivot(FVector2D(0.5, 0.0));
				BodyContainer->SetRenderTransform(FSlateRenderTransform(FScale2D(1.0f, FMath::Max(0.01f, CurrentAlpha))));
			}
			else
			{
				BodyContainer->SetRenderTransform(FSlateRenderTransform());
			}
		}

		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

FVector2D SNameTBDFoldableDropdown::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	(void)LayoutScaleMultiplier;
	const FVector2D HeaderSize = HeaderContainer.IsValid() ? HeaderContainer->GetDesiredSize() : FVector2D::ZeroVector;
	const FVector2D BodyFullSize = BodyContainer.IsValid() ? BodyContainer->GetDesiredSize() : FVector2D::ZeroVector;

	const double AnimatedBodyHeight = BodyFullSize.Y * static_cast<double>(CurrentAlpha);
	return FVector2D(FMath::Max(HeaderSize.X, BodyFullSize.X), HeaderSize.Y + AnimatedBodyHeight);
}

int32 SNameTBDFoldableDropdown::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FWidgetStyle ModifiedStyle = InWidgetStyle;
	if (AnimationMode == ENameTBDFoldAnimationMode::SlideAndFade || AnimationMode == ENameTBDFoldAnimationMode::Scale3DUnfold)
	{
		ModifiedStyle.BlendColorAndOpacityTint(FLinearColor(1.0f, 1.0f, 1.0f, FMath::Lerp(0.15f, 1.0f, CurrentAlpha)));
	}

	int32 OutLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, ModifiedStyle, bParentEnabled);

	if (bShowChevron && HeaderContainer.IsValid())
	{
		const FVector2D HeaderSize = HeaderContainer->GetDesiredSize();
		const FVector2D ChevronCenter(AllottedGeometry.GetLocalSize().X - 18.0, FMath::Max(12.0, HeaderSize.Y * 0.5));
		const double AngleRad = FMath::DegreesToRadians(FMath::Lerp(0.0, 180.0, static_cast<double>(CurrentAlpha)));
		const double CosA = FMath::Cos(AngleRad);
		const double SinA = FMath::Sin(AngleRad);

		auto RotatePt = [&](const FVector2D& LocalPt) -> FVector2D
		{
			return ChevronCenter + FVector2D(LocalPt.X * CosA - LocalPt.Y * SinA, LocalPt.X * SinA + LocalPt.Y * CosA);
		};

		TArray<FVector2D> ChevronPoints;
		ChevronPoints.Add(RotatePt(FVector2D(-5.0, -2.5)));
		ChevronPoints.Add(RotatePt(FVector2D(0.0, 2.5)));
		ChevronPoints.Add(RotatePt(FVector2D(5.0, -2.5)));

		++OutLayer;
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			OutLayer,
			AllottedGeometry.ToPaintGeometry(),
			ChevronPoints,
			ESlateDrawEffect::None,
			InWidgetStyle.GetColorAndOpacityTint(),
			true,
			1.75f
		);
	}

	return OutLayer;
}
