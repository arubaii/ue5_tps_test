// Copyright AR GameSystems 2026. All rights reserved.

#include "DiegeticInteractorComponent.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/WidgetComponent.h"
#include "DiegeticScreenComponent.h"
#include "DiegeticUISettings.h"
#include "DiegeticUISubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "TimerManager.h"
#include "Widgets/SWidget.h"

namespace DiegeticInteractorPrivate
{
	/** Navigation held repeats after this long, then this often (seconds) - as Slate's own key repeat. */
	constexpr double NavigationRepeatDelay = 0.4;
	constexpr double NavigationRepeatInterval = 0.12;

	/** How far past the screen the camera may be and still have its aim counted (cm): the camera's distance behind the pawn. */
	constexpr double CameraSlack = 1000.0;

	/** The first widget on a screen that can take focus, for a gamepad starting to use it with nothing marked to focus. */
	UWidget* FindFirstFocusable(UUserWidget* Root)
	{
		if (!Root || !Root->WidgetTree)
		{
			return nullptr;
		}
		UWidget* First = nullptr;
		Root->WidgetTree->ForEachWidget([&First](UWidget* Each)
		{
			if (First || !Each || !Each->IsVisible())
			{
				return;
			}
			const TSharedPtr<SWidget> Slate = Each->GetCachedWidget();
			if (Slate.IsValid() && Slate->SupportsKeyboardFocus())
			{
				First = Each;
			}
		});
		return First;
	}

	/** The key Slate's navigation reads for a direction. The D-pad's: every navigation config has them. */
	FKey NavigationKey(EUINavigation Direction)
	{
		switch (Direction)
		{
		case EUINavigation::Up:    return EKeys::Gamepad_DPad_Up;
		case EUINavigation::Down:  return EKeys::Gamepad_DPad_Down;
		case EUINavigation::Left:  return EKeys::Gamepad_DPad_Left;
		case EUINavigation::Right: return EKeys::Gamepad_DPad_Right;
		default:                   return EKeys::Invalid;
		}
	}
}

UDiegeticInteractorComponent::UDiegeticInteractorComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// It works out its own hit (UpdateAim) and hands it over as the Custom one.
	InteractionSource = EWidgetInteractionSource::Custom;
	InteractionDistance = 400.f;
	// Active from the start: activating is what makes its virtual Slate user, without which it can't send anything.
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

// =====================================================================================================
// Lifetime
// =====================================================================================================

void UDiegeticInteractorComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UDiegeticUISubsystem* Subsystem = UDiegeticUISubsystem::Get(this))
	{
		Subsystem->RegisterInteractor(this);
	}
}

void UDiegeticInteractorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndUsing();
	ReleaseHeldPointers();
	if (UDiegeticScreenComponent* Screen = GetAimedScreen())
	{
		Screen->NotifyAimed(this, false);
	}
	AimedComponent.Reset();
	ReleaseInputBinding();
	if (UDiegeticUISubsystem* Subsystem = UDiegeticUISubsystem::Get(this))
	{
		Subsystem->UnregisterInteractor(this);
	}
	Super::EndPlay(EndPlayReason);
}

void UDiegeticInteractorComponent::ForgetScreen(UDiegeticScreenComponent* Screen)
{
	if (UsedScreen.Get() == Screen)
	{
		EndUsing();
	}
	for (auto It = HeldPointers.CreateIterator(); It; ++It)
	{
		if (It.Value().Get() == Screen)
		{
			It.Value().Reset();
		}
	}
	if (AimedComponent.Get() == Screen)
	{
		AimedComponent.Reset();
		OnAimedScreenChanged.Broadcast(nullptr);
	}
}

APlayerController* UDiegeticInteractorComponent::GetInteractingPlayer() const
{
	AActor* OwningActor = GetOwner();
	if (APlayerController* AsController = Cast<APlayerController>(OwningActor))
	{
		return AsController;
	}
	if (const APawn* AsPawn = Cast<APawn>(OwningActor))
	{
		return Cast<APlayerController>(AsPawn->GetController());
	}
	return OwningActor ? Cast<APlayerController>(OwningActor->GetInstigatorController()) : nullptr;
}

UDiegeticInteractorComponent* UDiegeticInteractorComponent::FindInteractor(const APlayerController* Player)
{
	if (!Player)
	{
		return nullptr;
	}
	if (const UDiegeticUISubsystem* Subsystem = UDiegeticUISubsystem::Get(Player))
	{
		if (UDiegeticInteractorComponent* Found = Subsystem->FindInteractor(Player))
		{
			return Found;
		}
	}
	// Not registered yet (its BeginPlay hasn't run): on the pawn, or on the controller.
	if (const APawn* Pawn = Player->GetPawn())
	{
		if (UDiegeticInteractorComponent* OnPawn = Pawn->FindComponentByClass<UDiegeticInteractorComponent>())
		{
			return OnPawn;
		}
	}
	return Player->FindComponentByClass<UDiegeticInteractorComponent>();
}

UDiegeticScreenComponent* UDiegeticInteractorComponent::GetAimedScreen() const
{
	return Cast<UDiegeticScreenComponent>(AimedComponent.Get());
}

// =====================================================================================================
// Tick
// =====================================================================================================

void UDiegeticInteractorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UpdateInputBinding();

	// The hit first: the engine's interaction (Super) routes the pointer to whatever it is given.
	if (InteractionSource == EWidgetInteractionSource::Custom)
	{
		UpdateAim();
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateAimedState();
	UpdateInputContexts();

	if (!bUsingEnhancedInput && BoundPlayer.IsValid())
	{
		PollDefaultKeys();
	}

	// Navigation held repeats, as a held key does in Slate.
	if (HeldNavigation.IsSet() && UsedScreen.IsValid())
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextNavigationRepeat)
		{
			NextNavigationRepeat = Now + DiegeticInteractorPrivate::NavigationRepeatInterval;
			SendNavigation(HeldNavigation.GetValue());
		}
	}
}

bool UDiegeticInteractorComponent::GetAimRay(FVector& OutOrigin, FVector& OutDirection, FVector& OutReachFrom) const
{
	APlayerController* Player = GetInteractingPlayer();
	const APawn* Pawn = Player ? static_cast<const APawn*>(Player->GetPawn()) : Cast<APawn>(GetOwner());
	OutReachFrom = Pawn ? Pawn->GetActorLocation() : GetComponentLocation();

	// A screen in use with the cursor showing is pointed at with the cursor, whatever the source.
	const UDiegeticScreenComponent* Using = UsedScreen.Get();
	const bool bCursor = Player && Player->bShowMouseCursor && (AimSource == EDiegeticAimSource::MouseCursor || (Using && Using->UseSettings.bShowMouseCursor));
	if (bCursor)
	{
		return Player->DeprojectMousePositionToWorld(OutOrigin, OutDirection);
	}
	if (Using)
	{
		// In use without a cursor (a gamepad): focus does the pointing.
		return false;
	}

	switch (AimSource)
	{
	case EDiegeticAimSource::Camera:
	{
		if (!Player)
		{
			return false;
		}
		FRotator ViewRotation;
		Player->GetPlayerViewPoint(OutOrigin, ViewRotation);
		OutDirection = ViewRotation.Vector();
		return true;
	}
	case EDiegeticAimSource::MouseCursor:
		// The cursor isn't showing: nothing to point with.
		return false;

	default:
		OutOrigin = GetComponentLocation();
		OutDirection = GetForwardVector();
		OutReachFrom = OutOrigin;
		return true;
	}
}

void UDiegeticInteractorComponent::UpdateAim()
{
	FHitResult Aim;
	FVector Origin;
	FVector Direction;
	FVector ReachFrom;
	UWorld* World = GetWorld();
	UDiegeticUISubsystem* Subsystem = UDiegeticUISubsystem::Get(this);
	if (!World || !GetAimRay(Origin, Direction, ReachFrom))
	{
		SetCustomHitResult(Aim);
		return;
	}

	UDiegeticScreenComponent* Using = UsedScreen.Get();
	const double Reach = FMath::Max(static_cast<double>(InteractionDistance), 1.0);
	const double RayLength = Using ? 100000.0 : Reach + FVector::Dist(Origin, ReachFrom) + DiegeticInteractorPrivate::CameraSlack;
	const FVector RayEnd = Origin + Direction * RayLength;

	// The nearest Diegetic Screen along the ray: a flat one by its plane (no collision needed), a curved one by its collision.
	UDiegeticScreenComponent* Best = nullptr;
	double BestDistance = RayLength;
	FVector BestNormal = FVector::ZeroVector;
	TArray<UDiegeticScreenComponent*, TInlineAllocator<4>> UsingGroup;
	if (Using)
	{
		TArray<UDiegeticScreenComponent*> Group;
		GetFocusGroup(Using, Group);
		UsingGroup.Append(Group);
	}
	FVector BestPanelPoint = FVector::ZeroVector;
	auto Consider = [&](UDiegeticScreenComponent* Screen)
	{
		const bool bInUsedGroup = UsingGroup.Contains(Screen);
		if (!Screen || (!bInUsedGroup && !Screen->CanBeAimedAt()) || Screen->GetWorld() != World)
		{
			return;
		}
		const FVector2D Pixels = Screen->GetCurrentDrawSize();
		if (Pixels.X <= 0.0 || Pixels.Y <= 0.0)
		{
			return;
		}

		double Distance = 0.0;
		FVector Point;
		FVector Normal;
		FVector PanelPoint;
		if (Screen->GetDisplayMesh())
		{
			// A Mesh Surface: the mesh's slot, by its collision's UV; handed on as the same place on the screen's own panel.
			if (!Screen->TraceMeshSurface(Origin, RayEnd, PanelPoint, Point, Normal, Distance) || Distance >= BestDistance)
			{
				return;
			}
		}
		else if (Screen->GetGeometryMode() == EWidgetGeometryMode::Plane)
		{
			const FTransform& Transform = Screen->GetComponentTransform();
			Normal = Transform.GetUnitAxis(EAxis::X);
			const double Facing = FVector::DotProduct(Direction, Normal);
			// Edge on, or - one-sided - from behind (its face looks along +X: a ray from the front runs against it).
			if (FMath::Abs(Facing) < UE_KINDA_SMALL_NUMBER || (!Screen->GetTwoSided() && Facing > 0.0))
			{
				return;
			}
			Distance = FVector::DotProduct(Transform.GetLocation() - Origin, Normal) / Facing;
			if (Distance < 0.0 || Distance >= BestDistance)
			{
				return;
			}
			Point = Origin + Direction * Distance;
			FVector2D OnWidget;
			Screen->GetLocalHitLocation(Point, OnWidget);
			if (OnWidget.X < 0.0 || OnWidget.Y < 0.0 || OnWidget.X > Pixels.X || OnWidget.Y > Pixels.Y)
			{
				return;
			}
			if (Facing > 0.0)
			{
				Normal = -Normal;
			}
			PanelPoint = Point;
		}
		else
		{
			FHitResult OnCurve;
			const FCollisionQueryParams CurveParams(SCENE_QUERY_STAT(DiegeticInteractorCurve), /*bTraceComplex=*/true);
			if (!Screen->LineTraceComponent(OnCurve, Origin, RayEnd, CurveParams))
			{
				return;
			}
			Distance = OnCurve.Distance;
			Point = OnCurve.ImpactPoint;
			Normal = OnCurve.ImpactNormal;
			PanelPoint = Point;
			if (Distance >= BestDistance)
			{
				return;
			}
		}

		// Within reach of the pawn (the screen's own, or this interactor's), unless it is in use (or of the group in use).
		const float ScreenReach = Screen->UseSettings.ReachDistance > 0.f ? Screen->UseSettings.ReachDistance : InteractionDistance;
		if (!bInUsedGroup && FVector::Dist(ReachFrom, Point) > ScreenReach)
		{
			return;
		}
		Best = Screen;
		BestDistance = Distance;
		BestNormal = Normal;
		BestPanelPoint = PanelPoint;
	};

	if (Using)
	{
		for (UDiegeticScreenComponent* Member : UsingGroup)
		{
			Consider(Member);
		}
	}
	else if (Subsystem)
	{
		for (const TWeakObjectPtr<UDiegeticScreenComponent>& Screen : Subsystem->GetScreens())
		{
			Consider(Screen.Get());
		}
	}

	// One trace along the ray: what stands in front of the screen found - a wall, a door - and, with no screen found, a plain
	// Widget Component hit by its collision. The pawn (and the screen's own actor - its frame) don't count.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(DiegeticInteractor), /*bTraceComplex=*/false);
	if (AActor* OwningActor = GetOwner())
	{
		Params.AddIgnoredActor(OwningActor);
	}
	if (APlayerController* Player = GetInteractingPlayer())
	{
		if (APawn* Pawn = Player->GetPawn())
		{
			Params.AddIgnoredActor(Pawn);
		}
	}
	if (Best && Best->GetOwner())
	{
		Params.AddIgnoredActor(Best->GetOwner());
	}
	if (Best && Best->GetDisplayMesh() && Best->GetDisplayMesh()->GetOwner())
	{
		Params.AddIgnoredActor(Best->GetDisplayMesh()->GetOwner());
	}
	FHitResult Blocking;
	const bool bBlocked = !Using && World->LineTraceSingleByChannel(Blocking, Origin, RayEnd, TraceChannel, Params);

	if (Best && bBlocked && Blocking.Distance < BestDistance - 1.0 && Blocking.GetComponent() != Best)
	{
		Best = nullptr;
	}

	if (Best)
	{
		// On its panel (for a Mesh Surface, the undrawn panel's matching place): the engine's interaction maps that to pixels.
		Aim = FHitResult(Best->GetOwner(), Best, BestPanelPoint, BestNormal);
		Aim.bBlockingHit = true;
		Aim.TraceStart = Origin;
		Aim.TraceEnd = RayEnd;
		Aim.Distance = static_cast<float>(BestDistance);
		Aim.Time = static_cast<float>(BestDistance / RayLength);
	}
	else if (bAimAtWidgetComponents && bBlocked)
	{
		UWidgetComponent* Plain = Cast<UWidgetComponent>(Blocking.GetComponent());
		const UDiegeticScreenComponent* AsScreen = Cast<UDiegeticScreenComponent>(Plain);
		if (Plain && !AsScreen && FVector::Dist(ReachFrom, Blocking.ImpactPoint) <= InteractionDistance)
		{
			Aim = Blocking;
			Aim.TraceStart = Origin;
			Aim.TraceEnd = RayEnd;
		}
	}
	SetCustomHitResult(Aim);
}

void UDiegeticInteractorComponent::UpdateAimedState()
{
	UWidgetComponent* Hovered = GetHoveredWidgetComponent();
	UWidgetComponent* Previous = AimedComponent.Get();
	if (Hovered != Previous)
	{
		if (UDiegeticScreenComponent* Left = Cast<UDiegeticScreenComponent>(Previous))
		{
			Left->NotifyAimed(this, false);
		}
		AimedComponent = Hovered;
		if (UDiegeticScreenComponent* Entered = Cast<UDiegeticScreenComponent>(Hovered))
		{
			Entered->NotifyAimed(this, true);
		}
		OnAimedScreenChanged.Broadcast(Hovered);
	}

	if (UDiegeticScreenComponent* Screen = Cast<UDiegeticScreenComponent>(Hovered))
	{
		Screen->NotePointerPosition(Get2DHitLocation());
	}
}

// =====================================================================================================
// Pointer
// =====================================================================================================

void UDiegeticInteractorComponent::PressScreenPointer(FKey Button)
{
	if (HeldPointers.Contains(Button))
	{
		return;
	}
	UDiegeticScreenComponent* Screen = GetAimedScreen();
	if (!Screen && !AimedComponent.IsValid())
	{
		return;
	}
	HeldPointers.Add(Button, Screen);
	if (Screen)
	{
		Screen->NotePointerButton(Button, true, Get2DHitLocation());
	}
	PressPointerKey(Button);
}

void UDiegeticInteractorComponent::ReleaseScreenPointer(FKey Button)
{
	TWeakObjectPtr<UDiegeticScreenComponent> PressedOn;
	if (!HeldPointers.RemoveAndCopyValue(Button, PressedOn))
	{
		return;
	}
	if (UDiegeticScreenComponent* Screen = PressedOn.Get())
	{
		// Where it is let go: on the screen it was pressed on, or - pointed off it - where it last was there.
		Screen->NotePointerButton(Button, false, GetAimedScreen() == Screen ? Get2DHitLocation() : FVector2D::ZeroVector);
	}
	ReleasePointerKey(Button);
}

void UDiegeticInteractorComponent::ReleaseHeldPointers()
{
	TArray<FKey> Held;
	HeldPointers.GetKeys(Held);
	for (const FKey& Button : Held)
	{
		ReleaseScreenPointer(Button);
	}
	if (bAcceptHeld)
	{
		bAcceptHeld = false;
		ReleaseKey(EKeys::Enter);
	}
	HeldNavigation.Reset();
}

void UDiegeticInteractorComponent::ClickScreen(bool bPressed)
{
	UDiegeticScreenComponent* Screen = GetAimedScreen();
	if (bPressed && Screen && !UsedScreen.IsValid() && Screen->UseSettings.UseMode == EDiegeticUseMode::FocusView)
	{
		BeginUsing(Screen);
		return;
	}
	if (bPressed)
	{
		PressScreenPointer(EKeys::LeftMouseButton);
	}
	else
	{
		ReleaseScreenPointer(EKeys::LeftMouseButton);
	}
}

void UDiegeticInteractorComponent::BackFromScreen()
{
	UDiegeticScreenComponent* Screen = UsedScreen.Get();
	if (!Screen)
	{
		return;
	}
	UUserWidget* Root = Screen->GetSafeWidget();
	if (Root && Root->Implements<UDiegeticScreenWidget>() && IDiegeticScreenWidget::Execute_OnScreenBack(Root, Screen))
	{
		return;
	}
	EndUsing();
}

void UDiegeticInteractorComponent::NavigateScreen(EUINavigation Direction)
{
	SendNavigation(Direction);
}

void UDiegeticInteractorComponent::SendNavigation(EUINavigation Direction)
{
	if (!UsedScreen.IsValid())
	{
		return;
	}
	const FKey Key = DiegeticInteractorPrivate::NavigationKey(Direction);
	if (Key.IsValid())
	{
		// To the focus of this interactor's virtual user: Slate moves it when nothing handles the key.
		PressKey(Key);
		ReleaseKey(Key);
		if (UDiegeticScreenComponent* Screen = UsedScreen.Get())
		{
			Screen->MarkContentChanged();
		}
	}
}

// =====================================================================================================
// Focus View
// =====================================================================================================

void UDiegeticInteractorComponent::GetFocusGroup(UDiegeticScreenComponent* Screen, TArray<UDiegeticScreenComponent*>& OutScreens)
{
	OutScreens.Reset();
	if (!Screen)
	{
		return;
	}
	OutScreens.Add(Screen);
	const FName Group = Screen->UseSettings.FocusGroup;
	const AActor* OwningActor = Screen->GetOwner();
	if (Group.IsNone() || !OwningActor)
	{
		return;
	}
	TInlineComponentArray<UDiegeticScreenComponent*> OnActor(OwningActor);
	for (UDiegeticScreenComponent* Other : OnActor)
	{
		if (Other != Screen && Other->UseSettings.FocusGroup == Group && Other->IsVisible())
		{
			OutScreens.Add(Other);
		}
	}
}

FTransform UDiegeticInteractorComponent::ComputeFocusViewTransform(const UDiegeticScreenComponent* Screen, float FieldOfView) const
{
	const FTransform& ScreenTransform = Screen->GetComponentTransform();
	const FVector Face = ScreenTransform.GetUnitAxis(EAxis::X);
	const FVector Across = ScreenTransform.GetUnitAxis(EAxis::Y);
	const FVector Up = ScreenTransform.GetUnitAxis(EAxis::Z);

	// What to frame: the screen - with its Focus Group, the box around all of them, measured across and up the used screen.
	TArray<UDiegeticScreenComponent*> Group;
	GetFocusGroup(const_cast<UDiegeticScreenComponent*>(Screen), Group);
	FVector2D Low(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D High(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
	const FVector Anchor = Screen->GetDisplayCenter();
	double Depth = 0.0;
	for (const UDiegeticScreenComponent* Member : Group)
	{
		const FVector MemberCenter = Member->GetDisplayCenter();
		const FVector2D MemberSize = Member->GetDisplayWorldSize() * 0.5;
		const FVector MemberAcross = Member->GetRightVector();
		const FVector MemberUp = Member->GetUpVector();
		for (const FVector2D Corner : { FVector2D(-1.0, -1.0), FVector2D(1.0, -1.0), FVector2D(-1.0, 1.0), FVector2D(1.0, 1.0) })
		{
			const FVector Point = MemberCenter + MemberAcross * (Corner.X * MemberSize.X) + MemberUp * (Corner.Y * MemberSize.Y) - Anchor;
			const FVector2D OnPlane(FVector::DotProduct(Point, Across), FVector::DotProduct(Point, Up));
			Low = FVector2D(FMath::Min(Low.X, OnPlane.X), FMath::Min(Low.Y, OnPlane.Y));
			High = FVector2D(FMath::Max(High.X, OnPlane.X), FMath::Max(High.Y, OnPlane.Y));
			Depth = FMath::Max(Depth, FVector::DotProduct(Point, Face));
		}
	}
	const FVector2D Middle = (Low + High) * 0.5;
	const FVector Center = Anchor + Across * Middle.X + Up * Middle.Y + Face * Depth;
	const FVector2D Extent = High - Low;

	// Far enough back that the screen fills View Fill of the view, across or up - whichever is tighter.
	double Aspect = 16.0 / 9.0;
	if (const APlayerController* Player = GetInteractingPlayer())
	{
		int32 SizeX = 0;
		int32 SizeY = 0;
		Player->GetViewportSize(SizeX, SizeY);
		if (SizeX > 0 && SizeY > 0)
		{
			Aspect = static_cast<double>(SizeX) / SizeY;
		}
	}
	const double Fill = FMath::Clamp(static_cast<double>(Screen->UseSettings.ViewFill), 0.2, 1.0);
	const double HalfHorizontal = FMath::DegreesToRadians(FMath::Clamp(static_cast<double>(FieldOfView), 10.0, 170.0)) * 0.5;
	const double HalfVertical = FMath::Atan(FMath::Tan(HalfHorizontal) / Aspect);
	const double ForWidth = (Extent.X * 0.5) / FMath::Tan(HalfHorizontal) / Fill;
	const double ForHeight = (Extent.Y * 0.5) / FMath::Tan(HalfVertical) / Fill;
	const double Distance = FMath::Max(FMath::Max(ForWidth, ForHeight), 10.0);

	const FVector Offset = Screen->UseSettings.CameraOffset;
	const FVector Location = Center + Face * Distance + ScreenTransform.TransformVectorNoScale(Offset);
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(-Face, Up).Rotator();
	return FTransform(Rotation, Location);
}

bool UDiegeticInteractorComponent::BeginUsing(UDiegeticScreenComponent* Screen)
{
	APlayerController* Player = GetInteractingPlayer();
	UWorld* World = GetWorld();
	if (!Screen || !Player || !World || Screen->UseSettings.UseMode == EDiegeticUseMode::None)
	{
		return false;
	}
	if (UsedScreen.Get() == Screen)
	{
		return true;
	}
	if (Screen->IsInUse())
	{
		return false;
	}
	EndUsing();
	ReleaseHeldPointers();

	UsedScreen = Screen;
	const FDiegeticUseSettings& Use = Screen->UseSettings;

	if (Use.bBlockPawnInput)
	{
		Player->SetIgnoreMoveInput(true);
		Player->SetIgnoreLookInput(true);
		bBlockedPawnInput = true;
	}

	if (Use.bShowMouseCursor)
	{
		bCursorShownBeforeUse = Player->bShowMouseCursor;
		bChangedCursor = true;
		Player->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Player->SetInputMode(Mode);
	}

	// The camera: a camera actor of its own in front of the screen, moved with it, blended to. Not when the screen is used where
	// it is (Move Camera off: a menu in front of the player, a device raised into view).
	const float FieldOfView = Player->PlayerCameraManager ? Player->PlayerCameraManager->GetFOVAngle() : 90.f;
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform View = ComputeFocusViewTransform(Screen, FieldOfView);
	ACameraActor* Camera = Use.bMoveCamera ? World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), View, Spawn) : nullptr;
	if (Camera)
	{
		Camera->GetCameraComponent()->SetFieldOfView(FieldOfView);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		Camera->AttachToComponent(Screen, FAttachmentTransformRules::KeepWorldTransform);
		FocusCamera = Camera;
		ViewTargetBeforeUse = Player->GetViewTarget();
		Player->SetViewTargetWithBlend(Camera, Use.CameraBlendTime, VTBlend_Cubic);
	}

	// A gamepad uses it by focus: its widget's Desired Focus, else the first widget that takes focus.
	if (UUserWidget* Root = Screen->GetSafeWidget())
	{
		UWidget* Target = Root->GetDesiredFocusWidget();
		if (!Target)
		{
			Target = DiegeticInteractorPrivate::FindFirstFocusable(Root);
		}
		if (Target)
		{
			SetFocus(Target);
		}
	}

	Screen->SetUserInteractor(this);
	OnUseBegin.Broadcast(Screen);
	UpdateInputContexts();
	return true;
}

void UDiegeticInteractorComponent::EndUsing()
{
	UDiegeticScreenComponent* Screen = UsedScreen.Get();
	const bool bWasUsing = UsedScreen.IsValid() || FocusCamera.IsValid() || bBlockedPawnInput || bChangedCursor;
	if (!bWasUsing)
	{
		return;
	}
	ReleaseHeldPointers();
	UsedScreen.Reset();

	APlayerController* Player = GetInteractingPlayer();
	const float BlendTime = Screen ? Screen->UseSettings.CameraBlendTime : 0.f;
	if (Player)
	{
		if (FocusCamera.IsValid() && Player->GetViewTarget() == FocusCamera.Get())
		{
			AActor* Back = ViewTargetBeforeUse.Get();
			if (!Back || Back == FocusCamera.Get())
			{
				Back = Player->GetPawn() ? static_cast<AActor*>(Player->GetPawn()) : static_cast<AActor*>(Player);
			}
			Player->SetViewTargetWithBlend(Back, BlendTime, VTBlend_Cubic);
		}
		if (bChangedCursor)
		{
			Player->bShowMouseCursor = bCursorShownBeforeUse;
			if (!bCursorShownBeforeUse)
			{
				Player->SetInputMode(FInputModeGameOnly());
			}
		}
		if (bBlockedPawnInput)
		{
			Player->SetIgnoreMoveInput(false);
			Player->SetIgnoreLookInput(false);
		}
	}
	bChangedCursor = false;
	bBlockedPawnInput = false;

	// The camera stays until the blend away from it is over: the blend reads it.
	if (ACameraActor* Camera = FocusCamera.Get())
	{
		UWorld* World = GetWorld();
		if (World && BlendTime > 0.f)
		{
			const TWeakObjectPtr<ACameraActor> WeakCamera = Camera;
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakCamera]()
			{
				if (ACameraActor* Expired = WeakCamera.Get())
				{
					Expired->Destroy();
				}
			}), BlendTime + 0.1f, false);
		}
		else
		{
			Camera->Destroy();
		}
	}
	FocusCamera.Reset();
	ViewTargetBeforeUse.Reset();

	if (Screen)
	{
		Screen->SetUserInteractor(nullptr);
		OnUseEnd.Broadcast(Screen);
	}
	UpdateInputContexts();
}

// =====================================================================================================
// Input
// =====================================================================================================

void UDiegeticInteractorComponent::UpdateInputBinding()
{
	APlayerController* Player = bBindInput ? GetInteractingPlayer() : nullptr;
	if (Player && !Player->IsLocalController())
	{
		Player = nullptr;
	}
	if (Player == BoundPlayer.Get() && (Player == nullptr || !bUsingEnhancedInput || BoundInputComponent))
	{
		return;
	}
	ReleaseInputBinding();
	if (!Player)
	{
		return;
	}
	BoundPlayer = Player;

	// Without Enhanced Input (a project on the old input system): the Default Keys are read off the player each frame.
	bUsingEnhancedInput = Cast<UEnhancedPlayerInput>(Player->PlayerInput) != nullptr && Player->GetLocalPlayer() != nullptr;
	if (!bUsingEnhancedInput)
	{
		return;
	}

	BuildDefaultInput();

	AActor* OwningActor = GetOwner();
	UEnhancedInputComponent* Bindings = NewObject<UEnhancedInputComponent>(OwningActor ? OwningActor : static_cast<AActor*>(Player), NAME_None, RF_Transient);
	Bindings->RegisterComponent();
	auto Bind = [this, Bindings](UInputAction* Action, ETriggerEvent Event, void (UDiegeticInteractorComponent::*Handler)())
	{
		if (Action)
		{
			Bindings->BindAction(Action, Event, this, Handler);
		}
	};
	Bind(ClickAction, ETriggerEvent::Started, &UDiegeticInteractorComponent::HandleClickPressed);
	Bind(ClickAction, ETriggerEvent::Completed, &UDiegeticInteractorComponent::HandleClickReleased);
	Bind(SecondaryClickAction, ETriggerEvent::Started, &UDiegeticInteractorComponent::HandleSecondaryPressed);
	Bind(SecondaryClickAction, ETriggerEvent::Completed, &UDiegeticInteractorComponent::HandleSecondaryReleased);
	Bind(BackAction, ETriggerEvent::Started, &UDiegeticInteractorComponent::HandleBack);
	Bind(AcceptAction, ETriggerEvent::Started, &UDiegeticInteractorComponent::HandleAcceptPressed);
	Bind(AcceptAction, ETriggerEvent::Completed, &UDiegeticInteractorComponent::HandleAcceptReleased);
	Bind(NavigateUpAction, ETriggerEvent::Started, &UDiegeticInteractorComponent::HandleNavigateUp);
	Bind(NavigateDownAction, ETriggerEvent::Started, &UDiegeticInteractorComponent::HandleNavigateDown);
	Bind(NavigateLeftAction, ETriggerEvent::Started, &UDiegeticInteractorComponent::HandleNavigateLeft);
	Bind(NavigateRightAction, ETriggerEvent::Started, &UDiegeticInteractorComponent::HandleNavigateRight);
	for (UInputAction* Navigate : { NavigateUpAction.Get(), NavigateDownAction.Get(), NavigateLeftAction.Get(), NavigateRightAction.Get() })
	{
		Bind(Navigate, ETriggerEvent::Completed, &UDiegeticInteractorComponent::HandleNavigateReleased);
	}
	if (ScrollAction)
	{
		Bindings->BindAction(ScrollAction, ETriggerEvent::Triggered, this, &UDiegeticInteractorComponent::HandleScroll);
	}
	if (ScrollStickAction)
	{
		Bindings->BindAction(ScrollStickAction, ETriggerEvent::Triggered, this, &UDiegeticInteractorComponent::HandleScrollStick);
	}

	Player->PushInputComponent(Bindings);
	BoundInputComponent = Bindings;
}

void UDiegeticInteractorComponent::ReleaseInputBinding()
{
	APlayerController* Player = BoundPlayer.Get();
	if (Player)
	{
		if (ULocalPlayer* Local = Player->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Local))
			{
				if (bAimContextAdded && BoundAimContext)
				{
					Input->RemoveMappingContext(BoundAimContext);
				}
				if (bUseContextAdded && BoundUseContext)
				{
					Input->RemoveMappingContext(BoundUseContext);
				}
			}
		}
		if (BoundInputComponent)
		{
			Player->PopInputComponent(BoundInputComponent);
		}
	}
	if (BoundInputComponent)
	{
		BoundInputComponent->DestroyComponent();
	}
	BoundInputComponent = nullptr;
	bAimContextAdded = false;
	bUseContextAdded = false;
	bUsingEnhancedInput = false;
	BoundPlayer.Reset();
}

void UDiegeticInteractorComponent::BuildDefaultInput()
{
	// The project's own contexts and actions win; what is missing is made from the Default Keys - once: a player bound again
	// (a new pawn possessed) gets the same ones.
	if (!AimInput && !GeneratedAimContext)
	{
		GeneratedAimContext = NewObject<UInputMappingContext>(this, MakeUniqueObjectName(this, UInputMappingContext::StaticClass(), TEXT("DiegeticAimInput")), RF_Transient);
		bFillAimContext = true;
	}
	if (!UseInput && !GeneratedUseContext)
	{
		GeneratedUseContext = NewObject<UInputMappingContext>(this, MakeUniqueObjectName(this, UInputMappingContext::StaticClass(), TEXT("DiegeticUseInput")), RF_Transient);
		bFillUseContext = true;
	}
	BoundAimContext = AimInput ? AimInput.Get() : GeneratedAimContext.Get();
	BoundUseContext = UseInput ? UseInput.Get() : GeneratedUseContext.Get();
	if (!bFillAimContext && !bFillUseContext)
	{
		return;
	}

	const UDiegeticUISettings& Settings = UDiegeticUISettings::Get();
	auto MakeAction = [this](TObjectPtr<UInputAction>& Slot, const TCHAR* ActionName, EInputActionValueType Type) -> UInputAction*
	{
		if (!Slot)
		{
			UInputAction* Made = NewObject<UInputAction>(this, MakeUniqueObjectName(this, UInputAction::StaticClass(), FName(ActionName)), RF_Transient);
			Made->ValueType = Type;
			DefaultActions.Add(Made);
			Slot = Made;
		}
		return Slot;
	};
	auto MapKeys = [](UInputMappingContext* Context, UInputAction* Action, const TArray<FKey>& Keys, bool bDeadZone)
	{
		for (const FKey& Key : Keys)
		{
			if (!Key.IsValid())
			{
				continue;
			}
			FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
			if (bDeadZone)
			{
				Mapping.Modifiers.Add(NewObject<UInputModifierDeadZone>(Context));
			}
		}
	};

	if (bFillAimContext)
	{
		bFillAimContext = false;
		UInputMappingContext* Context = GeneratedAimContext;
		MapKeys(Context, MakeAction(ClickAction, TEXT("DiegeticClick"), EInputActionValueType::Boolean), Settings.ClickKeys, false);
		MapKeys(Context, MakeAction(SecondaryClickAction, TEXT("DiegeticSecondaryClick"), EInputActionValueType::Boolean), Settings.SecondaryClickKeys, false);
		MapKeys(Context, MakeAction(ScrollAction, TEXT("DiegeticScroll"), EInputActionValueType::Axis1D), Settings.ScrollWheelKeys, false);
	}
	if (bFillUseContext)
	{
		bFillUseContext = false;
		UInputMappingContext* Context = GeneratedUseContext;
		MapKeys(Context, MakeAction(BackAction, TEXT("DiegeticBack"), EInputActionValueType::Boolean), Settings.BackKeys, false);
		MapKeys(Context, MakeAction(AcceptAction, TEXT("DiegeticAccept"), EInputActionValueType::Boolean), Settings.AcceptKeys, false);
		MapKeys(Context, MakeAction(NavigateUpAction, TEXT("DiegeticNavigateUp"), EInputActionValueType::Boolean), Settings.NavigateUpKeys, false);
		MapKeys(Context, MakeAction(NavigateDownAction, TEXT("DiegeticNavigateDown"), EInputActionValueType::Boolean), Settings.NavigateDownKeys, false);
		MapKeys(Context, MakeAction(NavigateLeftAction, TEXT("DiegeticNavigateLeft"), EInputActionValueType::Boolean), Settings.NavigateLeftKeys, false);
		MapKeys(Context, MakeAction(NavigateRightAction, TEXT("DiegeticNavigateRight"), EInputActionValueType::Boolean), Settings.NavigateRightKeys, false);
		MapKeys(Context, MakeAction(ScrollStickAction, TEXT("DiegeticScrollStick"), EInputActionValueType::Axis1D), Settings.ScrollStickKeys, true);
	}
}

void UDiegeticInteractorComponent::UpdateInputContexts()
{
	APlayerController* Player = BoundPlayer.Get();
	ULocalPlayer* Local = Player && bUsingEnhancedInput ? Player->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Input = Local ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Local) : nullptr;
	if (!Input)
	{
		return;
	}

	// Aim: only while there is something to click under the aim - a button, a field, a Focus View screen to step into - or a
	// click is held, so the game keeps its keys everywhere else. Use: while a screen is in use.
	const bool bUsing = UsedScreen.IsValid();
	const UDiegeticScreenComponent* Aimed = GetAimedScreen();
	const bool bWantAim = bUsing || HeldPointers.Num() > 0
		|| (AimedComponent.IsValid() && (IsOverInteractableWidget() || IsOverFocusableWidget()))
		|| (Aimed && Aimed->UseSettings.UseMode == EDiegeticUseMode::FocusView);
	const int32 Priority = UDiegeticUISettings::Get().InputPriority;

	// Keys already down when a context comes in are ignored until let go (Enhanced Input's default): a held trigger swept
	// onto a button doesn't click it.
	if (bWantAim != bAimContextAdded && BoundAimContext)
	{
		if (bWantAim)
		{
			Input->AddMappingContext(BoundAimContext, Priority);
		}
		else
		{
			Input->RemoveMappingContext(BoundAimContext);
		}
		bAimContextAdded = bWantAim;
	}
	if (bUsing != bUseContextAdded && BoundUseContext)
	{
		if (bUsing)
		{
			Input->AddMappingContext(BoundUseContext, Priority + 1);
		}
		else
		{
			Input->RemoveMappingContext(BoundUseContext);
		}
		bUseContextAdded = bUsing;
	}
}

void UDiegeticInteractorComponent::PollDefaultKeys()
{
	APlayerController* Player = BoundPlayer.Get();
	if (!Player)
	{
		return;
	}
	const UDiegeticUISettings& Settings = UDiegeticUISettings::Get();
	auto AnyPressed = [Player](const TArray<FKey>& Keys)
	{
		return Keys.ContainsByPredicate([Player](const FKey& Key) { return Player->WasInputKeyJustPressed(Key); });
	};
	auto AnyReleased = [Player](const TArray<FKey>& Keys)
	{
		return Keys.ContainsByPredicate([Player](const FKey& Key) { return Player->WasInputKeyJustReleased(Key); });
	};

	// Read only while there is something to act on: the old input system can't hold the keys back from the game.
	const bool bUsing = UsedScreen.IsValid();
	if (AimedComponent.IsValid() || bUsing || HeldPointers.Num() > 0)
	{
		if (AnyPressed(Settings.ClickKeys)) { HandleClickPressed(); }
		if (AnyReleased(Settings.ClickKeys)) { HandleClickReleased(); }
		if (AnyPressed(Settings.SecondaryClickKeys)) { HandleSecondaryPressed(); }
		if (AnyReleased(Settings.SecondaryClickKeys)) { HandleSecondaryReleased(); }
		for (const FKey& Key : Settings.ScrollWheelKeys)
		{
			const float Notches = Player->GetInputAnalogKeyState(Key);
			if (!FMath::IsNearlyZero(Notches))
			{
				ScrollWheel(Notches);
			}
		}
	}
	if (bUsing)
	{
		if (AnyPressed(Settings.BackKeys)) { HandleBack(); }
		if (AnyPressed(Settings.AcceptKeys)) { HandleAcceptPressed(); }
		if (AnyReleased(Settings.AcceptKeys)) { HandleAcceptReleased(); }
		if (AnyPressed(Settings.NavigateUpKeys)) { HandleNavigateUp(); }
		if (AnyPressed(Settings.NavigateDownKeys)) { HandleNavigateDown(); }
		if (AnyPressed(Settings.NavigateLeftKeys)) { HandleNavigateLeft(); }
		if (AnyPressed(Settings.NavigateRightKeys)) { HandleNavigateRight(); }
	}
}

void UDiegeticInteractorComponent::HandleClickPressed()
{
	ClickScreen(true);
}

void UDiegeticInteractorComponent::HandleClickReleased()
{
	ClickScreen(false);
}

void UDiegeticInteractorComponent::HandleSecondaryPressed()
{
	PressScreenPointer(EKeys::RightMouseButton);
}

void UDiegeticInteractorComponent::HandleSecondaryReleased()
{
	ReleaseScreenPointer(EKeys::RightMouseButton);
}

void UDiegeticInteractorComponent::HandleScroll(const FInputActionValue& Value)
{
	const float Notches = Value.Get<float>();
	if (AimedComponent.IsValid() && !FMath::IsNearlyZero(Notches))
	{
		ScrollWheel(Notches);
		if (UDiegeticScreenComponent* Screen = GetAimedScreen())
		{
			Screen->MarkContentChanged();
		}
	}
}

void UDiegeticInteractorComponent::HandleScrollStick(const FInputActionValue& Value)
{
	const UWorld* World = GetWorld();
	const float Tilt = Value.Get<float>();
	if (!World || !AimedComponent.IsValid() || FMath::IsNearlyZero(Tilt))
	{
		return;
	}
	ScrollWheel(Tilt * UDiegeticUISettings::Get().StickScrollSpeed * World->GetDeltaSeconds());
	if (UDiegeticScreenComponent* Screen = GetAimedScreen())
	{
		Screen->MarkContentChanged();
	}
}

void UDiegeticInteractorComponent::HandleBack()
{
	BackFromScreen();
}

void UDiegeticInteractorComponent::HandleAcceptPressed()
{
	if (!UsedScreen.IsValid() || bAcceptHeld)
	{
		return;
	}
	// Enter: Slate's navigation config reads it as Accept on every platform (Virtual_Accept is deprecated in 5.7).
	bAcceptHeld = true;
	PressKey(EKeys::Enter);
	UsedScreen->MarkContentChanged();
}

void UDiegeticInteractorComponent::HandleAcceptReleased()
{
	if (!bAcceptHeld)
	{
		return;
	}
	bAcceptHeld = false;
	ReleaseKey(EKeys::Enter);
}

void UDiegeticInteractorComponent::HandleNavigateUp()
{
	HeldNavigation = EUINavigation::Up;
	NextNavigationRepeat = FPlatformTime::Seconds() + DiegeticInteractorPrivate::NavigationRepeatDelay;
	SendNavigation(EUINavigation::Up);
}

void UDiegeticInteractorComponent::HandleNavigateDown()
{
	HeldNavigation = EUINavigation::Down;
	NextNavigationRepeat = FPlatformTime::Seconds() + DiegeticInteractorPrivate::NavigationRepeatDelay;
	SendNavigation(EUINavigation::Down);
}

void UDiegeticInteractorComponent::HandleNavigateLeft()
{
	HeldNavigation = EUINavigation::Left;
	NextNavigationRepeat = FPlatformTime::Seconds() + DiegeticInteractorPrivate::NavigationRepeatDelay;
	SendNavigation(EUINavigation::Left);
}

void UDiegeticInteractorComponent::HandleNavigateRight()
{
	HeldNavigation = EUINavigation::Right;
	NextNavigationRepeat = FPlatformTime::Seconds() + DiegeticInteractorPrivate::NavigationRepeatDelay;
	SendNavigation(EUINavigation::Right);
}

void UDiegeticInteractorComponent::HandleNavigateReleased()
{
	HeldNavigation.Reset();
}
