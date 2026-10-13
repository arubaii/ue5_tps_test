// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "DiegeticTypes.h"
#include "NameTBD/WidgetAnimation/WidgetAnimInput.h"
#include "DiegeticScreenComponent.generated.h"

class APlayerController;
class UBackgroundBlur;
class UDiegeticInteractorComponent;
class SWindow;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class UPrimitiveComponent;
class UTextureRenderTarget2D;
class UWidget;

/** The plugin's screen materials (Material Mode Automatic): one for each way a screen can need drawing. */
enum class EDiegeticMaterialVariant : uint8
{
	/** Translucent (premultiplied alpha): every look but background blur. */
	Panel,
	/** Translucent with the world behind blurred: Frosted Glass, Background Blur widgets. */
	Glass,
	/** Opaque: a screen with Blend Mode Opaque, cheapest. */
	Opaque
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDiegeticScreenEvent, UDiegeticScreenComponent*, Screen);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDiegeticScreenAimEvent, UDiegeticScreenComponent*, Screen, bool, bAimedAt);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDiegeticScreenUseEvent, UDiegeticScreenComponent*, Screen, APlayerController*, User);

/**
 * A Widget Component for UI that lives in the world - see the note at the top of DiegeticTypes.h. Everything a Widget
 * Component does, it does (World space, set up for it), plus:
 *
 * - Size in centimetres (Physical Size, Pixel Density) instead of pixels and scale.
 * - Redraws scheduled by the world's Diegetic UI Subsystem: by distance from the camera, only while something on it moves
 *   (When Changed), never while every camera is behind a one-sided screen or it isn't seen, inside a per-frame budget shared
 *   by every screen in the world, and every frame while a player points at it or uses it. Its widget ticks when it redraws.
 * - Awake and asleep as players come and go (Presence), playing its widget in and out.
 * - Pointed at and clicked through a Diegetic Interactor (Use): Point And Click, or Focus View - the camera moves in and the
 *   player uses the screen with the mouse or a gamepad until Back.
 * - The plugin's own systems on its widget: pointer-driven Widget Animation inputs and state machine inputs read this screen's
 *   pointer (not the desktop's), UI sounds play from where they are on it, and the widget is told about the screen through the
 *   Diegetic Screen Widget interface and the ScreenAwake / ScreenAimedAt / ScreenInUse / ScreenDistance state machine inputs.
 * - Drawn with the plugin's own screen materials (Material Mode Automatic): exactly as UMG draws the widget, or with a Screen
 *   Look - terminal, hologram, frosted glass that blurs the world behind (Background Blur widgets on it too), LCD.
 * - Shown on its own panel, or on a Mesh Surface (a material slot of any mesh); placed as placed, facing the camera, or
 *   floating in front of the player (Placement). Quick Setup sets everything for the common kinds of diegetic UI at once.
 * - A Context object its widget can ask for (the weapon, the door), and Focus Groups for screens used together.
 *
 * In an editor world (not playing) it redraws as the engine has it; its look and material show, a Mesh Surface doesn't.
 */
UCLASS(ClassGroup = "UserInterface", Blueprintable, meta = (BlueprintSpawnableComponent, DisplayName = "Diegetic Screen", PrioritizeCategories = "Diegetic Screen"))
class NAMETBD_API UDiegeticScreenComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:

	UDiegeticScreenComponent(const FObjectInitializer& ObjectInitializer);

	// -------------------------------------------------------------------------------------------------
	// Authored settings
	// -------------------------------------------------------------------------------------------------

	/**
	 * The kind of diegetic UI this is: picking one sets Look, Display, Placement, Use, Presence, Updates and size for it at once
	 * (a wall terminal, a monitor, a hologram, a menu in front of the player, a device display, a sign, a glass panel). Change any
	 * setting afterwards. See EDiegeticScreenKind.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (DisplayName = "Quick Setup"))
	EDiegeticScreenKind QuickSetup = EDiegeticScreenKind::Custom;

	/** Which material it is drawn with. Automatic: the plugin's own, for its Screen Look. See EDiegeticMaterialMode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (DisplayName = "Material"))
	EDiegeticMaterialMode MaterialMode = EDiegeticMaterialMode::Automatic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (DisplayName = "Display"))
	FDiegeticDisplaySettings DisplaySettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (DisplayName = "Placement"))
	FDiegeticPlacementSettings PlacementSettings;

	/** Shown when play starts. Off: hidden until Set Screen Shown (a menu opened by the player). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen")
	bool bStartShown = true;

	/** The actor whose data the screen shows, when it isn't the screen's own: a door's panel showing the door. See Set Screen Context. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Diegetic Screen")
	TObjectPtr<AActor> ContextActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (DisplayName = "Sizing"))
	EDiegeticScreenSizing ScreenSizing = EDiegeticScreenSizing::DrawSize;

	/** Physical Size: the screen's width and height in centimetres, at the component's parent's scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (ForceUnits = "cm", EditCondition = "ScreenSizing == EDiegeticScreenSizing::PhysicalSize", EditConditionHides))
	FVector2D PhysicalSize = FVector2D(160.0, 90.0);

	/**
	 * Physical Size: pixels to the centimetre. 12 makes a 160 x 90 cm screen 1920 x 1080. Higher is crisper and costs more
	 * texture memory and drawing; text read from a step away needs about 8 to 12.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (ClampMin = "0.5", ClampMax = "64", UIMin = "2", UIMax = "24", EditCondition = "ScreenSizing == EDiegeticScreenSizing::PhysicalSize", EditConditionHides))
	float PixelDensity = 12.f;

	/** The texture the screen's widget is drawn into: its size and memory. Grows with Draw Size; see Pixel Density. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Diegetic Screen", meta = (DisplayName = "Texture"))
	FString TextureInfo;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (DisplayName = "Updates"))
	FDiegeticUpdateSettings UpdateSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (DisplayName = "Presence"))
	FDiegeticPresenceSettings PresenceSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (DisplayName = "Use"))
	FDiegeticUseSettings UseSettings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (DisplayName = "Screen Look"))
	FDiegeticScreenLook ScreenLook;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (DisplayName = "Sound"))
	FDiegeticSoundSettings SoundSettings;

	/**
	 * Sets ScreenAwake, ScreenAimedAt, ScreenInUse (Bool) and ScreenDistance (Number, cm from the nearest camera) on the
	 * state machines of the widget on it, where they have inputs of those names - so a state machine can light the screen up
	 * as the player looks at it, with no Blueprint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Diegetic Screen", meta = (DisplayName = "Set State Machine Inputs"))
	bool bDriveStateMachineInputs = true;

	// -------------------------------------------------------------------------------------------------
	// Events
	// -------------------------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Diegetic Screen")
	FDiegeticScreenEvent OnScreenWake;

	UPROPERTY(BlueprintAssignable, Category = "Diegetic Screen")
	FDiegeticScreenEvent OnScreenSleep;

	/** A player started or stopped pointing at it. */
	UPROPERTY(BlueprintAssignable, Category = "Diegetic Screen")
	FDiegeticScreenAimEvent OnScreenAimChanged;

	UPROPERTY(BlueprintAssignable, Category = "Diegetic Screen")
	FDiegeticScreenUseEvent OnScreenUseBegin;

	UPROPERTY(BlueprintAssignable, Category = "Diegetic Screen")
	FDiegeticScreenUseEvent OnScreenUseEnd;

	// -------------------------------------------------------------------------------------------------
	// Blueprint
	// -------------------------------------------------------------------------------------------------

	/**
	 * Something on it changed that it can't see for itself - text set, an image swapped: a When Changed screen redraws (for
	 * its Settle Time). Any screen redraws on its next turn. From inside the widget: Mark Diegetic Content Changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Screen")
	void MarkContentChanged();

	UFUNCTION(BlueprintCallable, Category = "Diegetic Screen")
	void SetUpdateMode(EDiegeticUpdateMode NewMode);

	/** Hands a new Screen Look to its material (the glass variant is picked when it blurs). */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Screen")
	void SetScreenLook(const FDiegeticScreenLook& NewLook);

	/** Gives it a ready-made look: Screen Look's values for Preset. */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Screen")
	void SetLookPreset(EDiegeticLookPreset NewPreset);

	/** Sets everything for a kind of diegetic UI, as Quick Setup does in the Details. Custom changes nothing. */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Screen")
	void ApplyQuickSetup(EDiegeticScreenKind Kind);

	/**
	 * Shows it (playing its widget in) or hides it (playing it out, then hiding it) whatever the distance - a menu opened and closed
	 * by the player. Hiding stops anyone using it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Screen")
	void SetScreenShown(bool bShown);

	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	bool IsScreenShown() const { return bShownByUser; }

	/**
	 * The object whose data the screen shows - a weapon, a door, a room - for its widget to read (Get Diegetic Screen Context) and
	 * hear about (Diegetic Screen Widget > On Screen Context Changed). Screens on one device can share it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Screen")
	void SetScreenContext(UObject* NewContext);

	/** The screen's Context: set with Set Screen Context, else Context Actor, else the screen's own actor. */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	UObject* GetScreenContext() const;

	/** Mesh Surface: the mesh it is shown on. None for a Panel, or when the mesh isn't found. */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	UMeshComponent* GetDisplayMesh() const;

	/** Whether a player is within Wake Distance (always, with Wake Distance 0, or while kept awake or in use). */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	bool IsAwake() const { return bAwake; }

	/** Keeps it awake whatever the distance (true), or lets distance decide again (false). */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Screen")
	void SetKeepAwake(bool bKeep);

	/** Whether a player is pointing at it. */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	bool IsAimedAt() const { return Aimers.Num() > 0; }

	/** Whether a player is using it (Focus View, or Begin Use). */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	bool IsInUse() const;

	/** The player using it. None when it isn't in use. */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	APlayerController* GetUser() const;

	/**
	 * Has Player use it, as a click on a Focus View screen does - whatever its Use Mode, except Not Usable. The player needs a
	 * Diegetic Interactor (one is added on its own; see Project Settings > Diegetic UI). False when it couldn't.
	 */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Screen")
	bool BeginUse(APlayerController* Player);

	/** Stops whoever is using it, as Back does. */
	UFUNCTION(BlueprintCallable, Category = "Diegetic Screen")
	void EndUse();

	/** Whether a player can point at it now: its Use Mode isn't Not Usable, it is shown and awake, and nobody else is using it. */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	bool CanBeAimedAt() const;

	/** How far the nearest camera was from it when last measured (cm). */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	float GetViewDistance() const { return ViewDistance; }

	/** Its width and height in the world (cm). */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	FVector2D GetScreenWorldSize() const;

	/** The middle of its face, in the world. */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	FVector GetScreenCenter() const;

	/** Where its picture is seen: the middle of its panel, or of the mesh it is shown on. */
	FVector GetDisplayCenter() const;

	/** The size of what shows its picture, across its Y and Z axes (cm): its panel, or the mesh it is shown on. */
	FVector2D GetDisplayWorldSize() const;

	/** What is drawn in the world for it - its panel, or its mesh: for visibility and distance. */
	UPrimitiveComponent* GetDisplayPrimitive() const;

	/**
	 * Mesh Surface: whether the ray from Start to End hits its slot, and where on its (undrawn) panel that is - what the Widget
	 * Interaction Component is handed. OutDistance from Start to the mesh.
	 */
	bool TraceMeshSurface(const FVector& Start, const FVector& End, FVector& OutPanelPoint, FVector& OutHitPoint, FVector& OutNormal, double& OutDistance) const;

	/** Moves it as its Placement says, for a camera at ViewLocation looking along ViewRotation. Subsystem tick (real time). */
	void UpdatePlacement(const FVector& ViewLocation, const FRotator& ViewRotation, float DeltaTime);

	/**
	 * Gives the editor's material maker to every screen (the editor module, at startup): returns the plugin's screen material for
	 * a variant, made in /NameTBD/Diegetic (or /Game/NameTBD/Diegetic) when missing or out of date. Null outside the editor:
	 * screens load the materials named in Project Settings > Diegetic UI.
	 */
	static void SetEditorMaterialProvider(TFunction<UMaterialInterface*(EDiegeticMaterialVariant)> InProvider);

	/** The plugin's screen material for a variant: the editor's, or the one Project Settings names. Null when there is none. */
	static UMaterialInterface* GetAutomaticMaterial(EDiegeticMaterialVariant Variant);

	/** Puts its material on again - the plugin's materials were just made or rebuilt. */
	void RefreshMaterial() { ApplyMaterialSetup(); }

	/** The variant of the plugin's material it is drawn with now. */
	EDiegeticMaterialVariant GetMaterialVariant() const { return AppliedVariant; }

	/** A point on it, in its widget's pixels from the top left, in the world. Flat screens; a Cylinder's is on its chord. */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	FVector WidgetToWorld(FVector2D WidgetPosition) const;

	/** Redraws in the last second. */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen")
	float GetRedrawsPerSecond() const;

	/**
	 * The Diegetic Screen a widget is shown on - anywhere inside its widget, nested User Widgets and widgets added at runtime
	 * included. None for a widget on the HUD.
	 */
	UFUNCTION(BlueprintPure, Category = "Diegetic Screen", meta = (DefaultToSelf = "ForWidget"))
	static UDiegeticScreenComponent* FindScreenForWidget(const UWidget* ForWidget);

	/** FindScreenForWidget for anything that may be a widget - what the UI sounds ask. Null for anything else. */
	static UDiegeticScreenComponent* FindScreenForObject(const UObject* Object);

	/** Whether any Diegetic Screen exists anywhere: the lookups above cost nothing while none does. */
	static bool AnyScreensExist();

	// -------------------------------------------------------------------------------------------------
	// Plugin integration (C++)
	// -------------------------------------------------------------------------------------------------

	/** The pointer on it, for its widgets' Widget Animation and state machine inputs - see WidgetAnimInput::FWorldPointer. */
	void GetWorldPointer(WidgetAnimInput::FWorldPointer& Out) const;

	/** Where a widget on it is, in the world: the middle of its drawn box. Its middle when the widget isn't laid out. */
	FVector GetWorldLocationOfWidget(const UWidget* OnScreen) const;

	/** A Widget Animation played something on it this frame: a When Changed screen redraws. */
	void NoteAnimating();

	// -------------------------------------------------------------------------------------------------
	// For the Diegetic UI Subsystem and Diegetic Interactor
	// -------------------------------------------------------------------------------------------------

	/** Whether the world's subsystem schedules its redraws (a game world). */
	bool IsScheduled() const { return bScheduled; }
	void SetScheduled(bool bInScheduled) { bScheduled = bInScheduled; }

	/** This frame's redraw, as the scheduler decided. */
	void SetDrawScheduled(bool bDraw) { bDrawScheduled = bDraw; }

	/** Seconds (FPlatformTime) of its last redraw. */
	double GetLastDrawTime() const { return LastDrawTime; }

	/**
	 * When Changed: whether anything on it moved, was pointed at or used, or was reported changed (Mark Content Changed) within
	 * its Settle Time of Now (FPlatformTime seconds). Takes the report.
	 */
	bool ConsumeChange(double Now);

	/** Asleep: whether it still redraws - Keep Updating, or its widget still playing out. */
	bool WantsRedrawWhileAsleep(double Now) const;

	/** Whether its widget, or something on it, is animating now. */
	bool IsWidgetAnimating() const;

	/** Its distances this frame, and whether any camera is in front of it. Sets ScreenDistance. */
	void SetMeasurements(float InViewDistance, float InPawnDistance, bool bInAnyViewInFront);
	float GetPawnDistance() const { return PawnDistance; }
	bool IsAnyViewInFront() const { return bAnyViewInFront; }

	/** Works out awake or asleep from the distances; plays in / out, tells the widget. */
	void UpdatePresence();

	/** Keeps the widget lookups in step with the widget and window it has now. */
	void SyncRegistration();

	/** Passes the screen's state to a widget that changed (SyncRegistration noted it) once it is safe to touch. Subsystem tick. */
	void FlushWidgetState();

	/**
	 * The widget on it, when it is safe to talk to: built into the screen's window, and not being replaced by a Blueprint
	 * recompile (PIE) or destroyed. Null otherwise.
	 */
	UUserWidget* GetSafeWidget() const;

	/** An interactor started or stopped pointing at it. */
	void NotifyAimed(UDiegeticInteractorComponent* Interactor, bool bAimed);

	/** Where an interactor points on it, in widget pixels. */
	void NotePointerPosition(const FVector2D& WidgetPosition);

	/** An interactor pressed or let go of a pointer button on it, at a place in widget pixels. */
	void NotePointerButton(const FKey& Button, bool bDown, const FVector2D& WidgetPosition);

	/** An interactor started (non-null) or stopped using it. */
	void SetUserInteractor(UDiegeticInteractorComponent* Interactor);

	/** Hides it as soon as nobody uses it any more - a screen opened as a menu (Open Diegetic Menu): Back closes it. */
	void SetHideWhenUseEnds(bool bHide) { bHideWhenUseEnds = bHide; }
	UDiegeticInteractorComponent* GetUserInteractor() const { return UserInteractor.Get(); }

	/** Debug text for NameTBD.Diegetic.Debug. */
	FString GetDebugText() const;

	// UWidgetComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void InitWidget() override;
	virtual void SetWidget(UUserWidget* NewWidget) override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void CheckForErrors() override;
#endif

protected:

	// UWidgetComponent
	virtual bool ShouldDrawWidget() const override;
	virtual void DrawWidgetToRenderTarget(float DeltaTime) override;

private:

	/** Physical Size: Draw Size and the component's Y / Z scale. */
	void ApplySizing();

	/** TextureInfo, from Draw Size. */
	void UpdateTextureInfo();

	/** Hands Screen Look to the material instances (the Widget Component's, the mesh's), when they or the look changed. */
	void ApplyScreenLook(bool bForce);
	void ApplyLookTo(UMaterialInstanceDynamic* Instance) const;

	/** Puts the right material on: Material Mode's, for the variant the look needs; and on the mesh, for a Mesh Surface. */
	void ApplyMaterialSetup();

	/** The variant its look and widget need now. */
	EDiegeticMaterialVariant GetWantedVariant() const;

	/** Mesh Surface (game worlds): its panel undrawn, its picture on the mesh's slot. Off: the mesh's slot as it was. */
	void SetUpMeshSurface();
	void TearDownMeshSurface();

	/** Mesh Surface: the render target, tint and UV region on the mesh's material instance. */
	void UpdateSurfaceParameters();

	/** The Background Blur widgets on the widget: found when it changes, their own drawing turned off (UE-41001). */
	void CollectBlurWidgets();
	void RestoreBlurWidgets();

	/** Their areas this frame, handed to the material as BlurRegion0..7 / BlurShape0..7. */
	void ApplyBlurRegions();

	/** Cull Distance on the component. */
	void ApplyCullDistance();

	/** Sets a state machine input on the widget's state machines (those of its Widget Animation Component and State Machine Component). */
	void SetWidgetBool(FName Input, bool bValue) const;
	void SetWidgetNumber(FName Input, float Value) const;

	void BroadcastWake(bool bInitial);
	void BroadcastSleep(bool bInitial);

	/** Removes it from the widget lookups. */
	void Unregister();

	// Runtime state
	bool bScheduled = false;
	bool bDrawScheduled = false;
	bool bAwake = true;
	bool bPresenceKnown = false;
	bool bKeepAwake = false;
	bool bHiddenAsleep = false;
	bool bContentChanged = true;
	bool bPushWidgetState = false;
	bool bShownByUser = true;
	bool bHasPlacement = false;
	bool bPushContext = false;
	bool bCollectBlur = false;
	bool bHideWhenUseEnds = false;
	double NextBlurCollectTime = 0.0;
	bool bAnyViewInFront = true;
	float ViewDistance = 0.f;
	float PawnDistance = 0.f;
	float LastSentDistance = -1.f;
	double LastDrawTime = 0.0;
	double LastChangeTime = -1.0e9;
	double SleepStartTime = -1.0e9;
	uint64 LastAnimatingFrame = 0;

	/** Redraw times in the last second, for GetRedrawsPerSecond. */
	TArray<double, TInlineAllocator<16>> RecentDraws;

	/** Interactors pointing at it now, and the one using it. */
	TArray<TWeakObjectPtr<UDiegeticInteractorComponent>, TInlineAllocator<2>> Aimers;
	TWeakObjectPtr<UDiegeticInteractorComponent> UserInteractor;

	/** The pointer on it, for its widgets' inputs: WidgetAnimInput::FWorldPointer, kept up by the interactors. */
	FVector2f PointerPosition = FVector2f::ZeroVector;
	bool bHasPointerPosition = false;
	bool bLeftDown = false;
	bool bRightDown = false;
	WidgetAnimInput::FPointerEventCount LeftPresses;
	WidgetAnimInput::FPointerEventCount RightPresses;
	WidgetAnimInput::FPointerEventCount RightReleases;

	/** The material instance Screen Look was last handed to, and the look handed. */
	TWeakObjectPtr<UMaterialInstanceDynamic> LookAppliedTo;
	FDiegeticScreenLook LookApplied;

	/** The Context set with Set Screen Context. */
	UPROPERTY(Transient)
	TObjectPtr<UObject> ScreenContext = nullptr;

	/** Mesh Surface: the material instance on the mesh's slot, the slot, the material it had, and the render target last given. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MeshMaterial = nullptr;
	TWeakObjectPtr<UMeshComponent> SurfaceMesh;
	int32 SurfaceSlot = INDEX_NONE;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SurfaceOriginalMaterial = nullptr;
	TWeakObjectPtr<UTextureRenderTarget2D> SurfaceTarget;

	/** The variant last put on, and whether the widget holds Background Blur widgets. */
	EDiegeticMaterialVariant AppliedVariant = EDiegeticMaterialVariant::Panel;
	bool bHasBlurWidgets = false;

	/** The Background Blur widgets on the widget, with the Blur Strength each had before its drawing was turned off. */
	TMap<TWeakObjectPtr<UBackgroundBlur>, float> BlurWidgets;

	/** The blur regions last handed to the material (count, then each region and shape), to set only what changed. */
	TArray<FLinearColor> AppliedBlurRegions;

	/** In Front Of Player: where it is easing toward. */
	FVector PlacedLocation = FVector::ZeroVector;
	FQuat PlacedRotation = FQuat::Identity;

	/** What it is registered under in the widget lookups. */
	TWeakObjectPtr<UUserWidget> RegisteredWidget;
	TWeakPtr<SWindow> RegisteredWindow;
	bool bInLookups = false;
};
