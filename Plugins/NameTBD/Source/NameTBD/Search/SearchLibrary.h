// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SearchLibrary.generated.h"

/** How a query is matched against a candidate. */
UENUM(BlueprintType)
enum class ESearchMatchMode : uint8
{
	Fuzzy UMETA(ToolTip = "Every typed letter, in order, anywhere in it: 'hpot' finds Health Potion. Ranked so the starts of words and unbroken runs of letters come first - what search-as-you-type usually means."),
	Contains UMETA(ToolTip = "The typed text, unbroken, anywhere in it: 'tion' finds Potion."),
	StartsWith UMETA(DisplayName = "Starts With", ToolTip = "It starts with the typed text."),
	WordStarts UMETA(DisplayName = "Word Starts", ToolTip = "A word in it starts with the typed text (CamelCase words too): 'pot' finds Health Potion, not Teapot."),
	Exact UMETA(ToolTip = "Exactly the typed text (case and accents as the other options say). Each Word Separately doesn't apply.")
};

/** How a search matches - shared by the Search Bar, the Search library and the filters built on it. */
USTRUCT(BlueprintType)
struct NAMETBD_API FSearchMatchOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search")
	ESearchMatchMode MatchMode = ESearchMatchMode::Fuzzy;

	/** Each word typed is looked for on its own, in any order, and all must be found: 'red pot' finds Potion (Red). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (DisplayName = "Each Word Separately"))
	bool bMatchEveryWord = true;

	/** 'e' finds 'é', 'u' finds 'ü': accented Latin letters match their plain ones, both ways. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search")
	bool bIgnoreAccents = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search")
	bool bCaseSensitive = false;

	/** A query shorter than this (spaces aside) matches everything - nothing is filtered until enough is typed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search", meta = (ClampMin = "0", UIMax = "5"))
	int32 MinQueryLength = 0;

	/** Also matches the keywords a searchable item gives (Get Search Keywords: synonyms, tags, an item's type). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Search")
	bool bSearchKeywords = true;
};

/**
 * Text matching with no idea what it's matching against. Nothing here knows about items,
 * quests, or any other domain — callers turn their own objects into a label string and
 * pass that in, which is what lets one search bar serve an inventory, a quest log, and a
 * settings menu without any of them touching this file.
 *
 * The algorithm is subsequence matching, not edit distance: every character of the query
 * must appear in the candidate in order, though not necessarily adjacently. "hp" matches
 * "Health Potion", "poti" matches "Potion", "hlth" matches "Health". This is what most
 * search-as-you-type UI actually uses — it's cheap enough to run against a full item list
 * on every keystroke, and it degrades gracefully as the player types more, which an
 * edit-distance threshold does not.
 */
UCLASS()
class NAMETBD_API USearchLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Does Candidate contain every character of Query, in order? Case-insensitive.
	 *
	 * An empty query matches everything, so an empty search box shows the full list
	 * rather than nothing — callers don't need to special-case "user hasn't typed yet".
	 */
	UFUNCTION(BlueprintPure, Category = "Search", meta = (ReturnDisplayName = "Matches"))
	static bool FuzzyMatch(const FString& Query, const FString& Candidate);

	/**
	 * FuzzyMatch plus a relevance score, for ranking results rather than only filtering
	 * them. Higher is better; OutScore is meaningless when the return value is false.
	 *
	 * Scoring rewards the things that make a match feel "obviously what I meant":
	 * consecutive matched characters, matches at the start of the candidate, and matches
	 * at word boundaries. So for the query "hp", "HP Serum" outranks "Health Potion",
	 * which in turn outranks "Alchemist's Sharpening Paste" — all three match, but they
	 * don't match equally well.
	 */
	UFUNCTION(BlueprintPure, Category = "Search", meta = (ReturnDisplayName = "Matches"))
	static bool FuzzyMatchWithScore(const FString& Query, const FString& Candidate, int32& OutScore);

	/**
	 * Filter and rank a list of labels in one call, returning the indices of matching
	 * entries ordered best-first.
	 *
	 * Indices rather than strings, because the caller's real data is whatever the labels
	 * were derived FROM — an array of item entries, quests, settings rows. Handing back
	 * matched strings would force the caller to look them up again; handing back indices
	 * lets it address its own array directly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Search")
	static void FuzzyFilterRanked(const FString& Query, const TArray<FString>& Candidates,
	                              TArray<int32>& OutMatchingIndices);
	
	/**
	 * Simple Blueprint node to filter and sort any array of objects.
	 * 
	 * @param Query The text typed by the user.
	 * @param ActiveFilter The dropdown category selected by the user.
	 * @param UnfilteredObjects Your master array of objects (Must implement ISearchableItemInterface).
	 * @param OutFilteredObjects The resulting array, filtered and sorted best-match first.
	 */
	UFUNCTION(BlueprintCallable, Category = "Search")
	static void FilterSearchableObjects(const FString& Query, FName ActiveFilter, const TArray<UObject*>& UnfilteredObjects, TArray<UObject*>& OutFilteredObjects);

	/**
	 * Does Candidate match Query, the way Options say (match mode, each word separately, accents, case)? OutScore ranks it:
	 * higher is better, meaningless when it doesn't match. A query shorter than Min Query Length matches everything.
	 */
	UFUNCTION(BlueprintPure, Category = "Search", meta = (ReturnDisplayName = "Matches", DisplayName = "Search Match"))
	static bool MatchWithOptions(const FString& Query, const FString& Candidate, const FSearchMatchOptions& Options, int32& OutScore);

	/** Filters a list of labels as Options say, returning the indices of the matches - best first when bBestFirst, else in their own order. */
	UFUNCTION(BlueprintCallable, Category = "Search", meta = (DisplayName = "Search Filter Ranked"))
	static void FilterRankedWithOptions(const FString& Query, const TArray<FString>& Candidates, const FSearchMatchOptions& Options, bool bBestFirst,
		TArray<int32>& OutMatchingIndices);

	/**
	 * Where Query matched in Candidate: each range's start (X) and length (Y), in order, not overlapping - to highlight what
	 * matched in a result. Empty when it doesn't match (or the query is empty).
	 */
	UFUNCTION(BlueprintPure, Category = "Search")
	static TArray<FIntPoint> GetMatchRanges(const FString& Query, const FString& Candidate, const FSearchMatchOptions& Options);

	/**
	 * Candidate as rich text with what Query matched wrapped in <StyleName>...</> - for a Rich Text Block whose style set has a
	 * row named StyleName (bold, or the accent colour). Every '<' and '&' of Candidate is escaped, so text that looks like a tag
	 * is shown as it is.
	 */
	UFUNCTION(BlueprintPure, Category = "Search")
	static FString HighlightMatches(const FString& Query, const FString& Candidate, const FSearchMatchOptions& Options, const FString& StyleName = TEXT("Match"));

	/** Text as a search compares it: accents folded and lower-cased as Options say (one character for one, so indices still line up). */
	UFUNCTION(BlueprintPure, Category = "Search")
	static FString NormalizeForSearch(const FString& Text, const FSearchMatchOptions& Options);

	/**
	 * Filter Searchable Objects, matched as Options say, against each object's Search Text and (Search Keywords on) its Search
	 * Keywords. bBestFirst ranks the matches; off keeps the objects' own order.
	 */
	UFUNCTION(BlueprintCallable, Category = "Search")
	static void FilterSearchableObjectsWithOptions(const FString& Query, FName ActiveFilter, const TArray<UObject*>& UnfilteredObjects,
		const FSearchMatchOptions& Options, bool bBestFirst, TArray<UObject*>& OutFilteredObjects);

	/** The best match of Query among several texts of one item (its label and its keywords). False when none matches. */
	static bool MatchAny(const FString& Query, TConstArrayView<FString> Texts, const FSearchMatchOptions& Options, int32& OutScore);

private:
	/** One term against one candidate (both normalized; Original keeps the candidate's case for CamelCase words). Ranges appended when given. */
	static bool MatchTerm(const FString& Term, const FString& Normalized, const FString& Original, ESearchMatchMode Mode, int32& OutScore, TArray<FIntPoint>* OutRanges);

	/** The whole query against a candidate - the terms, the score, the ranges (merged). */
	static bool MatchQuery(const FString& Query, const FString& Candidate, const FSearchMatchOptions& Options, int32& OutScore, TArray<FIntPoint>* OutRanges);

	/** Shared core. bScoreOnly callers skip the bookkeeping they won't read. */
	static bool MatchInternal(const FString& Query, const FString& Candidate, int32& OutScore);
};
