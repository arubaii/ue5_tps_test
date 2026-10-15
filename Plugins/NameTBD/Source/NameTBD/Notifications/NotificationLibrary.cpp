// Copyright AR GameSystems 2026. All rights reserved.

#include "NotificationLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "NotificationSubsystem.h"

// =====================================================================================================
// UNotificationLibrary
// =====================================================================================================

UNotificationSubsystem* UNotificationLibrary::GetNotificationSubsystem(const UObject* WorldContextObject, APlayerController* Player)
{
	return UNotificationSubsystem::Get(WorldContextObject, Player);
}

FNotificationHandle UNotificationLibrary::PostNotification(const UObject* WorldContextObject, const FNotificationRequest& Request, APlayerController* Player)
{
	UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(WorldContextObject, Player);
	return Subsystem ? Subsystem->PostNotification(Request) : FNotificationHandle();
}

FNotificationHandle UNotificationLibrary::ShowNotification(const UObject* WorldContextObject, FGameplayTag Type, FText Title, FText Body, int32 Count, APlayerController* Player)
{
	FNotificationRequest Request;
	Request.Type = Type;
	Request.Title = Title;
	Request.Body = Body;
	Request.Count = Count;
	return PostNotification(WorldContextObject, Request, Player);
}

bool UNotificationLibrary::UpdateNotification(const UObject* WorldContextObject, FNotificationHandle Handle, const FNotificationRequest& Request, bool bRestartTime, APlayerController* Player)
{
	UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(WorldContextObject, Player);
	return Subsystem && Subsystem->UpdateNotification(Handle, Request, bRestartTime);
}

bool UNotificationLibrary::SetNotificationProgress(const UObject* WorldContextObject, FNotificationHandle Handle, float Progress, APlayerController* Player)
{
	UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(WorldContextObject, Player);
	return Subsystem && Subsystem->SetNotificationProgress(Handle, Progress);
}

bool UNotificationLibrary::DismissNotification(const UObject* WorldContextObject, FNotificationHandle Handle, APlayerController* Player)
{
	UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(WorldContextObject, Player);
	return Subsystem && Subsystem->DismissNotification(Handle);
}

bool UNotificationLibrary::RespondToNotification(const UObject* WorldContextObject, FNotificationHandle Handle, FName ActionId, APlayerController* Player)
{
	UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(WorldContextObject, Player);
	return Subsystem && Subsystem->RespondToNotification(Handle, ActionId);
}

int32 UNotificationLibrary::PushNotificationSuppression(const UObject* WorldContextObject, ENotificationSuppressionMode Mode, FGameplayTagContainer TypesOrChannels, APlayerController* Player)
{
	UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(WorldContextObject, Player);
	return Subsystem ? Subsystem->PushNotificationSuppression(Mode, TypesOrChannels) : 0;
}

void UNotificationLibrary::PopNotificationSuppression(const UObject* WorldContextObject, int32 SuppressionId, APlayerController* Player)
{
	if (UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(WorldContextObject, Player))
	{
		Subsystem->PopNotificationSuppression(SuppressionId);
	}
}

FNotificationAction UNotificationLibrary::MakeNotificationAction(FName ActionId, FText Label, ENotificationActionStyle Style, FKey GamepadKey, FKey KeyboardKey, float HoldTime, bool bDismisses)
{
	FNotificationAction Action;
	Action.ActionId = ActionId;
	Action.Label = Label;
	Action.Style = Style;
	Action.GamepadKey = GamepadKey;
	Action.KeyboardKey = KeyboardKey;
	Action.HoldTime = FMath::Max(HoldTime, 0.f);
	Action.bDismisses = bDismisses;
	return Action;
}

void UNotificationLibrary::PostNotificationToPlayer(APlayerController* Player, const FNotificationRequest& Request)
{
	if (!Player)
	{
		return;
	}
	if (Player->IsLocalController())
	{
		if (UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(Player, Player))
		{
			Subsystem->PostNotification(Request);
		}
		return;
	}
	if (UNotificationRelayComponent* Relay = Player->FindComponentByClass<UNotificationRelayComponent>())
	{
		Relay->ClientPostNotification(Request);
		return;
	}
	UE_LOG(LogNameTBDNotifications, Warning, TEXT("Post Notification To Player: %s has no Notification Relay component - add one to the Player Controller class so the server can reach its player."),
		*Player->GetName());
}

void UNotificationLibrary::PostNotificationToAllPlayers(const UObject* WorldContextObject, const FNotificationRequest& Request)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		PostNotificationToPlayer(It->Get(), Request);
	}
}

// =====================================================================================================
// UAsyncAction_PostNotification
// =====================================================================================================

UAsyncAction_PostNotification* UAsyncAction_PostNotification::PostNotificationAndWait(UObject* WorldContextObject, const FNotificationRequest& Request, APlayerController* Player)
{
	UAsyncAction_PostNotification* Action = NewObject<UAsyncAction_PostNotification>();
	Action->PendingRequest = Request;
	Action->WeakContext = WorldContextObject;
	Action->WeakPlayer = Player;
	Action->RegisterWithGameInstance(WorldContextObject);
	return Action;
}

void UAsyncAction_PostNotification::Activate()
{
	UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(WeakContext.Get(), WeakPlayer.Get());
	if (!Subsystem)
	{
		OnFinished.Broadcast(FNotificationHandle(), NAME_None, ENotificationDismissReason::Cleared);
		SetReadyToDestroy();
		return;
	}

	const FOnNotificationActionNative ActionCallback = FOnNotificationActionNative::CreateWeakLambda(this, [this](FNotificationHandle Handle, FName ActionId)
	{
		OnAction.Broadcast(Handle, ActionId, ENotificationDismissReason::Responded);
	});
	const FOnNotificationFinishedNative FinishedCallback = FOnNotificationFinishedNative::CreateWeakLambda(this, [this](FNotificationHandle Handle, FName ActionId, ENotificationDismissReason Reason)
	{
		bFinished = true;
		OnFinished.Broadcast(Handle, ActionId, Reason);
		SetReadyToDestroy();
	});

	const FNotificationHandle Handle = Subsystem->PostNotificationWithCallbacks(PendingRequest, ActionCallback, FinishedCallback);
	// A throttled or suppressed post has already finished (inside the post): no Posted after Finished.
	if (!bFinished)
	{
		OnPosted.Broadcast(Handle, NAME_None, ENotificationDismissReason::TimedOut);
	}
}

// =====================================================================================================
// UNotificationRelayComponent
// =====================================================================================================

UNotificationRelayComponent::UNotificationRelayComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UNotificationRelayComponent::PostToOwningPlayer(const FNotificationRequest& Request)
{
	if (APlayerController* Owner = Cast<APlayerController>(GetOwner()))
	{
		if (Owner->IsLocalController())
		{
			if (UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(Owner, Owner))
			{
				Subsystem->PostNotification(Request);
			}
			return;
		}
	}
	ClientPostNotification(Request);
}

void UNotificationRelayComponent::ClearOnOwningPlayer(FGameplayTagContainer TypesOrChannels)
{
	ClientClearNotifications(TypesOrChannels);
}

void UNotificationRelayComponent::ClientPostNotification_Implementation(const FNotificationRequest& Request)
{
	if (UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(this, Cast<APlayerController>(GetOwner())))
	{
		Subsystem->PostNotification(Request);
	}
}

void UNotificationRelayComponent::ClientClearNotifications_Implementation(const FGameplayTagContainer& TypesOrChannels)
{
	if (UNotificationSubsystem* Subsystem = UNotificationSubsystem::Get(this, Cast<APlayerController>(GetOwner())))
	{
		Subsystem->ClearNotifications(TypesOrChannels, /*bIncludeQueued=*/true);
	}
}
