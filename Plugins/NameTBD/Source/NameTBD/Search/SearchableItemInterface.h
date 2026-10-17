// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SearchableItemInterface.generated.h"

UINTERFACE(BlueprintType)
class NAMETBD_API USearchableItemInterface : public UInterface
{
	GENERATED_BODY()
};

class NAMETBD_API ISearchableItemInterface
{
	GENERATED_BODY()

public:
	/** What string should the fuzzy search compare against? (e.g., Quest Name, Item Name) */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Search")
	FString GetSearchText() const;

	/** What category does this item belong to? (Matches the Dropdown Key). Return NAME_None if unused. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Search")
	FName GetSearchFilterCategory() const;

	/**
	 * More words this item is found by, beside its Search Text: synonyms, tags, its type ("sword" for Excalibur, "heal" for a
	 * potion). Matched when the search's Search Keywords option is on. None by default.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Search")
	TArray<FString> GetSearchKeywords() const;
	virtual TArray<FString> GetSearchKeywords_Implementation() const { return TArray<FString>(); }
};