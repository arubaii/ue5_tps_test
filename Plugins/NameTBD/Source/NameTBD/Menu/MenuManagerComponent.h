// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "MenuTypes.h"
#include "MenuManagerComponent.generated.h"

class APlayerController;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UInputMappingContext;
class UUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMenuStateChanged, FGameplayTag, ScreenTag, bool, bIsOpen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuInputContextChanged, EMenuInputContext, NewContext);

/**
 * Per-player menu/screen manager. C++ replacement for BPC_MenuManager.
 *
 * ============================ MULTIPLAYER CONTRACT ============================
 * This component is PURELY LOCAL. It replicates nothing and runs only on the machine
 * whose player owns it. Every public entry point is guarded by IsLocalOwner(), so the
 * copies that exist on simulated proxies of other players' pawns do nothing at all.
 *
 * The original Blueprint resolved its player through GetPlayerController(0), which on a
 * client returns the LOCAL controller regardless of which pawn asked - so a proxy of
 * another player would cache and drive the local player's camera and input. Here the
 * controller is always resolved from GetOwner(), and a null (or non-local) controller
 * means the component stays inert.
 * ==============================================================================
 *
 * Adding a screen: add one row to Screens. No enum, no parallel maps, no code change.
 */
UCLASS(ClassGroup = (UI), meta = (BlueprintSpawnableComponent))
class NAMETBD_API UMenuManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMenuManagerComponent();

	// ---------------------------------------------------------------- configuration

	/** Every screen this player can open, keyed by tag. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu|Screens")
	TMap<FGameplayTag, FMenuScreenDefinition> Screens;

	/** Mapping context applied for each input regime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu|Input")
	TMap<EMenuInputContext, TObjectPtr<UInputMappingContext>> MappingContexts;

	/** Priority passed to AddMappingContext. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu|Input")
	int32 MappingContextPriority = 0;

	/** Action that closes the top-most open screen. Optional. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu|Input")
	TObjectPtr<UInputAction> CloseTopScreenAction;

	/**
	 * Tags the owner must NOT have for a screen to open (in combat, airborne...).
	 * Evaluated through CanOpenMenu, which is Blueprint-overridable for anything richer.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Menu|Screens")
	FGameplayTagContainer BlockedByOwnerTags;

	// ---------------------------------------------------------------- events

	/**
	 * Fired when any screen opens or closes. The gameplay-tag component binds this to
	 * apply StateTag; the camera manager binds it to run its shift. Neither is referenced
	 * by this class, which is what lets the component move between projects.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Menu|Events")
	FOnMenuStateChanged OnMenuStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Menu|Events")
	FOnMenuInputContextChanged OnMenuInputContextChanged;

	// ---------------------------------------------------------------- api

	/** Open (or re-show) a screen. MenuContext is forwarded to InitializeScreen. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	bool OpenMenu(FGameplayTag ScreenTag, UObject* MenuContext = nullptr);

	/** Hide a screen. The widget is kept for reuse. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	bool CloseMenu(FGameplayTag ScreenTag, bool bKeepMenuContext = false);

	/** Open if closed, close if open. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	bool ToggleMenu(FGameplayTag ScreenTag, UObject* MenuContext = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Menu")
	void CloseAllMenus();

	/** Closes the most recently opened screen. Returns false if none were open. */
	UFUNCTION(BlueprintCallable, Category = "Menu")
	bool CloseTopMenu();

	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsMenuOpen(FGameplayTag ScreenTag) const;

	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsAnyMenuOpen() const;

	UFUNCTION(BlueprintPure, Category = "Menu")
	FGameplayTag GetTopMenu() const;

	/** The live widget for a screen, or null if it was never created. */
	UFUNCTION(BlueprintPure, Category = "Menu")
	UUserWidget* GetScreenWidget(FGameplayTag ScreenTag) const;

	/**
	 * Whether this component is the one belonging to the local player. Everything public
	 * short-circuits on this; exposed so callers can skip work rather than rely on it.
	 */
	UFUNCTION(BlueprintPure, Category = "Menu")
	bool IsLocalOwner() const;

	/** The owning player's controller, resolved from GetOwner(). Never index 0. */
	UFUNCTION(BlueprintPure, Category = "Menu")
	APlayerController* GetOwningPlayerController() const;

	/**
	 * Gate for opening. Default implementation rejects when the owner carries any tag in
	 * BlockedByOwnerTags; override in Blueprint for per-screen rules.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Menu")
	bool CanOpenMenu(FGameplayTag ScreenTag) const;
	virtual bool CanOpenMenu_Implementation(FGameplayTag ScreenTag) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Widgets created so far, live or hidden. Reused across open/close. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UUserWidget>> ScreenWidgets;

	/** Open screens, oldest first. Back element is the top-most. */
	UPROPERTY(Transient)
	TArray<FGameplayTag> OpenScreenStack;

	UPROPERTY(Transient)
	EMenuInputContext CurrentInputContext = EMenuInputContext::Gameplay;

	UPROPERTY(Transient)
	bool bInitialized = false;

	/** Deferred setup: the pawn may not be possessed when BeginPlay runs. */
	void TryInitialize();

	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

	void PreWarmScreens();
	void BindInputActions();

	UFUNCTION()
	void HandleToggleAction(FGameplayTag ScreenTag);

	/** Create-or-fetch. Returns null if the class is unset or fails to load. */
	UUserWidget* ResolveScreenWidget(FGameplayTag ScreenTag, const FMenuScreenDefinition& Definition,
	                                 UObject* MenuContext, bool& bOutNewlyCreated);

	/** Apply the input regime the top-most open screen asks for. */
	void RefreshInputContext();
	void ApplyInputContext(EMenuInputContext NewContext);

	/** Standalone-only pause; see the .cpp for why multiplayer is skipped. */
	void ApplyPauseState();

	UEnhancedInputLocalPlayerSubsystem* GetInputSubsystem() const;
	
	UFUNCTION()
	void HandleCloseTopScreenAction();
};