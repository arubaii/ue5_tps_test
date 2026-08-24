// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Item/Fragment/ItemFragment.h"
#include "UObject/Object.h"
#include "ItemAction.generated.h"

/**
 * 
 */
UCLASS(Blueprintable, BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class TPS_TEST_API UItemAction : public UObject
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
	void ExecuteItemAction(AActor* User, const FUsableFragment& FragmentData);
	virtual void ExecuteItemAction_Implementation(AActor* User, const FUsableFragment& FragmentData) {};	
};
