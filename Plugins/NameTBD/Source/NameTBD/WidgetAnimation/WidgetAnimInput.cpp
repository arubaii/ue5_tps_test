// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimInput.h"

#include "CoreGlobals.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/Widget.h"
#include "Components/WidgetComponent.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Geometry.h"
#include "Layout/WidgetPath.h"
#include "NameTBD/Widgets/ShapeBox.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

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

	/** The widget around Widget in its tree: its parent, or - at the top of a User Widget's tree - that User Widget. */
	const UWidget* GetOuterWidget(const UWidget* Widget)
	{
		const UWidget* Parent = Widget->GetParent();
		return Parent ? Parent : Widget->GetTypedOuter<UUserWidget>();
	}

	/**
	 * Whether Slate's hit test can reach Widget - itself, or something inside it: neither it nor anything around it is Not
	 * Hit-Testable (Self & All Children), Collapsed or Hidden. Where it can't, a press or hover on it is worked out from its
	 * widgets' own settings instead (IsOverOwnParts).
	 */
	bool CanSlateHit(const UWidget* Widget)
	{
		static constexpr int32 MaxDepth = 64;
		int32 Depth = 0;
		for (const UWidget* Search = Widget; Search && Depth < MaxDepth; Search = GetOuterWidget(Search), ++Depth)
		{
			const ESlateVisibility Setting = Search->GetVisibility();
			if (Setting == ESlateVisibility::HitTestInvisible || Setting == ESlateVisibility::Collapsed || Setting == ESlateVisibility::Hidden)
			{
				return false;
			}
		}
		return true;
	}

	/** Whether Widget is drawn: neither it nor anything around it is Collapsed or Hidden. */
	bool IsShownInTree(const UWidget* Widget)
	{
		static constexpr int32 MaxDepth = 64;
		int32 Depth = 0;
		for (const UWidget* Search = Widget; Search && Depth < MaxDepth; Search = GetOuterWidget(Search), ++Depth)
		{
			const ESlateVisibility Setting = Search->GetVisibility();
			if (Setting == ESlateVisibility::Collapsed || Setting == ESlateVisibility::Hidden)
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * Point in the space Widget is laid out in: Point itself - or, inside a Shape Box drawing its content warped (offscreen,
	 * into a window of its own), where Point lands in that content. False when it misses the Shape Box's drawn shape.
	 */
	bool ToLayoutSpace(const UWidget* Widget, const FVector2f& Point, FVector2f& OutPoint)
	{
		OutPoint = Point;
		static constexpr int32 MaxDepth = 64;
		int32 Depth = 0;
		for (const UWidget* Search = GetOuterWidget(Widget); Search && Depth < MaxDepth; Search = GetOuterWidget(Search), ++Depth)
		{
			if (const UShapeBox* Box = Cast<UShapeBox>(Search))
			{
				FVector2D Content;
				bool bOnShape = false;
				if (Box->MapDesktopToContent(FVector2D(Point), Content, bOnShape))
				{
					OutPoint = FVector2f(Content);
					return bOnShape;
				}
				return true;
			}
		}
		return true;
	}

	/** Whether Point is on Widget as it is drawn: its laid-out box, with every render transform on it and around it. */
	bool IsOnDrawnBox(const UWidget* Widget, const FVector2f& Point)
	{
		const TSharedPtr<SWidget> Slate = Widget->GetCachedWidget();
		FVector2f InLayout;
		if (!Slate.IsValid() || !ToLayoutSpace(Widget, Point, InLayout))
		{
			return false;
		}
		const FGeometry& Geometry = Slate->GetTickSpaceGeometry();
		return Geometry.GetLocalSize().SizeSquared() > UE_KINDA_SMALL_NUMBER && Geometry.IsUnderLocation(InLayout);
	}

	/**
	 * Whether Point is on a part of Widget a press can land on, by its widgets' own settings: Widget itself when it is Visible,
	 * and every widget inside it that is - into nested User Widgets - wherever each is drawn, whatever is set around it.
	 * bOutAnyVisible: whether it has any such part at all.
	 */
	bool IsOnVisiblePart(const UWidget* Widget, const FVector2f& Point, bool& bOutAnyVisible, int32 Depth = 0)
	{
		static constexpr int32 MaxDepth = 64;
		if (!Widget || Depth > MaxDepth)
		{
			return false;
		}
		const ESlateVisibility Setting = Widget->GetVisibility();
		if (Setting == ESlateVisibility::Collapsed || Setting == ESlateVisibility::Hidden)
		{
			return false;
		}
		if (Setting == ESlateVisibility::Visible)
		{
			bOutAnyVisible = true;
			if (IsOnDrawnBox(Widget, Point))
			{
				return true;
			}
		}
		if (const UUserWidget* AsUserWidget = Cast<UUserWidget>(Widget))
		{
			return AsUserWidget->WidgetTree && IsOnVisiblePart(AsUserWidget->WidgetTree->RootWidget, Point, bOutAnyVisible, Depth + 1);
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (IsOnVisiblePart(Panel->GetChildAt(Index), Point, bOutAnyVisible, Depth + 1))
				{
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * Whether Widget is in what Slate hit-tests at Point - drawn on top there, through its children - whatever has the mouse
	 * captured. The widgets hit are worked out once per frame and point, however many widgets ask.
	 */
	bool IsUnderPoint(const UWidget* Widget, const FVector2f& Point)
	{
		const TSharedPtr<SWidget> Slate = Widget ? Widget->GetCachedWidget() : nullptr;
		if (!Slate.IsValid() || !FSlateApplication::IsInitialized())
		{
			return false;
		}
		static TArray<TWeakPtr<SWidget>> Hit;
		static uint64 HitFrame = TNumericLimits<uint64>::Max();
		static FVector2f HitPoint = FVector2f::ZeroVector;
		if (HitFrame != GFrameCounter || !HitPoint.Equals(Point, 0.01f))
		{
			HitFrame = GFrameCounter;
			HitPoint = Point;
			Hit.Reset();
			FSlateApplication& SlateApp = FSlateApplication::Get();
			const FWidgetPath Path = SlateApp.LocateWindowUnderMouse(FVector2D(Point), SlateApp.GetInteractiveTopLevelWindows(), /*bIgnoreEnabledStatus=*/true);
			for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
			{
				Hit.Add(Path.Widgets[Index].Widget);
			}
		}
		const SWidget* Wanted = Slate.Get();
		return Hit.ContainsByPredicate([Wanted](const TWeakPtr<SWidget>& Candidate) { return Candidate.Pin().Get() == Wanted; });
	}

	/** What finds a widget's world pointer - see WidgetAnimInput::SetWorldPointerResolver. Game thread only. */
	WidgetAnimInput::FWorldPointerResolver& GetWorldPointerResolver()
	{
		static WidgetAnimInput::FWorldPointerResolver Resolver;
		return Resolver;
	}

	/**
	 * IsPointerOver from the context's pointer. A world pointer that has left its surface is over nothing there, whatever was
	 * under the place it was last: its position is only kept so Keep Following holds still.
	 */
	bool IsPointerOverIn(const FWidgetAnimInputContext& Context, const UWidget* Widget)
	{
		return Context.bHasPointer && (!Context.bWorldPointer || Context.WorldPointer.bOver)
			&& WidgetAnimInput::IsPointerOver(Widget, Context.PointerPosition);
	}

	bool IsFocused(const UWidget* Widget)
	{
		return Widget && (Widget->HasAnyUserFocus() || Widget->HasFocusedDescendants());
	}

	/**
	 * Whether Point is over a widget Slate's hit test can't reach (see CanSlateHit): on one of its Visible parts, by the widgets'
	 * own settings (IsOnVisiblePart) - so a press on an Image inside a widget set Not Hit-Testable is on both, and one beside
	 * them on neither. A widget with no Visible part at all - a decoration let through, a measured box - answers by its
	 * laid-out rectangle, as it always has.
	 */
	bool IsOverOwnParts(const UWidget* Widget, const FVector2f& Point)
	{
		if (!IsShownInTree(Widget))
		{
			return false;
		}
		bool bAnyVisible = false;
		if (IsOnVisiblePart(Widget, Point, bAnyVisible))
		{
			return true;
		}
		if (bAnyVisible)
		{
			return false;
		}
		FVector2f InLayout;
		FVector2f Local;
		FVector2f Size;
		return ToLayoutSpace(Widget, Point, InLayout) && WidgetAnimInput::ToUnanimatedLocal(Widget, InLayout, Local, Size)
			&& Local.X >= 0.f && Local.Y >= 0.f && Local.X <= Size.X && Local.Y <= Size.Y;
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

	// Slate's hover knows what is drawn on top of what, so a card under a popup isn't hovered through it. A widget Slate
	// can't hit - set, or inside something set, Not Hit-Testable (Self & All Children) - is never hovered in Slate's sense:
	// its own Visible parts answer instead.
	if (WidgetAnimInputPrivate::CanSlateHit(Widget))
	{
		if (Widget->IsHovered())
		{
			return true;
		}
		// Something has the mouse captured - the game viewport takes it on any press nothing in the UI handles, such as one on
		// an Image - and Slate then hovers nothing else: what is hit at the pointer answers instead, until it lets go.
		return FSlateApplication::IsInitialized() && FSlateApplication::Get().HasAnyMouseCaptor()
			&& WidgetAnimInputPrivate::IsUnderPoint(Widget, PointerPosition);
	}
	return WidgetAnimInputPrivate::IsOverOwnParts(Widget, PointerPosition);
}

bool WidgetAnimInput::IsPointUnder(const UWidget* Widget, const FVector2f& Point)
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
	// What Slate hit-tests there, when it can reach the widget; else the widget's own Visible parts - see IsPointerOver.
	if (WidgetAnimInputPrivate::CanSlateHit(Widget))
	{
		return WidgetAnimInputPrivate::IsUnderPoint(Widget, Point);
	}
	return WidgetAnimInputPrivate::IsOverOwnParts(Widget, Point);
}

bool WidgetAnimInput::IsPointUnder(const FWidgetAnimInputContext& Context, const UWidget* Widget, const FVector2f& Point)
{
	if (!Context.bWorldPointer)
	{
		return IsPointUnder(Widget, Point);
	}
	if (!Widget)
	{
		return false;
	}
	const ESlateVisibility Visibility = Widget->GetVisibility();
	if (Visibility == ESlateVisibility::Collapsed || Visibility == ESlateVisibility::Hidden)
	{
		return false;
	}
	if (!WidgetAnimInputPrivate::CanSlateHit(Widget))
	{
		return WidgetAnimInputPrivate::IsOverOwnParts(Widget, Point);
	}

	// The surface's own hit test: its window isn't one of the desktop's, so LocateWindowUnderMouse can't see into it.
	UWidgetComponent* Surface = Context.WorldPointer.Surface.Get();
	const TSharedPtr<SWidget> Slate = Widget->GetCachedWidget();
	if (!Surface || !Slate.IsValid())
	{
		return false;
	}
	const TArray<FWidgetAndPointer> Path = Surface->GetHitWidgetPath(FVector2D(Point), /*bIgnoreEnabledStatus=*/true);
	const SWidget* Wanted = Slate.Get();
	return Path.ContainsByPredicate([Wanted](const FWidgetAndPointer& Entry) { return &Entry.Widget.Get() == Wanted; });
}

void WidgetAnimInput::SetWorldPointerResolver(FWorldPointerResolver InResolver)
{
	WidgetAnimInputPrivate::GetWorldPointerResolver() = MoveTemp(InResolver);
}

void WidgetAnimInput::ReadPointer(FWidgetAnimInputContext& Context)
{
	Context.bWorldPointer = false;
	const FWorldPointerResolver& Resolver = WidgetAnimInputPrivate::GetWorldPointerResolver();
	if (Resolver && Context.HostWidget && Resolver(Context.HostWidget, Context.WorldPointer))
	{
		Context.bWorldPointer = true;
		Context.PointerPosition = Context.WorldPointer.Position;
		Context.bHasPointer = Context.WorldPointer.bHasPosition;
		return;
	}
	Context.WorldPointer = FWorldPointer();
	Context.bHasPointer = GetPointerPosition(Context.PointerPosition);
}

bool WidgetAnimInput::IsButtonDown(const FWidgetAnimInputContext& Context, const FKey& Button)
{
	return Context.bWorldPointer ? Context.WorldPointer.IsDown(Button) : IsMouseButtonDown(Button);
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

// =====================================================================================================
// Counted events
// =====================================================================================================

namespace WidgetAnimInputPrivate
{
	/** What the counter has seen. Shared by every reader; only the game thread touches it. */
	struct FEventCounts
	{
		TMap<FKey, uint32> KeyPresses;
		uint32 AcceptPresses = 0;
		uint32 BackPresses = 0;
		WidgetAnimInput::FPointerEventCount LeftPresses;
		WidgetAnimInput::FPointerEventCount DoubleClicks;
		WidgetAnimInput::FPointerEventCount RightPresses;
		WidgetAnimInput::FPointerEventCount RightReleases;
	};

	FEventCounts& GetEventCounts()
	{
		static FEventCounts Counts;
		return Counts;
	}

	/** Sees input before any widget does - and lets it all through. */
	class FEventCounter : public IInputProcessor
	{
	public:

		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
		{
		}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			if (InKeyEvent.IsRepeat())
			{
				return false;
			}
			FEventCounts& Counts = GetEventCounts();
			const FKey Key = InKeyEvent.GetKey();
			++Counts.KeyPresses.FindOrAdd(Key);

			// Confirm and cancel as the platform has them (a swapped layout's included); a gamepad face button the navigation
			// config says nothing about falls back to the usual bottom / right.
			const EUINavigationAction Action = SlateApp.GetNavigationActionFromKey(InKeyEvent);
			if (Action == EUINavigationAction::Accept || (Action == EUINavigationAction::Invalid && Key == EKeys::Gamepad_FaceButton_Bottom))
			{
				++Counts.AcceptPresses;
			}
			else if (Action == EUINavigationAction::Back || (Action == EUINavigationAction::Invalid && Key == EKeys::Gamepad_FaceButton_Right))
			{
				++Counts.BackPresses;
			}
			return false;
		}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
			{
				Count(GetEventCounts().RightPresses, MouseEvent);
			}
			else if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton || MouseEvent.IsTouchEvent())
			{
				Count(GetEventCounts().LeftPresses, MouseEvent);
			}
			return false;
		}

		virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
			{
				Count(GetEventCounts().RightReleases, MouseEvent);
			}
			return false;
		}

		virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				// A double-click's second press comes as this event alone: a press too.
				Count(GetEventCounts().LeftPresses, MouseEvent);
				Count(GetEventCounts().DoubleClicks, MouseEvent);
			}
			return false;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("WidgetAnimInputEvents"); }

	private:

		static void Count(WidgetAnimInput::FPointerEventCount& Counted, const FPointerEvent& MouseEvent)
		{
			++Counted.Count;
			const FVector2D Position = MouseEvent.GetScreenSpacePosition();
			Counted.Position = FVector2f(static_cast<float>(Position.X), static_cast<float>(Position.Y));
		}
	};

	TSharedPtr<FEventCounter>& GetEventCounter()
	{
		static TSharedPtr<FEventCounter> Counter;
		return Counter;
	}
}

void WidgetAnimInput::StartCountingEvents()
{
	TSharedPtr<WidgetAnimInputPrivate::FEventCounter>& Counter = WidgetAnimInputPrivate::GetEventCounter();
	if (Counter.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	Counter = MakeShared<WidgetAnimInputPrivate::FEventCounter>();
	FSlateApplication::Get().RegisterInputPreProcessor(Counter);
}

void WidgetAnimInput::StopCountingEvents()
{
	TSharedPtr<WidgetAnimInputPrivate::FEventCounter>& Counter = WidgetAnimInputPrivate::GetEventCounter();
	if (Counter.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Counter);
	}
	Counter.Reset();
}

uint32 WidgetAnimInput::GetKeyPressCount(const FKey& Key)
{
	return WidgetAnimInputPrivate::GetEventCounts().KeyPresses.FindRef(Key);
}

uint32 WidgetAnimInput::GetNavigationPressCount(bool bAccept)
{
	const WidgetAnimInputPrivate::FEventCounts& Counts = WidgetAnimInputPrivate::GetEventCounts();
	return bAccept ? Counts.AcceptPresses : Counts.BackPresses;
}

WidgetAnimInput::FPointerEventCount WidgetAnimInput::GetLeftPressCount()
{
	return WidgetAnimInputPrivate::GetEventCounts().LeftPresses;
}

WidgetAnimInput::FPointerEventCount WidgetAnimInput::GetDoubleClickCount()
{
	return WidgetAnimInputPrivate::GetEventCounts().DoubleClicks;
}

WidgetAnimInput::FPointerEventCount WidgetAnimInput::GetRightPressCount()
{
	return WidgetAnimInputPrivate::GetEventCounts().RightPresses;
}

WidgetAnimInput::FPointerEventCount WidgetAnimInput::GetRightReleaseCount()
{
	return WidgetAnimInputPrivate::GetEventCounts().RightReleases;
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

	const bool bMeasure = WhenAway == EWidgetAnimPointerAway::KeepFollowing || WidgetAnimInputPrivate::IsPointerOverIn(Context, Context.MeasuredWidget);

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
	const bool bHovered = WidgetAnimInputPrivate::IsPointerOverIn(Context, Widget);

	// Pressed is watched every frame whatever State is, so switching State at runtime doesn't start mid-press. The presses
	// are counted as Slate saw them (WidgetAnimInput::StartCountingEvents), where they were made: so a press over the widget is
	// seen however short it was - let go again before this look - and wherever the pointer has gone since. On a widget drawn
	// into the world, its surface's pointer counted them instead.
	const bool bDown = WidgetAnimInput::IsButtonDown(Context, EKeys::LeftMouseButton);
	if (!Context.bWorldPointer)
	{
		WidgetAnimInput::StartCountingEvents();
	}
	const WidgetAnimInput::FPointerEventCount Presses = Context.bWorldPointer ? Context.WorldPointer.LeftPresses : WidgetAnimInput::GetLeftPressCount();
	const bool bNewPress = bHasPressCount && Presses.Count != LastPressCount;
	LastPressCount = Presses.Count;
	bHasPressCount = true;
	if (bNewPress)
	{
		// Where the press was made - not hover now, which a press nothing handled takes away (the game viewport captures it).
		bPressStartedOver = WidgetAnimInput::IsPointUnder(Context, Widget, Presses.Position);
	}
	else if (bDown && !bWasDown)
	{
		// Counting only just began: the press as it is seen now.
		bPressStartedOver = bHovered;
	}
	// Down over the widget - or pressed over it and let go since the last look: on for this one look, so it is never missed.
	const bool bPressedOver = bPressStartedOver && (bDown || bNewPress);
	if (!bDown)
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
		// A Button knows its own pressed state, keyboard and gamepad presses included - but lets go of it the moment it loses
		// focus, which a press action that opens something does within the press: the mouse press over it counts too, so a
		// single click is seen as one (rather than only a double-click's second press, made once focus had moved).
		if (const UButton* Button = Cast<UButton>(Widget))
		{
			bOn = Button->IsPressed() || (bPressedOver && Button->GetIsEnabled());
		}
		else
		{
			bOn = bPressedOver;
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
	const bool bDown = Context.bHasPointer && WidgetAnimInput::IsButtonDown(Context, Button);

	if (bDown && !bWasDown)
	{
		// Only a press that starts over the widget drags it; one that starts elsewhere and slides over it doesn't.
		if (WidgetAnimInputPrivate::IsPointerOverIn(Context, Widget))
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
