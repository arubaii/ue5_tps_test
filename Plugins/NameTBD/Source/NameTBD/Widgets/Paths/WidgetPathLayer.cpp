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
#include "NameTBD/Widgets/WidgetEditorHooks.h"
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

	/** Cache slots past the passes': a look's extra strokes, MaxStrokes for each piece slot. */
	constexpr int32 StrokeCacheSlot = 1000;
	constexpr int32 MaxStrokes = 64;

	/** Which part of a look a material instance is for (one instance per line, look and part). */
	enum EMaterialSlot : int32
	{
		MaterialSlotLine = 0,
		MaterialSlotGlow,
		MaterialSlotFlow,
		MaterialSlotStartMarker,
		MaterialSlotEndMarker,
		MaterialSlotStrokes = 16
	};

	/** A picture's square, Size across at Center, its top (local -Y) toward Up: UVs 0..1 in each vertex's Along. */
	void AppendQuad(FWidgetShapeMesh& Mesh, const FVector2f& Center, const FVector2f& Up, float Size, float Coverage)
	{
		FVector2f UpDirection = Up.GetSafeNormal();
		if (UpDirection.IsNearlyZero())
		{
			UpDirection = FVector2f(0.f, -1.f);
		}
		const FVector2f Down = -UpDirection;
		const FVector2f Right(Down.Y, -Down.X);
		const uint32 Base = static_cast<uint32>(Mesh.Vertices.Num());
		const FVector2f Corners[4] = { FVector2f(0.f, 0.f), FVector2f(1.f, 0.f), FVector2f(1.f, 1.f), FVector2f(0.f, 1.f) };
		for (const FVector2f& Corner : Corners)
		{
			const FVector2f Position = Center + Right * ((Corner.X - 0.5f) * Size) + Down * ((Corner.Y - 0.5f) * Size);
			Mesh.Vertices.Add(FWidgetShapeMeshVertex(Position, Coverage, Corner, 1.f));
		}
		Mesh.Indices.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
	}

	/**
	 * Ribbons (a stroke whose width and offset change along it) over the part of Route between From and To (distances along
	 * it), dashed when DashLength > 0 - each dash its own ribbon. WidthAt and OffsetAt take the distance along the whole route.
	 */
	void BuildRibbons(const TArray<FVector2f>& Route, float From, float To, float DashLength, float DashGap, float DashOffset,
		TFunctionRef<float(float)> WidthAt, TFunctionRef<float(float)> OffsetAt, float AntiAlias, float TotalLength, FWidgetShapeMesh& Out)
	{
		if (To <= From || Route.Num() < 2)
		{
			return;
		}
		TArray<FVector2f> Run;
		auto AddRun = [&](float RunStart, float RunEnd)
		{
			if (RunEnd - RunStart <= 0.05f)
			{
				return;
			}
			WidgetPathGeometry::CutBetween(Route, RunStart, RunEnd, Run);
			if (Run.Num() >= 2)
			{
				WidgetPathGeometry::BuildRibbon(Run, [&WidthAt, RunStart](float Distance) { return WidthAt(RunStart + Distance); },
					[&OffsetAt, RunStart](float Distance) { return OffsetAt(RunStart + Distance); }, AntiAlias, RunStart, TotalLength, Out);
			}
		};
		if (DashLength <= 0.f)
		{
			AddRun(From, To);
			return;
		}
		const float Period = FMath::Max(DashLength + (DashGap > 0.f ? DashGap : DashLength), 0.5f);
		const float First = FMath::FloorToFloat((From + DashOffset) / Period) * Period - DashOffset;
		int32 Guard = 0;
		for (float DashStart = First; DashStart < To && Guard < 10000; DashStart += Period, ++Guard)
		{
			AddRun(FMath::Max(DashStart, From), FMath::Min(DashStart + DashLength, To));
		}
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

// The marker and ribbon builders, for other widgets that draw lines (a Curve's arrowheads and taper).
namespace WidgetPathGeometry
{
	float GetMarkerInset(EWidgetPathMarker Marker, float Size, float Width)
	{
		return WidgetPathLayerPrivate::MarkerInset(Marker, Size, Width);
	}

	void BuildEndMarker(EWidgetPathMarker Marker, const FVector2f& Tip, const FVector2f& Direction, float Size, float LineWidth, float AntiAlias,
		float Tolerance, FWidgetShapeMesh& Out)
	{
		WidgetPathLayerPrivate::BuildMarker(Marker, Tip, Direction, Size, LineWidth, AntiAlias, Tolerance, Out);
	}

	void BuildDashedRibbons(const TArray<FVector2f>& Route, float From, float To, float DashLength, float DashGap, float DashOffset,
		TFunctionRef<float(float)> WidthAt, TFunctionRef<float(float)> OffsetAt, float AntiAlias, float TotalLength, FWidgetShapeMesh& Out)
	{
		WidgetPathLayerPrivate::BuildRibbons(Route, From, To, DashLength, DashGap, DashOffset, WidthAt, OffsetAt, AntiAlias, TotalLength, Out);
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
	Materials.Reset();
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
	ResolveNodes();

	// The material instances of lines that are gone, or of looks they no longer show, are let go.
	TSet<uint64> KeepMaterials;
	for (const FEdgeState& State : Edges)
	{
		const int32 Shown[3] = { State.ShownLook, State.PreviousLook, State.Edge.ProgressLook };
		for (const int32 Look : Shown)
		{
			if (!Looks.IsValidIndex(Look))
			{
				continue;
			}
			for (int32 Slot = 0; Slot < WidgetPathLayerPrivate::MaterialSlotStrokes + Looks[Look].Strokes.Num(); ++Slot)
			{
				KeepMaterials.Add(MaterialKey(State.Edge.Key, Look, Slot));
			}
		}
	}
	Materials.Prune(KeepMaterials);

	Invalidate(EInvalidateWidgetReason::Paint);
	Wake();
}

bool SWidgetPathLayer::ResolveNodes() const
{
	bool bChanged = false;
	auto Resolve = [&bChanged](const TWeakObjectPtr<UWidget>& Widget, TWeakPtr<SWidget>& Slate)
	{
		const UWidget* Node = Widget.Get();
		if (!Node)
		{
			return;
		}
		// The node's current Slate widget: a node built after the layer (later in its panel), or built again (the designer's
		// compile, a rebuilt preview), has a new one - or had none when the line was set.
		const TSharedPtr<SWidget> Cached = Node->GetCachedWidget();
		if (Cached != Slate.Pin())
		{
			Slate = Cached;
			bChanged = true;
		}
	};
	for (FEdgeState& State : Edges)
	{
		Resolve(State.Edge.FromWidget, State.Edge.From);
		Resolve(State.Edge.ToWidget, State.Edge.To);
		Resolve(State.Edge.OrbitCenterWidget, State.Edge.OrbitCenter);
	}
	return bChanged;
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

	const bool bResolved = ResolveNodes();
	const uint32 Placement = HashNodePlacement();
	const bool bPlaced = bResolved || Placement != LastPlacementHash;
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

uint64 SWidgetPathLayer::MaterialKey(uint64 EdgeKey, int32 Look, int32 Slot)
{
	return EdgeKey * 0x9E3779B97F4A7C15ull + (static_cast<uint64>(Look + 1) << 12) + static_cast<uint64>(Slot);
}

const TArray<FVector2f>& SWidgetPathLayer::GetLookRoute(const FEdgeState& State, int32 Look, double Now, TArray<FVector2f>& Scratch) const
{
	if (!Looks.IsValidIndex(Look) || !Looks[Look].Distortion.IsActive() || State.Points.Num() < 3)
	{
		return State.Points;
	}
	const uint32 Seed = static_cast<uint32>(State.Edge.Key ^ (State.Edge.Key >> 32));
	WidgetPathGeometry::Distort(State.Points, Looks[Look].Distortion, Now, Seed, Scratch);
	return Scratch;
}

void SWidgetPathLayer::MakeMaterialInputs(const FDrawPiece& Piece, const FWidgetPathLook& Look, float TileLength, FWidgetEffectMaterialInputs& Out) const
{
	const FEdgeState& State = Edges[Piece.Edge];
	Out.Add(TEXT("PathLength"), State.Length);
	Out.Add(TEXT("PathWidth"), Look.Width);
	Out.Add(TEXT("PathRandom"), State.Edge.Random);
	Out.Add(TEXT("PathRevealStart"), Piece.Start);
	Out.Add(TEXT("PathRevealEnd"), Piece.End);
	Out.Add(TEXT("PathProgress"), FMath::Clamp(State.ShownProgress.Value, 0.f, 1.f));
	Out.Add(TEXT("PathHighlighted"), State.Edge.bHighlighted ? 1.f : 0.f);
	Out.Add(TEXT("PathHovered"), State.Edge.bHovered ? 1.f : 0.f);
	Out.Add(TEXT("PathSelected"), State.Edge.bSelected ? 1.f : 0.f);
	Out.Add(TEXT("PathTileLength"), TileLength);
}

void SWidgetPathLayer::PaintImageQuad(uint64 Key, const FWidgetEffectImage& Image, const FWidgetEffectMaterialInputs& Inputs, const FVector2f& Center, const FVector2f& Up,
	float Size, const FLinearColor& Tint, const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 PassLayer, const FWidgetStyle& InWidgetStyle) const
{
	const FSlateBrush* Brush = Materials.GetBrush(Key, Image, &Inputs, AllottedGeometry, FVector2f(Size, Size), *this);
	if (!Brush || Size <= 0.f || Tint.A <= 0.002f)
	{
		return;
	}
	FWidgetShapeMesh Quad;
	WidgetPathLayerPrivate::AppendQuad(Quad, Center, Up, Size, 1.f);
	FLinearColor Color = Tint;
	if (Image.ImageSource == EWidgetImageSource::Texture)
	{
		Color *= Image.Image.GetTint(InWidgetStyle);
	}
	WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Quad, InWidgetStyle.GetColorAndOpacityTint(),
		[Color](const FWidgetShapeMeshVertex&) { return Color; }, Brush, [](const FWidgetShapeMeshVertex& Vertex) { return Vertex.Along; });
}

void SWidgetPathLayer::PaintStrokes(const FDrawPiece& Piece, bool bAbove, const TArray<FVector2f>& Route, const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
	int32 PassLayer, const FWidgetStyle& InWidgetStyle, float PixelScale, double Now) const
{
	using namespace WidgetPathLayerPrivate;

	FEdgeState& State = Edges[Piece.Edge];
	const FWidgetPathLook& Look = Looks[Piece.Look];
	if (Look.Strokes.Num() == 0 || Route.Num() < 2)
	{
		return;
	}
	const float Length = WidgetShapeGeometry::GetLength(Route, /*bClosed=*/false);
	if (Length <= 0.f)
	{
		return;
	}
	const float AntiAlias = 1.f / FMath::Max(PixelScale, 0.01f);
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const float BreathScale = Breath(Look, Now);
	const uint32 RouteHash = HashCombineFast(State.RouteHash, GetTypeHash(Route.Num() > 2 ? Route[Route.Num() / 2] : Route[0]));

	for (int32 StrokeIndex = 0; StrokeIndex < Look.Strokes.Num(); ++StrokeIndex)
	{
		const FWidgetPathStroke& Stroke = Look.Strokes[StrokeIndex];
		if (!Stroke.bEnabled || Stroke.bAboveLine != bAbove || Stroke.Width <= 0.f)
		{
			continue;
		}
		const float Opacity = Piece.Opacity * Look.Opacity * Stroke.Opacity * (Stroke.bBreathe ? BreathScale : 1.f);
		if (Opacity <= 0.002f)
		{
			continue;
		}

		const float Period = Stroke.DashLength + (Stroke.DashGap > 0.f ? Stroke.DashGap : Stroke.DashLength);
		const float DashOffset = Stroke.DashLength > 0.f && Stroke.DashSpeed != 0.f ? -Wrap(Now * Stroke.DashSpeed, Period) : 0.f;
		uint32 Hash = HashCombineFast(RouteHash, Stroke.GetHash());
		Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(FMath::RoundToInt(Piece.Start * 10000.f)), GetTypeHash(FMath::RoundToInt(Piece.End * 10000.f))));
		Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(PixelScale), GetTypeHash(DashOffset)));
		bool bStale = false;
		FWidgetShapeMesh& Mesh = GetCache(State, StrokeCacheSlot + Piece.Slot * MaxStrokes + StrokeIndex, Hash, bStale);
		if (bStale)
		{
			BuildRibbons(Route, Piece.Start * Length, Piece.End * Length, Stroke.DashLength, Stroke.DashGap, DashOffset,
				[&Stroke, Length](float Distance) { return Stroke.Width * FMath::Max(FMath::Lerp(Stroke.StartWidthScale, Stroke.EndWidthScale, FMath::Clamp(Distance / Length, 0.f, 1.f)), 0.f); },
				[&Stroke](float) { return Stroke.Offset; }, AntiAlias, Length, Mesh);
		}
		if (Mesh.IsEmpty())
		{
			continue;
		}

		if (Stroke.Paint == EWidgetPathPaint::Image && Stroke.Image.IsSet())
		{
			const float Repeat = Stroke.ImageTileLength > KINDA_SMALL_NUMBER ? Stroke.ImageTileLength : Length;
			FWidgetEffectMaterialInputs Inputs;
			MakeMaterialInputs(Piece, Look, Repeat, Inputs);
			const FSlateBrush* Brush = Materials.GetBrush(MaterialKey(State.Edge.Key, Piece.Look, MaterialSlotStrokes + StrokeIndex), Stroke.Image, &Inputs, AllottedGeometry,
				FVector2f(Repeat, FMath::Max(Stroke.Width, 1.f)), *this);
			if (Brush)
			{
				FLinearColor Tint = Stroke.Color;
				if (Stroke.Image.ImageSource == EWidgetImageSource::Texture)
				{
					Tint *= Stroke.Image.Image.GetTint(InWidgetStyle);
				}
				Tint.A *= Opacity;
				const float Scroll = Stroke.ImageScrollSpeed != 0.f ? Wrap(Now * Stroke.ImageScrollSpeed, Repeat) : 0.f;
				WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, [Tint](const FWidgetShapeMeshVertex&) { return Tint; }, Brush,
					[Repeat, Scroll](const FWidgetShapeMeshVertex& Vertex) { return FVector2f((Vertex.Along.X - Scroll) / Repeat, Vertex.Along.Y); });
			}
			continue;
		}
		WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, [&Stroke, Opacity, Length](const FWidgetShapeMeshVertex& Vertex)
		{
			FLinearColor Color = Stroke.ColorAt(Vertex.Along.X / Length);
			Color.A *= Opacity;
			return Color;
		});
	}
}

void SWidgetPathLayer::PaintPiece(const FDrawPiece& Piece, int32 Pass, const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements,
	int32 PassLayer, const FWidgetStyle& InWidgetStyle, float PixelScale, float Tolerance, double Now) const
{
	using namespace WidgetPathLayerPrivate;

	FEdgeState& State = Edges[Piece.Edge];
	const FWidgetPathLook& Look = Looks[Piece.Look];
	const float Opacity = Piece.Opacity * Look.Opacity;
	if (State.Length <= 0.f || Opacity <= 0.002f)
	{
		return;
	}

	// The route this look draws along: the line's own, or moved by the look's distortion (waves, lightning).
	TArray<FVector2f> Distorted;
	const TArray<FVector2f>& Route = GetLookRoute(State, Piece.Look, Now, Distorted);
	const bool bDistorted = &Route != &State.Points;
	const float Length = bDistorted ? WidgetShapeGeometry::GetLength(Route, /*bClosed=*/false) : State.Length;
	if (Length <= 0.f || Route.Num() < 2)
	{
		return;
	}
	uint32 RouteHash = State.RouteHash;
	if (bDistorted)
	{
		RouteHash = HashCombineFast(RouteHash, Look.Distortion.GetHash());
		if (Look.Distortion.IsAnimated())
		{
			// Moving: built again every frame it moves.
			RouteHash = HashCombineFast(RouteHash, GetTypeHash(Now));
		}
	}

	const float AntiAlias = 1.f / FMath::Max(PixelScale, 0.01f);
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const float BreathScale = Breath(Look, Now);
	const EWidgetShapeLineJoin Join = State.Edge.Routing.Corner == EWidgetPathCorner::Sharp ? EWidgetShapeLineJoin::Miter : EWidgetShapeLineJoin::Round;
	const int32 CacheSlot = Piece.Slot * PassCount + Pass;
	const bool bVarying = Look.HasVaryingWidth();
	const bool bStartImage = Look.StartMarkerImage.IsSet();
	const bool bEndImage = Look.EndMarkerImage.IsSet();

	// The line is pulled back from an end that carries an arrowhead - only when this piece reaches that end.
	const float StartInset = Piece.Start <= 0.001f && !bStartImage ? MarkerInset(Look.StartMarker, Look.MarkerSize, Look.Width) : 0.f;
	const float EndInset = Piece.End >= 0.999f && !bEndImage ? MarkerInset(Look.EndMarker, Look.MarkerSize, Look.Width) : 0.f;
	TArray<FVector2f> CutPoints;
	const TArray<FVector2f>* LinePoints = &Route;
	if (StartInset > 0.f || EndInset > 0.f)
	{
		WidgetPathGeometry::CutBetween(Route, StartInset, Length - EndInset, CutPoints);
		LinePoints = &CutPoints;
	}
	// Where the piece is drawn, in distance along the route (varying-width lines are built on the whole route, cut there).
	const float DrawFrom = FMath::Max(Piece.Start * Length, StartInset);
	const float DrawTo = FMath::Min(Piece.End * Length, Length - EndInset);
	auto WidthAt = [&Look, Length](float Extra)
	{
		return [&Look, Length, Extra](float Distance) { return Look.Width * Look.WidthScaleAt(Distance / Length) + Extra; };
	};
	auto NoOffset = [](float) { return 0.f; };

	uint32 BaseHash = HashCombineFast(RouteHash, GetTypeHash(Piece.Look));
	BaseHash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(FMath::RoundToInt(Piece.Start * 10000.f)), GetTypeHash(FMath::RoundToInt(Piece.End * 10000.f))));
	BaseHash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(PixelScale), GetTypeHash(StartInset + EndInset * 7.f)));
	if (bVarying)
	{
		BaseHash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(Look.StartWidthScale), GetTypeHash(Look.EndWidthScale)));
	}

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
		const bool bGlowImage = Look.GlowImage.IsSet();
		uint32 Hash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(Look.Width), GetTypeHash(Look.GlowSize)));
		Hash = HashCombineFast(Hash, GetTypeHash(bGlowImage));
		bool bStale = false;
		FWidgetShapeMesh& Mesh = GetCache(State, CacheSlot, Hash, bStale);
		if (bStale)
		{
			if (bGlowImage)
			{
				// A picture across the whole glow: hard edges, its own falloff.
				BuildRibbons(Route, Piece.Start * Length, Piece.End * Length, 0.f, 0.f, 0.f, WidthAt(Look.GlowSize * 2.f), NoOffset, AntiAlias, Length, Mesh);
			}
			else if (bVarying)
			{
				BuildRibbons(Route, Piece.Start * Length, Piece.End * Length, 0.f, 0.f, 0.f, WidthAt(Look.GlowSize), NoOffset, Look.GlowSize + AntiAlias, Length, Mesh);
			}
			else
			{
				BuildPolyline(Route, Look.Width + Look.GlowSize, EWidgetShapeLineCap::Round, EWidgetShapeLineJoin::Round, Look.GlowSize + AntiAlias, Tolerance, Mesh,
					0.f, 0.f, 0.f, Piece.Start, Piece.End);
			}
		}
		FLinearColor Color = Look.GlowColor;
		Color.A *= Opacity * BreathScale;
		if (bGlowImage)
		{
			FWidgetEffectMaterialInputs Inputs;
			MakeMaterialInputs(Piece, Look, Length, Inputs);
			const FSlateBrush* Brush = Materials.GetBrush(MaterialKey(State.Edge.Key, Piece.Look, MaterialSlotGlow), Look.GlowImage, &Inputs, AllottedGeometry,
				FVector2f(Length, Look.Width + Look.GlowSize * 2.f), *this);
			if (Brush)
			{
				if (Look.GlowImage.ImageSource == EWidgetImageSource::Texture)
				{
					Color *= Look.GlowImage.Image.GetTint(InWidgetStyle);
				}
				WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, Solid(Color), Brush,
					[Length](const FWidgetShapeMeshVertex& Vertex) { return FVector2f(Vertex.Along.X / Length, Vertex.Along.Y); });
			}
			return;
		}
		WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, Solid(Color));
		break;
	}

	case PassOutline:
	{
		if (Look.bOutline && Look.OutlineWidth > 0.f && Look.OutlineColor.A > 0.f)
		{
			uint32 Hash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(Look.Width), GetTypeHash(Look.OutlineWidth)));
			Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Look.Cap)));
			bool bStale = false;
			FWidgetShapeMesh& Mesh = GetCache(State, CacheSlot, Hash, bStale);
			if (bStale)
			{
				if (bVarying)
				{
					BuildRibbons(Route, DrawFrom, DrawTo, 0.f, 0.f, 0.f, WidthAt(Look.OutlineWidth * 2.f), NoOffset, AntiAlias, Length, Mesh);
				}
				else
				{
					BuildPolyline(*LinePoints, Look.Width + Look.OutlineWidth * 2.f, Look.Cap, Join, AntiAlias, Tolerance, Mesh, 0.f, 0.f, 0.f, Piece.Start, Piece.End);
				}
			}
			FLinearColor Color = Look.OutlineColor;
			Color.A *= Opacity;
			WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, Solid(Color));
		}
		// Strokes under the line, over its casing.
		PaintStrokes(Piece, /*bAbove=*/false, Route, AllottedGeometry, OutDrawElements, PassLayer, InWidgetStyle, PixelScale, Now);
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
			if (bVarying)
			{
				BuildRibbons(Route, DrawFrom, DrawTo, Look.DashLength, Look.DashGap, DashOffset, WidthAt(0.f), NoOffset, AntiAlias, Length, Mesh);
			}
			else
			{
				BuildPolyline(*LinePoints, Look.Width, Look.Cap, Join, AntiAlias, Tolerance, Mesh, Look.DashLength, Look.DashGap, DashOffset, Piece.Start, Piece.End);
			}
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
				// A texture as it is; a material through an instance of its own for this line, with the live values.
				FWidgetEffectMaterialInputs Inputs;
				MakeMaterialInputs(Piece, Look, Repeat, Inputs);
				Brush = Materials.GetBrush(MaterialKey(State.Edge.Key, Piece.Look, MaterialSlotLine), Look.GetLineImage(), &Inputs, AllottedGeometry,
					FVector2f(Repeat, FMath::Max(Look.Width, 1.f)), *this);
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
				if (bVarying)
				{
					BuildRibbons(Route, DrawFrom, DrawTo, 0.f, 0.f, 0.f, WidthAt(0.f), NoOffset, AntiAlias, Length, Mesh);
				}
				else
				{
					BuildPolyline(*LinePoints, Look.Width, Look.Cap, Join, AntiAlias, Tolerance, Mesh, 0.f, 0.f, 0.f, Piece.Start, Piece.End);
				}
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

		// Dots or arrows travelling along the piece, fading in and out at its ends - or a picture for each.
		const bool bFlowImage = Look.FlowImage.IsSet();
		const float From = Piece.Start * Length;
		const float To = Piece.End * Length;
		uint32 Hash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(Look.FlowSize), GetTypeHash(Spacing)));
		Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(Travel), GetTypeHash(static_cast<uint8>(Look.Flow))));
		Hash = HashCombineFast(Hash, GetTypeHash(bFlowImage));
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
				if (!WidgetPathGeometry::SampleAt(Route, Distance, Point, Direction))
				{
					continue;
				}
				const float EdgeFade = FMath::Clamp(FMath::Min(Distance - From, To - Distance) / FMath::Max(Fade, 1.f), 0.f, 1.f);
				if (EdgeFade <= 0.01f)
				{
					continue;
				}
				// Arrows (and pictures) point the way the flow goes.
				const FVector2f Way = Look.FlowSpeed < 0.f ? -Direction : Direction;
				if (bFlowImage)
				{
					AppendQuad(Mesh, Point, Way, Look.FlowSize, EdgeFade);
					continue;
				}
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
		if (bFlowImage)
		{
			FWidgetEffectMaterialInputs Inputs;
			MakeMaterialInputs(Piece, Look, Spacing, Inputs);
			const FSlateBrush* Brush = Materials.GetBrush(MaterialKey(State.Edge.Key, Piece.Look, MaterialSlotFlow), Look.FlowImage, &Inputs, AllottedGeometry,
				FVector2f(Look.FlowSize, Look.FlowSize), *this);
			if (Brush)
			{
				if (Look.FlowImage.ImageSource == EWidgetImageSource::Texture)
				{
					Color *= Look.FlowImage.Image.GetTint(InWidgetStyle);
				}
				WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, Solid(Color), Brush,
					[](const FWidgetShapeMeshVertex& Vertex) { return Vertex.Along; });
			}
			return;
		}
		WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, Mesh, WidgetTint, Solid(Color));
		break;
	}

	case PassMarkers:
	{
		// Strokes over the line and its flow, under the markers.
		PaintStrokes(Piece, /*bAbove=*/true, Route, AllottedGeometry, OutDrawElements, PassLayer, InWidgetStyle, PixelScale, Now);

		const bool bStart = Piece.Start <= 0.001f && (bStartImage || Look.StartMarker != EWidgetPathMarker::None);
		const bool bEnd = Piece.End >= 0.999f && (bEndImage || Look.EndMarker != EWidgetPathMarker::None);
		if (!bStart && !bEnd)
		{
			return;
		}

		// Pictures: their top at the end of the line, pointing out of it - toward the node.
		if ((bStart && bStartImage) || (bEnd && bEndImage))
		{
			FWidgetEffectMaterialInputs Inputs;
			MakeMaterialInputs(Piece, Look, Length, Inputs);
			FVector2f Point;
			FVector2f Direction;
			if (bStart && bStartImage && WidgetPathGeometry::SampleAt(Route, 0.f, Point, Direction))
			{
				FLinearColor Color = Look.ColorAt(0.f);
				Color.A *= Opacity;
				PaintImageQuad(MaterialKey(State.Edge.Key, Piece.Look, MaterialSlotStartMarker), Look.StartMarkerImage, Inputs, Route[0] + Direction * (Look.MarkerSize * 0.5f),
					-Direction, Look.MarkerSize, Color, AllottedGeometry, OutDrawElements, PassLayer, InWidgetStyle);
			}
			if (bEnd && bEndImage && WidgetPathGeometry::SampleAt(Route, Length, Point, Direction))
			{
				FLinearColor Color = Look.ColorAt(1.f);
				Color.A *= Opacity;
				PaintImageQuad(MaterialKey(State.Edge.Key, Piece.Look, MaterialSlotEndMarker), Look.EndMarkerImage, Inputs, Route.Last() - Direction * (Look.MarkerSize * 0.5f),
					Direction, Look.MarkerSize, Color, AllottedGeometry, OutDrawElements, PassLayer, InWidgetStyle);
			}
		}

		const bool bStartShape = bStart && !bStartImage && Look.StartMarker != EWidgetPathMarker::None;
		const bool bEndShape = bEnd && !bEndImage && Look.EndMarker != EWidgetPathMarker::None;
		if (!bStartShape && !bEndShape)
		{
			return;
		}
		uint32 Hash = HashCombineFast(BaseHash, HashCombineFast(GetTypeHash(Look.MarkerSize), GetTypeHash(Look.Width)));
		Hash = HashCombineFast(Hash, HashCombineFast(GetTypeHash(static_cast<uint8>(Look.StartMarker)), GetTypeHash(static_cast<uint8>(Look.EndMarker))));
		// Two caches taken at once: room for both first, so adding the second can't move the first.
		State.Caches.Reserve(State.Caches.Num() + 2);
		bool bStale = false;
		FWidgetShapeMesh& StartMesh = GetCache(State, CacheSlot, Hash, bStale);
		if (bStale && bStartShape)
		{
			FVector2f Point;
			FVector2f Direction;
			if (WidgetPathGeometry::SampleAt(Route, 0.f, Point, Direction))
			{
				BuildMarker(Look.StartMarker, Route[0], -Direction, Look.MarkerSize, Look.Width, AntiAlias, Tolerance, StartMesh);
			}
		}
		bool bEndStale = false;
		FWidgetShapeMesh& EndMesh = GetCache(State, CacheSlot + PassCount * 8, Hash, bEndStale);
		if (bEndStale && bEndShape)
		{
			FVector2f Point;
			FVector2f Direction;
			if (WidgetPathGeometry::SampleAt(Route, Length, Point, Direction))
			{
				BuildMarker(Look.EndMarker, Route.Last(), Direction, Look.MarkerSize, Look.Width, AntiAlias, Tolerance, EndMesh);
			}
		}
		if (bStartShape)
		{
			FLinearColor Color = Look.ColorAt(0.f);
			Color.A *= Opacity;
			WidgetEffectsDraw::PaintMesh(OutDrawElements, PassLayer, AllottedGeometry, StartMesh, WidgetTint, Solid(Color));
		}
		if (bEndShape)
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

	// Nodes built or rebuilt since the lines were set are found now, not on the next sync.
	ResolveNodes();

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
	bool bLookChanged = false;
	if (ChangedName == GET_MEMBER_NAME_CHECKED(UWidgetPathLayer, Preset))
	{
		WidgetPathPresets::Apply(Preset, Routing, Styles, Rules);
		if (Preset != EWidgetPathPreset::Custom)
		{
			PresetAsset = nullptr;
		}
		bLookChanged = true;
	}
	else if (ChangedName == GET_MEMBER_NAME_CHECKED(UWidgetPathLayer, PresetAsset))
	{
		if (PresetAsset)
		{
			ApplyPresetAsset(PresetAsset);
		}
		bLookChanged = true;
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UWidgetPathLayer, Routing) || MemberName == GET_MEMBER_NAME_CHECKED(UWidgetPathLayer, Styles)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetPathLayer, Rules))
	{
		Preset = EWidgetPathPreset::Custom;
		PresetAsset = nullptr;
		bLookChanged = true;
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// The Details panel edits the designer's preview copy and hands only the edited value on to the Widget Blueprint's own
	// widget: the routing, styles and rules a preset just wrote would be lost at the next compile. They are written there too.
	if (bLookChanged)
	{
		SyncLookToTemplate();
	}
}
#endif

void UWidgetPathLayer::SyncLookToTemplate()
{
#if WITH_EDITOR
	UWidgetPathLayer* Template = Cast<UWidgetPathLayer>(WidgetEditorHooks::FindDesignerTemplate(this));
	if (!Template)
	{
		return;
	}
	Template->Modify();
	Template->Preset = Preset;
	Template->PresetAsset = PresetAsset;
	Template->Routing = Routing;
	Template->Styles = Styles;
	Template->Rules = Rules;
	Template->MarkPackageDirty();
#endif
}

void UWidgetPathLayer::PostLoad()
{
	Super::PostLoad();
	NormalizeStates();
}

void UWidgetPathLayer::NormalizeStates()
{
	WidgetPathStates::Normalize(DefaultState, DefaultCustomState);
	WidgetPathStates::Normalize(UnlockedNodeState, UnlockedCustomState);
	WidgetPathStates::Normalize(AvailableNodeState, AvailableCustomState);
	for (FWidgetPathNodeState& Entry : Nodes)
	{
		WidgetPathStates::Normalize(Entry.NodeState, Entry.CustomState);
	}
	for (FWidgetPathStyleRule& Rule : Rules)
	{
		WidgetPathStates::NormalizeFilter(Rule.FromNodeState, Rule.FromCustomState);
		WidgetPathStates::NormalizeFilter(Rule.ToNodeState, Rule.ToCustomState);
	}
}

FName UWidgetPathLayer::GetDefaultStateName() const
{
	const FName Name = WidgetPathStates::Resolve(DefaultState, DefaultCustomState);
	return Name.IsNone() ? WidgetPathPresets::Locked() : Name;
}

FName UWidgetPathLayer::GetUnlockedStateName() const
{
	const FName Name = WidgetPathStates::Resolve(UnlockedNodeState, UnlockedCustomState);
	return Name.IsNone() ? WidgetPathPresets::Unlocked() : Name;
}

FName UWidgetPathLayer::GetAvailableStateName() const
{
	const FName Name = WidgetPathStates::Resolve(AvailableNodeState, AvailableCustomState);
	return Name.IsNone() ? WidgetPathPresets::Available() : Name;
}

bool UWidgetPathLayer::CanRouteThrough(FName StateName) const
{
	return !bUnavailableBlocksRoutes || StateName != WidgetPathPresets::Unavailable();
}

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
			AuthoredStates.Add(Entry.Widget, Entry.GetState());
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
		return GetDefaultStateName();
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
	return GetDefaultStateName();
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
	if (!bDeriveAvailable)
	{
		return;
	}

	// Derived from the raw states only, so one Available doesn't make the next one Available. Only a node in the default
	// state becomes Available - an Unavailable one stays so.
	const FName UnlockedName = GetUnlockedStateName();
	const FName DefaultName = GetDefaultStateName();
	const FName AvailableName = GetAvailableStateName();
	const TMap<UWidget*, FName> Raw = OutStates;
	for (const FEdgeInfo& Edge : InEdges)
	{
		const FName FromState = Raw.FindRef(Edge.From);
		const FName ToState = Raw.FindRef(Edge.To);
		if (FromState == UnlockedName && ToState == DefaultName)
		{
			OutStates.Add(Edge.To, AvailableName);
		}
		if (!bRoutesFollowDirection && ToState == UnlockedName && FromState == DefaultName)
		{
			OutStates.Add(Edge.From, AvailableName);
		}
	}
}

TArray<UWidget*> UWidgetPathLayer::FindRouteIn(const TArray<FEdgeInfo>& InEdges, const TMap<UWidget*, FName>& States, UWidget* Target) const
{
	TArray<UWidget*> Route;
	const FName UnlockedName = GetUnlockedStateName();
	if (!Target || States.FindRef(Target) == UnlockedName || !CanRouteThrough(States.FindRef(Target)))
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
			const FName NextState = States.FindRef(Next);
			if (NextState != UnlockedName && !CanRouteThrough(NextState))
			{
				// An unavailable node is a dead end: no route goes through it.
				continue;
			}
			TowardTarget.Add(Next, Current);
			if (NextState == UnlockedName)
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
		const FName RuleFrom = Rule.GetFromState();
		const FName RuleTo = Rule.GetToState();
		const bool bForward = StateMatches(RuleFrom, FromState) && StateMatches(RuleTo, ToState);
		const bool bBackward = Rule.bEitherWay && StateMatches(RuleFrom, ToState) && StateMatches(RuleTo, FromState);
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

	const int32 DefaultProgressLook = FindStyleIndex(GetUnlockedStateName());
	UWidget* HoveredWidget = HoveredNode.Get();
	TArray<FWidgetPathSlateEdge> SlateEdges;
	SlateEdges.Reserve(EdgeInfos.Num());
	for (const FEdgeInfo& Edge : EdgeInfos)
	{
		const FWidgetPathRouting& EdgeRouting = Edge.Routing ? *Edge.Routing : Routing;
		FWidgetPathSlateEdge& Slate = SlateEdges.AddDefaulted_GetRef();
		Slate.Key = Edge.Key;
		// The Slate layer finds the nodes' Slate widgets from these itself, whenever they are built or rebuilt.
		Slate.FromWidget = Edge.From;
		Slate.ToWidget = Edge.To;
		Slate.From = Edge.From->GetCachedWidget();
		Slate.To = Edge.To->GetCachedWidget();
		Slate.FromShape = ReadNodeShape(Edge.From, EdgeRouting);
		Slate.ToShape = ReadNodeShape(Edge.To, EdgeRouting);
		Slate.Routing = EdgeRouting;
		if (UWidget* Center = FindNode(EdgeRouting.OrbitCenter))
		{
			Slate.OrbitCenterWidget = Center;
			Slate.OrbitCenter = Center->GetCachedWidget();
		}
		const bool bLit = Lit.Contains(Edge.Key);
		Slate.Look = ResolveStyle(Edge, States, bLit);
		Slate.bHighlighted = bLit;
		Slate.bHovered = HoveredWidget && (Edge.From == HoveredWidget || Edge.To == HoveredWidget);
		Slate.bSelected = Selected && (Edge.From == Selected || Edge.To == Selected);
		Slate.Random = static_cast<float>((Edge.Key ^ (Edge.Key >> 29)) % 10007ull) / 10006.f;
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
				EWidgetPathState ReadKind = EWidgetPathState::Locked;
				FName ReadCustom;
				const bool bReports = IWidgetPathNode::Execute_GetWidgetPathNodeState(Node, ReadKind, ReadCustom);
				const FName Read = bReports ? WidgetPathStates::Resolve(ReadKind, ReadCustom) : NAME_None;
				FNodeRecord& Record = NodeRecords.FindOrAdd(TObjectKey<UWidget>(Node));
				if (Record.ReadState != Read)
				{
					Record.Widget = Node;
					Record.ReadState = Read;
					NoteStateChange(Node);
					FName ChangedCustom;
					const EWidgetPathState Changed = WidgetPathStates::FromName(GetRawState(Node), ChangedCustom);
					OnNodeStateChanged.Broadcast(Node, Changed, ChangedCustom);
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

bool UWidgetPathLayer::SetNodeStateName(UWidget* Node, FName StateName)
{
	if (!Node)
	{
		return false;
	}
	FNodeRecord& Record = NodeRecords.FindOrAdd(TObjectKey<UWidget>(Node));
	Record.Widget = Node;
	if (Record.SetState == StateName)
	{
		return false;
	}
	Record.SetState = StateName;
	NoteStateChange(Node);
	FName ChangedCustom;
	const EWidgetPathState Changed = WidgetPathStates::FromName(GetRawState(Node), ChangedCustom);
	OnNodeStateChanged.Broadcast(Node, Changed, ChangedCustom);
	return true;
}

void UWidgetPathLayer::SetNodeState(UWidget* Node, EWidgetPathState State, FName CustomState, bool bAnimate)
{
	if (SetNodeStateName(Node, WidgetPathStates::Resolve(State, CustomState)))
	{
		Push(bAnimate);
	}
}

void UWidgetPathLayer::SetNodeStates(const TArray<UWidget*>& InNodes, EWidgetPathState State, FName CustomState, bool bAnimate)
{
	const FName StateName = WidgetPathStates::Resolve(State, CustomState);
	bool bChanged = false;
	for (UWidget* Node : InNodes)
	{
		bChanged |= SetNodeStateName(Node, StateName);
	}
	if (bChanged)
	{
		Push(bAnimate);
	}
}

FName UWidgetPathLayer::GetNodeStateName(UWidget* Node) const
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

EWidgetPathState UWidgetPathLayer::GetNodeState(UWidget* Node, FName& CustomState) const
{
	return WidgetPathStates::FromName(GetNodeStateName(Node), CustomState);
}

bool UWidgetPathLayer::IsNodeInState(UWidget* Node, EWidgetPathState State, FName CustomState) const
{
	const FName Wanted = WidgetPathStates::Resolve(State, CustomState);
	return Node && !Wanted.IsNone() && GetNodeStateName(Node) == Wanted;
}

TArray<UWidget*> UWidgetPathLayer::GetNodesInState(EWidgetPathState State, FName CustomState) const
{
	const FName Wanted = WidgetPathStates::Resolve(State, CustomState);
	TArray<FEdgeInfo> EdgeInfos;
	GatherEdges(EdgeInfos);
	TMap<UWidget*, FName> States;
	ComputeStates(EdgeInfos, States);
	TArray<UWidget*> Out;
	for (const TPair<UWidget*, FName>& Entry : States)
	{
		if (!Wanted.IsNone() && Entry.Value == Wanted)
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
	if (Preset != EWidgetPathPreset::Custom)
	{
		PresetAsset = nullptr;
	}
	bLooksDirty = true;
	Push(false);
}

void UWidgetPathLayer::ApplyPresetAsset(UWidgetPathPresetAsset* InPresetAsset)
{
	if (!InPresetAsset)
	{
		return;
	}
	PresetAsset = InPresetAsset;
	Preset = EWidgetPathPreset::Custom;
	if (InPresetAsset->bApplyRouting)
	{
		Routing = InPresetAsset->Routing;
	}
	Styles = InPresetAsset->Styles;
	Rules = InPresetAsset->Rules;
	bLooksDirty = true;
	Push(false);
}

void UWidgetPathLayer::CopyToPresetAsset(UWidgetPathPresetAsset* Target) const
{
	if (!Target)
	{
		return;
	}
	Target->Modify();
	Target->Routing = Routing;
	Target->Styles = Styles;
	Target->Rules = Rules;
	Target->MarkPackageDirty();
}

void UWidgetPathLayer::SaveAsPreset()
{
#if WITH_EDITOR
	const FString DefaultName = FString::Printf(TEXT("WPP_%s"), *GetName());
	UWidgetPathPresetAsset* Created = Cast<UWidgetPathPresetAsset>(WidgetEditorHooks::CreateAsset(UWidgetPathPresetAsset::StaticClass(), DefaultName));
	if (!Created)
	{
		return;
	}
	CopyToPresetAsset(Created);
	Modify();
	PresetAsset = Created;
	Preset = EWidgetPathPreset::Custom;
	SyncLookToTemplate();
#endif
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
