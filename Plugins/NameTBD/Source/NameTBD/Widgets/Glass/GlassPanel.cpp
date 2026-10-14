// Copyright AR GameSystems 2026. All rights reserved.

#include "GlassPanel.h"

#include "Algo/Reverse.h"
#include "Components/Button.h"
#include "Components/PanelSlot.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "NameTBD/Widgets/WidgetEffectsDraw.h"

#define LOCTEXT_NAMESPACE "GlassPanel"

namespace GlassPanelPrivate
{
	FLinearColor Rgb(uint8 R, uint8 G, uint8 B, float Alpha)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor(R, G, B, 255));
		Color.A = Alpha;
		return Color;
	}

	FWidgetShapePaint FlatTint(const FLinearColor& Color)
	{
		FWidgetShapePaint Paint;
		Paint.Type = EWidgetShapePaintType::Color;
		Paint.Color = Color;
		return Paint;
	}

	FWidgetShapePaint GradientTint(float Angle, const TArray<FWidgetShapeGradientStop>& Stops)
	{
		FWidgetShapePaint Paint;
		Paint.Type = EWidgetShapePaintType::LinearGradient;
		Paint.Color = FLinearColor::White;
		Paint.GradientAngle = Angle;
		Paint.GradientStops = Stops;
		return Paint;
	}

	/** Toward the light, from LightAngle (0 from the top, clockwise). */
	FVector2f TowardLight(float LightAngle)
	{
		float Sin = 0.f;
		float Cos = 0.f;
		FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(LightAngle));
		return FVector2f(Sin, -Cos);
	}

	/** Colors added the way light adds on glass: their alphas summed, their colors weighted by them. */
	FLinearColor AddLight(const FLinearColor& A, float WeightA, const FLinearColor& B, float WeightB, const FLinearColor& C, float WeightC)
	{
		const float AlphaA = A.A * WeightA;
		const float AlphaB = B.A * WeightB;
		const float AlphaC = C.A * WeightC;
		const float Total = AlphaA + AlphaB + AlphaC;
		if (Total <= KINDA_SMALL_NUMBER)
		{
			return FLinearColor(1.f, 1.f, 1.f, 0.f);
		}
		FLinearColor Result = (A * AlphaA + B * AlphaB + C * AlphaC) / Total;
		Result.A = FMath::Clamp(Total, 0.f, 1.f);
		return Result;
	}
}

// =====================================================================================================
// FGlassLook
// =====================================================================================================

FGlassLook::FGlassLook()
{
	Tint.Type = EWidgetShapePaintType::Color;
	Tint.Color = FLinearColor(1.f, 1.f, 1.f, 0.14f);
}

// =====================================================================================================
// SGlassSurface
// =====================================================================================================

void SGlassSurface::Construct(const FArguments& InArgs)
{
	SetCanTick(false);
	SetVisibility(EVisibility::HitTestInvisible);
}

void SGlassSurface::SetLook(const FGlassLook& InLook, const FVector4f& InRadii, bool bInPill)
{
	Look = InLook;
	Radii = InRadii;
	bPill = bInPill;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SGlassSurface::SetAmounts(float InHover, float InPress)
{
	if (!FMath::IsNearlyEqual(Hover, InHover) || !FMath::IsNearlyEqual(Press, InPress))
	{
		Hover = InHover;
		Press = InPress;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FVector4f SGlassSurface::ResolveRadii(const FVector2f& Size) const
{
	if (bPill)
	{
		const float Half = FMath::Min(Size.X, Size.Y) * 0.5f;
		return FVector4f(Half, Half, Half, Half);
	}
	return WidgetEffectsDraw::FitRadii(Radii, Size);
}

void SGlassSurface::Rebuild(const FVector2f& Size, float PixelScale) const
{
	using namespace WidgetEffectsDraw;

	const float Tolerance = WidgetShapeGeometry::GetTolerance(PixelScale);
	const float AntiAlias = 1.f / PixelScale;
	const FVector4f Corners = ResolveRadii(Size);
	const FBox2f Box(FVector2f::ZeroVector, Size);
	const FWidgetShapeFrame Frame = MakeBoxFrame(Box);

	FWidgetShapeOutline Outline;
	AppendRoundedRect(Outline, FVector2f::ZeroVector, Size, Corners, Tolerance);

	FWidgetShapeMesh Fill;
	TArray<int8> FillOnLeft;
	WidgetShapeGeometry::BuildFill(Outline, AntiAlias, Fill, FillOnLeft);

	TintMesh = Fill;
	WidgetShapeGeometry::CutForGradient(TintMesh, Look.Tint, Frame, 6.f / PixelScale);
	WashMesh = Fill;

	// The sheen's falloff, cut where it bends so the GPU draws it exactly.
	SheenMesh = Fill;
	const FVector2f Away = -GlassPanelPrivate::TowardLight(Look.LightAngle);
	const float Reach = FMath::Clamp(Look.SheenReach, 0.05f, 1.f);
	FWidgetShapePaint SheenPaint = GlassPanelPrivate::GradientTint(FMath::RadiansToDegrees(FMath::Atan2(Away.Y, Away.X)),
		{ FWidgetShapeGradientStop(0.f, FLinearColor::White), FWidgetShapeGradientStop(Reach * 0.5f, FLinearColor::White),
		  FWidgetShapeGradientStop(Reach, FLinearColor::White), FWidgetShapeGradientStop(1.f, FLinearColor::White) });
	WidgetShapeGeometry::CutForGradient(SheenMesh, SheenPaint, Frame, 6.f / PixelScale);

	GrainMesh.Reset();
	if (Look.Grain > 0.f && Outline.Lines.Num() > 0)
	{
		BuildGrainMesh(Outline.Lines[0], 128.f * FMath::Max(Look.GrainSize, 0.25f), GrainMesh);
	}

	EdgeMesh.Reset();
	if (Look.EdgeWidth > 0.f)
	{
		FWidgetShapeStroke Edge;
		Edge.bEnabled = true;
		Edge.Width = Look.EdgeWidth;
		Edge.Align = EWidgetShapeStrokeAlign::Inside;
		Edge.Join = EWidgetShapeLineJoin::Round;
		WidgetShapeGeometry::BuildStroke(Outline, FillOnLeft, Edge, FWidgetShapeTrim(), AntiAlias, Tolerance, EdgeMesh);
	}

	// The inner glow: one minus the panel blurred, inside it - an inset shadow in light.
	GlowMesh.Reset();
	if (Look.InnerGlow > 0.f)
	{
		FSoftBox Blurred;
		Blurred.Center = Size * 0.5f;
		Blurred.HalfSize = Size * 0.5f;
		Blurred.Radii = Corners;
		Blurred.Sigma = FMath::Max(Look.InnerGlowSize, 1.f) * 0.5f;
		const float Deepest = FMath::Min(Blurred.HalfSize.X, Blurred.HalfSize.Y) * 0.999f;
		TArray<float> Distances;
		MakeRingDistances(-Deepest, 0.f, FMath::Max(Blurred.Sigma * 0.5f, 1.5f * AntiAlias), 0.f, Blurred.Sigma * 3.f, FMath::Max(Blurred.Sigma * 0.25f, AntiAlias), 40, Distances);
		Distances.Add(AntiAlias);
		BuildRings(Blurred.Center, Blurred.HalfSize, Corners, Distances, FMath::Max(Blurred.Sigma, 4.f * AntiAlias), PixelScale, true, GlowMesh);
		for (FWidgetShapeMeshVertex& Vertex : GlowMesh.Vertices)
		{
			const float Distance = RoundedRectDistance(Vertex.Position, Blurred.Center, Blurred.HalfSize, Corners);
			const float Inside = FMath::Clamp(1.f - Distance / AntiAlias, 0.f, 1.f);
			Vertex.Coverage = (1.f - EvaluateSoftBox(Blurred, Vertex.Position, AntiAlias)) * Inside;
		}
	}
}

int32 SGlassSurface::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace WidgetEffectsDraw;

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
	{
		return LayerId;
	}

	const FVector2f Size(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	const float PixelScale = QuantizeScale(GetPixelScale(AllottedGeometry));
	const FVector4f Corners = ResolveRadii(Size);

	uint32 Hash = GetTypeHash(Size);
	Hash = HashCombineFast(Hash, GetTypeHash(PixelScale));
	Hash = HashCombineFast(Hash, WidgetEffectsDraw::HashRadii(Corners));
	Hash = HashCombineFast(Hash, GetTypeHash(Look.EdgeWidth));
	Hash = HashCombineFast(Hash, GetTypeHash(Look.Grain > 0.f ? Look.GrainSize : 0.f));
	Hash = HashCombineFast(Hash, GetTypeHash(Look.InnerGlow > 0.f ? Look.InnerGlowSize : 0.f));
	Hash = HashCombineFast(Hash, GetTypeHash(Look.LightAngle));
	Hash = HashCombineFast(Hash, GetTypeHash(Look.SheenReach));
	Hash = HashCombineFast(Hash, Look.Tint.GetLayoutHash());
	if (!bBuilt || Hash != BuiltHash)
	{
		Rebuild(Size, PixelScale);
		BuiltHash = Hash;
		bBuilt = true;
	}

	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FBox2f Box(FVector2f::ZeroVector, Size);
	const FWidgetShapeFrame Frame = MakeBoxFrame(Box);

	// The tint (film), then the grain in it.
	PaintMeshWithPaint(OutDrawElements, LayerId, AllottedGeometry, TintMesh, Look.Tint, Box, TintSVG, *this, InWidgetStyle);

	if (Look.Grain > 0.f)
	{
		if (const FSlateBrush* Grain = GetGrainBrush())
		{
			const FLinearColor GrainColor(1.f, 1.f, 1.f, FMath::Clamp(Look.Grain, 0.f, 1.f));
			PaintMesh(OutDrawElements, LayerId + 1, AllottedGeometry, GrainMesh, WidgetTint,
				[&GrainColor](const FWidgetShapeMeshVertex&) { return GrainColor; },
				Grain, [](const FWidgetShapeMeshVertex& Vertex) { return Vertex.Along; });
		}
	}

	// The sheen from the light, brighter on hover.
	const float SheenAlpha = FMath::Clamp(Look.Sheen + Response.SheenBoost * Hover, 0.f, 1.f);
	if (SheenAlpha > 0.f)
	{
		const FVector2f Away = -GlassPanelPrivate::TowardLight(Look.LightAngle);
		const float Reach = FMath::Clamp(Look.SheenReach, 0.05f, 1.f);
		const FWidgetShapePaint SheenPaint = GlassPanelPrivate::GradientTint(FMath::RadiansToDegrees(FMath::Atan2(Away.Y, Away.X)),
			{ FWidgetShapeGradientStop(0.f, FLinearColor(1.f, 1.f, 1.f, SheenAlpha)), FWidgetShapeGradientStop(Reach * 0.5f, FLinearColor(1.f, 1.f, 1.f, SheenAlpha * 0.3f)),
			  FWidgetShapeGradientStop(Reach, FLinearColor(1.f, 1.f, 1.f, 0.f)), FWidgetShapeGradientStop(1.f, FLinearColor(1.f, 1.f, 1.f, 0.f)) });
		PaintMesh(OutDrawElements, LayerId + 2, AllottedGeometry, SheenMesh, WidgetTint,
			[&SheenPaint, &Frame](const FWidgetShapeMeshVertex& Vertex) { return SheenPaint.Evaluate(Frame.ToBox(Vertex.Position)); });
	}

	// Hover brightens, press darkens.
	const float Brighten = FMath::Clamp(Response.Brighten * Hover, 0.f, 1.f);
	if (Brighten > 0.002f)
	{
		const FLinearColor Wash(1.f, 1.f, 1.f, Brighten);
		PaintMesh(OutDrawElements, LayerId + 2, AllottedGeometry, WashMesh, WidgetTint, [&Wash](const FWidgetShapeMeshVertex&) { return Wash; });
	}
	const float Darken = FMath::Clamp(Response.PressDarken * Press, 0.f, 1.f);
	if (Darken > 0.002f)
	{
		const FLinearColor Shade(0.f, 0.f, 0.f, Darken);
		PaintMesh(OutDrawElements, LayerId + 2, AllottedGeometry, WashMesh, WidgetTint, [&Shade](const FWidgetShapeMeshVertex&) { return Shade; });
	}

	if (Look.InnerGlow > 0.f)
	{
		FLinearColor Glow = Look.InnerGlowColor;
		Glow.A *= FMath::Clamp(Look.InnerGlow, 0.f, 1.f);
		PaintMesh(OutDrawElements, LayerId + 3, AllottedGeometry, GlowMesh, WidgetTint, [&Glow](const FWidgetShapeMeshVertex&) { return Glow; });
	}

	// The edge, lit where it faces the light (and, for thick glass, where it faces away).
	if (Look.EdgeWidth > 0.f)
	{
		const FVector2f Toward = GlassPanelPrivate::TowardLight(Look.LightAngle);
		const FVector2f Center = Size * 0.5f;
		const FVector2f HalfSize = Size * 0.5f;
		const float Focus = FMath::Max(Look.EdgeFocus, 0.1f);
		const float HighlightWeight = 1.f + (Look.EdgeHighlight.A > KINDA_SMALL_NUMBER ? Response.EdgeBoost * Hover / Look.EdgeHighlight.A : 0.f);
		PaintMesh(OutDrawElements, LayerId + 4, AllottedGeometry, EdgeMesh, WidgetTint,
			[this, &Toward, &Center, &HalfSize, &Corners, Focus, HighlightWeight](const FWidgetShapeMeshVertex& Vertex)
			{
				const FVector2f Normal = RoundedRectNormal(Vertex.Position, Center, HalfSize, Corners);
				const float Facing = FVector2f::DotProduct(Normal, Toward);
				const float Lit = FMath::Pow(FMath::Max(Facing, 0.f), Focus);
				const float Back = FMath::Pow(FMath::Max(-Facing, 0.f), Focus);
				return GlassPanelPrivate::AddLight(Look.EdgeColor, 1.f, Look.EdgeHighlight, Lit * HighlightWeight, Look.EdgeBacklight, Back);
			});
	}

	return LayerId + 4;
}

// =====================================================================================================
// SGlassPanel
// =====================================================================================================

void SGlassPanel::Construct(const FArguments& InArgs)
{
	FallbackBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	FallbackBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;

	ChildSlot
	[
		SAssignNew(Layers, SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(BlurHost, SBox)
			.Visibility(EVisibility::HitTestInvisible)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(Surface, SGlassSurface)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(ContentBox, SBox)
			.Padding(FMargin(16.f))
			[
				InArgs._Content.Widget
			]
		]
	];

	RebuildBlur();
}

void SGlassPanel::SetContent(const TSharedRef<SWidget>& InContent)
{
	ContentBox->SetContent(InContent);
}

void SGlassPanel::SetContentLayout(const FMargin& InPadding, EHorizontalAlignment InHAlign, EVerticalAlignment InVAlign)
{
	ContentBox->SetPadding(InPadding);
	ContentBox->SetHAlign(InHAlign);
	ContentBox->SetVAlign(InVAlign);
}

void SGlassPanel::SetLook(const FGlassLook& InLook, const FVector4f& InRadii, bool bInPill)
{
	Look = InLook;
	Radii = InRadii;
	bPill = bInPill;

	FallbackBrush.TintColor = FSlateColor(Look.FallbackColor);
	FallbackBrush.OutlineSettings.RoundingType = bPill ? ESlateBrushRoundingType::HalfHeightRadius : ESlateBrushRoundingType::FixedRadius;
	FallbackBrush.OutlineSettings.CornerRadii = FVector4(Radii.X, Radii.Y, Radii.Z, Radii.W);

	const uint32 NewHash = GetBlurLayoutHash();
	if (NewHash != BlurLayoutHash)
	{
		RebuildBlur();
	}
	UpdateBlurStrength();
	Surface->SetLook(Look, Radii, bPill);
	Invalidate(EInvalidateWidgetReason::Paint);
}

uint32 SGlassPanel::GetBlurLayoutHash() const
{
	uint32 Hash = GetTypeHash(static_cast<uint8>(Look.BlurMode));
	Hash = HashCombineFast(Hash, GetTypeHash(Look.BlurStrength > 0.f || Response.BlurBoost > 0.f));
	if (Look.BlurMode == EGlassBlurMode::Progressive)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Look.StrongEdge)));
		Hash = HashCombineFast(Hash, GetTypeHash(Look.FadeStart));
		Hash = HashCombineFast(Hash, GetTypeHash(Look.FadeEnd));
		Hash = HashCombineFast(Hash, GetTypeHash(Look.FadeSteps));
	}
	return Hash;
}

void SGlassPanel::RebuildBlur()
{
	BlurLayoutHash = GetBlurLayoutHash();
	Bands.Reset();

	if (Look.BlurStrength <= 0.f && Response.BlurBoost <= 0.f)
	{
		BlurHost->SetContent(SNullWidget::NullWidget);
		return;
	}

	auto MakeBlur = [this](int32 BandIndex) -> TSharedRef<SBackgroundBlur>
	{
		TSharedRef<SBackgroundBlur> Blur = SNew(SBackgroundBlur)
			.BlurStrength(0.f)
			.Padding(FMargin(0.f))
			.CornerRadius(TAttribute<FVector4>::CreateSP(this, &SGlassPanel::GetBandRadius, BandIndex))
			.LowQualityFallbackBrush(&FallbackBrush);
		Blur->SetApplyAlphaToBlur(true);
		return Blur;
	};

	if (Look.BlurMode == EGlassBlurMode::Uniform)
	{
		FBlurBand& Band = Bands.AddDefaulted_GetRef();
		Band.Blur = MakeBlur(0);
		Band.bTouchesStart = true;
		Band.bTouchesEnd = true;
		BlurHost->SetContent(Band.Blur.ToSharedRef());
		return;
	}

	// Progressive: the full blur up to Fade Start, then Fade Steps bands each a little weaker, then nothing - laid out from the
	// strong edge across the panel.
	const float Start = FMath::Clamp(Look.FadeStart, 0.f, 1.f);
	const float End = FMath::Clamp(FMath::Max(Look.FadeEnd, Start + 0.01f), 0.f, 1.f);
	const int32 Steps = FMath::Clamp(Look.FadeSteps, 2, 16);

	struct FSpan
	{
		float Fraction = 0.f;
		float Weight = 0.f;
	};
	TArray<FSpan> Spans;
	if (Start > 0.005f)
	{
		Spans.Add({ Start, 1.f });
	}
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		// Blur reads roughly by its radius: fall off a little faster than linear so the last bands are faint.
		const float Middle = (static_cast<float>(Step) + 0.5f) / static_cast<float>(Steps);
		Spans.Add({ (End - Start) / static_cast<float>(Steps), FMath::Pow(1.f - Middle, 1.25f) });
	}
	if (End < 0.995f)
	{
		Spans.Add({ 1.f - End, 0.f });
	}

	const bool bVertical = Look.StrongEdge == EGlassEdge::Top || Look.StrongEdge == EGlassEdge::Bottom;
	const bool bReversed = Look.StrongEdge == EGlassEdge::Bottom || Look.StrongEdge == EGlassEdge::Right;
	if (bReversed)
	{
		Algo::Reverse(Spans);
	}

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 SpanIndex = 0; SpanIndex < Spans.Num(); ++SpanIndex)
	{
		const FSpan& Span = Spans[SpanIndex];
		TSharedRef<SWidget> Piece = SNullWidget::NullWidget;
		if (Span.Weight > 0.f)
		{
			FBlurBand& Band = Bands.AddDefaulted_GetRef();
			Band.Blur = MakeBlur(Bands.Num() - 1);
			Band.Weight = Span.Weight;
			// Start / End here mean the panel's top or left edge / bottom or right edge, whichever way the blur falls.
			Band.bTouchesStart = SpanIndex == 0;
			Band.bTouchesEnd = SpanIndex == Spans.Num() - 1;
			Piece = Band.Blur.ToSharedRef();
		}
		if (bVertical)
		{
			Column->AddSlot().FillHeight(FMath::Max(Span.Fraction, 0.0001f))[Piece];
		}
		else
		{
			Row->AddSlot().FillWidth(FMath::Max(Span.Fraction, 0.0001f))[Piece];
		}
	}
	if (bVertical)
	{
		BlurHost->SetContent(Column);
	}
	else
	{
		BlurHost->SetContent(Row);
	}
}

FVector4 SGlassPanel::GetBandRadius(int32 BandIndex) const
{
	if (!Bands.IsValidIndex(BandIndex) || !Surface.IsValid())
	{
		return FVector4(0.0, 0.0, 0.0, 0.0);
	}
	const FBlurBand& Band = Bands[BandIndex];
	const FVector4f Corners = Surface->ResolveRadii(LastSize);
	const bool bVertical = Look.BlurMode == EGlassBlurMode::Uniform || Look.StrongEdge == EGlassEdge::Top || Look.StrongEdge == EGlassEdge::Bottom;

	// Top or left edge: bTouchesStart; bottom or right edge: bTouchesEnd.
	const bool bTop = bVertical ? Band.bTouchesStart : true;
	const bool bBottom = bVertical ? Band.bTouchesEnd : true;
	const bool bLeft = bVertical ? true : Band.bTouchesStart;
	const bool bRight = bVertical ? true : Band.bTouchesEnd;
	return FVector4(
		(bTop && bLeft) ? Corners.X : 0.f,
		(bTop && bRight) ? Corners.Y : 0.f,
		(bBottom && bRight) ? Corners.Z : 0.f,
		(bBottom && bLeft) ? Corners.W : 0.f);
}

void SGlassPanel::UpdateBlurStrength()
{
	const float Strength = FMath::Max(0.f, Look.BlurStrength + Response.BlurBoost * HoverAmount.Value);
	for (const FBlurBand& Band : Bands)
	{
		if (Band.Blur.IsValid())
		{
			Band.Blur->SetBlurStrength(Strength * Band.Weight);
		}
	}
}

void SGlassPanel::SetResponse(const FGlassHoverResponse& InResponse, EWidgetMotion InMotion, float InEnterDuration, float InExitDuration)
{
	Response = InResponse;
	Motion = InMotion;
	EnterDuration = InEnterDuration;
	ExitDuration = InExitDuration;
	Surface->SetResponse(Response);
	if (GetBlurLayoutHash() != BlurLayoutHash)
	{
		RebuildBlur();
	}
	UpdateBlurStrength();
}

void SGlassPanel::SetStateSource(TFunction<void(bool& bOutHovered, bool& bOutPressed)> InSource)
{
	StateSource = MoveTemp(InSource);
	if (StateSource && !PollTimer.IsValid())
	{
		PollTimer = RegisterActiveTimer(0.05f, FWidgetActiveTimerDelegate::CreateSP(this, &SGlassPanel::Poll));
	}
	else if (!StateSource && PollTimer.IsValid())
	{
		UnRegisterActiveTimer(PollTimer.ToSharedRef());
		PollTimer.Reset();
	}
}

void SGlassPanel::SetInteractionState(bool bHovered, bool bPressed, bool bInstant)
{
	const float HoverGoal = (bHovered || bPressed) ? 1.f : 0.f;
	const float PressGoal = bPressed ? 1.f : 0.f;
	HoverAmount.SetTarget(HoverGoal, Motion, HoverGoal > HoverAmount.Value ? EnterDuration : ExitDuration, bInstant);
	PressAmount.SetTarget(PressGoal, Motion, PressGoal > PressAmount.Value ? EnterDuration : ExitDuration, bInstant);
	if (bInstant)
	{
		Surface->SetAmounts(HoverAmount.Value, PressAmount.Value);
		UpdateBlurStrength();
		return;
	}
	StartAnimating();
}

EActiveTimerReturnType SGlassPanel::Poll(double InCurrentTime, float InDeltaTime)
{
	if (!StateSource)
	{
		PollTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}
	bool bHovered = false;
	bool bPressed = false;
	StateSource(bHovered, bPressed);
	const float HoverGoal = (bHovered || bPressed) ? 1.f : 0.f;
	const float PressGoal = bPressed ? 1.f : 0.f;
	if (!FMath::IsNearlyEqual(HoverGoal, HoverAmount.Target) || !FMath::IsNearlyEqual(PressGoal, PressAmount.Target))
	{
		SetInteractionState(bHovered, bPressed, false);
	}
	return EActiveTimerReturnType::Continue;
}

void SGlassPanel::StartAnimating()
{
	if (!AnimateTimer.IsValid())
	{
		AnimateTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SGlassPanel::Animate));
	}
}

EActiveTimerReturnType SGlassPanel::Animate(double InCurrentTime, float InDeltaTime)
{
	const bool bHoverMoving = HoverAmount.Advance(InDeltaTime);
	const bool bPressMoving = PressAmount.Advance(InDeltaTime);
	Surface->SetAmounts(HoverAmount.Value, PressAmount.Value);
	UpdateBlurStrength();
	if (bHoverMoving || bPressMoving)
	{
		return EActiveTimerReturnType::Continue;
	}
	AnimateTimer.Reset();
	return EActiveTimerReturnType::Stop;
}

int32 SGlassPanel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// The blur bands round their corners from the panel's size (a pill's radius is half its height).
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	LastSize = FVector2f(static_cast<float>(Size.X), static_cast<float>(Size.Y));
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

// =====================================================================================================
// UGlassPanel
// =====================================================================================================

UGlassPanel::UGlassPanel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Glass = MakePresetLook(EGlassPreset::Frosted);
}

FGlassLook UGlassPanel::MakePresetLook(EGlassPreset InPreset)
{
	using namespace GlassPanelPrivate;

	FGlassLook Look;
	switch (InPreset)
	{
	case EGlassPreset::FrostedDark:
		Look.BlurStrength = 20.f;
		Look.Tint = GradientTint(45.f, { FWidgetShapeGradientStop(0.f, Rgb(30, 33, 42, 0.62f)), FWidgetShapeGradientStop(1.f, Rgb(14, 15, 20, 0.5f)) });
		Look.Grain = 0.06f;
		Look.Sheen = 0.04f;
		Look.SheenReach = 0.5f;
		Look.EdgeColor = FLinearColor(1.f, 1.f, 1.f, 0.07f);
		Look.EdgeHighlight = FLinearColor(1.f, 1.f, 1.f, 0.28f);
		Look.FallbackColor = Rgb(18, 19, 24, 0.94f);
		break;

	case EGlassPreset::Clear:
		Look.BlurStrength = 3.f;
		Look.Tint = FlatTint(FLinearColor(1.f, 1.f, 1.f, 0.03f));
		Look.Grain = 0.f;
		Look.Sheen = 0.22f;
		Look.SheenReach = 0.35f;
		Look.EdgeColor = FLinearColor(1.f, 1.f, 1.f, 0.14f);
		Look.EdgeHighlight = FLinearColor(1.f, 1.f, 1.f, 0.75f);
		Look.EdgeBacklight = FLinearColor(1.f, 1.f, 1.f, 0.3f);
		Look.EdgeFocus = 2.5f;
		Look.InnerGlow = 0.1f;
		Look.InnerGlowSize = 6.f;
		Look.FallbackColor = FLinearColor(0.6f, 0.62f, 0.66f, 0.5f);
		break;

	case EGlassPreset::Liquid:
		Look.BlurStrength = 7.f;
		Look.Tint = GradientTint(90.f, { FWidgetShapeGradientStop(0.f, FLinearColor(1.f, 1.f, 1.f, 0.12f)), FWidgetShapeGradientStop(0.5f, FLinearColor(1.f, 1.f, 1.f, 0.04f)), FWidgetShapeGradientStop(1.f, FLinearColor(1.f, 1.f, 1.f, 0.08f)) });
		Look.Grain = 0.f;
		Look.LightAngle = -35.f;
		Look.Sheen = 0.2f;
		Look.SheenReach = 0.45f;
		Look.EdgeWidth = 1.5f;
		Look.EdgeColor = FLinearColor(1.f, 1.f, 1.f, 0.1f);
		Look.EdgeHighlight = FLinearColor(1.f, 1.f, 1.f, 0.9f);
		Look.EdgeBacklight = FLinearColor(1.f, 1.f, 1.f, 0.5f);
		Look.EdgeFocus = 3.f;
		Look.InnerGlow = 0.35f;
		Look.InnerGlowSize = 12.f;
		Look.FallbackColor = FLinearColor(0.55f, 0.58f, 0.64f, 0.6f);
		break;

	case EGlassPreset::Acrylic:
		Look.BlurStrength = 32.f;
		Look.Tint = FlatTint(Rgb(32, 32, 36, 0.72f));
		Look.Grain = 0.03f;
		Look.Sheen = 0.f;
		Look.EdgeColor = FLinearColor(1.f, 1.f, 1.f, 0.07f);
		Look.EdgeHighlight = FLinearColor(1.f, 1.f, 1.f, 0.08f);
		Look.FallbackColor = Rgb(32, 32, 36, 0.96f);
		break;

	case EGlassPreset::Smoked:
		Look.BlurStrength = 8.f;
		Look.Tint = GradientTint(90.f, { FWidgetShapeGradientStop(0.f, FLinearColor(0.f, 0.f, 0.f, 0.5f)), FWidgetShapeGradientStop(1.f, FLinearColor(0.f, 0.f, 0.f, 0.38f)) });
		Look.Grain = 0.02f;
		Look.Sheen = 0.05f;
		Look.SheenReach = 0.4f;
		Look.EdgeColor = FLinearColor(1.f, 1.f, 1.f, 0.06f);
		Look.EdgeHighlight = FLinearColor(1.f, 1.f, 1.f, 0.22f);
		Look.FallbackColor = FLinearColor(0.f, 0.f, 0.f, 0.7f);
		break;

	case EGlassPreset::Tinted:
		Look.BlurStrength = 16.f;
		Look.Tint = GradientTint(45.f, { FWidgetShapeGradientStop(0.f, Rgb(61, 139, 255, 0.38f)), FWidgetShapeGradientStop(1.f, Rgb(40, 90, 220, 0.22f)) });
		Look.Grain = 0.04f;
		Look.Sheen = 0.1f;
		Look.SheenReach = 0.5f;
		Look.EdgeColor = Rgb(120, 180, 255, 0.25f);
		Look.EdgeHighlight = Rgb(190, 220, 255, 0.7f);
		Look.InnerGlow = 0.25f;
		Look.InnerGlowSize = 14.f;
		Look.InnerGlowColor = Rgb(120, 180, 255, 1.f);
		Look.FallbackColor = Rgb(30, 70, 160, 0.9f);
		break;

	case EGlassPreset::ProgressiveTop:
	case EGlassPreset::ProgressiveBottom:
	{
		const bool bTop = InPreset == EGlassPreset::ProgressiveTop;
		Look.BlurMode = EGlassBlurMode::Progressive;
		Look.BlurStrength = 24.f;
		Look.StrongEdge = bTop ? EGlassEdge::Top : EGlassEdge::Bottom;
		Look.FadeStart = 0.2f;
		Look.FadeEnd = 1.f;
		Look.FadeSteps = 8;
		Look.Tint = GradientTint(bTop ? 90.f : -90.f, { FWidgetShapeGradientStop(0.f, FLinearColor(0.f, 0.f, 0.f, 0.45f)), FWidgetShapeGradientStop(1.f, FLinearColor(0.f, 0.f, 0.f, 0.f)) });
		Look.Grain = 0.f;
		Look.Sheen = 0.f;
		Look.EdgeWidth = 0.f;
		Look.FallbackColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);
		break;
	}

	case EGlassPreset::Frosted:
	case EGlassPreset::Custom:
	default:
		Look.BlurStrength = 14.f;
		Look.Tint = GradientTint(45.f, { FWidgetShapeGradientStop(0.f, FLinearColor(1.f, 1.f, 1.f, 0.26f)), FWidgetShapeGradientStop(1.f, FLinearColor(1.f, 1.f, 1.f, 0.08f)) });
		Look.Grain = 0.05f;
		Look.Sheen = 0.08f;
		Look.SheenReach = 0.55f;
		Look.EdgeColor = FLinearColor(1.f, 1.f, 1.f, 0.16f);
		Look.EdgeHighlight = FLinearColor(1.f, 1.f, 1.f, 0.55f);
		Look.FallbackColor = FLinearColor(0.82f, 0.85f, 0.9f, 0.85f);
		break;
	}
	return Look;
}

void UGlassPanel::SetPreset(EGlassPreset InPreset)
{
	Preset = InPreset;
	if (Preset != EGlassPreset::Custom)
	{
		Glass = MakePresetLook(Preset);
	}
	PushLook();
}

void UGlassPanel::SetLook(const FGlassLook& InLook)
{
	Glass = InLook;
	Preset = EGlassPreset::Custom;
	PushLook();
}

void UGlassPanel::SetBlurStrength(float InStrength)
{
	Glass.BlurStrength = FMath::Max(0.f, InStrength);
	Preset = EGlassPreset::Custom;
	PushLook();
}

void UGlassPanel::SetTintColor(FLinearColor InColor)
{
	Glass.Tint.Type = EWidgetShapePaintType::Color;
	Glass.Tint.Color = InColor;
	Preset = EGlassPreset::Custom;
	PushLook();
}

void UGlassPanel::SetCornerRadius(float InRadius)
{
	CornerRadius = FMath::Max(0.f, InRadius);
	bFullyRounded = false;
	bIndividualCorners = false;
	PushLook();
}

void UGlassPanel::SetInteractionState(bool bHovered, bool bPressed, bool bInstant)
{
	if (MyGlass.IsValid())
	{
		MyGlass->SetInteractionState(bHovered, bPressed, bInstant);
	}
}

FVector4f UGlassPanel::GetCornerRadiiSetting() const
{
	if (bIndividualCorners)
	{
		return FVector4f(static_cast<float>(CornerRadii.X), static_cast<float>(CornerRadii.Y), static_cast<float>(CornerRadii.Z), static_cast<float>(CornerRadii.W));
	}
	return FVector4f(CornerRadius, CornerRadius, CornerRadius, CornerRadius);
}

FVector4f UGlassPanel::GetCornerRadiiAt(const FVector2f& Size) const
{
	if (bFullyRounded)
	{
		const float Half = FMath::Min(Size.X, Size.Y) * 0.5f;
		return FVector4f(Half, Half, Half, Half);
	}
	return WidgetEffectsDraw::FitRadii(GetCornerRadiiSetting(), Size);
}

UButton* UGlassPanel::FindButton() const
{
	for (UWidget* Above = GetParent(); Above; Above = Above->GetParent())
	{
		if (UButton* Button = Cast<UButton>(Above))
		{
			return Button;
		}
	}
	return nullptr;
}

TSharedRef<SWidget> UGlassPanel::RebuildWidget()
{
	MyGlass = SNew(SGlassPanel);
	if (GetChildrenCount() > 0)
	{
		if (UWidget* Child = GetContentSlot()->Content)
		{
			MyGlass->SetContent(Child->TakeWidget());
		}
	}
	PushLook();
	PushReaction();
	return MyGlass.ToSharedRef();
}

void UGlassPanel::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	PushLook();
	PushReaction();
}

void UGlassPanel::PushLook()
{
	if (MyGlass.IsValid())
	{
		MyGlass->SetContentLayout(ContentPadding, HorizontalAlignment, VerticalAlignment);
		MyGlass->SetLook(Glass, GetCornerRadiiSetting(), bFullyRounded);
	}
}

void UGlassPanel::PushReaction()
{
	if (!MyGlass.IsValid())
	{
		return;
	}
	MyGlass->SetResponse(HoverResponse, Motion, EnterDuration, ExitDuration);

	// Never polled in the designer: nothing there hovers, and an idle editor shouldn't wake for it.
	if (IsDesignTime() || Reaction == EGlassReaction::None || Reaction == EGlassReaction::Manual)
	{
		MyGlass->SetStateSource(nullptr);
		if (Reaction == EGlassReaction::None)
		{
			MyGlass->SetInteractionState(false, false, true);
		}
		return;
	}

	if (Reaction == EGlassReaction::NearestButton)
	{
		const TWeakObjectPtr<UButton> WeakButton = FindButton();
		if (!WeakButton.IsValid())
		{
			MyGlass->SetStateSource(nullptr);
			return;
		}
		MyGlass->SetStateSource([WeakButton](bool& bOutHovered, bool& bOutPressed)
		{
			const UButton* Button = WeakButton.Get();
			bOutHovered = Button && Button->GetIsEnabled() && (Button->IsHovered() || Button->HasAnyUserFocus());
			bOutPressed = Button && Button->GetIsEnabled() && Button->IsPressed();
		});
		return;
	}

	const TWeakPtr<SGlassPanel> WeakGlass = MyGlass;
	MyGlass->SetStateSource([WeakGlass](bool& bOutHovered, bool& bOutPressed)
	{
		const TSharedPtr<SGlassPanel> Pinned = WeakGlass.Pin();
		bOutHovered = Pinned.IsValid() && Pinned->IsHovered();
		bOutPressed = false;
	});
}

void UGlassPanel::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyGlass.Reset();
}

void UGlassPanel::OnSlotAdded(UPanelSlot* InSlot)
{
	if (MyGlass.IsValid() && InSlot && InSlot->Content)
	{
		MyGlass->SetContent(InSlot->Content->TakeWidget());
	}
}

void UGlassPanel::OnSlotRemoved(UPanelSlot* InSlot)
{
	if (MyGlass.IsValid())
	{
		MyGlass->SetContent(SNullWidget::NullWidget);
	}
}

#if WITH_EDITOR
const FText UGlassPanel::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}

void UGlassPanel::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName ChangedName = PropertyChangedEvent.GetPropertyName();
	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
	if (ChangedName == GET_MEMBER_NAME_CHECKED(UGlassPanel, Preset))
	{
		if (Preset != EGlassPreset::Custom)
		{
			Glass = MakePresetLook(Preset);
		}
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UGlassPanel, Glass))
	{
		Preset = EGlassPreset::Custom;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

#undef LOCTEXT_NAMESPACE
