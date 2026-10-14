// Copyright NameTBD Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Core/NameTBDTypes.h"
#include "SNameTBDFoldableDropdown.generated.h"

/**
 * Collapse/Expand animation style for the Foldable Dropdown widget.
 */
UENUM(BlueprintType)
enum class ENameTBDFoldAnimationMode : uint8
{
	SlideAndClip UMETA(DisplayName = "Smooth Height Slide & Clip"),
	SlideAndFade UMETA(DisplayName = "Smooth Slide + Opacity Fade"),
	Scale3DUnfold UMETA(DisplayName = "3D Accordion Unfold")
};

DECLARE_DELEGATE_OneParam(FOnNameTBDFoldStateChangedSlate, bool /*bIsExpanded*/);

/**
 * Slate implementation of a smooth foldable/collapsible dropdown container.
 * Animates layout size, scissor clipping, child opacity, and header chevron rotation.
 */
class NAMETBD_API SNameTBDFoldableDropdown : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNameTBDFoldableDropdown)
		: _bInitiallyExpanded(false)
		, _AnimationMode(ENameTBDFoldAnimationMode::SlideAndFade)
		, _MotionSettings()
		, _bShowChevron(true)
	{}
		SLATE_ARGUMENT(bool, bInitiallyExpanded)
		SLATE_ARGUMENT(ENameTBDFoldAnimationMode, AnimationMode)
		SLATE_ARGUMENT(FNameTBDMotionSettings, MotionSettings)
		SLATE_ARGUMENT(bool, bShowChevron)
		SLATE_NAMED_SLOT(FArguments, HeaderContent)
		SLATE_NAMED_SLOT(FArguments, BodyContent)
		SLATE_EVENT(FOnNameTBDFoldStateChangedSlate, OnExpansionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetExpanded(bool bInExpanded, bool bAnimate = true);
	bool IsExpanded() const { return bTargetExpanded; }
	float GetExpansionAlpha() const { return CurrentAlpha; }

	void SetHeaderContent(const TSharedRef<SWidget>& InHeader);
	void SetBodyContent(const TSharedRef<SWidget>& InBody);
	void SetMotionSettings(const FNameTBDMotionSettings& InMotion);
	void SetAnimationMode(ENameTBDFoldAnimationMode InMode);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FReply HandleHeaderClicked();

private:
	TSharedPtr<class SBox> HeaderContainer;
	TSharedPtr<class SBox> BodyContainer;
	FOnNameTBDFoldStateChangedSlate OnExpansionChanged;

	ENameTBDFoldAnimationMode AnimationMode = ENameTBDFoldAnimationMode::SlideAndFade;
	FNameTBDMotionSettings MotionSettings;
	bool bTargetExpanded = false;
	bool bShowChevron = true;

	float RawProgress = 0.0f;
	float CurrentAlpha = 0.0f;
};
