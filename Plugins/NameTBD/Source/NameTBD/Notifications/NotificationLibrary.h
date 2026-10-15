// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NotificationTypes.h"

#include "NotificationLibrary.generated.h"

class APlayerController;
class UNotificationSubsystem;

/**
 * Notifications from Blueprint: post, update, answer and dismiss them for a local player (the one owning the calling widget
 * or actor, else the first), or - on a server - for any player through their Notification Relay.
 */
UCLASS()
class NAMETBD_API UNotificationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** The notification system of Player (or of whoever owns the caller, else the first local player). */
	UFUNCTION(BlueprintPure, Category = "Notifications", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Player"))
	static UNotificationSubsystem* GetNotificationSubsystem(const UObject* WorldContextObject, APlayerController* Player = nullptr);

	/** Shows a notification (see Make Notification Request's fields). Returns its handle - the one it merged into when it stacked. */
	UFUNCTION(BlueprintCallable, Category = "Notifications", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Player"))
	static FNotificationHandle PostNotification(const UObject* WorldContextObject, const FNotificationRequest& Request, APlayerController* Player = nullptr);

	/** Shows a notification of Type with a title and body: the quick way. */
	UFUNCTION(BlueprintCallable, Category = "Notifications", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Count,Player", Categories = "Notification.Type"))
	static FNotificationHandle ShowNotification(const UObject* WorldContextObject, FGameplayTag Type, FText Title, FText Body, int32 Count = 1, APlayerController* Player = nullptr);

	/** Changes a notification's content in place (bRestartTime gives it its full time again). */
	UFUNCTION(BlueprintCallable, Category = "Notifications", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Player"))
	static bool UpdateNotification(const UObject* WorldContextObject, FNotificationHandle Handle, const FNotificationRequest& Request, bool bRestartTime = false, APlayerController* Player = nullptr);

	/** Moves its progress bar (0 to 1; negative hides it). */
	UFUNCTION(BlueprintCallable, Category = "Notifications", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Player"))
	static bool SetNotificationProgress(const UObject* WorldContextObject, FNotificationHandle Handle, float Progress, APlayerController* Player = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Notifications", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Player"))
	static bool DismissNotification(const UObject* WorldContextObject, FNotificationHandle Handle, APlayerController* Player = nullptr);

	/** Takes one of its actions as if the player had. */
	UFUNCTION(BlueprintCallable, Category = "Notifications", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Player"))
	static bool RespondToNotification(const UObject* WorldContextObject, FNotificationHandle Handle, FName ActionId, APlayerController* Player = nullptr);

	/** Holds, freezes or drops notifications until popped (a cutscene, a menu). Returns the id to pop with. */
	UFUNCTION(BlueprintCallable, Category = "Notifications", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Player"))
	static int32 PushNotificationSuppression(const UObject* WorldContextObject, ENotificationSuppressionMode Mode, FGameplayTagContainer TypesOrChannels, APlayerController* Player = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Notifications", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Player"))
	static void PopNotificationSuppression(const UObject* WorldContextObject, int32 SuppressionId, APlayerController* Player = nullptr);

	/** An action for a notification's Actions. */
	UFUNCTION(BlueprintPure, Category = "Notifications", meta = (AdvancedDisplay = "Style,GamepadKey,KeyboardKey,HoldTime,bDismisses"))
	static FNotificationAction MakeNotificationAction(FName ActionId, FText Label, ENotificationActionStyle Style, FKey GamepadKey, FKey KeyboardKey, float HoldTime = 0.f, bool bDismisses = true);

	UFUNCTION(BlueprintPure, Category = "Notifications", meta = (DisplayName = "Is Valid (Notification Handle)"))
	static bool IsNotificationHandleValid(FNotificationHandle Handle) { return Handle.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Notifications", meta = (DisplayName = "Equal (Notification Handle)", CompactNodeTitle = "=="))
	static bool EqualNotificationHandles(FNotificationHandle A, FNotificationHandle B) { return A == B; }

	/**
	 * Server: shows a notification to Player, on their own machine - through the Notification Relay on their Player Controller
	 * (add one to your Player Controller class). A local player (the listen server's own) gets it directly.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Notifications|Network")
	static void PostNotificationToPlayer(APlayerController* Player, const FNotificationRequest& Request);

	/** Server: shows a notification to every player (a match starting, a server message). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Notifications|Network", meta = (WorldContext = "WorldContextObject"))
	static void PostNotificationToAllPlayers(const UObject* WorldContextObject, const FNotificationRequest& Request);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNotificationAsyncPin, FNotificationHandle, Handle, FName, ActionId, ENotificationDismissReason, Reason);

/**
 * Post Notification And Wait: shows a notification and carries on when the player answers it - an invite's Accept / Decline,
 * a "Retry?" on a failed download. On Action fires for each action taken; On Finished once when it has gone (Action Id: the
 * one taken, None if it timed out or was closed; Reason why).
 */
UCLASS()
class NAMETBD_API UAsyncAction_PostNotification : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Notifications", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", AdvancedDisplay = "Player", DisplayName = "Post Notification And Wait"))
	static UAsyncAction_PostNotification* PostNotificationAndWait(UObject* WorldContextObject, const FNotificationRequest& Request, APlayerController* Player = nullptr);

	/** Posted (its handle, to update or dismiss it). */
	UPROPERTY(BlueprintAssignable)
	FNotificationAsyncPin OnPosted;

	/** The player took an action (with Dismisses off, possibly more than once). */
	UPROPERTY(BlueprintAssignable)
	FNotificationAsyncPin OnAction;

	/** It has gone. */
	UPROPERTY(BlueprintAssignable)
	FNotificationAsyncPin OnFinished;

	virtual void Activate() override;

private:

	UPROPERTY()
	FNotificationRequest PendingRequest;

	TWeakObjectPtr<UObject> WeakContext;
	TWeakObjectPtr<APlayerController> WeakPlayer;
	bool bFinished = false;
};

/**
 * Notification Relay: lets the server show notifications on a player's own screen. Add it to your Player Controller class;
 * Post Notification To Player / To All Players use it. Requests travel as they are (text, tags, numbers, actions, the payload
 * struct, asset references); a Context Object arrives only if it replicates.
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent, DisplayName = "Notification Relay"))
class NAMETBD_API UNotificationRelayComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UNotificationRelayComponent();

	/** Server: shows Request to the player owning this controller. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Notifications")
	void PostToOwningPlayer(const FNotificationRequest& Request);

	/** Server: clears that player's notifications of these types / channels (all when empty). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Notifications")
	void ClearOnOwningPlayer(FGameplayTagContainer TypesOrChannels);

	UFUNCTION(Client, Reliable)
	void ClientPostNotification(const FNotificationRequest& Request);

	UFUNCTION(Client, Reliable)
	void ClientClearNotifications(const FGameplayTagContainer& TypesOrChannels);
};
