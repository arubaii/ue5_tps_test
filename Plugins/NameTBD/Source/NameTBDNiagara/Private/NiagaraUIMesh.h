// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/SlateRect.h"
#include "Rendering/RenderingCommon.h"
#include "Styling/SlateBrush.h"

#include "NiagaraUIWidget.h"

class FNiagaraDataSet;
class FNiagaraEmitterInstance;
class FSlateWindowElementList;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraRendererProperties;

/**
 * A plane of the system laid on the screen (see ENiagaraUIPlane): Right and Down are the system's axes along the screen's
 * X and Y, Depth points toward the viewer (Right x Down, so the view is the one an editor camera along -Depth sees).
 */
struct FNiagaraUIPlaneAxes
{
	FVector3f Right = FVector3f(1.f, 0.f, 0.f);
	FVector3f Down = FVector3f(0.f, 0.f, -1.f);
	FVector3f Depth = FVector3f(0.f, 1.f, 0.f);

	static FNiagaraUIPlaneAxes Get(ENiagaraUIPlane Plane);

	/** A point or direction of the system in the plane's 2D coordinates (Y down, as Slate's). */
	FVector2f Project(const FVector3f& Value) const { return FVector2f(FVector3f::DotProduct(Value, Right), FVector3f::DotProduct(Value, Down)); }

	/** How far toward the viewer a point is. */
	float DepthOf(const FVector3f& Point) const { return FVector3f::DotProduct(Point, Depth); }
};

/**
 * Where one simulation's particles land on screen: a point of the plane, in Niagara units, to Slate absolute space -
 * Origin + AxisX * X + AxisY * Y. Local-space emitters are mapped through the widget's whole render transform; world-space
 * ones through its scale alone, so what they left behind stays where it was.
 */
struct FNiagaraUISpace
{
	FVector2f Origin = FVector2f::ZeroVector;
	FVector2f AxisX = FVector2f(1.f, 0.f);
	FVector2f AxisY = FVector2f(0.f, 1.f);

	/** How long one Niagara unit is on screen, whichever way it points - for ribbon widths and tiling. */
	float LengthScale = 1.f;

	/** The point of the plane perspective scales about: the system's origin. */
	FVector2f Center = FVector2f::ZeroVector;

	FVector2f ToAbsolute(const FVector2f& PlanePoint) const { return Origin + AxisX * PlanePoint.X + AxisY * PlanePoint.Y; }
	FVector2f ToAbsoluteVector(const FVector2f& PlaneVector) const { return AxisX * PlaneVector.X + AxisY * PlaneVector.Y; }
};

/** One simulation to draw this paint, and where. Handed from UNiagaraUIWidget::PrepareForPaint to the mesh. */
struct FNiagaraUISource
{
	UNiagaraComponent* Component = nullptr;
	FNiagaraUISpace LocalSpace;
	FNiagaraUISpace WorldSpace;
};

/** Everything about the widget the mesh needs, read as it paints. */
struct FNiagaraUIBuildSettings
{
	ENiagaraUIPlane Plane = ENiagaraUIPlane::XZ;
	float PerspectiveDistance = 0.f;
	bool bSmoothRibbons = true;

	/** The widget's color, opacity and its parents' - multiplies every particle. */
	FLinearColor Tint = FLinearColor::White;

	/** Slate absolute space. Particles wholly outside it aren't built. */
	FSlateRect CullRect;

	const TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>>* MaterialRemap = nullptr;

	/** The widget's Particle Image when it has one; null for the built-in soft dot. */
	const FSlateBrush* ParticleImage = nullptr;

	/** Named in warnings. */
	const UObject* Owner = nullptr;
};

/**
 * The meshes one Niagara Particles widget draws: one batch per run of renderers that draw with the same brush, rebuilt
 * from the particle data only when it has changed - a simulation ticked, the widget moved or recolored - so a paused
 * effect, or one painted twice in a frame, costs nothing to build again. Every array is kept between builds and only grows.
 *
 * Game thread only.
 */
class FNiagaraUIMesh
{
public:
	/** Works out the batches for Sources, or keeps the last ones when nothing they came from has changed. */
	void Build(TConstArrayView<FNiagaraUISource> Sources, const FNiagaraUIBuildSettings& Settings);

	/** Draws the batches as custom-vertex elements at LayerId. */
	void Paint(FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

	/** Forgets what was built; the next Build builds. Keeps the memory. */
	void Reset();

private:
	struct FBatch
	{
		TSharedPtr<FSlateBrush> Brush;
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
	};

	/** What a renderer draws with: its UI material, a remap of it, or the Particle Image / soft dot, and their tint and UVs. */
	struct FBrushChoice
	{
		TSharedPtr<FSlateBrush> Brush;
		FLinearColor Tint = FLinearColor::White;
		FVector2f UVMin = FVector2f(0.f, 0.f);
		FVector2f UVSize = FVector2f(1.f, 1.f);
	};

	/** One renderer of one emitter of one simulation, in the order drawn. */
	struct FDrawEntry
	{
		const FNiagaraEmitterInstance* Emitter = nullptr;
		const UNiagaraRendererProperties* Renderer = nullptr;
		const FNiagaraUISpace* Space = nullptr;
		int32 SortOrder = 0;
		int32 EmitterIndex = 0;
		int32 RendererIndex = 0;
		int32 SourceIndex = 0;
		int32 NumParticles = 0;
	};

	struct FSortKey
	{
		float Key = 0.f;
		int32 Index = 0;
	};

	struct FRibbonParticle
	{
		uint64 Ribbon = 0;
		double Order = 0.0;
		int32 Index = 0;
	};

	struct FRibbonPoint
	{
		FVector2f Position = FVector2f::ZeroVector;
		float Width = 0.f;
		FLinearColor Color = FLinearColor::White;
		/** Which particle along its ribbon it is (fractions between, once smoothed) - for Scaled Uniformly UVs. */
		float Param = 0.f;
		float Distance = 0.f;
	};

	/** Gathers what to draw and how it all hashes - which says whether anything changed. */
	uint32 Gather(TConstArrayView<FNiagaraUISource> Sources, const FNiagaraUIBuildSettings& Settings);

	FBrushChoice ChooseBrush(UMaterialInterface* Material, const FNiagaraUIBuildSettings& Settings, const FDrawEntry& Entry);
	FBatch& BatchFor(const TSharedPtr<FSlateBrush>& Brush);

	void AddSprites(const FDrawEntry& Entry, const FNiagaraUIBuildSettings& Settings, const FNiagaraUIPlaneAxes& Axes, const FBrushChoice& Choice, FBatch& Batch);
	void AddRibbons(const FDrawEntry& Entry, const FNiagaraUIBuildSettings& Settings, const FNiagaraUIPlaneAxes& Axes, const FBrushChoice& Choice, FBatch& Batch);
	void AddRibbonStrip(int32 NumParticles, const FDrawEntry& Entry, const FNiagaraUIBuildSettings& Settings, const FBrushChoice& Choice, FBatch& Batch);

	TArray<FBatch> Batches;
	int32 NumBatches = 0;
	int32 NumParticlesDrawn = 0;

	bool bBuilt = false;
	uint32 BuiltHash = 0;

	/** The widget's Particle Image, copied so a batch can hold it safely; replaced when the image changes. */
	TSharedPtr<FSlateBrush> ImageBrush;

	/** What this widget has already warned about - a material, an emitter, a renderer - so each is said once, and cheaply. */
	TSet<const void*> Warned;

	// Scratch, kept between builds so building allocates nothing once warmed up.
	TArray<FDrawEntry> Entries;
	TArray<FSortKey> SortScratch;
	TArray<FRibbonParticle> RibbonScratch;
	TArray<FRibbonPoint> RibbonPoints;
	TArray<FRibbonPoint> SmoothedPoints;
};

namespace NiagaraUIMesh
{
	/** Particles alive now in Component's CPU emitters. */
	int32 CountParticles(UNiagaraComponent* Component);
}
