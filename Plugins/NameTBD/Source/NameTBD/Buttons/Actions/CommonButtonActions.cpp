// Copyright AR GameSystems 2026. All rights reserved.

#include "CommonButtonActions.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/UnrealType.h"

#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Actions/ContextMenuInterface.h"
#include "NameTBD/Cards/ContextMenu/ContextMenuWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogButtonActions, Log, All);

namespace CommonButtonActionsPrivate
{
	APlayerController* FindPlayer(const UObject* From)
	{
		if (const UWidget* Widget = Cast<UWidget>(From))
		{
			if (APlayerController* Owner = Widget->GetOwningPlayer())
			{
				return Owner;
			}
		}
		UWorld* World = From ? From->GetWorld() : nullptr;
		return World ? World->GetFirstPlayerController() : nullptr;
	}
}

// =================================================================================================
// Run Console Command
// =================================================================================================

void UButtonAction_ConsoleCommand::Run(UObject* WorldContext) const
{
	if (Command.IsEmpty())
	{
		return;
	}

	if (APlayerController* Player = CommonButtonActionsPrivate::FindPlayer(WorldContext))
	{
		Player->ConsoleCommand(Command, /*bWriteToLog=*/true);
	}
}

void UButtonAction_ConsoleCommand::ExecuteMenuAction_Implementation(const FContextMenuActionContext& Context)
{
	Run(Context.Menu ? static_cast<UObject*>(Context.Menu) : static_cast<UObject*>(Context.Button));
}

void UButtonAction_ConsoleCommand::ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads)
{
	Run(Button);
}

// =================================================================================================
// Copy Text
// =================================================================================================

void UButtonAction_CopyText::ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads)
{
	FString ToCopy = Text.ToString();
	if (bUsePayloadText)
	{
		if (const UContextButtonPayloadText* Payload = Cast<UContextButtonPayloadText>(FindActionPayload(ActionPayloads, UContextButtonPayloadText::StaticClass())))
		{
			ToCopy = Payload->Text.ToString();
		}
	}

	FPlatformApplicationMisc::ClipboardCopy(*ToCopy);
}

// =================================================================================================
// Call Function
// =================================================================================================

void UButtonAction_CallFunction::CallOn(UObject* Receiver, FName EntryId) const
{
	if (!Receiver || FunctionName.IsNone())
	{
		return;
	}

	UFunction* Function = Receiver->FindFunction(FunctionName);
	if (!Function)
	{
		UE_LOG(LogButtonActions, Warning, TEXT("Call Function: %s has no function or custom event named '%s'."), *Receiver->GetName(), *FunctionName.ToString());
		return;
	}

	int32 InputCount = 0;
	const FProperty* OnlyInput = nullptr;
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		if (!It->HasAnyPropertyFlags(CPF_ReturnParm) && !It->HasAnyPropertyFlags(CPF_OutParm))
		{
			++InputCount;
			OnlyInput = *It;
		}
	}

	const bool bTakesEntryId = InputCount == 1 && OnlyInput && OnlyInput->IsA<FNameProperty>();
	if (InputCount > 1 || (InputCount == 1 && !bTakesEntryId))
	{
		UE_LOG(LogButtonActions, Warning, TEXT("Call Function: '%s' on %s must take no parameters, or one Name (the Entry ID)."), *FunctionName.ToString(), *Receiver->GetName());
		return;
	}

	// Always through a parameter buffer when the function has one at all: a return value or an output is written into it.
	if (Function->ParmsSize == 0)
	{
		Receiver->ProcessEvent(Function, nullptr);
		return;
	}

	uint8* Parameters = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
	FMemory::Memzero(Parameters, Function->ParmsSize);
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(Parameters);
	}

	if (bTakesEntryId)
	{
		CastField<FNameProperty>(OnlyInput)->SetPropertyValue_InContainer(Parameters, EntryId);
	}

	Receiver->ProcessEvent(Function, Parameters);

	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(Parameters);
	}
}

void UButtonAction_CallFunction::ExecuteMenuAction_Implementation(const FContextMenuActionContext& Context)
{
	UObject* Receiver = nullptr;
	APlayerController* Player = CommonButtonActionsPrivate::FindPlayer(Context.Menu);
	switch (Target)
	{
	case EButtonActionFunctionTarget::OwningPlayer:
		Receiver = Player;
		break;
	case EButtonActionFunctionTarget::OwningPawn:
		Receiver = Player ? Player->GetPawn() : nullptr;
		break;
	case EButtonActionFunctionTarget::SourceWidget:
	default:
		Receiver = Context.SourceWidget;
		break;
	}

	CallOn(Receiver, Context.EntryId);
}

void UButtonAction_CallFunction::ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads)
{
	if (!Button)
	{
		return;
	}

	UObject* Receiver = nullptr;
	APlayerController* Player = CommonButtonActionsPrivate::FindPlayer(Button);
	switch (Target)
	{
	case EButtonActionFunctionTarget::OwningPlayer:
		Receiver = Player;
		break;
	case EButtonActionFunctionTarget::OwningPawn:
		Receiver = Player ? Player->GetPawn() : nullptr;
		break;
	case EButtonActionFunctionTarget::SourceWidget:
	default:
		Receiver = Button->GetTypedOuter<UUserWidget>();
		break;
	}

	CallOn(Receiver, Button->ButtonTag.GetTagName());
}

// =================================================================================================
// Open URL
// =================================================================================================

void UButtonAction_OpenURL::ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads)
{
	if (!URL.IsEmpty())
	{
		FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
	}
}

// =================================================================================================
// Play Sound
// =================================================================================================

void UButtonAction_PlaySound::ExecuteAction_Implementation(UContextMenuButton* Button, const TArray<UContextButtonPayload*>& ActionPayloads)
{
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound, VolumeMultiplier, PitchMultiplier);
	}
}
