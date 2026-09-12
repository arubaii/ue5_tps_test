// Copyright AR GameSystems 2026. All rights reserved.

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
};