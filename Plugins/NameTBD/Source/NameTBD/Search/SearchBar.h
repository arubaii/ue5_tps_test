// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "GameFramework/SaveGame.h"
#include "Layout/Margin.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Widgets/SCompoundWidget.h"
#include "NameTBD/Menu/Settings/MenuSettingTypes.h"
#include "NameTBD/Search/SearchLibrary.h"
#include "NameTBD/Search/W_SearchBarBase.h"

#include "SearchBar.generated.h"

class SEditableText;
class SMenuAnchor;
class UListView;
class UPanelWidget;
template <typename ItemType> class SListView;
template <typename OptionType> class SComboBox;

/**
 * The Search Bar: drop it from the palette (NameTBD) and it works - no Widget Blueprint to build, no widgets to name. It draws
 * itself (an icon, the field and its hint, a clear button, a filter dropdown, a result count), suggests as you type (matches
 * from what it searches, your own list, recent searches), and - given a Results Widget - filters it with no Blueprint at all:
 * a list or tile view's items, or a panel's children (a Wrap Box of inventory slots, a Vertical Box of quests), hiding what
 * doesn't match and showing a No Results widget when nothing does. Or leave Results Widget empty and use On Search Changed.
 *
 * What an item is found by: its Search Text (and Keywords) when it implements Searchable Item; otherwise a panel child's text
 * (every Text Block in it) and a list item's Name / Display Name / Title / Label / Text property.
 */

/** A ready-made look for the Search Bar. Picking one fills Style; Custom keeps your own. */
UENUM(BlueprintType)
enum class ESearchBarLook : uint8
{
	Dark UMETA(ToolTip = "A dark rounded field, its outline lit in the accent colour while typing - the usual game UI search."),
	Light UMETA(ToolTip = "A light field with dark text, for light menus."),
	Pill UMETA(ToolTip = "Fully rounded ends, dark - a toolbar or store search."),
	Minimal UMETA(ToolTip = "No box: text and icon only, a soft outline appearing while hovered or typing - over art or inside a header."),
	Custom UMETA(ToolTip = "Style as you set it.")
};

/** How a Search Bar is drawn. */
USTRUCT(BlueprintType)
struct NAMETBD_API FSearchBarStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field")
	FSlateBrush Background;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field")
	FSlateBrush HoveredBackground;

	/** While typing (the field has the keyboard focus). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field")
	FSlateBrush FocusedBackground;

	/** Inside the field, round its contents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field")
	FMargin Padding = FMargin(12.f, 8.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FSlateFontInfo Font;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FLinearColor TextColor = FLinearColor::White;

	/** The hint shown while nothing is typed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FLinearColor HintColor = FLinearColor(1.f, 1.f, 1.f, 0.45f);

	/** The magnifier before the text. Empty: a vector magnifier drawn in Icon Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FMenuImage SearchIcon;

	/** The clear button's picture. Empty: a vector cross drawn in Clear Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FMenuImage ClearIcon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons", meta = (ClampMin = "4", UIMax = "48", ForceUnits = "px"))
	float IconSize = 16.f;

	/** Between the icon, the text and the buttons. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons", meta = (ClampMin = "0", UIMax = "32", ForceUnits = "px"))
	float Spacing = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FLinearColor IconColor = FLinearColor(1.f, 1.f, 1.f, 0.55f);

	/** The icon while typing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FLinearColor FocusedIconColor = FLinearColor(0.35f, 0.6f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FLinearColor ClearColor = FLinearColor(1.f, 1.f, 1.f, 0.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icons")
	FLinearColor ClearHoveredColor = FLinearColor::White;

	/** The result count ("12 results") and the filter's label. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FSlateFontInfo SecondaryFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FLinearColor SecondaryColor = FLinearColor(1.f, 1.f, 1.f, 0.55f);

	/** Behind the suggestions list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suggestions")
	FSlateBrush PopupBackground;

	/** Behind the suggestion under the pointer or picked with up / down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suggestions")
	FSlateBrush SuggestionHighlight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suggestions")
	FLinearColor SuggestionColor = FLinearColor(1.f, 1.f, 1.f, 0.8f);

	/** The letters of a suggestion that match what is typed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suggestions")
	FLinearColor MatchColor = FLinearColor(0.45f, 0.7f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Suggestions")
	FMargin SuggestionPadding = FMargin(10.f, 6.f);

	FSearchBarStyle();

	/** One of the ready-made looks, in Font (its typeface and size kept; the secondary font a size smaller). */
	static FSearchBarStyle MakeLook(ESearchBarLook InLook, const FSlateFontInfo& InFont);

	/** Writes a ready-made look into Out (onto a style made with the defaults). */
	static void FillLook(FSearchBarStyle& Out, ESearchBarLook InLook, const FSlateFontInfo& InFont);
};

/** A Search Bar look kept as an asset, so every search in a game shares it (Miscellaneous > Data Asset > Search Bar Style). */
UCLASS(BlueprintType, meta = (DisplayName = "Search Bar Style"))
class NAMETBD_API USearchBarStyleAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	USearchBarStyleAsset();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search Bar", meta = (ShowOnlyInnerProperties))
	FSearchBarStyle Style;
};

/** A Search Bar's recent searches, kept in a save slot (Recent Searches Save Slot). */
UCLASS()
class NAMETBD_API USearchBarHistorySave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FString> RecentSearches;
};

/** One row of the suggestions list. */
struct FSearchBarSuggestion
{
	FString Text;
	bool bRecent = false;
	int32 Score = 0;
};

/** The Search Bar's Slate root: tells the UMG widget when the keyboard focus comes into or leaves it. */
class NAMETBD_API SSearchBarHost : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSearchBarHost) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Called with true when the focus moves into this widget (or anything inside it), false when it leaves. */
	TFunction<void(bool)> OnFocusMoved;

	/** Widgets outside this one that still count as inside it (the suggestions popup). */
	TArray<TWeakPtr<SWidget>> AlsoInside;

	virtual void OnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent) override;

private:
	bool bHadFocus = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSearchBarChanged, const FString&, Query, FName, ActiveFilter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSearchBarCommitted, const FString&, Query);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSearchBarCleared);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSearchBarResultsChanged, int32, ResultCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSearchBarSuggestionPicked, const FString&, Suggestion);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSearchBarFocusChanged, bool, bHasFocus);

/**
 * A search bar that works the moment it is placed: type, and it filters its Results Widget (a List / Tile View's items, or a
 * panel's children) or tells you what was typed (On Search Changed). Drawn in a ready-made Look or your own Style (or a Search
 * Bar Style asset); suggests as you type, remembers recent searches, offers filter categories, counts results and shows a No
 * Results widget. Keyboard: up / down pick a suggestion, Enter or Tab take it, Escape closes the suggestions, then clears.
 */
UCLASS(meta = (DisplayName = "Search Bar"))
class NAMETBD_API USearchBar : public UWidget
{
	GENERATED_BODY()

public:
	USearchBar(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// ---------------------------------------------------------------------------------------------
	// Search
	// ---------------------------------------------------------------------------------------------

	/** What is typed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search")
	FText Text;

	/** Shown while nothing is typed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search")
	FText HintText;

	/** How what is typed is matched: fuzzy, contains, starts with, word starts or exact; each word separately; accents; case. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search")
	FSearchMatchOptions MatchOptions;

	/** Seconds after the last key before searching (filtering a long list on every key is wasted work). 0: at once. Enter searches at once. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search", meta = (ClampMin = "0", UIMax = "1", ForceUnits = "s"))
	float DebounceSeconds = 0.15f;

	/** Escape clears what is typed (after closing the suggestions). Escape on an empty bar is left to the screen - its Back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search")
	bool bClearOnEscape = true;

	/** Selects what is typed when the bar takes the focus, so typing replaces it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search")
	bool bSelectAllOnFocus = false;

	/** The clear button, shown while something is typed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search")
	bool bShowClearButton = true;

	// ---------------------------------------------------------------------------------------------
	// Filter
	// ---------------------------------------------------------------------------------------------

	/**
	 * Categories to narrow the search to, picked from a dropdown at the bar's end (shown when there are any): Key is matched
	 * against an item's Search Filter Category, Display Text shown. Set here or with Set Filter Options.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Filter", meta = (TitleProperty = "DisplayText"))
	TArray<FSearchFilterOption> FilterOptions;

	/** The catch-all first entry of the dropdown. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Filter")
	FText AllFilterLabel;

	// ---------------------------------------------------------------------------------------------
	// Results
	// ---------------------------------------------------------------------------------------------

	/**
	 * What this bar filters, by name: a List / Tile / Tree View (its items) or any panel (its children - a Wrap Box, a Vertical
	 * Box, a Uniform Grid, a Scroll Box), in this Widget Blueprint. None: nothing is filtered; use On Search Changed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Results", meta = (GetOptions = "GetWidgetOptions"))
	FName ResultsWidget;

	/** A List View's matches put in order, best first (off: their own order). A panel's children stay in place. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Results")
	bool bBestMatchFirst = true;

	/** A panel child that isn't a Searchable Item is found by all its text (every Text Block inside it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Results")
	bool bSearchInsideWidgets = true;

	/** Shown while something is searched and nothing matches ("No items match"), hidden otherwise. None: nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Results", meta = (GetOptions = "GetWidgetOptions"))
	FName NoResultsWidget;

	/** Shows how many match, inside the bar while searching. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Results")
	bool bShowResultCount = false;

	/** {0}: how many match. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Results", meta = (EditCondition = "bShowResultCount"))
	FText ResultCountFormat;

	// ---------------------------------------------------------------------------------------------
	// Suggestions
	// ---------------------------------------------------------------------------------------------

	/** A list under the bar while typing: matches to pick with the mouse, or up / down and Enter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Suggestions")
	bool bShowSuggestions = true;

	/** Suggests what the Results Widget holds that matches (its items' names). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Suggestions", meta = (EditCondition = "bShowSuggestions"))
	bool bSuggestResults = true;

	/** Suggests recent searches - all of them while nothing is typed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Suggestions", meta = (EditCondition = "bShowSuggestions"))
	bool bSuggestRecentSearches = true;

	/** Suggestions of your own (popular searches, item names), matched against what is typed. Also Set Suggestions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Suggestions", meta = (EditCondition = "bShowSuggestions"))
	TArray<FText> Suggestions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Suggestions", meta = (EditCondition = "bShowSuggestions", ClampMin = "1", UIMax = "12"))
	int32 MaxSuggestions = 6;

	/** Remembers what was searched (Enter, or a suggestion picked) for the suggestions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Suggestions")
	bool bRememberRecentSearches = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Suggestions", meta = (EditCondition = "bRememberRecentSearches", ClampMin = "1", UIMax = "20"))
	int32 MaxRecentSearches = 8;

	/** A save slot recent searches are kept in between sessions (one per bar - "InventorySearch"). Empty: this session only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Search|Suggestions", meta = (EditCondition = "bRememberRecentSearches"))
	FString RecentSearchesSaveSlot;

	// ---------------------------------------------------------------------------------------------
	// Look
	// ---------------------------------------------------------------------------------------------

	/** A ready-made look. Picking Custom starts Style from the look it was. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
	ESearchBarLook Look = ESearchBarLook::Dark;

	/** How it is drawn, with Look Custom. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look", meta = (DisplayName = "Style", EditCondition = "Look == ESearchBarLook::Custom && StyleAsset == nullptr"))
	FSearchBarStyle SearchStyle;

	/** A Search Bar Style asset to draw with instead - every search in a game alike. Wins over Look and Style. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Look")
	TObjectPtr<USearchBarStyleAsset> StyleAsset = nullptr;

	// ---------------------------------------------------------------------------------------------
	// Events
	// ---------------------------------------------------------------------------------------------

	/** What is typed or the filter changed (after the debounce): the query and the filter's Key (None: All). */
	UPROPERTY(BlueprintAssignable, Category = "Search|Events")
	FOnSearchBarChanged OnSearchChanged;

	/** Enter pressed, or a suggestion picked. */
	UPROPERTY(BlueprintAssignable, Category = "Search|Events")
	FOnSearchBarCommitted OnSearchCommitted;

	/** Cleared - the clear button, Escape, Clear Search. */
	UPROPERTY(BlueprintAssignable, Category = "Search|Events")
	FOnSearchBarCleared OnSearchCleared;

	/** How many of the Results Widget's items match changed. */
	UPROPERTY(BlueprintAssignable, Category = "Search|Events")
	FOnSearchBarResultsChanged OnResultsChanged;

	UPROPERTY(BlueprintAssignable, Category = "Search|Events")
	FOnSearchBarSuggestionPicked OnSuggestionPicked;

	/** The bar took the keyboard focus (true) or lost it. */
	UPROPERTY(BlueprintAssignable, Category = "Search|Events")
	FOnSearchBarFocusChanged OnSearchFocusChanged;

	// ---------------------------------------------------------------------------------------------
	// Blueprint
	// ---------------------------------------------------------------------------------------------

	/** Sets what is typed. bSearch: search for it now (and tell On Search Changed). */
	UFUNCTION(BlueprintCallable, Category = "Search")
	void SetText(const FText& InText, bool bSearch = true);

	UFUNCTION(BlueprintPure, Category = "Search")
	FText GetText() const { return Text; }

	/** What is typed, as a string. */
	UFUNCTION(BlueprintPure, Category = "Search")
	FString GetQuery() const { return Text.ToString(); }

	/** Empties the bar and the filter, and shows everything again - at once. */
	UFUNCTION(BlueprintCallable, Category = "Search")
	void ClearSearch();

	/** Puts the keyboard focus in the bar, ready to type (a "/" or Ctrl+F shortcut of your screen calls this). */
	UFUNCTION(BlueprintCallable, Category = "Search")
	void FocusSearch();

	UFUNCTION(BlueprintPure, Category = "Search")
	bool HasSearchFocus() const;

	/** The filter dropdown's categories; empty hides it. */
	UFUNCTION(BlueprintCallable, Category = "Search|Filter")
	void SetFilterOptions(const TArray<FSearchFilterOption>& InOptions);

	/** Picks a filter by Key (None: All), and searches. */
	UFUNCTION(BlueprintCallable, Category = "Search|Filter")
	void SetActiveFilter(FName FilterKey);

	/** The filter picked, or None for All. */
	UFUNCTION(BlueprintPure, Category = "Search|Filter")
	FName GetActiveFilter() const { return ActiveFilter; }

	/** Suggestions of your own (replaces Suggestions). */
	UFUNCTION(BlueprintCallable, Category = "Search|Suggestions")
	void SetSuggestions(const TArray<FText>& InSuggestions);

	UFUNCTION(BlueprintCallable, Category = "Search|Suggestions")
	void AddRecentSearch(const FString& InQuery);

	UFUNCTION(BlueprintCallable, Category = "Search|Suggestions")
	void ClearRecentSearches();

	UFUNCTION(BlueprintPure, Category = "Search|Suggestions")
	TArray<FString> GetRecentSearches() const { return RecentSearches; }

	/** Filters this widget instead of Results Widget - one made in code, or in another Widget Blueprint. Null goes back to Results Widget. */
	UFUNCTION(BlueprintCallable, Category = "Search|Results")
	void SetResultsWidgetObject(UWidget* InWidget);

	/**
	 * The items a List View Results Widget is filtered from. Taken from the list itself when a search starts; give them here
	 * when your code changes the list's items while a search is showing (otherwise clearing the search puts back the old ones).
	 */
	UFUNCTION(BlueprintCallable, Category = "Search|Results")
	void SetSearchSourceItems(const TArray<UObject*>& InItems);

	/** Searches the Results Widget again - after its items or children changed. */
	UFUNCTION(BlueprintCallable, Category = "Search|Results")
	void RefreshResults();

	/** How many of the Results Widget's items match; -1 with no Results Widget. */
	UFUNCTION(BlueprintPure, Category = "Search|Results")
	int32 GetResultCount() const { return ResultCount; }

	/** Whether Candidate matches what is typed, as this bar matches (OutScore ranks it). */
	UFUNCTION(BlueprintPure, Category = "Search", meta = (ReturnDisplayName = "Matches"))
	bool MatchesQuery(const FString& Candidate, int32& OutScore) const;

	/** Candidate as rich text, what is typed highlighted with <StyleName> (Search library's Highlight Matches, this bar's options). */
	UFUNCTION(BlueprintPure, Category = "Search")
	FString HighlightQuery(const FString& Candidate, const FString& StyleName = TEXT("Match")) const;

	/** Puts a ready-made look on the bar (Custom keeps Style). */
	UFUNCTION(BlueprintCallable, Category = "Look")
	void SetLook(ESearchBarLook InLook);

	UFUNCTION(BlueprintCallable, Category = "Look")
	void SetStyleAsset(USearchBarStyleAsset* InStyleAsset);

	/** The widget names offered for Results Widget and No Results Widget. */
	UFUNCTION()
	TArray<FString> GetWidgetOptions() const;

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** The style drawn with: the asset's, the look's, or Style. */
	const FSearchBarStyle& GetActiveStyle() const;

	void HandleTextChanged(const FText& NewText);
	void HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitMethod);
	FReply HandleKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent);
	void HandleFocusMoved(bool bHasFocus);

	void ScheduleSearch();
	void SearchNow();

	/** Something is searched: enough typed, or a filter picked. */
	bool IsSearching() const;
	bool IsQueryActive() const;
	bool PassesFilter(FName Category) const;

	/** The widget filtered: Set Results Widget Object's, else Results Widget by name. */
	UWidget* FindResultsWidget() const;

	/** Filters the results widget; the number that match (-1: none to filter). */
	int32 ApplyToResults();
	int32 FilterList(UListView& List);
	int32 FilterPanel(UPanelWidget& Panel);
	void RestoreResults();
	void UpdateNoResults(int32 Count);

	/** What an item is found by: its Search Text and keywords, or its text. OutCategory its Search Filter Category. */
	void GetItemTexts(UObject* Item, TArray<FString>& OutTexts, FName& OutCategory, bool bLabelOnly) const;

	// Suggestions
	void UpdateSuggestions();
	void OpenSuggestions(bool bOpen);
	void MoveSuggestionHighlight(int32 Direction);
	void PickSuggestion(const TSharedPtr<FSearchBarSuggestion>& Suggestion);
	TSharedRef<class ITableRow> MakeSuggestionRow(TSharedPtr<FSearchBarSuggestion> Item, const TSharedRef<class STableViewBase>& OwnerTable);
	TSharedRef<SWidget> MakeHighlightedText(const FString& Value, bool bRecent) const;

	// Filter dropdown
	void RebuildFilterItems();
	TSharedRef<SWidget> MakeFilterRow(TSharedPtr<FSearchFilterOption> Option) const;
	void HandleFilterPicked(TSharedPtr<FSearchFilterOption> Option, ESelectInfo::Type SelectInfo);
	FText GetActiveFilterLabel() const;

	// Recent searches
	void LoadRecentSearches();
	void SaveRecentSearches() const;

	FText GetResultCountText() const;

	FString CurrentQuery;
	FName ActiveFilter = NAME_None;
	int32 ResultCount = -1;
	bool bFocused = false;
	bool bRecentLoaded = false;
	FTimerHandle DebounceTimer;

	TArray<FString> RecentSearches;
	TArray<TSharedPtr<FSearchBarSuggestion>> SuggestionItems;
	int32 HighlightedSuggestion = INDEX_NONE;
	TArray<TSharedPtr<FSearchFilterOption>> FilterItems;

	/** Panel children this bar hid, and the visibility each had. */
	TMap<TWeakObjectPtr<UWidget>, ESlateVisibility> HiddenChildren;

	/** The list filtered, and the items it is filtered from. */
	TWeakObjectPtr<UListView> FilteredList;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> SourceItems;
	bool bHasSourceItems = false;

	TWeakObjectPtr<UPanelWidget> FilteredPanel;
	TWeakObjectPtr<UWidget> ResultsOverride;

	/** Set Text is putting text in the field: its change isn't the player typing. */
	bool bSettingText = false;

	TSharedPtr<SSearchBarHost> SlateHost;
	TSharedPtr<SEditableText> SlateText;
	TSharedPtr<SMenuAnchor> SlateAnchor;
	TSharedPtr<SWidget> SlatePopup;
	TSharedPtr<SListView<TSharedPtr<FSearchBarSuggestion>>> SlateSuggestions;
	TSharedPtr<SComboBox<TSharedPtr<FSearchFilterOption>>> SlateFilter;
	TSharedPtr<class SBox> SlateSearchIcon;
	TSharedPtr<class SBox> SlateClearIcon;

	/** The look before an edit of Look (Custom then starts from it). */
	ESearchBarLook LookBeforeEdit = ESearchBarLook::Dark;

	/** The Look's style, made when first drawn with it. */
	mutable FSearchBarStyle LookStyle;
	mutable ESearchBarLook LookStyleMadeFor = ESearchBarLook::Custom;
};
