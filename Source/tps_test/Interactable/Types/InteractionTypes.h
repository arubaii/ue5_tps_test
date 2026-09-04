// Copyright arubaii. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/WidgetComponent.h"
#include "InteractionTypes.generated.h"

class UUserWidget;
class UWidgetComponent;
class UInteractableComponent;

/**
 * How an interaction is committed once the player is looking at something.
 *
 * This is the ONLY axis the plugin itself branches on. Everything else that varies per
 * interactable — what it looks like, what it does, what it says — is data or a virtual,
 * never a new enumerator here.
 *
 * Adding a method means adding one enumerator and one case in
 * UInteractorComponent::ResolveInteractionOutcome. Nothing else in the plugin switches
 * on this type.
 */
UENUM(BlueprintType)
enum class EInteractionMethod : uint8
{
	/** Commit the moment the key goes down. */
	Press UMETA(DisplayName = "Press"),

	/** Commit after the key has been held for FInteractionProfile::HoldDuration. */
	Hold UMETA(DisplayName = "Hold"),

	/** Commit as soon as the actor is focused, with no input at all (auto-pickup). */
	Automatic UMETA(DisplayName = "Automatic")
};

/**
 * Which presentation an interactable is currently receiving from one interactor.
 *
 * Deliberately an ordered scale rather than a set of flags: a given actor is in exactly
 * one of these states for a given viewer at a time, and the higher state always wins
 * (an item you are looking at does not also draw its ambient marker).
 */
UENUM(BlueprintType)
enum class EInteractionPresentation : uint8
{
	/** Nothing shown. */
	None,

	/** In range, drawing its at-a-distance marker. */
	Ambient,

	/** The interactor's current focus target, drawing its full prompt. */
	Focused
};

/** Why an in-progress interaction ended without committing. */
UENUM(BlueprintType)
enum class EInteractionAbortReason : uint8
{
	/** Player released the key early. */
	Released,
	/** Focus moved to a different actor, or was lost entirely. */
	FocusLost,
	/** Suppressed mid-hold (a menu opened, a cutscene started...). */
	Suppressed,
	/** The server rejected it. */
	Rejected
};

UENUM(BlueprintType)
enum class EWidgetFadeCurve : uint8
{
	Linear,
	Quadratic,
	Cubic,
	Exponential
};

UENUM(BlueprintType)
enum class EWidgetBillboard : uint8
{
	Disabled,
	Horizontal,
	Vertical,
	Both UMETA(DisplayName = "Horizontal and Vertical")
};

UENUM(BlueprintType)
enum class EWidgetDisplayPreset : uint8
{
	Custom,
	Focus,
	Ambient
};

/**
 * One world-space widget an interactable wants drawn.
 *
 * Authored on UInteractableComponent, resolved into live widgets by the interactor. The
 * spec is shared, immutable data on the actor; the widgets built from it are per-viewer
 * and live on the interactor, which is what makes this correct in multiplayer without
 * anyone replicating a UI object.
 */
USTRUCT(BlueprintType)
struct TPS_TEST_API FInteractionWidgetSpec
{
	GENERATED_BODY()

	/** The widget to build. Soft so an interactable costs nothing until it's first seen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget")
	TSoftClassPtr<UUserWidget> WidgetClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	FVector2D DrawSizeScale = FVector2D(1.f, 1.f);

	/**
	 * Hold this widget at a fixed size on screen.
	 *
	 * Screen space only. A World space widget is a quad in the level — its size on screen
	 * is supposed to fall off with distance, and the interactor leaves those completely
	 * alone: authored scale in, authored scale out. Setting this on a World space spec
	 * does nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget",
		meta = (EditCondition = "WidgetSpace == EWidgetSpace::Screen"))
	bool bConstantWidgetSize = false;

	/**
	 * The distance, in cm from the camera, at which a constant-size widget draws at its
	 * natural size. Nearer or further, the widget is scaled to keep the same footprint.
	 *
	 * Set it to roughly the distance you were standing at when you liked how it looked.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget",
		meta = (EditCondition = "bConstantWidgetSize && WidgetSpace == EWidgetSpace::Screen", ClampMin = "1.0"))
	float ConstantSizeReferenceDistance = 300.f;

	/**
	 * Offset from the attach point.
	 *
	 * Authoring tip that survived from the Blueprint version: drop a Widget Component in
	 * the actor's viewport, drag it until it sits right, right-click the transform in the
	 * details panel to copy it, paste it here, then delete the component.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget")
	FTransform RelativeTransform = FTransform::Identity;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget")
	EWidgetSpace WidgetSpace = EWidgetSpace::World;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (EditCondition = "WidgetSpace == EWidgetSpace::World"))
	EWidgetBillboard Billboard = EWidgetBillboard::Disabled;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	EWidgetDisplayPreset DisplayPreset = EWidgetDisplayPreset::Custom;

	/**
	 * Draw this even when the actor is behind something. Off, the widget fades out when
	 * the camera loses sight of the actor's focus point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget")
	bool bIgnoresLineOfSight = false;

	/** Which presentation levels show this widget. Focused implies Ambient is superseded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget")
	bool bShowWhenAmbient = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget")
	bool bShowWhenFocused = true;

	/**
	 * Fade this widget out with distance, using the interactor's falloff curve and the
	 * interactable's AmbientRange. Off, it draws at full opacity anywhere in range.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget")
	bool bFadeWithDistance = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget", meta = (EditCondition = "bFadeWithDistance"))
	EWidgetFadeCurve FadeCurve = EWidgetFadeCurve::Linear;

	bool IsValidSpec() const { return !WidgetClass.IsNull(); }
};

/**
 * A live widget built from a spec, owned by the interactor that built it.
 *
 * Never replicated and never authored — purely the interactor's bookkeeping so it can
 * find, fade, animate and tear down what it spawned.
 */
USTRUCT()
struct TPS_TEST_API FInteractionWidgetInstance
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Widget = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> Component = nullptr;

	UPROPERTY(Transient)
	bool bIgnoresLineOfSight = false;

	UPROPERTY(Transient)
	bool bShowWhenAmbient = false;

	UPROPERTY(Transient)
	bool bShowWhenFocused = true;
	
	UPROPERTY(Transient)
	EWidgetBillboard Billboard = EWidgetBillboard::Disabled;
	
	UPROPERTY(Transient)
	bool bFadeWithDistance = true;
	
	UPROPERTY(Transient)
	bool bConstantWidgetSize = false;

	/**
	 * Cached from the spec at build time.
	 *
	 * The scale pass used to ask the live UWidgetComponent for its widget space every
	 * frame; caching it here means the decision is made once, from the authored data,
	 * and can never disagree with what the tick-enable bookkeeping decided. Constant
	 * size applies to Screen only; billboarding applies to World only.
	 */
	UPROPERTY(Transient)
	EWidgetSpace WidgetSpace = EWidgetSpace::World;

	/**
	 * The widget's own render scale as authored in UMG, captured the first time the
	 * constant-size pass touches it.
	 *
	 * Screen space widget components ignore component scale entirely — the screen layer
	 * positions them at DrawSize in pixels — so the only lever that changes their size is
	 * the UUserWidget's render transform. Constant-size scaling MULTIPLIES this rather
	 * than replacing it.
	 */
	UPROPERTY(Transient)
	FVector2D BaseRenderScale = FVector2D(1.f, 1.f);

	UPROPERTY(Transient)
	float ConstantSizeReferenceDistance = 300.f;

	/** Resolved once at build: this instance needs the per-frame scale pass. */
	UPROPERTY(Transient)
	bool bWantsScaleTick = false;

	/** Resolved once at build: this instance needs the per-frame billboard pass. */
	UPROPERTY(Transient)
	bool bWantsBillboardTick = false;
	
	UPROPERTY(Transient)
	EWidgetFadeCurve FadeCurve = EWidgetFadeCurve::Linear;

	bool IsValidInstance() const { return ::IsValid(Widget) && ::IsValid(Component); }
};

/**
 * Everything one interactor has built for one interactable.
 *
 * The whole reason per-viewer state lives here rather than on the interactable: two
 * players looking at the same crate each get their own set, and neither can see or
 * clobber the other's. The Blueprint original stored this on the interactable actor,
 * which only worked because the widgets happened to be client-only.
 */
USTRUCT()
struct TPS_TEST_API FInteractionWidgetSet
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<FInteractionWidgetInstance> Widgets;

	UPROPERTY(Transient)
	EInteractionPresentation Presentation = EInteractionPresentation::None;

	/** Last line-of-sight result, so we only push visibility changes on transitions. */
	UPROPERTY(Transient)
	bool bInLineOfSight = true;

	UPROPERTY(Transient)
	bool bBuilt = false;
};

// ----------------------------------------------------------------------------- delegates

/** Focus moved. Either actor may be null (gained focus / lost focus). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionFocusChanged, AActor*, NewFocus, AActor*, OldFocus);

/** Hold progress, 0..1. Fires on the local client only; drives progress rings. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionProgress, AActor*, Target, float, Progress);

/** An interaction committed. Fires on the server and on every client that sees it. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionPerformed, AActor*, Target, AActor*, Instigator);

/** An interaction started but did not commit. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInteractionAborted, AActor*, Target, EInteractionAbortReason, Reason);

/** Availability of an interactable changed (locked/unlocked, depleted, disabled). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteractableAvailabilityChanged, bool, bAvailable);