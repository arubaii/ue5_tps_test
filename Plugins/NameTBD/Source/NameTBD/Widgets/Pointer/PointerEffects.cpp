// Copyright AR GameSystems 2026. All rights reserved.

#include "PointerEffects.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Layout/ArrangedChildren.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SOverlay.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"
#include "NameTBD/WidgetAnimation/WidgetAnimInput.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachineLibrary.h"
#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Widgets/WidgetEditorHooks.h"
#include "NameTBD/Widgets/WidgetEffectsDraw.h"
#include "NameTBD/Widgets/Shadow/DropShadow.h"
#include "NameTBD/Widgets/Shapes/ShapeWidget.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"

// =====================================================================================================
// Settings
// =====================================================================================================

FPointerTiltEffect::FPointerTiltEffect()
{
	Spring.VisualDuration = 0.35f;
	Spring.Bounce = 0.1f;
}

FPointerMagnetEffect::FPointerMagnetEffect()
{
	Spring.VisualDuration = 0.45f;
	Spring.Bounce = 0.3f;
}

FPointerPressEffect::FPointerPressEffect()
{
	Spring.VisualDuration = 0.25f;
	Spring.Bounce = 0.35f;
}

namespace PointerEffectsPrivate
{
	TArray<TWeakObjectPtr<UPointerEffectsComponent>>& GetComponents()
	{
		static TArray<TWeakObjectPtr<UPointerEffectsComponent>> Components;
		return Components;
	}

	/** A soft falloff from 1 at the center to 0 at Radius. */
	float Falloff(float Distance, float Radius)
	{
		const float Near = 1.f - FMath::Clamp(Distance / FMath::Max(Radius, 1.f), 0.f, 1.f);
		return Near * Near * (3.f - 2.f * Near);
	}

	/** Cuts a mesh along a grid of lines Cell apart, so a color worked out per vertex reads smoothly across it. */
	void CutGrid(FWidgetShapeMesh& Mesh, const FVector2f& Size, float Cell)
	{
		const int32 Columns = FMath::Clamp(FMath::CeilToInt(Size.X / Cell), 1, 96);
		const int32 Rows = FMath::Clamp(FMath::CeilToInt(Size.Y / Cell), 1, 96);
		for (int32 Column = 1; Column < Columns; ++Column)
		{
			WidgetShapeGeometry::CutAtLevel(Mesh, Size.X * Column / Columns, [](const FVector2f& Point) { return Point.X; });
		}
		for (int32 Row = 1; Row < Rows; ++Row)
		{
			WidgetShapeGeometry::CutAtLevel(Mesh, Size.Y * Row / Rows, [](const FVector2f& Point) { return Point.Y; });
		}
	}

	/** Whether a widget and every panel it is in are enabled. */
	bool IsUsable(const UWidget* Widget)
	{
		for (const UWidget* Link = Widget; Link; Link = Link->GetParent())
		{
			if (!Link->GetIsEnabled())
			{
				return false;
			}
		}
		return true;
	}

	bool SpringTo(const FWidgetSpringParams& Params, float Target, FWidgetSpringState& State, float DeltaTime, bool bSnap, float RestDistance)
	{
		const float Before = State.Value;
		if (bSnap)
		{
			State.Value = Target;
			State.Velocity = 0.f;
		}
		else
		{
			UWidgetAnimSpringLibrary::AdvanceSpring(Params, Target, State, DeltaTime, RestDistance, RestDistance * 2.f);
		}
		return !FMath::IsNearlyEqual(Before, State.Value, RestDistance * 0.1f);
	}
}

// =====================================================================================================
// SPointerEffectsHost
// =====================================================================================================

void SPointerEffectsHost::Construct(const FArguments& InArgs)
{
	Surface = SNew(SPointerEffectsSurface, SharedThis(this));
	Surface->SetVisibility(EVisibility::HitTestInvisible);
	UnderSurface = SNew(SPointerEffectsSurface, SharedThis(this)).Behind(true);
	UnderSurface->SetVisibility(EVisibility::HitTestInvisible);
	TiltBox = SNew(SShapeBox)
		.Shape(TAttribute<FShapeBoxShape>::CreateSP(this, &SPointerEffectsHost::GetTiltShape))
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				UnderSurface.ToSharedRef()
			]
			+ SOverlay::Slot()
			[
				InArgs._Content.Widget
			]
			+ SOverlay::Slot()
			[
				Surface.ToSharedRef()
			]
		];
	ChildSlot
	[
		TiltBox.ToSharedRef()
	];
	ScaleSpring.Value = 1.f;
	PressSpring.Value = 1.f;
}

void SPointerEffectsHost::SetSettings(const FPointerEffectsSettings& InSettings)
{
	Settings = InSettings;
	LightGates.SetNum(Settings.Lights.Num());
	OverlayGates.SetNum(Settings.Overlays.Num());
	if (Settings.bDesignTime)
	{
		// The designer: posed once where Designer Pointer says, nothing polled.
		Step(0.f, /*bSnap=*/true);
	}
	else
	{
		Wake();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	if (UnderSurface.IsValid())
	{
		UnderSurface->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SPointerEffectsHost::CopyMotionFrom(const SPointerEffectsHost& Other)
{
	State = Other.State;
	HoverValue = Other.HoverValue;
	ProximityValue = Other.ProximityValue;
	TiltX = Other.TiltX;
	TiltY = Other.TiltY;
	LeanX = Other.LeanX;
	LeanY = Other.LeanY;
	ScaleSpring = Other.ScaleSpring;
	PressSpring = Other.PressSpring;
	TiltGate = Other.TiltGate;
	ActiveGate = Other.ActiveGate;
	LightGates = Other.LightGates;
	OverlayGates = Other.OverlayGates;
	LastLocal = Other.LastLocal;
	bHasLastLocal = Other.bHasLastLocal;
	bWasDown = Other.bWasDown;
	bWasOver = Other.bWasOver;
	bPressStartedOver = Other.bPressStartedOver;
	PressHold = Other.PressHold;
	Ripples = Other.Ripples;
	VirtualPointer = Other.VirtualPointer;
}

void SPointerEffectsHost::SetVirtualPointer(TOptional<FVector2D> InUV)
{
	VirtualPointer = InUV;
	Wake();
}

FVector4f SPointerEffectsHost::GetCorners(const FVector2f& Size) const
{
	return CornerSource ? CornerSource(Size) : FVector4f(0.f, 0.f, 0.f, 0.f);
}

void SPointerEffectsHost::Wake()
{
	if (!TickTimer.IsValid() && !Settings.bDesignTime)
	{
		TickTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SPointerEffectsHost::OnActiveTick));
	}
}

EActiveTimerReturnType SPointerEffectsHost::OnActiveTick(double InCurrentTime, float InDeltaTime)
{
	if (Settings.bDesignTime || !Owner.IsValid())
	{
		TickTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}
	Step(FMath::Min(InDeltaTime, 0.1f), /*bSnap=*/false);
	return EActiveTimerReturnType::Continue;
}

FReply SPointerEffectsHost::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Seen on its way down to the widget under it (never handled here): the press and where it was, even when the button is
	// released before the next step.
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && !Settings.bDesignTime)
	{
		bPressCaught = true;
		const FVector2D PressLocal = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
		CaughtPressLocal = FVector2f(static_cast<float>(PressLocal.X), static_cast<float>(PressLocal.Y));
		Wake();
	}
	return FReply::Unhandled();
}

bool SPointerEffectsHost::IsConditionMet(FName Condition, TMap<FName, bool>& Asked) const
{
	if (const bool* Known = Asked.Find(Condition))
	{
		return *Known;
	}
	const bool bMet = (Settings.bDesignTime && Settings.bPreviewConditionsAsMet) || (ConditionSource && ConditionSource(Condition));
	Asked.Add(Condition, bMet);
	return bMet;
}

float SPointerEffectsHost::GateTarget(const FPointerEffectCondition& Condition, EPointerEffectTrigger DefaultTrigger, float Hover, float Near, bool bPressedNow,
	bool bFocused, TMap<FName, bool>& Asked) const
{
	const EPointerEffectTrigger Trigger = Condition.Trigger == EPointerEffectTrigger::Default ? DefaultTrigger : Condition.Trigger;
	float Value = 1.f;
	switch (Trigger)
	{
	case EPointerEffectTrigger::WhileHovered: Value = Hover; break;
	case EPointerEffectTrigger::NearPointer: Value = Near; break;
	case EPointerEffectTrigger::WhilePressed: Value = bPressedNow ? 1.f : 0.f; break;
	case EPointerEffectTrigger::WhileFocused: Value = bFocused ? 1.f : 0.f; break;
	default: break;
	}
	if (!Condition.Condition.IsNone() && Value > 0.f)
	{
		const bool bMet = IsConditionMet(Condition.Condition, Asked);
		if (bMet == Condition.bInvertCondition)
		{
			Value = 0.f;
		}
	}
	return Value;
}

void SPointerEffectsHost::Step(float DeltaTime, bool bSnap)
{
	using namespace PointerEffectsPrivate;

	const FGeometry& Geometry = GetTickSpaceGeometry();
	const FVector2D LocalSize = Geometry.GetLocalSize();
	const FVector2f Size(FMath::Max(static_cast<float>(LocalSize.X), 1.f), FMath::Max(static_cast<float>(LocalSize.Y), 1.f));
	const FPointerEffectsState Before = State;

	// Where the pointer is over the widget's laid-out box, how near, whether it is over the widget and pressing it.
	FVector2f Local = Size * 0.5f;
	bool bHasPointer = false;
	bool bOver = false;
	bool bDown = false;
	bool bFocused = false;
	bool bUsable = true;
	float Near = 0.f;
	if (Settings.bDesignTime)
	{
		if (Settings.bPreviewInDesigner)
		{
			Local = Size * FVector2f(static_cast<float>(Settings.DesignerPointer.X), static_cast<float>(Settings.DesignerPointer.Y));
			bHasPointer = true;
			bOver = true;
			Near = 1.f;
		}
	}
	else if (UWidget* OwnerWidget = Owner.Get())
	{
		bUsable = Settings.bReactWhileDisabled || IsUsable(OwnerWidget);

		FWidgetAnimInputContext Input;
		Input.HostWidget = OwnerWidget;
		Input.MeasuredWidget = OwnerWidget;
		WidgetAnimInput::ReadPointer(Input);
		if (Input.bHasPointer && (!Input.bWorldPointer || Input.WorldPointer.bOver))
		{
			bHasPointer = true;
			Local = Geometry.GetAccumulatedRenderTransform().Inverse().TransformPoint(Input.PointerPosition);
			const bool bHit = Input.bWorldPointer
				? WidgetAnimInput::IsPointUnder(Input, OwnerWidget, Input.PointerPosition)
				: WidgetAnimInput::IsPointerOver(OwnerWidget, Input.PointerPosition);
			// Over as drawn - and kept while the pointer stays inside the laid-out box, so a widget tilted, leaning or sunk
			// out from under the pointer at its edge doesn't flicker between hovered and not (nor its button's states with it).
			const bool bInBox = Local.X >= 0.f && Local.Y >= 0.f && Local.X <= Size.X && Local.Y <= Size.Y;
			bOver = bHit || (bWasOver && bInBox);
			const float DX = FMath::Max3(-Local.X, 0.f, Local.X - Size.X);
			const float DY = FMath::Max3(-Local.Y, 0.f, Local.Y - Size.Y);
			Near = 1.f - FMath::Clamp(FMath::Sqrt(DX * DX + DY * DY) / FMath::Max(Settings.ProximityRange, 1.f), 0.f, 1.f);
		}
		bDown = WidgetAnimInput::IsButtonDown(Input, EKeys::LeftMouseButton);

		// Navigation focus is hover, the pointer in the middle - unless the real pointer is over it. Not the focus a click left
		// behind: a clicked button keeps focus, and would otherwise stay "hovered" after the pointer has gone.
		if (Settings.bReactToFocus && HasFocusedDescendants() && FSlateApplication::IsInitialized())
		{
			const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
			const TOptional<EFocusCause> Cause = Focused.IsValid() ? Focused->HasAnyUserFocus() : TOptional<EFocusCause>();
			bFocused = !Cause.IsSet() || Cause.GetValue() != EFocusCause::Mouse;
		}
		if (bFocused && !bOver)
		{
			bOver = true;
			bHasPointer = true;
			Near = 1.f;
			Local = Size * 0.5f;
		}
		if (VirtualPointer.IsSet())
		{
			bOver = true;
			bHasPointer = true;
			Near = 1.f;
			Local = Size * FVector2f(static_cast<float>(VirtualPointer->X), static_cast<float>(VirtualPointer->Y));
		}
		if (!bUsable)
		{
			// Disabled: lies still.
			bOver = false;
			bDown = false;
			bFocused = false;
			Near = 0.f;
		}
	}
	bWasOver = bOver;

	// A press is one that started over the widget, still held - or one caught as it happened, held a moment so a click
	// quicker than a frame still sinks and ripples.
	bool bPressStart = false;
	FVector2f PressAt = Local;
	if (bPressCaught)
	{
		bPressCaught = false;
		bPressStart = bUsable;
		bPressStartedOver = bUsable;
		PressAt = CaughtPressLocal;
		PressHold = 0.12f;
	}
	else if (bDown && !bWasDown)
	{
		bPressStartedOver = bOver;
		bPressStart = bOver;
	}
	bWasDown = bDown;
	PressHold = FMath::Max(PressHold - DeltaTime, 0.f);
	const bool bPressed = !Settings.bDesignTime && bPressStartedOver && (bDown || PressHold > 0.f);

	// How much each effect shows: its trigger and condition, within the component's own (Active When).
	TMap<FName, bool> Asked;
	const float HoverTarget = bOver ? 1.f : 0.f;
	const bool bSnapFades = bSnap || Settings.HoverFade <= 0.f;
	const float Intensity = FMath::Max(Settings.Intensity, 0.f);
	bool bMoving = false;

	ActiveGate.SetTarget(bUsable || Settings.bDesignTime ? GateTarget(Settings.ActiveWhen, EPointerEffectTrigger::Always, HoverTarget, Near, bPressed, bFocused, Asked) : 0.f,
		EWidgetMotion::Smooth, Settings.HoverFade, bSnapFades);
	bMoving |= ActiveGate.Advance(DeltaTime);
	const float Active = FMath::Clamp(ActiveGate.Value, 0.f, 1.f);

	HoverValue.SetTarget(HoverTarget, EWidgetMotion::Smooth, Settings.HoverFade, bSnapFades);
	ProximityValue.SetTarget(Near, EWidgetMotion::Smooth, Settings.HoverFade, bSnapFades);
	bMoving |= HoverValue.Advance(DeltaTime);
	bMoving |= ProximityValue.Advance(DeltaTime);

	if (bPressStart && Settings.Ripple.bEnabled && !Settings.bDesignTime && Active > 0.5f
		&& GateTarget(Settings.Ripple.ShowWhen, EPointerEffectTrigger::Always, HoverTarget, Near, true, bFocused, Asked) > 0.5f)
	{
		FRipple& Added = Ripples.AddDefaulted_GetRef();
		Added.UV = PressAt / Size;
	}

	// The pointer's speed, eased so a single jump doesn't spike it.
	FVector2f Velocity = FVector2f::ZeroVector;
	if (bHasPointer && bHasLastLocal && DeltaTime > 0.f)
	{
		const FVector2f Raw = (Local - LastLocal) / DeltaTime;
		const float Ease = 1.f - FMath::Exp(-DeltaTime * 12.f);
		Velocity = FMath::Lerp(FVector2f(static_cast<float>(State.Velocity.X), static_cast<float>(State.Velocity.Y)), Raw, Ease);
	}
	LastLocal = Local;
	bHasLastLocal = bHasPointer;

	const FVector2f UV = Local / Size;
	const FVector2f Centered = UV * 2.f - FVector2f(1.f, 1.f);

	// Tilt: toward or away from the pointer at its edges, while it shows.
	const FPointerTiltEffect& TiltSettings = Settings.Tilt;
	TiltGate.SetTarget(TiltSettings.bEnabled ? GateTarget(TiltSettings.ShowWhen, EPointerEffectTrigger::WhileHovered, HoverTarget, Near, bPressed, bFocused, Asked) * Active : 0.f,
		EWidgetMotion::Smooth, Settings.HoverFade, bSnapFades);
	bMoving |= TiltGate.Advance(DeltaTime);
	const float TiltShown = FMath::Clamp(TiltGate.Value, 0.f, 1.f);
	const float TiltSign = TiltSettings.Direction == EPointerTiltDirection::PressAway ? -1.f : 1.f;
	const FVector2f ClampedCentered(FMath::Clamp(Centered.X, -1.f, 1.f), FMath::Clamp(Centered.Y, -1.f, 1.f));
	const float TiltXTarget = TiltSettings.bEnabled ? TiltSign * ClampedCentered.X * TiltSettings.MaxAngle * Intensity * TiltShown : 0.f;
	const float TiltYTarget = TiltSettings.bEnabled ? TiltSign * ClampedCentered.Y * TiltSettings.MaxAngle * Intensity * TiltShown : 0.f;
	bMoving |= SpringTo(TiltSettings.Spring, TiltXTarget, TiltX, DeltaTime, bSnap, 0.01f);
	bMoving |= SpringTo(TiltSettings.Spring, TiltYTarget, TiltY, DeltaTime, bSnap, 0.01f);

	// Magnet: a lean toward the pointer, as it nears (Range) or once over - or as its Show When says.
	const FPointerMagnetEffect& MagnetSettings = Settings.Magnet;
	FVector2f LeanTarget = FVector2f::ZeroVector;
	if (MagnetSettings.bEnabled && bHasPointer)
	{
		float Pull = 0.f;
		if (MagnetSettings.ShowWhen.Trigger == EPointerEffectTrigger::Default)
		{
			Pull = MagnetSettings.Range > 0.f
				? (1.f - FMath::Clamp(FMath::Sqrt(FMath::Square(FMath::Max3(-Local.X, 0.f, Local.X - Size.X)) + FMath::Square(FMath::Max3(-Local.Y, 0.f, Local.Y - Size.Y))) / MagnetSettings.Range, 0.f, 1.f))
				: HoverTarget;
			Pull *= GateTarget(MagnetSettings.ShowWhen, EPointerEffectTrigger::Always, HoverTarget, Near, bPressed, bFocused, Asked);
		}
		else
		{
			Pull = GateTarget(MagnetSettings.ShowWhen, EPointerEffectTrigger::NearPointer, HoverTarget, Near, bPressed, bFocused, Asked);
		}
		LeanTarget = (Local - Size * 0.5f) * MagnetSettings.Pull;
		const float Reach = LeanTarget.Size();
		if (Reach > MagnetSettings.MaxOffset && Reach > KINDA_SMALL_NUMBER)
		{
			LeanTarget *= MagnetSettings.MaxOffset / Reach;
		}
		LeanTarget *= Pull * Intensity * Active;
	}
	bMoving |= SpringTo(MagnetSettings.Spring, LeanTarget.X, LeanX, DeltaTime, bSnap, 0.02f);
	bMoving |= SpringTo(MagnetSettings.Spring, LeanTarget.Y, LeanY, DeltaTime, bSnap, 0.02f);

	// Lift with the tilt, sink on press (or as the press's Show When says).
	const float LiftTarget = TiltSettings.bEnabled ? FMath::Lerp(1.f, TiltSettings.HoverScale, FMath::Min(Intensity, 2.f) * TiltShown) : 1.f;
	const float PressShown = Settings.Press.bEnabled
		? GateTarget(Settings.Press.ShowWhen, EPointerEffectTrigger::WhilePressed, HoverTarget, Near, bPressed, bFocused, Asked) * Active
		: 0.f;
	const float PressTarget = FMath::Lerp(1.f, Settings.Press.PressScale, FMath::Min(Intensity, 1.f) * PressShown);
	bMoving |= SpringTo(TiltSettings.Spring, LiftTarget, ScaleSpring, DeltaTime, bSnap, 0.0005f);
	bMoving |= SpringTo(Settings.Press.Spring, PressTarget, PressSpring, DeltaTime, bSnap, 0.0005f);

	// Lights and overlays fade in and out as they say.
	for (int32 Index = 0; Index < LightGates.Num() && Index < Settings.Lights.Num(); ++Index)
	{
		const FPointerLight& Light = Settings.Lights[Index];
		float Target = 0.f;
		if (Light.bEnabled)
		{
			// Default: as its Show says.
			const EPointerEffectTrigger ShowTrigger = Light.Show == EPointerLightShow::NearPointer ? EPointerEffectTrigger::NearPointer
				: Light.Show == EPointerLightShow::Always ? EPointerEffectTrigger::Always : EPointerEffectTrigger::WhileHovered;
			Target = GateTarget(Light.ShowWhen, ShowTrigger, HoverTarget, Near, bPressed, bFocused, Asked) * Active;
		}
		LightGates[Index].SetTarget(Target, EWidgetMotion::Smooth, Settings.HoverFade, bSnapFades);
		bMoving |= LightGates[Index].Advance(DeltaTime);
	}
	for (int32 Index = 0; Index < OverlayGates.Num() && Index < Settings.Overlays.Num(); ++Index)
	{
		const FPointerOverlay& Overlay = Settings.Overlays[Index];
		const float Target = Overlay.bEnabled ? GateTarget(Overlay.ShowWhen, EPointerEffectTrigger::WhileHovered, HoverTarget, Near, bPressed, bFocused, Asked) * Active : 0.f;
		OverlayGates[Index].SetTarget(Target, EWidgetMotion::Smooth, Settings.HoverFade, bSnapFades);
		bMoving |= OverlayGates[Index].Advance(DeltaTime);
	}

	// Ripples spread and fade.
	for (int32 Index = Ripples.Num() - 1; Index >= 0; --Index)
	{
		Ripples[Index].Age += DeltaTime;
		if (Ripples[Index].Age >= FMath::Max(Settings.Ripple.Duration, 0.05f))
		{
			Ripples.RemoveAt(Index);
		}
	}
	bMoving |= Ripples.Num() > 0;

	State.UV = FVector2D(UV.X, UV.Y);
	State.Centered = FVector2D(Centered.X, Centered.Y);
	State.Hover = FMath::Clamp(HoverValue.Value, 0.f, 1.f);
	State.Proximity = FMath::Clamp(ProximityValue.Value, 0.f, 1.f);
	State.Pressed = bPressed ? 1.f : 0.f;
	State.Velocity = FVector2D(Velocity.X, Velocity.Y);
	State.bOver = bOver;

	const bool bChanged = bMoving || !Before.UV.Equals(State.UV, 1e-4) || Before.bOver != State.bOver || Before.Pressed != State.Pressed
		|| !Before.Velocity.Equals(State.Velocity, 0.5);
	if (bChanged || bSnap)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
		if (Surface.IsValid())
		{
			Surface->Invalidate(EInvalidateWidgetReason::Paint);
		}
		if (UnderSurface.IsValid())
		{
			UnderSurface->Invalidate(EInvalidateWidgetReason::Paint);
		}
		if (OnUpdated && !Settings.bDesignTime)
		{
			OnUpdated(State);
		}
	}
}

FShapeBoxShape SPointerEffectsHost::GetTiltShape() const
{
	FShapeBoxShape Shape;
	const float AngleX = FMath::Abs(TiltX.Value) < 0.01f ? 0.f : TiltX.Value;
	const float AngleY = FMath::Abs(TiltY.Value) < 0.01f ? 0.f : TiltY.Value;
	Shape.Angle = FVector2D(AngleX, AngleY);
	Shape.Perspective = Settings.Tilt.Perspective;
	return Shape;
}

void SPointerEffectsHost::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const TSharedRef<SWidget>& Child = ChildSlot.GetWidget();
	const EVisibility ChildVisibility = Child->GetVisibility();
	if (!ArrangedChildren.Accepts(ChildVisibility))
	{
		return;
	}
	const float Scale = ScaleSpring.Value * PressSpring.Value;
	const FVector2f Lean(LeanX.Value, LeanY.Value);
	if (FMath::IsNearlyEqual(Scale, 1.f, 1e-4f) && Lean.IsNearlyZero(1e-3f))
	{
		ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(Child, AllottedGeometry.GetLocalSize(), FSlateLayoutTransform()));
		return;
	}
	// Moved and scaled about its middle; its slot keeps its place.
	const FSlateRenderTransform Moved(FMatrix2x2(Scale, 0.f, 0.f, Scale), Lean);
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	ArrangedChildren.AddWidget(ChildVisibility, FArrangedWidget(Child, AllottedGeometry.MakeChild(LocalSize, FSlateLayoutTransform(), Moved, FVector2D(0.5, 0.5))));
}

// =====================================================================================================
// SPointerEffectsSurface
// =====================================================================================================

void SPointerEffectsSurface::Construct(const FArguments& InArgs, const TSharedRef<SPointerEffectsHost>& InHost)
{
	Host = InHost;
	bBehind = InArgs._Behind;
	SetCanTick(false);
}

int32 SPointerEffectsSurface::PaintOverlays(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
	const FVector2f& Size, const FVector4f& Corners, float Tolerance) const
{
	const TSharedPtr<SPointerEffectsHost> Owner = Host.Pin();
	if (!Owner.IsValid())
	{
		return LayerId;
	}
	const FPointerEffectsSettings& Settings = Owner->GetSettings();
	const FPointerEffectsState& State = Owner->GetState();
	const float Intensity = FMath::Max(Settings.Intensity, 0.f);
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FVector2f Pointer = Size * FVector2f(static_cast<float>(State.UV.X), static_cast<float>(State.UV.Y));
	int32 Layer = LayerId;

	for (int32 Index = 0; Index < Settings.Overlays.Num(); ++Index)
	{
		const FPointerOverlay& Overlay = Settings.Overlays[Index];
		if (!Overlay.bEnabled || Overlay.bBehindContent != bBehind || !Overlay.Image.IsSet())
		{
			continue;
		}
		const float Strength = Owner->GetOverlayStrength(Index) * Intensity;
		const float Alpha = Overlay.Tint.A * FMath::Min(Strength, 1.f);
		if (Alpha <= 0.002f)
		{
			continue;
		}

		// What its material is told: the pointer, as the Outputs name it, and more.
		FWidgetEffectMaterialInputs Inputs;
		if (!Settings.PointerParameter.IsNone())
		{
			Inputs.Add(Settings.PointerParameter, FLinearColor(static_cast<float>(State.UV.X), static_cast<float>(State.UV.Y), State.Hover, State.Pressed));
		}
		if (!Settings.HoverParameter.IsNone())
		{
			Inputs.Add(Settings.HoverParameter, State.Hover);
		}
		if (!Settings.VelocityParameter.IsNone())
		{
			Inputs.Add(Settings.VelocityParameter, FLinearColor(static_cast<float>(State.Velocity.X), static_cast<float>(State.Velocity.Y), static_cast<float>(State.Velocity.Size()), State.Proximity));
		}
		Inputs.Add(TEXT("PointerProximity"), State.Proximity);
		Inputs.Add(TEXT("PointerPressed"), State.Pressed);
		Inputs.Add(TEXT("EffectStrength"), FMath::Min(Strength, 1.f));
		Inputs.Add(TEXT("WidgetSize"), FLinearColor(Size.X, Size.Y, 0.f, 0.f));

		// Where it is drawn, and its UVs: over the whole widget, or a square on the pointer (cut to the widget, or not).
		const bool bFill = Overlay.Placement == EPointerOverlayPlacement::Fill;
		const float Side = FMath::Max(Overlay.Size, 1.f);
		const FVector2f QuadMin = Pointer - FVector2f(Side, Side) * 0.5f;
		FWidgetShapeMesh Cut;
		const FWidgetShapeMesh* Mesh = &FillMesh;
		if (!bFill)
		{
			TArray<FVector2f> Quad = { QuadMin, QuadMin + FVector2f(Side, 0.f), QuadMin + FVector2f(Side, Side), QuadMin + FVector2f(0.f, Side) };
			TArray<FVector2f> Polygon;
			if (Overlay.Placement == EPointerOverlayPlacement::FollowPointer && OutlinePoints.Num() >= 3)
			{
				WidgetEffectsDraw::ClipPolygon(Quad, OutlinePoints, Polygon);
			}
			else
			{
				Polygon = MoveTemp(Quad);
			}
			if (Polygon.Num() < 3)
			{
				continue;
			}
			// Convex: a fan.
			for (const FVector2f& Point : Polygon)
			{
				Cut.Vertices.Add(FWidgetShapeMeshVertex(Point, 1.f));
			}
			for (int32 Corner = 1; Corner + 1 < Polygon.Num(); ++Corner)
			{
				Cut.Indices.Append({ 0u, static_cast<uint32>(Corner), static_cast<uint32>(Corner + 1) });
			}
			Mesh = &Cut;
		}

		const FVector2f ImageSize = bFill ? Size : FVector2f(Side, Side);
		const FSlateBrush* Brush = Materials.GetBrush(static_cast<uint64>(Index), Overlay.Image, &Inputs, AllottedGeometry, ImageSize, *this);
		if (!Brush)
		{
			continue;
		}
		FLinearColor Color = Overlay.Tint;
		Color.A = Alpha;
		if (Overlay.Image.ImageSource == EWidgetImageSource::Texture)
		{
			Color *= Overlay.Image.Image.GetTint(InWidgetStyle);
		}
		const FVector2f Origin = bFill ? FVector2f::ZeroVector : QuadMin;
		WidgetEffectsDraw::PaintMesh(OutDrawElements, ++Layer, AllottedGeometry, *Mesh, WidgetTint, [Color](const FWidgetShapeMeshVertex&) { return Color; }, Brush,
			[Origin, ImageSize](const FWidgetShapeMeshVertex& Vertex) { return (Vertex.Position - Origin) / ImageSize; });
	}
	return Layer;
}

int32 SPointerEffectsSurface::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace PointerEffectsPrivate;
	using namespace WidgetEffectsDraw;

	const TSharedPtr<SPointerEffectsHost> Owner = Host.Pin();
	if (!Owner.IsValid())
	{
		return LayerId;
	}
	const FPointerEffectsSettings& Settings = Owner->GetSettings();
	const FPointerEffectsState& State = Owner->GetState();
	const TArray<SPointerEffectsHost::FRipple>& Ripples = Owner->GetRipples();
	const float Intensity = FMath::Max(Settings.Intensity, 0.f);

	// Behind the content only overlays are drawn; over it, lights, ripples and overlays.
	bool bAnyOverlay = false;
	for (int32 Index = 0; Index < Settings.Overlays.Num(); ++Index)
	{
		const FPointerOverlay& Overlay = Settings.Overlays[Index];
		bAnyOverlay |= Overlay.bEnabled && Overlay.bBehindContent == bBehind && Overlay.Image.IsSet() && Owner->GetOverlayStrength(Index) * Overlay.Tint.A > 0.002f;
	}
	bool bAnyLight = false;
	if (!bBehind)
	{
		for (int32 Index = 0; Index < Settings.Lights.Num(); ++Index)
		{
			const FPointerLight& Light = Settings.Lights[Index];
			bAnyLight |= Light.bEnabled && Light.Color.A * Owner->GetLightStrength(Index) * Intensity > 0.002f;
		}
	}
	if ((!bAnyLight && !bAnyOverlay && (bBehind || Ripples.Num() == 0)) || Intensity <= 0.f)
	{
		return LayerId;
	}

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2f Size(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	if (Size.X <= 0.f || Size.Y <= 0.f)
	{
		return LayerId;
	}
	const float PixelScale = QuantizeScale(GetPixelScale(AllottedGeometry));
	const float AntiAlias = 1.f / PixelScale;
	const float Tolerance = WidgetShapeGeometry::GetTolerance(PixelScale);
	const FVector4f Corners = FitRadii(Owner->GetCorners(Size), Size);

	// The widget's outline, filled and as a rim, cut into a grid so light worked out per vertex reads smoothly. Cached.
	uint32 Hash = HashCombineFast(GetTypeHash(Size), HashRadii(Corners));
	Hash = HashCombineFast(Hash, GetTypeHash(PixelScale));
	float RimWidth = 1.5f;
	for (const FPointerLight& Light : Settings.Lights)
	{
		if (Light.Kind == EPointerLightKind::BorderGlow)
		{
			RimWidth = Light.BorderWidth;
		}
	}
	Hash = HashCombineFast(Hash, GetTypeHash(RimWidth));
	if (MeshHash != Hash || FillMesh.IsEmpty())
	{
		MeshHash = Hash;
		FillMesh.Reset();
		RimMesh.Reset();
		FWidgetShapeOutline Outline;
		AppendRoundedRect(Outline, FVector2f::ZeroVector, Size, Corners, Tolerance);
		OutlinePoints = Outline.Lines.Num() > 0 ? Outline.Lines[0].Points : TArray<FVector2f>();
		TArray<int8> FillOnLeft;
		WidgetShapeGeometry::BuildFill(Outline, AntiAlias, FillMesh, FillOnLeft);
		FWidgetShapeStroke Stroke;
		Stroke.bEnabled = true;
		Stroke.Width = RimWidth;
		Stroke.Align = EWidgetShapeStrokeAlign::Inside;
		Stroke.Join = EWidgetShapeLineJoin::Round;
		WidgetShapeGeometry::BuildStroke(Outline, FillOnLeft, Stroke, FWidgetShapeTrim(), AntiAlias, Tolerance, RimMesh);
		const float Cell = FMath::Clamp(FMath::Min(Size.X, Size.Y) / 16.f, 4.f, 16.f);
		CutGrid(FillMesh, Size, Cell);
		CutGrid(RimMesh, Size, Cell);
	}

	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FVector2f Pointer = Size * FVector2f(static_cast<float>(State.UV.X), static_cast<float>(State.UV.Y));
	int32 Layer = LayerId;
	if (bBehind)
	{
		return PaintOverlays(AllottedGeometry, OutDrawElements, Layer, InWidgetStyle, Size, Corners, Tolerance);
	}

	for (int32 LightIndex = 0; LightIndex < Settings.Lights.Num(); ++LightIndex)
	{
		const FPointerLight& Light = Settings.Lights[LightIndex];
		if (!Light.bEnabled)
		{
			continue;
		}
		const float Strength = Light.Color.A * Owner->GetLightStrength(LightIndex) * Intensity;
		if (Strength <= 0.002f)
		{
			continue;
		}
		const FLinearColor Base(Light.Color.R, Light.Color.G, Light.Color.B, 1.f);
		++Layer;
		switch (Light.Kind)
		{
		case EPointerLightKind::Spotlight:
			PaintMesh(OutDrawElements, Layer, AllottedGeometry, FillMesh, WidgetTint, [&](const FWidgetShapeMeshVertex& Vertex)
			{
				FLinearColor Color = Base;
				Color.A = Strength * Falloff(FVector2f::Distance(Vertex.Position, Pointer), Light.Radius);
				return Color;
			});
			break;

		case EPointerLightKind::Sheen:
		{
			// A band through the pointer, slanted Sheen Angle from upright: its across direction is the band's normal.
			const float Radians = FMath::DegreesToRadians(Light.SheenAngle);
			const FVector2f Across(FMath::Cos(Radians), FMath::Sin(Radians));
			const float Half = FMath::Max(Light.SheenWidth * 0.5f, 1.f);
			PaintMesh(OutDrawElements, Layer, AllottedGeometry, FillMesh, WidgetTint, [&](const FWidgetShapeMeshVertex& Vertex)
			{
				const float Offset = FVector2f::DotProduct(Vertex.Position - Pointer, Across) / Half;
				const float Band = FMath::Square(FMath::Max(0.f, 1.f - Offset * Offset));
				FLinearColor Color = Base;
				Color.A = Strength * Band;
				return Color;
			});
			break;
		}

		case EPointerLightKind::BorderGlow:
			PaintMesh(OutDrawElements, Layer, AllottedGeometry, RimMesh, WidgetTint, [&](const FWidgetShapeMeshVertex& Vertex)
			{
				FLinearColor Color = Base;
				Color.A = Strength * Falloff(FVector2f::Distance(Vertex.Position, Pointer), Light.Radius);
				return Color;
			});
			break;

		case EPointerLightKind::Holographic:
		{
			// Bands of hue across the widget, shifted by where the pointer is - as a foil catches the light at another angle.
			const float Shift = static_cast<float>(State.UV.X - State.UV.Y) * 0.6f;
			PaintMesh(OutDrawElements, Layer, AllottedGeometry, FillMesh, WidgetTint, [&](const FWidgetShapeMeshVertex& Vertex)
			{
				const FVector2f Place = Vertex.Position / Size;
				const float Hue = FMath::Frac((Place.X * 0.7f + Place.Y * 0.5f) * 1.5f + Shift);
				FLinearColor Color = FLinearColor(Hue * 360.f, 0.55f, 1.f).HSVToLinearRGB() * Base;
				Color.A = Strength * (0.35f + 0.65f * Falloff(FVector2f::Distance(Vertex.Position, Pointer), Light.Radius));
				return Color;
			});
			break;
		}
		}
	}

	// Ripples: a disc spreading from where it was pressed until it covers the widget, fading as it goes.
	if (Ripples.Num() > 0)
	{
		const float Duration = FMath::Max(Settings.Ripple.Duration, 0.05f);
		const float Edge = FMath::Max(6.f * AntiAlias, 2.f);
		for (const SPointerEffectsHost::FRipple& Ripple : Ripples)
		{
			const float Time = FMath::Clamp(Ripple.Age / Duration, 0.f, 1.f);
			const FVector2f Center = Ripple.UV * Size;
			const float Farthest = FMath::Max(FMath::Max(FVector2f::Distance(Center, FVector2f::ZeroVector), FVector2f::Distance(Center, FVector2f(Size.X, 0.f))),
				FMath::Max(FVector2f::Distance(Center, FVector2f(0.f, Size.Y)), FVector2f::Distance(Center, Size)));
			const float Radius = Farthest * 1.05f * (1.f - FMath::Square(1.f - Time));
			const float Alpha = Settings.Ripple.Color.A * FMath::Pow(1.f - Time, 1.5f) * FMath::Min(Intensity, 1.f);
			if (Alpha <= 0.002f)
			{
				continue;
			}
			const FLinearColor Base(Settings.Ripple.Color.R, Settings.Ripple.Color.G, Settings.Ripple.Color.B, 1.f);
			PaintMesh(OutDrawElements, ++Layer, AllottedGeometry, FillMesh, WidgetTint, [&](const FWidgetShapeMeshVertex& Vertex)
			{
				const float Distance = FVector2f::Distance(Vertex.Position, Center);
				FLinearColor Color = Base;
				Color.A = Alpha * FMath::Clamp((Radius - Distance) / Edge + 0.5f, 0.f, 1.f);
				return Color;
			});
		}
	}

	// Overlays over the content, over the lights.
	return PaintOverlays(AllottedGeometry, OutDrawElements, Layer, InWidgetStyle, Size, Corners, Tolerance);
}

// =====================================================================================================
// UPointerEffectsComponent
// =====================================================================================================

UPointerEffectsComponent::UPointerEffectsComponent()
{
	MakePreset(Preset, Tilt, Magnet, Press, Lights, Ripple);
}

void UPointerEffectsComponent::MakePreset(EPointerEffectsPreset InPreset, FPointerTiltEffect& OutTilt, FPointerMagnetEffect& OutMagnet, FPointerPressEffect& OutPress,
	TArray<FPointerLight>& OutLights, FPointerRippleEffect& OutRipple)
{
	// Custom, and the presets that were taken away (Spotlight Border, Lift - kept only so old assets load): the effects as set.
	if (InPreset == EPointerEffectsPreset::Custom || InPreset == EPointerEffectsPreset::SpotlightBorder || InPreset == EPointerEffectsPreset::Lift)
	{
		return;
	}
	OutTilt = FPointerTiltEffect();
	OutMagnet = FPointerMagnetEffect();
	OutPress = FPointerPressEffect();
	OutLights.Reset();
	OutRipple = FPointerRippleEffect();

	auto AddLight = [&OutLights](EPointerLightKind Kind, EPointerLightShow Show, const FLinearColor& Color, float Radius)
	{
		FPointerLight& Light = OutLights.AddDefaulted_GetRef();
		Light.Kind = Kind;
		Light.Show = Show;
		Light.Color = Color;
		Light.Radius = Radius;
		return &Light;
	};

	switch (InPreset)
	{
	case EPointerEffectsPreset::TiltCard:
		OutTilt.bEnabled = true;
		OutTilt.MaxAngle = 8.f;
		AddLight(EPointerLightKind::Spotlight, EPointerLightShow::WhileHovered, FLinearColor(1.f, 1.f, 1.f, 0.18f), 220.f);
		break;
	case EPointerEffectsPreset::HolographicCard:
		OutTilt.bEnabled = true;
		OutTilt.MaxAngle = 14.f;
		OutTilt.HoverScale = 1.05f;
		AddLight(EPointerLightKind::Holographic, EPointerLightShow::WhileHovered, FLinearColor(1.f, 1.f, 1.f, 0.22f), 260.f);
		AddLight(EPointerLightKind::Spotlight, EPointerLightShow::WhileHovered, FLinearColor(1.f, 1.f, 1.f, 0.2f), 180.f);
		break;
	case EPointerEffectsPreset::ShimmerButton:
		AddLight(EPointerLightKind::Sheen, EPointerLightShow::WhileHovered, FLinearColor(1.f, 1.f, 1.f, 0.28f), 160.f);
		break;
	case EPointerEffectsPreset::MagneticButton:
		OutMagnet.bEnabled = true;
		AddLight(EPointerLightKind::Spotlight, EPointerLightShow::WhileHovered, FLinearColor(1.f, 1.f, 1.f, 0.15f), 120.f);
		break;
	case EPointerEffectsPreset::Ripple:
		OutRipple.bEnabled = true;
		OutPress.bEnabled = true;
		OutPress.PressScale = 0.98f;
		break;
	default:
		break;
	}
}

TSharedRef<SWidget> UPointerEffectsComponent::RebuildWidgetWithContent(const TSharedRef<SWidget> OwnerContent)
{
	TSharedRef<SPointerEffectsHost> Host = SNew(SPointerEffectsHost)
	[
		OwnerContent
	];
	// The widget built again (a button changing its parts, the designer's preview): its tilt, lean and hover carry on.
	if (const TSharedPtr<SPointerEffectsHost> Previous = MyHost.Pin())
	{
		Host->CopyMotionFrom(*Previous);
	}
	MyHost = Host;
	bTargetsCollected = false;
	bHasSent = false;
	Register();
	Push();
	return Host;
}

void UPointerEffectsComponent::Register()
{
	// From the moment it is initialized - the designer's preview included - so a Pointer Effects track finds it.
	PointerEffectsPrivate::GetComponents().AddUnique(this);
}

#if UE_VERSION_OLDER_THAN(5, 6, 0)
bool UPointerEffectsComponent::OnInitialize()
{
	Register();
	return true;
}
#else
void UPointerEffectsComponent::OnInitialize()
{
	Register();
}

void UPointerEffectsComponent::OnPreConstruct(bool bIsDesignTime)
{
	Register();
}
#endif

void UPointerEffectsComponent::PostLoad()
{
	Super::PostLoad();
	// The presets taken away load as Custom, their effects as they were.
	if (Preset == EPointerEffectsPreset::SpotlightBorder || Preset == EPointerEffectsPreset::Lift)
	{
		Preset = EPointerEffectsPreset::Custom;
	}
}

void UPointerEffectsComponent::SetEffectCondition(FName Condition, bool bMet)
{
	if (Condition.IsNone())
	{
		return;
	}
	Conditions.Add(Condition, bMet);
}

void UPointerEffectsComponent::ClearEffectCondition(FName Condition)
{
	Conditions.Remove(Condition);
}

bool UPointerEffectsComponent::IsEffectConditionMet(FName Condition) const
{
	if (Condition.IsNone())
	{
		return true;
	}
	// Set from Blueprint or C++ first.
	if (const bool* Set = Conditions.Find(Condition))
	{
		return *Set;
	}
	// Then the widget, and the User Widget it is in, answering themselves.
	UWidget* OwnerWidget = GetOwner().Get();
	UObject* Sources[2] = { OwnerWidget, OwnerWidget ? OwnerWidget->GetTypedOuter<UUserWidget>() : nullptr };
	for (UObject* Source : Sources)
	{
		if (Source && Source->GetClass()->ImplementsInterface(UPointerEffectsConditionSource::StaticClass()))
		{
			bool bMet = false;
			if (IPointerEffectsConditionSource::Execute_GetPointerEffectCondition(Source, Condition, bMet))
			{
				return bMet;
			}
		}
	}
	// Then the built-in ones: a Context Menu Button's toggle (On / Off), and whether the widget can be used.
	static const FName OnName(TEXT("On"));
	static const FName OffName(TEXT("Off"));
	static const FName UsableName(TEXT("Usable"));
	static const FName DisabledName(TEXT("Disabled"));
	if (Condition == OnName || Condition == OffName)
	{
		const UContextMenuButton* Button = Cast<UContextMenuButton>(OwnerWidget);
		const bool bOn = Button && Button->GetEnabledState();
		return Condition == OnName ? bOn : !bOn;
	}
	if (Condition == UsableName || Condition == DisabledName)
	{
		const bool bUsable = PointerEffectsPrivate::IsUsable(OwnerWidget);
		return Condition == UsableName ? bUsable : !bUsable;
	}
	return false;
}

void UPointerEffectsComponent::ApplyPresetAsset(UPointerEffectsPresetAsset* InPresetAsset)
{
	if (!InPresetAsset)
	{
		return;
	}
	PresetAsset = InPresetAsset;
	Preset = EPointerEffectsPreset::Custom;
	Tilt = InPresetAsset->Tilt;
	Magnet = InPresetAsset->Magnet;
	Press = InPresetAsset->Press;
	Lights = InPresetAsset->Lights;
	Ripple = InPresetAsset->Ripple;
	Overlays = InPresetAsset->Overlays;
	bTargetsCollected = false;
	Push();
}

void UPointerEffectsComponent::CopyToPresetAsset(UPointerEffectsPresetAsset* Target) const
{
	if (!Target)
	{
		return;
	}
	Target->Modify();
	Target->Tilt = Tilt;
	Target->Magnet = Magnet;
	Target->Press = Press;
	Target->Lights = Lights;
	Target->Ripple = Ripple;
	Target->Overlays = Overlays;
	Target->MarkPackageDirty();
}

void UPointerEffectsComponent::SaveAsPreset()
{
#if WITH_EDITOR
	const UWidget* OwnerWidget = GetOwner().Get();
	const FString DefaultName = FString::Printf(TEXT("PEP_%s"), OwnerWidget ? *OwnerWidget->GetName() : TEXT("PointerEffects"));
	UPointerEffectsPresetAsset* Created = Cast<UPointerEffectsPresetAsset>(WidgetEditorHooks::CreateAsset(UPointerEffectsPresetAsset::StaticClass(), DefaultName));
	if (!Created)
	{
		return;
	}
	CopyToPresetAsset(Created);
	Modify();
	PresetAsset = Created;
	Preset = EPointerEffectsPreset::Custom;
#endif
}

void UPointerEffectsComponent::BeginDestroy()
{
	PointerEffectsPrivate::GetComponents().Remove(this);
	Super::BeginDestroy();
}

void UPointerEffectsComponent::Push()
{
	const TSharedPtr<SPointerEffectsHost> Host = MyHost.Pin();
	if (!Host.IsValid())
	{
		return;
	}
	UWidget* OwnerWidget = GetOwner().Get();
	Host->SetOwner(OwnerWidget);

	const TWeakObjectPtr<UPointerEffectsComponent> WeakThisComponent(this);
	Host->SetCornerSource([WeakThisComponent](const FVector2f& Size)
	{
		const UPointerEffectsComponent* Component = WeakThisComponent.Get();
		if (!Component)
		{
			return FVector4f(0.f, 0.f, 0.f, 0.f);
		}
		if (!Component->bMatchWidgetCorners)
		{
			return FVector4f(Component->CornerRadius, Component->CornerRadius, Component->CornerRadius, Component->CornerRadius);
		}
		const FDropShadowCaster Caster = UDropShadowComponent::ReadCaster(Component->GetOwner().Get());
		if (Caster.bPill)
		{
			const float Half = FMath::Min(Size.X, Size.Y) * 0.5f;
			return FVector4f(Half, Half, Half, Half);
		}
		return Caster.Radii;
	});
	Host->SetOnUpdated([WeakThisComponent](const FPointerEffectsState& InState)
	{
		if (UPointerEffectsComponent* Component = WeakThisComponent.Get())
		{
			Component->HandleUpdated(InState);
		}
	});
	Host->SetConditionSource([WeakThisComponent](FName Condition)
	{
		const UPointerEffectsComponent* Component = WeakThisComponent.Get();
		return Component && Component->IsEffectConditionMet(Condition);
	});

	FPointerEffectsSettings Settings;
	Settings.Tilt = Tilt;
	Settings.Magnet = Magnet;
	Settings.Press = Press;
	Settings.Lights = Lights;
	Settings.Ripple = Ripple;
	Settings.Overlays = Overlays;
	Settings.Intensity = Intensity;
	Settings.ProximityRange = ProximityRange;
	Settings.HoverFade = HoverFade;
	Settings.bReactToFocus = bReactToFocus;
	Settings.bReactWhileDisabled = bReactWhileDisabled;
	Settings.ActiveWhen = ActiveWhen;
	Settings.bDesignTime = OwnerWidget && OwnerWidget->IsDesignTime();
	Settings.bPreviewInDesigner = bPreviewInDesigner;
	Settings.bPreviewConditionsAsMet = bPreviewConditionsAsMet;
	Settings.DesignerPointer = DesignerPointer;
	Settings.PointerParameter = Outputs.PointerParameter;
	Settings.HoverParameter = Outputs.HoverParameter;
	Settings.VelocityParameter = Outputs.VelocityParameter;
	Host->SetSettings(Settings);
}

void UPointerEffectsComponent::CollectTargets()
{
	bTargetsCollected = true;
	Materials.Reset();
	Shapes.Reset();

	TFunction<void(UWidget*, int32)> Visit = [&](UWidget* Widget, int32 Depth)
	{
		if (!Widget || Depth > 6)
		{
			return;
		}
		if (Outputs.bDriveMaterials)
		{
			UMaterialInstanceDynamic* Dynamic = nullptr;
			if (UImage* Image = Cast<UImage>(Widget))
			{
				Dynamic = Image->GetDynamicMaterial();
			}
			else if (UBorder* Border = Cast<UBorder>(Widget))
			{
				Dynamic = Border->GetDynamicMaterial();
			}
			else if (UShapeWidget* Shape = Cast<UShapeWidget>(Widget))
			{
				Dynamic = Shape->GetDynamicMaterial(Shape->GetImagePaint());
			}
			if (Dynamic)
			{
				Materials.AddUnique(Dynamic);
			}
		}
		if (Outputs.bDriveShapeGradients)
		{
			if (UShapeWidget* Shape = Cast<UShapeWidget>(Widget))
			{
				Shapes.AddUnique(Shape);
			}
		}
		if (!Outputs.bIncludeChildren)
		{
			return;
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				Visit(Panel->GetChildAt(Index), Depth + 1);
			}
		}
		else if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			if (UserWidget->WidgetTree)
			{
				Visit(UserWidget->WidgetTree->RootWidget, Depth + 1);
			}
		}
	};
	Visit(GetOwner().Get(), 0);
}

void UPointerEffectsComponent::HandleUpdated(const FPointerEffectsState& InState)
{
	UWidget* OwnerWidget = GetOwner().Get();
	if (!OwnerWidget)
	{
		return;
	}
	if (!bTargetsCollected)
	{
		CollectTargets();
	}

	if (Outputs.bDriveMaterials)
	{
		const FLinearColor PointerValue(static_cast<float>(InState.UV.X), static_cast<float>(InState.UV.Y), InState.Hover, InState.Pressed);
		const FLinearColor VelocityValue(static_cast<float>(InState.Velocity.X), static_cast<float>(InState.Velocity.Y), static_cast<float>(InState.Velocity.Size()), InState.Proximity);
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Weak : Materials)
		{
			if (UMaterialInstanceDynamic* Dynamic = Weak.Get())
			{
				if (!Outputs.PointerParameter.IsNone())
				{
					Dynamic->SetVectorParameterValue(Outputs.PointerParameter, PointerValue);
				}
				if (!Outputs.HoverParameter.IsNone())
				{
					Dynamic->SetScalarParameterValue(Outputs.HoverParameter, InState.Hover);
				}
				if (!Outputs.VelocityParameter.IsNone())
				{
					Dynamic->SetVectorParameterValue(Outputs.VelocityParameter, VelocityValue);
				}
			}
		}
	}

	if (Outputs.bDriveShapeGradients && (!bHasSent || !LastSent.UV.Equals(InState.UV, 2e-3)))
	{
		for (const TWeakObjectPtr<UShapeWidget>& Weak : Shapes)
		{
			UShapeWidget* Shape = Weak.Get();
			if (!Shape)
			{
				continue;
			}
			if (Shape->Fill.Type == EWidgetShapePaintType::RadialGradient)
			{
				FWidgetShapePaint Moved = Shape->Fill;
				Moved.GradientCenter = InState.UV;
				Shape->SetFill(Moved);
			}
			if (Shape->Stroke.Paint.Type == EWidgetShapePaintType::RadialGradient)
			{
				FWidgetShapeStroke Moved = Shape->Stroke;
				Moved.Paint.GradientCenter = InState.UV;
				Shape->SetStroke(Moved);
			}
		}
	}

	if (!Outputs.AnimationPointerValue.IsNone())
	{
		UWidgetAnimPlayLibrary::SetWidgetAnimationValueVector2D(OwnerWidget, Outputs.AnimationPointerValue, InState.Centered, false, true, true);
	}
	if (!Outputs.AnimationHoverValue.IsNone() && (!bHasSent || LastSent.Hover != InState.Hover))
	{
		UWidgetAnimPlayLibrary::SetWidgetAnimationValue(OwnerWidget, Outputs.AnimationHoverValue, InState.Hover, false, true, true);
	}
	if (!Outputs.MachinePointerInput.IsNone())
	{
		UWidgetAnimStateMachineLibrary::SetStateMachineVector2D(OwnerWidget, nullptr, Outputs.MachinePointerInput, InState.Centered, true, false);
	}
	if (!Outputs.MachineHoverInput.IsNone() && (!bHasSent || LastSent.Hover != InState.Hover))
	{
		UWidgetAnimStateMachineLibrary::SetStateMachineNumber(OwnerWidget, nullptr, Outputs.MachineHoverInput, InState.Hover, true, false);
	}
	if (!Outputs.MachineProximityInput.IsNone() && (!bHasSent || LastSent.Proximity != InState.Proximity))
	{
		UWidgetAnimStateMachineLibrary::SetStateMachineNumber(OwnerWidget, nullptr, Outputs.MachineProximityInput, InState.Proximity, true, false);
	}

	LastSent = InState;
	bHasSent = true;
	OnPointerEffectsUpdated.Broadcast(InState);
}

FPointerEffectsState UPointerEffectsComponent::GetPointerState() const
{
	const TSharedPtr<SPointerEffectsHost> Host = MyHost.Pin();
	return Host.IsValid() ? Host->GetState() : FPointerEffectsState();
}

void UPointerEffectsComponent::SetIntensity(float InIntensity)
{
	Intensity = FMath::Max(InIntensity, 0.f);
	Push();
}

void UPointerEffectsComponent::ApplyPreset(EPointerEffectsPreset InPreset)
{
	Preset = InPreset;
	MakePreset(Preset, Tilt, Magnet, Press, Lights, Ripple);
	if (Preset != EPointerEffectsPreset::Custom)
	{
		PresetAsset = nullptr;
	}
	Push();
}

void UPointerEffectsComponent::SetVirtualPointer(FVector2D UV)
{
	if (const TSharedPtr<SPointerEffectsHost> Host = MyHost.Pin())
	{
		Host->SetVirtualPointer(UV);
	}
}

void UPointerEffectsComponent::ClearVirtualPointer()
{
	if (const TSharedPtr<SPointerEffectsHost> Host = MyHost.Pin())
	{
		Host->SetVirtualPointer(TOptional<FVector2D>());
	}
}

void UPointerEffectsComponent::SetEffectValue(EPointerEffectsChannel Channel, float Value)
{
	switch (Channel)
	{
	case EPointerEffectsChannel::Intensity:
		Intensity = FMath::Max(Value, 0.f);
		break;
	case EPointerEffectsChannel::TiltAngle:
		Tilt.MaxAngle = FMath::Clamp(Value, 0.f, 45.f);
		break;
	case EPointerEffectsChannel::MagnetPull:
		Magnet.Pull = FMath::Clamp(Value, 0.f, 1.f);
		break;
	case EPointerEffectsChannel::LightStrength:
	{
		// Every light keeps its strength relative to the first: the value is the first light's alpha.
		const float First = Lights.Num() > 0 ? Lights[0].Color.A : 0.f;
		const float Scale = First > KINDA_SMALL_NUMBER ? FMath::Max(Value, 0.f) / First : 0.f;
		for (FPointerLight& Light : Lights)
		{
			Light.Color.A = First > KINDA_SMALL_NUMBER ? Light.Color.A * Scale : FMath::Max(Value, 0.f);
		}
		break;
	}
	}
	Push();
}

float UPointerEffectsComponent::GetEffectValue(EPointerEffectsChannel Channel) const
{
	switch (Channel)
	{
	case EPointerEffectsChannel::Intensity: return Intensity;
	case EPointerEffectsChannel::TiltAngle: return Tilt.MaxAngle;
	case EPointerEffectsChannel::MagnetPull: return Magnet.Pull;
	case EPointerEffectsChannel::LightStrength: return Lights.Num() > 0 ? Lights[0].Color.A : 0.f;
	default: return 0.f;
	}
}

void UPointerEffectsComponent::RefreshTargets()
{
	CollectTargets();
}

UWidget* UPointerEffectsComponent::GetAffectedWidget() const
{
	return GetOwner().Get();
}

UPointerEffectsComponent* UPointerEffectsComponent::FindPointerEffects(const UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}
	for (const TWeakObjectPtr<UPointerEffectsComponent>& Weak : PointerEffectsPrivate::GetComponents())
	{
		UPointerEffectsComponent* Component = Weak.Get();
		if (Component && Component->GetOwner().Get() == Widget)
		{
			return Component;
		}
	}
	return nullptr;
}

#if WITH_EDITOR
void UPointerEffectsComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName ChangedName = PropertyChangedEvent.GetPropertyName();
	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
	if (ChangedName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Preset))
	{
		MakePreset(Preset, Tilt, Magnet, Press, Lights, Ripple);
		if (Preset != EPointerEffectsPreset::Custom)
		{
			PresetAsset = nullptr;
		}
	}
	else if (ChangedName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, PresetAsset))
	{
		ApplyPresetAsset(PresetAsset);
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Tilt) || MemberName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Magnet)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Press) || MemberName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Lights)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Ripple) || MemberName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Overlays))
	{
		Preset = EPointerEffectsPreset::Custom;
		PresetAsset = nullptr;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bTargetsCollected = false;
	Push();
}
#endif
