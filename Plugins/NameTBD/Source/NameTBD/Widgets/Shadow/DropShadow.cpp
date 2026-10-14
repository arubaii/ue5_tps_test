// Copyright AR GameSystems 2026. All rights reserved.

#include "DropShadow.h"

#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Layout/ArrangedChildren.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"
#include "UObject/UnrealType.h"
#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Widgets/GlassButton.h"
#include "NameTBD/Widgets/Glass/GlassPanel.h"
#include "NameTBD/Widgets/Shapes/ShapeWidgets.h"
#include "NameTBD/Widgets/WidgetEffectsDraw.h"

namespace DropShadowPrivate
{
	FLinearColor WithAlpha(const FLinearColor& Color, float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Alpha);
	}

	FLinearColor Accent()
	{
		return FLinearColor::FromSRGBColor(FColor(61, 139, 255));
	}

	/** A brush's corners, if it is drawn rounded. */
	bool ReadBrushCorners(const FSlateBrush& Brush, FDropShadowCaster& Out)
	{
		if (Brush.DrawAs != ESlateBrushDrawType::RoundedBox)
		{
			return false;
		}
		if (Brush.OutlineSettings.RoundingType == ESlateBrushRoundingType::HalfHeightRadius)
		{
			Out.bPill = true;
			return true;
		}
		const FVector4& Radii = Brush.OutlineSettings.CornerRadii;
		Out.Radii = FVector4f(static_cast<float>(Radii.X), static_cast<float>(Radii.Y), static_cast<float>(Radii.Z), static_cast<float>(Radii.W));
		return true;
	}

	/** A brush property of a widget, by name (Image's Brush, Border's Background): read by reflection, which every version keeps. */
	const FSlateBrush* FindBrush(const UObject* Object, FName PropertyName)
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(Object->GetClass(), PropertyName);
		if (Property && Property->Struct == FSlateBrush::StaticStruct())
		{
			return Property->ContainerPtrToValuePtr<FSlateBrush>(static_cast<const void*>(Object));
		}
		return nullptr;
	}

	bool ReadShapeCorners(const UShapeWidget* Shape, FDropShadowCaster& Out)
	{
		if (const URectangleShape* Rectangle = Cast<URectangleShape>(Shape))
		{
			if (Rectangle->bFullyRounded)
			{
				Out.bPill = true;
			}
			else if (Rectangle->bIndividualCorners)
			{
				const FVector4& Radii = Rectangle->CornerRadii;
				Out.Radii = FVector4f(static_cast<float>(Radii.X), static_cast<float>(Radii.Y), static_cast<float>(Radii.Z), static_cast<float>(Radii.W));
			}
			else
			{
				Out.Radii = FVector4f(Rectangle->CornerRounding, Rectangle->CornerRounding, Rectangle->CornerRounding, Rectangle->CornerRounding);
			}
			return true;
		}
		if (Cast<UEllipseShape>(Shape))
		{
			// A pill: exact for a circle, close for an oval.
			Out.bPill = true;
			return true;
		}
		return false;
	}
}

// =====================================================================================================
// SDropShadow
// =====================================================================================================

void SDropShadow::Construct(const FArguments& InArgs)
{
	Elevation.Snap(1.f);
	HoverAmount.Snap(0.f);
	SetCanTick(false);
	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SDropShadow::SetContent(const TSharedRef<SWidget>& InContent)
{
	ChildSlot
	[
		InContent
	];
}

void SDropShadow::SetLayers(const TArray<FDropShadowLayer>& InLayers, float InOpacity, EDropShadowUnderWidget InUnder)
{
	Layers = InLayers;
	Opacity = FMath::Max(0.f, InOpacity);
	Under = InUnder;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SDropShadow::SetCasterSource(TFunction<FDropShadowCaster()> InSource)
{
	CasterSource = MoveTemp(InSource);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SDropShadow::SetResponse(bool bInReact, float InHoverElevation, float InPressedElevation, float InHoverLift, EWidgetMotion InMotion, float InEnterDuration, float InExitDuration)
{
	bReact = bInReact;
	HoverElevation = InHoverElevation;
	PressedElevation = InPressedElevation;
	HoverLift = InHoverLift;
	Motion = InMotion;
	EnterDuration = InEnterDuration;
	ExitDuration = InExitDuration;
	if (!bReact && !ForcedHover.IsSet())
	{
		HoverAmount.Snap(0.f);
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SDropShadow::SetPressedSource(TFunction<bool()> InSource)
{
	PressedSource = MoveTemp(InSource);
}

void SDropShadow::SetElevation(float InElevation, bool bAnimate)
{
	BaseElevation = FMath::Max(0.f, InElevation);
	Elevation.SetTarget(BaseElevation, Motion, BaseElevation > Elevation.Value ? EnterDuration : ExitDuration, !bAnimate);
	if (bAnimate)
	{
		Wake();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SDropShadow::SetForcedHover(TOptional<bool> InForced)
{
	ForcedHover = InForced;
	Wake();
}

float SDropShadow::GetHoverGoal() const
{
	if (ForcedHover.IsSet())
	{
		return ForcedHover.GetValue() ? 1.f : 0.f;
	}
	if (!bReact)
	{
		return 0.f;
	}
	const bool bHovered = IsHovered() || HasAnyUserFocusOrFocusedDescendants();
	const bool bPressed = bHovered && PressedSource && PressedSource();
	return bPressed ? 2.f : bHovered ? 1.f : 0.f;
}

float SDropShadow::GetLift() const
{
	// Up to Hover Lift at full hover, back down to rest as it is pressed in.
	const float Amount = HoverAmount.Value;
	return HoverLift * (Amount <= 1.f ? Amount : FMath::Max(0.f, 2.f - Amount));
}

void SDropShadow::Wake()
{
	if (!AnimateTimer.IsValid())
	{
		AnimateTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SDropShadow::Animate));
	}
}

EActiveTimerReturnType SDropShadow::Animate(double InCurrentTime, float InDeltaTime)
{
	const float Goal = GetHoverGoal();
	if (!FMath::IsNearlyEqual(Goal, HoverAmount.Target))
	{
		HoverAmount.SetTarget(Goal, Motion, Goal > HoverAmount.Value ? EnterDuration : ExitDuration);
	}
	const bool bHoverMoving = HoverAmount.Advance(InDeltaTime);
	const bool bElevationMoving = Elevation.Advance(InDeltaTime);
	Invalidate(EInvalidateWidgetReason::Layout);

	// Kept running while hovered, to see a press.
	const bool bWatching = bReact && (IsHovered() || HasAnyUserFocusOrFocusedDescendants());
	if (bHoverMoving || bElevationMoving || bWatching)
	{
		return EActiveTimerReturnType::Continue;
	}
	AnimateTimer.Reset();
	return EActiveTimerReturnType::Stop;
}

void SDropShadow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (bReact)
	{
		Wake();
	}
}

void SDropShadow::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	if (bReact)
	{
		Wake();
	}
}

void SDropShadow::OnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent)
{
	SCompoundWidget::OnFocusChanging(PreviousFocusPath, NewWidgetPath, InFocusEvent);
	if (bReact)
	{
		Wake();
	}
}

void SDropShadow::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const TSharedRef<SWidget>& Child = ChildSlot.GetWidget();
	const EVisibility ChildVisibility = Child->GetVisibility();
	if (ArrangedChildren.Accepts(ChildVisibility))
	{
		// The lift moves the widget, not its layout: its slot keeps its place.
		ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(Child, AllottedGeometry.GetLocalSize(), FSlateLayoutTransform(FVector2D(0.0, -GetLift()))));
	}
}

void SDropShadow::BuildLayer(const FDropShadowLayer& Layer, float ElevationScale, const FVector2f& Size, const FVector4f& Corners, bool bHideUnder, float PixelScale, FWidgetShapeMesh& Out) const
{
	using namespace WidgetEffectsDraw;

	Out.Reset();
	const float AntiAlias = 1.f / PixelScale;
	const FVector2f CasterCenter = Size * 0.5f;
	const FVector2f CasterHalf = Size * 0.5f;
	const bool bScales = Layer.Kind != EDropShadowKind::Inner;
	const float Scale = bScales ? ElevationScale : 1.f;
	const FVector2f Offset = FVector2f(static_cast<float>(Layer.Offset.X), static_cast<float>(Layer.Offset.Y)) * (Layer.Kind == EDropShadowKind::Floor ? 1.f : Scale);
	const float Sigma = FMath::Max(Layer.Blur, 0.f) * 0.5f * Scale;
	const float Spread = Layer.Spread * Scale;

	FSoftBox Box;
	Box.Sigma = Sigma;
	float AlphaScale = 1.f;

	switch (Layer.Kind)
	{
	case EDropShadowKind::Inner:
	{
		Box.Center = CasterCenter + Offset;
		Box.HalfSize = CasterHalf - FVector2f(Spread, Spread);
		Box.Radii = FVector4f(FMath::Max(Corners.X - Spread, 0.f), FMath::Max(Corners.Y - Spread, 0.f), FMath::Max(Corners.Z - Spread, 0.f), FMath::Max(Corners.W - Spread, 0.f));
		const float Deepest = FMath::Min(CasterHalf.X, CasterHalf.Y) * 0.999f;
		TArray<float> Distances;
		MakeRingDistances(-Deepest, 0.f, FMath::Max(Sigma * 0.5f, 1.5f * AntiAlias), 0.f, 0.f, AntiAlias, 48, Distances);
		Distances.Add(AntiAlias);
		BuildRings(CasterCenter, CasterHalf, Corners, Distances, FMath::Max(Sigma * 0.75f, 3.f * AntiAlias), PixelScale, true, Out);
		const bool bEmpty = Box.HalfSize.X <= 0.f || Box.HalfSize.Y <= 0.f;
		for (FWidgetShapeMeshVertex& Vertex : Out.Vertices)
		{
			const float Inside = FMath::Clamp(1.f - RoundedRectDistance(Vertex.Position, CasterCenter, CasterHalf, Corners) / AntiAlias, 0.f, 1.f);
			Vertex.Coverage = (bEmpty ? 1.f : 1.f - EvaluateSoftBox(Box, Vertex.Position, AntiAlias)) * Inside;
		}
		return;
	}

	case EDropShadowKind::Floor:
	{
		// On the floor, which doesn't rise with the widget: under its resting place, wider, softer and fainter as it lifts.
		const float Lifted = FMath::Max(ElevationScale, 0.25f);
		Box.Center = FVector2f(CasterCenter.X + Offset.X, Size.Y + Offset.Y + GetLift());
		Box.HalfSize = FVector2f(Size.X * FMath::Max(Layer.FloorWidth, 0.05f) * 0.5f * (1.f + 0.12f * (Lifted - 1.f)) + Spread, FMath::Max(Layer.FloorHeight, 1.f) * 0.5f + Spread * 0.5f);
		const float Half = FMath::Min(Box.HalfSize.X, Box.HalfSize.Y);
		Box.Radii = FVector4f(Half, Half, Half, Half);
		AlphaScale = 1.f / FMath::Sqrt(Lifted);
		bHideUnder = false;
		break;
	}

	case EDropShadowKind::Outer:
	default:
		Box.Center = CasterCenter + Offset;
		Box.HalfSize = CasterHalf + FVector2f(Spread, Spread);
		Box.Radii = FVector4f(FMath::Max(Corners.X + Spread, 0.f), FMath::Max(Corners.Y + Spread, 0.f), FMath::Max(Corners.Z + Spread, 0.f), FMath::Max(Corners.W + Spread, 0.f));
		break;
	}

	if (Box.HalfSize.X <= 0.f || Box.HalfSize.Y <= 0.f)
	{
		return;
	}

	TArray<float> Distances;
	if (!bHideUnder)
	{
		// Rings round the shadow's own shape, dense where it fades: few rings, and exact.
		const float Deepest = FMath::Min(Box.HalfSize.X, Box.HalfSize.Y) * 0.999f;
		if (Sigma < 0.3f * AntiAlias)
		{
			Distances = { -Deepest, -0.5f * AntiAlias, 0.5f * AntiAlias };
		}
		else
		{
			MakeRingDistances(-Deepest, 3.2f * Sigma + AntiAlias, FMath::Max(Sigma * 0.75f, 2.f * AntiAlias), 0.f, 2.2f * Sigma, FMath::Max(Sigma * 0.35f, AntiAlias), 40, Distances);
		}
		BuildRings(Box.Center, Box.HalfSize, Box.Radii, Distances, FMath::Max(Sigma, 4.f * AntiAlias), PixelScale, true, Out);
		for (FWidgetShapeMeshVertex& Vertex : Out.Vertices)
		{
			Vertex.Coverage = EvaluateSoftBox(Box, Vertex.Position, AntiAlias) * AlphaScale;
		}
		return;
	}

	// Left clear under the widget: rings round the widget itself from its edge out, so the clear part is crisp.
	const float Reach = Offset.Size() + FMath::Max(Spread, 0.f) + 3.2f * Sigma + AntiAlias;
	MakeRingDistances(0.f, Reach, FMath::Max(Sigma * 0.5f, 1.5f * AntiAlias), 0.f, 0.f, AntiAlias, 48, Distances);
	Distances.Insert(-AntiAlias, 0);
	BuildRings(CasterCenter, CasterHalf, Corners, Distances, FMath::Max(Sigma * 0.75f, 3.f * AntiAlias), PixelScale, false, Out);
	for (FWidgetShapeMeshVertex& Vertex : Out.Vertices)
	{
		const float Outside = FMath::Clamp(1.f + RoundedRectDistance(Vertex.Position, CasterCenter, CasterHalf, Corners) / AntiAlias, 0.f, 1.f);
		Vertex.Coverage = EvaluateSoftBox(Box, Vertex.Position, AntiAlias) * Outside * AlphaScale;
	}
}

int32 SDropShadow::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace WidgetEffectsDraw;

	FArrangedChildren Arranged(EVisibility::Visible);
	ArrangeChildren(AllottedGeometry, Arranged);
	if (Arranged.Num() == 0)
	{
		return LayerId;
	}

	// Cast from the widget as it is drawn: its own render transform (a Widget Animation scaling it) and the lift included.
	const FArrangedWidget& Caster = Arranged[0];
	const FGeometry& CasterGeometry = Caster.Geometry;
	const FVector2D LocalSize = CasterGeometry.GetLocalSize();
	const int32 LayerCount = Layers.Num();
	if (LayerCount == 0 || LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
	{
		return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	const FVector2f Size(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	const float PixelScale = QuantizeScale(GetPixelScale(CasterGeometry));
	const FDropShadowCaster Shape = CasterSource ? CasterSource() : FDropShadowCaster();
	const float Half = FMath::Min(Size.X, Size.Y) * 0.5f;
	const FVector4f Corners = Shape.bPill ? FVector4f(Half, Half, Half, Half) : FitRadii(Shape.Radii, Size);
	const bool bHideUnder = Under == EDropShadowUnderWidget::Hide || (Under == EDropShadowUnderWidget::Auto && Shape.bSeeThrough);

	// Elevation: as set, raised toward Hover Elevation while hovered, toward Pressed Elevation while pressed.
	const float Amount = HoverAmount.Value;
	const float HoverScale = Amount <= 1.f ? FMath::Lerp(1.f, HoverElevation, Amount) : FMath::Lerp(HoverElevation, PressedElevation, FMath::Min(Amount - 1.f, 1.f));
	const float ElevationScale = FMath::Max(0.f, Elevation.Value * HoverScale);
	const float Lift = GetLift();

	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint() * FLinearColor(1.f, 1.f, 1.f, Caster.Widget->GetRenderOpacity());
	Meshes.SetNum(LayerCount);

	auto PaintLayer = [&](int32 Index, int32 PaintLayerId)
	{
		const FDropShadowLayer& Layer = Layers[Index];
		uint32 Hash = GetTypeHash(static_cast<uint8>(Layer.Kind));
		Hash = HashCombineFast(Hash, GetTypeHash(Layer.Offset));
		Hash = HashCombineFast(Hash, GetTypeHash(Layer.Blur));
		Hash = HashCombineFast(Hash, GetTypeHash(Layer.Spread));
		Hash = HashCombineFast(Hash, GetTypeHash(Layer.FloorWidth));
		Hash = HashCombineFast(Hash, GetTypeHash(Layer.FloorHeight));
		Hash = HashCombineFast(Hash, GetTypeHash(Size));
		Hash = HashCombineFast(Hash, GetTypeHash(PixelScale));
		Hash = HashCombineFast(Hash, HashRadii(Corners));
		Hash = HashCombineFast(Hash, GetTypeHash(bHideUnder));
		Hash = HashCombineFast(Hash, GetTypeHash(Layer.Kind == EDropShadowKind::Inner ? 1.f : ElevationScale));
		Hash = HashCombineFast(Hash, GetTypeHash(Layer.Kind == EDropShadowKind::Floor ? Lift : 0.f));
		FLayerMesh& Cached = Meshes[Index];
		if (!Cached.bBuilt || Cached.Hash != Hash)
		{
			BuildLayer(Layer, ElevationScale, Size, Corners, bHideUnder, PixelScale, Cached.Mesh);
			Cached.Hash = Hash;
			Cached.bBuilt = true;
		}
		FLinearColor Color = Layer.Color;
		Color.A *= Opacity;
		PaintMesh(OutDrawElements, PaintLayerId, CasterGeometry, Cached.Mesh, WidgetTint, [&Color](const FWidgetShapeMeshVertex&) { return Color; });
	};

	// Drop and floor shadows under the widget, each over the last.
	int32 NextLayer = LayerId;
	for (int32 Index = 0; Index < LayerCount; ++Index)
	{
		const FDropShadowLayer& Layer = Layers[Index];
		if (Layer.bEnabled && Layer.Kind != EDropShadowKind::Inner && Layer.Color.A * Opacity > 0.001f)
		{
			PaintLayer(Index, NextLayer++);
		}
	}

	int32 MaxLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, NextLayer, InWidgetStyle, bParentEnabled);

	// Inner shadows over it: a wrapper can't get between a widget's background and its content.
	for (int32 Index = 0; Index < LayerCount; ++Index)
	{
		const FDropShadowLayer& Layer = Layers[Index];
		if (Layer.bEnabled && Layer.Kind == EDropShadowKind::Inner && Layer.Color.A * Opacity > 0.001f)
		{
			PaintLayer(Index, ++MaxLayer);
		}
	}
	return MaxLayer;
}

// =====================================================================================================
// UDropShadowComponent
// =====================================================================================================

UDropShadowComponent::UDropShadowComponent()
{
	Layers = MakePresetLayers(Preset, ShadowColor);
}

TArray<FDropShadowLayer> UDropShadowComponent::MakePresetLayers(EDropShadowPreset InPreset, FLinearColor InColor)
{
	using namespace DropShadowPrivate;

	const EDropShadowKind Drop = EDropShadowKind::Outer;
	const EDropShadowKind Inner = EDropShadowKind::Inner;
	TArray<FDropShadowLayer> Result;
	switch (InPreset)
	{
	case EDropShadowPreset::Subtle:
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 1.0), 3.f, 0.f, WithAlpha(InColor, 0.12f)));
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 1.0), 2.f, 0.f, WithAlpha(InColor, 0.22f)));
		break;

	case EDropShadowPreset::Elevated:
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 10.0), 20.f, 0.f, WithAlpha(InColor, 0.19f)));
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 6.0), 6.f, 0.f, WithAlpha(InColor, 0.23f)));
		break;

	case EDropShadowPreset::Layered:
		for (int32 Step = 0; Step < 6; ++Step)
		{
			const float Size = static_cast<float>(1 << Step);
			Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, Size), Size, 0.f, WithAlpha(InColor, 0.075f)));
		}
		break;

	case EDropShadowPreset::Dramatic:
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 30.0), 60.f, -12.f, WithAlpha(InColor, 0.28f)));
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 18.0), 36.f, -18.f, WithAlpha(InColor, 0.32f)));
		break;

	case EDropShadowPreset::Hard:
		Result.Add(FDropShadowLayer(Drop, FVector2D(6.0, 6.0), 0.f, 0.f, WithAlpha(InColor, 1.f)));
		break;

	case EDropShadowPreset::Glow:
		Result.Add(FDropShadowLayer(Drop, FVector2D::ZeroVector, 8.f, 0.f, WithAlpha(InColor, 0.75f)));
		Result.Add(FDropShadowLayer(Drop, FVector2D::ZeroVector, 24.f, 4.f, WithAlpha(InColor, 0.45f)));
		Result.Add(FDropShadowLayer(Drop, FVector2D::ZeroVector, 56.f, 8.f, WithAlpha(InColor, 0.25f)));
		break;

	case EDropShadowPreset::Tinted:
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 10.0), 30.f, -6.f, WithAlpha(InColor, 0.5f)));
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 4.0), 10.f, -2.f, WithAlpha(InColor, 0.3f)));
		break;

	case EDropShadowPreset::Floating:
	{
		FDropShadowLayer Floor(EDropShadowKind::Floor, FVector2D(0.0, 18.0), 16.f, 0.f, WithAlpha(InColor, 0.38f));
		Floor.FloorWidth = 0.78f;
		Floor.FloorHeight = 10.f;
		Result.Add(Floor);
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 2.0), 6.f, 0.f, WithAlpha(InColor, 0.08f)));
		break;
	}

	case EDropShadowPreset::Contact:
	{
		FDropShadowLayer Floor(EDropShadowKind::Floor, FVector2D(0.0, 1.0), 6.f, 0.f, WithAlpha(InColor, 0.5f));
		Floor.FloorWidth = 0.94f;
		Floor.FloorHeight = 5.f;
		Result.Add(Floor);
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 1.0), 2.f, 0.f, WithAlpha(InColor, 0.15f)));
		break;
	}

	case EDropShadowPreset::SoftUIRaised:
		Result.Add(FDropShadowLayer(Drop, FVector2D(-6.0, -6.0), 16.f, 0.f, FLinearColor(1.f, 1.f, 1.f, 0.7f)));
		Result.Add(FDropShadowLayer(Drop, FVector2D(6.0, 6.0), 16.f, 0.f, WithAlpha(InColor, 0.25f)));
		break;

	case EDropShadowPreset::SoftUIPressed:
		Result.Add(FDropShadowLayer(Inner, FVector2D(-4.0, -4.0), 10.f, 0.f, FLinearColor(1.f, 1.f, 1.f, 0.6f)));
		Result.Add(FDropShadowLayer(Inner, FVector2D(4.0, 4.0), 10.f, 0.f, WithAlpha(InColor, 0.28f)));
		break;

	case EDropShadowPreset::Inset:
		Result.Add(FDropShadowLayer(Inner, FVector2D(0.0, 2.0), 6.f, 0.f, WithAlpha(InColor, 0.38f)));
		break;

	case EDropShadowPreset::Soft:
	case EDropShadowPreset::Custom:
	default:
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 12.0), 28.f, -6.f, WithAlpha(InColor, 0.22f)));
		Result.Add(FDropShadowLayer(Drop, FVector2D(0.0, 2.0), 6.f, 0.f, WithAlpha(InColor, 0.08f)));
		break;
	}
	return Result;
}

void UDropShadowComponent::ApplyPreset()
{
	if (Preset == EDropShadowPreset::Custom)
	{
		return;
	}

	// A glow or colored shadow in black is no glow at all: start them in the accent color.
	const bool bWantsColor = Preset == EDropShadowPreset::Glow || Preset == EDropShadowPreset::Tinted;
	if (bWantsColor && ShadowColor.GetLuminance() < 0.02f)
	{
		ShadowColor = DropShadowPrivate::Accent();
	}
	Layers = MakePresetLayers(Preset, ShadowColor);
}

void UDropShadowComponent::SetPreset(EDropShadowPreset InPreset)
{
	Preset = InPreset;
	ApplyPreset();
	Push();
}

void UDropShadowComponent::SetShadowColor(FLinearColor InColor)
{
	ShadowColor = InColor;
	if (Preset != EDropShadowPreset::Custom)
	{
		Layers = MakePresetLayers(Preset, ShadowColor);
	}
	else
	{
		// Custom layers keep their own alpha, in the new color.
		for (FDropShadowLayer& Layer : Layers)
		{
			Layer.Color = DropShadowPrivate::WithAlpha(InColor, Layer.Color.A);
		}
	}
	Push();
}

void UDropShadowComponent::SetLayers(const TArray<FDropShadowLayer>& InLayers)
{
	Layers = InLayers;
	Preset = EDropShadowPreset::Custom;
	Push();
}

void UDropShadowComponent::SetOpacity(float InOpacity)
{
	Opacity = FMath::Max(0.f, InOpacity);
	Push();
}

void UDropShadowComponent::SetElevation(float InElevation, bool bAnimate)
{
	Elevation = FMath::Max(0.f, InElevation);
	if (const TSharedPtr<SDropShadow> Shadow = MyShadow.Pin())
	{
		Shadow->SetElevation(Elevation, bAnimate);
	}
}

void UDropShadowComponent::SetForcedHover(bool bHovered)
{
	if (const TSharedPtr<SDropShadow> Shadow = MyShadow.Pin())
	{
		Shadow->SetForcedHover(bHovered);
	}
}

void UDropShadowComponent::ClearForcedHover()
{
	if (const TSharedPtr<SDropShadow> Shadow = MyShadow.Pin())
	{
		Shadow->SetForcedHover(TOptional<bool>());
	}
}

UWidget* UDropShadowComponent::GetShadowedWidget() const
{
	return GetOwner().Get();
}

FDropShadowCaster UDropShadowComponent::ReadCaster(const UWidget* Widget)
{
	using namespace DropShadowPrivate;

	FDropShadowCaster Result;
	if (!Widget)
	{
		return Result;
	}

	if (const UGlassPanel* GlassPanel = Cast<UGlassPanel>(Widget))
	{
		Result.bPill = GlassPanel->bFullyRounded;
		Result.Radii = GlassPanel->GetCornerRadiiSetting();
		Result.bSeeThrough = true;
		return Result;
	}
	if (const UShapeWidget* Shape = Cast<UShapeWidget>(Widget))
	{
		ReadShapeCorners(Shape, Result);
		return Result;
	}
	if (const UContextMenuButton* MenuButton = Cast<UContextMenuButton>(Widget))
	{
		// A ready-made button's background is its Background Shape (a Glass Button's, its glass).
		Result.bSeeThrough = Widget->IsA<UGlassButton>();
		const UGlassButton* GlassButton = Cast<UGlassButton>(Widget);
		if (const UGlassPanel* Glass = GlassButton ? GlassButton->GetGlassPart() : nullptr)
		{
			Result.bPill = Glass->bFullyRounded;
			Result.Radii = Glass->GetCornerRadiiSetting();
			return Result;
		}
		if (ReadShapeCorners(MenuButton->GetBackgroundShapeWidget(), Result))
		{
			return Result;
		}
	}
	if (const UButton* Button = Cast<UButton>(Widget))
	{
		ReadBrushCorners(Button->GetStyle().Normal, Result);
		return Result;
	}
	if (const UBackgroundBlur* Blur = Cast<UBackgroundBlur>(Widget))
	{
		Result.bSeeThrough = true;
		const FStructProperty* Property = FindFProperty<FStructProperty>(Blur->GetClass(), TEXT("CornerRadius"));
		if (Property && Property->Struct == TBaseStructure<FVector4>::Get())
		{
			const FVector4& Radii = *Property->ContainerPtrToValuePtr<FVector4>(static_cast<const void*>(Blur));
			Result.Radii = FVector4f(static_cast<float>(Radii.X), static_cast<float>(Radii.Y), static_cast<float>(Radii.Z), static_cast<float>(Radii.W));
		}
		return Result;
	}
	if (const FSlateBrush* Brush = FindBrush(Widget, Widget->IsA<UBorder>() ? FName(TEXT("Background")) : FName(TEXT("Brush"))))
	{
		ReadBrushCorners(*Brush, Result);
	}
	return Result;
}

FDropShadowCaster UDropShadowComponent::ResolveCaster() const
{
	FDropShadowCaster Result = ReadCaster(GetOwner().Get());
	if (Corners == EDropShadowCorners::Custom)
	{
		Result.bPill = bFullyRounded;
		Result.Radii = FVector4f(static_cast<float>(CornerRadii.X), static_cast<float>(CornerRadii.Y), static_cast<float>(CornerRadii.Z), static_cast<float>(CornerRadii.W));
	}
	return Result;
}

TSharedRef<SWidget> UDropShadowComponent::RebuildWidgetWithContent(const TSharedRef<SWidget> OwnerContent)
{
	TSharedRef<SDropShadow> Shadow = SNew(SDropShadow)
	[
		OwnerContent
	];
	MyShadow = Shadow;
	Push();
	Shadow->SetElevation(Elevation, false);
	return Shadow;
}

void UDropShadowComponent::Push()
{
	const TSharedPtr<SDropShadow> Shadow = MyShadow.Pin();
	if (!Shadow.IsValid())
	{
		return;
	}

	Shadow->SetLayers(Layers, Opacity, UnderWidget);
	const TWeakObjectPtr<const UDropShadowComponent> WeakThisComponent = this;
	Shadow->SetCasterSource([WeakThisComponent]()
	{
		const UDropShadowComponent* Component = WeakThisComponent.Get();
		return Component ? Component->ResolveCaster() : FDropShadowCaster();
	});
	Shadow->SetResponse(bReactToHover, HoverElevation, PressedElevation, HoverLift, Motion, EnterDuration, ExitDuration);

	const TWeakObjectPtr<const UButton> WeakButton = Cast<UButton>(GetOwner().Get());
	Shadow->SetPressedSource(WeakButton.IsValid() ? TFunction<bool()>([WeakButton]() { return WeakButton.IsValid() && WeakButton->IsPressed(); }) : TFunction<bool()>());
}

#if WITH_EDITOR
void UDropShadowComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName ChangedName = PropertyChangedEvent.GetPropertyName();
	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
	if (ChangedName == GET_MEMBER_NAME_CHECKED(UDropShadowComponent, Preset) || ChangedName == GET_MEMBER_NAME_CHECKED(UDropShadowComponent, ShadowColor)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UDropShadowComponent, ShadowColor))
	{
		ApplyPreset();
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UDropShadowComponent, Layers))
	{
		Preset = EDropShadowPreset::Custom;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
	Push();
	if (const TSharedPtr<SDropShadow> Shadow = MyShadow.Pin())
	{
		Shadow->SetElevation(Elevation, false);
	}
}
#endif
