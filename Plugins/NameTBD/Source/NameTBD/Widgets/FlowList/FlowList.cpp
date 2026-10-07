// Copyright AR GameSystems 2026. All rights reserved.

#include "FlowList.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Slate/WidgetTransform.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SNullWidget.h"

#if WITH_EDITORONLY_DATA
#include "Engine/Blueprint.h"
#endif

#define LOCTEXT_NAMESPACE "FlowList"

DEFINE_LOG_CATEGORY_STATIC(LogFlowList, Log, All);

namespace FlowListAnimLog
{
	static TAutoConsoleVariable<int32> CVarLogItemAnimations(
		TEXT("FlowList.LogItemAnimations"),
		0,
		TEXT("Logs Flow Lists' item Widget Animations (Anim Enter Item / Anim Exit Item) to LogFlowList: 1 each preview, entrance and exit; 2 also every frame's pose."),
		ECVF_Default);

	int32 Level()
	{
		return CVarLogItemAnimations.GetValueOnGameThread();
	}
}

namespace FlowListRegistry
{
	/** Which list each virtual entry is shown by, while it is - entries aren't the list's children, so it can't be asked of UMG. */
	TMap<TWeakObjectPtr<const UUserWidget>, TWeakObjectPtr<UFlowList>>& EntryLists()
	{
		static TMap<TWeakObjectPtr<const UUserWidget>, TWeakObjectPtr<UFlowList>> Map;
		return Map;
	}
}

namespace FlowListSounds
{
	enum : int32
	{
		Select,
		Settle,
		Edge,
		Count
	};
}

// =====================================================================================================
// Slot
// =====================================================================================================

void UFlowListSlot::SetPadding(FMargin InPadding)
{
	Padding = InPadding;
	if (Slot)
	{
		Slot->SetPadding(InPadding);
		if (const TSharedPtr<SFlowList> Pinned = List.Pin())
		{
			// The cached layout and pitch measure slots too.
			Pinned->NotifySlotChanged();
		}
	}
}

void UFlowListSlot::SetSizeOverride(FVector2D InSizeOverride)
{
	SizeOverride = InSizeOverride;
	if (Slot)
	{
		Slot->SetSizeOverride(InSizeOverride);
		if (const TSharedPtr<SFlowList> Pinned = List.Pin())
		{
			// The cached layout and pitch measure slots too.
			Pinned->NotifySlotChanged();
		}
	}
}

void UFlowListSlot::BuildSlot(const TSharedRef<SFlowList>& InList, int32 Position, bool bAnimate)
{
	List = InList;
	const TSharedRef<SWidget> Widget = Content ? Content->TakeWidget() : SNullWidget::NullWidget;
	SlateContent = Widget;
	Slot = InList->InsertChild(Widget, Position, bAnimate);
	SynchronizeProperties();
}

void UFlowListSlot::SynchronizeProperties()
{
	SetPadding(Padding);
	SetSizeOverride(SizeOverride);
}

void UFlowListSlot::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Slot = nullptr;
	List.Reset();
	SlateContent.Reset();
}

// =====================================================================================================
// Widget
// =====================================================================================================

UFlowList::UFlowList()
{
	bCanHaveMultipleChildren = true;
	SoundRuntimes.SetNum(FlowListSounds::Count);
}

UClass* UFlowList::GetSlotClass() const
{
	return UFlowListSlot::StaticClass();
}

TSharedRef<SWidget> UFlowList::RebuildWidget()
{
	TGuardValue<bool> Building(bBuilding, true);

	MyFlowList = SNew(SFlowList);
	MyFlowList->OnSelectionChanged.BindUObject(this, &UFlowList::HandleSelectionChanged);
	MyFlowList->OnSettled.BindUObject(this, &UFlowList::HandleSettled);
	MyFlowList->OnItemClicked.BindUObject(this, &UFlowList::HandleItemClicked);
	MyFlowList->OnItemExited.BindUObject(this, &UFlowList::HandleItemExited);
	MyFlowList->OnEdgeReached.BindUObject(this, &UFlowList::HandleEdgeReached);
	MyFlowList->OnAnimated.BindUObject(this, &UFlowList::HandleAnimated);
	MyFlowList->OnItemTransitionStarted.BindUObject(this, &UFlowList::HandleItemTransitionStarted);
	PushSettings();

	if (IsVirtual())
	{
		// Entries instead of children: made as their items come into view.
		MyFlowList->OnRealizeEntry.BindUObject(this, &UFlowList::HandleRealizeEntry);
		MyFlowList->OnReleaseEntry.BindUObject(this, &UFlowList::HandleReleaseEntry);
		SyncVirtualItems(/*bRefresh=*/false);
	}
	else
	{
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (UFlowListSlot* TypedSlot = Cast<UFlowListSlot>(Slots[Index]))
			{
				TypedSlot->Parent = this;
				TypedSlot->BuildSlot(MyFlowList.ToSharedRef(), Index, /*bAnimate=*/false);
			}
		}
	}

	MyFlowList->SetScrollPosition(ScrollPosition, /*bAnimate=*/false);
	if (!IsDesignTime() && Transitions.Intro.bPlayIntro)
	{
		MyFlowList->PlayIntro();
	}
	return MyFlowList.ToSharedRef();
}

void UFlowList::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	PushSettings();

	// In the designer the start position is a preview: every edit shows the list scrolled to it.
	if (IsDesignTime() && MyFlowList.IsValid())
	{
		MyFlowList->SetScrollPosition(ScrollPosition, /*bAnimate=*/false);
	}
}

void UFlowList::ReleaseSlateResources(bool bReleaseChildren)
{
	// Leaving items draw from their UMG widgets: they go before those can be let go.
	if (MyFlowList.IsValid())
	{
		MyFlowList->DropAllLeaving();
	}
	Super::ReleaseSlateResources(bReleaseChildren);
	MyFlowList.Reset();
	StopAllItemAnimations();
	LeavingItems.Reset();
	LeavingSlateWidgets.Reset();
	ReleaseEntries();
	ReportedDistances.Reset();
}

void UFlowList::PushSettings()
{
	if (MyFlowList.IsValid())
	{
		MyFlowList->SetSettings(ListLayout, Effects, Scrolling, Transitions);
		MyFlowList->SetDesignTime(IsDesignTime(), bShowPathInDesigner);
		MyFlowList->SetFocusable(bListIsFocusable);

		// Without Basic Animations the items' own Widget Animations are the entrance and the exit: a leaving item is kept for
		// as long as its exit runs.
		if (!Transitions.bUseBasicAnimations)
		{
			auto Longest = [](const TArray<FWidgetAnimEntryBase>& Entries)
			{
				float Seconds = 0.f;
				for (const FWidgetAnimEntryBase& Entry : Entries)
				{
					if (!Entry.IsUsable())
					{
						continue;
					}
					TArray<FWidgetAnimSettings> Settings;
					Entry.BuildSettings(Settings, nullptr);
					float Length = UWidgetAnimLibrary::GetStackDuration(Settings);
					if (Length < 0.f)
					{
						// Loops forever: one pass, then it goes.
						Length = UWidgetAnimLibrary::GetStackPassDuration(Settings);
					}
					Seconds = FMath::Max(Seconds, Length);
				}
				return Seconds;
			};
			TArray<FWidgetAnimEntryBase> EnterEntries;
			TArray<FWidgetAnimEntryBase> ExitEntries;
			GetItemAnimationEntries(EnterEntries, ExitEntries);

			// A little over: the exit starts on the Slate list's clock a frame before the animation's own clock moves.
			const float ExitSeconds = Longest(ExitEntries);
			const float EnterSeconds = Longest(EnterEntries);
			MyFlowList->SetItemAnimationLengths(EnterSeconds, ExitSeconds > 0.f ? ExitSeconds + 0.05f : 0.f);
			if (FlowListAnimLog::Level() > 0)
			{
				UE_LOG(LogFlowList, Display, TEXT("%s: item animations - entrance %.2f s (%d entries), exit %.2f s (%d entries)."), *GetName(),
					EnterSeconds, EnterEntries.Num(), ExitSeconds, ExitEntries.Num());
			}
		}
	}
}

void UFlowList::OnSlotAdded(UPanelSlot* InSlot)
{
	// A virtual list shows its List Items; its children wait until the Entry Widget Class is cleared.
	if (IsVirtual())
	{
		return;
	}

	// Added back while it is still leaving: the leaving copy goes now. UMG let go of that Slate widget's parts when it was
	// removed, so it can't be reused - once it's gone, TakeWidget builds the item afresh.
	const int32 Leaving = InSlot->Content ? LeavingItems.IndexOfByKey(InSlot->Content) : INDEX_NONE;
	if (Leaving != INDEX_NONE)
	{
		if (MyFlowList.IsValid())
		{
			MyFlowList->DropLeaving(LeavingSlateWidgets[Leaving]);
		}
		LeavingItems.RemoveAt(Leaving);
		LeavingSlateWidgets.RemoveAt(Leaving);
		StopItemAnimation(InSlot->Content, /*bRestore=*/true);
	}

	if (MyFlowList.IsValid())
	{
		const int32 Position = PendingInsertIndex != INDEX_NONE ? PendingInsertIndex : Slots.Num() - 1;
		UFlowListSlot* TypedSlot = CastChecked<UFlowListSlot>(InSlot);
		TypedSlot->BuildSlot(MyFlowList.ToSharedRef(), Position, bAnimateNextChange && !IsDesignTime());
		if (FlowListAnimLog::Level() > 0)
		{
			bool bEntering = false;
			bool bExiting = false;
			const TSharedPtr<SWidget> Widget = TypedSlot->SlateContent.Pin();
			if (Widget.IsValid())
			{
				MyFlowList->GetItemTransition(Widget.ToSharedRef(), bEntering, bExiting);
			}
			UE_LOG(LogFlowList, Display, TEXT("%s: %s added at %d - asked to animate: %s, Animate Changes: %s, entrance started: %s."), *GetName(),
				InSlot->Content ? *InSlot->Content->GetName() : TEXT("(nothing)"), Position, bAnimateNextChange ? TEXT("yes") : TEXT("no"),
				Transitions.bAnimateChanges ? TEXT("on") : TEXT("off"), bEntering ? TEXT("yes") : TEXT("no"));
		}
	}
	else if (FlowListAnimLog::Level() > 0)
	{
		UE_LOG(LogFlowList, Display, TEXT("%s: %s added before the list was built - it shows without an entrance (Play Intro plays one)."), *GetName(),
			InSlot->Content ? *InSlot->Content->GetName() : TEXT("(nothing)"));
	}
}

void UFlowList::OnSlotRemoved(UPanelSlot* InSlot)
{
	if (!MyFlowList.IsValid() || !InSlot->Content || IsVirtual())
	{
		return;
	}
	// The content's slot is already cleared: the widget the list holds comes from the slot itself.
	const UFlowListSlot* TypedSlot = Cast<UFlowListSlot>(InSlot);
	const TSharedPtr<SWidget> Widget = TypedSlot && TypedSlot->SlateContent.IsValid() ? TypedSlot->SlateContent.Pin() : InSlot->Content->GetCachedWidget();
	if (!Widget.IsValid())
	{
		return;
	}
	const bool bLeaving = MyFlowList->RemoveChild(Widget.ToSharedRef(), bAnimateNextChange && !IsDesignTime());
	if (FlowListAnimLog::Level() > 0)
	{
		UE_LOG(LogFlowList, Display, TEXT("%s: %s removed - asked to animate: %s, Animate Changes: %s, exit started: %s."), *GetName(), *InSlot->Content->GetName(),
			bAnimateNextChange ? TEXT("yes") : TEXT("no"), Transitions.bAnimateChanges ? TEXT("on") : TEXT("off"), bLeaving ? TEXT("yes") : TEXT("no"));
	}
	if (!bLeaving && bAnimateNextChange && Transitions.bAnimateChanges && !Transitions.bUseBasicAnimations && !bWarnedNoExit
		&& !UWidgetAnimEntryLibrary::HasAnyUsableAnimation(Transitions.AnimExitItem)
		&& !Transitions.AnimEnterItem.ContainsByPredicate([](const FWidgetAnimEntryBase& Entry) { return Entry.bMirrorInOut && Entry.IsUsable(); }))
	{
		// Said once: nothing to play as it leaves, so it goes at once.
		bWarnedNoExit = true;
		UE_LOG(LogFlowList, Warning, TEXT("%s: an item was removed with no exit to play - Anim Exit Item has no usable entry (and no Anim Enter Item entry is set to Mirror In Out), so it went at once."),
			*GetPathName());
	}
	if (bLeaving)
	{
		LeavingItems.Add(InSlot->Content);
		LeavingSlateWidgets.Add(Widget.Get());
	}
	else
	{
		// Gone at once: whatever its entrance left it at is undone.
		StopItemAnimation(InSlot->Content, /*bRestore=*/true);
	}
}

// ---------------------------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------------------------

UFlowListSlot* UFlowList::AddItemAt(UWidget* Content, int32 Index, bool bAnimate)
{
	if (!Content)
	{
		return nullptr;
	}

	// Already one of this list's items: that's a move.
	if (Content->GetParent() == this)
	{
		MoveItem(Content, Index);
		return Cast<UFlowListSlot>(Content->Slot);
	}

	// AddChild always appends, and ShiftChild only reorders the UMG side, so the Slate slot is told where it goes up
	// front: it enters there, rather than appearing at the end and sliding across.
	const int32 Position = FMath::Clamp(Index, 0, Slots.Num());
	UPanelSlot* NewSlot = nullptr;
	{
		TGuardValue<int32> InsertAt(PendingInsertIndex, Position);
		TGuardValue<bool> Animate(bAnimateNextChange, bAnimate);
		NewSlot = AddChild(Content);
	}
	if (NewSlot && Position < Slots.Num() - 1)
	{
		ShiftChild(Position, Content);
	}
	return Cast<UFlowListSlot>(NewSlot);
}

UFlowListSlot* UFlowList::AddItem(UWidget* Content, bool bAnimate)
{
	return AddItemAt(Content, Slots.Num(), bAnimate);
}

UFlowListSlot* UFlowList::PushFront(UWidget* Content, bool bAnimate)
{
	return AddItemAt(Content, 0, bAnimate);
}

UFlowListSlot* UFlowList::InsertItem(UWidget* Content, int32 Index, bool bAnimate)
{
	return AddItemAt(Content, Index, bAnimate);
}

bool UFlowList::RemoveItemInternal(UWidget* Content, bool bAnimate)
{
	if (!Content || Content->GetParent() != this)
	{
		return false;
	}
	TGuardValue<bool> Animate(bAnimateNextChange, bAnimate);
	return RemoveChild(Content);
}

bool UFlowList::RemoveItem(UWidget* Content, bool bAnimate)
{
	return RemoveItemInternal(Content, bAnimate);
}

bool UFlowList::RemoveItemAt(int32 Index, bool bAnimate)
{
	return RemoveItemInternal(GetItemAt(Index), bAnimate);
}

UWidget* UFlowList::PopFront(bool bAnimate)
{
	UWidget* Item = GetItemAt(0);
	return RemoveItemInternal(Item, bAnimate) ? Item : nullptr;
}

UWidget* UFlowList::PopBack(bool bAnimate)
{
	UWidget* Item = GetItemAt(GetItemCount() - 1);
	return RemoveItemInternal(Item, bAnimate) ? Item : nullptr;
}

void UFlowList::MoveItem(UWidget* Content, int32 Index)
{
	if (!Content || Content->GetParent() != this || Slots.Num() == 0)
	{
		return;
	}
	const int32 Position = FMath::Clamp(Index, 0, Slots.Num() - 1);
	ShiftChild(Position, Content);
	if (MyFlowList.IsValid())
	{
		if (const TSharedPtr<SWidget> Widget = GetItemSlateWidget(Content))
		{
			MyFlowList->MoveChild(Widget.ToSharedRef(), Position);
		}
	}
}

void UFlowList::ClearItems(bool bAnimate)
{
	TGuardValue<bool> Animate(bAnimateNextChange, bAnimate);
	ClearChildren();
}

int32 UFlowList::GetItemCount() const
{
	if (MyFlowList.IsValid())
	{
		return MyFlowList->GetItemCount();
	}
	if (IsVirtual())
	{
		return ListItems.Num();
	}
	int32 Count = 0;
	for (const UPanelSlot* PanelSlot : Slots)
	{
		if (PanelSlot && PanelSlot->Content && PanelSlot->Content->GetVisibility() != ESlateVisibility::Collapsed)
		{
			++Count;
		}
	}
	return Count;
}

TSharedPtr<SWidget> UFlowList::GetItemSlateWidget(const UWidget* Item) const
{
	if (!Item)
	{
		return nullptr;
	}
	if (const UFlowListSlot* TypedSlot = Cast<UFlowListSlot>(Item->Slot))
	{
		if (const TSharedPtr<SWidget> Held = TypedSlot->SlateContent.Pin())
		{
			return Held;
		}
	}
	for (const FFlowListEntryWidget& Entry : ActiveEntries)
	{
		if (Entry.Widget == Item && Entry.Slate.IsValid())
		{
			return Entry.Slate;
		}
	}
	return Item->GetCachedWidget();
}

UWidget* UFlowList::FindItemWidget(const TSharedPtr<SWidget>& Widget) const
{
	if (!Widget.IsValid())
	{
		return nullptr;
	}
	for (const UPanelSlot* PanelSlot : Slots)
	{
		if (PanelSlot && PanelSlot->Content && GetItemSlateWidget(PanelSlot->Content) == Widget)
		{
			return PanelSlot->Content;
		}
	}
	return FindEntry(Widget);
}

UWidget* UFlowList::GetItemAt(int32 Index) const
{
	if (Index < 0)
	{
		return nullptr;
	}
	if (MyFlowList.IsValid())
	{
		return FindItemWidget(MyFlowList->GetItemWidget(Index));
	}

	// Not built yet: the same count the list will make, skipping collapsed children.
	int32 Seen = 0;
	for (const UPanelSlot* PanelSlot : Slots)
	{
		if (PanelSlot && PanelSlot->Content && PanelSlot->Content->GetVisibility() != ESlateVisibility::Collapsed)
		{
			if (Seen++ == Index)
			{
				return PanelSlot->Content;
			}
		}
	}
	return nullptr;
}

int32 UFlowList::GetIndexOfItem(const UWidget* Item) const
{
	// A virtual list's entries aren't its children: they are found by the Slate widget they are drawn with.
	if (Item && IsVirtual())
	{
		const TSharedPtr<SWidget> Widget = GetItemSlateWidget(Item);
		return Widget.IsValid() && MyFlowList.IsValid() && FindEntry(Widget) ? MyFlowList->GetItemIndex(Widget.ToSharedRef()) : INDEX_NONE;
	}
	if (!Item || Item->GetParent() != this)
	{
		return INDEX_NONE;
	}
	if (MyFlowList.IsValid())
	{
		const TSharedPtr<SWidget> Widget = GetItemSlateWidget(Item);
		return Widget.IsValid() ? MyFlowList->GetItemIndex(Widget.ToSharedRef()) : INDEX_NONE;
	}
	int32 Seen = 0;
	for (const UPanelSlot* PanelSlot : Slots)
	{
		if (PanelSlot && PanelSlot->Content && PanelSlot->Content->GetVisibility() != ESlateVisibility::Collapsed)
		{
			if (PanelSlot->Content == Item)
			{
				return Seen;
			}
			++Seen;
		}
	}
	return INDEX_NONE;
}

float UFlowList::GetItemDistance(const UWidget* Item) const
{
	const int32 Index = GetIndexOfItem(Item);
	if (Index == INDEX_NONE)
	{
		return TNumericLimits<float>::Max();
	}
	return MyFlowList.IsValid() ? MyFlowList->GetItemDistance(Index) : static_cast<float>(Index) - ScrollPosition;
}

void UFlowList::PlayIntro()
{
	if (MyFlowList.IsValid())
	{
		MyFlowList->PlayIntro();
	}
}

void UFlowList::PreviewEntrances()
{
	if (FlowListAnimLog::Level() > 0)
	{
		UE_LOG(LogFlowList, Display, TEXT("%s: previewing %d items' entrances (%s)."), *GetName(), GetItemCount(),
			Transitions.bUseBasicAnimations ? TEXT("Basic Animations") : TEXT("Anim Enter Item"));
	}
	PlayIntro();
}

void UFlowList::PreviewItem(UWidget* Item, bool bExit)
{
	const TSharedPtr<SWidget> Widget = GetItemSlateWidget(Item);
	if (FlowListAnimLog::Level() > 0)
	{
		UE_LOG(LogFlowList, Display, TEXT("%s: previewing %s's %s."), *GetName(), Item ? *Item->GetName() : TEXT("(none)"), bExit ? TEXT("exit") : TEXT("entrance"));
	}
	if (MyFlowList.IsValid() && Widget.IsValid())
	{
		MyFlowList->PlayItemPreview(Widget.ToSharedRef(), bExit);
	}
}

void UFlowList::PreviewExits()
{
	if (MyFlowList.IsValid())
	{
		MyFlowList->PlayExitPreview();
	}
}

// ---------------------------------------------------------------------------------------------
// Scrolling
// ---------------------------------------------------------------------------------------------

void UFlowList::ScrollToIndex(int32 Index, bool bAnimate)
{
	if (MyFlowList.IsValid())
	{
		MyFlowList->ScrollToIndex(Index, bAnimate);
	}
	else
	{
		ScrollPosition = static_cast<float>(FMath::Max(Index, 0));
	}
}

void UFlowList::ScrollToItem(UWidget* Item, bool bAnimate)
{
	const int32 Index = GetIndexOfItem(Item);
	if (Index != INDEX_NONE)
	{
		ScrollToIndex(Index, bAnimate);
	}
}

bool UFlowList::ScrollBy(int32 Steps, bool bWrap)
{
	return MyFlowList.IsValid() && MyFlowList->ScrollBy(Steps, bWrap);
}

void UFlowList::SetScrollPosition(float Position, bool bAnimate)
{
	if (MyFlowList.IsValid())
	{
		MyFlowList->SetScrollPosition(Position, bAnimate);
	}
	else
	{
		ScrollPosition = Position;
	}
}

void UFlowList::SnapToNearestItem()
{
	if (MyFlowList.IsValid())
	{
		MyFlowList->Settle();
	}
}

float UFlowList::GetScrollPosition() const
{
	return MyFlowList.IsValid() ? MyFlowList->GetScrollPosition() : ScrollPosition;
}

float UFlowList::GetScrollFraction() const
{
	return MyFlowList.IsValid() ? MyFlowList->GetScrollFraction() : 0.f;
}

void UFlowList::SetScrollFraction(float Fraction, bool bAnimate)
{
	if (MyFlowList.IsValid())
	{
		MyFlowList->SetScrollFraction(Fraction, bAnimate);
	}
}

float UFlowList::GetViewFraction() const
{
	return MyFlowList.IsValid() ? MyFlowList->GetViewFraction() : 1.f;
}

int32 UFlowList::GetSelectedIndex() const
{
	return MyFlowList.IsValid() ? MyFlowList->GetSelectedIndex() : INDEX_NONE;
}

UWidget* UFlowList::GetSelectedItem() const
{
	return GetItemAt(GetSelectedIndex());
}

bool UFlowList::IsScrolling() const
{
	return MyFlowList.IsValid() && MyFlowList->IsScrolling();
}

// ---------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------

void UFlowList::ApplyStylePreset(EFlowListStylePreset Preset)
{
	FlowListTypes::ApplyPreset(Preset, ListLayout, Effects, Scrolling, Transitions);
	StylePreset = Preset;
	PushSettings();
}

void UFlowList::SetListLayout(const FFlowListLayout& InLayout)
{
	ListLayout = InLayout;
	PushSettings();
}

void UFlowList::SetEffects(const FFlowListEffects& InEffects)
{
	Effects = InEffects;
	PushSettings();
}

void UFlowList::SetScrolling(const FFlowListScrolling& InScrolling)
{
	Scrolling = InScrolling;
	PushSettings();
}

void UFlowList::SetTransitions(const FFlowListTransitions& InTransitions)
{
	Transitions = InTransitions;
	PushSettings();
}

int32 UFlowList::GetItemValue(int32 Index) const
{
	if (Index < 0 || Index >= GetItemCount())
	{
		return INDEX_NONE;
	}
	if (!IsVirtual())
	{
		if (const UWidget* Item = GetItemAt(Index))
		{
			if (const UFlowListSlot* TypedSlot = Cast<UFlowListSlot>(Item->Slot))
			{
				if (TypedSlot->ItemValue >= 0)
				{
					return TypedSlot->ItemValue;
				}
			}
		}
	}
	return Index;
}

// ---------------------------------------------------------------------------------------------
// Virtual list
// ---------------------------------------------------------------------------------------------

void UFlowList::SyncVirtualItems(bool bRefresh)
{
	if (MyFlowList.IsValid() && IsVirtual())
	{
		// The designer has no items to show: it previews entries with none.
		const int32 Count = IsDesignTime() ? NumDesignerPreviewEntries : ListItems.Num();
		MyFlowList->SetVirtualItems(Count, bRefresh);
	}
	OnListUpdated.Broadcast();
}

void UFlowList::SetListItems(const TArray<UObject*>& InListItems)
{
	ListItems.Reset(InListItems.Num());
	for (UObject* Item : InListItems)
	{
		ListItems.Add(Item);
	}
	SyncVirtualItems(/*bRefresh=*/true);
}

TArray<UObject*> UFlowList::GetListItems() const
{
	TArray<UObject*> Items;
	Items.Reserve(ListItems.Num());
	for (UObject* Item : ListItems)
	{
		Items.Add(Item);
	}
	return Items;
}

void UFlowList::AddListItem(UObject* ListItem, bool bAnimate)
{
	InsertListItem(ListItem, ListItems.Num(), bAnimate);
}

void UFlowList::InsertListItem(UObject* ListItem, int32 Index, bool bAnimate)
{
	Index = FMath::Clamp(Index, 0, ListItems.Num());
	ListItems.Insert(ListItem, Index);
	if (MyFlowList.IsValid() && IsVirtual() && !IsDesignTime())
	{
		MyFlowList->NotifyVirtualInserted(Index, bAnimate);
	}
	OnListUpdated.Broadcast();
}

bool UFlowList::RemoveListItem(UObject* ListItem, bool bAnimate)
{
	return RemoveListItemAt(GetIndexOfListItem(ListItem), bAnimate);
}

bool UFlowList::RemoveListItemAt(int32 Index, bool bAnimate)
{
	if (!ListItems.IsValidIndex(Index))
	{
		return false;
	}
	ListItems.RemoveAt(Index);
	if (MyFlowList.IsValid() && IsVirtual() && !IsDesignTime())
	{
		MyFlowList->NotifyVirtualRemoved(Index, bAnimate);
	}
	OnListUpdated.Broadcast();
	return true;
}

void UFlowList::ClearListItems()
{
	ListItems.Reset();
	SyncVirtualItems(/*bRefresh=*/true);
}

void UFlowList::RefreshListItems()
{
	SyncVirtualItems(/*bRefresh=*/true);
}

UObject* UFlowList::GetListItemAt(int32 Index) const
{
	return ListItems.IsValidIndex(Index) ? ListItems[Index].Get() : nullptr;
}

int32 UFlowList::GetIndexOfListItem(const UObject* ListItem) const
{
	return ListItem ? ListItems.IndexOfByKey(ListItem) : INDEX_NONE;
}

UObject* UFlowList::GetSelectedListItem() const
{
	return GetListItemAt(GetSelectedIndex());
}

UUserWidget* UFlowList::GetEntryForListItem(const UObject* ListItem) const
{
	const int32 Index = GetIndexOfListItem(ListItem);
	return Index != INDEX_NONE && MyFlowList.IsValid() ? FindEntry(MyFlowList->GetItemWidget(Index)) : nullptr;
}

void UFlowList::ScrollToListItem(UObject* ListItem, bool bAnimate)
{
	const int32 Index = GetIndexOfListItem(ListItem);
	if (Index != INDEX_NONE)
	{
		ScrollToIndex(Index, bAnimate);
	}
}

UUserWidget* UFlowList::FindEntry(const TSharedPtr<SWidget>& Widget) const
{
	if (!Widget.IsValid())
	{
		return nullptr;
	}
	for (const FFlowListEntryWidget& Entry : ActiveEntries)
	{
		if (Entry.Slate == Widget)
		{
			return Entry.Widget;
		}
	}
	return nullptr;
}

TSharedPtr<SWidget> UFlowList::HandleRealizeEntry(int32 Index)
{
	if (!EntryWidgetClass)
	{
		return nullptr;
	}

	// From the pool when there is one of the class, else a new one.
	FFlowListEntryWidget Entry;
	while (EntryPool.Num() > 0 && !Entry.Widget)
	{
		Entry = EntryPool.Pop(EAllowShrinking::No);
		if (Entry.Widget && Entry.Widget->GetClass() != EntryWidgetClass)
		{
			Entry = FFlowListEntryWidget();
		}
	}
	if (!Entry.Widget)
	{
		Entry.Widget = CreateWidget<UUserWidget>(this, EntryWidgetClass);
		if (!Entry.Widget)
		{
			return nullptr;
		}
#if WITH_EDITOR
		if (IsDesignTime())
		{
			Entry.Widget->SetDesignerFlags(GetDesignerFlags());
		}
#endif
	}
	// Known as this list's before it is built: what it constructs - a Flow List Item Style - looks for its list at once.
	FlowListRegistry::EntryLists().Add(Entry.Widget.Get(), this);
	if (!Entry.Slate.IsValid())
	{
		Entry.Slate = Entry.Widget->TakeWidget();
	}
	ActiveEntries.Add(Entry);

	if (Entry.Widget->Implements<UFlowListEntry>())
	{
		IFlowListEntry::Execute_OnFlowListItemSet(Entry.Widget, this, GetListItemAt(Index), Index);
	}

	// An entry built at the selection - scrolled back into view, or the first build - is told so, as a plain item is.
	if (Index == GetSelectedIndex())
	{
		UWidget* Previous = SelectedItem.Get();
		if (Previous && Previous != Entry.Widget && Previous->Implements<UFlowListItem>())
		{
			IFlowListItem::Execute_OnFlowListSelected(Previous, this, false);
		}
		SelectedItem = Entry.Widget.Get();
		if (Entry.Widget->Implements<UFlowListItem>())
		{
			IFlowListItem::Execute_OnFlowListSelected(Entry.Widget, this, true);
		}
	}
	return Entry.Slate;
}

void UFlowList::HandleReleaseEntry(int32 Index, const TSharedRef<SWidget>& Widget)
{
	const int32 Found = ActiveEntries.IndexOfByPredicate([&Widget](const FFlowListEntryWidget& Entry) { return Entry.Slate == Widget; });
	if (Found == INDEX_NONE)
	{
		return;
	}
	FFlowListEntryWidget Entry = ActiveEntries[Found];
	ActiveEntries.RemoveAtSwap(Found);
	ReportedDistances.Remove(&Widget.Get());
	if (Entry.Widget)
	{
		// Pooled entries come back as other items: one leaving as the selection stops being it.
		if (SelectedItem.Get() == Entry.Widget)
		{
			SelectedItem = nullptr;
			if (Entry.Widget->Implements<UFlowListItem>())
			{
				IFlowListItem::Execute_OnFlowListSelected(Entry.Widget, this, false);
			}
		}
		FlowListRegistry::EntryLists().Remove(Entry.Widget.Get());
		if (Entry.Widget->Implements<UFlowListEntry>())
		{
			IFlowListEntry::Execute_OnFlowListEntryReleased(Entry.Widget, this);
		}

		// Back to the pool as it was authored, whatever its entrance or exit left it at.
		StopItemAnimation(Entry.Widget, /*bRestore=*/true);
		EntryPool.Add(Entry);
	}
}

void UFlowList::ReleaseEntries()
{
	for (const FFlowListEntryWidget& Entry : ActiveEntries)
	{
		if (Entry.Widget)
		{
			FlowListRegistry::EntryLists().Remove(Entry.Widget.Get());
		}
	}
	ActiveEntries.Reset();
	EntryPool.Reset();
}

// ---------------------------------------------------------------------------------------------
// Finding lists
// ---------------------------------------------------------------------------------------------

UFlowList* UFlowList::FindFlowListOfItem(const UWidget* Widget, UWidget*& OutItem, int32& OutIndex)
{
	OutItem = nullptr;
	OutIndex = INDEX_NONE;

	// Up through the panels it sits in, and out of each Widget Blueprint into the one it is placed in, until a Flow List
	// - or a virtual list's entry - is found.
	const UWidget* Current = Widget;
	for (int32 Guard = 0; Current && Guard < 64; ++Guard)
	{
		if (const UUserWidget* AsUserWidget = Cast<UUserWidget>(Current))
		{
			if (const TWeakObjectPtr<UFlowList>* Owner = FlowListRegistry::EntryLists().Find(AsUserWidget))
			{
				if (UFlowList* List = Owner->Get())
				{
					OutItem = const_cast<UUserWidget*>(AsUserWidget);
					const TSharedPtr<SWidget> Slate = List->GetItemSlateWidget(AsUserWidget);
					OutIndex = Slate.IsValid() && List->MyFlowList.IsValid() ? List->MyFlowList->GetItemIndex(Slate.ToSharedRef()) : INDEX_NONE;
					return List;
				}
			}
		}

		UPanelWidget* Parent = Current->GetParent();
		if (UFlowList* List = Cast<UFlowList>(Parent))
		{
			OutItem = const_cast<UWidget*>(Current);
			OutIndex = List->GetIndexOfItem(Current);
			return List;
		}
		Current = Parent ? static_cast<const UWidget*>(Parent) : static_cast<const UWidget*>(Current->GetTypedOuter<UUserWidget>());
	}
	return nullptr;
}

bool UFlowList::IsInSelectedItem(const UWidget* Widget)
{
	UWidget* Item = nullptr;
	int32 Index = INDEX_NONE;
	const UFlowList* List = FindFlowListOfItem(Widget, Item, Index);
	return List && Index != INDEX_NONE && Index == List->GetSelectedIndex();
}

namespace FlowListFind
{
	/** The first Flow List in Root's subtree, Root included, nested Widget Blueprints and all - nearest first, level by level. */
	UFlowList* FindInside(UWidget* Root, const UWidget* Skip)
	{
		TArray<UWidget*, TInlineAllocator<32>> Queue;
		Queue.Add(Root);
		for (int32 Head = 0; Head < Queue.Num() && Head < 4096; ++Head)
		{
			UWidget* Widget = Queue[Head];
			if (!Widget || Widget == Skip)
			{
				continue;
			}
			if (UFlowList* List = Cast<UFlowList>(Widget))
			{
				return List;
			}
			if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			{
				for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
				{
					Queue.Add(Panel->GetChildAt(Index));
				}
			}
			else if (const UUserWidget* User = Cast<UUserWidget>(Widget))
			{
				if (User->WidgetTree)
				{
					Queue.Add(User->WidgetTree->RootWidget);
				}
			}
		}
		return nullptr;
	}
}

UFlowList* UFlowList::FindNearestFlowList(const UWidget* Widget, FName ListName)
{
	if (!Widget)
	{
		return nullptr;
	}

	// By name: in its own Widget Blueprint's tree, then in each one around it.
	if (!ListName.IsNone())
	{
		for (const UUserWidget* User = Widget->GetTypedOuter<UUserWidget>(); User; User = User->GetTypedOuter<UUserWidget>())
		{
			if (User->WidgetTree)
			{
				if (UFlowList* Named = Cast<UFlowList>(User->WidgetTree->FindWidget(ListName)))
				{
					return Named;
				}
			}
		}
		if (const UUserWidget* AsUser = Cast<UUserWidget>(Widget))
		{
			if (AsUser->WidgetTree)
			{
				return Cast<UFlowList>(AsUser->WidgetTree->FindWidget(ListName));
			}
		}
		return nullptr;
	}

	// The list it is an item of; itself; inside it; then widening out a level at a time through what it sits in.
	UWidget* Item = nullptr;
	int32 Index = INDEX_NONE;
	if (UFlowList* Owner = FindFlowListOfItem(Widget, Item, Index))
	{
		return Owner;
	}
	UWidget* Current = const_cast<UWidget*>(Widget);
	const UWidget* Searched = nullptr;
	for (int32 Guard = 0; Current && Guard < 64; ++Guard)
	{
		if (UFlowList* Found = FlowListFind::FindInside(Current, Searched))
		{
			return Found;
		}
		Searched = Current;
		UPanelWidget* Parent = Current->GetParent();
		Current = Parent ? static_cast<UWidget*>(Parent) : static_cast<UWidget*>(Current->GetTypedOuter<UUserWidget>());
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Slate events
// ---------------------------------------------------------------------------------------------

void UFlowList::HandleSelectionChanged(int32 Index, int32 PreviousIndex)
{
	UWidget* Item = GetItemAt(Index);
	UWidget* Previous = SelectedItem.Get();

	// The index can change without the item changing - one added in front of it - and the item hears only real changes.
	if (Previous != Item)
	{
		if (Previous && Previous->Implements<UFlowListItem>())
		{
			IFlowListItem::Execute_OnFlowListSelected(Previous, this, false);
		}
		if (Item && Item->Implements<UFlowListItem>())
		{
			IFlowListItem::Execute_OnFlowListSelected(Item, this, true);
		}
		SelectedItem = Item;

		// Real time, not game time: a pause menu's list ticks too.
		const double Now = FPlatformTime::Seconds();
		if (!bBuilding && Item && Now - LastTickTime >= MinTimeBetweenTicks)
		{
			LastTickTime = Now;
			PlaySound(SelectSound, FlowListSounds::Select);
		}
	}
	OnSelectionChanged.Broadcast(Index, PreviousIndex, Item);
	OnListUpdated.Broadcast();
}

void UFlowList::HandleSettled(int32 Index)
{
	PlaySound(SettleSound, FlowListSounds::Settle);
	OnSettled.Broadcast(Index, GetItemAt(Index));
}

void UFlowList::HandleItemClicked(int32 Index)
{
	OnItemClicked.Broadcast(Index, GetItemAt(Index));
}

void UFlowList::HandleItemExited(const TSharedRef<SWidget>& Widget)
{
	const int32 Found = LeavingSlateWidgets.IndexOfByKey(&Widget.Get());
	if (Found == INDEX_NONE)
	{
		return;
	}
	UWidget* Item = LeavingItems[Found];
	LeavingItems.RemoveAt(Found);
	LeavingSlateWidgets.RemoveAt(Found);
	StopItemAnimation(Item, /*bRestore=*/true);
	OnItemExited.Broadcast(Item);
}

void UFlowList::HandleEdgeReached()
{
	PlaySound(EdgeSound, FlowListSounds::Edge);
	OnEdgeReached.Broadcast();
}

void UFlowList::HandleAnimated()
{
	if (!MyFlowList.IsValid())
	{
		return;
	}

	const float Position = MyFlowList->GetScrollPosition();
	if (Position != LastReportedScroll)
	{
		LastReportedScroll = Position;
		OnScrolled.Broadcast(Position);
	}
	OnListUpdated.Broadcast();

	if (!bReportItemDistance || !MyFlowList.IsValid())
	{
		return;
	}

	// Gathered first and told after: an item's handler may add or remove items, which would pull the list out from
	// under a loop over it. Children and a virtual list's entries alike, by the Slate widget they are drawn with.
	TMap<const SWidget*, UWidget*> ItemsBySlateWidget;
	for (UPanelSlot* PanelSlot : Slots)
	{
		if (PanelSlot && PanelSlot->Content)
		{
			if (const TSharedPtr<SWidget> Widget = GetItemSlateWidget(PanelSlot->Content))
			{
				ItemsBySlateWidget.Add(Widget.Get(), PanelSlot->Content);
			}
		}
	}
	for (const FFlowListEntryWidget& Entry : ActiveEntries)
	{
		if (Entry.Widget && Entry.Slate.IsValid())
		{
			ItemsBySlateWidget.Add(Entry.Slate.Get(), Entry.Widget);
		}
	}

	TArray<TPair<TWeakObjectPtr<UWidget>, float>, TInlineAllocator<16>> Changed;
	MyFlowList->ForEachItem([this, &ItemsBySlateWidget, &Changed](int32 Index, const TSharedRef<SWidget>& Widget, float Distance)
	{
		UWidget* const* Found = ItemsBySlateWidget.Find(&Widget.Get());
		float& Reported = ReportedDistances.FindOrAdd(&Widget.Get(), TNumericLimits<float>::Max());
		if (!Found || FMath::Abs(Reported - Distance) < 0.0005f)
		{
			return;
		}
		Reported = Distance;
		if ((*Found)->Implements<UFlowListItem>())
		{
			Changed.Emplace(*Found, Distance);
		}
	});

	// Widgets no longer items: forgotten, so the map doesn't grow with every entry a long list shows.
	if (ReportedDistances.Num() > ItemsBySlateWidget.Num() * 2 + 16)
	{
		for (auto It = ReportedDistances.CreateIterator(); It; ++It)
		{
			if (!ItemsBySlateWidget.Contains(It.Key()))
			{
				It.RemoveCurrent();
			}
		}
	}

	for (const TPair<TWeakObjectPtr<UWidget>, float>& Entry : Changed)
	{
		if (UWidget* Item = Entry.Key.Get())
		{
			IFlowListItem::Execute_OnFlowListDistanceChanged(Item, this, Entry.Value);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Items' own entrance and exit animations
// ---------------------------------------------------------------------------------------------

UWidget* UFlowList::FindAnyItemWidget(const TSharedRef<SWidget>& Widget) const
{
	if (UWidget* Item = FindItemWidget(Widget))
	{
		return Item;
	}
	const int32 Leaving = LeavingSlateWidgets.IndexOfByKey(&Widget.Get());
	return Leaving != INDEX_NONE ? LeavingItems[Leaving].Get() : nullptr;
}

void UFlowList::GetItemAnimationEntries(TArray<FWidgetAnimEntryBase>& OutEnter, TArray<FWidgetAnimEntryBase>& OutExit) const
{
	// As a Widget Animation Component pairs its Anim In and Anim Out: an entry set to Mirror In Out plays both ways.
	OutEnter.Reset();
	OutExit.Reset();
	UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(Transitions.AnimEnterItem, OutEnter, OutExit);
	UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(Transitions.AnimExitItem, OutExit, OutEnter);
}

void UFlowList::HandleItemTransitionStarted(const TSharedRef<SWidget>& Widget, bool bEnter, EFlowListItemCue Cue)
{
	// The designer's preview plays them too - see PreviewEntrances.
	if (Transitions.bUseBasicAnimations)
	{
		return;
	}
	UWidget* Item = FindAnyItemWidget(Widget);
	if (FlowListAnimLog::Level() > 0)
	{
		const TCHAR* Why = Cue == EFlowListItemCue::View ? (bEnter ? TEXT(" (scrolled into view)") : TEXT(" (scrolled out of view)"))
			: Cue == EFlowListItemCue::Selection ? (bEnter ? TEXT(" (selected)") : TEXT(" (deselected)")) : TEXT("");
		UE_LOG(LogFlowList, Display, TEXT("%s: %s %s for %s%s."), *GetName(), IsDesignTime() ? TEXT("[designer]") : TEXT("[runtime]"),
			bEnter ? TEXT("entrance starts") : TEXT("exit starts"), Item ? *Item->GetName() : TEXT("an item that wasn't found"), Why);
	}
	if (Item)
	{
		StartItemAnimation(Item, Widget, bEnter, /*bHoldPose=*/Cue != EFlowListItemCue::Transition);
	}
	else if (!bWarnedItemNotFound)
	{
		// Said once: an item the Slate list drew but this list can't name has nothing to play its animation on.
		bWarnedItemNotFound = true;
		UE_LOG(LogFlowList, Warning, TEXT("%s: an item's %s started, but its widget wasn't found - its Widget Animations can't play."),
			*GetPathName(), bEnter ? TEXT("entrance") : TEXT("exit"));
	}
}

void UFlowList::StartItemAnimation(UWidget* Item, const TSharedRef<SWidget>& Slate, bool bEnter, bool bHoldPose)
{
	if (!Item)
	{
		return;
	}

	TArray<FWidgetAnimEntryBase> EnterEntries;
	TArray<FWidgetAnimEntryBase> ExitEntries;
	GetItemAnimationEntries(EnterEntries, ExitEntries);
	const TArray<FWidgetAnimEntryBase>& Entries = bEnter ? EnterEntries : ExitEntries;

	// Nothing to play this way: the item is left exactly as it is - an entrance's end pose stays, nothing plays as an exit.
	// Only Anim Exit Item plays as an item is deselected, scrolls out of view or leaves.
	if (!UWidgetAnimEntryLibrary::HasAnyUsableAnimation(Entries))
	{
		if (bEnter && !bWarnedNoAnimation)
		{
			// Said once: an entry with no animation set - or one with no tracks - plays nothing.
			bWarnedNoAnimation = true;
			UE_LOG(LogFlowList, Warning, TEXT("%s: Use Basic Animations is off, but Anim Enter Item has no entry with an animation that has tracks - nothing plays."),
				*GetPathName());
		}
		if (FlowListAnimLog::Level() > 0)
		{
			UE_LOG(LogFlowList, Display, TEXT("%s: %s has nothing to play - %s stays as it is."), *GetName(), bEnter ? TEXT("Anim Enter Item") : TEXT("Anim Exit Item"),
				*Item->GetName());
		}
		return;
	}

	int32 Found = ItemAnimations.IndexOfByPredicate([Item](const FFlowListItemAnimation& Animation) { return Animation.Widget.Get() == Item; });
	if (Found == INDEX_NONE)
	{
		Found = ItemAnimations.AddDefaulted();
		ItemAnimations[Found].Widget = Item;
	}
	FFlowListItemAnimation& Animation = ItemAnimations[Found];
	Animation.Slate = Slate;
	Animation.bHoldPose = bHoldPose;

	// What the animations touch besides the item, as it is before they do: captured once, so an exit cut into an entrance
	// doesn't take the entrance's pose for theirs. The item's own transform and opacity aren't among them: the list draws the
	// item's pose itself (see PoseItemAnimation), so the item's own settings are never written.
	TArray<FWidgetAnimSettings> AllEnter;
	TArray<FWidgetAnimSettings> AllExit;
	for (const FWidgetAnimEntryBase& Entry : EnterEntries)
	{
		Entry.BuildSettings(AllEnter, Item);
	}
	for (const FWidgetAnimEntryBase& Entry : ExitEntries)
	{
		Entry.BuildSettings(AllExit, Item);
	}
	if (!Animation.Baselines.bCaptured)
	{
		UWidgetAnimLibrary::CaptureWidgetAnimBaselines(Item, AllEnter, AllExit, Animation.Baselines);
		Animation.Baselines.Baselines.RemoveAll([Item](const FWidgetAnimBaseline& Baseline) { return Baseline.Widget.Get() == Item; });
	}

	// One way at a time: an item removed while it still arrives leaves from there.
	UWidgetAnimLibrary::StopWidgetAnimTracks(Animation.Player, TArray<FName>(), /*bRemoveTracks=*/true);
	int32 Started = 0;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FWidgetAnimEntryBase& Entry = Entries[Index];
		if (!Entry.IsUsable())
		{
			continue;
		}
		TArray<FWidgetAnimSettings> Settings;
		Entry.BuildSettings(Settings, Item);
		if (Settings.Num() > 0)
		{
			UWidgetAnimLibrary::StartWidgetAnimTrack(Animation.Player, Entry.AnimId, Index, Settings, /*bIsOut=*/!bEnter, 1.f,
				/*bRestartIfPlaying=*/true, /*bHoldWhenFinished=*/true);
			++Started;
		}
	}
	if (FlowListAnimLog::Level() > 0)
	{
		UE_LOG(LogFlowList, Display, TEXT("%s: %s - %d of %d entries started on %s."), *GetName(), bEnter ? TEXT("Anim Enter Item") : TEXT("Anim Exit Item"),
			Started, Entries.Num(), *Item->GetName());
	}

	// Posed now, at its first frame, before the item is drawn.
	PoseItemAnimation(Animation, 0.f);

	if (!ItemAnimationTicker.IsValid())
	{
		ItemAnimationTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UFlowList::TickItemAnimations));
	}
}

bool UFlowList::PoseItemAnimation(FFlowListItemAnimation& Animation, float DeltaTime)
{
	UWidget* Item = Animation.Widget.Get();
	if (!Item)
	{
		return false;
	}

	FWidgetAnimResult Result;
	TArray<FName> JustFinished;
	const bool bPlaying = UWidgetAnimLibrary::EvaluateWidgetAnimPlayer(Item, Animation.Player, Animation.Baselines, DeltaTime, JustFinished, Result);

	// The item's own pose - opacity, scale, move, turn, shear - is taken out of the result and drawn by the list, as an
	// Enter is. Written onto the item instead, it was at the mercy of anything else writing the item's render transform or
	// opacity (a Widget Animation Component on it, an Item Style, the designer syncing the preview).
	bool bHasPose = false;
	float Opacity = 1.f;
	FVector2D Scale(1.0, 1.0);
	FVector2D Translation = FVector2D::ZeroVector;
	FVector2D Shear = FVector2D::ZeroVector;
	float Angle = 0.f;
	for (int32 Index = Result.TargetedTransforms.Num() - 1; Index >= 0; --Index)
	{
		const FWidgetAnimTargetedTransform& Transform = Result.TargetedTransforms[Index];
		if ((!Transform.TargetWidget || Transform.TargetWidget == Item) && !Transform.Part.IsSet())
		{
			bHasPose = true;
			Opacity *= Transform.Opacity;
			Scale *= Transform.Scale;
			Translation += Transform.Translation;
			Shear += Transform.Shear;
			Angle += Transform.Angle;
			Result.TargetedTransforms.RemoveAt(Index);
		}
	}
	Result.RestingWidgets.RemoveAll([Item](const UWidget* Resting) { return !Resting || Resting == Item; });

	// Everything else - widgets inside the item, letters, materials, shapes - is applied as any player applies it.
	UWidgetAnimLibrary::ApplyComposedResult(Item, Result, Animation.Baselines);
	UWidgetAnimLibrary::ReleaseFinishedTracks(Animation.Player);

	if (const TSharedPtr<SWidget> Slate = Animation.Slate.Pin())
	{
		if (MyFlowList.IsValid())
		{
			if (bHasPose)
			{
				const FWidgetTransform Pose(Translation, Scale, Shear, Angle);
				MyFlowList->SetItemAnimationPose(Slate.ToSharedRef(), Pose.ToSlateRenderTransform(), Opacity, FVector2f(Item->GetRenderTransformPivot()));
			}
			else
			{
				MyFlowList->ClearItemAnimationPose(Slate.ToSharedRef());
			}
		}
	}
	if (FlowListAnimLog::Level() > 1)
	{
		UE_LOG(LogFlowList, Display, TEXT("%s: %s - opacity %.3f, scale %.3f x %.3f, move %.1f, %.1f, angle %.1f%s."), *GetName(), *Item->GetName(),
			Opacity, Scale.X, Scale.Y, Translation.X, Translation.Y, Angle, bPlaying ? TEXT("") : TEXT(" (done)"));
	}
	return bPlaying;
}

bool UFlowList::TickItemAnimations(float DeltaTime)
{
	bool bPlaying = false;
	for (int32 Index = ItemAnimations.Num() - 1; Index >= 0; --Index)
	{
		FFlowListItemAnimation& Animation = ItemAnimations[Index];
		if (!Animation.Widget.IsValid())
		{
			ItemAnimations.RemoveAtSwap(Index);
			continue;
		}
		const bool bItemPlaying = PoseItemAnimation(Animation, FMath::Min(DeltaTime, 0.1f));
		bPlaying |= bItemPlaying;

		// A preview in the designer leaves the items as they are authored once an entrance is over. An exit holds until the
		// entrance after it starts, so the item isn't seen whole in between.
		const bool bLeaving = Animation.Player.Tracks.ContainsByPredicate([](const FWidgetAnimTrack& Track) { return Track.bIsOut; });
		if (!bItemPlaying && !bLeaving && !Animation.bHoldPose && IsDesignTime())
		{
			ReleaseItemAnimation(Index, /*bRestore=*/true);
		}
	}

	// Finished entrances hold their last pose; nothing more to play until another starts.
	if (!bPlaying)
	{
		ItemAnimationTicker.Reset();
	}
	return bPlaying;
}

void UFlowList::ReleaseItemAnimation(int32 Index, bool bRestore)
{
	if (!ItemAnimations.IsValidIndex(Index))
	{
		return;
	}
	FFlowListItemAnimation& Animation = ItemAnimations[Index];
	if (bRestore)
	{
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(Animation.Baselines);
	}
	const TSharedPtr<SWidget> Slate = Animation.Slate.Pin();
	if (Slate.IsValid() && MyFlowList.IsValid())
	{
		MyFlowList->ClearItemAnimationPose(Slate.ToSharedRef());
	}
	ItemAnimations.RemoveAtSwap(Index);
}

void UFlowList::StopItemAnimation(const UWidget* Item, bool bRestore)
{
	const int32 Found = Item ? ItemAnimations.IndexOfByPredicate([Item](const FFlowListItemAnimation& Animation) { return Animation.Widget.Get() == Item; }) : INDEX_NONE;
	ReleaseItemAnimation(Found, bRestore);
}

void UFlowList::StopAllItemAnimations()
{
	if (ItemAnimationTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ItemAnimationTicker);
		ItemAnimationTicker.Reset();
	}
	for (int32 Index = ItemAnimations.Num() - 1; Index >= 0; --Index)
	{
		ReleaseItemAnimation(Index, ItemAnimations[Index].Widget.IsValid());
	}
	ItemAnimations.Reset();
}

void UFlowList::PlaySound(const FUISoundSlot& Sound, int32 RuntimeIndex)
{
	if (IsDesignTime() || !Sound.IsValid())
	{
		return;
	}
	if (SoundRuntimes.Num() < FlowListSounds::Count)
	{
		SoundRuntimes.SetNum(FlowListSounds::Count);
	}

	// The list paces its own ticks in real time; the shared limit counts game time, which stops in a paused menu.
	FUISoundSetSettings Settings;
	Settings.MinTimeBetweenPlays = 0.f;
	FUISoundPlayer::Play(this, SoundRuntimes[RuntimeIndex], Settings, Sound);
}

// ---------------------------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------------------------

#if WITH_EDITOR
const FText UFlowList::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}

void UFlowList::OnDescendantSelectedByDesigner(UWidget* DescendantWidget)
{
	Super::OnDescendantSelectedByDesigner(DescendantWidget);

	// Selecting an item in the hierarchy brings it to the focus in the preview.
	if (UWidget* Item = UWidget::FindChildContainingDescendant(this, DescendantWidget))
	{
		ScrollToItem(Item, true);
	}
}

void UFlowList::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UFlowList, StylePreset))
	{
		FlowListTypes::ApplyPreset(StylePreset, ListLayout, Effects, Scrolling, Transitions);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// A transition edited - an animation set, a duration changed - is played in the designer's preview straight away.
	const bool bTransitions = PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UFlowList, Transitions);
	if (bTransitions && (PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive) == 0)
	{
		PreviewInDesigner(/*bExits=*/false);
	}
}

TArray<UFlowList*> UFlowList::FindDesignerPreviews() const
{
	TArray<UFlowList*> Previews;
	if (IsDesignTime())
	{
		// Already the preview.
		Previews.Add(const_cast<UFlowList*>(this));
		return Previews;
	}

	// The template the Details edit sits in its Widget Blueprint; its preview is the designer's instance of that Blueprint's
	// class, holding a list of the same name.
	const UObject* Blueprint = nullptr;
#if WITH_EDITORONLY_DATA
	Blueprint = GetTypedOuter<UBlueprint>();
	if (!Blueprint)
	{
		if (const UClass* AuthoredClass = GetTypedOuter<UClass>())
		{
			Blueprint = AuthoredClass->ClassGeneratedBy;
		}
	}
#endif
	for (TObjectIterator<UFlowList> It(RF_ClassDefaultObject | RF_ArchetypeObject, /*bIncludeDerivedClasses=*/true, EInternalObjectFlags::Garbage); It; ++It)
	{
		UFlowList* Candidate = *It;
		if (Candidate == this || !Candidate->IsDesignTime() || Candidate->GetFName() != GetFName() || !Candidate->MyFlowList.IsValid())
		{
			continue;
		}
#if WITH_EDITORONLY_DATA
		const UUserWidget* Owner = Candidate->GetTypedOuter<UUserWidget>();
		if (Blueprint && (!Owner || Owner->GetClass()->ClassGeneratedBy != Blueprint))
		{
			continue;
		}
#endif
		Previews.Add(Candidate);
	}
	return Previews;
}

void UFlowList::PreviewInDesigner(bool bExits)
{
	// Next frame: the designer copies the edit onto its preview - or builds the preview again - after this returns.
	const TWeakObjectPtr<UFlowList> WeakThis = this;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, bExits](float)
	{
		if (const UFlowList* Template = WeakThis.Get())
		{
			const TArray<UFlowList*> Previews = Template->FindDesignerPreviews();
			if (FlowListAnimLog::Level() > 0)
			{
				UE_LOG(LogFlowList, Display, TEXT("%s: a transition was edited - %d designer preview(s) found to play it in."), *Template->GetName(), Previews.Num());
			}
			for (UFlowList* Preview : Previews)
			{
				Preview->PushSettings();
				bExits ? Preview->PreviewExits() : Preview->PreviewEntrances();
			}
		}
		return false;
	}));
}
#endif

#undef LOCTEXT_NAMESPACE
