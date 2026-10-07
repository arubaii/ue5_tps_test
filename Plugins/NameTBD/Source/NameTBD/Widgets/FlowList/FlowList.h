// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "UObject/Interface.h"

#include "FlowListTypes.h"
#include "NameTBD/Sound/UISoundTypes.h"
#include "SFlowList.h"

#include "FlowList.generated.h"

class UFlowList;
class UUserWidget;

// =====================================================================================================
// Item interface
// =====================================================================================================

UINTERFACE(BlueprintType, meta = (DisplayName = "Flow List Item"))
class NAMETBD_API UFlowListItem : public UInterface
{
	GENERATED_BODY()
};

/**
 * Optional, for a Flow List's items: implement it on an item's Widget Blueprint (Class Settings > Interfaces) and the list
 * tells the item about itself - so an item can light up when selected, or drive its own material from how far it is from
 * the focus (a blur, a desaturation, a parallax layer) for looks the list's own Effects don't cover.
 */
class NAMETBD_API IFlowListItem
{
	GENERATED_BODY()

public:
	/** The item became the selected one, or stopped being it. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Flow List")
	void OnFlowListSelected(UFlowList* List, bool bIsSelected);
	virtual void OnFlowListSelected_Implementation(UFlowList* List, bool bIsSelected) {}

	/**
	 * How far the item is from the focus changed, in items: 0 focused, 1 the next, -1 the one before, fractions while the
	 * list moves. Only while the list's Report Item Distance is on - it fires every frame the list moves.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Flow List")
	void OnFlowListDistanceChanged(UFlowList* List, float Distance);
	virtual void OnFlowListDistanceChanged_Implementation(UFlowList* List, float Distance) {}
};

/**
 * For the Entry Widget Class of a virtual Flow List (one given List Items): the list hands each entry the item it shows as
 * it scrolls into view, and tells it when it goes back to the pool - so one entry widget shows many items in turn. Set up
 * the entry's look from the item in On Flow List Item Set; anything started there (a timer, a binding) stops in On Flow
 * List Entry Released.
 */
UINTERFACE(BlueprintType, meta = (DisplayName = "Flow List Entry"))
class NAMETBD_API UFlowListEntry : public UInterface
{
	GENERATED_BODY()
};

class NAMETBD_API IFlowListEntry
{
	GENERATED_BODY()

public:
	/** The entry now shows ListItem, the list's Index-th item. In the designer's preview ListItem is null. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Flow List")
	void OnFlowListItemSet(UFlowList* List, UObject* ListItem, int32 Index);
	virtual void OnFlowListItemSet_Implementation(UFlowList* List, UObject* ListItem, int32 Index) {}

	/** The entry scrolled away and went back to the pool, until it is given another item. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Flow List")
	void OnFlowListEntryReleased(UFlowList* List);
	virtual void OnFlowListEntryReleased_Implementation(UFlowList* List) {}
};

/** An entry widget of a virtual Flow List, kept with its Slate widget so it is reused without being rebuilt. */
USTRUCT()
struct FFlowListEntryWidget
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UUserWidget> Widget = nullptr;

	TSharedPtr<SWidget> Slate;
};

// =====================================================================================================
// Slot
// =====================================================================================================

/** A Flow List's slot: the room around one item, and its size if it shouldn't be its own. */
UCLASS(meta = (DisplayName = "Flow List Slot"))
class NAMETBD_API UFlowListSlot : public UPanelSlot
{
	GENERATED_BODY()

public:
	/** Space around the item, inside its place on the list. Scales with it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Flow List Slot")
	FMargin Padding;

	/** This item's size, overriding the list's Item Size and the item's own. 0 on an axis leaves that axis alone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Flow List Slot")
	FVector2D SizeOverride = FVector2D::ZeroVector;

	/**
	 * A number that stands for the item - which character it is, which tab - read by a state machine input Driven By the
	 * list's Selected Value, and by Get Item Value. Unlike its index, it stays the item's when items are added, removed or
	 * moved. -1: the item's index.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Flow List Slot", meta = (DisplayName = "Item Value", ClampMin = "-1"))
	int32 ItemValue = -1;

	UFUNCTION(BlueprintCallable, Category = "Layout|Flow List Slot")
	void SetPadding(FMargin InPadding);

	UFUNCTION(BlueprintCallable, Category = "Layout|Flow List Slot")
	void SetSizeOverride(FVector2D InSizeOverride);

	/** Builds the Slate slot at Position among the list's children. bAnimate plays the item's Enter transition. */
	void BuildSlot(const TSharedRef<SFlowList>& InList, int32 Position, bool bAnimate);

	// UPanelSlot
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

private:
	friend class UFlowList;

	SFlowList::FSlot* Slot = nullptr;
	TWeakPtr<SFlowList> List;

};

// =====================================================================================================
// Widget
// =====================================================================================================

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnFlowListSelectionChanged, int32, Index, int32, PreviousIndex, UWidget*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFlowListItemEvent, int32, Index, UWidget*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFlowListScrolled, float, ScrollPosition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFlowListItemExited, UWidget*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFlowListEdgeReached);

/**
 * A list of any widgets, laid along a path and styled by how far each is from the focus: scroll lists, carousels, cover
 * flows, picker wheels, Persona-style dials and slanted menus, tapering push/pop lists, rings. See FlowListTypes.h for how
 * it thinks.
 *
 * Start from a Style Preset, then shape it:
 *   Layout       - the path (Line, Arc, Wheel, Ring, Spline), where the focus sits, how items are spaced.
 *   Effects      - what each item takes from its distance to the focus: scale, squash, opacity, turn, offset, tint.
 *   Scrolling    - snapping, looping, drag and fling physics, wheel, keys and gamepad.
 *   Transitions  - how items arrive, leave and make room. Add Item, Remove Item, Pop Front and Move Item all animate.
 *
 * Items are the list's children: drop any widgets into it in the designer, or add them at runtime. Hit testing, focus and
 * navigation follow what is drawn. Buttons inside the list can still be dragged by - set a button's Touch Method to
 * Precise Tap for touch screens, as in any scroll box.
 *
 * Indexes count the items shown (collapsed children aren't items). Insert Item's and Move Item's indexes count children,
 * which is the same unless some are collapsed.
 *
 * Around it:
 *   Virtual list   - give it an Entry Widget Class and Set List Items: only the entries in view exist, pooled and reused.
 *   Item looks     - a Flow List Item Style component on an item, or anything inside one, gives it a Selected look.
 *   State machines - a widget inside the selected item reads the built-in Selected; any state machine input or value
 *                    track can be Driven By a Flow List (Selected Index, Item Focus...) - see FlowListInput.h.
 *   Scroll bar     - a Flow List Scroll Bar beside it follows it, as a bar or page dots.
 */
UCLASS(meta = (DisplayName = "Flow List"))
class NAMETBD_API UFlowList : public UPanelWidget
{
	GENERATED_BODY()

public:
	UFlowList();

	/** Fills Layout, Effects, Scrolling and Transitions with a look. Everything stays editable afterwards. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow List")
	EFlowListStylePreset StylePreset = EFlowListStylePreset::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow List", meta = (DisplayName = "Layout"))
	FFlowListLayout ListLayout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow List")
	FFlowListEffects Effects;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow List")
	FFlowListScrolling Scrolling;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow List")
	FFlowListTransitions Transitions;

	/** Where the list starts, in items. In the designer it previews the list scrolled there. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow List", meta = (UIMin = "0", UIMax = "20"))
	float ScrollPosition = 0.f;

	/** Lets the list itself take keyboard and gamepad focus - for items that can't (images, text). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow List", meta = (DisplayName = "Is Focusable"))
	bool bListIsFocusable = false;

	/** Calls On Flow List Distance Changed on items implementing Flow List Item, every frame the list moves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow List", AdvancedDisplay, meta = (DisplayName = "Report Item Distance"))
	bool bReportItemDistance = false;

	/** Draws the path and the focus point over the list in the designer. */
	UPROPERTY(EditAnywhere, Category = "Flow List", AdvancedDisplay, meta = (DisplayName = "Show Path In Designer"))
	bool bShowPathInDesigner = true;

	// ---------------------------------------------------------------------------------------------
	// Virtual list
	// ---------------------------------------------------------------------------------------------

	/**
	 * Makes the list virtual: it shows List Items - any objects, set with Set List Items - through entries of this class,
	 * made only for the items near enough the focus to be drawn and reused as they scroll away, so a list of thousands
	 * costs what the dozen on screen do. Implement Flow List Entry on it to be told which item an entry shows. The list's
	 * own children aren't used. Items are even: the list's Item Size, or the first entry's size.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow List|Virtual List", meta = (DisplayName = "Entry Widget Class"))
	TSubclassOf<UUserWidget> EntryWidgetClass;

	/** How many entries the designer previews, showing no item. */
	UPROPERTY(EditAnywhere, Category = "Flow List|Virtual List", meta = (ClampMin = "0", UIMax = "40", EditCondition = "EntryWidgetClass != nullptr"))
	int32 NumDesignerPreviewEntries = 8;

	// ---------------------------------------------------------------------------------------------
	// Sounds
	// ---------------------------------------------------------------------------------------------

	/** Each time the selection moves on - a tick per item as the list passes them, like a dial's detents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow List|Sounds")
	FUISoundSlot SelectSound;

	/** When the list comes to rest after moving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow List|Sounds")
	FUISoundSlot SettleSound;

	/** When the list is pushed against an end - a fling hitting it, a wheel or key going no further. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow List|Sounds")
	FUISoundSlot EdgeSound;

	/** The shortest time between two Select Sounds, in real seconds, so a fast fling ticks rather than buzzes. Plays even while paused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow List|Sounds", meta = (ClampMin = "0", UIMin = "0", UIMax = "0.2", Units = "s"))
	float MinTimeBetweenTicks = 0.035f;

	// ---------------------------------------------------------------------------------------------
	// Events
	// ---------------------------------------------------------------------------------------------

	/** The selected item changed: by scrolling past it, navigation, a click, or Scroll To Index. Item is null when the list emptied. */
	UPROPERTY(BlueprintAssignable, Category = "Flow List|Events")
	FOnFlowListSelectionChanged OnSelectionChanged;

	/** The list came to rest. */
	UPROPERTY(BlueprintAssignable, Category = "Flow List|Events")
	FOnFlowListItemEvent OnSettled;

	/**
	 * An item was clicked or tapped without being dragged - whatever is inside it, so a list of plain images can be clicked
	 * too. Fires alongside a button item's own On Clicked.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Flow List|Events")
	FOnFlowListItemEvent OnItemClicked;

	/** Every frame the list scrolls, with where it is in items. For a scroll bar or page dots: see Get Scroll Fraction. */
	UPROPERTY(BlueprintAssignable, Category = "Flow List|Events")
	FOnFlowListScrolled OnScrolled;

	/** A removed item finished its Exit transition and was let go - the moment to return it to a pool. */
	UPROPERTY(BlueprintAssignable, Category = "Flow List|Events")
	FOnFlowListItemExited OnItemExited;

	/** The list was pushed against an end. */
	UPROPERTY(BlueprintAssignable, Category = "Flow List|Events")
	FOnFlowListEdgeReached OnEdgeReached;

	// ---------------------------------------------------------------------------------------------
	// Items
	// ---------------------------------------------------------------------------------------------

	/**
	 * Adds an item at the end - Push Back. bAnimate plays its Enter transition; items added in the same frame enter one
	 * after another, so filling a list in Construct plays as an intro. Pass false to fill it instantly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Items", meta = (AdvancedDisplay = "bAnimate"))
	UFlowListSlot* AddItem(UWidget* Content, bool bAnimate = true);

	/** Adds an item at the front, playing its Enter transition while the rest make room. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Items", meta = (AdvancedDisplay = "bAnimate"))
	UFlowListSlot* PushFront(UWidget* Content, bool bAnimate = true);

	/** Adds an item at Index among the children, playing its Enter transition while the rest make room. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Items", meta = (AdvancedDisplay = "bAnimate"))
	UFlowListSlot* InsertItem(UWidget* Content, int32 Index, bool bAnimate = true);

	/** Removes an item. bAnimate plays its Exit transition while the rest close the gap; otherwise it goes at once. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Items", meta = (AdvancedDisplay = "bAnimate"))
	bool RemoveItem(UWidget* Content, bool bAnimate = true);

	UFUNCTION(BlueprintCallable, Category = "Flow List|Items", meta = (AdvancedDisplay = "bAnimate"))
	bool RemoveItemAt(int32 Index, bool bAnimate = true);

	/** Removes the first item and returns it. With the focus on it, the next one slides up into its place. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Items", meta = (AdvancedDisplay = "bAnimate"))
	UWidget* PopFront(bool bAnimate = true);

	/** Removes the last item and returns it. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Items", meta = (AdvancedDisplay = "bAnimate"))
	UWidget* PopBack(bool bAnimate = true);

	/** Moves an item to Index among the children; the items between slide to their new places. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Items")
	void MoveItem(UWidget* Content, int32 Index);

	/** Removes every item. bAnimate lets them all play their Exit transitions. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Items", meta = (AdvancedDisplay = "bAnimate"))
	void ClearItems(bool bAnimate = true);

	/** The number of items shown. */
	UFUNCTION(BlueprintPure, Category = "Flow List|Items")
	int32 GetItemCount() const;

	UFUNCTION(BlueprintPure, Category = "Flow List|Items")
	UWidget* GetItemAt(int32 Index) const;

	/** -1 when the widget isn't an item of this list, or is collapsed. */
	UFUNCTION(BlueprintPure, Category = "Flow List|Items")
	int32 GetIndexOfItem(const UWidget* Item) const;

	/** How far an item is from the focus, in items: 0 focused, 1 the next, -1 the one before. Large when it isn't an item. */
	UFUNCTION(BlueprintPure, Category = "Flow List|Items")
	float GetItemDistance(const UWidget* Item) const;

	/** Plays Enter on every item, the focused one first and spreading out by Stagger. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Items")
	void PlayIntro();

	/** The number standing for an item: its slot's Item Value, or its index when that is -1. -1 for no item. */
	UFUNCTION(BlueprintPure, Category = "Flow List|Items")
	int32 GetItemValue(int32 Index) const;

	// ---------------------------------------------------------------------------------------------
	// Virtual list
	// ---------------------------------------------------------------------------------------------

	/** Whether the list shows List Items through an Entry Widget Class rather than its own children. */
	UFUNCTION(BlueprintPure, Category = "Flow List|Virtual List")
	bool IsVirtual() const { return EntryWidgetClass != nullptr; }

	/** Shows these objects, one entry each as they come into view. Every entry shown is handed its item again. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Virtual List")
	void SetListItems(const TArray<UObject*>& InListItems);

	UFUNCTION(BlueprintPure, Category = "Flow List|Virtual List")
	TArray<UObject*> GetListItems() const;

	/** Adds an object at the end; its entry enters if it is in view. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Virtual List", meta = (AdvancedDisplay = "bAnimate"))
	void AddListItem(UObject* ListItem, bool bAnimate = true);

	/** Adds an object at Index; the items after it slide along to make room. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Virtual List", meta = (AdvancedDisplay = "bAnimate"))
	void InsertListItem(UObject* ListItem, int32 Index, bool bAnimate = true);

	/** Removes an object; its entry leaves and the items after it slide into its place. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Virtual List", meta = (AdvancedDisplay = "bAnimate"))
	bool RemoveListItem(UObject* ListItem, bool bAnimate = true);

	UFUNCTION(BlueprintCallable, Category = "Flow List|Virtual List", meta = (AdvancedDisplay = "bAnimate"))
	bool RemoveListItemAt(int32 Index, bool bAnimate = true);

	UFUNCTION(BlueprintCallable, Category = "Flow List|Virtual List")
	void ClearListItems();

	/** Hands every entry shown its item again - after the items themselves changed. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Virtual List")
	void RefreshListItems();

	UFUNCTION(BlueprintPure, Category = "Flow List|Virtual List")
	UObject* GetListItemAt(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Flow List|Virtual List")
	int32 GetIndexOfListItem(const UObject* ListItem) const;

	UFUNCTION(BlueprintPure, Category = "Flow List|Virtual List")
	UObject* GetSelectedListItem() const;

	/** The entry showing an object, or null when it isn't in view. */
	UFUNCTION(BlueprintPure, Category = "Flow List|Virtual List")
	UUserWidget* GetEntryForListItem(const UObject* ListItem) const;

	UFUNCTION(BlueprintCallable, Category = "Flow List|Virtual List", meta = (AdvancedDisplay = "bAnimate"))
	void ScrollToListItem(UObject* ListItem, bool bAnimate = true);

	// ---------------------------------------------------------------------------------------------
	// Finding lists
	// ---------------------------------------------------------------------------------------------

	/**
	 * The Flow List that Widget is an item of - the item itself, anything inside it, or an entry of a virtual list - with the
	 * item and its index. Null when Widget isn't in a Flow List's item.
	 */
	UFUNCTION(BlueprintPure, Category = "Flow List", meta = (DefaultToSelf = "Widget"))
	static UFlowList* FindFlowListOfItem(const UWidget* Widget, UWidget*& OutItem, int32& OutIndex);

	/**
	 * The Flow List nearest Widget: the one it is an item of, itself, the first inside it, else the first found widening out
	 * through the widgets around it - its own Widget Blueprint and then the ones it sits in. ListName picks one by its name
	 * in the Widget Blueprint instead.
	 */
	UFUNCTION(BlueprintPure, Category = "Flow List", meta = (DefaultToSelf = "Widget"))
	static UFlowList* FindNearestFlowList(const UWidget* Widget, FName ListName = NAME_None);

	/**
	 * Whether Widget is a Flow List's selected item, or inside it. What every state machine's built-in Selected reads, so a
	 * button inside a Flow List's item is Selected while the item is, with nothing to set up.
	 */
	UFUNCTION(BlueprintPure, Category = "Flow List", meta = (DefaultToSelf = "Widget"))
	static bool IsInSelectedItem(const UWidget* Widget);

	/** Every frame the list moves, and whenever its items or selection change. For widgets that follow a list - see UFlowListScrollBar. */
	FSimpleMulticastDelegate OnListUpdated;

	// ---------------------------------------------------------------------------------------------
	// Scrolling
	// ---------------------------------------------------------------------------------------------

	/** Sends the list to an item and selects it - the short way round a looping list. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Scrolling", meta = (AdvancedDisplay = "bAnimate"))
	void ScrollToIndex(int32 Index, bool bAnimate = true);

	UFUNCTION(BlueprintCallable, Category = "Flow List|Scrolling", meta = (AdvancedDisplay = "bAnimate"))
	void ScrollToItem(UWidget* Item, bool bAnimate = true);

	/** Moves the selection by Steps items - Select Next is 1, Select Previous -1. False when it is already at that end. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Scrolling", meta = (AdvancedDisplay = "bWrap"))
	bool ScrollBy(int32 Steps, bool bWrap = false);

	/** Springs to the nearest item when the list snaps, or back inside its ends - after moving it to somewhere between items. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Scrolling")
	void SnapToNearestItem();

	/** Scrolls to a position in items: 2.5 is halfway between the third and fourth. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Scrolling", meta = (AdvancedDisplay = "bAnimate"))
	void SetScrollPosition(float Position, bool bAnimate = true);

	/** Where the list is, in items. */
	UFUNCTION(BlueprintPure, Category = "Flow List|Scrolling")
	float GetScrollPosition() const;

	/** 0 at the first item, 1 at the last - a scroll bar's thumb position. Around a looping list, how far round it is. */
	UFUNCTION(BlueprintPure, Category = "Flow List|Scrolling")
	float GetScrollFraction() const;

	UFUNCTION(BlueprintCallable, Category = "Flow List|Scrolling", meta = (AdvancedDisplay = "bAnimate"))
	void SetScrollFraction(float Fraction, bool bAnimate = false);

	/** How much of the list fits on screen at once, as a fraction of it - a scroll bar thumb's length. */
	UFUNCTION(BlueprintPure, Category = "Flow List|Scrolling")
	float GetViewFraction() const;

	UFUNCTION(BlueprintPure, Category = "Flow List|Scrolling")
	int32 GetSelectedIndex() const;

	UFUNCTION(BlueprintPure, Category = "Flow List|Scrolling")
	UWidget* GetSelectedItem() const;

	/** Whether the list is moving: dragged, coasting or springing. */
	UFUNCTION(BlueprintPure, Category = "Flow List|Scrolling")
	bool IsScrolling() const;

	// ---------------------------------------------------------------------------------------------
	// Settings
	// ---------------------------------------------------------------------------------------------

	/** Fills Layout, Effects, Scrolling and Transitions with a preset's look. */
	UFUNCTION(BlueprintCallable, Category = "Flow List|Settings")
	void ApplyStylePreset(EFlowListStylePreset Preset);

	UFUNCTION(BlueprintCallable, Category = "Flow List|Settings")
	void SetListLayout(const FFlowListLayout& InLayout);

	UFUNCTION(BlueprintCallable, Category = "Flow List|Settings")
	void SetEffects(const FFlowListEffects& InEffects);

	UFUNCTION(BlueprintCallable, Category = "Flow List|Settings")
	void SetScrolling(const FFlowListScrolling& InScrolling);

	UFUNCTION(BlueprintCallable, Category = "Flow List|Settings")
	void SetTransitions(const FFlowListTransitions& InTransitions);

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void OnDescendantSelectedByDesigner(UWidget* DescendantWidget) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	// UPanelWidget
	virtual UClass* GetSlotClass() const override;
	virtual void OnSlotAdded(UPanelSlot* InSlot) override;
	virtual void OnSlotRemoved(UPanelSlot* InSlot) override;

	// UWidget
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void PushSettings();
	UFlowListSlot* AddItemAt(UWidget* Content, int32 Index, bool bAnimate);
	bool RemoveItemInternal(UWidget* Content, bool bAnimate);
	UWidget* FindItemWidget(const TSharedPtr<SWidget>& Widget) const;

	void HandleSelectionChanged(int32 Index, int32 PreviousIndex);
	void HandleSettled(int32 Index);
	void HandleItemClicked(int32 Index);
	void HandleItemExited(const TSharedRef<SWidget>& Widget);
	void HandleEdgeReached();
	void HandleAnimated();

	void PlaySound(const FUISoundSlot& Sound, int32 RuntimeIndex);

	// Virtual list
	TSharedPtr<SWidget> HandleRealizeEntry(int32 Index);
	void HandleReleaseEntry(int32 Index, const TSharedRef<SWidget>& Widget);
	void SyncVirtualItems(bool bRefresh);
	void ReleaseEntries();
	UUserWidget* FindEntry(const TSharedPtr<SWidget>& Widget) const;

	TSharedPtr<SFlowList> MyFlowList;

	/** A virtual list's items. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> ListItems;

	/** Entries showing items, and entries waiting to show one. */
	UPROPERTY(Transient)
	TArray<FFlowListEntryWidget> ActiveEntries;

	UPROPERTY(Transient)
	TArray<FFlowListEntryWidget> EntryPool;

	/** The distance last reported to each item's IFlowListItem, by Slate widget, so an item only hears about real changes. */
	TMap<const SWidget*, float> ReportedDistances;

	/** Where the next slot added goes among the children, and whether it animates; set around AddChild. */
	int32 PendingInsertIndex = INDEX_NONE;
	bool bAnimateNextChange = true;

	/**
	 * Widgets playing their Exit transition, and the Slate widgets they are drawn by. Held strongly until they finish: an
	 * Image or Border hands Slate a pointer to its own brush, so one collected mid-exit would leave its Slate widget
	 * drawing freed memory.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> LeavingItems;
	TArray<const SWidget*> LeavingSlateWidgets;

	/** Set while the Slate list is being built, so the selection it starts on doesn't sound. */
	bool bBuilding = false;

	TWeakObjectPtr<UWidget> SelectedItem;
	float LastReportedScroll = TNumericLimits<float>::Max();
	double LastTickTime = -1.0e9;

	TArray<FUISoundRuntime> SoundRuntimes;
};
