// Copyright AR GameSystems 2026. All rights reserved.

#include "InfoCardAnchor.h"

#include "NameTBD/Sound/UISoundTypes.h"

#include "InfoCardWidget.h"
#include "SInfoCardDesignerPreview.h"
#include "NameTBD/DeveloperSettings/InfoCardDeveloperSettings.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Containers/Ticker.h"
#include "Widgets/Layout/SBorder.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"

#define LOCTEXT_NAMESPACE "InfoCardAnchor"

DEFINE_LOG_CATEGORY_STATIC(LogInfoCard, Log, All);

TArray<TWeakObjectPtr<UInfoCardAnchor>> UInfoCardAnchor::ActiveCardAnchors;

// A card closing before the process has ever recorded one shouldn't look
// like "a card JUST closed" — this sentinel keeps ShouldSkipHoverDelay false
// until RecordCardClosed has actually run at least once.
double UInfoCardAnchor::LastCardClosedRealTimeSeconds = -1.0;

// 0 is a legitimate GFrameCounter value (the very first frame), so this
// starts at a value no real frame can ever equal instead — an
// uninitialized/never-consumed state that WasDismissClickConsumedThisFrame
// correctly reads as false on frame 0 without a separate "has this ever
// been set" bool.
uint64 UInfoCardAnchor::DismissClickConsumedFrame = TNumericLimits<uint64>::Max();

int32 UInfoCardAnchor::ShowCardDepth = 0;

// How many info cards may be nested inside one another before ShowCardAtIndex
// refuses to go further. A submenu inside a context menu inside a HUD card is
// three; anything approaching this is a configuration mistake, not a menu.
static constexpr int32 MaxShowCardDepth = 8;

// Why every delay in this file runs on FTSTicker rather than FTimerManager:
//
// FTimerManager — the world's, and the GameInstance's, which UWorld hands out
// as its own whenever one exists, so they are the same object — is only
// ticked from UWorld::Tick while the world is NOT paused. Any timer armed
// while the game is paused simply sits there until the game is unpaused. For
// info cards that meant a Highlight card with a Highlight Delay never
// appeared in a pause menu (the delay timer never fired), and an Always On
// card spawned while paused never showed either (its next-tick pass never
// ran). Mouse enter/leave, focus changes, and the card widgets' own
// NativeTick all keep running during pause, because Slate does — only the
// timers were frozen.
//
// FTSTicker::GetCoreTicker() is ticked by the engine loop every frame
// regardless of world pause state, with real (undilated) frame time, which is
// also what the rest of this system already measures UI time in
// (FPlatformTime for the successive-hover window, Slate's delta in the card
// widget's NativeTick).

UInfoCardAnchor::UInfoCardAnchor()
{
	// The anchor must be hit-testable or it will never receive mouse enter/leave
	// or right-click. UBorder's own constructor sets SelfHitTestInvisible, so
	// this override is load-bearing, not cosmetic.
	// Children are hit-tested first, so a Button inside still gets its clicks.
	SetVisibility(ESlateVisibility::Visible);

	// Invisible by default; it's a behaviour wrapper, not a visual one.
	Background.DrawAs = ESlateBrushDrawType::NoDrawType;
	
	// UBorder defaults to a small internal padding; zero it out so wrapping a
	// widget in an anchor never nudges its layout.
	SetPadding(FMargin(0.f));
}

TSharedRef<SWidget> UInfoCardAnchor::RebuildWidget()
{
	// Just build the underlying SBorder here. Mouse handlers are bound in
	// SynchronizeProperties (see header comment) — do NOT bind them here,
	// UBorder installs its own OnMouseButtonDown as part of this call.
	TSharedRef<SWidget> Result = Super::RebuildWidget();

	// GetCachedWidget() (used by HandleSlateFocusChanging to identify this
	// anchor in the focus path) only returns something valid once the
	// underlying SWidget above actually exists, so this is bound here
	// rather than in the constructor. RebuildWidget can run more than once
	// over this UWidget's lifetime (e.g. a forced Slate rebuild), so
	// BindSlateFocusChanging replaces any previous subscription rather than
	// stacking a second one.
	if (!IsDesignTime())
	{
		BindSlateFocusChanging();
	}

#if WITH_EDITOR
	// The designer gets the border wrapped in a panel that can also draw the previewed card beside it, outside layout
	// (see Preview Card). In game the border is handed over as it always was.
	if (IsDesignTime())
	{
		DesignerPreviewPanel = SNew(SInfoCardDesignerPreview)
			[
				Result
			];
		return DesignerPreviewPanel.ToSharedRef();
	}
#endif

	return Result;
}

void UInfoCardAnchor::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// InfoCards can be mutated in place at runtime after this first copy —
	// e.g. GetOrAddPayload attaching data into a payload's own properties.
	// UMG can call SynchronizeProperties repeatedly during normal play
	// (visibility changes, layout invalidation, a hover card opening
	// elsewhere), and an unconditional re-copy here would silently discard
	// those mutations, resetting every payload back to its authored
	// default the next time a card reads it. Only sync once at runtime;
	// always resync at design-time so editing the source — the Data Asset,
	// or CustomInfoCards directly — is reflected live in the UMG preview.
	if (UsesGeneratedInfoCards())
	{
		// A subclass building its cards from its own fields (Tooltip Card Anchor, Context Menu Anchor). Same once-at-runtime
		// rule as the other sources; its setters call RebuildGeneratedInfoCards themselves.
		if (IsDesignTime() || !bInfoCardsSynced)
		{
			RebuildGeneratedInfoCards();
		}
	}
	else if (SourceMode == EInfoCardSourceMode::Custom)
	{
		if (IsDesignTime() || !bInfoCardsSynced)
		{
			RebuildInfoCardsFromCustomArray();
		}
	}
	else if (IsValid(InfoCards_DA) && (IsDesignTime() || !bInfoCardsSynced))
	{
		RebuildInfoCardsFromDataAsset();
	}

	// An AlwaysOn entry has no hover/focus/click to wait for, so something has
	// to ask it to show, and this is the first moment InfoCards has content.
	//
	// Gated on bAlwaysOnArmed, NOT on bInfoCardsSynced flipping false-to-true
	// the way it used to be. bInfoCardsSynced is a plain Transient bool, and
	// plain Transient properties are still carried across object duplication
	// (that's what DuplicateTransient exists to opt out of) — so an anchor
	// whose InfoCards were synced during a design-time pass could arrive at
	// runtime already flagged as synced, the false-to-true transition never
	// happened, and every Always On entry on it silently never showed. Both
	// flags are DuplicateTransient now, and arming has its own flag rather
	// than riding on a side effect of another one.
	//
	// Requested, not run: see RequestShowAlwaysOnCards for why spawning a card
	// from inside this call is unsafe. bAlwaysOnShowPending surviving a failed
	// schedule (no world yet) is what makes the retry here work.
	if (!IsDesignTime() && bInfoCardsSynced && (!bAlwaysOnArmed || bAlwaysOnShowPending))
	{
		bAlwaysOnArmed = true;
		RequestShowAlwaysOnCards();
	}

#if WITH_EDITOR
	// Design-time only, and only meaningful for the Data Asset path: Custom
	// mode has no separate asset for PayloadDefaults/SettingsOverrides to
	// stay in sync with, and both arrays are hidden while it's active.
	//
	// For the Data Asset path, this catches the case PostEditChangeProperty
	// can't: the Data Asset is a separate asset, so edits to ITS contents (a
	// card added, a payload removed) never notify this anchor. Re-syncing
	// here means the Details panel is correct again as soon as the UMG
	// editor next refreshes this widget, with no button for anyone to
	// remember to press.
	//
	// Skipped on the class default object: these two rewrite UPROPERTY arrays
	// (and SyncPayloadDefaultsFromDataAsset allocates a payload object per
	// slot while doing it), which has no business happening on a CDO during
	// class construction.
	if (IsDesignTime() && SourceMode == EInfoCardSourceMode::UseDataAsset && !UsesGeneratedInfoCards() && !HasAnyFlags(RF_ClassDefaultObject))
	{
		SyncPayloadDefaultsFromDataAsset();
		SyncSettingsOverridesFromDataAsset();
		SyncAdditiveAnimationOverridesFromDataAsset();
	}
#endif

	// The real card beside the anchor in the designer, from InfoCards as they now stand. Before the animation preview, which
	// animates that card.
	RefreshDesignerPreviewCard();

	// After the syncs above, so the preview is built from InfoCards as they now stand - additive
	// animation overrides included. Returns immediately at runtime, which is why there is no
	// IsDesignTime() guard here.
	UWidgetAnimDesignerPreview::Synchronize(this);

	EnsureRuntimeStatesSize();
	EnsureHitTestable();

	if (MyBorder.IsValid())
	{
		MyBorder->SetOnMouseEnter(FNoReplyPointerEventHandler::CreateUObject(this, &UInfoCardAnchor::HandleMouseEnter));
		MyBorder->SetOnMouseLeave(FSimpleNoReplyPointerEventHandler::CreateUObject(this, &UInfoCardAnchor::HandleMouseLeave));
		MyBorder->SetOnMouseButtonDown(FPointerEventHandler::CreateUObject(this, &UInfoCardAnchor::HandleMouseButtonDown));
	}
}

void UInfoCardAnchor::EnsureHitTestable()
{
	if (InfoCards.Num() == 0)
	{
		return; // no cards configured - leave the designer's choice alone
	}

	const ESlateVisibility CurrentVisibility = GetVisibility();
	if (CurrentVisibility != ESlateVisibility::HitTestInvisible && CurrentVisibility != ESlateVisibility::SelfHitTestInvisible)
	{
		return;
	}

	// Collapsed/Hidden are respected - someone deliberately turned the whole
	// thing off. The two hit-test-invisible modes are the ones that look fine
	// on screen while silently swallowing every event the anchor exists to
	// receive, so those get corrected.
	//
	// Warned once per anchor rather than every time: SynchronizeProperties
	// runs repeatedly (every property change in the editor, every Slate
	// rebuild), and the same line repeated a few hundred times buries the
	// rest of the log without telling anyone anything new.
	if (!bHasWarnedAboutHitTesting)
	{
		bHasWarnedAboutHitTesting = true;

		UE_LOG(LogInfoCard, Warning,
			TEXT("InfoCardAnchor '%s' has %d card(s) but was hit-test invisible, so it could never receive hover or right-click. Forcing Visibility to Visible."),
			*GetName(), InfoCards.Num());
	}

	SetVisibility(ESlateVisibility::Visible);
}

void UInfoCardAnchor::ReleaseSlateResources(bool bReleaseChildren)
{
	// Cards live in the viewport, not under us, so they won't be cleaned up
	// automatically when the owning widget is removed.
	HideAllCards();

	// No Slate widget, no hover or focus. Left set, a rebuilt anchor would
	// believe a trigger from its previous life was still holding a card up
	// (see NotifyCardUnpinned).
	bIsMouseOverAnchor   = false;
	bIsFocusInAnchorPath = false;

	// The SWidget this subscription was keyed off of is about to go away
	// (and RebuildWidget may or may not run again to make a new one), so
	// drop it here rather than leave a stale subscription registered
	// against FSlateApplication for the lifetime of the process.
	UnbindSlateFocusChanging();

	// The Slate widget the designer preview's active timer was registered against is going away.
	UWidgetAnimDesignerPreview::Shutdown(this);

	// The designer's card lives in that panel. Built again by the next SynchronizeProperties.
	DesignerPreviewPanel.Reset();
	if (DesignerPreviewCard)
	{
		DesignerPreviewCard->ReleaseSlateResources(true);
		DesignerPreviewCard = nullptr;
	}

	Super::ReleaseSlateResources(bReleaseChildren);
}

void UInfoCardAnchor::BeginDestroy()
{
	// Weak pointers would eventually go stale on their own, but leaving dead
	// entries in a process-wide static means every exclusivity walk gets
	// slower for the rest of the session.
	ActiveCardAnchors.RemoveSwap(TWeakObjectPtr<UInfoCardAnchor>(this));

	// Belt-and-suspenders alongside ReleaseSlateResources: BeginDestroy can
	// run without ReleaseSlateResources having been called first in some
	// teardown orders, and a subscription left bound after this object is
	// gone would call back into a dead UObject.
	UnbindSlateFocusChanging();

	UWidgetAnimDesignerPreview::Shutdown(this);

	Super::BeginDestroy();
}

void UInfoCardAnchor::PostLoad()
{
	Super::PostLoad();

	// The harness's mode is no longer in the Details panel (the toolbar plays the card on the designer's copy), so a mode
	// an older version saved would play forever with no way to switch it off.
	DesignerAnimPreview.Mode = EWidgetAnimPreviewMode::Off;

	// Additive Animation Overrides from before Use Basic Animations: the three loose arrays become the struct.
	for (FInfoCardAdditiveAnimationsOverride& Override : AdditiveAnimationOverrides)
	{
		if (Override.Animations.IsEmpty() && Override.PlayIn.IsEmpty() && Override.PlayOut.IsEmpty())
		{
			continue;
		}

		Override.AdditiveAnimations.PlayIn.Append(Override.PlayIn);
		Override.AdditiveAnimations.PlayOut.Append(Override.PlayOut);
		Override.AdditiveAnimations.MigrateLegacyAnimations(Override.Animations);
		Override.PlayIn.Reset();
		Override.PlayOut.Reset();
	}

	// PayloadDefaults used to be one element per CardId, each holding a slot
	// per payload class gated by Set Defaults. It's now one flat element per
	// payload. Old elements load their CardId and the hidden legacy Payloads
	// array; flatten them here so nothing a designer already set is lost.
	const bool bHasLegacyLayout = PayloadDefaults.ContainsByPredicate(
		[](const FInfoCardPayloadDefault& Default) { return !Default.Payloads.IsEmpty(); });

	if (!bHasLegacyLayout)
	{
		return;
	}

	TArray<FInfoCardPayloadDefault> Flattened;
	for (const FInfoCardPayloadDefault& Existing : PayloadDefaults)
	{
		if (Existing.Payloads.IsEmpty())
		{
			if (Existing.Payload)
			{
				Flattened.Add(Existing); // already the new layout
			}
			continue;
		}

		for (const FInfoCardPayloadDefaultEntry& LegacySlot : Existing.Payloads)
		{
			// A slot that was never switched on overrode nothing. The next
			// editor sync recreates it as a plain copy of the Data Asset.
			if (!LegacySlot.bSetDefaults || !LegacySlot.Payload)
			{
				continue;
			}

			// No Baseline: ApplyPayloadDefaults then counts every field that
			// differs from the Data Asset as changed, which is what Set
			// Defaults meant. The next editor sync gives it a real Baseline.
			FInfoCardPayloadDefault& Converted = Flattened.AddDefaulted_GetRef();
			Converted.CardId = Existing.CardId;
			Converted.Payload = LegacySlot.Payload;
		}
	}

	PayloadDefaults = MoveTemp(Flattened);
}

UUserWidget* UInfoCardAnchor::GetCardSourceWidget() const
{
	// Every widget under a WidgetTree is outered to that tree, whose own
	// outer is the UserWidget instance that owns it (e.g. a specific
	// W_InventorySlot). GetTypedOuter walks up until it finds one.
	return GetTypedOuter<UUserWidget>();
}


// =================================================================================================
// Input
// =================================================================================================

void UInfoCardAnchor::HandleMouseEnter(const FGeometry& Geometry, const FPointerEvent& PointerEvent)
{
	if (IsDesignTime())
	{
		return;
	}

	bIsMouseOverAnchor = true;

	// Fired before TriggerHoverEntries gets a chance to spawn anything
	// (including zero-delay hover cards, which spawn synchronously inside
	// that same call) — a Blueprint bound here can set payloads and have
	// them already in place by the time ShowCardAtIndex builds the widget.
	OnMouseHoverEnter.Broadcast();

	TriggerHoverEntries(EInfoCardHoverTriggerSource::Mouse);
}

void UInfoCardAnchor::HandleMouseLeave(const FPointerEvent& PointerEvent)
{
	if (IsDesignTime())
	{
		return;
	}

	bIsMouseOverAnchor = false;

	OnMouseHoverExit.Broadcast();

	UntriggerHoverEntries(EInfoCardHoverTriggerSource::Mouse);
}

void UInfoCardAnchor::BindSlateFocusChanging()
{
	if (!FSlateApplication::IsInitialized())
	{
		return; // e.g. running in a commandlet with no Slate app at all
	}

	UnbindSlateFocusChanging(); // replace, never stack, a previous subscription

	SlateFocusChangingHandle = FSlateApplication::Get().OnFocusChanging().AddUObject(
		this, &UInfoCardAnchor::HandleSlateFocusChanging);
}

void UInfoCardAnchor::UnbindSlateFocusChanging()
{
	if (!SlateFocusChangingHandle.IsValid())
	{
		return;
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnFocusChanging().Remove(SlateFocusChangingHandle);
	}

	SlateFocusChangingHandle.Reset();
}

void UInfoCardAnchor::HandleSlateFocusChanging(const FFocusEvent& FocusEvent,
	const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget,
	const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget)
{
	if (IsDesignTime() || FocusTriggerMode != EInfoCardFocusTriggerMode::Automatic)
	{
		// Only the Automatic mode reacts to this app-wide notification —
		// Manual projects drive the same effect through NotifyFocusReceived
		// / NotifyFocusLost instead, and Disabled means focus never
		// triggers anything here.
		return;
	}

	TSharedPtr<SWidget> MySlateWidget = GetCachedWidget();
	if (!MySlateWidget.IsValid())
	{
		return;
	}

	// "In the focus path" means this anchor's own SWidget, or one of its
	// ancestors up from the newly/previously focused leaf widget, is us —
	// i.e. the focused widget is this anchor or something nested inside
	// it, e.g. a wrapped Button a menu's navigation system moved focus to.
	// Walking SWidget::GetParentWidget() directly, rather than relying on
	// FWidgetPath/FWeakWidgetPath helper methods, keeps this resilient to
	// exactly the kind of engine-version API drift that broke the first
	// version of this (UWidget-level virtuals that didn't actually exist
	// here).
	const auto IsThisAnchorOrAncestorOf = [&MySlateWidget](TSharedPtr<SWidget> LeafWidget) -> bool
	{
		for (TSharedPtr<SWidget> Current = LeafWidget; Current.IsValid(); Current = Current->GetParentWidget())
		{
			if (Current == MySlateWidget)
			{
				return true;
			}
		}
		return false;
	};

	const bool bWasInFocusPath = IsThisAnchorOrAncestorOf(OldFocusedWidget);
	const bool bIsInFocusPath = IsThisAnchorOrAncestorOf(NewFocusedWidget);

	if (bIsInFocusPath && !bWasInFocusPath)
	{
		// Keyboard/gamepad counterpart to HandleMouseEnter. Deliberately
		// routed through the same TriggerHoverEntries used by mouse hover:
		// a Highlight-type InfoCards entry shows the same way and respects
		// the same HoverDelay regardless of which input drove it, which
		// keeps the common case (one card, one trigger) as simple to
		// author as it's ever been.
		bIsFocusInAnchorPath = true;
		OnFocusReceived.Broadcast();
		TriggerHoverEntries(EInfoCardHoverTriggerSource::Focus);
	}
	else if (bWasInFocusPath && !bIsInFocusPath)
	{
		bIsFocusInAnchorPath = false;
		OnFocusLost.Broadcast();
		UntriggerHoverEntries(EInfoCardHoverTriggerSource::Focus);
	}
}

FReply UInfoCardAnchor::HandleMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& PointerEvent)
{
	if (!IsDesignTime())
	{
		// Right and left click drive their own, independent CardType — a
		// single mouse-down is never both, so at most one of these matches.
		const FKey EffectingButton = PointerEvent.GetEffectingButton();

		// Broadcast before MatchType is acted on below — the loop further
		// down is what actually calls Show/HideCardAtIndex, so a Blueprint
		// bound to either event here can set payloads (GetOrAddPayload /
		// SetPayloadById) and have them in place before the matching card
		// builds, synchronously, later in this same call.
		ECardType MatchType;
		if (EffectingButton == EKeys::RightMouseButton)
		{
			MatchType = ECardType::RightClick;
			OnRightClickPressed.Broadcast();
		}
		else if (EffectingButton == EKeys::LeftMouseButton)
		{
			MatchType = ECardType::LeftClick;
			OnLeftClickPressed.Broadcast();
		}
		else
		{
			MatchType = ECardType::Hover; // sentinel: neither button we react to on mouse-down
		}

		if (MatchType != ECardType::Hover)
		{
			EnsureRuntimeStatesSize();

			bool bHandledAny = false;

			for (int32 Index = 0; Index < InfoCards.Num(); ++Index)
			{
				// Read by value, not held as a reference across the
				// Show/Hide calls below. Those can re-enter this anchor
				// (Blueprint bound to OnCardShown/OnCardHidden, a payload
				// mutation that re-syncs InfoCards) and reallocate the array
				// out from under a reference that outlived it.
				if (!InfoCards[Index].bEnabled || InfoCards[Index].CardType != MatchType)
				{
					continue;
				}

				bHandledAny = true;

				// Re-clicking the same trigger while its card is already up
				// always closes it now (no per-entry option to instead
				// force a rebuild-on-reclick — that was more configuration
				// than the common case needed).
				//
				// IsSlotActive rather than a bare pointer check: a card that
				// closed itself (click-outside) but lost the race to notify
				// this anchor would otherwise read as "still active" here,
				// making this click close a card that is already gone
				// instead of opening a new one — the fresh click right
				// after would then be the one that actually shows it. See
				// IsSlotActive's comment.
				const bool bIsActive = IsSlotActive(Index);
				if (bIsActive && ShouldReopenOnRepeatClick(Index) && !IsSlotPinned(Index))
				{
					// A context menu follows the latest click: the open one goes, and a new one opens where this press
					// landed. The closing card is already out of the slot once HideCardAtIndex returns.
					HideCardAtIndex(Index);
					ShowCardAtIndex(Index);
				}
				else if (bIsActive)
				{
					// Re-clicking the trigger is an automatic toggle, not an
					// explicit close request, so a pinned card ignores it.
					// The click is still reported handled (bHandledAny) so it
					// doesn't fall through to whatever is underneath as though
					// this anchor weren't there.
					if (!IsSlotPinned(Index))
					{
						HideCardAtIndex(Index);
					}
				}
				else
				{
					ShowCardAtIndex(Index);
				}
			}

			if (bHandledAny)
			{
				return FReply::Handled();
			}
		}
	}

	// Preserve UBorder's normal OnMouseButtonDownEvent BP delegate, which we
	// replaced when we installed our own handler in SynchronizeProperties.
	if (OnMouseButtonDownEvent.IsBound())
	{
		return OnMouseButtonDownEvent.Execute(Geometry, PointerEvent).NativeReply;
	}

	return FReply::Unhandled();
}

void UInfoCardAnchor::PlayCardSound(int32 Index, EInfoCardSoundEvent Event)
{
	if (!InfoCards.IsValidIndex(Index))
	{
		return;
	}

	EnsureRuntimeStatesSize();
	if (!RuntimeStates.IsValidIndex(Index))
	{
		return;
	}

	const TArray<FInfoCardSoundSet>& Sets = InfoCards[Index].Sounds;
	TArray<FUISoundRuntime>& Runtimes = RuntimeStates[Index].TriggerSoundRuntimes;
	FUISoundPlayer::ResizeRuntimes(this, Runtimes, Sets.Num());

	// Straight Play, not PlayOnEventChange: a trigger is an event, not a state the anchor settles into, so
	// hovering the same anchor twice has to sound twice. The set's own repeat limit is what stops a jittering
	// cursor on the anchor's edge from firing it continuously.
	for (int32 SetIndex = 0; SetIndex < Sets.Num(); ++SetIndex)
	{
		FUISoundPlayer::Play(this, Runtimes[SetIndex], Sets[SetIndex].Settings, Sets[SetIndex].GetSoundForEvent(Event));
	}
}

void UInfoCardAnchor::PlayCardSoundById(FName CardId, EInfoCardSoundEvent Event)
{
	PlayCardSound(FindCardIndexById(CardId), Event);
}

void UInfoCardAnchor::HandleHoverDelayElapsedForIndex(int32 Index)
{
	// The ticker element that called us removes itself when its delegate
	// returns false, so the stored handle only needs forgetting, not
	// removing. Reset BEFORE ShowCardAtIndex, which can re-enter this anchor
	// and legitimately arm a fresh delay for this same slot.
	if (RuntimeStates.IsValidIndex(Index))
	{
		RuntimeStates[Index].HoverDelayHandle.Reset();
	}

	ShowCardAtIndex(Index);
}

void UInfoCardAnchor::ClearHoverDelayAtIndex(int32 Index)
{
	if (!RuntimeStates.IsValidIndex(Index))
	{
		return;
	}

	// Safe on an empty or already-fired handle (RemoveTicker no-ops when the
	// weak handle no longer pins anything), and safe from inside the ticker's
	// own callback — ShowCardAtIndex -> IsSlotActive can reach here while the
	// delay that triggered it is still on the stack.
	FTSTicker::GetCoreTicker().RemoveTicker(RuntimeStates[Index].HoverDelayHandle);
	RuntimeStates[Index].HoverDelayHandle.Reset();
}

void UInfoCardAnchor::RequestShowAlwaysOnCards()
{
	if (IsDesignTime())
	{
		return; // never spawn real card widgets into the UMG preview
	}

	// "A pass is wanted." Set before any of the early-outs below so a request
	// that can't be scheduled right now is still remembered.
	bAlwaysOnShowPending = true;

	if (bAlwaysOnShowQueued)
	{
		// One is already scheduled for the coming tick. Several requests in a
		// single frame — a batch of suppression tags removed one by one, a
		// re-enable next to a re-arm — should still produce exactly one pass.
		return;
	}

	if (!GetWorld())
	{
		// Nothing to show against yet: this anchor's widget isn't tied to a
		// world (and so has no owning player to create a card for) at this
		// point in construction. bAlwaysOnShowPending stays set and
		// SynchronizeProperties asks again, by which point there normally is
		// one.
		return;
	}

	bAlwaysOnShowQueued = true;

	// Core ticker rather than the world's SetTimerForNextTick, which never
	// fires while the game is paused — see the note at the top of this file.
	// A 0 delay fires on the ticker's next pass, which is always a later
	// frame than this call and always outside Slate construction.
	//
	// Weak lambda: if this anchor is destroyed between now and next tick the
	// delegate is unbound, the ticker drops it, and the callback simply
	// doesn't run, rather than calling into a dead UObject. Returning false
	// makes it one-shot.
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(this, [this](float /*DeltaTime*/)
		{
			bAlwaysOnShowQueued  = false;
			bAlwaysOnShowPending = false;
			ShowAlwaysOnCards();
			return false;
		}),
		0.f);
}

void UInfoCardAnchor::ShowAlwaysOnCards()
{
	if (IsDesignTime())
	{
		return;
	}

	EnsureRuntimeStatesSize();

	// Indexed rather than ranged-for, and re-bounds-checked each iteration:
	// ShowCardAtIndex spawns a widget whose NativeConstruct can run arbitrary
	// Blueprint, and that Blueprint can shorten InfoCards on this very anchor.
	for (int32 Index = 0; Index < InfoCards.Num(); ++Index)
	{
		if (InfoCards[Index].bEnabled && InfoCards[Index].CardType == ECardType::AlwaysOn)
		{
			// ShowCardAtIndex already checks CardClass, suppression, and
			// IsSlotActive — nothing further to gate here.
			ShowCardAtIndex(Index);
		}
	}
}

void UInfoCardAnchor::TriggerHoverEntries(EInfoCardHoverTriggerSource Source)
{
	if (!GetWorld())
	{
		return;
	}

	EnsureRuntimeStatesSize();

	for (int32 Index = 0; Index < InfoCards.Num(); ++Index)
	{
		TriggerHoverEntryAtIndex(Index, Source);
	}
}

void UInfoCardAnchor::TriggerHoverEntryAtIndex(int32 Index, EInfoCardHoverTriggerSource Source)
{
	if (!GetWorld())
	{
		return;
	}

	// Self-contained rather than trusting the caller to have sized
	// RuntimeStates already: TriggerHoverEntries does this once before its
	// loop, but RetriggerHoverCardById calls straight in for a single Index
	// with no such guarantee.
	EnsureRuntimeStatesSize();

	if (!InfoCards.IsValidIndex(Index) || !RuntimeStates.IsValidIndex(Index))
	{
		return;
	}

	const FInfoCardSpec& Spec = InfoCards[Index];
	if (!Spec.bEnabled || Spec.CardType != ECardType::Hover)
	{
		return;
	}

	// This entry has to actually opt into whichever input raised this
	// call — a mouse-only entry (bTriggerOnFocus off) shouldn't react
	// just because a gamepad happened to focus this anchor, and vice
	// versa.
	const bool bSourceAllowed = (Source == EInfoCardHoverTriggerSource::Focus)
		? Spec.bTriggerOnFocus
		: Spec.bTriggerOnMouseHover;
	if (!bSourceAllowed)
	{
		return;
	}

	// Each entry's own SuccessiveHoverWindow (which may itself come from
	// a project preset or be Custom) decides how "successive" has to
	// feel for THIS entry — resolved fresh per-entry rather than reading
	// one fixed project value, since two entries could reasonably want
	// different windows.
	// Before the delay, so this is the sound of the interaction rather than of the card. Everything that could
	// still stop the card appearing (suppression, a re-entrant close) is checked later, so a Triggered sound
	// can be heard for a card that never actually shows - which is the right way round for a click noise.
	PlayCardSound(Index, EInfoCardSoundEvent::Triggered);

	const float SuccessiveWindow = Spec.SuccessiveHoverWindow.Resolve(EInfoCardConfigurableFloatKind::SuccessiveHoverWindow);
	const float EffectiveHoverDelay = ShouldSkipHoverDelay(SuccessiveWindow)
		? 0.f
		: Spec.HoverDelay.Resolve(EInfoCardConfigurableFloatKind::HoverDelay);

	if (EffectiveHoverDelay <= KINDA_SMALL_NUMBER)
	{
		ShowCardAtIndex(Index);
		return;
	}

	// Re-entering (by mouse OR by focus) while a delay from a previous
	// entry is still pending restarts the clock: the old one is removed
	// before the new one is added, so there is never more than one pending
	// delay per slot. A currently-suppressed entry still gets a delay here —
	// the elapsed handler re-checks suppression via ShowCardAtIndex, so this
	// stays harmless and the entry picks up automatically the moment it's
	// un-suppressed within the delay window.
	//
	// Core ticker, not FTimerManager, so the delay keeps counting down while
	// the game is paused — see the note at the top of this file. Weak
	// lambda for the same reason as RequestShowAlwaysOnCards; returning false
	// makes it one-shot.
	ClearHoverDelayAtIndex(Index);

	RuntimeStates[Index].HoverDelayHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(this, [this, Index](float /*DeltaTime*/)
		{
			HandleHoverDelayElapsedForIndex(Index);
			return false;
		}),
		EffectiveHoverDelay);
}

void UInfoCardAnchor::UntriggerHoverEntries(EInfoCardHoverTriggerSource Source)
{
	EnsureRuntimeStatesSize();

	for (int32 Index = 0; Index < InfoCards.Num(); ++Index)
	{
		const FInfoCardSpec& Spec = InfoCards[Index];
		if (Spec.CardType != ECardType::Hover)
		{
			continue;
		}

		// Symmetric with TriggerHoverEntries: only close an entry on
		// mouse-leave if the mouse was allowed to have opened it in the
		// first place (and likewise for focus). Note this is a simple,
		// per-source model, not full dual-state tracking: if an entry
		// allows both sources and is currently showing because of focus,
		// the mouse leaving a widget it's also hovering will still close
		// it. That's an acceptable trade-off for how rarely mouse and
		// gamepad/keyboard are used on the same widget at the same time;
		// most games effectively use one input modality at a time anyway.
		const bool bSourceAllowed = (Source == EInfoCardHoverTriggerSource::Focus)
			? Spec.bTriggerOnFocus
			: Spec.bTriggerOnMouseHover;
		if (!bSourceAllowed)
		{
			continue;
		}

		// A card configured this way manages its own closing (see
		// Uinfocardwidget's per-tick cursor check) once the cursor
		// actually leaves both the icon and the card — the anchor's own
		// Border losing mouse-over is exactly the moment the cursor is
		// expected to be travelling toward the card, not a reason to close
		// it. Mouse-only: a Focus source still closes immediately below,
		// since losing gamepad/keyboard focus is a discrete event with no
		// cursor position to wait on.
		//
		// Only the CLOSE is skipped, and only while a card is genuinely on
		// screen to hand the decision over to. Skipping the whole iteration
		// used to leave the pending hover-delay timer running, so brushing
		// across the anchor armed a card that then popped up a third of a
		// second after the cursor had gone — and immediately closed itself
		// again once its own cursor check ran. That is the flicker.
		const bool bCardIsUp = IsSlotActive(Index);

		// A pinned card ignores its trigger going away, from either source.
		// NotifyCardUnpinned closes it later if the trigger is still gone once
		// the pin is released. The delay timer is cleared for the same reason
		// as the keep-open branch below: the slot is occupied, so a pending
		// timer could only ever re-fire into it.
		if (bCardIsUp && IsSlotPinned(Index))
		{
			ClearHoverDelayAtIndex(Index);
			continue;
		}

		if (Source == EInfoCardHoverTriggerSource::Mouse && Spec.bKeepOpenUntilCursorLeaves && bCardIsUp)
		{
			// Cancel the delay timer anyway: a card is already showing, so a
			// timer still pending for this entry can only ever re-fire into
			// an already-occupied slot.
			ClearHoverDelayAtIndex(Index);
			continue;
		}

		HideCardAtIndex(Index);
	}
}

bool UInfoCardAnchor::ShouldSkipHoverDelay(float SuccessiveWindowSeconds)
{
	const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
	if (!Settings || !Settings->bEnableSuccessiveHover)
	{
		return false;
	}

	if (LastCardClosedRealTimeSeconds < 0.0)
	{
		return false; // no card has ever closed this process — nothing to be "successive" to
	}

	return (FPlatformTime::Seconds() - LastCardClosedRealTimeSeconds) <= SuccessiveWindowSeconds;
}

void UInfoCardAnchor::RecordCardClosed()
{
	LastCardClosedRealTimeSeconds = FPlatformTime::Seconds();
}


// =================================================================================================
// Runtime-state bookkeeping
// =================================================================================================

void UInfoCardAnchor::EnsureRuntimeStatesSize()
{
	if (RuntimeStates.Num() == InfoCards.Num())
	{
		return;
	}

	// Closing a card below broadcasts OnCardHidden, which can run Blueprint
	// that calls back into this anchor — and every public entry point here
	// starts by calling this function. The array isn't resized until the very
	// end, so a re-entrant call would find the same mismatch and close the
	// same leftover entries a second time.
	if (bIsEnsuringRuntimeStates)
	{
		return;
	}

	TGuardValue<bool> ReentryGuard(bIsEnsuringRuntimeStates, true);

	// Close and clear anything left over from entries that were removed from
	// InfoCards (e.g. shrinking the array in the editor).
	for (int32 Index = InfoCards.Num(); Index < RuntimeStates.Num(); ++Index)
	{
		ClearHoverDelayAtIndex(Index);

		UInfoCardWidget* CardToClose = RuntimeStates[Index].ActiveCard;
		if (!CardToClose)
		{
			continue;
		}

		CardToClose->CloseCard();

		// RuntimeStates can have been resized by the close above (a bound
		// OnCardHidden handler reaching back into this anchor), so the index
		// is re-validated rather than assumed still good.
		if (!RuntimeStates.IsValidIndex(Index) || RuntimeStates[Index].ActiveCard != CardToClose)
		{
			continue; // same idempotency guard as HideCardAtIndex — see its comment
		}

		// Read out and cleared BEFORE the broadcast. A handler bound to
		// OnCardHidden can resize RuntimeStates, and writing
		// RuntimeStates[Index].ActiveCardId afterward would then be writing
		// through a reallocated array.
		const FName ClosedCardId = RuntimeStates[Index].ActiveCardId;
		RuntimeStates[Index].ActiveCard   = nullptr;
		RuntimeStates[Index].ActiveCardId = NAME_None;

		RecordCardClosed();
		OnCardHidden.Broadcast(ClosedCardId);
	}

	RuntimeStates.SetNum(InfoCards.Num());

	// Shrinking may have removed the last active card, so the registry needs
	// to hear about it.
	UpdateRegistryMembership();
}

void UInfoCardAnchor::PruneRegistry()
{
	ActiveCardAnchors.RemoveAllSwap([](const TWeakObjectPtr<UInfoCardAnchor>& Weak) { return !Weak.IsValid(); });
}

void UInfoCardAnchor::UpdateRegistryMembership()
{
	PruneRegistry();

	const bool bHasActiveCard = RuntimeStates.ContainsByPredicate(
		[](const FInfoCardRuntimeState& State) { return State.ActiveCard != nullptr; });

	if (bHasActiveCard)
	{
		ActiveCardAnchors.AddUnique(this);
	}
	else
	{
		ActiveCardAnchors.RemoveSwap(TWeakObjectPtr<UInfoCardAnchor>(this));
	}
}

bool UInfoCardAnchor::IsAnyCardShowing()
{
	// ActiveCardAnchors only ever contains anchors with at least one active
	// card (see UpdateRegistryMembership), so membership alone answers this
	// — no need to walk into RuntimeStates on each entry.
	PruneRegistry();
	return ActiveCardAnchors.Num() > 0;
}

bool UInfoCardAnchor::WasDismissClickConsumedThisFrame()
{
	return DismissClickConsumedFrame == GFrameCounter;
}

void UInfoCardAnchor::MarkDismissClickConsumed()
{
	DismissClickConsumedFrame = GFrameCounter;
}

void UInfoCardAnchor::ApplyCloseOthersScope(int32 SourceIndex) const
{
	if (!InfoCards.IsValidIndex(SourceIndex))
	{
		return;
	}

	const ECardCloseOthersScope Scope = InfoCards[SourceIndex].CloseOtherCardsScope;
	if (Scope == ECardCloseOthersScope::None)
	{
		return;
	}

	const UUserWidget* MySourceWidget = GetCardSourceWidget();

	// If this anchor lives inside another InfoCard — a submenu row, in a
	// recursively-nested context menu built from the same Widget Blueprint
	// at every depth — that containing card, and everything IT is in turn
	// nested inside of, must never be closed by this scope. Every level of
	// such a menu shares one source widget class, so from three levels deep
	// onward there IS another anchor of "the same class" currently showing
	// a card — except that anchor isn't a sibling, it's the very ancestor
	// whose card this one is anchored inside. Closing it collapses the
	// chain this card depends on to exist at all, which is exactly the "the
	// new submenu closes itself" bug this guards against.
	//
	// Resolved through Uinfocardwidget::FindContainingCard rather than by
	// casting MySourceWidget. MySourceWidget is only the NEAREST UserWidget
	// around this anchor, and for a menu row built as its own widget (a
	// W_MenuRow placed inside the card, or created at runtime and added to it)
	// that's the row, not the card, so the ancestor guard below silently never
	// ran and a nested dropdown could close the very menu it lives in.
	const UInfoCardWidget* MyContainingCard = UInfoCardWidget::FindContainingCard(this);

	// Copy first: HideUnpinnedCards() below mutates ActiveCardAnchors via the
	// entries being removed, so iterating the live array while shrinking it
	// is unsafe.
	TArray<TWeakObjectPtr<UInfoCardAnchor>> Snapshot = ActiveCardAnchors;

	for (const TWeakObjectPtr<UInfoCardAnchor>& WeakAnchor : Snapshot)
	{
		UInfoCardAnchor* OtherAnchor = WeakAnchor.Get();
		if (!OtherAnchor || OtherAnchor == this)
		{
			// Never touch this anchor's own other entries — coexisting is
			// the entire point of having more than one InfoCards entry.
			continue;
		}

		if (MyContainingCard)
		{
			bool bOtherIsAncestor = false;
			for (const FInfoCardRuntimeState& OtherState : OtherAnchor->RuntimeStates)
			{
				const UInfoCardWidget* OtherActiveCard = OtherState.ActiveCard;
				if (!OtherActiveCard)
				{
					continue;
				}

				// Equality catches the immediate parent (OtherAnchor is
				// literally the anchor that spawned the card this anchor
				// lives inside); IsAncestorCard catches every level above
				// that, so a submenu any number of levels deep still
				// recognizes every one of its ancestors, not just its
				// direct parent.
				if (OtherActiveCard == MyContainingCard || MyContainingCard->IsAncestorCard(OtherActiveCard))
				{
					bOtherIsAncestor = true;
					break;
				}
			}

			if (bOtherIsAncestor)
			{
				continue;
			}
		}

		// HideUnpinnedCards rather than HideAllCards: making room for this
		// card is an automatic close, and a pinned card holds against those.
		if (Scope == ECardCloseOthersScope::AllCards)
		{
			OtherAnchor->HideUnpinnedCards();
			continue;
		}

		// SameSourceWidgetClass: compare the *containing* widget's class
		// (e.g. W_InventorySlot_C), not the anchor's own class, which is
		// always UInfoCardAnchor regardless of where it's placed.
		const UUserWidget* OtherSourceWidget = OtherAnchor->GetCardSourceWidget();
		if (MySourceWidget && OtherSourceWidget && OtherSourceWidget->GetClass() == MySourceWidget->GetClass())
		{
			OtherAnchor->HideUnpinnedCards();
		}
	}
}

bool UInfoCardAnchor::IsSlotActive(int32 Index)
{
	if (!RuntimeStates.IsValidIndex(Index))
	{
		return false;
	}

	FInfoCardRuntimeState& State = RuntimeStates[Index];
	UInfoCardWidget* Card = State.ActiveCard;

	if (!Card)
	{
		return false;
	}

	// IsInViewport is the load-bearing part of this test, not IsClosing.
	// IsClosing only ever becomes true inside CloseCard (it's just
	// bCloseRequested, which nothing else sets), so a card that left the
	// screen by ANY other route — RemoveFromParent from Blueprint, a HUD or
	// level teardown, an out-animation path that removed the widget without
	// the close flag having been set — is still a perfectly valid, "not
	// closing" UObject as far as this anchor can tell. That is precisely the
	// state that wedges this slot: the pointer is non-null and healthy, so
	// the anchor keeps believing a card is up, while the player is looking at
	// an empty screen. Asking "is it actually on screen" instead of "did it
	// tell us it was closing" makes this robust to every removal path rather
	// than only the one that remembers to report itself.
	// HasBeenConstructed is what makes the IsInViewport test safe to apply.
	// ShowCardAtIndex deliberately fills this slot BEFORE calling
	// AddToViewport (so a card that closes itself during NativeConstruct has
	// a slot to clear), which means there is a real window where the card is
	// alive, correctly tracked, and not yet in the viewport. Without this
	// check, anything that asked about the slot during that window — and a
	// nested Always On card spawning inside its host card's construction does
	// exactly that — would declare a perfectly healthy card stale and clear
	// it out from under itself.
	const bool bLeftViewport = Card->HasBeenConstructed() && !Card->IsInViewport();

	if (!IsValid(Card) || Card->IsClosing() || bLeftViewport)
	{
		// Stale: this card is gone (or on its way out) but its
		// NotifyCardSelfClosed never reached us. Do the cleanup that
		// notification would have done, right now, instead of leaving this
		// slot stuck "occupied" forever (which is what stops a hover entry
		// from ever showing again) or for one extra call (which is what
		// makes a click entry take two presses — the first press is spent
		// "closing" a card that is already gone).
		State.ActiveCard = nullptr;
		State.ActiveCardId = NAME_None;

		ClearHoverDelayAtIndex(Index);

		UpdateRegistryMembership();
		return false;
	}

	return true;
}

bool UInfoCardAnchor::IsCardSuppressedAtIndex(int32 Index) const
{
	if (ActiveSuppressionTags.IsEmpty() || !InfoCards.IsValidIndex(Index))
	{
		return false;
	}

	// Per-entry suppression: only this InfoCards[Index] is affected.
	if (InfoCards[Index].SuppressionTags.HasAny(ActiveSuppressionTags))
	{
		return true;
	}

	// Data-asset-wide suppression: every entry sourced from InfoCards_DA is
	// affected, regardless of that entry's own SuppressionTags. Only
	// meaningful in UseDataAsset mode — a lingering InfoCards_DA reference
	// left over from before a switch to Custom shouldn't reach in and
	// suppress cards it no longer actually sources.
	if (SourceMode == EInfoCardSourceMode::UseDataAsset && IsValid(InfoCards_DA) && InfoCards_DA->AnchorSuppressionTags.HasAny(ActiveSuppressionTags))
	{
		return true;
	}

	return false;
}


// =================================================================================================
// Data asset sync / per-instance defaults
// =================================================================================================

namespace InfoCardPayloadDefaultFields
{
	/** A field a designer can set on a payload in the Details panel. Transient fields (e.g. hold progress) never count. */
	bool IsOverridable(const FProperty* Property)
	{
		return Property
			&& Property->HasAnyPropertyFlags(CPF_Edit)
			&& !Property->HasAnyPropertyFlags(CPF_Transient);
	}

	/**
	 * Whether one field holds the same value on two payloads of the same class.
	 * Text compares by its displayed string: an FText also carries a
	 * localization key, and the Details panel issues a new one on every edit, so
	 * typing a value back to what it was would otherwise still count as changed.
	 */
	bool AreEqual(const FProperty* Property, const UObject* A, const UObject* B)
	{
		const FTextProperty* TextProperty = CastField<FTextProperty>(Property);

		for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
		{
			if (TextProperty)
			{
				const FText TextA = TextProperty->GetPropertyValue_InContainer(A, ArrayIndex);
				const FText TextB = TextProperty->GetPropertyValue_InContainer(B, ArrayIndex);
				if (!TextA.ToString().Equals(TextB.ToString(), ESearchCase::CaseSensitive))
				{
					return false;
				}
			}
			else if (!Property->Identical_InContainer(A, B, ArrayIndex, PPF_DeepComparison))
			{
				return false;
			}
		}

		return true;
	}

	bool HasAnyChangedField(const UObject* Payload, const UObject* Reference)
	{
		for (TFieldIterator<FProperty> It(Payload->GetClass()); It; ++It)
		{
			if (IsOverridable(*It) && !AreEqual(*It, Payload, Reference))
			{
				return true;
			}
		}
		return false;
	}

	UNameTBD_Payload* FindOfClass(const TArray<TObjectPtr<UNameTBD_Payload>>& Payloads, const UClass* PayloadClass)
	{
		for (const TObjectPtr<UNameTBD_Payload>& Payload : Payloads)
		{
			if (Payload && Payload->GetClass() == PayloadClass)
			{
				return Payload.Get();
			}
		}
		return nullptr;
	}
}

void UInfoCardAnchor::RebuildInfoCardsFromDataAsset()
{
	if (!IsValid(InfoCards_DA))
	{
		return;
	}

	// NOT a plain "InfoCards = InfoCards_DA->InfoCards;" — that copies
	// FInfoCardSpec::Payloads by pointer, not by value. Every InfoCardAnchor
	// in the game would then reference the SAME payload UObjects the data
	// asset itself owns, so any one slot's InitSlot mutating a payload
	// silently overwrites what every other slot using the same InfoCards_DA
	// had just written — whichever slot's InitSlot runs last "wins" for the
	// entire session. Each anchor needs its own duplicated payload instances
	// instead.
	InfoCards.Reset(InfoCards_DA->InfoCards.Num());

	for (const FInfoCardSpec& SourceSpec : InfoCards_DA->InfoCards)
	{
		InfoCards.Add(MakeSpecFromDataAsset(*InfoCards_DA, SourceSpec, this));
	}

	// Layer this anchor's own defaults/overrides on top of what the Data
	// Asset just gave us, before anything else (suppression checks, first
	// paint, etc.) can read InfoCards.
	ApplyPayloadDefaults();
	ApplySettingsOverrides();
	ApplyAdditiveAnimationOverrides();
	ApplyCardTextOverrides();

	bInfoCardsSynced = true;
}

FInfoCardSpec UInfoCardAnchor::MakeSpecFromDataAsset(const UInfoCardDataAsset& Asset, const FInfoCardSpec& SourceSpec, UObject* PayloadOuter)
{
	FInfoCardSpec NewSpec = SourceSpec;
	NewSpec.Payloads.Reset(SourceSpec.Payloads.Num());

	for (const TObjectPtr<UNameTBD_Payload>& SourcePayload : SourceSpec.Payloads)
	{
		NewSpec.Payloads.Add(SourcePayload ? DuplicateObject<UNameTBD_Payload>(SourcePayload, PayloadOuter) : nullptr);
	}

	// Fallback, not a merge: an entry that authored its own sounds meant those and not the asset's. Done
	// here rather than at play time so everything downstream - the overrides, the card itself - only
	// ever has one array to read.
	if (NewSpec.Sounds.IsEmpty())
	{
		NewSpec.Sounds = Asset.DefaultSounds;
	}

	// Same fallback-not-merge rule as the sounds above, for the same reason: an entry that authored its
	// own animations meant those and not the asset's.
	if (NewSpec.AdditiveAnimations.IsEmpty())
	{
		NewSpec.AdditiveAnimations = Asset.DefaultCardAnimations;
	}

	return NewSpec;
}

void UInfoCardAnchor::SetCardText(FName CardId, FText Title, FText Body)
{
	FInfoCardTextOverride& Override = CardTextOverrides.FindOrAdd(CardId);
	Override.Title = Title;
	Override.Body = Body;

	const int32 Index = FindCardIndexById(CardId);
	if (InfoCards.IsValidIndex(Index))
	{
		ApplyCardTextOverrides();
		RefreshCard(CardId);
	}

	// The designer's card shows it too: a Widget Blueprint wrapping an anchor sets its text from Pre Construct.
	RefreshDesignerPreviewCard();
}

void UInfoCardAnchor::ClearCardText(FName CardId)
{
	CardTextOverrides.Remove(CardId);
}

void UInfoCardAnchor::ApplyCardTextOverrides()
{
	for (const TPair<FName, FInfoCardTextOverride>& Pair : CardTextOverrides)
	{
		const int32 Index = FindCardIndexById(Pair.Key);
		if (!InfoCards.IsValidIndex(Index))
		{
			continue;
		}

		FInfoCardSpec& Spec = InfoCards[Index];
		UInfoCardPayload_Text* TextPayload = nullptr;
		for (const TObjectPtr<UNameTBD_Payload>& Payload : Spec.Payloads)
		{
			TextPayload = Cast<UInfoCardPayload_Text>(Payload.Get());
			if (TextPayload)
			{
				break;
			}
		}

		// A card with no text payload yet gets one: Set Card Text is meant to work on any card, not only on ones whose
		// Data Asset happened to author one.
		if (!TextPayload)
		{
			TextPayload = NewObject<UInfoCardPayload_Text>(this);
			Spec.Payloads.Add(TextPayload);
		}

		TextPayload->Title = Pair.Value.Title;
		TextPayload->Body = Pair.Value.Body;
	}
}

void UInfoCardAnchor::RebuildInfoCardsFromCustomArray()
{
	// Mirrors RebuildInfoCardsFromDataAsset's payload-duplication step for
	// the same reason, even though nothing else could be sharing
	// CustomInfoCards' payload UObjects the way multiple anchors can share
	// one InfoCards_DA: without it, InfoCards[Index].Payloads would point at
	// the exact same instances CustomInfoCards[Index].Payloads does, and a
	// payload mutated at runtime (GetOrAddPayload, SetPayloadsAtIndex, a
	// live edit here while the card it belongs to is showing) would reach
	// back and rewrite what's saved on this anchor's own Details panel
	// instead of a private runtime copy.
	InfoCards.Reset(CustomInfoCards.Num());

	for (const FInfoCardSpec& SourceSpec : CustomInfoCards)
	{
		FInfoCardSpec NewSpec = SourceSpec;
		NewSpec.Payloads.Reset(SourceSpec.Payloads.Num());

		for (const TObjectPtr<UNameTBD_Payload>& SourcePayload : SourceSpec.Payloads)
		{
			NewSpec.Payloads.Add(SourcePayload ? DuplicateObject<UNameTBD_Payload>(SourcePayload, this) : nullptr);
		}

		InfoCards.Add(MoveTemp(NewSpec));
	}

	// No ApplyPayloadDefaults/ApplySettingsOverrides/ApplyAdditiveAnimationOverrides step: those exist to
	// layer THIS anchor's tweaks on top of a SEPARATE, shared source
	// (InfoCards_DA). CustomInfoCards is already this anchor's own copy —
	// there is nothing else for a default or override to be relative to.

	ApplyCardTextOverrides();
	bInfoCardsSynced = true;
}

void UInfoCardAnchor::RebuildGeneratedInfoCards()
{
	TArray<FInfoCardSpec> Generated;
	if (!BuildGeneratedInfoCards(Generated))
	{
		return;
	}

	InfoCards = MoveTemp(Generated);
	ApplyCardTextOverrides();
	bInfoCardsSynced = true;

	if (IsDesignTime())
	{
		return;
	}

	EnsureRuntimeStatesSize();

	// A card already on screen shows the new values at once rather than on its next opening; one whose entry went away or
	// was switched off closes. Indexed and re-bounds-checked: SetPayloads runs Blueprint (Apply Payloads).
	for (int32 Index = 0; Index < InfoCards.Num(); ++Index)
	{
		if (!RuntimeStates.IsValidIndex(Index))
		{
			break;
		}

		UInfoCardWidget* LiveCard = RuntimeStates[Index].ActiveCard;
		if (!LiveCard || LiveCard->IsClosing())
		{
			continue;
		}

		if (!InfoCards[Index].bEnabled || RuntimeStates[Index].ActiveCardId != InfoCards[Index].CardId)
		{
			HideCardAtIndex(Index);
			continue;
		}

		RefreshCard(InfoCards[Index].CardId);
	}
}

void UInfoCardAnchor::ApplyPayloadDefaults()
{
	using namespace InfoCardPayloadDefaultFields;

	for (const FInfoCardPayloadDefault& Default : PayloadDefaults)
	{
		if (Default.CardId == NAME_None || !Default.Payload)
		{
			continue;
		}

		const int32 Index = FindCardIndexById(Default.CardId);
		if (!InfoCards.IsValidIndex(Index))
		{
			// This entry doesn't correspond to anything on the currently
			// assigned InfoCards_DA (wrong CardId, or the DA was swapped) —
			// nothing to apply it to.
			continue;
		}

		FInfoCardSpec& Spec = InfoCards[Index];
		UClass* PayloadClass = Default.Payload->GetClass();

		// What "unchanged" is measured against. Normally Baseline: the Data
		// Asset's value when this entry was last synced, so a field the
		// designer never touched keeps following the Data Asset even if the
		// Data Asset has been edited since. An entry with no usable Baseline
		// (converted from the old layout, not synced yet) falls back to the
		// Data Asset's current payload instead.
		const UNameTBD_Payload* Baseline = (Default.Baseline && Default.Baseline->GetClass() == PayloadClass)
			? Default.Baseline.Get()
			: nullptr;

		UNameTBD_Payload* Target = FindOfClass(Spec.Payloads, PayloadClass);
		if (!Target)
		{
			// The Data Asset has no payload of this class for this card
			// (anchor saved against an older version of the asset). Add this
			// one only if the designer actually changed something on it.
			const UObject* Reference = Baseline ? static_cast<const UObject*>(Baseline) : PayloadClass->GetDefaultObject();
			if (HasAnyChangedField(Default.Payload, Reference))
			{
				// Each anchor needs its own instance, same reasoning as the
				// Data Asset's own payloads in RebuildInfoCardsFromDataAsset.
				Spec.Payloads.Add(DuplicateObject<UNameTBD_Payload>(Default.Payload, this));
			}
			continue;
		}

		const UObject* Reference = Baseline ? static_cast<const UObject*>(Baseline) : static_cast<const UObject*>(Target);

		// Field by field: only what was changed is copied onto this anchor's
		// runtime copy of the Data Asset's payload.
		for (TFieldIterator<FProperty> It(PayloadClass); It; ++It)
		{
			const FProperty* Property = *It;
			if (!IsOverridable(Property) || AreEqual(Property, Default.Payload, Reference))
			{
				continue;
			}

			Property->CopyCompleteValue_InContainer(Target, Default.Payload);
		}
	}
}

void UInfoCardAnchor::ApplySettingsOverrides()
{
	for (const FInfoCardSettingsOverrideElement& Element : SettingsOverrides)
	{
		if (Element.CardId == NAME_None)
		{
			continue;
		}

		const int32 Index = FindCardIndexById(Element.CardId);
		if (!InfoCards.IsValidIndex(Index))
		{
			// No InfoCards entry for this CardId on the currently assigned
			// InfoCards_DA — nothing to apply these overrides to.
			continue;
		}

		FInfoCardSpec& Spec = InfoCards[Index];
		const FInfoCardSettingsOverride& Settings = Element.Settings;

		if (Settings.bDisableCard)
		{
			// Equivalent to SetCardEnabledById(CardId, false) at sync time.
			// None of the other override groups matter for a card that can't
			// show, so skip straight past them.
			Spec.bEnabled = false;
			continue;
		}

		if (Settings.bOverrideCardType)
		{
			Spec.CardType = Settings.CardType;
			Spec.HoverDelay = Settings.HoverDelay;
			Spec.SuccessiveHoverWindow = Settings.SuccessiveHoverWindow;
			Spec.bTriggerOnMouseHover = Settings.bTriggerOnMouseHover;
			Spec.bTriggerOnFocus = Settings.bTriggerOnFocus;
			Spec.bReappearOnClickDismiss = Settings.bReappearOnClickDismiss;
			Spec.bKeepOpenUntilCursorLeaves = Settings.bKeepOpenUntilCursorLeaves;
			Spec.KeepOpenSlack = Settings.KeepOpenSlack;
			Spec.KeepOpenGracePeriod = Settings.KeepOpenGracePeriod;
		}

		if (Settings.bOverrideCardPosition)
		{
			Spec.PositionMode = Settings.PositionMode;
			Spec.IconPosition = Settings.IconPosition;
			Spec.FillIconPadding = Settings.FillIconPadding;
			Spec.CursorPosition = Settings.CursorPosition;
			Spec.ViewportPosition = Settings.ViewportPosition;
			Spec.bClampToViewport = Settings.bClampToViewport;
			Spec.FlipToAvoidClipping = Settings.FlipToAvoidClipping;
			Spec.ClampPadding = Settings.ClampPadding;
			Spec.bAvoidOverlappingCards = Settings.bAvoidOverlappingCards;
		}

		if (Settings.bOverrideOffset)
		{
			Spec.CardOffset = Settings.Offset;
			Spec.IconAnchorPadding = Settings.HoverAnchorPadding;
		}

		if (Settings.bOverrideTickUpdate)
		{
			Spec.bUpdatePositionOnTick = Settings.bUpdatePositionOnTick;
		}

		if (Settings.bOverrideDismissal)
		{
			Spec.RemoveCardOnPressing = Settings.RemoveCardOnPressing;
			Spec.AutoDismissDuration = Settings.AutoDismissDuration;
			Spec.DismissClickGetsConsumed = Settings.DismissClickGetsConsumed;
			Spec.bIgnoresMenuStateChange = Settings.bIgnoresMenuStateChange;
		}

		if (Settings.bOverrideSounds)
		{
			// Replaces rather than merges - see FInfoCardSettingsOverride::bOverrideSounds for why. An empty
			// array here is a deliberate "no sounds on this anchor", not a missing value to fall back from.
			Spec.Sounds = Settings.Sounds;
		}

		if (Settings.bOverrideCardTransform)
		{
			Spec.CardTransform = Settings.CardTransform;
		}
	}
}

void UInfoCardAnchor::ApplyAdditiveAnimationOverrides()
{
	for (const FInfoCardAdditiveAnimationsOverride& Override : AdditiveAnimationOverrides)
	{
		if (!Override.bOverride || Override.CardId == NAME_None)
		{
			continue;
		}

		const int32 Index = FindCardIndexById(Override.CardId);
		if (!InfoCards.IsValidIndex(Index))
		{
			continue;
		}

		// Replaces the Data Asset's additive set for this card. Still additive
		// with respect to the card class: UInfoCardWidget::ApplyAdditiveAnimations
		// appends these after the class's own Anim In / Anim Out.
		InfoCards[Index].AdditiveAnimations = Override.AdditiveAnimations;
	}
}


// =================================================================================================
// Designer preview
// =================================================================================================

TArray<FString> UInfoCardAnchor::GetInfoCardIdOptions() const
{
	TArray<FString> Options;

	// First, so there is a way back to "preview nothing" once a card has been picked.
	Options.Add(TEXT("None"));

	for (const FName Id : GetInfoCardIds())
	{
		Options.AddUnique(Id.ToString());
	}

	return Options;
}

TArray<FName> UInfoCardAnchor::GetInfoCardIds() const
{
	TArray<FName> Ids;
	for (const FInfoCardSpec& Spec : InfoCards)
	{
		if (Spec.CardId != NAME_None)
		{
			Ids.AddUnique(Spec.CardId);
		}
	}
	return Ids;
}

FName UInfoCardAnchor::GetDesignerPreviewCard() const
{
	return bHasDesignerPreviewCardPick ? DesignerPreviewCardPick : DesignerPreviewCardId;
}

void UInfoCardAnchor::SetDesignerPreviewCardPick(FName CardId)
{
	if (!IsDesignTime())
	{
		return;
	}

	DesignerPreviewCardPick = CardId;
	bHasDesignerPreviewCardPick = true;
	RefreshDesignerPreviewCard();
	UWidgetAnimDesignerPreview::Synchronize(this);
}

void UInfoCardAnchor::ClearDesignerPreviewCardPick()
{
	if (!bHasDesignerPreviewCardPick)
	{
		return;
	}

	bHasDesignerPreviewCardPick = false;
	DesignerPreviewCardPick = NAME_None;
	RefreshDesignerPreviewCard();
	UWidgetAnimDesignerPreview::Synchronize(this);
}

void UInfoCardAnchor::PlayDesignerCardAnimation(bool bOut)
{
	if (!IsDesignTime() || !HasDesignerPreviewAnimations())
	{
		return;
	}

	// Once, from the start, the way the button toolbar plays a button's: the harness restarts on a changed mode, so a replay
	// of the same one goes through Off first. Only this designer copy's mode changes; the template's stays Off.
	const EWidgetAnimPreviewMode Mode = bOut ? EWidgetAnimPreviewMode::PlayOut : EWidgetAnimPreviewMode::PlayIn;
	if (DesignerAnimPreview.Mode == Mode)
	{
		DesignerAnimPreview.Mode = EWidgetAnimPreviewMode::Off;
		UWidgetAnimDesignerPreview::Synchronize(this);
	}
	DesignerAnimPreview.Mode = Mode;
	UWidgetAnimDesignerPreview::Synchronize(this);
}

void UInfoCardAnchor::RefreshDesignerPreviewCard()
{
#if WITH_EDITOR
	if (!IsDesignTime())
	{
		return;
	}

	const FName CardToShow = GetDesignerPreviewCard();
	const int32 Index = CardToShow.IsNone() ? INDEX_NONE : FindCardIndexById(CardToShow);
	const TSubclassOf<UInfoCardWidget> CardClass = InfoCards.IsValidIndex(Index) ? InfoCards[Index].CardClass : nullptr;

	// Templates (the Widget Blueprint's own widgets) never draw; only the designer's copy, which lives in a User Widget,
	// can own a card.
	UUserWidget* OwningUserWidget = GetTypedOuter<UUserWidget>();

	// Put the card back as authored before anything about it changes: the harness re-reads its baselines after this, and
	// would otherwise record a mid-animation pose as the card's own.
	UWidgetAnimLibrary::RestoreWidgetAnimBaselines(DesignerAnimBaselines);
	DesignerAnimBaselines.ResetBaselines();

	if (!CardClass || !OwningUserWidget || IsTemplate())
	{
		if (DesignerPreviewPanel.IsValid())
		{
			DesignerPreviewPanel->SetCard(nullptr, FInfoCardDesignerPlacement());
		}
		DesignerPreviewCard = nullptr;
		return;
	}

	if (!DesignerPreviewCard || DesignerPreviewCard->GetClass() != CardClass)
	{
		DesignerPreviewCard = CreateWidget<UInfoCardWidget>(OwningUserWidget, CardClass);
		if (!DesignerPreviewCard)
		{
			return;
		}
		DesignerPreviewCard->SetDesignerFlags(GetDesignerFlags());
	}

	// Payloads, transform and placement fields only: no timers, no press watcher, no viewport (see SetupDesignerPreview).
	DesignerPreviewCard->SetupDesignerPreview(InfoCards[Index], this);

	if (DesignerPreviewPanel.IsValid())
	{
		DesignerPreviewPanel->SetCard(DesignerPreviewCard->TakeWidget(), FInfoCardDesignerPlacement::FromSpec(InfoCards[Index]));
	}
#endif
}

UWidget* UInfoCardAnchor::GetPreviewAnimationTarget()
{
	if (DesignerPreviewCard)
	{
		return DesignerPreviewCard;
	}

	// No card to show (no Card Class, or a template): the anchor is a layout wrapper sized to its child, so animating the
	// child is the closest stand-in. An empty anchor has nothing else to offer, and animating it is harmless.
	UWidget* AnchorContent = GetContent();
	return AnchorContent ? AnchorContent : this;
}

void UInfoCardAnchor::RebuildPreviewAnimations()
{
	PreviewAnimIn.Reset();
	PreviewAnimOut.Reset();

	const FName CardToShow = GetDesignerPreviewCard();
	if (CardToShow == NAME_None)
	{
		return;
	}

	const int32 Index = FindCardIndexById(CardToShow);
	if (!InfoCards.IsValidIndex(Index))
	{
		// The selected id was renamed or the Data Asset swapped underneath it. Nothing to play, rather
		// than quietly previewing some other card.
		return;
	}

	const FInfoCardSpec& Spec = InfoCards[Index];

	// The card class's own animations first: they are the base layer, exactly as they are on a real card,
	// where the additive entries go onto the card's Widget Animation Component as extra layers on top of its
	// own Anim In / Anim Out (UInfoCardWidget::ApplyAdditiveAnimations).
	TArray<FWidgetAnimEntry> ClassAnimIn;
	TArray<FWidgetAnimEntry> ClassAnimOut;
	if (Spec.CardClass && UInfoCardWidget::GetClassAnimationEntries(Spec.CardClass, this, ClassAnimIn, ClassAnimOut))
	{
		// Mirroring resolved at the entry level, before any settings are built, so an entry marked
		// Mirror In Out contributes to both stacks - the same order the card itself would do it in.
		TArray<FWidgetAnimEntry> EntriesIn;
		TArray<FWidgetAnimEntry> EntriesOut;
		UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(ClassAnimIn, EntriesIn, EntriesOut);
		UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(ClassAnimOut, EntriesOut, EntriesIn);

		// A Target Widget names something inside the card's own tree, which the designer's card has. Without one (a
		// template), every target falls back to the stand-in the preview is animating.
		UInfoCardWidget* PreviewCard = DesignerPreviewCard;
		auto Resolver = [PreviewCard](FName TargetName) -> UWidget*
		{
			return (PreviewCard && !TargetName.IsNone()) ? PreviewCard->GetWidgetFromName(TargetName) : nullptr;
		};
		UWidgetAnimEntryLibrary::FlattenEntries(EntriesIn, Resolver, PreviewAnimIn);
		UWidgetAnimEntryLibrary::FlattenEntries(EntriesOut, Resolver, PreviewAnimOut);
	}

	// Then the additives, on top - and these are the resolved ones: ApplyAdditiveAnimationOverrides has already written
	// this anchor's Additive Animation Overrides into the spec, so what this appends is precisely what a spawned card would
	// be handed.
	Spec.AdditiveAnimations.ResolveInto(PreviewAnimIn, PreviewAnimOut);
}

void UInfoCardAnchor::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	if (UWidgetAnimDesignerPreview::ShouldRun(this))
	{
		EnsurePreviewTimer();
	}
}


// =================================================================================================
// Suppression
// =================================================================================================

void UInfoCardAnchor::AddSuppressionTag(FGameplayTag Tag)
{
	if (!Tag.IsValid() || ActiveSuppressionTags.HasTagExact(Tag))
	{
		return;
	}

	ActiveSuppressionTags.AddTag(Tag);

	EnsureRuntimeStatesSize();
	for (int32 Index = 0; Index < InfoCards.Num(); ++Index)
	{
		if (IsCardSuppressedAtIndex(Index))
		{
			HideCardAtIndex(Index);
		}
	}
}

void UInfoCardAnchor::AddSuppressionTags(const FGameplayTagContainer& Tags)
{
	if (Tags.IsEmpty() || ActiveSuppressionTags.HasAll(Tags))
	{
		return; // nothing new — every one of these is already attached
	}

	ActiveSuppressionTags.AppendTags(Tags);

	EnsureRuntimeStatesSize();
	for (int32 Index = 0; Index < InfoCards.Num(); ++Index)
	{
		if (IsCardSuppressedAtIndex(Index))
		{
			HideCardAtIndex(Index);
		}
	}
}

void UInfoCardAnchor::RemoveSuppressionTag(FGameplayTag Tag)
{
	if (!ActiveSuppressionTags.HasTagExact(Tag))
	{
		return;
	}

	ActiveSuppressionTags.RemoveTag(Tag);

	// Hover and click entries are left alone: one closed by suppression
	// rebuilds fresh from whatever's actually true the next time it's
	// naturally triggered, the same "safe default" used for the
	// inventory-display bind in Uinfocardwidget::HandleBoundInventoryChanged.
	//
	// Always On entries have no such next trigger. Suppressing one closed it
	// permanently, which made suppression a one-way door for exactly the card
	// type that most needs it to be reversible. ShowCardAtIndex re-checks
	// suppression and IsSlotActive itself, so this is a no-op for anything
	// still suppressed by another tag or already showing.
	RequestShowAlwaysOnCards();
}

void UInfoCardAnchor::RemoveSuppressionTags(const FGameplayTagContainer& Tags)
{
	if (Tags.IsEmpty())
	{
		return;
	}

	ActiveSuppressionTags.RemoveTags(Tags);

	// Same re-arm as the single-tag version above, for the same reason.
	RequestShowAlwaysOnCards();
}


// =================================================================================================
// By index
// =================================================================================================

void UInfoCardAnchor::ShowCardAtIndex(int32 Index)
{
	// Nothing spawns real card widgets into the UMG designer preview. This
	// used to be enforced only by the callers (the input handlers all bail on
	// IsDesignTime), which left every Blueprint-callable path into here
	// unguarded.
	if (IsDesignTime())
	{
		return;
	}

	EnsureRuntimeStatesSize();

	if (!InfoCards.IsValidIndex(Index) || !RuntimeStates.IsValidIndex(Index))
	{
		return;
	}

	// A card containing an anchor that shows another card is a supported,
	// normal setup — a submenu, a dropdown inside a context menu. A card
	// whose nested anchor ends up showing that same card class again is not,
	// and without a cap it recurses through CreateWidget/AddToViewport until
	// the stack runs out, which takes the whole editor down rather than
	// misbehaving visibly. Bail with a diagnostic instead.
	if (ShowCardDepth >= MaxShowCardDepth)
	{
		UE_LOG(LogInfoCard, Error,
			TEXT("InfoCardAnchor '%s': refusing to show card '%s' — nested info cards are %d deep, which almost certainly means a card contains an anchor that spawns that same card again. Check the Always On entries on any anchor inside this card's widget."),
			*GetName(), *InfoCards[Index].CardId.ToString(), ShowCardDepth);
		return;
	}

	// bEnabled: designer/runtime master switch for this entry. IsSlotActive
	// rather than a bare pointer check: a card that closed itself but lost
	// the race to notify this anchor (see IsSlotActive) would otherwise
	// leave this slot permanently "occupied," and TriggerHoverEntries has
	// no fallback the way HideCardAtIndex does — a hover entry stuck this
	// way would never show again.
	if (!InfoCards[Index].bEnabled || !InfoCards[Index].CardClass || IsSlotActive(Index))
	{
		return;
	}

	// Suppression tags currently attached to this anchor (see
	// AddSuppressionTag) can block this specific entry, or every entry
	// sourced from InfoCards_DA — see IsCardSuppressedAtIndex.
	if (IsCardSuppressedAtIndex(Index))
	{
		return;
	}

	APlayerController* OwningPlayer = GetOwningPlayer();
	if (!OwningPlayer)
	{
		return;
	}

	// Close whatever CloseOtherCardsScope says needs closing before we spawn, so
	// there's never a one-frame overlap between the old card and the new one.
	// This can re-enter the anchors' own bookkeeping, so nothing above is
	// held by reference across it.
	ApplyCloseOthersScope(Index);

	if (!InfoCards.IsValidIndex(Index) || !RuntimeStates.IsValidIndex(Index))
	{
		return; // paranoia: an exclusivity close could have torn things down
	}

	UInfoCardWidget* NewCard = CreateWidget<UInfoCardWidget>(OwningPlayer, InfoCards[Index].CardClass);
	if (!NewCard)
	{
		return;
	}

	// Scoped from here rather than the top of the function: everything above
	// is cheap bookkeeping that can't recurse. It's SetupCard/AddToViewport
	// below that build a second widget tree and can reach back into this
	// system, so that's the span the depth counter needs to cover.
	TGuardValue<int32> DepthGuard(ShowCardDepth, ShowCardDepth + 1);

	// SetupCard before AddToViewport: NativeConstruct (which plays the
	// entrance animation) runs inside AddToViewport, and it should see the
	// card's final trigger type / payloads, not the defaults.
	NewCard->SetupCard(InfoCards[Index], this);

	// Registered BEFORE AddToViewport, not after. NativeConstruct runs inside
	// that call and can close the card then and there (a menu-state change, an
	// inventory bind that resolves to nothing, a Blueprint override deciding
	// it has nothing to show). If the slot were still empty at that moment,
	// the card's NotifyCardSelfClosed would find nothing to clear — and then
	// the assignment below would drop an already-dead card into the slot and
	// wedge it permanently.
	RuntimeStates[Index].ActiveCard = NewCard;
	RuntimeStates[Index].ActiveCardId = InfoCards[Index].CardId;

	NewCard->AddToViewport(InfoCards[Index].ZOrder);

	// Did it survive being constructed? If it closed itself on the way up, the
	// slot has already been cleared by NotifyCardSelfClosed and there is
	// nothing on screen to position or announce.
	if (!RuntimeStates.IsValidIndex(Index) || RuntimeStates[Index].ActiveCard != NewCard)
	{
		UpdateRegistryMembership();
		return;
	}

	// Place it immediately so it doesn't flash at the origin for one frame.
	NewCard->UpdatePosition();

	UpdateRegistryMembership();

	// Read out before the broadcast, for the same reason the close paths do:
	// a handler bound to OnCardShown can reach back into this anchor and
	// resize RuntimeStates, and indexing it again afterward would be indexing
	// a reallocated array.
	const FName ShownCardId = RuntimeStates[Index].ActiveCardId;

	// Broadcast last, once the card is fully built, positioned, and
	// discoverable via GetActiveCardAtIndex/GetActiveCardById.
	OnCardShown.Broadcast(ShownCardId);
}

void UInfoCardAnchor::HideCardAtIndex(int32 Index)
{
	EnsureRuntimeStatesSize();

	if (!RuntimeStates.IsValidIndex(Index))
	{
		return;
	}

	ClearHoverDelayAtIndex(Index);

	UInfoCardWidget* CardToClose = RuntimeStates[Index].ActiveCard;
	if (CardToClose)
	{
		CardToClose->CloseCard();

		// Indexed rather than held as a reference across CloseCard. That call
		// runs the card's teardown, which broadcasts and can run Blueprint
		// that reaches back into this anchor and resizes RuntimeStates — at
		// which point a reference taken before it points into freed memory.
		//
		// CloseCard() may also have already cleared this slot via
		// NotifyCardSelfClosed — every card tells its anchor when it closes,
		// regardless of who initiated it. Only redo the cleanup here if that
		// didn't already happen, so OnCardHidden never double-fires (once
		// correctly, once more with a CardId already reset to NAME_None).
		if (RuntimeStates.IsValidIndex(Index) && RuntimeStates[Index].ActiveCard == CardToClose)
		{
			// Cleared before the broadcast, not after: a bound handler can
			// resize RuntimeStates, and the write-back afterward would land in
			// a reallocated array.
			const FName ClosedCardId = RuntimeStates[Index].ActiveCardId;
			RuntimeStates[Index].ActiveCard   = nullptr;
			RuntimeStates[Index].ActiveCardId = NAME_None;

			RecordCardClosed();
			OnCardHidden.Broadcast(ClosedCardId);
		}
	}

	UpdateRegistryMembership();
}

void UInfoCardAnchor::ToggleCardAtIndex(int32 Index)
{
	EnsureRuntimeStatesSize();

	// IsSlotActive, not a bare pointer test — the same reasoning the
	// click-toggle in HandleMouseButtonDown already used, and the reason it
	// was changed there. A card that left the screen without its
	// NotifyCardSelfClosed reaching this anchor leaves a non-null pointer in
	// the slot, and a bare test would spend this toggle "hiding" a card that
	// is already gone instead of showing a new one — the classic "the first
	// press does nothing" bug.
	if (IsSlotActive(Index))
	{
		HideCardAtIndex(Index);
	}
	else
	{
		ShowCardAtIndex(Index);
	}
}

void UInfoCardAnchor::SetCardEnabledAtIndex(int32 Index, bool bNewEnabled)
{
	if (!InfoCards.IsValidIndex(Index))
	{
		return;
	}

	InfoCards[Index].bEnabled = bNewEnabled;

	if (!bNewEnabled)
	{
		HideCardAtIndex(Index);
		return;
	}

	// Re-enabling a Hover/Click entry needs nothing here — the next hover or
	// press shows it. An Always On entry has no such trigger: without this it
	// would sit enabled and invisible until something else happened to call
	// ShowCardAtIndex for it, which for most projects is never.
	if (InfoCards[Index].CardType == ECardType::AlwaysOn)
	{
		RequestShowAlwaysOnCards();
	}
}

void UInfoCardAnchor::SetPayloadsAtIndex(int32 Index, const TArray<UNameTBD_Payload*>& NewPayloads)
{
	if (!InfoCards.IsValidIndex(Index))
	{
		return;
	}

	FInfoCardSpec& Spec = InfoCards[Index];
	Spec.Payloads.Reset(NewPayloads.Num());
	for (UNameTBD_Payload* PayloadObj : NewPayloads)
	{
		Spec.Payloads.Add(PayloadObj);
	}

	EnsureRuntimeStatesSize();

	if (UInfoCardWidget* ActiveCard = RuntimeStates.IsValidIndex(Index) ? RuntimeStates[Index].ActiveCard : nullptr)
	{
		if (NewPayloads.Num() > 0)
		{
			ActiveCard->SetPayloads(NewPayloads);
		}
		else
		{
			// Nothing sensible left to show (e.g. item removed mid-hover).
			HideCardAtIndex(Index);
		}
	}
}

void UInfoCardAnchor::SetPayloadAtIndex(int32 Index, UNameTBD_Payload* NewPayload)
{
	TArray<UNameTBD_Payload*> Single;
	if (NewPayload)
	{
		Single.Add(NewPayload);
	}
	SetPayloadsAtIndex(Index, Single);
}

UInfoCardWidget* UInfoCardAnchor::GetActiveCardAtIndex(int32 Index) const
{
	if (!RuntimeStates.IsValidIndex(Index))
	{
		return nullptr;
	}

	// Filtered rather than returned raw. This is const, so it can't do
	// IsSlotActive's cleanup, but it can at least avoid handing a caller a
	// card that is invalid or already on its way out — which is the one thing
	// a "give me the card that's currently showing" accessor must not do.
	UInfoCardWidget* Card = RuntimeStates[Index].ActiveCard;
	if (!IsValid(Card) || Card->IsClosing())
	{
		return nullptr;
	}

	return Card;
}


// =================================================================================================
// By CardId
// =================================================================================================

int32 UInfoCardAnchor::FindCardIndexById(FName CardId) const
{
	return InfoCards.IndexOfByPredicate([CardId](const FInfoCardSpec& Spec) { return Spec.CardId == CardId; });
}

void UInfoCardAnchor::ShowCardById(FName CardId)
{
	ShowCardAtIndex(FindCardIndexById(CardId));
}

void UInfoCardAnchor::HideCardById(FName CardId)
{
	HideCardAtIndex(FindCardIndexById(CardId));
}

void UInfoCardAnchor::ToggleCardById(FName CardId)
{
	ToggleCardAtIndex(FindCardIndexById(CardId));
}

void UInfoCardAnchor::SetCardEnabledById(FName CardId, bool bEnabled, bool bRevealCard)
{
	SetCardEnabledAtIndex(FindCardIndexById(CardId), bEnabled);
	
	if (bRevealCard)
		ShowCardAtIndex(FindCardIndexById(CardId));
}

void UInfoCardAnchor::SetPayloadsById(FName CardId, const TArray<UNameTBD_Payload*>& NewPayloads)
{
	SetPayloadsAtIndex(FindCardIndexById(CardId), NewPayloads);
}

void UInfoCardAnchor::SetPayloadById(FName CardId, UNameTBD_Payload* NewPayload)
{
	SetPayloadAtIndex(FindCardIndexById(CardId), NewPayload);
}

UNameTBD_Payload* UInfoCardAnchor::GetOrAddPayload(FName CardId, TSubclassOf<UNameTBD_Payload> PayloadClass, bool& bIsPayloadValid, FName& InputCardID)
{
	bIsPayloadValid = false;
	InputCardID = CardId;
	
	if (!PayloadClass)
	{
		return nullptr;
	}
	
	if (InfoCards.Num() == 0)
	{
		if (UsesGeneratedInfoCards())
		{
			RebuildGeneratedInfoCards();
		}
		else if (SourceMode == EInfoCardSourceMode::Custom)
		{
			if (CustomInfoCards.Num() > 0)
			{
				RebuildInfoCardsFromCustomArray();
			}
		}
		else if (IsValid(InfoCards_DA) && InfoCards_DA->InfoCards.Num() > 0)
		{
			RebuildInfoCardsFromDataAsset();
		}
	}

	const int32 Index = FindCardIndexById(CardId);

	if (!InfoCards.IsValidIndex(Index))
	{
		return nullptr;
	}

	FInfoCardSpec& Spec = InfoCards[Index];
	
	for (const TObjectPtr<UNameTBD_Payload>& Existing : Spec.Payloads)
	{
		if (Existing && Existing->IsA(PayloadClass))
		{
			bIsPayloadValid = true;
			return Existing.Get();
		}
	}
	
	UE_LOG(LogInfoCard, Error,
		TEXT("GetOrAddPayload: CardId '%s' has no authored payload of class %s. Add one to this entry's Payloads array in its Info Card data asset."),
		*CardId.ToString(), *PayloadClass->GetName());
	return nullptr;
}

void UInfoCardAnchor::RefreshCard(FName CardId)
{
	const int32 Index = FindCardIndexById(CardId);
	if (!InfoCards.IsValidIndex(Index))
	{
		return;
	}

	EnsureRuntimeStatesSize();

	UInfoCardWidget* ActiveCard = RuntimeStates.IsValidIndex(Index) ? RuntimeStates[Index].ActiveCard : nullptr;
	if (!ActiveCard)
	{
		return; // not currently shown — nothing to redraw; next open already picks up the mutated data
	}

	TArray<UNameTBD_Payload*> RawPayloads;
	RawPayloads.Reserve(InfoCards[Index].Payloads.Num());
	for (const TObjectPtr<UNameTBD_Payload>& P : InfoCards[Index].Payloads)
	{
		RawPayloads.Add(P.Get());
	}

	ActiveCard->SetPayloads(RawPayloads);
}

UInfoCardWidget* UInfoCardAnchor::GetActiveCardById(FName CardId) const
{
	return GetActiveCardAtIndex(FindCardIndexById(CardId));
}


// =================================================================================================
// Pinning
// =================================================================================================

bool UInfoCardAnchor::PinInfoCard(FName CardId)
{
	if (IsDesignTime())
	{
		return false;
	}

	const int32 Index = FindCardIndexById(CardId);
	if (!InfoCards.IsValidIndex(Index))
	{
		return false;
	}

	EnsureRuntimeStatesSize();

	// Not up yet: bring it up first. ShowCardAtIndex applies all of its usual
	// guards (bEnabled, suppression, CardClass, owning player), and a card
	// that fails any of them simply isn't there to pin.
	if (!IsSlotActive(Index))
	{
		ShowCardAtIndex(Index);
	}

	// Asked again rather than assumed: the card can close itself during its
	// own construction, and a handler bound to OnCardShown can have closed or
	// reshaped things before control came back here.
	if (!IsSlotActive(Index))
	{
		return false;
	}

	UInfoCardWidget* Card = RuntimeStates[Index].ActiveCard;
	Card->SetPinned(true);
	return Card->IsPinned();
}

void UInfoCardAnchor::UnpinInfoCard(FName CardId)
{
	const int32 Index = FindCardIndexById(CardId);
	if (!RuntimeStates.IsValidIndex(Index))
	{
		return;
	}

	// SetPinned does the rest, including calling back into
	// NotifyCardUnpinned. Keeping that the single path means a card unpinned
	// from its own Blueprint gets exactly the same treatment as one unpinned
	// from here.
	UInfoCardWidget* Card = RuntimeStates[Index].ActiveCard;
	if (IsValid(Card) && !Card->IsClosing())
	{
		Card->SetPinned(false);
	}
}

bool UInfoCardAnchor::IsInfoCardPinned(FName CardId) const
{
	return IsSlotPinned(FindCardIndexById(CardId));
}

void UInfoCardAnchor::CloseInfoCard(FName CardId)
{
	// Nothing pin-specific needed: HideCardAtIndex is an explicit close and
	// never consults the pin. The pin lives on the card instance and goes
	// away with it.
	HideCardAtIndex(FindCardIndexById(CardId));
}

bool UInfoCardAnchor::IsSlotPinned(int32 Index) const
{
	if (!RuntimeStates.IsValidIndex(Index))
	{
		return false;
	}

	const UInfoCardWidget* Card = RuntimeStates[Index].ActiveCard;
	return IsValid(Card) && !Card->IsClosing() && Card->IsPinned();
}

void UInfoCardAnchor::HideUnpinnedCards()
{
	EnsureRuntimeStatesSize();

	// Indexed and re-bounded every iteration, same as HideAllCards: closing a
	// card broadcasts OnCardHidden, which can reach back into this anchor.
	for (int32 Index = 0; Index < RuntimeStates.Num(); ++Index)
	{
		if (IsSlotPinned(Index))
		{
			continue;
		}

		HideCardAtIndex(Index);
	}
}

void UInfoCardAnchor::NotifyCardUnpinned(UInfoCardWidget* UnpinnedCard)
{
	if (!UnpinnedCard || IsDesignTime())
	{
		return;
	}

	EnsureRuntimeStatesSize();

	const int32 Index = RuntimeStates.IndexOfByPredicate(
		[UnpinnedCard](const FInfoCardRuntimeState& State) { return State.ActiveCard == UnpinnedCard; });

	if (!InfoCards.IsValidIndex(Index))
	{
		return; // not tracked on this anchor (already cleared, or never was)
	}

	// Read by value before HideCardAtIndex below, which can run Blueprint that
	// reshapes InfoCards.
	const ECardType CardType              = InfoCards[Index].CardType;
	const bool bTriggerOnMouseHover       = InfoCards[Index].bTriggerOnMouseHover;
	const bool bTriggerOnFocus            = InfoCards[Index].bTriggerOnFocus;
	const bool bKeepOpenUntilCursorLeaves = InfoCards[Index].bKeepOpenUntilCursorLeaves;

	// Only Highlight cards are held up by a trigger that can come and go while
	// a pin is in place. Click and Always On cards were never waiting on a
	// leave/lost event; they just resume their normal dismissal (Remove Card
	// On Pressing, Auto Dismiss Duration) from here on.
	if (CardType != ECardType::Hover)
	{
		return;
	}

	// Keep Open Until Cursor Leaves already has a per-tick check that answers
	// this better than a one-off test here can: the cursor may well be on the
	// card itself rather than the anchor. SetPinned reset that check's grace
	// period, so it gets the full window.
	if (bKeepOpenUntilCursorLeaves)
	{
		return;
	}

	const bool bMouseStillHolding = bTriggerOnMouseHover && bIsMouseOverAnchor;
	const bool bFocusStillHolding = bTriggerOnFocus && bIsFocusInAnchorPath
		&& FocusTriggerMode != EInfoCardFocusTriggerMode::Disabled;

	if (bMouseStillHolding || bFocusStillHolding)
	{
		return; // the trigger is still up, so the next leave/lost closes it normally
	}

	HideCardAtIndex(Index);
}

void UInfoCardAnchor::NotifyCardSelfClosed(UInfoCardWidget* ClosedCard)
{
	if (!ClosedCard)
	{
		return;
	}

	for (int32 Index = 0; Index < RuntimeStates.Num(); ++Index)
	{
		if (RuntimeStates[Index].ActiveCard != ClosedCard)
		{
			continue;
		}

		const FName ClosedCardId = RuntimeStates[Index].ActiveCardId;
		RuntimeStates[Index].ActiveCard = nullptr;
		RuntimeStates[Index].ActiveCardId = NAME_None;

		ClearHoverDelayAtIndex(Index);

		UpdateRegistryMembership();
		RecordCardClosed();
		OnCardHidden.Broadcast(ClosedCardId);
		return;
	}
}

void UInfoCardAnchor::RetriggerHoverCardById(FName CardId)
{
	// TriggerHoverEntryAtIndex already checks bEnabled/CardType/Source and
	// no-ops on an invalid Index (FindCardIndexById returns INDEX_NONE for
	// no match), so nothing further needs validating here.
	TriggerHoverEntryAtIndex(FindCardIndexById(CardId), EInfoCardHoverTriggerSource::Mouse);
}


// =================================================================================================
// Hold-to-interact
// =================================================================================================

void UInfoCardAnchor::NotifyHoldCompleted(FName CardId)
{
	ShowCardById(CardId);
}

void UInfoCardAnchor::NotifyHoldCanceled(FName CardId)
{
	HideCardById(CardId);
}

void UInfoCardAnchor::NotifyHoldProgress(FName CardId, float Progress)
{
	const int32 Index = FindCardIndexById(CardId);
	if (!InfoCards.IsValidIndex(Index))
	{
		return;
	}

	EnsureRuntimeStatesSize();

	// First call for this hold: nothing on screen yet, so show it. Every
	// call after this one just updates the payload on the card already up.
	if (!RuntimeStates.IsValidIndex(Index) || !RuntimeStates[Index].ActiveCard)
	{
		ShowCardAtIndex(Index);
	}

	if (!RuntimeStates.IsValidIndex(Index) || !RuntimeStates[Index].ActiveCard)
	{
		// Didn't actually spawn (disabled, suppressed, missing CardClass,
		// no owning player, etc.) — nothing to push progress into.
		return;
	}

	UInfoCardPayload_HoldProgress* ProgressPayload = nullptr;
	for (const TObjectPtr<UNameTBD_Payload>& Payload : InfoCards[Index].Payloads)
	{
		ProgressPayload = Cast<UInfoCardPayload_HoldProgress>(Payload.Get());
		if (ProgressPayload)
		{
			break;
		}
	}

	if (!ProgressPayload)
	{
		UE_LOG(LogInfoCard, Warning,
			TEXT("NotifyHoldProgress: CardId '%s' has no UInfoCardPayload_HoldProgress payload authored — progress can't be pushed to it."),
			*CardId.ToString());
		return;
	}

	ProgressPayload->Progress = Progress;

	// Re-deliver the (now-updated) payload set to the live card so the
	// widget can reflect the new value.
	RefreshCard(CardId);
}


// =================================================================================================
// Focus (Manual FocusTriggerMode)
// =================================================================================================

void UInfoCardAnchor::NotifyFocusReceived()
{
	if (FocusTriggerMode == EInfoCardFocusTriggerMode::Disabled)
	{
		return;
	}

	bIsFocusInAnchorPath = true;
	OnFocusReceived.Broadcast();
	TriggerHoverEntries(EInfoCardHoverTriggerSource::Focus);
}

void UInfoCardAnchor::NotifyFocusLost()
{
	if (FocusTriggerMode == EInfoCardFocusTriggerMode::Disabled)
	{
		return;
	}

	bIsFocusInAnchorPath = false;
	OnFocusLost.Broadcast();
	UntriggerHoverEntries(EInfoCardHoverTriggerSource::Focus);
}


// =================================================================================================
// All cards
// =================================================================================================

void UInfoCardAnchor::HideAllCards()
{
	EnsureRuntimeStatesSize();

	for (int32 Index = 0; Index < RuntimeStates.Num(); ++Index)
	{
		HideCardAtIndex(Index);
	}
}

#if WITH_EDITOR
const FText UInfoCardAnchor::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "Panel");
}

void UInfoCardAnchor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName ChangedPropertyName = PropertyChangedEvent.GetPropertyName();
	if (ChangedPropertyName == GET_MEMBER_NAME_CHECKED(UInfoCardAnchor, InfoCards_DA))
	{
		// Covers both assigning a Data Asset (populate) and clearing it
		// (empty out), since both syncs handle a null InfoCards_DA themselves.
		SyncPayloadDefaultsFromDataAsset();
		SyncSettingsOverridesFromDataAsset();
		SyncAdditiveAnimationOverridesFromDataAsset();
	}

	// Pushes a mid-drag edit - Scrub Alpha, Playback Speed - straight to the anchor being painted in the
	// viewport, which the commit-time rebuild would only show once the mouse came up.
	UWidgetAnimDesignerPreview::NotifyPropertyChanged(this, PropertyChangedEvent);
}

void UInfoCardAnchor::SyncPayloadDefaultsFromDataAsset()
{
	using namespace InfoCardPayloadDefaultFields;

	// No Data Asset assigned: there is nothing for defaults to be layered on
	// top of, so leaving the previous asset's entries sitting in the Details
	// panel would just be showing the designer a list that does nothing.
	if (!IsValid(InfoCards_DA))
	{
		PayloadDefaults.Reset();
		return;
	}

	// Built as a fresh array and swapped in at the end rather than mutated in
	// place: that gets pruning for free (anything not rebuilt simply isn't
	// carried over) and keeps PayloadDefaults in the same order as the Data
	// Asset's own InfoCards and payloads.
	TArray<FInfoCardPayloadDefault> Rebuilt;
	TSet<int32> ClaimedExisting;

	for (const FInfoCardSpec& SourceSpec : InfoCards_DA->InfoCards)
	{
		if (SourceSpec.CardId == NAME_None)
		{
			// Without an id there's nothing for ApplyPayloadDefaults to match
			// against, so an entry for it could never take effect anyway.
			continue;
		}

		for (const TObjectPtr<UNameTBD_Payload>& SourcePayloadPtr : SourceSpec.Payloads)
		{
			const UNameTBD_Payload* SourcePayload = SourcePayloadPtr.Get();
			if (!SourcePayload)
			{
				continue;
			}

			UClass* PayloadClass = SourcePayload->GetClass();

			// Matched by CardId + class. Each existing entry is claimed at
			// most once, so a card carrying two payloads of the same class
			// keeps two separate entries.
			int32 ExistingIndex = INDEX_NONE;
			for (int32 Candidate = 0; Candidate < PayloadDefaults.Num(); ++Candidate)
			{
				const FInfoCardPayloadDefault& Existing = PayloadDefaults[Candidate];
				if (!ClaimedExisting.Contains(Candidate)
					&& Existing.CardId == SourceSpec.CardId
					&& Existing.Payload
					&& Existing.Payload->GetClass() == PayloadClass)
				{
					ExistingIndex = Candidate;
					break;
				}
			}

			if (ExistingIndex == INDEX_NONE)
			{
				// New: start as an exact copy of the Data Asset, so the Details
				// panel shows the real current values and nothing counts as
				// changed until the designer edits something.
				FInfoCardPayloadDefault& NewDefault = Rebuilt.AddDefaulted_GetRef();
				NewDefault.CardId   = SourceSpec.CardId;
				NewDefault.Payload  = DuplicateObject<UNameTBD_Payload>(SourcePayload, this);
				NewDefault.Baseline = DuplicateObject<UNameTBD_Payload>(SourcePayload, this);
				continue;
			}

			ClaimedExisting.Add(ExistingIndex);

			FInfoCardPayloadDefault& Kept = Rebuilt.Add_GetRef(PayloadDefaults[ExistingIndex]);
			Kept.Payloads.Reset();

			if (!Kept.Baseline || Kept.Baseline->GetClass() != PayloadClass)
			{
				// Converted from the old layout (or otherwise missing): capture
				// one now. Fields already differing from the Data Asset stay
				// overridden; the rest become plain copies.
				Kept.Baseline = DuplicateObject<UNameTBD_Payload>(SourcePayload, this);
				continue;
			}

			// Bring Baseline up to the Data Asset's current values. A field the
			// designer hadn't changed (still equal to the old Baseline) follows
			// along, so the Details panel keeps showing what will really be
			// used. A changed field is left exactly as the designer set it.
			for (TFieldIterator<FProperty> It(PayloadClass); It; ++It)
			{
				const FProperty* Property = *It;
				if (!IsOverridable(Property) || AreEqual(Property, Kept.Baseline, SourcePayload))
				{
					continue;
				}

				if (AreEqual(Property, Kept.Payload, Kept.Baseline))
				{
					Property->CopyCompleteValue_InContainer(Kept.Payload, SourcePayload);
				}

				Property->CopyCompleteValue_InContainer(Kept.Baseline, SourcePayload);
			}
		}
	}

	PayloadDefaults = MoveTemp(Rebuilt);
}

void UInfoCardAnchor::SyncSettingsOverridesFromDataAsset()
{
	// No Data Asset assigned: same reasoning as SyncPayloadDefaultsFromDataAsset
	// — a list of overrides for a Data Asset that's no longer set is just
	// misleading, so clear it rather than leave it stale.
	if (!IsValid(InfoCards_DA))
	{
		SettingsOverrides.Reset();
		return;
	}

	// Same rebuild-and-swap approach as SyncPayloadDefaultsFromDataAsset: gets
	// pruning of removed CardIds for free and keeps this in the same order as
	// the Data Asset's own InfoCards.
	TArray<FInfoCardSettingsOverrideElement> Rebuilt;
	Rebuilt.Reserve(InfoCards_DA->InfoCards.Num());

	for (const FInfoCardSpec& SourceSpec : InfoCards_DA->InfoCards)
	{
		if (SourceSpec.CardId == NAME_None)
		{
			// Without an id there's nothing for ApplySettingsOverrides to
			// match against, so an entry for it could never take effect.
			continue;
		}

		const FInfoCardSettingsOverrideElement* ExistingElement = SettingsOverrides.FindByPredicate(
			[&SourceSpec](const FInfoCardSettingsOverrideElement& Existing) { return Existing.CardId == SourceSpec.CardId; });

		FInfoCardSettingsOverrideElement NewElement;
		NewElement.CardId = SourceSpec.CardId;

		// Carry over whatever was already set for this CardId — this sync
		// only needs to add/prune entries by CardId, never touch the
		// FInfoCardSettingsOverride contents of one that survives.
		if (ExistingElement)
		{
			NewElement.Settings = ExistingElement->Settings;
		}

		Rebuilt.Add(MoveTemp(NewElement));
	}

	SettingsOverrides = MoveTemp(Rebuilt);
}

void UInfoCardAnchor::SyncAdditiveAnimationOverridesFromDataAsset()
{
	if (!IsValid(InfoCards_DA))
	{
		AdditiveAnimationOverrides.Reset();
		return;
	}

	// Same rebuild-and-swap approach as the two syncs above.
	TArray<FInfoCardAdditiveAnimationsOverride> Rebuilt;
	Rebuilt.Reserve(InfoCards_DA->InfoCards.Num());

	for (const FInfoCardSpec& SourceSpec : InfoCards_DA->InfoCards)
	{
		if (SourceSpec.CardId == NAME_None)
		{
			continue;
		}

		const FInfoCardAdditiveAnimationsOverride* ExistingElement = AdditiveAnimationOverrides.FindByPredicate(
			[&SourceSpec](const FInfoCardAdditiveAnimationsOverride& Existing) { return Existing.CardId == SourceSpec.CardId; });

		FInfoCardAdditiveAnimationsOverride NewElement;
		if (ExistingElement)
		{
			NewElement = *ExistingElement;
		}
		NewElement.CardId = SourceSpec.CardId;

		// Not overriding: mirror the Data Asset, so the greyed-out arrays show
		// what this card actually plays, and switching Override on starts from
		// there instead of from nothing.
		if (!NewElement.bOverride)
		{
			// What the card really plays: its own set, or the Data Asset's Default Additive Animations when its own
			// would play nothing (the same fallback RebuildInfoCardsFromDataAsset applies).
			NewElement.AdditiveAnimations = SourceSpec.AdditiveAnimations.IsEmpty()
				? InfoCards_DA->DefaultCardAnimations
				: SourceSpec.AdditiveAnimations;
		}

		Rebuilt.Add(MoveTemp(NewElement));
	}

	AdditiveAnimationOverrides = MoveTemp(Rebuilt);
}
#endif

#undef LOCTEXT_NAMESPACE