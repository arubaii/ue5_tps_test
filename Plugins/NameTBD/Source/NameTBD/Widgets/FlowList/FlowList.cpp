// Copyright AR GameSystems 2026. All rights reserved.

#include "FlowList.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "HAL/PlatformTime.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "FlowList"

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
	Slot = InList->InsertChild(Content ? Content->TakeWidget() : SNullWidget::NullWidget, Position, bAnimate);
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
	if (!IsDesignTime() && Transitions.bPlayIntro)
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
	}

	if (MyFlowList.IsValid())
	{
		const int32 Position = PendingInsertIndex != INDEX_NONE ? PendingInsertIndex : Slots.Num() - 1;
		CastChecked<UFlowListSlot>(InSlot)->BuildSlot(MyFlowList.ToSharedRef(), Position, bAnimateNextChange && !IsDesignTime());
	}
}

void UFlowList::OnSlotRemoved(UPanelSlot* InSlot)
{
	if (!MyFlowList.IsValid() || !InSlot->Content || IsVirtual())
	{
		return;
	}
	const TSharedPtr<SWidget> Widget = InSlot->Content->GetCachedWidget();
	if (!Widget.IsValid())
	{
		return;
	}
	if (MyFlowList->RemoveChild(Widget.ToSharedRef(), bAnimateNextChange && !IsDesignTime()))
	{
		LeavingItems.Add(InSlot->Content);
		LeavingSlateWidgets.Add(Widget.Get());
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
		if (const TSharedPtr<SWidget> Widget = Content->GetCachedWidget())
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

UWidget* UFlowList::FindItemWidget(const TSharedPtr<SWidget>& Widget) const
{
	if (!Widget.IsValid())
	{
		return nullptr;
	}
	for (const UPanelSlot* PanelSlot : Slots)
	{
		if (PanelSlot && PanelSlot->Content && PanelSlot->Content->GetCachedWidget() == Widget)
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
		const TSharedPtr<SWidget> Widget = Item->GetCachedWidget();
		return Widget.IsValid() && MyFlowList.IsValid() && FindEntry(Widget) ? MyFlowList->GetItemIndex(Widget.ToSharedRef()) : INDEX_NONE;
	}
	if (!Item || Item->GetParent() != this)
	{
		return INDEX_NONE;
	}
	if (MyFlowList.IsValid())
	{
		const TSharedPtr<SWidget> Widget = Item->GetCachedWidget();
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
					const TSharedPtr<SWidget> Slate = AsUserWidget->GetCachedWidget();
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
			if (const TSharedPtr<SWidget> Widget = PanelSlot->Content->GetCachedWidget())
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
}
#endif

#undef LOCTEXT_NAMESPACE
