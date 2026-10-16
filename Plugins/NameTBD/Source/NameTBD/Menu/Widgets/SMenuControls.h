// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "NameTBD/Widgets/WidgetEffectImage.h"
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

/** The state a menu part is drawn in: picks its skin's image and is handed to a material. */
struct FMenuPartState
{
	/** 0..1, eased. */
	float Hover = 0.f;
	float Focus = 0.f;
	bool bPressed = false;
	bool bSelected = false;
	bool bDisabled = false;
	/** 0..1 (a slider, a switch's knob, a selector's option); -1: none. */
	float Value = -1.f;
};

/**
 * Draws style parts in their skin (FMenuPartSkin): the state's image, a material's live inputs, the tint. One per widget: it owns
 * the material instances and SVG drawings of the parts that widget draws, one per Key.
 */
class NAMETBD_API FMenuSkinPainter
{
public:
	/**
	 * Draws Part over Geometry - or over Size at Offset in it, turned by Angle (radians) round its middle. False when the part has
	 * no image (draw the built-in shape); true when it was drawn (or its SVG is still being made).
	 */
	bool Draw(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FMenuPartSkin& Part, const FMenuPartState& State,
		const FLinearColor& PaletteColor, const FMenuStylePalette& Palette, const FLinearColor& WidgetTint, const SWidget& Widget, uint64 Key,
		const FVector2f& Offset = FVector2f::ZeroVector, const FVector2f& Size = FVector2f::ZeroVector, float Angle = 0.f) const;

	void Reset() { Materials.Reset(); }

private:
	mutable FWidgetEffectMaterials Materials;
};

/** A part's built-in look where it is a plain rounded box. */
struct FMenuBuiltInBox
{
	FLinearColor Fill = FLinearColor::Transparent;
	FLinearColor Outline = FLinearColor::Transparent;
	float OutlineWidth = 0.f;
	float Radius = 0.f;
};

DECLARE_DELEGATE_RetVal_OneParam(const FMenuPartSkin*, FOnMenuGetPart, const FMenuStyle& /*Style*/);
DECLARE_DELEGATE_RetVal_TwoParams(FMenuBuiltInBox, FOnMenuGetBuiltInBox, const FMenuStyle& /*Style*/, const FMenuPartState& /*State*/);

/**
 * A box behind its content drawn as a style part: the part's skin when it has one, else its built-in rounded box (from the
 * palette). Key slots, the search box, the description panel, the footer, cards, headings, the tab bar. Reads the style at paint.
 */
class NAMETBD_API SMenuPartBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuPartBox)
		: _Padding(FMargin(0.f))
		, _IsSelected(false)
		, _IsFocused(false)
		, _Value(-1.f)
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ATTRIBUTE(FMargin, Padding)
		SLATE_ATTRIBUTE(bool, IsSelected)
		SLATE_ATTRIBUTE(bool, IsFocused)
		SLATE_ATTRIBUTE(float, Value)
		/** The part, from the style. */
		SLATE_EVENT(FOnMenuGetPart, Part)
		/** The built-in look (none: nothing drawn without a skin). */
		SLATE_EVENT(FOnMenuGetBuiltInBox, BuiltIn)
		SLATE_EVENT(FPointerEventHandler, OnMouseButtonDown)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	TAttribute<bool> IsSelectedAttribute;
	TAttribute<bool> IsFocusedAttribute;
	TAttribute<float> ValueAttribute;
	FOnMenuGetPart PartDelegate;
	FOnMenuGetBuiltInBox BuiltInDelegate;
	FPointerEventHandler MouseDownHandler;
	FMenuSkinPainter Painter;
};

DECLARE_DELEGATE(FOnMenuControlClicked);
DECLARE_DELEGATE_TwoParams(FOnMenuPartPointerState, bool /*bHovered*/, bool /*bPressed*/);

/**
 * Holds a part made in UMG (a custom tab, prompt, dialog...): tells it when it is hovered and pressed, clicks it when your widget
 * doesn't handle the click itself, and answers Back for dialogs and lists while focus is inside.
 */
class NAMETBD_API SMenuCustomPart : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuCustomPart)
		: _IsFocusable(false)
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(bool, IsFocusable)
		SLATE_EVENT(FOnMenuControlClicked, OnClicked)
		SLATE_EVENT(FOnMenuPartPointerState, OnPointerState)
		/** Back (or Escape) while focus is inside. Unbound: not handled. */
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return bFocusable; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void SendState();

	FOnMenuControlClicked ClickedDelegate;
	FOnMenuPartPointerState PointerStateDelegate;
	FSimpleDelegate BackDelegate;
	bool bFocusable = false;
	bool bPressed = false;
};

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
	ListItem,
	/** One of a Choice's options shown side by side (drawn like a tab; its own skin part). */
	Segment
};

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
	FMenuSkinPainter Painter;
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

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return StyleHolder->Style.SwitchSize; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Whether its row (or what holds it) has focus: drawn focused. */
	void SetFocusedAttribute(const TAttribute<bool>& InFocused) { FocusedAttribute = InFocused; }

private:
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);

	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	TAttribute<bool> IsOnAttribute;
	TAttribute<bool> FocusedAttribute;
	FOnMenuSwitchToggled ToggledDelegate;
	FWidgetMotionValue Knob;
	FMenuSkinPainter Painter;
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

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		const FMenuStyle& Style = StyleHolder->Style;
		return FVector2D(160.f, FMath::Max(24.f, FMath::Max(Style.SliderThumbSize + 4.f, Style.SliderTrackHeight)));
	}
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
	FMenuSkinPainter Painter;
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
	FMenuSkinPainter Painter;
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
		/** 0: the style's Dialog Width. */
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
	TSharedPtr<SWidget> Card;
	TArray<TSharedPtr<SMenuButton>> OptionWidgets;
	TSharedPtr<SScrollBox> ScrollBox;
	FOnMenuOptionPicked PickedDelegate;
	int32 CurrentIndex = 0;
	bool bPicked = false;
};
