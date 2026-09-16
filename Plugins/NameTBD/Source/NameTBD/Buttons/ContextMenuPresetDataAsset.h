#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTags.h"
#include "ContextMenuPresetDataAsset.generated.h"


UCLASS(BlueprintType)
class UContextMenuPresetDataAsset : public UDataAsset
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TArray<FGameplayTag> Buttons;
};
