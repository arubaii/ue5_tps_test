// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "NameTBD/Widgets/WidgetMotion.h"
#include "MenuStyle.h"

class SScrollBox;
class STextBlock;
class SVerticalBox;

/**
 * The style every part of one settings screen reads when it paints, so a live style change (the asset edited, High Contrast
 * turned on) reaches them all without rebuilding.
 */
struct NAMETBD_API FMenuStyleHolder
{
	FMenuStyle Style;
	const FMenuStylePalette& Palette() const { return Style.GetPalette(); }
};

using FMenuStyleRef = TSharedRef<FMenuStyleHolder>;

namespace MenuDraw
{
	/** A rounded box filled with Fill (and outlined) over Geometry - or over Size at Offset in it. */
	NAMETBD_API void RoundedBox(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FLinearColor& Fill, float Radius,
		const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f);
	NAMETBD_API void RoundedBoxAt(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FVector2f& Offset, const FVector2f& Size,
		const FLinearColor& Fill, float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f);

	/** A chevron pointing left (Direction -1), right (1) or down (0), centered at Center. */
	NAMETBD_API void Chevron(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FVector2f& Center, float HalfSize, int32 Direction,
		const FLinearColor& Color, float Thickness = 2.f);

	NAMETBD_API FVector2f MeasureText(const FText& Text, const FSlateFontInfo& Font);
	NAMETBD_API void Text(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FText& InText, const FSlateFontInfo& Font,
		const FVector2f& At, const FLinearColor& Color);

	/** Black or white, whichever reads on Background. */
	NAMETBD_API FLinearColor ReadableOn(const FLinearColor& Background);
}

/** How a menu button looks. */
enum class EMenuButtonKind : uint8
{
	/** Filled in the accent. */
	Primary,
	/** Outlined. */
	Secondary,
	/** Text only, highlighted when hovered. */
	Ghost,
	/** A page tab (the style's Tab Style). */
	Tab,
	/** An option in a list (fills the width, left aligned). */
	ListItem
};

DECLARE_DELEGATE(FOnMenuControlClicked);
DECLARE_DELEGATE_OneParam(FOnMenuControlHovered, bool /*bHovered*/);

/** A button drawn in the menu style: dialogs, footer, tabs, actions, list options. */
class NAMETBD_API SMenuButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuButton)
		: _Kind(EMenuButtonKind::Secondary)
		, _IsFocusable(true)
		, _IsSelected(false)
		, _MinWidth(0.f)
		, _IconOnly(false)
	{}
		SLATE_ARGUMENT(EMenuButtonKind, Kind)
		SLATE_ATTRIBUTE(FText, Text)
		SLATE_ARGUMENT(bool, IsFocusable)
		SLATE_ATTRIBUTE(bool, IsSelected)
		SLATE_ARGUMENT(float, MinWidth)
		SLATE_ARGUMENT(FMenuImage, Icon)
		SLATE_ARGUMENT(bool, IconOnly)
		SLATE_EVENT(FOnMenuControlClicked, OnClicked)
		SLATE_EVENT(FOnMenuControlHovered, OnHovered)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle);

	virtual bool SupportsKeyboardFocus() const override { return bFocusable; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FSlateColor GetTextColor() const;
	FSlateFontInfo GetFont() const;
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);
	void StartAnimating();

	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	EMenuButtonKind Kind = EMenuButtonKind::Secondary;
	TAttribute<bool> IsSelectedAttribute;
	FOnMenuControlClicked ClickedDelegate;
	FOnMenuControlHovered HoveredDelegate;
	FWidgetMotionValue Highlight;
	bool bFocusable = true;
	bool bPressed = false;
	bool bAnimating = false;
};

DECLARE_DELEGATE_OneParam(FOnMenuSwitchToggled, bool /*bOn*/);

/** An on / off switch, the knob gliding with the style's motion. */
class NAMETBD_API SMenuSwitch : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuSwitch) {}
		SLATE_ATTRIBUTE(bool, IsOn)
		SLATE_EVENT(FOnMenuSwitchToggled, OnToggled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(52.f, 28.f); }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);

	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	TAttribute<bool> IsOnAttribute;
	FOnMenuSwitchToggled ToggledDelegate;
	FWidgetMotionValue Knob;
	bool bLastOn = false;
};

DECLARE_DELEGATE_OneParam(FOnMenuSliderChanged, float /*Fraction*/);

/** A slider: a track, its fill to the value, a knob; drag or click anywhere on it. */
class NAMETBD_API SMenuSlider : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuSlider) {}
		SLATE_ATTRIBUTE(float, Fraction)
		SLATE_EVENT(FOnMenuSliderChanged, OnChanged)
		/** The drag ended (the value to commit). */
		SLATE_EVENT(FOnMenuSliderChanged, OnCommitted)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D(160.f, 24.f); }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	float FractionAt(const FGeometry& MyGeometry, const FVector2D& ScreenPosition) const;

	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	TAttribute<float> FractionAttribute;
	FOnMenuSliderChanged ChangedDelegate;
	FOnMenuSliderChanged CommittedDelegate;
	bool bDragging = false;
	float DragFraction = 0.f;
};

DECLARE_DELEGATE_OneParam(FOnMenuSelectorStep, int32 /*Direction*/);

/** "<  High  >" with pips: arrows step, the middle opens a list (dropdown mode). */
class NAMETBD_API SMenuSelector : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuSelector)
		: _IsDropdown(false)
	{}
		SLATE_ATTRIBUTE(FText, ValueText)
		SLATE_ATTRIBUTE(int32, OptionCount)
		SLATE_ATTRIBUTE(int32, OptionIndex)
		SLATE_ATTRIBUTE(bool, CanStepLeft)
		SLATE_ATTRIBUTE(bool, CanStepRight)
		SLATE_ARGUMENT(bool, IsDropdown)
		SLATE_EVENT(FOnMenuSelectorStep, OnStep)
		SLATE_EVENT(FOnMenuControlClicked, OnOpen)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** A short pulse on the arrow of the last step (feedback for a pad press). */
	void Pulse(int32 Direction);

private:
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);

	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	TAttribute<FText> ValueTextAttribute;
	TAttribute<int32> OptionCountAttribute;
	TAttribute<int32> OptionIndexAttribute;
	TAttribute<bool> CanStepLeftAttribute;
	TAttribute<bool> CanStepRightAttribute;
	FOnMenuSelectorStep StepDelegate;
	FOnMenuControlClicked OpenDelegate;
	bool bDropdown = false;
	int32 PulseDirection = 0;
	float PulseTime = 0.f;
};

/** One button of a dialog. */
struct FMenuDialogButton
{
	FText Text;
	EMenuButtonKind Kind = EMenuButtonKind::Secondary;
};

DECLARE_DELEGATE_OneParam(FOnMenuDialogChoice, int32 /*ButtonIndex*/);

/** A modal dialog over a scrim: a title, a body, buttons. Back picks the Cancel button. Focus goes to the Default button. */
class NAMETBD_API SMenuDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuDialog)
		: _DefaultButton(0)
		, _CancelButton(INDEX_NONE)
		, _Width(520.f)
	{}
		SLATE_ATTRIBUTE(FText, Title)
		SLATE_ATTRIBUTE(FText, Body)
		SLATE_ARGUMENT(TArray<FMenuDialogButton>, Buttons)
		SLATE_ARGUMENT(int32, DefaultButton)
		SLATE_ARGUMENT(int32, CancelButton)
		SLATE_ARGUMENT(float, Width)
		/** More content between the body and the buttons (an option list). */
		SLATE_NAMED_SLOT(FArguments, Extra)
		SLATE_EVENT(FOnMenuDialogChoice, OnChoice)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle);

	/** Puts keyboard focus on the default button for User. */
	void FocusDefault(int32 UserIndex);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { return FReply::Handled(); }
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;

private:
	void Choose(int32 Index);

	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	TUniquePtr<FSlateBrush> CardBrush;
	TArray<TSharedPtr<SMenuButton>> ButtonWidgets;
	FOnMenuDialogChoice ChoiceDelegate;
	int32 DefaultIndex = 0;
	int32 CancelIndex = INDEX_NONE;
	bool bChosen = false;
};

DECLARE_DELEGATE_OneParam(FOnMenuOptionPicked, int32 /*OptionIndex*/);

/** A modal list of options (a dropdown): Up / Down, Accept picks, Back cancels (-1). */
class NAMETBD_API SMenuOptionList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuOptionList)
		: _Current(0)
	{}
		SLATE_ARGUMENT(FText, Title)
		SLATE_ARGUMENT(TArray<FText>, Options)
		SLATE_ARGUMENT(int32, Current)
		SLATE_EVENT(FOnMenuOptionPicked, OnPicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle);
	void FocusCurrent(int32 UserIndex);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	TUniquePtr<FSlateBrush> CardBrush;
	TSharedPtr<SWidget> Card;
	TArray<TSharedPtr<SMenuButton>> OptionWidgets;
	TSharedPtr<SScrollBox> ScrollBox;
	FOnMenuOptionPicked PickedDelegate;
	int32 CurrentIndex = 0;
	bool bPicked = false;
};
