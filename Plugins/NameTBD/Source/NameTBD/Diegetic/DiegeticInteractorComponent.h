// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetInteractionComponent.h"
#include "Types/SlateEnums.h"
#include "DiegeticInteractorComponent.generated.h"

class ACameraActor;
class APlayerController;
class UDiegeticScreenComponent;
class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
class UWidgetComponent;
struct FInputActionValue;

/** Where a Diegetic Interactor points from. */
UENUM(BlueprintType)
enum class EDiegeticAimSource : uint8
{
	Camera UMETA(ToolTip = "From the player's camera through the middle of the view: a crosshair. Reach is measured from the pawn, so a third-person camera's distance behind it doesn't count."),

	MouseCursor UMETA(DisplayName = "Mouse Cursor", ToolTip = "Through the mouse cursor (for a game that shows it). Reach is measured from the pawn."),

	ThisComponent UMETA(DisplayName = "This Component", ToolTip = "Along this component's forward axis from where it is: a hand, a motion controller, a laser pointer.")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDiegeticAimedScreenEvent, UWidgetComponent*, Screen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDiegeticInteractorUseEvent, UDiegeticScreenComponent*, Screen);

/**
 * A Widget Interaction Component that points at Diegetic Screens - see DiegeticTypes.h. On a player's pawn (added on its own
 * unless Project Settings > Diegetic UI says otherwise): it finds the screen under its aim itself - flat screens by their
 * plane, no collision needed; a wall in the way stops it (its Trace Channel) - and hands the hit to the engine's own
 * interaction, which hovers, clicks and focuses the widgets there as a Widget Interaction Component always has.
 *
 * Input with nothing set up: it binds its own keys (Project Settings > Diegetic UI > Default Keys) through Enhanced Input, in
 * mapping contexts added only while it points at something on a screen that can be clicked, or while a screen is in use -
 * so the click doesn't also reach the game (a weapon) and the game keeps its keys the rest of the time. A project with its
 * own input sets Aim Input / Use Input and their actions instead, or calls Click Screen and the rest from its own bindings.
 *
 * Focus View (a screen's Use Mode): a click on the screen starts using it - the camera moves in front of it, the pawn stops,
 * the cursor shows and points at it; with a gamepad, focus goes to its widget and the D-pad moves it - until Back.
 *
 * Its presses are what the plugin's state machines and Widget Animation inputs on the screen read (Pressed, Right Clicked,
 * Drag): call Press Screen Pointer / Release Screen Pointer, not the engine's Press Pointer Key, from Blueprint.
 */
UCLASS(ClassGroup = "UserInterface", meta = (BlueprintSpawnableComponent, DisplayName = "Diegetic Interactor"))
class NAMETBD_API UDiegeticInteractorComponent : public UWidgetInteractionComponent
{
	GENERATED_BODY()

public:

	UDiegeticInteractorComponent(const FObjectInitializer& ObjectInitializer);

	// -------------------------------------------------------------------------------------------------
	// Settings
	// -------------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diegetic Interactor")
	EDiegeticAimSource AimSource = EDiegeticAimSource::Camera;

	/** Also points at plain Widget Components the trace hits (by their collision), as a Widget Interaction Component does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Diegetic Interactor")
	bool bAimAtWidgetComponents = true;

	/** Binds its input on its own: Aim Input and Use Input, or - unset - the Default Keys of Project Settings > Diegetic UI. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input")
	bool bBindInput = true;

	/**
	 * Added while it points at something on a screen that can be clicked (or at a Focus View screen), or holds a click: Click
	 * Action, Secondary Click Action, Scroll Action. Unset: made from the Default Keys.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputMappingContext> AimInput = nullptr;

	/** Added while a screen is in use: Back, Accept, Navigate and Scroll Stick Actions. Unset: made from the Default Keys. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputMappingContext> UseInput = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputAction> ClickAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputAction> SecondaryClickAction = nullptr;

	/** An Axis 1D action: a notch a step, as a mouse wheel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputAction> ScrollAction = nullptr;

	/** An Axis 1D action: scrolls smoothly while held, as a stick (Stick Scroll Speed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputAction> ScrollStickAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputAction> BackAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputAction> AcceptAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputAction> NavigateUpAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputAction> NavigateDownAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputAction> NavigateLeftAction = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Interactor|Input", AdvancedDisplay, meta = (EditCondition = "bBindInput"))
	TObjectPtr<UInputAction> NavigateRightAction = nullptr;

	// -------------------------------------------------------------------------------------------------
	// Events
	// -------------------------------------------------------------------------------------------------

	/** It started pointing at another screen (a Diegetic Screen, or a plain Widget Component), or at none: a crosshair's cue. */
	UPROPERTY(BlueprintAssignable, Category = "Diegetic Interactor")
	FDiegeticAimedScreenEvent OnAimedScreenChanged;

	UPROPERTY(BlueprintAssignable, Category = "Diegetic Interactor")
	FDiegeticInteractorUseEvent OnUseBegin;

	UPROPERTY(BlueprintAssignable, Category = "Diegetic Interactor")
	FDiegeticInteractorUseEvent OnUseEnd;

	// -------------------------------------------------------------------------------------------------
	// Blueprint
	// -------------------------------------------------------------------------------------------------

	/** Presses a pointer button where it points - counted for the screen's state machines and Widget Animation inputs. */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Interactor")
	void PressScreenPointer(FKey Button);

	UFUNCTION(BlueprintCallable, Category = "Diegetic Interactor")
	void ReleaseScreenPointer(FKey Button);

	/**
	 * What its Click does: on a Focus View screen not in use, starts using it; otherwise presses the left button where it points
	 * (bPressed) or lets it go (!bPressed).
	 */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Interactor")
	void ClickScreen(bool bPressed);

	/** Back: asks the screen's widget (Diegetic Screen Widget > On Screen Back), and stops using the screen unless it handled it. */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Interactor")
	void BackFromScreen();

	/** Moves focus on the screen in use, as the D-pad does. */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Interactor")
	void NavigateScreen(EUINavigation Direction);

	/** Uses Screen: Focus View's camera, cursor and input, whatever the screen's Use Mode (except Not Usable). */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Interactor")
	bool BeginUsing(UDiegeticScreenComponent* Screen);

	/** Stops using the screen in use: the camera goes back to the pawn, the cursor and the pawn's input as they were. */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Interactor")
	void EndUsing();

	UFUNCTION(BlueprintPure, Category = "Diegetic Interactor")
	UDiegeticScreenComponent* GetUsedScreen() const { return UsedScreen.Get(); }

	/** The Diegetic Screen it points at. None when it points at none (or at a plain Widget Component). */
	UFUNCTION(BlueprintPure, Category = "Diegetic Interactor")
	UDiegeticScreenComponent* GetAimedScreen() const;

	/** The player it belongs to: its owner, its pawn's controller. */
	UFUNCTION(BlueprintPure, Category = "Diegetic Interactor")
	APlayerController* GetInteractingPlayer() const;

	/** The Diegetic Interactor of Player's pawn (or of Player). None when it has none. */
	UFUNCTION(BlueprintPure, Category = "Diegetic Interactor")
	static UDiegeticInteractorComponent* FindInteractor(const APlayerController* Player);

	/** A screen going away: no longer pointed at or used. */
	void ForgetScreen(UDiegeticScreenComponent* Screen);

	// UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

	/** Works out what it points at, and hands it to the engine's interaction as its Custom hit. */
	void UpdateAim();

	/** The ray it points along, and where reach is measured from. False when it can't point (no cursor, no player). */
	bool GetAimRay(FVector& OutOrigin, FVector& OutDirection, FVector& OutReachFrom) const;

	/** After the engine's interaction has run: tells screens they are (or aren't) pointed at, and where. */
	void UpdateAimedState();

	/** Binds input to the player it belongs to now (once per player), or lets go of it. */
	void UpdateInputBinding();
	void ReleaseInputBinding();
	void BuildDefaultInput();

	/** Adds or removes the Aim and Use mapping contexts for what it points at now. */
	void UpdateInputContexts();

	/** Without Enhanced Input: reads the Default Keys off the player each frame. */
	void PollDefaultKeys();

	/** Lets go of every pointer button it holds. */
	void ReleaseHeldPointers();

	// Enhanced Input handlers
	void HandleClickPressed();
	void HandleClickReleased();
	void HandleSecondaryPressed();
	void HandleSecondaryReleased();
	void HandleScroll(const FInputActionValue& Value);
	void HandleScrollStick(const FInputActionValue& Value);
	void HandleBack();
	void HandleAcceptPressed();
	void HandleAcceptReleased();
	void HandleNavigateUp();
	void HandleNavigateDown();
	void HandleNavigateLeft();
	void HandleNavigateRight();
	void HandleNavigateReleased();

	/** Sends one navigation key press to the screen in use. */
	void SendNavigation(EUINavigation Direction);

	/** The camera Focus View moves to, for Screen - and the screens of its Focus Group, framed together. */
	FTransform ComputeFocusViewTransform(const UDiegeticScreenComponent* Screen, float FieldOfView) const;

	/** Screen and the other screens on its actor with the same Focus Group (just Screen when it has none). */
	static void GetFocusGroup(UDiegeticScreenComponent* Screen, TArray<UDiegeticScreenComponent*>& OutScreens);

	/** The screen it points at now, and the Widget Component (either kind) the engine's interaction hovers. */
	TWeakObjectPtr<UWidgetComponent> AimedComponent;

	/** The screen in use, and what was there before using it. */
	TWeakObjectPtr<UDiegeticScreenComponent> UsedScreen;
	TWeakObjectPtr<ACameraActor> FocusCamera;
	TWeakObjectPtr<AActor> ViewTargetBeforeUse;
	bool bCursorShownBeforeUse = false;
	bool bChangedCursor = false;
	bool bBlockedPawnInput = false;

	/** Pointer buttons it holds, and the screen each was pressed on (it hears the release too). */
	TMap<FKey, TWeakObjectPtr<UDiegeticScreenComponent>> HeldPointers;

	/** The Accept key it holds down on the screen in use. */
	bool bAcceptHeld = false;

	/** Navigation held, and when it repeats next (FPlatformTime seconds). */
	TOptional<EUINavigation> HeldNavigation;
	double NextNavigationRepeat = 0.0;

	/** Input bound to this player, through this input component. */
	TWeakObjectPtr<APlayerController> BoundPlayer;
	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> BoundInputComponent = nullptr;
	bool bUsingEnhancedInput = false;
	bool bAimContextAdded = false;
	bool bUseContextAdded = false;

	/** The contexts bound: Aim Input / Use Input, or the ones made from the Default Keys (Generated...). */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> BoundAimContext = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> BoundUseContext = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> GeneratedAimContext = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> GeneratedUseContext = nullptr;
	bool bFillAimContext = false;
	bool bFillUseContext = false;

	/** The actions made from the Default Keys (also set into Click Action and the rest). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> DefaultActions;
};
