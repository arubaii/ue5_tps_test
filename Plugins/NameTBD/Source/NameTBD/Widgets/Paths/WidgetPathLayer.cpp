// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetPathLayer.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Algo/Reverse.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "NameTBD/WidgetAnimation/WidgetAnimInput.h"
#include "NameTBD/Widgets/WidgetEffectsDraw.h"
#include "NameTBD/Widgets/WidgetTreeLookup.h"
#include "NameTBD/Widgets/Shadow/DropShadow.h"
#include "NameTBD/Widgets/Shapes/ShapeWidgets.h"

#define LOCTEXT_NAMESPACE "WidgetPathLayer"

// =====================================================================================================
// Drawing helpers
// =====================================================================================================

namespace WidgetPathLayerPrivate
{
	/** Lines are cut into runs no longer than this (Slate units), so colors given per vertex - gradients, pulses - read smoothly. */
	constexpr float ResampleStep = 6.f;

	/** The passes every piece of line is drawn in, each on its own layer, so every glow is under every line of a draw order. */
	enum EPass : int32
	{
		PassGlow = 0,
		PassOutline,
		PassLine,
		PassFlow,
		PassMarkers,
		PassCount
	};

	/** The breathing multiplier of a look at Now: 1 - Breathe at the bottom of a breath, 1 at the top. */
	float Breath(const FWidgetPathLook& Look, double Now)
	{
		if (Look.Breathe <= 0.f)
		{
			return 1.f;
		}
		const float Phase = static_cast<float>(FMath::Fmod(Now * FMath::Max(Look.BreatheRate, 0.01f), 1.0));
		const float Wave = 0.5f + 0.5f * FMath::Cos(Phase * UE_TWO_PI);
		return 1.f - Look.Breathe * Wave;
	}

	/** Wraps Value into [0, Period). */
	float Wrap(double Value, float Period)
	{
		if (Period <= KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		float Wrapped = static_cast<float>(FMath::Fmod(Value, static_cast<double>(Period)));
		if (Wrapped < 0.f)
		{
			Wrapped += Period;
		}
		return Wrapped;
	}

	void AppendMesh(FWidgetShapeMesh& Into, const FWidgetShapeMesh& From, float CoverageScale)
	{
		const uint32 Base = static_cast<uint32>(Into.Vertices.Num());
		for (const FWidgetShapeMeshVertex& Vertex : From.Vertices)
		{
			FWidgetShapeMeshVertex& Added = Into.Vertices.Add_GetRef(Vertex);
			Added.Coverage *= CoverageScale;
		}
		for (const uint32 Index : From.Indices)
		{
			Into.Indices.Add(Base + Index);
		}
	}

	/** A filled polygon, anti-aliased. */
	void BuildPolygon(const TArray<FVector2f>& Points, float AntiAlias, FWidgetShapeMesh& Out)
	{
		FWidgetShapeOutline Outline;
		FWidgetShapeLine& Line = Outline.Lines.AddDefaulted_GetRef();
		Line.Points = Points;
		Line.bClosed = true;
		TArray<int8> FillOnLeft;
		WidgetShapeGeometry::BuildFill(Outline, AntiAlias, Out, FillOnLeft);
	}

	/** A stroked open line - dashed and trimmed as asked. */
	void BuildPolyline(const TArray<FVector2f>& Points, float Width, EWidgetShapeLineCap Cap, EWidgetShapeLineJoin Join, float AntiAlias, float Tolerance,
		FWidgetShapeMesh& Out, float DashLength = 0.f, float DashGap = 0.f, float DashOffset = 0.f, float TrimStart = 0.f, float TrimEnd = 1.f)
	{
		if (Points.Num() < 2 || Width <= 0.f || TrimEnd <= TrimStart)
		{
			return;
		}
		FWidgetShapeOutline Outline;
		FWidgetShapeLine& Line = Outline.Lines.AddDefaulted_GetRef();
		Line.Points = Points;
		Line.bClosed = false;

		FWidgetShapeStroke Stroke;
		Stroke.bEnabled = true;
		Stroke.Width = Width;
		Stroke.Align = EWidgetShapeStrokeAlign::Center;
		Stroke.Join = Join;
		Stroke.MiterLimit = 4.f;
		Stroke.Cap = Cap;
		Stroke.DashLength = DashLength;
		Stroke.DashGap = DashGap;
		Stroke.DashOffset = DashOffset;

		FWidgetShapeTrim Trim;
		Trim.Start = FMath::Clamp(TrimStart, 0.f, 1.f);
		Trim.End = FMath::Clamp(TrimEnd, 0.f, 1.f);

		TArray<int8> FillOnLeft;
		FillOnLeft.Add(0);
		WidgetShapeGeometry::BuildStroke(Outline, FillOnLeft, Stroke, Trim, AntiAlias, Tolerance, Out);
	}

	TArray<FVector2f> MakeCircle(const FVector2f& Center, float Radius, int32 Segments)
	{
		TArray<FVector2f> Points;
		Points.Reserve(Segments);
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const float Angle = UE_TWO_PI * static_cast<float>(Index) / Segments;
			Points.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Points;
	}

	/** How far a marker pulls its end of the line back, so the line doesn't poke through an arrowhead's tip. */
	float MarkerInset(EWidgetPathMarker Marker, float Size, float Width)
	{
		switch (Marker)
		{
		case EWidgetPathMarker::Arrow: return Size * 0.75f;
		case EWidgetPathMarker::OpenArrow: return Width * 0.5f;
		case EWidgetPathMarker::Diamond: return Size * 0.5f;
		default: return 0.f;
		}
	}

	/** A marker at Tip, pointing Direction (outward, away from the line). */
	void BuildMarker(EWidgetPathMarker Marker, const FVector2f& Tip, const FVector2f& Direction, float Size, float LineWidth, float AntiAlias, float Tolerance, FWidgetShapeMesh& Out)
	{
		const FVector2f Across(-Direction.Y, Direction.X);
		switch (Marker)
		{
		case EWidgetPathMarker::Arrow:
		{
			const FVector2f Base = Tip - Direction * Size;
			TArray<FVector2f> Points = { Tip, Base + Across * (Size * 0.5f), Base - Across * (Size * 0.5f) };
			BuildPolygon(Points, AntiAlias, Out);
			break;
		}
		case EWidgetPathMarker::OpenArrow:
		{
			const FVector2f Base = Tip - Direction * Size;
			TArray<FVector2f> Points = { Base + Across * (Size * 0.55f), Tip, Base - Across * (Size * 0.55f) };
			BuildPolyline(Points, FMath::Max(LineWidth, 1.5f), EWidgetShapeLineCap::Round, EWidgetShapeLineJoin::Round, AntiAlias, Tolerance, Out);
			break;
		}
		case EWidgetPathMarker::Dot:
			BuildPolygon(MakeCircle(Tip, Size * 0.5f, 20), AntiAlias, Out);
			break;
		case EWidgetPathMarker::Diamond:
		{
			const FVector2f Middle = Tip - Direction * (Size * 0.5f);
			TArray<FVector2f> Points = { Tip, Middle + Across * (Size * 0.38f), Middle - Direction * (Size * 0.5f), Middle - Across * (Size * 0.38f) };
			BuildPolygon(Points, AntiAlias, Out);
			break;
		}
		case EWidgetPathMarker::Bar:
		{
			TArray<FVector2f> Points = { Tip + Across * (Size * 0.5f), Tip - Across * (Size * 0.5f) };
			BuildPolyline(Points, FMath::Max(LineWidth, 2.f), EWidgetShapeLineCap::Butt, EWidgetShapeLineJoin::Miter, AntiAlias, Tolerance, Out);
			break;
		}
		default:
			break;
		}
	}

	uint32 HashBox(const FWidgetPathNodeBox& Box)
	{
		uint32 Hash = GetTypeHash(Box.Center);
		Hash = HashCombineFast(Hash, GetTypeHash(Box.HalfSize));
		Hash = HashCombineFast(Hash, WidgetEffectsDraw::HashRadii(Box.Radii));
		Hash = HashCombineFast(Hash, GetTypeHash(Box.bEllipse) | (GetTypeHash(Box.bPoint) << 1));
		return Hash;
	}

	uint32 HashTransform(const FSlateRenderTransform& Transform)
	{
		float A, B, C, D;
		Transform.GetMatrix().GetMatrix(A, B, C, D);
		uint32 Hash = GetTypeHash(Transform.GetTranslation());
		Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(A), GetTypeHash(B)));
		return HashCombineFast(Hash, HashCombineFast(GetTypeHash(C), GetTypeHash(D)));
	}
}

// =====================================================================================================
// SWidgetPathLayer
// =====================================================================================================

void SWidgetPathLayer::Construct(const FArguments& InArgs)
{
	SetCanTick(false);
}

void SWidgetPathLayer::SetLooks(const TArray<FWidgetPathLook>& InLooks)
{
	Looks = InLooks;
	for (FEdgeState& State : Edges)
	{
		State.Caches.Reset();
	}
	SVGBrushes.Reset();
	Invalidate(EInvalidateWidgetReason::Paint);
	Wake();
}

void SWidgetPathLayer::SetAnimation(const FWidgetPathSlateAnimation& InAnimation)
{
	Animation = InAnimation;
}

void SWidgetPathLayer::SetTickCallback(TFunction<void(float)> InCallback)
{
	TickCallback = MoveTemp(InCallback);
	Wake();
}

void SWidgetPathLayer::SetEdges(const TArray<FWidgetPathSlateEdge>& InEdges, bool bAnimate)
{
	const bool bAnimateChanges = bAnimate && Animation.Transition != EWidgetPathTransition::Instant && Animation.TransitionDuration > 0.f;

	TArray<FEdgeState> NewEdges;
	NewEdges.Reserve(InEdges.Num());
	TMap<uint64, int32> NewByKey;
	TSet<int32> Taken;
	for (const FWidgetPathSlateEdge& Edge : InEdges)
	{
		if (NewByKey.Contains(Edge.Key))
		{
			continue;
		}

		FEdgeState State;
		const int32* Old = EdgeByKey.Find(Edge.Key);
		const bool bKnown = Old && Edges.IsValidIndex(*Old) && !Taken.Contains(*Old);
		if (bKnown)
		{
			Taken.Add(*Old);
			State = MoveTemp(Edges[*Old]);
		}

		const int32 WasShown = State.ShownLook;
		State.Edge = Edge;
		if (!bKnown)
		{
			State.ShownLook = Edge.Look;
			State.PreviousLook = INDEX_NONE;
			State.ShownProgress.Snap(Edge.Progress);
			State.Intro.Snap(1.f);
			if (bAnimateChanges && Edge.Look != INDEX_NONE)
			{
				// A line added while shown draws itself on from its first node.
				State.bChangeFromStart = true;
				State.Change.Snap(0.f);
				State.Change.SetTarget(1.f, Animation.Motion, Animation.TransitionDuration);
			}
			else
			{
				State.Change.Snap(1.f);
			}
		}
		else
		{
			if (Edge.Look != WasShown)
			{
				if (bAnimateChanges && WasShown != INDEX_NONE && Edge.Look != INDEX_NONE)
				{
					State.PreviousLook = WasShown;
					State.bChangeFromStart = Edge.bChangeFromStart;
					State.Change.Snap(0.f);
					State.Change.SetTarget(1.f, Animation.Motion, Animation.TransitionDuration);
				}
				else
				{
					State.PreviousLook = INDEX_NONE;
					State.Change.Snap(1.f);
				}
				State.ShownLook = Edge.Look;
			}
			if (bAnimate && Animation.TransitionDuration > 0.f)
			{
				State.ShownProgress.SetTarget(Edge.Progress, Animation.Motion, Animation.TransitionDuration);
			}
			else
			{
				State.ShownProgress.Snap(Edge.Progress);
			}
		}
		NewByKey.Add(Edge.Key, NewEdges.Add(MoveTemp(State)));
	}

	Edges = MoveTemp(NewEdges);
	EdgeByKey = MoveTemp(NewByKey);
	Invalidate(EInvalidateWidgetReason::Paint);
	Wake();
}

bool SWidgetPathLayer::GetRoute(uint64 Key, TArray<FVector2f>& OutPoints) const
{
	OutPoints.Reset();
	const int32* Found = EdgeByKey.Find(Key);
	if (!Found || !Edges.IsValidIndex(*Found) || Edges[*Found].Points.Num() < 2)
	{
		return false;
	}
	OutPoints = Edges[*Found].Points;
	return true;
}

void SWidgetPathLayer::PlayIntro()
{
	if (Animation.Intro == EWidgetPathIntro::None)
	{
		return;
	}
	for (FEdgeState& State : Edges)
	{
		State.Intro.Snap(0.f);
		State.bIntroWaiting = true;
		State.IntroDelay = State.Edge.Depth * Animation.IntroStagger;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
	Wake();
}

void SWidgetPathLayer::Wake()
{
	if (!TickTimer.IsValid())
	{
		TickTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SWidgetPathLayer::OnActiveTick));
	}
}

bool SWidgetPathLayer::IsAnimating() const
{
	for (const FEdgeState& State : Edges)
	{
		if (State.bIntroWaiting || State.Intro.IsMoving() || State.Change.IsMoving() || State.ShownProgress.IsMoving())
		{
			return true;
		}
		const int32 Shown[3] = { State.ShownLook, State.PreviousLook, State.Edge.ProgressLook };
		for (const int32 Look : Shown)
		{
			if (Looks.IsValidIndex(Look) && Looks[Look].IsAnimated())
			{
				return true;
			}
		}
	}
	return false;
}

uint32 SWidgetPathLayer::HashNodePlacement() const
{
	using namespace WidgetPathLayerPrivate;

	uint32 Hash = 0;
	auto Add = [&Hash](const TWeakPtr<SWidget>& Weak)
	{
		if (const TSharedPtr<SWidget> Widget = Weak.Pin())
		{
			const FGeometry& Geometry = Widget->GetPaintSpaceGeometry();
			Hash = HashCombineFast(Hash, HashTransform(Geometry.GetAccumulatedRenderTransform()));
			const FVector2D Size = Geometry.GetLocalSize();
			Hash = HashCombineFast(Hash, GetTypeHash(Size));
			Hash = HashCombineFast(Hash, GetTypeHash(Widget->GetVisibility().IsVisible()));
		}
	};
	for (const FEdgeState& State : Edges)
	{
		Add(State.Edge.From);
		Add(State.Edge.To);
		Add(State.Edge.OrbitCenter);
	}
	return Hash;
}

EActiveTimerReturnType SWidgetPathLayer::OnActiveTick(double InCurrentTime, float InDeltaTime)
{
	if (TickCallback && !bDesignTime)
	{
		// May set new edges or looks (a hover lights a route): copy the callback, the layer may be told anything.
		const TFunction<void(float)> Callback = TickCallback;
		Callback(InDeltaTime);
	}

	bool bMoving = false;
	for (FEdgeState& State : Edges)
	{
		if (State.bIntroWaiting)
		{
			State.IntroDelay -= InDeltaTime;
			if (State.IntroDelay <= 0.f)
			{
				State.bIntroWaiting = false;
				State.Intro.SetTarget(1.f, Animation.Motion, Animation.IntroDuration, Animation.IntroDuration <= 0.f);
			}
			bMoving = true;
		}
		bMoving |= State.Intro.Advance(InDeltaTime);
		const bool bChanging = State.Change.Advance(InDeltaTime);
		bMoving |= bChanging;
		if (!bChanging && State.Change.Value >= 1.f)
		{
			State.PreviousLook = INDEX_NONE;
		}
		bMoving |= State.ShownProgress.Advance(InDeltaTime);
	}

	const uint32 Placement = HashNodePlacement();
	const bool bPlaced = Placement != LastPlacementHash;
	LastPlacementHash = Placement;

	if (bMoving || bPlaced || IsAnimating())
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	// Kept running while there are lines (their nodes may move at any time) or polling to do.
	if (Edges.Num() > 0 || (TickCallback && !bDesignTime))
	{
		return EActiveTimerReturnType::Continue;
	}
	TickTimer.Reset();
	return EActiveTimerReturnType::Stop;
}

bool SWidgetPathLayer::MeasureNode(const TWeakPtr<SWidget>& Node, const FWidgetPathNodeShapeInfo& Shape, const FGeometry& AllottedGeometry, FWidgetPathNodeBox& Out) const
{
	Out = FWidgetPathNodeBox();
	const TSharedPtr<SWidget> Widget = Node.Pin();
	if (!Widget.IsValid() || !Widget->GetVisibility().IsVisible())
	{
		return false;
	}

	// Where the node was last painted, in this layer's space: its layout, render transforms and anything above it included.
	const FGeometry& NodeGeometry = Widget->GetPaintSpaceGeometry();
	const FVector2D LocalSize = NodeGeometry.GetLocalSize();
	if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
	{
		return false;
	}
	const FSlateRenderTransform ToLayer = Concatenate(NodeGeometry.GetAccumulatedRenderTransform(), AllottedGeometry.GetAccumulatedRenderTransform().Inverse());
	const FVector2f Size(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	const FVector2f Corners[4] =
	{
		ToLayer.TransformPoint(FVector2f(0.f, 0.f)),
		ToLayer.TransformPoint(FVector2f(Size.X, 0.f)),
		ToLayer.TransformPoint(FVector2f(0.f, Size.Y)),
		ToLayer.TransformPoint(Size)
	};
	FVector2f Min = Corners[0];
	FVector2f Max = Corners[0];
	for (const FVector2f& Corner : Corners)
	{
		Min = FVector2f(FMath::Min(Min.X, Corner.X), FMath::Min(Min.Y, Corner.Y));
		Max = FVector2f(FMath::Max(Max.X, Corner.X), FMath::Max(Max.Y, Corner.Y));
	}

	Out.Center = (Min + Max) * 0.5f;
	Out.HalfSize = (Max - Min) * 0.5f;
	Out.bEllipse = Shape.bEllipse;
	Out.bPoint = Shape.bPoint;
	if (Shape.bPill)
	{
		const float Half = FMath::Min(Out.HalfSize.X, Out.HalfSize.Y);
		Out.Radii = FVector4f(Half, Half, Half, Half);
	}
	else
	{
		const float Scale = ToLayer.TransformVector(FVector2f(1.f, 0.f)).Size();
		Out.Radii = WidgetEffectsDraw::FitRadii(Shape.Radii * Scale, Out.HalfSize * 2.f);
	}
	Out.bValid = true;
	return true;
}

void SWidgetPathLayer::UpdateRoutes(const FGeometry& AllottedGeometry, float Tolerance) const
{
	using namespace WidgetPathLayerPrivate;

	struct FMeasured
	{
		FWidgetPathRouteInput Input;
		EWidgetPathSide FromSide = EWidgetPathSide::Center;
		EWidgetPathSide ToSide = EWidgetPathSide::Center;
		bool bValid = false;
	};
	TArray<FMeasured> Measured;
	Measured.SetNum(Edges.Num());

	for (int32 Index = 0; Index < Edges.Num(); ++Index)
	{
		const FWidgetPathSlateEdge& Edge = Edges[Index].Edge;
		FMeasured& Entry = Measured[Index];
		Entry.Input.Routing = Edge.Routing;
		if (!MeasureNode(Edge.From, Edge.FromShape, AllottedGeometry, Entry.Input.From) || !MeasureNode(Edge.To, Edge.ToShape, AllottedGeometry, Entry.Input.To))
		{
			continue;
		}
		Entry.Input.OrbitCenter = FVector2f(static_cast<float>(Edge.Routing.OrbitPoint.X), static_cast<float>(Edge.Routing.OrbitPoint.Y));
		FWidgetPathNodeBox CenterBox;
		if (Edge.Routing.Route == EWidgetPathRoute::Orbit && MeasureNode(Edge.OrbitCenter, FWidgetPathNodeShapeInfo(), AllottedGeometry, CenterBox))
		{
			Entry.Input.OrbitCenter = CenterBox.Center;
		}
		WidgetPathGeometry::ResolveSides(Entry.Input, Entry.FromSide, Entry.ToSide);
		Entry.bValid = true;
	}

	// Spread ports: lines on the same side of the same node, in order along that side by where their other end is.
	struct FPortUse
	{
		int32 Edge = INDEX_NONE;
		bool bFromEnd = true;
		float Along = 0.f;
	};
	TMap<TPair<const SWidget*, uint8>, TArray<FPortUse>> Ports;
	for (int32 Index = 0; Index < Edges.Num(); ++Index)
	{
		const FMeasured& Entry = Measured[Index];
		if (!Entry.bValid || !Entry.Input.Routing.bSpreadPorts)
		{
			continue;
		}
		auto Note = [&](const TWeakPtr<SWidget>& Node, EWidgetPathSide Side, bool bFromEnd, const FVector2f& Other)
		{
			const FVector2f Normal = WidgetPathGeometry::SideNormal(Side);
			if (Normal.IsNearlyZero())
			{
				return;
			}
			const FVector2f Along(FMath::Abs(Normal.Y), FMath::Abs(Normal.X));
			FPortUse Use;
			Use.Edge = Index;
			Use.bFromEnd = bFromEnd;
			Use.Along = FVector2f::DotProduct(Other, Along);
			Ports.FindOrAdd(TPair<const SWidget*, uint8>(Node.Pin().Get(), static_cast<uint8>(Side))).Add(Use);
		};
		Note(Edges[Index].Edge.From, Entry.FromSide, true, Entry.Input.To.Center);
		Note(Edges[Index].Edge.To, Entry.ToSide, false, Entry.Input.From.Center);
	}
	for (TPair<TPair<const SWidget*, uint8>, TArray<FPortUse>>& Port : Ports)
	{
		TArray<FPortUse>& Uses = Port.Value;
		if (Uses.Num() < 2)
		{
			continue;
		}
		Uses.Sort([](const FPortUse& A, const FPortUse& B) { return A.Along < B.Along; });
		for (int32 PortIndex = 0; PortIndex < Uses.Num(); ++PortIndex)
		{
			FMeasured& Entry = Measured[Uses[PortIndex].Edge];
			const float Offset = (static_cast<float>(PortIndex) - (Uses.Num() - 1) * 0.5f) * Entry.Input.Routing.PortSpacing;
			if (Uses[PortIndex].bFromEnd)
			{
				Entry.Input.FromPortOffset = Offset;
			}
			else
			{
				Entry.Input.ToPortOffset = Offset;
			}
		}
	}

	for (int32 Index = 0; Index < Edges.Num(); ++Index)
	{
		FEdgeState& State = Edges[Index];
		const FMeasured& Entry = Measured[Index];
		if (!Entry.bValid)
		{
			State.Points.Reset();
			State.Length = 0.f;
			State.RouteHash = 0;
			continue;
		}

		uint32 Hash = HashCombineFast(HashBox(Entry.Input.From), HashBox(Entry.Input.To));
		Hash = HashCombineFast(Hash, Entry.Input.Routing.GetHash());
		Hash = HashCombineFast(Hash, GetTypeHash(Entry.Input.OrbitCenter));
		Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(Entry.Input.FromPortOffset), GetTypeHash(Entry.Input.ToPortOffset)));
		Hash = HashCombineFast(Hash, GetTypeHash(Tolerance));
		Hash = Hash == 0 ? 1 : Hash;
		if (Hash == State.RouteHash)
		{
			continue;
		}

		State.RouteHash = Hash;
		WidgetPathGeometry::BuildRoute(Entry.Input, Tolerance, State.Points);
		WidgetPathGeometry::Resample(State.Points, ResampleStep);
		State.Length = WidgetShapeGeometry::GetLength(State.Points, /*bClosed=*/false);
		State.Caches.Reset();
	}
}

void SWidgetPathLayer::GatherPieces(TArray<FDrawPiece>& Out) const
{
	Out.Reset();
	for (int32 Index = 0; Index < Edges.Num(); ++Index)
	{
		const FEdgeState& State = Edges[Index];
		if (State.Points.Num() < 2 || State.Length <= 0.f)
		{
			continue;
		}

		const float IntroValue = State.bIntroWaiting ? 0.f : FMath::Clamp(State.Intro.Value, 0.f, 1.f);
		if (IntroValue <= 0.f)
		{
			continue;
		}
		const float IntroEnd = Animation.Intro == EWidgetPathIntro::DrawOn ? IntroValue : 1.f;
		const float IntroOpacity = Animation.Intro == EWidgetPathIntro::Fade ? IntroValue : 1.f;

		auto Add = [&](int32 Look, float Start, float End, float Opacity, int32 Slot)
		{
			End = FMath::Min(End, IntroEnd);
			if (!Looks.IsValidIndex(Look) || End - Start <= 1e-4f || Opacity * IntroOpacity <= 0.002f)
			{
				return;
			}
			FDrawPiece& Piece = Out.AddDefaulted_GetRef();
			Piece.Edge = Index;
			Piece.Look = Look;
			Piece.Start = Start;
			Piece.End = End;
			Piece.Opacity = Opacity * IntroOpacity;
			Piece.Slot = Slot;
		};

		const float Change = FMath::Clamp(State.Change.Value, 0.f, 1.f);
		const bool bChanging = Change < 1.f;
		if (bChanging && State.PreviousLook != INDEX_NONE)
		{
			if (Animation.Transition == EWidgetPathTransition::Fade)
			{
				Add(State.PreviousLook, 0.f, 1.f, 1.f - Change, 0);
				Add(State.ShownLook, 0.f, 1.f, Change, 1);
			}
			else if (State.bChangeFromStart)
			{
				Add(State.PreviousLook, Change, 1.f, 1.f, 0);
				Add(State.ShownLook, 0.f, Change, 1.f, 1);
			}
			else
			{
				Add(State.PreviousLook, 0.f, 1.f - Change, 1.f, 0);
				Add(State.ShownLook, 1.f - Change, 1.f, 1.f, 1);
			}
		}
		else if (bChanging)
		{
			// A new line drawing itself on (or fading in).
			if (Animation.Transition == EWidgetPathTransition::Fade)
			{
				Add(State.ShownLook, 0.f, 1.f, Change, 1);
			}
			else
			{
				Add(State.ShownLook, 0.f, Change, 1.f, 1);
			}
		}
		else
		{
			Add(State.ShownLook, 0.f, 1.f, 1.f, 1);
		}

		const float Progress = FMath::Clamp(State.ShownProgress.Value, 0.f, 1.f);
		if (Progress > 0.001f)
		{
			Add(State.Edge.ProgressLook, 0.f, Progress, 1.f, 2);
		}
	}

	// Lower draw orders first; the order otherwise kept.
	Out.StableSort([this](const FDrawPiece& A, const FDrawPiece& B)
	{
		return Looks[A.Look].DrawOrder < Looks[B.Look].DrawOrder;
	});
}

FWidgetShapeMesh& SWidgetPathLayer::GetCache(FEdgeState& State, int32 Slot, uint32 Hash, bool& bOutStale)
{
	for (FMeshCache& Cache : State.Caches)
	{
		if (Cache.Slot == Slot)
		{
			bOutStale = Cache.Hash != Hash;
			if (bOutStale)
			{
				Cache.Hash = Hash;
				Cache.Mesh.Reset();
			}
			return Cache.Mesh;
		}
	}
	FMeshCache& Added = State.Caches.AddDefaulted_GetRef();
	Added.Slot = Slot;
	Added.Hash = Hash;
	bOutStale = true;
	return Added.Mesh;
}

void SWidgetPathLayer::PaintPiece(const FDrawPiece& Piece, int32 Pass, const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
	int32 PassLayer, const FWidgetStyle& InWidgetStyle, float PixelScale, float Tolerance, double Now) const
{
	using namespace WidgetPathLayerPrivate;

	FEdgeState& State = Edges[Piece.Edge];
	const FWidgetPathLook& Look = Looks[Piece.Look];
	const float Length = State.Length;
	const float Opacity = Piece.Opacity * Look.Opacity;
	if (Length <= 0.f || Opacity <= 0.002f)
	{
		return;
	}

	const float AntiAlias = 1.f / FMath::Max(PixelScale, 0.01f);
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const float BreathScale = Breath(Look, Now);
	const EWidgetShapeLineJoin Join = State.Edge.Routing.Corner == EWidgetPathCorner::Sharp ? EWidgetShapeLineJoin::Miter : EWidgetShapeLineJoin::Round;
	const int32 CacheSlot = Piece.Slot * PassCount + Pass;

	// The line is pulled back from an end that carries an arrowhead - only when this piece reaches that end.
	const float StartInset = Piece.Start <= 0.001f ? MarkerInset(Look.StartMarker, Look.MarkerSize, Look.Width) : 0.f;
	const float EndInset = Piece.End >= 0.999f ? MarkerInset(Look.EndMarker, Look.MarkerSize, Look.Width) : 0.f;
	TArray<FVector2f> CutPoints;
	const TArray<FVector2f>* LinePoints = &State.Points;
	if (StartInset > 0.f || EndInset > 0.f)
	{
		WidgetPathGeometry::CutBetween(State.Points, StartInset, Length - EndInset, CutPoints);
		LinePoints = &CutPoints;
	}

	uint32 BaseHash = HashCombineFast(State.RouteHash, GetTypeHash(Piece.Look));
	BaseHash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(FMath::RoundToInt(Piece.Start * 10000.f)), GetTypeHash(FMath::RoundToInt(Piece.End * 10000.f))));
	BaseHash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(PixelScale), GetTypeHash(StartInset + EndInset * 7.f)));

	auto Solid = [](const FLinearColor& Color)
	{
		return [Color](const FWidgetShapeMeshVertex&) { return Color; };
	};

	switch (Pass)
	{
	case PassGlow:
	{
		if (!Look.bGlow || Look.GlowColor.A <= 0.f || Look.GlowSize <= 0.f)
		{
			return;
		}
		uint32 Hash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(Look.Width), GetTypeHash(Look.GlowSize)));
		bool bStale = false;
		FWidgetShapeMesh& Mesh = GetCache(State, CacheSlot, Hash, bStale);
		if (bStale)
		{
			BuildPolyline(*LinePoints, Look.Width + Look.GlowSize, EWidgetShapeLineCap::Round, EWidgetShapeLineJoin::Round, Look.GlowSize + AntiAlias, Tolerance, Mesh,
				0.f, 0.f, 0.f, Piece.Start, Piece.End);
		}
		FLinearColor Color = Look.GlowColor;
		Color.A *= Opacity * BreathScale;
		WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, Solid(Color));
		break;
	}

	case PassOutline:
	{
		if (!Look.bOutline || Look.OutlineWidth <= 0.f || Look.OutlineColor.A <= 0.f)
		{
			return;
		}
		uint32 Hash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(Look.Width), GetTypeHash(Look.OutlineWidth)));
		Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Look.Cap)));
		bool bStale = false;
		FWidgetShapeMesh& Mesh = GetCache(State, CacheSlot, Hash, bStale);
		if (bStale)
		{
			BuildPolyline(*LinePoints, Look.Width + Look.OutlineWidth * 2.f, Look.Cap, Join, AntiAlias, Tolerance, Mesh, 0.f, 0.f, 0.f, Piece.Start, Piece.End);
		}
		FLinearColor Color = Look.OutlineColor;
		Color.A *= Opacity;
		WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, Solid(Color));
		break;
	}

	case PassLine:
	{
		if (Look.Width <= 0.f)
		{
			return;
		}
		const float Period = Look.DashLength + (Look.DashGap > 0.f ? Look.DashGap : Look.DashLength);
		const float DashOffset = Look.DashLength > 0.f && Look.DashSpeed != 0.f ? -Wrap(Now * Look.DashSpeed, Period) : 0.f;
		uint32 Hash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(Look.Width), GetTypeHash(static_cast<uint8>(Look.Cap))));
		Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(Look.DashLength), GetTypeHash(Look.DashGap)));
		Hash = HashCombineFast(Hash, GetTypeHash(DashOffset));
		bool bStale = false;
		FWidgetShapeMesh& Mesh = GetCache(State, CacheSlot, Hash, bStale);
		if (bStale)
		{
			BuildPolyline(*LinePoints, Look.Width, Look.Cap, Join, AntiAlias, Tolerance, Mesh, Look.DashLength, Look.DashGap, DashOffset, Piece.Start, Piece.End);
		}
		if (Mesh.IsEmpty())
		{
			return;
		}

		if (Look.Paint == EWidgetPathPaint::Image)
		{
			const FSlateBrush* Brush = nullptr;
			FLinearColor Tint = Look.Color;
			const float Repeat = Look.ImageTileLength > KINDA_SMALL_NUMBER ? Look.ImageTileLength : FMath::Max(Length, 1.f);
			if (Look.ImageSource == EWidgetImageSource::SVG && Look.SVG)
			{
				TSharedPtr<FWidgetSVGBrush>& SVGBrush = SVGBrushes.FindOrAdd(Piece.Look);
				if (!SVGBrush.IsValid())
				{
					SVGBrush = MakeShared<FWidgetSVGBrush>();
				}
				Brush = SVGBrush->Get(Look.SVG, nullptr, AllottedGeometry, FVector2f(Repeat, FMath::Max(Look.Width, 1.f)), *this);
				if (!Brush)
				{
					// Its first drawing isn't ready: repainted when it is.
					return;
				}
			}
			else if (Look.ImageSource == EWidgetImageSource::Texture && Look.Image.GetResourceObject())
			{
				Brush = &Look.Image;
				Tint *= Look.Image.GetTint(InWidgetStyle);
			}
			if (Brush)
			{
				Tint.A *= Opacity;
				const float Scroll = Look.ImageScrollSpeed != 0.f ? Wrap(Now * Look.ImageScrollSpeed, Repeat) : 0.f;
				WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, Solid(Tint), Brush,
					[Repeat, Scroll](const FWidgetShapeMeshVertex& Vertex)
					{
						return FVector2f((Vertex.Along.X - Scroll) / Repeat, Vertex.Along.Y);
					});
				return;
			}
		}

		if (Look.Paint == EWidgetPathPaint::Gradient)
		{
			WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, [&Look, Opacity](const FWidgetShapeMeshVertex& Vertex)
			{
				FLinearColor Color = Look.ColorAt(Vertex.OutlineLength > 0.f ? Vertex.Along.X / Vertex.OutlineLength : 0.f);
				Color.A *= Opacity;
				return Color;
			});
		}
		else
		{
			FLinearColor Color = Look.Color;
			Color.A *= Opacity;
			WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, Solid(Color));
		}
		break;
	}

	case PassFlow:
	{
		if (Look.Flow == EWidgetPathFlow::None || Look.FlowColor.A <= 0.f)
		{
			return;
		}
		const float Spacing = FMath::Max(Look.FlowSpacing, 4.f);
		const float Travel = Wrap(Now * Look.FlowSpeed, Spacing);
		const float FlowAlpha = Look.FlowColor.A * Opacity * BreathScale;

		if (Look.Flow == EWidgetPathFlow::Pulse)
		{
			// Bands of light: the line's solid mesh, lit where a band is.
			uint32 Hash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(Look.Width), GetTypeHash(static_cast<uint8>(Look.Cap))));
			bool bStale = false;
			FWidgetShapeMesh& Mesh = GetCache(State, CacheSlot, Hash, bStale);
			if (bStale)
			{
				BuildPolyline(*LinePoints, Look.Width, Look.Cap, Join, AntiAlias, Tolerance, Mesh, 0.f, 0.f, 0.f, Piece.Start, Piece.End);
			}
			const FLinearColor FlowColor = Look.FlowColor;
			const float BandLength = FMath::Clamp(Look.FlowLength, 1.f, Spacing);
			WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint,
				[FlowColor, BandLength, Spacing, Travel, FlowAlpha](const FWidgetShapeMeshVertex& Vertex)
				{
					const float Phase = Wrap(static_cast<double>(Vertex.Along.X - Travel), Spacing);
					const float Band = Phase < BandLength ? FMath::Square(FMath::Sin(UE_PI * Phase / BandLength)) : 0.f;
					FLinearColor Color = FlowColor;
					Color.A = FlowAlpha * Band;
					return Color;
				});
			return;
		}

		// Dots or arrows travelling along the piece, fading in and out at its ends.
		const float From = Piece.Start * Length;
		const float To = Piece.End * Length;
		uint32 Hash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(Look.FlowSize), GetTypeHash(Spacing)));
		Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(Travel), GetTypeHash(static_cast<uint8>(Look.Flow))));
		bool bStale = false;
		FWidgetShapeMesh& Mesh = GetCache(State, CacheSlot, Hash, bStale);
		if (bStale)
		{
			const float Fade = Spacing * 0.25f;
			FWidgetShapeMesh Mark;
			for (float Distance = Travel; Distance <= Length; Distance += Spacing)
			{
				if (Distance < From || Distance > To)
				{
					continue;
				}
				FVector2f Point;
				FVector2f Direction;
				if (!WidgetPathGeometry::SampleAt(State.Points, Distance, Point, Direction))
				{
					continue;
				}
				const float EdgeFade = FMath::Clamp(FMath::Min(Distance - From, To - Distance) / FMath::Max(Fade, 1.f), 0.f, 1.f);
				if (EdgeFade <= 0.01f)
				{
					continue;
				}
				// Arrows point the way the flow goes.
				const FVector2f Way = Look.FlowSpeed < 0.f ? -Direction : Direction;
				Mark.Reset();
				if (Look.Flow == EWidgetPathFlow::Dots)
				{
					BuildPolygon(MakeCircle(Point, Look.FlowSize * 0.5f, 14), AntiAlias, Mark);
				}
				else
				{
					const FVector2f Across(-Way.Y, Way.X);
					const float Size = Look.FlowSize;
					TArray<FVector2f> Chevron = { Point - Way * (Size * 0.35f) + Across * (Size * 0.5f), Point + Way * (Size * 0.35f), Point - Way * (Size * 0.35f) - Across * (Size * 0.5f) };
					BuildPolyline(Chevron, FMath::Max(Size * 0.3f, AntiAlias), EWidgetShapeLineCap::Round, EWidgetShapeLineJoin::Round, AntiAlias, Tolerance, Mark);
				}
				AppendMesh(Mesh, Mark, EdgeFade);
			}
		}
		FLinearColor Color = Look.FlowColor;
		Color.A = FlowAlpha;
		WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, Solid(Color));
		break;
	}

	case PassMarkers:
	{
		const bool bStart = Look.StartMarker != EWidgetPathMarker::None && Piece.Start <= 0.001f;
		const bool bEnd = Look.EndMarker != EWidgetPathMarker::None && Piece.End >= 0.999f;
		if (!bStart && !bEnd)
		{
			return;
		}
		uint32 Hash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(Look.MarkerSize), GetTypeHash(Look.Width)));
		Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(static_cast<uint8>(Look.StartMarker)), GetTypeHash(static_cast<uint8>(Look.EndMarker))));
		// Two caches taken at once: room for both first, so adding the second can't move the first.
		State.Caches.Reserve(State.Caches.Num() + 2);
		bool bStale = false;
		FWidgetShapeMesh& StartMesh = GetCache(State, CacheSlot, Hash, bStale);
		if (bStale && bStart)
		{
			FVector2f Point;
			FVector2f Direction;
			if (WidgetPathGeometry::SampleAt(State.Points, 0.f, Point, Direction))
			{
				BuildMarker(Look.StartMarker, State.Points[0], -Direction, Look.MarkerSize, Look.Width, AntiAlias, Tolerance, StartMesh);
			}
		}
		bool bEndStale = false;
		FWidgetShapeMesh& EndMesh = GetCache(State, CacheSlot + PassCount * 8, Hash, bEndStale);
		if (bEndStale && bEnd)
		{
			FVector2f Point;
			FVector2f Direction;
			if (WidgetPathGeometry::SampleAt(State.Points, Length, Point, Direction))
			{
				BuildMarker(Look.EndMarker, State.Points.Last(), Direction, Look.MarkerSize, Look.Width, AntiAlias, Tolerance, EndMesh);
			}
		}
		if (bStart)
		{
			FLinearColor Color = Look.ColorAt(0.f);
			Color.A *= Opacity;
			WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, StartMesh, WidgetTint, Solid(Color));
		}
		if (bEnd)
		{
			FLinearColor Color = Look.ColorAt(1.f);
			Color.A *= Opacity;
			WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, EndMesh, WidgetTint, Solid(Color));
		}
		break;
	}

	default:
		break;
	}
}

int32 SWidgetPathLayer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace WidgetPathLayerPrivate;

	if (Edges.Num() == 0 || Looks.Num() == 0)
	{
		return LayerId;
	}

	const float PixelScale = WidgetEffectsDraw::QuantizeScale(WidgetEffectsDraw::GetPixelScale(AllottedGeometry));
	const float Tolerance = WidgetShapeGeometry::GetTolerance(PixelScale);
	UpdateRoutes(AllottedGeometry, Tolerance);

	TArray<FDrawPiece> Pieces;
	GatherPieces(Pieces);
	if (Pieces.Num() == 0)
	{
		return LayerId;
	}

	const double Now = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : 0.0;
	int32 Layer = LayerId;
	int32 GroupStart = 0;
	while (GroupStart < Pieces.Num())
	{
		const int32 Order = Looks[Pieces[GroupStart].Look].DrawOrder;
		int32 GroupEnd = GroupStart;
		while (GroupEnd < Pieces.Num() && Looks[Pieces[GroupEnd].Look].DrawOrder == Order)
		{
			++GroupEnd;
		}
		for (int32 Pass = 0; Pass < PassCount; ++Pass)
		{
			for (int32 Index = GroupStart; Index < GroupEnd; ++Index)
			{
				PaintPiece(Pieces[Index], Pass, AllottedGeometry, OutDrawElements, Layer + Pass, InWidgetStyle, PixelScale, Tolerance, Now);
			}
		}
		Layer += PassCount;
		GroupStart = GroupEnd;
	}
	return Layer;
}

// =====================================================================================================
// UWidgetPathLayer
// =====================================================================================================

UWidgetPathLayer::UWidgetPathLayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Lines never take the pointer: a layer laid over a tree must not block its nodes.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	WidgetPathPresets::Apply(Preset, Routing, Styles, Rules);
}

TSharedRef<SWidget> UWidgetPathLayer::RebuildWidget()
{
	MyLayer = SNew(SWidgetPathLayer);
	bLooksDirty = true;
	return MyLayer.ToSharedRef();
}

void UWidgetPathLayer::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyLayer.Reset();
}

void UWidgetPathLayer::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (!MyLayer.IsValid())
	{
		return;
	}

	const bool bDesign = IsDesignTime();
	MyLayer->SetDesignTime(bDesign);

	FWidgetPathSlateAnimation SlateAnimation;
	SlateAnimation.Transition = Transition;
	SlateAnimation.TransitionDuration = TransitionDuration;
	SlateAnimation.Motion = TransitionMotion;
	SlateAnimation.Intro = Intro;
	SlateAnimation.IntroDuration = IntroDuration;
	SlateAnimation.IntroStagger = IntroStagger;
	MyLayer->SetAnimation(SlateAnimation);

	if (!bDesign)
	{
		const TWeakObjectPtr<UWidgetPathLayer> WeakLayer(this);
		MyLayer->SetTickCallback([WeakLayer](float DeltaTime)
		{
			if (UWidgetPathLayer* Layer = WeakLayer.Get())
			{
				Layer->PollNodes(DeltaTime);
			}
		});
	}

	bLooksDirty = true;
	CacheNames();
	Push(false);

	if (!bDesign && Intro != EWidgetPathIntro::None && !bIntroPlayed)
	{
		bIntroPlayed = true;
		MyLayer->PlayIntro();
	}
}

#if WITH_EDITOR
const FText UWidgetPathLayer::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}

void UWidgetPathLayer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName ChangedName = PropertyChangedEvent.GetPropertyName();
	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
	if (ChangedName == GET_MEMBER_NAME_CHECKED(UWidgetPathLayer, Preset))
	{
		WidgetPathPresets::Apply(Preset, Routing, Styles, Rules);
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UWidgetPathLayer, Routing) || MemberName == GET_MEMBER_NAME_CHECKED(UWidgetPathLayer, Styles)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetPathLayer, Rules))
	{
		Preset = EWidgetPathPreset::Custom;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

// ---------------------------------------------------------------------------------------------
// Graph
// ---------------------------------------------------------------------------------------------

uint64 UWidgetPathLayer::MakeKey(const UWidget* A, const UWidget* B)
{
	return (static_cast<uint64>(PointerHash(A)) << 32) | static_cast<uint64>(PointerHash(B));
}

void UWidgetPathLayer::CacheNames()
{
	NameCache.Reset();
	if (const UWidgetTree* Tree = WidgetTreeLookup::FindWidgetTree(this))
	{
		Tree->ForEachWidget([this](UWidget* Widget)
		{
			if (Widget)
			{
				NameCache.Add(Widget->GetFName(), Widget);
			}
		});
	}
	AuthoredStates.Reset();
	for (const FWidgetPathNodeState& Entry : Nodes)
	{
		if (!Entry.Widget.IsNone())
		{
			AuthoredStates.Add(Entry.Widget, Entry.State);
		}
	}
}

UWidget* UWidgetPathLayer::FindNode(FName Name) const
{
	if (Name.IsNone())
	{
		return nullptr;
	}
	if (const TWeakObjectPtr<UWidget>* Found = NameCache.Find(Name))
	{
		return Found->Get();
	}
	return nullptr;
}

void UWidgetPathLayer::GatherEdges(TArray<FEdgeInfo>& Out) const
{
	Out.Reset();
	TSet<uint64> Seen;
	auto AddEdge = [&](UWidget* From, UWidget* To, FName StyleName, float InProgress, FName InProgressStyle, const FWidgetPathRouting* InRouting)
	{
		if (!From || !To || From == To)
		{
			return;
		}
		const uint64 Key = MakeKey(From, To);
		if (RemovedKeys.Contains(Key) || Seen.Contains(Key) || Seen.Contains(MakeKey(To, From)))
		{
			return;
		}
		Seen.Add(Key);
		FEdgeInfo& Info = Out.AddDefaulted_GetRef();
		Info.From = From;
		Info.To = To;
		Info.Key = Key;
		Info.Style = StyleName;
		Info.Progress = InProgress;
		Info.ProgressStyle = InProgressStyle;
		Info.Routing = InRouting;
		if (const FConnectionOverride* Override = Overrides.Find(Key))
		{
			if (Override->Style.IsSet())
			{
				Info.Style = Override->Style.GetValue();
			}
			if (Override->Progress >= 0.f)
			{
				Info.Progress = Override->Progress;
			}
		}
	};

	for (const FWidgetPathConnection& Connection : Connections)
	{
		AddEdge(FindNode(Connection.From), FindNode(Connection.To), Connection.Style, Connection.Progress, Connection.ProgressStyle,
			Connection.bOverrideRouting ? &Connection.Routing : &Routing);
	}
	for (const FAddedConnection& Added : AddedConnections)
	{
		AddEdge(Added.From.Get(), Added.To.Get(), Added.Style, 0.f, NAME_None, &Routing);
	}
}

FName UWidgetPathLayer::GetRawState(UWidget* Node) const
{
	if (!Node)
	{
		return DefaultNodeState;
	}
	if (const FNodeRecord* Record = NodeRecords.Find(TObjectKey<UWidget>(Node)))
	{
		if (!Record->SetState.IsNone())
		{
			return Record->SetState;
		}
		if (!Record->ReadState.IsNone())
		{
			return Record->ReadState;
		}
	}
	if (const FName* Authored = AuthoredStates.Find(Node->GetFName()))
	{
		if (!Authored->IsNone())
		{
			return *Authored;
		}
	}
	return DefaultNodeState;
}

void UWidgetPathLayer::ComputeStates(const TArray<FEdgeInfo>& InEdges, TMap<UWidget*, FName>& OutStates) const
{
	OutStates.Reset();
	for (const FEdgeInfo& Edge : InEdges)
	{
		if (!OutStates.Contains(Edge.From))
		{
			OutStates.Add(Edge.From, GetRawState(Edge.From));
		}
		if (!OutStates.Contains(Edge.To))
		{
			OutStates.Add(Edge.To, GetRawState(Edge.To));
		}
	}
	if (!bDeriveAvailable || AvailableState.IsNone())
	{
		return;
	}

	// Derived from the raw states only, so one Available doesn't make the next one Available.
	const TMap<UWidget*, FName> Raw = OutStates;
	for (const FEdgeInfo& Edge : InEdges)
	{
		const FName FromState = Raw.FindRef(Edge.From);
		const FName ToState = Raw.FindRef(Edge.To);
		if (FromState == UnlockedState && ToState == DefaultNodeState)
		{
			OutStates.Add(Edge.To, AvailableState);
		}
		if (!bRoutesFollowDirection && ToState == UnlockedState && FromState == DefaultNodeState)
		{
			OutStates.Add(Edge.From, AvailableState);
		}
	}
}

TArray<UWidget*> UWidgetPathLayer::FindRouteIn(const TArray<FEdgeInfo>& InEdges, const TMap<UWidget*, FName>& States, UWidget* Target) const
{
	TArray<UWidget*> Route;
	if (!Target || States.FindRef(Target) == UnlockedState)
	{
		return Route;
	}

	// Breadth first from the target back toward the nearest unlocked node: the first one reached is the nearest.
	TMap<UWidget*, UWidget*> TowardTarget;
	TArray<UWidget*> Queue;
	Queue.Add(Target);
	TowardTarget.Add(Target, nullptr);
	UWidget* Found = nullptr;
	for (int32 Head = 0; Head < Queue.Num() && !Found; ++Head)
	{
		UWidget* Current = Queue[Head];
		for (const FEdgeInfo& Edge : InEdges)
		{
			UWidget* Next = nullptr;
			if (Edge.To == Current)
			{
				Next = Edge.From;
			}
			else if (!bRoutesFollowDirection && Edge.From == Current)
			{
				Next = Edge.To;
			}
			if (!Next || TowardTarget.Contains(Next))
			{
				continue;
			}
			TowardTarget.Add(Next, Current);
			if (States.FindRef(Next) == UnlockedState)
			{
				Found = Next;
				break;
			}
			Queue.Add(Next);
		}
	}

	for (UWidget* Step = Found; Step; Step = TowardTarget.FindRef(Step))
	{
		Route.Add(Step);
	}
	return Route;
}

int32 UWidgetPathLayer::FindStyleIndex(FName StyleName) const
{
	if (StyleName.IsNone())
	{
		return INDEX_NONE;
	}
	return Styles.IndexOfByPredicate([StyleName](const FWidgetPathStyle& Style) { return Style.Name == StyleName; });
}

int32 UWidgetPathLayer::ResolveStyle(const FEdgeInfo& Edge, const TMap<UWidget*, FName>& States, bool bHighlighted) const
{
	const int32 Explicit = FindStyleIndex(Edge.Style);
	if (Explicit != INDEX_NONE)
	{
		return Explicit;
	}

	const FName FromState = States.FindRef(Edge.From);
	const FName ToState = States.FindRef(Edge.To);
	UWidget* Hovered = HoveredNode.Get();
	UWidget* Selected = SelectedNode.Get();
	if (!Selected && IsDesignTime())
	{
		Selected = FindNode(PreviewSelectedNode);
	}
	const bool bHovered = Hovered && (Edge.From == Hovered || Edge.To == Hovered);
	const bool bSelected = Selected && (Edge.From == Selected || Edge.To == Selected);

	auto Matches = [](EWidgetPathMatch Match, bool bValue)
	{
		return Match == EWidgetPathMatch::Any || (Match == EWidgetPathMatch::Yes) == bValue;
	};
	auto StateMatches = [](FName Wanted, FName Actual)
	{
		return Wanted.IsNone() || Wanted == Actual;
	};

	for (const FWidgetPathStyleRule& Rule : Rules)
	{
		if (!Matches(Rule.Highlighted, bHighlighted) || !Matches(Rule.Hovered, bHovered) || !Matches(Rule.Selected, bSelected))
		{
			continue;
		}
		const bool bForward = StateMatches(Rule.FromState, FromState) && StateMatches(Rule.ToState, ToState);
		const bool bBackward = Rule.bEitherWay && StateMatches(Rule.FromState, ToState) && StateMatches(Rule.ToState, FromState);
		if (!bForward && !bBackward)
		{
			continue;
		}
		const int32 Found = FindStyleIndex(Rule.Style);
		if (Found != INDEX_NONE)
		{
			return Found;
		}
	}
	return Styles.Num() > 0 ? 0 : INDEX_NONE;
}

FWidgetPathNodeShapeInfo UWidgetPathLayer::ReadNodeShape(UWidget* Node, const FWidgetPathRouting& InRouting) const
{
	FWidgetPathNodeShapeInfo Info;
	switch (InRouting.NodeShape)
	{
	case EWidgetPathNodeShape::Point:
		Info.bPoint = true;
		return Info;
	case EWidgetPathNodeShape::Ellipse:
		Info.bEllipse = true;
		return Info;
	case EWidgetPathNodeShape::Box:
		return Info;
	default:
		break;
	}

	// Auto: the node's own outline, or - through a User Widget's root and plain layout boxes - its background's.
	UWidget* Probe = Node;
	for (int32 Depth = 0; Probe && Depth < 4; ++Depth)
	{
		if (Probe->IsA<UEllipseShape>())
		{
			Info.bEllipse = true;
			return Info;
		}
		const FDropShadowCaster Caster = UDropShadowComponent::ReadCaster(Probe);
		if (Caster.bPill || Caster.Radii.X > 0.f || Caster.Radii.Y > 0.f || Caster.Radii.Z > 0.f || Caster.Radii.W > 0.f)
		{
			Info.bPill = Caster.bPill;
			Info.Radii = Caster.Radii;
			return Info;
		}
		if (const UUserWidget* UserWidget = Cast<UUserWidget>(Probe))
		{
			Probe = UserWidget->WidgetTree ? UserWidget->WidgetTree->RootWidget : nullptr;
		}
		else if (Probe->IsA<USizeBox>() || Probe->IsA<UScaleBox>() || Probe->IsA<UOverlay>())
		{
			const UPanelWidget* Panel = Cast<UPanelWidget>(Probe);
			Probe = Panel && Panel->GetChildrenCount() > 0 ? Panel->GetChildAt(0) : nullptr;
		}
		else
		{
			break;
		}
	}
	return Info;
}

void UWidgetPathLayer::Push(bool bAnimate)
{
	if (!MyLayer.IsValid())
	{
		return;
	}

	if (bLooksDirty)
	{
		bLooksDirty = false;
		TArray<FWidgetPathLook> Looks;
		Looks.Reserve(Styles.Num());
		for (const FWidgetPathStyle& Style : Styles)
		{
			Looks.Add(Style.Look);
		}
		MyLayer->SetLooks(Looks);
	}

	TArray<FEdgeInfo> EdgeInfos;
	GatherEdges(EdgeInfos);
	TMap<UWidget*, FName> States;
	ComputeStates(EdgeInfos, States);

	KnownNodes.Reset();
	for (const TPair<UWidget*, FName>& Entry : States)
	{
		KnownNodes.Add(Entry.Key);
	}

	// Highlighted: the manual set, the route to the selected node, and what hovering lights.
	TSet<uint64> Lit = ManualHighlight;
	auto LightRoute = [&Lit](const TArray<UWidget*>& Route)
	{
		for (int32 Index = 1; Index < Route.Num(); ++Index)
		{
			Lit.Add(MakeKey(Route[Index - 1], Route[Index]));
			Lit.Add(MakeKey(Route[Index], Route[Index - 1]));
		}
	};
	UWidget* Selected = SelectedNode.Get();
	if (!Selected && IsDesignTime())
	{
		Selected = FindNode(PreviewSelectedNode);
	}
	if (bHighlightRouteToSelected && Selected)
	{
		LightRoute(FindRouteIn(EdgeInfos, States, Selected));
	}
	if (UWidget* Hovered = HoveredNode.Get())
	{
		if (HoverHighlight == EWidgetPathHoverHighlight::RouteTo)
		{
			LightRoute(FindRouteIn(EdgeInfos, States, Hovered));
		}
		else if (HoverHighlight == EWidgetPathHoverHighlight::Connections)
		{
			for (const FEdgeInfo& Edge : EdgeInfos)
			{
				if (Edge.From == Hovered || Edge.To == Hovered)
				{
					Lit.Add(Edge.Key);
				}
			}
		}
	}

	// Depth from the roots (nodes nothing leads to), for the intro's stagger.
	TMap<UWidget*, int32> Depths;
	{
		TSet<UWidget*> HasIncoming;
		for (const FEdgeInfo& Edge : EdgeInfos)
		{
			HasIncoming.Add(Edge.To);
		}
		TArray<UWidget*> Queue;
		for (const TPair<UWidget*, FName>& Entry : States)
		{
			if (!HasIncoming.Contains(Entry.Key))
			{
				Depths.Add(Entry.Key, 0);
				Queue.Add(Entry.Key);
			}
		}
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 Next = Depths.FindRef(Queue[Head]) + 1;
			for (const FEdgeInfo& Edge : EdgeInfos)
			{
				if (Edge.From == Queue[Head] && !Depths.Contains(Edge.To))
				{
					Depths.Add(Edge.To, Next);
					Queue.Add(Edge.To);
				}
			}
		}
	}

	const int32 DefaultProgressLook = FindStyleIndex(UnlockedState);
	TArray<FWidgetPathSlateEdge> SlateEdges;
	SlateEdges.Reserve(EdgeInfos.Num());
	for (const FEdgeInfo& Edge : EdgeInfos)
	{
		const FWidgetPathRouting& EdgeRouting = Edge.Routing ? *Edge.Routing : Routing;
		FWidgetPathSlateEdge& Slate = SlateEdges.AddDefaulted_GetRef();
		Slate.Key = Edge.Key;
		Slate.From = Edge.From->GetCachedWidget();
		Slate.To = Edge.To->GetCachedWidget();
		Slate.FromShape = ReadNodeShape(Edge.From, EdgeRouting);
		Slate.ToShape = ReadNodeShape(Edge.To, EdgeRouting);
		Slate.Routing = EdgeRouting;
		if (UWidget* Center = FindNode(EdgeRouting.OrbitCenter))
		{
			Slate.OrbitCenter = Center->GetCachedWidget();
		}
		Slate.Look = ResolveStyle(Edge, States, Lit.Contains(Edge.Key));
		const int32 ProgressLook = FindStyleIndex(Edge.ProgressStyle);
		Slate.ProgressLook = ProgressLook != INDEX_NONE ? ProgressLook : DefaultProgressLook;
		Slate.Progress = FMath::Clamp(Edge.Progress, 0.f, 1.f);

		// Drawn on from the end that changed first: toward the node that changed last.
		const FNodeRecord* FromRecord = NodeRecords.Find(TObjectKey<UWidget>(Edge.From));
		const FNodeRecord* ToRecord = NodeRecords.Find(TObjectKey<UWidget>(Edge.To));
		const uint32 FromSerial = FromRecord ? FromRecord->ChangeSerial : 0;
		const uint32 ToSerial = ToRecord ? ToRecord->ChangeSerial : 0;
		Slate.bChangeFromStart = ToSerial >= FromSerial;
		Slate.Depth = Depths.FindRef(Edge.From);
	}
	MyLayer->SetEdges(SlateEdges, bAnimate);
}

bool UWidgetPathLayer::RulesReadHover() const
{
	return Rules.ContainsByPredicate([](const FWidgetPathStyleRule& Rule) { return Rule.Hovered != EWidgetPathMatch::Any; });
}

void UWidgetPathLayer::NoteStateChange(UWidget* Node)
{
	FNodeRecord& Record = NodeRecords.FindOrAdd(TObjectKey<UWidget>(Node));
	Record.Widget = Node;
	Record.ChangeSerial = NextChangeSerial++;
}

void UWidgetPathLayer::PollNodes(float DeltaTime)
{
	bool bChanged = false;

	// States the nodes report of themselves.
	if (bReadStatesFromNodes)
	{
		ReadStatesTimer -= DeltaTime;
		if (ReadStatesTimer <= 0.f)
		{
			ReadStatesTimer = 0.1f;
			for (const TWeakObjectPtr<UWidget>& Weak : KnownNodes)
			{
				UWidget* Node = Weak.Get();
				if (!Node || !Node->GetClass()->ImplementsInterface(UWidgetPathNode::StaticClass()))
				{
					continue;
				}
				const FName Read = IWidgetPathNode::Execute_GetWidgetPathNodeState(Node);
				FNodeRecord& Record = NodeRecords.FindOrAdd(TObjectKey<UWidget>(Node));
				if (Record.ReadState != Read)
				{
					Record.Widget = Node;
					Record.ReadState = Read;
					NoteStateChange(Node);
					OnNodeStateChanged.Broadcast(Node, GetRawState(Node));
					bChanged = true;
				}
			}
		}
	}

	// The node under the pointer.
	if (HoverHighlight != EWidgetPathHoverHighlight::None || RulesReadHover() || OnHoveredNodeChanged.IsBound())
	{
		FWidgetAnimInputContext Context;
		Context.HostWidget = this;
		Context.MeasuredWidget = this;
		WidgetAnimInput::ReadPointer(Context);
		UWidget* Under = nullptr;
		if (Context.bHasPointer && (!Context.bWorldPointer || Context.WorldPointer.bOver))
		{
			for (const TWeakObjectPtr<UWidget>& Weak : KnownNodes)
			{
				UWidget* Node = Weak.Get();
				if (!Node || !Node->IsVisible())
				{
					continue;
				}
				const bool bOver = Context.bWorldPointer
					? WidgetAnimInput::IsPointUnder(Context, Node, Context.PointerPosition)
					: WidgetAnimInput::IsPointerOver(Node, Context.PointerPosition);
				if (bOver)
				{
					Under = Node;
					break;
				}
			}
		}
		if (Under != HoveredNode.Get())
		{
			HoveredNode = Under;
			OnHoveredNodeChanged.Broadcast(Under);
			bChanged = true;
		}
	}

	if (bChanged)
	{
		Push(true);
	}
}

// ---------------------------------------------------------------------------------------------
// Blueprint
// ---------------------------------------------------------------------------------------------

bool UWidgetPathLayer::AddConnection(UWidget* FromNode, UWidget* ToNode, FName StyleName)
{
	if (!FromNode || !ToNode || FromNode == ToNode || AreConnected(FromNode, ToNode))
	{
		return false;
	}
	RemovedKeys.Remove(MakeKey(FromNode, ToNode));
	RemovedKeys.Remove(MakeKey(ToNode, FromNode));
	if (AreConnected(FromNode, ToNode))
	{
		// An authored line that had been removed is back.
		Push(true);
		return true;
	}
	FAddedConnection& Added = AddedConnections.AddDefaulted_GetRef();
	Added.From = FromNode;
	Added.To = ToNode;
	Added.Style = StyleName;
	Push(true);
	return true;
}

bool UWidgetPathLayer::RemoveConnection(UWidget* FromNode, UWidget* ToNode)
{
	if (!AreConnected(FromNode, ToNode))
	{
		return false;
	}
	AddedConnections.RemoveAll([FromNode, ToNode](const FAddedConnection& Added)
	{
		return (Added.From.Get() == FromNode && Added.To.Get() == ToNode) || (Added.From.Get() == ToNode && Added.To.Get() == FromNode);
	});
	if (AreConnected(FromNode, ToNode))
	{
		// Authored: hidden by key.
		RemovedKeys.Add(MakeKey(FromNode, ToNode));
		RemovedKeys.Add(MakeKey(ToNode, FromNode));
	}
	Push(true);
	return true;
}

void UWidgetPathLayer::ClearConnections(bool bIncludeAuthored)
{
	AddedConnections.Reset();
	if (bIncludeAuthored)
	{
		TArray<FEdgeInfo> EdgeInfos;
		GatherEdges(EdgeInfos);
		for (const FEdgeInfo& Edge : EdgeInfos)
		{
			RemovedKeys.Add(Edge.Key);
			RemovedKeys.Add(MakeKey(Edge.To, Edge.From));
		}
	}
	Push(false);
}

bool UWidgetPathLayer::AreConnected(UWidget* NodeA, UWidget* NodeB) const
{
	if (!NodeA || !NodeB)
	{
		return false;
	}
	TArray<FEdgeInfo> EdgeInfos;
	GatherEdges(EdgeInfos);
	return EdgeInfos.ContainsByPredicate([NodeA, NodeB](const FEdgeInfo& Edge)
	{
		return (Edge.From == NodeA && Edge.To == NodeB) || (Edge.From == NodeB && Edge.To == NodeA);
	});
}

bool UWidgetPathLayer::SetConnectionStyle(UWidget* FromNode, UWidget* ToNode, FName StyleName)
{
	TArray<FEdgeInfo> EdgeInfos;
	GatherEdges(EdgeInfos);
	for (const FEdgeInfo& Edge : EdgeInfos)
	{
		if ((Edge.From == FromNode && Edge.To == ToNode) || (Edge.From == ToNode && Edge.To == FromNode))
		{
			FConnectionOverride& Override = Overrides.FindOrAdd(Edge.Key);
			if (StyleName.IsNone())
			{
				Override.Style.Reset();
			}
			else
			{
				Override.Style = StyleName;
			}
			Push(true);
			return true;
		}
	}
	return false;
}

bool UWidgetPathLayer::SetConnectionProgress(UWidget* FromNode, UWidget* ToNode, float InProgress, bool bAnimate)
{
	TArray<FEdgeInfo> EdgeInfos;
	GatherEdges(EdgeInfos);
	for (const FEdgeInfo& Edge : EdgeInfos)
	{
		if ((Edge.From == FromNode && Edge.To == ToNode) || (Edge.From == ToNode && Edge.To == FromNode))
		{
			// Progress always fills from the line's first node (its From), whichever way round the two are given.
			Overrides.FindOrAdd(Edge.Key).Progress = FMath::Clamp(InProgress, 0.f, 1.f);
			Push(bAnimate);
			return true;
		}
	}
	return false;
}

void UWidgetPathLayer::SetNodeState(UWidget* Node, FName State, bool bAnimate)
{
	if (!Node)
	{
		return;
	}
	FNodeRecord& Record = NodeRecords.FindOrAdd(TObjectKey<UWidget>(Node));
	Record.Widget = Node;
	if (Record.SetState == State)
	{
		return;
	}
	Record.SetState = State;
	NoteStateChange(Node);
	OnNodeStateChanged.Broadcast(Node, GetRawState(Node));
	Push(bAnimate);
}

void UWidgetPathLayer::SetNodeStates(const TArray<UWidget*>& InNodes, FName State, bool bAnimate)
{
	bool bChanged = false;
	for (UWidget* Node : InNodes)
	{
		if (!Node)
		{
			continue;
		}
		FNodeRecord& Record = NodeRecords.FindOrAdd(TObjectKey<UWidget>(Node));
		Record.Widget = Node;
		if (Record.SetState != State)
		{
			Record.SetState = State;
			NoteStateChange(Node);
			OnNodeStateChanged.Broadcast(Node, GetRawState(Node));
			bChanged = true;
		}
	}
	if (bChanged)
	{
		Push(bAnimate);
	}
}

FName UWidgetPathLayer::GetNodeState(UWidget* Node) const
{
	TArray<FEdgeInfo> EdgeInfos;
	GatherEdges(EdgeInfos);
	TMap<UWidget*, FName> States;
	ComputeStates(EdgeInfos, States);
	if (const FName* Found = States.Find(Node))
	{
		return *Found;
	}
	return GetRawState(Node);
}

TArray<UWidget*> UWidgetPathLayer::GetNodesInState(FName State) const
{
	TArray<FEdgeInfo> EdgeInfos;
	GatherEdges(EdgeInfos);
	TMap<UWidget*, FName> States;
	ComputeStates(EdgeInfos, States);
	TArray<UWidget*> Out;
	for (const TPair<UWidget*, FName>& Entry : States)
	{
		if (Entry.Value == State)
		{
			Out.Add(Entry.Key);
		}
	}
	return Out;
}

void UWidgetPathLayer::ResetNodeStates(bool bAnimate)
{
	for (TPair<TObjectKey<UWidget>, FNodeRecord>& Entry : NodeRecords)
	{
		if (!Entry.Value.SetState.IsNone())
		{
			Entry.Value.SetState = NAME_None;
			Entry.Value.ChangeSerial = NextChangeSerial++;
		}
	}
	Push(bAnimate);
}

TArray<UWidget*> UWidgetPathLayer::GetConnectedNodes(UWidget* Node, EWidgetPathDirection Direction) const
{
	TArray<UWidget*> Out;
	TArray<FEdgeInfo> EdgeInfos;
	GatherEdges(EdgeInfos);
	for (const FEdgeInfo& Edge : EdgeInfos)
	{
		if (Edge.From == Node && Direction != EWidgetPathDirection::Incoming)
		{
			Out.AddUnique(Edge.To);
		}
		if (Edge.To == Node && Direction != EWidgetPathDirection::Outgoing)
		{
			Out.AddUnique(Edge.From);
		}
	}
	return Out;
}

TArray<UWidget*> UWidgetPathLayer::FindRoute(UWidget* Target) const
{
	TArray<FEdgeInfo> EdgeInfos;
	GatherEdges(EdgeInfos);
	TMap<UWidget*, FName> States;
	ComputeStates(EdgeInfos, States);
	return FindRouteIn(EdgeInfos, States, Target);
}

void UWidgetPathLayer::SetSelectedNode(UWidget* Node)
{
	if (SelectedNode.Get() == Node)
	{
		return;
	}
	SelectedNode = Node;
	Push(true);
}

void UWidgetPathLayer::HighlightRoute(const TArray<UWidget*>& Route)
{
	for (int32 Index = 1; Index < Route.Num(); ++Index)
	{
		ManualHighlight.Add(MakeKey(Route[Index - 1], Route[Index]));
		ManualHighlight.Add(MakeKey(Route[Index], Route[Index - 1]));
	}
	Push(true);
}

void UWidgetPathLayer::HighlightNodeConnections(UWidget* Node)
{
	TArray<FEdgeInfo> EdgeInfos;
	GatherEdges(EdgeInfos);
	for (const FEdgeInfo& Edge : EdgeInfos)
	{
		if (Edge.From == Node || Edge.To == Node)
		{
			ManualHighlight.Add(Edge.Key);
		}
	}
	Push(true);
}

void UWidgetPathLayer::ClearHighlight()
{
	if (ManualHighlight.Num() == 0)
	{
		return;
	}
	ManualHighlight.Reset();
	Push(true);
}

void UWidgetPathLayer::ApplyPreset(EWidgetPathPreset InPreset)
{
	Preset = InPreset;
	WidgetPathPresets::Apply(Preset, Routing, Styles, Rules);
	bLooksDirty = true;
	Push(false);
}

void UWidgetPathLayer::SetStyleLook(FName StyleName, const FWidgetPathLook& InLook)
{
	const int32 Found = FindStyleIndex(StyleName);
	if (Found != INDEX_NONE)
	{
		Styles[Found].Look = InLook;
	}
	else
	{
		FWidgetPathStyle& Added = Styles.AddDefaulted_GetRef();
		Added.Name = StyleName;
		Added.Look = InLook;
	}
	Preset = EWidgetPathPreset::Custom;
	bLooksDirty = true;
	Push(false);
}

void UWidgetPathLayer::SetRouting(const FWidgetPathRouting& InRouting)
{
	Routing = InRouting;
	Preset = EWidgetPathPreset::Custom;
	Push(false);
}

void UWidgetPathLayer::PlayIntro()
{
	if (MyLayer.IsValid())
	{
		MyLayer->PlayIntro();
	}
}

bool UWidgetPathLayer::GetConnectionPath(UWidget* FromNode, UWidget* ToNode, TArray<FVector2D>& OutPoints) const
{
	OutPoints.Reset();
	if (!MyLayer.IsValid() || !FromNode || !ToNode)
	{
		return false;
	}
	TArray<FVector2f> Local;
	bool bReversed = false;
	if (!MyLayer->GetRoute(MakeKey(FromNode, ToNode), Local))
	{
		if (!MyLayer->GetRoute(MakeKey(ToNode, FromNode), Local))
		{
			return false;
		}
		bReversed = true;
	}
	const FSlateRenderTransform& ToDesktop = MyLayer->GetTickSpaceGeometry().GetAccumulatedRenderTransform();
	OutPoints.Reserve(Local.Num());
	for (const FVector2f& Point : Local)
	{
		const FVector2f Desktop = ToDesktop.TransformPoint(Point);
		OutPoints.Add(FVector2D(Desktop.X, Desktop.Y));
	}
	if (bReversed)
	{
		Algo::Reverse(OutPoints);
	}
	return true;
}

void UWidgetPathLayer::RefreshNodes()
{
	CacheNames();
	ReadStatesTimer = 0.f;
	Push(false);
}

TArray<FString> UWidgetPathLayer::GetWidgetOptions() const
{
	TArray<FString> Names;
	WidgetTreeLookup::GetWidgetNames(this, Names, this);
	return Names;
}

TArray<FString> UWidgetPathLayer::GetStyleOptions() const
{
	TArray<FString> Names;
	Names.Add(TEXT("None"));
	for (const FWidgetPathStyle& Style : Styles)
	{
		if (!Style.Name.IsNone())
		{
			Names.AddUnique(Style.Name.ToString());
		}
	}
	return Names;
}

#undef LOCTEXT_NAMESPACE
