# Search

## Search Bar
SearchBar (palette NameTBD) is a finished search field: no Widget Blueprint, no named parts. It draws an icon, the field with its HintText, a clear button, a filter dropdown (FilterOptions [{Key, DisplayText}], AllFilterLabel) and a result count (bShowResultCount, ResultCountFormat). Look: Dark (default), Light, Pill, Minimal, or Custom (SearchStyle: Background / HoveredBackground / FocusedBackground brushes, Padding, Font, TextColor, HintColor, SearchIcon / ClearIcon (texture or SVG), IconSize, IconColor, FocusedIconColor, PopupBackground, SuggestionHighlight, MatchColor). StyleAsset (SearchBarStyleAsset) shares one look across a game.

## Filtering with no Blueprint
ResultsWidget names a widget of the same Widget Blueprint: a ListView / TileView (its items are filtered; bBestMatchFirst ranks them) or any panel - WrapBox, VerticalBox, UniformGridPanel, ScrollBox (non-matching children are collapsed and given back their visibility later). NoResultsWidget names a widget shown only while nothing matches. An item is found by its Search Text and keywords when it implements the Searchable Item interface (GetSearchText, GetSearchFilterCategory - matched against the filter Key, GetSearchKeywords); otherwise a panel child by all its Text Blocks (bSearchInsideWidgets) and a list item by a DisplayName / Name / ItemName / Title / Label / Text property.

## Matching
MatchOptions (FSearchMatchOptions): MatchMode Fuzzy (letters in order: 'hpot' finds Health Potion), Contains, StartsWith, WordStarts, Exact; bMatchEveryWord (each word anywhere, all needed), bIgnoreAccents, bCaseSensitive, MinQueryLength, bSearchKeywords. DebounceSeconds (0.15). The Search library has the same for any code: Search Match, Search Filter Ranked, Get Match Ranges, Highlight Matches (rich text with <Match> around what matched), Filter Searchable Objects With Options.

## Suggestions and recent searches
bShowSuggestions: a list under the bar while typing - matches from the results (bSuggestResults), Suggestions (your own), recent searches (bSuggestRecentSearches; shown on focus when empty). Up / down and Enter / Tab pick; Escape closes, then clears (bClearOnEscape). bRememberRecentSearches, MaxRecentSearches, RecentSearchesSaveSlot (kept between sessions).

## Events and functions
On Search Changed (Query, ActiveFilter), On Search Committed, On Search Cleared, On Results Changed (count), On Suggestion Picked, On Search Focus Changed. Set Text, Clear Search, Focus Search, Set Filter Options, Set Active Filter, Set Suggestions, Refresh Results (after the items changed), Set Search Source Items, Get Result Count, Matches Query, Highlight Query. Example spec: {class: "SearchBar", name: "InventorySearch", props: {ResultsWidget: "ItemGrid", NoResultsWidget: "NoItemsText", HintText: "Search items", Look: "Pill"}}.

## Hand-built search bars
W_SearchBarBase is for a search bar laid out entirely by hand (a Widget Blueprint with an Editable Text Box named SearchText, optional ComboBoxString FilterBox and Button ClearButton). Prefer SearchBar.
