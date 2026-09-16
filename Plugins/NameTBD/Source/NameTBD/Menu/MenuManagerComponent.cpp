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
#include "Kismet/GameplayStatics.h"

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
	CloseTopMenu();
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

	// Mutual exclusion is data, not a switch statement: the pause menu declares that it
	// closes the loadout rather than the code knowing about either.
	if (!definition->ClosesScreens.IsEmpty())
	{
		TArray<FGameplayTag> toClose = OpenScreenStack;
		for (const FGameplayTag& openTag : toClose)
		{
			if (definition->ClosesScreens.HasTag(openTag))
				CloseMenu(openTag, true);
		}
	}

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

	OnMenuStateChanged.Broadcast(ScreenTag, true);
	UInfoCardWidget::DispatchGlobalMenuStateChanged(ScreenTag, true);
	return true;
}

bool UMenuManagerComponent::CloseMenu(FGameplayTag ScreenTag, bool bKeepMenuContext)
{
	if (!IsLocalOwner() || !ScreenTag.IsValid())
		return false;

	if (!IsMenuOpen(ScreenTag))
		return false;

	if (TObjectPtr<UUserWidget>* widget = ScreenWidgets.Find(ScreenTag))
	{
		if (*widget)
		{
			(*widget)->SetVisibility(ESlateVisibility::Collapsed);
			if ((*widget)->GetClass()->ImplementsInterface(UMenuScreenInterface::StaticClass()))
				IMenuScreenInterface::Execute_OnScreenClosed(*widget);
		}
	}

	OpenScreenStack.Remove(ScreenTag);

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

	// Iterate a copy: CloseMenu mutates the stack.
	const TArray<FGameplayTag> toClose = OpenScreenStack;
	for (const FGameplayTag& screenTag : toClose)
	{
		CloseMenu(screenTag, true);
	}

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