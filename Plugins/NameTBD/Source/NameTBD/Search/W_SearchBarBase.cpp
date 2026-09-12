// Copyright AR GameSystems 2026. All rights reserved.

#include "W_SearchBarBase.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "TimerManager.h"

void UW_SearchBarBase::NativeConstruct()
{
	Super::NativeConstruct();

	if (SearchText)
	{
		SearchText->OnTextChanged.AddDynamic(this, &UW_SearchBarBase::HandleTextChanged);

		// Committing (Enter) bypasses the debounce: the player has explicitly said
		// "I'm done typing", so making them wait out a timer would feel unresponsive.
		SearchText->OnTextCommitted.AddDynamic(this, &UW_SearchBarBase::HandleTextCommitted);
	}

	if (FilterBox)
	{
		FilterBox->OnSelectionChanged.AddDynamic(this, &UW_SearchBarBase::HandleFilterSelectionChanged);

		// Hidden until SetFilterOptions gives it something to show. A dropdown whose only
		// entry is "All" is a control that can't do anything.
		FilterBox->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (ClearButton)
		ClearButton->OnClicked.AddDynamic(this, &UW_SearchBarBase::HandleClearClicked);
}

void UW_SearchBarBase::NativeDestruct()
{
	// The timer holds a raw this — leaving it live past teardown is a crash waiting for
	// the next tick.
	if (UWorld* world = GetWorld())
		world->GetTimerManager().ClearTimer(DebounceTimer);

	Super::NativeDestruct();
}

void UW_SearchBarBase::SetFilterOptions(const TArray<FSearchFilterOption>& Options, FText AllOptionLabel)
{
	if (!FilterBox)
		return;

	FilterBox->ClearOptions();
	FilterOptions.Reset();

	if (Options.Num() == 0)
	{
		ActiveFilter = NAME_None;
		FilterBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	// The catch-all gets a reserved key rather than NAME_None directly, so a domain that
	// legitimately uses NAME_None as one of its own filter keys (unlikely, but not this
	// widget's business to forbid) can't collide with it.
	FilterOptions.Add(FSearchFilterOption{ FName(AllOptionKey), AllOptionLabel });
	FilterOptions.Append(Options);

	// ComboBoxString only knows strings; the label is what's shown, Key is resolved back
	// on selection via FilterOptions below. Two options with the same DisplayText but
	// different Keys will visually collide in the dropdown — that's a content problem
	// for the caller to avoid, not something this widget can prevent generically.
	for (const FSearchFilterOption& option : FilterOptions)
		FilterBox->AddOption(option.DisplayText.ToString());

	// SetSelectedIndex fires OnSelectionChanged with ESelectInfo::Direct, which the
	// handler ignores — so populating the box doesn't masquerade as the player choosing
	// something and doesn't kick off a filter pass on its own.
	FilterBox->SetSelectedIndex(0);
	ActiveFilter = NAME_None;

	FilterBox->SetVisibility(ESlateVisibility::Visible);
}

void UW_SearchBarBase::ClearSearch()
{
	CurrentQuery.Reset();
	ActiveFilter = NAME_None;

	if (SearchText)
		SearchText->SetText(FText::GetEmpty());

	if (FilterBox && FilterBox->GetOptionCount() > 0)
		FilterBox->SetSelectedIndex(0);

	// Deliberately immediate rather than debounced: clearing is a discrete action, and
	// the consumer needs to restore the unfiltered list now, not in 150ms.
	BroadcastNow();
}

void UW_SearchBarBase::HandleTextChanged(const FText& NewText)
{
	CurrentQuery = NewText.ToString();
	ScheduleBroadcast();
}

void UW_SearchBarBase::HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod)
{
	if (CommitMethod != ETextCommit::OnEnter)
		return;

	CurrentQuery = NewText.ToString();
	BroadcastNow();
}

void UW_SearchBarBase::HandleFilterSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// Direct means we set the selection ourselves in code; only react to the player.
	if (SelectionType == ESelectInfo::Direct)
		return;

	// Resolve the picked label back to its Key via FilterOptions rather than trying to
	// derive one from the string itself — the string is display text, possibly
	// localized, and was never meant to be parsed back into an identifier.
	const FSearchFilterOption* match = FilterOptions.FindByPredicate(
		[&SelectedItem](const FSearchFilterOption& Option)
		{
			return Option.DisplayText.ToString() == SelectedItem;
		});

	const FName resolvedKey = match ? match->Key : NAME_None;
	ActiveFilter = (resolvedKey == FName(AllOptionKey)) ? NAME_None : resolvedKey;

	// A dropdown pick is a deliberate single action, not a stream of keystrokes, so it
	// has nothing to debounce.
	BroadcastNow();
}

void UW_SearchBarBase::HandleClearClicked()
{
	ClearSearch();
}

void UW_SearchBarBase::ScheduleBroadcast()
{
	UWorld* world = GetWorld();
	if (!world || SearchDebounceSeconds <= 0.f)
	{
		BroadcastNow();
		return;
	}

	// Restarting rather than stacking: each keystroke pushes the broadcast further out,
	// so a burst of typing produces exactly one filter pass at the end of it.
	world->GetTimerManager().ClearTimer(DebounceTimer);
	world->GetTimerManager().SetTimer(DebounceTimer, this, &UW_SearchBarBase::BroadcastNow,
	                                  SearchDebounceSeconds, false);
}

void UW_SearchBarBase::BroadcastNow()
{
	if (UWorld* world = GetWorld())
		world->GetTimerManager().ClearTimer(DebounceTimer);

	OnSearchChanged.Broadcast(CurrentQuery, ActiveFilter);
}