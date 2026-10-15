// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidgetPool.h"
#include "Components/Widget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SPanel.h"
#include "NameTBD/Widgets/WidgetMotion.h"
#include "NotificationSubsystem.h"
#include "NotificationTypes.h"
#include "SNotificationCard.h"

#include "NotificationHost.generated.h"

class SNotificationCard;
class SNotificationStack;
class UNotificationEntryWidget;

/** A starting setup for a Notification Host: picking one sets its channels, layout, motion and card look. */
UENUM(BlueprintType)
enum class ENotificationHostPreset : uint8
{
	Custom UMETA(ToolTip = "Your own settings (editing any preset value makes the preset Custom)."),
	Toasts UMETA(ToolTip = "Toast channel: up to 4 cards, newest nearest the corner, sliding in from the side, pausing while hovered, swipe to close. Put it in a corner."),
	Banner UMETA(ToolTip = "Banner channel: one band at a time across the screen, popping in. Put it top center, stretched across."),
	Feed UMETA(ToolTip = "Feed channel: compact lines, newest at the end, the list sliding up as old lines go. Loot, XP, kill feed."),
	Hints UMETA(ToolTip = "Hint channel: one compact tip at a time, rising in. Put it bottom center."),
	History UMETA(ToolTip = "Shows the notification history (a notification center), newest first, scrolling. Marks it read when shown.")
};

/** What a Notification Host shows. */
UENUM(BlueprintType)
enum class ENotificationHostSource : uint8
{
	Live UMETA(ToolTip = "Notifications as they come, for the channels it shows."),
	History UMETA(ToolTip = "The notification history (types with Keep In History), in a scrolling list.")
};

/** Which way a host's list grows from where it is anchored. */
UENUM(BlueprintType)
enum class ENotificationStackGrowth : uint8
{
	Down UMETA(ToolTip = "From the top edge down (a host at the top of the screen)."),
	Up UMETA(ToolTip = "From the bottom edge up (a host at the bottom of the screen).")
};

/** How a host lays its notifications out. */
UENUM(BlueprintType)
enum class ENotificationStackLayout : uint8
{
	List UMETA(ToolTip = "One after another, Spacing apart; the others glide to make room or close a gap."),
	Deck UMETA(ToolTip = "Stacked like cards: the newest in front, older ones peeking out behind it, smaller and fainter; hovering spreads them into a list.")
};

/** How a notification comes in or goes out. */
UENUM(BlueprintType)
enum class ENotificationTransition : uint8
{
	SlideFade UMETA(DisplayName = "Slide and Fade", ToolTip = "Slides in from Slide From while fading in."),
	Slide UMETA(ToolTip = "Slides in from Slide From, fully opaque (from off the edge)."),
	Fade,
	Pop UMETA(ToolTip = "Grows from 85% while fading in (with Bouncy motion, overshoots)."),
	Rise UMETA(ToolTip = "Rises a little from the growth direction, growing from 96% and fading in: a calm, modern entrance.")
};

/** Where a sliding notification comes from (and goes back to). */
UENUM(BlueprintType)
enum class ENotificationSlideFrom : uint8
{
	Auto UMETA(ToolTip = "The side the host is aligned to (Item Alignment Right: from the right), else from where the list grows from."),
	Left,
	Right,
	Top,
	Bottom
};

/** What clicking a notification (not one of its buttons) does. */
UENUM(BlueprintType)
enum class ENotificationClickBehavior : uint8
{
	Dismiss UMETA(ToolTip = "Closes it (when its type allows it). On Notification Clicked fires too."),
	FirstAction UMETA(ToolTip = "Takes its first action (or only fires On Notification Clicked when it has none)."),
	EventOnly UMETA(ToolTip = "Only fires On Notification Clicked - open the screen it is about."),
	None UMETA(ToolTip = "Clicks pass through to whatever is under it (nothing on it takes the mouse but its buttons).")
};

/** How a host's stack lays out and moves (what the Slate panel needs from the widget). */
struct FNotificationStackSettings
{
	ENotificationStackGrowth Growth = ENotificationStackGrowth::Down;
	ENotificationStackLayout StackLayout = ENotificationStackLayout::List;
	bool bNewestFirst = true;
	float Spacing = 10.f;
	TEnumAsByte<EHorizontalAlignment> ItemAlignment = HAlign_Right;
	float ItemWidth = 0.f;
	float DeckPeek = 10.f;
	int32 DeckDepth = 2;
	ENotificationTransition EnterTransition = ENotificationTransition::SlideFade;
	ENotificationTransition ExitTransition = ENotificationTransition::SlideFade;
	ENotificationSlideFrom SlideFrom = ENotificationSlideFrom::Auto;
	float SlideDistance = 0.f;
	EWidgetMotion Motion = EWidgetMotion::Smooth;
	float EnterDuration = 0.35f;
	float ExitDuration = 0.25f;
	bool bInteractive = true;
	/** Clicks on a notification's body are taken (Click Behavior other than None). */
	bool bTakesClicks = true;
	bool bSwipeToDismiss = true;
	bool bReduceMotion = false;
	bool bShowOverflow = true;
	FLinearColor OverflowColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);
	FSlateFontInfo OverflowFont;
};

DECLARE_DELEGATE_OneParam(FOnNotificationStackItem, int32 /*Id*/);
DECLARE_DELEGATE_OneParam(FOnNotificationStackHover, bool /*bHovered*/);

/** One notification in the stack: its content, and the mouse on it (hover, click, swipe). */
class NAMETBD_API SNotificationItemFrame : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNotificationItemFrame) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SNotificationStack>& InStack, int32 InId);

	void SetContent(const TSharedRef<SWidget>& InContent);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	TWeakPtr<SNotificationStack> WeakStack;
	int32 Id = 0;
};

/**
 * The Slate side of a Notification Host: a panel stacking notifications, animating each in and out (its Enter / Exit
 * Transition), gliding the rest to make room or close a gap (the space each takes grows and shrinks with it), easing to a new
 * size when content changes, laying them out as a list or a deck, swiping them away, and showing "+N" for those still
 * waiting. Only the notifications take the mouse; the panel itself never does.
 */
class NAMETBD_API SNotificationStack : public SPanel
{
public:
	SLATE_BEGIN_ARGS(SNotificationStack) {}
		/** An item finished leaving (its exit played) and was removed. */
		SLATE_EVENT(FOnNotificationStackItem, OnItemGone)
		SLATE_EVENT(FOnNotificationStackItem, OnItemClicked)
		SLATE_EVENT(FOnNotificationStackItem, OnItemSwiped)
		SLATE_EVENT(FOnNotificationStackHover, OnHoverChanged)
	SLATE_END_ARGS()

	SNotificationStack();

	void Construct(const FArguments& InArgs);

	void SetStackSettings(const FNotificationStackSettings& InSettings);

	/** Adds Id (or brings a leaving Id back). bAnimate off: it is there at once (designer, history). */
	void AddItem(int32 Id, const TSharedRef<SWidget>& Content, bool bCanSwipe, bool bAnimate);

	/** Id's content changed (its size may have): eased to the new size. */
	void NotifyContentChanged(int32 Id);

	/** Starts Id leaving; OnItemGone when it has. bAnimate off: gone at once. */
	void RemoveItem(int32 Id, bool bAnimate);

	/** Removes everything at once (no events). */
	void ClearItems();

	/** Has Id, not leaving. */
	bool HasItem(int32 Id) const;

	/** Has Id, leaving or not. */
	bool ContainsItem(int32 Id) const;

	int32 GetItemCount() const;

	void SetOverflowCount(int32 InCount);

	/** The mouse is over one of the notifications (or pressing one). */
	bool IsHoveringItem() const { return HoveredId != 0 || PressedId != 0; }

	// Called by the item frames
	void HandleItemHover(int32 Id, bool bHovered);
	void HandlePress(int32 Id, const FVector2D& ScreenPosition);
	void HandleMove(int32 Id, const FVector2D& ScreenPosition);
	void HandleRelease(int32 Id, const FVector2D& ScreenPosition);
	void HandleCancel(int32 Id);
	bool IsInteractive() const { return StackSettings.bInteractive; }

	// SPanel
	virtual FChildren* GetChildren() override { return &Children; }
	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	struct FItem
	{
		int32 Id = 0;
		TSharedPtr<SNotificationItemFrame> Frame;
		/** 0 gone, 1 in place (Bouncy goes past). */
		FWidgetMotionValue Presence;
		/** Its extent along the stack, eased to the content's desired size. */
		FWidgetMotionValue Extent;
		/** Swipe offset across the stack. */
		FWidgetMotionValue Drag;
		bool bMeasured = false;
		/** Its width when last arranged (how far a swipe must go). */
		mutable float LastWidth = 0.f;
		bool bLeaving = false;
		bool bSwiped = false;
		bool bCanSwipe = true;
		int64 Order = 0;
		/** Set by arranging: what painting needs. */
		mutable float PaintOpacity = 1.f;
	};

	struct FPose
	{
		FVector2f Position = FVector2f::ZeroVector;
		FVector2f Size = FVector2f::ZeroVector;
		float Scale = 1.f;
		float Opacity = 1.f;
		FVector2f Pivot = FVector2f(0.5f, 0.5f);
	};

	void Wake();
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);
	EVisibility GetFrameVisibility() const;
	void SetHovered(int32 Id);
	int32 FindIndex(int32 Id) const;
	void RemoveAt(int32 Index);

	/** Every item's pose in the panel of LocalSize, and where the stack ends (for "+N"). */
	void ComputePoses(const FVector2f& LocalSize, TArray<FPose>& OutPoses, float& OutEnd) const;

	/** Which way and how far an item of Size slides for Transition. */
	FVector2f GetSlideOffset(const FVector2f& Size) const;

	/** Applies Transition at presence P to a pose. */
	void ApplyTransition(ENotificationTransition Transition, float P, FPose& Pose) const;

	float GetMainExtent(const FVector2D& Desired) const;
	FVector2f GetItemSize(const FItem& Item, const FVector2f& LocalSize) const;
	FVector2D GetOverflowSize() const;
	FText GetOverflowText() const;

	TSlotlessChildren<SNotificationItemFrame> Children;
	TArray<FItem> Items;
	FNotificationStackSettings StackSettings;

	FOnNotificationStackItem OnItemGone;
	FOnNotificationStackItem OnItemClicked;
	FOnNotificationStackItem OnItemSwiped;
	FOnNotificationStackHover OnHoverChanged;

	int32 HoveredId = 0;
	int32 PressedId = 0;
	FVector2D PressStart = FVector2D::ZeroVector;
	bool bDragging = false;
	bool bWasHovering = false;
	int64 NextOrder = 0;
	int32 OverflowCount = 0;

	/** Deck: 0 stacked, 1 spread into a list (while hovered). */
	FWidgetMotionValue Spread;

	/** Seconds the clock keeps running with nothing moving, to catch a size change after a content change. */
	float KeepAwake = 0.f;

	TSharedPtr<FActiveTimerHandle> AnimateTimer;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotificationHostEvent, const FNotificationView&, Notification);

/**
 * Notification Host: where notifications show. Place one per area of the HUD (a corner for toasts, the top for banners, a side
 * for the feed) and give it the channels it shows; the player's notification system hands it what to draw - in the built-in
 * card (Card Style: toast, banner, compact) or your own widget (a Notification Entry Widget class, per host or per type).
 *
 * It animates notifications in and out, glides the others into place, pauses their time while hovered, lets them be clicked or
 * swiped away, shows "+N" for those waiting, and holds no notification while it isn't on screen (they wait for it, or for
 * another host of the channel). With Source History it is a notification center: the history, scrolling, newest first.
 * Works on a Diegetic Screen as on the HUD (sounds come from the screen).
 */
UCLASS(meta = (DisplayName = "Notification Host"))
class NAMETBD_API UNotificationHost : public UWidget
{
	GENERATED_BODY()

public:
	UNotificationHost(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Picks a starting setup (channels, layout, motion, card look). Editing a value it set makes it Custom. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notifications")
	ENotificationHostPreset Preset = ENotificationHostPreset::Toasts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notifications")
	ENotificationHostSource Source = ENotificationHostSource::Live;

	/** The channels it shows (and their child tags). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notifications", meta = (Categories = "Notification.Channel", EditCondition = "Source == ENotificationHostSource::Live", EditConditionHides))
	FGameplayTagContainer Channels;

	/** When two hosts on screen show the same channel, the higher one gets it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notifications", meta = (EditCondition = "Source == ENotificationHostSource::Live", EditConditionHides))
	int32 HostPriority = 0;

	/** At most this many at once. 0: the channel's Max Visible (Project Settings > Plugins > Notifications). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notifications", meta = (ClampMin = "0", UIMax = "12", EditCondition = "Source == ENotificationHostSource::Live", EditConditionHides))
	int32 MaxVisible = 0;

	/** History: at most this many shown. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notifications", meta = (ClampMin = "1", UIMax = "200", EditCondition = "Source == ENotificationHostSource::History", EditConditionHides))
	int32 MaxHistoryShown = 50;

	/** History: marks the history read while it is shown. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Notifications", meta = (EditCondition = "Source == ENotificationHostSource::History", EditConditionHides))
	bool bMarkReadWhenShown = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	ENotificationStackGrowth Growth = ENotificationStackGrowth::Down;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	ENotificationStackLayout StackLayout = ENotificationStackLayout::List;

	/** The newest nearest the edge it grows from (toasts). Off: the newest at the far end (a feed, a chat). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	bool bNewestFirst = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0", UIMax = "40", ForceUnits = "px"))
	float Spacing = 10.f;

	/** Where each notification sits across the host. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout")
	TEnumAsByte<EHorizontalAlignment> ItemAlignment = HAlign_Right;

	/** Every notification this wide. 0: as wide as it asks (the card's Width), never wider than the host. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "0", UIMax = "1200", ForceUnits = "px"))
	float ItemWidth = 0.f;

	/** Deck: how far each card behind peeks out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (EditCondition = "StackLayout == ENotificationStackLayout::Deck", EditConditionHides, ClampMin = "0", UIMax = "30", ForceUnits = "px"))
	float DeckPeek = 10.f;

	/** Deck: how many cards show behind the front one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (EditCondition = "StackLayout == ENotificationStackLayout::Deck", EditConditionHides, ClampMin = "0", ClampMax = "6"))
	int32 DeckDepth = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	ENotificationTransition EnterTransition = ENotificationTransition::SlideFade;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	ENotificationTransition ExitTransition = ENotificationTransition::SlideFade;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	ENotificationSlideFrom SlideFrom = ENotificationSlideFrom::Auto;

	/** How far a slide travels. 0: its own size and a little more (from off its edge). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (ClampMin = "0", UIMax = "800", ForceUnits = "px"))
	float SlideDistance = 0.f;

	/** How notifications come in, go out and glide into place. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion")
	EWidgetMotion Motion = EWidgetMotion::Smooth;

	/** Seconds to come in. 0: Motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (EditCondition = "Motion != EWidgetMotion::Instant", EditConditionHides, ClampMin = "0", UIMax = "2", Units = "s"))
	float EnterDuration = 0.35f;

	/** Seconds to go out. 0: Motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion", meta = (EditCondition = "Motion != EWidgetMotion::Instant", EditConditionHides, ClampMin = "0", UIMax = "2", Units = "s"))
	float ExitDuration = 0.25f;

	/** Notifications take the mouse: hover, click, close button, swipe. Off: the mouse goes through them (a kill feed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	bool bInteractive = true;

	/** Their time stops while the mouse is over one (all of this host's). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (EditCondition = "bInteractive"))
	bool bPauseOnHover = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (EditCondition = "bInteractive"))
	ENotificationClickBehavior ClickBehavior = ENotificationClickBehavior::EventOnly;

	/** Dragging one sideways and letting go past a third of its width closes it (types that allow it). History: removes it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (EditCondition = "bInteractive"))
	bool bSwipeToDismiss = true;

	/** A "+3" at the end of the list while notifications of its channels wait for room. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (EditCondition = "Source == ENotificationHostSource::Live", EditConditionHides))
	bool bShowOverflowCount = true;

	/**
	 * The widget each notification is drawn with (a type's own Entry Widget Class wins). None: the built-in card, looking as
	 * Card Style says.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TSubclassOf<UNotificationEntryWidget> EntryWidgetClass;

	/** The built-in card's look (when no entry widget class applies). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FNotificationCardStyle CardStyle;

	/** Shows sample notifications in the designer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Designer")
	bool bPreviewInDesigner = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Designer", meta = (EditCondition = "bPreviewInDesigner", ClampMin = "1", ClampMax = "8"))
	int32 PreviewCount = 3;

	/** A notification came on screen in this host. */
	UPROPERTY(BlueprintAssignable, Category = "Notifications")
	FOnNotificationHostEvent OnNotificationShown;

	/** A notification left this host (played out). */
	UPROPERTY(BlueprintAssignable, Category = "Notifications")
	FOnNotificationHostEvent OnNotificationRemoved;

	/** Applies a preset's values now (as picking it in the Details does). */
	UFUNCTION(BlueprintCallable, Category = "Notification Host")
	void ApplyPreset(ENotificationHostPreset InPreset);

	UFUNCTION(BlueprintCallable, Category = "Notification Host")
	void SetCardStyle(const FNotificationCardStyle& InCardStyle);

	UFUNCTION(BlueprintCallable, Category = "Notification Host")
	void SetChannels(const FGameplayTagContainer& InChannels);

	/** Dismisses every notification shown here (and waiting for its channels when bIncludeQueued). */
	UFUNCTION(BlueprintCallable, Category = "Notification Host")
	void DismissAll(bool bIncludeQueued = false);

	/** How many are on screen in it. */
	UFUNCTION(BlueprintPure, Category = "Notification Host")
	int32 GetShownCount() const;

	// For the notification subsystem
	/** Made by the subsystem because nothing else showed its channel (Create Default Hosts); replaced by a host of yours. */
	bool bIsDefaultHost = false;

	/** The subsystem it belongs to when it isn't inside a player's widget (a default host). */
	TWeakObjectPtr<UNotificationSubsystem> SubsystemOverride;

	bool IsLive() const;
	bool ShowsChannel(const FGameplayTag& Channel) const;
	int32 GetCapacity(const FNotificationChannelSettings& ChannelSettings) const;
	bool IsHoldingTime() const;
	bool HasEntry(int32 Id) const;
	void ShowEntry(const FNotificationView& View, bool bFirstTime);
	void UpdateEntry(const FNotificationView& View, bool bStacked);
	void HideEntry(int32 Id);
	void RefreshEntry(int32 Id);
	void RefreshAllEntries();
	void SetOverflowCount(int32 InCount);

	/** Once a frame from the subsystem: the "+N", a history marked read while shown. */
	void TickFromSubsystem(UNotificationSubsystem& Subsystem);

	/** The player's preferences changed (Reduce Motion). */
	void ApplyPreferences();

	/** Its Slate widget is in a window and nothing above it is hidden. */
	bool IsOnScreen() const;

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** The values each preset sets. */
	static void MakePresetValues(ENotificationHostPreset InPreset, UNotificationHost& Out);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	FNotificationStackSettings MakeStackSettings() const;
	UNotificationSubsystem* FindSubsystem() const;

	/** The Slate content for View: an entry widget or a card. */
	TSharedRef<SWidget> MakeEntryContent(const FNotificationView& View, bool bHistoryEntry);
	TSubclassOf<UNotificationEntryWidget> ResolveEntryClass(const FNotificationView& View) const;
	FNotificationCardHooks MakeHooks(int32 Id, bool bHistoryEntry);
	void ReleaseEntry(int32 Id);

	void HandleItemGone(int32 Id);
	void HandleItemClicked(int32 Id);
	void HandleItemSwiped(int32 Id);
	void HandleHoverChanged(bool bInHovered);

	void RegisterWithSubsystem();
	void UnregisterFromSubsystem();

	/** History: matches the shown list to the subsystem's history. */
	UFUNCTION()
	void SyncHistory();

	void BuildDesignerPreview();
	static TArray<FNotificationView> MakePreviewViews(ENotificationHostPreset InPreset, int32 Count);

	TSharedPtr<SNotificationStack> MyStack;
	TSharedPtr<SWidget> MyRoot;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UNotificationEntryWidget>> EntryWidgets;

	TMap<int32, TSharedPtr<SNotificationCard>> Cards;
	TMap<int32, FNotificationView> Views;

	UPROPERTY(Transient)
	FUserWidgetPool EntryPool;

	TWeakObjectPtr<UNotificationSubsystem> RegisteredSubsystem;
	bool bHovered = false;

	/** Set while ApplyPreset writes, so the edit isn't taken for a change by hand. */
	bool bApplyingPreset = false;

	/** Set while the history is matched, so marking it read doesn't come back round. */
	bool bSyncingHistory = false;
};
