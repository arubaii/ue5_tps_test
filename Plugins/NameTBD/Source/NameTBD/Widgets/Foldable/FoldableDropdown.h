// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/NamedSlotInterface.h"
#include "Components/Widget.h"
#include "Layout/Children.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SPanel.h"
#include "NameTBD/Widgets/WidgetMotion.h"
#include "NameTBD/Widgets/WidgetSVG.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"

#include "FoldableDropdown.generated.h"

class SBox;
class UWidgetSVG;

/** How a Foldable Dropdown's body comes in and goes away. Every style grows the dropdown's height with it, so what is below moves smoothly. */
UENUM(BlueprintType)
enum class EFoldableAnimation : uint8
{
	FadeSlide UMETA(DisplayName = "Fade & Slide", ToolTip = "The body fades in as it drifts down by Slide Distance into place - the modern accordion."),
	Reveal UMETA(ToolTip = "The body stays still and is uncovered from the top as the dropdown grows, like a blind."),
	Slide UMETA(ToolTip = "The body slides down out from under the header as the dropdown grows, its bottom edge leading."),
	Fade UMETA(ToolTip = "The body fades in where it is as the dropdown grows."),
	Pop UMETA(ToolTip = "The body fades and scales up from just under full size, as a menu or popover opens.")
};

/** The header's open / closed marker. */
UENUM(BlueprintType)
enum class EFoldableIcon : uint8
{
	Chevron UMETA(ToolTip = "A chevron pointing down, turning to point up when open."),
	SideChevron UMETA(DisplayName = "Side Chevron", ToolTip = "A chevron pointing to the side, turning down when open - a tree or settings list."),
	PlusMinus UMETA(DisplayName = "Plus / Minus", ToolTip = "A plus whose upright folds away into a minus when open."),
	Caret UMETA(ToolTip = "A small filled triangle pointing to the side, turning down when open."),
	Image UMETA(ToolTip = "Your own icon - a texture, a material or an SVG - turned by Icon Open Angle when open."),
	None UMETA(ToolTip = "No marker.")
};

/** Which side of the header the marker sits on. */
UENUM(BlueprintType)
enum class EFoldableIconSide : uint8
{
	Right,
	Left
};

/** Everything a Foldable Dropdown's header draws: its background in each state, and the marker. Plain data for Slate. */
struct FFoldableHeaderLook
{
	FMargin Padding = FMargin(12.f, 10.f);
	FLinearColor Color = FLinearColor::Transparent;
	FLinearColor HoverColor = FLinearColor(1.f, 1.f, 1.f, 0.06f);
	FLinearColor PressedColor = FLinearColor(1.f, 1.f, 1.f, 0.1f);
	float CornerRadius = 8.f;
	EFoldableIcon Icon = EFoldableIcon::Chevron;
	EFoldableIconSide IconSide = EFoldableIconSide::Right;
	float IconSize = 12.f;
	float IconThickness = 1.75f;
	float IconSpacing = 10.f;
	FLinearColor IconColor = FLinearColor(1.f, 1.f, 1.f, 0.8f);
	float IconOpenAngle = 180.f;
	EWidgetImageSource IconImageSource = EWidgetImageSource::Texture;
	FSlateBrush IconImage;
	TWeakObjectPtr<UWidgetSVG> IconSVG;
	bool bFocusable = true;
};

/** A Foldable Dropdown's header: clickable (pointer, Enter / Space, a gamepad's accept), lit on hover, its marker turning as it opens. */
class NAMETBD_API SFoldableHeader : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFoldableHeader) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetContent(const TSharedRef<SWidget>& InContent);
	void SetLook(const FFoldableHeaderLook& InLook);
	void SetOpenAmount(float InAmount);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return HeaderLook.bFocusable; }
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;

private:
	void PaintIcon(const FGeometry& AllottedGeometry, const FBox2f& IconBox, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const;
	float GetIconReserve() const;
	void Lit();
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);

	FSimpleDelegate OnClicked;
	FFoldableHeaderLook HeaderLook;
	float OpenAmount = 0.f;
	bool bPressed = false;
	FWidgetMotionValue HoverAmount;
	FWidgetMotionValue PressAmount;
	TSharedPtr<FActiveTimerHandle> AnimateTimer;
	mutable FWidgetSVGBrush IconSVGBrush;
};

/** A Foldable Dropdown's body: clipped to the height it is given, its content placed, scaled and faded by the open amount. */
class NAMETBD_API SFoldableBody : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFoldableBody) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetContent(const TSharedRef<SWidget>& InContent);
	void SetBodyPadding(const FMargin& InPadding);
	void SetAnimation(EFoldableAnimation InAnimation, float InSlideDistance);
	void SetOpenAmount(float InAmount);

	/** The content's full height, padding included. */
	float GetFullHeight() const;

	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	float GetOpacity() const;

	TSharedPtr<SBox> PaddingBox;
	EFoldableAnimation Animation = EFoldableAnimation::FadeSlide;
	float SlideDistance = 12.f;
	float OpenAmount = 0.f;
};

DECLARE_DELEGATE_OneParam(FOnFoldableFinished, bool /*bExpanded*/);

/**
 * The Slate side of a Foldable Dropdown: the header over the body, the body's height following the open amount so the
 * dropdown (and whatever is laid out after it) grows and shrinks smoothly. A body fully closed is collapsed: no cost.
 */
class NAMETBD_API SFoldableDropdown : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SFoldableDropdown) {}
		SLATE_EVENT(FSimpleDelegate, OnHeaderClicked)
		SLATE_EVENT(FOnFoldableFinished, OnFinished)
	SLATE_END_ARGS()

	SFoldableDropdown();

	void Construct(const FArguments& InArgs);

	void SetHeaderContent(const TSharedRef<SWidget>& InContent);
	void SetBodyContent(const TSharedRef<SWidget>& InContent);
	void SetHeaderLook(const FFoldableHeaderLook& InLook);
	void SetBodyLook(const FMargin& InPadding, EFoldableAnimation InAnimation, float InSlideDistance, bool bInDivider, const FLinearColor& InDividerColor, float InDividerThickness);
	void SetMotion(EWidgetMotion InMotion, float InEnterDuration, float InExitDuration);

	void SetExpanded(bool bExpand, bool bAnimate);
	bool IsExpanded() const { return Expansion.Target > 0.5f; }
	float GetOpenAmount() const { return Expansion.Value; }

	virtual FChildren* GetChildren() override { return &Children; }
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	void ApplyAmount();
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);
	float GetBodyHeight() const;

	TSlotlessChildren<SWidget> Children;
	TSharedPtr<SFoldableHeader> Header;
	TSharedPtr<SFoldableBody> Body;
	FOnFoldableFinished OnFinished;

	EWidgetMotion Motion = EWidgetMotion::Smooth;
	float EnterDuration = 0.f;
	float ExitDuration = 0.f;
	bool bDivider = false;
	FLinearColor DividerColor = FLinearColor(1.f, 1.f, 1.f, 0.08f);
	float DividerThickness = 1.f;
	FWidgetMotionValue Expansion;
	TSharedPtr<FActiveTimerHandle> AnimateTimer;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFoldableExpansionChanged, bool, bExpanded);

/**
 * Foldable Dropdown: a header that opens and closes the body under it - an accordion, an expander, a collapsible settings
 * group, a FAQ. Click the header (or press Enter, Space or a gamepad's accept on it) and the body folds out with the chosen
 * animation while the dropdown grows, so whatever is laid out below glides down with it. Header and Body are named slots:
 * drop any widgets in. Dropdowns sharing an Accordion Group in the same panel close each other.
 */
UCLASS(meta = (DisplayName = "Foldable Dropdown"))
class NAMETBD_API UFoldableDropdown : public UWidget, public INamedSlotInterface
{
	GENERATED_BODY()

public:
	UFoldableDropdown(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Open (the body shown). Also what the designer shows - selecting a widget inside the body opens it there. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foldable", meta = (DisplayName = "Expanded"))
	bool bIsExpanded = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foldable")
	EFoldableAnimation Animation = EFoldableAnimation::FadeSlide;

	/** How it opens and closes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foldable")
	EWidgetMotion Motion = EWidgetMotion::Smooth;

	/** Seconds to open. 0: Motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foldable", meta = (EditCondition = "Motion != EWidgetMotion::Instant", EditConditionHides, ClampMin = "0", UIMax = "2", Units = "s"))
	float EnterDuration = 0.3f;

	/** Seconds to close. 0: Motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foldable", meta = (EditCondition = "Motion != EWidgetMotion::Instant", EditConditionHides, ClampMin = "0", UIMax = "2", Units = "s"))
	float ExitDuration = 0.22f;

	/** Fade & Slide: how far the body drifts down into place, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foldable", meta = (EditCondition = "Animation == EFoldableAnimation::FadeSlide", EditConditionHides, ClampMin = "0", UIMax = "48", ForceUnits = "px"))
	float SlideDistance = 12.f;

	/** Clicking the header opens and closes it. Off: only Set Expanded / Toggle Expanded do. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foldable")
	bool bToggleOnHeaderClick = true;

	/** Dropdowns with the same group, in the same panel, close when another of them opens. None: no group. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foldable")
	FName AccordionGroup;

	/** The header takes keyboard / gamepad focus (and opens with Enter, Space or accept). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Foldable")
	bool bFocusable = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Header")
	FMargin HeaderPadding = FMargin(12.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Header")
	FLinearColor HeaderColor = FLinearColor::Transparent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Header")
	FLinearColor HeaderHoverColor = FLinearColor(1.f, 1.f, 1.f, 0.06f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Header")
	FLinearColor HeaderPressedColor = FLinearColor(1.f, 1.f, 1.f, 0.1f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Header", meta = (ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float HeaderCornerRadius = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icon")
	EFoldableIcon Icon = EFoldableIcon::Chevron;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icon", meta = (EditCondition = "Icon != EFoldableIcon::None", EditConditionHides))
	EFoldableIconSide IconSide = EFoldableIconSide::Right;

	/** The marker's size, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icon", meta = (EditCondition = "Icon != EFoldableIcon::None", EditConditionHides, ClampMin = "4", UIMax = "48", ForceUnits = "px"))
	float IconSize = 12.f;

	/** The chevron's or plus's line width, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icon", meta = (EditCondition = "Icon == EFoldableIcon::Chevron || Icon == EFoldableIcon::SideChevron || Icon == EFoldableIcon::PlusMinus", EditConditionHides, ClampMin = "0.5", UIMax = "6", ForceUnits = "px"))
	float IconThickness = 1.75f;

	/** Space between the marker and the header's content. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icon", meta = (EditCondition = "Icon != EFoldableIcon::None", EditConditionHides, ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float IconSpacing = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icon", meta = (EditCondition = "Icon != EFoldableIcon::None", EditConditionHides))
	FLinearColor IconColor = FLinearColor(1.f, 1.f, 1.f, 0.8f);

	/** Image: where the icon comes from - a texture or material (the brush), or an SVG drawn sharp at its size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icon", meta = (EditCondition = "Icon == EFoldableIcon::Image", EditConditionHides))
	EWidgetImageSource IconImageSource = EWidgetImageSource::Texture;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icon", meta = (EditCondition = "Icon == EFoldableIcon::Image && IconImageSource == EWidgetImageSource::Texture", EditConditionHides))
	FSlateBrush IconImage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icon", meta = (DisplayName = "Icon SVG", EditCondition = "Icon == EFoldableIcon::Image && IconImageSource == EWidgetImageSource::SVG", EditConditionHides))
	TObjectPtr<UWidgetSVG> IconSVG = nullptr;

	/** Image: how far the icon turns when open, in degrees (clockwise). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Icon", meta = (EditCondition = "Icon == EFoldableIcon::Image", EditConditionHides, UIMin = "-360", UIMax = "360", ForceUnits = "Degrees"))
	float IconOpenAngle = 180.f;

	/** Space round the body's content. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	FMargin BodyPadding = FMargin(12.f, 4.f, 12.f, 12.f);

	/** A line between the header and the open body, fading in as it opens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body")
	bool bShowDivider = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (EditCondition = "bShowDivider", EditConditionHides))
	FLinearColor DividerColor = FLinearColor(1.f, 1.f, 1.f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Body", meta = (EditCondition = "bShowDivider", EditConditionHides, ClampMin = "0.5", UIMax = "4", ForceUnits = "px"))
	float DividerThickness = 1.f;

	/** It started opening (true) or closing (false). */
	UPROPERTY(BlueprintAssignable, Category = "Foldable")
	FOnFoldableExpansionChanged OnExpansionChanged;

	/** It finished opening (true) or closing (false). */
	UPROPERTY(BlueprintAssignable, Category = "Foldable")
	FOnFoldableExpansionChanged OnFoldFinished;

	/** Opens or closes it - animated, or at once. */
	UFUNCTION(BlueprintCallable, Category = "Foldable Dropdown")
	void SetExpanded(bool bExpand, bool bAnimate = true);

	UFUNCTION(BlueprintCallable, Category = "Foldable Dropdown")
	void ToggleExpanded(bool bAnimate = true);

	/** Open, or opening. */
	UFUNCTION(BlueprintPure, Category = "Foldable Dropdown")
	bool IsExpanded() const { return bIsExpanded; }

	/** How far open it is now: 0 closed, 1 open (past 1 while a Bouncy motion overshoots). */
	UFUNCTION(BlueprintPure, Category = "Foldable Dropdown")
	float GetExpansionAmount() const;

	/** The widget in the Header slot. */
	UFUNCTION(BlueprintPure, Category = "Foldable Dropdown")
	UWidget* GetHeaderContent() const { return HeaderContent; }

	/** The widget in the Body slot. */
	UFUNCTION(BlueprintPure, Category = "Foldable Dropdown")
	UWidget* GetBodyContent() const { return BodyContent; }

	// INamedSlotInterface
	virtual void GetSlotNames(TArray<FName>& SlotNames) const override;
	virtual UWidget* GetContentForSlot(FName SlotName) const override;
	virtual void SetContentForSlot(FName SlotName, UWidget* Content) override;

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void OnDescendantSelectedByDesigner(UWidget* DescendantWidget) override;
	virtual void OnDescendantDeselectedByDesigner(UWidget* DescendantWidget) override;
#endif

protected:
	UPROPERTY()
	TObjectPtr<UWidget> HeaderContent;

	UPROPERTY()
	TObjectPtr<UWidget> BodyContent;

	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void HandleHeaderClicked();
	void HandleFinished(bool bExpanded);
	void CloseGroupSiblings();
	FFoldableHeaderLook MakeHeaderLook() const;

	TSharedPtr<SFoldableDropdown> MyDropdown;

#if WITH_EDITORONLY_DATA
	/** Opened in the designer while a widget inside the body is selected. */
	bool bDesignerOpened = false;
#endif
};
