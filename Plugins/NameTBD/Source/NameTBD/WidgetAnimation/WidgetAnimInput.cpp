// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimInput.h"

#include "CoreGlobals.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Geometry.h"
#include "Widgets/SWidget.h"

// =====================================================================================================
// Measuring
// =====================================================================================================

namespace WidgetAnimInputPrivate
{
	/**
	 * Widget's desktop render transform with its own render transform taken back out: where it would be drawn if nothing
	 * were animating it. Slate builds a widget's transform as its own transform about its pivot, then its layout, then
	 * its parents' - so undoing the first step leaves the other two, which an animation on this widget never touches.
	 */
	bool GetUnanimatedTransform(const UWidget* Widget, FSlateRenderTransform& OutTransform, FVector2f& OutSize)
	{
		const TSharedPtr<SWidget> Slate = Widget ? Widget->GetCachedWidget() : nullptr;
		if (!Slate.IsValid())
		{
			return false;
		}

		const FGeometry& Geometry = Slate->GetTickSpaceGeometry();
		OutSize = Geometry.GetLocalSize();

		// Not laid out yet (its first frame): nothing to measure against.
		if (OutSize.X <= UE_KINDA_SMALL_NUMBER && OutSize.Y <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		OutTransform = Geometry.GetAccumulatedRenderTransform();

		const TOptional<FSlateRenderTransform> Own = Slate->GetRenderTransformWithRespectToFlowDirection();
		if (Own.IsSet())
		{
			const FVector2D PivotFraction(Slate->GetRenderTransformPivotWithRespectToFlowDirection());
			const FVector2f PivotPx(static_cast<float>(PivotFraction.X) * OutSize.X, static_cast<float>(PivotFraction.Y) * OutSize.Y);
			const FSlateRenderTransform UndoOwn = ::Concatenate(::Concatenate(FSlateRenderTransform(-PivotPx), Own.GetValue().Inverse()), FSlateRenderTransform(PivotPx));
			OutTransform = ::Concatenate(UndoOwn, OutTransform);
		}
		return true;
	}

	/** The widget's laid-out rectangle in desktop space: without any render transform, its own or its parents'. */
	bool GetLayoutRect(const UWidget* Widget, FSlateRect& OutRect)
	{
		const TSharedPtr<SWidget> Slate = Widget ? Widget->GetCachedWidget() : nullptr;
		if (!Slate.IsValid())
		{
			return false;
		}

		OutRect = Slate->GetTickSpaceGeometry().GetLayoutBoundingRect();
		return OutRect.GetSize().SizeSquared() > UE_KINDA_SMALL_NUMBER;
	}

	bool IsHitTestable(ESlateVisibility Visibility)
	{
		return Visibility == ESlateVisibility::Visible || Visibility == ESlateVisibility::SelfHitTestInvisible;
	}

	bool IsFocused(const UWidget* Widget)
	{
		return Widget && (Widget->HasAnyUserFocus() || Widget->HasFocusedDescendants());
	}

	/** One point measured in Units, from its local position and the widget's size. */
	FVector2D MeasurePointer(EWidgetAnimPointerUnits Units, const FVector2f& Local, const FVector2f& Size)
	{
		const FVector2D Point(Local.X, Local.Y);
		const FVector2D Extent(FMath::Max(Size.X, UE_KINDA_SMALL_NUMBER), FMath::Max(Size.Y, UE_KINDA_SMALL_NUMBER));

		switch (Units)
		{
		case EWidgetAnimPointerUnits::Fraction:
			return Point / Extent;

		case EWidgetAnimPointerUnits::Pixels:
			return Point - Extent * 0.5;

		default:
			return (Point / Extent) * 2.0 - FVector2D(1.0, 1.0);
		}
	}
}

bool WidgetAnimInput::IsPointerOver(const UWidget* Widget, const FVector2f& PointerPosition)
{
	if (!Widget)
	{
		return false;
	}

	const ESlateVisibility Visibility = Widget->GetVisibility();
	if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
	{
		return false;
	}

	// Slate's hover knows what is drawn on top of what, so a card under a popup isn't hovered through it. A widget that
	// can't be hit, itself or through its children, is never hovered in Slate's sense, so its rectangle answers instead.
	if (WidgetAnimInputPrivate::IsHitTestable(Visibility))
	{
		return Widget->IsHovered();
	}

	FVector2f Local;
	FVector2f Size;
	if (!ToUnanimatedLocal(Widget, PointerPosition, Local, Size))
	{
		return false;
	}
	return Local.X >= 0.f && Local.Y >= 0.f && Local.X <= Size.X && Local.Y <= Size.Y;
}

bool WidgetAnimInput::ToUnanimatedLocal(const UWidget* Widget, const FVector2f& DesktopPoint, FVector2f& OutLocal, FVector2f& OutSize)
{
	FSlateRenderTransform Transform;
	if (!WidgetAnimInputPrivate::GetUnanimatedTransform(Widget, Transform, OutSize))
	{
		return false;
	}

	OutLocal = Transform.Inverse().TransformPoint(DesktopPoint);
	return true;
}

FVector2f WidgetAnimInput::ToUnanimatedLocalVector(const UWidget* Widget, const FVector2f& DesktopVector)
{
	FSlateRenderTransform Transform;
	FVector2f Size;
	if (!WidgetAnimInputPrivate::GetUnanimatedTransform(Widget, Transform, Size))
	{
		return DesktopVector;
	}

	return Transform.Inverse().TransformVector(DesktopVector);
}

bool WidgetAnimInput::IsMouseButtonDown(const FKey& Button)
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	// The buttons held don't change within a frame; the set is copied out of Slate once a frame rather than once per input.
	static TSet<FKey> Pressed;
	static uint64 PressedFrame = TNumericLimits<uint64>::Max();
	if (PressedFrame != GFrameCounter)
	{
		PressedFrame = GFrameCounter;
		Pressed = FSlateApplication::Get().GetPressedMouseButtons();
	}
	return Pressed.Contains(Button);
}

bool WidgetAnimInput::GetPointerPosition(FVector2f& OutPosition)
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}

	static FVector2f Position = FVector2f::ZeroVector;
	static uint64 PositionFrame = TNumericLimits<uint64>::Max();
	if (PositionFrame != GFrameCounter)
	{
		PositionFrame = GFrameCounter;
		const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
		Position = FVector2f(static_cast<float>(Cursor.X), static_cast<float>(Cursor.Y));
	}
	OutPosition = Position;
	return true;
}

FVector4 WidgetAnimInput::FromAxes(EWidgetAnimInputAxes Axes, const FVector2D& Value)
{
	switch (Axes)
	{
	case EWidgetAnimInputAxes::X:        return WidgetAnimValue::FromFloat(Value.X);
	case EWidgetAnimInputAxes::Y:        return WidgetAnimValue::FromFloat(Value.Y);
	case EWidgetAnimInputAxes::Distance: return WidgetAnimValue::FromFloat(Value.Size());
	default:                             return WidgetAnimValue::FromVector2D(Value);
	}
}

UScrollBox* WidgetAnimInput::FindScrollBox(UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}

	if (UScrollBox* Itself = Cast<UScrollBox>(Widget))
	{
		return Itself;
	}

	// Around it first - an item in a list reads the list it scrolls in. Up through its panels, and out of each User
	// Widget's tree into the widget that User Widget is placed in, whose own parent carries on.
	static constexpr int32 MaxDepth = 256;
	int32 Depth = 0;
	for (UWidget* Walk = Widget; Walk && Depth < MaxDepth; ++Depth)
	{
		UWidget* Next = Walk->GetParent();
		if (!Next)
		{
			const UWidgetTree* Tree = Cast<UWidgetTree>(Walk->GetOuter());
			Next = Tree ? Cast<UUserWidget>(Tree->GetOuter()) : nullptr;
		}

		if (UScrollBox* Around = Cast<UScrollBox>(Next))
		{
			return Around;
		}
		Walk = Next;
	}

	// Then inside it: a whole screen reading the list it holds.
	UScrollBox* Inside = nullptr;
	UWidgetTree::ForWidgetAndChildren(Widget, [&Inside](UWidget* Child)
	{
		if (!Inside)
		{
			Inside = Cast<UScrollBox>(Child);
		}
	});
	return Inside;
}

// =====================================================================================================
// Pointer Position
// =====================================================================================================

bool FWidgetAnimInput_PointerPosition::Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue)
{
	if (!Context.bHasPointer)
	{
		return false;
	}

	FVector2f Local;
	FVector2f Size;
	if (!WidgetAnimInput::ToUnanimatedLocal(Context.MeasuredWidget, Context.PointerPosition, Local, Size))
	{
		return false;
	}

	const bool bMeasure = WhenAway == EWidgetAnimPointerAway::KeepFollowing || WidgetAnimInput::IsPointerOver(Context.MeasuredWidget, Context.PointerPosition);

	FVector2D Value;
	if (bMeasure)
	{
		Value = WidgetAnimInputPrivate::MeasurePointer(Units, Local, Size);
		Last = Value;
		bHasLast = true;
	}
	else if (WhenAway == EWidgetAnimPointerAway::HoldLast && bHasLast)
	{
		Value = Last;
	}
	else
	{
		// The middle: 0 Centered, 0.5 as a Fraction, 0 in Pixels from the middle.
		Value = WidgetAnimInputPrivate::MeasurePointer(Units, Size * 0.5f, Size);
	}

	OutValue = WidgetAnimInput::FromAxes(Output, Value);
	return true;
}

EWidgetAnimValueKind FWidgetAnimInput_PointerPosition::GetValueKind() const
{
	return WidgetAnimInput::KindOfAxes(Output);
}

FString FWidgetAnimInput_PointerPosition::GetSummary() const
{
	return TEXT("Pointer Position");
}

// =====================================================================================================
// Hover, Press or Focus
// =====================================================================================================

bool FWidgetAnimInput_Interaction::Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue)
{
	UWidget* Widget = Context.MeasuredWidget;
	const bool bHovered = Context.bHasPointer && WidgetAnimInput::IsPointerOver(Widget, Context.PointerPosition);

	// Pressed is watched every frame whatever State is, so switching State at runtime doesn't start mid-press.
	const bool bDown = WidgetAnimInput::IsMouseButtonDown(EKeys::LeftMouseButton);
	if (bDown && !bWasDown)
	{
		bPressStartedOver = bHovered;
	}
	else if (!bDown)
	{
		bPressStartedOver = false;
	}
	bWasDown = bDown;

	bool bOn = false;
	switch (State)
	{
	case EWidgetAnimInteractionState::Hovered:
		bOn = bHovered;
		break;

	case EWidgetAnimInteractionState::Pressed:
		// A Button knows its own pressed state, keyboard and gamepad presses included.
		if (const UButton* Button = Cast<UButton>(Widget))
		{
			bOn = Button->IsPressed();
		}
		else
		{
			bOn = bDown && bPressStartedOver;
		}
		break;

	case EWidgetAnimInteractionState::Focused:
		bOn = WidgetAnimInputPrivate::IsFocused(Widget);
		break;

	default:
		bOn = bHovered || WidgetAnimInputPrivate::IsFocused(Widget);
		break;
	}

	OutValue = WidgetAnimValue::FromFloat(bOn ? 1.0 : 0.0);
	return true;
}

FString FWidgetAnimInput_Interaction::GetSummary() const
{
	switch (State)
	{
	case EWidgetAnimInteractionState::Hovered:  return TEXT("Hovered");
	case EWidgetAnimInteractionState::Pressed:  return TEXT("Pressed");
	case EWidgetAnimInteractionState::Focused:  return TEXT("Focused");
	default:                                    return TEXT("Hovered Or Focused");
	}
}

// =====================================================================================================
// Drag
// =====================================================================================================

bool FWidgetAnimInput_Drag::Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue)
{
	UWidget* Widget = Context.MeasuredWidget;
	const bool bDown = Context.bHasPointer && WidgetAnimInput::IsMouseButtonDown(Button);

	if (bDown && !bWasDown)
	{
		// Only a press that starts over the widget drags it; one that starts elsewhere and slides over it doesn't.
		if (WidgetAnimInput::IsPointerOver(Widget, Context.PointerPosition))
		{
			bDragging = true;
			bPastDeadZone = false;
			PressPosition = Context.PointerPosition;
			if (OnRelease != EWidgetAnimDragRelease::Accumulate)
			{
				Kept = FVector2D::ZeroVector;
			}
		}
	}
	else if (!bDown && bDragging)
	{
		bDragging = false;
		switch (OnRelease)
		{
		case EWidgetAnimDragRelease::Accumulate:
			Kept = Current;
			break;

		case EWidgetAnimDragRelease::Stay:
			// Shown until the next drag gets past its dead zone, which starts again from 0.
			Kept = FVector2D::ZeroVector;
			break;

		default:
			Kept = FVector2D::ZeroVector;
			Current = FVector2D::ZeroVector;
			break;
		}
	}
	bWasDown = bDown;

	if (bDragging)
	{
		// In the widget's own units, as laid out: a widget that follows the drag doesn't change how far it has gone.
		const FVector2f Offset = WidgetAnimInput::ToUnanimatedLocalVector(Widget, Context.PointerPosition - PressPosition);
		if (!bPastDeadZone && Offset.Size() >= DeadZone)
		{
			bPastDeadZone = true;
		}
		if (bPastDeadZone)
		{
			Current = Kept + FVector2D(Offset.X, Offset.Y);
		}
	}

	OutValue = WidgetAnimInput::FromAxes(Output, Current * Scale);
	return true;
}

EWidgetAnimValueKind FWidgetAnimInput_Drag::GetValueKind() const
{
	return WidgetAnimInput::KindOfAxes(Output);
}

FString FWidgetAnimInput_Drag::GetSummary() const
{
	return TEXT("Drag");
}

// =====================================================================================================
// Scroll
// =====================================================================================================

bool FWidgetAnimInput_Scroll::Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue)
{
	// Found once per widget measured on, and again only if the one found goes away.
	if (!bSearched || SearchedFrom.Get() != Context.MeasuredWidget || ScrollBox.IsStale())
	{
		ScrollBox = WidgetAnimInput::FindScrollBox(Context.MeasuredWidget);
		SearchedFrom = Context.MeasuredWidget;
		bSearched = true;
		bHasLastOffset = false;
	}

	UScrollBox* Box = ScrollBox.Get();
	if (!Box)
	{
		return false;
	}

	const float Offset = Box->GetScrollOffset();
	double Value = 0.0;

	switch (Measure)
	{
	case EWidgetAnimScrollMeasure::Offset:
		Value = Offset;
		break;

	case EWidgetAnimScrollMeasure::Speed:
		Value = bHasLastOffset && Context.DeltaTime > UE_KINDA_SMALL_NUMBER ? (Offset - LastOffset) / Context.DeltaTime : 0.0;
		break;

	case EWidgetAnimScrollMeasure::PositionInView:
	{
		// The widget the track animates - or, when that is the Scroll Box itself, the widget playing the animation -
		// against the box's visible area, both as laid out, so the widget's own animation doesn't move where it is measured.
		const UWidget* Item = Context.MeasuredWidget != Box ? Context.MeasuredWidget : Context.HostWidget;
		FSlateRect ItemRect;
		FSlateRect ViewRect;
		if (!WidgetAnimInputPrivate::GetLayoutRect(Item, ItemRect) || !WidgetAnimInputPrivate::GetLayoutRect(Box, ViewRect))
		{
			return false;
		}

		const bool bVertical = Box->GetOrientation() == Orient_Vertical;
		const double ItemMin = bVertical ? ItemRect.Top : ItemRect.Left;
		const double ItemMax = bVertical ? ItemRect.Bottom : ItemRect.Right;
		const double ViewMin = bVertical ? ViewRect.Top : ViewRect.Left;
		const double ViewMax = bVertical ? ViewRect.Bottom : ViewRect.Right;

		// 0 when its leading edge meets the view's far edge, 1 when its trailing edge meets the near one: the whole
		// journey across the view, however tall the item and the view are.
		const double Journey = (ViewMax - ViewMin) + (ItemMax - ItemMin);
		if (Journey <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		Value = (ViewMax - ItemMin) / Journey;
		break;
	}

	default:
	{
		const float End = Box->GetScrollOffsetOfEnd();
		Value = End > UE_KINDA_SMALL_NUMBER ? Offset / End : 0.0;
		break;
	}
	}

	LastOffset = Offset;
	bHasLastOffset = true;

	OutValue = WidgetAnimValue::FromFloat(Value);
	return true;
}

FString FWidgetAnimInput_Scroll::GetSummary() const
{
	switch (Measure)
	{
	case EWidgetAnimScrollMeasure::Offset:         return TEXT("Scroll Offset");
	case EWidgetAnimScrollMeasure::Speed:          return TEXT("Scroll Speed");
	case EWidgetAnimScrollMeasure::PositionInView: return TEXT("Position In Scroll View");
	default:                                       return TEXT("Scroll Progress");
	}
}
