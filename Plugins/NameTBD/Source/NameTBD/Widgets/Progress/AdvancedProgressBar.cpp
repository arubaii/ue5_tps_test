// Copyright AR GameSystems 2026. All rights reserved.

#include "AdvancedProgressBar.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "NameTBD/Widgets/WidgetEffectsDraw.h"
#include "NameTBD/Widgets/WidgetTreeLookup.h"
#include "NameTBD/Widgets/WidgetEditorHooks.h"
#include "Algo/Reverse.h"
#include "NameTBD/Widgets/Paths/WidgetPathTypes.h"

#define LOCTEXT_NAMESPACE "AdvancedProgressBar"

namespace AdvancedProgressBarPrivate
{
	using namespace WidgetEffectsDraw;

	FLinearColor Rgb(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B, 255));
	}

	bool IsVertical(EProgressFillDirection Direction)
	{
		return Direction == EProgressFillDirection::BottomToTop || Direction == EProgressFillDirection::TopToBottom;
	}

	/** Box from F0 to F1 (fractions, may run past either end) along Direction, whole the other way. From Center: left to right. */
	FBox2f SubRect(const FBox2f& Box, EProgressFillDirection Direction, float F0, float F1)
	{
		const FVector2f Size = Box.GetSize();
		switch (Direction)
		{
		case EProgressFillDirection::RightToLeft:
			return FBox2f(FVector2f(Box.Max.X - F1 * Size.X, Box.Min.Y), FVector2f(Box.Max.X - F0 * Size.X, Box.Max.Y));
		case EProgressFillDirection::TopToBottom:
			return FBox2f(FVector2f(Box.Min.X, Box.Min.Y + F0 * Size.Y), FVector2f(Box.Max.X, Box.Min.Y + F1 * Size.Y));
		case EProgressFillDirection::BottomToTop:
			return FBox2f(FVector2f(Box.Min.X, Box.Max.Y - F1 * Size.Y), FVector2f(Box.Max.X, Box.Max.Y - F0 * Size.Y));
		default:
			return FBox2f(FVector2f(Box.Min.X + F0 * Size.X, Box.Min.Y), FVector2f(Box.Min.X + F1 * Size.X, Box.Max.Y));
		}
	}

	/** How far along Direction a point is across Box: 0 at its start, 1 at its end. */
	float AlongOf(const FBox2f& Box, EProgressFillDirection Direction, const FVector2f& Point)
	{
		const FVector2f Size(FMath::Max(Box.GetSize().X, 0.01f), FMath::Max(Box.GetSize().Y, 0.01f));
		switch (Direction)
		{
		case EProgressFillDirection::RightToLeft: return (Box.Max.X - Point.X) / Size.X;
		case EProgressFillDirection::TopToBottom: return (Point.Y - Box.Min.Y) / Size.Y;
		case EProgressFillDirection::BottomToTop: return (Box.Max.Y - Point.Y) / Size.Y;
		default: return (Point.X - Box.Min.X) / Size.X;
		}
	}

	TArray<FVector2f> RoundedPolygon(const FBox2f& Rect, float Radius, float Tolerance)
	{
		FWidgetShapeOutline Outline;
		AppendRoundedRect(Outline, Rect.Min, Rect.GetSize(), FVector4f(Radius, Radius, Radius, Radius), Tolerance);
		return Outline.Lines.Num() > 0 ? Outline.Lines[0].Points : TArray<FVector2f>();
	}

	TArray<FVector2f> RectPolygon(const FBox2f& Rect)
	{
		return { Rect.Min, FVector2f(Rect.Max.X, Rect.Min.Y), Rect.Max, FVector2f(Rect.Min.X, Rect.Max.Y) };
	}

	void AppendMesh(FWidgetShapeMesh& Out, const FWidgetShapeMesh& Piece)
	{
		const uint32 Base = static_cast<uint32>(Out.Vertices.Num());
		Out.Vertices.Append(Piece.Vertices);
		for (const uint32 Index : Piece.Indices)
		{
			Out.Indices.Add(Base + Index);
		}
	}

	void AppendPolygonFill(const TArray<FVector2f>& Polygon, float AntiAlias, FWidgetShapeMesh& Out)
	{
		if (Polygon.Num() < 3)
		{
			return;
		}
		FWidgetShapeOutline Outline;
		Outline.Lines.AddDefaulted_GetRef().Points = Polygon;
		FWidgetShapeMesh Piece;
		TArray<int8> FillOnLeft;
		WidgetShapeGeometry::BuildFill(Outline, AntiAlias, Piece, FillOnLeft);
		AppendMesh(Out, Piece);
	}

	/** Points round a circle from one angle to another (degrees, 0 the top, clockwise), as close as Tolerance. */
	void AppendArc(TArray<FVector2f>& Out, const FVector2f& Center, float Radius, float FromDegrees, float ToDegrees, float Tolerance)
	{
		const float SpanRadians = FMath::DegreesToRadians(FMath::Abs(ToDegrees - FromDegrees));
		const float StepRadians = 2.f * FMath::Acos(FMath::Clamp(1.f - Tolerance / FMath::Max(Radius, 0.01f), -1.f, 1.f));
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(SpanRadians / FMath::Max(StepRadians, 0.005f)), 2, 720);
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Degrees = FMath::Lerp(FromDegrees, ToDegrees, static_cast<float>(Step) / static_cast<float>(Steps));
			float Sin = 0.f;
			float Cos = 1.f;
			FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(Degrees));
			Out.Add(Center + FVector2f(Sin, -Cos) * Radius);
		}
	}

	FLinearColor SampleStops(const TArray<FWidgetShapeGradientStop>& Stops, float Position)
	{
		FWidgetShapePaint Paint;
		Paint.GradientStops = Stops;
		return Paint.SampleGradient(Position);
	}

	float EaseInOut(float Alpha)
	{
		const float T = FMath::Clamp(Alpha, 0.f, 1.f);
		return T < 0.5f ? 4.f * T * T * T : 1.f - FMath::Pow(-2.f * T + 2.f, 3.f) * 0.5f;
	}

	/** The shine's band, along the fill (0 its start, 1 its leading edge): its middle, or false between sweeps. */
	bool GetShineCenter(double Clock, float Interval, float Width, float& OutCenter)
	{
		constexpr double SweepSeconds = 0.9;
		const double Cycle = FMath::Max(static_cast<double>(Interval), SweepSeconds + 0.1);
		const double Phase = FMath::Fmod(Clock, Cycle);
		if (Phase >= SweepSeconds)
		{
			return false;
		}
		OutCenter = FMath::Lerp(-Width, 1.f + Width, static_cast<float>(Phase / SweepSeconds));
		return true;
	}

	const FSlateBrush* WhiteBox()
	{
		return FCoreStyle::Get().GetBrush("GenericWhiteBox");
	}
}

// =====================================================================================================
// FAdvancedProgressBarStyle
// =====================================================================================================

FAdvancedProgressBarStyle::FAdvancedProgressBarStyle()
{
	using namespace AdvancedProgressBarPrivate;

	Fill.Type = EWidgetShapePaintType::Color;
	Fill.Color = Rgb(61, 139, 255);
	Track.Type = EWidgetShapePaintType::Color;
	Track.Color = FLinearColor(1.f, 1.f, 1.f, 0.1f);
	ValueColors = { FWidgetShapeGradientStop(0.f, Rgb(235, 64, 52)), FWidgetShapeGradientStop(0.5f, Rgb(245, 190, 40)), FWidgetShapeGradientStop(1.f, Rgb(70, 200, 110)) };
}

// =====================================================================================================
// SAdvancedProgressBar
// =====================================================================================================

void SAdvancedProgressBar::Construct(const FArguments& InArgs)
{
	OnReachedEnd = InArgs._OnReachedEnd;
	SetCanTick(false);
	FillValue.Snap(0.f);
	GhostValue.Snap(0.f);
}

void SAdvancedProgressBar::SetBarStyle(const FAdvancedProgressBarStyle& InStyle)
{
	BarStyle = InStyle;
	bTrackBuilt = false;
	Invalidate(EInvalidateWidgetReason::Layout);
	if (NeedsClock())
	{
		Wake();
	}
}

void SAdvancedProgressBar::SetMotion(EWidgetMotion InMotion, float InEnterDuration, float InExitDuration)
{
	Motion = InMotion;
	EnterDuration = InEnterDuration;
	ExitDuration = InExitDuration;
}

void SAdvancedProgressBar::SetPercentSource(TAttribute<float> InSource)
{
	PercentSource = MoveTemp(InSource);
	if (PercentSource.IsBound())
	{
		Wake();
	}
}

void SAdvancedProgressBar::SetCurve(const UCurveShape* InCurve)
{
	if (Curve.Get() != InCurve)
	{
		Curve = InCurve;
		CurveHash = 0;
		bTrackBuilt = false;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FWidgetEffectMaterialInputs SAdvancedProgressBar::MakeMaterialInputs(const FVector2f& Size) const
{
	FWidgetEffectMaterialInputs Inputs;
	Inputs.Add(TEXT("Progress"), FMath::Clamp(FillValue.Value, 0.f, 1.f));
	Inputs.Add(TEXT("Trail"), FMath::Clamp(GhostValue.Value, 0.f, 1.f));
	Inputs.Add(TEXT("ShapeSize"), FLinearColor(Size.X, Size.Y, 0.f, 0.f));
	return Inputs;
}

void SAdvancedProgressBar::SetIndeterminate(bool bInIndeterminate)
{
	bIndeterminate = bInIndeterminate;
	Invalidate(EInvalidateWidgetReason::Paint);
	if (bIndeterminate)
	{
		Wake();
	}
}

bool SAdvancedProgressBar::NeedsClock() const
{
	return bIndeterminate || BarStyle.bShine || (BarStyle.bStripes && BarStyle.Shape == EProgressBarShape::Bar);
}

void SAdvancedProgressBar::SetPercent(float InPercent, bool bAnimate)
{
	const float NewTarget = FMath::Clamp(InPercent, 0.f, 1.f);
	if (bAnimate && FMath::IsNearlyEqual(NewTarget, Target))
	{
		return;
	}
	Target = NewTarget;

	if (!bAnimate || Motion == EWidgetMotion::Instant)
	{
		const float Before = FillValue.Value;
		FillValue.Snap(Target);
		GhostValue.Snap(Target);
		Hold = 0.f;
		Invalidate(EInvalidateWidgetReason::Paint);
		if (bAnimate && !FMath::IsNearlyEqual(Before, Target) && (Target >= 1.f - KINDA_SMALL_NUMBER || Target <= KINDA_SMALL_NUMBER))
		{
			OnReachedEnd.ExecuteIfBound(Target >= 0.5f);
		}
		return;
	}

	const float Shown = FillValue.Value;
	if (Target < Shown)
	{
		// A loss: the fill drops now; the trail holds what was there, then drains.
		bGain = false;
		FillValue.SetTarget(Target, Motion, ExitDuration);
		if (BarStyle.bTrail)
		{
			GhostValue.Snap(FMath::Max(GhostValue.Value, Shown));
			Hold = BarStyle.TrailDelay;
		}
	}
	else
	{
		// A gain: the trail shows it at once; the fill catches up after the delay.
		bGain = true;
		if (BarStyle.bTrail)
		{
			GhostValue.Snap(Target);
			FillValue.Snap(Shown);
			Hold = BarStyle.TrailDelay;
		}
		else
		{
			FillValue.SetTarget(Target, Motion, EnterDuration);
		}
	}
	if (BarStyle.bTrail && Hold <= 0.f)
	{
		if (bGain)
		{
			FillValue.SetTarget(Target, Motion, EnterDuration);
		}
		else
		{
			GhostValue.SetTarget(Target, EWidgetMotion::Smooth, BarStyle.TrailDuration);
		}
	}
	Wake();
}

void SAdvancedProgressBar::Wake()
{
	if (!AnimateTimer.IsValid())
	{
		AnimateTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SAdvancedProgressBar::Animate));
	}
}

EActiveTimerReturnType SAdvancedProgressBar::Animate(double InCurrentTime, float InDeltaTime)
{
	Clock += InDeltaTime;
	if (PercentSource.IsBound())
	{
		const float Bound = PercentSource.Get();
		if (!FMath::IsNearlyEqual(FMath::Clamp(Bound, 0.f, 1.f), Target))
		{
			SetPercent(Bound, true);
		}
	}

	if (Hold > 0.f)
	{
		Hold -= InDeltaTime;
		if (Hold <= 0.f)
		{
			if (bGain)
			{
				FillValue.SetTarget(Target, Motion, EnterDuration);
			}
			else
			{
				GhostValue.SetTarget(Target, EWidgetMotion::Smooth, BarStyle.TrailDuration);
			}
		}
	}

	const bool bWasFilling = FillValue.IsMoving();
	const bool bFilling = FillValue.Advance(InDeltaTime);
	const bool bTrailing = GhostValue.Advance(InDeltaTime);
	if (!BarStyle.bTrail)
	{
		GhostValue.Snap(FillValue.Value);
	}
	if (bWasFilling && !bFilling && (Target >= 1.f - KINDA_SMALL_NUMBER || Target <= KINDA_SMALL_NUMBER))
	{
		OnReachedEnd.ExecuteIfBound(Target >= 0.5f);
	}
	Invalidate(EInvalidateWidgetReason::Paint);

	if (bFilling || bTrailing || Hold > 0.f || NeedsClock() || PercentSource.IsBound())
	{
		return EActiveTimerReturnType::Continue;
	}
	AnimateTimer.Reset();
	return EActiveTimerReturnType::Stop;
}

FVector2D SAdvancedProgressBar::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	if (BarStyle.Shape == EProgressBarShape::Ring)
	{
		return FVector2D(BarStyle.RingDiameter, BarStyle.RingDiameter);
	}
	if (BarStyle.Shape == EProgressBarShape::Curve)
	{
		return BarStyle.CurveSize;
	}
	return AdvancedProgressBarPrivate::IsVertical(BarStyle.Direction) ? FVector2D(BarStyle.BarSize.Y, BarStyle.BarSize.X) : BarStyle.BarSize;
}

FWidgetShapePaint SAdvancedProgressBar::MakeFillPaint(float Shown) const
{
	FWidgetShapePaint Paint = BarStyle.Fill;
	if (BarStyle.bColorByValue && BarStyle.ValueColors.Num() > 0)
	{
		Paint.Color = AdvancedProgressBarPrivate::SampleStops(BarStyle.ValueColors, Shown);
	}
	return Paint;
}

void SAdvancedProgressBar::GetIndeterminateSpan(float& OutStart, float& OutEnd) const
{
	using namespace AdvancedProgressBarPrivate;

	// A segment whose head runs ahead of its tail: it grows as it sets off and shrinks as it arrives, sliding in past the
	// start and out past the end.
	constexpr double Period = 1.8;
	const float Phase = static_cast<float>(FMath::Fmod(Clock, Period) / Period);
	const float Head = EaseInOut(Phase * 1.35f);
	const float Tail = EaseInOut(Phase * 1.35f - 0.35f);
	OutStart = Tail * 1.4f - 0.2f;
	OutEnd = Head * 1.4f - 0.2f;
}

int32 SAdvancedProgressBar::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
	{
		return LayerId;
	}
	switch (BarStyle.Shape)
	{
	case EProgressBarShape::Ring:
		return PaintRing(AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle);
	case EProgressBarShape::Curve:
		return PaintCurve(AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle);
	default:
		return PaintBar(AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle);
	}
}

int32 SAdvancedProgressBar::PaintBar(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	using namespace AdvancedProgressBarPrivate;

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2f Size(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	const float PixelScale = QuantizeScale(GetPixelScale(AllottedGeometry));
	const float Tolerance = WidgetShapeGeometry::GetTolerance(PixelScale);
	const float AntiAlias = 1.f / PixelScale;
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();

	const EProgressFillDirection Direction = BarStyle.Direction;
	const bool bFromCenter = Direction == EProgressFillDirection::FromCenter;
	const bool bVertical = IsVertical(Direction);
	const FBox2f Whole(FVector2f::ZeroVector, Size);
	const float Length = bVertical ? Size.Y : Size.X;
	const float Thickness = bVertical ? Size.X : Size.Y;
	const int32 Count = FMath::Clamp(BarStyle.Segments, 1, 100);
	const float Gap = Count > 1 ? FMath::Max(BarStyle.SegmentGap, 0.f) : 0.f;
	const float SegmentLength = FMath::Max((Length - Gap * static_cast<float>(Count - 1)) / static_cast<float>(Count), 0.5f);
	const float TrackRadius = FMath::Min(BarStyle.bFullyRounded ? Thickness * 0.5f : BarStyle.CornerRadius, FMath::Min(Thickness, SegmentLength) * 0.5f);
	const float Inset = FMath::Clamp(BarStyle.FillInset, 0.f, Thickness * 0.45f);
	const float FillRadius = FMath::Max(TrackRadius - Inset, 0.f);

	auto SegmentSpan = [SegmentLength, Gap, Length](int32 Index, float& OutA, float& OutB)
	{
		OutA = static_cast<float>(Index) * (SegmentLength + Gap) / Length;
		OutB = OutA + SegmentLength / Length;
	};

	// The track: made again only when its size or look changes.
	uint32 Hash = GetTypeHash(Size);
	Hash = HashCombineFast(Hash, GetTypeHash(PixelScale));
	Hash = HashCombineFast(Hash, GetTypeHash(Count));
	Hash = HashCombineFast(Hash, GetTypeHash(Gap));
	Hash = HashCombineFast(Hash, GetTypeHash(TrackRadius));
	Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Direction)));
	Hash = HashCombineFast(Hash, GetTypeHash(BarStyle.TrackOutline));
	Hash = HashCombineFast(Hash, BarStyle.Track.GetLayoutHash());
	if (!bTrackBuilt || Hash != TrackHash)
	{
		TrackMesh.Reset();
		TrackOutlineMesh.Reset();
		FWidgetShapeOutline Outline;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			float A = 0.f;
			float B = 1.f;
			SegmentSpan(Index, A, B);
			const FBox2f Segment = SubRect(Whole, Direction, A, B);
			AppendRoundedRect(Outline, Segment.Min, Segment.GetSize(), FVector4f(TrackRadius, TrackRadius, TrackRadius, TrackRadius), Tolerance);
		}
		TArray<int8> FillOnLeft;
		WidgetShapeGeometry::BuildFill(Outline, AntiAlias, TrackMesh, FillOnLeft);
		WidgetShapeGeometry::CutForGradient(TrackMesh, BarStyle.Track, MakeBoxFrame(Whole), 6.f / PixelScale);
		if (BarStyle.TrackOutline > 0.f)
		{
			FWidgetShapeStroke Edge;
			Edge.bEnabled = true;
			Edge.Width = BarStyle.TrackOutline;
			Edge.Align = EWidgetShapeStrokeAlign::Inside;
			Edge.Join = EWidgetShapeLineJoin::Round;
			WidgetShapeGeometry::BuildStroke(Outline, FillOnLeft, Edge, FWidgetShapeTrim(), AntiAlias, Tolerance, TrackOutlineMesh);
		}
		TrackHash = Hash;
		bTrackBuilt = true;
	}
	const FWidgetEffectMaterialInputs MaterialInputs = MakeMaterialInputs(Size);
	PaintMeshWithPaint(OutDrawElements, LayerId, AllottedGeometry, TrackMesh, BarStyle.Track, Whole, TrackSVG, *this, InWidgetStyle, FLinearColor::White, &Materials, 0, &MaterialInputs);
	if (!TrackOutlineMesh.IsEmpty())
	{
		const FLinearColor OutlineColor = BarStyle.TrackOutlineColor;
		PaintMesh(OutDrawElements, LayerId + 1, AllottedGeometry, TrackOutlineMesh, WidgetTint, [&OutlineColor](const FWidgetShapeMeshVertex&) { return OutlineColor; });
	}

	// What is shown: the fill, and the trail beyond it.
	const float Shown = FMath::Clamp(FillValue.Value, 0.f, 1.f);
	const float Trail = FMath::Clamp(GhostValue.Value, 0.f, 1.f);
	auto Snapped = [this, Count](float Value)
	{
		return (BarStyle.bWholeSegments && Count > 1) ? FMath::FloorToFloat(Value * static_cast<float>(Count) + 1e-3f) / static_cast<float>(Count) : Value;
	};
	const float ShownLevel = Snapped(Shown);
	const float TrailLevel = Snapped(Trail);

	float FillStart = 0.f;
	float FillEnd = ShownLevel;
	float TrailStart = 0.f;
	float TrailEnd = TrailLevel;
	if (bIndeterminate)
	{
		GetIndeterminateSpan(FillStart, FillEnd);
		TrailEnd = TrailStart;
	}
	else if (bFromCenter)
	{
		FillStart = 0.5f - ShownLevel * 0.5f;
		FillEnd = 0.5f + ShownLevel * 0.5f;
		TrailStart = 0.5f - TrailLevel * 0.5f;
		TrailEnd = 0.5f + TrailLevel * 0.5f;
	}

	// One piece per segment it covers, shaped by Fill End.
	auto BuildPieces = [&](float SpanStart, float SpanEnd, FWidgetShapeMesh& OutMesh, TArray<TArray<FVector2f>>* OutPolygons)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			float A = 0.f;
			float B = 1.f;
			SegmentSpan(Index, A, B);
			const float From = FMath::Max(SpanStart, A);
			const float To = FMath::Min(SpanEnd, B);
			if (To - From <= 1e-5f)
			{
				continue;
			}
			const FBox2f Segment = SubRect(Whole, Direction, A, B);
			const FBox2f Inner(Segment.Min + FVector2f(Inset, Inset), Segment.Max - FVector2f(Inset, Inset));
			const float InnerLength = (bVertical ? Inner.GetSize().Y : Inner.GetSize().X);
			const float InnerThickness = (bVertical ? Inner.GetSize().X : Inner.GetSize().Y);
			if (InnerLength <= 0.f || InnerThickness <= 0.f)
			{
				continue;
			}
			const float F0 = (From - A) / (B - A);
			const float F1 = (To - A) / (B - A);
			const float Radius = FMath::Min(FillRadius, FMath::Min(InnerLength, InnerThickness) * 0.5f);
			const TArray<FVector2f> SegmentPolygon = RoundedPolygon(Inner, Radius, Tolerance);
			const float PieceLength = (F1 - F0) * InnerLength;

			TArray<FVector2f> Piece;
			if (F0 <= 1e-4f && F1 >= 1.f - 1e-4f)
			{
				Piece = SegmentPolygon;
			}
			else if (BarStyle.FillEnd == EProgressFillEnd::Flat)
			{
				ClipPolygon(RectPolygon(SubRect(Inner, bFromCenter ? EProgressFillDirection::LeftToRight : Direction, F0, F1)), SegmentPolygon, Piece);
			}
			else if (PieceLength >= 2.f * Radius - 0.01f)
			{
				Piece = RoundedPolygon(SubRect(Inner, bFromCenter ? EProgressFillDirection::LeftToRight : Direction, F0, F1), Radius, Tolerance);
			}
			else if (F0 <= 1e-4f)
			{
				// Shorter than its rounding: a full round end, grown out of the track's own.
				const float LeadStart = F1 - 2.f * Radius / FMath::Max(InnerLength, 0.01f);
				ClipPolygon(RoundedPolygon(SubRect(Inner, Direction, LeadStart, F1), Radius, Tolerance), SegmentPolygon, Piece);
			}
			else
			{
				Piece = RoundedPolygon(SubRect(Inner, bFromCenter ? EProgressFillDirection::LeftToRight : Direction, F0, F1), PieceLength * 0.5f, Tolerance);
			}
			AppendPolygonFill(Piece, AntiAlias, OutMesh);
			if (OutPolygons && Piece.Num() >= 3)
			{
				OutPolygons->Add(MoveTemp(Piece));
			}
		}
	};

	// The fill's own colors: along the track, along the fill, or as a shape's paint over the box.
	const FWidgetShapePaint Paint = MakeFillPaint(Shown);
	const bool bSpanGradient = Paint.IsGradient() && BarStyle.GradientSpan != EProgressGradientSpan::Box;
	const float FillReach = FMath::Max(bFromCenter ? ShownLevel : FillEnd - FillStart, 0.001f);
	auto ColorPosition = [&](const FVector2f& Point)
	{
		float Along = AlongOf(Whole, Direction, Point);
		if (bFromCenter)
		{
			Along = FMath::Abs(Along - 0.5f) * 2.f;
		}
		if (BarStyle.GradientSpan == EProgressGradientSpan::Fill)
		{
			Along = (Along - (bFromCenter ? 0.f : FillStart)) / FillReach;
		}
		return Along;
	};
	const FLinearColor LeadColor = bSpanGradient ? Paint.SampleGradient(ColorPosition(SubRect(Whole, Direction, FillEnd, FillEnd).GetCenter())) * Paint.Color : Paint.Color;

	// Glow round the fill, under it.
	if (BarStyle.Glow > 0.f && FillEnd - FillStart > 1e-4f)
	{
		const FBox2f InnerWhole(Whole.Min + FVector2f(Inset, Inset), Whole.Max - FVector2f(Inset, Inset));
		const FBox2f Lit = SubRect(InnerWhole, bFromCenter ? EProgressFillDirection::LeftToRight : Direction, FMath::Max(FillStart, 0.f), FMath::Min(FillEnd, 1.f));
		FSoftBox Box;
		Box.Center = Lit.GetCenter();
		Box.HalfSize = Lit.GetExtent();
		const float Radius = FMath::Min(FillRadius, FMath::Min(Box.HalfSize.X, Box.HalfSize.Y));
		Box.Radii = FVector4f(Radius, Radius, Radius, Radius);
		Box.Sigma = FMath::Max(BarStyle.GlowSize, 1.f) * 0.5f;
		if (Box.HalfSize.X > 0.f && Box.HalfSize.Y > 0.f)
		{
			TArray<float> Distances;
			MakeRingDistances(-FMath::Min(Box.HalfSize.X, Box.HalfSize.Y) * 0.999f, 3.2f * Box.Sigma, FMath::Max(Box.Sigma * 0.75f, 2.f * AntiAlias), 0.f, 2.2f * Box.Sigma, FMath::Max(Box.Sigma * 0.35f, AntiAlias), 32, Distances);
			FWidgetShapeMesh GlowMesh;
			BuildRings(Box.Center, Box.HalfSize, Box.Radii, Distances, FMath::Max(Box.Sigma, 4.f * AntiAlias), PixelScale, true, GlowMesh);
			for (FWidgetShapeMeshVertex& Vertex : GlowMesh.Vertices)
			{
				Vertex.Coverage = EvaluateSoftBox(Box, Vertex.Position, AntiAlias);
			}
			FLinearColor GlowColor = LeadColor;
			GlowColor.A *= BarStyle.Glow;
			PaintMesh(OutDrawElements, LayerId + 2, AllottedGeometry, GlowMesh, WidgetTint, [&GlowColor](const FWidgetShapeMeshVertex&) { return GlowColor; });
		}
	}

	// The trail: what was just lost (or gained), under the fill.
	if (!bIndeterminate && BarStyle.bTrail && TrailLevel > ShownLevel + 1e-4f)
	{
		FWidgetShapeMesh TrailMesh;
		BuildPieces(TrailStart, TrailEnd, TrailMesh, nullptr);
		const FLinearColor TrailColor = bGain ? BarStyle.GainColor : BarStyle.LossColor;
		PaintMesh(OutDrawElements, LayerId + 3, AllottedGeometry, TrailMesh, WidgetTint, [&TrailColor](const FWidgetShapeMeshVertex&) { return TrailColor; });
	}

	// The fill.
	FWidgetShapeMesh FillMesh;
	TArray<TArray<FVector2f>> Pieces;
	BuildPieces(FillStart, FillEnd, FillMesh, &Pieces);
	if (bFromCenter)
	{
		WidgetShapeGeometry::CutAtLevel(FillMesh, 0.5f, [&Whole, Direction](const FVector2f& Point) { return AlongOf(Whole, Direction, Point); });
	}
	if (bSpanGradient)
	{
		TArray<float> Stops;
		Paint.GetStopPositions(Stops);
		for (const float Stop : Stops)
		{
			WidgetShapeGeometry::CutAtLevel(FillMesh, Stop, ColorPosition);
		}
		PaintMesh(OutDrawElements, LayerId + 4, AllottedGeometry, FillMesh, WidgetTint,
			[&Paint, &ColorPosition](const FWidgetShapeMeshVertex& Vertex) { return Paint.SampleGradient(ColorPosition(Vertex.Position)) * Paint.Color; });
	}
	else
	{
		WidgetShapeGeometry::CutForGradient(FillMesh, Paint, MakeBoxFrame(Whole), 6.f / PixelScale);
		PaintMeshWithPaint(OutDrawElements, LayerId + 4, AllottedGeometry, FillMesh, Paint, Whole, FillSVG, *this, InWidgetStyle, FLinearColor::White, &Materials, 1, &MaterialInputs);
	}

	// Stripes moving along the fill.
	if (BarStyle.bStripes && Pieces.Num() > 0)
	{
		const float StripeWidth = FMath::Max(BarStyle.StripeWidth, 2.f);
		const float Period = StripeWidth * 2.f;
		const float Offset = static_cast<float>(FMath::Fmod(Clock * BarStyle.StripeSpeed, static_cast<double>(Period)));
		FWidgetShapeMesh StripeMesh;
		TArray<FVector2f> Clipped;
		for (float Start = -Thickness - Period + Offset; Start < Length + Period; Start += Period)
		{
			// Leaning 45 degrees: along the bar by its thickness from one side to the other.
			TArray<FVector2f> Stripe = bVertical
				? TArray<FVector2f>{ FVector2f(0.f, Start), FVector2f(0.f, Start + StripeWidth), FVector2f(Size.X, Start + StripeWidth + Thickness), FVector2f(Size.X, Start + Thickness) }
				: TArray<FVector2f>{ FVector2f(Start, Size.Y), FVector2f(Start + StripeWidth, Size.Y), FVector2f(Start + StripeWidth + Thickness, 0.f), FVector2f(Start + Thickness, 0.f) };
			for (const TArray<FVector2f>& Piece : Pieces)
			{
				ClipPolygon(Stripe, Piece, Clipped);
				AppendPolygonFill(Clipped, AntiAlias, StripeMesh);
			}
		}
		const FLinearColor StripeColor(1.f, 1.f, 1.f, BarStyle.StripeOpacity);
		PaintMesh(OutDrawElements, LayerId + 5, AllottedGeometry, StripeMesh, WidgetTint, [&StripeColor](const FWidgetShapeMeshVertex&) { return StripeColor; });
	}

	// A band of light sweeping along the fill.
	float ShineCenter = 0.f;
	if (BarStyle.bShine && !bIndeterminate && !FillMesh.IsEmpty() && GetShineCenter(Clock, BarStyle.ShineInterval, BarStyle.ShineWidth, ShineCenter))
	{
		const float Width = FMath::Max(BarStyle.ShineWidth, 0.02f);
		auto AlongFill = [&](const FVector2f& Point)
		{
			float Along = AlongOf(Whole, Direction, Point);
			Along = bFromCenter ? FMath::Abs(Along - 0.5f) * 2.f : Along - FillStart;
			return Along / FillReach;
		};
		FWidgetShapeMesh Band = FillMesh;
		WidgetShapeGeometry::CutAtLevel(Band, ShineCenter - Width, AlongFill);
		WidgetShapeGeometry::CutAtLevel(Band, ShineCenter, AlongFill);
		WidgetShapeGeometry::CutAtLevel(Band, ShineCenter + Width, AlongFill);
		const float Opacity = BarStyle.ShineOpacity;
		PaintMesh(OutDrawElements, LayerId + 6, AllottedGeometry, Band, WidgetTint, [&AlongFill, ShineCenter, Width, Opacity](const FWidgetShapeMeshVertex& Vertex)
		{
			return FLinearColor(1.f, 1.f, 1.f, Opacity * FMath::Max(0.f, 1.f - FMath::Abs(AlongFill(Vertex.Position) - ShineCenter) / Width));
		});
	}

	// Markers across the track.
	for (const float Marker : BarStyle.Markers)
	{
		const float Half = FMath::Max(BarStyle.MarkerWidth, 0.5f) * 0.5f / FMath::Max(Length, 1.f);
		const FBox2f Tick = SubRect(Whole, Direction, Marker - Half, Marker + Half);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 7, AllottedGeometry.ToPaintGeometry(FVector2D(Tick.GetSize()), FSlateLayoutTransform(FVector2D(Tick.Min))),
			WhiteBox(), ESlateDrawEffect::None, BarStyle.MarkerColor * WidgetTint);
	}
	return LayerId + 7;
}

int32 SAdvancedProgressBar::PaintRing(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	using namespace AdvancedProgressBarPrivate;

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2f Size(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	const float PixelScale = QuantizeScale(GetPixelScale(AllottedGeometry));
	const float Tolerance = WidgetShapeGeometry::GetTolerance(PixelScale);
	const float AntiAlias = 1.f / PixelScale;
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FBox2f Whole(FVector2f::ZeroVector, Size);

	const float Diameter = FMath::Min(Size.X, Size.Y);
	const float Thickness = FMath::Clamp(BarStyle.RingThickness, 1.f, Diameter * 0.5f);
	const float Radius = FMath::Max((Diameter - Thickness) * 0.5f, 0.5f);
	const FVector2f Center = Size * 0.5f;
	const float SweepDegrees = FMath::Clamp(BarStyle.Sweep, 10.f, 360.f);
	const bool bFull = SweepDegrees >= 359.9f;
	const float Turn = BarStyle.bClockwise ? 1.f : -1.f;
	const float StartAngle = BarStyle.StartAngle;
	const float ArcLength = Radius * FMath::DegreesToRadians(SweepDegrees);
	const bool bRound = BarStyle.RingCap == EProgressRingCap::Round;
	const float Inset = FMath::Clamp(BarStyle.FillInset, 0.f, Thickness * 0.45f);

	const int32 Count = FMath::Clamp(BarStyle.Segments, 1, 100);
	const float GapDegrees = Count > 1 ? FMath::RadiansToDegrees((FMath::Max(BarStyle.SegmentGap, 0.f) + (bRound ? Thickness : 0.f)) / Radius) : 0.f;
	const float GapSpan = GapDegrees / SweepDegrees;
	const int32 Gaps = Count > 1 ? (bFull ? Count : Count - 1) : 0;
	const float SegmentSpanLength = FMath::Max((1.f - GapSpan * static_cast<float>(Gaps)) / static_cast<float>(Count), 0.001f);
	const float FirstOffset = (bFull && Count > 1) ? GapSpan * 0.5f : 0.f;

	auto AngleAt = [StartAngle, Turn, SweepDegrees](float Along) { return StartAngle + Turn * SweepDegrees * Along; };

	FWidgetShapeStroke Stroke;
	Stroke.bEnabled = true;
	Stroke.Align = EWidgetShapeStrokeAlign::Center;
	Stroke.Join = EWidgetShapeLineJoin::Round;
	Stroke.Cap = bRound ? EWidgetShapeLineCap::Round : EWidgetShapeLineCap::Butt;

	// Arcs over a span (0..1 along the track), split by the segments; every vertex's Along.X becomes where it is along the track.
	auto BuildArcs = [&](float SpanStart, float SpanEnd, float Width, bool bFreeSpan, FWidgetShapeMesh& Out, TArray<TArray<FVector2f>>* OutLines)
	{
		Stroke.Width = Width;
		auto AddArc = [&](float From, float To, bool bClosed)
		{
			FWidgetShapeOutline Outline;
			FWidgetShapeLine& Line = Outline.Lines.AddDefaulted_GetRef();
			AppendArc(Line.Points, Center, Radius, AngleAt(From), AngleAt(To), Tolerance);
			Line.bClosed = bClosed;
			if (bClosed && Line.Points.Num() > 2)
			{
				Line.Points.Pop();
			}
			TArray<int8> FillOnLeft;
			WidgetShapeGeometry::ClassifyLines(Outline, FillOnLeft);
			FWidgetShapeMesh Piece;
			WidgetShapeGeometry::BuildStroke(Outline, FillOnLeft, Stroke, FWidgetShapeTrim(), AntiAlias, Tolerance, Piece);
			for (FWidgetShapeMeshVertex& Vertex : Piece.Vertices)
			{
				Vertex.Along.X = From + Vertex.Along.X / FMath::Max(ArcLength, 0.01f);
			}
			AppendMesh(Out, Piece);
			if (OutLines)
			{
				OutLines->Add(Line.Points);
			}
		};

		if (bFreeSpan || (Count == 1 && !(bFull && SpanStart <= 0.f && SpanEnd >= 1.f)))
		{
			if (SpanEnd - SpanStart > 1e-5f)
			{
				AddArc(bFreeSpan ? SpanStart : FMath::Max(SpanStart, 0.f), bFreeSpan ? SpanEnd : FMath::Min(SpanEnd, 1.f), false);
			}
			return;
		}
		if (Count == 1)
		{
			AddArc(0.f, 1.f, true);
			return;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float A = FirstOffset + static_cast<float>(Index) * (SegmentSpanLength + GapSpan);
			const float B = A + SegmentSpanLength;
			const float From = FMath::Max(SpanStart, A);
			const float To = FMath::Min(SpanEnd, B);
			if (To - From > 1e-5f)
			{
				AddArc(From, To, false);
			}
		}
	};

	// The track.
	uint32 Hash = GetTypeHash(Size);
	Hash = HashCombineFast(Hash, GetTypeHash(PixelScale));
	Hash = HashCombineFast(Hash, GetTypeHash(Count));
	Hash = HashCombineFast(Hash, GetTypeHash(GapDegrees));
	Hash = HashCombineFast(Hash, GetTypeHash(Thickness));
	Hash = HashCombineFast(Hash, GetTypeHash(StartAngle));
	Hash = HashCombineFast(Hash, GetTypeHash(SweepDegrees * Turn));
	Hash = HashCombineFast(Hash, GetTypeHash(bRound));
	Hash = HashCombineFast(Hash, BarStyle.Track.GetLayoutHash() + 1u);
	if (!bTrackBuilt || Hash != TrackHash)
	{
		TrackMesh.Reset();
		TrackOutlineMesh.Reset();
		BuildArcs(0.f, 1.f, Thickness, false, TrackMesh, nullptr);
		WidgetShapeGeometry::CutForGradient(TrackMesh, BarStyle.Track, MakeBoxFrame(Whole), 6.f / PixelScale);
		TrackHash = Hash;
		bTrackBuilt = true;
	}
	const FWidgetEffectMaterialInputs MaterialInputs = MakeMaterialInputs(Size);
	PaintMeshWithPaint(OutDrawElements, LayerId, AllottedGeometry, TrackMesh, BarStyle.Track, Whole, TrackSVG, *this, InWidgetStyle, FLinearColor::White, &Materials, 0, &MaterialInputs);

	// What is shown.
	const float Shown = FMath::Clamp(FillValue.Value, 0.f, 1.f);
	const float Trail = FMath::Clamp(GhostValue.Value, 0.f, 1.f);
	auto Snapped = [this, Count](float Value)
	{
		return (BarStyle.bWholeSegments && Count > 1) ? FMath::FloorToFloat(Value * static_cast<float>(Count) + 1e-3f) / static_cast<float>(Count) : Value;
	};
	float FillStart = 0.f;
	float FillEnd = Snapped(Shown);
	const float TrailEnd = Snapped(Trail);
	bool bFreeSpan = false;
	if (bIndeterminate)
	{
		if (bFull)
		{
			// Round and round, its length breathing.
			const float Rotation = static_cast<float>(Clock / 1.4);
			const float Breath = 0.5f - 0.5f * FMath::Cos(static_cast<float>(Clock * UE_TWO_PI / 1.3));
			FillStart = Rotation + 0.15f * Breath;
			FillEnd = FillStart + 0.08f + 0.55f * Breath;
			bFreeSpan = true;
		}
		else
		{
			GetIndeterminateSpan(FillStart, FillEnd);
		}
	}
	const float FillWidth = FMath::Max(Thickness - 2.f * Inset, 0.5f);
	const FWidgetShapePaint Paint = MakeFillPaint(Shown);
	const bool bSpanGradient = Paint.IsGradient() && BarStyle.GradientSpan != EProgressGradientSpan::Box;
	const float FillReach = FMath::Max(FillEnd - FillStart, 0.001f);
	auto ColorPosition = [&](float Along)
	{
		return BarStyle.GradientSpan == EProgressGradientSpan::Fill ? (Along - FillStart) / FillReach : (bFreeSpan ? FMath::Frac(Along) : Along);
	};
	const FLinearColor LeadColor = bSpanGradient ? Paint.SampleGradient(ColorPosition(FillEnd)) * Paint.Color : Paint.Color;

	// Glow along the fill, under it: four lanes across the arc, clear at the outside, full over the fill.
	TArray<TArray<FVector2f>> FillLines;
	FWidgetShapeMesh FillMesh;
	BuildArcs(FillStart, FillEnd, FillWidth, bFreeSpan, FillMesh, &FillLines);
	if (BarStyle.Glow > 0.f && FillLines.Num() > 0)
	{
		const float Spread = FMath::Max(BarStyle.GlowSize, 1.f);
		const float Offsets[4] = { -FillWidth * 0.5f - Spread, -FillWidth * 0.5f, FillWidth * 0.5f, FillWidth * 0.5f + Spread };
		const float Strength[4] = { 0.f, 1.f, 1.f, 0.f };
		FWidgetShapeMesh GlowMesh;
		for (const TArray<FVector2f>& Points : FillLines)
		{
			const uint32 Base = static_cast<uint32>(GlowMesh.Vertices.Num());
			for (const FVector2f& Point : Points)
			{
				const FVector2f Outward = (Point - Center).GetSafeNormal();
				for (int32 Lane = 0; Lane < 4; ++Lane)
				{
					GlowMesh.AddVertex(Point + Outward * Offsets[Lane], Strength[Lane]);
				}
			}
			for (int32 Step = 0; Step + 1 < Points.Num(); ++Step)
			{
				for (int32 Lane = 0; Lane < 3; ++Lane)
				{
					const uint32 A = Base + static_cast<uint32>(Step * 4 + Lane);
					GlowMesh.AddQuad(A, A + 1, A + 5, A + 4);
				}
			}
		}
		FLinearColor GlowColor = LeadColor;
		GlowColor.A *= BarStyle.Glow * 0.6f;
		PaintMesh(OutDrawElements, LayerId + 2, AllottedGeometry, GlowMesh, WidgetTint, [&GlowColor](const FWidgetShapeMeshVertex&) { return GlowColor; });
	}

	// The trail.
	if (!bIndeterminate && BarStyle.bTrail && TrailEnd > FillEnd + 1e-4f)
	{
		FWidgetShapeMesh TrailMesh;
		BuildArcs(0.f, TrailEnd, FillWidth, false, TrailMesh, nullptr);
		const FLinearColor TrailColor = bGain ? BarStyle.GainColor : BarStyle.LossColor;
		PaintMesh(OutDrawElements, LayerId + 3, AllottedGeometry, TrailMesh, WidgetTint, [&TrailColor](const FWidgetShapeMeshVertex&) { return TrailColor; });
	}

	// The fill: its arcs are fine enough that colors round it are drawn per vertex.
	if (bSpanGradient)
	{
		PaintMesh(OutDrawElements, LayerId + 4, AllottedGeometry, FillMesh, WidgetTint,
			[&Paint, &ColorPosition](const FWidgetShapeMeshVertex& Vertex) { return Paint.SampleGradient(ColorPosition(Vertex.Along.X)) * Paint.Color; });
	}
	else
	{
		WidgetShapeGeometry::CutForGradient(FillMesh, Paint, MakeBoxFrame(Whole), 6.f / PixelScale);
		PaintMeshWithPaint(OutDrawElements, LayerId + 4, AllottedGeometry, FillMesh, Paint, Whole, FillSVG, *this, InWidgetStyle, FLinearColor::White, &Materials, 1, &MaterialInputs);
	}

	// The shine, round the fill.
	float ShineCenter = 0.f;
	if (BarStyle.bShine && !bIndeterminate && !FillMesh.IsEmpty() && GetShineCenter(Clock, BarStyle.ShineInterval, BarStyle.ShineWidth, ShineCenter))
	{
		const float Width = FMath::Max(BarStyle.ShineWidth, 0.02f);
		const float Opacity = BarStyle.ShineOpacity;
		PaintMesh(OutDrawElements, LayerId + 6, AllottedGeometry, FillMesh, WidgetTint, [FillStart, FillReach, ShineCenter, Width, Opacity](const FWidgetShapeMeshVertex& Vertex)
		{
			const float AlongFill = (Vertex.Along.X - FillStart) / FillReach;
			return FLinearColor(1.f, 1.f, 1.f, Opacity * FMath::Max(0.f, 1.f - FMath::Abs(AlongFill - ShineCenter) / Width));
		});
	}

	// Markers: ticks across the ring.
	if (BarStyle.Markers.Num() > 0)
	{
		FWidgetShapeMesh Ticks;
		const float HalfWidth = FMath::Max(BarStyle.MarkerWidth, 0.5f) * 0.5f;
		for (const float Marker : BarStyle.Markers)
		{
			float Sin = 0.f;
			float Cos = 1.f;
			FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(AngleAt(Marker)));
			const FVector2f Outward(Sin, -Cos);
			const FVector2f Side(Cos, Sin);
			const FVector2f Inner = Center + Outward * (Radius - Thickness * 0.5f);
			const FVector2f Outer = Center + Outward * (Radius + Thickness * 0.5f);
			const uint32 A = Ticks.AddVertex(Inner - Side * HalfWidth, 1.f);
			const uint32 B = Ticks.AddVertex(Outer - Side * HalfWidth, 1.f);
			const uint32 C = Ticks.AddVertex(Outer + Side * HalfWidth, 1.f);
			const uint32 D = Ticks.AddVertex(Inner + Side * HalfWidth, 1.f);
			Ticks.AddQuad(A, B, C, D);
		}
		const FLinearColor MarkerColor = BarStyle.MarkerColor;
		PaintMesh(OutDrawElements, LayerId + 7, AllottedGeometry, Ticks, WidgetTint, [&MarkerColor](const FWidgetShapeMeshVertex&) { return MarkerColor; });
	}
	return LayerId + 7;
}

int32 SAdvancedProgressBar::PaintCurve(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle) const
{
	using namespace AdvancedProgressBarPrivate;

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2f Size(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	const float PixelScale = QuantizeScale(GetPixelScale(AllottedGeometry));
	const float Tolerance = WidgetShapeGeometry::GetTolerance(PixelScale);
	const float AntiAlias = 1.f / PixelScale;
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FBox2f Whole(FVector2f::ZeroVector, Size);
	const UCurveShape* Line = Curve.Get();
	if (!Line)
	{
		return LayerId;
	}

	const float Thickness = FMath::Clamp(BarStyle.CurveThickness, 1.f, FMath::Max(FMath::Min(Size.X, Size.Y), 1.f));
	const bool bRound = BarStyle.CurveCap == EProgressRingCap::Round;
	const float SlantAngle = bRound ? 0.f : FMath::Clamp(BarStyle.CurveCapAngle, -80.f, 80.f);
	const bool bSlant = !FMath::IsNearlyZero(SlantAngle, 0.01f);
	const float Inset = FMath::Clamp(BarStyle.FillInset, 0.f, Thickness * 0.45f);

	// The line in local space: the Curve's points in this box, kept clear of its edges by half the thickness.
	uint32 LineHash = Line->GetMeshHash();
	LineHash = HashCombineFast(LineHash, GetTypeHash(Size));
	LineHash = HashCombineFast(LineHash, GetTypeHash(PixelScale));
	LineHash = HashCombineFast(LineHash, GetTypeHash(Thickness));
	LineHash = HashCombineFast(LineHash, GetTypeHash(BarStyle.bReverseCurve));
	if (CurveHash != LineHash || CurvePoints.Num() < 2)
	{
		CurveHash = LineHash;
		bTrackBuilt = false;
		FWidgetShapeFrame Frame;
		Frame.Center = Size * 0.5f;
		Frame.Size = FVector2f(FMath::Max(Size.X - Thickness, 1.f), FMath::Max(Size.Y - Thickness, 1.f));
		FWidgetShapePath Path;
		Line->BuildCurvePath(Path, FVector2D(Frame.Size.X, Frame.Size.Y));
		FWidgetShapeOutline Outline;
		WidgetShapeGeometry::FlattenPath(Path, Frame, Tolerance, Outline);
		CurvePoints.Reset();
		bCurveClosed = false;
		if (Outline.Lines.Num() > 0 && Outline.Lines[0].Points.Num() >= 2)
		{
			CurvePoints = Outline.Lines[0].Points;
			bCurveClosed = Outline.Lines[0].bClosed;
			if (bCurveClosed)
			{
				// Round and back to the start: measured as one line from the first point to itself.
				CurvePoints.Add(FVector2f(CurvePoints[0]));
			}
			if (BarStyle.bReverseCurve)
			{
				Algo::Reverse(CurvePoints);
			}
			// Fine enough that colors along it (a gradient, the shine) read smoothly.
			WidgetPathGeometry::Resample(CurvePoints, 4.f);
		}
		CurveLength = WidgetShapeGeometry::GetLength(CurvePoints, /*bClosed=*/false);
	}
	if (CurvePoints.Num() < 2 || CurveLength <= KINDA_SMALL_NUMBER)
	{
		return LayerId;
	}
	const float Length = CurveLength;

	const int32 Count = FMath::Clamp(BarStyle.Segments, 1, 100);
	const float GapLength = Count > 1 ? FMath::Max(BarStyle.SegmentGap, 0.f) + (bRound ? Thickness : 0.f) : 0.f;
	const float GapSpan = GapLength / Length;
	const int32 Gaps = Count > 1 ? (bCurveClosed ? Count : Count - 1) : 0;
	const float SegmentSpanLength = FMath::Max((1.f - GapSpan * static_cast<float>(Gaps)) / static_cast<float>(Count), 0.001f);
	const float FirstOffset = (bCurveClosed && Count > 1) ? GapSpan * 0.5f : 0.f;

	FWidgetShapeStroke Stroke;
	Stroke.bEnabled = true;
	Stroke.Align = EWidgetShapeStrokeAlign::Center;
	Stroke.Join = EWidgetShapeLineJoin::Round;
	Stroke.Cap = bRound ? EWidgetShapeLineCap::Round : EWidgetShapeLineCap::Butt;
	TArray<int8> OpenSide;
	OpenSide.Add(0);

	// Pieces over a span (0..1 along the line), split by the segments; every vertex's Along.X becomes where it is along the line.
	auto BuildPieces = [&](float SpanStart, float SpanEnd, float Width, bool bFreeSpan, FWidgetShapeMesh& Out, TArray<TArray<FVector2f>>* OutLines)
	{
		Stroke.Width = Width;
		const float HalfWidth = Width * 0.5f;
		auto AddPiece = [&](float From, float To)
		{
			TArray<FVector2f> Points;
			WidgetPathGeometry::CutBetween(CurvePoints, From * Length, To * Length, Points);
			if (Points.Num() < 2)
			{
				return;
			}
			if (OutLines)
			{
				OutLines->Add(Points);
			}
			// All the way round a closed line: one closed stroke, no ends meeting at the start.
			if (bCurveClosed && From <= 1e-5f && To >= 1.f - 1e-5f && Points.Num() > 3)
			{
				FWidgetShapeOutline Loop;
				FWidgetShapeLine& Closed = Loop.Lines.AddDefaulted_GetRef();
				Closed.Points = Points;
				Closed.Points.Pop();
				Closed.bClosed = true;
				FWidgetShapeMesh Piece;
				WidgetShapeGeometry::BuildStroke(Loop, OpenSide, Stroke, FWidgetShapeTrim(), AntiAlias, Tolerance, Piece);
				for (FWidgetShapeMeshVertex& Vertex : Piece.Vertices)
				{
					Vertex.Along.X /= Length;
					Vertex.OutlineLength = 1.f;
				}
				AppendMesh(Out, Piece);
				return;
			}
			// Slanted: built longer, square-ended, then cut at the angle.
			float Extend = 0.f;
			FVector2f StartWay = FVector2f::ZeroVector;
			FVector2f EndWay = FVector2f::ZeroVector;
			const FVector2f StartPoint = Points[0];
			const FVector2f EndPoint = Points.Last();
			if (bSlant)
			{
				Extend = WidgetShapeGeometry::GetSlantReach(SlantAngle, HalfWidth, AntiAlias);
				StartWay = (Points[0] - Points[1]).GetSafeNormal();
				EndWay = (Points.Last() - Points[Points.Num() - 2]).GetSafeNormal();
				Points.Insert(StartPoint + StartWay * Extend, 0);
				Points.Add(EndPoint + EndWay * Extend);
			}
			FWidgetShapeOutline Outline;
			FWidgetShapeLine& Run = Outline.Lines.AddDefaulted_GetRef();
			Run.Points = MoveTemp(Points);
			Run.bClosed = false;
			FWidgetShapeMesh Piece;
			WidgetShapeGeometry::BuildStroke(Outline, OpenSide, Stroke, FWidgetShapeTrim(), AntiAlias, Tolerance, Piece);
			for (FWidgetShapeMeshVertex& Vertex : Piece.Vertices)
			{
				Vertex.Along.X = From + (Vertex.Along.X - Extend) / Length;
				Vertex.OutlineLength = 1.f;
			}
			if (bSlant)
			{
				WidgetShapeGeometry::SlantStrokeEnd(Piece, StartPoint, StartWay, SlantAngle, HalfWidth, AntiAlias);
				WidgetShapeGeometry::SlantStrokeEnd(Piece, EndPoint, EndWay, SlantAngle, HalfWidth, AntiAlias);
			}
			AppendMesh(Out, Piece);
		};
		// A span running past the end of a closed line carries on from its start.
		auto AddWrapped = [&](float From, float To)
		{
			if (!bCurveClosed)
			{
				From = FMath::Max(From, 0.f);
				To = FMath::Min(To, 1.f);
				if (To - From > 1e-5f)
				{
					AddPiece(From, To);
				}
				return;
			}
			const float Shift = FMath::FloorToFloat(From);
			From -= Shift;
			To -= Shift;
			if (To <= 1.f)
			{
				AddPiece(From, To);
				return;
			}
			AddPiece(From, 1.f);
			AddPiece(0.f, FMath::Min(To - 1.f, 1.f));
		};

		if (bFreeSpan || Count == 1)
		{
			if (SpanEnd - SpanStart > 1e-5f)
			{
				AddWrapped(SpanStart, SpanEnd);
			}
			return;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float A = FirstOffset + static_cast<float>(Index) * (SegmentSpanLength + GapSpan);
			const float B = A + SegmentSpanLength;
			const float From = FMath::Max(SpanStart, A);
			const float To = FMath::Min(SpanEnd, B);
			if (To - From > 1e-5f)
			{
				AddPiece(From, To);
			}
		}
	};

	// The track.
	uint32 Hash = CurveHash;
	Hash = HashCombineFast(Hash, GetTypeHash(Count));
	Hash = HashCombineFast(Hash, GetTypeHash(GapLength));
	Hash = HashCombineFast(Hash, GetTypeHash(bRound));
	Hash = HashCombineFast(Hash, GetTypeHash(SlantAngle));
	Hash = HashCombineFast(Hash, BarStyle.Track.GetLayoutHash() + 2u);
	if (!bTrackBuilt || Hash != TrackHash)
	{
		TrackMesh.Reset();
		TrackOutlineMesh.Reset();
		BuildPieces(0.f, 1.f, Thickness, false, TrackMesh, nullptr);
		if (!BarStyle.Track.MapsAlongOutline())
		{
			WidgetShapeGeometry::CutForGradient(TrackMesh, BarStyle.Track, MakeBoxFrame(Whole), 6.f / PixelScale);
		}
		TrackHash = Hash;
		bTrackBuilt = true;
	}
	const FWidgetEffectMaterialInputs MaterialInputs = MakeMaterialInputs(Size);
	PaintMeshWithPaint(OutDrawElements, LayerId, AllottedGeometry, TrackMesh, BarStyle.Track, Whole, TrackSVG, *this, InWidgetStyle, FLinearColor::White,
		&Materials, 0, &MaterialInputs, /*bAlongOutline=*/true);

	// What is shown.
	const float Shown = FMath::Clamp(FillValue.Value, 0.f, 1.f);
	const float Trail = FMath::Clamp(GhostValue.Value, 0.f, 1.f);
	auto Snapped = [this, Count](float Value)
	{
		return (BarStyle.bWholeSegments && Count > 1) ? FMath::FloorToFloat(Value * static_cast<float>(Count) + 1e-3f) / static_cast<float>(Count) : Value;
	};
	float FillStart = 0.f;
	float FillEnd = Snapped(Shown);
	const float TrailEnd = Snapped(Trail);
	bool bFreeSpan = false;
	if (bIndeterminate)
	{
		if (bCurveClosed)
		{
			// Round and round, its length breathing, as a full ring's.
			const float Rotation = static_cast<float>(Clock / 1.4);
			const float Breath = 0.5f - 0.5f * FMath::Cos(static_cast<float>(Clock * UE_TWO_PI / 1.3));
			FillStart = Rotation + 0.15f * Breath;
			FillEnd = FillStart + 0.08f + 0.55f * Breath;
		}
		else
		{
			GetIndeterminateSpan(FillStart, FillEnd);
		}
		bFreeSpan = true;
	}
	const float FillWidth = FMath::Max(Thickness - 2.f * Inset, 0.5f);
	const FWidgetShapePaint Paint = MakeFillPaint(Shown);
	const bool bSpanGradient = Paint.IsGradient() && BarStyle.GradientSpan != EProgressGradientSpan::Box;
	const float FillReach = FMath::Max(FillEnd - FillStart, 0.001f);
	auto ColorPosition = [&](float Along)
	{
		return BarStyle.GradientSpan == EProgressGradientSpan::Fill ? (Along - FillStart) / FillReach : (bCurveClosed ? FMath::Frac(Along) : Along);
	};
	const FLinearColor LeadColor = bSpanGradient ? Paint.SampleGradient(ColorPosition(FillEnd)) * Paint.Color : Paint.Color;

	// Glow along the fill, under it: four lanes across the line, clear at the outside, full over the fill.
	TArray<TArray<FVector2f>> FillLines;
	FWidgetShapeMesh FillMesh;
	BuildPieces(FillStart, FillEnd, FillWidth, bFreeSpan, FillMesh, &FillLines);
	if (BarStyle.Glow > 0.f && FillLines.Num() > 0)
	{
		const float Spread = FMath::Max(BarStyle.GlowSize, 1.f);
		const float Offsets[4] = { -FillWidth * 0.5f - Spread, -FillWidth * 0.5f, FillWidth * 0.5f, FillWidth * 0.5f + Spread };
		const float Strength[4] = { 0.f, 1.f, 1.f, 0.f };
		FWidgetShapeMesh GlowMesh;
		for (const TArray<FVector2f>& Points : FillLines)
		{
			const uint32 Base = static_cast<uint32>(GlowMesh.Vertices.Num());
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const FVector2f Along = (Points[FMath::Min(Index + 1, Points.Num() - 1)] - Points[FMath::Max(Index - 1, 0)]).GetSafeNormal();
				const FVector2f Side(-Along.Y, Along.X);
				for (int32 Lane = 0; Lane < 4; ++Lane)
				{
					GlowMesh.AddVertex(Points[Index] + Side * Offsets[Lane], Strength[Lane]);
				}
			}
			for (int32 Step = 0; Step + 1 < Points.Num(); ++Step)
			{
				for (int32 Lane = 0; Lane < 3; ++Lane)
				{
					const uint32 A = Base + static_cast<uint32>(Step * 4 + Lane);
					GlowMesh.AddQuad(A, A + 1, A + 5, A + 4);
				}
			}
		}
		FLinearColor GlowColor = LeadColor;
		GlowColor.A *= BarStyle.Glow * 0.6f;
		PaintMesh(OutDrawElements, LayerId + 2, AllottedGeometry, GlowMesh, WidgetTint, [&GlowColor](const FWidgetShapeMeshVertex&) { return GlowColor; });
	}

	// The trail.
	if (!bIndeterminate && BarStyle.bTrail && TrailEnd > FillEnd + 1e-4f)
	{
		FWidgetShapeMesh TrailMesh;
		BuildPieces(0.f, TrailEnd, FillWidth, false, TrailMesh, nullptr);
		const FLinearColor TrailColor = bGain ? BarStyle.GainColor : BarStyle.LossColor;
		PaintMesh(OutDrawElements, LayerId + 3, AllottedGeometry, TrailMesh, WidgetTint, [&TrailColor](const FWidgetShapeMeshVertex&) { return TrailColor; });
	}

	// The fill: fine enough along the line that colors along it are drawn per vertex.
	if (bSpanGradient)
	{
		PaintMesh(OutDrawElements, LayerId + 4, AllottedGeometry, FillMesh, WidgetTint,
			[&Paint, &ColorPosition](const FWidgetShapeMeshVertex& Vertex) { return Paint.SampleGradient(ColorPosition(Vertex.Along.X)) * Paint.Color; });
	}
	else
	{
		if (!Paint.MapsAlongOutline())
		{
			WidgetShapeGeometry::CutForGradient(FillMesh, Paint, MakeBoxFrame(Whole), 6.f / PixelScale);
		}
		PaintMeshWithPaint(OutDrawElements, LayerId + 4, AllottedGeometry, FillMesh, Paint, Whole, FillSVG, *this, InWidgetStyle, FLinearColor::White,
			&Materials, 1, &MaterialInputs, /*bAlongOutline=*/true);
	}

	// The shine, along the fill.
	float ShineCenter = 0.f;
	if (BarStyle.bShine && !bIndeterminate && !FillMesh.IsEmpty() && GetShineCenter(Clock, BarStyle.ShineInterval, BarStyle.ShineWidth, ShineCenter))
	{
		const float Width = FMath::Max(BarStyle.ShineWidth, 0.02f);
		const float Opacity = BarStyle.ShineOpacity;
		PaintMesh(OutDrawElements, LayerId + 6, AllottedGeometry, FillMesh, WidgetTint, [FillStart, FillReach, ShineCenter, Width, Opacity](const FWidgetShapeMeshVertex& Vertex)
		{
			const float AlongFill = (Vertex.Along.X - FillStart) / FillReach;
			return FLinearColor(1.f, 1.f, 1.f, Opacity * FMath::Max(0.f, 1.f - FMath::Abs(AlongFill - ShineCenter) / Width));
		});
	}

	// Markers: ticks across the line.
	if (BarStyle.Markers.Num() > 0)
	{
		FWidgetShapeMesh Ticks;
		const float HalfWidth = FMath::Max(BarStyle.MarkerWidth, 0.5f) * 0.5f;
		for (const float Marker : BarStyle.Markers)
		{
			FVector2f Point;
			FVector2f Direction;
			if (!WidgetPathGeometry::SampleAt(CurvePoints, FMath::Clamp(Marker, 0.f, 1.f) * Length, Point, Direction))
			{
				continue;
			}
			const FVector2f Across(-Direction.Y, Direction.X);
			const FVector2f Inner = Point - Across * (Thickness * 0.5f);
			const FVector2f Outer = Point + Across * (Thickness * 0.5f);
			const uint32 A = Ticks.AddVertex(Inner - Direction * HalfWidth, 1.f);
			const uint32 B = Ticks.AddVertex(Outer - Direction * HalfWidth, 1.f);
			const uint32 C = Ticks.AddVertex(Outer + Direction * HalfWidth, 1.f);
			const uint32 D = Ticks.AddVertex(Inner + Direction * HalfWidth, 1.f);
			Ticks.AddQuad(A, B, C, D);
		}
		const FLinearColor MarkerColor = BarStyle.MarkerColor;
		PaintMesh(OutDrawElements, LayerId + 7, AllottedGeometry, Ticks, WidgetTint, [&MarkerColor](const FWidgetShapeMeshVertex&) { return MarkerColor; });
	}
	return LayerId + 7;
}

// =====================================================================================================
// UAdvancedProgressBar
// =====================================================================================================

UAdvancedProgressBar::UAdvancedProgressBar(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

float UAdvancedProgressBar::GetBoundPercent() const
{
	return PercentDelegate.IsBound() ? PercentDelegate.Execute() : Percent;
}

TSharedRef<SWidget> UAdvancedProgressBar::RebuildWidget()
{
	MyBar = SNew(SAdvancedProgressBar)
		.OnReachedEnd(BIND_UOBJECT_DELEGATE(FOnProgressReachedEnd, HandleReachedEnd));
	MyBar->SetBarStyle(BarStyle);
	MyBar->SetPercent(Percent, /*bAnimate=*/false);
	return MyBar.ToSharedRef();
}

void UAdvancedProgressBar::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (!MyBar.IsValid())
	{
		return;
	}
	MyBar->SetBarStyle(BarStyle);
	MyBar->SetCurve(BarStyle.Shape == EProgressBarShape::Curve ? ResolveCurve() : nullptr);
	MyBar->SetMotion(Motion, EnterDuration, ExitDuration);
	MyBar->SetIndeterminate(bIndeterminate);
	if (PercentDelegate.IsBound())
	{
		MyBar->SetPercentSource(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateUObject(this, &UAdvancedProgressBar::GetBoundPercent)));
	}
	else
	{
		MyBar->SetPercentSource(TAttribute<float>());
		MyBar->SetPercent(Percent, !IsDesignTime());
	}
}

void UAdvancedProgressBar::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyBar.Reset();
}

void UAdvancedProgressBar::SetPercent(float InPercent)
{
	const float Clamped = FMath::Clamp(InPercent, 0.f, 1.f);
	if (FMath::IsNearlyEqual(Clamped, Percent))
	{
		return;
	}
	const float OldPercent = Percent;
	Percent = Clamped;
	if (MyBar.IsValid())
	{
		MyBar->SetPercent(Percent, !IsDesignTime());
	}
	OnPercentChanged.Broadcast(Percent, OldPercent);
}

void UAdvancedProgressBar::SnapToPercent(float InPercent)
{
	const float OldPercent = Percent;
	Percent = FMath::Clamp(InPercent, 0.f, 1.f);
	if (MyBar.IsValid())
	{
		MyBar->SetPercent(Percent, false);
	}
	if (!FMath::IsNearlyEqual(OldPercent, Percent))
	{
		OnPercentChanged.Broadcast(Percent, OldPercent);
	}
}

float UAdvancedProgressBar::GetDisplayedPercent() const
{
	return MyBar.IsValid() ? MyBar->GetDisplayedPercent() : Percent;
}

void UAdvancedProgressBar::SetIndeterminate(bool bInIndeterminate)
{
	bIndeterminate = bInIndeterminate;
	if (MyBar.IsValid())
	{
		MyBar->SetIndeterminate(bIndeterminate);
	}
}

void UAdvancedProgressBar::SetBarStyle(const FAdvancedProgressBarStyle& InStyle)
{
	BarStyle = InStyle;
	if (MyBar.IsValid())
	{
		MyBar->SetBarStyle(BarStyle);
		MyBar->SetCurve(BarStyle.Shape == EProgressBarShape::Curve ? ResolveCurve() : nullptr);
	}
}

const UCurveShape* UAdvancedProgressBar::ResolveCurve()
{
	if (BarStyle.CurveSource == EProgressCurveSource::Widget)
	{
		if (const UCurveShape* Found = Cast<UCurveShape>(WidgetTreeLookup::FindWidget(this, BarStyle.CurveWidget)))
		{
			return Found;
		}
	}
	if (!MadeCurve)
	{
		MadeCurve = NewObject<UCurveShape>(this, NAME_None, RF_Transient);
	}
	if (BarStyle.CurveSource == EProgressCurveSource::Preset && BarStyle.CurvePreset)
	{
		MadeCurve->ApplyCurvePreset(BarStyle.CurvePreset);
	}
	else
	{
		// A ready-made line - or, with nothing picked yet, the ready-made line the bar shows while it waits.
		MadeCurve->SetCurveFromTemplate(BarStyle.CurveTemplate);
		MadeCurve->Interpolation = EWidgetCurveInterpolation::CatmullRom;
	}
	return MadeCurve;
}

TArray<FString> UAdvancedProgressBar::GetWidgetOptions() const
{
	TArray<FString> Names;
	WidgetTreeLookup::GetWidgetNames(this, Names, this);
	return Names;
}

void UAdvancedProgressBar::SetFillColor(FLinearColor InColor)
{
	BarStyle.Fill.Type = EWidgetShapePaintType::Color;
	BarStyle.Fill.Color = InColor;
	BarStyle.bColorByValue = false;
	SetBarStyle(BarStyle);
}

void UAdvancedProgressBar::SetSegments(int32 InSegments, float InGap)
{
	BarStyle.Segments = FMath::Clamp(InSegments, 1, 100);
	BarStyle.SegmentGap = FMath::Max(0.f, InGap);
	SetBarStyle(BarStyle);
}

void UAdvancedProgressBar::SetMarkers(const TArray<float>& InMarkers)
{
	BarStyle.Markers = InMarkers;
	SetBarStyle(BarStyle);
}

void UAdvancedProgressBar::HandleReachedEnd(bool bFull)
{
	if (bFull)
	{
		OnFilled.Broadcast();
	}
	else
	{
		OnEmptied.Broadcast();
	}
}

#if WITH_EDITOR
const FText UAdvancedProgressBar::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}

void UAdvancedProgressBar::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// A Material paint lists its material's parameters as the material is picked; made on the Widget Blueprint's own widget too
	// (the Details panel hands it only the edited value).
	const bool bListed = BarStyle.Fill.SyncMaterialParameters() | BarStyle.Track.SyncMaterialParameters();
	if (bListed)
	{
		if (UAdvancedProgressBar* Template = Cast<UAdvancedProgressBar>(WidgetEditorHooks::FindDesignerTemplate(this)))
		{
			if (Template != this)
			{
				Template->Modify();
				Template->BarStyle.Fill.MaterialParameters = BarStyle.Fill.MaterialParameters;
				Template->BarStyle.Track.MaterialParameters = BarStyle.Track.MaterialParameters;
			}
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

#undef LOCTEXT_NAMESPACE
