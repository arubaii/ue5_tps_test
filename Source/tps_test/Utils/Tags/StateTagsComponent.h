#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "StateTagsComponent.generated.h"

UCLASS( ClassGroup=(Tags), meta=(BlueprintSpawnableComponent) )
class TPS_TEST_API UStateTagsComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UStateTagsComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Tags")
	FGameplayTagContainer StateTags;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};