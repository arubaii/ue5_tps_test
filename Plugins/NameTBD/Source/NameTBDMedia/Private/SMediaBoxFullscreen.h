// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Children.h"
#include "Layout/SlateRect.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SPanel.h"

#include "MediaBoxTypes.h"

class UMediaBox;

/**
 * Where a Media Box sits in its layout: holds what the box shows (the video, and a With Controls' bar) and lends it to the
 * full-screen layer while the box is full screen - an empty box of the same size keeps its place meanwhile.
 */
class SMediaBoxFullscreenSlot : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMediaBoxFullscreenSlot) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Gives the content away (for the full-screen layer); a box of the size it had keeps the place. */
	TSharedPtr<SWidget> LendContent();

	/** Takes the content back. */
	void ReturnContent(const TSharedRef<SWidget>& Returned);

	/** Its place on the desktop, as drawn. */
	FSlateRect GetDesktopRect() const;

private:
	TSharedPtr<SWidget> HeldContent;
};

/**
 * A full-screen Media Box: over the whole game viewport, a backdrop with the box's content on it, faded or grown in from where the
 * box was (Project Settings > Media Box > Fullscreen Transition), and out again. Takes the clicks over it; Escape leaves it.
 */
class SMediaBoxFullscreenLayer : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SMediaBoxFullscreenLayer)
		: _Transition(EMediaBoxFullscreenTransition::Zoom)
		, _TransitionTime(0.25f)
		, _Backdrop(FLinearColor::Black)
		, _bEscapeLeaves(true)
	{
		_Visibility = EVisibility::Visible;
	}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(TWeakObjectPtr<UMediaBox>, OwnerBox)
		SLATE_ARGUMENT(FSlateRect, From)
		SLATE_ARGUMENT(EMediaBoxFullscreenTransition, Transition)
		SLATE_ARGUMENT(float, TransitionTime)
		SLATE_ARGUMENT(FLinearColor, Backdrop)
		SLATE_ARGUMENT(bool, bEscapeLeaves)
	SLATE_END_ARGS()

	SMediaBoxFullscreenLayer();

	void Construct(const FArguments& InArgs);

	/** Goes back out to To (desktop space); Done is called once it is out (on a later tick, never inside Slate's). bInstant: at once. */
	void BeginExit(const FSlateRect& To, bool bInstant, TFunction<void()> Done);

	/** Comes back in (full screen asked for again while going out). */
	void BeginEnter();

	/** Gives the content back (as the layer is removed). */
	TSharedPtr<SWidget> TakeContent();

	//~ SWidget
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FChildren* GetChildren() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	/** 0 where the box was, 1 full screen - eased. */
	float GetEased() const;

	TSlotlessChildren<SWidget> LayerChildren;
	TSharedPtr<SWidget> LayerContent;
	TWeakObjectPtr<UMediaBox> WeakOwnerBox;

	/** Where it grows from and shrinks back to, in desktop space. */
	FSlateRect FromRect;

	EMediaBoxFullscreenTransition Transition = EMediaBoxFullscreenTransition::Zoom;
	float TransitionTime = 0.25f;
	FLinearColor Backdrop = FLinearColor::Black;
	bool bEscapeLeaves = true;

	/** 0 - 1, toward Goal. */
	float Progress = 0.f;
	float Goal = 1.f;
	TFunction<void()> OnExited;
	bool bExitReported = false;
};
