// Copyright AR GameSystems 2026. All rights reserved.

#include "SMenuControls.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "NameTBD/Menu/Settings/MenuSettingTargets.h"
#include "SMenuImage.h"

// =====================================================================================================
//  Drawing
// =====================================================================================================

namespace MenuDraw
{
	/** A fill as MakeBox's tint: a clear one made barely there, since a fully clear tint is culled with the outline on it. */
	static FLinearColor FillTint(const FLinearColor& Fill)
	{
		return Fill.A > 0.f ? Fill : FLinearColor(Fill.R, Fill.G, Fill.B, 0.0001f);
	}

	void RoundedBox(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FLinearColor& Fill, float Radius,
		const FLinearColor& Outline, float OutlineWidth)
	{
		if (Fill.A <= 0.f && (Outline.A <= 0.f || OutlineWidth <= 0.f))
		{
			return;
		}
		// MakeBox draws in its tint, not the brush's own tint: the fill goes in as the tint over a white brush. The outline keeps its
		// own colour (the brush's outline settings).
		const FSlateRoundedBoxBrush Brush(FLinearColor::White, Radius, Outline, OutlineWidth);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), &Brush, ESlateDrawEffect::None, FillTint(Fill));
	}

	void RoundedBoxAt(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FVector2f& Offset, const FVector2f& Size,
		const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth)
	{
		if (Size.X <= 0.f || Size.Y <= 0.f || (Fill.A <= 0.f && (Outline.A <= 0.f || OutlineWidth <= 0.f)))
		{
			return;
		}
		const FSlateRoundedBoxBrush Brush(FLinearColor::White, FMath::Min(Radius, FMath::Min(Size.X, Size.Y) * 0.5f), Outline, OutlineWidth);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Offset)), &Brush, ESlateDrawEffect::None, FillTint(Fill));
	}

	void Chevron(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FVector2f& Center, float HalfSize, int32 Direction,
		const FLinearColor& Color, float Thickness)
	{
		TArray<FVector2f> Points;
		if (Direction == 0)
		{
			Points = { Center + FVector2f(-HalfSize, -HalfSize * 0.5f), Center + FVector2f(0.f, HalfSize * 0.5f), Center + FVector2f(HalfSize, -HalfSize * 0.5f) };
		}
		else
		{
			const float Side = static_cast<float>(Direction);
			Points = { Center + FVector2f(-HalfSize * 0.5f * Side, -HalfSize), Center + FVector2f(HalfSize * 0.5f * Side, 0.f), Center + FVector2f(-HalfSize * 0.5f * Side, HalfSize) };
		}
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
	}

	FVector2f MeasureText(const FText& InText, const FSlateFontInfo& Font)
	{
		return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(InText, Font));
	}

	void Text(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FText& InText, const FSlateFontInfo& Font,
		const FVector2f& At, const FLinearColor& Color)
	{
		const FVector2f Size = MeasureText(InText, Font);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(At)), InText, Font, ESlateDrawEffect::None, Color);
	}

	FLinearColor ReadableOn(const FLinearColor& Background)
	{
		return Background.GetLuminance() > 0.5f ? FLinearColor(0.03f, 0.03f, 0.04f, 1.f) : FLinearColor::White;
	}
}

namespace MenuControlsLocal
{
	bool IsBackKey(const FKeyEvent& InKeyEvent)
	{
		return FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Back || InKeyEvent.GetKey() == EKeys::Escape;
	}

	bool IsAcceptKey(const FKeyEvent& InKeyEvent)
	{
		return FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept;
	}

	EWidgetMotion MotionFor(const FMenuStyle& Style)
	{
		return MenuAccessibility::IsReduceMotionEnabled() ? EWidgetMotion::Instant : Style.Motion;
	}

	/** The scrim behind a dialog or list: its skin, else the palette's Scrim. */
	TSharedRef<SWidget> MakeScrim(const FMenuStyleRef& Holder)
	{
		return SNew(SMenuPartBox, Holder)
			.Part_Lambda([](const FMenuStyle& Style) { return &Style.Skin.Scrim; })
			.BuiltIn_Lambda([](const FMenuStyle& Style, const FMenuPartState&)
			{
				FMenuBuiltInBox Box;
				Box.Fill = Style.GetPalette().Scrim;
				return Box;
			});
	}

	/** A dialog's or list's card: its skin, else a rounded Surface box outlined in Divider. */
	FMenuBuiltInBox CardBox(const FMenuStyle& Style, const FMenuPartState&)
	{
		FMenuBuiltInBox Box;
		Box.Fill = Style.GetPalette().Surface;
		Box.Outline = Style.GetPalette().Divider;
		Box.OutlineWidth = Style.OutlineWidth;
		Box.Radius = Style.CornerRadius * 1.5f;
		return Box;
	}
}

// =====================================================================================================
//  Skin
// =====================================================================================================

bool FMenuSkinPainter::Draw(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FMenuPartSkin& Part, const FMenuPartState& State,
	const FLinearColor& PaletteColor, const FMenuStylePalette& Palette, const FLinearColor& WidgetTint, const SWidget& Widget, uint64 Key,
	const FVector2f& Offset, const FVector2f& Size, float Angle) const
{
	if (!Part.IsSet())
	{
		return false;
	}
	const FWidgetEffectImage& Image = Part.ForState(State.bDisabled, State.bPressed, State.bSelected, State.Focus > 0.5f, State.Hover > 0.5f);
	if (!Image.IsSet())
	{
		// Another state has an image, this one none: nothing for it.
		return true;
	}

	FVector2f PartSize = (Size.X > 0.f && Size.Y > 0.f) ? Size : FVector2f(Geometry.GetLocalSize());
	const FVector2f PartOffset = Offset - FVector2f(Part.Outset.Left, Part.Outset.Top);
	PartSize += FVector2f(Part.Outset.Left + Part.Outset.Right, Part.Outset.Top + Part.Outset.Bottom);
	if (PartSize.X <= 0.f || PartSize.Y <= 0.f)
	{
		return true;
	}
	const FGeometry PartGeometry = Angle != 0.f
		? Geometry.MakeChild(PartSize, FSlateLayoutTransform(PartOffset), FSlateRenderTransform(FQuat2f(Angle)), FVector2f(0.5f, 0.5f))
		: Geometry.MakeChild(PartSize, FSlateLayoutTransform(PartOffset));

	FWidgetEffectMaterialInputs Inputs;
	const bool bMaterial = Image.GetMaterial() != nullptr;
	if (bMaterial)
	{
		Inputs.Add(TEXT("Hover"), State.Hover);
		Inputs.Add(TEXT("Focus"), State.Focus);
		Inputs.Add(TEXT("Pressed"), State.bPressed ? 1.f : 0.f);
		Inputs.Add(TEXT("Selected"), State.bSelected ? 1.f : 0.f);
		Inputs.Add(TEXT("Disabled"), State.bDisabled ? 1.f : 0.f);
		Inputs.Add(TEXT("Value"), State.Value);
		Inputs.Add(TEXT("Time"), static_cast<float>(FMath::Fmod(FPlatformTime::Seconds(), 3600.0)));
		Inputs.Add(TEXT("Width"), PartSize.X);
		Inputs.Add(TEXT("Height"), PartSize.Y);
		Inputs.Add(TEXT("PaletteColor"), PaletteColor);
		Inputs.Add(TEXT("Accent"), Palette.Accent);
	}
	const FSlateBrush* Brush = Materials.GetBrush(Key, Image, bMaterial ? &Inputs : nullptr, PartGeometry, PartSize, Widget);
	if (!Brush)
	{
		return true;
	}
	FLinearColor Color = Part.Tint * WidgetTint;
	if (Part.bTintWithPalette)
	{
		Color *= PaletteColor;
	}
	if (Image.ImageSource == EWidgetImageSource::Texture)
	{
		// MakeBox draws in its tint only: the brush's own tint goes in here.
		Color *= Image.Image.TintColor.GetSpecifiedColor();
	}
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PartGeometry.ToPaintGeometry(), Brush, ESlateDrawEffect::None, Color);
	return true;
}

void SMenuPartBox::Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle)
{
	StyleHolder = InStyle;
	IsSelectedAttribute = InArgs._IsSelected;
	IsFocusedAttribute = InArgs._IsFocused;
	ValueAttribute = InArgs._Value;
	PartDelegate = InArgs._Part;
	BuiltInDelegate = InArgs._BuiltIn;
	MouseDownHandler = InArgs._OnMouseButtonDown;
	ChildSlot
	.Padding(InArgs._Padding)
	[
		InArgs._Content.Widget
	];
}

FReply SMenuPartBox::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseDownHandler.IsBound())
	{
		return MouseDownHandler.Execute(MyGeometry, MouseEvent);
	}
	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

int32 SMenuPartBox::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FMenuStyle& Style = StyleHolder->Style;
	const FMenuStylePalette& Palette = StyleHolder->Palette();
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();

	FMenuPartState State;
	State.Hover = IsHovered() ? 1.f : 0.f;
	State.Focus = IsFocusedAttribute.Get(false) ? 1.f : 0.f;
	State.bSelected = IsSelectedAttribute.Get(false);
	State.bDisabled = !ShouldBeEnabled(bParentEnabled);
	State.Value = ValueAttribute.Get(-1.f);

	const FMenuBuiltInBox Box = BuiltInDelegate.IsBound() ? BuiltInDelegate.Execute(Style, State) : FMenuBuiltInBox();
	const FMenuPartSkin* Part = PartDelegate.IsBound() ? PartDelegate.Execute(Style) : nullptr;
	if (!Part || Part->DrawsBuiltIn())
	{
		MenuDraw::RoundedBox(OutDrawElements, LayerId, AllottedGeometry, Box.Fill * Tint, Box.Radius, Box.Outline * Tint, Box.OutlineWidth);
	}
	if (Part)
	{
		Painter.Draw(OutDrawElements, LayerId + 1, AllottedGeometry, *Part, State, Box.Fill.A > 0.f ? Box.Fill : Palette.Surface, Palette, Tint, *this, 0);
	}
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}

// =====================================================================================================
//  Custom part
// =====================================================================================================

void SMenuCustomPart::Construct(const FArguments& InArgs)
{
	bFocusable = InArgs._IsFocusable;
	ClickedDelegate = InArgs._OnClicked;
	PointerStateDelegate = InArgs._OnPointerState;
	BackDelegate = InArgs._OnBack;
	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SMenuCustomPart::SendState()
{
	PointerStateDelegate.ExecuteIfBound(IsHovered(), bPressed);
}

FReply SMenuCustomPart::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && IsEnabled() && ClickedDelegate.IsBound())
	{
		bPressed = true;
		SendState();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SMenuCustomPart::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPressed)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	SendState();
	if (MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
	{
		ClickedDelegate.ExecuteIfBound();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

void SMenuCustomPart::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	SendState();
}

void SMenuCustomPart::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	bPressed = false;
	SendState();
}

FReply SMenuCustomPart::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (BackDelegate.IsBound() && MenuControlsLocal::IsBackKey(InKeyEvent))
	{
		BackDelegate.Execute();
		return FReply::Handled();
	}
	if (bFocusable && ClickedDelegate.IsBound() && MenuControlsLocal::IsAcceptKey(InKeyEvent) && HasKeyboardFocus())
	{
		ClickedDelegate.Execute();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

// =====================================================================================================
//  Button
// =====================================================================================================

void SMenuButton::Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle)
{
	StyleHolder = InStyle;
	Kind = InArgs._Kind;
	bFocusable = InArgs._IsFocusable;
	IsSelectedAttribute = InArgs._IsSelected;
	ClickedDelegate = InArgs._OnClicked;
	HoveredDelegate = InArgs._OnHovered;
	Highlight.Snap(0.f);

	const FMenuStyle& ConstructStyle = StyleHolder->Style;
	FMargin ContentPadding = ConstructStyle.ButtonPadding;
	switch (Kind)
	{
	case EMenuButtonKind::Tab: ContentPadding = ConstructStyle.TabPadding; break;
	case EMenuButtonKind::Segment: ContentPadding = FMargin(10.f, 6.f); break;
	case EMenuButtonKind::ListItem: ContentPadding = FMargin(16.f, 11.f); break;
	case EMenuButtonKind::Ghost: ContentPadding = FMargin(10.f, 6.f); break;
	default: break;
	}
	const bool bUppercase = Kind == EMenuButtonKind::Tab ? ConstructStyle.bUppercaseTabs
		: (Kind == EMenuButtonKind::Primary || Kind == EMenuButtonKind::Secondary || Kind == EMenuButtonKind::Ghost) ? ConstructStyle.bUppercaseButtons : false;
	const TAttribute<FText> TextAttribute = InArgs._Text;

	const bool bHasIcon = InArgs._Icon.HasImage();
	const bool bIconOnly = bHasIcon && InArgs._IconOnly;
	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);
	if (bHasIcon)
	{
		Content->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, bIconOnly ? 0.f : 8.f, 0.f)
			[
				SNew(SMenuImage)
				.Image(InArgs._Icon)
				.ImageSize(FVector2D(20.f, 20.f))
				.Tint(this, &SMenuButton::GetTextColor)
			];
	}
	if (!bIconOnly)
	{
		Content->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([TextAttribute, bUppercase]() { return bUppercase ? TextAttribute.Get(FText::GetEmpty()).ToUpper() : TextAttribute.Get(FText::GetEmpty()); })
				.Font(this, &SMenuButton::GetFont)
				.ColorAndOpacity(this, &SMenuButton::GetTextColor)
			];
	}

	const FOptionalSize MinWidth = InArgs._MinWidth > 0.f ? FOptionalSize(InArgs._MinWidth) : FOptionalSize();
	ChildSlot
	.HAlign(Kind == EMenuButtonKind::ListItem ? HAlign_Fill : HAlign_Center)
	[
		SNew(SBox)
		.MinDesiredWidth(MinWidth)
		.HAlign(Kind == EMenuButtonKind::ListItem ? HAlign_Left : HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(ContentPadding)
		[
			Content
		]
	];
}

FSlateFontInfo SMenuButton::GetFont() const
{
	const FMenuStyle& Style = StyleHolder->Style;
	switch (Kind)
	{
	case EMenuButtonKind::Tab:
	case EMenuButtonKind::Segment: return Style.TabFont;
	case EMenuButtonKind::ListItem: return Style.LabelFont;
	default: return Style.ButtonFont;
	}
}

FSlateColor SMenuButton::GetTextColor() const
{
	const FMenuStylePalette& Palette = StyleHolder->Palette();
	if (!IsEnabled())
	{
		return Palette.DisabledText;
	}
	const bool bSelected = IsSelectedAttribute.Get(false);
	switch (Kind)
	{
	case EMenuButtonKind::Primary:
		return MenuDraw::ReadableOn(Palette.Accent);
	case EMenuButtonKind::Tab:
	case EMenuButtonKind::Segment:
		if (bSelected && StyleHolder->Style.TabStyle != EMenuTabStyle::Underline)
		{
			return Palette.AccentText;
		}
		return (bSelected || IsHovered() || HasAnyUserFocus().IsSet()) ? Palette.Text : Palette.MutedText;
	case EMenuButtonKind::ListItem:
		return bSelected ? Palette.Accent : Palette.Text;
	default:
		return Palette.Text;
	}
}

EActiveTimerReturnType SMenuButton::Animate(double InCurrentTime, float InDeltaTime)
{
	const bool bMoving = Highlight.Advance(InDeltaTime);
	Invalidate(EInvalidateWidgetReason::Paint);
	if (!bMoving)
	{
		bAnimating = false;
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void SMenuButton::StartAnimating()
{
	Highlight.SetTarget(IsHovered() ? 1.f : 0.f, MenuControlsLocal::MotionFor(StyleHolder->Style), 0.12f);
	if (!bAnimating)
	{
		bAnimating = true;
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SMenuButton::Animate));
	}
}

FReply SMenuButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && IsEnabled())
	{
		bPressed = true;
		Invalidate(EInvalidateWidgetReason::Paint);
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SMenuButton::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPressed)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	Invalidate(EInvalidateWidgetReason::Paint);
	if (MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
	{
		ClickedDelegate.ExecuteIfBound();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

void SMenuButton::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	StartAnimating();
	HoveredDelegate.ExecuteIfBound(true);
}

void SMenuButton::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	bPressed = false;
	StartAnimating();
	HoveredDelegate.ExecuteIfBound(false);
}

FReply SMenuButton::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (MenuControlsLocal::IsAcceptKey(InKeyEvent) && IsEnabled())
	{
		ClickedDelegate.ExecuteIfBound();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FCursorReply SMenuButton::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return IsEnabled() ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
}

int32 SMenuButton::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FMenuStyle& Style = StyleHolder->Style;
	const FMenuStylePalette& Palette = StyleHolder->Palette();
	const bool bSelected = IsSelectedAttribute.Get(false);
	const bool bFocused = HasAnyUserFocus().IsSet();
	const float Hover = Highlight.Value;
	const float Radius = Style.CornerRadius;
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const bool bTabLike = Kind == EMenuButtonKind::Tab || Kind == EMenuButtonKind::Segment;
	auto Mix = [](const FLinearColor& A, const FLinearColor& B, float Alpha) { return FMath::Lerp(A, B, Alpha); };

	FLinearColor Fill = FLinearColor::Transparent;
	FLinearColor Outline = FLinearColor::Transparent;
	float OutlineWidth = 0.f;
	switch (Kind)
	{
	case EMenuButtonKind::Primary:
		Fill = Mix(Palette.Accent, Palette.Accent * 1.15f, Hover);
		Fill.A = Palette.Accent.A;
		break;
	case EMenuButtonKind::Secondary:
		Fill = Mix(Palette.Row, Palette.RowHovered, Hover);
		Outline = Palette.Track;
		OutlineWidth = Style.OutlineWidth;
		break;
	case EMenuButtonKind::Ghost:
		Fill = Palette.RowHovered * Hover;
		break;
	case EMenuButtonKind::ListItem:
		Fill = (bFocused || Hover > 0.f) ? Mix(Palette.Row, Palette.RowFocused, FMath::Max(Hover, bFocused ? 1.f : 0.f)) : FLinearColor::Transparent;
		break;
	case EMenuButtonKind::Tab:
	case EMenuButtonKind::Segment:
		switch (Style.TabStyle)
		{
		case EMenuTabStyle::Pills:
			Fill = bSelected ? Palette.Accent : Palette.RowHovered * Hover;
			break;
		case EMenuTabStyle::Boxed:
			Fill = bSelected ? Palette.Accent : Mix(Palette.Row, Palette.RowHovered, Hover);
			Outline = Palette.Track;
			OutlineWidth = Style.OutlineWidth;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	if (bPressed)
	{
		Fill = Fill * 0.85f;
	}

	const FMenuStyleSkin& Skin = Style.Skin;
	const FMenuPartSkin* Part = nullptr;
	switch (Kind)
	{
	case EMenuButtonKind::Primary: Part = &Skin.PrimaryButton; break;
	case EMenuButtonKind::Secondary: Part = &Skin.SecondaryButton; break;
	case EMenuButtonKind::Ghost: Part = &Skin.GhostButton; break;
	case EMenuButtonKind::Tab: Part = &Skin.Tab; break;
	case EMenuButtonKind::Segment: Part = &Skin.Segment; break;
	default: Part = &Skin.ListOption; break;
	}
	FMenuPartState State;
	State.Hover = Hover;
	State.Focus = bFocused ? 1.f : 0.f;
	State.bPressed = bPressed;
	State.bSelected = bSelected;
	State.bDisabled = !ShouldBeEnabled(bParentEnabled);

	const float BoxRadius = (bTabLike && Style.TabStyle == EMenuTabStyle::Pills) ? Size.Y * 0.5f : Radius;
	const bool bBuiltIn = Part->DrawsBuiltIn();
	if (bBuiltIn)
	{
		MenuDraw::RoundedBox(OutDrawElements, LayerId, AllottedGeometry, Fill * Tint, BoxRadius, Outline * Tint, OutlineWidth);
	}
	Painter.Draw(OutDrawElements, LayerId, AllottedGeometry, *Part, State, Fill.A > 0.f ? Fill : Palette.Accent, Palette, Tint, *this, 0);

	if (Kind == EMenuButtonKind::Tab && Style.TabStyle == EMenuTabStyle::Underline && (bSelected || Hover > 0.f))
	{
		const float BarWidth = Size.X * (bSelected ? 0.8f : 0.4f * Hover);
		const FLinearColor BarColor = bSelected ? Palette.Accent : Palette.Track;
		const FVector2f BarOffset((Size.X - BarWidth) * 0.5f, Size.Y - 3.f);
		const FVector2f BarSize(BarWidth, 3.f);
		if (Skin.TabIndicator.DrawsBuiltIn())
		{
			MenuDraw::RoundedBoxAt(OutDrawElements, LayerId + 1, AllottedGeometry, BarOffset, BarSize, BarColor * Tint, 1.5f);
		}
		Painter.Draw(OutDrawElements, LayerId + 1, AllottedGeometry, Skin.TabIndicator, State, BarColor, Palette, Tint, *this, 1, BarOffset, BarSize);
	}
	if (Kind == EMenuButtonKind::ListItem && bSelected && bBuiltIn)
	{
		MenuDraw::RoundedBoxAt(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2f(4.f, Size.Y * 0.25f), FVector2f(3.f, Size.Y * 0.5f), Palette.Accent * Tint, 1.5f);
	}
	if (bFocused && bBuiltIn && Style.FocusOutlineWidth > 0.f && Kind != EMenuButtonKind::ListItem)
	{
		MenuDraw::RoundedBox(OutDrawElements, LayerId + 1, AllottedGeometry, FLinearColor::Transparent, BoxRadius, Palette.FocusOutline * Tint, Style.FocusOutlineWidth);
	}
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}

// =====================================================================================================
//  Switch
// =====================================================================================================

void SMenuSwitch::Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle)
{
	StyleHolder = InStyle;
	IsOnAttribute = InArgs._IsOn;
	ToggledDelegate = InArgs._OnToggled;
	bLastOn = IsOnAttribute.Get(false);
	Knob.Snap(bLastOn ? 1.f : 0.f);
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SMenuSwitch::Animate));
}

EActiveTimerReturnType SMenuSwitch::Animate(double InCurrentTime, float InDeltaTime)
{
	const bool bOn = IsOnAttribute.Get(false);
	if (bOn != bLastOn)
	{
		bLastOn = bOn;
		Knob.SetTarget(bOn ? 1.f : 0.f, MenuControlsLocal::MotionFor(StyleHolder->Style), 0.f);
	}
	if (Knob.IsMoving())
	{
		Knob.Advance(InDeltaTime);
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return EActiveTimerReturnType::Continue;
}

FReply SMenuSwitch::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && IsEnabled())
	{
		ToggledDelegate.ExecuteIfBound(!IsOnAttribute.Get(false));
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FCursorReply SMenuSwitch::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return IsEnabled() ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
}

int32 SMenuSwitch::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FMenuStyleSkin& Skin = StyleHolder->Style.Skin;
	const FMenuStylePalette& Palette = StyleHolder->Palette();
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FLinearColor Tint = WidgetTint * (bEnabled ? 1.f : 0.45f);
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const float Alpha = FMath::Clamp(Knob.Value, 0.f, 1.f);

	FMenuPartState State;
	State.Hover = IsHovered() ? 1.f : 0.f;
	State.Focus = FocusedAttribute.Get(false) ? 1.f : 0.f;
	State.bSelected = IsOnAttribute.Get(false);
	State.bDisabled = !bEnabled;
	State.Value = Knob.Value;

	const FLinearColor Track = FMath::Lerp(Palette.Track, Palette.Accent, Alpha);
	if (Skin.SwitchTrack.DrawsBuiltIn())
	{
		MenuDraw::RoundedBox(OutDrawElements, LayerId, AllottedGeometry, Track * Tint, Size.Y * 0.5f);
	}
	Painter.Draw(OutDrawElements, LayerId, AllottedGeometry, Skin.SwitchTrack, State, Track, Palette, WidgetTint, *this, 0);

	const float KnobSize = Size.Y - 6.f;
	const float KnobX = 3.f + (Size.X - KnobSize - 6.f) * Knob.Value;
	const FLinearColor KnobColor = FMath::Lerp(Palette.Text, MenuDraw::ReadableOn(Palette.Accent), Alpha);
	if (Skin.SwitchKnob.DrawsBuiltIn())
	{
		MenuDraw::RoundedBoxAt(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2f(KnobX, 3.f), FVector2f(KnobSize, KnobSize), KnobColor * Tint, KnobSize * 0.5f);
	}
	Painter.Draw(OutDrawElements, LayerId + 1, AllottedGeometry, Skin.SwitchKnob, State, KnobColor, Palette, WidgetTint, *this, 1, FVector2f(KnobX, 3.f), FVector2f(KnobSize, KnobSize));
	return LayerId + 1;
}

// =====================================================================================================
//  Slider
// =====================================================================================================

void SMenuSlider::Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle)
{
	StyleHolder = InStyle;
	FractionAttribute = InArgs._Fraction;
	ChangedDelegate = InArgs._OnChanged;
	CommittedDelegate = InArgs._OnCommitted;
}

float SMenuSlider::FractionAt(const FGeometry& MyGeometry, const FVector2D& ScreenPosition) const
{
	const FVector2D Local = MyGeometry.AbsoluteToLocal(ScreenPosition);
	const double Inset = FMath::Max(8.0, StyleHolder->Style.SliderThumbSize * 0.5 + 0.0);
	const double Width = FMath::Max(1.0, MyGeometry.GetLocalSize().X - Inset * 2.0);
	return FMath::Clamp(static_cast<float>((Local.X - Inset) / Width), 0.f, 1.f);
}

FReply SMenuSlider::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !IsEnabled())
	{
		return FReply::Unhandled();
	}
	bDragging = true;
	DragFraction = FractionAt(MyGeometry, MouseEvent.GetScreenSpacePosition());
	ChangedDelegate.ExecuteIfBound(DragFraction);
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SMenuSlider::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging)
	{
		return FReply::Unhandled();
	}
	const float Fraction = FractionAt(MyGeometry, MouseEvent.GetScreenSpacePosition());
	if (!FMath::IsNearlyEqual(Fraction, DragFraction))
	{
		DragFraction = Fraction;
		ChangedDelegate.ExecuteIfBound(DragFraction);
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Handled();
}

FReply SMenuSlider::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging)
	{
		return FReply::Unhandled();
	}
	bDragging = false;
	CommittedDelegate.ExecuteIfBound(DragFraction);
	Invalidate(EInvalidateWidgetReason::Paint);
	return FReply::Handled().ReleaseMouseCapture();
}

FCursorReply SMenuSlider::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return IsEnabled() ? FCursorReply::Cursor(EMouseCursor::ResizeLeftRight) : FCursorReply::Unhandled();
}

int32 SMenuSlider::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FMenuStyle& Style = StyleHolder->Style;
	const FMenuStyleSkin& Skin = Style.Skin;
	const FMenuStylePalette& Palette = StyleHolder->Palette();
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FLinearColor Tint = WidgetTint * (bEnabled ? 1.f : 0.45f);
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const float Fraction = bDragging ? DragFraction : FMath::Clamp(FractionAttribute.Get(0.f), 0.f, 1.f);
	const float Inset = FMath::Max(8.f, Style.SliderThumbSize * 0.5f);
	const float TrackHeight = Style.SliderTrackHeight;
	const float Width = FMath::Max(1.f, Size.X - Inset * 2.f);
	const float TrackY = (Size.Y - TrackHeight) * 0.5f;

	FMenuPartState State;
	State.Hover = IsHovered() ? 1.f : 0.f;
	State.bPressed = bDragging;
	State.bDisabled = !bEnabled;
	State.Value = Fraction;

	const FVector2f TrackOffset(Inset, TrackY);
	const FVector2f TrackSize(Width, TrackHeight);
	if (Skin.SliderTrack.DrawsBuiltIn())
	{
		MenuDraw::RoundedBoxAt(OutDrawElements, LayerId, AllottedGeometry, TrackOffset, TrackSize, Palette.Track * Tint, TrackHeight * 0.5f);
	}
	Painter.Draw(OutDrawElements, LayerId, AllottedGeometry, Skin.SliderTrack, State, Palette.Track, Palette, WidgetTint, *this, 0, TrackOffset, TrackSize);

	if (Skin.SliderFill.DrawsBuiltIn())
	{
		MenuDraw::RoundedBoxAt(OutDrawElements, LayerId + 1, AllottedGeometry, TrackOffset, FVector2f(Width * Fraction, TrackHeight), Palette.Accent * Tint, TrackHeight * 0.5f);
	}
	if (Skin.SliderFill.IsSet() && Fraction > 0.f)
	{
		// The fill's image covers the whole track, cut at the value: an image isn't squashed as the value changes.
		const FGeometry FillClip = AllottedGeometry.MakeChild(FVector2f(Width * Fraction, Size.Y), FSlateLayoutTransform(FVector2f(Inset, 0.f)));
		OutDrawElements.PushClip(FSlateClippingZone(FillClip));
		Painter.Draw(OutDrawElements, LayerId + 1, AllottedGeometry, Skin.SliderFill, State, Palette.Accent, Palette, WidgetTint, *this, 1, TrackOffset, TrackSize);
		OutDrawElements.PopClip();
	}

	const float KnobSize = Style.SliderThumbSize + ((bDragging || IsHovered()) ? 2.f : 0.f);
	if (KnobSize > 0.f)
	{
		const FVector2f KnobOffset(Inset + Width * Fraction - KnobSize * 0.5f, (Size.Y - KnobSize) * 0.5f);
		if (Skin.SliderThumb.DrawsBuiltIn())
		{
			MenuDraw::RoundedBoxAt(OutDrawElements, LayerId + 2, AllottedGeometry, KnobOffset, FVector2f(KnobSize, KnobSize), Palette.Text * Tint, KnobSize * 0.5f, Palette.Accent * Tint, 2.f);
		}
		Painter.Draw(OutDrawElements, LayerId + 2, AllottedGeometry, Skin.SliderThumb, State, Palette.Text, Palette, WidgetTint, *this, 2, KnobOffset, FVector2f(KnobSize, KnobSize));
	}
	return LayerId + 2;
}

// =====================================================================================================
//  Selector
// =====================================================================================================

void SMenuSelector::Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle)
{
	StyleHolder = InStyle;
	ValueTextAttribute = InArgs._ValueText;
	OptionCountAttribute = InArgs._OptionCount;
	OptionIndexAttribute = InArgs._OptionIndex;
	CanStepLeftAttribute = InArgs._CanStepLeft;
	CanStepRightAttribute = InArgs._CanStepRight;
	bDropdown = InArgs._IsDropdown;
	StepDelegate = InArgs._OnStep;
	OpenDelegate = InArgs._OnOpen;
}

FVector2D SMenuSelector::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const FVector2f TextSize = MenuDraw::MeasureText(ValueTextAttribute.Get(FText::GetEmpty()), StyleHolder->Style.ValueFont);
	return FVector2D(FMath::Max(160.f, TextSize.X + 80.f), FMath::Max(32.f, TextSize.Y + 14.f));
}

FReply SMenuSelector::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !IsEnabled())
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const double Width = MyGeometry.GetLocalSize().X;
	if (bDropdown)
	{
		OpenDelegate.ExecuteIfBound();
		return FReply::Handled();
	}
	const int32 Direction = Local.X < 36.0 ? -1 : Local.X > Width - 36.0 ? 1 : (OpenDelegate.IsBound() ? 0 : 1);
	if (Direction == 0)
	{
		OpenDelegate.ExecuteIfBound();
	}
	else
	{
		StepDelegate.ExecuteIfBound(Direction);
	}
	return FReply::Handled();
}

FCursorReply SMenuSelector::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return IsEnabled() ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
}

void SMenuSelector::Pulse(int32 Direction)
{
	PulseDirection = Direction;
	PulseTime = MenuAccessibility::IsReduceMotionEnabled() ? 0.f : 0.18f;
	if (PulseTime > 0.f)
	{
		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SMenuSelector::Animate));
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

EActiveTimerReturnType SMenuSelector::Animate(double InCurrentTime, float InDeltaTime)
{
	PulseTime -= InDeltaTime;
	Invalidate(EInvalidateWidgetReason::Paint);
	return PulseTime > 0.f ? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
}

int32 SMenuSelector::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FMenuStyle& Style = StyleHolder->Style;
	const FMenuStyleSkin& Skin = Style.Skin;
	const FMenuStylePalette& Palette = StyleHolder->Palette();
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const FText Value = ValueTextAttribute.Get(FText::GetEmpty());
	const FVector2f TextSize = MenuDraw::MeasureText(Value, Style.ValueFont);
	const FLinearColor TextColor = bEnabled ? Palette.Text : Palette.DisabledText;
	const int32 Count = OptionCountAttribute.Get(0);
	const int32 Current = OptionIndexAttribute.Get(0);
	const bool bPips = !bDropdown && Style.bShowPips && Count > 1 && Count <= 12;
	const float PipSpace = bPips ? Style.PipHeight + 1.f : 0.f;
	const float TextY = (Size.Y - TextSize.Y) * 0.5f - PipSpace;

	FMenuPartState State;
	State.Hover = IsHovered() ? 1.f : 0.f;
	State.bDisabled = !bEnabled;
	State.Value = Count > 1 ? static_cast<float>(Current) / static_cast<float>(Count - 1) : -1.f;

	if (bDropdown)
	{
		if (Skin.Dropdown.DrawsBuiltIn())
		{
			MenuDraw::RoundedBox(OutDrawElements, LayerId, AllottedGeometry, Palette.Row * Tint, Style.CornerRadius, Palette.Track * Tint, Style.OutlineWidth);
		}
		Painter.Draw(OutDrawElements, LayerId, AllottedGeometry, Skin.Dropdown, State, Palette.Row, Palette, Tint, *this, 0);
		MenuDraw::Text(OutDrawElements, LayerId + 1, AllottedGeometry, Value, Style.ValueFont, FVector2f(14.f, (Size.Y - TextSize.Y) * 0.5f), TextColor * Tint);
		const float ArrowHalf = Style.SelectorArrowSize * 0.85f;
		const FVector2f ArrowCenter(Size.X - 18.f, Size.Y * 0.5f);
		if (Skin.DropdownArrow.DrawsBuiltIn())
		{
			MenuDraw::Chevron(OutDrawElements, LayerId + 1, AllottedGeometry, ArrowCenter, ArrowHalf, 0, Palette.MutedText * Tint);
		}
		Painter.Draw(OutDrawElements, LayerId + 1, AllottedGeometry, Skin.DropdownArrow, State, Palette.MutedText, Palette, Tint, *this, 1,
			ArrowCenter - FVector2f(ArrowHalf, ArrowHalf), FVector2f(ArrowHalf * 2.f, ArrowHalf * 2.f));
		return LayerId + 1;
	}

	if (Skin.SelectorBox.IsSet())
	{
		Painter.Draw(OutDrawElements, LayerId, AllottedGeometry, Skin.SelectorBox, State, Palette.Row, Palette, Tint, *this, 2);
	}
	MenuDraw::Text(OutDrawElements, LayerId + 1, AllottedGeometry, Value, Style.ValueFont, FVector2f((Size.X - TextSize.X) * 0.5f, TextY), TextColor * Tint);
	const float Glow = PulseTime > 0.f ? PulseTime / 0.18f : 0.f;
	const bool bLeft = bEnabled && CanStepLeftAttribute.Get(true);
	const bool bRight = bEnabled && CanStepRightAttribute.Get(true);
	const FLinearColor LeftColor = bLeft ? FMath::Lerp(Palette.MutedText, Palette.Accent, PulseDirection < 0 ? Glow : 0.f) : Palette.DisabledText;
	const FLinearColor RightColor = bRight ? FMath::Lerp(Palette.MutedText, Palette.Accent, PulseDirection > 0 ? Glow : 0.f) : Palette.DisabledText;
	const float ArrowHalf = Style.SelectorArrowSize;
	const FVector2f LeftCenter(8.f + ArrowHalf - (PulseDirection < 0 ? Glow * 3.f : 0.f), Size.Y * 0.5f);
	const FVector2f RightCenter(Size.X - 8.f - ArrowHalf + (PulseDirection > 0 ? Glow * 3.f : 0.f), Size.Y * 0.5f);
	if (Skin.SelectorArrow.DrawsBuiltIn())
	{
		MenuDraw::Chevron(OutDrawElements, LayerId + 1, AllottedGeometry, LeftCenter, ArrowHalf, -1, LeftColor * Tint);
		MenuDraw::Chevron(OutDrawElements, LayerId + 1, AllottedGeometry, RightCenter, ArrowHalf, 1, RightColor * Tint);
	}
	if (Skin.SelectorArrow.IsSet())
	{
		const FVector2f ArrowBox(ArrowHalf * 2.f, ArrowHalf * 2.f);
		FMenuPartState LeftState = State;
		LeftState.bDisabled = !bLeft;
		LeftState.bPressed = PulseDirection < 0 && Glow > 0.f;
		FMenuPartState RightState = State;
		RightState.bDisabled = !bRight;
		RightState.bPressed = PulseDirection > 0 && Glow > 0.f;
		Painter.Draw(OutDrawElements, LayerId + 1, AllottedGeometry, Skin.SelectorArrow, LeftState, LeftColor, Palette, Tint, *this, 3, LeftCenter - ArrowBox * 0.5f, ArrowBox);
		Painter.Draw(OutDrawElements, LayerId + 1, AllottedGeometry, Skin.SelectorArrow, RightState, RightColor, Palette, Tint, *this, 4, RightCenter - ArrowBox * 0.5f, ArrowBox, UE_PI);
	}

	if (bPips)
	{
		const float PipHeight = Style.PipHeight;
		const float PipWidth = FMath::Min(18.f, (Size.X - 80.f) / Count - 3.f);
		const float Total = Count * PipWidth + (Count - 1) * 3.f;
		float X = (Size.X - Total) * 0.5f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2f PipOffset(X, Size.Y - PipHeight - 3.f);
			const FVector2f PipSize(PipWidth, PipHeight);
			const FLinearColor PipColor = Index == Current ? Palette.Accent : Palette.Track;
			if (Skin.Pip.DrawsBuiltIn())
			{
				MenuDraw::RoundedBoxAt(OutDrawElements, LayerId + 1, AllottedGeometry, PipOffset, PipSize, PipColor * Tint, PipHeight * 0.5f);
			}
			if (Skin.Pip.IsSet())
			{
				FMenuPartState PipState = State;
				PipState.bSelected = Index == Current;
				Painter.Draw(OutDrawElements, LayerId + 1, AllottedGeometry, Skin.Pip, PipState, PipColor, Palette, Tint, *this, 16 + Index, PipOffset, PipSize);
			}
			X += PipWidth + 3.f;
		}
	}
	return LayerId + 1;
}

// =====================================================================================================
//  Dialog
// =====================================================================================================

void SMenuDialog::Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle)
{
	StyleHolder = InStyle;
	ChoiceDelegate = InArgs._OnChoice;
	DefaultIndex = InArgs._DefaultButton;
	CancelIndex = InArgs._CancelButton;
	const FMenuStyle& Style = StyleHolder->Style;
	const FMenuStylePalette& Palette = StyleHolder->Palette();

	TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < InArgs._Buttons.Num(); ++Index)
	{
		TSharedPtr<SMenuButton> Button;
		Buttons->AddSlot()
			.AutoWidth()
			.Padding(Index > 0 ? 10.f : 0.f, 0.f, 0.f, 0.f)
			[
				SAssignNew(Button, SMenuButton, StyleHolder)
				.Kind(InArgs._Buttons[Index].Kind)
				.Text(InArgs._Buttons[Index].Text)
				.MinWidth(120.f)
				.OnClicked(FOnMenuControlClicked::CreateSP(this, &SMenuDialog::Choose, Index))
			];
		ButtonWidgets.Add(Button);
	}

	const TAttribute<FText> BodyAttribute = InArgs._Body;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			MenuControlsLocal::MakeScrim(StyleHolder)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(InArgs._Width > 0.f ? InArgs._Width : Style.DialogWidth)
			[
				SNew(SMenuPartBox, StyleHolder)
				.Part_Lambda([](const FMenuStyle& PartStyle) { return &PartStyle.Skin.DialogCard; })
				.BuiltIn_Static(&MenuControlsLocal::CardBox)
				.Padding(FMargin(28.f, 24.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(InArgs._Title)
						.Font(Style.DescriptionTitleFont)
						.ColorAndOpacity(Palette.Text)
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 10.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(BodyAttribute)
						.Font(Style.DescriptionFont)
						.ColorAndOpacity(Palette.MutedText)
						.AutoWrapText(true)
						.Visibility_Lambda([BodyAttribute]() { return BodyAttribute.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.f, 12.f, 0.f, 0.f)
					[
						InArgs._Extra.Widget
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Right)
					.Padding(0.f, 22.f, 0.f, 0.f)
					[
						Buttons
					]
				]
			]
		]
	];
}

void SMenuDialog::FocusDefault(int32 UserIndex)
{
	if (ButtonWidgets.IsValidIndex(DefaultIndex) && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetUserFocus(UserIndex, ButtonWidgets[DefaultIndex], EFocusCause::SetDirectly);
	}
}

FReply SMenuDialog::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (ButtonWidgets.IsValidIndex(DefaultIndex))
	{
		return FReply::Handled().SetUserFocus(ButtonWidgets[DefaultIndex].ToSharedRef(), EFocusCause::SetDirectly);
	}
	return FReply::Handled();
}

FReply SMenuDialog::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (MenuControlsLocal::IsBackKey(InKeyEvent))
	{
		if (CancelIndex != INDEX_NONE)
		{
			Choose(CancelIndex);
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SMenuDialog::Choose(int32 Index)
{
	if (bChosen)
	{
		return;
	}
	bChosen = true;
	ChoiceDelegate.ExecuteIfBound(Index);
}

// =====================================================================================================
//  Option list
// =====================================================================================================

void SMenuOptionList::Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle)
{
	StyleHolder = InStyle;
	PickedDelegate = InArgs._OnPicked;
	CurrentIndex = InArgs._Current;
	const FMenuStyle& Style = StyleHolder->Style;
	const FMenuStylePalette& Palette = StyleHolder->Palette();

	SAssignNew(ScrollBox, SScrollBox);
	for (int32 Index = 0; Index < InArgs._Options.Num(); ++Index)
	{
		TSharedPtr<SMenuButton> Option;
		ScrollBox->AddSlot()
			.Padding(0.f, 1.f)
			[
				SAssignNew(Option, SMenuButton, StyleHolder)
				.Kind(EMenuButtonKind::ListItem)
				.Text(InArgs._Options[Index])
				.IsSelected(Index == CurrentIndex)
				.OnClicked_Lambda([this, Index]()
				{
					if (!bPicked)
					{
						bPicked = true;
						PickedDelegate.ExecuteIfBound(Index);
					}
				})
			];
		OptionWidgets.Add(Option);
	}

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			MenuControlsLocal::MakeScrim(StyleHolder)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(Card, SBox)
			.WidthOverride(440.f)
			.MaxDesiredHeight(560.f)
			[
				SNew(SMenuPartBox, StyleHolder)
				.Part_Lambda([](const FMenuStyle& PartStyle) { return &PartStyle.Skin.ListCard; })
				.BuiltIn_Static(&MenuControlsLocal::CardBox)
				.Padding(FMargin(10.f, 16.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(16.f, 0.f, 16.f, 10.f)
					[
						SNew(STextBlock)
						.Text(InArgs._Title)
						.Font(Style.SectionFont)
						.ColorAndOpacity(Palette.MutedText)
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						ScrollBox.ToSharedRef()
					]
				]
			]
		]
	];
}

void SMenuOptionList::FocusCurrent(int32 UserIndex)
{
	const int32 Index = OptionWidgets.IsValidIndex(CurrentIndex) ? CurrentIndex : 0;
	if (OptionWidgets.IsValidIndex(Index) && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetUserFocus(UserIndex, OptionWidgets[Index], EFocusCause::Navigation);
		ScrollBox->ScrollDescendantIntoView(OptionWidgets[Index], false, EDescendantScrollDestination::Center);
	}
}

FReply SMenuOptionList::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (MenuControlsLocal::IsBackKey(InKeyEvent))
	{
		if (!bPicked)
		{
			bPicked = true;
			PickedDelegate.ExecuteIfBound(INDEX_NONE);
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SMenuOptionList::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// A click on the scrim (outside the list) closes it; one on the card's own space does nothing.
	if (Card.IsValid() && Card->GetTickSpaceGeometry().IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
	{
		return FReply::Handled();
	}
	if (!bPicked)
	{
		bPicked = true;
		PickedDelegate.ExecuteIfBound(INDEX_NONE);
	}
	return FReply::Handled();
}
