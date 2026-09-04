#include "UI/Cards/InfoCardAnchor.h"

#include "UI/Cards/InfoCardWidget_Base.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "InfoCardAnchor"

DEFINE_LOG_CATEGORY_STATIC(LogInfoCard, Log, All);

TArray<TWeakObjectPtr<UInfoCardAnchor>> UInfoCardAnchor::ActiveCardAnchors;

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
	return Super::RebuildWidget();
}

void UInfoCardAnchor::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// InfoCards can be mutated in place at runtime after this first copy —
	// e.g. GetOrAddPayload attaching data into a payload's own properties.
	// UMG can call SynchronizeProperties repeatedly during normal play
	// (visibility changes, layout invalidation, a hover card opening
	// elsewhere), and an unconditional re-copy here would silently discard
	// those mutations, resetting every payload back to its data-asset
	// default the next time a card reads it. Only sync once at runtime;
	// always resync at design-time so editing the data asset is reflected
	// live in the UMG preview.
	if (IsValid(InfoCards_DA) && (IsDesignTime() || !bInfoCardsSyncedFromDataAsset))
	{
		RebuildInfoCardsFromDataAsset();
	}

#if WITH_EDITOR
	// Design-time only. Catches the case PostEditChangeProperty can't: the
	// Data Asset is a separate asset, so edits to ITS contents (a card added,
	// a payload removed) never notify this anchor. Re-syncing here means the
	// Details panel is correct again as soon as the UMG editor next
	// refreshes this widget, with no button for anyone to remember to press.
	if (IsDesignTime())
	{
		SyncPayloadDefaultsFromDataAsset();
		SyncSettingsOverridesFromDataAsset();
	}
#endif

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
	UE_LOG(LogInfoCard, Warning,
		TEXT("InfoCardAnchor '%s' has %d card(s) but was hit-test invisible, so it could never receive hover or right-click. Forcing Visibility to Visible."),
		*GetName(), InfoCards.Num());

	SetVisibility(ESlateVisibility::Visible);
}

void UInfoCardAnchor::ReleaseSlateResources(bool bReleaseChildren)
{
	// Cards live in the viewport, not under us, so they won't be cleaned up
	// automatically when the owning widget is removed.
	HideAllCards();

	Super::ReleaseSlateResources(bReleaseChildren);
}

void UInfoCardAnchor::BeginDestroy()
{
	// Weak pointers would eventually go stale on their own, but leaving dead
	// entries in a process-wide static means every exclusivity walk gets
	// slower for the rest of the session.
	ActiveCardAnchors.RemoveSwap(TWeakObjectPtr<UInfoCardAnchor>(this));

	Super::BeginDestroy();
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

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	EnsureRuntimeStatesSize();

	for (int32 Index = 0; Index < InfoCards.Num(); ++Index)
	{
		const FInfoCardSpec& Spec = InfoCards[Index];
		if (!Spec.bEnabled || Spec.CardType != ECardType::Hover)
		{
			continue;
		}

		if (Spec.HoverDelay <= KINDA_SMALL_NUMBER)
		{
			ShowCardAtIndex(Index);
			continue;
		}

		// Re-entering while a delay timer from a previous enter is still
		// pending would otherwise restart the clock; SetTimer on the same
		// handle replaces it, which is what we want either way. A
		// currently-suppressed entry still gets a timer here — the elapsed
		// handler re-checks suppression via ShowCardAtIndex, so this stays
		// harmless and the entry picks up automatically the moment it's
		// un-suppressed within the delay window.
		World->GetTimerManager().SetTimer(
			RuntimeStates[Index].HoverDelayHandle,
			FTimerDelegate::CreateUObject(this, &UInfoCardAnchor::HandleHoverDelayElapsedForIndex, Index),
			Spec.HoverDelay,
			false);
	}
}

void UInfoCardAnchor::HandleMouseLeave(const FPointerEvent& PointerEvent)
{
	if (IsDesignTime())
	{
		return;
	}

	for (int32 Index = 0; Index < InfoCards.Num(); ++Index)
	{
		if (InfoCards[Index].CardType == ECardType::Hover)
		{
			HideCardAtIndex(Index);
		}
	}
}

FReply UInfoCardAnchor::HandleMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& PointerEvent)
{
	if (!IsDesignTime())
	{
		// Right and left click drive their own, independent CardType — a
		// single mouse-down is never both, so at most one of these matches.
		const FKey EffectingButton = PointerEvent.GetEffectingButton();

		ECardType MatchType;
		if (EffectingButton == EKeys::RightMouseButton)
		{
			MatchType = ECardType::RightClick;
		}
		else if (EffectingButton == EKeys::LeftMouseButton)
		{
			MatchType = ECardType::LeftClick;
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
				const FInfoCardSpec& Spec = InfoCards[Index];
				if (!Spec.bEnabled || Spec.CardType != MatchType)
				{
					continue;
				}

				bHandledAny = true;

				// Right and left click each carry their own toggle flag
				// (Spec.bToggleOnRightClick / Spec.bToggleOnLeftClick) so a
				// single anchor can toggle on one button and re-open-on-
				// reclick on the other, independently, if ever configured
				// that way.
				const bool bToggleOnThisClick = (MatchType == ECardType::RightClick)
					? Spec.bToggleOnRightClick
					: Spec.bToggleOnLeftClick;

				const bool bIsActive = RuntimeStates.IsValidIndex(Index) && RuntimeStates[Index].ActiveCard != nullptr;
				if (bIsActive && bToggleOnThisClick)
				{
					HideCardAtIndex(Index);
				}
				else
				{
					HideCardAtIndex(Index);
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

void UInfoCardAnchor::HandleHoverDelayElapsedForIndex(int32 Index)
{
	ShowCardAtIndex(Index);
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

	// Close and clear anything left over from entries that were removed from
	// InfoCards (e.g. shrinking the array in the editor).
	for (int32 Index = InfoCards.Num(); Index < RuntimeStates.Num(); ++Index)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(RuntimeStates[Index].HoverDelayHandle);
		}
		if (RuntimeStates[Index].ActiveCard)
		{
			RuntimeStates[Index].ActiveCard->CloseCard();
			RuntimeStates[Index].ActiveCard = nullptr;
		}
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

void UInfoCardAnchor::CloseCardsForExclusivity(int32 SourceIndex) const
{
	if (!InfoCards.IsValidIndex(SourceIndex))
	{
		return;
	}

	const ECardExclusivityScope Scope = InfoCards[SourceIndex].ExclusivityScope;
	if (Scope == ECardExclusivityScope::None)
	{
		return;
	}

	const UUserWidget* MySourceWidget = GetCardSourceWidget();

	// Copy first: HideAllCards() below mutates ActiveCardAnchors via the
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

		if (Scope == ECardExclusivityScope::AllCards)
		{
			OtherAnchor->HideAllCards();
			continue;
		}

		// SameSourceWidgetClass: compare the *containing* widget's class
		// (e.g. W_InventorySlot_C), not the anchor's own class, which is
		// always UInfoCardAnchor regardless of where it's placed.
		const UUserWidget* OtherSourceWidget = OtherAnchor->GetCardSourceWidget();
		if (MySourceWidget && OtherSourceWidget && OtherSourceWidget->GetClass() == MySourceWidget->GetClass())
		{
			OtherAnchor->HideAllCards();
		}
	}
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
	// affected, regardless of that entry's own SuppressionTags.
	if (IsValid(InfoCards_DA) && InfoCards_DA->SuppressionTags.HasAny(ActiveSuppressionTags))
	{
		return true;
	}

	return false;
}


// =================================================================================================
// Data asset sync / per-instance defaults
// =================================================================================================

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
		FInfoCardSpec NewSpec = SourceSpec;
		NewSpec.Payloads.Reset(SourceSpec.Payloads.Num());

		for (const TObjectPtr<UInfoCardPayload>& SourcePayload : SourceSpec.Payloads)
		{
			NewSpec.Payloads.Add(SourcePayload ? DuplicateObject<UInfoCardPayload>(SourcePayload, this) : nullptr);
		}

		InfoCards.Add(MoveTemp(NewSpec));
	}

	// Layer this anchor's own defaults/overrides on top of what the Data
	// Asset just gave us, before anything else (suppression checks, first
	// paint, etc.) can read InfoCards.
	ApplyPayloadDefaults();
	ApplySettingsOverrides();

	bInfoCardsSyncedFromDataAsset = true;
}

void UInfoCardAnchor::ApplyPayloadDefaults()
{
	for (const FInfoCardPayloadDefault& Default : PayloadDefaults)
	{
		if (Default.CardId == NAME_None)
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

		for (const FInfoCardPayloadDefaultEntry& DefaultEntry : Default.Payloads)
		{
			// Off by default (including every auto-generated stub) — an
			// un-customized stub must never displace the Data Asset's real
			// value, which is the whole reason this is a struct with a gate
			// rather than a bare payload pointer.
			if (!DefaultEntry.bSetDefaults || !DefaultEntry.Payload)
			{
				continue;
			}

			// Each anchor needs its own instance, same reasoning as the
			// Data Asset's own payloads above — otherwise every anchor
			// using this default would share one UObject.
			UInfoCardPayload* Duplicated = DuplicateObject<UInfoCardPayload>(DefaultEntry.Payload, this);

			const int32 ExistingIndex = Spec.Payloads.IndexOfByPredicate(
				[Duplicated](const TObjectPtr<UInfoCardPayload>& ExistingPayload)
				{
					return ExistingPayload && ExistingPayload->GetClass() == Duplicated->GetClass();
				});

			if (ExistingIndex != INDEX_NONE)
			{
				// Same payload class as something the Data Asset already
				// gave this CardId — replace it rather than showing both.
				Spec.Payloads[ExistingIndex] = Duplicated;
			}
			else
			{
				// A payload class the Data Asset didn't have for this
				// CardId — just add it.
				Spec.Payloads.Add(Duplicated);
			}
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
			Spec.bToggleOnRightClick = Settings.bToggleOnRightClick;
			Spec.bToggleOnLeftClick = Settings.bToggleOnLeftClick;
		}

		if (Settings.bOverrideCardPosition)
		{
			Spec.PositionMode = Settings.PositionMode;
			Spec.IconPosition = Settings.IconPosition;
			Spec.CursorPosition = Settings.CursorPosition;
			Spec.ViewportPosition = Settings.ViewportPosition;
			Spec.bClampToViewport = Settings.bClampToViewport;
		}

		if (Settings.bOverrideOffset)
		{
			Spec.Offset = Settings.Offset;
			Spec.HoverAnchorPadding = Settings.HoverAnchorPadding;
		}

		if (Settings.bOverrideTickUpdate)
		{
			Spec.bUpdatePositionOnTick = Settings.bUpdatePositionOnTick;
		}
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
	if (Tags.IsEmpty())
	{
		return;
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
	// Doesn't re-open anything: a card closed by suppression rebuilds fresh
	// from whatever's actually true the next time it's naturally triggered,
	// the same "safe default" already used for the inventory-display bind
	// in UInfoCardWidgetBase::HandleBoundInventoryChanged.
	ActiveSuppressionTags.RemoveTag(Tag);
}

void UInfoCardAnchor::RemoveSuppressionTags(const FGameplayTagContainer& Tags)
{
	ActiveSuppressionTags.RemoveTags(Tags);
}


// =================================================================================================
// By index
// =================================================================================================

void UInfoCardAnchor::ShowCardAtIndex(int32 Index)
{
	EnsureRuntimeStatesSize();

	if (!InfoCards.IsValidIndex(Index) || !RuntimeStates.IsValidIndex(Index))
	{
		return;
	}

	// bEnabled: designer/runtime master switch for this entry.
	if (!InfoCards[Index].bEnabled || !InfoCards[Index].CardClass || RuntimeStates[Index].ActiveCard)
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

	// Close whatever ExclusivityScope says needs closing before we spawn, so
	// there's never a one-frame overlap between the old card and the new one.
	// This can re-enter the anchors' own bookkeeping, so nothing above is
	// held by reference across it.
	CloseCardsForExclusivity(Index);

	if (!InfoCards.IsValidIndex(Index) || !RuntimeStates.IsValidIndex(Index))
	{
		return; // paranoia: an exclusivity close could have torn things down
	}

	UInfoCardWidgetBase* NewCard = CreateWidget<UInfoCardWidgetBase>(OwningPlayer, InfoCards[Index].CardClass);
	if (!NewCard)
	{
		return;
	}

	// SetupCard before AddToViewport: NativeConstruct (which plays the
	// entrance animation) runs inside AddToViewport, and it should see the
	// card's final trigger type / payloads, not the defaults.
	NewCard->SetupCard(InfoCards[Index], this);
	NewCard->AddToViewport(InfoCards[Index].ZOrder);

	RuntimeStates[Index].ActiveCard = NewCard;

	// Place it immediately so it doesn't flash at the origin for one frame.
	NewCard->UpdatePosition();

	UpdateRegistryMembership();
}

void UInfoCardAnchor::HideCardAtIndex(int32 Index)
{
	EnsureRuntimeStatesSize();

	if (!RuntimeStates.IsValidIndex(Index))
	{
		return;
	}

	FInfoCardRuntimeState& State = RuntimeStates[Index];

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(State.HoverDelayHandle);
	}

	if (State.ActiveCard)
	{
		State.ActiveCard->CloseCard();
		State.ActiveCard = nullptr;
	}

	UpdateRegistryMembership();
}

void UInfoCardAnchor::ToggleCardAtIndex(int32 Index)
{
	if (RuntimeStates.IsValidIndex(Index) && RuntimeStates[Index].ActiveCard)
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
	}
}

void UInfoCardAnchor::SetPayloadsAtIndex(int32 Index, const TArray<UInfoCardPayload*>& NewPayloads)
{
	if (!InfoCards.IsValidIndex(Index))
	{
		return;
	}

	FInfoCardSpec& Spec = InfoCards[Index];
	Spec.Payloads.Reset(NewPayloads.Num());
	for (UInfoCardPayload* PayloadObj : NewPayloads)
	{
		Spec.Payloads.Add(PayloadObj);
	}

	EnsureRuntimeStatesSize();

	if (UInfoCardWidgetBase* ActiveCard = RuntimeStates.IsValidIndex(Index) ? RuntimeStates[Index].ActiveCard : nullptr)
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

void UInfoCardAnchor::SetPayloadAtIndex(int32 Index, UInfoCardPayload* NewPayload)
{
	TArray<UInfoCardPayload*> Single;
	if (NewPayload)
	{
		Single.Add(NewPayload);
	}
	SetPayloadsAtIndex(Index, Single);
}

UInfoCardWidgetBase* UInfoCardAnchor::GetActiveCardAtIndex(int32 Index) const
{
	return RuntimeStates.IsValidIndex(Index) ? RuntimeStates[Index].ActiveCard : nullptr;
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

void UInfoCardAnchor::SetCardEnabledById(FName CardId, bool bNewEnabled)
{
	SetCardEnabledAtIndex(FindCardIndexById(CardId), bNewEnabled);
}

void UInfoCardAnchor::SetPayloadsById(FName CardId, const TArray<UInfoCardPayload*>& NewPayloads)
{
	SetPayloadsAtIndex(FindCardIndexById(CardId), NewPayloads);
}

void UInfoCardAnchor::SetPayloadById(FName CardId, UInfoCardPayload* NewPayload)
{
	SetPayloadAtIndex(FindCardIndexById(CardId), NewPayload);
}

UInfoCardPayload* UInfoCardAnchor::GetOrAddPayload(FName CardId, TSubclassOf<UInfoCardPayload> PayloadClass, bool& bIsPayloadValid, FName& InputCardID)
{
	bIsPayloadValid = false;
	InputCardID = CardId;
	
	if (!PayloadClass)
	{
		return nullptr;
	}
	
	if (InfoCards.Num() == 0 && IsValid(InfoCards_DA) && InfoCards_DA->InfoCards.Num() > 0)
	{
		RebuildInfoCardsFromDataAsset();
	}

	const int32 Index = FindCardIndexById(CardId);

	if (!InfoCards.IsValidIndex(Index))
	{
		return nullptr;
	}

	FInfoCardSpec& Spec = InfoCards[Index];
	
	for (const TObjectPtr<UInfoCardPayload>& Existing : Spec.Payloads)
	{
		if (Existing && Existing->IsA(PayloadClass))
		{
			bIsPayloadValid = true;
			return Existing;
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

	UInfoCardWidgetBase* ActiveCard = RuntimeStates.IsValidIndex(Index) ? RuntimeStates[Index].ActiveCard : nullptr;
	if (!ActiveCard)
	{
		return; // not currently shown — nothing to redraw; next open already picks up the mutated data
	}

	TArray<UInfoCardPayload*> RawPayloads;
	RawPayloads.Reserve(InfoCards[Index].Payloads.Num());
	for (const TObjectPtr<UInfoCardPayload>& P : InfoCards[Index].Payloads)
	{
		RawPayloads.Add(P.Get());
	}

	ActiveCard->SetPayloads(RawPayloads);
}

UInfoCardWidgetBase* UInfoCardAnchor::GetActiveCardById(FName CardId) const
{
	return GetActiveCardAtIndex(FindCardIndexById(CardId));
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
	}
}

void UInfoCardAnchor::SyncPayloadDefaultsFromDataAsset()
{
	// No Data Asset assigned: there is nothing for defaults to be layered on
	// top of, so leaving the previous asset's entries sitting in the Details
	// panel would just be showing the designer a list that does nothing.
	if (!IsValid(InfoCards_DA))
	{
		PayloadDefaults.Reset();
		return;
	}

	// Built as a fresh array and swapped in at the end rather than mutated in
	// place: that gets pruning of removed CardIds for free (anything not
	// rebuilt simply isn't carried over) and keeps PayloadDefaults in the
	// same order as the Data Asset's own InfoCards, which is what a designer
	// comparing the two side by side expects.
	TArray<FInfoCardPayloadDefault> Rebuilt;
	Rebuilt.Reserve(InfoCards_DA->InfoCards.Num());

	for (const FInfoCardSpec& SourceSpec : InfoCards_DA->InfoCards)
	{
		if (SourceSpec.CardId == NAME_None)
		{
			// Without an id there's nothing for ApplyPayloadDefaults to match
			// against, so a slot for it could never take effect anyway.
			continue;
		}

		const FInfoCardPayloadDefault* ExistingDefault = PayloadDefaults.FindByPredicate(
			[&SourceSpec](const FInfoCardPayloadDefault& Existing) { return Existing.CardId == SourceSpec.CardId; });

		FInfoCardPayloadDefault NewDefault;
		NewDefault.CardId = SourceSpec.CardId;

		for (const TObjectPtr<UInfoCardPayload>& SourcePayload : SourceSpec.Payloads)
		{
			if (!SourcePayload)
			{
				continue;
			}

			UClass* PayloadClass = SourcePayload->GetClass();

			// Carry over the existing slot for this payload class if there is
			// one, so anything already switched on and edited survives the
			// sync untouched. Only genuinely new classes get a fresh slot.
			const FInfoCardPayloadDefaultEntry* ExistingEntry = ExistingDefault
				? ExistingDefault->Payloads.FindByPredicate(
					[PayloadClass](const FInfoCardPayloadDefaultEntry& Entry)
					{
						return Entry.Payload && Entry.Payload->GetClass() == PayloadClass;
					})
				: nullptr;

			if (ExistingEntry)
			{
				NewDefault.Payloads.Add(*ExistingEntry);
				continue;
			}

			FInfoCardPayloadDefaultEntry NewEntry;
			NewEntry.bSetDefaults = false;
			NewEntry.Payload = NewObject<UInfoCardPayload>(this, PayloadClass);
			NewDefault.Payloads.Add(MoveTemp(NewEntry));
		}

		Rebuilt.Add(MoveTemp(NewDefault));
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
#endif

#undef LOCTEXT_NAMESPACE