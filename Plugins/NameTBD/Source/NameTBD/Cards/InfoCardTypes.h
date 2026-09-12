// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "Components/Widget.h"
#include "Layout/Margin.h"
#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h" // FWidgetAnimSettings, for FInfoCardAdditiveAnimations
#include "InfoCardTypes.generated.h"

class UInfoCardWidget;

/**
 * Where the info card is placed relative to an icon. Every value except
 * FillIcon is a direction the card sits OUT from the icon in; FillIcon instead
 * lays the card directly over the icon, sized to cover it (inset by
 * FInfoCardSpec::FillIconPadding).
 *
 * FillIcon is appended last on purpose. Enum properties serialize by name, but
 * Blueprint byte conversions and anything else that stored the raw number
 * keep meaning exactly what they meant before.
 */
UENUM(BlueprintType)
enum class EDirection2D : uint8
{
	Up,
	Down,
	Left,
	Right,
	UpLeft	   UMETA(DisplayName = "Up Left"),
	UpRight	   UMETA(DisplayName = "Up Right"),
	DownLeft   UMETA(DisplayName = "Down Left"),
	DownRight  UMETA(DisplayName = "Down Right"),
	FillIcon   UMETA(DisplayName = "Fill Icon", ToolTip = "Lays the card directly over the icon, sized to cover it exactly, inset by Fill Icon Padding. Never flips, clamps to the viewport, or nudges itself away from other cards: it follows the icon.")
};

/** Corner/edge used to place the info card relative to the mouse cursor. */
UENUM(BlueprintType)
enum class EInfoCardCursorPosition : uint8
{
	UpRight 	UMETA(DisplayName = "Up Right"),
	DownRight 	UMETA(DisplayName = "Down Right"),
	UpLeft 		UMETA(DisplayName = "Up Left"),
	DownLeft 	UMETA(DisplayName = "Down Left"),
	UpCenter 	UMETA(DisplayName = "Up Center"),
	DownCenter 	UMETA(DisplayName = "Down Center"),
	LeftCenter 	UMETA(DisplayName = "Left Center"),
	RightCenter UMETA(DisplayName = "Right Center")
};

/** Whether the card anchors itself to an icon, a fixed cursor-relative position, or a fixed viewport-relative position. */
UENUM(BlueprintType)
enum class EInfoCardPosition : uint8
{
	ToIcon		UMETA(DisplayName = "To Icon"),
	ToCursor	UMETA(DisplayName = "To Cursor"),
	ToViewport	UMETA(DisplayName = "To Viewport")
};

/** Fixed corner/edge of the viewport used to place the info card when PositionMode is ToViewport. */
UENUM(BlueprintType)
enum class EViewportPosition : uint8
{
	TopLeft		 UMETA(DisplayName = "Top Left"),
	TopCenter	 UMETA(DisplayName = "Top Center"),
	TopRight	 UMETA(DisplayName = "Top Right"),
	LeftCenter	 UMETA(DisplayName = "Left Center"),
	RightCenter  UMETA(DisplayName = "Right Center"),
	BottomLeft	 UMETA(DisplayName = "Bottom Left"),
	BottomCenter UMETA(DisplayName = "Bottom Center"),
	BottomRight  UMETA(DisplayName = "Bottom Right")
};

/**
 * How the card is triggered by the widget it is attached to.
 *
 * Hover shows the DisplayName "Highlight" in editor UI (see UMETA below):
 * an entry of this type can be triggered by mouse hover, keyboard/gamepad
 * focus, or both — see FInfoCardSpec::bTriggerOnMouseHover /
 * bTriggerOnFocus. The underlying C++ name is left as Hover rather than
 * renamed, so existing code/data referencing ECardType::Hover keeps
 * compiling; only the editor-facing label changed.
 */
UENUM(BlueprintType)
enum class ECardType : uint8
{
	AlwaysOn
	UMETA(DisplayName = "Always On", ToolTip = "A plain, unconditional info card. Shows as soon as this entry is available — no hover, focus, or click needed — and stays up until something else removes it (Remove Card On Pressing, a suppression tag, HideCardAtIndex/HideCardById, ApplyCloseOthersScope, etc.). The default: most info cards are just 'a card that's there', not one that needs a trigger."),

	Hover 
	UMETA(DisplayName = "Highlight", ToolTip = "Shown on mouse hover and/or keyboard/gamepad focus, based on Trigger On Mouse Hover / Trigger On Focus below."),
	
	RightClick	UMETA(DisplayName = "Right Click"),
	LeftClick	UMETA(DisplayName = "Left Click")
};

/**
 * When this card shows, should any OTHER already-showing info card(s) be
 * closed to make room for it? This never affects the anchor's own other
 * entries (coexisting is the point of having more than one entry per
 * anchor) — only cards belonging to other UInfoCardAnchor instances. See
 * UInfoCardAnchor::ApplyCloseOthersScope.
 */
UENUM(BlueprintType)
enum class ECardCloseOthersScope : uint8
{
	None 
	UMETA(ToolTip = "Don't close anything else. Other anchors' cards stay open alongside this one."),
	
	SameSourceWidgetClass 
	UMETA(DisplayName = "Same Source Widget Class", ToolTip = "Close other anchors' cards, but only if they belong to the same kind of containing widget as this one (e.g. every W_InventorySlot closes its card when another W_InventorySlot's opens, but a HUD tooltip elsewhere is untouched)."),
	
	AllCards 
	UMETA(DisplayName = "All Cards", ToolTip = "Close every other anchor's card, anywhere, regardless of what widget it belongs to.")
};

/**
 * Where does a press have to land to remove this card? Checked by
 * Uinfocardwidget::TickClickOutsideToClose (only while that card's own
 * bCloseOnClickOutside is on, and never for the press that spawned the card
 * itself — see Uinfocardwidget::SpawnPressSerial). Applies to every
 * ECardType, not just Hover — an AlwaysOn card is exactly the case that has
 * no other way to go away short of code calling HideCardAtIndex/HideCardById,
 * so this is frequently its only removal path.
 *
 * "A press" here means ANY press, not one particular button: every mouse
 * button and every gamepad button qualifies, regardless of which one (if
 * any) triggered this card in the first place. A right-click card is
 * dismissed by a left-click just as readily as by another right-click, the
 * way an OS context menu behaves. Keyboard keys deliberately don't count —
 * typing in a text field shouldn't tear down whatever is on screen. See
 * Uinfocardwidget::IsDismissingKey.
 *
 * Presses are seen through a Slate input pre-processor, so a press is
 * registered wherever it lands: on a Button, on another card, on this card's
 * own anchor, or over an area where the game has UI-only input mode set and
 * no press ever reaches the PlayerController at all.
 */
UENUM(BlueprintType)
enum class ECardRemoveOnPressZone : uint8
{
	InsideAnchorOrCard
	UMETA(DisplayName = "Inside Card or Anchor", ToolTip = "A press landing on this card itself, or on its anchor, removes it. A press anywhere else is ignored. Useful for a card with its own explicit 'tap to dismiss' interaction."),

	OutsideAnchorAndCard
	UMETA(DisplayName = "Outside Anchor and Card", ToolTip = "A press landing anywhere OTHER than this card or its anchor removes it — the ordinary 'click away to dismiss a tooltip' behaviour, and this card's previous, hardcoded default. Pressing the anchor keeps the card, which is what you want when the anchor is a button the card describes."),

	OutsideCard
	UMETA(DisplayName = "Outside Card", ToolTip = "A press landing anywhere other than this card itself removes it — INCLUDING a press on its own anchor. The difference from 'Outside Anchor and Card' is exactly that: use this when re-pressing the anchor should dismiss the card rather than be ignored, e.g. a menu that toggles shut when you press the thing that opened it."),

	Anywhere
	UMETA(ToolTip = "Any press at all removes this card, regardless of where it lands — including on the card or its anchor.")
};

/**
 * Governs whether UInfoCardAnchor's keyboard/gamepad focus support is driven
 * automatically by the engine's own Slate focus path, driven manually by the
 * host project's own navigation system, or not used at all. See
 * UInfoCardAnchor::FocusTriggerMode.
 */
UENUM(BlueprintType)
enum class EInfoCardFocusTriggerMode : uint8
{
	Automatic 
	UMETA(ToolTip = "Standard UMG/Slate keyboard and gamepad navigation drives this automatically. Works out of the box with engine-standard focus and most navigation frameworks built on it."),
	
	Manual	
	UMETA(ToolTip = "The engine's native focus events are ignored. Call NotifyFocusReceived/NotifyFocusLost from your own navigation system instead. Use this if your project tracks focus outside of Slate's native focus system."),
	
	Disabled 
	UMETA(ToolTip = "Focus never triggers a card on this anchor, through either path. Use this for a mouse/click-only anchor.")
};

/**
 * Whether a tunable value on an InfoCards entry comes from the project-wide
 * UInfoCardDeveloperSettings, or is authored right here on this entry.
 * UseProjectSettings is the default: a fresh card automatically tracks
 * whatever the project's Default preset says, and stays in sync with it —
 * change the project setting once, and every card using it moves together,
 * no per-asset re-tuning required. Switch to Custom only for the entries
 * that genuinely need their own value.
 */
UENUM(BlueprintType)
enum class EInfoCardValueSource : uint8
{
	UseProjectSettings UMETA(DisplayName = "Use Project Settings"),
	Custom
};

/**
 * Identifies which project-wide tunable a given FInfoCardConfigurableFloat
 * represents, so FInfoCardConfigurableFloat::Resolve and
 * UInfoCardDeveloperSettings::TryGetPresetValue know which field of a named
 * preset to read. Not stored on the struct itself — passed in by whoever's
 * reading it, since the same struct type is reused for every tunable and has
 * no way to know on its own which one it's standing in for.
 */
UENUM(BlueprintType)
enum class EInfoCardConfigurableFloatKind : uint8
{
	HoverDelay,
	KeepOpenSlack,
	KeepOpenGracePeriod,
	SuccessiveHoverWindow,
	AutoDismissDuration
};

/**
 * A float that's either pulled live from a named preset on
 * UInfoCardDeveloperSettings or authored right here as Custom — the building
 * block behind the {Use Project Settings, Custom} dropdown described there.
 * Reused as-is for HoverDelay, KeepOpenSlack, KeepOpenGracePeriod, and
 * SuccessiveHoverWindow on both FInfoCardSpec and FInfoCardSettingsOverride,
 * rather than four near-identical structs, since the shape (source, which
 * preset, custom value) is identical for all of them — only Resolve's Kind
 * argument tells them apart.
 *
 * PresetName is a free-form name rather than an enum on purpose: the list of
 * presets on UInfoCardDeveloperSettings is a designer-editable array (see
 * that class), starting with just one entry named "Default" and growing as
 * the project adds more — there's no fixed count to enumerate. The Details
 * panel restricts PresetName to whatever presets currently exist (see
 * UInfoCardDeveloperSettings::GetPresetNameOptions), but because it's a name
 * rather than an index, renaming a preset a card already points at breaks
 * the link — Resolve falls back to CustomValue if PresetName no longer
 * matches anything.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FInfoCardConfigurableFloat
{
	GENERATED_BODY()

	FInfoCardConfigurableFloat() = default;

	/** Constructs a value that defaults to UseProjectSettings > "Default", but has a sensible CustomValue already sitting there in case someone switches this entry to Custom. */
	explicit FInfoCardConfigurableFloat(float InDefaultCustomValue)
		: CustomValue(InDefaultCustomValue)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	EInfoCardValueSource Source = EInfoCardValueSource::UseProjectSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (EditCondition = "Source == EInfoCardValueSource::UseProjectSettings", EditConditionHides, GetOptions = "InfoCardDeveloperSettings.GetPresetNameOptions"))
	FName PresetName = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (EditCondition = "Source == EInfoCardValueSource::Custom", EditConditionHides, ClampMin = "0.0"))
	float CustomValue = 0.f;

	/**
	 * The number to actually use: CustomValue as-authored if Source is
	 * Custom; otherwise whichever preset PresetName points at, for the field
	 * Kind selects, read live off UInfoCardDeveloperSettings every call — so
	 * editing a preset is picked up by every card pointed at it immediately,
	 * nothing on the card itself needing to resync. Falls back to
	 * CustomValue if PresetName doesn't match any current preset (renamed or
	 * deleted since this entry was set to it) rather than silently
	 * substituting some other preset's number.
	 */
	float Resolve(EInfoCardConfigurableFloatKind Kind) const;
};

/**
 * Identifies which project-wide bool a given FInfoCardConfigurableBool
 * represents — the bool equivalent of EInfoCardConfigurableFloatKind.
 */
UENUM(BlueprintType)
enum class EInfoCardConfigurableBoolKind : uint8
{
	DismissClickGetsConsumed,
	FlipToAvoidClipping
};

/**
 * A bool that's either pulled live from a single project-wide default on
 * UInfoCardDeveloperSettings or authored right here as Custom — the bool
 * equivalent of FInfoCardConfigurableFloat, behind the same {Use Project
 * Settings, Custom} dropdown (EInfoCardValueSource is shared between the
 * two rather than duplicated).
 *
 * Deliberately simpler than FInfoCardConfigurableFloat: no PresetName. The
 * float version's named presets exist because a numeric tuning value
 * (a delay, a padding) plausibly wants a different profile per use case: a
 * project might want "Default" and "Fast" hover-delay presets, say. A
 * project-wide behavioural switch like "does a dismiss click get consumed"
 * doesn't have that same use case — there's one sensible project default,
 * matching how UInfoCardDeveloperSettings::bEnableSuccessiveHover is
 * already a single flat bool rather than a per-preset one.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FInfoCardConfigurableBool
{
	GENERATED_BODY()

	FInfoCardConfigurableBool() = default;

	/** Constructs a value that defaults to UseProjectSettings, but has a sensible CustomValue already sitting there in case someone switches this entry to Custom. */
	explicit FInfoCardConfigurableBool(bool InDefaultCustomValue)
		: CustomValue(InDefaultCustomValue)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	EInfoCardValueSource Source = EInfoCardValueSource::UseProjectSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (EditCondition = "Source == EInfoCardValueSource::Custom", EditConditionHides))
	bool CustomValue = false;

	/**
	 * The bool to actually use: CustomValue as-authored if Source is Custom;
	 * otherwise whichever single project-wide default Kind selects, read
	 * live off UInfoCardDeveloperSettings every call. Falls back to
	 * CustomValue if there's no settings CDO (shouldn't happen).
	 */
	bool Resolve(EInfoCardConfigurableBoolKind Kind) const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Payloads
////////////////////////////////////////////////////////////////////////////////////////////////////////////

UCLASS(DefaultToInstanced, EditInlineNew, DisplayName = "Info Card Data")
class NAMETBD_API UInfoCardPayload : public UObject
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override;
};

UCLASS(DisplayName = "Info Card Data (Text)")
class NAMETBD_API UInfoCardPayload_Text : public UInfoCardPayload
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (MultiLine = true))
	FText Body;
};

/**
 * Written at runtime by UInfoCardAnchor::NotifyHoldProgress, once per tick,
 * while a hold-to-interact gesture is building toward this card's CardId.
 * Unlike the other payload types there's nothing here for a designer to
 * author in the Details panel — Progress only ever has a meaningful value
 * once a hold is actually in progress — so just attach this payload to the
 * InfoCards entry you want a hold's progress pushed into, and let
 * NotifyHoldProgress fill it in.
 */
UCLASS(DisplayName = "Info Card Data (Hold Progress)")
class NAMETBD_API UInfoCardPayload_HoldProgress : public UInfoCardPayload
{
	GENERATED_BODY()

public:
	/** 0–1 (or whatever range the caller uses) fraction of the hold completed so far. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Hold Progress")
	float Progress = 0.f;
};



/**
 * Where a UInfoCardAnchor gets its InfoCards from. See
 * UInfoCardAnchor::SourceMode for what each option shows/hides in the
 * Details panel.
 */
UENUM(BlueprintType)
enum class EInfoCardSourceMode : uint8
{
	UseDataAsset UMETA(DisplayName = "Use Data Asset",
		ToolTip = "InfoCards is sourced from InfoCards_DA below, with PayloadDefaults / SettingsOverrides layering this anchor's own per-instance tweaks on top. The normal path for anything meant to be reused across more than one anchor."),

	Custom UMETA(
		ToolTip = "InfoCards_DA, PayloadDefaults, and SettingsOverrides are hidden and ignored. InfoCards entries are authored directly on this anchor instead (Custom Info Cards below) — quicker for a one-off tooltip that doesn't need its own Data Asset, at the cost of not being shareable with any other anchor.")
};

/** The animation types an additive info card animation can use: every whole-widget type. Material types need a target image inside the card, which an additive entry has no way to pick. */
UENUM(BlueprintType)
enum class EInfoCardAdditiveAnimType : uint8
{
	Fade,
	Scale,
	Slide,
	Rotate,
	Pulse
};

/**
 * One additive animation entry. The same settings as FWidgetAnimSettings, minus
 * target selection: an additive entry always animates the whole spawned card.
 *
 * A spec lives on a Data Asset or on an anchor inside some other Widget
 * Blueprint, so a target picker there can only ever offer widgets from THAT
 * Blueprint, never the card's own widgets. Targeted animations belong on the
 * card class's own Anim In / Anim Out, where the picker shows the right tree.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FInfoCardAdditiveAnimSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	EInfoCardAdditiveAnimType AnimType = EInfoCardAdditiveAnimType::Fade;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.01"))
	float Duration = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (ClampMin = "0.0"))
	float Delay = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	EWidgetAnimEasing Easing = EWidgetAnimEasing::EaseOut;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "Easing == EWidgetAnimEasing::Custom", EditConditionHides))
	TObjectPtr<UCurveFloat> CustomCurve = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bLoop = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "bLoop", EditConditionHides))
	bool bPingPong = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (EditCondition = "bLoop", EditConditionHides, ClampMin = "0"))
	int32 LoopCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bInvertAnimation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	bool bMirrorInOut = false;

	// Fade
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (EditCondition = "AnimType == EInfoCardAdditiveAnimType::Fade", EditConditionHides))
	float StartOpacity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fade", meta = (EditCondition = "AnimType == EInfoCardAdditiveAnimType::Fade", EditConditionHides))
	float EndOpacity = 1.f;

	// Scale
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EInfoCardAdditiveAnimType::Scale", EditConditionHides))
	float StartScale = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EInfoCardAdditiveAnimType::Scale", EditConditionHides))
	float EndScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scale", meta = (EditCondition = "AnimType == EInfoCardAdditiveAnimType::Scale", EditConditionHides, ClampMin = "0.0"))
	float Overshoot = 0.4f;

	// Slide
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EInfoCardAdditiveAnimType::Slide", EditConditionHides))
	EWidgetSlideDirection SlideFromDirection = EWidgetSlideDirection::Down;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EInfoCardAdditiveAnimType::Slide", EditConditionHides, ClampMin = "0.0"))
	float SlideDistance = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide", meta = (EditCondition = "AnimType == EInfoCardAdditiveAnimType::Slide", EditConditionHides, ClampMin = "0.0"))
	float SlideOvershoot = 0.f;

	// Rotate
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotate", meta = (EditCondition = "AnimType == EInfoCardAdditiveAnimType::Rotate", EditConditionHides))
	float StartAngle = -180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotate", meta = (EditCondition = "AnimType == EInfoCardAdditiveAnimType::Rotate", EditConditionHides))
	float EndAngle = 0.f;

	// Pulse
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pulse", meta = (EditCondition = "AnimType == EInfoCardAdditiveAnimType::Pulse", EditConditionHides, ClampMin = "0.0"))
	float PulseScaleAmount = 0.05f;

	/** The equivalent FWidgetAnimSettings, with no target, so the animation library applies it to the card itself. */
	FWidgetAnimSettings ToWidgetAnimSettings() const;

	/** Converts an old FWidgetAnimSettings additive entry. Its target is dropped (the entry now animates the whole card). Returns false for material entries, which have no whole-card equivalent. */
	static bool FromLegacyWidgetAnimSettings(const FWidgetAnimSettings& Legacy, FInfoCardAdditiveAnimSettings& Out);
};

/**
 * Extra entrance/exit animations layered on top of the ones authored on the
 * card class itself (UUserWidget_Animatable::AnimIn / AnimOut).
 * ADDITIVE, never a replacement: whatever the card's Widget Blueprint already
 * plays still plays, and every entry here plays alongside it. Since the
 * animation system combines simultaneous entries rather than letting them
 * overwrite each other (opacity and scale multiply, translation and angle
 * sum), an entry here can stack on top of the card's own — e.g. a small extra
 * slide on a card whose class already fades in.
 *
 * Every entry animates the whole card; there is no target selection (see
 * FInfoCardAdditiveAnimSettings for why).
 *
 * Merged into the spawned card's AnimIn / AnimOut in
 * UInfoCardWidget::SetupCard, before the card is constructed, so the
 * NativeConstruct -> RebuildEffectiveAnimations -> PlayIn that follows already
 * includes them. Entries with bMirrorInOut set are mirrored into the other
 * direction exactly the way the card's own are.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FInfoCardAdditiveAnimations
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Play In", ToolTip = "Played in addition to the card class's own Anim In when this card appears. Does not replace them. Always animates the whole card."))
	TArray<FInfoCardAdditiveAnimSettings> PlayIn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation", meta = (DisplayName = "Play Out", ToolTip = "Played in addition to the card class's own Anim Out when this card closes. Does not replace them. Always animates the whole card."))
	TArray<FInfoCardAdditiveAnimSettings> PlayOut;

	/**
	 * Entries saved before additive animations lost their target picker, under
	 * the old property names. Hidden; PostSerialize converts them into PlayIn /
	 * PlayOut on load and empties these, so re-saving the asset drops them.
	 */
	UPROPERTY()
	TArray<FWidgetAnimSettings> PlayInAnimations;

	UPROPERTY()
	TArray<FWidgetAnimSettings> PlayOutAnimations;

	bool IsEmpty() const
	{
		return PlayIn.IsEmpty() && PlayOut.IsEmpty();
	}

	void PostSerialize(const FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FInfoCardAdditiveAnimations> : public TStructOpsTypeTraitsBase2<FInfoCardAdditiveAnimations>
{
	enum
	{
		WithPostSerialize = true
	};
};

USTRUCT(BlueprintType)
struct NAMETBD_API FInfoCardSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (DisplayName = "Card ID", ToolTip = "Identifier used for info card event handling."))
	FName CardId = NAME_None;
	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "User defined info cards; children of UInfoCardWidgetBase."))
	TSubclassOf<UInfoCardWidget> CardClass;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "Particularlly useful for disabling cards in the overrides array."))
	bool bEnabled = true;
	
	/**
	 * Per-card suppression: 
	 * When the owning anchor's suppression tags (by InfoCardAnchor->AddSuppressionTag) contain 
	 * any tag in this container, the info card will be suppressed: it will not spawn, and if it is
	 * already showing it is closed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Suppression")
	FGameplayTagContainer SuppressionTags;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card")
	ECardType CardType = ECardType::AlwaysOn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (DisplayName = "Highlight Delay", ToolTip = "Seconds to wait after being triggered (by mouse hover or focus) before this card appears. 0 shows it immediately. Also skipped — shown instantly regardless of this value — if another card closed within Successive Hover Window below; see UInfoCardDeveloperSettings' Successive Hover section for the on/off switch.", EditCondition = "CardType == ECardType::Hover"))
	FInfoCardConfigurableFloat HoverDelay = FInfoCardConfigurableFloat(0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "Seconds after any card (anywhere) closes during which THIS card, if triggered, skips Highlight Delay entirely and shows instantly — the 'Windows tooltip' behaviour of hovering a second control right after dismissing the first one's tooltip. Only takes effect while UInfoCardDeveloperSettings::bEnableSuccessiveHover is on; that on/off switch isn't per-card, only this window length is.", EditCondition = "CardType == ECardType::Hover"))
	FInfoCardConfigurableFloat SuccessiveHoverWindow = FInfoCardConfigurableFloat(0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "If true, this card can be triggered by the mouse hovering its anchor.", EditCondition = "CardType == ECardType::Hover"))
	bool bTriggerOnMouseHover = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "If true, this card can be triggered by keyboard/gamepad focus reaching its anchor. See UInfoCardAnchor::FocusTriggerMode for how that focus is detected.", EditCondition = "CardType == ECardType::Hover"))
	bool bTriggerOnFocus = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (DisplayName = "Reappear On Click Dismiss", ToolTip = "If true, dismissing this card by clicking its own anchor (cursor never actually leaving it) makes it trigger again immediately, instead of waiting for the cursor to leave and come back.\n \n Useful for tooltips on non-buttons", EditCondition = "CardType == ECardType::Hover"))
	bool bReappearOnClickDismiss = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "If true, once shown this card stays open even after the cursor leaves the anchor, as long as the cursor is still over the anchor's icon (expanded by Icon Anchor Padding) or over the card itself. Only closes once the cursor leaves both.\nUseful for interactable cards (e.g. containing buttons) so moving the mouse from the icon onto the card doesn't close it along the way. \n \nMouse-only — losing keyboard/gamepad focus still closes it immediately regardless of this setting.", EditCondition = "CardType == ECardType::Hover"))
	bool bKeepOpenUntilCursorLeaves = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "Extra slack, in viewport units, added around both the icon and the card when deciding whether the cursor has 'left'. This is what bridges the gap the card is drawn across, so it should be at least as large as any Card Offset you apply. Raise it if the card still closes while the cursor is travelling toward it; lower it if the card hangs around too far away.", EditCondition = "CardType == ECardType::Hover && bKeepOpenUntilCursorLeaves"))
	FInfoCardConfigurableFloat KeepOpenSlack = FInfoCardConfigurableFloat(16.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "Seconds the cursor must stay outside the icon/card region before this card actually closes. Any return inside the region resets it. A small non-zero value here is what stops a single bad frame (a layout pass, a fast diagonal flick across a corner) from closing the card.", EditCondition = "CardType == ECardType::Hover && bKeepOpenUntilCursorLeaves"))
	FInfoCardConfigurableFloat KeepOpenGracePeriod = FInfoCardConfigurableFloat(0.2f);

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "When this card shows, whether it closes other already-showing cards elsewhere to make room for it. Never affects this same anchor's other entries."))
	ECardCloseOthersScope CloseOtherCardsScope = ECardCloseOthersScope::None;



	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "Where a press has to land to remove this card. Outside Anchor and Card is the ordinary 'click away to dismiss' behavior. Any mouse or gamepad button counts, not just whichever one triggered the card."))
	ECardRemoveOnPressZone RemoveCardOnPressing = ECardRemoveOnPressZone::OutsideAnchorAndCard;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "Seconds after this card appears before it auto-closes itself, regardless of cursor position. 0 means never. Independent of KeepOpenGracePeriod, which only reacts to the cursor leaving."))
	FInfoCardConfigurableFloat AutoDismissDuration = FInfoCardConfigurableFloat(0.f);


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "If true, a dismissing card click will not have any effect on the screen, only to dismiss the card."))
	FInfoCardConfigurableBool DismissClickGetsConsumed = FInfoCardConfigurableBool(false);

	/**
	 * If true, this card persists through menus opening and closing instead of closing itself the moment any menu state 
	 * changes. Useful for manually handling the card's lifetime.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card", meta = (ToolTip = "If true, the card will ignore the event and will persist throughout menus opening and closing. Useful for manually handling the card's lifetime."))
	bool bIgnoresMenuStateChange = false;


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position")
	EInfoCardPosition PositionMode = EInfoCardPosition::ToIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToIcon", EditConditionHides, ToolTip = "Info card position relative to the icon (anchor's children in designer)."))
	EDirection2D IconPosition = EDirection2D::Up;

	/**
	 * Only used while IconPosition is Fill Icon.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToIcon && IconPosition == EDirection2D::FillIcon", EditConditionHides, ToolTip = "Inset from each edge of the icon (Left/Top/Right/Bottom) when Icon Position is Fill Icon. 0 covers the icon exactly; negative values extend past it. The card is sized to the result, so its root content must be able to stretch."))
	FMargin FillIconPadding = FMargin(0.f);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToCursor", EditConditionHides))
	EInfoCardCursorPosition CursorPosition = EInfoCardCursorPosition::UpRight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToViewport", EditConditionHides))
	EViewportPosition ViewportPosition = EViewportPosition::TopRight;
	
	/**
	 * The card widget's own render transform.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Transform", meta = (ToolTip = "Translation, scale, shear, and angle applied to the card widget itself, on top of wherever CardOffset/PositionMode placed it. Purely visual — plays no part in the icon/card/corridor hit-testing math."))
	FWidgetTransform CardTransform;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToCursor || PositionMode == EInfoCardPosition::ToViewport", EditConditionHides, ToolTip = "Offset between cursor/viewport to info card."))
	FVector2D CardOffset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position", meta = (EditCondition = "PositionMode == EInfoCardPosition::ToIcon", EditConditionHides, ToolTip = "Padding between icon (anchor's children in designer) and info card."))
	float IconAnchorPadding = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position", meta = (ToolTip = "If true, a card that would run out of room is kept fully on screen. For PositionMode::ToIcon and ToCursor specifically: if FlipToAvoidClipping is also on and the card would exit off the same side its IconPosition/CursorPosition points toward, it flips to the opposite side first — the way most OS tooltips behave. Only once flipping is off, isn't possible, or doesn't help does it fall back to clamping, with Clamp Padding kept as spacing from the edge. The axis perpendicular to the flip (e.g. left/right for a card anchored Up/Down) has no 'other side' to flip to and is always just clamped."))
	bool bClampToViewport = true;

	/**
	 * Whether ClampToViewport is allowed to flip this card to the opposite
	 * side before falling back to a plain edge clamp — see bClampToViewport
	 * for the mechanics. Only meaningful for PositionMode::ToIcon and
	 * ToCursor (ToViewport is already anchored to a specific corner on
	 * purpose, nothing to flip). Defaults to true, matching the flip
	 * behaviour ToIcon always had before this was a separate toggle;
	 * turn it off for a card that should only ever clamp, never flip
	 * sides.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position", meta = (EditCondition = "bClampToViewport && PositionMode != EInfoCardPosition::ToViewport", EditConditionHides, ToolTip = "Whether a card running out of room flips to the opposite side (Up<->Down, Left<->Right, and each axis of a diagonal independently) before falling back to a plain clamp. Only meaningful for ToIcon/ToCursor."))
	FInfoCardConfigurableBool FlipToAvoidClipping = FInfoCardConfigurableBool(true);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position", meta = (EditCondition = "bClampToViewport", EditConditionHides, ToolTip = "Breathing room, in viewport units, kept between the card and the screen edge whenever ClampToViewport has to pin it there, and between this card and any other currently-visible card it would otherwise overlap (see Avoid Overlapping Cards). Also the margin used to decide a ToIcon card has run out of room in the first place, so it flips a little before touching the edge rather than at the very last pixel. Applies to every PositionMode, not just ToIcon."))
	float ClampPadding = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position", meta = (ToolTip = "If true, this card checks itself against every other currently-visible card (any anchor, any PositionMode) and, if it would overlap one, nudges itself below that card by Clamp Padding — the way toast notifications stack rather than pile on top of each other. A card only ever yields to a card that was shown before it, never the other way around, so two cards never fight over the same spot. A ToIcon card nested inside other cards may sit flush against the menu it came out of, but won't cover the rest of its own menu chain: if it would (a dropdown that flipped up over its own menu, or a cascading card landing on a menu further up the chain), it slides to the nearest clear spot beside it instead, preferring to carry on in its Icon Position's direction. Turn this off for a card that's meant to sit in an exact spot regardless of what else is on screen (e.g. a fixed HUD readout)."))
	bool bAvoidOverlappingCards = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Card Position")
	bool bUpdatePositionOnTick = true;
	

	/**
	 * Extra Play In / Play Out animations for this card, played on top of —
	 * never instead of — the Anim In / Anim Out authored on CardClass. See
	 * FInfoCardAdditiveAnimations.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Animations", meta = (DisplayName = "Additive Animations", ToolTip = "Extra Play In / Play Out animations, played in addition to the Anim In / Anim Out already set on the card class. They add to the card's own animations; they never replace them."))
	FInfoCardAdditiveAnimations AdditiveAnimations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Info Card|Placement")
	int32 ZOrder = 1000;

	/** User defined payloads; children of UInfoCardPayload. 
	 * The info card class defined above will recieve these payloads by the event ApplyPayloads. 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Info Card|Payloads")
	TArray<TObjectPtr<UInfoCardPayload>> Payloads;

};