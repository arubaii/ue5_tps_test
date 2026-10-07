// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Children.h"
#include "Layout/Margin.h"
#include "SlotBase.h"
#include "Widgets/SPanel.h"

#include "FlowListTypes.h"

class FFlowListStickInput;

/**
 * The Slate side of UFlowList: lays its items along a path by their distance from a scroll position measured in items,
 * styles each from that distance, and moves the scroll position with springs, drags and flings. See FlowListTypes.h for
 * the ideas, and UFlowList for the Blueprint face.
 *
 * Items are placed with layout transforms (uniform scale - text re-lays out crisp at the smaller size) plus a render
 * transform for turning and squashing, composed AFTER any render transform an item has of its own, so a Widget
 * Animation playing on an item still plays. Opacity and tint are given to each item through its widget style as it is
 * painted. Hit testing follows what is drawn, turns and all.
 *
 * Children that are leaving stay in the panel until their exit transition ends, drawn but not hit-testable, and are
 * not items: nothing indexes them. Collapsed children are not items either.
 */
/** What starts an item's own entrance or exit Widget Animation - see SFlowList::OnItemTransitionStarted. */
enum class EFlowListItemCue : uint8
{
	/** It was added, removed or shown, or the intro or a preview plays: its Enter or Exit. */
	Transition,

	/** It scrolled into view, or out of it (Play As Items Scroll In And Out). */
	View,

	/** It became the selected item, or stopped being it (Play On Selection). */
	Selection
};

class NAMETBD_API SFlowList : public SPanel
{
public:
	class NAMETBD_API FSlot : public TSlotBase<FSlot>
	{
	public:
		SLATE_SLOT_BEGIN_ARGS(FSlot, TSlotBase<FSlot>)
		SLATE_SLOT_END_ARGS()

		FSlot() = default;

		/** Whoever sets these invalidates the list's layout - see UFlowListSlot. */
		void SetPadding(const FMargin& InPadding);
		const FMargin& GetPadding() const { return Padding; }

		/** 0 on an axis leaves that axis to the list's Item Size, or the item's desired size. */
		void SetSizeOverride(const FVector2D& InSize);
		const FVector2D& GetSizeOverride() const { return SizeOverride; }

	private:
		friend SFlowList;

		FMargin Padding;
		FVector2D SizeOverride = FVector2D::ZeroVector;

		/** Its place among the list's items. INDEX_NONE while it leaves or is collapsed. */
		int32 ItemIndex = INDEX_NONE;

		/** Where it is drawn, in items: springs to ItemIndex, which is how items slide aside for each other. */
		float Place = 0.f;
		float PlaceVelocity = 0.f;
		bool bPlaced = false;

		bool bCollapsed = false;
		bool bExiting = false;
		bool bEntering = false;
		float TransitionTime = 0.f;
		float TransitionDelay = 0.f;

		/** Whether its Enter or Exit has got past its delay - when an item's own Widget Animations are told to start. */
		bool bTransitionStarted = false;

		/** Playing its Exit for a preview (PlayExitPreview): still an item, and it enters again once the exit is done. */
		bool bPreviewLeaving = false;

		/**
		 * Without Use Basic Animations: the pose the item's own Widget Animations give it this frame - drawn by the list, about
		 * AnimPivot (a fraction of its size), on top of the item's own render transform. See SetItemAnimationPose.
		 */
		/** Whether it was last seen in view, for Play As Items Scroll In And Out; not known until its first layout. */
		bool bViewKnown = false;
		bool bInView = false;

		bool bHasAnimPose = false;
		FSlateRenderTransform AnimTransform;
		float AnimOpacity = 1.f;
		FVector2f AnimPivot = FVector2f(0.5f, 0.5f);

		/** The visibility it had before it started leaving, put back if it is added again mid-exit. */
		TOptional<EVisibility> SavedVisibility;
	};

	DECLARE_DELEGATE_TwoParams(FOnSelectionChanged, int32 /*Index*/, int32 /*PreviousIndex*/);
	DECLARE_DELEGATE_OneParam(FOnIndexEvent, int32 /*Index*/);
	DECLARE_DELEGATE_OneParam(FOnWidgetEvent, const TSharedRef<SWidget>& /*Widget*/);
	DECLARE_DELEGATE_ThreeParams(FOnItemTransition, const TSharedRef<SWidget>& /*Widget*/, bool /*bEnter*/, EFlowListItemCue /*Cue*/);
	DECLARE_DELEGATE(FOnListEvent);

	/** Virtual lists: the entry widget to show item Index, or null to show nothing there. */
	DECLARE_DELEGATE_RetVal_OneParam(TSharedPtr<SWidget>, FOnRealizeEntry, int32 /*Index*/);

	/** Virtual lists: an entry widget no longer shown - back to the pool. Index is the item it last showed, or INDEX_NONE once it has finished leaving. */
	DECLARE_DELEGATE_TwoParams(FOnReleaseEntry, int32 /*Index*/, const TSharedRef<SWidget>& /*Widget*/);

	SLATE_BEGIN_ARGS(SFlowList) {}
	SLATE_END_ARGS()

	SFlowList();
	virtual ~SFlowList() override;

	void Construct(const FArguments& InArgs);

	// ---------------------------------------------------------------------------------------------
	// Settings
	// ---------------------------------------------------------------------------------------------

	void SetSettings(const FFlowListLayout& InLayout, const FFlowListEffects& InEffects, const FFlowListScrolling& InScrolling, const FFlowListTransitions& InTransitions);
	void SetDesignTime(bool bInDesignTime, bool bInShowPath);
	void SetFocusable(bool bInFocusable) { bFocusable = bInFocusable; }

	/**
	 * Without Use Basic Animations: how long the items' own entrance and exit Widget Animations run, in seconds. An item
	 * leaving stays drawn that long; nothing of Enter and Exit is drawn - the animations are. See OnItemTransitionStarted.
	 */
	void SetItemAnimationLengths(float InEnterSeconds, float InExitSeconds);

	/**
	 * The pose an item's entrance or exit Widget Animations give it, drawn by the list like an Enter or Exit is - so nothing
	 * else writing the item's own render transform or opacity (its Widget Animation Component, an Item Style, the designer)
	 * can undo it. Pivot is a fraction of the item's size. Leaving items included.
	 */
	void SetItemAnimationPose(const TSharedRef<SWidget>& Widget, const FSlateRenderTransform& Transform, float Opacity, const FVector2f& Pivot);
	void ClearItemAnimationPose(const TSharedRef<SWidget>& Widget);

	/** Whether the item drawn with Widget is entering or leaving, for logging. False when it is neither or isn't a child. */
	bool GetItemTransition(const TSharedRef<SWidget>& Widget, bool& bOutEntering, bool& bOutExiting) const;

	// ---------------------------------------------------------------------------------------------
	// Items. Positions here count every child that isn't leaving - collapsed ones included - which is the
	// UMG panel's child order. Indexes everywhere else count only the items shown.
	// ---------------------------------------------------------------------------------------------

	/** Adds a child at Position among the children. bAnimate plays its Enter transition and slides the others aside. */
	FSlot* InsertChild(const TSharedRef<SWidget>& Widget, int32 Position, bool bAnimate);

	/**
	 * Removes a child. bAnimate keeps it drawn through its Exit transition while the rest close the gap. True when it is
	 * leaving that way - On Item Exited follows - and false when it went at once or wasn't a child.
	 */
	bool RemoveChild(const TSharedRef<SWidget>& Widget, bool bAnimate);

	/** Moves a child to Position among the children; the items between slide to their new places. */
	void MoveChild(const TSharedRef<SWidget>& Widget, int32 Position);

	void ClearChildren(bool bAnimate);

	/** Lets go of a child that is leaving, at once and without On Item Exited. True if Widget was one. */
	bool DropLeaving(const SWidget* Widget);

	/** Lets go of every child that is leaving, at once and without On Item Exited. */
	void DropAllLeaving();

	/** The items shown, in order. */
	int32 GetItemCount() const { return LiveOrder.Num(); }
	TSharedPtr<SWidget> GetItemWidget(int32 Index) const;
	int32 GetItemIndex(const TSharedRef<SWidget>& Widget) const;

	/** How far an item is from the focus, in items: 0 focused, negative before it. Large when it isn't an item. */
	float GetItemDistance(int32 Index) const;

	/** Calls Visitor with every item's index, widget and distance from the focus. */
	void ForEachItem(TFunctionRef<void(int32 Index, const TSharedRef<SWidget>& Widget, float Distance)> Visitor) const;

	/** Plays Enter on every item, the focused one first and spreading outward by Stagger. */
	void PlayIntro();

	/**
	 * Plays Exit on every item without removing any - the focused one first, spreading out by the Intro's Stagger - and then
	 * Enter again. For previewing the exit in the designer.
	 */
	void PlayExitPreview();

	/** One item's entrance - or its exit, then its entrance again - for previewing it in the designer. */
	void PlayItemPreview(const TSharedRef<SWidget>& Widget, bool bExit);

	// ---------------------------------------------------------------------------------------------
	// Virtual lists. Instead of a child per item, the list holds Count items by index and asks for an entry widget only
	// for those near enough the focus to be drawn, handing each back once it scrolls away - so a list of ten thousand
	// costs what the dozen on screen cost. Every index above is then an item index; children aren't added by hand.
	// ---------------------------------------------------------------------------------------------

	/** Makes the list virtual, with Count items. bRefresh hands back every entry shown and asks for them again - the items changed. */
	void SetVirtualItems(int32 Count, bool bRefresh);

	/** An item was added at Index: the items after it slide down to make room, and it enters (bAnimate) if it is shown. */
	void NotifyVirtualInserted(int32 Index, bool bAnimate);

	/** The item at Index was removed: its entry leaves (bAnimate) and the items after it slide up into its place. */
	void NotifyVirtualRemoved(int32 Index, bool bAnimate);

	bool IsVirtual() const { return bVirtual; }

	FOnRealizeEntry OnRealizeEntry;
	FOnReleaseEntry OnReleaseEntry;

	// ---------------------------------------------------------------------------------------------
	// Scrolling
	// ---------------------------------------------------------------------------------------------

	float GetScrollPosition() const { return Scroll; }
	int32 GetSelectedIndex() const { return SelectedIndex; }
	bool IsScrolling() const { return Motion != EMotion::Idle; }

	/** Sends the list to an item: the short way round a looping list. Selects it at once. */
	void ScrollToIndex(int32 Index, bool bAnimate);

	/** Moves the selection Steps items, as navigation would. False when it is already at that end, reported unless bQuiet. */
	bool ScrollBy(int32 Steps, bool bWrap, bool bQuiet = false);

	void SetScrollPosition(float Position, bool bAnimate);

	/** A slot's padding or size changed: laid out again. */
	void NotifySlotChanged() { ++LayoutVersion; Invalidate(EInvalidateWidgetReason::Layout); }

	/** Comes to rest on the nearest item when snapping, or back inside the ends - after a scroll bar let go mid-item. */
	void Settle() { SettleInPlace(); WakeUp(); }

	/** 0 at the first item, 1 at the last - for a scroll bar or progress readout. Around a looping list, how far round it is. */
	float GetScrollFraction() const;
	void SetScrollFraction(float Fraction, bool bAnimate);

	/** How much of the list fits on screen at once, as a fraction of it: a scroll bar thumb's length. */
	float GetViewFraction() const;

	// ---------------------------------------------------------------------------------------------
	// Events
	// ---------------------------------------------------------------------------------------------

	FOnSelectionChanged OnSelectionChanged;
	FOnIndexEvent OnSettled;
	FOnIndexEvent OnItemClicked;
	FOnWidgetEvent OnItemExited;
	FOnListEvent OnEdgeReached;

	/** Every frame the list moves or animates, after it has moved. */
	FOnListEvent OnAnimated;

	/**
	 * Without Use Basic Animations: an item's entrance (bEnter) or exit begins - after its stagger, or as it scrolls into or
	 * out of view, or gains or loses the selection - and its own Widget Animations are to start. Until a Transition entrance
	 * begins an item isn't drawn.
	 */
	FOnItemTransition OnItemTransitionStarted;

	// ---------------------------------------------------------------------------------------------
	// SWidget
	// ---------------------------------------------------------------------------------------------

	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual FChildren* GetChildren() override { return &Children; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return bFocusable; }

	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void OnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent) override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	friend class FFlowListStickInput;

	enum class EMotion : uint8
	{
		Idle,
		Drag,
		Coast,
		Spring,

		/** Driven by the stick, at Velocity - see Stick Scrolls. */
		Stick
	};

	/** The path's frame in the list's local space this frame. */
	struct FPathFrame
	{
		FVector2f Size = FVector2f::ZeroVector;
		FVector2f Focus = FVector2f::ZeroVector;
		FVector2f Dir = FVector2f(1.f, 0.f);
		FVector2f Cross = FVector2f(0.f, 1.f);

		/** +1 when Cross is Dir turned clockwise, -1 when anticlockwise: which way a positive arc turns the path. */
		float Turn = 1.f;

		/** The orientation's own axis, before Angle: what Align To Path measures an item's turn from. */
		float BaseAngle = 0.f;
	};

	/** A point on the path, and what the path does to an item there. */
	struct FPathPoint
	{
		FVector2f Position = FVector2f::ZeroVector;
		FVector2f Tangent = FVector2f(1.f, 0.f);
		FVector2f Normal = FVector2f(0.f, 1.f);
		float Depth = 0.f;
		float Squash = 1.f;
		float Opacity = 1.f;
		bool bHidden = false;
	};

	/** The length from each item's pivot to the next one's, at rest. */
	struct FPitchTable
	{
		TArray<float, TInlineAllocator<64>> Extents;
		TArray<float, TInlineAllocator<64>> Segments;
		bool bLoop = false;
		float Total = 0.f;

		/** The pitch with fewer than two items to measure between - and every pitch of a virtual list, whose items are even. */
		float Fallback = 100.f;

		/** A virtual list's items' length, all alike. */
		float UniformExtent = 0.f;
		bool bUniform = false;

		float At(int64 Segment) const;
		float ExtentAt(int32 Index) const { return bUniform ? UniformExtent : (Extents.IsValidIndex(Index) ? Extents[Index] : UniformExtent); }
	};

	/** One child as it will be drawn. */
	struct FItemLayout
	{
		int32 Child = INDEX_NONE;
		FGeometry Geometry;
		FLinearColor Color = FLinearColor::White;
		float SortKey = 0.f;
	};

	struct FPress
	{
		bool bActive = false;
		bool bDragging = false;
		bool bTouch = false;
		bool bCaught = false;
		bool bClickCancelled = false;
		uint32 PointerIndex = 0;
		uint32 UserIndex = 0;
		FVector2f StartAbsolute = FVector2f::ZeroVector;
		FVector2f LastAbsolute = FVector2f::ZeroVector;
		float RawScroll = 0.f;
		float StartScroll = 0.f;
		/** The item pressed. Only compared, never followed, until FindChild says it is still there - and its widget, in case a slot is made again at the same address. */
		const FSlot* PressedSlot = nullptr;
		TWeakPtr<SWidget> PressedWidget;
	};

	struct FFocusAnchor
	{
		const FSlot* Focused = nullptr;
		int32 FocusedIndex = INDEX_NONE;
		const FSlot* Selected = nullptr;
	};

	// Structure
	void RebuildLiveOrder();
	void PlaceNewItems();
	FFocusAnchor CaptureAnchor() const;
	void RestoreAnchor(const FFocusAnchor& Anchor);
	int32 FindChild(const FSlot* Slot) const;
	int32 FindChild(const TSharedRef<SWidget>& Widget, bool bExiting) const;
	int32 ChildPositionToIndex(int32 Position) const;
	void StartEnter(FSlot& Slot, float ExtraDelay);
	void ShiftFrame(float Delta);
	void RestoreVisibility(FSlot& Slot);

	// Scroll model
	bool IsLooping() const;
	float GetMinScroll() const { return 0.f; }
	float GetMaxScroll() const;
	float WrapDistance(float Distance) const;
	int32 IndexAt(float Position) const;
	float ApplyRubberBand(float Raw) const;
	void SpringTo(float InTarget, float InVelocity, bool bInSelectionFollows);
	void StopMotion();
	void SettleInPlace();
	void ReleaseDrag(float StartScroll);
	bool StepBy(float Steps, bool bWrap);
	bool StepScroll(float DeltaTime);
	bool StepItems(float DeltaTime);
	void UpdateNearest();
	void SetSelected(int32 Index, bool bMoveFocus, bool bForce = false);
	void MoveUserFocusTo(int32 Index);
	void ReachedEdge();

	// Stick Scrolls. The stick is read before Slate turns it into navigation, by an input preprocessor kept registered
	// while the setting is on outside the designer.
	void UpdateStickInput();
	bool HandleStickAnalog(const FAnalogInputEvent& Event);
	bool HandleStickKey(const FKeyEvent& Event);
	bool HasStickFocus(int32 UserIndex) const;
	FKey GetStickAxisKey() const;
	void UpdateStick(float DeltaTime);
	void ReleaseStick();

	// Pointer
	void BeginPress(const FPointerEvent& Event);
	bool HandlePointerMove(const FVector2f& Absolute);
	void EndPress(const FVector2f& Absolute);
	void PollPointer();
	void AddVelocitySample();
	float EstimateVelocity() const;
	const FSlot* SlotUnderPoint(const FVector2f& Absolute) const;
	bool MatchesPress(const FPointerEvent& Event) const;

	// Layout
	FPathFrame MakeFrame(const FVector2f& Size) const;
	FPathPoint SamplePath(const FPathFrame& Frame, float PathDistance, const FPitchTable& Pitch) const;
	void BuildPitch(FPitchTable& Out) const;
	FVector2f GetItemSize(const FSlot& Slot) const;
	float SpacingFactor(float Distance) const;
	float IntegratePath(float From, float To, const FPitchTable& Pitch) const;
	void ComputeLayout(const FGeometry& AllottedGeometry, TArray<FItemLayout>& Out) const;

	/** The layout for AllottedGeometry, worked out once a frame however many times arranging, painting and hit testing ask. */
	const TArray<FItemLayout>& GetLayout(const FGeometry& AllottedGeometry) const;

	/** Hover Magnify's scale at Distance items from the focus: 1 away from the pointer. */
	float MagnifyAt(float Distance) const;

	// Virtual lists
	void UpdateRealized();
	bool RealizeIndex(int32 Index);
	void ReleaseChild(int32 ChildIndex);
	void ReleaseAllEntries();

	/** Tells of the entries released since the last time - once the panel is consistent again, since whoever hears may change it. */
	void FlushReleases();

	/** Round a loop, brings the scroll back to within the first lap - before the count changes, which changes what a lap is. */
	void NormalizeLoop();
	void FinishVirtualChange(int32 OldSelected, int32 NewSelected, bool bSelectionLeft);

	/** The item's slot when it has one - every item of a plain list, the shown ones of a virtual list. */
	const FSlot* FindItemSlot(int32 Index) const { return LiveOrder.IsValidIndex(Index) && LiveOrder[Index] != INDEX_NONE ? &Children[LiveOrder[Index]] : nullptr; }
	float ComputeContentEndScroll(const FPathFrame& Frame, const FPitchTable& Pitch) const;
	void BuildSpline(const FVector2f& Size) const;
	FPathPoint SampleSpline(float Along) const;
	FFlowListTransitionState GetTransition(const FSlot& Slot) const;

	/** Enter's and Exit's delay and length - Basic Animations' own, or the items' Widget Animations' lengths without them. */
	bool UsesItemAnimations() const { return !Transitions.bUseBasicAnimations; }
	float GetEnterDelay() const { return UsesItemAnimations() ? 0.f : Transitions.Enter.Delay; }
	float GetEnterSeconds() const { return UsesItemAnimations() ? ItemEnterSeconds : Transitions.Enter.Duration; }
	float GetExitDelay() const { return UsesItemAnimations() ? 0.f : Transitions.Exit.Delay; }
	float GetExitSeconds() const { return UsesItemAnimations() ? ItemExitSeconds : Transitions.Exit.Duration; }

	/** Seconds between items entering together - added in one frame - which enter one after another with Play Intro on. */
	float GetBatchStagger() const { return Transitions.Intro.bPlayIntro ? Transitions.Intro.Stagger : 0.f; }

	/** Starts a slot's Exit: drawn, not hit-testable, no longer an item. */
	void StartExit(FSlot& Slot);

	/** Play As Items Scroll In And Out: an entrance for each item that came into view since last time, an exit for each that left. */
	void UpdateViewCues();

	/** Tells of the view and selection cues gathered since the last time - once the list is consistent, as it is each frame. */
	void FlushCues();
	int32 PaintPathGuide(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	void WakeUp();
	EActiveTimerReturnType OnAnimate(double InCurrentTime, float InDeltaTime);

	TPanelChildren<FSlot> Children;

	FFlowListLayout Layout;
	FFlowListEffects Effects;
	FFlowListScrolling Scrolling;
	FFlowListTransitions Transitions;

	bool bDesignTime = false;
	bool bShowPath = false;
	bool bFocusable = false;

	/** See SetItemAnimationLengths. */
	float ItemEnterSeconds = 0.f;
	float ItemExitSeconds = 0.f;

	/** View and selection cues not yet told of: the item, and whether it enters. */
	TArray<TTuple<TWeakPtr<SWidget>, bool, EFlowListItemCue>> PendingCues;

	/** Whether the view has been looked at once: what is in view then is just there, and plays nothing. */
	bool bViewTracked = false;

	/** Per child, from the last layout: whether its box overlaps the list. Worked out whatever it is drawn at, faded or not. */
	mutable TArray<bool> CachedInView;

	/**
	 * Child indices of the items, in item order. In a virtual list it has an entry per item, INDEX_NONE for the ones with no
	 * entry widget shown.
	 */
	TArray<int32> LiveOrder;

	bool bVirtual = false;
	int32 VirtualCount = 0;

	/** Entries released and not yet told of - see FlushReleases. */
	TArray<TPair<int32, TSharedRef<SWidget>>> PendingReleases;

	/** A virtual list's items' length along the list, measured once off an entry and kept until refreshed. */
	mutable float VirtualExtent = 0.f;

	// What OnAnimated last told of, so it is only told of a frame where something moved.
	float LastAnimatedScroll = TNumericLimits<float>::Max();
	float LastAnimatedMagnify = -1.f;

	// Hover Magnify: how far it has swelled (0 - 1), where the pointer was last seen, and - for the layout being worked out -
	// where along the list the pointer is, in items from the focus, and how strongly it counts.
	float MagnifyAmount = 0.f;
	FVector2f HoverAbsolute = FVector2f::ZeroVector;
	mutable float MagnifyCenter = 0.f;
	mutable float MagnifyWeight = 0.f;

	// The layout last worked out, and what it was worked out for.
	mutable TArray<FItemLayout> CachedLayout;
	mutable uint64 CachedLayoutFrame = MAX_uint64;
	mutable uint32 CachedLayoutVersion = 0;
	mutable float CachedLayoutScroll = 0.f;
	mutable FVector2f CachedLayoutSize = FVector2f::ZeroVector;
	mutable FSlateRenderTransform CachedLayoutTransform;
	uint32 LayoutVersion = 1;

	// The scroll, in items.
	EMotion Motion = EMotion::Idle;
	float Scroll = 0.f;
	float Velocity = 0.f;
	float Target = 0.f;
	float CoastOrigin = 0.f;
	float CoastVelocity = 0.f;
	float CoastTime = 0.f;
	float WheelRemainder = 0.f;
	bool bSelectionFollowsSpring = true;

	/**
	 * The frame a D-pad or arrow key went down on - the navigation that follows in the same frame is a step, which selects.
	 * Navigation with no key behind it (a stick) is scrolling. See Select While Scrolling.
	 */
	uint64 StepKeyFrame = 0;
	bool bWasMoving = false;

	// The stick, for Stick Scrolls: its last value along the list, whose it is, how long it has been held over, and whether
	// it has run the list into an end.
	TSharedPtr<FFlowListStickInput> StickInput;
	float StickValue = 0.f;
	int32 StickUser = 0;
	float StickHeld = 0.f;
	bool bStickAtEdge = false;

	int32 SelectedIndex = INDEX_NONE;
	int32 NearestIndex = INDEX_NONE;
	bool bMovingFocus = false;

	/** Set while navigation or a focus change is moving the selection, which moves focus itself. */
	bool bNavigating = false;

	FPress Press;
	TArray<TPair<double, float>, TInlineAllocator<16>> VelocitySamples;

	uint64 EnterBatchFrame = 0;
	int32 EnterBatchCount = 0;

	TSharedPtr<FActiveTimerHandle> ActiveTimer;

	// What the last layout measured, for turning pointer travel into items.
	mutable float CachedPixelsPerItem = 100.f;
	mutable FVector2f CachedDragAxis = FVector2f(1.f, 0.f);
	mutable float CachedViewItems = 3.f;
	mutable float CachedContentEndScroll = -1.f;
	mutable bool bVisibilityDirty = false;

	// The spline, flattened, for the size it was last built at.
	mutable TArray<FVector2f> SplinePoints;
	mutable TArray<float> SplineLengths;
	mutable FVector2f SplineBuiltSize = FVector2f(-1.f, -1.f);
	mutable uint32 SplineBuiltHash = 0;
};
