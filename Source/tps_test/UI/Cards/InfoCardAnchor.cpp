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
		// handle replaces it, which is what we want either way.
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
	if (!IsDesignTime() && PointerEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		EnsureRuntimeStatesSize();

		bool bHandledAny = false;

		for (int32 Index = 0; Index < InfoCards.Num(); ++Index)
		{
			const FInfoCardSpec& Spec = InfoCards[Index];
			if (!Spec.bEnabled || Spec.CardType != ECardType::RightClick)
			{
				continue;
			}

			bHandledAny = true;

			const bool bIsActive = RuntimeStates.IsValidIndex(Index) && RuntimeStates[Index].ActiveCard != nullptr;
			if (bIsActive && Spec.bToggleOnRightClick)
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
#endif

#undef LOCTEXT_NAMESPACE