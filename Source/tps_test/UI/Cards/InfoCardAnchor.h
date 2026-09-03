#pragma once

#include "CoreMinimal.h"
#include "Components/Border.h"
#include "UI/Cards/InfoCardSpecDataAsset.h"
#include "InfoCardAnchor.generated.h"

class UInfoCardWidgetBase;

/**
 * Live state for one entry in InfoCards. Kept index-parallel with InfoCards,
 * the same way UInfoCardWidgetBase keeps its animation states index-parallel
 * with its animation arrays — the config array is authored in the Details
 * panel, this one is runtime-only and rebuilt to match it. Its own struct
 * (rather than two separate transient arrays) so ActiveCard and
 * HoverDelayHandle can never drift out of alignment with each other.
 */
USTRUCT()
struct FInfoCardRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UInfoCardWidgetBase> ActiveCard = nullptr;

	FTimerHandle HoverDelayHandle;
};

/**
 * Wrap any widget in one of these to give it one or more info cards. Derived
 * from UBorder, so it behaves like a normal single-child container: drag
 * "Info Card Anchor" out of the palette, drop your Button / Border / Image
 * inside it, then add entries to InfoCards in the Details panel.
 *
 * Each entry in InfoCards is independent: its own trigger (hover with its own
 * delay, or right-click), its own payload(s), its own placement. A single
 * anchor can show a quick hover tip at 0.1s, a detailed hover tip at 1.5s,
 * and a right-click context card, all at once if the player manages it.
 *
 * The anchor MUST be hit-testable to receive hover / right-click at all.
 * UBorder defaults to SelfHitTestInvisible, so this class flips the default
 * to Visible and, if it finds itself hit-test-invisible with cards
 * configured, forces it back and warns — that combination is never what
 * anyone wants and is otherwise completely silent.
 *
 * No Blueprint graph work is required on the owning widget.
 */
UCLASS(meta = (DisplayName = "Info Card Anchor"))
class TPS_TEST_API UInfoCardAnchor : public UBorder
{
	GENERATED_BODY()

public:

	UInfoCardAnchor();

	/**
	 * Every card this anchor can show. Add one entry per independent
	 * tooltip/context-card behaviour. Give an entry a CardId if you want to
	 * address it from Blueprint by name instead of array index.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (TitleProperty = "CardId", DisplayName = "Info Cards"))
	TObjectPtr<UInfoCardDataAsset> InfoCards_DA;

	 
	
	// ------------------------------------------------------------------
	// By index
	// ------------------------------------------------------------------

	/** Spawns InfoCards[Index] now, ignoring hover delay. Safe to call if it's already up. */
	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void ShowCardAtIndex(int32 Index);

	/** Destroys the active card at Index and cancels its pending hover timer, if any. */
	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void HideCardAtIndex(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void ToggleCardAtIndex(int32 Index);

	/** Enables/disables InfoCards[Index] at runtime. Disabling closes it immediately if active. */
	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void SetCardEnabledAtIndex(int32 Index, bool bNewEnabled);

	/** Replaces InfoCards[Index]'s payload set. Updates the live card if one is showing; closes it if NewPayloads is empty. */
	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void SetPayloadsAtIndex(int32 Index, const TArray<UInfoCardPayload*>& NewPayloads);

	/** Convenience wrapper around SetPayloadsAtIndex for the common single-payload case. */
	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void SetPayloadAtIndex(int32 Index, UInfoCardPayload* NewPayload);

	/** The card currently on screen at Index, or null. */
	UFUNCTION(BlueprintPure, Category = "Info Card")
	UInfoCardWidgetBase* GetActiveCardAtIndex(int32 Index) const;

	// ------------------------------------------------------------------
	// By CardId
	// ------------------------------------------------------------------

	/** Index of the InfoCards entry whose CardId matches, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "Info Card")
	int32 FindCardIndexById(FName CardId) const;

	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void ShowCardById(FName CardId);

	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void HideCardById(FName CardId);

	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void ToggleCardById(FName CardId);

	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void SetCardEnabledById(FName CardId, bool bNewEnabled);

	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void SetPayloadsById(FName CardId, const TArray<UInfoCardPayload*>& NewPayloads);

	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void SetPayloadById(FName CardId, UInfoCardPayload* NewPayload);

	/**
	 * The low-friction path for supplying data. Finds CardId's existing
	 * payload of PayloadClass — one authored directly in InfoCards[].Payloads
	 * in the Details panel — and returns it so you can set its properties
	 * directly with normal Set nodes, rather than constructing a new payload
	 * object and calling SetPayloadsById every time. The object is live:
	 * mutating its properties takes effect the next time this card is shown
	 * with no further calls. If the card might already be open and you want
	 * it to redraw immediately, follow up with RefreshCard.
	 *
	 * Does NOT fabricate a payload that wasn't authored — bIsPayloadValid
	 * comes back false (and ReturnValue is null) if CardId doesn't exist or
	 * has no Payloads entry of PayloadClass. The Payloads array in the data
	 * asset is a required declaration, not an optional default: add the
	 * entry there rather than expecting this to create one silently.
	 */
	UFUNCTION(BlueprintCallable, Category = "Info Card", meta = (DeterminesOutputType = "PayloadClass"))
	UInfoCardPayload* GetOrAddPayload(FName CardId, TSubclassOf<UInfoCardPayload> PayloadClass, bool& bIsPayloadValid);

	/**
	 * Re-delivers CardId's current payload set to its active card, if one is
	 * showing (a no-op otherwise). Use after mutating a payload object
	 * returned by GetOrAddPayload when the card is already on screen and you
	 * want the change to appear without waiting for it to re-open.
	 */
	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void RefreshCard(FName CardId);

	UFUNCTION(BlueprintPure, Category = "Info Card")
	UInfoCardWidgetBase* GetActiveCardById(FName CardId) const;

	// ------------------------------------------------------------------
	// All cards
	// ------------------------------------------------------------------

	/** Closes every active card on this anchor and cancels every pending hover timer. */
	UFUNCTION(BlueprintCallable, Category = "Info Card")
	void HideAllCards();

	/**
	 * The UserWidget instance that contains this anchor in its widget tree
	 * (e.g. the specific W_InventorySlot this anchor was placed in). This is
	 * what ExclusivityScope::SameSourceWidgetClass compares by class against
	 * — not the anchor's own class, which is always UInfoCardAnchor.
	 */
	UFUNCTION(BlueprintPure, Category = "Info Card")
	UUserWidget* GetCardSourceWidget() const;

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:

	virtual TSharedRef<SWidget> RebuildWidget() override;

	// NOTE: mouse handlers are (re)bound here, not in RebuildWidget. UBorder
	// installs its own OnMouseButtonDown handler on MyBorder as part of
	// RebuildWidget, so binding ours there gets overwritten in the same
	// breath. SynchronizeProperties runs after RebuildWidget, so binding
	// here is what makes right-click actually reach us.
	virtual void SynchronizeProperties() override;

	void HandleMouseEnter(const FGeometry& Geometry, const FPointerEvent& PointerEvent);
	void HandleMouseLeave(const FPointerEvent& PointerEvent);
	FReply HandleMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& PointerEvent);
	void HandleHoverDelayElapsedForIndex(int32 Index);

	/**
	 * Applies InfoCards[SourceIndex].ExclusivityScope: closes other anchors'
	 * cards (never this anchor's own other entries — those are meant to
	 * coexist).
	 */
	void CloseCardsForExclusivity(int32 SourceIndex) const;

	/**
	 * Keeps RuntimeStates the same length as InfoCards, closing and clearing
	 * any state left over from entries that were removed. Cheap; called
	 * defensively at the top of every entry point, not just from
	 * SynchronizeProperties, so runtime edits to InfoCards from Blueprint
	 * stay in sync too.
	 */
	void EnsureRuntimeStatesSize();

	/**
	 * A Border that isn't hit-testable never receives MouseEnter or
	 * MouseButtonDown, so an anchor in that state silently does nothing at
	 * all. Puts it back to Visible (and warns once) if it has cards to show.
	 */
	void EnsureHitTestable();

	/** Adds/removes this anchor from the static exclusivity registry based on whether anything is currently active. */
	void UpdateRegistryMembership();

	/** Drops entries whose anchor has since been destroyed. */
	static void PruneRegistry();

	UPROPERTY(Transient)
	TArray<FInfoCardRuntimeState> RuntimeStates;

	/**
	 * Every anchor currently showing at least one card, across the whole
	 * process. Used to implement ExclusivityScope without anchors needing to
	 * know about each other directly. Weak so a destroyed anchor just falls
	 * out; pruned in PruneRegistry wherever it's walked or updated.
	 */
	static TArray<TWeakObjectPtr<UInfoCardAnchor>> ActiveCardAnchors;

	/**
	 * Set once InfoCards has been populated from InfoCards_DA at runtime.
	 * SynchronizeProperties only re-copies from InfoCards_DA while this is
	 * false (or at design-time) — see SynchronizeProperties for why an
	 * unconditional copy on every call is wrong.
	 */
	UPROPERTY(Transient)
	bool bInfoCardsSyncedFromDataAsset = false;

	/**
	 * Must stay UPROPERTY. FInfoCardSpec::Payloads holds UObject* payloads
	 * (via Instanced TObjectPtr) — without reflection on this array, the GC
	 * has no visible reference path to them, and any payload added here
	 * before its card is ever shown (e.g. via GetOrAddPayload) gets
	 * collected and turns into a dangling pointer on the very next GC pass.
	 */
	UPROPERTY()
	TArray<FInfoCardSpec> InfoCards;
};