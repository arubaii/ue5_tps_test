#pragma once

#include "CoreMinimal.h"
#include "ContextMenuPayload.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"

#include "NameTBD/Cards/InfoCardTypes.h"
#include "NameTBD/Buttons/ContextMenuPresetDataAsset.h"
#include "ContextMenuInterface.generated.h"

class UContextMenuButton;

/**
 * Data handed to a button's actions when it is clicked.
 *
 * One of these per thing an action needs to know. A tag's entry in Project Settings > Info Card >
 * Button Actions lists the CLASSES it expects; the INSTANCES come from the info card the button lives
 * on, which is where a designer (or a Blueprint, at runtime) fills the values in. So "Delete Item"
 * names the payload class it wants, and each card that carries a Delete Item button supplies one
 * pointing at whichever item that card is about.
 *
 * Subclass this to add fields. The button matches by IsA, so a subclass satisfies an expectation of
 * its base.
 */
UCLASS(DisplayName = "Context Button Payload")
class NAMETBD_API UContextButtonPayload : public UNameTBD_Payload
{
	GENERATED_BODY()

public:
	
};




UCLASS(DisplayName = "Context Button Payload (Text)")
class NAMETBD_API UContextButtonPayloadText : public UContextButtonPayload
{
	GENERATED_BODY()
	
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload")
	FText Text = FText::GetEmpty();
};



UINTERFACE(BlueprintType)
class UContextMenuInterface : public UInterface
{
	GENERATED_BODY()
};

class NAMETBD_API IContextMenuInterface
{
	GENERATED_BODY()

public:

	/**
	 * What a button actually does when it is clicked.
	 *
	 * Implemented by subclassing UButtonAction, which is what a Button Actions entry lists. The button
	 * instantiates each listed class once and calls this on every one of them, in the order they were
	 * listed.
	 *
	 * @param Button          The button that was clicked. Its Button Tag says which entry ran, and
	 *                        Get Owning Card reaches the card it came from.
	 * @param ActionPayloads  The payload instances, index-for-index with that entry's Expected Payloads.
	 *                        A slot whose class the card did not supply is NULL rather than absent, so
	 *                        index N is always the Nth expected class - check before using it. The button
	 *                        logs a warning for each missing one.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Context Menus")
	void ExecuteAction(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads);
};