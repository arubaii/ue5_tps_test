// Copyright AR GameSystems 2026. All rights reserved.

#include "NotificationHost.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SScrollBox.h"
#include "NotificationEntryWidget.h"
#include "NotificationSettings.h"

#define LOCTEXT_NAMESPACE "NotificationHost"

namespace NotificationHostPrivate
{
	FVector2f ToVector2f(const FVector2D& Value)
	{
		return FVector2f(static_cast<float>(Value.X), static_cast<float>(Value.Y));
	}

	const FSlateBrush* PillBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White);
		return &Brush;
	}

	/** How far a drag must go before it is a swipe rather than a click, in Slate units. */
	constexpr float DragThreshold = 8.f;

	/** The share of its width a swipe must travel to close a notification. */
	constexpr float SwipeShare = 0.33f;
}

// =====================================================================================================
// SNotificationItemFrame
// =====================================================================================================

void SNotificationItemFrame::Construct(const FArguments& InArgs, const TSharedRef<SNotificationStack>& InStack, int32 InId)
{
	WeakStack = InStack;
	Id = InId;
	SetCanTick(false);
	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SNotificationItemFrame::SetContent(const TSharedRef<SWidget>& InContent)
{
	ChildSlot
	[
		InContent
	];
}

FReply SNotificationItemFrame::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const TSharedPtr<SNotificationStack> Stack = WeakStack.Pin();
	if (!Stack.IsValid() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	Stack->HandlePress(Id, MouseEvent.GetScreenSpacePosition());
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SNotificationItemFrame::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	if (const TSharedPtr<SNotificationStack> Stack = WeakStack.Pin())
	{
		Stack->HandleMove(Id, MouseEvent.GetScreenSpacePosition());
	}
	return FReply::Handled();
}

FReply SNotificationItemFrame::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	if (const TSharedPtr<SNotificationStack> Stack = WeakStack.Pin())
	{
		Stack->HandleRelease(Id, MouseEvent.GetScreenSpacePosition());
	}
	return FReply::Handled().ReleaseMouseCapture();
}

void SNotificationItemFrame::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (const TSharedPtr<SNotificationStack> Stack = WeakStack.Pin())
	{
		Stack->HandleItemHover(Id, true);
	}
}

void SNotificationItemFrame::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	if (const TSharedPtr<SNotificationStack> Stack = WeakStack.Pin())
	{
		Stack->HandleItemHover(Id, false);
	}
}

void SNotificationItemFrame::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
	if (const TSharedPtr<SNotificationStack> Stack = WeakStack.Pin())
	{
		Stack->HandleCancel(Id);
	}
}

// =====================================================================================================
// SNotificationStack
// =====================================================================================================

SNotificationStack::SNotificationStack()
	: Children(this)
{
}

void SNotificationStack::Construct(const FArguments& InArgs)
{
	OnItemGone = InArgs._OnItemGone;
	OnItemClicked = InArgs._OnItemClicked;
	OnItemSwiped = InArgs._OnItemSwiped;
	OnHoverChanged = InArgs._OnHoverChanged;
	SetCanTick(false);
	// The panel never takes the mouse itself: only the notifications on it do.
	SetVisibility(EVisibility::SelfHitTestInvisible);
	Spread.Snap(0.f);
}

void SNotificationStack::SetStackSettings(const FNotificationStackSettings& InSettings)
{
	StackSettings = InSettings;
	const EVisibility FrameVisibility = GetFrameVisibility();
	for (FItem& Item : Items)
	{
		Item.Frame->SetVisibility(FrameVisibility);
	}
	Invalidate(EInvalidateWidgetReason::Layout);
	Wake();
}

EVisibility SNotificationStack::GetFrameVisibility() const
{
	if (!StackSettings.bInteractive)
	{
		return EVisibility::HitTestInvisible;
	}
	return StackSettings.bTakesClicks ? EVisibility::Visible : EVisibility::SelfHitTestInvisible;
}

int32 SNotificationStack::FindIndex(int32 Id) const
{
	return Items.IndexOfByPredicate([Id](const FItem& Item) { return Item.Id == Id; });
}

void SNotificationStack::AddItem(int32 Id, const TSharedRef<SWidget>& Content, bool bCanSwipe, bool bAnimate)
{
	const bool bInstant = !bAnimate || StackSettings.Motion == EWidgetMotion::Instant;
	const int32 Existing = FindIndex(Id);
	if (Existing != INDEX_NONE)
	{
		// Back before it had finished leaving (a Hold lifted, shown again): it turns round where it is.
		FItem& Item = Items[Existing];
		Item.bLeaving = false;
		Item.bSwiped = false;
		Item.bCanSwipe = bCanSwipe;
		Item.Frame->SetContent(Content);
		Item.Drag.SetTarget(0.f, EWidgetMotion::Smooth, 0.2f, bInstant);
		Item.Presence.SetTarget(1.f, StackSettings.Motion, StackSettings.EnterDuration, bInstant);
		Wake();
		return;
	}

	FItem Item;
	Item.Id = Id;
	Item.Order = NextOrder++;
	Item.bCanSwipe = bCanSwipe;
	Item.Frame = SNew(SNotificationItemFrame, SharedThis(this), Id)[Content];
	Item.Frame->SetVisibility(GetFrameVisibility());
	Item.Presence.Snap(bInstant ? 1.f : 0.f);
	if (!bInstant)
	{
		Item.Presence.SetTarget(1.f, StackSettings.Motion, StackSettings.EnterDuration);
	}
	Item.Extent.Snap(0.f);
	Item.Drag.Snap(0.f);
	Children.Add(Item.Frame.ToSharedRef());
	Items.Add(MoveTemp(Item));
	Invalidate(EInvalidateWidgetReason::Layout);
	Wake();
}

void SNotificationStack::NotifyContentChanged(int32 Id)
{
	if (FindIndex(Id) != INDEX_NONE)
	{
		KeepAwake = FMath::Max(KeepAwake, 0.25f);
		Invalidate(EInvalidateWidgetReason::Layout);
		Wake();
	}
}

void SNotificationStack::RemoveItem(int32 Id, bool bAnimate)
{
	const int32 Index = FindIndex(Id);
	if (Index == INDEX_NONE)
	{
		return;
	}
	FItem& Item = Items[Index];
	if (!bAnimate || StackSettings.Motion == EWidgetMotion::Instant)
	{
		RemoveAt(Index);
		OnItemGone.ExecuteIfBound(Id);
		return;
	}
	Item.bLeaving = true;
	Item.Presence.SetTarget(0.f, Item.bSwiped ? EWidgetMotion::Smooth : StackSettings.Motion, StackSettings.ExitDuration);
	Wake();
}

void SNotificationStack::RemoveAt(int32 Index)
{
	const int32 Id = Items[Index].Id;
	Children.RemoveAt(Index);
	Items.RemoveAt(Index);
	if (PressedId == Id)
	{
		PressedId = 0;
		bDragging = false;
	}
	if (HoveredId == Id)
	{
		SetHovered(0);
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SNotificationStack::ClearItems()
{
	Children.Empty();
	Items.Empty();
	PressedId = 0;
	bDragging = false;
	SetHovered(0);
	Invalidate(EInvalidateWidgetReason::Layout);
}

bool SNotificationStack::HasItem(int32 Id) const
{
	const int32 Index = FindIndex(Id);
	return Index != INDEX_NONE && !Items[Index].bLeaving;
}

bool SNotificationStack::ContainsItem(int32 Id) const
{
	return FindIndex(Id) != INDEX_NONE;
}

int32 SNotificationStack::GetItemCount() const
{
	int32 Count = 0;
	for (const FItem& Item : Items)
	{
		Count += Item.bLeaving ? 0 : 1;
	}
	return Count;
}

void SNotificationStack::SetOverflowCount(int32 InCount)
{
	if (OverflowCount != InCount)
	{
		OverflowCount = InCount;
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SNotificationStack::SetHovered(int32 Id)
{
	const bool bBefore = IsHoveringItem();
	HoveredId = Id;
	const bool bAfter = IsHoveringItem();
	if (bBefore != bAfter)
	{
		OnHoverChanged.ExecuteIfBound(bAfter);
		Wake();
	}
}

void SNotificationStack::HandleItemHover(int32 Id, bool bHovered)
{
	if (bHovered)
	{
		SetHovered(Id);
	}
	else if (HoveredId == Id)
	{
		SetHovered(0);
	}
}

void SNotificationStack::HandlePress(int32 Id, const FVector2D& ScreenPosition)
{
	const bool bBefore = IsHoveringItem();
	PressedId = Id;
	PressStart = ScreenPosition;
	bDragging = false;
	if (!bBefore)
	{
		OnHoverChanged.ExecuteIfBound(true);
	}
}

void SNotificationStack::HandleMove(int32 Id, const FVector2D& ScreenPosition)
{
	if (Id != PressedId || !StackSettings.bSwipeToDismiss)
	{
		return;
	}
	const int32 Index = FindIndex(Id);
	if (Index == INDEX_NONE || !Items[Index].bCanSwipe || Items[Index].bLeaving)
	{
		return;
	}
	const FGeometry& Geometry = GetTickSpaceGeometry();
	const float Delta = static_cast<float>(Geometry.AbsoluteToLocal(ScreenPosition).X - Geometry.AbsoluteToLocal(PressStart).X);
	if (!bDragging && FMath::Abs(Delta) > NotificationHostPrivate::DragThreshold)
	{
		bDragging = true;
	}
	if (bDragging)
	{
		Items[Index].Drag.Snap(Delta);
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SNotificationStack::HandleRelease(int32 Id, const FVector2D& ScreenPosition)
{
	if (Id != PressedId)
	{
		return;
	}
	const bool bWasDragging = bDragging;
	PressedId = 0;
	bDragging = false;
	if (!IsHoveringItem())
	{
		OnHoverChanged.ExecuteIfBound(false);
	}

	const int32 Index = FindIndex(Id);
	if (Index == INDEX_NONE)
	{
		return;
	}
	FItem& Item = Items[Index];
	if (!bWasDragging)
	{
		OnItemClicked.ExecuteIfBound(Id);
		return;
	}

	const float Offset = Item.Drag.Value;
	const float Width = FMath::Max(Item.LastWidth, 1.f);
	if (FMath::Abs(Offset) > Width * NotificationHostPrivate::SwipeShare)
	{
		// Flung away the way it was dragged; the host is told, and the leaving fades it as it goes.
		Item.bSwiped = true;
		Item.Drag.SetTarget(FMath::Sign(Offset) * (Width + 48.f), EWidgetMotion::Smooth, 0.22f);
		Wake();
		OnItemSwiped.ExecuteIfBound(Id);
		return;
	}
	Item.Drag.SetTarget(0.f, EWidgetMotion::Bouncy, 0.f, StackSettings.bReduceMotion);
	Wake();
}

void SNotificationStack::HandleCancel(int32 Id)
{
	if (Id != PressedId)
	{
		return;
	}
	PressedId = 0;
	bDragging = false;
	const int32 Index = FindIndex(Id);
	if (Index != INDEX_NONE && !Items[Index].bSwiped)
	{
		Items[Index].Drag.SetTarget(0.f, EWidgetMotion::Smooth, 0.2f);
	}
	if (!IsHoveringItem())
	{
		OnHoverChanged.ExecuteIfBound(false);
	}
	Wake();
}

void SNotificationStack::Wake()
{
	if (!AnimateTimer.IsValid())
	{
		AnimateTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SNotificationStack::Animate));
	}
}

float SNotificationStack::GetMainExtent(const FVector2D& Desired) const
{
	return static_cast<float>(Desired.Y);
}

EActiveTimerReturnType SNotificationStack::Animate(double InCurrentTime, float InDeltaTime)
{
	const float Delta = FMath::Min(InDeltaTime, 0.1f);
	const EWidgetMotion SizeMotion = StackSettings.bReduceMotion ? EWidgetMotion::Instant
		: (StackSettings.Motion == EWidgetMotion::Bouncy ? EWidgetMotion::Smooth : StackSettings.Motion);

	bool bMoving = false;
	TArray<int32> Gone;
	for (int32 Index = Items.Num() - 1; Index >= 0; --Index)
	{
		FItem& Item = Items[Index];
		bMoving |= Item.Presence.Advance(Delta);
		bMoving |= Item.Drag.Advance(Delta);

		const float Desired = GetMainExtent(Item.Frame->GetDesiredSize());
		if (!Item.bMeasured)
		{
			if (Desired > 0.5f)
			{
				Item.Extent.Snap(Desired);
				Item.bMeasured = true;
			}
			bMoving = true;
		}
		else if (!FMath::IsNearlyEqual(Item.Extent.Target, Desired, 0.5f))
		{
			Item.Extent.SetTarget(Desired, SizeMotion, 0.25f);
		}
		bMoving |= Item.Extent.Advance(Delta);

		if (Item.bLeaving && Item.Presence.Value <= 0.001f && !Item.Presence.IsMoving())
		{
			Gone.Add(Item.Id);
			RemoveAt(Index);
		}
	}

	if (StackSettings.StackLayout == ENotificationStackLayout::Deck)
	{
		Spread.SetTarget(IsHoveringItem() ? 1.f : 0.f, StackSettings.Motion, 0.3f, StackSettings.bReduceMotion);
		bMoving |= Spread.Advance(Delta);
	}

	KeepAwake = FMath::Max(KeepAwake - Delta, 0.f);
	Invalidate(EInvalidateWidgetReason::Layout);

	// Told last: the host may add or remove items in answer.
	for (const int32 GoneId : Gone)
	{
		OnItemGone.ExecuteIfBound(GoneId);
	}

	if (bMoving || KeepAwake > 0.f)
	{
		return EActiveTimerReturnType::Continue;
	}
	AnimateTimer.Reset();
	return EActiveTimerReturnType::Stop;
}

FVector2f SNotificationStack::GetItemSize(const FItem& Item, const FVector2f& LocalSize) const
{
	const FVector2D Desired = Item.Frame->GetDesiredSize();
	float Width = static_cast<float>(Desired.X);
	if (StackSettings.ItemWidth > 0.f)
	{
		Width = StackSettings.ItemWidth;
	}
	else if (StackSettings.ItemAlignment == HAlign_Fill)
	{
		Width = LocalSize.X;
	}
	if (LocalSize.X > 0.f)
	{
		Width = FMath::Min(Width, LocalSize.X);
	}
	const float Height = Item.bMeasured ? Item.Extent.Value : GetMainExtent(Desired);
	return FVector2f(FMath::Max(Width, 0.f), FMath::Max(Height, 0.f));
}

FVector2f SNotificationStack::GetSlideOffset(const FVector2f& Size) const
{
	FVector2f Direction(0.f, -1.f);
	switch (StackSettings.SlideFrom)
	{
	case ENotificationSlideFrom::Left: Direction = FVector2f(-1.f, 0.f); break;
	case ENotificationSlideFrom::Right: Direction = FVector2f(1.f, 0.f); break;
	case ENotificationSlideFrom::Top: Direction = FVector2f(0.f, -1.f); break;
	case ENotificationSlideFrom::Bottom: Direction = FVector2f(0.f, 1.f); break;
	default:
		if (StackSettings.ItemAlignment == HAlign_Right)
		{
			Direction = FVector2f(1.f, 0.f);
		}
		else if (StackSettings.ItemAlignment == HAlign_Left)
		{
			Direction = FVector2f(-1.f, 0.f);
		}
		else
		{
			Direction = StackSettings.Growth == ENotificationStackGrowth::Down ? FVector2f(0.f, -1.f) : FVector2f(0.f, 1.f);
		}
		break;
	}
	const bool bAcross = Direction.X != 0.f;
	const float Distance = StackSettings.SlideDistance > 0.f ? StackSettings.SlideDistance : (bAcross ? Size.X + 24.f : Size.Y + 16.f);
	return Direction * Distance;
}

void SNotificationStack::ApplyTransition(ENotificationTransition Transition, float P, FPose& Pose) const
{
	const float Shown = FMath::Clamp(P, 0.f, 1.f);
	if (StackSettings.bReduceMotion)
	{
		Transition = ENotificationTransition::Fade;
	}
	switch (Transition)
	{
	case ENotificationTransition::Slide:
		Pose.Position += GetSlideOffset(Pose.Size) * (1.f - P);
		break;
	case ENotificationTransition::SlideFade:
		Pose.Position += GetSlideOffset(Pose.Size) * (1.f - P);
		Pose.Opacity *= Shown;
		break;
	case ENotificationTransition::Pop:
		Pose.Scale *= 0.85f + 0.15f * P;
		Pose.Opacity *= Shown;
		break;
	case ENotificationTransition::Rise:
	{
		const float Away = StackSettings.Growth == ENotificationStackGrowth::Down ? -1.f : 1.f;
		Pose.Position.Y += Away * 14.f * (1.f - P);
		Pose.Scale *= 0.96f + 0.04f * P;
		Pose.Opacity *= Shown;
		break;
	}
	default:
		Pose.Opacity *= Shown;
		break;
	}
}

void SNotificationStack::ComputePoses(const FVector2f& LocalSize, TArray<FPose>& OutPoses, float& OutEnd) const
{
	const int32 Count = Items.Num();
	OutPoses.SetNum(Count);
	OutEnd = 0.f;
	if (Count == 0)
	{
		return;
	}

	const bool bUp = StackSettings.Growth == ENotificationStackGrowth::Up;

	// Along the stack from the edge it grows from.
	TArray<int32> DisplayOrder;
	DisplayOrder.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		DisplayOrder.Add(Index);
	}
	const bool bNewestFirst = StackSettings.bNewestFirst;
	DisplayOrder.Sort([this, bNewestFirst](int32 A, int32 B)
	{
		return bNewestFirst ? Items[A].Order > Items[B].Order : Items[A].Order < Items[B].Order;
	});

	TArray<FVector2f> Sizes;
	Sizes.SetNum(Count);
	TArray<float> ListMain;
	ListMain.SetNum(Count);
	float Cursor = 0.f;
	for (const int32 Index : DisplayOrder)
	{
		const FItem& Item = Items[Index];
		Sizes[Index] = GetItemSize(Item, LocalSize);
		const float Space = StackSettings.bReduceMotion ? 1.f : FMath::Clamp(Item.Presence.Value, 0.f, 1.f);
		ListMain[Index] = bUp ? LocalSize.Y - Cursor - Sizes[Index].Y : Cursor;
		Cursor += (Sizes[Index].Y + StackSettings.Spacing) * Space;
	}
	const float ListEnd = FMath::Max(Cursor - StackSettings.Spacing, 0.f);

	// Deck: the newest in front at the edge, each older one peeking out past the one in front of it.
	const bool bDeck = StackSettings.StackLayout == ENotificationStackLayout::Deck;
	const float SpreadAmount = bDeck ? FMath::Clamp(Spread.Value, 0.f, 1.f) : 1.f;
	TArray<int32> Recency;
	float DeckEnd = 0.f;
	float DeckFront = 0.f;
	if (bDeck)
	{
		Recency.SetNum(Count);
		TArray<int32> ByAge = DisplayOrder;
		ByAge.Sort([this](int32 A, int32 B) { return Items[A].Order > Items[B].Order; });
		int32 Rank = 0;
		for (const int32 Index : ByAge)
		{
			Recency[Index] = Rank;
			Rank += Items[Index].bLeaving ? 0 : 1;
		}
		DeckFront = Sizes[ByAge[0]].Y;
		DeckEnd = DeckFront + FMath::Max(FMath::Min(Rank - 1, StackSettings.DeckDepth), 0) * StackSettings.DeckPeek;
	}

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FItem& Item = Items[Index];
		FPose& Pose = OutPoses[Index];
		Pose.Size = Sizes[Index];

		float Main = ListMain[Index];
		if (bDeck)
		{
			const int32 Rank = Recency[Index];
			const float FrontEdge = bUp ? LocalSize.Y : 0.f;
			const float Far = static_cast<float>(FMath::Min(Rank, StackSettings.DeckDepth + 1)) * StackSettings.DeckPeek;
			// Each card's far edge sits Peek past the one in front's: scaled about that edge, it peeks out below (or above).
			const float DeckMain = bUp ? FrontEdge - DeckFront - Far : DeckFront + Far - Pose.Size.Y;
			const float DeckScale = 1.f - 0.05f * static_cast<float>(Rank);
			const float DeckOpacity = Rank > StackSettings.DeckDepth ? 0.f : 1.f - 0.25f * static_cast<float>(Rank);
			Main = FMath::Lerp(DeckMain, Main, SpreadAmount);
			Pose.Scale = FMath::Lerp(DeckScale, 1.f, SpreadAmount);
			Pose.Opacity = FMath::Lerp(DeckOpacity, 1.f, SpreadAmount);
			Pose.Pivot = FVector2f(0.5f, bUp ? 0.f : 1.f);
		}

		float Cross = 0.f;
		switch (StackSettings.ItemAlignment)
		{
		case HAlign_Center: Cross = (LocalSize.X - Pose.Size.X) * 0.5f; break;
		case HAlign_Right: Cross = LocalSize.X - Pose.Size.X; break;
		default: break;
		}
		Pose.Position = FVector2f(Cross, Main);

		if (Item.bSwiped)
		{
			Pose.Opacity *= FMath::Clamp(Item.Presence.Value, 0.f, 1.f);
		}
		else
		{
			ApplyTransition(Item.bLeaving ? StackSettings.ExitTransition : StackSettings.EnterTransition, Item.Presence.Value, Pose);
		}

		const float DragOffset = Item.Drag.Value;
		if (DragOffset != 0.f)
		{
			Pose.Position.X += DragOffset;
			Pose.Opacity *= 1.f - 0.7f * FMath::Clamp(FMath::Abs(DragOffset) / FMath::Max(Pose.Size.X, 1.f), 0.f, 1.f);
		}
	}

	OutEnd = bDeck ? FMath::Lerp(DeckEnd, ListEnd, SpreadAmount) : ListEnd;
}

FText SNotificationStack::GetOverflowText() const
{
	return FText::Format(LOCTEXT("Overflow", "+{0}"), FText::AsNumber(OverflowCount));
}

FVector2D SNotificationStack::GetOverflowSize() const
{
	if (!StackSettings.bShowOverflow || OverflowCount <= 0 || !FSlateApplication::IsInitialized())
	{
		return FVector2D::ZeroVector;
	}
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2D TextSize = Measure->Measure(GetOverflowText(), StackSettings.OverflowFont);
	return TextSize + FVector2D(18.0, 6.0);
}

FVector2D SNotificationStack::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	float Width = 0.f;
	for (const FItem& Item : Items)
	{
		Width = FMath::Max(Width, GetItemSize(Item, FVector2f::ZeroVector).X);
	}
	TArray<FPose> Poses;
	float End = 0.f;
	ComputePoses(FVector2f(Width, 0.f), Poses, End);

	const FVector2D Overflow = GetOverflowSize();
	if (Overflow.Y > 0.0)
	{
		End += StackSettings.Spacing + static_cast<float>(Overflow.Y);
		Width = FMath::Max(Width, static_cast<float>(Overflow.X));
	}
	return FVector2D(Width, End);
}

void SNotificationStack::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const FVector2f LocalSize = NotificationHostPrivate::ToVector2f(AllottedGeometry.GetLocalSize());
	TArray<FPose> Poses;
	float End = 0.f;
	ComputePoses(LocalSize, Poses, End);

	// Oldest first, so newer ones paint (and take the mouse) on top - what a deck needs.
	TArray<int32> PaintOrder;
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		PaintOrder.Add(Index);
	}
	PaintOrder.Sort([this](int32 A, int32 B) { return Items[A].Order < Items[B].Order; });

	for (const int32 Index : PaintOrder)
	{
		const FItem& Item = Items[Index];
		const FPose& Pose = Poses[Index];
		Item.PaintOpacity = Pose.Opacity;
		Item.LastWidth = Pose.Size.X;
		const EVisibility FrameVisibility = Item.Frame->GetVisibility();
		if (!ArrangedChildren.Accepts(FrameVisibility))
		{
			continue;
		}
		ArrangedChildren.AddWidget(FrameVisibility, FArrangedWidget(Item.Frame.ToSharedRef(), AllottedGeometry.MakeChild(
			FVector2D(Pose.Size.X, Pose.Size.Y),
			FSlateLayoutTransform(FVector2D(Pose.Position.X, Pose.Position.Y)),
			FSlateRenderTransform(FScale2f(Pose.Scale)),
			FVector2D(Pose.Pivot.X, Pose.Pivot.Y))));
	}
}

int32 SNotificationStack::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FArrangedChildren Arranged(EVisibility::Visible);
	ArrangeChildren(AllottedGeometry, Arranged);

	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	int32 MaxLayer = LayerId;
	for (int32 ChildIndex = 0; ChildIndex < Arranged.Num(); ++ChildIndex)
	{
		const FArrangedWidget& Child = Arranged[ChildIndex];
		float Opacity = 1.f;
		for (const FItem& Item : Items)
		{
			if (Item.Frame.Get() == &Child.Widget.Get())
			{
				Opacity = Item.PaintOpacity;
				break;
			}
		}
		if (Opacity <= 0.002f)
		{
			continue;
		}
		FWidgetStyle Faded = InWidgetStyle;
		Faded.BlendColorAndOpacityTint(FLinearColor(1.f, 1.f, 1.f, Opacity));
		const int32 ChildLayer = Child.Widget->Paint(Args.WithNewParent(this), Child.Geometry, MyCullingRect, OutDrawElements, MaxLayer + 1, Faded, bEnabled);
		MaxLayer = FMath::Max(MaxLayer, ChildLayer);
	}

	// "+N" at the end of the list for those still waiting.
	const FVector2D Overflow = GetOverflowSize();
	if (Overflow.X > 0.0)
	{
		const FVector2f LocalSize = NotificationHostPrivate::ToVector2f(AllottedGeometry.GetLocalSize());
		TArray<FPose> Poses;
		float End = 0.f;
		ComputePoses(LocalSize, Poses, End);
		const float Gap = Items.Num() > 0 ? StackSettings.Spacing : 0.f;
		const float Main = StackSettings.Growth == ENotificationStackGrowth::Up ? LocalSize.Y - End - Gap - static_cast<float>(Overflow.Y) : End + Gap;
		float Cross = 0.f;
		if (StackSettings.ItemAlignment == HAlign_Center)
		{
			Cross = (LocalSize.X - static_cast<float>(Overflow.X)) * 0.5f;
		}
		else if (StackSettings.ItemAlignment == HAlign_Right)
		{
			Cross = LocalSize.X - static_cast<float>(Overflow.X);
		}
		const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
		FSlateDrawElement::MakeBox(OutDrawElements, ++MaxLayer, AllottedGeometry.ToPaintGeometry(Overflow, FSlateLayoutTransform(FVector2D(Cross, Main))),
			NotificationHostPrivate::PillBrush(), ESlateDrawEffect::None, StackSettings.OverflowColor * Tint);
		FSlateDrawElement::MakeText(OutDrawElements, ++MaxLayer,
			AllottedGeometry.ToPaintGeometry(Overflow - FVector2D(18.0, 6.0), FSlateLayoutTransform(FVector2D(Cross + 9.0, Main + 3.0))),
			GetOverflowText(), StackSettings.OverflowFont, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.85f) * Tint);
	}
	return MaxLayer;
}

// =====================================================================================================
// UNotificationHost
// =====================================================================================================

UNotificationHost::UNotificationHost(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, EntryPool(*this)
{
	SetVisibilityInternal(ESlateVisibility::SelfHitTestInvisible);
	bApplyingPreset = true;
	MakePresetValues(ENotificationHostPreset::Toasts, *this);
	bApplyingPreset = false;
}

void UNotificationHost::MakePresetValues(ENotificationHostPreset InPreset, UNotificationHost& Out)
{
	using namespace NotificationTags;
	if (InPreset == ENotificationHostPreset::Custom)
	{
		return;
	}

	// What every preset starts from (the toast setup); each changes what makes it what it is.
	Out.Source = ENotificationHostSource::Live;
	Out.Channels = FGameplayTagContainer(Channel_Toast);
	Out.Growth = ENotificationStackGrowth::Down;
	Out.StackLayout = ENotificationStackLayout::List;
	Out.bNewestFirst = true;
	Out.Spacing = 10.f;
	Out.ItemAlignment = HAlign_Right;
	Out.ItemWidth = 0.f;
	Out.EnterTransition = ENotificationTransition::SlideFade;
	Out.ExitTransition = ENotificationTransition::SlideFade;
	Out.SlideFrom = ENotificationSlideFrom::Auto;
	Out.SlideDistance = 0.f;
	Out.Motion = EWidgetMotion::Smooth;
	Out.EnterDuration = 0.35f;
	Out.ExitDuration = 0.25f;
	Out.bInteractive = true;
	Out.bPauseOnHover = true;
	Out.ClickBehavior = ENotificationClickBehavior::EventOnly;
	Out.bSwipeToDismiss = true;
	Out.bShowOverflowCount = true;
	Out.CardStyle = FNotificationCardStyle::Make(ENotificationCardLayout::Toast);

	switch (InPreset)
	{
	case ENotificationHostPreset::Banner:
		Out.Channels = FGameplayTagContainer(Channel_Banner);
		Out.ItemAlignment = HAlign_Center;
		Out.EnterTransition = ENotificationTransition::Pop;
		Out.ExitTransition = ENotificationTransition::Fade;
		Out.EnterDuration = 0.4f;
		Out.ExitDuration = 0.3f;
		Out.bPauseOnHover = false;
		Out.ClickBehavior = ENotificationClickBehavior::None;
		Out.bSwipeToDismiss = false;
		Out.bShowOverflowCount = false;
		Out.CardStyle = FNotificationCardStyle::Make(ENotificationCardLayout::Banner);
		break;
	case ENotificationHostPreset::Feed:
		Out.Channels = FGameplayTagContainer(Channel_Feed);
		Out.bNewestFirst = false;
		Out.Spacing = 4.f;
		Out.ItemAlignment = HAlign_Left;
		Out.ExitTransition = ENotificationTransition::Fade;
		Out.EnterDuration = 0.25f;
		Out.ExitDuration = 0.35f;
		Out.bInteractive = false;
		Out.bShowOverflowCount = false;
		Out.CardStyle = FNotificationCardStyle::Make(ENotificationCardLayout::Compact);
		break;
	case ENotificationHostPreset::Hints:
		Out.Channels = FGameplayTagContainer(Channel_Hint);
		Out.Growth = ENotificationStackGrowth::Up;
		Out.ItemAlignment = HAlign_Center;
		Out.EnterTransition = ENotificationTransition::Rise;
		Out.ExitTransition = ENotificationTransition::Fade;
		Out.bInteractive = false;
		Out.bShowOverflowCount = false;
		Out.CardStyle = FNotificationCardStyle::Make(ENotificationCardLayout::Compact);
		Out.CardStyle.Padding = FMargin(14.f, 8.f);
		Out.CardStyle.TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 14);
		Out.CardStyle.BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 14);
		Out.CardStyle.bSeverityGlyph = false;
		break;
	case ENotificationHostPreset::History:
		Out.Source = ENotificationHostSource::History;
		Out.Channels.Reset();
		Out.Spacing = 8.f;
		Out.ItemAlignment = HAlign_Fill;
		Out.EnterTransition = ENotificationTransition::Fade;
		Out.bPauseOnHover = false;
		Out.bShowOverflowCount = false;
		Out.CardStyle.Width = 0.f;
		Out.CardStyle.bShowTimerBar = false;
		Out.CardStyle.ShadowColor = FLinearColor(0.f, 0.f, 0.f, 0.2f);
		Out.CardStyle.ShadowBlur = 10.f;
		Out.CardStyle.ShadowOffset = FVector2D(0.0, 2.0);
		break;
	default:
		break;
	}
}

void UNotificationHost::ApplyPreset(ENotificationHostPreset InPreset)
{
	bApplyingPreset = true;
	MakePresetValues(InPreset, *this);
	Preset = InPreset;
	bApplyingPreset = false;
	SynchronizeProperties();
}

FNotificationStackSettings UNotificationHost::MakeStackSettings() const
{
	FNotificationStackSettings Out;
	Out.Growth = Growth;
	Out.StackLayout = StackLayout;
	Out.bNewestFirst = bNewestFirst;
	Out.Spacing = Spacing;
	Out.ItemAlignment = ItemAlignment;
	Out.ItemWidth = ItemWidth;
	Out.DeckPeek = DeckPeek;
	Out.DeckDepth = DeckDepth;
	Out.EnterTransition = EnterTransition;
	Out.ExitTransition = ExitTransition;
	Out.SlideFrom = SlideFrom;
	Out.SlideDistance = SlideDistance;
	Out.Motion = Motion;
	Out.EnterDuration = EnterDuration;
	Out.ExitDuration = ExitDuration;
	Out.bInteractive = bInteractive;
	Out.bTakesClicks = ClickBehavior != ENotificationClickBehavior::None;
	Out.bSwipeToDismiss = bSwipeToDismiss;
	Out.bShowOverflow = bShowOverflowCount && Source == ENotificationHostSource::Live;
	Out.OverflowFont = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	if (const UNotificationSubsystem* Subsystem = RegisteredSubsystem.Get())
	{
		Out.bReduceMotion = Subsystem->GetNotificationPreferences().bReduceMotion;
	}
	return Out;
}

UNotificationSubsystem* UNotificationHost::FindSubsystem() const
{
	if (UNotificationSubsystem* Override = SubsystemOverride.Get())
	{
		return Override;
	}
	return UNotificationSubsystem::Get(this, GetOwningPlayer());
}

TSharedRef<SWidget> UNotificationHost::RebuildWidget()
{
	MyStack = SNew(SNotificationStack)
		.OnItemGone(BIND_UOBJECT_DELEGATE(FOnNotificationStackItem, HandleItemGone))
		.OnItemClicked(BIND_UOBJECT_DELEGATE(FOnNotificationStackItem, HandleItemClicked))
		.OnItemSwiped(BIND_UOBJECT_DELEGATE(FOnNotificationStackItem, HandleItemSwiped))
		.OnHoverChanged(BIND_UOBJECT_DELEGATE(FOnNotificationStackHover, HandleHoverChanged));

	if (Source == ENotificationHostSource::History)
	{
		MyRoot = SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				MyStack.ToSharedRef()
			];
	}
	else
	{
		MyRoot = MyStack;
	}

	if (IsDesignTime())
	{
		MyStack->SetStackSettings(MakeStackSettings());
		BuildDesignerPreview();
	}
	else
	{
		RegisterWithSubsystem();
		MyStack->SetStackSettings(MakeStackSettings());
		if (Source == ENotificationHostSource::History)
		{
			SyncHistory();
		}
	}
	return MyRoot.ToSharedRef();
}

void UNotificationHost::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (!MyStack.IsValid())
	{
		return;
	}
	MyStack->SetStackSettings(MakeStackSettings());
	for (const TPair<int32, TSharedPtr<SNotificationCard>>& Pair : Cards)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->SetCardStyle(CardStyle);
		}
	}
	if (IsDesignTime())
	{
		BuildDesignerPreview();
	}
}

void UNotificationHost::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	UnregisterFromSubsystem();

	TArray<int32> Ids;
	Views.GetKeys(Ids);
	for (const int32 Id : Ids)
	{
		ReleaseEntry(Id);
	}
	EntryPool.ReleaseAllSlateResources();
	Views.Reset();
	Cards.Reset();
	MyStack.Reset();
	MyRoot.Reset();
}

void UNotificationHost::RegisterWithSubsystem()
{
	UNotificationSubsystem* Subsystem = FindSubsystem();
	if (!Subsystem || RegisteredSubsystem.Get() == Subsystem)
	{
		return;
	}
	UnregisterFromSubsystem();
	RegisteredSubsystem = Subsystem;
	Subsystem->RegisterHost(this);
	if (Source == ENotificationHostSource::History)
	{
		Subsystem->OnHistoryChanged.AddUniqueDynamic(this, &UNotificationHost::SyncHistory);
	}
}

void UNotificationHost::UnregisterFromSubsystem()
{
	if (UNotificationSubsystem* Subsystem = RegisteredSubsystem.Get())
	{
		Subsystem->OnHistoryChanged.RemoveDynamic(this, &UNotificationHost::SyncHistory);
		Subsystem->UnregisterHost(this);
	}
	RegisteredSubsystem.Reset();
}

bool UNotificationHost::IsOnScreen() const
{
	if (!MyStack.IsValid())
	{
		return false;
	}
	// Up to its window, nothing hidden on the way: a HUD collapsed, or removed from the screen, shows nothing.
	TSharedPtr<const SWidget> Walk = MyStack;
	for (int32 Guard = 0; Walk.IsValid() && Guard < 512; ++Guard)
	{
		if (!Walk->GetVisibility().IsVisible())
		{
			return false;
		}
		if (Walk->Advanced_IsWindow())
		{
			return true;
		}
		Walk = Walk->GetParentWidget();
	}
	return false;
}

bool UNotificationHost::IsLive() const
{
	return Source == ENotificationHostSource::Live && !IsDesignTime() && IsOnScreen();
}

bool UNotificationHost::ShowsChannel(const FGameplayTag& Channel) const
{
	return Source == ENotificationHostSource::Live && Channel.IsValid() && Channel.MatchesAny(Channels);
}

int32 UNotificationHost::GetCapacity(const FNotificationChannelSettings& ChannelSettings) const
{
	return FMath::Max(MaxVisible > 0 ? MaxVisible : ChannelSettings.MaxVisible, 1);
}

bool UNotificationHost::IsHoldingTime() const
{
	return bInteractive && bPauseOnHover && MyStack.IsValid() && MyStack->IsHoveringItem();
}

bool UNotificationHost::HasEntry(int32 Id) const
{
	return MyStack.IsValid() && MyStack->HasItem(Id);
}

TSubclassOf<UNotificationEntryWidget> UNotificationHost::ResolveEntryClass(const FNotificationView& View) const
{
	const TSoftClassPtr<UNotificationEntryWidget>& TypeClass = UNotificationSettings::Get().FindType(View.Type).EntryWidgetClass;
	if (!TypeClass.IsNull())
	{
		// Loaded ahead by the subsystem (it preloads every type's class); this only waits when that hasn't finished.
		if (UClass* Loaded = TypeClass.LoadSynchronous())
		{
			return Loaded;
		}
	}
	return EntryWidgetClass;
}

FNotificationCardHooks UNotificationHost::MakeHooks(int32 Id, bool bHistoryEntry)
{
	FNotificationCardHooks Hooks;
	const FNotificationHandle Handle(Id);
	const TWeakObjectPtr<UNotificationSubsystem> WeakSubsystem = RegisteredSubsystem;

	if (IsDesignTime())
	{
		// The designer has no notification system: a timer bar part run down, and the keys on the first with actions.
		Hooks.TimeFraction = []() { return 0.62f; };
		Hooks.IsShortcutTarget = [this, Id]()
		{
			for (const TPair<int32, FNotificationView>& Pair : Views)
			{
				if (Pair.Value.Actions.Num() > 0)
				{
					return Pair.Key == Id;
				}
			}
			return false;
		};
		Hooks.DisplayKey = [](const FNotificationAction& Action) { return Action.KeyboardKey.IsValid() ? Action.KeyboardKey : Action.GamepadKey; };
		return Hooks;
	}

	if (!bHistoryEntry)
	{
		Hooks.TimeFraction = [WeakSubsystem, Handle]()
		{
			const UNotificationSubsystem* Subsystem = WeakSubsystem.Get();
			return Subsystem ? Subsystem->GetTimeRemainingFraction(Handle) : 1.f;
		};
		Hooks.HoldProgress = [WeakSubsystem, Handle](FName ActionId)
		{
			const UNotificationSubsystem* Subsystem = WeakSubsystem.Get();
			return Subsystem ? Subsystem->GetHoldProgress(Handle, ActionId) : 0.f;
		};
		Hooks.IsShortcutTarget = [WeakSubsystem, Handle]()
		{
			const UNotificationSubsystem* Subsystem = WeakSubsystem.Get();
			return Subsystem && Subsystem->IsShortcutTarget(Handle);
		};
		Hooks.DisplayKey = [WeakSubsystem](const FNotificationAction& Action)
		{
			const UNotificationSubsystem* Subsystem = WeakSubsystem.Get();
			return Subsystem ? Subsystem->GetDisplayKey(Action) : Action.KeyboardKey;
		};
		Hooks.OnAction = FOnNotificationCardAction::CreateLambda([WeakSubsystem, Handle](FName ActionId)
		{
			if (UNotificationSubsystem* Subsystem = WeakSubsystem.Get())
			{
				Subsystem->RespondToNotification(Handle, ActionId);
			}
		});
	}
	Hooks.OnClose = FSimpleDelegate::CreateLambda([WeakSubsystem, Handle, bHistoryEntry]()
	{
		if (UNotificationSubsystem* Subsystem = WeakSubsystem.Get())
		{
			if (bHistoryEntry)
			{
				Subsystem->RemoveFromHistory(Handle);
			}
			else
			{
				Subsystem->DismissByPlayer(Handle);
			}
		}
	});
	return Hooks;
}

TSharedRef<SWidget> UNotificationHost::MakeEntryContent(const FNotificationView& View, bool bHistoryEntry)
{
	const int32 Id = View.Handle.Id;
	if (const TSubclassOf<UNotificationEntryWidget> Class = ResolveEntryClass(View))
	{
		if (UNotificationEntryWidget* Entry = EntryPool.GetOrCreateInstance<UNotificationEntryWidget>(Class))
		{
			Entry->SetNotification(View, this, RegisteredSubsystem.Get(), bHistoryEntry);
			EntryWidgets.Add(Id, Entry);
			return Entry->TakeWidget();
		}
	}
	TSharedRef<SNotificationCard> Card = SNew(SNotificationCard, CardStyle, View, MakeHooks(Id, bHistoryEntry)).bHistory(bHistoryEntry);
	Cards.Add(Id, Card);
	return Card;
}

void UNotificationHost::ReleaseEntry(int32 Id)
{
	if (const TObjectPtr<UNotificationEntryWidget>* Found = EntryWidgets.Find(Id))
	{
		if (UNotificationEntryWidget* Entry = Found->Get())
		{
			Entry->ClearNotification();
			EntryPool.Release(Entry);
		}
		EntryWidgets.Remove(Id);
	}
	Cards.Remove(Id);
}

void UNotificationHost::ShowEntry(const FNotificationView& View, bool bFirstTime)
{
	if (!MyStack.IsValid())
	{
		return;
	}
	const int32 Id = View.Handle.Id;
	Views.Add(Id, View);

	TSharedPtr<SWidget> Content;
	if (const TObjectPtr<UNotificationEntryWidget>* Entry = EntryWidgets.Find(Id))
	{
		(*Entry)->UpdateNotification(View, false);
		Content = (*Entry)->TakeWidget();
	}
	else if (const TSharedPtr<SNotificationCard>* Card = Cards.Find(Id))
	{
		(*Card)->SetView(View, false);
		Content = *Card;
	}
	else
	{
		Content = MakeEntryContent(View, false);
	}
	MyStack->AddItem(Id, Content.ToSharedRef(), View.bCanDismiss, /*bAnimate=*/true);
	OnNotificationShown.Broadcast(View);
}

void UNotificationHost::UpdateEntry(const FNotificationView& View, bool bStacked)
{
	const int32 Id = View.Handle.Id;
	if (!Views.Contains(Id))
	{
		return;
	}
	Views.Add(Id, View);
	if (const TObjectPtr<UNotificationEntryWidget>* Entry = EntryWidgets.Find(Id))
	{
		(*Entry)->UpdateNotification(View, bStacked);
	}
	else if (const TSharedPtr<SNotificationCard>* Card = Cards.Find(Id))
	{
		(*Card)->SetView(View, bStacked);
	}
	if (MyStack.IsValid())
	{
		MyStack->NotifyContentChanged(Id);
	}
}

void UNotificationHost::HideEntry(int32 Id)
{
	if (MyStack.IsValid() && MyStack->ContainsItem(Id))
	{
		// Off screen nothing animates (no ticks): it goes at once.
		MyStack->RemoveItem(Id, IsOnScreen());
		return;
	}
	HandleItemGone(Id);
}

void UNotificationHost::RefreshEntry(int32 Id)
{
	if (const TObjectPtr<UNotificationEntryWidget>* Entry = EntryWidgets.Find(Id))
	{
		(*Entry)->RefreshShortcutState();
	}
	else if (const TSharedPtr<SNotificationCard>* Card = Cards.Find(Id))
	{
		(*Card)->RefreshHints();
	}
	if (MyStack.IsValid())
	{
		MyStack->NotifyContentChanged(Id);
	}
}

void UNotificationHost::RefreshAllEntries()
{
	TArray<int32> Ids;
	Views.GetKeys(Ids);
	for (const int32 Id : Ids)
	{
		RefreshEntry(Id);
	}
}

void UNotificationHost::SetOverflowCount(int32 InCount)
{
	if (MyStack.IsValid())
	{
		MyStack->SetOverflowCount(bShowOverflowCount ? InCount : 0);
	}
}

void UNotificationHost::TickFromSubsystem(UNotificationSubsystem& Subsystem)
{
	if (Source == ENotificationHostSource::Live)
	{
		SetOverflowCount(IsLive() ? Subsystem.GetQueuedCount(Channels) : 0);
		return;
	}
	if (bMarkReadWhenShown && Subsystem.GetUnreadCount() > 0 && IsOnScreen())
	{
		Subsystem.MarkNotificationsRead();
	}
}

void UNotificationHost::ApplyPreferences()
{
	if (MyStack.IsValid())
	{
		MyStack->SetStackSettings(MakeStackSettings());
	}
}

void UNotificationHost::HandleItemGone(int32 Id)
{
	const FNotificationView View = Views.FindRef(Id);
	ReleaseEntry(Id);
	Views.Remove(Id);
	if (Source == ENotificationHostSource::Live)
	{
		if (UNotificationSubsystem* Subsystem = RegisteredSubsystem.Get())
		{
			Subsystem->NotifyHostEntryGone(this, Id);
		}
		if (View.Handle.IsValid())
		{
			OnNotificationRemoved.Broadcast(View);
		}
	}
}

void UNotificationHost::HandleItemClicked(int32 Id)
{
	UNotificationSubsystem* Subsystem = RegisteredSubsystem.Get();
	if (!Subsystem || IsDesignTime())
	{
		return;
	}
	const FNotificationHandle Handle(Id);
	switch (ClickBehavior)
	{
	case ENotificationClickBehavior::Dismiss:
		Subsystem->NotifyClicked(Handle);
		if (Source == ENotificationHostSource::Live)
		{
			Subsystem->DismissByPlayer(Handle);
		}
		break;
	case ENotificationClickBehavior::FirstAction:
	{
		const FNotificationView* View = Views.Find(Id);
		if (Source == ENotificationHostSource::Live && View && View->Actions.Num() > 0)
		{
			Subsystem->RespondToNotification(Handle, View->Actions[0].ActionId);
		}
		else
		{
			Subsystem->NotifyClicked(Handle);
		}
		break;
	}
	case ENotificationClickBehavior::EventOnly:
		Subsystem->NotifyClicked(Handle);
		break;
	default:
		break;
	}
}

void UNotificationHost::HandleItemSwiped(int32 Id)
{
	UNotificationSubsystem* Subsystem = RegisteredSubsystem.Get();
	if (!Subsystem)
	{
		// Designer: nothing to tell, it just goes.
		if (MyStack.IsValid())
		{
			MyStack->RemoveItem(Id, true);
		}
		return;
	}
	if (Source == ENotificationHostSource::History)
	{
		Subsystem->RemoveFromHistory(FNotificationHandle(Id));
	}
	else
	{
		Subsystem->DismissByPlayer(FNotificationHandle(Id));
	}
}

void UNotificationHost::HandleHoverChanged(bool bInHovered)
{
	bHovered = bInHovered;
}

void UNotificationHost::SyncHistory()
{
	UNotificationSubsystem* Subsystem = RegisteredSubsystem.Get();
	if (!MyStack.IsValid() || Source != ENotificationHostSource::History || !Subsystem || bSyncingHistory)
	{
		return;
	}
	TGuardValue<bool> Guard(bSyncingHistory, true);

	const TArray<FNotificationView>& History = Subsystem->GetNotificationHistory();
	const int32 Count = FMath::Min(History.Num(), MaxHistoryShown);
	TSet<int32> Wanted;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Wanted.Add(History[Index].Handle.Id);
	}

	// Gone from the history (removed, cleared, pushed out): out of the list.
	TArray<int32> Shown;
	Views.GetKeys(Shown);
	for (const int32 Id : Shown)
	{
		if (!Wanted.Contains(Id) && MyStack->HasItem(Id))
		{
			MyStack->RemoveItem(Id, true);
		}
	}

	// New ones in, oldest first so the newest ends up first; the rest take any change (read).
	const bool bAnimate = MyStack->GetItemCount() > 0;
	for (int32 Index = Count - 1; Index >= 0; --Index)
	{
		const FNotificationView& View = History[Index];
		const int32 Id = View.Handle.Id;
		if (Views.Contains(Id))
		{
			Views.Add(Id, View);
			if (const TSharedPtr<SNotificationCard>* Card = Cards.Find(Id))
			{
				(*Card)->SetView(View, false);
			}
			else if (const TObjectPtr<UNotificationEntryWidget>* Entry = EntryWidgets.Find(Id))
			{
				(*Entry)->UpdateNotification(View, false);
			}
			continue;
		}
		Views.Add(Id, View);
		MyStack->AddItem(Id, MakeEntryContent(View, true), /*bCanSwipe=*/true, bAnimate);
	}
}

void UNotificationHost::SetCardStyle(const FNotificationCardStyle& InCardStyle)
{
	CardStyle = InCardStyle;
	for (const TPair<int32, TSharedPtr<SNotificationCard>>& Pair : Cards)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->SetCardStyle(CardStyle);
		}
	}
}

void UNotificationHost::SetChannels(const FGameplayTagContainer& InChannels)
{
	Channels = InChannels;
}

void UNotificationHost::DismissAll(bool bIncludeQueued)
{
	UNotificationSubsystem* Subsystem = RegisteredSubsystem.Get();
	if (!Subsystem || Source != ENotificationHostSource::Live)
	{
		return;
	}
	TArray<int32> Ids;
	Views.GetKeys(Ids);
	for (const int32 Id : Ids)
	{
		Subsystem->DismissNotification(FNotificationHandle(Id));
	}
	if (bIncludeQueued && !Channels.IsEmpty())
	{
		Subsystem->ClearNotifications(Channels, /*bIncludeQueued=*/true);
	}
}

int32 UNotificationHost::GetShownCount() const
{
	return MyStack.IsValid() ? MyStack->GetItemCount() : 0;
}

void UNotificationHost::BuildDesignerPreview()
{
	if (!MyStack.IsValid())
	{
		return;
	}
	TArray<int32> Ids;
	Views.GetKeys(Ids);
	for (const int32 Id : Ids)
	{
		ReleaseEntry(Id);
	}
	Views.Reset();
	MyStack->ClearItems();
	if (!bPreviewInDesigner)
	{
		MyStack->SetOverflowCount(0);
		return;
	}

	ENotificationHostPreset Look = Preset;
	if (Look == ENotificationHostPreset::Custom)
	{
		// A custom host previews what its channels suggest.
		if (Source == ENotificationHostSource::History)
		{
			Look = ENotificationHostPreset::History;
		}
		else if (Channels.HasTag(NotificationTags::Channel_Banner))
		{
			Look = ENotificationHostPreset::Banner;
		}
		else if (Channels.HasTag(NotificationTags::Channel_Feed))
		{
			Look = ENotificationHostPreset::Feed;
		}
		else if (Channels.HasTag(NotificationTags::Channel_Hint))
		{
			Look = ENotificationHostPreset::Hints;
		}
		else
		{
			Look = ENotificationHostPreset::Toasts;
		}
	}

	const int32 Shown = Source == ENotificationHostSource::Live && MaxVisible > 0 ? FMath::Min(PreviewCount, MaxVisible) : PreviewCount;
	const TArray<FNotificationView> Samples = MakePreviewViews(Look, Shown);
	for (const FNotificationView& View : Samples)
	{
		Views.Add(View.Handle.Id, View);
	}
	for (const FNotificationView& View : Samples)
	{
		MyStack->AddItem(View.Handle.Id, MakeEntryContent(View, Source == ENotificationHostSource::History), false, /*bAnimate=*/false);
	}
	MyStack->SetOverflowCount(bShowOverflowCount && Source == ENotificationHostSource::Live && Look == ENotificationHostPreset::Toasts ? 2 : 0);
}

TArray<FNotificationView> UNotificationHost::MakePreviewViews(ENotificationHostPreset InPreset, int32 Count)
{
	using namespace NotificationTags;

	TArray<FNotificationView> Samples;
	auto Add = [&Samples](const FGameplayTag& Type, ENotificationSeverity Severity, const FText& Title, const FText& Body)
	{
		FNotificationView& View = Samples.AddDefaulted_GetRef();
		View.Handle = FNotificationHandle(-(Samples.Num()));
		View.Type = Type;
		View.Severity = Severity;
		View.Title = Title;
		View.Body = Body;
		View.Duration = 6.f;
		View.PostedAt = FDateTime::UtcNow() - FTimespan::FromMinutes(Samples.Num() * 7);
		View.bRead = Samples.Num() > 2;
		return &View;
	};

	switch (InPreset)
	{
	case ENotificationHostPreset::Banner:
		Add(Type_Objective, ENotificationSeverity::Info, LOCTEXT("PreviewObjective", "Objective Updated"), LOCTEXT("PreviewObjectiveBody", "Reach the lighthouse before nightfall"));
		Add(Type_Objective, ENotificationSeverity::Reward, LOCTEXT("PreviewLevel", "Level 12"), LOCTEXT("PreviewLevelBody", "New skill point available"));
		break;
	case ENotificationHostPreset::Feed:
	{
		FNotificationView* Ore = Add(Type_Loot, ENotificationSeverity::Reward, LOCTEXT("PreviewOre", "Iron Ore"), FText::GetEmpty());
		Ore->Count = 3;
		Add(Type_Reward, ENotificationSeverity::Reward, LOCTEXT("PreviewXP", "+150 XP"), LOCTEXT("PreviewXPBody", "Headshot"));
		Add(Type_Elimination, ENotificationSeverity::Info, LOCTEXT("PreviewKill", "Kestrel"), LOCTEXT("PreviewKillBody", "eliminated Rook"));
		Add(Type_Loot, ENotificationSeverity::Reward, LOCTEXT("PreviewPotion", "Health Potion"), FText::GetEmpty());
		Add(Type_Reward, ENotificationSeverity::Reward, LOCTEXT("PreviewGold", "+40 Gold"), FText::GetEmpty());
		break;
	}
	case ENotificationHostPreset::Hints:
		Add(Type_Hint, ENotificationSeverity::Info, LOCTEXT("PreviewHint", "Hold Shift to sprint"), FText::GetEmpty());
		Add(Type_Hint, ENotificationSeverity::Info, LOCTEXT("PreviewHint2", "Press Tab to open the map"), FText::GetEmpty());
		break;
	default:
	{
		Add(Type_Achievement, ENotificationSeverity::Reward, LOCTEXT("PreviewAchievement", "Achievement Unlocked"), LOCTEXT("PreviewAchievementBody", "Cartographer - discover every region"));
		FNotificationView* Invite = Add(Type_Social, ENotificationSeverity::Social, LOCTEXT("PreviewInvite", "Ava invited you to a party"), LOCTEXT("PreviewInviteBody", "Squad - 2 of 4 players"));
		FNotificationAction Accept;
		Accept.ActionId = TEXT("Accept");
		Accept.Label = LOCTEXT("PreviewAccept", "Accept");
		Accept.KeyboardKey = EKeys::Y;
		Accept.GamepadKey = EKeys::Gamepad_DPad_Right;
		FNotificationAction Decline;
		Decline.ActionId = TEXT("Decline");
		Decline.Label = LOCTEXT("PreviewDecline", "Decline");
		Decline.Style = ENotificationActionStyle::Secondary;
		Decline.KeyboardKey = EKeys::N;
		Decline.GamepadKey = EKeys::Gamepad_DPad_Left;
		Invite->Actions = { Accept, Decline };
		FNotificationView* Download = Add(Type_Info, ENotificationSeverity::Info, LOCTEXT("PreviewDownload", "Downloading update"), LOCTEXT("PreviewDownloadBody", "1.4 GB of 3.1 GB"));
		Download->Progress = 0.45f;
		Add(Type_Warning, ENotificationSeverity::Warning, LOCTEXT("PreviewWarning", "Inventory almost full"), LOCTEXT("PreviewWarningBody", "38 of 40 slots used"));
		FNotificationView* Saved = Add(Type_Success, ENotificationSeverity::Success, LOCTEXT("PreviewSaved", "Game saved"), FText::GetEmpty());
		Saved->Count = 2;
		Add(Type_Error, ENotificationSeverity::Error, LOCTEXT("PreviewError", "Couldn't join the session"), LOCTEXT("PreviewErrorBody", "The host left the game"));
		break;
	}
	}

	// A list of the right length: repeat the samples, or cut them.
	TArray<FNotificationView> Out;
	for (int32 Index = 0; Index < Count && Samples.Num() > 0; ++Index)
	{
		FNotificationView View = Samples[Index % Samples.Num()];
		View.Handle = FNotificationHandle(-(Index + 1));
		Out.Add(View);
	}
	return Out;
}

#if WITH_EDITOR
const FText UNotificationHost::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}

void UNotificationHost::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName Changed = PropertyChangedEvent.GetMemberPropertyName();
	if (Changed == GET_MEMBER_NAME_CHECKED(UNotificationHost, Preset))
	{
		bApplyingPreset = true;
		MakePresetValues(Preset, *this);
		bApplyingPreset = false;
	}
	else if (!bApplyingPreset && Preset != ENotificationHostPreset::Custom)
	{
		// What a preset doesn't set stays out of it: changing these keeps the preset.
		static const TSet<FName> Free = {
			GET_MEMBER_NAME_CHECKED(UNotificationHost, HostPriority),
			GET_MEMBER_NAME_CHECKED(UNotificationHost, MaxVisible),
			GET_MEMBER_NAME_CHECKED(UNotificationHost, MaxHistoryShown),
			GET_MEMBER_NAME_CHECKED(UNotificationHost, bMarkReadWhenShown),
			GET_MEMBER_NAME_CHECKED(UNotificationHost, EntryWidgetClass),
			GET_MEMBER_NAME_CHECKED(UNotificationHost, bPreviewInDesigner),
			GET_MEMBER_NAME_CHECKED(UNotificationHost, PreviewCount),
			GET_MEMBER_NAME_CHECKED(UNotificationHost, DeckPeek),
			GET_MEMBER_NAME_CHECKED(UNotificationHost, DeckDepth),
		};
		const FProperty* Member = PropertyChangedEvent.MemberProperty;
		if (Member && !Free.Contains(Changed) && Member->GetOwnerClass() == UNotificationHost::StaticClass())
		{
			Preset = ENotificationHostPreset::Custom;
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

#undef LOCTEXT_NAMESPACE
