// Copyright AR Game Systems 2026. All rights reserved.


#include "MenuManagerComponent.h"
#include "MenuScreenInterface.h"
#include "NameTBD/Cards/InfoCardWidget.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTagAssetInterface.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Kismet/GameplayStatics.h"
#include "NameTBD/Menu/Input/MenuInputSubsystem.h"
#include "NameTBD/Notifications/NotificationSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogMenuManager, Log, All);

UMenuManagerComponent::UMenuManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Deliberately NOT replicated. Menus are a local presentation concern; anything that
	// other machines must know about goes out through gameplay state, not through here.
	SetIsReplicatedByDefault(false);
}

// =====================================================================================
//  Ownership resolution
// =====================================================================================

APlayerController* UMenuManagerComponent::GetOwningPlayerController() const
{
	AActor* owner = GetOwner();
	if (!owner)
		return nullptr;

	// Works wherever the component is placed: pawn, controller, or player state.
	if (APlayerController* asController = Cast<APlayerController>(owner))
		return asController;

	if (const APawn* asPawn = Cast<APawn>(owner))
		return Cast<APlayerController>(asPawn->GetController());

	if (const APlayerState* asPlayerState = Cast<APlayerState>(owner))
		return Cast<APlayerController>(asPlayerState->GetOwner());

	return nullptr;
}

bool UMenuManagerComponent::IsLocalOwner() const
{
	const APlayerController* pc = GetOwningPlayerController();
	return pc && pc->IsLocalController();
}

// =====================================================================================
//  Lifecycle
// =====================================================================================

void UMenuManagerComponent::BeginPlay()
{
	Super::BeginPlay();

	// A pawn is frequently unpossessed at BeginPlay, and on a client the controller for a
	// remote player's proxy never arrives at all. Both cases are handled by deferring:
	// initialise now if possible, otherwise wait for possession and re-check.
	if (APawn* ownerPawn = Cast<APawn>(GetOwner()))
		ownerPawn->ReceiveControllerChangedDelegate.AddDynamic(this, &UMenuManagerComponent::HandleControllerChanged);

	TryInitialize();
}

void UMenuManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* ownerPawn = Cast<APawn>(GetOwner()))
		ownerPawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &UMenuManagerComponent::HandleControllerChanged);

	// Leave the player in a sane input state if the pawn dies with a menu open.
	if (bInitialized && CurrentInputContext != EMenuInputContext::Gameplay)
		ApplyInputContext(EMenuInputContext::Gameplay);

	if (UEnhancedInputLocalPlayerSubsystem* subsystem = GetInputSubsystem())
	{
		if (ActiveScreenContext)
			subsystem->RemoveMappingContext(ActiveScreenContext);
	}
	ActiveScreenContext = nullptr;

	if (const APlayerController* pc = GetOwningPlayerController())
	{
		if (UMenuInputSubsystem* inputDevices = UMenuInputSubsystem::Get(this, pc))
			inputDevices->OnInputDeviceChangedNative().Remove(DeviceChangedHandle);

		if (UNotificationSubsystem* notifications = UNotificationSubsystem::Get(this, pc))
		{
			for (const TPair<FGameplayTag, int32>& pair : NotificationSuppressions)
				notifications->PopNotificationSuppression(pair.Value);
		}
	}
	NotificationSuppressions.Reset();

	for (const TPair<FGameplayTag, TSharedPtr<FStreamableHandle>>& pair : LoadHandles)
	{
		if (pair.Value.IsValid())
			pair.Value->CancelHandle();
	}
	LoadHandles.Reset();
	PendingOpens.Reset();
	PendingHides.Reset();
	FTSTicker::GetCoreTicker().RemoveTicker(HideTicker);
	HideTicker.Reset();

	for (const TPair<FGameplayTag, TObjectPtr<UUserWidget>>& pair : ScreenWidgets)
	{
		if (pair.Value)
			pair.Value->RemoveFromParent();
	}
	ScreenWidgets.Reset();
	OpenScreenStack.Reset();

	Super::EndPlay(EndPlayReason);
}

void UMenuManagerComponent::HandleControllerChanged(APawn* /*Pawn*/, AController* /*OldController*/,
                                                    AController* /*NewController*/)
{
	TryInitialize();
}

void UMenuManagerComponent::TryInitialize()
{
	if (bInitialized)
		return;

	// The single guard the whole class rests on. On a proxy this is false forever, so the
	// component never creates a widget, never touches input, never caches a controller.
	if (!IsLocalOwner())
		return;

	bInitialized = true;

	BindInputActions();
	PreWarmScreens();
	ApplyInputContext(EMenuInputContext::Gameplay);

	// Picking up the other device: cursor and focus follow it (Hide Cursor With Gamepad, Focus On Open).
	if (UMenuInputSubsystem* inputDevices = UMenuInputSubsystem::Get(this, GetOwningPlayerController()))
		DeviceChangedHandle = inputDevices->OnInputDeviceChangedNative().AddUObject(this, &UMenuManagerComponent::HandleInputDeviceChanged);
}

// =====================================================================================
//  Input
// =====================================================================================

UEnhancedInputLocalPlayerSubsystem* UMenuManagerComponent::GetInputSubsystem() const
{
	const APlayerController* pc = GetOwningPlayerController();
	if (!pc)
		return nullptr;

	return ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(pc->GetLocalPlayer());
}

void UMenuManagerComponent::BindInputActions()
{
	APlayerController* pc = GetOwningPlayerController();
	if (!pc)
		return;

	UEnhancedInputComponent* input = Cast<UEnhancedInputComponent>(pc->InputComponent);
	if (!input)
	{
		UE_LOG(LogMenuManager, Warning,
		       TEXT("[MenuManager] Owning controller has no EnhancedInputComponent; toggle bindings inactive."));
		return;
	}

	for (const TPair<FGameplayTag, FMenuScreenDefinition>& pair : Screens)
	{
		if (pair.Value.ToggleAction)
		{
			input->BindAction(pair.Value.ToggleAction, ETriggerEvent::Triggered, this,
			                  &UMenuManagerComponent::HandleToggleAction, pair.Key);
		}
	}

	if (CloseTopScreenAction)
	{
		input->BindAction(CloseTopScreenAction, ETriggerEvent::Triggered, this,
						  &UMenuManagerComponent::HandleCloseTopScreenAction);
	}
}

void UMenuManagerComponent::HandleToggleAction(FGameplayTag ScreenTag)
{
	ToggleMenu(ScreenTag, nullptr);
}

void UMenuManagerComponent::HandleCloseTopScreenAction()
{
	RequestBack();
}

void UMenuManagerComponent::RefreshInputContext()
{
	// The top-most open screen decides the regime; with nothing open we go back to gameplay.
	EMenuInputContext desired = EMenuInputContext::Gameplay;

	if (OpenScreenStack.Num() > 0)
	{
		if (const FMenuScreenDefinition* topDefinition = Screens.Find(OpenScreenStack.Last()))
			desired = topDefinition->InputContext;
	}

	ApplyInputContext(desired);
	ApplyTopScreenInput();
}

void UMenuManagerComponent::ApplyInputContext(EMenuInputContext NewContext)
{
	APlayerController* pc = GetOwningPlayerController();
	if (!pc)
		return;

	// Parity with the Blueprint's "Already set?" branches.
	if (bInitialized && CurrentInputContext == NewContext)
		return;

	if (UEnhancedInputLocalPlayerSubsystem* subsystem = GetInputSubsystem())
	{
		for (const TPair<EMenuInputContext, TObjectPtr<UInputMappingContext>>& pair : MappingContexts)
		{
			if (!pair.Value)
				continue;

			if (pair.Key == NewContext)
				subsystem->AddMappingContext(pair.Value, MappingContextPriority);
			else
				subsystem->RemoveMappingContext(pair.Value);
		}
	}

	const bool bMenuMode = (NewContext == EMenuInputContext::Menu);

	pc->bShowMouseCursor = bMenuMode;
	pc->SetIgnoreLookInput(bMenuMode);

	if (bMenuMode)
	{
		FInputModeGameAndUI inputMode;
		inputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		inputMode.SetHideCursorDuringCapture(false);
		pc->SetInputMode(inputMode);
	}
	else
		pc->SetInputMode(FInputModeGameOnly());

	CurrentInputContext = NewContext;
	OnMenuInputContextChanged.Broadcast(NewContext);
}

// =====================================================================================
//  Screens
// =====================================================================================

void UMenuManagerComponent::PreWarmScreens()
{
	for (const TPair<FGameplayTag, FMenuScreenDefinition>& pair : Screens)
	{
		if (!pair.Value.bPreWarm)
			continue;

		bool bCreated = false;
		if (UUserWidget* widget = ResolveScreenWidget(pair.Key, pair.Value, nullptr, bCreated))
			widget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

UUserWidget* UMenuManagerComponent::ResolveScreenWidget(FGameplayTag ScreenTag,
                                                        const FMenuScreenDefinition& Definition,
                                                        UObject* MenuContext, bool& bOutNewlyCreated)
{
	bOutNewlyCreated = false;

	if (TObjectPtr<UUserWidget>* existing = ScreenWidgets.Find(ScreenTag))
	{
		if (*existing)
			return *existing;
	}

	APlayerController* pc = GetOwningPlayerController();
	if (!pc)
		return nullptr;

	UClass* widgetClass = Definition.WidgetClass.LoadSynchronous();
	if (!widgetClass)
	{
		UE_LOG(LogMenuManager, Warning, TEXT("[MenuManager] Screen '%s' has no widget class set."),
		       *ScreenTag.ToString());
		return nullptr;
	}

	// Owning player comes from the resolved controller, never from player index 0. This is
	// what makes each client's widget belong to its own player.
	UUserWidget* widget = CreateWidget<UUserWidget>(pc, widgetClass);
	if (!widget)
		return nullptr;

	ScreenWidgets.Add(ScreenTag, widget);
	bOutNewlyCreated = true;

	if (widget->GetClass()->ImplementsInterface(UMenuScreenInterface::StaticClass()))
		IMenuScreenInterface::Execute_InitializeScreen(widget, MenuContext);

	widget->AddToPlayerScreen(Definition.ZOrder);
	widget->SetVisibility(ESlateVisibility::Collapsed);
	return widget;
}

bool UMenuManagerComponent::CanOpenMenu_Implementation(FGameplayTag ScreenTag) const
{
	if (BlockedByOwnerTags.IsEmpty())
		return true;

	// Read tags off whatever on the owner chain advertises them, without naming a class.
	const AActor* owner = GetOwner();
	const IGameplayTagAssetInterface* tagSource = Cast<IGameplayTagAssetInterface>(owner);
	if (!tagSource)
	{
		if (const APawn* asPawn = Cast<APawn>(owner))
			tagSource = Cast<IGameplayTagAssetInterface>(asPawn->GetPlayerState());
	}

	if (!tagSource)
		return true;

	FGameplayTagContainer ownerTags;
	tagSource->GetOwnedGameplayTags(ownerTags);
	return !ownerTags.HasAny(BlockedByOwnerTags);
}

bool UMenuManagerComponent::OpenMenu(FGameplayTag ScreenTag, UObject* MenuContext)
{
	if (!IsLocalOwner() || !ScreenTag.IsValid())
		return false;

	const FMenuScreenDefinition* definition = Screens.Find(ScreenTag);
	if (!definition)
	{
		UE_LOG(LogMenuManager, Warning, TEXT("[MenuManager] No screen registered for tag '%s'."),
		       *ScreenTag.ToString());
		return false;
	}

	if (IsMenuOpen(ScreenTag))
		return true;

	if (!CanOpenMenu(ScreenTag))
		return false;

	// Async Load: a class not in memory yet streams in, and the screen opens when it arrives.
	const TObjectPtr<UUserWidget>* cached = ScreenWidgets.Find(ScreenTag);
	if (definition->bAsyncLoad && !(cached && *cached) && !definition->WidgetClass.IsNull() && !definition->WidgetClass.Get())
	{
		if (!PendingOpens.Contains(ScreenTag))
		{
			PendingOpens.Add(ScreenTag, MenuContext);
			TWeakObjectPtr<UMenuManagerComponent> weakThis(this);
			LoadHandles.Add(ScreenTag, Streamable.RequestAsyncLoad(definition->WidgetClass.ToSoftObjectPath(),
				FStreamableDelegate::CreateLambda([weakThis, ScreenTag]()
				{
					UMenuManagerComponent* manager = weakThis.Get();
					if (!manager)
						return;

					TWeakObjectPtr<UObject> context;
					if (manager->PendingOpens.RemoveAndCopyValue(ScreenTag, context))
						manager->OpenMenu(ScreenTag, context.Get());
					manager->LoadHandles.Remove(ScreenTag);
				})));
		}
		return true;
	}

	// Reopened during its Close Delay: it simply stays.
	CancelPendingHide(ScreenTag);

	// Remember where focus was in the screen this one covers, to put it back when this one closes.
	const FGameplayTag coveredTag = GetTopMenu();
	if (coveredTag.IsValid() && FSlateApplication::IsInitialized())
	{
		const APlayerController* pc = GetOwningPlayerController();
		const ULocalPlayer* localPlayer = pc ? pc->GetLocalPlayer() : nullptr;
		if (const TSharedPtr<const FSlateUser> user = localPlayer ? localPlayer->GetSlateUser() : nullptr)
			FocusMemory.Add(coveredTag, FSlateApplication::Get().GetUserFocusedWidget(user->GetUserIndex()));
	}

	// Mutual exclusion is data, not a switch statement: the pause menu declares that it
	// closes the loadout rather than the code knowing about either.
	FGameplayTagContainer closedHere;
	if (!definition->ClosesScreens.IsEmpty())
	{
		TArray<FGameplayTag> toClose = OpenScreenStack;
		for (const FGameplayTag& openTag : toClose)
		{
			if (definition->ClosesScreens.HasTag(openTag))
			{
				CloseMenu(openTag, true);
				closedHere.AddTag(openTag);
			}
		}
	}
	ClosedByScreen.Add(ScreenTag, closedHere);

	bool bNewlyCreated = false;
	UUserWidget* widget = ResolveScreenWidget(ScreenTag, *definition, MenuContext, bNewlyCreated);
	if (!widget)
		return false;

	// A cached widget still gets its context refreshed - opening a chest twice with
	// different chests must not show the first chest's contents.
	if (!bNewlyCreated && widget->GetClass()->ImplementsInterface(UMenuScreenInterface::StaticClass()))
		IMenuScreenInterface::Execute_InitializeScreen(widget, MenuContext);

	widget->SetVisibility(ESlateVisibility::Visible);
	OpenScreenStack.AddUnique(ScreenTag);

	if (widget->GetClass()->ImplementsInterface(UMenuScreenInterface::StaticClass()))
		IMenuScreenInterface::Execute_OnScreenOpened(widget, MenuContext);

	RefreshInputContext();
	ApplyPauseState();

	if (definition->bSuppressNotifications && !NotificationSuppressions.Contains(ScreenTag))
	{
		if (UNotificationSubsystem* notifications = UNotificationSubsystem::Get(this, GetOwningPlayerController()))
			NotificationSuppressions.Add(ScreenTag, notifications->PushNotificationSuppression(definition->NotificationSuppression, FGameplayTagContainer()));
	}

	ApplyScreenFocus(ScreenTag, false);

	OnMenuStateChanged.Broadcast(ScreenTag, true);
	UInfoCardWidget::DispatchGlobalMenuStateChanged(ScreenTag, true);
	return true;
}

bool UMenuManagerComponent::CloseMenu(FGameplayTag ScreenTag, bool bKeepMenuContext)
{
	if (!IsLocalOwner() || !ScreenTag.IsValid())
		return false;

	if (!IsMenuOpen(ScreenTag))
	{
		// Closing one still loading cancels its open.
		return PendingOpens.Remove(ScreenTag) > 0;
	}

	const FMenuScreenDefinition* closingDefinition = Screens.Find(ScreenTag);
	if (TObjectPtr<UUserWidget>* widget = ScreenWidgets.Find(ScreenTag))
	{
		if (*widget)
		{
			// Close Delay keeps it shown (not taking input) while its exit animation plays.
			if (closingDefinition && closingDefinition->CloseDelay > 0.f)
			{
				(*widget)->SetVisibility(ESlateVisibility::HitTestInvisible);
				PendingHides.Add(ScreenTag, FPlatformTime::Seconds() + closingDefinition->CloseDelay);
				if (!HideTicker.IsValid())
					HideTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMenuManagerComponent::TickPendingHides));
			}
			else
				(*widget)->SetVisibility(ESlateVisibility::Collapsed);

			if ((*widget)->GetClass()->ImplementsInterface(UMenuScreenInterface::StaticClass()))
				IMenuScreenInterface::Execute_OnScreenClosed(*widget);
		}
	}

	OpenScreenStack.Remove(ScreenTag);
	FocusMemory.Remove(ScreenTag);

	int32 suppression = INDEX_NONE;
	if (NotificationSuppressions.RemoveAndCopyValue(ScreenTag, suppression))
	{
		if (UNotificationSubsystem* notifications = UNotificationSubsystem::Get(this, GetOwningPlayerController()))
			notifications->PopNotificationSuppression(suppression);
	}

	// Return To Closed Screens: what this one replaced comes back (Settings -> Pause).
	FGameplayTagContainer toReopen;
	ClosedByScreen.RemoveAndCopyValue(ScreenTag, toReopen);
	if (closingDefinition && closingDefinition->bReturnToClosedScreens && !toReopen.IsEmpty() && !bClosingAll)
	{
		bKeepMenuContext = true;
		for (const FGameplayTag& reopenTag : toReopen)
			OpenMenu(reopenTag, nullptr);
	}
	else if (OpenScreenStack.Num() > 0)
		ApplyScreenFocus(OpenScreenStack.Last(), true);

	// bKeepMenuContext exists so a screen can hand off to another screen without the input
	// mode flickering back to gameplay for a frame in between.
	if (!bKeepMenuContext)
		RefreshInputContext();

	ApplyPauseState();

	OnMenuStateChanged.Broadcast(ScreenTag, false);
	UInfoCardWidget::DispatchGlobalMenuStateChanged(ScreenTag, false);
	return true;
}

bool UMenuManagerComponent::ToggleMenu(FGameplayTag ScreenTag, UObject* MenuContext)
{
	if (IsMenuOpen(ScreenTag))
		return CloseMenu(ScreenTag);

	return OpenMenu(ScreenTag, MenuContext);
}

void UMenuManagerComponent::CloseAllMenus()
{
	if (!IsLocalOwner())
		return;

	// Iterate a copy: CloseMenu mutates the stack. Nothing comes back (Return To Closed Screens) while all close.
	TGuardValue<bool> closingAll(bClosingAll, true);
	const TArray<FGameplayTag> toClose = OpenScreenStack;
	for (const FGameplayTag& screenTag : toClose)
	{
		CloseMenu(screenTag, true);
	}
	PendingOpens.Reset();

	RefreshInputContext();
	ApplyPauseState();
}

bool UMenuManagerComponent::CloseTopMenu()
{
	if (OpenScreenStack.Num() == 0)
		return false;

	return CloseMenu(OpenScreenStack.Last());
}

// =====================================================================================
//  Queries
// =====================================================================================

bool UMenuManagerComponent::IsMenuOpen(FGameplayTag ScreenTag) const
{
	return OpenScreenStack.Contains(ScreenTag);
}

bool UMenuManagerComponent::IsAnyMenuOpen() const
{
	return OpenScreenStack.Num() > 0;
}

FGameplayTag UMenuManagerComponent::GetTopMenu() const
{
	return OpenScreenStack.Num() > 0 ? OpenScreenStack.Last() : FGameplayTag();
}

UUserWidget* UMenuManagerComponent::GetScreenWidget(FGameplayTag ScreenTag) const
{
	const TObjectPtr<UUserWidget>* found = ScreenWidgets.Find(ScreenTag);
	return found ? *found : nullptr;
}

// =====================================================================================
//  Pause
// =====================================================================================

void UMenuManagerComponent::ApplyPauseState()
{
	const UWorld* world = GetWorld();
	if (!world)
		return;

	// SetGamePaused is server-authoritative and pauses the whole session. In the original
	// Blueprint a client calling it silently did nothing, and the host calling it froze
	// every player. Neither is wanted, so pausing is limited to standalone; in a networked
	// session the menu opens without stopping the world. A co-op pause needs a vote or a
	// game-mode call, which belongs in game state, not in a per-player UI component.
	if (world->GetNetMode() != NM_Standalone)
		return;

	bool bWantsPause = false;
	for (const FGameplayTag& screenTag : OpenScreenStack)
	{
		const FMenuScreenDefinition* definition = Screens.Find(screenTag);
		if (definition && definition->bPausesGame)
		{
			bWantsPause = true;
			break;
		}
	}

	if (APlayerController* pc = GetOwningPlayerController())
		UGameplayStatics::SetGamePaused(this, bWantsPause);
}

// =====================================================================================================
//  Back, lookup
// =====================================================================================================

bool UMenuManagerComponent::RequestBack()
{
	if (!IsLocalOwner())
		return false;

	if (OpenScreenStack.Num() == 0)
		return BackOpensScreen.IsValid() && OpenMenu(BackOpensScreen, nullptr);

	// The top screen first: it may go back a sub-page, close a dialog, or ask about unsaved changes.
	const FGameplayTag topTag = OpenScreenStack.Last();
	if (UUserWidget* widget = GetScreenWidget(topTag))
	{
		if (widget->GetClass()->ImplementsInterface(UMenuScreenInterface::StaticClass())
			&& IMenuScreenInterface::Execute_OnBackRequested(widget))
			return true;
	}

	const FMenuScreenDefinition* definition = Screens.Find(topTag);
	if (definition && !definition->bCloseOnBack)
		return false;

	return CloseMenu(topTag);
}

FGameplayTag UMenuManagerComponent::FindScreenTag(const UUserWidget* Widget) const
{
	for (const TPair<FGameplayTag, TObjectPtr<UUserWidget>>& pair : ScreenWidgets)
	{
		if (Widget && pair.Value == Widget)
			return pair.Key;
	}
	return FGameplayTag();
}

bool UMenuManagerComponent::CloseScreenWidget(UUserWidget* Widget)
{
	const FGameplayTag screenTag = FindScreenTag(Widget);
	return screenTag.IsValid() && IsMenuOpen(screenTag) && CloseMenu(screenTag);
}

bool UMenuManagerComponent::IsMenuLoading(FGameplayTag ScreenTag) const
{
	return PendingOpens.Contains(ScreenTag);
}

UMenuManagerComponent* UMenuManagerComponent::FindForPlayer(const APlayerController* Player)
{
	if (!Player)
		return nullptr;

	if (UMenuManagerComponent* onController = Player->FindComponentByClass<UMenuManagerComponent>())
		return onController;

	if (const APawn* pawn = Player->GetPawn())
	{
		if (UMenuManagerComponent* onPawn = pawn->FindComponentByClass<UMenuManagerComponent>())
			return onPawn;
	}

	if (const APlayerState* playerState = Player->PlayerState)
		return playerState->FindComponentByClass<UMenuManagerComponent>();

	return nullptr;
}

// =====================================================================================================
//  Top screen input, focus, close delay
// =====================================================================================================

void UMenuManagerComponent::ApplyTopScreenInput()
{
	APlayerController* pc = GetOwningPlayerController();
	if (!pc)
		return;

	const FGameplayTag topTag = GetTopMenu();
	const FMenuScreenDefinition* topDefinition = topTag.IsValid() ? Screens.Find(topTag) : nullptr;

	// The top screen's own mapping context.
	UInputMappingContext* wantedContext = topDefinition ? topDefinition->ScreenMappingContext.Get() : nullptr;
	if (wantedContext != ActiveScreenContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* subsystem = GetInputSubsystem())
		{
			if (ActiveScreenContext)
				subsystem->RemoveMappingContext(ActiveScreenContext);
			if (wantedContext)
				subsystem->AddMappingContext(wantedContext, MappingContextPriority + 1);
		}
		ActiveScreenContext = wantedContext;
	}

	// Input mode past the context's.
	const EMenuScreenInputMode wantedMode = topDefinition ? topDefinition->InputMode : EMenuScreenInputMode::Default;
	if (wantedMode == EMenuScreenInputMode::UIOnly)
	{
		FInputModeUIOnly inputMode;
		inputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		if (UUserWidget* widget = GetScreenWidget(topTag))
			inputMode.SetWidgetToFocus(widget->TakeWidget());
		pc->SetInputMode(inputMode);
		pc->bShowMouseCursor = true;
	}
	else if (wantedMode == EMenuScreenInputMode::GameAndUI)
	{
		FInputModeGameAndUI inputMode;
		inputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		inputMode.SetHideCursorDuringCapture(false);
		pc->SetInputMode(inputMode);
		pc->bShowMouseCursor = true;
	}
	else if (AppliedInputMode != EMenuScreenInputMode::Default)
	{
		// Back from a screen with its own mode: the context's routing again.
		if (CurrentInputContext == EMenuInputContext::Menu)
		{
			FInputModeGameAndUI inputMode;
			inputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			inputMode.SetHideCursorDuringCapture(false);
			pc->SetInputMode(inputMode);
			pc->bShowMouseCursor = true;
		}
		else
		{
			pc->SetInputMode(FInputModeGameOnly());
			pc->bShowMouseCursor = false;
		}
	}
	AppliedInputMode = wantedMode;

	// No cursor while a pad drives a screen that asks for that.
	if (topDefinition && topDefinition->bHideCursorWithGamepad && CurrentInputContext == EMenuInputContext::Menu)
	{
		const UMenuInputSubsystem* inputDevices = UMenuInputSubsystem::Get(this, pc);
		if (inputDevices && inputDevices->IsUsingGamepad())
			pc->bShowMouseCursor = false;
	}
}

void UMenuManagerComponent::ApplyScreenFocus(FGameplayTag ScreenTag, bool bReturning)
{
	APlayerController* pc = GetOwningPlayerController();
	const FMenuScreenDefinition* definition = Screens.Find(ScreenTag);
	UUserWidget* widget = GetScreenWidget(ScreenTag);
	if (!pc || !definition || !widget)
		return;

	if (bReturning && definition->bRestoreFocus && FSlateApplication::IsInitialized())
	{
		const TWeakPtr<SWidget>* remembered = FocusMemory.Find(ScreenTag);
		const TSharedPtr<SWidget> pinned = remembered ? remembered->Pin() : nullptr;
		const ULocalPlayer* localPlayer = pc->GetLocalPlayer();
		const TSharedPtr<const FSlateUser> user = localPlayer ? localPlayer->GetSlateUser() : nullptr;
		if (pinned.IsValid() && user.IsValid())
		{
			FSlateApplication::Get().SetUserFocus(user->GetUserIndex(), pinned, EFocusCause::SetDirectly);
			return;
		}
	}

	switch (definition->FocusOnOpen)
	{
	case EMenuScreenFocus::Screen:
		widget->SetUserFocus(pc);
		break;
	case EMenuScreenFocus::NamedWidget:
		if (UWidget* named = widget->GetWidgetFromName(definition->FocusWidgetName))
			named->SetUserFocus(pc);
		else
			UE_LOG(LogMenuManager, Warning, TEXT("[MenuManager] Screen '%s' has no widget '%s' to focus."), *ScreenTag.ToString(), *definition->FocusWidgetName.ToString());
		break;
	default:
		break;
	}
}

void UMenuManagerComponent::HandleInputDeviceChanged(EMenuInputDevice Device)
{
	if (OpenScreenStack.Num() == 0)
		return;

	ApplyTopScreenInput();

	// Picking up a pad on a screen with nothing focused: give it its focus, so the pad can navigate.
	if (Device == EMenuInputDevice::Gamepad)
	{
		const APlayerController* pc = GetOwningPlayerController();
		const ULocalPlayer* localPlayer = pc ? pc->GetLocalPlayer() : nullptr;
		const TSharedPtr<const FSlateUser> user = localPlayer ? localPlayer->GetSlateUser() : nullptr;
		if (user.IsValid() && FSlateApplication::IsInitialized() && !FSlateApplication::Get().GetUserFocusedWidget(user->GetUserIndex()).IsValid())
			ApplyScreenFocus(OpenScreenStack.Last(), false);
	}
}

void UMenuManagerComponent::HideScreenWidget(FGameplayTag ScreenTag)
{
	if (IsMenuOpen(ScreenTag))
		return;

	if (UUserWidget* widget = GetScreenWidget(ScreenTag))
		widget->SetVisibility(ESlateVisibility::Collapsed);
}

void UMenuManagerComponent::CancelPendingHide(FGameplayTag ScreenTag)
{
	PendingHides.Remove(ScreenTag);
}

bool UMenuManagerComponent::TickPendingHides(float DeltaTime)
{
	const double now = FPlatformTime::Seconds();
	TArray<FGameplayTag> due;
	for (const TPair<FGameplayTag, double>& pair : PendingHides)
	{
		if (now >= pair.Value)
			due.Add(pair.Key);
	}
	for (const FGameplayTag& screenTag : due)
	{
		PendingHides.Remove(screenTag);
		HideScreenWidget(screenTag);
	}
	if (PendingHides.Num() == 0)
	{
		HideTicker.Reset();
		return false;
	}
	return true;
}
