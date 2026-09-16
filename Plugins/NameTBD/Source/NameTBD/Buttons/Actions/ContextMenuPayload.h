#pragma once

#include "CoreMinimal.h"

#include "GameplayTagContainer.h"
#include "NameTBD/Cards/InfoCardTypes.h"
#include "NameTBD/Buttons/ContextMenuPresetDataAsset.h"

#include "ContextMenuPayload.generated.h"



UCLASS(NotBlueprintable, DisplayName = "Tag Payload")
class NAMETBD_API UContextMenuPayload : public UNameTBD_Payload
{
	GENERATED_BODY()

public:
	
	/** 
	 * The resulting context menu will be built in exact order of the buttons in the array from top to bottom.
	 * If a tag is selected, but it is not implemented in the developer settings, it is skipped. 
	*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	TObjectPtr<UContextMenuPresetDataAsset> ContextMenuPreset; 
	
	/** Used only if the data asset is not set.
	 * 
	 *  The resulting context menu will be built in exact order of the buttons in the array from top to bottom.
		If a tag is selected, but it is not implemented in the developer settings, it is skipped. 
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Payload", 
		meta = (Categories = "UIContext.Button", EditCondition = "ContextMenuPreset == nullptr"))
	TArray<FGameplayTag> ContextButtons;

	/**
	 * What each button above resolves to: its label, the actions that will run, and the payload classes
	 * those actions expect to be handed.
	 *
	 * This is here because picking buttons and knowing what they need are the same job, and the second
	 * half otherwise lives in Project Settings > Info Card > Button Actions - a different window, opened
	 * once per tag, every time. Whatever this lists under "Expected payloads" has to be present in the
	 * Payloads of the card this menu spawns, or those slots reach the action null.
	 *
	 * Rebuilt whenever this payload is edited or loaded. Read-only and transient; nothing reads it back.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Payload", meta = (DisplayName = "Required Payloads", MultiLine = true))
	FString RequiredPayloadsSummary;

	/** Rebuilds Required Payloads Summary from the current button list. Cheap; safe to call at any time. */
	void RefreshRequiredPayloadsSummary();

	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};