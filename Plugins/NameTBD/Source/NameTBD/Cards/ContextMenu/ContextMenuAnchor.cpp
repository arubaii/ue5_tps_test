// Copyright AR GameSystems 2026. All rights reserved.

#include "ContextMenuAnchor.h"

#include "Components/Button.h"
#include "Containers/Ticker.h"

#include "NameTBD/Buttons/Actions/ContextMenuInterface.h"
#include "ContextMenuWidget.h"

const FName UContextMenuAnchor::ContextMenuCardId(TEXT("ContextMenu"));
const FName UContextMenuAnchor::LeftClickContextMenuCardId(TEXT("ContextMenuLeft"));

void UContextMenuAnchor::SynchronizeProperties()
{
#if WITH_EDITOR
	// Before the cards are generated, so they are built from overrides that match the menu as it is now (its asset may
	// have been edited since this anchor was last looked at).
	if (IsDesignTime() && !HasAnyFlags(RF_ClassDefaultObject))
	{
		SyncEntryOverridesFromMenu();
	}
#endif

	Super::SynchronizeProperties();

	// The click that opens the menu reaches these before the card is built: the hook for On Context Menu Opening, and
	// for putting a keyboard opening's placement back to the mouse's.
	if (!IsDesignTime())
	{
		OnRightClickPressed.AddUniqueDynamic(this, &UContextMenuAnchor::HandleClickOpening);
		OnLeftClickPressed.AddUniqueDynamic(this, &UContextMenuAnchor::HandleClickOpening);

		if (UButton* ContentButton = Cast<UButton>(GetContent()))
		{
			if (OpenWith == EContextMenuOpenTrigger::LeftClick || OpenWith == EContextMenuOpenTrigger::AnyClick)
			{
				ContentButton->OnPressed.AddUniqueDynamic(this, &UContextMenuAnchor::HandleContentButtonPressed);
				ContentButton->OnClicked.AddUniqueDynamic(this, &UContextMenuAnchor::HandleContentButtonClicked);
			}
		}
	}
}

void UContextMenuAnchor::HandleContentButtonPressed()
{
	// Read at the press: the press itself dismisses an open menu (Outside Card counts the anchor as outside), so by the
	// click it may already be gone, and the click must not open it straight back up.
	bMenuOpenAtButtonPress = IsContextMenuOpen();
}

void UContextMenuAnchor::HandleContentButtonClicked()
{
	if (bMenuOpenAtButtonPress)
	{
		bMenuOpenAtButtonPress = false;
		CloseContextMenu();
		return;
	}

	HandleClickOpening();
	ShowCardById(OpenWith == EContextMenuOpenTrigger::AnyClick ? LeftClickContextMenuCardId : ContextMenuCardId);
}

bool UContextMenuAnchor::BuildGeneratedInfoCards(TArray<FInfoCardSpec>& OutSpecs)
{
	const FContextMenuStyle& MenuLook = UContextMenuStyleAsset::Resolve(StyleOverride, Menu);

	UInfoCardPayload_ContextMenu* MenuData = NewObject<UInfoCardPayload_ContextMenu>(this);
	MenuData->Menu = Menu;
	MenuData->StyleOverride = StyleOverride;
	for (const FContextMenuEntryOverride& Override : EntryOverrides)
	{
		if (!Override.IsUntouched())
		{
			MenuData->EntryOverrides.Add(Override);
		}
	}
	MenuData->ExtraEntries = ExtraEntries;
	MenuData->MenuAnchor = this;
	MenuData->bFocusFirstEntry = bOpeningFromKeyboard;

	auto MakeSpec = [this, &MenuLook, MenuData](FName MenuCardId, ECardType Trigger)
	{
		FInfoCardSpec Spec;
		Spec.CardId = MenuCardId;
		Spec.CardType = Trigger;
		Spec.CardClass = MenuLook.MenuWidgetClass ? TSubclassOf<UInfoCardWidget>(MenuLook.MenuWidgetClass) : TSubclassOf<UInfoCardWidget>(UContextMenuWidget::StaticClass());
		Spec.bEnabled = Menu != nullptr || ExtraEntries.Num() > 0;

		// Manual: a Highlight entry nothing can trigger; Open Context Menu shows it by ID.
		if (OpenWith == EContextMenuOpenTrigger::Manual)
		{
			Spec.CardType = ECardType::Hover;
			Spec.bTriggerOnMouseHover = false;
			Spec.bTriggerOnFocus = false;
		}

		const bool bAtCursor = Placement == EContextMenuPlacement::AtCursor && !bOpeningFromKeyboard;
		if (bAtCursor)
		{
			// Where the click landed, and staying there: a menu doesn't follow the cursor.
			Spec.PositionMode = EInfoCardPosition::ToCursor;
			Spec.CursorPosition = EInfoCardCursorPosition::DownRight;
			Spec.CardOffset = FVector2D(2.f, 2.f);
			Spec.bUpdatePositionOnTick = false;
		}
		else
		{
			Spec.PositionMode = EInfoCardPosition::ToIcon;
			Spec.IconPosition = Placement == EContextMenuPlacement::RightOfWidget ? EDirection2D::Right : EDirection2D::Down;
			Spec.IconAnchorPadding = 2.f;
		}

		Spec.bClampToViewport = true;
		Spec.FlipToAvoidClipping.Source = EInfoCardValueSource::Custom;
		Spec.FlipToAvoidClipping.CustomValue = true;
		Spec.RemoveCardOnPressing = ECardRemoveOnPressZone::OutsideCard;
		Spec.CloseOtherCardsScope = MenuLook.bCloseOtherCardsOnOpen ? ECardCloseOthersScope::AllCards : ECardCloseOthersScope::None;
		// A menu stays where it opened; a tooltip elsewhere on screen is no reason to move it.
		Spec.bAvoidOverlappingCards = false;
		Spec.ZOrder = 1500;

		Spec.Payloads.Add(MenuData);
		for (const TObjectPtr<UContextButtonPayload>& ActionPayload : ActionPayloads)
		{
			if (ActionPayload)
			{
				Spec.Payloads.Add(DuplicateObject<UContextButtonPayload>(ActionPayload, this));
			}
		}
		return Spec;
	};

	switch (OpenWith)
	{
	case EContextMenuOpenTrigger::LeftClick:
		OutSpecs.Add(MakeSpec(ContextMenuCardId, ECardType::LeftClick));
		break;
	case EContextMenuOpenTrigger::AnyClick:
		OutSpecs.Add(MakeSpec(ContextMenuCardId, ECardType::RightClick));
		OutSpecs.Add(MakeSpec(LeftClickContextMenuCardId, ECardType::LeftClick));
		break;
	case EContextMenuOpenTrigger::RightClick:
	case EContextMenuOpenTrigger::Manual:
	default:
		OutSpecs.Add(MakeSpec(ContextMenuCardId, ECardType::RightClick));
		break;
	}
	return true;
}

void UContextMenuAnchor::HandleClickOpening()
{
	OnContextMenuOpening.Broadcast();

	// A click opens at the cursor, whatever the last opening from a key left the cards set to.
	if (bOpeningFromKeyboard)
	{
		bOpeningFromKeyboard = false;
		RebuildGeneratedInfoCards();
	}
}

void UContextMenuAnchor::OpenContextMenu(bool bFocusFirstEntry)
{
	OnContextMenuOpening.Broadcast();

	if (IsContextMenuOpen())
	{
		CloseContextMenu();
	}

	// The placement and the focus depend on how the menu was asked for, and both are read from the card as it is built.
	if (bOpeningFromKeyboard != bFocusFirstEntry || InfoCards.Num() == 0)
	{
		bOpeningFromKeyboard = bFocusFirstEntry;
		RebuildGeneratedInfoCards();
	}

	ShowCardById(ContextMenuCardId);
}

void UContextMenuAnchor::CloseContextMenu()
{
	HideCardById(ContextMenuCardId);
	HideCardById(LeftClickContextMenuCardId);
}

UContextMenuWidget* UContextMenuAnchor::GetOpenContextMenu() const
{
	for (const FName MenuCardId : { ContextMenuCardId, LeftClickContextMenuCardId })
	{
		UContextMenuWidget* Open = Cast<UContextMenuWidget>(GetActiveCardById(MenuCardId));
		if (Open && !Open->IsClosing())
		{
			return Open;
		}
	}
	return nullptr;
}

bool UContextMenuAnchor::IsContextMenuOpen() const
{
	return GetOpenContextMenu() != nullptr;
}

void UContextMenuAnchor::SetContextMenu(UContextMenuAsset* NewMenu)
{
	Menu = NewMenu;
	RebuildGeneratedInfoCards();
}

void UContextMenuAnchor::SetActionPayloads(const TArray<UContextButtonPayload*>& NewActionPayloads)
{
	ActionPayloads.Reset(NewActionPayloads.Num());
	for (UContextButtonPayload* Payload : NewActionPayloads)
	{
		ActionPayloads.Add(Payload);
	}
	RebuildGeneratedInfoCards();
}

void UContextMenuAnchor::SetEntryEnabled(FName EntryId, bool bEntryEnabled)
{
	EntryRuntimeStates.FindOrAdd(EntryId).bEnabled = bEntryEnabled;
	RequestOpenMenuRefresh();
}

void UContextMenuAnchor::SetEntryHidden(FName EntryId, bool bEntryHidden)
{
	EntryRuntimeStates.FindOrAdd(EntryId).bHidden = bEntryHidden;
	RequestOpenMenuRefresh();
}

void UContextMenuAnchor::SetEntryLabel(FName EntryId, FText NewLabel)
{
	EntryRuntimeStates.FindOrAdd(EntryId).Label = NewLabel;
	RequestOpenMenuRefresh();
}

void UContextMenuAnchor::SetEntryChecked(FName EntryId, bool bEntryChecked)
{
	EntryRuntimeStates.FindOrAdd(EntryId).bChecked = bEntryChecked;
	RequestOpenMenuRefresh();
}

bool UContextMenuAnchor::IsEntryChecked(FName EntryId) const
{
	if (const FContextMenuEntryRuntimeState* State = EntryRuntimeStates.Find(EntryId))
	{
		if (State->bChecked.IsSet())
		{
			return State->bChecked.GetValue();
		}
	}

	if (const FContextMenuEntryOverride* Override = EntryOverrides.FindByPredicate([EntryId](const FContextMenuEntryOverride& Candidate) { return Candidate.EntryId == EntryId; }))
	{
		if (Override->bOverrideChecked)
		{
			return Override->bChecked;
		}
	}

	if (Menu)
	{
		if (const FContextMenuEntry* Found = Menu->Entries.FindByPredicate([EntryId](const FContextMenuEntry& Candidate) { return Candidate.EntryId == EntryId; }))
		{
			return Found->bChecked;
		}
	}

	const FContextMenuExtraEntry* Extra = ExtraEntries.FindByPredicate([EntryId](const FContextMenuExtraEntry& Candidate) { return Candidate.Entry.EntryId == EntryId; });
	return Extra && Extra->Entry.bChecked;
}

void UContextMenuAnchor::ClearEntryChanges()
{
	EntryRuntimeStates.Reset();
	RequestOpenMenuRefresh();
}

void UContextMenuAnchor::RememberEntryChecked(FName EntryId, bool bNowChecked)
{
	// Remembered without rebuilding: the menu already shows it, and its rows are mid-click.
	EntryRuntimeStates.FindOrAdd(EntryId).bChecked = bNowChecked;
}

void UContextMenuAnchor::NotifyMenuEntryChosen(FName EntryId, bool bNowChecked)
{
	OnMenuEntryClicked.Broadcast(EntryId, bNowChecked);
}

void UContextMenuAnchor::RequestOpenMenuRefresh()
{
	if (bOpenMenuRefreshQueued || !IsContextMenuOpen())
	{
		return;
	}

	// Next tick, never now: a Set Entry... call from inside a row's action or On Menu Entry Clicked would otherwise rebuild
	// the rows while one of them is still handling its click.
	bOpenMenuRefreshQueued = true;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
	{
		bOpenMenuRefreshQueued = false;
		if (UContextMenuWidget* Open = GetOpenContextMenu())
		{
			Open->RebuildMenu();
			for (UContextMenuEntryWidget* Row : Open->GetEntryWidgets())
			{
				if (UContextMenuWidget* Submenu = Row ? Row->GetOpenSubmenu() : nullptr)
				{
					Submenu->RebuildMenu();
				}
			}
		}
		return false;
	}), 0.f);
}

TArray<FString> UContextMenuAnchor::GetMenuEntryIdOptions() const
{
	TArray<FString> Options;
	if (Menu)
	{
		for (const FName Id : Menu->GetEntryIds())
		{
			Options.Add(Id.ToString());
		}
	}
	return Options;
}

#if WITH_EDITOR
void UContextMenuAnchor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UContextMenuAnchor, Menu))
	{
		SyncEntryOverridesFromMenu();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UContextMenuAnchor::SyncEntryOverridesFromMenu()
{
	if (!Menu)
	{
		EntryOverrides.Reset();
		return;
	}

	// Rebuilt and swapped, as the base anchor's syncs do: pruning for free, and the menu's order.
	TArray<FContextMenuEntryOverride> Rebuilt;
	for (const FContextMenuEntry& Entry : Menu->Entries)
	{
		if (Entry.EntryId.IsNone())
		{
			continue;
		}

		const FContextMenuEntryOverride* Existing = EntryOverrides.FindByPredicate([&Entry](const FContextMenuEntryOverride& Candidate) { return Candidate.EntryId == Entry.EntryId; });
		FContextMenuEntryOverride& Kept = Rebuilt.Add_GetRef(Existing ? *Existing : FContextMenuEntryOverride());
		Kept.EntryId = Entry.EntryId;

		// An override not switched on shows what the menu has, so switching it on starts from there.
		if (!Kept.bOverrideLabel)
		{
			Kept.Label = Entry.Label;
		}
		if (!Kept.bOverrideEnabled)
		{
			Kept.bEnabled = Entry.bEnabled;
		}
		if (!Kept.bOverrideChecked)
		{
			Kept.bChecked = Entry.bChecked;
		}
	}

	EntryOverrides = MoveTemp(Rebuilt);
}
#endif
