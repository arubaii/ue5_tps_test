// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "SMenuControls.h"

#include "MenuDialog.generated.h"

class APlayerController;
class SMenuDialog;
class SWidget;
class UMenuStyleAsset;

/** How a dialog button looks. */
UENUM(BlueprintType)
enum class EMenuDialogButtonStyle : uint8
{
	Primary UMETA(ToolTip = "Filled in the accent: the expected answer."),
	Secondary UMETA(ToolTip = "Outlined."),
	Subtle UMETA(ToolTip = "Text only (Cancel).")
};

/** One button of a menu dialog. */
USTRUCT(BlueprintType)
struct NAMETBD_API FMenuDialogButtonSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog")
	FText Text;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog")
	EMenuDialogButtonStyle Style = EMenuDialogButtonStyle::Secondary;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMenuDialogChosen, int32, ButtonIndex, FText, ButtonText);

/**
 * Show Menu Dialog: a modal question on the player's screen in the menu style ("Quit to desktop?", "Overwrite this save?") -
 * a title, a body and buttons; pads and keys work (Back picks the Cancel button), focus returns where it was afterwards. With a
 * timeout it counts down and picks the Timeout Button by itself.
 */
UCLASS(meta = (DisplayName = "Show Menu Dialog"))
class NAMETBD_API UAsyncAction_ShowMenuDialog : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:

	/**
	 * Buttons left to right. Default Button gets focus; Cancel Button is taken by Back (-1: Back does nothing). Timeout Seconds > 0
	 * picks Timeout Button when time runs out (the body says how long is left). Style: empty, the project's Default Style.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu|Dialog", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "Buttons"))
	static UAsyncAction_ShowMenuDialog* ShowMenuDialog(UObject* WorldContextObject, APlayerController* Player, FText Title, FText Body,
		const TArray<FMenuDialogButtonSpec>& Buttons, int32 DefaultButton = 0, int32 CancelButton = -1, UMenuStyleAsset* StyleAsset = nullptr,
		float TimeoutSeconds = 0.f, int32 TimeoutButton = -1);

	virtual void Activate() override;

	/** Which button was pressed (or taken by Back / the timeout). */
	UPROPERTY(BlueprintAssignable)
	FOnMenuDialogChosen OnChosen;

	/** Closes the dialog without an answer (On Chosen fires with -1). */
	UFUNCTION(BlueprintCallable, Category = "Menu|Dialog")
	void Cancel();

private:
	void Finish(int32 Index);
	bool Tick(float DeltaTime);

	TWeakObjectPtr<UObject> Context;
	TWeakObjectPtr<APlayerController> PlayerController;
	FText DialogTitle;
	FText DialogBody;
	TArray<FMenuDialogButtonSpec> ButtonSpecs;
	int32 DefaultIndex = 0;
	int32 CancelIndex = INDEX_NONE;
	int32 TimeoutIndex = INDEX_NONE;
	float TimeLeft = 0.f;
	bool bFinished = false;

	UPROPERTY(Transient)
	TObjectPtr<UMenuStyleAsset> Style;

	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	TSharedPtr<SWidget> DialogWidget;
	TWeakPtr<SWidget> PreviousFocus;
	FTSTicker::FDelegateHandle TickerHandle;
	int32 QueuedChoice = INDEX_NONE;
	bool bChoiceQueued = false;
};
