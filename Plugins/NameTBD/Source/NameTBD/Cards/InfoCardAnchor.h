// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Border.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "Input/Events.h"
#include "InfoCardSpecDataAsset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimDesignerPreview.h"
#include "InfoCardAnchor.generated.h"

class UInfoCardWidget;
class FWidgetPath;
class FWeakWidgetPath;
class SInfoCardDesignerPreview;

/**
 * Live state for one entry in InfoCards. Kept index-parallel with InfoCards,
 * the same way Uinfocardwidget keeps its animation arrays index-parallel
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
    TObjectPtr<UInfoCardWidget> ActiveCard = nullptr;

    /**
     * CardId of the InfoCards entry ActiveCard was spawned from, cached at
     * spawn time. Lets OnCardHidden report the correct CardId even in the
     * edge case where InfoCards has already changed out from under this
     * slot (e.g. shrunk at runtime) by the time the card is closed.
     */
    UPROPERTY(Transient)
    FName ActiveCardId = NAME_None;

    /**
     * Pending Highlight Delay for this entry, if any. A core-ticker handle
     * rather than an FTimerHandle on purpose: the world timer manager stops
     * ticking while the game is paused, which left Highlight cards unable to
     * appear in pause menus. See the note at the top of InfoCardAnchor.cpp.
     * Always cleared through UInfoCardAnchor::ClearHoverDelayAtIndex.
     */
    FTSTicker::FDelegateHandle HoverDelayHandle;

    /**
     * Sound bookkeeping for the Triggered event only. It fires before Highlight Delay, so there is no card
     * widget yet to own it - the anchor has to. Everything from Shown onwards is the card's own business and
     * uses the runtimes on UInfoCardWidget instead. Sized on demand; see UInfoCardAnchor::PlayCardSound.
     */
    TArray<FUISoundRuntime> TriggerSoundRuntimes;
};

/**
 * Pre-flattening payload default slot. Only kept so anchors saved with the old
 * PayloadDefaults layout still load their values: UInfoCardAnchor::PostLoad
 * converts every slot that had Set Defaults on into a flat
 * FInfoCardPayloadDefault and discards the rest. Not shown anywhere.
 */
USTRUCT()
struct FInfoCardPayloadDefaultEntry
{
    GENERATED_BODY()

    UPROPERTY()
    bool bSetDefaults = false;

    UPROPERTY(Instanced)
    TObjectPtr<UNameTBD_Payload> Payload = nullptr;
};

/**
 * This anchor's own default for ONE payload of ONE CardId, layered on top of
 * whatever InfoCards_DA authored. PayloadDefaults holds exactly one of these
 * per payload the Data Asset has, kept in sync automatically, so entries can't
 * be added or removed by hand.
 *
 * There is no on/off switch. Payload starts out as a copy of the Data Asset's
 * payload, and only the fields you actually change are applied: every field
 * still matching Baseline (the Data Asset's value when this entry was made or
 * last refreshed) keeps coming from the Data Asset, including any later edits
 * made there.
 */
USTRUCT(BlueprintType)
struct FInfoCardPayloadDefault
{
    GENERATED_BODY()

    /**
     * Which InfoCards_DA entry this payload belongs to. Kept in sync automatically. Shown as the element's title (the
     * editor module's customization hides the row itself, and shows the payload's fields directly under the title).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (DisplayName = "Card ID", EditCondition = "false"))
    FName CardId = NAME_None;

    /**
     * This anchor's value for the payload. Starts as a copy of the Data Asset's. Only fields you change override the
     * Data Asset; anything left as-is keeps following it. Don't change the class here: it has to match the Data
     * Asset's payload to apply.
     */
    UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Info Card", meta = (NoResetToDefault))
    TObjectPtr<UNameTBD_Payload> Payload = nullptr;

    /**
     * The Data Asset's payload as it was when Payload was last refreshed from it. A field in Payload counts as
     * overridden exactly when it differs from the same field here. Hidden.
     */
    UPROPERTY(Instanced)
    TObjectPtr<UNameTBD_Payload> Baseline = nullptr;

    /** Pre-flattening layout, read only by UInfoCardAnchor::PostLoad. Always empty once loaded. */
    UPROPERTY()
    TArray<FInfoCardPayloadDefaultEntry> Payloads;
};

/**
 * This anchor's own trigger/placement/offset/tick settings for a single
 * CardId, layered on top of whatever InfoCards_DA authored — the settings
 * equivalent of FInfoCardPayloadDefault, but one struct per CardId rather
 * than one slot per payload class, since these settings aren't per-payload.
 *
 *
 * CardType/HoverDelay additionally fold in the same CardType-relevance
 * check FInfoCardSpec itself uses (HoverDelay only makes sense for Hover,
 * etc.) — combined into the same hide condition as bOverrideCardType,
 * since a field can only have one.
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
    ECardType CardType = ECardType::AlwaysOn;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (DisplayName = "Highlight Delay", ToolTip = "Seconds to wait after being triggered (by mouse hover or focus) before this card appears. 0 shows it immediately. Also skipped if another card closed within Successive Hover Window below — see UInfoCardDeveloperSettings' Successive Hover section for the on/off switch.", EditCondition = "bOverrideCardType && CardType == ECardType::Hover", EditConditionHides))
    FInfoCardConfigurableFloat HoverDelay = FInfoCardConfigurableFloat(0.35f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (ToolTip = "Seconds after any card (anywhere) closes during which this card, if triggered, skips Highlight Delay and shows instantly. Only takes effect while UInfoCardDeveloperSettings::bEnableSuccessiveHover is on.", EditCondition = "bOverrideCardType && CardType == ECardType::Hover", EditConditionHides))
    FInfoCardConfigurableFloat SuccessiveHoverWindow = FInfoCardConfigurableFloat(0.5f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (ToolTip = "If on, this card can be triggered by the mouse hovering its anchor.", EditCondition = "bOverrideCardType && CardType == ECardType::Hover", EditConditionHides))
    bool bTriggerOnMouseHover = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (ToolTip = "If on, this card can be triggered by keyboard/gamepad focus reaching its anchor. See UInfoCardAnchor::FocusTriggerMode for how that focus is detected.", EditCondition = "bOverrideCardType && CardType == ECardType::Hover", EditConditionHides))
    bool bTriggerOnFocus = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (DisplayName = "Reappear On Click Dismiss", ToolTip = "If on, dismissing this card by clicking its own anchor (cursor never actually leaving it) makes it trigger again immediately, instead of waiting for the cursor to leave and come back.", EditCondition = "bOverrideCardType && CardType == ECardType::Hover", EditConditionHides))
    bool bReappearOnClickDismiss = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (ToolTip = "If on, once shown this card stays open even after the cursor leaves the anchor, as long as it's still over the anchor's icon (expanded by Icon Anchor Padding) or the card itself. Mouse-only — losing focus still closes it immediately.", EditCondition = "bOverrideCardType && CardType == ECardType::Hover", EditConditionHides))
    bool bKeepOpenUntilCursorLeaves = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (ToolTip = "Extra slack, in viewport units, around both the icon and the card when deciding whether the cursor has left. Bridges the gap the card is drawn across; raise it if the card closes while the cursor is on its way to it.", EditCondition = "bOverrideCardType && CardType == ECardType::Hover && bKeepOpenUntilCursorLeaves", EditConditionHides))
    FInfoCardConfigurableFloat KeepOpenSlack = FInfoCardConfigurableFloat(16.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (ToolTip = "Seconds the cursor must stay outside the icon/card region before the card actually closes. Any return inside resets it.", EditCondition = "bOverrideCardType && CardType == ECardType::Hover && bKeepOpenUntilCursorLeaves", EditConditionHides))
    FInfoCardConfigurableFloat KeepOpenGracePeriod = FInfoCardConfigurableFloat(0.2f);

    // ------------------------------------------------------------------
    // Placement
    // ------------------------------------------------------------------

    /** Puts a named Placement Preset (Project Settings > Info Card > Placement Presets) on this card; Override Card Position and Offset still go on top. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (DisplayName = "Override Placement Preset", EditCondition = "!bDisableCard"))
    bool bOverridePlacementPreset = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (DisplayName = "Placement Preset", GetOptions = "InfoCardDeveloperSettings.GetPlacementPresetNameOptions", EditCondition = "bOverridePlacementPreset", EditConditionHides))
    FName PlacementPreset = TEXT("Default");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "!bDisableCard"))
    bool bOverrideCardPosition = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition", EditConditionHides))
    EInfoCardPosition PositionMode = EInfoCardPosition::ToIcon;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition && PositionMode == EInfoCardPosition::ToIcon", EditConditionHides))
    EDirection2D IconPosition = EDirection2D::Up;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ToolTip = "Inset from each edge of the icon (Left/Top/Right/Bottom) when Icon Position is Fill Icon. See FInfoCardSpec::FillIconPadding.", EditCondition = "bOverrideCardPosition && PositionMode == EInfoCardPosition::ToIcon && IconPosition == EDirection2D::FillIcon", EditConditionHides))
    FMargin FillIconPadding = FMargin(0.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition && PositionMode == EInfoCardPosition::ToCursor", EditConditionHides))
    EInfoCardCursorPosition CursorPosition = EInfoCardCursorPosition::UpRight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition && PositionMode == EInfoCardPosition::ToViewport", EditConditionHides))
    EViewportPosition ViewportPosition = EViewportPosition::TopRight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition", EditConditionHides))
    bool bClampToViewport = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ToolTip = "Whether a card running out of room flips to the opposite side before falling back to a plain clamp. Only meaningful for ToIcon/ToCursor.", EditCondition = "bOverrideCardPosition && bClampToViewport && PositionMode != EInfoCardPosition::ToViewport", EditConditionHides))
    FInfoCardConfigurableBool FlipToAvoidClipping = FInfoCardConfigurableBool(true);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition && bClampToViewport", EditConditionHides))
    float ClampPadding = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "bOverrideCardPosition", EditConditionHides, ToolTip = "If on, this card nudges itself out of the way of any other currently-visible card it would overlap. See FInfoCardSpec::bAvoidOverlappingCards."))
    bool bAvoidOverlappingCards = true;

    // ------------------------------------------------------------------
    // Offset
    // ------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offset", meta = (EditCondition = "!bDisableCard"))
    bool bOverrideOffset = false;

    // Offset only matters for ToCursor/ToViewport, and HoverAnchorPadding
    // only for ToIcon — same as FInfoCardSpec's CardOffset/IconAnchorPadding
    // — but this struct's own PositionMode field is only trustworthy when
    // bOverrideCardPosition is ALSO on; a designer can toggle bOverrideOffset
    // without touching bOverrideCardPosition at all, in which case the real
    // PositionMode in effect comes from the base spec, not from this stale,
    // still-default field here. Each condition below only hides its field
    // once bOverrideCardPosition confirms the mode; otherwise the mode is
    // unknown and the field stays visible rather than guessing.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offset", meta = (EditCondition = "bOverrideOffset && (!bOverrideCardPosition || PositionMode != EInfoCardPosition::ToIcon)", EditConditionHides))
    FVector2D Offset = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offset", meta = (EditCondition = "bOverrideOffset && (!bOverrideCardPosition || PositionMode == EInfoCardPosition::ToIcon)", EditConditionHides))
    float HoverAnchorPadding = 0.f;

    // ------------------------------------------------------------------
    // Tick
    // ------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tick", meta = (EditCondition = "!bDisableCard"))
    bool bOverrideTickUpdate = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tick", meta = (EditCondition = "bOverrideTickUpdate", EditConditionHides))
    bool bUpdatePositionOnTick = true;

    // ------------------------------------------------------------------
    // Dismissal
    // ------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dismissal", meta = (EditCondition = "!bDisableCard"))
    bool bOverrideDismissal = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dismissal", meta = (ToolTip = "Where a press has to land to remove this card. Outside Anchor and Card is the ordinary 'click away to dismiss' behaviour; Outside Card differs only in that pressing the anchor dismisses too. Any mouse or gamepad button counts.", EditCondition = "bOverrideDismissal", EditConditionHides))
    ECardRemoveOnPressZone RemoveCardOnPressing = ECardRemoveOnPressZone::OutsideAnchorAndCard;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dismissal", meta = (ToolTip = "Seconds after this card appears before it auto-closes itself, regardless of cursor position. 0 means never.", EditCondition = "bOverrideDismissal", EditConditionHides))
    FInfoCardConfigurableFloat AutoDismissDuration = FInfoCardConfigurableFloat(0.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dismissal", meta = (ToolTip = "If on, a dismiss click also marks this frame as consumed for info-card purposes (see UInfoCardAnchor::WasDismissClickConsumedThisFrame). Advisory only — see that function's comment for the real limit.", EditCondition = "bOverrideDismissal", EditConditionHides))
    FInfoCardConfigurableBool DismissClickGetsConsumed = FInfoCardConfigurableBool(false);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dismissal", meta = (ToolTip = "If on, the card will ignore menu open/close events and persist throughout menus opening and closing. Useful for manually handling the card's lifetime.", EditCondition = "bOverrideDismissal", EditConditionHides))
    bool bIgnoresMenuStateChange = false;

    // ------------------------------------------------------------------
    // Sound
    // ------------------------------------------------------------------

    /**
     * Replaces the card's whole Sounds array rather than merging into it. Sound sets have no stable identity
     * to merge by - two sets on the same card are just two sets - so "override" here means "this anchor
     * supplies the sounds instead", including supplying none. Leave it off and the Data Asset's sounds play.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (EditCondition = "!bDisableCard"))
    bool bOverrideSounds = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (DisplayName = "Sounds", EditCondition = "bOverrideSounds", EditConditionHides, ToolTip = "Played instead of whatever the Data Asset authored for this card. An empty array silences the card on this anchor."))
    TArray<FInfoCardSoundSet> Sounds;

    // ------------------------------------------------------------------
    // Card Transform
    // ------------------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Card Transform", meta = (EditCondition = "!bDisableCard"))
    bool bOverrideCardTransform = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Card Transform", meta = (ToolTip = "Translation, scale, shear, and angle applied to the card widget itself, on top of wherever it was placed. Purely visual.", EditCondition = "bOverrideCardTransform", EditConditionHides))
    FWidgetTransform CardTransform;
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
 * One array element of AdditiveAnimationOverrides: which CardId (auto-synced,
 * not typed by hand) plus replacement Additive Animations for it on this anchor.
 *
 * While Override is off, Additive Animations just mirrors the Data Asset (greyed
 * out), so switching it on starts from what the card already does.
 */
USTRUCT(BlueprintType)
struct FInfoCardAdditiveAnimationsOverride
{
    GENERATED_BODY()

    /** Which InfoCards_DA entry this applies to. Kept in sync automatically; shown as the element's title, not as a row. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (DisplayName = "Card ID", EditCondition = "false"))
    FName CardId = NAME_None;

    /**
     * When on, Additive Animations below replace the Data Asset's for this card on this anchor. Still played in addition to
     * the card class's own Anim In / Anim Out, never instead of them. Turn this on and empty them to remove the Data Asset's
     * additive animations here.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (DisplayName = "Override"))
    bool bOverride = false;

    /** This card's additive animations on this anchor. Mirrors the Data Asset while Override is off. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (DisplayName = "Additive Animations", EditCondition = "bOverride"))
    FInfoCardAdditiveAnimations AdditiveAnimations;

    /** Pre-Use Basic Animations layout. Hidden; UInfoCardAnchor::PostLoad folds them into Additive Animations. */
    UPROPERTY()
    TArray<FWidgetAnimEntryBase> Animations;

    UPROPERTY()
    TArray<FInfoCardAdditiveAnimSettings> PlayIn;

    UPROPERTY()
    TArray<FInfoCardAdditiveAnimSettings> PlayOut;
};

/** Text set on a card by UInfoCardAnchor::SetCardText. Plain C++: runtime state, kept out of the Details panel and saves. */
struct FInfoCardTextOverride
{
    FText Title;
    FText Body;
};

/**
 * Fired directly off raw pointer input (mouse enter/leave, left/right
 * mouse-down) — BEFORE any InfoCards entry driven by that input has had a
 * chance to spawn or close its card. This ordering is deliberate: bind one
 * of these and call GetOrAddPayload / SetPayloadById / SetPayloadsById from
 * inside the handler to populate payload data, and by the time the matching
 * card actually builds — later in the same call, synchronously, for
 * zero-delay hover and for click — it already has the values you just set.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FInfoCardAnchorHoverSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FInfoCardAnchorClickSignature);

/**
 * Fired when a specific card actually appears on / is removed from screen,
 * carrying that entry's CardId. Unlike the input-level events above, these
 * report the outcome rather than the trigger: a hover with a delay fires
 * OnMouseHoverEnter immediately on mouse-enter, but only fires OnCardShown
 * once the delay elapses and the widget is actually up — and not at all if
 * the mouse left, or the entry was disabled/suppressed, before then.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FInfoCardAnchorCardIdSignature, FName, CardId);

/**
 * Internal only — never exposed to Blueprint. Distinguishes which input
 * raised a Highlight-type (ECardType::Hover) trigger, so TriggerHoverEntries
 * / UntriggerHoverEntries can respect each entry's
 * bTriggerOnMouseHover/bTriggerOnFocus independently rather than always
 * reacting to both.
 */
enum class EInfoCardHoverTriggerSource : uint8
{
    Mouse,
    Focus
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
 * Asset or affecting any other anchor that shares it, edit that payload in
 * PayloadDefaults: only the fields you change are applied. That array, like
 * SettingsOverrides and AdditiveAnimationOverrides, is maintained
 * automatically from the assigned Data Asset.
 */
UCLASS(meta = (DisplayName = "Info Card Anchor"))
class NAMETBD_API UInfoCardAnchor : public UBorder, public IWidgetAnimDesignerPreviewHost
{
    GENERATED_BODY()

public:

    UInfoCardAnchor();

    /**
     * Where this anchor's InfoCards come from. UseDataAsset (the default)
     * reads InfoCards_DA below, with PayloadDefaults / SettingsOverrides
     * layering this anchor's own per-instance tweaks on top of it — the
     * normal path for anything meant to be reusable across more than one
     * anchor.
     *
     * Custom hides all three of those (they have nothing to do while this
     * is active) and instead authors InfoCards entries directly here, in
     * Custom Info Cards below. Meant for a quick one-off card — a single
     * tooltip, say — that doesn't warrant creating (and keeping track of)
     * a whole Data Asset just for itself. Switching back to UseDataAsset
     * later doesn't migrate anything already typed into Custom Info Cards;
     * move it into a proper Data Asset by hand if it turns out to be worth
     * sharing after all.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (DisplayName = "Source"))
    EInfoCardSourceMode SourceMode = EInfoCardSourceMode::UseDataAsset;

    /**
     * Every card this anchor can show. Add one entry per independent
     * tooltip/context-card behaviour. Give an entry a CardId if you want to
     * address it from Blueprint by name instead of array index.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (TitleProperty = "CardId", DisplayName = "Info Cards", EditCondition = "SourceMode == EInfoCardSourceMode::UseDataAsset", EditConditionHides))
    TObjectPtr<UInfoCardDataAsset> InfoCards_DA;

    /**
     * This anchor's own values for the Data Asset's payloads, one entry per payload. Starts as a copy of the Data
     * Asset; only the fields you change are applied, and everything left as-is keeps following the Data Asset.
     *
     * Payloads cannot be added or removed in this array. It is kept in sync with the Data Asset automatically: add or
     * remove payloads on the Data Asset instead. If it doesn't pick a change up, clear the Data Asset and set it again.
     */
    UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "Info Card", meta = (TitleProperty = "CardId", DisplayName = "Payload Defaults", EditCondition = "SourceMode == EInfoCardSourceMode::UseDataAsset", EditConditionHides))
    TArray<FInfoCardPayloadDefault> PayloadDefaults;

    /**
     * This anchor's own trigger/placement/offset/tick overrides, one per
     * CardId. Kept in sync with InfoCards_DA the same way PayloadDefaults is
     * (auto add/prune by CardId; if a field is added to the data asset and
     * this doesn't pick it up, clear InfoCards_DA and set it again). Every
     * entry starts fully un-toggled, so nothing changes until you switch an
     * override on. See FInfoCardSettingsOverride for what each one does.
     */
    UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "Info Card", meta = (TitleProperty = "CardId", DisplayName = "Settings Overrides", EditCondition = "SourceMode == EInfoCardSourceMode::UseDataAsset", EditConditionHides))
    TArray<FInfoCardSettingsOverrideElement> SettingsOverrides;

    /**
     * Per-CardId replacement Additive Animations for this anchor, one entry per CardId, kept in sync with InfoCards_DA
     * the same way as the arrays above (entries cannot be added or removed here). Turn Override on for a card to
     * replace the Data Asset's Play In / Play Out with the ones below.
     */
    UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "Info Card", meta = (TitleProperty = "CardId", DisplayName = "Additive Animation Overrides", EditCondition = "SourceMode == EInfoCardSourceMode::UseDataAsset", EditConditionHides))
    TArray<FInfoCardAdditiveAnimationsOverride> AdditiveAnimationOverrides;

    // ------------------------------------------------------------------
    // Designer preview
    // ------------------------------------------------------------------

    /**
     * Which card the UMG designer shows next to this anchor. None: no card is shown.
     *
     * The REAL card: the entry's Card Class built in the designer, given this anchor's resolved payloads (so a tooltip shows
     * its text), its Card Transform, and placed where the game would put it - Icon Position and padding beside the anchor;
     * To Cursor treats the anchor's centre as the cursor; To Viewport shows it to the anchor's right. Clamping, flipping and
     * overlap avoidance only happen in game. Editor only; never shown in game.
     *
     * The Widget Blueprint editor's toolbar plays the card's entrance and exit (In / Out, with this anchor selected) - the
     * card class's Anim In / Anim Out plus this anchor's resolved additive animations - and can show another card without
     * changing this field.
     */
    UPROPERTY(EditAnywhere, Category = "Info Card|Designer Preview", meta = (DisplayName = "Preview Card", GetOptions = "GetInfoCardIdOptions"))
    FName DesignerPreviewCardId = NAME_None;

    /**
     * The preview harness's settings. Not in the Details panel: the toolbar's In / Out drive it on the designer's copy only.
     * A mode saved by an older version is switched back to Off on load.
     */
    UPROPERTY()
    FWidgetAnimPreviewSettings DesignerAnimPreview;

    /** Backs the Preview Card dropdown. Must stay a UFUNCTION for GetOptions. */
    UFUNCTION()
    TArray<FString> GetInfoCardIdOptions() const;

    /** Every Card ID this anchor has, in order (unnamed entries skipped). */
    TArray<FName> GetInfoCardIds() const;

    /** The card the designer shows: the toolbar's pick on this designer copy, else Preview Card. */
    FName GetDesignerPreviewCard() const;

    /**
     * Shows CardId in the designer on this copy only (the toolbar's card picker). Nothing authored changes. None hides the
     * card; NAME_None after a pick goes back to Preview Card via ClearDesignerPreviewCardPick.
     */
    void SetDesignerPreviewCardPick(FName CardId);

    /** Drops the toolbar's pick, so Preview Card decides again. */
    void ClearDesignerPreviewCardPick();

    /** Whether the toolbar has picked a card on this copy. */
    bool HasDesignerPreviewCardPick() const { return bHasDesignerPreviewCardPick; }

    /** Whether the previewed card has any entrance or exit to play. */
    bool HasDesignerPreviewAnimations() const { return PreviewAnimIn.Num() > 0 || PreviewAnimOut.Num() > 0; }

    /** Plays the previewed card's entrance (or exit) once on the designer's copy - what the toolbar's In / Out call. */
    void PlayDesignerCardAnimation(bool bOut);

    /** The card widget built for the designer, or null. Editor only in practice. */
    UInfoCardWidget* GetDesignerPreviewCardWidget() const { return DesignerPreviewCard; }

    // IWidgetAnimDesignerPreviewHost
    virtual UWidget* GetPreviewOwnerWidget() override { return this; }

    /**
     * The card shown in the designer when there is one: the animation plays on the real card. Without one (no Card Class,
     * or the anchor is a template), the anchor's content stands in, and failing that the anchor itself.
     */
    virtual UWidget* GetPreviewAnimationTarget() override;

    virtual FWidgetAnimPreviewSettings& GetPreviewSettings() override { return DesignerAnimPreview; }
    virtual FWidgetAnimPreviewState& GetPreviewState() override { return DesignerAnimPreviewState; }
    virtual FWidgetAnimBaselineSet& GetPreviewBaselines() override { return DesignerAnimBaselines; }
    virtual const TArray<FWidgetAnimSettings>& GetPreviewAnimIn() const override { return PreviewAnimIn; }
    virtual const TArray<FWidgetAnimSettings>& GetPreviewAnimOut() const override { return PreviewAnimOut; }
    virtual void RebuildPreviewAnimations() override;

    /**
     * InfoCards, authored directly on this anchor, used in place of
     * InfoCards_DA while SourceMode is Custom. Same FInfoCardSpec shape a
     * Data Asset's own InfoCards array holds — CardId, trigger, placement,
     * payloads, everything — just typed here instead of on a separate,
     * shareable asset.
     *
     * Ignored entirely (and hidden) while SourceMode is UseDataAsset, and
     * has no PayloadDefaults/SettingsOverrides equivalent — there's nothing
     * to layer on top of, since this anchor already owns these entries
     * outright.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (TitleProperty = "CardId", DisplayName = "Custom Info Cards", EditCondition = "SourceMode == EInfoCardSourceMode::Custom", EditConditionHides))
    TArray<FInfoCardSpec> CustomInfoCards;

    /**
     * How this anchor's keyboard/gamepad focus support is driven. Automatic
     * (the default) works with standard UMG/Slate focus and most navigation
     * frameworks built on it, with no extra setup. Switch to Manual if your
     * project's menu navigation tracks focus itself rather than through
     * Slate's native focus system, and drive Highlight-type cards from it
     * via NotifyFocusReceived/NotifyFocusLost instead. Disabled means focus
     * never triggers a card on this anchor at all — useful for a
     * mouse/click-only anchor. See EInfoCardFocusTriggerMode.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "Automatic: standard Slate focus drives this anchor for free. Manual: call NotifyFocusReceived/NotifyFocusLost yourself. Disabled: focus never triggers a card here."))
    EInfoCardFocusTriggerMode FocusTriggerMode = EInfoCardFocusTriggerMode::Automatic;

    // ------------------------------------------------------------------
    // Events
    // ------------------------------------------------------------------

    /**
     * Raw mouse-enter on this anchor. Fires before any Hover-type entry in
     * InfoCards spawns (zero delay) or starts its delay timer — safe to set
     * payloads from a bound handler here.
     */
    UPROPERTY(BlueprintAssignable, Category = "Info Card|Events")
    FInfoCardAnchorHoverSignature OnMouseHoverEnter;

    /** Raw mouse-leave on this anchor. Fires before any Hover-type entry in InfoCards is hidden. */
    UPROPERTY(BlueprintAssignable, Category = "Info Card|Events")
    FInfoCardAnchorHoverSignature OnMouseHoverExit;

    /**
     * Keyboard/gamepad counterpart to OnMouseHoverEnter: this anchor or a widget
     * nested inside it (e.g. a wrapped Button) just entered the Slate focus
     * path. Fires before any Hover-type entry in InfoCards spawns or starts
     * its delay timer — same "safe to set payloads here" guarantee as
     * OnMouseHoverEnter, since focus and mouse hover drive the exact same
     * Hover-type entries through the exact same trigger logic.
     */
    UPROPERTY(BlueprintAssignable, Category = "Info Card|Events")
    FInfoCardAnchorHoverSignature OnFocusReceived;

    /** Keyboard/gamepad counterpart to OnMouseHoverExit: this anchor or a nested widget just left the Slate focus path. Fires before any Hover-type entry in InfoCards is hidden. */
    UPROPERTY(BlueprintAssignable, Category = "Info Card|Events")
    FInfoCardAnchorHoverSignature OnFocusLost;

    /**
     * Raw left mouse-down on this anchor. Fires before any LeftClick-type
     * entry in InfoCards toggles or spawns — safe to set payloads from a
     * bound handler here.
     */
    UPROPERTY(BlueprintAssignable, Category = "Info Card|Events")
    FInfoCardAnchorClickSignature OnLeftClickPressed;

    /**
     * Raw right mouse-down on this anchor. Fires before any RightClick-type
     * entry in InfoCards toggles or spawns — safe to set payloads from a
     * bound handler here.
     */
    UPROPERTY(BlueprintAssignable, Category = "Info Card|Events")
    FInfoCardAnchorClickSignature OnRightClickPressed;

    /**
     * A card actually appeared on screen. Fires once per successful spawn,
     * after the widget has been created, set up with its final payloads,
     * added to the viewport, and positioned.
     */
    UPROPERTY(BlueprintAssignable, Category = "Info Card|Events")
    FInfoCardAnchorCardIdSignature OnCardShown;

    /**
     * A card that was on screen has been closed. Fires once per actual
     * removal — never for a Hide call on an entry that wasn't showing.
     */
    UPROPERTY(BlueprintAssignable, Category = "Info Card|Events")
    FInfoCardAnchorCardIdSignature OnCardHidden;

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
    void SetPayloadsAtIndex(int32 Index, const TArray<UNameTBD_Payload*>& NewPayloads);

    /** Convenience wrapper around SetPayloadsAtIndex for the common single-payload case. */
    UFUNCTION(BlueprintCallable, Category = "Info Card")
    void SetPayloadAtIndex(int32 Index, UNameTBD_Payload* NewPayload);

    /** The card currently on screen at Index, or null. */
    UFUNCTION(BlueprintPure, Category = "Info Card")
    UInfoCardWidget* GetActiveCardAtIndex(int32 Index) const;

    // ------------------------------------------------------------------
    // By CardId
    // ------------------------------------------------------------------

    /** Index of the InfoCards entry whose CardId matches, or INDEX_NONE. */
    UFUNCTION(BlueprintPure, Category = "Info Card")
    int32 FindCardIndexById(UPARAM(DisplayName = "Card ID") FName CardId) const;

    UFUNCTION(BlueprintCallable, Category = "Info Card")
    void ShowCardById(UPARAM(DisplayName = "Card ID") FName CardId);

    UFUNCTION(BlueprintCallable, Category = "Info Card")
    void HideCardById(UPARAM(DisplayName = "Card ID") FName CardId);

    UFUNCTION(BlueprintCallable, Category = "Info Card")
    void ToggleCardById(UPARAM(DisplayName = "Card ID") FName CardId);


    /**
     * @param bRevealCard Will do nothing is Enabled is set to alse 
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card", meta = (Keywords = "Disable, Deactivate, Off, Turn"))
    void SetCardEnabledById(UPARAM(DisplayName = "Card ID") FName CardId, bool bEnabled, bool bRevealCard);

    UFUNCTION(BlueprintCallable, Category = "Info Card")
    void SetPayloadsById(UPARAM(DisplayName = "Card ID") FName CardId, const TArray<UNameTBD_Payload*>& NewPayloads);

    UFUNCTION(BlueprintCallable, Category = "Info Card")
    void SetPayloadById(UPARAM(DisplayName = "Card ID") FName CardId, UNameTBD_Payload* NewPayload);

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
    UNameTBD_Payload* GetOrAddPayload(UPARAM(DisplayName = "Card ID") FName CardId, 
                              TSubclassOf<UNameTBD_Payload> PayloadClass,
                              bool& bIsPayloadValid,
                              UPARAM(DisplayName = "Card ID") FName& InputCardID);

    /**
     * Re-delivers CardId's current payload set to its active card, if one is
     * showing (a no-op otherwise). Use after mutating a payload object
     * returned by GetOrAddPayload when the card is already on screen and you
     * want the change to appear without waiting for it to re-open.
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card")
    void RefreshCard(UPARAM(DisplayName = "Card ID") FName CardId);

    UFUNCTION(BlueprintPure, Category = "Info Card")
    UInfoCardWidget* GetActiveCardById(UPARAM(DisplayName = "Card ID") FName CardId) const;

    /**
     * The quick way to give a card its words: sets the Title / Body of CardId's text payload (Info Card Data (Text)), adding
     * one if the card has none, and updates the card if it is showing. Kept across the anchor re-reading its Data Asset, and
     * shown by the designer's card too - so a Widget Blueprint that wraps an anchor can expose a "Tooltip Text" variable and
     * call this from Pre Construct, and every placement of it is a one-field tooltip. An empty Title hides the title line of
     * the built-in Text Info Card.
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card", meta = (Keywords = "Tooltip Title Body Description"))
    void SetCardText(UPARAM(DisplayName = "Card ID") FName CardId, FText Title, FText Body);

    /** Forgets text set by Set Card Text for CardId; the authored payload is used again from the next time the cards are re-read. */
    UFUNCTION(BlueprintCallable, Category = "Info Card")
    void ClearCardText(UPARAM(DisplayName = "Card ID") FName CardId);

    /**
     * One Data Asset entry as this anchor uses it: payloads duplicated (outered to PayloadOuter, so no two anchors share
     * one), and the asset's Default Sounds / Default Additive Animations filled in where the entry has none of its own.
     * Shared with subclasses that take a template entry from a Data Asset (Tooltip Card Anchor).
     */
    static FInfoCardSpec MakeSpecFromDataAsset(const UInfoCardDataAsset& Asset, const FInfoCardSpec& SourceSpec, UObject* PayloadOuter);

    // ------------------------------------------------------------------
    // Pinning
    // ------------------------------------------------------------------
    //
    // A pinned card ignores everything that would normally close it on its
    // own: the cursor leaving (mouse-leave, or Keep Open Until Cursor Leaves'
    // grace period), focus being lost, Remove Card On Pressing, re-clicking
    // its Right/Left Click anchor, Auto Dismiss Duration, menu state changes,
    // and other anchors' Close Other Cards Scope. It stays up until something
    // deliberately closes it.
    //
    // A pin never blocks an explicit request: CloseInfoCard, HideCardById,
    // HideCardAtIndex, ToggleCardById/AtIndex, HideAllCards,
    // NotifyHoldCanceled, SetCardEnabled...(false), a suppression tag,
    // SetPayloads... with an empty set, a UContextMenuButton inside the card
    // closing it, or CloseCard called on the card itself. A pinned card also
    // still closes when there is nothing left for it to stand on: its anchor
    // being destroyed (which includes its parent card closing, for a card
    // nested inside another card, so pin the parent too if the whole chain
    // should stay), or its InventoryDisplay's source component changing.
    //
    // The pin belongs to the live card, not to the InfoCards entry. Once the
    // card closes, the next card that entry spawns starts unpinned.

    /**
     * Pins CardId's card open. See the Pinning section above for exactly what
     * that does and doesn't block. If the card isn't showing yet, it's shown
     * first (ignoring Highlight Delay, the same as ShowCardById) and then
     * pinned.
     *
     * @return True if CardId's card is showing and pinned once this returns.
     *         False if CardId doesn't match an entry, or its card couldn't be
     *         shown (disabled, suppressed, no CardClass, no owning player).
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card|Pin")
    bool PinInfoCard(UPARAM(DisplayName = "Card ID") FName CardId);

    /**
     * Releases a pin set by PinInfoCard (or by the card's own SetPinned). The
     * card goes back to closing the normal way from here on. Nothing closes
     * as a direct result of unpinning, with one exception: a Highlight card
     * whose trigger already went away while it was pinned (cursor off the
     * anchor, focus not on it) closes now, since the mouse-leave/focus-lost
     * that would have closed it already happened and was ignored. A Highlight
     * card with Keep Open Until Cursor Leaves is instead left to its own
     * cursor check, with a fresh grace period, since the cursor may be
     * sitting on the card itself. A card with an Auto Dismiss Duration starts
     * that countdown again from the full duration. No-op if the card isn't
     * showing.
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card|Pin")
    void UnpinInfoCard(UPARAM(DisplayName = "Card ID") FName CardId);

    /** True if CardId's card is currently showing and pinned. */
    UFUNCTION(BlueprintPure, Category = "Info Card|Pin")
    bool IsInfoCardPinned(UPARAM(DisplayName = "Card ID") FName CardId) const;

    /**
     * Closes CardId's card if it's showing, pinned or not, and cancels any
     * pending Highlight Delay for it. The explicit counterpart to PinInfoCard.
     * Behaves exactly like HideCardById (which, like every explicit close,
     * also closes pinned cards); this just gives the pin/close pair names
     * that read together in a graph.
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card|Pin")
    void CloseInfoCard(UPARAM(DisplayName = "Card ID") FName CardId);

    // ------------------------------------------------------------------
    // Hold-to-interact
    // ------------------------------------------------------------------
    //
    // These are deliberately thin: a hold gesture (its duration, whether it's
    // button-driven or Enhanced-Input-Trigger-driven, etc.) is owned entirely
    // by whatever menu/interact system detects it, never by InfoCardAnchor.
    // These functions are just readably-named reactions to that system's
    // events, built on the same Show/HideCardById primitives used everywhere
    // else in this class.

    /**
     * For a simple "hold for X seconds, then a card appears" case with no
     * progress readout: call once when the hold finishes. Equivalent to
     * ShowCardById(CardId) — the wrapper exists purely so the calling
     * Blueprint's graph reads as "the hold completed" instead of an
     * unrelated-looking Show call.
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card|Hold")
    void NotifyHoldCompleted(UPARAM(DisplayName = "Card ID") FName CardId);

    /**
     * Companion to NotifyHoldCompleted/NotifyHoldProgress: call when a hold
     * is released or interrupted before finishing. Equivalent to
     * HideCardById(CardId) — closes whichever card CardId refers to,
     * whether that's a NotifyHoldCompleted result card or a
     * NotifyHoldProgress bar.
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card|Hold")
    void NotifyHoldCanceled(UPARAM(DisplayName = "Card ID") FName CardId);

    /**
     * Call every tick while a hold is building toward CardId. Shows the
     * card on the first call (whatever Progress happens to be) and, on
     * every call after, pushes Progress into that entry's
     * UInfoCardPayload_HoldProgress payload and re-delivers payloads to the
     * live card via RefreshCard. No-op — logged once as a warning — if this
     * CardId's entry has no such payload authored. On success or
     * cancellation, close it the same way as any other card:
     * HideCardById(CardId), e.g. via NotifyHoldCanceled.
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card|Hold")
    void NotifyHoldProgress(UPARAM(DisplayName = "Card ID") FName CardId, float Progress);

    // ------------------------------------------------------------------
    // Focus (Manual FocusTriggerMode)
    // ------------------------------------------------------------------
    //
    // Only meaningful when FocusTriggerMode is Manual (or Automatic, if you
    // want to layer a manual call on top of the native one for some reason)
    // — a no-op when FocusTriggerMode is Disabled. Unlike the Hold
    // functions above, these aren't CardId-addressed: like mouse hover,
    // "focus" is one state for the whole anchor, and it drives every
    // Highlight-type entry whose bTriggerOnFocus is on, exactly the way the
    // automatic Slate-focus path does.

    /**
     * Call from your own navigation system when it moves focus onto this
     * anchor or something inside it. No-op if FocusTriggerMode is Disabled.
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card|Focus")
    void NotifyFocusReceived();

    /**
     * Call from your own navigation system when focus leaves this anchor or
     * something inside it. No-op if FocusTriggerMode is Disabled.
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card|Focus")
    void NotifyFocusLost();

    // ------------------------------------------------------------------
    // All cards
    // ------------------------------------------------------------------

    /** Closes every active card on this anchor and cancels every pending hover timer. */
    UFUNCTION(BlueprintCallable, Category = "Info Card")
    void HideAllCards();

    /**
     * True if ANY InfoCardAnchor, anywhere in the process, currently has at
     * least one card on screen. Static/global rather than per-anchor because
     * the usual reason to ask is to gate something unrelated to any one
     * anchor — suppressing a hotkey, pausing camera rotation, blocking a
     * world click — for as long as the player has some card open, whichever
     * anchor it belongs to. Backed by the same registry ApplyCloseOthersScope
     * uses, so it costs nothing beyond what's already tracked.
     */
    UFUNCTION(BlueprintPure, Category = "Info Card")
    static bool IsAnyCardShowing();

    /**
     * True if some card's dismissing click, ANYWHERE in the process, marked
     * THIS frame as consumed for info-card purposes — see
     * FInfoCardSpec::DismissClickGetsConsumed, which gates whether a given
     * card's dismiss click calls MarkDismissClickConsumed at all.
     *
     * Meant to be checked by your OWN world/gameplay click-handling code
     * (a character controller's click-to-move, a click-to-interact system,
     * anything reading raw mouse input this same frame) before reacting to
     * a click — if this is true, an info card's own dismissal already
     * "claimed" it.
     *
     * IMPORTANT ARCHITECTURAL LIMIT: this is advisory, not genuine Slate
     * input capture. Cards see presses through a Slate input pre-processor
     * (see Uinfocardwidget's FInfoCardPressWatcher), which observes every
     * press before Slate routes it but deliberately consumes none of them —
     * a tooltip that swallowed input would break every button underneath it.
     * The dismissal itself is then applied on the card's next tick, after
     * Slate has already dispatched that press to whatever it was going to
     * dispatch it to. So this flag can't retroactively undo a synchronous
     * OnMouseButtonDown / click-to-move handler that already reacted to the
     * raw event. What it CAN do: anything that checks this flag before
     * reacting — later in the same frame, or on a delay — correctly sees
     * "this press was claimed" and can choose to skip.
     */
    UFUNCTION(BlueprintPure, Category = "Info Card")
    static bool WasDismissClickConsumedThisFrame();

    /**
     * Called by Uinfocardwidget::TickClickOutsideToClose when a card's
     * dismiss click resolves DismissClickGetsConsumed to true. Stamps the
     * current GFrameCounter; WasDismissClickConsumedThisFrame just compares
     * against it, so this needs no matching "clear" call — it goes stale on
     * its own the moment the frame counter advances.
     *
     * Deliberately plain C++, not a UFUNCTION: this is the internal-facing
     * setter, matching NotifyCardSelfClosed/RetriggerHoverCardById's own
     * "card talks to anchor" pattern — WasDismissClickConsumedThisFrame
     * above is the public-facing read side.
     */
    static void MarkDismissClickConsumed();

    /**
     * The UserWidget instance that contains this anchor in its widget tree
     * (e.g. the specific W_InventorySlot this anchor was placed in). This is
     * what CloseOtherCardsScope::SameSourceWidgetClass compares by class against
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

    /**
     * Called by Uinfocardwidget::CloseCard — every time, regardless of
     * what triggered the close — so this anchor's own bookkeeping never
     * goes stale when a card closes itself rather than being closed via
     * HideCardAtIndex/HideCardById (click-outside, an InventoryDisplay's
     * source changing, or bKeepOpenUntilCursorLeaves's own cursor check).
     * Finds whichever RuntimeStates slot has ClosedCard as its ActiveCard,
     * clears it, and fires OnCardHidden — the same cleanup HideCardAtIndex
     * does, just reached from the opposite direction. No-op if ClosedCard
     * isn't currently tracked as active on this anchor (already cleared,
     * or belongs to a different anchor entirely).
     *
     * Deliberately plain C++, not a UFUNCTION: this is an internal
     * handshake between a card and its anchor, not something a plugin user
     * should ever need to call directly.
     */
    void NotifyCardSelfClosed(UInfoCardWidget* ClosedCard);

    /**
     * Called by a card's own TickClickOutsideToClose right after it closes
     * itself because a dismissing click landed while the cursor never
     * actually left this anchor — see FInfoCardSpec::bReappearOnClickDismiss,
     * which gates whether the card asks for this at all.
     *
     * Re-runs the matching entry's normal hover-trigger logic (HoverDelay,
     * Successive Hover Window, bEnabled, suppression — everything
     * TriggerHoverEntries itself would apply) as a stand-in for the
     * MouseEnter that won't be coming, since the cursor never left in the
     * first place. No-op if CardId doesn't resolve to a Hover-type entry on
     * this anchor, or if that entry doesn't allow mouse-hover triggers
     * (bTriggerOnMouseHover off) — this speaks for the mouse, not a generic
     * "show it again."
     *
     * Deliberately plain C++, not a UFUNCTION: same internal-handshake
     * reasoning as NotifyCardSelfClosed.
     */
    void RetriggerHoverCardById(FName CardId);

    /**
     * Called by Uinfocardwidget::SetPinned every time a card is unpinned,
     * whoever unpinned it (UnpinInfoCard, or Blueprint calling SetPinned on
     * the card directly). If the card is a Highlight entry whose trigger went
     * away while it was pinned, closes it now. See UnpinInfoCard for the full
     * rules.
     *
     * Deliberately plain C++, not a UFUNCTION: same internal card-to-anchor
     * handshake as NotifyCardSelfClosed.
     */
    void NotifyCardUnpinned(UInfoCardWidget* UnpinnedCard);

protected:

    // These four override public virtuals declared on UWidget/UObject
    // (ReleaseSlateResources, BeginDestroy, GetPaletteCategory,
    // PostEditChangeProperty), so the engine and editor still call them
    // correctly via virtual dispatch through a base-class pointer — that's
    // how every real call site reaches them. Declaring the override itself
    // as protected here just keeps them out of this class's own public API,
    // where they'd otherwise look like something a plugin user is meant to
    // call directly on a UInfoCardAnchor*.
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
    virtual void BeginDestroy() override;

    /** Converts PayloadDefaults saved in the old nested layout (Set Defaults + per-card slot arrays) into flat entries. */
    virtual void PostLoad() override;

#if WITH_EDITOR
    virtual const FText GetPaletteCategory() override;

    /** Auto-runs the three Sync...FromDataAsset functions when InfoCards_DA is (re)assigned in the Details panel. */
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    virtual TSharedRef<SWidget> RebuildWidget() override;

    /**
     * SynchronizeProperties can run before the Slate widget exists, and the designer preview's active
     * timer needs one to register against. By here it definitely does.
     */
    virtual void OnWidgetRebuilt() override;

    // NOTE: mouse handlers are (re)bound here, not in RebuildWidget. UBorder
    // installs its own OnMouseButtonDown handler on MyBorder as part of
    // RebuildWidget, so binding ours there gets overwritten in the same
    // breath. SynchronizeProperties runs after RebuildWidget, so binding
    // here is what makes right-click actually reach us.
    virtual void SynchronizeProperties() override;

    /**
     * For anchors that build their cards from fields of their own (Tooltip Card Anchor, Context Menu Anchor): fill OutSpecs
     * and return true, and InfoCards comes from these instead of Source / Info Cards / Custom Info Cards (which such a
     * subclass hides). Payloads made here should be outered to this anchor. Called on every design-time synchronize and once
     * at runtime; call RebuildGeneratedInfoCards after changing the fields at runtime.
     */
    virtual bool BuildGeneratedInfoCards(TArray<FInfoCardSpec>& OutSpecs) { return false; }

    /** Whether BuildGeneratedInfoCards supplies this anchor's cards. Must agree with it. */
    virtual bool UsesGeneratedInfoCards() const { return false; }

    /**
     * Rebuilds InfoCards from BuildGeneratedInfoCards and re-delivers payloads to any card showing (one whose entry is gone
     * or disabled closes). For a subclass's runtime setters.
     */
    void RebuildGeneratedInfoCards();

    /**
     * Whether a repeat of the click that opened entry Index, landing on the anchor while its card is up, closes it (false,
     * the default: a toggle) or opens it again at the new spot (true: a context menu, which follows the latest right-click).
     */
    virtual bool ShouldReopenOnRepeatClick(int32 Index) const { return false; }

    /** Builds, refreshes or removes the card shown in the designer (see Preview Card). Design time only. */
    void RefreshDesignerPreviewCard();

    void HandleMouseEnter(const FGeometry& Geometry, const FPointerEvent& PointerEvent);
    void HandleMouseLeave(const FPointerEvent& PointerEvent);
    FReply HandleMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& PointerEvent);
    void HandleHoverDelayElapsedForIndex(int32 Index);

    /**
     * Cancels RuntimeStates[Index]'s pending Highlight Delay, if any. The one
     * place that removes a HoverDelayHandle from the core ticker, so every
     * close/cleanup path cancels delays the same way. No-op for an invalid
     * Index or a slot with nothing pending.
     */
    void ClearHoverDelayAtIndex(int32 Index);

    /**
     * The card class's Anim In / Anim Out flattened together with this anchor's resolved additive
     * animations: what UWidgetAnimDesignerPreview plays. Rebuilt by RebuildPreviewAnimations on every
     * design-time synchronize, so editing an override is reflected immediately. Editor-only in practice,
     * but not compiled out, because the interface accessors that return them are not.
     */
    UPROPERTY(Transient, DuplicateTransient)
    TArray<FWidgetAnimSettings> PreviewAnimIn;

    UPROPERTY(Transient, DuplicateTransient)
    TArray<FWidgetAnimSettings> PreviewAnimOut;

    UPROPERTY(Transient, DuplicateTransient)
    FWidgetAnimPreviewState DesignerAnimPreviewState;

    /** What the previewed content looked like before the preview touched it, so Off can put it back. */
    UPROPERTY(Transient, DuplicateTransient)
    FWidgetAnimBaselineSet DesignerAnimBaselines;

    /** The card built for the designer (see Preview Card). Design time only. */
    UPROPERTY(Transient, DuplicateTransient)
    TObjectPtr<UInfoCardWidget> DesignerPreviewCard;

    /** What RebuildWidget hands the designer: the border plus the previewed card. Design time only. */
    TSharedPtr<SInfoCardDesignerPreview> DesignerPreviewPanel;

    /** The toolbar's card pick on this designer copy (see SetDesignerPreviewCardPick). Never saved. */
    FName DesignerPreviewCardPick = NAME_None;
    bool bHasDesignerPreviewCardPick = false;

    /**
     * Shows every ECardType::AlwaysOn entry that's currently eligible
     * (bEnabled, not suppressed — same guards ShowCardAtIndex always
     * applies). Called once from SynchronizeProperties, right after
     * InfoCards is first populated at runtime — an AlwaysOn entry has no
     * hover/focus/click to wait for, so nothing else would ever call
     * ShowCardAtIndex for it otherwise. Safe to call again later (e.g. if a
     * caller wants to re-arm AlwaysOn entries after suppression tags
     * change) since ShowCardAtIndex already no-ops on an entry that's
     * disabled, suppressed, or already showing.
     */
    void ShowAlwaysOnCards();

    /**
     * Schedules ShowAlwaysOnCards for the START OF THE NEXT TICK rather than
     * running it inline. Everything that arms Always On entries goes through
     * here; nothing calls ShowAlwaysOnCards directly.
     *
     * The deferral is the whole point. The natural moment to arm these is
     * SynchronizeProperties, which is the first time InfoCards has content —
     * but SynchronizeProperties runs from inside UWidget::TakeWidget, i.e.
     * partway through Slate constructing this anchor's own widget hierarchy.
     * Spawning a card there means calling AddToViewport (which builds a whole
     * second widget tree, and runs its NativeConstruct, and can close other
     * cards through CloseOtherCardsScope) re-entrantly inside a construction
     * pass that hasn't finished. For an anchor sitting inside another card —
     * a dropdown inside a context menu — that construction pass belongs to
     * the containing card, so the nested Always On entry tears at a widget
     * tree that is still being assembled. It also arrives before this
     * anchor's geometry exists, so a ToIcon card would place itself against a
     * zero-size rect at the origin.
     *
     * One tick later, all of that is settled: the host widget is built and
     * laid out, the anchor has real geometry, and spawning a card is an
     * ordinary top-level operation again. Scheduled on the core ticker rather
     * than the world timer manager so it still runs while the game is paused.
     * If there's no world yet to show against, bAlwaysOnShowPending stays set
     * and the next SynchronizeProperties retries.
     */
    void RequestShowAlwaysOnCards();

    /**
     * Keyboard/gamepad counterpart to HandleMouseEnter/HandleMouseLeave.
     * Bound to FSlateApplication::OnFocusChanging (see BindSlateFocusChanging)
     * rather than any UWidget-level virtual — plain UWidget subclasses like
     * this one (unlike UUserWidget) aren't generically notified about focus
     * by the engine, so this anchor has to ask Slate itself. Fires whenever
     * this anchor — or any focusable widget nested inside it, e.g. a
     * wrapped Button a menu's navigation system moved focus to — enters or
     * leaves the focus path, determined by walking up from the
     * newly/previously focused widget via SWidget::GetParentWidget().
     *
     * No-op unless FocusTriggerMode is Automatic — see NotifyFocusReceived
     * / NotifyFocusLost for the Manual equivalent.
     */
    void HandleSlateFocusChanging(const FFocusEvent& FocusEvent,
       const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget,
       const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget);

    /** Subscribes HandleSlateFocusChanging to FSlateApplication's OnFocusChanging, replacing any previous subscription. Called from RebuildWidget, since the underlying SWidget (and therefore what GetCachedWidget() returns) is only valid after that. */
    void BindSlateFocusChanging();

    /** Unsubscribes HandleSlateFocusChanging, if currently bound. Called from ReleaseSlateResources and BeginDestroy so a destroyed/rebuilt anchor never leaves a stale subscription registered against FSlateApplication. */
    void UnbindSlateFocusChanging();

    /**
     * Shared by HandleMouseEnter, HandleSlateFocusChanging, and
     * NotifyFocusReceived: shows every Hover-type ("Highlight" in the
     * editor) InfoCards entry that allows this Source (see
     * FInfoCardSpec::bTriggerOnMouseHover / bTriggerOnFocus) immediately
     * (resolved HoverDelay <= 0, or the successive-hover window is active —
     * see ShouldSkipHoverDelay) or starts its (pause-safe) delay. This is what lets
     * one Highlight-type entry serve mouse hover, focus, or both, without
     * being authored twice.
     *
     * Just a loop over TriggerHoverEntryAtIndex — see that function for the
     * actual per-entry logic, factored out so RetriggerHoverCardById can run
     * it for a single entry without a real MouseEnter having happened.
     */
    void TriggerHoverEntries(EInfoCardHoverTriggerSource Source);

    /**
     * Per-entry body of TriggerHoverEntries: does this ONE entry qualify to
     * show right now (bEnabled, CardType, whether it allows Source), and if
     * so either spawns it immediately or arms its HoverDelayHandle delay on
     * the core ticker (which, unlike the world timer manager, keeps running
     * while the game is paused).
     * Safe to call directly for a single Index outside of TriggerHoverEntries'
     * own loop — used that way by RetriggerHoverCardById, which needs this
     * exact logic for one entry without a real MouseEnter event to iterate
     * the whole array from.
     */
    void TriggerHoverEntryAtIndex(int32 Index, EInfoCardHoverTriggerSource Source);

    /**
     * Plays one entry's Sounds for an event, from the anchor rather than from the card. Only really needed
     * for Triggered, which fires before the card widget exists; Shown and Hidden are played by the card
     * itself in UInfoCardWidget. Exposed anyway so a project driving cards manually can fire any of them.
     */
    UFUNCTION(BlueprintCallable, Category = "Info Card|Sound")
    void PlayCardSound(int32 Index, EInfoCardSoundEvent Event);

    UFUNCTION(BlueprintCallable, Category = "Info Card|Sound", meta = (DisplayName = "Play Card Sound By Id"))
    void PlayCardSoundById(FName CardId, EInfoCardSoundEvent Event);

    /** Shared by HandleMouseLeave, HandleSlateFocusChanging, and NotifyFocusLost: hides every Hover-type InfoCards entry that allows this Source and cancels its pending delay timer, if any. */
    void UntriggerHoverEntries(EInfoCardHoverTriggerSource Source);

    /**
     * True if UInfoCardDeveloperSettings::bEnableSuccessiveHover is on and
     * some card, anywhere, closed within the last SuccessiveWindowSeconds —
     * the "Windows tooltip" behaviour where hovering a second control
     * shortly after dismissing the first one's tooltip shows the new one
     * instantly rather than making the player wait out the delay again.
     * SuccessiveWindowSeconds comes from the entry actually being triggered
     * (its own resolved FInfoCardSpec::SuccessiveHoverWindow) rather than a
     * single fixed project value, so different entries can tune how
     * "successive" has to feel.
     */
    static bool ShouldSkipHoverDelay(float SuccessiveWindowSeconds);

    /**
     * Stamps LastCardClosedRealTimeSeconds with the current time — called
     * from every place a card actually transitions to closed (HideCardAtIndex,
     * NotifyCardSelfClosed, and the forced closes in EnsureRuntimeStatesSize),
     * so ShouldSkipHoverDelay always has an accurate "how long ago" to check
     * regardless of which of those paths did the closing.
     */
    static void RecordCardClosed();

    /**
     * Applies InfoCards[SourceIndex].CloseOtherCardsScope: closes other
     * anchors' cards (never this anchor's own other entries — those are
     * meant to coexist).
     */
    void ApplyCloseOthersScope(int32 SourceIndex) const;

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
     * True if RuntimeStates[Index].ActiveCard is a genuinely live, still-open
     * card — the single source of truth every entry point (ShowCardAtIndex,
     * the click-toggle in HandleMouseButtonDown, UntriggerHoverEntries)
     * should ask instead of reading RuntimeStates[Index].ActiveCard directly.
     *
     * A card that's already closing (Uinfocardwidget::IsClosing) or has
     * gone invalid counts as NOT active even though the pointer itself is
     * still non-null. That gap matters: a card closing itself — e.g.
     * TickClickOutsideToClose's click-outside path — reports the close to
     * this anchor via NotifyCardSelfClosed, but CloseCard's own
     * already-closing guard means only the FIRST close attempt on a given
     * card ever reaches that notify. If some other close (a mouse-leave
     * HideCardAtIndex, an inventory refresh, anything else that can also
     * ask this same card to close) loses that race, NotifyCardSelfClosed
     * never runs for it, and RuntimeStates[Index].ActiveCard is left
     * pointing at a card that is, for every practical purpose, already
     * gone.
     *
     * HideCardAtIndex tolerates this fine — it re-checks after calling
     * CloseCard and cleans up itself if the notify didn't land. This
     * function gives ShowCardAtIndex (which has no such fallback, and so
     * would otherwise refuse to ever spawn again) and the click toggle
     * (which would otherwise need an extra press before HideCardAtIndex's
     * fallback finally clears the stale slot) the same resilience: the
     * slot is cleared — the same cleanup NotifyCardSelfClosed would have
     * done — right here, the first time anything asks.
     */
    bool IsSlotActive(int32 Index);

    /**
     * True if RuntimeStates[Index] holds a live, not-closing card that is
     * pinned (see PinInfoCard). Every AUTOMATIC close path on this anchor asks
     * this before closing anything: UntriggerHoverEntries, the click toggle in
     * HandleMouseButtonDown, and HideUnpinnedCards (what other anchors'
     * CloseOtherCardsScope calls). Explicit close calls deliberately don't.
     *
     * Const, unlike IsSlotActive, so it does no stale-slot cleanup. Callers
     * that need a trustworthy "is this slot up at all" answer ask IsSlotActive
     * first.
     */
    bool IsSlotPinned(int32 Index) const;

    /**
     * HideAllCards, skipping pinned cards. This is what ApplyCloseOthersScope
     * calls on OTHER anchors: a Close Other Cards Scope is an automatic "make
     * room" close, not an explicit request, so it must not take a pinned card
     * down with it. HideAllCards itself stays unconditional; it's what
     * ReleaseSlateResources uses, and a card whose anchor is going away can't
     * stay up regardless of any pin.
     */
    void HideUnpinnedCards();

    /**
     * Rebuilds InfoCards from InfoCards_DA: duplicates every payload (so this
     * anchor owns its own instances rather than sharing the Data Asset's),
     * then calls ApplyPayloadDefaults so PayloadDefaults is layered on top.
     * The single place both SynchronizeProperties and the lazy-sync branch of
     * GetOrAddPayload funnel through, so defaults apply consistently no
     * matter which path populated InfoCards first. No-op if InfoCards_DA
     * isn't set. Only relevant while SourceMode is UseDataAsset — see
     * RebuildInfoCardsFromCustomArray for the Custom equivalent.
     */
    void RebuildInfoCardsFromDataAsset();

    /**
     * RebuildInfoCardsFromDataAsset's Custom-mode counterpart: duplicates
     * every payload out of CustomInfoCards into InfoCards the same way (so a
     * payload mutated at runtime — GetOrAddPayload, SetPayloadsAtIndex —
     * never reaches back and rewrites what's authored on this anchor's own
     * Details panel). No PayloadDefaults/SettingsOverrides layering step:
     * Custom mode has no separate source for those to be defaults ON TOP OF
     * — CustomInfoCards already IS this anchor's own authored copy.
     */
    void RebuildInfoCardsFromCustomArray();

    /**
     * Merges PayloadDefaults into the just-synced InfoCards, field by field: for
     * each entry, every editable field of Payload that differs from Baseline is
     * copied onto the matching CardId's payload of the same class. Fields that
     * match Baseline are left alone, so they keep the Data Asset's current
     * value. If the Data Asset has no payload of that class for the CardId, a
     * copy of Payload is appended, but only when at least one field was
     * actually changed. A CardId with no matching InfoCards entry is skipped.
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

    /** Replaces the matching CardId's AdditiveAnimations (animation assets included) with each AdditiveAnimationOverrides entry that has Override on. */
    void ApplyAdditiveAnimationOverrides();

    /** Writes Set Card Text's text into the matching cards' text payloads. Last step of every rebuild of InfoCards. */
    void ApplyCardTextOverrides();

    /** Set Card Text's text, by Card ID. */
    TMap<FName, FInfoCardTextOverride> CardTextOverrides;

#if WITH_EDITOR
    /**
     * Makes PayloadDefaults match InfoCards_DA exactly: one entry per payload
     * (CardId + payload class) the Data Asset has, in the Data Asset's order.
     * New entries start as copies of the Data Asset's payload, with a matching
     * Baseline. Surviving entries keep every field that was changed; fields
     * that weren't are refreshed to the Data Asset's current value (in both
     * Payload and Baseline), so the Details panel shows what will actually be
     * used. Entries the Data Asset no longer has are pruned, and the array is
     * emptied when InfoCards_DA is cleared.
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

    /**
     * AdditiveAnimationOverrides equivalent of the syncs above: one entry per
     * CardId, in Data Asset order, pruned the same way. Entries with Override
     * off have Play In / Play Out re-copied from the Data Asset every sync;
     * entries with Override on are left untouched.
     */
    void SyncAdditiveAnimationOverridesFromDataAsset();
#endif

    UPROPERTY(Transient)
    TArray<FInfoCardRuntimeState> RuntimeStates;

    /**
     * True between RequestShowAlwaysOnCards being called and the deferred
     * ShowAlwaysOnCards actually running. Stops a burst of re-arm requests in
     * one frame (several suppression tags removed in a row, say) from
     * queueing several passes, and doubles as the "couldn't schedule, try
     * again from SynchronizeProperties" flag when there's no world yet.
     */
    UPROPERTY(Transient)
    bool bAlwaysOnShowPending = false;

    /**
     * True only while a next-tick pass is actually scheduled. Separate from
     * bAlwaysOnShowPending, which means "a pass is wanted": the two differ in
     * the case that matters, where a request arrived with no world to
     * schedule against — wanted, not queued — and SynchronizeProperties has
     * to be able to tell that apart from an already-queued request so it
     * knows to retry rather than assume it's handled.
     */
    UPROPERTY(Transient)
    bool bAlwaysOnShowQueued = false;

    /**
     * Set the first time this anchor arms its Always On entries at runtime.
     * Distinct from bInfoCardsSynced, which only says InfoCards has been
     * populated: an anchor whose InfoCards were synced at design time arrives
     * at runtime already flagged as synced, and keying the arming off that
     * flag alone is why Always On entries could silently never show at all.
     */
    UPROPERTY(Transient, DuplicateTransient)
    bool bAlwaysOnArmed = false;

    /**
     * Re-entrancy guard for EnsureRuntimeStatesSize. That function closes
     * cards, which broadcasts OnCardHidden, which can run Blueprint that
     * calls straight back into this anchor — and every entry point here
     * begins by calling EnsureRuntimeStatesSize. Since the array isn't
     * resized until the end of the function, the re-entrant call would see
     * the same mismatched sizes and walk the same leftover entries again.
     */
    bool bIsEnsuringRuntimeStates = false;

    /**
     * Whether the mouse is currently over this anchor (between its MouseEnter
     * and MouseLeave), and whether this anchor is currently in the focus path
     * (between focus-received and focus-lost, from either the Automatic Slate
     * path or NotifyFocusReceived/NotifyFocusLost).
     *
     * Only NotifyCardUnpinned reads these. A pinned Highlight card ignores the
     * leave/lost event that would normally close it, so when the pin is
     * released these are how the anchor knows whether that trigger is still
     * holding the card up or already went away.
     */
    bool bIsMouseOverAnchor = false;
    bool bIsFocusInAnchorPath = false;

    /**
     * Set once by EnsureHitTestable so its warning is logged a single time
     * per anchor rather than on every SynchronizeProperties.
     */
    UPROPERTY(Transient)
    bool bHasWarnedAboutHitTesting = false;

    /**
     * Depth of nested ShowCardAtIndex calls across every anchor. A card can
     * legitimately contain an anchor that shows another card, but a
     * misconfigured setup (a card whose own Always On entry spawns its own
     * class again) would otherwise recurse until the stack runs out, which
     * takes the editor down with it. ShowCardAtIndex refuses to go deeper
     * than a fixed cap (see MaxShowCardDepth in InfoCardAnchor.cpp) and logs
     * instead.
     *
     * Not a UPROPERTY: a plain int32 counter, not a UObject reference, so it
     * needs no GC tracking — the same reasoning as
     * DismissClickConsumedFrame above.
     */
    static int32 ShowCardDepth;

    /**
     * Handle for this anchor's subscription to
     * FSlateApplication::OnFocusChanging, set by BindSlateFocusChanging and
     * cleared by UnbindSlateFocusChanging. Not a UPROPERTY — FDelegateHandle
     * isn't a UObject reference and needs no GC tracking.
     */
    FDelegateHandle SlateFocusChangingHandle;

    /**
     * Every anchor currently showing at least one card, across the whole
     * process. Used to implement CloseOtherCardsScope without anchors needing to
     * know about each other directly. Weak so a destroyed anchor just falls
     * out; pruned in PruneRegistry wherever it's walked or updated.
     */
    static TArray<TWeakObjectPtr<UInfoCardAnchor>> ActiveCardAnchors;

    /**
     * Wall-clock (FPlatformTime::Seconds, not game/world time — deliberately
     * so this isn't tangled up with pause state or which of possibly several
     * PIE worlds a card belongs to) timestamp of the last time ANY card,
     * on ANY anchor, closed. Read by ShouldSkipHoverDelay, written by
     * RecordCardClosed. Global rather than per-anchor because the
     * successive-hover speedup is meant to feel like one continuous
     * "browsing" session regardless of which anchor's tooltip the player
     * looks at next.
     */
    static double LastCardClosedRealTimeSeconds;

    /**
     * GFrameCounter value on which MarkDismissClickConsumed was last called,
     * by any card on any anchor. WasDismissClickConsumedThisFrame just
     * compares this against the current GFrameCounter — using the frame
     * counter rather than a bool-plus-reset means nothing needs to remember
     * to clear this, and it can never leak "consumed" into a later frame
     * that never actually had a consuming click.
     *
     * uint64, not a UPROPERTY: GFrameCounter itself is a plain uint64
     * global, not a UObject reference, so this needs no GC tracking.
     */
    static uint64 DismissClickConsumedFrame;

    /**
     * Set once InfoCards has been populated from its source (InfoCards_DA,
     * or CustomInfoCards while SourceMode is Custom) at runtime.
     * SynchronizeProperties only re-copies while this is false (or at
     * design-time) — see SynchronizeProperties for why an unconditional copy
     * on every call is wrong.
     */
    UPROPERTY(Transient, DuplicateTransient)
    bool bInfoCardsSynced = false;

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