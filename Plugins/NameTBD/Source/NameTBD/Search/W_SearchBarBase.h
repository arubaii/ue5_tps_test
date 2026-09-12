// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "W_SearchBarBase.generated.h"

class UEditableTextBox;
class UComboBoxString;
class UButton;

/**
 * One filter option shown in the dropdown. Key is the opaque identifier broadcast back
 * to the owner in OnSearchChanged — it's an FName, not the real payload, on purpose.
 *
 * A search bar that stored TObjectPtr<UScriptStruct> for inventory fragments would be
 * useless for a quest log filtering by FGameplayTag, or a settings menu filtering by
 * enum. Key stays opaque so this struct works for all three; each owner keeps its own
 * map from Key back to whatever it actually needs — e.g. TMap<FName, UScriptStruct*> on
 * W_Inventory mapping "Weapons" to a fragment type, entirely invisible to this widget.
 *
 * DisplayText is separate from Key so the dropdown label can be localized/renamed
 * without changing the identifier consumers switch on.
 */
USTRUCT(BlueprintType)
struct FSearchFilterOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search")
	FName Key = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search")
	FText DisplayText;
};

/**
 * A search bar that doesn't know what it searches.
 *
 * It owns exactly two pieces of state — the typed query and the selected filter — and
 * broadcasts them when either changes. It never filters anything itself, never holds a
 * reference to an inventory or a quest log, and never asks what its results are for.
 * Whoever embeds it subscribes to OnSearchChanged and does the domain work there, which
 * is what lets the same widget serve an inventory panel and a settings menu unmodified.
 *
 * The filter options are supplied by the owner via SetFilterOptions, for the same reason:
 * "Weapons / Consumables / Quest Items" is an inventory's vocabulary, not a search bar's.
 *
 * BLUEPRINT SETUP — create a WBP child of this class containing:
 *   - an EditableTextBox named exactly "SearchText"          (required)
 *   - a ComboBoxString named exactly "FilterBox"             (optional)
 *   - a Button named exactly "ClearButton"                   (optional)
 * BindWidget will fail compilation if the required one is missing or misnamed.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSearchChanged, const FString&, Query, FName, ActiveFilter);

UCLASS()
class NAMETBD_API UW_SearchBarBase : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Fires whenever the query text or the selected filter changes, after debouncing.
	 * Both values are passed together because a consumer almost always needs both to
	 * decide what to show, and firing them separately would make it filter twice for
	 * what the player experienced as one action.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Search")
	FOnSearchChanged OnSearchChanged;

	/**
	 * Populate the filter dropdown. Pass an empty array to hide it entirely — a search
	 * bar with nothing to filter by shouldn't show a dropdown with one dead entry in it.
	 *
	 * AllOption is prepended as the catch-all ("All", "Any Type") and is what
	 * ActiveFilter reports as NAME_None, so consumers can treat "no filter" as a single
	 * condition rather than comparing against whatever key this was given.
	 */
	UFUNCTION(BlueprintCallable, Category = "Search")
	void SetFilterOptions(const TArray<FSearchFilterOption>& Options,
	                      FText AllOptionLabel = NSLOCTEXT("SearchBar", "AllFilter", "All"));

	/** Current query text. */
	UFUNCTION(BlueprintPure, Category = "Search")
	FString GetQuery() const { return CurrentQuery; }

	/** Selected filter, or NAME_None when the catch-all option is selected. */
	UFUNCTION(BlueprintPure, Category = "Search")
	FName GetActiveFilter() const { return ActiveFilter; }

	/** Reset text and filter and broadcast once, e.g. when the owning panel is reopened. */
	UFUNCTION(BlueprintCallable, Category = "Search")
	void ClearSearch();

	/**
	 * Seconds to wait after the last keystroke before broadcasting. Filtering a large
	 * list on literally every character is wasted work the player can't perceive; a
	 * short delay collapses a burst of typing into one filter pass. Set to 0 to
	 * broadcast immediately.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "0.0"))
	float SearchDebounceSeconds = 0.15f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Search")
	TObjectPtr<UEditableTextBox> SearchText;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Search")
	TObjectPtr<UComboBoxString> FilterBox;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Search")
	TObjectPtr<UButton> ClearButton;

private:
	UFUNCTION()
	void HandleTextChanged(const FText& NewText);

	UFUNCTION()
	void HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleFilterSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleClearClicked();

	/** Start (or restart) the debounce window. Broadcasts immediately if the delay is 0. */
	void ScheduleBroadcast();

	/** The one place OnSearchChanged is fired from, so state is always consistent when it is. */
	void BroadcastNow();

	FString CurrentQuery;
	FName ActiveFilter = NAME_None;

	/** The options SetFilterOptions was last given, so a selection can be mapped back to a Key. */
	UPROPERTY()
	TArray<FSearchFilterOption> FilterOptions;

	/** Reserved key for the catch-all entry — ActiveFilter reports NAME_None for it regardless. */
	static constexpr const TCHAR* AllOptionKey = TEXT("__SearchBar_All__");

	FTimerHandle DebounceTimer;
};