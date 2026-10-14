// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Core/NameTBDTypes.h"
#include "SNameTBDFlippableCard.generated.h"

/**
 * Axis along which the two-sided Flippable Card rotates.
 */
UENUM(BlueprintType)
enum class ENameTBDCardFlipAxis : uint8
{
	Horizontal UMETA(DisplayName = "Horizontal (Flip Left / Right)"),
	Vertical UMETA(DisplayName = "Vertical (Flip Up / Down)"),
	Diagonal UMETA(DisplayName = "Diagonal 3D Flip")
};

DECLARE_DELEGATE_OneParam(FOnNameTBDCardFaceChangedSlate, bool /*bIsShowingBack*/);

/**
 * Slate implementation of a two-sided Flippable Card with 3D perspective-simulated flip animation,
 * automatic 90-degree midpoint face swap, dynamic elevation scale pop, and specular tilt sheen.
 */
class NAMETBD_API SNameTBDFlippableCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNameTBDFlippableCard)
		: _bInitiallyShowBack(false)
		, _FlipAxis(ENameTBDCardFlipAxis::Horizontal)
		, _MotionSettings()
		, _MidFlipElevationScale(1.08f)
		, _PerspectiveSkewStrength(0.14f)
		, _bFlipOnClick(false)
	{}
		SLATE_ARGUMENT(bool, bInitiallyShowBack)
		SLATE_ARGUMENT(ENameTBDCardFlipAxis, FlipAxis)
		SLATE_ARGUMENT(FNameTBDMotionSettings, MotionSettings)
		SLATE_ARGUMENT(float, MidFlipElevationScale)
		SLATE_ARGUMENT(float, PerspectiveSkewStrength)
		SLATE_ARGUMENT(bool, bFlipOnClick)
		SLATE_NAMED_SLOT(FArguments, FrontContent)
		SLATE_NAMED_SLOT(FArguments, BackContent)
		SLATE_EVENT(FOnNameTBDCardFaceChangedSlate, OnFaceChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void FlipToBack(bool bAnimate = true);
	void FlipToFront(bool bAnimate = true);
	void ToggleFlip(bool bAnimate = true);

	bool IsShowingBack() const { return bTargetShowBack; }
	float GetFlipProgress() const { return CurrentFlipAlpha; }

	void SetFrontContent(const TSharedRef<SWidget>& InFront);
	void SetBackContent(const TSharedRef<SWidget>& InBack);
	void SetMotionSettings(const FNameTBDMotionSettings& InMotion);
	void SetFlipAxis(ENameTBDCardFlipAxis InAxis);
	void SetFlipOnClick(bool bInFlipOnClick) { bFlipOnClick = bInFlipOnClick; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void UpdateActiveFaceVisibilityAndTransform();

private:
	TSharedPtr<class SBox> FrontSlotBox;
	TSharedPtr<class SBox> BackSlotBox;
	FOnNameTBDCardFaceChangedSlate OnFaceChanged;

	ENameTBDCardFlipAxis FlipAxis = ENameTBDCardFlipAxis::Horizontal;
	FNameTBDMotionSettings MotionSettings;
	float MidFlipElevationScale = 1.08f;
	float PerspectiveSkewStrength = 0.14f;
	bool bFlipOnClick = false;

	bool bTargetShowBack = false;
	float RawFlipProgress = 0.0f;
	float CurrentFlipAlpha = 0.0f;
};
