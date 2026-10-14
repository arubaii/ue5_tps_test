// Copyright AR GameSystems 2026. All rights reserved.

#include "FlipCard.h"

#include "Layout/ArrangedChildren.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SNullWidget.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeTypes.h"
#include "NameTBD/Widgets/WidgetEffectsDraw.h"

#define LOCTEXT_NAMESPACE "FlipCard"

namespace FlipCardPrivate
{
	const FName FrontSlot(TEXT("Front"));
	const FName BackSlot(TEXT("Back"));

	/** Short of edge-on: a Shape Box turns at most this far (its own limit is 89.5). */
	constexpr float MaxFaceAngle = 89.4f;

	float SmoothStep(float Alpha)
	{
		const float T = FMath::Clamp(Alpha, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** A render transform scaling by Scale and turning by Degrees (clockwise) about the widget's pivot, then moving by Translation. */
	FSlateRenderTransform MakeTransform(float Scale, float Degrees, const FVector2D& Translation)
	{
		float Sin = 0.f;
		float Cos = 1.f;
		FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(Degrees));
		return FSlateRenderTransform(FMatrix2x2(Cos * Scale, Sin * Scale, -Sin * Scale, Cos * Scale), Translation);
	}
}

// =====================================================================================================
// SFlipCard
// =====================================================================================================

SFlipCard::SFlipCard()
	: Children(this)
{
}

void SFlipCard::Construct(const FArguments& InArgs)
{
	OnRequestFlip = InArgs._OnRequestFlip;
	OnFlipFinished = InArgs._OnFlipFinished;
	SetCanTick(false);

	FrontFace = SNew(SShapeBox);
	BackFace = SNew(SShapeBox);
	Children.Add(FrontFace.ToSharedRef());
	Children.Add(BackFace.ToSharedRef());

	Turn.Snap(0.f);
	TiltX.Snap(0.f);
	TiltY.Snap(0.f);
	HoverAmount.Snap(0.f);
	ApplyPose();
}

void SFlipCard::SetFrontContent(const TSharedRef<SWidget>& InContent)
{
	FrontFace->SetContent(InContent);
}

void SFlipCard::SetBackContent(const TSharedRef<SWidget>& InContent)
{
	BackFace->SetContent(InContent);
}

void SFlipCard::SetSettings(const FFlipCardSettings& InSettings)
{
	const bool bWasTurning = IsTurnMotion();
	Settings = InSettings;
	if (bWasTurning != IsTurnMotion() && !Turn.IsMoving())
	{
		Turn.Snap(bShowingBack ? 1.f : 0.f);
	}
	if (!Settings.bTiltOnHover)
	{
		TiltX.Snap(0.f);
		TiltY.Snap(0.f);
		HoverAmount.Snap(0.f);
	}
	ApplyPose();
}

bool SFlipCard::IsTurnMotion() const
{
	switch (Settings.Motion)
	{
	case EFlipCardMotion::Swing:
	case EFlipCardMotion::FadeThrough:
	case EFlipCardMotion::Shuffle:
		return false;
	default:
		return true;
	}
}

EWidgetMotion SFlipCard::GetCurve() const
{
	switch (Settings.Motion)
	{
	case EFlipCardMotion::SpringFlip: return EWidgetMotion::Bouncy;
	case EFlipCardMotion::CoinSpin: return EWidgetMotion::Smooth;
	case EFlipCardMotion::FadeThrough: return EWidgetMotion::Linear;
	default: return EWidgetMotion::Gentle;
	}
}

void SFlipCard::SetShowBack(bool bBack, bool bAnimate)
{
	if (bBack == bShowingBack)
	{
		if (!bAnimate)
		{
			Turn.Snap(bBack ? 1.f : 0.f);
			ApplyPose();
		}
		return;
	}
	bShowingBack = bBack;

	float Target = bBack ? 1.f : 0.f;
	if (IsTurnMotion() && bAnimate)
	{
		// In half turns: the back is odd, the front even. Carry on round the same way, or turn back the way it came.
		const float Step = Settings.Motion == EFlipCardMotion::CoinSpin ? 3.f : 1.f;
		const float Heading = FMath::RoundToFloat(Turn.Target);
		const bool bBackward = !bBack && Settings.bReverseOnReturn;
		Target = bBackward ? Heading - Step : Heading + Step;
	}

	// A coin spin covers three times the turn: it gets a little longer to do it in.
	const float Duration = (bBack ? Settings.EnterDuration : Settings.ExitDuration) * (Settings.Motion == EFlipCardMotion::CoinSpin ? 1.6f : 1.f);
	TurnFrom = Turn.Value;
	Turn.SetTarget(Target, GetCurve(), Duration, !bAnimate);
	ApplyPose();
	if (Turn.IsMoving())
	{
		Wake();
	}
}

float SFlipCard::GetProgress() const
{
	if (!IsTurnMotion())
	{
		return FMath::Clamp(Turn.Value, 0.f, 1.f);
	}
	float Wrapped = FMath::Fmod(Turn.Value, 2.f);
	if (Wrapped < 0.f)
	{
		Wrapped += 2.f;
	}
	return Wrapped <= 1.f ? Wrapped : 2.f - Wrapped;
}

FShapeBoxShape SFlipCard::MakeFaceShape(const FFacePose& Pose) const
{
	FShapeBoxShape Shape;
	Shape.Angle = FVector2D(Pose.Yaw, Pose.Pitch);
	Shape.Perspective = Settings.Perspective;
	return Shape;
}

void SFlipCard::ApplyPose()
{
	using namespace FlipCardPrivate;

	FFacePose& Front = Poses[0];
	FFacePose& Back = Poses[1];
	Front = FFacePose();
	Back = FFacePose();
	bBackOnTop = false;
	const double Width = FMath::Max(LastSize.X, 1.0);

	switch (Settings.Motion)
	{
	case EFlipCardMotion::Swing:
	{
		const float Progress = FMath::Clamp(Turn.Value, 0.f, 1.f);
		if (Progress < 0.5f)
		{
			// The front opens away on its left edge, speeding up.
			const float Open = FMath::Square(Progress / 0.5f);
			Front.Yaw = -MaxFaceAngle * Open;
			Front.Shade = Settings.Shading * Open;
			Back.bVisible = false;
		}
		else
		{
			// The back closes in on its right edge, settling.
			const float Close = 1.f - FMath::Square(1.f - (Progress - 0.5f) / 0.5f);
			Back.Yaw = MaxFaceAngle * (1.f - Close);
			Back.Shade = Settings.Shading * (1.f - Close);
			Front.bVisible = false;
		}
		break;
	}

	case EFlipCardMotion::FadeThrough:
	{
		const float Progress = FMath::Clamp(Turn.Value, 0.f, 1.f);
		if (Progress < 0.5f)
		{
			const float Out = SmoothStep(Progress / 0.5f);
			Front.Opacity = 1.f - Out;
			Front.Scale = FMath::Lerp(1.f, 0.92f, Out);
			Back.bVisible = false;
		}
		else
		{
			const float In = SmoothStep((Progress - 0.5f) / 0.5f);
			Back.Opacity = In;
			Back.Scale = FMath::Lerp(0.92f, 1.f, In);
			Front.bVisible = false;
		}
		Front.Lift = 0.f;
		Back.Lift = 0.f;
		break;
	}

	case EFlipCardMotion::Shuffle:
	{
		const float Progress = FMath::Clamp(Turn.Value, 0.f, 1.f);
		const float Out = FMath::Sin(Progress * UE_PI);
		Front.Translation = FVector2D(0.62 * Width * Out, 0.0);
		Front.Roll = 7.f * Out;
		Front.Lift = 0.5f * Out;
		Front.bVisible = Progress < 0.999f;
		Back.Translation = FVector2D(-0.08 * Width * Out, 0.0);
		Back.Roll = -3.f * Out;
		Back.Scale = FMath::Lerp(0.96f, 1.f, Progress);
		Back.Lift = 0.25f * Out;
		Back.bVisible = Progress > 0.001f;
		bBackOnTop = Progress >= 0.5f;
		break;
	}

	default:
	{
		// A turn: 180 degrees per half turn. Whichever face looks toward the viewer is shown, turned by what is left of it.
		const float Degrees = 180.f * Turn.Value;
		float Around = FMath::Fmod(Degrees, 360.f);
		if (Around < 0.f)
		{
			Around += 360.f;
		}
		const bool bFrontShown = Around <= 90.f || Around >= 270.f;
		const float FaceAngle = FMath::Clamp(bFrontShown ? (Around > 180.f ? Around - 360.f : Around) : Around - 180.f, -MaxFaceAngle, MaxFaceAngle);
		const float Sideways = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Degrees)));

		FFacePose& Shown = bFrontShown ? Front : Back;
		(bFrontShown ? Back : Front).bVisible = false;
		if (Settings.Motion == EFlipCardMotion::FlipVertical)
		{
			Shown.Pitch = FaceAngle;
		}
		else
		{
			Shown.Yaw = FaceAngle;
		}
		Shown.Shade = Settings.Shading * (1.f - FMath::Cos(FMath::DegreesToRadians(FaceAngle)));
		Shown.Glare = Settings.Glare * Sideways;
		Shown.bGlareFromRight = FaceAngle > 0.f;
		Shown.Lift = 0.3f * Sideways;
		if (Settings.Motion == EFlipCardMotion::LiftAndFlip)
		{
			Shown.Lift = Sideways;
			Shown.Scale = 1.f + Settings.Lift * Sideways;
			Shown.Translation = FVector2D(0.0, -8.0 * Sideways);
		}
		break;
	}
	}

	// Tilt toward the pointer: the part under it pressed away.
	for (FFacePose& Pose : Poses)
	{
		if (Pose.bVisible && (TiltX.Value != 0.f || TiltY.Value != 0.f))
		{
			Pose.Yaw = FMath::Clamp(Pose.Yaw + TiltX.Value, -MaxFaceAngle, MaxFaceAngle);
			Pose.Pitch = FMath::Clamp(Pose.Pitch + TiltY.Value, -MaxFaceAngle, MaxFaceAngle);
		}
	}

	// Swing hinges on an edge: move the turned face so that edge stays where it was.
	if (Settings.Motion == EFlipCardMotion::Swing)
	{
		const FVector2D LayoutSize(Width, FMath::Max(LastSize.Y, 1.0));
		if (Front.bVisible)
		{
			const FVector2D Hinge = MakeFaceShape(Front).Evaluate(LayoutSize, FVector2D(0.0, 0.5));
			Front.Translation.X += -Hinge.X;
		}
		if (Back.bVisible)
		{
			const FVector2D Hinge = MakeFaceShape(Back).Evaluate(LayoutSize, FVector2D(1.0, 0.5));
			Back.Translation.X += LayoutSize.X - Hinge.X;
		}
	}

	const TSharedPtr<SShapeBox> Faces[2] = { FrontFace, BackFace };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FFacePose& Pose = Poses[Index];
		const TSharedPtr<SShapeBox>& Face = Faces[Index];
		if (!Face.IsValid())
		{
			continue;
		}
		// Self Hit Test Invisible: the card takes the clicks, a button on the face its own.
		Face->SetVisibility(Pose.bVisible ? EVisibility::SelfHitTestInvisible : EVisibility::Hidden);
		Face->SetShape(MakeFaceShape(Pose));
		Face->SetRenderTransformPivot(FVector2D(0.5, 0.5));
		Face->SetRenderTransform(MakeTransform(Pose.Scale, Pose.Roll, Pose.Translation));
		Face->SetRenderOpacity(Pose.Opacity);
		Face->Invalidate(EInvalidateWidgetReason::Paint);
	}
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SFlipCard::Wake()
{
	if (!AnimateTimer.IsValid())
	{
		AnimateTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SFlipCard::Animate));
	}
}

EActiveTimerReturnType SFlipCard::Animate(double InCurrentTime, float InDeltaTime)
{
	const bool bWasTurning = Turn.IsMoving();
	const bool bTurning = Turn.Advance(InDeltaTime);
	const bool bTilting = TiltX.Advance(InDeltaTime) | TiltY.Advance(InDeltaTime) | HoverAmount.Advance(InDeltaTime);

	if (bWasTurning && !bTurning)
	{
		// Arrived: back to a plain front (0) or back (1), however many turns it took to get there.
		if (IsTurnMotion())
		{
			float Wrapped = FMath::Fmod(FMath::RoundToFloat(Turn.Value), 2.f);
			if (Wrapped < 0.f)
			{
				Wrapped += 2.f;
			}
			Turn.Snap(Wrapped);
		}
		ApplyPose();
		OnFlipFinished.ExecuteIfBound(bShowingBack);
	}
	else
	{
		ApplyPose();
	}

	if (bTurning || bTilting)
	{
		return EActiveTimerReturnType::Continue;
	}
	AnimateTimer.Reset();
	return EActiveTimerReturnType::Stop;
}

FVector2D SFlipCard::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const FVector2D FrontSize = FrontFace->GetDesiredSize();
	const FVector2D BackSize = BackFace->GetDesiredSize();
	return FVector2D(FMath::Max(FrontSize.X, BackSize.X), FMath::Max(FrontSize.Y, BackSize.Y));
}

void SFlipCard::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	LastSize = LocalSize;
	for (const TSharedPtr<SShapeBox>& FacePtr : { FrontFace, BackFace })
	{
		if (!FacePtr.IsValid())
		{
			continue;
		}
		const TSharedRef<SShapeBox> Face = FacePtr.ToSharedRef();
		const EVisibility FaceVisibility = Face->GetVisibility();
		if (ArrangedChildren.Accepts(FaceVisibility))
		{
			// Its own render transform (lift, slide, turn about the middle) comes with it.
			ArrangedChildren.AddWidget(FaceVisibility, AllottedGeometry.MakeChild(Face, LocalSize, FSlateLayoutTransform()));
		}
	}
}

void SFlipCard::PaintShadow(const FArrangedWidget& Face, const FFacePose& Pose, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	using namespace WidgetEffectsDraw;

	if (!Settings.bCastShadow || Settings.ShadowColor.A <= 0.001f || Pose.Opacity <= 0.01f)
	{
		return;
	}

	// Under the face as it is drawn: its turned outline's bounds, the shadow spreading and fading as it lifts.
	const FVector2D Size = Face.Geometry.GetLocalSize();
	const FShapeBoxShape Shape = MakeFaceShape(Pose);
	FBox2f Bounds(ForceInit);
	for (const FVector2D& Corner : { FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(1.0, 1.0), FVector2D(0.0, 1.0) })
	{
		const FVector2D Projected = Shape.Evaluate(Size, Corner);
		Bounds += FVector2f(static_cast<float>(Projected.X), static_cast<float>(Projected.Y));
	}

	const float Lifted = FMath::Clamp(Pose.Lift, 0.f, 1.f);
	const float PixelScale = QuantizeScale(GetPixelScale(Face.Geometry));
	const float AntiAlias = 1.f / PixelScale;
	FSoftBox Box;
	Box.Center = Bounds.GetCenter() + FVector2f(static_cast<float>(Settings.ShadowOffset.X), static_cast<float>(Settings.ShadowOffset.Y)) * (1.f + 1.2f * Lifted);
	Box.HalfSize = Bounds.GetExtent();
	const float Radius = FMath::Min(Settings.CornerRadius, FMath::Min(Box.HalfSize.X, Box.HalfSize.Y));
	Box.Radii = FVector4f(Radius, Radius, Radius, Radius);
	Box.Sigma = FMath::Max(Settings.ShadowBlur, 0.f) * 0.5f * (1.f + 0.8f * Lifted);
	if (Box.HalfSize.X <= 0.5f || Box.HalfSize.Y <= 0.5f)
	{
		return;
	}

	TArray<float> Distances;
	const float Deepest = FMath::Min(Box.HalfSize.X, Box.HalfSize.Y) * 0.999f;
	MakeRingDistances(-Deepest, 3.2f * Box.Sigma + AntiAlias, FMath::Max(Box.Sigma * 0.75f, 2.f * AntiAlias), 0.f, 2.2f * Box.Sigma, FMath::Max(Box.Sigma * 0.35f, AntiAlias), 32, Distances);
	FWidgetShapeMesh Mesh;
	BuildRings(Box.Center, Box.HalfSize, Box.Radii, Distances, FMath::Max(Box.Sigma, 4.f * AntiAlias), PixelScale, true, Mesh);
	for (FWidgetShapeMeshVertex& Vertex : Mesh.Vertices)
	{
		Vertex.Coverage = EvaluateSoftBox(Box, Vertex.Position, AntiAlias);
	}

	FLinearColor Color = Settings.ShadowColor;
	Color.A *= Pose.Opacity * (1.f - 0.35f * Lifted);
	PaintMesh(OutDrawElements, LayerId, Face.Geometry, Mesh, InWidgetStyle.GetColorAndOpacityTint(), [&Color](const FWidgetShapeMeshVertex&) { return Color; });
}

void SFlipCard::PaintFace(int32 FaceIndex, const FArrangedWidget& Face, const FPaintArgs& Args, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32& InOutLayer, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace WidgetEffectsDraw;

	const FFacePose& Pose = Poses[FaceIndex];
	InOutLayer = Face.Widget->Paint(Args.WithNewParent(this), Face.Geometry, MyCullingRect, OutDrawElements, InOutLayer + 1, InWidgetStyle, ShouldBeEnabled(bParentEnabled));

	const float TiltLight = Settings.bTiltOnHover ? Settings.TiltGlare * HoverAmount.Value : 0.f;
	if (Pose.Shade <= 0.002f && Pose.Glare <= 0.002f && TiltLight <= 0.002f)
	{
		return;
	}

	// The face's outline, shaded and lit in its own terms (0..1 across it), then turned with it.
	const FVector2D Size = Face.Geometry.GetLocalSize();
	const FVector2f FaceSize(static_cast<float>(Size.X), static_cast<float>(Size.Y));
	const float PixelScale = QuantizeScale(GetPixelScale(Face.Geometry));
	FWidgetShapeOutline Outline;
	AppendRoundedRect(Outline, FVector2f::ZeroVector, FaceSize, FVector4f(Settings.CornerRadius, Settings.CornerRadius, Settings.CornerRadius, Settings.CornerRadius), WidgetShapeGeometry::GetTolerance(PixelScale));
	FWidgetShapeMesh Mesh;
	TArray<int8> FillOnLeft;
	WidgetShapeGeometry::BuildFill(Outline, 1.f / PixelScale, Mesh, FillOnLeft);
	if (TiltLight > 0.002f)
	{
		FWidgetShapePaint Spot;
		Spot.Type = EWidgetShapePaintType::RadialGradient;
		Spot.GradientCenter = FVector2D(PointerUV.X, PointerUV.Y);
		Spot.GradientRadius = 0.6f;
		Spot.GradientStops = { FWidgetShapeGradientStop(0.f, FLinearColor::White), FWidgetShapeGradientStop(1.f, FLinearColor::White) };
		WidgetShapeGeometry::CutForGradient(Mesh, Spot, MakeBoxFrame(FBox2f(FVector2f::ZeroVector, FaceSize)), 10.f / PixelScale);
	}

	// Colors from where each vertex is on the flat face; positions projected as the Shape Box turns it.
	TArray<float> Light;
	Light.SetNumUninitialized(Mesh.Vertices.Num());
	const FShapeBoxShape Shape = MakeFaceShape(Pose);
	for (int32 Index = 0; Index < Mesh.Vertices.Num(); ++Index)
	{
		FWidgetShapeMeshVertex& Vertex = Mesh.Vertices[Index];
		const FVector2f UV(Vertex.Position.X / FMath::Max(FaceSize.X, 1.f), Vertex.Position.Y / FMath::Max(FaceSize.Y, 1.f));
		const float Across = Pose.bGlareFromRight ? UV.X : 1.f - UV.X;
		const float Spot = TiltLight > 0.f ? FMath::Square(FMath::Max(0.f, 1.f - FVector2f::Distance(UV, PointerUV) / 0.6f)) : 0.f;
		Light[Index] = FMath::Clamp(Pose.Glare * Across + TiltLight * Spot, 0.f, 1.f);
		const FVector2D Projected = Shape.Evaluate(Size, FVector2D(UV.X, UV.Y));
		Vertex.Position = FVector2f(static_cast<float>(Projected.X), static_cast<float>(Projected.Y));
	}

	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint() * FLinearColor(1.f, 1.f, 1.f, Pose.Opacity);
	if (Pose.Shade > 0.002f)
	{
		const FLinearColor Shadow(0.f, 0.f, 0.f, FMath::Clamp(Pose.Shade, 0.f, 1.f));
		PaintMesh(OutDrawElements, ++InOutLayer, Face.Geometry, Mesh, WidgetTint, [&Shadow](const FWidgetShapeMeshVertex&) { return Shadow; });
	}
	if (Pose.Glare > 0.002f || TiltLight > 0.002f)
	{
		const FWidgetShapeMeshVertex* First = Mesh.Vertices.GetData();
		PaintMesh(OutDrawElements, ++InOutLayer, Face.Geometry, Mesh, WidgetTint, [First, &Light](const FWidgetShapeMeshVertex& Vertex)
		{
			return FLinearColor(1.f, 1.f, 1.f, Light[static_cast<int32>(&Vertex - First)]);
		});
	}
}

int32 SFlipCard::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FArrangedChildren Arranged(EVisibility::Visible);
	ArrangeChildren(AllottedGeometry, Arranged);

	const FArrangedWidget* Faces[2] = { nullptr, nullptr };
	for (int32 Index = 0; Index < Arranged.Num(); ++Index)
	{
		const FArrangedWidget& Child = Arranged[Index];
		if (&Child.Widget.Get() == FrontFace.Get())
		{
			Faces[0] = &Child;
		}
		else if (&Child.Widget.Get() == BackFace.Get())
		{
			Faces[1] = &Child;
		}
	}

	int32 Layer = LayerId;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (Faces[Index])
		{
			PaintShadow(*Faces[Index], Poses[Index], OutDrawElements, Layer, InWidgetStyle);
		}
	}

	const int32 Order[2] = { bBackOnTop ? 0 : 1, bBackOnTop ? 1 : 0 };
	for (const int32 Index : Order)
	{
		if (Faces[Index])
		{
			PaintFace(Index, *Faces[Index], Args, MyCullingRect, OutDrawElements, Layer, InWidgetStyle, bParentEnabled);
		}
	}
	return Layer;
}

FReply SFlipCard::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Settings.Trigger == EFlipCardTrigger::Click && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bPressedHere = true;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SFlipCard::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPressedHere || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressedHere = false;
	if (MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
	{
		OnRequestFlip.ExecuteIfBound(!bShowingBack);
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SFlipCard::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Settings.bTiltOnHover)
	{
		const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const FVector2D Size = MyGeometry.GetLocalSize();
		PointerUV = FVector2f(
			FMath::Clamp(static_cast<float>(Local.X / FMath::Max(Size.X, 1.0)), 0.f, 1.f),
			FMath::Clamp(static_cast<float>(Local.Y / FMath::Max(Size.Y, 1.0)), 0.f, 1.f));
		TiltX.SetTarget(-(PointerUV.X - 0.5f) * 2.f * Settings.MaxTilt, EWidgetMotion::Smooth, 0.15f);
		TiltY.SetTarget(-(PointerUV.Y - 0.5f) * 2.f * Settings.MaxTilt, EWidgetMotion::Smooth, 0.15f);
		Wake();
	}
	return FReply::Unhandled();
}

void SFlipCard::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SPanel::OnMouseEnter(MyGeometry, MouseEvent);
	if (Settings.Trigger == EFlipCardTrigger::Hover)
	{
		OnRequestFlip.ExecuteIfBound(true);
	}
	if (Settings.bTiltOnHover)
	{
		HoverAmount.SetTarget(1.f, EWidgetMotion::Smooth, 0.2f);
		Wake();
	}
}

void SFlipCard::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SPanel::OnMouseLeave(MouseEvent);
	bPressedHere = false;
	if (Settings.Trigger == EFlipCardTrigger::Hover)
	{
		OnRequestFlip.ExecuteIfBound(false);
	}
	if (Settings.bTiltOnHover)
	{
		TiltX.SetTarget(0.f, EWidgetMotion::Smooth, 0.35f);
		TiltY.SetTarget(0.f, EWidgetMotion::Smooth, 0.35f);
		HoverAmount.SetTarget(0.f, EWidgetMotion::Smooth, 0.35f);
		Wake();
	}
}

// =====================================================================================================
// UFlipCard
// =====================================================================================================

UFlipCard::UFlipCard(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FFlipCardSettings UFlipCard::MakeSettings() const
{
	FFlipCardSettings Result;
	Result.Motion = FlipMotion;
	Result.EnterDuration = EnterDuration > KINDA_SMALL_NUMBER ? EnterDuration : 0.6f;
	Result.ExitDuration = ExitDuration > KINDA_SMALL_NUMBER ? ExitDuration : 0.6f;
	Result.bReverseOnReturn = bReverseOnReturn;
	Result.Perspective = Perspective;
	Result.Lift = Lift;
	Result.Shading = Shading;
	Result.Glare = Glare;
	Result.CornerRadius = CornerRadius;
	Result.Trigger = Trigger;
	Result.bTiltOnHover = bTiltOnHover;
	Result.MaxTilt = MaxTilt;
	Result.TiltGlare = TiltGlare;
	Result.bCastShadow = bCastShadow;
	Result.ShadowColor = ShadowColor;
	Result.ShadowBlur = ShadowBlur;
	Result.ShadowOffset = ShadowOffset;
	return Result;
}

TSharedRef<SWidget> UFlipCard::RebuildWidget()
{
	MyCard = SNew(SFlipCard)
		.OnRequestFlip(BIND_UOBJECT_DELEGATE(FOnFlipCardRequest, HandleRequestFlip))
		.OnFlipFinished(BIND_UOBJECT_DELEGATE(FOnFlipCardRequest, HandleFlipFinished));
	if (FrontContent)
	{
		MyCard->SetFrontContent(FrontContent->TakeWidget());
	}
	if (BackContent)
	{
		MyCard->SetBackContent(BackContent->TakeWidget());
	}
	return MyCard.ToSharedRef();
}

void UFlipCard::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyCard.IsValid())
	{
		MyCard->SetSettings(MakeSettings());
		if (MyCard->IsShowingBack() != bShowBack)
		{
			MyCard->SetShowBack(bShowBack, /*bAnimate=*/false);
		}
	}
}

void UFlipCard::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyCard.Reset();
	if (bReleaseChildren)
	{
		if (FrontContent)
		{
			FrontContent->ReleaseSlateResources(true);
		}
		if (BackContent)
		{
			BackContent->ReleaseSlateResources(true);
		}
	}
}

void UFlipCard::SetShowBack(bool bBack, bool bAnimate)
{
	const bool bChanged = bShowBack != bBack;
	bShowBack = bBack;
	if (MyCard.IsValid() && MyCard->IsShowingBack() != bBack)
	{
		MyCard->SetShowBack(bBack, bAnimate && !IsDesignTime());
	}
	if (bChanged)
	{
		OnFlipStarted.Broadcast(bBack);
	}
}

void UFlipCard::Flip(bool bAnimate)
{
	SetShowBack(!bShowBack, bAnimate);
}

void UFlipCard::FlipToBack(bool bAnimate)
{
	SetShowBack(true, bAnimate);
}

void UFlipCard::FlipToFront(bool bAnimate)
{
	SetShowBack(false, bAnimate);
}

float UFlipCard::GetFlipProgress() const
{
	return MyCard.IsValid() ? MyCard->GetProgress() : (bShowBack ? 1.f : 0.f);
}

void UFlipCard::HandleRequestFlip(bool bToBack)
{
	SetShowBack(bToBack, true);
}

void UFlipCard::HandleFlipFinished(bool bBack)
{
	OnFlipFinished.Broadcast(bBack);
}

void UFlipCard::GetSlotNames(TArray<FName>& SlotNames) const
{
	SlotNames.Add(FlipCardPrivate::FrontSlot);
	SlotNames.Add(FlipCardPrivate::BackSlot);
}

UWidget* UFlipCard::GetContentForSlot(FName SlotName) const
{
	if (SlotName == FlipCardPrivate::FrontSlot)
	{
		return FrontContent;
	}
	if (SlotName == FlipCardPrivate::BackSlot)
	{
		return BackContent;
	}
	return nullptr;
}

void UFlipCard::SetContentForSlot(FName SlotName, UWidget* Content)
{
	if (SlotName == FlipCardPrivate::FrontSlot)
	{
		FrontContent = Content;
		if (MyCard.IsValid())
		{
			MyCard->SetFrontContent(Content ? Content->TakeWidget() : SNullWidget::NullWidget);
		}
	}
	else if (SlotName == FlipCardPrivate::BackSlot)
	{
		BackContent = Content;
		if (MyCard.IsValid())
		{
			MyCard->SetBackContent(Content ? Content->TakeWidget() : SNullWidget::NullWidget);
		}
	}
}

#if WITH_EDITOR
const FText UFlipCard::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
