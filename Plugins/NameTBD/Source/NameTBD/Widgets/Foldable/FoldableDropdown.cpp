// Copyright AR GameSystems 2026. All rights reserved.

#include "FoldableDropdown.h"

#include "Components/PanelWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/ArrangedChildren.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeTypes.h"
#include "NameTBD/Widgets/WidgetEffectsDraw.h"

#define LOCTEXT_NAMESPACE "FoldableDropdown"

namespace FoldableDropdownPrivate
{
	const FName HeaderSlot(TEXT("Header"));
	const FName BodySlot(TEXT("Body"));

	/** Turns Point clockwise (on screen) by Degrees about the origin. */
	FVector2f Turn(const FVector2f& Point, float Degrees)
	{
		float Sin = 0.f;
		float Cos = 0.f;
		FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(Degrees));
		return FVector2f(Point.X * Cos - Point.Y * Sin, Point.X * Sin + Point.Y * Cos);
	}

	FLinearColor Over(const FLinearColor& Under, const FLinearColor& Top, float Amount)
	{
		// Top laid over Under by Amount of its alpha.
		const float TopAlpha = FMath::Clamp(Top.A * Amount, 0.f, 1.f);
		const float Alpha = TopAlpha + Under.A * (1.f - TopAlpha);
		if (Alpha <= KINDA_SMALL_NUMBER)
		{
			return FLinearColor(0.f, 0.f, 0.f, 0.f);
		}
		FLinearColor Result = (Top * TopAlpha + Under * (Under.A * (1.f - TopAlpha))) / Alpha;
		Result.A = Alpha;
		return Result;
	}

	float SmoothStep(float Alpha)
	{
		const float T = FMath::Clamp(Alpha, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}
}

// =====================================================================================================
// SFoldableHeader
// =====================================================================================================

void SFoldableHeader::Construct(const FArguments& InArgs)
{
	OnClicked = InArgs._OnClicked;
	HoverAmount.Snap(0.f);
	PressAmount.Snap(0.f);
	SetCanTick(false);
	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SFoldableHeader::SetContent(const TSharedRef<SWidget>& InContent)
{
	ChildSlot
	[
		InContent
	];
}

void SFoldableHeader::SetLook(const FFoldableHeaderLook& InLook)
{
	HeaderLook = InLook;
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SFoldableHeader::SetOpenAmount(float InAmount)
{
	if (!FMath::IsNearlyEqual(OpenAmount, InAmount))
	{
		OpenAmount = InAmount;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

float SFoldableHeader::GetIconReserve() const
{
	return HeaderLook.Icon == EFoldableIcon::None ? 0.f : HeaderLook.IconSize + HeaderLook.IconSpacing;
}

FVector2D SFoldableHeader::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const TSharedRef<SWidget>& Child = ChildSlot.GetWidget();
	const FVector2D ContentSize = Child->GetVisibility() != EVisibility::Collapsed ? Child->GetDesiredSize() : FVector2D::ZeroVector;
	const FMargin& Padding = HeaderLook.Padding;
	const float Icon = HeaderLook.Icon == EFoldableIcon::None ? 0.f : HeaderLook.IconSize;
	return FVector2D(
		ContentSize.X + GetIconReserve() + Padding.GetTotalSpaceAlong<Orient_Horizontal>(),
		FMath::Max(ContentSize.Y, static_cast<double>(Icon)) + Padding.GetTotalSpaceAlong<Orient_Vertical>());
}

void SFoldableHeader::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const TSharedRef<SWidget>& Child = ChildSlot.GetWidget();
	const EVisibility ChildVisibility = Child->GetVisibility();
	if (!ArrangedChildren.Accepts(ChildVisibility))
	{
		return;
	}
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FMargin& Padding = HeaderLook.Padding;
	const float Reserve = GetIconReserve();
	const double Left = Padding.Left + (HeaderLook.IconSide == EFoldableIconSide::Left ? Reserve : 0.f);
	const double Width = FMath::Max(0.0, LocalSize.X - Padding.GetTotalSpaceAlong<Orient_Horizontal>() - Reserve);
	const double AreaHeight = FMath::Max(0.0, LocalSize.Y - Padding.GetTotalSpaceAlong<Orient_Vertical>());

	// Full width, its own height, centred down the header (beside a marker taller than it).
	const double Height = FMath::Min(Child->GetDesiredSize().Y, AreaHeight);
	const double Top = Padding.Top + (AreaHeight - Height) * 0.5;
	ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(Child, FVector2D(Width, Height), FSlateLayoutTransform(FVector2D(Left, Top))));
}

void SFoldableHeader::PaintIcon(const FGeometry& AllottedGeometry, const FBox2f& IconBox, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	using namespace FoldableDropdownPrivate;
	using namespace WidgetEffectsDraw;

	const float Size = HeaderLook.IconSize;
	const FVector2f Middle = IconBox.GetCenter();
	const float Open = FMath::Clamp(OpenAmount, 0.f, 1.f);
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();

	if (HeaderLook.Icon == EFoldableIcon::Image)
	{
		const FSlateBrush* Brush = nullptr;
		FLinearColor BrushTint = FLinearColor::White;
		if (HeaderLook.IconImageSource == EWidgetImageSource::SVG)
		{
			Brush = IconSVGBrush.Get(HeaderLook.IconSVG.Get(), nullptr, AllottedGeometry, FVector2f(Size, Size), *this);
		}
		else
		{
			IconSVGBrush.Reset();
			Brush = HeaderLook.IconImage.GetResourceObject() ? &HeaderLook.IconImage : nullptr;
			BrushTint = HeaderLook.IconImage.GetTint(InWidgetStyle);
		}
		if (Brush)
		{
			const float Angle = FMath::DegreesToRadians(HeaderLook.IconOpenAngle * OpenAmount);
			const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry(FVector2D(Size, Size), FSlateLayoutTransform(FVector2D(IconBox.Min)),
				FSlateRenderTransform(FQuat2f(Angle)), FVector2D(0.5, 0.5));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeometry, Brush, ESlateDrawEffect::None, HeaderLook.IconColor * BrushTint * WidgetTint);
		}
		return;
	}

	const float PixelScale = QuantizeScale(GetPixelScale(AllottedGeometry));
	const float Tolerance = WidgetShapeGeometry::GetTolerance(PixelScale);
	const float AntiAlias = 1.f / PixelScale;
	const float Thickness = HeaderLook.IconThickness;
	const float Reach = FMath::Max(Size * 0.5f - Thickness * 0.5f, 1.f);

	FWidgetShapeOutline Outline;
	auto AddLine = [&Outline, &Middle](std::initializer_list<FVector2f> Points, float Degrees, bool bClosed)
	{
		FWidgetShapeLine& Line = Outline.Lines.AddDefaulted_GetRef();
		Line.bClosed = bClosed;
		for (const FVector2f& Point : Points)
		{
			Line.Points.Add(Middle + Turn(Point, Degrees));
		}
	};

	FWidgetShapeMesh Mesh;
	switch (HeaderLook.Icon)
	{
	case EFoldableIcon::Chevron:
		// Down when closed, turning over to point up.
		AddLine({ FVector2f(-Reach, -Reach * 0.5f), FVector2f(0.f, Reach * 0.5f), FVector2f(Reach, -Reach * 0.5f) }, 180.f * OpenAmount, false);
		break;

	case EFoldableIcon::SideChevron:
		AddLine({ FVector2f(-Reach * 0.5f, -Reach), FVector2f(Reach * 0.5f, 0.f), FVector2f(-Reach * 0.5f, Reach) }, 90.f * OpenAmount, false);
		break;

	case EFoldableIcon::PlusMinus:
		AddLine({ FVector2f(-Reach, 0.f), FVector2f(Reach, 0.f) }, 0.f, false);
		if (Open < 0.999f)
		{
			// The upright turns as it folds flat into the bar.
			const float Upright = Reach * (1.f - Open);
			AddLine({ FVector2f(0.f, -Upright), FVector2f(0.f, Upright) }, 90.f * Open, false);
		}
		break;

	case EFoldableIcon::Caret:
	{
		const float Half = Size * 0.5f;
		AddLine({ FVector2f(-Half * 0.55f, -Half * 0.8f), FVector2f(Half * 0.75f, 0.f), FVector2f(-Half * 0.55f, Half * 0.8f) }, 90.f * OpenAmount, true);
		TArray<int8> FillOnLeft;
		WidgetShapeGeometry::BuildFill(Outline, AntiAlias, Mesh, FillOnLeft);
		break;
	}

	default:
		return;
	}

	if (HeaderLook.Icon != EFoldableIcon::Caret)
	{
		TArray<int8> FillOnLeft;
		WidgetShapeGeometry::ClassifyLines(Outline, FillOnLeft);
		FWidgetShapeStroke Stroke;
		Stroke.bEnabled = true;
		Stroke.Width = Thickness;
		Stroke.Align = EWidgetShapeStrokeAlign::Center;
		Stroke.Join = EWidgetShapeLineJoin::Round;
		Stroke.Cap = EWidgetShapeLineCap::Round;
		WidgetShapeGeometry::BuildStroke(Outline, FillOnLeft, Stroke, FWidgetShapeTrim(), AntiAlias, Tolerance, Mesh);
	}

	const FLinearColor IconColor = HeaderLook.IconColor;
	PaintMesh(OutDrawElements, LayerId, AllottedGeometry, Mesh, WidgetTint, [&IconColor](const FWidgetShapeMeshVertex&) { return IconColor; });
}

int32 SFoldableHeader::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace FoldableDropdownPrivate;

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FLinearColor Background = Over(Over(HeaderLook.Color, HeaderLook.HoverColor, HoverAmount.Value), HeaderLook.PressedColor, PressAmount.Value);
	if (Background.A > 0.002f)
	{
		FSlateBrush Rounded;
		Rounded.DrawAs = ESlateBrushDrawType::RoundedBox;
		Rounded.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		const double Radius = FMath::Min(static_cast<double>(HeaderLook.CornerRadius), FMath::Min(LocalSize.X, LocalSize.Y) * 0.5);
		Rounded.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &Rounded, ESlateDrawEffect::None,
			Background * InWidgetStyle.GetColorAndOpacityTint());
	}

	if (HeaderLook.Icon != EFoldableIcon::None)
	{
		const float Size = HeaderLook.IconSize;
		const float Left = HeaderLook.IconSide == EFoldableIconSide::Right
			? static_cast<float>(LocalSize.X) - HeaderLook.Padding.Right - Size
			: HeaderLook.Padding.Left;
		const float Top = (static_cast<float>(LocalSize.Y) - Size) * 0.5f;
		PaintIcon(AllottedGeometry, FBox2f(FVector2f(Left, Top), FVector2f(Left + Size, Top + Size)), OutDrawElements, LayerId + 1, InWidgetStyle);
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}

void SFoldableHeader::Lit()
{
	const float HoverGoal = (IsHovered() || HasKeyboardFocus()) ? 1.f : 0.f;
	const float PressGoal = (bPressed && IsHovered()) ? 1.f : 0.f;
	HoverAmount.SetTarget(HoverGoal, EWidgetMotion::Smooth, HoverGoal > HoverAmount.Value ? 0.1f : 0.2f);
	PressAmount.SetTarget(PressGoal, EWidgetMotion::Smooth, 0.08f);
	if (!AnimateTimer.IsValid() && (HoverAmount.IsMoving() || PressAmount.IsMoving()))
	{
		AnimateTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFoldableHeader::Animate));
	}
}

EActiveTimerReturnType SFoldableHeader::Animate(double InCurrentTime, float InDeltaTime)
{
	const bool bHoverMoving = HoverAmount.Advance(InDeltaTime);
	const bool bPressMoving = PressAmount.Advance(InDeltaTime);
	Invalidate(EInvalidateWidgetReason::Paint);
	if (bHoverMoving || bPressMoving)
	{
		return EActiveTimerReturnType::Continue;
	}
	AnimateTimer.Reset();
	return EActiveTimerReturnType::Stop;
}

FReply SFoldableHeader::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	Lit();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SFoldableHeader::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	// A quick second click toggles again, as any button does.
	return OnMouseButtonDown(InMyGeometry, InMouseEvent);
}

FReply SFoldableHeader::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	Lit();
	const bool bReleasedOver = MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition());
	FReply Reply = FReply::Handled().ReleaseMouseCapture();
	if (bReleasedOver)
	{
		OnClicked.ExecuteIfBound();
	}
	return Reply;
}

void SFoldableHeader::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	Lit();
}

void SFoldableHeader::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	Lit();
}

void SFoldableHeader::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
	bPressed = false;
	Lit();
}

FReply SFoldableHeader::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const bool bAccept = Key == EKeys::Enter || Key == EKeys::SpaceBar
		|| (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept);
	if (bAccept)
	{
		OnClicked.ExecuteIfBound();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SFoldableHeader::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	SCompoundWidget::OnFocusReceived(MyGeometry, InFocusEvent);
	Lit();
	return FReply::Handled();
}

void SFoldableHeader::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	SCompoundWidget::OnFocusLost(InFocusEvent);
	Lit();
}

// =====================================================================================================
// SFoldableBody
// =====================================================================================================

void SFoldableBody::Construct(const FArguments& InArgs)
{
	SetClipping(EWidgetClipping::ClipToBoundsAlways);
	SetCanTick(false);
	ChildSlot
	[
		SAssignNew(PaddingBox, SBox)
		[
			InArgs._Content.Widget
		]
	];
}

void SFoldableBody::SetContent(const TSharedRef<SWidget>& InContent)
{
	PaddingBox->SetContent(InContent);
}

void SFoldableBody::SetBodyPadding(const FMargin& InPadding)
{
	PaddingBox->SetPadding(InPadding);
}

void SFoldableBody::SetAnimation(EFoldableAnimation InAnimation, float InSlideDistance)
{
	Animation = InAnimation;
	SlideDistance = InSlideDistance;
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SFoldableBody::SetOpenAmount(float InAmount)
{
	OpenAmount = InAmount;
	Invalidate(EInvalidateWidgetReason::Layout);
}

float SFoldableBody::GetFullHeight() const
{
	return static_cast<float>(ChildSlot.GetWidget()->GetDesiredSize().Y);
}

float SFoldableBody::GetOpacity() const
{
	switch (Animation)
	{
	case EFoldableAnimation::Reveal:
	case EFoldableAnimation::Slide:
		return 1.f;
	default:
		// Fades in a little behind the height, so the content doesn't show before it has room.
		return FoldableDropdownPrivate::SmoothStep((OpenAmount - 0.1f) / 0.9f);
	}
}

void SFoldableBody::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const TSharedRef<SWidget>& Child = ChildSlot.GetWidget();
	const EVisibility ChildVisibility = Child->GetVisibility();
	if (!ArrangedChildren.Accepts(ChildVisibility))
	{
		return;
	}

	const double Width = AllottedGeometry.GetLocalSize().X;
	const double Height = Child->GetDesiredSize().Y;
	const double Open = FMath::Clamp(static_cast<double>(OpenAmount), 0.0, 1.0);
	double Offset = 0.0;
	switch (Animation)
	{
	case EFoldableAnimation::Slide:
		Offset = -(1.0 - Open) * Height;
		break;
	case EFoldableAnimation::FadeSlide:
		Offset = -(1.0 - Open) * SlideDistance;
		break;
	default:
		break;
	}

	if (Animation == EFoldableAnimation::Pop)
	{
		const float Scale = FMath::Lerp(0.94f, 1.f, static_cast<float>(Open));
		ArrangedChildren.AddWidget(ChildVisibility, FArrangedWidget(Child, AllottedGeometry.MakeChild(FVector2D(Width, Height),
			FSlateLayoutTransform(FVector2D(0.0, Offset)), FSlateRenderTransform(FScale2f(Scale)), FVector2D(0.5, 0.0))));
		return;
	}
	ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(Child, FVector2D(Width, Height), FSlateLayoutTransform(FVector2D(0.0, Offset))));
}

int32 SFoldableBody::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const float Opacity = GetOpacity();
	if (Opacity <= 0.001f)
	{
		return LayerId;
	}
	FWidgetStyle Faded = InWidgetStyle;
	Faded.BlendColorAndOpacityTint(FLinearColor(1.f, 1.f, 1.f, Opacity));
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, Faded, bParentEnabled);
}

// =====================================================================================================
// SFoldableDropdown
// =====================================================================================================

SFoldableDropdown::SFoldableDropdown()
	: Children(this)
{
}

void SFoldableDropdown::Construct(const FArguments& InArgs)
{
	OnFinished = InArgs._OnFinished;
	SetCanTick(false);
	Header = SNew(SFoldableHeader).OnClicked(InArgs._OnHeaderClicked);
	Body = SNew(SFoldableBody);
	Body->SetVisibility(EVisibility::Collapsed);
	Children.Add(Header.ToSharedRef());
	Children.Add(Body.ToSharedRef());
	Expansion.Snap(0.f);
	ApplyAmount();
}

void SFoldableDropdown::SetHeaderContent(const TSharedRef<SWidget>& InContent)
{
	Header->SetContent(InContent);
}

void SFoldableDropdown::SetBodyContent(const TSharedRef<SWidget>& InContent)
{
	Body->SetContent(InContent);
}

void SFoldableDropdown::SetHeaderLook(const FFoldableHeaderLook& InLook)
{
	Header->SetLook(InLook);
}

void SFoldableDropdown::SetBodyLook(const FMargin& InPadding, EFoldableAnimation InAnimation, float InSlideDistance, bool bInDivider, const FLinearColor& InDividerColor, float InDividerThickness)
{
	Body->SetBodyPadding(InPadding);
	Body->SetAnimation(InAnimation, InSlideDistance);
	bDivider = bInDivider;
	DividerColor = InDividerColor;
	DividerThickness = InDividerThickness;
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SFoldableDropdown::SetMotion(EWidgetMotion InMotion, float InEnterDuration, float InExitDuration)
{
	Motion = InMotion;
	EnterDuration = InEnterDuration;
	ExitDuration = InExitDuration;
}

void SFoldableDropdown::SetExpanded(bool bExpand, bool bAnimate)
{
	if (bExpand)
	{
		Body->SetVisibility(EVisibility::SelfHitTestInvisible);
	}
	Expansion.SetTarget(bExpand ? 1.f : 0.f, Motion, bExpand ? EnterDuration : ExitDuration, !bAnimate);
	ApplyAmount();
	if (Expansion.IsMoving())
	{
		if (!AnimateTimer.IsValid())
		{
			AnimateTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFoldableDropdown::Animate));
		}
	}
	else if (!bExpand)
	{
		Body->SetVisibility(EVisibility::Collapsed);
	}
}

void SFoldableDropdown::ApplyAmount()
{
	Header->SetOpenAmount(Expansion.Value);
	Body->SetOpenAmount(Expansion.Value);
	Invalidate(EInvalidateWidgetReason::Layout);
}

EActiveTimerReturnType SFoldableDropdown::Animate(double InCurrentTime, float InDeltaTime)
{
	const bool bMoving = Expansion.Advance(InDeltaTime);
	ApplyAmount();
	if (bMoving)
	{
		return EActiveTimerReturnType::Continue;
	}

	const bool bOpen = Expansion.Target > 0.5f;
	if (!bOpen)
	{
		// Fully closed: out of layout, paint and hit testing.
		Body->SetVisibility(EVisibility::Collapsed);
	}
	AnimateTimer.Reset();
	OnFinished.ExecuteIfBound(bOpen);
	return EActiveTimerReturnType::Stop;
}

float SFoldableDropdown::GetBodyHeight() const
{
	if (Body->GetVisibility() == EVisibility::Collapsed)
	{
		return 0.f;
	}
	return Body->GetFullHeight() * FMath::Max(Expansion.Value, 0.f);
}

FVector2D SFoldableDropdown::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const FVector2D HeaderSize = Header->GetDesiredSize();
	const bool bBodyShown = Body->GetVisibility() != EVisibility::Collapsed;
	const double BodyWidth = bBodyShown ? Body->GetDesiredSize().X : 0.0;
	return FVector2D(FMath::Max(HeaderSize.X, BodyWidth), HeaderSize.Y + GetBodyHeight());
}

void SFoldableDropdown::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const double HeaderHeight = FMath::Min(Header->GetDesiredSize().Y, LocalSize.Y);
	const EVisibility HeaderVisibility = Header->GetVisibility();
	if (ArrangedChildren.Accepts(HeaderVisibility))
	{
		ArrangedChildren.AddWidget(HeaderVisibility, AllottedGeometry.MakeChild(Header.ToSharedRef(), FVector2D(LocalSize.X, HeaderHeight), FSlateLayoutTransform()));
	}

	// The body gets what is left: during the animation, the share of its height the layout has given the dropdown so far.
	const EVisibility BodyVisibility = Body->GetVisibility();
	const double BodyHeight = FMath::Max(0.0, LocalSize.Y - HeaderHeight);
	if (BodyHeight > 0.0 && ArrangedChildren.Accepts(BodyVisibility))
	{
		ArrangedChildren.AddWidget(BodyVisibility, AllottedGeometry.MakeChild(Body.ToSharedRef(), FVector2D(LocalSize.X, BodyHeight), FSlateLayoutTransform(FVector2D(0.0, HeaderHeight))));
	}
}

int32 SFoldableDropdown::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 MaxLayer = SPanel::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const float Open = FMath::Clamp(Expansion.Value, 0.f, 1.f);
	if (bDivider && Open > 0.001f && Body->GetVisibility() != EVisibility::Collapsed)
	{
		const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
		const double HeaderHeight = FMath::Min(Header->GetDesiredSize().Y, LocalSize.Y);
		FLinearColor Line = DividerColor;
		Line.A *= Open;
		FSlateDrawElement::MakeBox(OutDrawElements, ++MaxLayer,
			AllottedGeometry.ToPaintGeometry(FVector2D(LocalSize.X, DividerThickness), FSlateLayoutTransform(FVector2D(0.0, HeaderHeight - DividerThickness * 0.5))),
			FCoreStyle::Get().GetBrush("GenericWhiteBox"), ESlateDrawEffect::None, Line * InWidgetStyle.GetColorAndOpacityTint());
	}
	return MaxLayer;
}

// =====================================================================================================
// UFoldableDropdown
// =====================================================================================================

UFoldableDropdown::UFoldableDropdown(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FFoldableHeaderLook UFoldableDropdown::MakeHeaderLook() const
{
	FFoldableHeaderLook Look;
	Look.Padding = HeaderPadding;
	Look.Color = HeaderColor;
	Look.HoverColor = HeaderHoverColor;
	Look.PressedColor = HeaderPressedColor;
	Look.CornerRadius = HeaderCornerRadius;
	Look.Icon = Icon;
	Look.IconSide = IconSide;
	Look.IconSize = IconSize;
	Look.IconThickness = IconThickness;
	Look.IconSpacing = IconSpacing;
	Look.IconColor = IconColor;
	Look.IconOpenAngle = IconOpenAngle;
	Look.IconImageSource = IconImageSource;
	Look.IconImage = IconImage;
	Look.IconSVG = IconSVG.Get();
	Look.bFocusable = bFocusable;
	return Look;
}

TSharedRef<SWidget> UFoldableDropdown::RebuildWidget()
{
	MyDropdown = SNew(SFoldableDropdown)
		.OnHeaderClicked(BIND_UOBJECT_DELEGATE(FSimpleDelegate, HandleHeaderClicked))
		.OnFinished(BIND_UOBJECT_DELEGATE(FOnFoldableFinished, HandleFinished));

	if (HeaderContent)
	{
		MyDropdown->SetHeaderContent(HeaderContent->TakeWidget());
	}
	if (BodyContent)
	{
		MyDropdown->SetBodyContent(BodyContent->TakeWidget());
	}
	return MyDropdown.ToSharedRef();
}

void UFoldableDropdown::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (!MyDropdown.IsValid())
	{
		return;
	}
	MyDropdown->SetHeaderLook(MakeHeaderLook());
	MyDropdown->SetBodyLook(BodyPadding, Animation, SlideDistance, bShowDivider, DividerColor, DividerThickness);
	MyDropdown->SetMotion(Motion, EnterDuration, ExitDuration);

	bool bShowOpen = bIsExpanded;
#if WITH_EDITORONLY_DATA
	bShowOpen |= bDesignerOpened;
#endif
	if (MyDropdown->IsExpanded() != bShowOpen)
	{
		MyDropdown->SetExpanded(bShowOpen, /*bAnimate=*/false);
	}
}

void UFoldableDropdown::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyDropdown.Reset();
	if (bReleaseChildren)
	{
		if (HeaderContent)
		{
			HeaderContent->ReleaseSlateResources(true);
		}
		if (BodyContent)
		{
			BodyContent->ReleaseSlateResources(true);
		}
	}
}

void UFoldableDropdown::SetExpanded(bool bExpand, bool bAnimate)
{
	const bool bChanged = bIsExpanded != bExpand;
	bIsExpanded = bExpand;
	if (MyDropdown.IsValid() && MyDropdown->IsExpanded() != bExpand)
	{
		MyDropdown->SetExpanded(bExpand, bAnimate && !IsDesignTime());
	}
	if (!bChanged)
	{
		return;
	}
	if (bExpand)
	{
		CloseGroupSiblings();
	}
	OnExpansionChanged.Broadcast(bExpand);
}

void UFoldableDropdown::ToggleExpanded(bool bAnimate)
{
	SetExpanded(!bIsExpanded, bAnimate);
}

float UFoldableDropdown::GetExpansionAmount() const
{
	return MyDropdown.IsValid() ? MyDropdown->GetOpenAmount() : (bIsExpanded ? 1.f : 0.f);
}

void UFoldableDropdown::HandleHeaderClicked()
{
	if (bToggleOnHeaderClick)
	{
		ToggleExpanded(true);
	}
}

void UFoldableDropdown::HandleFinished(bool bExpanded)
{
	OnFoldFinished.Broadcast(bExpanded);
}

void UFoldableDropdown::CloseGroupSiblings()
{
	if (AccordionGroup.IsNone())
	{
		return;
	}
	UPanelWidget* ParentPanel = GetParent();
	if (!ParentPanel)
	{
		return;
	}
	for (int32 Index = 0; Index < ParentPanel->GetChildrenCount(); ++Index)
	{
		UFoldableDropdown* Sibling = Cast<UFoldableDropdown>(ParentPanel->GetChildAt(Index));
		if (Sibling && Sibling != this && Sibling->AccordionGroup == AccordionGroup && Sibling->IsExpanded())
		{
			Sibling->SetExpanded(false, true);
		}
	}
}

void UFoldableDropdown::GetSlotNames(TArray<FName>& SlotNames) const
{
	SlotNames.Add(FoldableDropdownPrivate::HeaderSlot);
	SlotNames.Add(FoldableDropdownPrivate::BodySlot);
}

UWidget* UFoldableDropdown::GetContentForSlot(FName SlotName) const
{
	if (SlotName == FoldableDropdownPrivate::HeaderSlot)
	{
		return HeaderContent;
	}
	if (SlotName == FoldableDropdownPrivate::BodySlot)
	{
		return BodyContent;
	}
	return nullptr;
}

void UFoldableDropdown::SetContentForSlot(FName SlotName, UWidget* Content)
{
	if (SlotName == FoldableDropdownPrivate::HeaderSlot)
	{
		HeaderContent = Content;
		if (MyDropdown.IsValid())
		{
			MyDropdown->SetHeaderContent(Content ? Content->TakeWidget() : SNullWidget::NullWidget);
		}
	}
	else if (SlotName == FoldableDropdownPrivate::BodySlot)
	{
		BodyContent = Content;
		if (MyDropdown.IsValid())
		{
			MyDropdown->SetBodyContent(Content ? Content->TakeWidget() : SNullWidget::NullWidget);
		}
	}
}

#if WITH_EDITOR
const FText UFoldableDropdown::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}

void UFoldableDropdown::OnDescendantSelectedByDesigner(UWidget* DescendantWidget)
{
	// Open in the designer while something inside the body is being edited, as Expandable Area does.
	if (BodyContent && DescendantWidget && (DescendantWidget == BodyContent || DescendantWidget->IsChildOf(BodyContent)))
	{
		bDesignerOpened = true;
		if (MyDropdown.IsValid())
		{
			MyDropdown->SetExpanded(true, false);
		}
	}
}

void UFoldableDropdown::OnDescendantDeselectedByDesigner(UWidget* DescendantWidget)
{
	if (bDesignerOpened)
	{
		bDesignerOpened = false;
		if (MyDropdown.IsValid())
		{
			MyDropdown->SetExpanded(bIsExpanded, false);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
