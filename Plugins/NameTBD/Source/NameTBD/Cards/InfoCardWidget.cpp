// Copyright AR Game Systems 2026. All rights reserved.

#include "InfoCardWidget.h"

#include "NameTBD/Sound/UISoundTypes.h"

#include "InfoCardAnchor.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetMathLibrary.h"
#include "Misc/CoreDelegates.h"
#include "Slate/SObjectWidget.h"
#include "Containers/Ticker.h"
#include "Widgets/SWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogInfoCardWidget, Log, All);

TArray<TWeakObjectPtr<UInfoCardWidget>> UInfoCardWidget::ActiveCards;
int64 UInfoCardWidget::NextSpawnSequence = 0;


namespace
{
	/**
	 * How long a dismissal may sit waiting for a mouse-up that never comes
	 * before it gives up and closes the card anyway. See
	 * Uinfocardwidget::bPendingDismissAwaitsRelease — the release can be
	 * lost to the window losing focus mid-click, or to a widget that takes
	 * mouse capture and never gives the up-event back. Long enough that no
	 * real click is cut short, short enough that a card never looks stuck.
	 */
	constexpr double GInfoCardPendingDismissTimeout = 0.75;
}

namespace
{
	/**
	 * Slate input pre-processor: sees every mouse and gamepad press in the
	 * whole application, before Slate routes it to any widget, and hands it
	 * to Uinfocardwidget::RecordGlobalPress.
	 *
	 * This exists because the thing it replaces —
	 * APlayerController::WasInputKeyJustPressed, polled once per tick — only
	 * ever reports a press that Slate declined to handle. Every press landing
	 * on a Button, on another card, on a card's own anchor, or anywhere at
	 * all while the game is in a UI-only input mode is invisible to it. From
	 * a player's point of view that reads as dismissal working in some parts
	 * of the screen and not others, and needing a second press "to wake it
	 * up" wherever the first one got eaten. A pre-processor has no such
	 * blind spot: it is upstream of routing entirely.
	 *
	 * Every handler returns false. This watches input, it never consumes it —
	 * a tooltip that swallowed presses would break every button underneath
	 * it, which is a far worse bug than the one being fixed.
	 */
	class FInfoCardPressWatcher : public IInputProcessor
	{
	public:
		/**
		 * Runs once per frame, after Slate has pumped and routed that frame's
		 * input and before any widget ticks. Both halves of this matter:
		 *
		 * - Flushing here means a card closes on the SAME frame as the click
		 *   that dismissed it, not the next one, and it happens outside input
		 *   dispatch, so removing the widget is safe.
		 * - Refreshing here means every live card's rects are current even if
		 *   that card is collapsed, hidden, or has ticking switched off in its
		 *   WBP — cards that, before, silently stopped responding to presses
		 *   altogether.
		 */
		virtual void Tick(const float /*DeltaTime*/, FSlateApplication& /*SlateApp*/, TSharedRef<ICursor> /*Cursor*/) override
		{
			UInfoCardWidget::FlushPendingDismissals();
			UInfoCardWidget::RefreshAllActiveCardGeometry();
		}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& MouseEvent) override
		{
			UInfoCardWidget::RecordGlobalPress(MouseEvent.GetEffectingButton(), true, MouseEvent.GetScreenSpacePosition());
			return false;
		}

		virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& MouseEvent) override
		{
			// The second press of a double-click arrives here rather than
			// through HandleMouseButtonDownEvent. Without this, double-clicking
			// to dismiss registers as one press, not two — which is the exact
			// "sometimes I have to double click and it still doesn't take"
			// symptom, seen from the other side.
			UInfoCardWidget::RecordGlobalPress(MouseEvent.GetEffectingButton(), true, MouseEvent.GetScreenSpacePosition());
			return false;
		}

		/**
		 * Releases any dismissal that was deliberately held until the mouse
		 * came back up — see Uinfocardwidget::bPendingDismissAwaitsRelease.
		 * This runs BEFORE Slate routes the release, but the close itself
		 * happens in Tick above, which runs after; so the button under the
		 * cursor still receives its mouse-up and fires OnClicked, and only
		 * then does the card go away.
		 */
		virtual bool HandleMouseButtonUpEvent(FSlateApplication& /*SlateApp*/, const FPointerEvent& /*MouseEvent*/) override
		{
			UInfoCardWidget::NotifyGlobalRelease();
			return false;
		}

		virtual bool HandleKeyDownEvent(FSlateApplication& /*SlateApp*/, const FKeyEvent& KeyEvent) override
		{
			// A held button re-fires key-down every frame. Each repeat used to
			// count as a fresh press, so holding any gamepad button meant a
			// continuous stream of dismissals — and, for a card that had just
			// been opened by that same held button, an immediate close.
			if (KeyEvent.IsRepeat())
			{
				return false;
			}

			// Gamepad face/shoulder/dpad buttons arrive as key events, not
			// pointer events, and carry no screen position — see
			// ShouldPressCloseCard for how a positionless press is resolved.
			// RecordGlobalPress filters out keyboard keys.
			UInfoCardWidget::RecordGlobalPress(KeyEvent.GetKey(), false, FVector2D::ZeroVector);
			return false;
		}

		virtual bool HandleKeyUpEvent(FSlateApplication& /*SlateApp*/, const FKeyEvent& /*KeyEvent*/) override
		{
			UInfoCardWidget::NotifyGlobalRelease();
			return false;
		}

		// Not marked override: some engine versions declare this on
		// IInputProcessor and some don't, and this way the file compiles
		// either way.
		virtual const TCHAR* GetDebugName() const { return TEXT("InfoCardPressWatcher"); }
	};

	TSharedPtr<FInfoCardPressWatcher> GInfoCardPressWatcher;
}

bool UInfoCardWidget::IsDismissingKey(const FKey& Key)
{
	if (!Key.IsValid())
	{
		return false;
	}

	// Mouse wheel is a mouse "button" as far as FKey is concerned, and
	// scrolling a list the card is sitting beside is not a request to dismiss
	// anything.
	if (Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown || Key == EKeys::MouseWheelAxis)
	{
		return false;
	}

	// Any mouse button and any gamepad button count, whatever the card's own
	// trigger was — a right-click card is dismissed by a left-click just as
	// readily, the way an OS context menu behaves.
	//
	// Gamepad AXES are excluded: a stick drifting off centre is a continuous
	// analog value, not a press, and would dismiss every card on screen the
	// moment anyone touched the controller.
	if (Key.IsGamepadKey())
	{
		return !Key.IsAxis1D() && !Key.IsAxis2D() && !Key.IsAxis3D();
	}

	return Key.IsMouseButton();
}

void UInfoCardWidget::RecordGlobalPress(const FKey& Key, bool bHasPosition, const FVector2D& AbsolutePosition)
{
	if (!IsDismissingKey(Key))
	{
		return;
	}

	DispatchPressToActiveCards(bHasPosition, AbsolutePosition);
}

void UInfoCardWidget::DispatchPressToActiveCards(bool bHasPosition, const FVector2D& AbsolutePosition)
{
	// Iterated over a copy. EvaluateGlobalPress only sets flags today, but
	// this array is the system's one piece of shared mutable state and a
	// Blueprint override reached from here must not be able to invalidate the
	// iteration underneath it.
	TArray<TWeakObjectPtr<UInfoCardWidget>> Snapshot = ActiveCards;

	for (const TWeakObjectPtr<UInfoCardWidget>& WeakCard : Snapshot)
	{
		UInfoCardWidget* Card = WeakCard.Get();
		if (!Card || Card->IsDesignTime() || Card->bEmbeddedInLayout)
		{
			continue;
		}

		// The press point has to be converted per card: AbsoluteToViewport
		// resolves against the widget's own geometry/viewport context, and a
		// split-screen or multi-viewport setup does not share one.
		FVector2D PressPoint = FVector2D::ZeroVector;
		if (bHasPosition)
		{
			FVector2D PixelScratch = FVector2D::ZeroVector;
			USlateBlueprintLibrary::AbsoluteToViewport(Card, AbsolutePosition, PixelScratch, PressPoint);
		}

		Card->EvaluateGlobalPress(PressPoint, bHasPosition);
	}
}

void UInfoCardWidget::NotifyGlobalRelease()
{
	for (const TWeakObjectPtr<UInfoCardWidget>& WeakCard : ActiveCards)
	{
		if (UInfoCardWidget* Card = WeakCard.Get())
		{
			Card->bPendingDismissAwaitsRelease = false;
		}
	}
}

void UInfoCardWidget::FlushPendingDismissals()
{
	// Copied, and for a much harder reason than the dispatch above:
	// ApplyPendingDismiss closes cards, a close can remove the widget
	// outright, and NativeDestruct removes it from ActiveCards. Iterating the
	// live array here would be modifying it mid-loop.
	TArray<TWeakObjectPtr<UInfoCardWidget>> Snapshot = ActiveCards;

	for (const TWeakObjectPtr<UInfoCardWidget>& WeakCard : Snapshot)
	{
		if (UInfoCardWidget* Card = WeakCard.Get())
		{
			Card->ApplyPendingDismiss();
		}
	}
}

void UInfoCardWidget::RefreshAllActiveCardGeometry()
{
	for (const TWeakObjectPtr<UInfoCardWidget>& WeakCard : ActiveCards)
	{
		if (UInfoCardWidget* Card = WeakCard.Get())
		{
			Card->RefreshDismissGeometry();
		}
	}
}

void UInfoCardWidget::EnsurePressWatcher()
{
	if (GInfoCardPressWatcher.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	GInfoCardPressWatcher = MakeShared<FInfoCardPressWatcher>();

	// Index 0: ahead of anything else that may have registered, so a press is
	// seen even if some other pre-processor consumes it outright.
	FSlateApplication::Get().RegisterInputPreProcessor(GInfoCardPressWatcher, 0);

	// Registered once and left in place — see EnsurePressWatcher's comment in
	// the header for why unregistering per card was actively dangerous. This
	// is the only teardown, and it runs at a point where nothing is
	// dispatching input.
	FCoreDelegates::OnPreExit.AddLambda([]()
	{
		if (GInfoCardPressWatcher.IsValid())
		{
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().UnregisterInputPreProcessor(GInfoCardPressWatcher);
			}

			GInfoCardPressWatcher.Reset();
		}
	});
}

void UInfoCardWidget::SetupCard(const FInfoCardSpec& Spec, UWidget* InAnchorWidget)
{
	PositionMode          = Spec.PositionMode;
	IconPosition          = Spec.IconPosition;
	FillIconPadding       = Spec.FillIconPadding;
	CursorPosition        = Spec.CursorPosition;
	ViewportPosition      = Spec.ViewportPosition;
	Offset                = Spec.CardOffset;
	HoverAnchorPadding    = Spec.IconAnchorPadding;
	bKeepOpenUntilCursorLeaves = Spec.bKeepOpenUntilCursorLeaves;
	bReappearOnClickDismiss = Spec.bReappearOnClickDismiss;
	RemoveCardOnPressing = Spec.RemoveCardOnPressing;
	bIgnoresMenuStateChange = Spec.bIgnoresMenuStateChange;
	bCloseOnClickOutside = true; // Managed cards close on outside clicks by default
	bDismissClickGetsConsumed = Spec.DismissClickGetsConsumed.Resolve(EInfoCardConfigurableBoolKind::DismissClickGetsConsumed);
	bFlipToAvoidClipping = Spec.FlipToAvoidClipping.Resolve(EInfoCardConfigurableBoolKind::FlipToAvoidClipping);

	// Purely visual — applied once, here, and never read back by anything
	// downstream. UUserWidget has no bulk SetRenderTransform(FWidgetTransform)
	// of its own (unlike UWidget, which Uinfocardwidget — a UUserWidget —
	// doesn't inherit from), so this goes through the four individual
	// setters instead. CardTransform itself is kept as a readable record for
	// Blueprint/C++ subclasses, since there's no equivalent getter to ask
	// the widget what's currently applied.
	CardTransform = Spec.CardTransform;
	SetRenderTranslation(CardTransform.Translation);
	SetRenderScale(CardTransform.Scale);
	SetRenderShear(CardTransform.Shear);
	SetRenderTransformAngle(CardTransform.Angle);

	// Copied wholesale. Anything the anchor wanted to change about them was already folded into the spec by
	// ApplySettingsOverrides, so by the time a card sees them there is only one answer to what it sounds like.
	Sounds = Spec.Sounds;
	FUISoundPlayer::ResizeRuntimes(this, SoundRuntimes, Sounds.Num());

	// After CreateWidget (so a component added in the card's Widget Blueprint
	// already exists) and before AddToViewport (which constructs it and plays
	// the entrance), so that entrance already includes these. See
	// ApplyAdditiveAnimations.
	AdditiveAnimations = Spec.AdditiveAnimations;
	ApplyAdditiveAnimations();

	// Resolved once, here, rather than kept as the configurable struct on the
	// widget: everything downstream (TickKeepOpenWhileCursorNearby et al.)
	// just wants a plain float, and this is the one point in a card's life
	// where "what does the project say right now" needs asking.
	KeepOpenSlack       = Spec.KeepOpenSlack.Resolve(EInfoCardConfigurableFloatKind::KeepOpenSlack);
	KeepOpenGracePeriod = Spec.KeepOpenGracePeriod.Resolve(EInfoCardConfigurableFloatKind::KeepOpenGracePeriod);
	AutoDismissDuration = Spec.AutoDismissDuration.Resolve(EInfoCardConfigurableFloatKind::AutoDismissDuration);

	bClampToViewport        = Spec.bClampToViewport;
	ClampPadding             = Spec.ClampPadding;
	bAvoidOverlappingCards   = Spec.bAvoidOverlappingCards;
	UpdatesPositionOnTick    = Spec.bUpdatePositionOnTick;

	AnchorWidget    = InAnchorWidget;
	SourceCardType  = Spec.CardType;
	CardId          = Spec.CardId;

	IsFirstFrame          = true;
	ElapsedTime           = 0.f;

	// No "ignore the press that spawned me" bookkeeping is needed any more.
	// A card is only offered a press once it is in ActiveCards, and it only
	// joins ActiveCards in NativeConstruct — which, for a click-triggered
	// card, runs inside the AddToViewport that follows this function, well
	// after the pre-processor already handed that press to everything that
	// existed at the time. The card that a press creates is structurally
	// invisible to it.
	ClearPendingDismiss();

	// Registered before the card can possibly be pressed rather than waiting
	// for NativeConstruct. Cheap and idempotent.
	EnsurePressWatcher();

	FramesSinceConstruct = 0;
	KeepOpenOutsideTime  = 0.f;
	bHasCardRect         = false;
	bHasAppliedPosition  = false;
	bCloseRequested      = false;

	// Reset alongside bCloseRequested: SetupCard runs before AddToViewport, so
	// at this point NativeConstruct genuinely has not happened yet for this
	// instance and anything asking CloseCard to run before it does must take
	// the deferred path.
	bHasBeenConstructed     = false;
	bClosePendingConstruct  = false;

	// A fresh card is never pinned, never carries a fill size over from a
	// previous life, and resolves its parent card anew.
	bIsPinned            = false;
	bHasAppliedFillSize  = false;
	bHitTestBlockedForAnimation = false;
	LastAppliedFillSize  = FVector2D::ZeroVector;
	CachedParentCard.Reset();
	ParentCardMissFrame  = TNumericLimits<uint64>::Max();

	SpawnSequence        = ++NextSpawnSequence;

	// A fixed lifetime from spawn — see AutoDismissTimerHandle's own comment
	// for why it isn't reset by continued interaction. StartAutoDismissTimer
	// clears any previous one first, in case SetupCard is somehow ever called
	// a second time on the same instance (it currently isn't, but costs
	// nothing to be safe rather than stacking two timers).
	StartAutoDismissTimer();

	RefreshAnchorGeometry();

	TArray<UNameTBD_Payload*> RawPayloads;
	RawPayloads.Reserve(Spec.Payloads.Num());
	for (const TObjectPtr<UNameTBD_Payload>& P : Spec.Payloads)
	{
		RawPayloads.Add(P.Get());
	}
	SetPayloads(RawPayloads);
}

void UInfoCardWidget::SetPayloads(const TArray<UNameTBD_Payload*>& NewPayloads)
{
	Payloads.Reset(NewPayloads.Num());
	for (UNameTBD_Payload* P : NewPayloads)
	{
		Payloads.Add(P);
	}

	ApplyPayloads(NewPayloads);
}

void UInfoCardWidget::ApplyPayloads_Implementation(const TArray<UNameTBD_Payload*>& InPayloads)
{
	// Base implementation does nothing.
	// Subclasses override this to wire up custom payload logic.
}

UNameTBD_Payload* UInfoCardWidget::GetPrimaryPayload() const
{
	return Payloads.IsValidIndex(0) ? Payloads[0].Get() : nullptr;
}

UNameTBD_Payload* UInfoCardWidget::GetPayloadAtIndex(int32 Index) const
{
	return Payloads.IsValidIndex(Index) ? Payloads[Index].Get() : nullptr;
}

void UInfoCardWidget::CloseCard()
{
	if (IsClosing())
	{
		return;
	}

	// Set before anything else below can re-enter this function. A card with
	// no out animation never reports IsAnimClosing(), so this flag is the only
	// thing stopping a per-tick closer from calling CloseCard on every
	// subsequent frame — which used to re-run the whole teardown (and
	// re-broadcast OnCardHidden via NotifyCardSelfClosed) once per frame for
	// as long as the card stayed on screen.
	bCloseRequested = true;

	// Guarded by bCloseRequested above, so the per-tick closers that call CloseCard every frame only sound
	// once. Fired as the exit starts rather than after it: a card with a slow Anim Out shouldn't hold its
	// close sound back until the animation finishes.
	PlayCardSound(EInfoCardSoundEvent::Hidden);

	// The anchor's RuntimeStates holds a strong TObjectPtr to this card, so
	// closing without telling it leaves that slot pointing at a
	// closing/removed-but-not-collected card — and a later Show.../Notify...
	// call on the anchor sees that non-null pointer and thinks this card is
	// still up, silently no-opping instead of spawning a fresh one. This
	// covers every path that closes a card from the card's own side rather
	// than via the anchor's HideCardAtIndex/HideCardById — click-outside,
	// TickKeepOpenWhileCursorNearby, and an InventoryDisplay's source
	// changing — not just whichever one happened to be added most recently.
	if (UInfoCardAnchor* Anchor = Cast<UInfoCardAnchor>(AnchorWidget.Get()))
	{
		Anchor->NotifyCardSelfClosed(this);
	}

	// Everything above is bookkeeping and safe at any point in this card's
	// life. FinishClose is not: for a card with no out animation PlayOut
	// removes the widget outright, and doing that while Slate is still in the
	// middle of constructing it tears down a widget the construction pass is
	// still walking. That window is real and routinely hit — a nested Always
	// On card spawns during its host card's own construction, and whatever it
	// closes on the way up (a CloseOtherCardsScope, a menu-state change fired
	// by its own NativeConstruct) can be a card that isn't finished yet.
	if (!bHasBeenConstructed)
	{
		bClosePendingConstruct = true;
		return;
	}

	FinishClose();
}

void UInfoCardWidget::FinishClose()
{
	bClosePendingConstruct = false;

	if (UWidgetAnimationComponent* Anim = ResolveCardAnimation())
	{
		// Removes the card through HandleCardAnimationOutFinished once the exit has played.
		Anim->PlayOut();
	}
	else
	{
		RemoveFromParent();
	}

	UpdateAnimationHitTestBlock();
}

bool UInfoCardWidget::IsClosing() const
{
	return bCloseRequested || (CardAnimation && CardAnimation->IsAnimClosing());
}

void UInfoCardWidget::PlayCardSound(EInfoCardSoundEvent Event)
{
	if (Sounds.Num() == 0)
	{
		return;
	}

	FUISoundPlayer::ResizeRuntimes(this, SoundRuntimes, Sounds.Num());

	// Straight Play, not PlayOnEventChange: a card's events are one-shots in a fixed order, not states it
	// settles into, so there is no repeated event to filter out. Each set's own repeat limit still applies.
	for (int32 Index = 0; Index < Sounds.Num(); ++Index)
	{
		FUISoundPlayer::Play(this, SoundRuntimes[Index], Sounds[Index].Settings, Sounds[Index].GetSoundForEvent(Event));
	}
}

void UInfoCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// A designer-added component is also constructed by the UI component system; NotifyOwnerConstructed
	// only acts once, so whichever of the two gets there first starts the entrance. Called here as well so
	// the entrance has definitely started before the hit-test block below reads the phase, and because a
	// component the card made for itself has nobody else to construct it.
	if (UWidgetAnimationComponent* Anim = ResolveCardAnimation())
	{
		Anim->NotifyOwnerConstructed();
	}

	IsFirstFrame         = true;
	ElapsedTime          = 0.f;
	FramesSinceConstruct = 0;
	KeepOpenOutsideTime  = 0.f;
	bHasCardRect         = false;
	bHasAppliedPosition  = false;

	bEmbeddedInLayout = ComputeEmbeddedInLayout();

	if (bEmbeddedInLayout)
	{
		// Placed in a Widget Blueprint (or a panel) rather than spawned by an
		// anchor. Deliberately NOT added to ActiveCards: that list is what
		// menu-state changes and press dismissal walk to find cards to close,
		// and closing ends in RemoveFromParent, which pulled the card straight
		// back out of the layout it was placed in on its first frame.
		if (bHitTestableWhenEmbedded && GetVisibility() == ESlateVisibility::SelfHitTestInvisible)
		{
			SetVisibility(ESlateVisibility::Visible);
		}
	}
	else
	{
		// AvoidOverlappingCards (on every OTHER card, checking against this one)
		// needs to find this card here, and so does the press dispatch: a card
		// not in this array is a card no press is ever offered to.
		ActiveCards.AddUnique(this);

		// Idempotent; see EnsurePressWatcher. Called from here as well as
		// SetupCard so a card built by hand, without going through an anchor,
		// still gets a working dismissal path.
		EnsurePressWatcher();
	}

	bHasBeenConstructed = true;

	// Here rather than in SetupCard: this is the point the card is actually on screen and its entrance has
	// started, and it's also the only point a card built by hand (no anchor, no SetupCard) passes through.
	PlayCardSound(EInfoCardSoundEvent::Shown);

	// The in animation has just started. Block now,
	// before the first hit test can see the card at its starting transform.
	UpdateAnimationHitTestBlock();

	// Something closed this card while it was still being built (see
	// CloseCard). The bookkeeping half already ran back then; this is the
	// teardown half, now that removing the widget is actually safe. Runs last
	// so the bind above is undone in the normal way rather than left dangling
	// on a card that is about to disappear.
	if (bClosePendingConstruct)
	{
		FinishClose();
	}
}

void UInfoCardWidget::NativeDestruct()
{
	// Cleared first: a widget that has left the viewport must never be
	// reported as constructed, or IsSlotActive's "created but not added yet"
	// allowance would keep a dead card's slot alive on the anchor.
	bHasBeenConstructed    = false;
	bClosePendingConstruct = false;

	// A delayed sound outliving the widget would fire its timer into a card that is already gone.
	FUISoundPlayer::CancelAll(this, SoundRuntimes);

	ActiveCards.RemoveSwap(TWeakObjectPtr<UInfoCardWidget>(this));

	// Nothing unregisters the press watcher here any more. It used to, and
	// this is exactly the call site that made that unsafe: a card closed by a
	// click reaches NativeDestruct while Slate is still walking its
	// pre-processor list for that same click. See EnsurePressWatcher.
	ClearPendingDismiss();


	if (UInfoCardAnchor* Anchor = Cast<UInfoCardAnchor>(AnchorWidget.Get()))
	{
		Anchor->NotifyCardSelfClosed(this);
	}

	ClearAutoDismissTimer();

	// Only the card's own component; a designer-added one is destructed by the UI component system.
	if (CardAnimation && bOwnsCardAnimation)
	{
		CardAnimation->NotifyOwnerDestructed();
	}

	// Re-evaluated on the next NativeConstruct, in case the widget is re-parented.
	bEmbeddedInLayout = false;

	Super::NativeDestruct();
}

bool UInfoCardWidget::ComputeEmbeddedInLayout() const
{
	// Anything an anchor set up belongs to the viewport, whatever else is true.
	if (AnchorWidget.IsValid())
	{
		return false;
	}

	// In a panel slot: the panel owns position and size.
	if (GetParent() != nullptr)
	{
		return true;
	}

	// The root of another Widget Blueprint's tree has no slot, but its outer is
	// that tree. Cards added to the viewport are never a tree's root.
	if (const UWidgetTree* OuterTree = Cast<UWidgetTree>(GetOuter()))
	{
		return OuterTree->RootWidget == this;
	}

	return false;
}

void UInfoCardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (IsDesignTime())
	{
		return;
	}

	ElapsedTime += InDeltaTime;
	++FramesSinceConstruct;

	if (bEmbeddedInLayout)
	{
		// The layout owns this card: no viewport positioning (SetPositionInViewport /
		// SetDesiredSizeInViewport on a slotted widget hands it to the game viewport
		// subsystem), no anchor geometry, no dismissal, no keep-open.
		IsFirstFrame = false;
		UpdateAnimationHitTestBlock();
		return;
	}

	if (UpdatesPositionOnTick || IsFirstFrame)
	{
		UpdatePosition();
	}

	IsFirstFrame = false;

	// Both rects, unconditionally, every frame.
	//
	// The icon rect used to be refreshed here only for a card with
	// bKeepOpenUntilCursorLeaves set, and otherwise only by UpdatePosition,
	// and only then when PositionMode was ToIcon. Every other card was
	// testing presses against whatever IconPositon held from SetupCard —
	// frequently a zero rect at the origin, captured before the anchor had
	// any geometry at all. GetIconViewportRect then reported "no rect", and
	// the anchor-aware zones (InsideAnchorOrCard, OutsideAnchorAndCard) had
	// nothing to work with: pressing the anchor of an ordinary ToCursor or
	// ToViewport hover card dismissed it, and pressing it a second time to
	// bring it back did nothing, because the cursor had never left.
	//
	// The card rect has to stay current for a further reason: it is what
	// every OTHER visible card's AvoidOverlappingCards compares itself
	// against, not just what this card's own tests need.
	RefreshDismissGeometry();

	ApplyPendingDismiss();
	TickKeepOpenWhileCursorNearby(InDeltaTime);
	// The animation component keeps its own clock; only the hit-test block needs updating from here.
	UpdateAnimationHitTestBlock();
}

// =====================================================================================================
// Widget Animation Component
// =====================================================================================================

UWidgetAnimationComponent* UInfoCardWidget::FindAuthoredAnimationComponent() const
{
	// Only components the UI component system attached; the card's own fallback is excluded by the caller.
	if (UWidgetAnimationComponent* OnSelf = UWidgetAnimationComponent::FindWidgetAnimationComponent(this))
	{
		if (OnSelf != CardAnimation || !bOwnsCardAnimation)
		{
			return OnSelf;
		}
	}

	if (UWidget* Root = GetRootWidget())
	{
		if (UWidgetAnimationComponent* OnRoot = UWidgetAnimationComponent::FindWidgetAnimationComponent(Root))
		{
			return OnRoot;
		}
	}

	UWidgetAnimationComponent* Found = nullptr;
	if (WidgetTree)
	{
		// This card's own tree only. Components inside nested User Widgets belong to those widgets, and are
		// reached through the cascade rather than mistaken for the card's own.
		WidgetTree->ForEachWidget([&Found](UWidget* Widget)
		{
			if (!Found)
			{
				Found = UWidgetAnimationComponent::FindWidgetAnimationComponent(Widget);
			}
		});
	}

	return Found;
}

UWidgetAnimationComponent* UInfoCardWidget::ResolveCardAnimation()
{
	if (!CardAnimation)
	{
		if (UWidgetAnimationComponent* Authored = FindAuthoredAnimationComponent())
		{
			CardAnimation = Authored;
			bOwnsCardAnimation = false;
		}
		else
		{
			// No component in the Widget Blueprint. The card still needs something to carry the additive
			// layers and to report the close, so it makes one and animates itself with it - which is also
			// exactly what a card class built before components existed did.
			UWidgetAnimationComponent* Own = NewObject<UWidgetAnimationComponent>(this, NAME_None, RF_Transient);
			Own->AnimIn = LegacyAnimIn;
			Own->AnimOut = LegacyAnimOut;
			Own->bPreserveAuthoredTransforms = bLegacyPreserveAuthoredTransforms;
			Own->BindToWidget(this);

			CardAnimation = Own;
			bOwnsCardAnimation = true;
		}
	}

	if (!CardAnimationOutFinishedHandle.IsValid())
	{
		CardAnimationOutFinishedHandle = CardAnimation->OnOutAnimationFinishedNative.AddUObject(this, &UInfoCardWidget::HandleCardAnimationOutFinished);
	}

	return CardAnimation;
}

void UInfoCardWidget::HandleCardAnimationOutFinished(UWidgetAnimationComponent* /*Component*/)
{
	// A component also finishes exits that have nothing to do with closing (Set Visibility Animated, a
	// cascade from a parent). Only a card that was asked to close removes itself - bCloseRequested rather
	// than IsClosing(), which is true after ANY finished exit.
	if (bCloseRequested)
	{
		RemoveFromParent();
	}
}

bool UInfoCardWidget::GetClassAnimationEntries(TSubclassOf<UInfoCardWidget> CardClass, UWidget* OwningWidget, TArray<FWidgetAnimEntry>& OutAnimIn, TArray<FWidgetAnimEntry>& OutAnimOut)
{
	OutAnimIn.Reset();
	OutAnimOut.Reset();

	if (!CardClass)
	{
		return false;
	}

	// The Widget Blueprint keeps designer-added components as archetypes and only creates them on an
	// instance, during Initialize. A throwaway instance is the supported way to read them: it is never
	// added to a parent, so it is never constructed, never animates, and is simply collected.
	if (OwningWidget && !OwningWidget->IsTemplate())
	{
		if (UInfoCardWidget* Probe = CreateWidget<UInfoCardWidget>(OwningWidget, CardClass))
		{
			const UWidgetAnimationComponent* Authored = Probe->FindAuthoredAnimationComponent();
			if (Authored)
			{
				OutAnimIn = Authored->AnimIn;
				OutAnimOut = Authored->AnimOut;
				return true;
			}
		}
	}

	const UInfoCardWidget* Defaults = CardClass->GetDefaultObject<UInfoCardWidget>();
	OutAnimIn = Defaults->LegacyAnimIn;
	OutAnimOut = Defaults->LegacyAnimOut;
	return OutAnimIn.Num() > 0 || OutAnimOut.Num() > 0;
}

void UInfoCardWidget::PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying)
{
	if (UWidgetAnimationComponent* Anim = ResolveCardAnimation())
	{
		Anim->PlayWidgetAnimationsById(AnimIds, bReverse, PlaybackSpeed, bHoldFinalPose, bRestartIfPlaying);
	}
}

void UInfoCardWidget::StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart)
{
	if (UWidgetAnimationComponent* Anim = ResolveCardAnimation())
	{
		Anim->StopWidgetAnimationsById(AnimIds, bResetToStart);
	}
}

bool UInfoCardWidget::AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const
{
	return CardAnimation && CardAnimation->AreWidgetAnimationsPlayingById(AnimIds);
}

bool UInfoCardWidget::SetWidgetAnimationValues(FName ValueName, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying)
{
	UWidgetAnimationComponent* Anim = ResolveCardAnimation();
	return Anim && Anim->SetWidgetAnimationValues(ValueName, Value, bSnap, bStartIfNotPlaying);
}

FVector4 UInfoCardWidget::GetWidgetAnimationValues(FName ValueName, bool bTargetValue) const
{
	return CardAnimation ? CardAnimation->GetWidgetAnimationValues(ValueName, bTargetValue) : FVector4(0.0, 0.0, 0.0, 0.0);
}

void UInfoCardWidget::DispatchGlobalMenuStateChanged(FGameplayTag ScreenTag, bool bIsOpen)
{
	// Snapshot the array because HandleMenuStateChanged might close cards, mutating ActiveCards mid-loop.
	TArray<TWeakObjectPtr<UInfoCardWidget>> Snapshot = ActiveCards;

	for (const TWeakObjectPtr<UInfoCardWidget>& WeakCard : Snapshot)
	{
		if (UInfoCardWidget* Card = WeakCard.Get())
		{
			Card->HandleMenuStateChanged(ScreenTag, bIsOpen);
		}
	}
}

void UInfoCardWidget::HandleMenuStateChanged(FGameplayTag ScreenTag, bool bIsOpen)
{
	if (bIgnoresMenuStateChange || bIsPinned || bEmbeddedInLayout)
	{
		return;
	}

	CloseCard();
}

void UInfoCardWidget::StartAutoDismissTimer()
{
	ClearAutoDismissTimer();

	if (AutoDismissDuration <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Core ticker rather than the world timer manager, which is not ticked at
	// all while the game is paused — a card shown in a pause menu would
	// otherwise never auto-dismiss, and one already up when the game paused
	// would stay frozen on screen. The core ticker runs every frame
	// regardless, on real frame time, the same clock NativeTick's keep-open
	// grace period already uses.
	//
	// Weak lambda: if this card is garbage collected without NativeDestruct
	// having run, the delegate is unbound and the ticker simply drops it.
	// Returning false makes it one-shot.
	AutoDismissTimerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateWeakLambda(this, [this](float /*DeltaTime*/)
		{
			// This element removes itself on return; just forget the handle.
			AutoDismissTimerHandle.Reset();
			HandleAutoDismissElapsed();
			return false;
		}),
		AutoDismissDuration);
}

void UInfoCardWidget::ClearAutoDismissTimer()
{
	// Safe on an empty or already-fired handle.
	FTSTicker::GetCoreTicker().RemoveTicker(AutoDismissTimerHandle);
	AutoDismissTimerHandle.Reset();
}

void UInfoCardWidget::ApplyAdditiveAnimations()
{
	TArray<FWidgetAnimSettings> AdditiveAnimIn;
	TArray<FWidgetAnimSettings> AdditiveAnimOut;
	AdditiveAnimations.ResolveInto(AdditiveAnimIn, AdditiveAnimOut);

	// Additive layers carry no target, so they move whatever the component is on - the widget picked in
	// the card's designer, or the card itself for a card without one.
	if (UWidgetAnimationComponent* Anim = ResolveCardAnimation())
	{
		Anim->SetExtraAnimSettings(AdditiveAnimIn, AdditiveAnimOut);
	}
}

void UInfoCardWidget::UpdateAnimationHitTestBlock()
{
	const bool bIsPlayingIn = CardAnimation && CardAnimation->GetAnimPhase() == EWidgetAnimPhase::PlayingIn;
	// The intro block only exists to stop a Hover card stealing the cursor from its
	// anchor. An embedded card has no anchor, so it stays hit-testable throughout.
	const bool bShouldBlock = IsClosing() || (bIsPlayingIn && SourceCardType == ECardType::Hover && !bEmbeddedInLayout);

	if (bShouldBlock == bHitTestBlockedForAnimation)
	{
		return;
	}

	if (bShouldBlock)
	{
		// Only the two hit-testable states need blocking. Hidden/Collapsed
		const ESlateVisibility Current = GetVisibility();
		// (or already hit-test invisible) is left exactly as it is.
		if (Current != ESlateVisibility::Visible && Current != ESlateVisibility::SelfHitTestInvisible)
		{
			return;
		}

		VisibilityBeforeHitTestBlock = Current;
		bHitTestBlockedForAnimation = true;

		// Not a hide, so this never triggers visibility-driven animations.
		SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}

	bHitTestBlockedForAnimation = false;

	// Something else changed the visibility meanwhile: leave its choice alone.
	if (GetVisibility() == ESlateVisibility::HitTestInvisible)
	{
		SetVisibility(VisibilityBeforeHitTestBlock);
	}
}

void UInfoCardWidget::HandleAutoDismissElapsed()
{
	// SetPinned clears this timer, so reaching here while pinned would take
	// something re-arming it in between. Cheap insurance either way.
	if (bIsPinned)
	{
		return;
	}

	CloseCard();
}

void UInfoCardWidget::SetPinned(bool bNewPinned)
{
	if (bIsPinned == bNewPinned)
	{
		return;
	}

	// A card already on its way out can't be held: CloseCard has already told
	// the anchor and the teardown is underway.
	if (bNewPinned && IsClosing())
	{
		return;
	}

	bIsPinned = bNewPinned;

	// Either way, the keep-open grace period starts fresh. Time accumulated
	// before a pin mustn't close the card the instant the pin is released.
	KeepOpenOutsideTime = 0.f;

	if (bIsPinned)
	{
		// A dismissing press decided earlier this frame, just before the pin,
		// hasn't been applied yet, and now it mustn't be.
		ClearPendingDismiss();

		// Cleared outright rather than just ignored when it fires, so
		// unpinning can restart it cleanly from the full duration.
		ClearAutoDismissTimer();
	}
	else if (!IsClosing())
	{
		// No-op (beyond clearing) when AutoDismissDuration is 0.
		StartAutoDismissTimer();
	}

	OnPinStateChanged(bIsPinned);

	// Last, because it can close this card: a Highlight card whose trigger
	// went away while it was pinned has nothing else coming to close it. The
	// IsClosing check covers OnPinStateChanged's Blueprint having closed the
	// card itself.
	if (!bIsPinned && !IsClosing())
	{
		if (UInfoCardAnchor* Anchor = Cast<UInfoCardAnchor>(AnchorWidget.Get()))
		{
			Anchor->NotifyCardUnpinned(this);
		}
	}
}

bool UInfoCardWidget::ShouldPressCloseCard(const FVector2D& PressPoint, bool bHasPressPoint) const
{
	if (RemoveCardOnPressing == ECardRemoveOnPressZone::Anywhere)
	{
		return true;
	}

	// A press with no screen position at all — a gamepad button. There is no
	// honest way to ask "did it land on the card", and the cursor's last
	// resting place from whenever the mouse was touched is not an answer.
	// Resolved as "not over anything", which dismisses for every outside-ish
	// zone and holds for InsideAnchorOrCard: a player pressing a pad button
	// is doing something other than reading this card, but they also did not
	// press the card.
	if (!bHasPressPoint)
	{
		return RemoveCardOnPressing != ECardRemoveOnPressZone::InsideAnchorOrCard;
	}

	FVector2D CardMin = FVector2D::ZeroVector;
	FVector2D CardMax = FVector2D::ZeroVector;
	const bool bHasCard     = GetCardViewportRect(CardMin, CardMax);
	const bool bPressOnCard = bHasCard && IsPointInRect(PressPoint, CardMin, CardMax);

	FVector2D IconMin = FVector2D::ZeroVector;
	FVector2D IconMax = FVector2D::ZeroVector;
	const bool bHasIcon       = GetIconViewportRect(IconMin, IconMax);
	const bool bPressOnAnchor = bHasIcon && IsPointInRect(PressPoint, IconMin, IconMax);

	switch (RemoveCardOnPressing)
	{
	case ECardRemoveOnPressZone::InsideAnchorOrCard:
		// Nothing resolved: no rect exists that the press could be "inside"
		// of, so there is nothing to be inside. IsPointOverIconOrCard's own
		// fail-safe answer in that situation is TRUE ("can't tell — assume
		// the player is still engaged"), which is right for keep-open and
		// exactly wrong here, where it would destroy a card that had not
		// finished appearing.
		if (!bHasCard && !bHasIcon)
		{
			return false;
		}

		// Padded, corridor-aware test (the same geometry
		// bKeepOpenUntilCursorLeaves trusts) rather than a bare rect test.
		//
		// The OWN-region version, not IsPointOverIconOrCard. That one also
		// counts every card nested inside this one, which is right for
		// keep-open (the cursor on a sub-dropdown is still engaging with this
		// menu) but here would turn a press inside a nested dropdown into a
		// press "inside" this card too, closing the parent and taking the
		// dropdown the player just pressed down with it.
		return IsPointOverOwnIconOrCard(PressPoint, /*bUnresolvedCountsAsInside=*/true);

	case ECardRemoveOnPressZone::OutsideCard:
		// Without a card rect there is no boundary, and "no boundary" is not
		// the same as "the press was outside it". A card in its first frame
		// or two has no rect yet, and answering true here is how a card could
		// be killed by the click that had just opened something on top of it.
		if (!bHasCard)
		{
			return false;
		}

		// The anchor is deliberately NOT excluded here — that is the entire
		// difference from OutsideAnchorAndCard below. A press on the anchor
		// dismisses, which is what a menu that toggles shut when you press
		// the thing that opened it needs.
		//
		// Bare card rect, no padding: this is a dismissal boundary, not a
		// hover tolerance, and a player pressing eight pixels off the edge of
		// a card meant it.
		return !bPressOnCard;

	case ECardRemoveOnPressZone::OutsideAnchorAndCard:
	default:
		if (!bHasCard)
		{
			return false; // same reasoning as OutsideCard above
		}

		if (bKeepOpenUntilCursorLeaves)
		{
			// A card the cursor is allowed to travel to needs the corridor
			// between icon and card treated as "inside" here too, or a press
			// mid-journey dismisses the very card the player is reaching for.
			//
			// This includes cards nested inside this one, for the same reason
			// keep-open includes them: a press on a sub-dropdown the cursor
			// was allowed to travel to isn't a press away from this card.
			return !IsPointOverIconOrCard(PressPoint);
		}

		// Both rects, as the name has always said. An unresolved ANCHOR rect
		// is tolerable here in a way an unresolved card rect is not: it means
		// the anchor is not currently painted, and a widget that is not on
		// screen is not one the player just pressed.
		return !bPressOnCard && !bPressOnAnchor;
	}
}

void UInfoCardWidget::ClearPendingDismiss()
{
	bPendingDismissFromPress     = false;
	bPendingDismissPressOnAnchor = false;
	bPendingDismissAwaitsRelease = false;
	PendingDismissRealTime       = 0.0;
}

void UInfoCardWidget::RefreshDismissGeometry()
{
	RefreshAnchorGeometry();
	CacheCardViewportRect();
}

void UInfoCardWidget::EvaluateGlobalPress(const FVector2D& PressPointViewport, bool bHasPressPoint)
{
	// Pinned: RemoveCardOnPressing is an automatic close, so no press, wherever
	// it lands, is even considered.
	if (!bCloseOnClickOutside || IsClosing() || bIsPinned)
	{
		return;
	}

	// Already decided and not yet acted on. A second press before the first
	// has been applied changes nothing about the outcome, and re-deciding
	// would re-stamp the consumed frame and re-arm the release wait.
	if (bPendingDismissFromPress)
	{
		return;
	}

	if (!ShouldPressCloseCard(PressPointViewport, bHasPressPoint))
	{
		return;
	}

	// Stamped here, inside the pre-processor, which runs before Slate routes
	// this press to any widget and before the player controller sees it. That
	// ordering is the entire point: gameplay code checking
	// WasDismissClickConsumedThisFrame() while handling this same click now
	// gets a true answer. Stamped from a tick, as it was, it was set a frame
	// after everything that might have asked had already run and moved on.
	if (bDismissClickGetsConsumed)
	{
		UInfoCardAnchor::MarkDismissClickConsumed();
	}

	// Captured now rather than recomputed at apply time, against the anchor
	// rect as it stood when the press landed. Independent of WHICH
	// RemoveCardOnPressing zone triggered the dismissal: what matters for
	// "should this reappear" is only where the press went.
	bPendingDismissPressOnAnchor = false;
	bool bPressOnOwnCard = false;
	if (bHasPressPoint)
	{
		FVector2D IconMin = FVector2D::ZeroVector;
		FVector2D IconMax = FVector2D::ZeroVector;
		bPendingDismissPressOnAnchor = GetIconViewportRect(IconMin, IconMax) && IsPointInRect(PressPointViewport, IconMin, IconMax);

		FVector2D CardMin = FVector2D::ZeroVector;
		FVector2D CardMax = FVector2D::ZeroVector;
		bPressOnOwnCard = GetCardViewportRect(CardMin, CardMax) && IsPointInRect(PressPointViewport, CardMin, CardMax);
	}

	// A press that landed on the card itself — only possible for Anywhere and
	// InsideAnchorOrCard — waits for the button to come back up before the
	// card goes away, so whatever the player pressed inside the card gets to
	// finish its click first. See bPendingDismissAwaitsRelease.
	bPendingDismissAwaitsRelease = bPressOnOwnCard;
	// bPendingDismissAwaitsRelease = false;
	
	bPendingDismissFromPress = true;
	PendingDismissRealTime   = FPlatformTime::Seconds();
}

void UInfoCardWidget::ApplyPendingDismiss()
{
	if (!bPendingDismissFromPress)
	{
		return;
	}

	if (IsClosing())
	{
		// Something else got there first — most often a UContextMenuButton
		// inside this very card, closing it from its own OnClicked. Nothing
		// left to do, and the pending flag must not survive to fire again.
		ClearPendingDismiss();
		return;
	}

	// Pinned between the press being judged and now (a pin button inside the
	// card, pressed with the very press that was judged to dismiss it). The
	// pin wins.
	if (bIsPinned)
	{
		ClearPendingDismiss();
		return;
	}

	if (bPendingDismissAwaitsRelease)
	{
		// The mouse-up may never arrive: focus can be lost mid-click, and a
		// widget that takes capture can swallow the release outright. Without
		// this the card would sit open indefinitely with a dismissal it can
		// never apply.
		if (FPlatformTime::Seconds() - PendingDismissRealTime < GInfoCardPendingDismissTimeout)
		{
			return;
		}
	}

	const bool bShouldReappear = bReappearOnClickDismiss
		&& SourceCardType == ECardType::Hover
		&& bPendingDismissPressOnAnchor;

	ClearPendingDismiss();

	CloseCard();

	// Standing in for the MouseEnter that isn't coming: the cursor never
	// left the anchor, so nothing else will ask this entry to trigger
	// again. See FInfoCardSpec::bReappearOnClickDismiss.
	if (bShouldReappear)
	{
		if (UInfoCardAnchor* Anchor = Cast<UInfoCardAnchor>(AnchorWidget.Get()))
		{
			Anchor->RetriggerHoverCardById(CardId);
		}
	}
}

void UInfoCardWidget::TickKeepOpenWhileCursorNearby(float InDeltaTime)
{
	if (!bKeepOpenUntilCursorLeaves || IsClosing())
	{
		return;
	}

	// Pinned: the cursor leaving is exactly what a pin holds against. The
	// accumulator is held at zero so unpinning starts a fresh grace period.
	if (bIsPinned)
	{
		KeepOpenOutsideTime = 0.f;
		return;
	}

	// A card that has never been laid out has no rect of its own to test
	// against, and the anchor's own geometry may still be settling on the
	// frame the card spawns. Closing on that evidence is how a card could
	// appear and vanish in the same breath, so the check simply doesn't run
	// until there's something real to check.
	if (FramesSinceConstruct < 2)
	{
		KeepOpenOutsideTime = 0.f;
		return;
	}

	if (IsCursorOverIconOrCard())
	{
		KeepOpenOutsideTime = 0.f;
		return;
	}

	KeepOpenOutsideTime += InDeltaTime;

	// Grace period rather than an immediate close: the cursor legitimately
	// passes outside the region for a frame or two during fast movement, and
	// a single such frame is not the player disengaging. Any frame back
	// inside resets the accumulator above, so a cursor that keeps returning
	// never accumulates enough to close anything.
	if (KeepOpenOutsideTime >= KeepOpenGracePeriod)
	{
		CloseCard();
	}
}

bool UInfoCardWidget::IsPointInRect(const FVector2D& Point, const FVector2D& Min, const FVector2D& Max)
{
	return Point.X >= Min.X && Point.X <= Max.X &&
	       Point.Y >= Min.Y && Point.Y <= Max.Y;
}

bool UInfoCardWidget::GetIconViewportRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	// A zero-size frame means RefreshAnchorGeometry never got a valid cached
	// widget out of the anchor. Reporting the degenerate rect at the origin as
	// though it were real is what made "is the cursor still here?" answer no
	// on frames where the honest answer is "ask again next frame".
	if (IconFrameSize.X <= KINDA_SMALL_NUMBER || IconFrameSize.Y <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	OutMin = IconPositon;
	OutMax = IconPositon + IconFrameSize;
	return true;
}

bool UInfoCardWidget::GetCardViewportRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bHasCardRect)
	{
		return false;
	}

	OutMin = CardRectMin;
	OutMax = CardRectMax;
	return true;
}

void UInfoCardWidget::CacheCardViewportRect()
{
	bool bAnyValid = false;
	FVector2D Min = FVector2D::ZeroVector;
	FVector2D Max = FVector2D::ZeroVector;

	// Source 1: what Slate actually painted. Exact, including any size the
	// card ended up at that GetDesiredSize doesn't predict — but a frame
	// behind, and zero before the first paint.
	const FGeometry& MyGeometry = GetCachedGeometry();
	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	if (LocalSize.X > KINDA_SMALL_NUMBER && LocalSize.Y > KINDA_SMALL_NUMBER)
	{
		FVector2D PixelScratch = FVector2D::ZeroVector;
		FVector2D PaintedMin   = FVector2D::ZeroVector;
		FVector2D PaintedMax   = FVector2D::ZeroVector;

		// Converted into the same viewport space as IconPositon/IconFrameSize
		// (see RefreshAnchorGeometry) and as GetMousePositionOnViewport, so
		// every comparison in IsCursorOverIconOrCard is like-for-like.
		USlateBlueprintLibrary::LocalToViewport(this, MyGeometry, FVector2D::ZeroVector, PixelScratch, PaintedMin);
		USlateBlueprintLibrary::LocalToViewport(this, MyGeometry, LocalSize, PixelScratch, PaintedMax);

		Min = PaintedMin;
		Max = PaintedMax;
		bAnyValid = true;
	}

	// Source 2: where UpdatePosition actually PUT this card, last time it ran.
	// Available immediately (including on the very frame the card is first
	// placed) and already correct for a card that's still moving.
	//
	// This used to call GetTargetPosition() instead, which is a different
	// question — it recomputes placement from scratch, and for
	// PositionMode == ToCursor that recomputation reads the live cursor. A
	// card with UpdatesPositionOnTick off therefore had its rect unioned
	// every frame with a phantom copy of itself sitting under the mouse, so
	// "did the press land on the card?" was true almost everywhere the player
	// could click and the card could never be dismissed. See
	// LastAppliedPosition.
	const FVector2D ComputedSize = GetCardDesiredSize();
	if (bHasAppliedPosition && ComputedSize.X > KINDA_SMALL_NUMBER && ComputedSize.Y > KINDA_SMALL_NUMBER)
	{
		const FVector2D ComputedMin = LastAppliedPosition;
		const FVector2D ComputedMax = ComputedMin + ComputedSize;

		if (bAnyValid)
		{
			Min.X = FMath::Min(Min.X, ComputedMin.X);
			Min.Y = FMath::Min(Min.Y, ComputedMin.Y);
			Max.X = FMath::Max(Max.X, ComputedMax.X);
			Max.Y = FMath::Max(Max.Y, ComputedMax.Y);
		}
		else
		{
			Min = ComputedMin;
			Max = ComputedMax;
			bAnyValid = true;
		}
	}

	if (bAnyValid)
	{
		CardRectMin  = Min;
		CardRectMax  = Max;
		bHasCardRect = true;
	}
}

bool UInfoCardWidget::IsCursorOverIconOrCard() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		// No world to read a cursor position from is another "can't tell yet",
		// and it fails the same way everything else here does: in favour of
		// keeping the card up.
		return true;
	}

	return IsPointOverIconOrCard(UWidgetLayoutLibrary::GetMousePositionOnViewport(World));
}

bool UInfoCardWidget::IsPointOverIconOrCard(const FVector2D& ViewportPoint) const
{
	// This card's own icon/card/corridor first: by far the common answer, and
	// the only part that doesn't need to look at any other card.
	if (IsPointOverOwnIconOrCard(ViewportPoint, /*bUnresolvedCountsAsInside=*/true))
	{
		return true;
	}

	// Then every card nested inside this one, at any depth. This is what lets
	// Keep Open Until Cursor Leaves cascade: a context menu, a dropdown
	// anchored inside it, a dropdown anchored inside THAT, and so on, all stay
	// open while the cursor is anywhere along the chain. This used to stop at
	// this card's own rects, so reaching a second-level dropdown (which sits
	// outside the first-level dropdown's rect) closed the first-level one,
	// and the second-level one, whose anchor lives inside it, went with it.
	return IsPointOverDescendantCard(ViewportPoint);
}

bool UInfoCardWidget::IsPointOverDescendantCard(const FVector2D& ViewportPoint) const
{
	// Iterated directly rather than over a snapshot: nothing below can close a
	// card or otherwise touch ActiveCards.
	for (const TWeakObjectPtr<UInfoCardWidget>& WeakOther : ActiveCards)
	{
		const UInfoCardWidget* Other = WeakOther.Get();
		if (!Other || Other == this || Other->IsDesignTime())
		{
			continue;
		}

		// Same filter AvoidOverlappingCards uses. A card mid-construction has
		// no real rect yet, and one already closing is about to stop existing;
		// neither is somewhere the player is engaging.
		if (!Other->bHasBeenConstructed || Other->IsClosing())
		{
			continue;
		}

		// Asked from the descendant's side, since a card knows its parent (via
		// its anchor) but not its children. IsAncestorCard climbs the whole
		// chain, so a grandchild answers yes here exactly like a child does.
		if (!Other->IsAncestorCard(this))
		{
			continue;
		}

		// The descendant's OWN region only. Its own descendants are already in
		// this same loop, as descendants of this card, so recursing through
		// their IsPointOverIconOrCard would just test them twice.
		if (Other->IsPointOverOwnIconOrCard(ViewportPoint, /*bUnresolvedCountsAsInside=*/false))
		{
			return true;
		}
	}

	return false;
}

bool UInfoCardWidget::IsPointOverOwnIconOrCard(const FVector2D& ViewportPoint, bool bUnresolvedCountsAsInside) const
{
	FVector2D IconMin = FVector2D::ZeroVector;
	FVector2D IconMax = FVector2D::ZeroVector;
	FVector2D CardMin = FVector2D::ZeroVector;
	FVector2D CardMax = FVector2D::ZeroVector;

	const bool bHasIcon = GetIconViewportRect(IconMin, IconMax);
	const bool bHasCard = GetCardViewportRect(CardMin, CardMax);

	if (!bHasIcon && !bHasCard)
	{
		// Nothing resolved yet. For this card's own keep-open test that means
		// "can't tell, keep it up" (see IsCursorOverIconOrCard's header
		// comment). For a descendant being asked on its parent's behalf it
		// means "not somewhere the cursor can be". The caller decides which.
		return bUnresolvedCountsAsInside;
	}

	// HoverAnchorPadding is the visual gap PositionToIcon leaves between the
	// icon and the card, and KeepOpenSlack is the extra tolerance on top. Both
	// are applied to both rects: padding alone is frequently 0 (it's a
	// placement value, not a tolerance), and an Offset can push the card
	// further away than the padding accounts for, either of which leaves a
	// strip of dead space the cursor has to cross.
	const float Pad = FMath::Max(0.f, HoverAnchorPadding) + FMath::Max(0.f, KeepOpenSlack);
	const FVector2D PadVec(Pad, Pad);

	if (bHasIcon)
	{
		IconMin -= PadVec;
		IconMax += PadVec;

		if (IsPointInRect(ViewportPoint, IconMin, IconMax))
		{
			return true;
		}
	}

	if (bHasCard)
	{
		CardMin -= PadVec;
		CardMax += PadVec;

		if (IsPointInRect(ViewportPoint, CardMin, CardMax))
		{
			return true;
		}
	}

	if (!bHasIcon || !bHasCard)
	{
		return false;
	}

	// The corridor between the two. Even padded, icon and card can end up
	// genuinely disjoint (a large Offset, a clamped-to-viewport card that got
	// pushed away from its anchor), and the straight line the player's hand
	// draws between them runs right through the gap. Rather than widen the
	// padding until the two rects happen to touch — which would also make the
	// card cling on far off to the sides — the strip that actually spans the
	// gap is treated as inside.
	//
	// Built on whichever axis the two rects overlap in: that overlap is the
	// corridor's width, and the space between their facing edges is its
	// length. If they overlap on both axes they already intersect and this
	// can't be reached; if they overlap on neither (a diagonal placement far
	// enough out that even the half-card shift doesn't line them up), there's
	// no defensible corridor to draw and the cursor is treated as outside.
	const bool bOverlapX = IconMin.X <= CardMax.X && CardMin.X <= IconMax.X;
	const bool bOverlapY = IconMin.Y <= CardMax.Y && CardMin.Y <= IconMax.Y;

	FVector2D BridgeMin = FVector2D::ZeroVector;
	FVector2D BridgeMax = FVector2D::ZeroVector;

	if (bOverlapX)
	{
		BridgeMin.X = FMath::Max(IconMin.X, CardMin.X);
		BridgeMax.X = FMath::Min(IconMax.X, CardMax.X);
		BridgeMin.Y = FMath::Min(IconMax.Y, CardMax.Y);
		BridgeMax.Y = FMath::Max(IconMin.Y, CardMin.Y);
	}
	else if (bOverlapY)
	{
		BridgeMin.Y = FMath::Max(IconMin.Y, CardMin.Y);
		BridgeMax.Y = FMath::Min(IconMax.Y, CardMax.Y);
		BridgeMin.X = FMath::Min(IconMax.X, CardMax.X);
		BridgeMax.X = FMath::Max(IconMin.X, CardMin.X);
	}
	else
	{
		return false;
	}

	if (BridgeMin.X > BridgeMax.X || BridgeMin.Y > BridgeMax.Y)
	{
		return false;
	}

	return IsPointInRect(ViewportPoint, BridgeMin, BridgeMax);
}

bool UInfoCardWidget::IsCursorOverAnchorIcon() const
{
	// Unlike IsCursorOverIconOrCard, an unresolved rect here means "not over
	// it" rather than "can't tell yet, assume yes".
	//
	// Kept for subclasses, but no longer on the dismissal path itself:
	// EvaluateGlobalPress now tests where the PRESS landed rather than
	// where the cursor is at the moment the card gets round to reacting, and
	// those differ by however far the cursor travelled in between.
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FVector2D IconMin = FVector2D::ZeroVector;
	FVector2D IconMax = FVector2D::ZeroVector;
	if (!GetIconViewportRect(IconMin, IconMax))
	{
		return false;
	}

	const FVector2D MousePos = UWidgetLayoutLibrary::GetMousePositionOnViewport(World);
	return IsPointInRect(MousePos, IconMin, IconMax);
}

namespace
{
	/**
	 * A rect AvoidOverlappingCards must keep this card clear of, in the same
	 * viewport space as CardRectMin/Max. For an unrelated card this is simply
	 * that card's rect; for one of this card's own ancestors it's only the
	 * part of that ancestor this card isn't allowed to cover (see
	 * GetParentCardAvoidRect / GetAncestorCardAvoidRect).
	 */
	struct FInfoCardAvoidRect
	{
		FVector2D Min = FVector2D::ZeroVector;
		FVector2D Max = FVector2D::ZeroVector;

		/**
		 * True for the rect standing in for the card this card is directly
		 * anchored inside of. A slide that continues along a ToIcon card's
		 * placement axis only hops past this one as a last resort: it puts
		 * the card on the far side of its own menu, a whole card's length
		 * away from the row it belongs to.
		 */
		bool bIsParent = false;
	};

	/** The four directions AvoidOverlappingCards can slide a card to get it clear of its own ancestors. */
	enum class EInfoCardSlide : uint8
	{
		Left,
		Right,
		Up,
		Down,
	};

	/**
	 * Shaved off every side of an ancestor's avoid rect. Two cards laid out
	 * exactly flush against each other can come out overlapping by a
	 * fraction of a unit once painted geometry has been converted back into
	 * viewport space; that's float noise, not a card covering its menu.
	 */
	constexpr float GInfoCardAncestorOverlapTolerance = 0.5f;

	/**
	 * Cost multiplier for a slide that DOESN'T continue along a ToIcon card's
	 * own placement axis (see AvoidOverlappingCards). Moving a Left card
	 * further left keeps it lined up with its row exactly as authored;
	 * moving it up or down by the same distance detaches it from that row,
	 * so it has to be clearly shorter to win. Not so large that a card
	 * poking a few units into a grandparent's side gets sent a whole card's
	 * height away instead of just stepping sideways.
	 */
	constexpr float GInfoCardSidestepCostMultiplier = 2.f;

	/** Two slides whose costs are within this many viewport units of each other are treated as a tie. */
	constexpr float GInfoCardSlideCostTieTolerance = 1.f;

	/** Bounds how many cards a single slide may hop past before it gives up. */
	constexpr int32 GInfoCardMaxSlideHops = 16;

	/** Strict overlap: rects that only share an edge don't count, so cards laid out exactly flush never read as colliding. */
	bool InfoCardRectsOverlap(const FVector2D& AMin, const FVector2D& AMax, const FVector2D& BMin, const FVector2D& BMax)
	{
		return AMin.X < BMax.X && BMin.X < AMax.X && AMin.Y < BMax.Y && BMin.Y < AMax.Y;
	}

	bool InfoCardOverlapsAnyAvoidRect(const FVector2D& Position, const FVector2D& CardSize, const TArray<FInfoCardAvoidRect>& Rects)
	{
		const FVector2D Max = Position + CardSize;
		for (const FInfoCardAvoidRect& Rect : Rects)
		{
			if (InfoCardRectsOverlap(Position, Max, Rect.Min, Rect.Max))
			{
				return true;
			}
		}

		return false;
	}

	/**
	 * Slides a CardSize card from Start in one direction, hopping it past
	 * every rect it runs into (leaving Padding between them), until it
	 * overlaps none of Rects. Only ever moves along the one axis Slide points
	 * down; the other stays exactly where it started.
	 *
	 * False if the card would have to leave the viewport (inset by Padding)
	 * to get clear, if it runs out of hops, or — when bParentBlocksSlide is
	 * set — if it would have to hop past the parent's rect at all.
	 */
	bool InfoCardSlideClear(
		const FVector2D& Start,
		const FVector2D& CardSize,
		EInfoCardSlide Slide,
		const TArray<FInfoCardAvoidRect>& Rects,
		float Padding,
		const FVector2D& ViewportSize,
		bool bParentBlocksSlide,
		FVector2D& OutPosition)
	{
		const bool bAlongX = Slide == EInfoCardSlide::Left || Slide == EInfoCardSlide::Right;

		FVector2D Position = Start;

		for (int32 Hop = 0; Hop < GInfoCardMaxSlideHops; ++Hop)
		{
			const FVector2D Max = Position + CardSize;

			bool bHitAny = false;
			double Target = bAlongX ? Position.X : Position.Y;

			// Every rect overlapped at this spot is cleared in one hop, to the
			// furthest of their edges, so two rects stacked along the slide
			// can't bounce the card between them.
			for (const FInfoCardAvoidRect& Rect : Rects)
			{
				if (!InfoCardRectsOverlap(Position, Max, Rect.Min, Rect.Max))
				{
					continue;
				}

				if (bParentBlocksSlide && Rect.bIsParent)
				{
					return false;
				}

				bHitAny = true;

				switch (Slide)
				{
				case EInfoCardSlide::Left:  Target = FMath::Min(Target, Rect.Min.X - Padding - CardSize.X); break;
				case EInfoCardSlide::Right: Target = FMath::Max(Target, Rect.Max.X + Padding);              break;
				case EInfoCardSlide::Up:    Target = FMath::Min(Target, Rect.Min.Y - Padding - CardSize.Y); break;
				case EInfoCardSlide::Down:  Target = FMath::Max(Target, Rect.Max.Y + Padding);              break;
				}
			}

			if (!bHitAny)
			{
				OutPosition = Position;
				return true;
			}

			if (bAlongX)
			{
				Position.X = Target;
			}
			else
			{
				Position.Y = Target;
			}

			// Getting clear by sliding off the screen isn't getting clear.
			// Checked whether or not this card clamps: a card that doesn't is
			// allowed to be authored partly off-screen, but never pushed
			// there by this.
			if (!ViewportSize.IsNearlyZero())
			{
				const double AxisPosition = bAlongX ? Position.X     : Position.Y;
				const double AxisExtent   = bAlongX ? CardSize.X     : CardSize.Y;
				const double AxisViewport = bAlongX ? ViewportSize.X : ViewportSize.Y;

				if (AxisPosition < Padding || AxisPosition + AxisExtent > AxisViewport - Padding)
				{
					return false;
				}
			}
		}

		return false;
	}
}

FVector2D UInfoCardWidget::AvoidOverlappingCards(const FVector2D& InPosition, const FVector2D& CardSize) const
{
	const float EffectivePadding = FMath::Max(0.f, ClampPadding);

	// This card's ancestor chain, nearest first. Built once up front rather
	// than climbing it again (via IsAncestorCard) for every card in
	// ActiveCards. Same bound and same cycle safety as IsAncestorCard.
	TArray<const UInfoCardWidget*> AncestorChain;
	{
		const UInfoCardWidget* Current = this;
		for (int32 Depth = 0; Depth < 16; ++Depth)
		{
			const UInfoCardWidget* Parent = Current->GetParentCard();
			if (!Parent || Parent == this || AncestorChain.Contains(Parent))
			{
				break;
			}

			AncestorChain.Add(Parent);
			Current = Parent;
		}
	}

	// Only a ToIcon card has a defined side of its anchor it's meant to sit
	// on, and that is exactly what tells "flush against the menu it came
	// out of" apart from "covering that menu". A ToCursor or ToViewport card
	// has no such relationship with the cards it's nested in, so for those
	// the whole ancestor chain stays exempt, exactly as it always was.
	const bool bKeepClearOfAncestors = PositionMode == EInfoCardPosition::ToIcon && !IsFillingIcon();

	TArray<FInfoCardAvoidRect> PeerRects;
	TArray<FInfoCardAvoidRect> AncestorRects;

	for (const TWeakObjectPtr<UInfoCardWidget>& WeakOther : ActiveCards)
	{
		const UInfoCardWidget* Other = WeakOther.Get();
		if (!Other || Other == this || !Other->bHasCardRect)
		{
			continue;
		}

		// A card that is mid-construction or already closing isn't
		// something to yield screen space to: its cached rect is either
		// not real yet or about to stop existing, and nudging below it
		// leaves this card visibly offset from where it belongs once the
		// other one finishes appearing or disappearing.
		if (!Other->bHasBeenConstructed || Other->IsClosing())
		{
			continue;
		}

		// A Fill Icon card only covers its own icon, which is part of the UI
		// underneath rather than a card competing for screen space, so
		// there's nothing to yield to.
		if (Other->IsFillingIcon())
		{
			continue;
		}

		// Only ever yield to a card that was shown before this one. If
		// both cards deferred to each other, two overlapping cards would
		// each nudge away every tick and visibly jitter; making the
		// ordering strictly one-directional gives every pair a single,
		// stable winner. (An ancestor always passes this: a card can't
		// exist before the menu its anchor lives in.)
		if (Other->SpawnSequence >= SpawnSequence)
		{
			continue;
		}

		const int32 ChainIndex = AncestorChain.IndexOfByKey(Other);
		if (ChainIndex == INDEX_NONE)
		{
			FInfoCardAvoidRect& Peer = PeerRects.AddDefaulted_GetRef();
			Peer.Min = Other->CardRectMin;
			Peer.Max = Other->CardRectMax;
			continue;
		}

		// One of this card's own ancestors. A submenu is SUPPOSED to sit
		// flush against (often touching) the menu it's anchored inside of —
		// that's not the "two unrelated tooltips fighting for the same spot"
		// case the push-down below exists for, and treating the parent's
		// whole rect as an obstacle would shove a submenu anchored to a row
		// below the parent's entire bottom edge. So ancestors are never
		// pushed against wholesale; only the part of each one this card has
		// no business covering is kept clear, and that's resolved separately
		// further down, by sliding rather than pushing.
		if (!bKeepClearOfAncestors)
		{
			continue;
		}

		FInfoCardAvoidRect Guarded;
		const bool bHasGuardedRect = ChainIndex == 0
			? GetParentCardAvoidRect(Other, CardSize, Guarded.Min, Guarded.Max)
			: GetAncestorCardAvoidRect(AncestorChain, ChainIndex, Guarded.Min, Guarded.Max);

		if (bHasGuardedRect)
		{
			Guarded.bIsParent = ChainIndex == 0;
			AncestorRects.Add(Guarded);
		}
	}

	// ------------------------------------------------------------------
	// Pass 1: unrelated cards. Unchanged from how this has always worked.
	// ------------------------------------------------------------------

	FVector2D Min = InPosition;
	FVector2D Max = InPosition + CardSize;

	bool bMoved = true;
	int32 SafetyIterations = 0;

	// Iterative relaxation, same shape as PositionToIcon's own flip/clamp
	// pass: resolving one overlap can create another against a third card,
	// so this keeps going until nothing overlaps or the safety cap gives up
	// (a cluster of cards that genuinely can't all fit without overlapping
	// settles for "mostly clear" rather than looping forever).
	while (bMoved && SafetyIterations++ < 8)
	{
		bMoved = false;

		for (const FInfoCardAvoidRect& Peer : PeerRects)
		{
			if (!InfoCardRectsOverlap(Min, Max, Peer.Min, Peer.Max))
			{
				continue;
			}

			// Push straight down below the older card — the same direction
			// toast notifications stack in, and simpler/more predictable
			// than trying to pick a side to slide toward.
			const float PushTo = Peer.Max.Y + EffectivePadding;
			Min.Y = PushTo;
			Max.Y = PushTo + CardSize.Y;
			bMoved = true;
		}
	}

	// Re-clamped the same way a flipped ToIcon position is: nudging to avoid
	// one card must never be allowed to push this one off the screen.
	const FVector2D PeerResolved = ClampToViewport(Min, CardSize);

	// Nothing in this card's own chain is being covered: done, and exactly
	// the answer this function always gave.
	if (AncestorRects.Num() == 0 || !InfoCardOverlapsAnyAvoidRect(PeerResolved, CardSize, AncestorRects))
	{
		return PeerResolved;
	}

	// ------------------------------------------------------------------
	// Pass 2: this card is covering part of its own ancestor chain.
	// ------------------------------------------------------------------
	//
	// Typically a dropdown that flipped up over the very menu it came out
	// of, or a cascading card whose placement lands it on top of a menu
	// further up the chain. Pushing down is the wrong fix here — it would
	// just move the card along the column it's already stacked in — so
	// instead the card slides in each of the four directions until it's
	// clear of everything (ancestors AND unrelated cards), and the best of
	// those slides wins.
	//
	// Started from InPosition, where placement actually put the card, not
	// from the push-down result, which was only ever answering a narrower
	// question.

	TArray<FInfoCardAvoidRect> AllRects = PeerRects;
	AllRects.Append(AncestorRects);

	const FVector2D Start        = ClampToViewport(InPosition, CardSize);
	const FVector2D StartCenter  = Start + (CardSize * 0.5f);
	const FVector2D IconCenter   = IconPositon + (IconFrameSize * 0.5f);
	const FVector2D ViewportSize = GetViewportSizeInViewportUnits();

	const bool bPlacedAlongX = IconPosition == EDirection2D::Left || IconPosition == EDirection2D::Right;
	const bool bPlacedAlongY = IconPosition == EDirection2D::Up   || IconPosition == EDirection2D::Down;
	const bool bIsCardinal   = bPlacedAlongX || bPlacedAlongY;

	// Tie-break reference: the outermost ancestor with a rect. A cascade
	// grows away from its root menu, so between two equally good sidesteps
	// (a column of dropdowns can clear itself just as cheaply on either side)
	// the one that keeps growing outward is picked rather than the one that
	// doubles back over the menus it came from.
	FVector2D RootCenter = StartCenter;
	for (int32 Index = AncestorChain.Num() - 1; Index >= 0; --Index)
	{
		if (AncestorChain[Index]->bHasCardRect)
		{
			RootCenter = (AncestorChain[Index]->CardRectMin + AncestorChain[Index]->CardRectMax) * 0.5f;
			break;
		}
	}

	static const EInfoCardSlide AllSlides[] = { EInfoCardSlide::Left, EInfoCardSlide::Right, EInfoCardSlide::Up, EInfoCardSlide::Down };

	bool bFoundSlide = false;
	FVector2D BestPosition = PeerResolved;
	double BestCost = 0.;
	bool bBestAwayFromRoot = false;

	// Two attempts. The first never lets a slide along the placement axis
	// hop past the parent (see below). Only if nothing at all works under
	// that rule does the second allow it: a card sitting clear of its own
	// menu, a card's length from its row, still beats one covering that
	// menu's rows. The second attempt only re-tries the slides that rule
	// actually affected; every other slide already came out the same way.
	for (int32 Attempt = 0; Attempt < 2 && !bFoundSlide; ++Attempt)
	{
		const bool bMayHopParent = Attempt == 1;

		if (bMayHopParent && !bIsCardinal)
		{
			break; // diagonals never carve a parent rect, so there's nothing different to try
		}

		for (EInfoCardSlide Slide : AllSlides)
		{
			const bool bSlideAlongX    = Slide == EInfoCardSlide::Left || Slide == EInfoCardSlide::Right;
			const bool bSlideNegative  = Slide == EInfoCardSlide::Left || Slide == EInfoCardSlide::Up;
			const bool bAlongPlacement = bSlideAlongX ? bPlacedAlongX : bPlacedAlongY;

			if (bMayHopParent && !bAlongPlacement)
			{
				continue;
			}

			// Along the axis this card was placed on (Left/Right for a Left or
			// Right card, Up/Down for an Up or Down card), only the direction
			// that carries on AWAY from the icon — from whichever side of it
			// the card ended up on, flipped or not — is considered. Going the
			// other way means crossing back over the icon and its menu. And,
			// on the first attempt, that continuing slide may not hop past the
			// parent either (see FInfoCardAvoidRect::bIsParent): a dropdown
			// that flipped up over its menu doesn't get fixed by jumping above
			// the whole menu, it gets fixed by stepping sideways out of it.
			//
			// Diagonals point along both axes at once and have no single
			// placement axis, so all four slides are fair game for them, at
			// plain cost.
			if (bAlongPlacement)
			{
				const bool bCardOnNegativeSide = bSlideAlongX ? (StartCenter.X < IconCenter.X) : (StartCenter.Y < IconCenter.Y);
				if (bCardOnNegativeSide != bSlideNegative)
				{
					continue;
				}
			}

			FVector2D Candidate = FVector2D::ZeroVector;
			if (!InfoCardSlideClear(Start, CardSize, Slide, AllRects, EffectivePadding, ViewportSize, /*bParentBlocksSlide=*/bAlongPlacement && !bMayHopParent, Candidate))
			{
				continue;
			}

			// Normally a no-op (the slide already refused to leave the viewport
			// on the axis it moved along, and the other axis is untouched), but
			// if clamping does move it, the clamped spot is what would actually
			// be shown, so that's the one that has to be clear.
			Candidate = ClampToViewport(Candidate, CardSize);
			if (InfoCardOverlapsAnyAvoidRect(Candidate, CardSize, AllRects))
			{
				continue;
			}

			double Cost = FVector2D::Distance(Candidate, Start);
			if (bIsCardinal && !bAlongPlacement)
			{
				Cost *= GInfoCardSidestepCostMultiplier;
			}

			const double StartAxis = bSlideAlongX ? StartCenter.X : StartCenter.Y;
			const double RootAxis  = bSlideAlongX ? RootCenter.X  : RootCenter.Y;
			const bool bAwayFromRoot = bSlideNegative ? (StartAxis <= RootAxis) : (StartAxis >= RootAxis);

			const bool bClearlyCheaper = Cost < BestCost - GInfoCardSlideCostTieTolerance;
			const bool bTiedButBetter  = Cost <= BestCost + GInfoCardSlideCostTieTolerance && bAwayFromRoot && !bBestAwayFromRoot;

			if (!bFoundSlide || bClearlyCheaper || bTiedButBetter)
			{
				bFoundSlide       = true;
				BestPosition      = Candidate;
				BestCost          = Cost;
				bBestAwayFromRoot = bAwayFromRoot;
			}
		}
	}

	// Nowhere to go (a screen too crowded for any slide to get clear): settle
	// for the answer this function has always given rather than jumping
	// somewhere arbitrary.
	return bFoundSlide ? BestPosition : PeerResolved;
}

bool UInfoCardWidget::GetParentCardAvoidRect(const UInfoCardWidget* Parent, const FVector2D& CardSize, FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!Parent || !Parent->bHasCardRect || PositionMode != EInfoCardPosition::ToIcon)
	{
		return false;
	}

	FVector2D IconMin = FVector2D::ZeroVector;
	FVector2D IconMax = FVector2D::ZeroVector;
	if (!GetIconViewportRect(IconMin, IconMax))
	{
		return false; // no icon to measure against yet; parent stays exempt this frame
	}

	const FVector2D ParentMin = Parent->CardRectMin;
	const FVector2D ParentMax = Parent->CardRectMax;

	// Where this card's own IconPosition puts it before any flip or clamp.
	// Offset and HoverAnchorPadding are both baked in, so a designer who
	// deliberately pulled the card back over its menu gets exactly that.
	const FVector2D Authored    = PositionToIconRaw(IconPosition, CardSize, Offset.X, Offset.Y);
	const FVector2D AuthoredMax = Authored + CardSize;

	OutMin = ParentMin;
	OutMax = ParentMax;

	// Two things carve the parent down to the part this card mustn't cover:
	//
	// - The authored side. Everything from the authored position's leading
	//   edge outward is the card's to cover — it's where it was asked to
	//   go. A Down dropdown hanging over the rows below a mid-menu row is
	//   doing exactly what it was set up to do, and stays untouched.
	//
	// - The same inset, mirrored onto the opposite edge. A card that flips
	//   lands against the far side of the menu, and the icon-to-edge margin
	//   on the authored side is the best available measure of how much
	//   frame (padding, border) surrounds the row. A submenu flipping from
	//   Right to Left only overlaps that margin and still reads as flush;
	//   a dropdown flipping up from a menu's bottom row covers every row
	//   above it, which is far more than that margin and is what gets
	//   caught.
	switch (IconPosition)
	{
	case EDirection2D::Down:
		OutMax.Y = FMath::Min(OutMax.Y, Authored.Y);
		OutMin.Y = ParentMin.Y + FMath::Max(0., ParentMax.Y - IconMax.Y);
		break;

	case EDirection2D::Up:
		OutMin.Y = FMath::Max(OutMin.Y, AuthoredMax.Y);
		OutMax.Y = ParentMax.Y - FMath::Max(0., IconMin.Y - ParentMin.Y);
		break;

	case EDirection2D::Right:
		OutMax.X = FMath::Min(OutMax.X, Authored.X);
		OutMin.X = ParentMin.X + FMath::Max(0., ParentMax.X - IconMax.X);
		break;

	case EDirection2D::Left:
		OutMin.X = FMath::Max(OutMin.X, AuthoredMax.X);
		OutMax.X = ParentMax.X - FMath::Max(0., IconMin.X - ParentMin.X);
		break;

	default:
		// Diagonals are centered on a corner of the icon and deliberately
		// sit over part of it, so there's no clean "authored side" to carve
		// by. The parent stays fully exempt for those, as before.
		return false;
	}

	OutMin += FVector2D(GInfoCardAncestorOverlapTolerance, GInfoCardAncestorOverlapTolerance);
	OutMax -= FVector2D(GInfoCardAncestorOverlapTolerance, GInfoCardAncestorOverlapTolerance);

	return OutMax.X > OutMin.X && OutMax.Y > OutMin.Y;
}

bool UInfoCardWidget::GetAncestorCardAvoidRect(const TArray<const UInfoCardWidget*>& AncestorChain, int32 AncestorIndex, FVector2D& OutMin, FVector2D& OutMax)
{
	if (!AncestorChain.IsValidIndex(AncestorIndex))
	{
		return false;
	}

	const UInfoCardWidget* Ancestor = AncestorChain[AncestorIndex];
	if (!Ancestor || !Ancestor->bHasCardRect)
	{
		return false;
	}

	const FVector2D AncestorMin = Ancestor->CardRectMin;
	const FVector2D AncestorMax = Ancestor->CardRectMax;

	// A card further up the chain than the parent is fully off limits, with
	// one allowance: wherever the cards in between already poke into it,
	// this card may too. That's how a cascade legitimately looks — a
	// submenu overlaps its menu's border by a few units, and a dropdown
	// hanging straight down off that submenu inherits the same few units of
	// overlap with the menu above. Tracked per edge, so an allowance earned
	// along one edge doesn't license covering the ancestor from another.
	double InsetLeft   = 0.;
	double InsetRight  = 0.;
	double InsetTop    = 0.;
	double InsetBottom = 0.;

	for (int32 LinkIndex = 0; LinkIndex < AncestorIndex; ++LinkIndex)
	{
		const UInfoCardWidget* Link = AncestorChain[LinkIndex];
		if (!Link || !Link->bHasCardRect)
		{
			continue;
		}

		const FVector2D LinkMin = Link->CardRectMin;
		const FVector2D LinkMax = Link->CardRectMax;

		if (!InfoCardRectsOverlap(LinkMin, LinkMax, AncestorMin, AncestorMax))
		{
			continue;
		}

		// Only an edge the link actually crosses can earn an allowance (a
		// link sitting entirely inside the ancestor crosses none, and earns
		// none), and of the edges it crosses, only the one it pokes through
		// most shallowly. A submenu hanging off a menu's right edge by a few
		// units and extending well below it crosses both the right and bottom
		// edges, but it's only really overlapping through the right one;
		// granting the bottom allowance too would hand every card further
		// down the chain license to cover most of the menu from below.
		double BestInset = TNumericLimits<double>::Max();
		double* BestEdge = nullptr;

		const auto ConsiderEdge = [&BestInset, &BestEdge](bool bCrosses, double Inset, double& Edge)
		{
			if (bCrosses && Inset < BestInset)
			{
				BestInset = Inset;
				BestEdge  = &Edge;
			}
		};

		ConsiderEdge(LinkMin.X < AncestorMin.X, LinkMax.X - AncestorMin.X, InsetLeft);
		ConsiderEdge(LinkMax.X > AncestorMax.X, AncestorMax.X - LinkMin.X, InsetRight);
		ConsiderEdge(LinkMin.Y < AncestorMin.Y, LinkMax.Y - AncestorMin.Y, InsetTop);
		ConsiderEdge(LinkMax.Y > AncestorMax.Y, AncestorMax.Y - LinkMin.Y, InsetBottom);

		if (BestEdge)
		{
			*BestEdge = FMath::Max(*BestEdge, BestInset);
		}
	}

	OutMin = FVector2D(AncestorMin.X + InsetLeft,  AncestorMin.Y + InsetTop)    + FVector2D(GInfoCardAncestorOverlapTolerance, GInfoCardAncestorOverlapTolerance);
	OutMax = FVector2D(AncestorMax.X - InsetRight, AncestorMax.Y - InsetBottom) - FVector2D(GInfoCardAncestorOverlapTolerance, GInfoCardAncestorOverlapTolerance);

	// Links crossing both opposite edges can eat the whole thing; nothing
	// left to keep clear of.
	return OutMax.X > OutMin.X && OutMax.Y > OutMin.Y;
}

bool UInfoCardWidget::IsAncestorCard(const UInfoCardWidget* PotentialAncestor) const
{
	if (!PotentialAncestor || PotentialAncestor == this)
	{
		return false;
	}

	const UInfoCardWidget* Current = this;

	// Bounded. MaxShowCardDepth (InfoCardAnchor.cpp) caps real nesting well
	// below this, so the cap only exists to stop a misconfigured or cyclic
	// setup from looping forever.
	for (int32 Depth = 0; Depth < 16; ++Depth)
	{
		const UInfoCardWidget* Parent = Current->GetParentCard();
		if (!Parent || Parent == this)
		{
			return false; // reached a root-level card, or looped back around
		}

		if (Parent == PotentialAncestor)
		{
			return true;
		}

		Current = Parent; // keep climbing: a submenu-of-a-submenu still has to recognize its grandparent
	}

	return false;
}

UInfoCardWidget* UInfoCardWidget::GetParentCard() const
{
	if (UInfoCardWidget* Cached = CachedParentCard.Get())
	{
		return Cached;
	}

	// A card with no parent card is the common case (every root-level
	// tooltip), and this is asked several times a frame by the keep-open
	// cascade and AvoidOverlappingCards. Without this, each of those asks
	// would walk the Slate tree all the way to the root to rediscover
	// "nothing".
	if (ParentCardMissFrame == GFrameCounter)
	{
		return nullptr;
	}

	UInfoCardWidget* Found = FindContainingCard(AnchorWidget.Get());
	if (Found == this)
	{
		Found = nullptr; // a card can't be its own parent
	}

	if (Found)
	{
		CachedParentCard = Found;
	}
	else
	{
		ParentCardMissFrame = GFrameCounter;
	}

	return Found;
}

UInfoCardWidget* UInfoCardWidget::FindContainingCard(const UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}

	// 1. Outer chain. Every widget built into a UUserWidget's WidgetTree has
	// that UserWidget as an Outer-chain ancestor, however many panels and
	// nested UserWidgets deep it sits. Cheap, and covers anything laid out in
	// a WBP.
	if (UInfoCardWidget* FromOuter = Widget->GetTypedOuter<UInfoCardWidget>())
	{
		return FromOuter;
	}

	// 2. Live Slate hierarchy. A widget created at runtime with CreateWidget
	// and then added into a card (a menu whose rows are built dynamically) is
	// Outered to the owning player, not the card, so step 1 can't see it. The
	// Slate parent chain always reflects where it's actually displayed; every
	// UUserWidget in it shows up as an SObjectWidget.
	static const FName ObjectWidgetType(TEXT("SObjectWidget"));

	TSharedPtr<SWidget> Current = Widget->GetCachedWidget();
	for (int32 Depth = 0; Current.IsValid() && Depth < 256; ++Depth)
	{
		if (Current->GetType() == ObjectWidgetType)
		{
			const SObjectWidget& ObjectWidget = static_cast<const SObjectWidget&>(*Current);
			UInfoCardWidget* Card = Cast<UInfoCardWidget>(ObjectWidget.GetWidgetObject());
			if (Card && Card != Widget)
			{
				return Card;
			}
		}

		Current = Current->GetParentWidget();
	}

	return nullptr;
}

FVector2D UInfoCardWidget::GetCardDesiredSize() const
{
	// A filling card has been told how big to be, so its content's desired
	// size says nothing about where it actually is on screen. Before a fill
	// size has been applied (icon not resolved yet), the card really is drawn
	// at its content's desired size, so that's still the honest answer.
	if (IsFillingIcon() && bHasAppliedFillSize)
	{
		return LastAppliedFillSize;
	}

	return GetDesiredSize();
}

bool UInfoCardWidget::IsFillingIcon() const
{
	return PositionMode == EInfoCardPosition::ToIcon && IconPosition == EDirection2D::FillIcon;
}

FVector2D UInfoCardWidget::GetFillIconSize() const
{
	const FVector2D Inset(
		FillIconPadding.Left + FillIconPadding.Right,
		FillIconPadding.Top + FillIconPadding.Bottom);

	// Padding larger than the icon leaves nothing to fill; clamp at zero
	// rather than hand the viewport slot a negative size.
	return (IconFrameSize - Inset).ComponentMax(FVector2D::ZeroVector);
}

FVector2D UInfoCardWidget::GetViewportSizeInViewportUnits() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return FVector2D::ZeroVector;
	}

	const float ViewportScale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(World), KINDA_SMALL_NUMBER);
	return UWidgetLayoutLibrary::GetViewportSize(World) / ViewportScale;
}

void UInfoCardWidget::RefreshAnchorGeometry()
{
	UWidget* Anchor = AnchorWidget.Get();
	if (!Anchor || !Anchor->GetCachedWidget().IsValid())
	{
		return;
	}

	const FGeometry& AnchorGeometry = Anchor->GetCachedGeometry();

	FVector2D PixelScratch  = FVector2D::ZeroVector;
	FVector2D ViewportTL    = FVector2D::ZeroVector;
	FVector2D ViewportBR    = FVector2D::ZeroVector;

	USlateBlueprintLibrary::LocalToViewport(this, AnchorGeometry, FVector2D::ZeroVector, PixelScratch, ViewportTL);
	USlateBlueprintLibrary::LocalToViewport(this, AnchorGeometry, AnchorGeometry.GetLocalSize(), PixelScratch, ViewportBR);

	IconPositon   = ViewportTL;
	IconFrameSize = ViewportBR - ViewportTL;
}

void UInfoCardWidget::UpdatePosition()
{
	// Also guards Blueprint calls: an embedded card is positioned by its panel.
	if (bEmbeddedInLayout)
	{
		return;
	}

	if (PositionMode == EInfoCardPosition::ToIcon && !AnchorWidget.IsValid())
	{
		return;
	}

	const bool bFillIcon = IsFillingIcon();

	if (PositionMode == EInfoCardPosition::ToIcon)
	{
		RefreshAnchorGeometry();
	}

	// Fill Icon sizes the card to the icon instead of letting it take its
	// content's desired size. Applied BEFORE the layout prepass and placement
	// below, so everything downstream (GetCardDesiredSize,
	// CacheCardViewportRect) sees the size the card is about to be drawn at.
	//
	// Only pushed when it actually changed: re-sizing the slot invalidates
	// layout, and an icon that isn't changing size shouldn't cost a layout
	// pass every frame.
	//
	// An unresolved icon (a zero-size frame, the first frame or so) is
	// skipped rather than applied. A zero size in a viewport slot means "use
	// the content's desired size", the opposite of filling anything.
	if (bFillIcon)
	{
		const FVector2D FillSize = GetFillIconSize();
		const bool bFillSizeValid = FillSize.X > KINDA_SMALL_NUMBER && FillSize.Y > KINDA_SMALL_NUMBER;

		if (bFillSizeValid && (!bHasAppliedFillSize || !FillSize.Equals(LastAppliedFillSize, 0.01)))
		{
			SetDesiredSizeInViewport(FillSize);

			// Same size pushed into a canvas slot too, for a card that was
			// placed in a canvas rather than added to the viewport.
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
			{
				CanvasSlot->SetAutoSize(false);
				CanvasSlot->SetSize(FillSize);
			}

			LastAppliedFillSize = FillSize;
			bHasAppliedFillSize = true;
		}
	}
	else if (bHasAppliedFillSize)
	{
		// Switched out of Fill Icon at runtime: hand sizing back to the
		// content. Zero is the viewport slot's "use desired size".
		SetDesiredSizeInViewport(FVector2D::ZeroVector);

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			CanvasSlot->SetAutoSize(true);
		}

		LastAppliedFillSize = FVector2D::ZeroVector;
		bHasAppliedFillSize = false;
	}

	ForceLayoutPrepass();

	FVector2D TargetPosition = GetTargetPosition();

	// A filling card is bound to its icon; being nudged away from other cards
	// would detach it from the very thing it exists to cover.
	if (bAvoidOverlappingCards && !bFillIcon)
	{
		TargetPosition = AvoidOverlappingCards(TargetPosition, GetCardDesiredSize());
	}

	SetPositionInViewport(TargetPosition, /*bRemoveDPIScale=*/false);

	// Recorded so CacheCardViewportRect can ask "where was this card put?"
	// without re-running the placement math — see LastAppliedPosition.
	LastAppliedPosition  = TargetPosition;
	bHasAppliedPosition  = true;

	// Fill Icon manages its canvas slot's sizing itself, above.
	if (IsFirstFrame && !bFillIcon)
	{
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			CanvasSlot->SetAutoSize(true);
		}
	}
}

FVector2D UInfoCardWidget::GetTargetPosition() const
{
	switch (PositionMode)
	{
	case EInfoCardPosition::ToIcon:
		return PositionToIcon(IconPosition, Offset.X, Offset.Y);

	case EInfoCardPosition::ToCursor:
		return PositionToCursor(CursorPosition, Offset.X, Offset.Y);

	case EInfoCardPosition::ToViewport:
		return PositionToViewport(ViewportPosition, Offset.X, Offset.Y);

	default:
		return FVector2D::ZeroVector;
	}
}

namespace
{
	/**
	 * Left/Right flip of a placement direction's horizontal component. Up and
	 * Down have no horizontal component (they're centered on the icon) and
	 * come back unchanged — there's nothing to flip.
	 */
	EDirection2D FlipHorizontalDirection(EDirection2D Direction)
	{
		switch (Direction)
		{
		case EDirection2D::Left:      return EDirection2D::Right;
		case EDirection2D::Right:     return EDirection2D::Left;
		case EDirection2D::UpLeft:    return EDirection2D::UpRight;
		case EDirection2D::UpRight:   return EDirection2D::UpLeft;
		case EDirection2D::DownLeft:  return EDirection2D::DownRight;
		case EDirection2D::DownRight: return EDirection2D::DownLeft;
		default:                      return Direction;
		}
	}

	/** Up/Down flip of a placement direction's vertical component. Left and Right have no vertical component and come back unchanged. */
	EDirection2D FlipVerticalDirection(EDirection2D Direction)
	{
		switch (Direction)
		{
		case EDirection2D::Up:        return EDirection2D::Down;
		case EDirection2D::Down:      return EDirection2D::Up;
		case EDirection2D::UpLeft:    return EDirection2D::DownLeft;
		case EDirection2D::DownLeft:  return EDirection2D::UpLeft;
		case EDirection2D::UpRight:   return EDirection2D::DownRight;
		case EDirection2D::DownRight: return EDirection2D::UpRight;
		default:                      return Direction;
		}
	}

	/** Up and Down are purely vertical — nothing to flip horizontally. FillIcon isn't a direction at all. Every other direction (Left, Right, and all four diagonals) has a horizontal component. */
	bool DirectionHasHorizontalComponent(EDirection2D Direction)
	{
		return Direction != EDirection2D::Up && Direction != EDirection2D::Down && Direction != EDirection2D::FillIcon;
	}

	/** Left and Right are purely horizontal — nothing to flip vertically. FillIcon isn't a direction at all. Every other direction has a vertical component. */
	bool DirectionHasVerticalComponent(EDirection2D Direction)
	{
		return Direction != EDirection2D::Left && Direction != EDirection2D::Right && Direction != EDirection2D::FillIcon;
	}

	/**
	 * Left/Right flip of a cursor placement's horizontal component. UpCenter
	 * and DownCenter are centered horizontally on the cursor and come back
	 * unchanged — there's nothing to flip.
	 */
	EInfoCardCursorPosition FlipHorizontalCursorPosition(EInfoCardCursorPosition Position)
	{
		switch (Position)
		{
		case EInfoCardCursorPosition::UpRight:     return EInfoCardCursorPosition::UpLeft;
		case EInfoCardCursorPosition::UpLeft:      return EInfoCardCursorPosition::UpRight;
		case EInfoCardCursorPosition::DownRight:   return EInfoCardCursorPosition::DownLeft;
		case EInfoCardCursorPosition::DownLeft:    return EInfoCardCursorPosition::DownRight;
		case EInfoCardCursorPosition::LeftCenter:  return EInfoCardCursorPosition::RightCenter;
		case EInfoCardCursorPosition::RightCenter: return EInfoCardCursorPosition::LeftCenter;
		default:                                   return Position; // UpCenter, DownCenter
		}
	}

	/** Up/Down flip of a cursor placement's vertical component. LeftCenter and RightCenter are centered vertically on the cursor and come back unchanged. */
	EInfoCardCursorPosition FlipVerticalCursorPosition(EInfoCardCursorPosition Position)
	{
		switch (Position)
		{
		case EInfoCardCursorPosition::UpRight:     return EInfoCardCursorPosition::DownRight;
		case EInfoCardCursorPosition::DownRight:   return EInfoCardCursorPosition::UpRight;
		case EInfoCardCursorPosition::UpLeft:      return EInfoCardCursorPosition::DownLeft;
		case EInfoCardCursorPosition::DownLeft:    return EInfoCardCursorPosition::UpLeft;
		case EInfoCardCursorPosition::UpCenter:    return EInfoCardCursorPosition::DownCenter;
		case EInfoCardCursorPosition::DownCenter:  return EInfoCardCursorPosition::UpCenter;
		default:                                   return Position; // LeftCenter, RightCenter
		}
	}

	/** Every EInfoCardCursorPosition except UpCenter/DownCenter has a horizontal component — those two are centered on the cursor's X. */
	bool CursorPositionHasHorizontalComponent(EInfoCardCursorPosition Position)
	{
		return Position != EInfoCardCursorPosition::UpCenter && Position != EInfoCardCursorPosition::DownCenter;
	}

	/** Every EInfoCardCursorPosition except LeftCenter/RightCenter has a vertical component — those two are centered on the cursor's Y. */
	bool CursorPositionHasVerticalComponent(EInfoCardCursorPosition Position)
	{
		return Position != EInfoCardCursorPosition::LeftCenter && Position != EInfoCardCursorPosition::RightCenter;
	}

	/** Whether Position..Position+Extent fits inside [Padding, ViewportExtent - Padding] on one axis. */
	bool FitsWithPadding(float Position, float Extent, float ViewportExtent, float Padding)
	{
		return Position >= Padding && Position + Extent <= ViewportExtent - Padding;
	}
}

FVector2D UInfoCardWidget::ClampToViewport(const FVector2D& InPosition, const FVector2D& CardSize) const
{
	if (!bClampToViewport)
	{
		return InPosition;
	}

	const FVector2D ViewportSize = GetViewportSizeInViewportUnits();
	if (ViewportSize.IsNearlyZero())
	{
		return InPosition;
	}

	// ClampPadding is the same margin PositionToIcon uses to decide a ToIcon
	// card needs to flip in the first place, so a card that flip resolved
	// already satisfies this and passes through unchanged; a card that
	// couldn't be resolved by flipping (or was never flippable on this axis
	// to begin with — ToCursor, ToViewport, or the axis perpendicular to a
	// ToIcon card's IconPosition) gets pinned here instead, with Padding kept
	// as spacing from the literal screen edge rather than touching it.
	const float EffectivePadding = FMath::Max(0.f, ClampPadding);

	FVector2D Result = InPosition;
	Result.X = FMath::Clamp(Result.X, EffectivePadding, FMath::Max(EffectivePadding, ViewportSize.X - CardSize.X - EffectivePadding));
	Result.Y = FMath::Clamp(Result.Y, EffectivePadding, FMath::Max(EffectivePadding, ViewportSize.Y - CardSize.Y - EffectivePadding));
	return Result;
}

FVector2D UInfoCardWidget::PositionToIconRaw(EDirection2D TryPosition, const FVector2D& CardSize, float OffsetX, float OffsetY) const
{
	const FVector2D IconCenter = IconPositon + (IconFrameSize * 0.5f);

	FVector2D Result = FVector2D::ZeroVector;

	switch (TryPosition)
	{
	case EDirection2D::Up:
		Result.X = IconCenter.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = IconPositon.Y - CardSize.Y - HoverAnchorPadding - OffsetY;
		break;

	case EDirection2D::Down:
		Result.X = IconCenter.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = IconPositon.Y + IconFrameSize.Y + HoverAnchorPadding + OffsetY;
		break;

	case EDirection2D::Left:
		Result.X = IconPositon.X - CardSize.X - HoverAnchorPadding - OffsetX;
		Result.Y = IconCenter.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EDirection2D::Right:
		Result.X = IconPositon.X + IconFrameSize.X + HoverAnchorPadding + OffsetX;
		Result.Y = IconCenter.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EDirection2D::UpLeft:
		Result.X = IconPositon.X - (CardSize.X * 0.5f) - HoverAnchorPadding - OffsetX;
		Result.Y = IconPositon.Y - (CardSize.Y * 0.5f) - HoverAnchorPadding - OffsetY;
		break;

	case EDirection2D::UpRight:
		Result.X = IconPositon.X + IconFrameSize.X - (CardSize.X * 0.5f) + HoverAnchorPadding + OffsetX;
		Result.Y = IconPositon.Y - (CardSize.Y * 0.5f) - HoverAnchorPadding - OffsetY;
		break;

	case EDirection2D::DownLeft:
		Result.X = IconPositon.X - (CardSize.X * 0.5f) - HoverAnchorPadding - OffsetX;
		Result.Y = IconPositon.Y + IconFrameSize.Y - (CardSize.Y * 0.5f) + HoverAnchorPadding + OffsetY;
		break;

	case EDirection2D::DownRight:
		Result.X = IconPositon.X + IconFrameSize.X - (CardSize.X * 0.5f) + HoverAnchorPadding + OffsetX;
		Result.Y = IconPositon.Y + IconFrameSize.Y - (CardSize.Y * 0.5f) + HoverAnchorPadding + OffsetY;
		break;

	case EDirection2D::FillIcon:
		// The icon's top-left, inset by the padding's Left/Top. The matching
		// size (the icon's, minus the padding on both sides) isn't a position
		// and is applied separately, by UpdatePosition. CardSize and
		// HoverAnchorPadding don't apply: a filling card is the size of the
		// icon, sitting on top of it rather than beside it.
		Result.X = IconPositon.X + FillIconPadding.Left + OffsetX;
		Result.Y = IconPositon.Y + FillIconPadding.Top + OffsetY;
		break;
	}

	return Result;
}

FVector2D UInfoCardWidget::PositionToIcon(EDirection2D TryPosition, float OffsetX, float OffsetY) const
{
	const FVector2D CardSize = GetCardDesiredSize();
	FVector2D Result = PositionToIconRaw(TryPosition, CardSize, OffsetX, OffsetY);

	// Fill Icon follows its icon exactly. There's no opposite side to flip to,
	// and clamping would pull it off an icon that's partly off screen, leaving
	// it covering something else.
	if (TryPosition == EDirection2D::FillIcon)
	{
		return Result;
	}

	if (!bClampToViewport)
	{
		return Result;
	}

	const FVector2D ViewportSize = GetViewportSizeInViewportUnits();
	if (ViewportSize.IsNearlyZero())
	{
		return Result;
	}

	const float EffectivePadding = FMath::Max(0.f, ClampPadding);

	// Up/Down and Left/Right only ever flip along the axis their IconPosition
	// points down, exactly as asked: a card running out of room to the right
	// tries the left instead, one running out above tries below, and so on.
	// The perpendicular axis (left/right overflow for an Up/Down card, or
	// vice versa) is centered on the icon and has no "other side" to flip
	// to, so it's left untouched here and picked up by ClampToViewport below.
	// The four diagonals point along both axes at once, so each is tried
	// independently: an UpLeft card that only runs out on the left becomes
	// UpRight, one that only runs out on top becomes DownLeft, and one that
	// runs out of both becomes DownRight.
	//
	// bFlipToAvoidClipping gates whether any of this runs at all — off, and
	// this falls straight through to the plain inset clamp below, the same
	// as if every FitsWithPadding check here had failed.
	if (bFlipToAvoidClipping && DirectionHasHorizontalComponent(TryPosition) &&
		!FitsWithPadding(Result.X, CardSize.X, ViewportSize.X, EffectivePadding))
	{
		const FVector2D FlippedResult = PositionToIconRaw(FlipHorizontalDirection(TryPosition), CardSize, OffsetX, OffsetY);

		// Only take the flip if it actually solves the problem. A card wider
		// than the room available on both sides doesn't get better by
		// switching sides — clamping from the side it was actually
		// authored for (below) keeps it closer to its intended position
		// than an arbitrary flip would.
		if (FitsWithPadding(FlippedResult.X, CardSize.X, ViewportSize.X, EffectivePadding))
		{
			Result.X = FlippedResult.X;
		}
	}

	if (bFlipToAvoidClipping && DirectionHasVerticalComponent(TryPosition) &&
		!FitsWithPadding(Result.Y, CardSize.Y, ViewportSize.Y, EffectivePadding))
	{
		const FVector2D FlippedResult = PositionToIconRaw(FlipVerticalDirection(TryPosition), CardSize, OffsetX, OffsetY);

		if (FitsWithPadding(FlippedResult.Y, CardSize.Y, ViewportSize.Y, EffectivePadding))
		{
			Result.Y = FlippedResult.Y;
		}
	}

	// Whatever flipping didn't apply to (the non-flippable perpendicular
	// axis) or couldn't resolve (an axis where even the opposite side ran
	// out of room) falls back to a plain inset clamp here. Any axis flipping
	// already resolved already satisfies this exact padding/bounds test, so
	// this is a no-op for it.
	return ClampToViewport(Result, CardSize);
}

FVector2D UInfoCardWidget::PositionToCursorRaw(EInfoCardCursorPosition TryPosition, const FVector2D& CursorPos, const FVector2D& CardSize, float OffsetX, float OffsetY) const
{
	FVector2D Result = CursorPos;

	switch (TryPosition)
	{
	case EInfoCardCursorPosition::UpRight:
		Result.X = CursorPos.X + OffsetX;
		Result.Y = CursorPos.Y - CardSize.Y - OffsetY;
		break;

	case EInfoCardCursorPosition::UpLeft:
		Result.X = CursorPos.X - CardSize.X - OffsetX;
		Result.Y = CursorPos.Y - CardSize.Y - OffsetY;
		break;

	case EInfoCardCursorPosition::DownRight:
		Result.X = CursorPos.X + OffsetX;
		Result.Y = CursorPos.Y + OffsetY;
		break;

	case EInfoCardCursorPosition::DownLeft:
		Result.X = CursorPos.X - CardSize.X - OffsetX;
		Result.Y = CursorPos.Y + OffsetY;
		break;

	case EInfoCardCursorPosition::UpCenter:
		Result.X = CursorPos.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = CursorPos.Y - CardSize.Y - OffsetY;
		break;

	case EInfoCardCursorPosition::DownCenter:
		Result.X = CursorPos.X - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = CursorPos.Y + OffsetY;
		break;

	case EInfoCardCursorPosition::LeftCenter:
		Result.X = CursorPos.X - CardSize.X - OffsetX;
		Result.Y = CursorPos.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EInfoCardCursorPosition::RightCenter:
		Result.X = CursorPos.X + OffsetX;
		Result.Y = CursorPos.Y - (CardSize.Y * 0.5f) + OffsetY;
		break;
	}

	return Result;
}

FVector2D UInfoCardWidget::PositionToCursor(EInfoCardCursorPosition Position, float OffsetX, float OffsetY) const
{
	const FVector2D CardSize = GetCardDesiredSize();

	// Read once and reused for both the raw and (if needed) flipped attempt
	// below — querying it twice within the same UpdatePosition call could
	// theoretically read two different cursor positions between calls and
	// make the flip decision inconsistent with what it's flipping away from.
	FVector2D CursorPos = FVector2D::ZeroVector;
	if (UWorld* World = GetWorld())
	{
		CursorPos = UWidgetLayoutLibrary::GetMousePositionOnViewport(World);
	}

	FVector2D Result = PositionToCursorRaw(Position, CursorPos, CardSize, OffsetX, OffsetY);

	if (!bClampToViewport)
	{
		return Result;
	}

	const FVector2D ViewportSize = GetViewportSizeInViewportUnits();
	if (ViewportSize.IsNearlyZero())
	{
		return Result;
	}

	const float EffectivePadding = FMath::Max(0.f, ClampPadding);

	// Same flip-before-clamp idea PositionToIcon uses (see its own comment),
	// mapped onto cursor placements instead of icon placements: a card
	// running out of room to the right tries the left instead, one running
	// out above tries below, and each of the four corners flips its two axes
	// independently. UpCenter/DownCenter have no horizontal component to
	// flip (centered on the cursor's X); LeftCenter/RightCenter have no
	// vertical component (centered on the cursor's Y) — see
	// CursorPositionHasHorizontalComponent/CursorPositionHasVerticalComponent.
	if (bFlipToAvoidClipping && CursorPositionHasHorizontalComponent(Position) &&
		!FitsWithPadding(Result.X, CardSize.X, ViewportSize.X, EffectivePadding))
	{
		const FVector2D FlippedResult = PositionToCursorRaw(FlipHorizontalCursorPosition(Position), CursorPos, CardSize, OffsetX, OffsetY);

		// Only take the flip if it actually solves the problem — same
		// reasoning as PositionToIcon: a card wider than the room available
		// on both sides doesn't get better by switching sides.
		if (FitsWithPadding(FlippedResult.X, CardSize.X, ViewportSize.X, EffectivePadding))
		{
			Result.X = FlippedResult.X;
		}
	}

	if (bFlipToAvoidClipping && CursorPositionHasVerticalComponent(Position) &&
		!FitsWithPadding(Result.Y, CardSize.Y, ViewportSize.Y, EffectivePadding))
	{
		const FVector2D FlippedResult = PositionToCursorRaw(FlipVerticalCursorPosition(Position), CursorPos, CardSize, OffsetX, OffsetY);

		if (FitsWithPadding(FlippedResult.Y, CardSize.Y, ViewportSize.Y, EffectivePadding))
		{
			Result.Y = FlippedResult.Y;
		}
	}

	// Whatever flipping didn't apply to, or couldn't resolve, falls back to
	// a plain inset clamp here — same as PositionToIcon's tail.
	return ClampToViewport(Result, CardSize);
}

FVector2D UInfoCardWidget::PositionToViewport(EViewportPosition Position, float OffsetX, float OffsetY) const
{
	const FVector2D CardSize     = GetCardDesiredSize();
	const FVector2D ViewportSize = GetViewportSizeInViewportUnits();

	FVector2D Result = FVector2D::ZeroVector;

	switch (Position)
	{
	case EViewportPosition::TopLeft:
		Result.X = OffsetX;
		Result.Y = OffsetY;
		break;

	case EViewportPosition::TopCenter:
		Result.X = (ViewportSize.X * 0.5f) - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = OffsetY;
		break;

	case EViewportPosition::TopRight:
		Result.X = ViewportSize.X - CardSize.X - OffsetX;
		Result.Y = OffsetY;
		break;

	case EViewportPosition::LeftCenter:
		Result.X = OffsetX;
		Result.Y = (ViewportSize.Y * 0.5f) - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EViewportPosition::RightCenter:
		Result.X = ViewportSize.X - CardSize.X - OffsetX;
		Result.Y = (ViewportSize.Y * 0.5f) - (CardSize.Y * 0.5f) + OffsetY;
		break;

	case EViewportPosition::BottomLeft:
		Result.X = OffsetX;
		Result.Y = ViewportSize.Y - CardSize.Y - OffsetY;
		break;

	case EViewportPosition::BottomCenter:
		Result.X = (ViewportSize.X * 0.5f) - (CardSize.X * 0.5f) + OffsetX;
		Result.Y = ViewportSize.Y - CardSize.Y - OffsetY;
		break;

	case EViewportPosition::BottomRight:
		Result.X = ViewportSize.X - CardSize.X - OffsetX;
		Result.Y = ViewportSize.Y - CardSize.Y - OffsetY;
		break;
	}

	// Viewport-relative positions are already computed to fit within the
	// viewport bounds, but ClampToViewport is still applied for consistency
	// with PositionToIcon/PositionToCursor and to guard against a card
	// larger than the viewport itself.
	return ClampToViewport(Result, CardSize);
}