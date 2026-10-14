// Copyright AR GameSystems 2026. All rights reserved.

#include "PointerEffects.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Layout/ArrangedChildren.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SOverlay.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"
#include "NameTBD/WidgetAnimation/WidgetAnimInput.h"
#include "NameTBD/WidgetAnimation/WidgetAnimStateMachineLibrary.h"
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
	TiltBox = SNew(SShapeBox)
		.Shape(TAttribute<FShapeBoxShape>::CreateSP(this, &SPointerEffectsHost::GetTiltShape))
		[
			SNew(SOverlay)
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
		FWidgetAnimInputContext Input;
		Input.HostWidget = OwnerWidget;
		Input.MeasuredWidget = OwnerWidget;
		WidgetAnimInput::ReadPointer(Input);
		if (Input.bHasPointer && (!Input.bWorldPointer || Input.WorldPointer.bOver))
		{
			bHasPointer = true;
			Local = Geometry.GetAccumulatedRenderTransform().Inverse().TransformPoint(Input.PointerPosition);
			bOver = Input.bWorldPointer
				? WidgetAnimInput::IsPointUnder(Input, OwnerWidget, Input.PointerPosition)
				: WidgetAnimInput::IsPointerOver(OwnerWidget, Input.PointerPosition);
			const float DX = FMath::Max3(-Local.X, 0.f, Local.X - Size.X);
			const float DY = FMath::Max3(-Local.Y, 0.f, Local.Y - Size.Y);
			Near = 1.f - FMath::Clamp(FMath::Sqrt(DX * DX + DY * DY) / FMath::Max(Settings.ProximityRange, 1.f), 0.f, 1.f);
		}
		bDown = WidgetAnimInput::IsButtonDown(Input, EKeys::LeftMouseButton);

		// Focus is hover, the pointer in the middle - unless the real pointer is over it.
		if (Settings.bReactToFocus && !bOver && HasFocusedDescendants())
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
	}

	// A press is one that started over the widget, still held.
	if (bDown && !bWasDown)
	{
		bPressStartedOver = bOver;
		if (bOver && Settings.Ripple.bEnabled && !Settings.bDesignTime)
		{
			FRipple& Added = Ripples.AddDefaulted_GetRef();
			Added.UV = Local / Size;
		}
	}
	bWasDown = bDown;
	const bool bPressed = bDown && bPressStartedOver && !Settings.bDesignTime;

	const float Intensity = FMath::Max(Settings.Intensity, 0.f);
	HoverValue.SetTarget(bOver ? 1.f : 0.f, EWidgetMotion::Smooth, Settings.HoverFade, bSnap || Settings.HoverFade <= 0.f);
	ProximityValue.SetTarget(Near, EWidgetMotion::Smooth, Settings.HoverFade, bSnap || Settings.HoverFade <= 0.f);
	bool bMoving = HoverValue.Advance(DeltaTime);
	bMoving |= ProximityValue.Advance(DeltaTime);

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
	const float HoverTarget = bOver ? 1.f : 0.f;

	// Tilt: toward or away from the pointer at its edges, while hovered.
	const FPointerTiltEffect& TiltSettings = Settings.Tilt;
	const float TiltSign = TiltSettings.Direction == EPointerTiltDirection::PressAway ? -1.f : 1.f;
	const FVector2f ClampedCentered(FMath::Clamp(Centered.X, -1.f, 1.f), FMath::Clamp(Centered.Y, -1.f, 1.f));
	const float TiltXTarget = TiltSettings.bEnabled && bOver ? TiltSign * ClampedCentered.X * TiltSettings.MaxAngle * Intensity : 0.f;
	const float TiltYTarget = TiltSettings.bEnabled && bOver ? TiltSign * ClampedCentered.Y * TiltSettings.MaxAngle * Intensity : 0.f;
	bMoving |= SpringTo(TiltSettings.Spring, TiltXTarget, TiltX, DeltaTime, bSnap, 0.01f);
	bMoving |= SpringTo(TiltSettings.Spring, TiltYTarget, TiltY, DeltaTime, bSnap, 0.01f);

	// Magnet: a lean toward the pointer, as it nears (Range) or once over.
	const FPointerMagnetEffect& MagnetSettings = Settings.Magnet;
	FVector2f LeanTarget = FVector2f::ZeroVector;
	if (MagnetSettings.bEnabled && bHasPointer)
	{
		const float Pull = MagnetSettings.Range > 0.f
			? (1.f - FMath::Clamp(FMath::Sqrt(FMath::Square(FMath::Max3(-Local.X, 0.f, Local.X - Size.X)) + FMath::Square(FMath::Max3(-Local.Y, 0.f, Local.Y - Size.Y))) / MagnetSettings.Range, 0.f, 1.f))
			: HoverTarget;
		LeanTarget = (Local - Size * 0.5f) * MagnetSettings.Pull;
		const float Reach = LeanTarget.Size();
		if (Reach > MagnetSettings.MaxOffset && Reach > KINDA_SMALL_NUMBER)
		{
			LeanTarget *= MagnetSettings.MaxOffset / Reach;
		}
		LeanTarget *= Pull * Intensity;
	}
	bMoving |= SpringTo(MagnetSettings.Spring, LeanTarget.X, LeanX, DeltaTime, bSnap, 0.02f);
	bMoving |= SpringTo(MagnetSettings.Spring, LeanTarget.Y, LeanY, DeltaTime, bSnap, 0.02f);

	// Lift on hover, sink on press.
	const float LiftTarget = TiltSettings.bEnabled ? FMath::Lerp(1.f, FMath::Lerp(1.f, TiltSettings.HoverScale, Intensity), HoverTarget) : 1.f;
	const float PressTarget = Settings.Press.bEnabled && bPressed ? FMath::Lerp(1.f, Settings.Press.PressScale, FMath::Min(Intensity, 1.f)) : 1.f;
	bMoving |= SpringTo(TiltSettings.Spring, LiftTarget, ScaleSpring, DeltaTime, bSnap, 0.0005f);
	bMoving |= SpringTo(Settings.Press.Spring, PressTarget, PressSpring, DeltaTime, bSnap, 0.0005f);

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
	SetCanTick(false);
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

	bool bAnyLight = false;
	for (const FPointerLight& Light : Settings.Lights)
	{
		const float Shown = Light.Show == EPointerLightShow::WhileHovered ? State.Hover : Light.Show == EPointerLightShow::NearPointer ? State.Proximity : 1.f;
		bAnyLight |= Light.bEnabled && Light.Color.A * Shown * Intensity > 0.002f;
	}
	if ((!bAnyLight && Ripples.Num() == 0) || Intensity <= 0.f)
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

	for (const FPointerLight& Light : Settings.Lights)
	{
		if (!Light.bEnabled)
		{
			continue;
		}
		const float Shown = Light.Show == EPointerLightShow::WhileHovered ? State.Hover : Light.Show == EPointerLightShow::NearPointer ? State.Proximity : 1.f;
		const float Strength = Light.Color.A * Shown * Intensity;
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
	return Layer;
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
	if (InPreset == EPointerEffectsPreset::Custom)
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
		OutPress.bEnabled = true;
		AddLight(EPointerLightKind::Sheen, EPointerLightShow::WhileHovered, FLinearColor(1.f, 1.f, 1.f, 0.28f), 160.f);
		break;
	case EPointerEffectsPreset::MagneticButton:
		OutMagnet.bEnabled = true;
		OutPress.bEnabled = true;
		AddLight(EPointerLightKind::Spotlight, EPointerLightShow::WhileHovered, FLinearColor(1.f, 1.f, 1.f, 0.15f), 120.f);
		break;
	case EPointerEffectsPreset::SpotlightBorder:
		AddLight(EPointerLightKind::BorderGlow, EPointerLightShow::NearPointer, FLinearColor(1.f, 1.f, 1.f, 0.7f), 200.f);
		AddLight(EPointerLightKind::Spotlight, EPointerLightShow::WhileHovered, FLinearColor(1.f, 1.f, 1.f, 0.07f), 260.f);
		break;
	case EPointerEffectsPreset::Ripple:
		OutRipple.bEnabled = true;
		OutPress.bEnabled = true;
		OutPress.PressScale = 0.98f;
		break;
	case EPointerEffectsPreset::Lift:
		OutTilt.bEnabled = true;
		OutTilt.MaxAngle = 0.f;
		OutTilt.HoverScale = 1.04f;
		OutPress.bEnabled = true;
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
	MyHost = Host;
	bTargetsCollected = false;
	bHasSent = false;
	PointerEffectsPrivate::GetComponents().AddUnique(this);
	Push();
	return Host;
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

	FPointerEffectsSettings Settings;
	Settings.Tilt = Tilt;
	Settings.Magnet = Magnet;
	Settings.Press = Press;
	Settings.Lights = Lights;
	Settings.Ripple = Ripple;
	Settings.Intensity = Intensity;
	Settings.ProximityRange = ProximityRange;
	Settings.HoverFade = HoverFade;
	Settings.bReactToFocus = bReactToFocus;
	Settings.bDesignTime = OwnerWidget && OwnerWidget->IsDesignTime();
	Settings.bPreviewInDesigner = bPreviewInDesigner;
	Settings.DesignerPointer = DesignerPointer;
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
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Tilt) || MemberName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Magnet)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Press) || MemberName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Lights)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UPointerEffectsComponent, Ripple))
	{
		Preset = EPointerEffectsPreset::Custom;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
	bTargetsCollected = false;
	Push();
}
#endif
