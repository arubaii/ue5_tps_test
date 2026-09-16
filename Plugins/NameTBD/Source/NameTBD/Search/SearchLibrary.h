// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SearchLibrary.generated.h"

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

private:
	/** Shared core. bScoreOnly callers skip the bookkeeping they won't read. */
	static bool MatchInternal(const FString& Query, const FString& Candidate, int32& OutScore);
};
