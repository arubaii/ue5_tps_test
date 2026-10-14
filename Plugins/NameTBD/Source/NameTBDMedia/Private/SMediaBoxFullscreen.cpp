// Copyright AR GameSystems 2026. All rights reserved.

#include "SMediaBoxFullscreen.h"

#include "MediaBox.h"

#include "Containers/Ticker.h"
#include "InputCoreTypes.h"
#include "Layout/ArrangedChildren.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"

// =====================================================================================================
// SMediaBoxFullscreenSlot
// =====================================================================================================

void SMediaBoxFullscreenSlot::Construct(const FArguments& InArgs)
{
	HeldContent = InArgs._Content.Widget;
	ChildSlot
	[
		HeldContent.ToSharedRef()
	];
}

TSharedPtr<SWidget> SMediaBoxFullscreenSlot::LendContent()
{
	if (!HeldContent.IsValid() || HeldContent == SNullWidget::NullWidget)
	{
		return nullptr;
	}

	// The place kept at the size it had, so nothing around the box moves while it is full screen.
	const FVector2D KeptSize = GetTickSpaceGeometry().GetLocalSize();
	const TSharedPtr<SWidget> Lent = HeldContent;
	HeldContent.Reset();
	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(static_cast<float>(KeptSize.X))
		.HeightOverride(static_cast<float>(KeptSize.Y))
	];
	return Lent;
}

void SMediaBoxFullscreenSlot::ReturnContent(const TSharedRef<SWidget>& Returned)
{
	HeldContent = Returned;
	ChildSlot
	[
		Returned
	];
}

FSlateRect SMediaBoxFullscreenSlot::GetDesktopRect() const
{
	return GetTickSpaceGeometry().GetRenderBoundingRect();
}

// =====================================================================================================
// SMediaBoxFullscreenLayer
// =====================================================================================================

SMediaBoxFullscreenLayer::SMediaBoxFullscreenLayer()
	: LayerChildren(this)
{
}

void SMediaBoxFullscreenLayer::Construct(const FArguments& InArgs)
{
	WeakOwnerBox = InArgs._OwnerBox;
	FromRect = InArgs._From;
	Transition = InArgs._Transition;
	TransitionTime = InArgs._TransitionTime;
	Backdrop = InArgs._Backdrop;
	bEscapeLeaves = InArgs._bEscapeLeaves;

	LayerContent = InArgs._Content.Widget;
	LayerChildren.Add(LayerContent.ToSharedRef());

	// No transition: there at once.
	Progress = (Transition == EMediaBoxFullscreenTransition::None || TransitionTime <= 0.f) ? 1.f : 0.f;
	Goal = 1.f;
}

void SMediaBoxFullscreenLayer::BeginExit(const FSlateRect& To, bool bInstant, TFunction<void()> Done)
{
	if (To.IsValid() && To.GetArea() > 0.f)
	{
		FromRect = To;
	}
	Goal = 0.f;
	OnExited = MoveTemp(Done);
	bExitReported = false;
	if (bInstant || Transition == EMediaBoxFullscreenTransition::None || TransitionTime <= 0.f)
	{
		Progress = 0.f;
	}
}

void SMediaBoxFullscreenLayer::BeginEnter()
{
	Goal = 1.f;
	OnExited = nullptr;
	bExitReported = false;
}

TSharedPtr<SWidget> SMediaBoxFullscreenLayer::TakeContent()
{
	const TSharedPtr<SWidget> Taken = LayerContent;
	LayerChildren.Empty();
	LayerContent.Reset();
	return Taken;
}

float SMediaBoxFullscreenLayer::GetEased() const
{
	// Eased both ways (smoothstep): starts and lands gently.
	const float Clamped = FMath::Clamp(Progress, 0.f, 1.f);
	return Clamped * Clamped * (3.f - 2.f * Clamped);
}

void SMediaBoxFullscreenLayer::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	if (!LayerContent.IsValid() || !ArrangedChildren.Accepts(LayerContent->GetVisibility()))
	{
		return;
	}

	const FVector2f Full = FVector2f(FVector2D(AllottedGeometry.GetLocalSize()));
	FVector2f TopLeft = FVector2f::ZeroVector;
	FVector2f Size = Full;

	// Zoom: from where the box was on screen to the whole viewport.
	if (Transition == EMediaBoxFullscreenTransition::Zoom && FromRect.IsValid() && Progress < 1.f)
	{
		// FromRect is on the desktop: placed through the tick geometry (desktop space; its local space is the same as this one's).
		const FGeometry& DesktopGeometry = GetTickSpaceGeometry();
		const FVector2f FromTopLeft = FVector2f(FVector2D(DesktopGeometry.AbsoluteToLocal(FVector2D(FromRect.Left, FromRect.Top))));
		const FVector2f FromBottomRight = FVector2f(FVector2D(DesktopGeometry.AbsoluteToLocal(FVector2D(FromRect.Right, FromRect.Bottom))));
		const float Eased = GetEased();
		TopLeft = FMath::Lerp(FromTopLeft, FVector2f::ZeroVector, Eased);
		Size = FMath::Lerp(FromBottomRight - FromTopLeft, Full, Eased);
		Size.X = FMath::Max(Size.X, 1.f);
		Size.Y = FMath::Max(Size.Y, 1.f);
	}
	ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(LayerContent.ToSharedRef(), Size, FSlateLayoutTransform(TopLeft)));
}

FVector2D SMediaBoxFullscreenLayer::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	// Fills the viewport whatever the content asks for.
	return FVector2D::ZeroVector;
}

FChildren* SMediaBoxFullscreenLayer::GetChildren()
{
	return &LayerChildren;
}

void SMediaBoxFullscreenLayer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SPanel::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const float Step = TransitionTime > 0.f ? InDeltaTime / TransitionTime : 1.f;
	const float Before = Progress;
	Progress = Goal > Progress ? FMath::Min(Goal, Progress + Step) : FMath::Max(Goal, Progress - Step);
	if (Progress != Before)
	{
		Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
	}

	// Out: told on a later tick, never while Slate is ticking this widget (the layer is taken off the viewport then).
	if (Goal <= 0.f && Progress <= 0.f && !bExitReported && OnExited)
	{
		bExitReported = true;
		TFunction<void()> Done = MoveTemp(OnExited);
		OnExited = nullptr;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Done](float)
		{
			Done();
			return false; // one-shot
		}));
	}
}

int32 SMediaBoxFullscreenLayer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// Fade: everything at the transition's alpha. Zoom: the backdrop fades as the video grows.
	const float Eased = GetEased();
	FLinearColor BackdropNow = Backdrop * InWidgetStyle.GetColorAndOpacityTint();
	BackdropNow.A *= Eased;
	if (BackdropNow.A > 0.f)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, BackdropNow);
	}

	FWidgetStyle ContentStyle = InWidgetStyle;
	if (Transition == EMediaBoxFullscreenTransition::Fade)
	{
		ContentStyle.BlendColorAndOpacityTint(FLinearColor(1.f, 1.f, 1.f, Eased));
	}
	return SPanel::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, ContentStyle, bParentEnabled);
}

FReply SMediaBoxFullscreenLayer::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bEscapeLeaves && InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (UMediaBox* OwnerBox = WeakOwnerBox.Get())
		{
			OwnerBox->SetFullscreen(false);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SMediaBoxFullscreenLayer::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Nothing under the full-screen video is clicked through it.
	return FReply::Handled();
}
