#pragma once

#include "CoreMinimal.h"
#include "Components/Border.h"
#include "GameplayTagContainer.h"
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
 * One payload-class slot within an FInfoCardPayloadDefault. Split out from a
 * bare payload array so a slot can exist — filled in with the right class,
 * ready to edit — WITHOUT taking effect until bSetDefault is switched on.
 * This is what makes the automatic sync safe: it can stamp out a slot for
 * every payload class InfoCards_DA has for a CardId, and those slots stay
 * inert (the Data Asset's own value keeps being used) until you deliberately
 * opt one in.
 */
USTRUCT(BlueprintType)
struct FInfoCardPayloadDefaultEntry
{
	GENERATED_BODY()

	/**
	 * Off by default on every auto-generated slot. While off, Payload's value
	 * is ignored entirely and the Data Asset's own payload of this class keeps
	 * being used. Switch on once you've actually edited Payload below and want
	 * it to be this anchor's default instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	bool bSetDefaults = false;

	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Info Card", meta = (EditCondition = "bSetDefaults"))
	TObjectPtr<UInfoCardPayload> Payload = nullptr;
};

/**
 * This anchor's own payload defaults for a single CardId, layered on top of
 * whatever InfoCards_DA authored. Matched by CardId rather than array index,
 * since indices shift if the Data Asset's InfoCards array is reordered.
 *
 * You never build this by hand: PayloadDefaults is kept in sync with
 * InfoCards_DA automatically, so it always shows exactly one entry per
 * CardId, each with one slot per payload class that card carries — CardId
 * and class already filled in, bSetDefault off. Switch bSetDefault on for
 * whichever slots this particular anchor should customize, and edit that
 * slot's fields (Instanced/EditInlineNew, so e.g. a Text payload's
 * Title/Body expand and edit inline right here). Everything left off keeps
 * coming from the Data Asset unchanged.
 */
USTRUCT(BlueprintType)
struct FInfoCardPayloadDefault
{
	GENERATED_BODY()

	/** Which InfoCards_DA entry these defaults apply to. Kept in sync automatically; not meant to be typed by hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (DisplayName = "Card ID", EditCondition = "false"))
	FName CardId = NAME_None;

	/** One slot per payload class this CardId carries. See FInfoCardPayloadDefaultEntry for how bSetDefault gates each one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	TArray<FInfoCardPayloadDefaultEntry> Payloads;
};

/**
 * This anchor's own trigger/placement/offset/tick settings for a single
 * CardId, layered on top of whatever InfoCards_DA authored — the settings
 * equivalent of FInfoCardPayloadDefault, but one struct per CardId rather
 * than one slot per payload class, since these settings aren't per-payload.
 *
 * Each of the four bOverride* toggles below gates a related group of
 * fields, and those fields are HIDDEN (not merely greyed) until their own
 * toggle is on — so an untouched entry shows nothing but the toggles
 * themselves. bDisableCard sits above all four: when it's on, this CardId
 * is force-disabled on this anchor (as if SetCardEnabledById(CardId, false)
 * had been called) and none of the other overrides are applied, which is
 * why the four toggles grey out while it's on — there'd be nothing for them
 * to do.
 *
 * CardType/HoverDelay/bToggleOnRightClick/bToggleOnLeftClick additionally
 * fold in the same CardType-relevance check FInfoCardSpec itself uses
 * (HoverDelay only makes sense for Hover, etc.) — combined into the same
 * hide condition as bOverrideCardType, since a field can only have one.
 */
USTRUCT(BlueprintType)
struct FInfoCardSettingsOverride
{
	GENERATED_BODY()

	/**
	 * When on, this CardId is force-disabled on this anchor — equivalent to
	 * SetCardEnabledById(CardId, false) — regardless of InfoCards_DA's
	 * bEnabled or any of the overrides below. Every override toggle on this
	 * struct greys out while this is on, since none of them would have any
	 * effect on a card that can't show.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	bool bDisableCard = false;

	// ------------------------------------------------------------------
	// Trigger
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (EditCondition = "!bDisableCard"))
	bool bOverrideCardType = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (EditCondition = "bOverrideCardType", EditConditionHides))
	ECardType CardType = ECardType::Hover;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (EditCondition = "bOverrideCardType && CardType == ECardType::Hover", EditConditionHides, ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float HoverDelay = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (EditCondition = "bOverrideCardType && CardType == ECardType::RightClick", EditConditionHides))
	bool bToggleOnRightClick = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (EditCondition = "bOverrideCardType && CardType == ECardType::LeftClick", EditConditionHides))
	bool bToggleOnLeftClick = true;

	// ------------------------------------------------------------------
	// Placement
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "!bDisableCard"))
	bool bOverrideCardPosition = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition", EditConditionHides))
	EInfoCardPosition PositionMode = EInfoCardPosition::ToIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition", EditConditionHides))
	EDirection2D IconPosition = EDirection2D::Up;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition", EditConditionHides))
	EInfoCardCursorPosition CursorPosition = EInfoCardCursorPosition::UpRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition", EditConditionHides))
	EViewportPosition ViewportPosition = EViewportPosition::TopRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition", EditConditionHides))
	bool bClampToViewport = true;

	// ------------------------------------------------------------------
	// Offset
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offset", meta = (EditCondition = "!bDisableCard"))
	bool bOverrideOffset = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offset", meta = (EditCondition = "bOverrideOffset", EditConditionHides))
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offset", meta = (EditCondition = "bOverrideOffset", EditConditionHides))
	float HoverAnchorPadding = 0.f;

	// ------------------------------------------------------------------
	// Tick
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tick", meta = (EditCondition = "!bDisableCard"))
	bool bOverrideTickUpdate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tick", meta = (EditCondition = "bOverrideTickUpdate", EditConditionHides))
	bool bUpdatePositionOnTick = true;
};

/**
 * One array element of SettingsOverrides: which CardId (matched the same
 * way as FInfoCardPayloadDefault::CardId — auto-synced, not typed by hand)
 * plus that CardId's FInfoCardSettingsOverride.
 */
USTRUCT(BlueprintType)
struct FInfoCardSettingsOverrideElement
{
	GENERATED_BODY()

	/** Which InfoCards_DA entry this applies to. Kept in sync automatically; not meant to be typed by hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (DisplayName = "Card ID", EditCondition = "false"))
	FName CardId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	FInfoCardSettingsOverride Settings;
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
 *
 * SUPPRESSION: any gameplay tag rooted at InfoCard.Suppress can be attached to
 * this anchor at runtime via AddSuppressionTag/AddSuppressionTags (and
 * removed via RemoveSuppressionTag/RemoveSuppressionTags). While an attached
 * tag overlaps FInfoCardSpec::SuppressionTags on a given InfoCards entry,
 * that specific entry is suppressed (won't spawn; closes immediately if it's
 * already up) without affecting any other entry on the same anchor. While an
 * attached tag overlaps UInfoCardDataAsset::SuppressionTags on InfoCards_DA,
 * EVERY entry sourced from that data asset is suppressed the same way.
 *
 * PER-INSTANCE DEFAULTS: InfoCards_DA supplies the values every anchor
 * referencing it starts from. To make one particular anchor use different
 * payload values (e.g. different Title/Body text) without touching the Data
 * Asset or affecting any other anchor that shares it, switch on Set Default
 * for that CardId's payload slot in PayloadDefaults and edit it there. That
 * array is maintained automatically from the assigned Data Asset.
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

	/**
	 * Optionally set payload defaults here. Useful for quick info cards, like tooltips. 
	 * If a field is added to the data asset and the array doesn't update, just clear the data asset and set it again. 
	 *
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (TitleProperty = "CardId", DisplayName = "Payload Defaults"))
	TArray<FInfoCardPayloadDefault> PayloadDefaults;

	/**
	 * This anchor's own trigger/placement/offset/tick overrides, one per
	 * CardId. Kept in sync with InfoCards_DA the same way PayloadDefaults is
	 * (auto add/prune by CardId; if a field is added to the data asset and
	 * this doesn't pick it up, clear InfoCards_DA and set it again). Every
	 * entry starts fully un-toggled, so nothing changes until you switch an
	 * override on. See FInfoCardSettingsOverride for what each one does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (TitleProperty = "CardId", DisplayName = "Settings Overrides"))
	TArray<FInfoCardSettingsOverrideElement> SettingsOverrides;

	// ------------------------------------------------------------------
	// By index
	// ------------------------------------------------------------------

	/** Spawns InfoCards[Index] now, ignoring hover delay. Safe to call if it's already up. No-ops if the entry is currently suppressed. */
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
	 * Once payload variables are set, call RefreshCard with the same CardID to update the payload 
	 * to the info card.
	 * 
	 * @param PayloadClass A child class of InfoCardPayload. 
	 * @return Returns the current state of the payload object.
	 * @param bIsPayloadValid Returns false if the card was invalid or if the payload was not set
	 * on the info card element in the info card data asset. 
	 * Drag this pin off and wire your values to the SET values. If the payload is categorized
	 * correctly, variables can be found under Variables > Payload, or just by checking the payload class.
	 * @param InputCardID The input CardID as an output pin, for convenience.
	 */
	UFUNCTION(BlueprintCallable, Category = "Info Card", meta = (DeterminesOutputType = "PayloadClass"))
	UInfoCardPayload* GetOrAddPayload(FName CardId, 
									  TSubclassOf<UInfoCardPayload> PayloadClass,
									  bool& bIsPayloadValid,
									  FName& InputCardID);

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

	// ------------------------------------------------------------------
	// Suppression
	// ------------------------------------------------------------------

	/**
	 * Attaches Tag (expected to be rooted at InfoCard.Suppress) to this anchor.
	 * Any InfoCards entry whose own SuppressionTags, or whose InfoCards_DA's
	 * SuppressionTags, now overlaps this anchor's suppression tags is closed
	 * immediately if it's active, and won't spawn again until the matching
	 * tag is removed. Every other entry is unaffected. Safe to call with an
	 * already-attached tag.
	 */
	UFUNCTION(BlueprintCallable, Category = "Info Card|Suppression")
	void AddSuppressionTag(FGameplayTag Tag);

	/** Batched version of AddSuppressionTag. */
	UFUNCTION(BlueprintCallable, Category = "Info Card|Suppression")
	void AddSuppressionTags(const FGameplayTagContainer& Tags);

	/**
	 * Detaches Tag from this anchor. Does not re-open any card that was
	 * closed by suppression — the next natural trigger (hover, click, an
	 * explicit Show call) rebuilds it fresh once it's no longer suppressed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Info Card|Suppression")
	void RemoveSuppressionTag(FGameplayTag Tag);

	/** Batched version of RemoveSuppressionTag. */
	UFUNCTION(BlueprintCallable, Category = "Info Card|Suppression")
	void RemoveSuppressionTags(const FGameplayTagContainer& Tags);

	/** The suppression tags currently attached to this anchor. */
	UFUNCTION(BlueprintPure, Category = "Info Card|Suppression")
	const FGameplayTagContainer& GetSuppressionTags() const { return ActiveSuppressionTags; }

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;

	/** Auto-runs SyncPayloadDefaultsFromDataAsset and SyncSettingsOverridesFromDataAsset when InfoCards_DA is (re)assigned in the Details panel. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
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

	/**
	 * True if InfoCards[Index] is currently suppressed on this anchor —
	 * either because the entry's own SuppressionTags overlaps
	 * ActiveSuppressionTags, or because InfoCards_DA's data-asset-wide
	 * SuppressionTags does. False (never suppressed) if ActiveSuppressionTags
	 * is empty, so anchors that never call AddSuppressionTag pay effectively
	 * nothing for this check.
	 */
	bool IsCardSuppressedAtIndex(int32 Index) const;

	/**
	 * Rebuilds InfoCards from InfoCards_DA: duplicates every payload (so this
	 * anchor owns its own instances rather than sharing the Data Asset's),
	 * then calls ApplyPayloadDefaults so PayloadDefaults is layered on top.
	 * The single place both SynchronizeProperties and the lazy-sync branch of
	 * GetOrAddPayload funnel through, so defaults apply consistently no
	 * matter which path populated InfoCards first. No-op if InfoCards_DA
	 * isn't set.
	 */
	void RebuildInfoCardsFromDataAsset();

	/**
	 * Merges PayloadDefaults into the just-synced InfoCards: for each
	 * FInfoCardPayloadDefaultEntry with bSetDefault on, replaces the existing
	 * payload of the same class on the matching CardId (or appends it, if
	 * InfoCards_DA had none of that class). Entries with bSetDefault off — the
	 * default for auto-generated stubs — are skipped entirely, so an
	 * un-customized stub never displaces the Data Asset's real value. A
	 * CardId with no matching InfoCards entry is simply skipped too.
	 */
	void ApplyPayloadDefaults();

	/**
	 * Merges SettingsOverrides into the just-synced InfoCards: for the
	 * matching CardId, bDisableCard (if on) force-disables the entry and
	 * skips everything else; otherwise each bOverride* group that's on
	 * copies its fields onto the matching FInfoCardSpec fields, and any
	 * group left off leaves those fields exactly as InfoCards_DA authored
	 * them. A CardId with no matching InfoCards entry is skipped.
	 */
	void ApplySettingsOverrides();

#if WITH_EDITOR
	/**
	 * Makes PayloadDefaults match InfoCards_DA exactly: adds a slot for every
	 * CardId / payload class the Data Asset has and doesn't already have one
	 * for, and prunes any entry or slot the Data Asset no longer has (which
	 * includes emptying PayloadDefaults entirely when InfoCards_DA is
	 * cleared — a stale list of defaults for a Data Asset that's no longer
	 * assigned is just misleading). Slots that survive are left untouched,
	 * so anything already switched on and edited keeps its value.
	 *
	 * Editor-only, and called automatically: from PostEditChangeProperty
	 * when InfoCards_DA is reassigned, and from SynchronizeProperties at
	 * design time, which covers edits made to the Data Asset's own contents
	 * afterward (a separate asset this anchor can't otherwise be notified
	 * about).
	 */
	void SyncPayloadDefaultsFromDataAsset();

	/**
	 * SettingsOverrides equivalent of SyncPayloadDefaultsFromDataAsset: adds
	 * an entry (all overrides off) for every CardId InfoCards_DA has that
	 * doesn't already have one, and prunes any entry for a CardId the Data
	 * Asset no longer has (which includes emptying SettingsOverrides
	 * entirely when InfoCards_DA is cleared). An entry that survives keeps
	 * its FInfoCardSettingsOverride untouched. Called from the same two
	 * places as SyncPayloadDefaultsFromDataAsset.
	 */
	void SyncSettingsOverridesFromDataAsset();
#endif

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

	/**
	 * Runtime-only tags attached to this anchor via AddSuppressionTag(s) /
	 * RemoveSuppressionTag(s). Not authored in the Details panel — this is
	 * live state (e.g. "player is in combat", "menu is open"), not
	 * configuration; the configuration side lives on FInfoCardSpec and
	 * UInfoCardDataAsset. Deliberately not BlueprintReadWrite: mutation goes
	 * through Add/RemoveSuppressionTag(s) so a newly-suppressed card is
	 * closed as part of the same call, rather than left active and stale.
	 */
	UPROPERTY(Transient)
	FGameplayTagContainer ActiveSuppressionTags;
};