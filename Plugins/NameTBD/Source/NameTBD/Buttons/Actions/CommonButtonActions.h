// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "ButtonAction.h"

#include "CommonButtonActions.generated.h"

class USoundBase;

/** Which object Call Function calls its function on. */
UENUM(BlueprintType)
enum class EButtonActionFunctionTarget : uint8
{
	SourceWidget UMETA(DisplayName = "Source Widget", ToolTip = "The User Widget the menu's anchor sits in (the inventory slot, the list row). For a plain button: the User Widget the button sits in."),
	OwningPlayer UMETA(DisplayName = "Owning Player", ToolTip = "The player controller that owns the widget."),
	OwningPawn UMETA(DisplayName = "Owning Pawn", ToolTip = "The pawn of the player controller that owns the widget.")
};

/** Runs a console command (exec) as the owning player: "stat fps", "open MainMenu", a cheat, any Exec function. */
UCLASS(DisplayName = "Run Console Command")
class NAMETBD_API UButtonAction_ConsoleCommand : public UButtonAction
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	FString Command;

	virtual void ExecuteMenuAction_Implementation(const FContextMenuActionContext& Context) override;
	virtual void ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads) override;

protected:

	void Run(UObject* WorldContext) const;
};

/** Copies text to the clipboard: Text, or the first Context Button Payload (Text) the card carries. */
UCLASS(DisplayName = "Copy Text")
class NAMETBD_API UButtonAction_CopyText : public UButtonAction
{
	GENERATED_BODY()

public:

	/** Copy the card's first Context Button Payload (Text) instead of Text (falls back to Text when there is none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (DisplayName = "Use Payload Text"))
	bool bUsePayloadText = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (MultiLine = true))
	FText Text;

	virtual void ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads) override;
};

/**
 * Calls a function or custom event by name - the quickest way to hook a row up to Blueprint: add a custom event called, say,
 * "DropItem" to the widget the anchor sits in, and type DropItem here. The function takes no parameters, or one Name (it
 * gets the row's Entry ID). A function that doesn't exist, or wants other parameters, is reported in the log.
 */
UCLASS(DisplayName = "Call Function")
class NAMETBD_API UButtonAction_CallFunction : public UButtonAction
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	EButtonActionFunctionTarget Target = EButtonActionFunctionTarget::SourceWidget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	FName FunctionName = NAME_None;

	virtual void ExecuteMenuAction_Implementation(const FContextMenuActionContext& Context) override;
	virtual void ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads) override;

protected:

	void CallOn(UObject* Receiver, FName EntryId) const;
};

/** Opens a web page in the system browser: a store page, the wiki, a bug report form. */
UCLASS(DisplayName = "Open URL")
class NAMETBD_API UButtonAction_OpenURL : public UButtonAction
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (DisplayName = "URL"))
	FString URL;

	virtual void ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads) override;
};

/** Plays a sound (2D, UI). */
UCLASS(DisplayName = "Play Sound")
class NAMETBD_API UButtonAction_PlaySound : public UButtonAction
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action")
	TObjectPtr<USoundBase> Sound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (ClampMin = "0"))
	float VolumeMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Action", meta = (ClampMin = "0.01"))
	float PitchMultiplier = 1.f;

	virtual void ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads) override;
};
