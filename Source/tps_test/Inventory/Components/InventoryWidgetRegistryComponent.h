#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Inventory/Components/InventoryComponent.h"
#include "InventoryWidgetRegistryComponent.generated.h"

/**
 * Add to the PlayerState when using the inventory system for W_Inventory to work
 * properly. Class Defaults -> Add Component -> Inventory Widget Registry Component.
 * No interface to implement, no functions to write — this class already does the work.
 *
 * Holds {InventoryComponent -> the widget currently displaying it}, which is what lets
 * something elsewhere in the game ask "is this inventory's UI already open, and if so,
 * which widget" without W_Inventory having to expose itself globally.
 */
UCLASS(ClassGroup = (Inventory), meta = (BlueprintSpawnableComponent))
class TPS_TEST_API UInventoryWidgetRegistryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RegisterWidgetForInventory(UInventoryComponent* InventoryComponent, UUserWidget* InventoryWidget);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void DeregisterInventoryWidget(UInventoryComponent* InventoryComponent);

	/**
	 * Currently-open widget for a given inventory, or null if none is registered.
	 * OutFound mirrors Map_Find's own Found output — needed because a null InventoryWidget
	 * is itself ambiguous (a widget that was registered as null vs. a key that was never
	 * registered at all), same reason Map_Find doesn't just rely on the return value being
	 * non-null.
	 */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	UUserWidget* ResolveWidgetForInventory(UInventoryComponent* InventoryComponent, bool& OutFound) const;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	TMap<TObjectPtr<UInventoryComponent>, TObjectPtr<UUserWidget>> InventoryWidgets;
};