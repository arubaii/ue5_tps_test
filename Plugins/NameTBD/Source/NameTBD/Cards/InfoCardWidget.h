// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"
#include "GameplayTagContainer.h"
#include "InfoCardTypes.h"
#include "InfoCardWidget.generated.h"

class UWidget;
class UInfoCardAnchor;

/**
 * A tooltip / context card spawned by UInfoCardAnchor.
 *
 * Animation is not built into the card. In the card's Widget Blueprint, select the widget that should
 * move (usually the root) and Add Component > Widget Animation Component; its Anim In / Anim Out are the
 * card's. The anchor-specific part - FInfoCardSpec::AdditiveAnimations, after an anchor's Additive
 * Animation Overrides - is handed to that component as extra layers in SetupCard.
 *
 * A card class without one still works: the card makes a component of its own at runtime, bound to the
 * card itself, holding the Legacy Anim In / Anim Out below (if any) plus the additive layers.
 */
UCLASS()
class NAMETBD_API UInfoCardWidget : public UUserWidget, public IWidgetAnimHost
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	EInfoCardPosition PositionMode = EInfoCardPosition::ToIcon;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToIcon"))
	EDirection2D IconPosition = EDirection2D::Up;

	/**
	 * Set from FInfoCardSpec::FillIconPadding via SetupCard. Only used while
	 * PositionMode is ToIcon and IconPosition is FillIcon; see that property
	 * for what it controls. UpdatePosition is what applies it, sizing this
	 * card to the icon's rect inset by this margin.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToIcon && IconPosition == EDirection2D::FillIcon", EditConditionHides))
	FMargin FillIconPadding = FMargin(0.f);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToCursor"))
	EInfoCardCursorPosition CursorPosition = EInfoCardCursorPosition::UpRight;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToViewport"))
	EViewportPosition ViewportPosition = EViewportPosition::TopRight;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	FVector2D Offset = FVector2D(0.0f, 0.0f);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	bool UpdatesPositionOnTick = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	float HoverAnchorPadding = 8.f;

	/**
	 * Set from FInfoCardSpec::bKeepOpenUntilCursorLeaves via SetupCard. While
	 * true, this card won't close just because the anchor lost mouse-over —
	 * see TickKeepOpenWhileCursorNearby, which is what actually decides when
	 * to close it in that case.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	bool bKeepOpenUntilCursorLeaves = false;

	/**
	 * Set from FInfoCardSpec::bReappearOnClickDismiss via SetupCard. See
	 * that property for the full explanation; ApplyPendingDismiss is what
	 * actually acts on it.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	bool bReappearOnClickDismiss = false;

	/**
	 * Set from FInfoCardSpec::KeepOpenSlack via SetupCard. Extra padding, in
	 * viewport units, added around BOTH the icon rect and this card's rect
	 * when testing whether the cursor has left (see IsCursorOverIconOrCard).
	 * Separate from HoverAnchorPadding on purpose: that one is a placement
	 * value a designer picks for how the card should look, and it's routinely
	 * 0, which would leave the keep-open test with no tolerance at all.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (ClampMin = "0.0"))
	float KeepOpenSlack = 16.f;

	/**
	 * Set from FInfoCardSpec::KeepOpenGracePeriod via SetupCard. Seconds the
	 * cursor has to stay outside the keep-open region before this card closes;
	 * any frame back inside resets the accumulator. Without this, one frame of
	 * stale geometry (or a fast flick past a corner) closes the card outright,
	 * which is what "flickers, only works sometimes" looks like in practice.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (ClampMin = "0.0"))
	float KeepOpenGracePeriod = 0.2f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	bool bClampToViewport = true;

	/**
	 * Set from FInfoCardSpec::ClampPadding via SetupCard. See that property
	 * for what it controls; same value, just carried onto the live card.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (ClampMin = "0.0"))
	float ClampPadding = 8.f;

	/**
	 * Set from FInfoCardSpec::bAvoidOverlappingCards via SetupCard. See that
	 * property for what it controls.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	bool bAvoidOverlappingCards = true;

	/**
	 * Set from FInfoCardSpec::bIgnoresMenuStateChange via SetupCard. If
	 * true, this card ignores UMenuManagerComponent::OnMenuStateChanged
	 * entirely — it never binds to it in the first place (see
	 * NativeConstruct) — and persists through menus opening and closing
	 * instead of closing itself the moment any menu state changes.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard", meta = (ToolTip = "If true, the card will ignore the event and will persist throughout menus opening and closing. Useful for manually handling the card's lifetime."))
	bool bIgnoresMenuStateChange = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	bool bCloseOnClickOutside = false;

	/**
	 * Set from FInfoCardSpec::RemoveCardOnPressing via SetupCard. See
	 * ECardRemoveOnPressZone for what each value means; ShouldPressCloseCard
	 * is what actually acts on it.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	ECardRemoveOnPressZone RemoveCardOnPressing = ECardRemoveOnPressZone::OutsideAnchorAndCard;

	/**
	 * Resolved once, in SetupCard, from FInfoCardSpec::DismissClickGetsConsumed
	 * — see that property, and UInfoCardAnchor::WasDismissClickConsumedThisFrame,
	 * for what this actually does (and its real limits). ShouldPressCloseCard's
	 * caller, EvaluateGlobalPress, is what acts on it.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "InfoCard")
	bool bDismissClickGetsConsumed = false;

	/**
	 * Resolved once, in SetupCard, from FInfoCardSpec::AutoDismissDuration.
	 * 0 (the default) means never — see that property. Actually acted on by
	 * AutoDismissTimerHandle/HandleAutoDismissElapsed, also set up in
	 * SetupCard.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "InfoCard")
	float AutoDismissDuration = 0.f;

	/**
	 * Set from FInfoCardSpec::CardTransform via SetupCard, and applied to
	 * this card via SetRenderTranslation/SetRenderScale/SetRenderShear/
	 * SetRenderTransformAngle at the same point. Kept as a readable record
	 * of what was applied — UUserWidget has no RenderTransform getter of its
	 * own to read it back from.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "InfoCard")
	FWidgetTransform CardTransform;

	/**
	 * Set from FInfoCardSpec::AdditiveAnimations via SetupCard (after any
	 * anchor settings override has been applied to that spec). Handed to the
	 * card's Widget Animation Component as extra layers, on top of its own
	 * Anim In / Anim Out, by ApplyAdditiveAnimations — see that function.
	 * Kept as a readable record of what the spec asked for.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "InfoCard")
	FInfoCardAdditiveAnimations AdditiveAnimations;

	/**
	 * Resolved once, in SetupCard, from FInfoCardSpec::FlipToAvoidClipping.
	 * Only consulted by PositionToIcon/PositionToCursor while
	 * bClampToViewport is also on — see bClampToViewport's own comment for
	 * the flip-then-clamp mechanics this gates.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "InfoCard")
	bool bFlipToAvoidClipping = true;

	/** Now visible in the BP Details Panel so you can see/edit the trigger type per card */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InfoCard")
	ECardType SourceCardType = ECardType::AlwaysOn;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "InfoCard")
	TArray<TObjectPtr<UNameTBD_Payload>> Payloads;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "InfoCard")
	FName CardId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "InfoCard")
	TWeakObjectPtr<UWidget> AnchorWidget;

	UFUNCTION(BlueprintCallable, Category = "InfoCard")
	void SetupCard(const FInfoCardSpec& Spec, UWidget* InAnchorWidget);

	UFUNCTION(BlueprintCallable, Category = "InfoCard")
	void SetPayloads(const TArray<UNameTBD_Payload*>& NewPayloads);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InfoCard", meta = (ToolTips = "Loop through the payload array and cast to the payload class(es) on each loop to implement."))
	void ApplyPayloads(const TArray<UNameTBD_Payload*>& InPayloads);
	virtual void ApplyPayloads_Implementation(const TArray<UNameTBD_Payload*>& InPayloads);

	UFUNCTION(BlueprintPure, Category = "InfoCard")
	UNameTBD_Payload* GetPrimaryPayload() const;

	UFUNCTION(BlueprintPure, Category = "InfoCard")
	UNameTBD_Payload* GetPayloadAtIndex(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category = "InfoCard")
	void CloseCard();

	/**
	 * Sound sets for this card, copied from FInfoCardSpec::Sounds by SetupCard (so an anchor's Settings
	 * Overrides > Sound has already been folded in by the time they get here). Shown and Hidden are played
	 * automatically; Triggered is played by the anchor, before this widget exists.
	 */
	UPROPERTY(BlueprintReadWrite, Transient, Category = "InfoCard|Sound")
	TArray<FInfoCardSoundSet> Sounds;

	/** Plays every set's slot for an event. Exposed so a card driving its own lifetime can fire one by hand. */
	UFUNCTION(BlueprintCallable, Category = "InfoCard|Sound")
	void PlayCardSound(EInfoCardSoundEvent Event);

	UFUNCTION(BlueprintPure, Category = "InfoCard")
	bool IsClosing() const;

	/**
	 * Pins or unpins this card. A pinned card ignores every AUTOMATIC reason
	 * to close (the cursor leaving, focus lost, RemoveCardOnPressing,
	 * AutoDismissDuration, menu state changes, other anchors'
	 * CloseOtherCardsScope) and stays up until something explicitly closes it.
	 * See UInfoCardAnchor's Pinning section for the full list both ways.
	 * UInfoCardAnchor::PinInfoCard / UnpinInfoCard are the usual way in and
	 * just call this.
	 *
	 * Also callable on the card directly, e.g. from a pin button inside the
	 * card's own WBP. Unpinning from either place restarts AutoDismissDuration
	 * (if any) from the full duration and lets the anchor close a Highlight
	 * card whose trigger went away while it was pinned (see
	 * UInfoCardAnchor::NotifyCardUnpinned). Pinning a card that is already
	 * closing does nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "InfoCard|Pin")
	void SetPinned(bool bNewPinned);

	/** True while this card is pinned. See SetPinned. */
	UFUNCTION(BlueprintPure, Category = "InfoCard|Pin")
	bool IsPinned() const { return bIsPinned; }

	/**
	 * Fired whenever SetPinned actually changes the pin state. Hook up a pin
	 * icon, an outline, or anything else that should show the player this
	 * card is staying put.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "InfoCard|Pin")
	void OnPinStateChanged(bool bPinned);

	/**
	 * True once NativeConstruct has run and false again after NativeDestruct
	 * — i.e. "this card is a real, laid-out widget", not one still on its way
	 * up or already torn down.
	 *
	 * Two things depend on this. CloseCard defers its own teardown until
	 * construction finishes (removing a widget from the viewport partway
	 * through Slate building it is a hard crash, not a graceful close), and
	 * UInfoCardAnchor::IsSlotActive uses it so a card that has been created
	 * and registered but not yet added to the viewport isn't mistaken for a
	 * stale slot and cleared out from under itself.
	 */
	UFUNCTION(BlueprintPure, Category = "InfoCard")
	bool HasBeenConstructed() const { return bHasBeenConstructed; }

	UFUNCTION(BlueprintCallable, Category = "InfoCard|Position")
	void UpdatePosition();

	/**
	 * True while this card is placed directly in a layout (dragged into a Widget Blueprint's hierarchy, or
	 * added to a panel by hand) rather than spawned into the viewport by a UInfoCardAnchor. Resolved in
	 * NativeConstruct.
	 *
	 * An embedded card is owned by its layout: it is never positioned or sized through the viewport, never
	 * registered in ActiveCards (so menu-state changes, click-away dismissal, keep-open and overlap
	 * avoidance all leave it alone), and never hit-test blocked while animating in. It only goes away when
	 * something explicitly calls CloseCard / RemoveFromParent on it.
	 */
	UFUNCTION(BlueprintPure, Category = "InfoCard")
	bool IsEmbeddedInLayout() const { return bEmbeddedInLayout; }

	/**
	 * Embedded cards only (see IsEmbeddedInLayout). UUserWidget defaults to Self Hit Test Invisible, so the
	 * card's own background never receives the cursor. When on, an embedded card left at that default is
	 * promoted to Visible on construct. Any other visibility you chose is left alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InfoCard|Embedded")
	bool bHitTestableWhenEmbedded = true;

	/**
	 * True if the cursor is currently inside this card's "keep open" region:
	 * the anchor's icon, this card itself, or the corridor between the two —
	 * all three expanded by HoverAnchorPadding + KeepOpenSlack. The corridor
	 * matters: the card is deliberately drawn with a gap between it and the
	 * icon, and without treating that gap as inside, dragging the cursor from
	 * one to the other passes through dead space and closes the card halfway
	 * there.
	 *
	 * Fails safe. If the anchor's geometry or this card's own rect isn't
	 * resolved yet (the first frame or two after spawning, before Slate has
	 * laid the card out), this returns true rather than guessing from a
	 * zero-size rect — "I don't know yet" must never read as "the cursor
	 * left".
	 *
	 * The region CASCADES through nested cards: it also includes the same
	 * region (icon, card, corridor) of every live card nested inside this
	 * one, at any depth. That covers a dropdown anchored to a row of this
	 * card, a dropdown anchored to a row of THAT dropdown, and so on (see
	 * IsPointOverDescendantCard). Without it, moving onto a second-level
	 * dropdown takes the cursor outside the first-level dropdown's own rect,
	 * the first-level card closes, and the second-level card (whose anchor
	 * lives inside it) is torn down along with it.
	 *
	 * Backs bKeepOpenUntilCursorLeaves (see TickKeepOpenWhileCursorNearby),
	 * but exposed generally in case a Blueprint subclass wants the same
	 * "is the player still engaging with this" check for something else.
	 */
	UFUNCTION(BlueprintPure, Category = "InfoCard|Position")
	bool IsCursorOverIconOrCard() const;

	/**
	 * IsCursorOverIconOrCard against an arbitrary viewport-space point rather
	 * than wherever the cursor happens to be right now. All the containment
	 * logic actually lives here; the cursor version just samples the cursor
	 * and calls this.
	 *
	 * The split exists because dismissal has to test the point the press
	 * ACTUALLY LANDED ON, captured at press time, not the cursor position at
	 * the moment the card gets round to reacting. Those are different: a
	 * press is observed during Slate's input pass and acted on during the
	 * card's tick, and a fast-moving cursor covers real distance in between.
	 * Testing the later position is how a press that clearly landed on the
	 * card ends up dismissing it anyway.
	 */
	UFUNCTION(BlueprintPure, Category = "InfoCard|Position")
	bool IsPointOverIconOrCard(const FVector2D& ViewportPoint) const;

	/**
	 * Global entry point for menu state changes. Call this from your custom Menu Manager 
	 * (or the included UMenuManagerComponent) to automatically close unpinned info cards.
	 */
	UFUNCTION(BlueprintCallable, Category = "InfoCard|Menus")
	static void DispatchGlobalMenuStateChanged(FGameplayTag ScreenTag, bool bIsOpen);

	/**
	 * Called by the Slate input pre-processor for every mouse and gamepad
	 * press anywhere in the application, before Slate routes it anywhere.
	 * Hands the press to every live card, each of which decides there and
	 * then whether the press dismisses it (see EvaluateGlobalPress).
	 *
	 * Nothing is closed from here. Deciding is safe during input dispatch;
	 * tearing a widget out of a hierarchy Slate is mid-route through is not,
	 * so the close itself is deferred by one step to FlushPendingDismissals.
	 *
	 * Public and static because the pre-processor is a plain Slate object,
	 * not a widget, and has no instance to route this through.
	 */
	static void RecordGlobalPress(const FKey& Key, bool bHasPosition, const FVector2D& AbsolutePosition);

	/**
	 * Called by the press watcher on mouse-up. Releases every card whose
	 * dismissal was held pending the release — see
	 * bPendingDismissAwaitsRelease. The actual close still happens from
	 * FlushPendingDismissals, which runs after Slate has routed the release,
	 * so a button inside the card gets its click first.
	 */
	static void NotifyGlobalRelease();

	/** Runs ApplyPendingDismiss on every live card. Called once per frame from the press watcher's tick, outside input dispatch. */
	static void FlushPendingDismissals();

	/** Runs RefreshDismissGeometry on every live card, so rects stay current even for cards that are not ticking. */
	static void RefreshAllActiveCardGeometry();

	/**
	 * Whether Key is one this system treats as a dismissing press: any mouse
	 * button, any gamepad button. Keyboard keys are deliberately excluded —
	 * a card that vanished because the player typed into a search box (or
	 * held W) would be worse than one that lingers. Mouse WHEEL is excluded
	 * for the same reason: scrolling a list the card sits next to isn't
	 * dismissal intent.
	 */
	static bool IsDismissingKey(const FKey& Key);

	/**
	 * True if PotentialAncestor is the card this card is (directly or
	 * transitively) nested inside of. Walked card to card via GetParentCard,
	 * climbing through each ancestor's own parent in turn, so a
	 * submenu-of-a-submenu still recognizes its grandparent.
	 *
	 * Public rather than protected: originally written for
	 * AvoidOverlappingCards alone (a submenu is SUPPOSED to sit flush against
	 * its own parent menu, which would otherwise read as two cards
	 * overlapping), but the same ancestor test is exactly what
	 * UInfoCardAnchor::ApplyCloseOthersScope needs — a CloseOtherCardsScope
	 * of SameSourceWidgetClass or AllCards must never close a card's own
	 * ancestor chain, or opening a new submenu collapses the very menu it's
	 * nested inside of.
	 *
	 * It's also the backbone of the keep-open cascade, asked in reverse:
	 * IsPointOverDescendantCard finds this card's descendants by asking every
	 * other live card whether this card is one of its ancestors.
	 */
	bool IsAncestorCard(const UInfoCardWidget* PotentialAncestor) const;

	/**
	 * The card this card's anchor lives inside of, or null for a card whose
	 * anchor isn't inside any card (an inventory slot's tooltip, a HUD card).
	 * Resolved through FindContainingCard and cached once found, since which
	 * card an anchor sits inside doesn't change over a card's lifetime. A miss
	 * isn't cached permanently, only for the rest of the frame, so a lookup
	 * made before the anchor's Slate hierarchy settled isn't stuck reporting
	 * "no parent" for good.
	 */
	UFUNCTION(BlueprintPure, Category = "InfoCard")
	UInfoCardWidget* GetParentCard() const;

	/**
	 * The info card Widget is displayed inside of, or null. Tries Widget's
	 * Outer chain first (cheap, and correct for anything placed in a WBP's
	 * widget tree, through any number of nested UserWidgets), then falls back
	 * to walking the live Slate hierarchy upward. The fallback is needed for
	 * widgets created at runtime with CreateWidget and added into a card (a
	 * dynamically built menu's rows), whose Outer is the owning player rather
	 * than the card they're shown in. Same two-step search as
	 * UContextMenuButton::ResolveOwningCard.
	 */
	static UInfoCardWidget* FindContainingCard(const UWidget* Widget);

	/**
	 * The Widget Animation Component that plays this card's entrance and exit: one added in the card's
	 * Widget Blueprint (on the card itself, its root, or any widget in its tree, in that order), or else the
	 * one the card made for itself. Resolved in SetupCard / NativeConstruct; null before either has run.
	 */
	UFUNCTION(BlueprintPure, Category = "InfoCard|Animation")
	UWidgetAnimationComponent* GetCardAnimation() const { return CardAnimation; }

	/**
	 * The Anim In / Anim Out a card of CardClass plays, before additive animations - for previews that have
	 * no real card. A designer-added component only exists on an instance, so this briefly creates one
	 * (owned by OwningWidget, never constructed or shown). Falls back to the class's Legacy Anim In / Out.
	 */
	static bool GetClassAnimationEntries(TSubclassOf<UInfoCardWidget> CardClass, UWidget* OwningWidget, TArray<FWidgetAnimEntry>& OutAnimIn, TArray<FWidgetAnimEntry>& OutAnimOut);

	// IWidgetAnimHost - forwarded, so Play Widget Animation keeps working when aimed at a card.
	virtual void PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying) override;
	virtual void StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart) override;
	virtual bool AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const override;

	/**
	 * Anim In / Anim Out saved on this class while it derived from UUserWidget_Animatable (brought here by
	 * CoreRedirects). Copy them into a Widget Animation Component on the card (right-click > Copy, then
	 * Paste on the component's Anim In), then clear them here. Only played while the card has no
	 * component of its own.
	 */
	UPROPERTY(EditAnywhere, Category = "Animation|Legacy", meta = (DisplayName = "Legacy Anim In", TitleProperty = "Animation"))
	TArray<FWidgetAnimEntry> LegacyAnimIn;

	UPROPERTY(EditAnywhere, Category = "Animation|Legacy", meta = (DisplayName = "Legacy Anim Out", TitleProperty = "Animation"))
	TArray<FWidgetAnimEntry> LegacyAnimOut;

	UPROPERTY(EditAnywhere, Category = "Animation|Legacy", meta = (DisplayName = "Legacy Preserve Authored Transforms"))
	bool bLegacyPreserveAuthoredTransforms = false;

protected:

	/**
	 * Cached viewport-space rect of the anchor's icon, refreshed by
	 * RefreshAnchorGeometry. Pure derived state — nothing here is ever
	 * hand-authored, so there's no reason for it to be Blueprint-writable
	 * (or visible outside a subclass's own graph at all).
	 */
	UPROPERTY(Transient)
	FVector2D IconPositon = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	FVector2D IconFrameSize = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	bool IsFirstFrame = true;

	UPROPERTY(Transient)
	float ElapsedTime = 0.f;

	/**
	 * This card's own rect in the same viewport space as IconPositon /
	 * IconFrameSize, refreshed by CacheCardViewportRect. Kept as cached state
	 * rather than read straight off GetCachedGeometry() at the point of use
	 * because that geometry is a frame stale while the card is still being
	 * positioned and is outright zero-sized before its first paint — reading
	 * it directly is why the keep-open test used to fail on the frames right
	 * after the card appeared.
	 */
	UPROPERTY(Transient)
	FVector2D CardRectMin = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	FVector2D CardRectMax = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	bool bHasCardRect = false;

	/**
	 * The viewport-space position UpdatePosition actually applied the last
	 * time it ran, and whether it has ever run.
	 *
	 * CacheCardViewportRect needs "where this card was last put", and it used
	 * to get that by calling GetTargetPosition() again at cache time. That is
	 * not the same question: GetTargetPosition() RE-EVALUATES placement from
	 * whatever the inputs say right now, and for PositionMode == ToCursor
	 * those inputs include the live cursor. On a card with
	 * UpdatesPositionOnTick off — one deliberately placed once and then left
	 * alone — the card rect was therefore being unioned every frame with a
	 * phantom rect sitting under the cursor, wherever the cursor happened to
	 * be. The card's effective rect followed the mouse around the screen, so
	 * every press read as "on the card" and the card could not be dismissed
	 * by clicking away from it at all.
	 *
	 * Recording what was applied, rather than recomputing what would be
	 * applied, is the whole fix.
	 */
	UPROPERTY(Transient)
	FVector2D LastAppliedPosition = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	bool bHasAppliedPosition = false;

	/**
	 * A press has been judged (by EvaluateGlobalPress, at the moment the
	 * press happened) to be one that removes this card, and the card is
	 * waiting for the next safe point to act on it. See ApplyPendingDismiss.
	 *
	 * The decision and the action are deliberately separated. The decision
	 * has to happen while Slate is dispatching the press, because that is the
	 * only moment the press point and the on-screen layout the player was
	 * actually looking at are both true. The action must NOT happen there,
	 * because closing a card means tearing a widget out of the hierarchy that
	 * Slate is in the middle of routing an event through.
	 */
	UPROPERTY(Transient)
	bool bPendingDismissFromPress = false;

	/**
	 * Whether the press that set bPendingDismissFromPress landed on this
	 * card's own anchor icon. Captured at press time (the only time it can be
	 * answered honestly) and read by ApplyPendingDismiss for
	 * bReappearOnClickDismiss.
	 */
	UPROPERTY(Transient)
	bool bPendingDismissPressOnAnchor = false;

	/**
	 * Set when the dismissing press landed inside this card's own rect, which
	 * only happens for the zones that dismiss on a press over the card
	 * (Anywhere, and InsideAnchorOrCard). Holds the close until the mouse
	 * button is released.
	 *
	 * Without it, a card set to dismiss on a press over itself was destroyed
	 * on mouse-DOWN, so anything clickable inside it — a UContextMenuButton,
	 * most obviously — never lived to see the matching mouse-up and its
	 * OnClicked never fired. The button appeared to do nothing except make
	 * the menu disappear. Waiting for the release lets the click complete
	 * first; the card then closes immediately afterwards (or is already
	 * closing, because the button closed it itself, in which case this is a no-op).
	 */
	UPROPERTY(Transient)
	bool bPendingDismissAwaitsRelease = false;

	/**
	 * FPlatformTime::Seconds() when bPendingDismissFromPress was set. Backs
	 * the timeout in ApplyPendingDismiss so a dismissal that is waiting on a
	 * release which never arrives (the window losing focus mid-click, a
	 * capture-stealing widget swallowing the mouse-up) still resolves instead
	 * of leaving the card wedged open forever.
	 */
	double PendingDismissRealTime = 0.0;

	/** How many ticks this card has had since being constructed. The keep-open test is held off until the card has been laid out at least once. */
	UPROPERTY(Transient)
	int32 FramesSinceConstruct = 0;

	/** Seconds the cursor has been continuously outside the keep-open region. Reset to 0 the moment it comes back inside. */
	UPROPERTY(Transient)
	float KeepOpenOutsideTime = 0.f;

	/**
	 * Set by CloseCard, never cleared. IsAnimClosing() alone isn't a reliable
	 * "already closing" signal — a card with no out animation reports false
	 * forever — so without this a per-tick closer (click-outside, keep-open)
	 * calls CloseCard again every frame, and each call re-broadcasts
	 * OnCardHidden through NotifyCardSelfClosed.
	 */
	UPROPERTY(Transient)
	bool bCloseRequested = false;

	/**
	 * Set in NativeConstruct, cleared in NativeDestruct. See
	 * HasBeenConstructed for what reads it and why.
	 */
	UPROPERTY(Transient)
	bool bHasBeenConstructed = false;

	/**
	 * Set when CloseCard runs before NativeConstruct has — the card was asked
	 * to close while Slate was still building it. The actual teardown
	 * (PlayOut, which for a card with no out animation removes the widget
	 * outright) is held until NativeConstruct finishes and then run from
	 * there. Removing a widget from its parent partway through the
	 * construction pass that is adding it crashes, and that is exactly the
	 * window a nested Always On card opens: it spawns while its host card is
	 * still being built, and anything it closes on the way up (a
	 * CloseOtherCardsScope, a menu-state change) can land on a card that is
	 * mid-construction.
	 */
	UPROPERTY(Transient)
	bool bClosePendingConstruct = false;

	/** See SetPinned. Reset in SetupCard, so a card always starts unpinned. */
	UPROPERTY(Transient)
	bool bIsPinned = false;

	/**
	 * The size UpdatePosition last pushed into this card's viewport slot for
	 * Fill Icon, and whether it has pushed one at all. Tracked so the size is
	 * only re-applied when the icon's size actually changes (re-sizing the
	 * slot invalidates layout), so GetCardDesiredSize can report the size the
	 * card is really drawn at rather than its content's desired size, and so
	 * switching out of Fill Icon can hand sizing back to the content.
	 */
	UPROPERTY(Transient)
	FVector2D LastAppliedFillSize = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	bool bHasAppliedFillSize = false;

	/** GetParentCard's cache. Weak and not a UPROPERTY: weak pointers need no GC tracking. Reset in SetupCard. */
	mutable TWeakObjectPtr<UInfoCardWidget> CachedParentCard;

	/** GFrameCounter of GetParentCard's most recent miss, so a card with no parent card re-walks the Slate tree at most once per frame. */
	mutable uint64 ParentCardMissFrame = TNumericLimits<uint64>::Max();

	/**
	 * Stamped once in SetupCard from the static NextSpawnSequence counter.
	 * Gives every card a total, stable "who showed up first" ordering —
	 * AvoidOverlappingCards uses it so a card only ever yields to one that
	 * was shown before it, never the reverse, which is what stops two
	 * overlapping cards from both nudging away from each other and
	 * oscillating frame to frame.
	 */
	UPROPERTY(Transient)
	int64 SpawnSequence = 0;

	/**
	 * Internal position math, driven automatically by UpdatePosition every
	 * tick — a card built the normal way (via UInfoCardAnchor) never needs
	 * these called directly. Still available to a Blueprint subclass's own
	 * graph if it wants to build custom placement on top of these, just not
	 * to unrelated external code holding a reference to a spawned card.
	 */
	UFUNCTION(BlueprintCallable, Category = "InfoCard|Position")
	void RefreshAnchorGeometry();

	UFUNCTION(BlueprintPure, Category = "InfoCard|Position")
	FVector2D GetTargetPosition() const;

	UFUNCTION(BlueprintPure, Category = "InfoCard|Position")
	FVector2D PositionToIcon(EDirection2D TryPosition, float OffsetX, float OffsetY) const;

	/**
	 * Same formula as PositionToIcon for a single direction, but with no flip
	 * or clamp applied — just where TryPosition's own math puts the card
	 * relative to the icon. PositionToIcon calls this for the requested
	 * direction and, when a flip is being considered, calls it again for the
	 * opposite direction to see whether flipping actually helps.
	 */
	FVector2D PositionToIconRaw(EDirection2D TryPosition, const FVector2D& CardSize, float OffsetX, float OffsetY) const;

	/** True while PositionMode is ToIcon and IconPosition is FillIcon. */
	bool IsFillingIcon() const;

	/** The icon's size minus FillIconPadding on each side, never negative. Zero while the icon's geometry hasn't resolved. */
	FVector2D GetFillIconSize() const;

	UFUNCTION(BlueprintPure, Category = "InfoCard|Position")
	FVector2D PositionToCursor(EInfoCardCursorPosition Position, float OffsetX, float OffsetY) const;

	/**
	 * Same formula as PositionToCursor for a single position, but with no
	 * flip or clamp applied — just where TryPosition's own math puts the
	 * card relative to CursorPos. PositionToCursor calls this for the
	 * requested position and, when a flip is being considered, calls it
	 * again for the opposite position to see whether flipping actually
	 * helps. CursorPos is passed in (rather than re-read here) so a raw
	 * attempt and its flipped counterpart, within the same PositionToCursor
	 * call, are always measured against the exact same cursor sample.
	 */
	FVector2D PositionToCursorRaw(EInfoCardCursorPosition TryPosition, const FVector2D& CursorPos, const FVector2D& CardSize, float OffsetX, float OffsetY) const;

	UFUNCTION(BlueprintPure, Category = "InfoCard|Position")
	FVector2D PositionToViewport(EViewportPosition Position, float OffsetX, float OffsetY) const;

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Removes the card once its exit animation has finished. Bound to CardAnimation's native event. */
	void HandleCardAnimationOutFinished(UWidgetAnimationComponent* Component);

	/**
	 * Finds the card's component (see GetCardAnimation), creating one bound to the card if the Widget
	 * Blueprint has none, and binds HandleCardAnimationOutFinished to it. Idempotent.
	 */
	UWidgetAnimationComponent* ResolveCardAnimation();

	/** A designer-added component on this card or anywhere in its tree, or null. */
	UWidgetAnimationComponent* FindAuthoredAnimationComponent() const;

	/** The component that animates this card. See GetCardAnimation. */
	UPROPERTY(Transient)
	TObjectPtr<UWidgetAnimationComponent> CardAnimation;

	/** True when CardAnimation was created by the card rather than added in the designer. */
	bool bOwnsCardAnimation = false;

	FDelegateHandle CardAnimationOutFinishedHandle;

	/**
	 * The size this card is drawn at. Normally its content's desired size;
	 * for a Fill Icon card, the size UpdatePosition last applied to fill the
	 * icon (content desired size is meaningless for a card that's been told
	 * how big to be). Every rect and placement calculation reads this rather
	 * than GetDesiredSize directly, so they all agree.
	 */
	FVector2D GetCardDesiredSize() const;
	FVector2D ClampToViewport(const FVector2D& InPosition, const FVector2D& CardSize) const;
	FVector2D GetViewportSizeInViewportUnits() const;

	UFUNCTION()
	void HandleMenuStateChanged(FGameplayTag ScreenTag, bool bIsOpen);

	/**
	 * Started in SetupCard if AutoDismissDuration resolves > 0. Fires
	 * HandleAutoDismissElapsed, which just calls CloseCard() — a fixed
	 * lifetime from spawn, not reset by continued hovering/interaction, the
	 * same way a classic OS tooltip auto-hides after a few seconds even
	 * while the cursor is still sitting on it. Independent of
	 * bKeepOpenUntilCursorLeaves/KeepOpenGracePeriod, which only react to
	 * the cursor leaving — whichever fires first closes the card. Cleared
	 * in NativeDestruct.
	 *
	 * A core-ticker handle rather than an FTimerHandle: the world timer
	 * manager isn't ticked while the game is paused, so an auto-dismissing
	 * card shown in (or left up going into) a pause menu never closed. Not a
	 * UPROPERTY — FTSTicker handles aren't reflected types, and nothing needs
	 * this serialized or visible to Blueprint. Only ever touched through
	 * StartAutoDismissTimer / ClearAutoDismissTimer.
	 */
	FTSTicker::FDelegateHandle AutoDismissTimerHandle;

	/** (Re)starts AutoDismissTimerHandle from the full AutoDismissDuration, clearing any pending one first. Clears and does nothing else if AutoDismissDuration is 0. Runs while the game is paused. */
	void StartAutoDismissTimer();

	/** Cancels AutoDismissTimerHandle, if pending. Safe to call when nothing is. */
	void ClearAutoDismissTimer();

	UFUNCTION()
	void HandleAutoDismissElapsed();

	/**
	 * Resolves AdditiveAnimations and hands the result to the card's Widget
	 * Animation Component as its extra layers, on top of its own Anim In /
	 * Anim Out.
	 *
	 * Called from SetupCard, which runs after CreateWidget (so a designer-added
	 * component already exists) and before AddToViewport (so the layers are in
	 * place for the very first PlayIn). The extra layers are replaced, not
	 * appended, so calling SetupCard twice never stacks them.
	 */
	void ApplyAdditiveAnimations();

	/**
	 * Keeps this card out of hit testing while it's animating in (Highlight
	 * cards only) or closing (every card), and restores its visibility
	 * afterward.
	 *
	 * Animations move and scale the card through its render transform, and
	 * Slate hit-tests against the transformed card. A Highlight card whose
	 * animation passes over its own anchor (a Scale overshoot, a Slide that
	 * starts on top of the icon) takes the cursor away from the anchor, the
	 * anchor reads that as the mouse leaving and closes the card, the closing
	 * card goes away, the cursor is over the anchor again, and a new card
	 * spawns and starts the same animation: the continuous replay loop. With
	 * the card hit-test invisible for those moments it can never steal the
	 * cursor from its anchor. A closing card is going away anyway and should
	 * never block anything underneath it.
	 */
	void UpdateAnimationHitTestBlock();

	/** Whether this card counts as placed in a layout rather than spawned into the viewport. Evaluates live; NativeConstruct caches it. */
	bool ComputeEmbeddedInLayout() const;

	/** Cached result of ComputeEmbeddedInLayout, set in NativeConstruct and cleared in NativeDestruct. See IsEmbeddedInLayout. */
	bool bEmbeddedInLayout = false;

	/** Whether UpdateAnimationHitTestBlock currently has this card hit-test invisible. */
	bool bHitTestBlockedForAnimation = false;

	/** The visibility to restore once the block lifts. */
	ESlateVisibility VisibilityBeforeHitTestBlock = ESlateVisibility::Visible;

	/**
	 * The second half of CloseCard: plays the out animation (which, for a card
	 * with no out animation, removes it immediately). Split out so CloseCard
	 * can record the close, tell the anchor, and unbind straight away while
	 * deferring only this part to NativeConstruct when the card isn't built
	 * yet — see bClosePendingConstruct.
	 */
	void FinishClose();

	/**
	 * Decides, at the instant a press is dispatched, whether that press
	 * removes this card — and records the answer for ApplyPendingDismiss to
	 * act on. Called for every live card by RecordGlobalPress.
	 *
	 * Deciding here rather than on this card's next tick is what makes the
	 * whole feature reliable, for four separate reasons:
	 *
	 *  1. The geometry tested is the geometry the player was looking at when
	 *     they pressed. A tick-time test compares the press point against a
	 *     layout that has since moved (a following card, a scrolling list, a
	 *     card that flipped to avoid clipping).
	 *  2. Every press reaches every card. The tick-time version read a single
	 *     "most recent press" slot, so two presses inside one frame collapsed
	 *     into one, and a card that did not tick that frame missed presses
	 *     outright.
	 *  3. It does not depend on the card ticking at all. A UUserWidget's
	 *     TickFrequency can be set to Never or Auto in the WBP, and a
	 *     collapsed or hidden card does not tick either — in all of which
	 *     cases tick-driven dismissal simply never ran, which is most of
	 *     "it works on some cards and not others".
	 *  4. bDismissClickGetsConsumed becomes meaningful. The consumed-frame
	 *     stamp is now set before Slate routes the press to anything, so
	 *     gameplay code checking WasDismissClickConsumedThisFrame() in its own
	 *     handler for that same click actually sees it. Stamped a tick later,
	 *     as it used to be, it was always too late to be read by anyone.
	 */
	void EvaluateGlobalPress(const FVector2D& PressPointViewport, bool bHasPressPoint);

	/**
	 * The other half of EvaluateGlobalPress: if a dismissal was decided, and
	 * it is not still waiting on a mouse release, closes the card (and
	 * re-triggers it for bReappearOnClickDismiss). Safe to call from anywhere
	 * outside input dispatch — it runs from the press watcher's own tick, so
	 * the card closes on the same frame as the click, and again from
	 * NativeTick as a fallback for when no watcher is registered.
	 */
	void ApplyPendingDismiss();

	/** Clears any decided-but-unapplied dismissal. Called from SetupCard so a recycled card never inherits one. */
	void ClearPendingDismiss();

	/**
	 * Re-reads both rects this card's dismissal and keep-open tests depend on
	 * — the anchor icon's and its own. Runs every frame for every live card,
	 * from the press watcher's tick as well as from NativeTick, because a
	 * press can arrive on a frame this particular card did not tick and a
	 * stale rect is the difference between "the player clicked the card" and
	 * "the player clicked next to where the card used to be".
	 */
	void RefreshDismissGeometry();

	/**
	 * Does a press at PressPoint qualify to remove this card, per
	 * RemoveCardOnPressing?
	 *
	 * PressPoint is in viewport space and is where the press LANDED, sampled
	 * when it happened. bHasPressPoint is false for a press that has no
	 * meaningful screen position at all — a gamepad button. That case is
	 * resolved as "not over anything": a gamepad press dismisses every zone
	 * except InsideAnchorOrCard, which is the right call, since a player
	 * pressing a pad button is doing something other than reading this card,
	 * and the stale cursor position left over from whenever the mouse was
	 * last touched is not evidence of anything.
	 *
	 * Anywhere is unconditional. The three positional zones all test
	 * viewport-space icon/card rects, via the same GetIconViewportRect /
	 * GetCardViewportRect / IsPointInRect building blocks IsPointOverIconOrCard
	 * and IsCursorOverAnchorIcon use.
	 *
	 * OutsideAnchorAndCard defers to IsPointOverIconOrCard (padded,
	 * corridor-aware) while bKeepOpenUntilCursorLeaves is on, and otherwise
	 * tests the card and anchor rects directly.
	 *
	 * Every zone that needs a rect it cannot get answers NO. An unresolved
	 * rect means "this card has not been laid out yet", not "the press was
	 * outside it", and the three outside-ish zones treating the second as the
	 * first is why a card could be dismissed by the very click that was
	 * aimed at it, one frame after it appeared. Refusing to act on a rect
	 * that does not exist costs at most a frame of latency on a press nobody
	 * could have aimed yet.
	 */
	bool ShouldPressCloseCard(const FVector2D& PressPoint, bool bHasPressPoint) const;

	/**
	 * IsPointOverIconOrCard WITHOUT the nested-card cascade: only this card's
	 * own icon, card, and the corridor between them, padded the same way. All
	 * of the actual geometry lives here; IsPointOverIconOrCard is this plus
	 * IsPointOverDescendantCard.
	 *
	 * bUnresolvedCountsAsInside decides what "neither rect has resolved yet"
	 * answers. True for this card's own keep-open test ("can't tell yet" must
	 * never read as "the cursor left"). False when the question is asked
	 * about a descendant on its parent's behalf: a nested card that hasn't
	 * been laid out isn't somewhere the cursor can be, and answering true
	 * would let a card that never finished appearing hold its whole ancestor
	 * chain open.
	 *
	 * Also what RemoveCardOnPressing's InsideAnchorOrCard zone uses, so that
	 * zone keeps meaning exactly this card and its anchor.
	 */
	bool IsPointOverOwnIconOrCard(const FVector2D& ViewportPoint, bool bUnresolvedCountsAsInside) const;

	/**
	 * True if ViewportPoint is inside the own region (IsPointOverOwnIconOrCard)
	 * of any live card nested inside this one, at any depth. Found by walking
	 * ActiveCards and asking each card whether this one is among its
	 * ancestors (IsAncestorCard), so grandchildren are found in the same pass
	 * as children, with no recursion. Cards still being constructed or
	 * already closing are skipped.
	 */
	bool IsPointOverDescendantCard(const FVector2D& ViewportPoint) const;

	/**
	 * True if the cursor is currently over the anchor's icon rect, with no
	 * padding and no corridor/card allowance — deliberately narrower than
	 * IsCursorOverIconOrCard, which this is not a variant of. Backs
	 * bReappearOnClickDismiss. Note that the dismissal path itself no longer
	 * calls this: EvaluateGlobalPress asks where the PRESS landed, at the
	 * moment it landed, which is the same distinction answered against
	 * better evidence. Kept because a subclass may still want the live
	 * cursor form of the question.
	 * False if the anchor's geometry hasn't resolved (no rect to test
	 * against), which correctly favours "not over it" here rather than
	 * IsCursorOverIconOrCard's fail-safe true — an unresolved rect on the
	 * one frame this runs (right as the card is closing) is not a reason to
	 * reopen it.
	 */
	bool IsCursorOverAnchorIcon() const;

	/**
	 * Only does anything if bKeepOpenUntilCursorLeaves is set: closes this
	 * card once IsCursorOverIconOrCard() has been false for
	 * KeepOpenGracePeriod seconds straight. The anchor deliberately skips its
	 * own mouse-leave close for an entry like this (see
	 * UInfoCardAnchor::UntriggerHoverEntries), so this is the only thing
	 * actually closing it in that case.
	 */
	void TickKeepOpenWhileCursorNearby(float InDeltaTime);

	/**
	 * Recomputes CardRectMin/Max as the union of two views of where this card
	 * is: the position UpdatePosition last computed (correct immediately, but
	 * only as accurate as GetDesiredSize) and the geometry Slate actually
	 * painted (exact, but a frame behind). Unioning them means a card that is
	 * moving — following the cursor, say — is never briefly considered to be
	 * somewhere the cursor isn't. Runs unconditionally every tick: both the
	 * keep-open cursor test and AvoidOverlappingCards (on OTHER cards, testing
	 * against this one) depend on it being current.
	 */
	void CacheCardViewportRect();

	/** The anchor icon's rect in viewport space. False if the anchor's geometry hasn't resolved yet, in which case the rect is meaningless and must not be treated as "cursor is outside". */
	bool GetIconViewportRect(FVector2D& OutMin, FVector2D& OutMax) const;

	/** This card's own rect in viewport space, from CacheCardViewportRect. False before the card's first layout pass. */
	bool GetCardViewportRect(FVector2D& OutMin, FVector2D& OutMax) const;

	static bool IsPointInRect(const FVector2D& Point, const FVector2D& Min, const FVector2D& Max);

	/**
	 * If bAvoidOverlappingCards is on, checks InPosition/CardSize against
	 * every other currently-visible card's cached rect (see ActiveCards) that
	 * was shown earlier than this card (lower SpawnSequence), in two passes.
	 *
	 * Pass 1 — unrelated cards: for each one it overlaps, pushes this card
	 * below it by ClampPadding. Repeats (bounded) since resolving one overlap
	 * can create another against a third card. Always re-clamped to the
	 * viewport afterward, the same way PositionToIcon's own flip/clamp does,
	 * so a nudge can never push the card off-screen.
	 *
	 * Pass 2 — this card's own ancestor chain (see IsAncestorCard), ToIcon
	 * cards only. Ancestors are never pushed against wholesale: a submenu is
	 * supposed to sit flush against the menu it came out of. Only the part of
	 * each ancestor this card has no business covering is kept clear — for
	 * the parent, everything except its authored side (see
	 * GetParentCardAvoidRect), and for anything further up, the whole card
	 * less whatever the cards in between already overlap (see
	 * GetAncestorCardAvoidRect). If pass 1's answer covers any of that, the
	 * card instead slides left/right/up/down from InPosition, hopping past
	 * everything in its way (ancestors and unrelated cards alike), and the
	 * cheapest slide that ends up clear and on screen wins. Slides that
	 * carry on along the card's placement axis are preferred; stepping
	 * sideways off that axis costs double; doubling back across the icon is
	 * never tried. Nothing clear anywhere keeps pass 1's answer.
	 *
	 * This is what moves a dropdown that flipped up over its own menu out to
	 * the side, and a Left card that would land on a menu further up the
	 * chain on past it, rather than leaving either one on top.
	 *
	 * Fill Icon cards are left out of this in both directions. UpdatePosition
	 * never calls this for one (it's bound to its icon by definition, and a
	 * nudge would detach it), and other cards don't yield to one either (it
	 * only covers its own icon, which is part of the UI underneath rather than
	 * a card competing for screen space).
	 *
	 * This is a heuristic, not a true simultaneous layout solver: it only
	 * ever moves the later card, compared against peers' positions from their
	 * own last tick (up to one frame stale). For the handful of
	 * concurrently-visible cards this system is meant for, that settles
	 * visually within a frame or two and never oscillates, because "earlier
	 * card never yields to a later one" gives every pair a single, consistent
	 * winner, and an ancestor is always the earlier card.
	 */
	FVector2D AvoidOverlappingCards(const FVector2D& InPosition, const FVector2D& CardSize) const;

	/**
	 * The part of Parent (the card this card's anchor lives inside) that a
	 * ToIcon card of CardSize must keep clear of, for AvoidOverlappingCards.
	 * Parent's rect with two strips removed: everything from where this
	 * card's IconPosition puts it (before any flip or clamp, Offset included)
	 * outward, which is the card's to cover; and a strip of the same
	 * icon-to-edge margin on the opposite edge, so a card that flipped only
	 * overlapping the menu's frame still counts as flush. False (parent fully
	 * exempt) for diagonal placements, before the icon has resolved, or if
	 * nothing is left to keep clear of.
	 */
	bool GetParentCardAvoidRect(const UInfoCardWidget* Parent, const FVector2D& CardSize, FVector2D& OutMin, FVector2D& OutMax) const;

	/**
	 * The part of AncestorChain[AncestorIndex] (a grandparent or further up)
	 * that a card must keep clear of, for AvoidOverlappingCards: the
	 * ancestor's whole rect, inset along any edge the cards between it and
	 * this one (AncestorChain[0 .. AncestorIndex-1]) already poke through,
	 * by as far as they poke through it. Each of those cards only earns an
	 * allowance on the one edge it crosses most shallowly. False if nothing is
	 * left to keep clear of.
	 */
	static bool GetAncestorCardAvoidRect(const TArray<const UInfoCardWidget*>& AncestorChain, int32 AncestorIndex, FVector2D& OutMin, FVector2D& OutMax);

	/**
	 * Every Uinfocardwidget currently on screen, across every anchor —
	 * added in NativeConstruct, removed in NativeDestruct. Weak so a card
	 * destroyed without going through CloseCard (e.g. a hard teardown) just
	 * falls out rather than dangling. Exists purely to give
	 * AvoidOverlappingCards something to test against without having to
	 * reach through every UInfoCardAnchor's RuntimeStates to find them.
	 */
	static TArray<TWeakObjectPtr<UInfoCardWidget>> ActiveCards;

	/** Source of SpawnSequence; incremented once per card in SetupCard. */
	static int64 NextSpawnSequence;

	// ------------------------------------------------------------------
	// Global press tracking
	// ------------------------------------------------------------------
	//
	// Cards used to detect dismissing presses by polling
	// APlayerController::WasInputKeyJustPressed each tick. That only ever
	// sees a press Slate declined to handle, which makes it blind to
	// precisely the presses that matter most: one landing on a Button, on
	// another card, on this card's own anchor, or anywhere at all while the
	// game has a UI-only input mode set. That is the whole of "it works in
	// some places and not others", and no amount of zone logic on top could
	// fix it, because the press simply never arrived.
	//
	// A Slate input pre-processor sees every press before routing, whatever
	// ends up consuming it. It observes and dispatches only — it never
	// returns true, so nothing downstream is starved of input.
	//
	// The press is pushed straight to every live card rather than parked in a
	// "most recent press" slot for cards to pull from on their next tick.
	// That slot was the source of most of the remaining flakiness: it held
	// one press (so two in a frame became one), it was read a frame late (so
	// the layout it was tested against was not the one the player clicked),
	// it required the card to tick (so a collapsed card, or one whose WBP has
	// TickFrequency set to Never or Auto, never dismissed at all), and it
	// needed serial bookkeeping on every card to avoid double-handling. A
	// card created during the dispatch of a press is simply not in
	// ActiveCards yet, which is why no "ignore the press that spawned me"
	// guard is needed any more — the guard the serials existed for is now
	// structural.

	/**
	 * Hands Key/position to every card in ActiveCards. Sets flags only;
	 * nothing is torn down from inside input dispatch.
	 */
	static void DispatchPressToActiveCards(bool bHasPosition, const FVector2D& AbsolutePosition);

	/**
	 * Registers the shared press pre-processor with FSlateApplication if it
	 * isn't already. Idempotent, and called from both SetupCard and
	 * NativeConstruct so the hook is in place before the first card can
	 * possibly need it.
	 *
	 * Registered once and left registered for the life of the application
	 * (torn down from FCoreDelegates::OnPreExit), rather than refcounted up
	 * and down as cards come and go. Refcounting meant unregistering from
	 * NativeDestruct, and NativeDestruct routinely runs while Slate is
	 * dispatching the very input that closed the card — mutating the
	 * pre-processor list from inside its own iteration. The processor costs
	 * nothing when no cards exist: DispatchPressToActiveCards over an empty
	 * array.
	 */
	static void EnsurePressWatcher();

	/** One entry per set in Sounds, resized on demand. Not a UPROPERTY: it holds timer handles, nothing to serialize. */
	TArray<FUISoundRuntime> SoundRuntimes;
};