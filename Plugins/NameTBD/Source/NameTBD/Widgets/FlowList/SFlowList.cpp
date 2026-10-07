// Copyright AR GameSystems 2026. All rights reserved.

#include "SFlowList.h"

#include "Algo/BinarySearch.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/WidgetPath.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderTransform.h"

#include "NameTBD/WidgetAnimation/WidgetAnimInput.h"

namespace FlowListPrivate
{
	/** A scroll spring this close to its target, in items, and this slow, in items per second, has arrived. */
	constexpr float RestDistance = 0.0005f;
	constexpr float RestSpeed = 0.005f;

	/** The same, for items sliding to new places. */
	constexpr float PlaceRestDistance = 0.001f;
	constexpr float PlaceRestSpeed = 0.01f;

	/** No fling is faster than this, in items per second - a twitchy release on a long list shouldn't launch it. */
	constexpr float MaxFlingSpeed = 60.f;

	/** A release at least this fast, in items per second, always moves at least one item: a flick pages. */
	constexpr float FlickSpeed = 2.f;

	/** How much pointer history, in seconds, a release's speed is read from. */
	constexpr double VelocityWindow = 0.1;

	/** A pointer held still this long before it lets go releases with no speed: put down, not thrown. */
	constexpr double StillTime = 0.06;

	/** A press on a list moving faster than this, in items per second, catches it rather than clicking what is under it. */
	constexpr float CatchSpeed = 1.5f;

	/** With no Cull Distance, how many items either side of the focus are ever laid out. */
	constexpr float MaxLaidOutItems = 64.f;

	/** How much more a drag must run along the list than across it to be the list's. Otherwise it is left to whatever is behind. */
	constexpr float AxisLockRatio = 1.2f;

	FVector2f Rotate(const FVector2f& Vector, float Radians)
	{
		const float Sin = FMath::Sin(Radians);
		const float Cos = FMath::Cos(Radians);
		return FVector2f(Vector.X * Cos - Vector.Y * Sin, Vector.X * Sin + Vector.Y * Cos);
	}

	float SmoothStep01(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * (3.f - 2.f * X);
	}

	/** Squash along the item's length, then turn, both about Pivot - in the item's own local space. */
	FSlateRenderTransform MakeItemTransform(float Radians, float Squash, bool bHorizontal, const FVector2f& Pivot)
	{
		const float ScaleX = bHorizontal ? Squash : 1.f;
		const float ScaleY = bHorizontal ? 1.f : Squash;
		const float Cos = FMath::Cos(Radians);
		const float Sin = FMath::Sin(Radians);

		// Rows are where the local X and Y axes land, the way ShapeBox builds its transforms.
		const FSlateRenderTransform Turn(FMatrix2x2f(Cos * ScaleX, Sin * ScaleX, -Sin * ScaleY, Cos * ScaleY), FVector2f::ZeroVector);
		return ::Concatenate(::Concatenate(FSlateRenderTransform(-Pivot), Turn), FSlateRenderTransform(Pivot));
	}

	/** The first widget in Widget's subtree, Widget included, that keyboard and gamepad focus can land on. */
	TSharedPtr<SWidget> FindFocusable(const TSharedRef<SWidget>& Widget, int32 Depth)
	{
		if (!Widget->GetVisibility().IsVisible() || !Widget->IsEnabled())
		{
			return nullptr;
		}
		if (Widget->SupportsKeyboardFocus())
		{
			return Widget;
		}
		if (Depth >= 32)
		{
			return nullptr;
		}
		if (FChildren* Kids = Widget->GetChildren())
		{
			for (int32 Index = 0; Index < Kids->Num(); ++Index)
			{
				if (TSharedPtr<SWidget> Found = FindFocusable(Kids->GetChildAt(Index), Depth + 1))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}
}

// =====================================================================================================
// Slot
// =====================================================================================================

void SFlowList::FSlot::SetPadding(const FMargin& InPadding)
{
	Padding = InPadding;
}

void SFlowList::FSlot::SetSizeOverride(const FVector2D& InSize)
{
	SizeOverride = InSize;
}

float SFlowList::FPitchTable::At(int64 Segment) const
{
	if (Segments.Num() == 0)
	{
		return Fallback;
	}
	const int64 Count = Segments.Num();
	if (bLoop)
	{
		int64 Wrapped = Segment % Count;
		if (Wrapped < 0)
		{
			Wrapped += Count;
		}
		return Segments[Wrapped];
	}
	// Past either end the end's pitch carries on, so overscroll and items still sliding in keep an even spacing.
	return Segments[FMath::Clamp<int64>(Segment, 0, Count - 1)];
}

// =====================================================================================================
// Construction and settings
// =====================================================================================================

/**
 * Stick Scrolls: hands a list the stick's moves before Slate turns them into navigation - which the focused item, not the
 * list, would otherwise make of them. The list takes only the axis along it, and only while it or an item has focus.
 */
class FFlowListStickInput : public IInputProcessor
{
public:
	explicit FFlowListStickInput(SFlowList* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
	{
		return Owner && Owner->HandleStickAnalog(InAnalogInputEvent);
	}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		return Owner && Owner->HandleStickKey(InKeyEvent);
	}

	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		return Owner && Owner->HandleStickKey(InKeyEvent);
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("FlowListStickInput"); }

	/** Cleared as the list goes, before it is unregistered: input already on its way finds no one. */
	SFlowList* Owner = nullptr;
};

SFlowList::SFlowList()
	: Children(this)
{
	SetCanTick(false);
}

SFlowList::~SFlowList()
{
	if (StickInput.IsValid())
	{
		StickInput->Owner = nullptr;
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(StickInput);
		}
	}
}

void SFlowList::Construct(const FArguments& InArgs)
{
}

void SFlowList::SetSettings(const FFlowListLayout& InLayout, const FFlowListEffects& InEffects, const FFlowListScrolling& InScrolling, const FFlowListTransitions& InTransitions)
{
	Layout = InLayout;
	Effects = InEffects;
	Scrolling = InScrolling;
	Transitions = InTransitions;

	SplineBuiltHash = 0;
	VirtualExtent = 0.f;
	++LayoutVersion;
	if (!Scrolling.bStopAtContentEnd)
	{
		CachedContentEndScroll = -1.f;
	}
	UpdateStickInput();

	// A list that stops looping may be somewhere past its ends; the next tick brings it back.
	Invalidate(EInvalidateWidgetReason::Layout);
	WakeUp();
}

void SFlowList::SetItemAnimationLengths(float InEnterSeconds, float InExitSeconds)
{
	ItemEnterSeconds = FMath::Max(InEnterSeconds, 0.f);
	ItemExitSeconds = FMath::Max(InExitSeconds, 0.f);
}

void SFlowList::SetItemAnimationPose(const TSharedRef<SWidget>& Widget, const FSlateRenderTransform& Transform, float Opacity, const FVector2f& Pivot)
{
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		FSlot& Slot = Children[Index];
		if (Slot.GetWidget() == Widget)
		{
			Slot.bHasAnimPose = true;
			Slot.AnimTransform = Transform;
			Slot.AnimOpacity = Opacity;
			Slot.AnimPivot = Pivot;
		}
	}
	++LayoutVersion;
	Invalidate(EInvalidateWidgetReason::Layout);
}

bool SFlowList::GetItemTransition(const TSharedRef<SWidget>& Widget, bool& bOutEntering, bool& bOutExiting) const
{
	bOutEntering = false;
	bOutExiting = false;
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		const FSlot& Slot = Children[Index];
		if (Slot.GetWidget() == Widget)
		{
			bOutEntering |= Slot.bEntering;
			bOutExiting |= Slot.bExiting;
		}
	}
	return bOutEntering || bOutExiting;
}

void SFlowList::ClearItemAnimationPose(const TSharedRef<SWidget>& Widget)
{
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		FSlot& Slot = Children[Index];
		if (Slot.GetWidget() == Widget && Slot.bHasAnimPose)
		{
			Slot.bHasAnimPose = false;
			Slot.AnimTransform = FSlateRenderTransform();
			Slot.AnimOpacity = 1.f;
			++LayoutVersion;
			Invalidate(EInvalidateWidgetReason::Layout);
		}
	}
}

void SFlowList::SetDesignTime(bool bInDesignTime, bool bInShowPath)
{
	bDesignTime = bInDesignTime;
	bShowPath = bInShowPath;
	UpdateStickInput();
	Invalidate(EInvalidateWidgetReason::Paint);
}

// =====================================================================================================
// Structure
// =====================================================================================================

SFlowList::FSlot* SFlowList::InsertChild(const TSharedRef<SWidget>& Widget, int32 Position, bool bAnimate)
{
	// Added back while it was still leaving: drop the leaving copy, the new slot takes over.
	const int32 Leaving = FindChild(Widget, /*bExiting=*/true);
	if (Leaving != INDEX_NONE)
	{
		RestoreVisibility(Children[Leaving]);
		Children.RemoveAt(Leaving);
		RebuildLiveOrder();
	}

	// Within one lap before the anchor is measured: a looping list keeps its place as its count changes.
	NormalizeLoop();
	const FFocusAnchor Anchor = CaptureAnchor();

	// Position counts children that aren't leaving; leaving ones keep their own place in the array.
	int32 ChildIndex = Children.Num();
	int32 Seen = 0;
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		if (Children[Index].bExiting)
		{
			continue;
		}
		if (Seen == Position)
		{
			ChildIndex = Index;
			break;
		}
		++Seen;
	}

	TUniquePtr<FSlot> NewSlot = MakeUnique<FSlot>();
	FSlot* Slot = NewSlot.Get();
	FSlot::FSlotArguments Arguments(MoveTemp(NewSlot));
	Arguments[Widget];
	Children.InsertSlot(MoveTemp(Arguments), ChildIndex);

	RestoreAnchor(Anchor);

	// Whoever heard the selection change may have removed it again already.
	if (FindChild(Slot) == INDEX_NONE || Slot->bExiting)
	{
		return nullptr;
	}

	if (bAnimate && Transitions.bAnimateChanges && Slot->ItemIndex != INDEX_NONE)
	{
		// Several added in one frame - a page of results arriving - enter one after another.
		if (GFrameCounter != EnterBatchFrame)
		{
			EnterBatchFrame = GFrameCounter;
			EnterBatchCount = 0;
		}
		StartEnter(*Slot, GetEnterDelay() + GetBatchStagger() * static_cast<float>(EnterBatchCount++));
	}
	return Slot;
}

bool SFlowList::RemoveChild(const TSharedRef<SWidget>& Widget, bool bAnimate)
{
	const int32 ChildIndex = FindChild(Widget, /*bExiting=*/false);
	if (ChildIndex == INDEX_NONE)
	{
		return false;
	}

	// Within one lap before the anchor is measured: a looping list keeps its place as its count changes.
	NormalizeLoop();
	const FFocusAnchor Anchor = CaptureAnchor();

	FSlot& Slot = Children[ChildIndex];
	const bool bLeaveAnimated = bAnimate && Transitions.bAnimateChanges && !bDesignTime && Slot.ItemIndex != INDEX_NONE && Slot.bPlaced
		&& (GetExitSeconds() + GetExitDelay()) > 0.f;
	if (bLeaveAnimated)
	{
		StartExit(Slot);
	}
	else
	{
		Children.RemoveAt(ChildIndex);
	}

	RestoreAnchor(Anchor);
	return bLeaveAnimated;
}

void SFlowList::MoveChild(const TSharedRef<SWidget>& Widget, int32 Position)
{
	const int32 From = FindChild(Widget, /*bExiting=*/false);
	if (From == INDEX_NONE)
	{
		return;
	}

	// Within one lap before the anchor is measured: a looping list keeps its place as its count changes.
	NormalizeLoop();
	const FFocusAnchor Anchor = CaptureAnchor();

	// The child to go in front of, counting children that aren't leaving and leaving out the one moving.
	int32 Before = Children.Num();
	int32 Seen = 0;
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		if (Index == From || Children[Index].bExiting)
		{
			continue;
		}
		if (Seen == Position)
		{
			Before = Index;
			break;
		}
		++Seen;
	}

	// Bubbled into place a neighbour at a time. Moving forward, everything past From shifts down one as it leaves.
	if (Before > From)
	{
		for (int32 Index = From; Index < Before - 1; ++Index)
		{
			Children.Swap(Index, Index + 1);
		}
	}
	else
	{
		for (int32 Index = From; Index > Before; --Index)
		{
			Children.Swap(Index, Index - 1);
		}
	}
	Invalidate(EInvalidateWidgetReason::ChildOrder);

	RestoreAnchor(Anchor);
}

void SFlowList::ClearChildren(bool bAnimate)
{
	TArray<TSharedRef<SWidget>> Widgets;
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		if (!Children[Index].bExiting)
		{
			Widgets.Add(Children[Index].GetWidget());
		}
	}
	for (const TSharedRef<SWidget>& Widget : Widgets)
	{
		RemoveChild(Widget, bAnimate);
	}
}

bool SFlowList::DropLeaving(const SWidget* Widget)
{
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		FSlot& Slot = Children[Index];
		if (Slot.bExiting && &Slot.GetWidget().Get() == Widget)
		{
			RestoreVisibility(Slot);
			Children.RemoveAt(Index);
			RebuildLiveOrder();
			Invalidate(EInvalidateWidgetReason::Layout);
			return true;
		}
	}
	return false;
}

void SFlowList::DropAllLeaving()
{
	bool bDropped = false;
	for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
	{
		if (Children[Index].bExiting)
		{
			RestoreVisibility(Children[Index]);
			Children.RemoveAt(Index);
			bDropped = true;
		}
	}
	if (bDropped)
	{
		RebuildLiveOrder();
		Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SFlowList::RebuildLiveOrder()
{
	++LayoutVersion;

	// A virtual list's slots say which item they show; the rest of its items have none.
	if (bVirtual)
	{
		LiveOrder.Init(INDEX_NONE, VirtualCount);
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			const FSlot& Slot = Children[Index];
			if (!Slot.bExiting && LiveOrder.IsValidIndex(Slot.ItemIndex))
			{
				LiveOrder[Slot.ItemIndex] = Index;
			}
		}
		return;
	}

	LiveOrder.Reset();
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		FSlot& Slot = Children[Index];
		if (Slot.bExiting)
		{
			continue;
		}
		Slot.bCollapsed = Slot.GetWidget()->GetVisibility() == EVisibility::Collapsed;
		if (Slot.bCollapsed)
		{
			Slot.ItemIndex = INDEX_NONE;
			Slot.bPlaced = false;
			continue;
		}
		Slot.ItemIndex = LiveOrder.Num();
		LiveOrder.Add(Index);
	}
}

void SFlowList::PlaceNewItems()
{
	for (const int32 Child : LiveOrder)
	{
		if (Child == INDEX_NONE)
		{
			continue;
		}
		FSlot& Slot = Children[Child];
		if (!Slot.bPlaced)
		{
			Slot.Place = static_cast<float>(Slot.ItemIndex);
			Slot.PlaceVelocity = 0.f;
			Slot.bPlaced = true;
		}
	}
}

SFlowList::FFocusAnchor SFlowList::CaptureAnchor() const
{
	FFocusAnchor Anchor;
	if (bVirtual)
	{
		// A virtual list keeps its focus by index - see NotifyVirtualInserted and NotifyVirtualRemoved.
		return Anchor;
	}
	if (LiveOrder.Num() > 0)
	{
		Anchor.FocusedIndex = IndexAt(Scroll);
		Anchor.Focused = &Children[LiveOrder[Anchor.FocusedIndex]];
	}
	if (LiveOrder.IsValidIndex(SelectedIndex))
	{
		Anchor.Selected = &Children[LiveOrder[SelectedIndex]];
	}
	return Anchor;
}

void SFlowList::RestoreAnchor(const FFocusAnchor& Anchor)
{
	RebuildLiveOrder();

	const int32 Count = LiveOrder.Num();
	if (Count == 0)
	{
		StopMotion();
		Scroll = 0.f;
		Target = 0.f;
		NearestIndex = INDEX_NONE;
		SetSelected(INDEX_NONE, false);
		Invalidate(EInvalidateWidgetReason::Layout);
		WakeUp();
		return;
	}

	// The focused item stays where it is: everything shifts by however far its index moved, and the items around it
	// slide into their new places from where they were drawn. Only the slots are compared - a removed one is gone.
	if (Transitions.bKeepFocusedItem && Anchor.Focused)
	{
		const int32 Child = FindChild(Anchor.Focused);
		if (Child != INDEX_NONE && Children[Child].ItemIndex != INDEX_NONE)
		{
			const int32 Delta = Children[Child].ItemIndex - Anchor.FocusedIndex;
			if (Delta != 0)
			{
				ShiftFrame(static_cast<float>(Delta));
			}
		}
	}

	PlaceNewItems();
	NearestIndex = IndexAt(Scroll);

	// The selected item stays selected wherever it went. If it left, whichever item slides into the focus is - and
	// focus, which was on the item that left, goes with it.
	int32 NewSelected = INDEX_NONE;
	if (Anchor.Selected)
	{
		const int32 Child = FindChild(Anchor.Selected);
		if (Child != INDEX_NONE)
		{
			NewSelected = Children[Child].ItemIndex;
		}
	}
	// Forced when it left: the next item usually slides into the same index, which is still a new item selected.
	const bool bSelectionLeft = NewSelected == INDEX_NONE;
	SetSelected(bSelectionLeft ? NearestIndex : NewSelected, bSelectionLeft, /*bForce=*/bSelectionLeft);

	if (!IsLooping())
	{
		const float Clamped = FMath::Clamp(Scroll, GetMinScroll(), GetMaxScroll());
		const bool bTargetOut = Motion == EMotion::Spring && (Target < GetMinScroll() || Target > GetMaxScroll());
		if (Motion != EMotion::Drag && (Clamped != Scroll || bTargetOut))
		{
			SpringTo(FMath::Clamp(bTargetOut ? Target : Scroll, GetMinScroll(), GetMaxScroll()), Velocity, false);
		}
	}

	Invalidate(EInvalidateWidgetReason::Layout);
	WakeUp();
}

int32 SFlowList::FindChild(const FSlot* Slot) const
{
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		if (&Children[Index] == Slot)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 SFlowList::FindChild(const TSharedRef<SWidget>& Widget, bool bExiting) const
{
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		const FSlot& Slot = Children[Index];
		if (Slot.bExiting == bExiting && Slot.GetWidget() == Widget)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void SFlowList::StartEnter(FSlot& Slot, float Delay)
{
	Slot.bEntering = true;
	Slot.bPreviewLeaving = false;
	Slot.bTransitionStarted = false;
	Slot.TransitionTime = 0.f;
	Slot.TransitionDelay = FMath::Max(Delay, 0.f);
	++LayoutVersion;
	WakeUp();
}

void SFlowList::StartExit(FSlot& Slot)
{
	Slot.bExiting = true;
	Slot.bEntering = false;
	Slot.bPreviewLeaving = false;
	Slot.bTransitionStarted = false;
	Slot.ItemIndex = INDEX_NONE;
	Slot.TransitionTime = 0.f;
	Slot.TransitionDelay = GetExitDelay();

	// Still drawn while it leaves, but no longer something to click.
	const EVisibility Visibility = Slot.GetWidget()->GetVisibility();
	if (Visibility.IsVisible())
	{
		Slot.SavedVisibility = Visibility;
		Slot.GetWidget()->SetVisibility(EVisibility::HitTestInvisible);
	}
	++LayoutVersion;
	WakeUp();
}

void SFlowList::ShiftFrame(float Delta)
{
	++LayoutVersion;
	Scroll += Delta;
	Target += Delta;
	CoastOrigin += Delta;
	Press.RawScroll += Delta;
	Press.StartScroll += Delta;
	for (TPair<double, float>& Sample : VelocitySamples)
	{
		Sample.Value += Delta;
	}
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		Children[Index].Place += Delta;
	}
}

void SFlowList::RestoreVisibility(FSlot& Slot)
{
	if (Slot.SavedVisibility.IsSet())
	{
		Slot.GetWidget()->SetVisibility(Slot.SavedVisibility.GetValue());
		Slot.SavedVisibility.Reset();
	}
}

TSharedPtr<SWidget> SFlowList::GetItemWidget(int32 Index) const
{
	const FSlot* Slot = FindItemSlot(Index);
	return Slot ? TSharedPtr<SWidget>(Slot->GetWidget()) : TSharedPtr<SWidget>();
}

int32 SFlowList::GetItemIndex(const TSharedRef<SWidget>& Widget) const
{
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		const FSlot& Slot = Children[Index];
		if (!Slot.bExiting && Slot.ItemIndex != INDEX_NONE && Slot.GetWidget() == Widget)
		{
			return Slot.ItemIndex;
		}
	}
	return INDEX_NONE;
}

float SFlowList::GetItemDistance(int32 Index) const
{
	if (const FSlot* Slot = FindItemSlot(Index))
	{
		return WrapDistance(Slot->Place - Scroll);
	}
	// A virtual list's item with no entry shown sits at its index.
	return bVirtual && LiveOrder.IsValidIndex(Index) ? WrapDistance(static_cast<float>(Index) - Scroll) : TNumericLimits<float>::Max();
}

void SFlowList::ForEachItem(TFunctionRef<void(int32 Index, const TSharedRef<SWidget>& Widget, float Distance)> Visitor) const
{
	for (int32 Index = 0; Index < LiveOrder.Num(); ++Index)
	{
		if (const FSlot* Slot = FindItemSlot(Index))
		{
			Visitor(Index, Slot->GetWidget(), WrapDistance(Slot->Place - Scroll));
		}
	}
}

void SFlowList::PlayIntro()
{
	TArray<TPair<float, int32>> Order;
	for (const int32 Child : LiveOrder)
	{
		if (Child == INDEX_NONE)
		{
			continue;
		}
		Order.Emplace(FMath::Abs(WrapDistance(Children[Child].Place - Scroll)), Child);
	}
	Order.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	for (int32 Rank = 0; Rank < Order.Num(); ++Rank)
	{
		StartEnter(Children[Order[Rank].Value], GetEnterDelay() + Transitions.Intro.Stagger * static_cast<float>(Rank));
	}
}

void SFlowList::UpdateViewCues()
{
	if (!UsesItemAnimations() || !Transitions.bPlayOnScrollIntoView)
	{
		bViewTracked = false;
		return;
	}
	const FGeometry& Geometry = GetTickSpaceGeometry();
	if (Geometry.GetLocalSize().X <= 0.f || Geometry.GetLocalSize().Y <= 0.f)
	{
		return;
	}

	// The layout this frame - what is drawn - says what is in view.
	GetLayout(Geometry);
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		FSlot& Slot = Children[Index];
		if (Slot.bExiting || Slot.ItemIndex == INDEX_NONE)
		{
			continue;
		}
		const bool bNow = CachedInView.IsValidIndex(Index) && CachedInView[Index];
		const bool bBusy = Slot.bEntering || Slot.bPreviewLeaving;
		if (!Slot.bViewKnown)
		{
			// First seen: in view when the list first shows, it was just there. Coming into view later - a virtual list's entry
			// made as it scrolls in, an item shown - it enters, unless it is already entering.
			Slot.bViewKnown = true;
			Slot.bInView = bNow;
			if (bViewTracked && bNow && !bBusy)
			{
				PendingCues.Emplace(Slot.GetWidget(), true, EFlowListItemCue::View);
			}
			continue;
		}
		if (bNow != Slot.bInView)
		{
			Slot.bInView = bNow;
			if (!bBusy)
			{
				PendingCues.Emplace(Slot.GetWidget(), bNow, EFlowListItemCue::View);
			}
		}
	}
	bViewTracked = true;
}

void SFlowList::FlushCues()
{
	if (PendingCues.Num() == 0)
	{
		return;
	}
	// Taken first: whoever hears may change the list, and with it the cues.
	TArray<TTuple<TWeakPtr<SWidget>, bool, EFlowListItemCue>> Cues = MoveTemp(PendingCues);
	PendingCues.Reset();
	for (const TTuple<TWeakPtr<SWidget>, bool, EFlowListItemCue>& Cue : Cues)
	{
		if (const TSharedPtr<SWidget> Widget = Cue.Get<0>().Pin())
		{
			OnItemTransitionStarted.ExecuteIfBound(Widget.ToSharedRef(), Cue.Get<1>(), Cue.Get<2>());
		}
	}
}

void SFlowList::PlayItemPreview(const TSharedRef<SWidget>& Widget, bool bExit)
{
	const int32 Child = FindChild(Widget, /*bExiting=*/false);
	if (Child == INDEX_NONE || Children[Child].ItemIndex == INDEX_NONE)
	{
		return;
	}
	FSlot& Slot = Children[Child];
	if (!bExit)
	{
		StartEnter(Slot, GetEnterDelay());
		return;
	}
	Slot.bEntering = false;
	Slot.bPreviewLeaving = true;
	Slot.bTransitionStarted = false;
	Slot.TransitionTime = 0.f;
	Slot.TransitionDelay = GetExitDelay();
	++LayoutVersion;
	WakeUp();
}

void SFlowList::PlayExitPreview()
{
	TArray<TPair<float, int32>> Order;
	for (const int32 Child : LiveOrder)
	{
		if (Child != INDEX_NONE)
		{
			Order.Emplace(FMath::Abs(WrapDistance(Children[Child].Place - Scroll)), Child);
		}
	}
	Order.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	for (int32 Rank = 0; Rank < Order.Num(); ++Rank)
	{
		FSlot& Slot = Children[Order[Rank].Value];
		Slot.bEntering = false;
		Slot.bPreviewLeaving = true;
		Slot.bTransitionStarted = false;
		Slot.TransitionTime = 0.f;
		Slot.TransitionDelay = GetExitDelay() + Transitions.Intro.Stagger * static_cast<float>(Rank);
	}
	++LayoutVersion;
	WakeUp();
}

// =====================================================================================================
// Scroll model
// =====================================================================================================

bool SFlowList::IsLooping() const
{
	return Scrolling.bLoop && LiveOrder.Num() > 1;
}

float SFlowList::GetMaxScroll() const
{
	const int32 Count = LiveOrder.Num();
	if (Count <= 1)
	{
		return 0.f;
	}
	float Max = static_cast<float>(Count - 1);
	if (Scrolling.bStopAtContentEnd && Layout.Type == EFlowListPathType::Line && CachedContentEndScroll >= 0.f)
	{
		Max = FMath::Min(Max, CachedContentEndScroll);
	}
	return Max;
}

float SFlowList::WrapDistance(float Distance) const
{
	if (!IsLooping())
	{
		return Distance;
	}
	const float Count = static_cast<float>(LiveOrder.Num());
	Distance = FMath::Fmod(Distance, Count);
	if (Distance < -Count * 0.5f)
	{
		Distance += Count;
	}
	else if (Distance >= Count * 0.5f)
	{
		Distance -= Count;
	}
	return Distance;
}

int32 SFlowList::IndexAt(float Position) const
{
	const int32 Count = LiveOrder.Num();
	if (Count == 0)
	{
		return INDEX_NONE;
	}
	const int32 Rounded = FMath::RoundToInt(Position);
	if (IsLooping())
	{
		return ((Rounded % Count) + Count) % Count;
	}
	return FMath::Clamp(Rounded, 0, Count - 1);
}

float SFlowList::ApplyRubberBand(float Raw) const
{
	if (IsLooping())
	{
		return Raw;
	}
	const float Min = GetMinScroll();
	const float Max = GetMaxScroll();
	if (Scrolling.Overscroll == EFlowListOverscroll::Clamp)
	{
		return FMath::Clamp(Raw, Min, Max);
	}

	// UIScrollView's rubber band: the further past the end, the less each pixel of drag moves it, approaching a whole
	// view's length but never reaching it.
	const float Dimension = FMath::Max(CachedViewItems, 1.f);
	const float Coefficient = FMath::Clamp(Scrolling.Elasticity, 0.05f, 1.f);
	auto Band = [Dimension, Coefficient](float Over)
	{
		return (1.f - 1.f / (Over * Coefficient / Dimension + 1.f)) * Dimension;
	};
	if (Raw < Min)
	{
		return Min - Band(Min - Raw);
	}
	if (Raw > Max)
	{
		return Max + Band(Raw - Max);
	}
	return Raw;
}

void SFlowList::SpringTo(float InTarget, float InVelocity, bool bInSelectionFollows)
{
	Motion = EMotion::Spring;
	Target = InTarget;
	Velocity = InVelocity;
	bSelectionFollowsSpring = bInSelectionFollows;
	WakeUp();
}

void SFlowList::StopMotion()
{
	Motion = EMotion::Idle;
	Velocity = 0.f;
	Target = Scroll;
}

void SFlowList::SettleInPlace()
{
	using namespace FlowListPrivate;

	if (LiveOrder.Num() == 0)
	{
		StopMotion();
		return;
	}
	if (!IsLooping() && (Scroll < GetMinScroll() || Scroll > GetMaxScroll()))
	{
		SpringTo(FMath::Clamp(Scroll, GetMinScroll(), GetMaxScroll()), 0.f, true);
		return;
	}
	if (Scrolling.bSnapToItems)
	{
		float Snap = FMath::RoundToFloat(Scroll);
		if (!IsLooping())
		{
			Snap = FMath::Clamp(Snap, GetMinScroll(), GetMaxScroll());
		}
		if (!FMath::IsNearlyEqual(Snap, Scroll, RestDistance))
		{
			SpringTo(Snap, 0.f, true);
			return;
		}
	}
	StopMotion();
}

void SFlowList::ReleaseDrag(float StartScroll)
{
	using namespace FlowListPrivate;

	const float ReleaseVelocity = FMath::Clamp(EstimateVelocity(), -MaxFlingSpeed, MaxFlingSpeed);
	VelocitySamples.Reset();
	Motion = EMotion::Idle;

	if (LiveOrder.Num() == 0)
	{
		StopMotion();
		return;
	}

	const float Min = GetMinScroll();
	const float Max = GetMaxScroll();

	// Let go past an end: spring back, keeping whatever speed it was thrown with.
	if (!IsLooping() && (Scroll < Min || Scroll > Max))
	{
		SpringTo(FMath::Clamp(Scroll, Min, Max), ReleaseVelocity, true);
		return;
	}

	if (Scrolling.bSnapToItems)
	{
		// Where it would have coasted to, rounded to an item, and a spring there that starts at the release speed: one
		// unbroken motion from the finger to the item, the way paging on iOS feels.
		const float From = FMath::RoundToFloat(StartScroll);
		float Snap = FMath::RoundToFloat(Scroll + ReleaseVelocity * FMath::Max(Scrolling.FlingCoast, 0.f));
		if (FMath::Abs(ReleaseVelocity) >= FlickSpeed && Snap == From)
		{
			Snap = From + FMath::Sign(ReleaseVelocity);
		}
		if (Scrolling.MaxItemsPerFling > 0)
		{
			const float Limit = static_cast<float>(Scrolling.MaxItemsPerFling);
			Snap = FMath::Clamp(Snap, From - Limit, From + Limit);
		}
		if (!IsLooping())
		{
			Snap = FMath::Clamp(Snap, Min, Max);
		}
		SpringTo(Snap, ReleaseVelocity, true);
		return;
	}

	if (FMath::Abs(ReleaseVelocity) > 0.05f && Scrolling.FlingCoast > 0.f)
	{
		Motion = EMotion::Coast;
		CoastOrigin = Scroll;
		CoastVelocity = ReleaseVelocity;
		CoastTime = 0.f;
		Velocity = ReleaseVelocity;
		WakeUp();
		return;
	}
	StopMotion();
}

bool SFlowList::StepBy(float Steps, bool bWrap)
{
	if (LiveOrder.Num() == 0)
	{
		return false;
	}

	const float Base = Motion == EMotion::Spring ? Target : Scroll;

	// Already at the end it is pushed toward: the wheel goes on to whatever scrolls behind the list. Only a whole notch
	// counts as hitting it, so a trackpad's stream of fractions doesn't chatter.
	if (!IsLooping() && !bWrap
		&& ((Steps > 0.f && Base >= GetMaxScroll() - 0.001f) || (Steps < 0.f && Base <= GetMinScroll() + 0.001f)))
	{
		WheelRemainder = 0.f;
		if (FMath::Abs(Steps) >= 1.f)
		{
			ReachedEdge();
		}
		return false;
	}

	float NewTarget = 0.f;
	if (Scrolling.bSnapToItems)
	{
		// A trackpad sends fractions of a notch: they add up until they make a whole item.
		WheelRemainder += Steps;
		const float Whole = FMath::TruncToFloat(WheelRemainder);
		if (Whole == 0.f)
		{
			return true;
		}
		WheelRemainder -= Whole;
		NewTarget = FMath::RoundToFloat(Base) + Whole;
	}
	else
	{
		NewTarget = Base + Steps;
	}

	if (!IsLooping())
	{
		const float Min = GetMinScroll();
		const float Max = GetMaxScroll();
		if (bWrap)
		{
			if (NewTarget > Max + 0.5f)
			{
				NewTarget = Min;
			}
			else if (NewTarget < Min - 0.5f)
			{
				NewTarget = Max;
			}
		}
		const float Clamped = FMath::Clamp(NewTarget, Min, Max);
		if (FMath::IsNearlyEqual(Clamped, Base, 0.001f))
		{
			// Already at the end: say so, and let the wheel go on to whatever scrolls behind the list.
			WheelRemainder = 0.f;
			ReachedEdge();
			return false;
		}
		NewTarget = Clamped;
	}

	SpringTo(NewTarget, Velocity, true);
	return true;
}

void SFlowList::ScrollToIndex(int32 Index, bool bAnimate)
{
	const int32 Count = LiveOrder.Num();
	if (Count == 0)
	{
		return;
	}

	float NewTarget = 0.f;
	if (IsLooping())
	{
		Index = ((Index % Count) + Count) % Count;
		const float Base = Motion == EMotion::Spring ? Target : Scroll;
		NewTarget = Base + WrapDistance(static_cast<float>(Index) - Base);
	}
	else
	{
		Index = FMath::Clamp(Index, 0, Count - 1);
		NewTarget = FMath::Clamp(static_cast<float>(Index), GetMinScroll(), GetMaxScroll());
	}

	SetSelected(Index, true);

	if (bAnimate)
	{
		SpringTo(NewTarget, Velocity, false);
		return;
	}
	StopMotion();
	Scroll = NewTarget;
	Target = NewTarget;
	NearestIndex = IndexAt(Scroll);
	Invalidate(EInvalidateWidgetReason::Layout);
	WakeUp();
}

bool SFlowList::ScrollBy(int32 Steps, bool bWrap, bool bQuiet)
{
	const int32 Count = LiveOrder.Num();
	if (Count == 0 || Steps == 0)
	{
		return false;
	}

	const int32 From = LiveOrder.IsValidIndex(SelectedIndex) ? SelectedIndex : IndexAt(Scroll);
	if (IsLooping())
	{
		// Round a loop, keep going the way asked rather than the short way round.
		const int32 To = (((From + Steps) % Count) + Count) % Count;
		SetSelected(To, true);
		const float Base = FMath::RoundToFloat(Motion == EMotion::Spring ? Target : Scroll);
		SpringTo(Base + WrapDistance(static_cast<float>(From) - Base) + static_cast<float>(Steps), Velocity, false);
		return true;
	}

	int32 To = From + Steps;
	if (To < 0 || To >= Count)
	{
		if (!bWrap)
		{
			To = FMath::Clamp(To, 0, Count - 1);
			if (To == From)
			{
				if (!bQuiet)
				{
					ReachedEdge();
				}
				return false;
			}
		}
		else
		{
			To = ((To % Count) + Count) % Count;
		}
	}
	ScrollToIndex(To, true);
	return true;
}

void SFlowList::SetScrollPosition(float Position, bool bAnimate)
{
	const int32 Count = LiveOrder.Num();
	if (Count == 0)
	{
		Scroll = Target = 0.f;
		return;
	}
	if (!IsLooping())
	{
		Position = FMath::Clamp(Position, GetMinScroll(), GetMaxScroll());
	}
	if (bAnimate)
	{
		SpringTo(Position, Velocity, true);
		return;
	}
	StopMotion();
	Scroll = Position;
	Target = Position;
	NearestIndex = IndexAt(Scroll);

	// Moved there outright - a scroll bar's thumb dragged, say: the item there is selected, unless scrolling doesn't select.
	if (Scrolling.bSelectWhileScrolling)
	{
		SetSelected(NearestIndex, false);
	}
	Invalidate(EInvalidateWidgetReason::Layout);
	WakeUp();
}

float SFlowList::GetScrollFraction() const
{
	const int32 Count = LiveOrder.Num();
	if (Count == 0)
	{
		return 0.f;
	}
	if (IsLooping())
	{
		float Wrapped = FMath::Fmod(Scroll, static_cast<float>(Count));
		if (Wrapped < 0.f)
		{
			Wrapped += static_cast<float>(Count);
		}
		return Wrapped / static_cast<float>(Count);
	}
	const float Range = GetMaxScroll() - GetMinScroll();
	return Range > 0.f ? FMath::Clamp((Scroll - GetMinScroll()) / Range, 0.f, 1.f) : 0.f;
}

void SFlowList::SetScrollFraction(float Fraction, bool bAnimate)
{
	const int32 Count = LiveOrder.Num();
	if (Count == 0)
	{
		return;
	}
	const float Position = IsLooping()
		? Fraction * static_cast<float>(Count)
		: FMath::Lerp(GetMinScroll(), GetMaxScroll(), FMath::Clamp(Fraction, 0.f, 1.f));
	SetScrollPosition(Position, bAnimate);
}

float SFlowList::GetViewFraction() const
{
	const int32 Count = LiveOrder.Num();
	return Count == 0 ? 1.f : FMath::Clamp(CachedViewItems / static_cast<float>(Count), 0.f, 1.f);
}

bool SFlowList::StepScroll(float DeltaTime)
{
	using namespace FlowListPrivate;

	switch (Motion)
	{
	case EMotion::Idle:
		return false;

	case EMotion::Drag:
		return true;

	case EMotion::Coast:
	{
		// Exponential decay, solved exactly: the same glide at any frame rate.
		CoastTime += DeltaTime;
		const float Tau = FMath::Max(Scrolling.FlingCoast, 0.01f);
		const float Decay = FMath::Exp(-CoastTime / Tau);
		Scroll = CoastOrigin + CoastVelocity * Tau * (1.f - Decay);
		Velocity = CoastVelocity * Decay;

		if (!IsLooping() && (Scroll < GetMinScroll() || Scroll > GetMaxScroll()))
		{
			const float Edge = FMath::Clamp(Scroll, GetMinScroll(), GetMaxScroll());
			if (Scrolling.Overscroll == EFlowListOverscroll::Elastic)
			{
				// Carries on a little past the end with what's left of its speed, and springs back.
				SpringTo(Edge, Velocity * 0.5f, true);
			}
			else
			{
				Scroll = Edge;
				StopMotion();
			}
			ReachedEdge();
			return Motion != EMotion::Idle;
		}
		if (FMath::Abs(Velocity) < 0.02f)
		{
			StopMotion();
		}
		return Motion != EMotion::Idle;
	}

	case EMotion::Spring:
	{
		FWidgetSpringState State;
		State.Value = Scroll;
		State.Velocity = Velocity;
		const bool bAtRest = UWidgetAnimSpringLibrary::AdvanceSpring(Scrolling.ScrollSpring, Target, State, DeltaTime, RestDistance, RestSpeed);
		Scroll = State.Value;
		Velocity = State.Velocity;
		if (!IsLooping() && Scrolling.Overscroll == EFlowListOverscroll::Clamp)
		{
			Scroll = FMath::Clamp(Scroll, GetMinScroll(), GetMaxScroll());
		}
		if (bAtRest)
		{
			Scroll = Target;
			StopMotion();
		}
		return Motion != EMotion::Idle;
	}

	case EMotion::Stick:
	{
		// At the speed UpdateStick worked out this frame. Held into an end it stops there - no rubber band to hold open.
		Scroll += Velocity * DeltaTime;
		if (!IsLooping())
		{
			const float Edge = FMath::Clamp(Scroll, GetMinScroll(), GetMaxScroll());
			if (Edge != Scroll)
			{
				Scroll = Edge;
				if (!bStickAtEdge)
				{
					bStickAtEdge = true;
					ReachedEdge();
				}
			}
			else
			{
				bStickAtEdge = false;
			}
		}
		Target = Scroll;
		return true;
	}
	}
	return false;
}

bool SFlowList::StepItems(float DeltaTime)
{
	using namespace FlowListPrivate;

	bool bBusy = false;
	TArray<TSharedRef<SWidget>, TInlineAllocator<4>> Exited;

	// Items whose own Widget Animations are to start this frame: an entrance or an exit past its delay.
	TArray<TPair<TSharedRef<SWidget>, bool>, TInlineAllocator<4>> Started;

	// Items an exit preview has finished with, by distance from the focus: they enter again, nearest first.
	TArray<TPair<float, TSharedRef<SWidget>>, TInlineAllocator<4>> Returning;
	auto NoteStart = [this, &Started](FSlot& Slot, bool bEnter)
	{
		if (!Slot.bTransitionStarted && Slot.TransitionTime >= Slot.TransitionDelay)
		{
			Slot.bTransitionStarted = true;
			if (UsesItemAnimations())
			{
				Started.Emplace(Slot.GetWidget(), bEnter);
			}
		}
	};

	for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
	{
		FSlot& Slot = Children[Index];

		if (Slot.bExiting)
		{
			Slot.TransitionTime += DeltaTime;
			NoteStart(Slot, /*bEnter=*/false);
			if (Slot.TransitionTime >= Slot.TransitionDelay + GetExitSeconds())
			{
				Exited.Add(Slot.GetWidget());
				RestoreVisibility(Slot);
				Children.RemoveAt(Index);
			}
			else
			{
				bBusy = true;
			}
			continue;
		}

		if (Slot.ItemIndex == INDEX_NONE)
		{
			continue;
		}

		// Sliding to its index - the short way round a looping list.
		const float Goal = Slot.Place + WrapDistance(static_cast<float>(Slot.ItemIndex) - Slot.Place);
		if (Slot.Place != Goal || Slot.PlaceVelocity != 0.f)
		{
			FWidgetSpringState State;
			State.Value = Slot.Place;
			State.Velocity = Slot.PlaceVelocity;
			if (UWidgetAnimSpringLibrary::AdvanceSpring(Transitions.ReorderSpring, Goal, State, DeltaTime, PlaceRestDistance, PlaceRestSpeed))
			{
				// At rest it is exactly its index; round a loop that is the same place as Goal.
				Slot.Place = static_cast<float>(Slot.ItemIndex);
				Slot.PlaceVelocity = 0.f;
			}
			else
			{
				Slot.Place = State.Value;
				Slot.PlaceVelocity = State.Velocity;
				bBusy = true;
			}
		}

		// An exit preview: the item stays, and comes back in once its exit is done.
		if (Slot.bPreviewLeaving)
		{
			Slot.TransitionTime += DeltaTime;
			NoteStart(Slot, /*bEnter=*/false);
			if (Slot.bTransitionStarted && Slot.TransitionTime >= Slot.TransitionDelay + GetExitSeconds())
			{
				Slot.bPreviewLeaving = false;
				Returning.Emplace(FMath::Abs(WrapDistance(Slot.Place - Scroll)), Slot.GetWidget());
			}
			else
			{
				bBusy = true;
			}
		}

		if (Slot.bEntering)
		{
			Slot.TransitionTime += DeltaTime;
			NoteStart(Slot, /*bEnter=*/true);
			if (Slot.bTransitionStarted && Slot.TransitionTime >= Slot.TransitionDelay + GetEnterSeconds())
			{
				Slot.bEntering = false;
			}
			else
			{
				bBusy = true;
			}
		}
	}

	if (Returning.Num() > 0)
	{
		Returning.Sort([](const TPair<float, TSharedRef<SWidget>>& A, const TPair<float, TSharedRef<SWidget>>& B) { return A.Key < B.Key; });
		for (int32 Rank = 0; Rank < Returning.Num(); ++Rank)
		{
			const int32 Child = FindChild(Returning[Rank].Value, /*bExiting=*/false);
			if (Child != INDEX_NONE)
			{
				StartEnter(Children[Child], GetEnterDelay() + Transitions.Intro.Stagger * static_cast<float>(Rank));
			}
		}
		bBusy = true;
	}

	// Told before any exit below lets its widget go: an exit that starts and ends in one frame still starts.
	for (const TPair<TSharedRef<SWidget>, bool>& Start : Started)
	{
		OnItemTransitionStarted.ExecuteIfBound(Start.Key, Start.Value, EFlowListItemCue::Transition);
	}

	if (Exited.Num() > 0)
	{
		RebuildLiveOrder();

		// Told only once the panel is consistent again, since whoever hears may add or remove items. A virtual list's entry
		// goes back to the pool.
		for (const TSharedRef<SWidget>& Widget : Exited)
		{
			if (bVirtual)
			{
				OnReleaseEntry.ExecuteIfBound(INDEX_NONE, Widget);
			}
			else
			{
				OnItemExited.ExecuteIfBound(Widget);
			}
		}
	}
	return bBusy;
}

void SFlowList::UpdateNearest()
{
	if (LiveOrder.Num() == 0)
	{
		return;
	}
	const int32 Now = IndexAt(Scroll);
	if (Now == NearestIndex)
	{
		return;
	}
	NearestIndex = Now;

	// While a person moves the list, the selection follows whatever passes the focus - a dial clicking past each item.
	// A list sent to an item by code or navigation already has it selected, and doesn't flicker through the ones between.
	const bool bFollow = Motion == EMotion::Drag || Motion == EMotion::Coast || Motion == EMotion::Stick
		|| (Motion == EMotion::Spring && bSelectionFollowsSpring);
	if (bFollow)
	{
		// Select While Scrolling off: scrolling never selects.
		if (Scrolling.bSelectWhileScrolling)
		{
			SetSelected(Now, true);
		}
	}
}

void SFlowList::SetSelected(int32 Index, bool bMoveFocus, bool bForce)
{
	if (Index == SelectedIndex && !bForce)
	{
		return;
	}
	const int32 Previous = SelectedIndex;
	SelectedIndex = Index;

	// Play On Selection: told on the next step, once whatever changed the selection is done changing the list.
	if (UsesItemAnimations() && Transitions.bPlayOnSelection)
	{
		if (const FSlot* Old = FindItemSlot(Previous))
		{
			PendingCues.Emplace(Old->GetWidget(), false, EFlowListItemCue::Selection);
		}
		if (const FSlot* New = FindItemSlot(Index))
		{
			PendingCues.Emplace(New->GetWidget(), true, EFlowListItemCue::Selection);
		}
		WakeUp();
	}

	if (bMoveFocus)
	{
		MoveUserFocusTo(Index);
	}
	OnSelectionChanged.ExecuteIfBound(Index, Previous);
}

void SFlowList::MoveUserFocusTo(int32 Index)
{
	if (!Scrolling.bFocusFollowsSelection || bDesignTime || bNavigating || !LiveOrder.IsValidIndex(Index) || !FSlateApplication::IsInitialized())
	{
		return;
	}

	// A virtual list's item may have no entry yet - it is about to scroll into view: it gets one now, to take the focus.
	if (bVirtual && !FindItemSlot(Index))
	{
		RealizeIndex(Index);
	}
	const FSlot* ItemSlot = FindItemSlot(Index);
	TSharedPtr<SWidget> FocusTarget = ItemSlot ? FlowListPrivate::FindFocusable(ItemSlot->GetWidget(), 0) : nullptr;
	if (!FocusTarget.IsValid())
	{
		if (!bFocusable)
		{
			return;
		}
		FocusTarget = AsShared();
	}

	// Only the users whose focus is already in the list follow it - another player's cursor elsewhere stays put.
	FSlateApplication& Application = FSlateApplication::Get();
	TArray<int32, TInlineAllocator<4>> Users;
	Application.ForEachUser([this, &Users](FSlateUser& User)
	{
		const int32 UserIndex = User.GetUserIndex();
		if (HasUserFocusedDescendants(UserIndex) || HasUserFocus(UserIndex).IsSet())
		{
			Users.Add(UserIndex);
		}
	});

	TGuardValue<bool> Guard(bMovingFocus, true);
	for (const int32 UserIndex : Users)
	{
		Application.SetUserFocus(UserIndex, FocusTarget, EFocusCause::Navigation);
	}
}

void SFlowList::ReachedEdge()
{
	OnEdgeReached.ExecuteIfBound();
}

// =====================================================================================================
// Pointer
// =====================================================================================================

bool SFlowList::MatchesPress(const FPointerEvent& Event) const
{
	return Press.bActive && Event.GetPointerIndex() == Press.PointerIndex && Event.IsTouchEvent() == Press.bTouch;
}

void SFlowList::BeginPress(const FPointerEvent& Event)
{
	Press = FPress();
	Press.bActive = true;
	Press.bTouch = Event.IsTouchEvent();
	Press.PointerIndex = Event.GetPointerIndex();
	Press.UserIndex = Event.GetUserIndex();
	Press.StartAbsolute = FVector2f(Event.GetScreenSpacePosition());
	Press.LastAbsolute = Press.StartAbsolute;
	Press.StartScroll = Scroll;
	Press.RawScroll = Scroll;
	Press.PressedSlot = SlotUnderPoint(Press.StartAbsolute);
	if (Press.PressedSlot)
	{
		Press.PressedWidget = Press.PressedSlot->GetWidget();
	}
}

bool SFlowList::HandlePointerMove(const FVector2f& Absolute)
{
	using namespace FlowListPrivate;

	const FGeometry& Geometry = GetTickSpaceGeometry();
	FSlateApplication& Application = FSlateApplication::Get();

	if (!Press.bDragging)
	{
		const FVector2f Travel = Absolute - Press.StartAbsolute;
		if (Travel.Size() < Application.GetDragTriggerDistance())
		{
			return false;
		}
		Press.bClickCancelled = true;
		if (!Scrolling.bDragToScroll || LiveOrder.Num() == 0 || bDesignTime)
		{
			return false;
		}

		// Mostly across the list: the drag belongs to whatever scrolls the other way behind it - a carousel inside a
		// vertical scroll box. Once the list has the pointer, it keeps it whichever way it goes.
		const FVector2f LocalTravel = FVector2f(Geometry.AbsoluteToLocal(Absolute)) - FVector2f(Geometry.AbsoluteToLocal(Press.StartAbsolute));
		const float Along = FMath::Abs(FVector2f::DotProduct(LocalTravel, CachedDragAxis));
		const float Across = FMath::Abs(FVector2f::CrossProduct(LocalTravel, CachedDragAxis));
		if (!HasMouseCapture() && Across > Along * AxisLockRatio)
		{
			Press = FPress();
			return false;
		}

		Press.bDragging = true;
		Press.LastAbsolute = Absolute;
		Press.RawScroll = Scroll;
		Motion = EMotion::Drag;
		Velocity = 0.f;
		VelocitySamples.Reset();
		AddVelocitySample();

		// Take the press away from the item it started on: dragging a list never clicks what it was grabbed by.
		if (!HasMouseCapture() && !Press.bTouch)
		{
			Application.ReleaseAllPointerCapture(static_cast<int32>(Press.UserIndex));
		}
		WakeUp();
		return true;
	}

	const FVector2f Delta = FVector2f(Geometry.AbsoluteToLocal(Absolute)) - FVector2f(Geometry.AbsoluteToLocal(Press.LastAbsolute));
	Press.LastAbsolute = Absolute;
	const float AlongPixels = FVector2f::DotProduct(Delta, CachedDragAxis);
	Press.RawScroll -= AlongPixels / FMath::Max(CachedPixelsPerItem, 1.f) * Scrolling.DragSensitivity;
	Scroll = ApplyRubberBand(Press.RawScroll);
	AddVelocitySample();
	Invalidate(EInvalidateWidgetReason::Layout);
	return true;
}

void SFlowList::EndPress(const FVector2f& Absolute)
{
	const FPress Ended = Press;
	Press = FPress();

	if (Ended.bDragging)
	{
		ReleaseDrag(Ended.StartScroll);
	}
	else if (Ended.bCaught)
	{
		SettleInPlace();
	}

	// Items may have come and gone during the press: the click goes to the item pressed, wherever it is now.
	const bool bClick = !Ended.bDragging && !Ended.bCaught && !Ended.bClickCancelled && Ended.PressedSlot
		&& SlotUnderPoint(Absolute) == Ended.PressedSlot;
	const int32 PressedChild = bClick ? FindChild(Ended.PressedSlot) : INDEX_NONE;
	const bool bSameWidget = PressedChild != INDEX_NONE && Ended.PressedWidget.Pin() == Children[PressedChild].GetWidget();
	const int32 PressedIndex = bSameWidget ? Children[PressedChild].ItemIndex : INDEX_NONE;
	if (PressedIndex != INDEX_NONE)
	{
		if (Scrolling.bClickToFocus)
		{
			ScrollToIndex(PressedIndex, true);
		}
		else if (!Scrolling.bSelectWhileScrolling)
		{
			// Scrolling doesn't select: a click is how an item is chosen, brought to the focus or not.
			SetSelected(PressedIndex, true);
		}
		OnItemClicked.ExecuteIfBound(PressedIndex);
	}
	WakeUp();
}

void SFlowList::PollPointer()
{
	// An item holding the mouse - a button pressed - keeps its moves to itself, so the list reads the cursor directly
	// to see the press turn into a drag.
	if (!Press.bActive || Press.bTouch || HasMouseCapture() || !FSlateApplication::IsInitialized())
	{
		return;
	}
	FSlateApplication& Application = FSlateApplication::Get();
	const FVector2f Cursor(Application.GetCursorPos());
	if (!Application.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
	{
		EndPress(Cursor);
		return;
	}
	HandlePointerMove(Cursor);
}

void SFlowList::AddVelocitySample()
{
	const double Now = FPlatformTime::Seconds();
	VelocitySamples.Emplace(Now, Scroll);
	while (VelocitySamples.Num() > 2 && Now - VelocitySamples[0].Key > FlowListPrivate::VelocityWindow * 2.0)
	{
		VelocitySamples.RemoveAt(0, EAllowShrinking::No);
	}
}

float SFlowList::EstimateVelocity() const
{
	using namespace FlowListPrivate;

	if (VelocitySamples.Num() < 2)
	{
		return 0.f;
	}
	const double Now = FPlatformTime::Seconds();
	const TPair<double, float>& Last = VelocitySamples.Last();
	if (Now - Last.Key > StillTime)
	{
		return 0.f;
	}

	int32 First = VelocitySamples.Num() - 2;
	while (First > 0 && Last.Key - VelocitySamples[First - 1].Key <= VelocityWindow)
	{
		--First;
	}
	const double Span = Last.Key - VelocitySamples[First].Key;
	if (Span < 0.004)
	{
		return 0.f;
	}
	return static_cast<float>((Last.Value - VelocitySamples[First].Value) / Span);
}

const SFlowList::FSlot* SFlowList::SlotUnderPoint(const FVector2f& Absolute) const
{
	const TArray<FItemLayout>& Items = GetLayout(GetTickSpaceGeometry());

	// Front to back: whichever is drawn on top is the one under the pointer.
	for (int32 Index = Items.Num() - 1; Index >= 0; --Index)
	{
		const FSlot& Slot = Children[Items[Index].Child];
		if (Slot.ItemIndex != INDEX_NONE && Items[Index].Geometry.IsUnderLocation(Absolute))
		{
			return &Slot;
		}
	}
	return nullptr;
}

FReply SFlowList::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	using namespace FlowListPrivate;

	const bool bTouch = MouseEvent.IsTouchEvent();
	if (LiveOrder.Num() == 0 || bDesignTime || (!bTouch && MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton))
	{
		return FReply::Unhandled();
	}

	// A second finger while the first is down is ignored. A touch left behind by an item that kept its release is not.
	if (Press.bActive && !(Press.bTouch && !HasMouseCapture()))
	{
		return FReply::Unhandled();
	}

	BeginPress(MouseEvent);

	// A press on a list in flight catches it where it is, and doesn't click whatever was passing under the pointer.
	const bool bInFlight = Motion == EMotion::Coast || (Motion == EMotion::Spring && FMath::Abs(Velocity) > CatchSpeed);
	if (Scrolling.bDragToScroll && bInFlight)
	{
		Press.bCaught = true;
		StopMotion();
		WakeUp();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	WakeUp();
	return FReply::Unhandled();
}

FReply SFlowList::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Nothing under the pointer took the press: the list does, to see it through to a drag or a click.
	if (MatchesPress(MouseEvent))
	{
		FReply Reply = FReply::Handled().CaptureMouse(SharedThis(this));
		if (bFocusable)
		{
			Reply.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
		}
		return Reply;
	}
	return FReply::Unhandled();
}

FReply SFlowList::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MatchesPress(MouseEvent))
	{
		EndPress(FVector2f(MouseEvent.GetScreenSpacePosition()));
		return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SFlowList::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MatchesPress(MouseEvent) && HandlePointerMove(FVector2f(MouseEvent.GetScreenSpacePosition())))
	{
		return HasMouseCapture() ? FReply::Handled() : FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SFlowList::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	return OnMouseMove(MyGeometry, TouchEvent);
}

FReply SFlowList::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	return OnMouseButtonUp(MyGeometry, TouchEvent);
}

void SFlowList::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SPanel::OnMouseCaptureLost(CaptureLostEvent);

	// Lost mid-drag - the window lost focus, a menu opened: let go where it is.
	if (Press.bActive && (Press.bDragging || Press.bCaught))
	{
		const FPress Ended = Press;
		Press = FPress();
		if (Ended.bDragging)
		{
			ReleaseDrag(Ended.StartScroll);
		}
		else
		{
			SettleInPlace();
		}
		WakeUp();
	}
}

FReply SFlowList::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!Scrolling.bWheelScroll || LiveOrder.Num() == 0 || Press.bDragging || bDesignTime)
	{
		return FReply::Unhandled();
	}
	// Wheel down moves on through the list, whichever way it runs.
	const float Steps = -MouseEvent.GetWheelDelta() * Scrolling.WheelStep;
	return StepBy(Steps, false) ? FReply::Handled() : FReply::Unhandled();
}

FNavigationReply SFlowList::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	if (!Scrolling.bNavigation || LiveOrder.Num() == 0 || bDesignTime)
	{
		return SPanel::OnNavigation(MyGeometry, InNavigationEvent);
	}

	const bool bHorizontal = Layout.Orientation == Orient_Horizontal;
	int32 Step = 0;
	switch (InNavigationEvent.GetNavigationType())
	{
	case EUINavigation::Next:     Step = 1; break;
	case EUINavigation::Previous: Step = -1; break;
	case EUINavigation::Right:    Step = bHorizontal ? 1 : 0; break;
	case EUINavigation::Left:     Step = bHorizontal ? -1 : 0; break;
	case EUINavigation::Down:     Step = bHorizontal ? 0 : 1; break;
	case EUINavigation::Up:       Step = bHorizontal ? 0 : -1; break;
	default: break;
	}
	if (Step == 0)
	{
		return SPanel::OnNavigation(MyGeometry, InNavigationEvent);
	}

	// Focus is moved by the reply, not by the selection changing under it.
	TGuardValue<bool> Guard(bNavigating, true);

	if (!Scrolling.bSelectWhileScrolling)
	{
		// A stick - navigation with no D-pad or arrow key behind it - scrolls, a notch an item, and selects nothing.
		const bool bStep = StepKeyFrame == GFrameCounter;
		if (!bStep)
		{
			return StepBy(static_cast<float>(Step), Scrolling.bWrapNavigation) ? FNavigationReply::Stop() : FNavigationReply::Escape();
		}

		// A step from where the list is: with the selection scrolled away, the first press selects the item at the focus.
		const int32 AtFocus = IndexAt(Motion == EMotion::Spring ? Target : Scroll);
		if (AtFocus != INDEX_NONE && AtFocus != SelectedIndex)
		{
			ScrollToIndex(AtFocus, true);
			if (const FSlot* ItemSlot = FindItemSlot(SelectedIndex))
			{
				if (const TSharedPtr<SWidget> Focus = FlowListPrivate::FindFocusable(ItemSlot->GetWidget(), 0))
				{
					return FNavigationReply::Explicit(Focus);
				}
			}
			return FNavigationReply::Stop();
		}
	}

	if (!ScrollBy(Step, Scrolling.bWrapNavigation, /*bQuiet=*/true))
	{
		// At the end: navigation carries on out of the list, to whatever is that way - quietly, it isn't a bump.
		return FNavigationReply::Escape();
	}
	if (bVirtual && LiveOrder.IsValidIndex(SelectedIndex) && !FindItemSlot(SelectedIndex))
	{
		RealizeIndex(SelectedIndex);
	}
	if (const FSlot* ItemSlot = FindItemSlot(SelectedIndex))
	{
		if (const TSharedPtr<SWidget> Focus = FlowListPrivate::FindFocusable(ItemSlot->GetWidget(), 0))
		{
			return FNavigationReply::Explicit(Focus);
		}
	}
	return FNavigationReply::Stop();
}

FReply SFlowList::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Noted, not handled: Slate turns the key into navigation after this, which OnNavigation reads as a step. A stick's
	// own key events (some devices send them) are scrolling, as its analog navigation is.
	const FKey Key = InKeyEvent.GetKey();
	const bool bStick = Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Gamepad_LeftStick_Down || Key == EKeys::Gamepad_LeftStick_Left
		|| Key == EKeys::Gamepad_LeftStick_Right || Key == EKeys::Gamepad_RightStick_Up || Key == EKeys::Gamepad_RightStick_Down
		|| Key == EKeys::Gamepad_RightStick_Left || Key == EKeys::Gamepad_RightStick_Right;
	StepKeyFrame = bStick ? 0 : GFrameCounter;
	return SPanel::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

// =====================================================================================================
// Stick Scrolls
// =====================================================================================================

void SFlowList::UpdateStickInput()
{
	const bool bWanted = Scrolling.bStickScrolls && !bDesignTime && FSlateApplication::IsInitialized();
	if (bWanted && !StickInput.IsValid())
	{
		StickInput = MakeShared<FFlowListStickInput>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(StickInput);
	}
	else if (!bWanted && StickInput.IsValid())
	{
		StickInput->Owner = nullptr;
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(StickInput);
		}
		StickInput.Reset();
		StickValue = 0.f;
	}
}

FKey SFlowList::GetStickAxisKey() const
{
	const bool bRight = Scrolling.Stick == EFlowListStick::RightStick;
	if (Layout.Orientation == Orient_Horizontal)
	{
		return bRight ? EKeys::Gamepad_RightX : EKeys::Gamepad_LeftX;
	}
	return bRight ? EKeys::Gamepad_RightY : EKeys::Gamepad_LeftY;
}

bool SFlowList::HasStickFocus(int32 UserIndex) const
{
	if (!FSlateApplication::IsInitialized())
	{
		return false;
	}
	const TSharedRef<const SWidget> Self = AsShared();
	return HasUserFocus(UserIndex).IsSet() || FSlateApplication::Get().HasUserFocusedDescendants(Self, UserIndex);
}

bool SFlowList::HandleStickAnalog(const FAnalogInputEvent& Event)
{
	if (!Scrolling.bStickScrolls || bDesignTime || LiveOrder.Num() == 0 || Event.GetKey() != GetStickAxisKey())
	{
		return false;
	}
	const int32 User = static_cast<int32>(Event.GetUserIndex());
	if (!HasStickFocus(User))
	{
		if (User == StickUser)
		{
			StickValue = 0.f;
		}
		return false;
	}

	// Kept, not acted on: a stick held still sends nothing more, and keeps scrolling at the value it last sent. Taken
	// even inside the dead zone, so that axis never becomes navigation while the list has it.
	StickUser = User;
	StickValue = Event.GetAnalogValue();
	if (FMath::Abs(StickValue) > Scrolling.StickDeadZone)
	{
		WakeUp();
	}
	return true;
}

bool SFlowList::HandleStickKey(const FKeyEvent& Event)
{
	// Some devices also send the stick as keys past a threshold - Left Stick Up and the rest - which would step. On the
	// axis the list takes, they're swallowed with it.
	if (!Scrolling.bStickScrolls || bDesignTime || LiveOrder.Num() == 0)
	{
		return false;
	}
	const FKey Key = Event.GetKey();
	const FKey Axis = GetStickAxisKey();
	const bool bOnAxis = (Axis == EKeys::Gamepad_LeftY && (Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Gamepad_LeftStick_Down))
		|| (Axis == EKeys::Gamepad_LeftX && (Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::Gamepad_LeftStick_Right))
		|| (Axis == EKeys::Gamepad_RightY && (Key == EKeys::Gamepad_RightStick_Up || Key == EKeys::Gamepad_RightStick_Down))
		|| (Axis == EKeys::Gamepad_RightX && (Key == EKeys::Gamepad_RightStick_Left || Key == EKeys::Gamepad_RightStick_Right));
	return bOnAxis && HasStickFocus(static_cast<int32>(Event.GetUserIndex()));
}

void SFlowList::UpdateStick(float DeltaTime)
{
	// Focus went elsewhere while it was held, or the setting went off: whatever it was doing ends here.
	if (StickValue != 0.f && (!Scrolling.bStickScrolls || !StickInput.IsValid() || !HasStickFocus(StickUser)))
	{
		StickValue = 0.f;
	}

	const float DeadZone = FMath::Clamp(Scrolling.StickDeadZone, 0.f, 0.9f);
	const float Push = FMath::Clamp((FMath::Abs(StickValue) - DeadZone) / (1.f - DeadZone), 0.f, 1.f);
	if (Push <= 0.f || LiveOrder.Num() == 0 || Press.bActive || bDesignTime)
	{
		StickHeld = 0.f;
		if (Motion == EMotion::Stick)
		{
			ReleaseStick();
		}
		return;
	}

	// Up a vertical list goes back toward its first item, as Up navigates; right along a horizontal one goes on.
	float Direction = FMath::Sign(StickValue) * (Layout.Orientation == Orient_Horizontal ? 1.f : -1.f);
	if (Scrolling.bInvertStick)
	{
		Direction = -Direction;
	}
	if (Motion != EMotion::Stick || FMath::Sign(Velocity) != Direction)
	{
		// Starting, taking over from a spring or fling, or reversing: the acceleration starts over.
		StickHeld = 0.f;
		bStickAtEdge = false;
	}

	// Pushed part way it is slower, and climbs toward full speed more slowly.
	const float Shaped = FMath::Pow(Push, FMath::Clamp(Scrolling.StickResponse, 0.5f, 4.f));
	StickHeld += DeltaTime * Shaped;
	float Speed = FMath::Max(Scrolling.StickSpeed, 0.1f);
	if (Scrolling.StickAccelerationTime > 0.f)
	{
		const float Ramp = FMath::Clamp(StickHeld / Scrolling.StickAccelerationTime, 0.f, 1.f);
		Speed = FMath::Lerp(Speed, FMath::Max(Scrolling.StickMaxSpeed, Speed), Ramp * Ramp);
	}

	Motion = EMotion::Stick;
	Velocity = Direction * Speed * Shaped;
	Target = Scroll;
}

void SFlowList::ReleaseStick()
{
	// Held into an end, there is no speed left to carry on with.
	const float ReleaseVelocity = bStickAtEdge ? 0.f : Velocity;
	bStickAtEdge = false;
	Motion = EMotion::Idle;

	if (LiveOrder.Num() == 0)
	{
		StopMotion();
		return;
	}

	if (Scrolling.bSnapToItems)
	{
		// The item it was passing, or the next one ahead once it was a quarter of the way past - never back the way it came
		// by more than that. The spring starts at the speed it was going, so it flows straight in.
		float Snap = FMath::RoundToFloat(Scroll);
		if (ReleaseVelocity > 0.f)
		{
			Snap = FMath::CeilToFloat(Scroll - 0.25f);
		}
		else if (ReleaseVelocity < 0.f)
		{
			Snap = FMath::FloorToFloat(Scroll + 0.25f);
		}
		if (!IsLooping())
		{
			Snap = FMath::Clamp(Snap, GetMinScroll(), GetMaxScroll());
		}
		SpringTo(Snap, ReleaseVelocity, true);
		return;
	}

	// Without snapping it glides to a stop on whatever speed it had, as a fling does.
	if (FMath::Abs(ReleaseVelocity) > 0.05f && Scrolling.FlingCoast > 0.f)
	{
		Motion = EMotion::Coast;
		CoastOrigin = Scroll;
		CoastVelocity = ReleaseVelocity;
		CoastTime = 0.f;
		Velocity = ReleaseVelocity;
		WakeUp();
		return;
	}
	StopMotion();
}

void SFlowList::OnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent)
{
	SPanel::OnFocusChanging(PreviousFocusPath, NewWidgetPath, InFocusEvent);

	// Focus moved onto an item some other way - Tab, a gamepad leaving a neighbouring widget, Set Focus from Blueprint:
	// bring that item to the focus. A click's focus is left to the click, which may yet become a drag.
	if (bMovingFocus || Press.bDragging || bDesignTime || InFocusEvent.GetCause() == EFocusCause::Mouse)
	{
		return;
	}

	const FArrangedChildren& Path = NewWidgetPath.Widgets;
	for (int32 Index = 0; Index + 1 < Path.Num(); ++Index)
	{
		if (&Path[Index].Widget.Get() != this)
		{
			continue;
		}
		const SWidget* Child = &Path[Index + 1].Widget.Get();
		for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
		{
			const FSlot& Slot = Children[ChildIndex];
			if (!Slot.bExiting && Slot.ItemIndex != INDEX_NONE && &Slot.GetWidget().Get() == Child)
			{
				TGuardValue<bool> Guard(bNavigating, true);
				ScrollToIndex(Slot.ItemIndex, true);
				return;
			}
		}
		return;
	}
}

void SFlowList::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SPanel::OnMouseEnter(MyGeometry, MouseEvent);
	if (Effects.Magnify.bEnabled)
	{
		HoverAbsolute = FVector2f(MouseEvent.GetScreenSpacePosition());
		WakeUp();
	}
}

void SFlowList::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SPanel::OnMouseLeave(MouseEvent);
	if (Effects.Magnify.bEnabled)
	{
		WakeUp();
	}
}

// =====================================================================================================
// Virtual lists
// =====================================================================================================

void SFlowList::SetVirtualItems(int32 Count, bool bRefresh)
{
	const int32 OldSelected = SelectedIndex;
	bVirtual = true;
	if (bRefresh)
	{
		ReleaseAllEntries();
	}
	if (bRefresh)
	{
		VirtualExtent = 0.f;
	}
	NormalizeLoop();
	VirtualCount = FMath::Max(Count, 0);

	// Entries for items that are gone go back.
	for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
	{
		const FSlot& Slot = Children[Index];
		if (!Slot.bExiting && Slot.ItemIndex >= VirtualCount)
		{
			ReleaseChild(Index);
		}
	}
	FlushReleases();

	const bool bSelectionLeft = !LiveOrder.IsValidIndex(OldSelected);
	FinishVirtualChange(OldSelected, bSelectionLeft ? INDEX_NONE : OldSelected, bSelectionLeft);
}

void SFlowList::NotifyVirtualInserted(int32 Index, bool bAnimate)
{
	if (!bVirtual)
	{
		return;
	}
	Index = FMath::Clamp(Index, 0, VirtualCount);
	NormalizeLoop();
	const int32 OldFocus = VirtualCount > 0 ? IndexAt(Scroll) : INDEX_NONE;
	const int32 OldSelected = SelectedIndex;
	++VirtualCount;

	// Everything from Index on moves down one, drawn where it was until it slides there.
	for (int32 Child = 0; Child < Children.Num(); ++Child)
	{
		FSlot& Slot = Children[Child];
		if (!Slot.bExiting && Slot.ItemIndex >= Index)
		{
			++Slot.ItemIndex;
		}
	}
	RebuildLiveOrder();

	// Added before the focused item: everything shifts with it, so the same item stays focused.
	if (Transitions.bKeepFocusedItem && OldFocus != INDEX_NONE && Index <= OldFocus && VirtualCount > 1)
	{
		ShiftFrame(1.f);
	}

	FinishVirtualChange(OldSelected, OldSelected != INDEX_NONE && OldSelected >= Index ? OldSelected + 1 : OldSelected, false);

	if (const FSlot* Added = FindItemSlot(Index))
	{
		if (bAnimate && Transitions.bAnimateChanges && !bDesignTime)
		{
			if (GFrameCounter != EnterBatchFrame)
			{
				EnterBatchFrame = GFrameCounter;
				EnterBatchCount = 0;
			}
			StartEnter(Children[LiveOrder[Index]], GetEnterDelay() + GetBatchStagger() * static_cast<float>(EnterBatchCount++));
		}
	}
}

void SFlowList::NotifyVirtualRemoved(int32 Index, bool bAnimate)
{
	if (!bVirtual || Index < 0 || Index >= VirtualCount)
	{
		return;
	}
	NormalizeLoop();
	const int32 OldFocus = IndexAt(Scroll);
	const int32 OldSelected = SelectedIndex;

	// Its entry leaves - drawn through Exit, then back to the pool (see StepItems) - or goes back at once.
	if (const FSlot* Shown = FindItemSlot(Index))
	{
		const int32 Child = LiveOrder[Index];
		FSlot& Slot = Children[Child];
		if (bAnimate && Transitions.bAnimateChanges && !bDesignTime && (GetExitSeconds() + GetExitDelay()) > 0.f)
		{
			StartExit(Slot);
		}
		else
		{
			ReleaseChild(Child);
		}
	}

	--VirtualCount;
	for (int32 Child = 0; Child < Children.Num(); ++Child)
	{
		FSlot& Slot = Children[Child];
		if (!Slot.bExiting && Slot.ItemIndex > Index)
		{
			--Slot.ItemIndex;
		}
	}
	FlushReleases();

	// Removed before the focused item: everything shifts with it. The focused item itself: the next slides into its place.
	if (Transitions.bKeepFocusedItem && Index < OldFocus)
	{
		ShiftFrame(-1.f);
	}

	const bool bSelectionLeft = OldSelected == Index;
	FinishVirtualChange(OldSelected, bSelectionLeft ? INDEX_NONE : (OldSelected > Index ? OldSelected - 1 : OldSelected), bSelectionLeft);
}

void SFlowList::FinishVirtualChange(int32 OldSelected, int32 NewSelected, bool bSelectionLeft)
{
	if (VirtualCount == 0)
	{
		StopMotion();
		Scroll = 0.f;
		Target = 0.f;
		NearestIndex = INDEX_NONE;
		SetSelected(INDEX_NONE, false);
	}
	else
	{
		NearestIndex = IndexAt(Scroll);
		const int32 Selected = LiveOrder.IsValidIndex(NewSelected) ? NewSelected : NearestIndex;
		SetSelected(Selected, bSelectionLeft, /*bForce=*/bSelectionLeft || Selected != OldSelected);

		if (!IsLooping())
		{
			const float Clamped = FMath::Clamp(Scroll, GetMinScroll(), GetMaxScroll());
			const bool bTargetOut = Motion == EMotion::Spring && (Target < GetMinScroll() || Target > GetMaxScroll());
			if (Motion != EMotion::Drag && (Clamped != Scroll || bTargetOut))
			{
				SpringTo(FMath::Clamp(bTargetOut ? Target : Scroll, GetMinScroll(), GetMaxScroll()), Velocity, false);
			}
		}
	}
	UpdateRealized();
	Invalidate(EInvalidateWidgetReason::Layout);
	WakeUp();
}

void SFlowList::UpdateRealized()
{
	if (!bVirtual)
	{
		return;
	}

	// The items within reach of the view either side of the focus - however the focus sits in the view - and a couple more
	// so an item is ready before it scrolls in. A little further out before one is given back, so an item on the edge isn't
	// made and given back every other frame.
	const float Cull = Effects.CullDistance > 0.f ? Effects.CullDistance : FLT_MAX;
	const float Reach = FMath::Min(FMath::CeilToFloat(CachedViewItems) + 2.f, Cull);
	const float Keep = Reach + 1.5f;

	// Never the selected item's entry, or one holding focus: a gamepad's focus, sent to an item far down the list ahead of
	// the scroll, would otherwise be thrown away with it.
	for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
	{
		const FSlot& Slot = Children[Index];
		if (!Slot.bExiting && Slot.ItemIndex != INDEX_NONE && Slot.ItemIndex != SelectedIndex
			&& FMath::Abs(WrapDistance(Slot.Place - Scroll)) > Keep && !Slot.GetWidget()->HasAnyUserFocusOrFocusedDescendants())
		{
			ReleaseChild(Index);
		}
	}
	FlushReleases();

	if (VirtualCount == 0)
	{
		return;
	}
	const int32 Center = FMath::RoundToInt(Scroll);
	const int32 Span = FMath::Min(FMath::CeilToInt(Reach), VirtualCount);
	for (int32 Offset = 0; Offset <= Span; ++Offset)
	{
		for (const int32 Sign : { 1, -1 })
		{
			if (Offset == 0 && Sign < 0)
			{
				continue;
			}
			int32 Index = Center + Offset * Sign;
			if (IsLooping())
			{
				Index = ((Index % VirtualCount) + VirtualCount) % VirtualCount;
			}
			if (LiveOrder.IsValidIndex(Index) && LiveOrder[Index] == INDEX_NONE)
			{
				RealizeIndex(Index);
			}
		}
	}
}

bool SFlowList::RealizeIndex(int32 Index)
{
	if (!bVirtual || !LiveOrder.IsValidIndex(Index) || LiveOrder[Index] != INDEX_NONE || !OnRealizeEntry.IsBound())
	{
		return false;
	}
	const TSharedPtr<SWidget> Widget = OnRealizeEntry.Execute(Index);
	if (!Widget.IsValid())
	{
		return false;
	}
	if (!LiveOrder.IsValidIndex(Index) || LiveOrder[Index] != INDEX_NONE)
	{
		// Whoever made it changed the list meanwhile: it goes straight back.
		OnReleaseEntry.ExecuteIfBound(Index, Widget.ToSharedRef());
		return false;
	}

	TUniquePtr<FSlot> NewSlot = MakeUnique<FSlot>();
	FSlot* Slot = NewSlot.Get();
	FSlot::FSlotArguments Arguments(MoveTemp(NewSlot));
	Arguments[Widget.ToSharedRef()];
	Children.AddSlot(MoveTemp(Arguments));

	Slot->ItemIndex = Index;
	Slot->Place = static_cast<float>(Index);
	Slot->PlaceVelocity = 0.f;
	Slot->bPlaced = true;
	LiveOrder[Index] = Children.Num() - 1;
	++LayoutVersion;
	return true;
}

void SFlowList::ReleaseChild(int32 ChildIndex)
{
	if (ChildIndex < 0 || ChildIndex >= Children.Num())
	{
		return;
	}
	FSlot& Slot = Children[ChildIndex];
	const int32 ItemIndex = Slot.ItemIndex;
	const TSharedRef<SWidget> Widget = Slot.GetWidget();
	RestoreVisibility(Slot);
	Children.RemoveAt(ChildIndex);
	++LayoutVersion;
	PendingReleases.Emplace(ItemIndex, Widget);
}

void SFlowList::ReleaseAllEntries()
{
	for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
	{
		ReleaseChild(Index);
	}
	FlushReleases();
}

void SFlowList::FlushReleases()
{
	RebuildLiveOrder();
	TArray<TPair<int32, TSharedRef<SWidget>>> Released = MoveTemp(PendingReleases);
	PendingReleases.Reset();
	for (const TPair<int32, TSharedRef<SWidget>>& Entry : Released)
	{
		OnReleaseEntry.ExecuteIfBound(Entry.Key, Entry.Value);
	}
}

void SFlowList::NormalizeLoop()
{
	const int32 Count = LiveOrder.Num();
	if (!IsLooping() || Count <= 0)
	{
		return;
	}
	const float Laps = FMath::FloorToFloat(Scroll / static_cast<float>(Count));
	if (Laps == 0.f)
	{
		return;
	}
	// Everything measured in the scroll's frame moves with it; the slots' places are already within a lap.
	const float Delta = -Laps * static_cast<float>(Count);
	Scroll += Delta;
	Target += Delta;
	CoastOrigin += Delta;
	Press.RawScroll += Delta;
	Press.StartScroll += Delta;
	for (TPair<double, float>& Sample : VelocitySamples)
	{
		Sample.Value += Delta;
	}
	++LayoutVersion;
}

// =====================================================================================================
// Layout
// =====================================================================================================

SFlowList::FPathFrame SFlowList::MakeFrame(const FVector2f& Size) const
{
	const bool bHorizontal = Layout.Orientation == Orient_Horizontal;
	const float Angle = FMath::DegreesToRadians(Layout.Angle);

	FPathFrame Frame;
	Frame.Size = Size;
	Frame.Dir = FlowListPrivate::Rotate(bHorizontal ? FVector2f(1.f, 0.f) : FVector2f(0.f, 1.f), Angle);
	Frame.Cross = FlowListPrivate::Rotate(bHorizontal ? FVector2f(0.f, 1.f) : FVector2f(1.f, 0.f), Angle);
	Frame.Turn = bHorizontal ? 1.f : -1.f;
	Frame.BaseAngle = bHorizontal ? 0.f : UE_HALF_PI;
	Frame.Focus = Size * FVector2f(Layout.FocusAnchor) + FVector2f(Layout.FocusOffset);
	return Frame;
}

FVector2f SFlowList::GetItemSize(const FSlot& Slot) const
{
	FVector2f Size(Slot.GetWidget()->GetDesiredSize());
	if (Layout.ItemSize.X > 0.0)
	{
		Size.X = static_cast<float>(Layout.ItemSize.X);
	}
	if (Layout.ItemSize.Y > 0.0)
	{
		Size.Y = static_cast<float>(Layout.ItemSize.Y);
	}
	if (Slot.SizeOverride.X > 0.0)
	{
		Size.X = static_cast<float>(Slot.SizeOverride.X);
	}
	if (Slot.SizeOverride.Y > 0.0)
	{
		Size.Y = static_cast<float>(Slot.SizeOverride.Y);
	}
	return Size + FVector2f(Slot.Padding.Left + Slot.Padding.Right, Slot.Padding.Top + Slot.Padding.Bottom);
}

void SFlowList::BuildPitch(FPitchTable& Out) const
{
	const bool bHorizontal = Layout.Orientation == Orient_Horizontal;
	const float Pivot = static_cast<float>(bHorizontal ? Layout.ItemPivot.X : Layout.ItemPivot.Y);
	const int32 Count = LiveOrder.Num();

	Out.bLoop = IsLooping();

	// A virtual list's items are even: one length, from the list's Item Size or the first entry shown - nothing per item, so
	// ten thousand items cost what one does.
	if (bVirtual)
	{
		float Extent = static_cast<float>(bHorizontal ? Layout.ItemSize.X : Layout.ItemSize.Y);
		if (Extent <= 0.f)
		{
			// Measured once, off the first entry that has been measured itself, and kept: measuring whichever entry happens
			// to be first would change the spacing as entries come and go.
			if (VirtualExtent <= 0.f)
			{
				for (int32 Index = 0; Index < Children.Num(); ++Index)
				{
					if (!Children[Index].bExiting)
					{
						const FVector2f Size = GetItemSize(Children[Index]);
						const float Measured = bHorizontal ? Size.X : Size.Y;
						if (Measured > 1.f)
						{
							VirtualExtent = Measured;
							break;
						}
					}
				}
			}
			Extent = VirtualExtent;
		}
		Out.bUniform = true;
		Out.UniformExtent = Extent > 0.f ? Extent : 100.f;
		Out.Fallback = Layout.Pitch > 0.f ? Layout.Pitch : FMath::Max(Out.UniformExtent + Layout.Spacing, 1.f);
		Out.Total = Out.Fallback * static_cast<float>(FMath::Max(Count, 1));
		return;
	}

	for (const int32 Child : LiveOrder)
	{
		const FVector2f Size = GetItemSize(Children[Child]);
		Out.Extents.Add(bHorizontal ? Size.X : Size.Y);
	}

	// Pivot to pivot: the rest of this item past its pivot, the gap, and the next item up to its pivot.
	const int32 SegmentCount = Out.bLoop ? Count : FMath::Max(Count - 1, 0);
	for (int32 Index = 0; Index < SegmentCount; ++Index)
	{
		const int32 Next = (Index + 1) % Count;
		const float Length = Layout.Pitch > 0.f
			? Layout.Pitch
			: Out.Extents[Index] * (1.f - Pivot) + Layout.Spacing + Out.Extents[Next] * Pivot;
		Out.Segments.Add(FMath::Max(Length, 0.f));
		Out.Total += Out.Segments.Last();
	}

	Out.Fallback = Layout.Pitch > 0.f ? Layout.Pitch : (Count > 0 ? FMath::Max(Out.Extents[0] + Layout.Spacing, 1.f) : 100.f);
	if (!Out.bLoop)
	{
		// An open list going round a ring still leaves a gap the size of a pitch between its ends.
		Out.Total += Out.Segments.Num() > 0 ? Out.Segments.Last() : Out.Fallback;
	}
}

float SFlowList::SpacingFactor(float Distance) const
{
	if (!Effects.bSpacingFollowsScale)
	{
		return 1.f;
	}
	return FMath::Max(Effects.Scale.Evaluate(Distance, 1.f) * Effects.Squash.Evaluate(Distance, 1.f) * MagnifyAt(Distance), 0.f);
}

float SFlowList::MagnifyAt(float Distance) const
{
	if (MagnifyWeight <= 0.f)
	{
		return 1.f;
	}
	const FFlowListMagnify& Magnify = Effects.Magnify;
	const float Away = FMath::Abs(Distance - MagnifyCenter) * CachedPixelsPerItem / FMath::Max(Magnify.Radius, 1.f);
	if (Away >= 1.f)
	{
		return 1.f;
	}
	const float Falloff = 1.f - WidgetAnimEasing::Evaluate(Magnify.Easing, nullptr, Away);
	return 1.f + (Magnify.Scale - 1.f) * MagnifyWeight * Falloff;
}

float SFlowList::IntegratePath(float From, float To, const FPitchTable& Pitch) const
{
	// The path length between two distances from the focus: each stretch between neighbouring items is its pitch, scaled
	// by how big items are along it (Simpson's rule per stretch). Exact for an even list, and what makes a tapering list
	// pack tighter the further it goes rather than leaving gaps where its items shrank.
	if (From == To)
	{
		return 0.f;
	}
	const float Sign = To > From ? 1.f : -1.f;
	double Low = FMath::Min(From, To);
	const double High = FMath::Max(From, To);
	double Sum = 0.0;

	for (int32 Guard = 0; Low < High && Guard < 4096; ++Guard)
	{
		const double Position = static_cast<double>(Scroll) + Low;
		const double Floor = FMath::FloorToDouble(Position);
		double End = FMath::Min(High, Floor + 1.0 - static_cast<double>(Scroll));
		if (End <= Low)
		{
			// Sitting exactly on a boundary that rounding put a hair behind: step over it.
			End = FMath::Min(High, Low + 1.0e-4);
		}

		const float Length = Pitch.At(static_cast<int64>(Floor));
		const float A = static_cast<float>(Low);
		const float B = static_cast<float>(End);
		const float Factor = (SpacingFactor(A) + 4.f * SpacingFactor((A + B) * 0.5f) + SpacingFactor(B)) / 6.f;
		Sum += static_cast<double>(Length * Factor) * (End - Low);
		Low = End;
	}
	return Sign * static_cast<float>(Sum);
}

SFlowList::FPathPoint SFlowList::SamplePath(const FPathFrame& Frame, float PathDistance, const FPitchTable& Pitch) const
{
	using namespace FlowListPrivate;

	FPathPoint Point;
	Point.Tangent = Frame.Dir;
	Point.Normal = Frame.Cross;

	auto NormalOf = [&Frame](const FVector2f& Tangent)
	{
		return Frame.Turn > 0.f ? FVector2f(-Tangent.Y, Tangent.X) : FVector2f(Tangent.Y, -Tangent.X);
	};

	switch (Layout.Type)
	{
	case EFlowListPathType::Arc:
	{
		const float Radius = Layout.ArcRadius;
		if (FMath::Abs(Radius) < 1.f)
		{
			Point.Position = Frame.Focus + Frame.Dir * PathDistance;
			break;
		}
		// A circle touching the path's line at its apex, curling toward the cross side when the radius is positive.
		auto Curl = [&](float Along)
		{
			const float Theta = (Along - Layout.ArcApex) / Radius;
			return Frame.Cross * (Radius * (1.f - FMath::Cos(Theta))) + Frame.Dir * (Radius * FMath::Sin(Theta));
		};
		const float Theta = (PathDistance - Layout.ArcApex) / Radius;
		Point.Position = Frame.Focus + Curl(PathDistance) - Curl(0.f);
		Point.Tangent = Frame.Dir * FMath::Cos(Theta) + Frame.Cross * FMath::Sin(Theta);
		Point.Normal = Frame.Cross * FMath::Cos(Theta) - Frame.Dir * FMath::Sin(Theta);
		break;
	}

	case EFlowListPathType::Wheel:
	{
		const float Radius = FMath::Max(Layout.WheelRadius, 1.f);
		const float Theta = PathDistance / Radius;
		const float Cos = FMath::Cos(Theta);
		Point.bHidden = FMath::Abs(Theta) >= UE_HALF_PI || Cos <= 0.01f;
		Point.Position = Frame.Focus + Frame.Dir * (Radius * FMath::Sin(Theta));
		Point.Squash = FMath::Max(Cos, 0.f);
		Point.Depth = Radius * (1.f - Cos);
		Point.Opacity = SmoothStep01(Cos / 0.35f);
		break;
	}

	case EFlowListPathType::Ring:
	{
		const float RadiusX = FMath::Max(static_cast<float>(Layout.RingRadius.X), 1.f);
		const float RadiusY = static_cast<float>(Layout.RingRadius.Y);
		const float Orbit = (Layout.bFitRingToItems && Pitch.Total > 1.f) ? Pitch.Total / UE_TWO_PI : RadiusX;
		const float Theta = PathDistance / FMath::Max(Orbit, 1.f);
		const float Sin = FMath::Sin(Theta);
		const float Cos = FMath::Cos(Theta);
		Point.Position = Frame.Focus + Frame.Dir * (RadiusX * Sin) - Frame.Cross * (RadiusY * (1.f - Cos));
		const FVector2f Tangent = (Frame.Dir * (RadiusX * Cos) - Frame.Cross * (RadiusY * Sin)).GetSafeNormal();
		Point.Tangent = Tangent.IsNearlyZero() ? Frame.Dir : Tangent;
		Point.Normal = NormalOf(Point.Tangent);
		Point.Depth = RadiusX * (1.f - Cos);
		Point.Opacity = FMath::Lerp(1.f, FMath::Clamp(Layout.RingBackOpacity, 0.f, 1.f), (1.f - Cos) * 0.5f);
		break;
	}

	case EFlowListPathType::Spline:
	{
		BuildSpline(Frame.Size);
		const FPathPoint OnSpline = SampleSpline(PathDistance);
		Point.Position = OnSpline.Position + FVector2f(Layout.FocusOffset);
		Point.Tangent = OnSpline.Tangent;
		Point.Normal = NormalOf(Point.Tangent);
		break;
	}

	case EFlowListPathType::Line:
	default:
		Point.Position = Frame.Focus + Frame.Dir * PathDistance;
		break;
	}
	return Point;
}

void SFlowList::BuildSpline(const FVector2f& Size) const
{
	uint32 Hash = GetTypeHash(Layout.SplinePoints.Num()) + 1;
	for (const FVector2D& Point : Layout.SplinePoints)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Point));
	}
	if (Hash == SplineBuiltHash && Size == SplineBuiltSize)
	{
		return;
	}
	SplineBuiltHash = Hash;
	SplineBuiltSize = Size;
	SplineLengths.Reset();

	// The same curve the designer's path editor draws - see FlowListTypes::BuildSplineCurve.
	TArray<FVector2f> Curve;
	FlowListTypes::BuildSplineCurve(Layout.SplinePoints, Size, Curve);
	SplinePoints = MoveTemp(Curve);

	float Length = 0.f;
	for (int32 Index = 0; Index < SplinePoints.Num(); ++Index)
	{
		if (Index > 0)
		{
			Length += FVector2f::Distance(SplinePoints[Index - 1], SplinePoints[Index]);
		}
		SplineLengths.Add(Length);
	}
}

SFlowList::FPathPoint SFlowList::SampleSpline(float Along) const
{
	FPathPoint Point;
	const int32 Count = SplinePoints.Num();
	if (Count < 2)
	{
		return Point;
	}

	auto Direction = [](const FVector2f& From, const FVector2f& To)
	{
		const FVector2f Tangent = (To - From).GetSafeNormal();
		return Tangent.IsNearlyZero() ? FVector2f(1.f, 0.f) : Tangent;
	};

	const float Total = SplineLengths.Last();
	const float At = FMath::Clamp(Layout.SplineFocus, 0.f, 1.f) * Total + Along;
	if (At <= 0.f)
	{
		// Before the first point the curve carries straight on, so items scrolled past it don't bunch up.
		Point.Tangent = Direction(SplinePoints[0], SplinePoints[1]);
		Point.Position = SplinePoints[0] + Point.Tangent * At;
		return Point;
	}
	if (At >= Total)
	{
		Point.Tangent = Direction(SplinePoints[Count - 2], SplinePoints[Count - 1]);
		Point.Position = SplinePoints[Count - 1] + Point.Tangent * (At - Total);
		return Point;
	}

	const int32 Upper = FMath::Clamp(static_cast<int32>(Algo::UpperBound(SplineLengths, At)), 1, Count - 1);
	const int32 Lower = Upper - 1;
	const float Span = SplineLengths[Upper] - SplineLengths[Lower];
	const float Alpha = Span > UE_KINDA_SMALL_NUMBER ? (At - SplineLengths[Lower]) / Span : 0.f;
	Point.Position = FMath::Lerp(SplinePoints[Lower], SplinePoints[Upper], Alpha);
	Point.Tangent = Direction(SplinePoints[Lower], SplinePoints[Upper]);
	return Point;
}

float SFlowList::ComputeContentEndScroll(const FPathFrame& Frame, const FPitchTable& Pitch) const
{
	const int32 Count = LiveOrder.Num();
	if (Count <= 1)
	{
		return 0.f;
	}

	// The room from the focus to the list's far edge, along the list.
	float Room = TNumericLimits<float>::Max();
	if (Frame.Dir.X > UE_KINDA_SMALL_NUMBER)
	{
		Room = FMath::Min(Room, (Frame.Size.X - Frame.Focus.X) / Frame.Dir.X);
	}
	else if (Frame.Dir.X < -UE_KINDA_SMALL_NUMBER)
	{
		Room = FMath::Min(Room, -Frame.Focus.X / Frame.Dir.X);
	}
	if (Frame.Dir.Y > UE_KINDA_SMALL_NUMBER)
	{
		Room = FMath::Min(Room, (Frame.Size.Y - Frame.Focus.Y) / Frame.Dir.Y);
	}
	else if (Frame.Dir.Y < -UE_KINDA_SMALL_NUMBER)
	{
		Room = FMath::Min(Room, -Frame.Focus.Y / Frame.Dir.Y);
	}
	if (Room == TNumericLimits<float>::Max())
	{
		return static_cast<float>(Count - 1);
	}

	// How far along the content - from the first item's pivot - must sit at the focus for its far end to just reach the
	// edge, found among the items' pivots.
	const bool bHorizontal = Layout.Orientation == Orient_Horizontal;
	const float Pivot = static_cast<float>(bHorizontal ? Layout.ItemPivot.X : Layout.ItemPivot.Y);
	if (Pitch.bUniform)
	{
		// Even items: no list of pivots needed.
		const float Needed = Pitch.Fallback * static_cast<float>(Count - 1) + Pitch.UniformExtent * (1.f - Pivot) - Room;
		return FMath::Clamp(Needed / FMath::Max(Pitch.Fallback, 0.001f), 0.f, static_cast<float>(Count - 1));
	}
	TArray<float, TInlineAllocator<64>> Pivots;
	Pivots.Add(0.f);
	for (int32 Index = 1; Index < Count; ++Index)
	{
		Pivots.Add(Pivots.Last() + Pitch.At(Index - 1));
	}
	const float ContentEnd = Pivots.Last() + Pitch.ExtentAt(Count - 1) * (1.f - Pivot);
	const float Needed = ContentEnd - Room;
	if (Needed <= 0.f)
	{
		return 0.f;
	}
	for (int32 Index = 0; Index < Count - 1; ++Index)
	{
		if (Needed < Pivots[Index + 1])
		{
			return static_cast<float>(Index) + (Needed - Pivots[Index]) / FMath::Max(Pitch.At(Index), 0.001f);
		}
	}
	return static_cast<float>(Count - 1);
}

FFlowListTransitionState SFlowList::GetTransition(const FSlot& Slot) const
{
	// The items' own Widget Animations draw their entrances and exits. An item waiting for its entrance to begin isn't
	// shown yet - it would otherwise sit there whole until its animation started and took it to its first pose.
	if (UsesItemAnimations())
	{
		FFlowListTransitionState State;
		if (Slot.bEntering && !Slot.bTransitionStarted)
		{
			State.Opacity = 0.f;
		}
		return State;
	}
	if (Slot.bExiting || Slot.bPreviewLeaving)
	{
		const float Alpha = (Slot.TransitionTime - Slot.TransitionDelay) / FMath::Max(Transitions.Exit.Duration, 0.001f);
		return FlowListTypes::EvaluateTransition(Transitions.Exit, FMath::Clamp(Alpha, 0.f, 1.f), /*bEnter=*/false);
	}
	if (Slot.bEntering)
	{
		const float Alpha = (Slot.TransitionTime - Slot.TransitionDelay) / FMath::Max(Transitions.Enter.Duration, 0.001f);
		return FlowListTypes::EvaluateTransition(Transitions.Enter, FMath::Clamp(Alpha, 0.f, 1.f), /*bEnter=*/true);
	}
	return FFlowListTransitionState();
}

void SFlowList::ComputeLayout(const FGeometry& AllottedGeometry, TArray<FItemLayout>& Out) const
{
	using namespace FlowListPrivate;

	Out.Reset();
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const FPathFrame Frame = MakeFrame(Size);
	CachedInView.Init(false, Children.Num());
	FPitchTable Pitch;
	BuildPitch(Pitch);

	// Measured without the lens; it is placed once the measurements say where the pointer is along the list.
	MagnifyWeight = 0.f;

	// A child shown or collapsed since the list last looked: the next tick treats it as arriving or leaving.
	for (int32 Index = 0; Index < Children.Num() && !bVirtual; ++Index)
	{
		const FSlot& Slot = Children[Index];
		if (!Slot.bExiting && (Slot.GetWidget()->GetVisibility() == EVisibility::Collapsed) != Slot.bCollapsed)
		{
			bVisibilityDirty = true;
		}
	}
	if (bVisibilityDirty)
	{
		const_cast<SFlowList*>(this)->WakeUp();
	}

	// What a pointer's travel means here: the path's direction at the focus, and how far one item spans along it.
	{
		const FPathPoint AtFocus = SamplePath(Frame, 0.f, Pitch);
		const FPathPoint Ahead = SamplePath(Frame, IntegratePath(0.f, 0.5f, Pitch), Pitch);
		const FPathPoint Behind = SamplePath(Frame, IntegratePath(0.f, -0.5f, Pitch), Pitch);
		const FVector2f Axis = AtFocus.Tangent.GetSafeNormal();
		CachedDragAxis = Axis.IsNearlyZero() ? Frame.Dir : Axis;
		CachedPixelsPerItem = FMath::Max(FVector2f::DotProduct(Ahead.Position - Behind.Position, CachedDragAxis), 1.f);
		const float ViewLength = FMath::Abs(Size.X * CachedDragAxis.X) + FMath::Abs(Size.Y * CachedDragAxis.Y);
		const float PreviousViewItems = CachedViewItems;
		CachedViewItems = FMath::Max(ViewLength / CachedPixelsPerItem, 0.5f);

		// A virtual list learns how many items fit only now: the next tick shows entries for them.
		if (bVirtual && FMath::Abs(CachedViewItems - PreviousViewItems) > 0.5f)
		{
			const_cast<SFlowList*>(this)->WakeUp();
		}

		// Hover Magnify: the pointer's place along the list, in items from the focus, through the path's direction there.
		if (Effects.Magnify.bEnabled && MagnifyAmount > 0.f)
		{
			const FVector2f Pointer(AllottedGeometry.AbsoluteToLocal(HoverAbsolute));
			MagnifyCenter = FVector2f::DotProduct(Pointer - AtFocus.Position, CachedDragAxis) / CachedPixelsPerItem;
			MagnifyWeight = MagnifyAmount;
		}
	}
	CachedContentEndScroll = (Scrolling.bStopAtContentEnd && Layout.Type == EFlowListPathType::Line && !IsLooping())
		? ComputeContentEndScroll(Frame, Pitch)
		: -1.f;

	// Every child that is drawn - the items and the ones leaving - by distance from the focus.
	struct FEntry
	{
		int32 Child;
		float Distance;
	};
	TArray<FEntry, TInlineAllocator<64>> Entries;
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		const FSlot& Slot = Children[Index];
		if (Slot.bExiting || (Slot.ItemIndex != INDEX_NONE && Slot.bPlaced))
		{
			Entries.Add({ Index, WrapDistance(Slot.Place - Scroll) });
		}
	}
	Entries.Sort([](const FEntry& A, const FEntry& B) { return A.Distance < B.Distance; });

	const bool bHorizontal = Layout.Orientation == Orient_Horizontal;
	const bool bPerspective = Layout.Type == EFlowListPathType::Wheel || Layout.Type == EFlowListPathType::Ring;
	const float Cull = Effects.CullDistance > 0.f ? Effects.CullDistance : MaxLaidOutItems;

	auto PlaceItem = [&](const FEntry& Entry, float PathDistance)
	{
		const FSlot& Slot = Children[Entry.Child];
		const TSharedRef<SWidget>& Widget = Slot.GetWidget();
		const float Distance = Entry.Distance;
		const FFlowListTransitionState Transition = GetTransition(Slot);

		const float Spread = Effects.Spread.Evaluate(Distance, 0.f);
		const FPathPoint Point = SamplePath(Frame, PathDistance + Spread + Transition.Offset.X, Pitch);
		if (Point.bHidden)
		{
			return;
		}

		// In view: its box at the size the list gives it overlaps the list - whatever it is faded to, so an item its own exit
		// animation faded out still counts as back in view when it scrolls back.
		{
			const FVector2f Half = GetItemSize(Slot) * (0.5f * FMath::Max(Effects.Scale.Evaluate(Distance, 1.f), 0.f));
			const FVector2f Center = Point.Position + Point.Normal * (Effects.CrossOffset.Evaluate(Distance, 0.f) + Transition.Offset.Y);
			if (CachedInView.IsValidIndex(Entry.Child))
			{
				CachedInView[Entry.Child] = Center.X + Half.X > 0.f && Center.X - Half.X < Size.X && Center.Y + Half.Y > 0.f && Center.Y - Half.Y < Size.Y;
			}
		}

		float Opacity = FMath::Clamp(Effects.Opacity.Evaluate(Distance, 1.f), 0.f, 1.f) * Point.Opacity * FMath::Clamp(Transition.Opacity, 0.f, 1.f)
			* (Slot.bHasAnimPose ? FMath::Clamp(Slot.AnimOpacity, 0.f, 1.f) : 1.f);
		if (Effects.EdgeFade > 0.f)
		{
			const bool bAlongX = FMath::Abs(Frame.Dir.X) >= FMath::Abs(Frame.Dir.Y);
			const float Margin = bAlongX
				? FMath::Min(Point.Position.X, Size.X - Point.Position.X)
				: FMath::Min(Point.Position.Y, Size.Y - Point.Position.Y);
			Opacity *= SmoothStep01(Margin / Effects.EdgeFade);
		}
		// Invisible items aren't laid out at all, so nothing can click on them.
		if (Opacity <= 0.002f)
		{
			return;
		}

		const float Perspective = bPerspective ? 1.f / (1.f + Point.Depth * FMath::Max(Layout.Perspective, 0.f)) : 1.f;
		const float Scale = Effects.Scale.Evaluate(Distance, 1.f) * Transition.Scale * Perspective * MagnifyAt(Distance);
		if (Scale <= 0.001f)
		{
			return;
		}
		const float Squash = Effects.Squash.Evaluate(Distance, 1.f) * Point.Squash;
		float Degrees = Effects.Rotation.Evaluate(Distance, 0.f) + Transition.Rotation;
		if (Layout.bAlignToPath)
		{
			Degrees += FMath::RadiansToDegrees(FMath::Atan2(Point.Tangent.Y, Point.Tangent.X) - Frame.BaseAngle);
		}
		const float Cross = Effects.CrossOffset.Evaluate(Distance, 0.f) + Transition.Offset.Y;
		const FVector2f Anchor = Point.Position + Point.Normal * Cross;

		// Uniform scale goes in the layout transform, so the item is laid out - text rasterized - at the size it is drawn.
		const FVector2f OuterSize = GetItemSize(Slot);
		const FVector2f PaddingOffset(Slot.Padding.Left, Slot.Padding.Top);
		const FVector2f ChildSize(
			FMath::Max(OuterSize.X - Slot.Padding.Left - Slot.Padding.Right, 0.f),
			FMath::Max(OuterSize.Y - Slot.Padding.Top - Slot.Padding.Bottom, 0.f));
		const FVector2f PivotOuter = OuterSize * FVector2f(Layout.ItemPivot);
		const FSlateLayoutTransform LayoutTransform(Scale, Anchor - PivotOuter * Scale + PaddingOffset * Scale);

		// Turning and squashing go in the render transform, about the pivot, after any transform the item has of its own -
		// a Widget Animation playing on it still plays.
		FSlateRenderTransform RenderTransform = MakeItemTransform(FMath::DegreesToRadians(Degrees), Squash, bHorizontal, PivotOuter - PaddingOffset);

		// The item's own entrance or exit animation, about its pivot, under the list's turn - and over its own transform.
		if (Slot.bHasAnimPose)
		{
			const FVector2f AnimPivot = Slot.AnimPivot * ChildSize;
			const FSlateRenderTransform AboutAnimPivot = ::Concatenate(::Concatenate(FSlateRenderTransform(-AnimPivot), Slot.AnimTransform), FSlateRenderTransform(AnimPivot));
			RenderTransform = ::Concatenate(AboutAnimPivot, RenderTransform);
		}
		const TOptional<FSlateRenderTransform> OwnTransform = Widget->GetRenderTransformWithRespectToFlowDirection();
		if (OwnTransform.IsSet())
		{
			const FVector2f OwnPivot = FVector2f(FVector2D(Widget->GetRenderTransformPivotWithRespectToFlowDirection())) * ChildSize;
			const FSlateRenderTransform AboutOwnPivot = ::Concatenate(::Concatenate(FSlateRenderTransform(-OwnPivot), OwnTransform.GetValue()), FSlateRenderTransform(OwnPivot));
			RenderTransform = ::Concatenate(AboutOwnPivot, RenderTransform);
		}

		FItemLayout& Item = Out.AddDefaulted_GetRef();
		Item.Child = Entry.Child;
		Item.Geometry = AllottedGeometry.MakeChild(FVector2D(ChildSize), LayoutTransform, RenderTransform, FVector2D::ZeroVector);

		FLinearColor Color = Effects.Tint.Evaluate(Distance);
		Color.A *= Opacity;
		Item.Color = Color;

		switch (Effects.ZOrder)
		{
		case EFlowListZOrder::FirstOnTop:
			Item.SortKey = -Distance;
			break;
		case EFlowListZOrder::LastOnTop:
			Item.SortKey = Distance;
			break;
		case EFlowListZOrder::Depth:
			Item.SortKey = bPerspective ? -Point.Depth - FMath::Abs(Distance) * 0.001f : -FMath::Abs(Distance);
			break;
		case EFlowListZOrder::FocusOnTop:
		default:
			Item.SortKey = -FMath::Abs(Distance);
			break;
		}
	};

	// Out from the focus each way, adding up the path as it goes: each item sits one (scaled) pitch past the last.
	int32 Split = 0;
	while (Split < Entries.Num() && Entries[Split].Distance < 0.f)
	{
		++Split;
	}
	float PathDistance = 0.f;
	float From = 0.f;
	for (int32 Index = Split; Index < Entries.Num(); ++Index)
	{
		if (Entries[Index].Distance > Cull)
		{
			break;
		}
		PathDistance += IntegratePath(From, Entries[Index].Distance, Pitch);
		From = Entries[Index].Distance;
		PlaceItem(Entries[Index], PathDistance);
	}
	PathDistance = 0.f;
	From = 0.f;
	for (int32 Index = Split - 1; Index >= 0; --Index)
	{
		if (Entries[Index].Distance < -Cull)
		{
			break;
		}
		PathDistance += IntegratePath(From, Entries[Index].Distance, Pitch);
		From = Entries[Index].Distance;
		PlaceItem(Entries[Index], PathDistance);
	}

	// Back to front.
	Out.StableSort([](const FItemLayout& A, const FItemLayout& B) { return A.SortKey < B.SortKey; });
}

const TArray<SFlowList::FItemLayout>& SFlowList::GetLayout(const FGeometry& AllottedGeometry) const
{
	// Arranging, painting and every hit test of a frame ask for the same layout: it is worked out once. Anything that moves
	// the list between them changes the scroll or bumps LayoutVersion, and a new frame always starts afresh.
	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();
	if (CachedLayoutFrame != GFrameCounter || CachedLayoutVersion != LayoutVersion || CachedLayoutScroll != Scroll
		|| CachedLayoutSize != Size || !(CachedLayoutTransform == Transform))
	{
		ComputeLayout(AllottedGeometry, CachedLayout);
		CachedLayoutFrame = GFrameCounter;
		CachedLayoutVersion = LayoutVersion;
		CachedLayoutScroll = Scroll;
		CachedLayoutSize = Size;
		CachedLayoutTransform = Transform;
	}
	return CachedLayout;
}

void SFlowList::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const TArray<FItemLayout>& Items = GetLayout(AllottedGeometry);
	for (const FItemLayout& Item : Items)
	{
		const TSharedRef<SWidget>& Widget = Children[Item.Child].GetWidget();
		const EVisibility Visibility = Widget->GetVisibility();
		if (ArrangedChildren.Accepts(Visibility))
		{
			ArrangedChildren.AddWidget(Visibility, FArrangedWidget(Widget, Item.Geometry));
		}
	}
}

int32 SFlowList::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// A copy: painting an item runs its Tick, which may move the list and so lay it out again under this loop.
	const TArray<FItemLayout> Items = GetLayout(AllottedGeometry);

	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const FPaintArgs NewArgs = Args.WithNewParent(this);
	int32 MaxLayer = LayerId;

	for (const FItemLayout& Item : Items)
	{
		const TSharedRef<SWidget>& Widget = Children[Item.Child].GetWidget();
		if (!Widget->GetVisibility().IsVisible())
		{
			continue;
		}
		const FArrangedWidget Arranged(Widget, Item.Geometry);
		if (IsChildWidgetCulled(MyCullingRect, Arranged))
		{
			continue;
		}

		// Each item above everything drawn before it, so overlapping items stack whole rather than interleaving.
		FWidgetStyle Style = InWidgetStyle;
		Style.BlendColorAndOpacityTint(Item.Color);
		const int32 ChildLayer = Widget->Paint(NewArgs, Item.Geometry, MyCullingRect, OutDrawElements, MaxLayer + 1, Style, bEnabled);
		MaxLayer = FMath::Max(MaxLayer, ChildLayer);
	}

	return PaintPathGuide(AllottedGeometry, OutDrawElements, MaxLayer);
}

int32 SFlowList::PaintPathGuide(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	if (!bDesignTime || !bShowPath)
	{
		return LayerId;
	}

	const FVector2f Size(AllottedGeometry.GetLocalSize());
	const FPathFrame Frame = MakeFrame(Size);
	FPitchTable Pitch;
	BuildPitch(Pitch);

	float Reach = FMath::Max(Size.Size(), 200.f) * 1.5f;
	if (Layout.Type == EFlowListPathType::Ring)
	{
		const float Orbit = (Layout.bFitRingToItems && Pitch.Total > 1.f) ? Pitch.Total / UE_TWO_PI : FMath::Max(static_cast<float>(Layout.RingRadius.X), 1.f);
		Reach = Orbit * UE_PI;
	}

	const int32 Layer = LayerId + 1;
	const FLinearColor PathColor(0.25f, 0.7f, 1.f, 0.55f);
	TArray<FVector2f> Points;
	auto Flush = [&]()
	{
		if (Points.Num() > 1)
		{
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, PathColor, /*bAntialias=*/true, 1.f);
		}
		Points.Reset();
	};
	const int32 StepCount = FMath::Clamp(FMath::CeilToInt(Reach * 2.f / 6.f), 2, 4096);
	for (int32 Step = 0; Step <= StepCount; ++Step)
	{
		const float Along = -Reach + Reach * 2.f * static_cast<float>(Step) / static_cast<float>(StepCount);
		const FPathPoint Point = SamplePath(Frame, Along, Pitch);
		if (Point.bHidden)
		{
			Flush();
			continue;
		}
		Points.Add(Point.Position);
	}
	Flush();

	// The focus: a small cross on the path, its long arm along it.
	const FPathPoint Focus = SamplePath(Frame, 0.f, Pitch);
	const FLinearColor FocusColor(1.f, 0.75f, 0.2f, 0.9f);
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(),
		TArray<FVector2f>{ Focus.Position - Focus.Tangent * 10.f, Focus.Position + Focus.Tangent * 10.f }, ESlateDrawEffect::None, FocusColor, true, 2.f);
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(),
		TArray<FVector2f>{ Focus.Position - Focus.Normal * 6.f, Focus.Position + Focus.Normal * 6.f }, ESlateDrawEffect::None, FocusColor, true, 2.f);
	return Layer;
}

FVector2D SFlowList::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const bool bHorizontal = Layout.Orientation == Orient_Horizontal;
	float Along = 0.f;
	float Across = 0.f;
	float LongestExtent = 0.f;
	if (bVirtual)
	{
		// Even items: the entries shown say how big one is, and the count how many there are.
		FPitchTable Pitch;
		BuildPitch(Pitch);
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			const FVector2f Size = GetItemSize(Children[Index]);
			Across = FMath::Max(Across, bHorizontal ? Size.Y : Size.X);
		}
		Along = VirtualCount > 0 ? Pitch.Fallback * static_cast<float>(VirtualCount - 1) + Pitch.UniformExtent : 0.f;
		FVector2D VirtualSize = bHorizontal ? FVector2D(Along, Across) : FVector2D(Across, Along);
		if (Layout.DesiredSize.X > 0.0)
		{
			VirtualSize.X = Layout.DesiredSize.X;
		}
		if (Layout.DesiredSize.Y > 0.0)
		{
			VirtualSize.Y = Layout.DesiredSize.Y;
		}
		return VirtualSize;
	}
	for (const int32 Child : LiveOrder)
	{
		const FVector2f Size = GetItemSize(Children[Child]);
		const float Extent = bHorizontal ? Size.X : Size.Y;
		Along += Extent;
		LongestExtent = FMath::Max(LongestExtent, Extent);
		Across = FMath::Max(Across, bHorizontal ? Size.Y : Size.X);
	}
	const int32 Count = LiveOrder.Num();
	if (Count > 1)
	{
		Along = Layout.Pitch > 0.f
			? Layout.Pitch * static_cast<float>(Count - 1) + LongestExtent
			: Along + Layout.Spacing * static_cast<float>(Count - 1);
	}

	FVector2D Desired = bHorizontal ? FVector2D(Along, Across) : FVector2D(Across, Along);
	if (Layout.DesiredSize.X > 0.0)
	{
		Desired.X = Layout.DesiredSize.X;
	}
	if (Layout.DesiredSize.Y > 0.0)
	{
		Desired.Y = Layout.DesiredSize.Y;
	}
	return Desired;
}

// =====================================================================================================
// Ticking
// =====================================================================================================

void SFlowList::WakeUp()
{
	if (!ActiveTimer.IsValid())
	{
		ActiveTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFlowList::OnAnimate));
	}
}

EActiveTimerReturnType SFlowList::OnAnimate(double InCurrentTime, float InDeltaTime)
{
	using namespace FlowListPrivate;

	// A hitch shouldn't teleport anything: springs are exact for any step, but a drag polled across a long frame isn't.
	const float DeltaTime = FMath::Clamp(InDeltaTime, 0.f, 0.1f);

	if (bVisibilityDirty)
	{
		bVisibilityDirty = false;

		// Shown again: enters like a new item. Collapsed: goes at once, since a collapsed widget can't be drawn leaving.
		TArray<const FSlot*, TInlineAllocator<4>> Shown;
		for (int32 Index = 0; Index < Children.Num(); ++Index)
		{
			const FSlot& Slot = Children[Index];
			if (!Slot.bExiting && Slot.bCollapsed && Slot.GetWidget()->GetVisibility() != EVisibility::Collapsed)
			{
				Shown.Add(&Slot);
			}
		}
		NormalizeLoop();
		const FFocusAnchor Anchor = CaptureAnchor();
		RestoreAnchor(Anchor);
		if (Transitions.bAnimateChanges && !bDesignTime)
		{
			for (int32 Rank = 0; Rank < Shown.Num(); ++Rank)
			{
				const int32 Child = FindChild(Shown[Rank]);
				if (Child != INDEX_NONE)
				{
					StartEnter(Children[Child], GetEnterDelay() + GetBatchStagger() * static_cast<float>(Rank));
				}
			}
		}
	}

	PollPointer();
	UpdateStick(DeltaTime);
	StepScroll(DeltaTime);
	const bool bItemsBusy = StepItems(DeltaTime);

	// Hover Magnify swells while the pointer is over the list - and, by choice, the list isn't being dragged or flung - and
	// settles as it leaves. It keeps the list ticking while the pointer is over it, to follow the pointer.
	bool bMagnifyBusy = false;
	if (Effects.Magnify.bEnabled && !bDesignTime)
	{
		FVector2f Pointer;
		const bool bHasPointer = WidgetAnimInput::GetPointerPosition(Pointer);
		const bool bScrolling = Motion == EMotion::Drag || Motion == EMotion::Coast || Motion == EMotion::Stick;
		const bool bWanted = bHasPointer && IsHovered() && !(Effects.Magnify.bPauseWhileScrolling && bScrolling);
		if (bHasPointer && bWanted)
		{
			HoverAbsolute = Pointer;
		}
		const float Goal = bWanted ? 1.f : 0.f;
		const float Step = Effects.Magnify.FadeTime > 0.f ? DeltaTime / Effects.Magnify.FadeTime : 1.f;
		MagnifyAmount = MagnifyAmount < Goal ? FMath::Min(MagnifyAmount + Step, Goal) : FMath::Max(MagnifyAmount - Step, Goal);
		bMagnifyBusy = bWanted || MagnifyAmount > 0.f;
	}
	else
	{
		MagnifyAmount = 0.f;
	}

	// A virtual list shows entries for the items near the focus, and gives back the ones scrolled away.
	UpdateRealized();

	if (LiveOrder.Num() > 0 && Motion == EMotion::Idle)
	{
		if (!IsLooping())
		{
			// Out of range after a resize, or settings that changed where the end is: go back in range.
			const float Clamped = FMath::Clamp(Scroll, GetMinScroll(), GetMaxScroll());
			if (!FMath::IsNearlyEqual(Clamped, Scroll, RestDistance))
			{
				SpringTo(Clamped, 0.f, true);
			}
		}
		else if (!Press.bActive)
		{
			// Round a loop the position is only meaningful modulo the count: keep the number small. Not mid-press, which
			// measures a fling from where the press started.
			const float Count = static_cast<float>(LiveOrder.Num());
			float Wrapped = FMath::Fmod(Scroll, Count);
			if (Wrapped < 0.f)
			{
				Wrapped += Count;
			}
			const float Delta = Wrapped - Scroll;
			Scroll += Delta;
			Target += Delta;
		}
	}

	UpdateNearest();
	UpdateViewCues();
	FlushCues();

	// Only a frame where something moved is told of: a list held under a resting pointer, ticking for Hover Magnify, isn't.
	if (bItemsBusy || Motion != EMotion::Idle || Scroll != LastAnimatedScroll || MagnifyAmount != LastAnimatedMagnify || bWasMoving)
	{
		LastAnimatedScroll = Scroll;
		LastAnimatedMagnify = MagnifyAmount;
		OnAnimated.ExecuteIfBound();
	}

	const bool bMoving = Motion != EMotion::Idle;
	if (bWasMoving && !bMoving && LiveOrder.Num() > 0)
	{
		OnSettled.ExecuteIfBound(SelectedIndex);
	}
	bWasMoving = bMoving;

	++LayoutVersion;
	Invalidate(EInvalidateWidgetReason::Layout);

	if (bMoving || bItemsBusy || bMagnifyBusy || (Press.bActive && !Press.bTouch) || bVisibilityDirty)
	{
		return EActiveTimerReturnType::Continue;
	}
	ActiveTimer.Reset();
	return EActiveTimerReturnType::Stop;
}
