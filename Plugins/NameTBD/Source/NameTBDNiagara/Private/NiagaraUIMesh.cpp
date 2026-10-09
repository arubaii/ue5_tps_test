// Copyright AR GameSystems 2026. All rights reserved.

#include "NiagaraUIMesh.h"

#include "NiagaraUILog.h"
#include "NiagaraUIRuntime.h"

#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/DrawElements.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("Niagara UI"), STATGROUP_NiagaraUI, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Build Meshes"), STAT_NiagaraUIBuild, STATGROUP_NiagaraUI);
DECLARE_CYCLE_STAT(TEXT("Paint"), STAT_NiagaraUIPaint, STATGROUP_NiagaraUI);
DECLARE_DWORD_COUNTER_STAT(TEXT("Particles Drawn"), STAT_NiagaraUIParticles, STATGROUP_NiagaraUI);
DECLARE_DWORD_COUNTER_STAT(TEXT("Draw Elements"), STAT_NiagaraUIDrawElements, STATGROUP_NiagaraUI);
DECLARE_DWORD_COUNTER_STAT(TEXT("Meshes Rebuilt"), STAT_NiagaraUIRebuilds, STATGROUP_NiagaraUI);

namespace NiagaraUIMeshPrivate
{
	/** Vertices are built in absolute space already, so they go through no transform of their own. */
	const FSlateRenderTransform& GetIdentity()
	{
		static const FSlateRenderTransform Identity;
		return Identity;
	}

	/** A sprite's size when its system writes none. */
	constexpr float DefaultSpriteSize = 10.f;

	/** How long a ribbon segment may be on screen, in pixels, before Smooth Ribbons bends it through more points. */
	constexpr float SmoothStepPixels = 6.f;
	constexpr int32 MaxSmoothSteps = 8;

	/** How much a ribbon may widen at a corner to keep its width along both segments; sharper corners are cut. */
	constexpr float MaxMiter = 2.f;

	/** How much bigger a point Depth toward the viewer is drawn, for a Perspective Distance. Never through the eye. */
	float PerspectiveScale(float Depth, float Distance)
	{
		return Distance / FMath::Max(Distance - Depth, Distance * 0.05f);
	}

	void HashFloat(uint32& Hash, float Value)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Value));
	}

	void HashInt(uint32& Hash, int32 Value)
	{
		Hash = HashCombineFast(Hash, GetTypeHash(Value));
	}

	void HashPointer(uint32& Hash, const void* Pointer)
	{
		Hash = HashCombineFast(Hash, PointerHash(Pointer));
	}

	void HashVector(uint32& Hash, const FVector2f& Value)
	{
		HashFloat(Hash, Value.X);
		HashFloat(Hash, Value.Y);
	}

	void HashSpace(uint32& Hash, const FNiagaraUISpace& Space)
	{
		HashVector(Hash, Space.Origin);
		HashVector(Hash, Space.AxisX);
		HashVector(Hash, Space.AxisY);
		HashVector(Hash, Space.Center);
	}

	/** Whether the box Min - Max misses Rect entirely. */
	bool IsOutside(const FVector2f& Min, const FVector2f& Max, const FSlateRect& Rect)
	{
		return Max.X < Rect.Left || Min.X > Rect.Right || Max.Y < Rect.Top || Min.Y > Rect.Bottom;
	}

	/** The point T of the way from P1 to P2 on a Catmull-Rom curve through P0 - P3. */
	FVector2f CatmullRom(const FVector2f& P0, const FVector2f& P1, const FVector2f& P2, const FVector2f& P3, float T)
	{
		const float T2 = T * T;
		const float T3 = T2 * T;
		return (P1 * 2.f
			+ (P2 - P0) * T
			+ (P0 * 2.f - P1 * 5.f + P2 * 4.f - P3) * T2
			+ (P1 * 3.f - P0 - P2 * 3.f + P3) * T3) * 0.5f;
	}

	/** Component's running system instance, or null while it has none. */
	FNiagaraSystemInstance* GetSystemInstance(UNiagaraComponent* Component)
	{
		if (!Component)
		{
			return nullptr;
		}
		auto Controller = Component->GetSystemInstanceController();
		return Controller.IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
	}

	/** The emitter's name, for warnings. */
	FString GetEmitterName(const FNiagaraEmitterInstance& Emitter)
	{
		const FVersionedNiagaraEmitter Versioned = Emitter.GetVersionedEmitter();
		return Versioned.Emitter ? Versioned.Emitter->GetUniqueEmitterName() : FString(TEXT("(unnamed)"));
	}

	/** "Niagara Particles 'Sparkles' (NS_Sparkle)", for warnings. */
	FString DescribeOwner(const UObject* Owner, const UNiagaraComponent* Component)
	{
		const UObject* System = Component ? Component->GetAsset() : nullptr;
		return FString::Printf(TEXT("Niagara Particles '%s' (%s)"), Owner ? *Owner->GetName() : TEXT("?"), System ? *System->GetName() : TEXT("no system"));
	}
}

// =====================================================================================================
// FNiagaraUIPlaneAxes
// =====================================================================================================

FNiagaraUIPlaneAxes FNiagaraUIPlaneAxes::Get(ENiagaraUIPlane Plane)
{
	FNiagaraUIPlaneAxes Axes;
	switch (Plane)
	{
	case ENiagaraUIPlane::XY:
		Axes.Right = FVector3f(1.f, 0.f, 0.f);
		Axes.Down = FVector3f(0.f, 1.f, 0.f);
		break;
	case ENiagaraUIPlane::YZ:
		Axes.Right = FVector3f(0.f, 1.f, 0.f);
		Axes.Down = FVector3f(0.f, 0.f, -1.f);
		break;
	case ENiagaraUIPlane::XZ:
	default:
		Axes.Right = FVector3f(1.f, 0.f, 0.f);
		Axes.Down = FVector3f(0.f, 0.f, -1.f);
		break;
	}
	// Right x Down: a turn of the widget by an angle on screen is a turn of the system by the same angle about this axis.
	Axes.Depth = FVector3f::CrossProduct(Axes.Right, Axes.Down);
	return Axes;
}

// =====================================================================================================
// FNiagaraUIMesh
// =====================================================================================================

uint32 FNiagaraUIMesh::Gather(TConstArrayView<FNiagaraUISource> Sources, const FNiagaraUIBuildSettings& Settings)
{
	using namespace NiagaraUIMeshPrivate;

	Entries.Reset();

	uint32 Hash = 0;
	HashInt(Hash, static_cast<int32>(Settings.Plane));
	HashFloat(Hash, Settings.PerspectiveDistance);
	HashInt(Hash, Settings.bSmoothRibbons ? 1 : 0);
	HashFloat(Hash, Settings.Tint.R);
	HashFloat(Hash, Settings.Tint.G);
	HashFloat(Hash, Settings.Tint.B);
	HashFloat(Hash, Settings.Tint.A);
	HashFloat(Hash, Settings.CullRect.Left);
	HashFloat(Hash, Settings.CullRect.Top);
	HashFloat(Hash, Settings.CullRect.Right);
	HashFloat(Hash, Settings.CullRect.Bottom);
	HashPointer(Hash, ImageBrush.Get());
	if (Settings.MaterialRemap)
	{
		for (const TPair<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInterface>>& Pair : *Settings.MaterialRemap)
		{
			HashPointer(Hash, Pair.Key.Get());
			HashPointer(Hash, Pair.Value.Get());
		}
	}

	for (int32 SourceIndex = 0; SourceIndex < Sources.Num(); ++SourceIndex)
	{
		const FNiagaraUISource& Source = Sources[SourceIndex];
		FNiagaraSystemInstance* SystemInstance = GetSystemInstance(Source.Component);
		if (!SystemInstance)
		{
			continue;
		}

		// This frame's simulation may still be finishing on a worker thread (Niagara lets async work run to the end of the
		// frame); its particle data is only whole, and only safe to read, once it is done.
		SystemInstance->WaitForConcurrentTickAndFinalize();

		HashPointer(Hash, Source.Component);
		HashSpace(Hash, Source.LocalSpace);
		HashSpace(Hash, Source.WorldSpace);

		int32 EmitterIndex = INDEX_NONE;
		for (const auto& EmitterRef : SystemInstance->GetEmitters())
		{
			++EmitterIndex;
			const FNiagaraEmitterInstance& Emitter = EmitterRef.Get();
			if (Emitter.IsDisabled())
			{
				continue;
			}

			const FVersionedNiagaraEmitterData* EmitterData = Emitter.GetVersionedEmitter().GetEmitterData();
			if (!EmitterData)
			{
				bool bAlreadyWarned = false;
				Warned.Add(&Emitter, &bAlreadyWarned);
				if (!bAlreadyWarned)
				{
					NiagaraUIRuntime::WarnOnce(FString::Printf(TEXT("%s: emitter %d isn't a regular emitter (a lightweight emitter?) and isn't drawn in UI."),
						*DescribeOwner(Settings.Owner, Source.Component), EmitterIndex));
				}
				continue;
			}

			if (EmitterData->SimTarget != ENiagaraSimTarget::CPUSim)
			{
				bool bAlreadyWarned = false;
				Warned.Add(EmitterData, &bAlreadyWarned);
				if (!bAlreadyWarned)
				{
					NiagaraUIRuntime::WarnOnce(FString::Printf(TEXT("%s: emitter '%s' simulates on the GPU, which UI can't read back - it isn't drawn. Set its Sim Target to CPUSim to draw it in UI."),
						*DescribeOwner(Settings.Owner, Source.Component), *GetEmitterName(Emitter)));
				}
				continue;
			}

			const FNiagaraDataBuffer* Buffer = Emitter.GetParticleData().GetCurrentData();
			const int32 NumParticles = Buffer ? static_cast<int32>(Buffer->GetNumInstances()) : 0;

			// A new buffer every time the emitter simulates: the same buffer and count mean nothing moved.
			HashPointer(Hash, Buffer);
			HashInt(Hash, NumParticles);
			if (NumParticles <= 0)
			{
				continue;
			}

			const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
			for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
			{
				const UNiagaraRendererProperties* Renderer = Renderers[RendererIndex];
				if (!Renderer || !Renderer->GetIsEnabled())
				{
					continue;
				}

				if (!Renderer->IsA<UNiagaraSpriteRendererProperties>() && !Renderer->IsA<UNiagaraRibbonRendererProperties>())
				{
					bool bAlreadyWarned = false;
					Warned.Add(Renderer, &bAlreadyWarned);
					if (!bAlreadyWarned)
					{
						NiagaraUIRuntime::WarnOnce(FString::Printf(TEXT("%s: emitter '%s' has a %s, which UI doesn't draw - only Sprite and Ribbon renderers are."),
							*DescribeOwner(Settings.Owner, Source.Component), *GetEmitterName(Emitter), *Renderer->GetClass()->GetName()));
					}
					continue;
				}

				FDrawEntry& Entry = Entries.AddDefaulted_GetRef();
				Entry.Emitter = &Emitter;
				Entry.Renderer = Renderer;
				Entry.Space = EmitterData->bLocalSpace ? &Source.LocalSpace : &Source.WorldSpace;
				Entry.SortOrder = Renderer->SortOrderHint;
				Entry.EmitterIndex = EmitterIndex;
				Entry.RendererIndex = RendererIndex;
				Entry.SourceIndex = SourceIndex;
				Entry.NumParticles = NumParticles;
			}
		}
	}

	// Drawn in the renderers' Sort Order Hint, then as the system lists them. The same renderer of every simulation of a
	// system - the main one and its bursts - is drawn together, so they share one batch.
	Entries.Sort([](const FDrawEntry& A, const FDrawEntry& B)
	{
		if (A.SortOrder != B.SortOrder)
		{
			return A.SortOrder < B.SortOrder;
		}
		if (A.EmitterIndex != B.EmitterIndex)
		{
			return A.EmitterIndex < B.EmitterIndex;
		}
		if (A.RendererIndex != B.RendererIndex)
		{
			return A.RendererIndex < B.RendererIndex;
		}
		return A.SourceIndex < B.SourceIndex;
	});

	return Hash;
}

void FNiagaraUIMesh::Build(TConstArrayView<FNiagaraUISource> Sources, const FNiagaraUIBuildSettings& Settings)
{
	using namespace NiagaraUIMeshPrivate;
	SCOPE_CYCLE_COUNTER(STAT_NiagaraUIBuild);
	TRACE_CPUPROFILER_EVENT_SCOPE(FNiagaraUIMesh::Build);

	// The Particle Image, copied only when it changes; a batch keeps its own reference to the copy it drew with.
	if (Settings.ParticleImage)
	{
		if (!ImageBrush.IsValid() || !(*ImageBrush == *Settings.ParticleImage))
		{
			ImageBrush = MakeShared<FSlateBrush>(*Settings.ParticleImage);
		}
	}
	else
	{
		ImageBrush.Reset();
	}

	const uint32 Hash = Gather(Sources, Settings);
	if (bBuilt && Hash == BuiltHash)
	{
		return;
	}
	bBuilt = true;
	BuiltHash = Hash;
	INC_DWORD_STAT(STAT_NiagaraUIRebuilds);

	NumBatches = 0;
	NumParticlesDrawn = 0;

	const FNiagaraUIPlaneAxes Axes = FNiagaraUIPlaneAxes::Get(Settings.Plane);
	for (const FDrawEntry& Entry : Entries)
	{
		UMaterialInterface* Material = nullptr;
		bool bRibbon = false;
		if (const UNiagaraSpriteRendererProperties* SpriteRenderer = Cast<UNiagaraSpriteRendererProperties>(Entry.Renderer))
		{
			Material = SpriteRenderer->Material;
		}
		else if (const UNiagaraRibbonRendererProperties* RibbonRenderer = Cast<UNiagaraRibbonRendererProperties>(Entry.Renderer))
		{
			Material = RibbonRenderer->Material;
			bRibbon = true;
		}

		const FBrushChoice Choice = ChooseBrush(Material, Settings, Entry);
		if (!Choice.Brush.IsValid())
		{
			continue;
		}

		FBatch& Batch = BatchFor(Choice.Brush);
		if (bRibbon)
		{
			AddRibbons(Entry, Settings, Axes, Choice, Batch);
		}
		else
		{
			AddSprites(Entry, Settings, Axes, Choice, Batch);
		}
	}

	// Batches not used this time let go of their brushes, so a material no longer drawn can be released.
	for (int32 BatchIndex = NumBatches; BatchIndex < Batches.Num(); ++BatchIndex)
	{
		Batches[BatchIndex].Brush.Reset();
	}
}

void FNiagaraUIMesh::Paint(FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraUIPaint);

	for (int32 BatchIndex = 0; BatchIndex < NumBatches; ++BatchIndex)
	{
		const FBatch& Batch = Batches[BatchIndex];
		if (Batch.Indices.Num() == 0 || !Batch.Brush.IsValid())
		{
			continue;
		}
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, Batch.Brush->GetRenderingResource(), Batch.Vertices, Batch.Indices, nullptr, 0, 0);
		INC_DWORD_STAT(STAT_NiagaraUIDrawElements);
	}
	INC_DWORD_STAT_BY(STAT_NiagaraUIParticles, NumParticlesDrawn);
}

void FNiagaraUIMesh::Reset()
{
	NumBatches = 0;
	NumParticlesDrawn = 0;
	bBuilt = false;
	for (FBatch& Batch : Batches)
	{
		Batch.Brush.Reset();
	}
}

FNiagaraUIMesh::FBatch& FNiagaraUIMesh::BatchFor(const TSharedPtr<FSlateBrush>& Brush)
{
	// Renderers drawn one after another with the same brush share a batch - one draw for all of them.
	if (NumBatches > 0 && Batches[NumBatches - 1].Brush == Brush)
	{
		return Batches[NumBatches - 1];
	}
	if (NumBatches == Batches.Num())
	{
		Batches.AddDefaulted();
	}
	FBatch& Batch = Batches[NumBatches++];
	Batch.Brush = Brush;
	Batch.Vertices.Reset();
	Batch.Indices.Reset();
	return Batch;
}

FNiagaraUIMesh::FBrushChoice FNiagaraUIMesh::ChooseBrush(UMaterialInterface* Material, const FNiagaraUIBuildSettings& Settings, const FDrawEntry& Entry)
{
	using namespace NiagaraUIMeshPrivate;

	FBrushChoice Choice;

	if (Material && Settings.MaterialRemap)
	{
		if (const TObjectPtr<UMaterialInterface>* Remapped = Settings.MaterialRemap->Find(Material))
		{
			if (*Remapped)
			{
				Material = Remapped->Get();
			}
		}
	}

	if (NiagaraUIRuntime::IsUIMaterial(Material))
	{
		Choice.Brush = NiagaraUIRuntime::GetMaterialBrush(Material);
		return Choice;
	}

	if (Material)
	{
		bool bAlreadyWarned = false;
		Warned.Add(Material, &bAlreadyWarned);
		if (!bAlreadyWarned)
		{
			NiagaraUIRuntime::WarnOnce(FString::Printf(TEXT("Niagara Particles '%s': material %s (emitter '%s') isn't a User Interface material, so its particles are drawn with the Particle Image. Give the renderer a UI material, or map one to it in Material Remap."),
				Settings.Owner ? *Settings.Owner->GetName() : TEXT("?"), *Material->GetName(), *GetEmitterName(*Entry.Emitter)));
		}
	}

	if (ImageBrush.IsValid())
	{
		Choice.Brush = ImageBrush;
		Choice.Tint = ImageBrush->TintColor.GetSpecifiedColor();

		// An image from an atlas covers only part of its texture: its UV Region says which.
		const FBox2f UVRegion = ImageBrush->GetUVRegion();
		if (UVRegion.bIsValid && UVRegion.GetArea() > 0.f)
		{
			Choice.UVMin = UVRegion.Min;
			Choice.UVSize = UVRegion.GetSize();
		}
		return Choice;
	}

	Choice.Brush = NiagaraUIRuntime::GetDefaultParticleBrush();
	return Choice;
}

void FNiagaraUIMesh::AddSprites(const FDrawEntry& Entry, const FNiagaraUIBuildSettings& Settings, const FNiagaraUIPlaneAxes& Axes, const FBrushChoice& Choice, FBatch& Batch)
{
	using namespace NiagaraUIMeshPrivate;

	const UNiagaraSpriteRendererProperties& Props = *CastChecked<UNiagaraSpriteRendererProperties>(Entry.Renderer);
	const FNiagaraDataSet& DataSet = Entry.Emitter->GetParticleData();
	const FNiagaraUISpace& Space = *Entry.Space;
	const int32 Count = Entry.NumParticles;

	const auto Positions = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(DataSet, Props.PositionBinding.GetDataSetBindableVariable().GetName());
	if (!Positions.IsValid())
	{
		return;
	}
	const auto Colors = FNiagaraDataSetAccessor<FLinearColor>::CreateReader(DataSet, Props.ColorBinding.GetDataSetBindableVariable().GetName());
	const auto Sizes = FNiagaraDataSetAccessor<FVector2f>::CreateReader(DataSet, Props.SpriteSizeBinding.GetDataSetBindableVariable().GetName());
	const auto Rotations = FNiagaraDataSetAccessor<float>::CreateReader(DataSet, Props.SpriteRotationBinding.GetDataSetBindableVariable().GetName());
	const auto Velocities = FNiagaraDataSetAccessor<FVector3f>::CreateReader(DataSet, Props.VelocityBinding.GetDataSetBindableVariable().GetName());
	const auto Alignments = FNiagaraDataSetAccessor<FVector3f>::CreateReader(DataSet, Props.SpriteAlignmentBinding.GetDataSetBindableVariable().GetName());
	const auto SubImages = FNiagaraDataSetAccessor<float>::CreateReader(DataSet, Props.SubImageIndexBinding.GetDataSetBindableVariable().GetName());
	const auto Dynamics = FNiagaraDataSetAccessor<FVector4f>::CreateReader(DataSet, Props.DynamicMaterialBinding.GetDataSetBindableVariable().GetName());
	const auto CustomSorts = FNiagaraDataSetAccessor<float>::CreateReader(DataSet, Props.CustomSortingBinding.GetDataSetBindableVariable().GetName());

	const bool bVelocityAligned = Props.Alignment == ENiagaraSpriteAlignment::VelocityAligned;
	const bool bCustomAligned = !bVelocityAligned && Props.Alignment != ENiagaraSpriteAlignment::Unaligned && Alignments.IsValid();

	const FVector2f Pivot(Props.PivotInUVSpace);
	const int32 Columns = FMath::Max(FMath::RoundToInt(Props.SubImageSize.X), 1);
	const int32 Rows = FMath::Max(FMath::RoundToInt(Props.SubImageSize.Y), 1);
	const int32 NumFrames = Columns * Rows;
	const FVector2f FrameSize(Choice.UVSize.X / Columns, Choice.UVSize.Y / Rows);

	const FLinearColor Tint = Settings.Tint * Choice.Tint;
	const float Perspective = Settings.PerspectiveDistance;

	// Draw order: as the renderer sorts - back to front by depth, or by its custom key - else as stored.
	const bool bSortByDepth = Props.SortMode == ENiagaraSortMode::ViewDepth || Props.SortMode == ENiagaraSortMode::ViewDistance;
	const bool bSortByCustom = (Props.SortMode == ENiagaraSortMode::CustomAscending || Props.SortMode == ENiagaraSortMode::CustomDecending) && CustomSorts.IsValid();
	const bool bSorted = bSortByDepth || bSortByCustom;
	if (bSorted)
	{
		const float CustomSign = Props.SortMode == ENiagaraSortMode::CustomDecending ? -1.f : 1.f;
		SortScratch.SetNumUninitialized(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FSortKey& SortKey = SortScratch[Index];
			SortKey.Index = Index;
			SortKey.Key = bSortByDepth
				? Axes.DepthOf(Positions.GetSafe(Index, FVector3f::ZeroVector))
				: CustomSorts.GetSafe(Index, 0.f) * CustomSign;
		}
		SortScratch.Sort([](const FSortKey& A, const FSortKey& B) { return A.Key < B.Key; });
	}

	Batch.Vertices.Reserve(Batch.Vertices.Num() + Count * 4);
	Batch.Indices.Reserve(Batch.Indices.Num() + Count * 6);

	const FSlateRenderTransform& Identity = GetIdentity();
	for (int32 Step = 0; Step < Count; ++Step)
	{
		const int32 Index = bSorted ? SortScratch[Step].Index : Step;

		const FLinearColor Color = Colors.GetSafe(Index, FLinearColor::White) * Tint;
		if (Color.A <= 0.f)
		{
			continue;
		}

		const FVector3f Position = Positions.GetSafe(Index, FVector3f::ZeroVector);
		FVector2f Point = Axes.Project(Position);
		FVector2f Size = Sizes.GetSafe(Index, FVector2f(DefaultSpriteSize, DefaultSpriteSize));
		if (Perspective > 0.f)
		{
			const float Scale = PerspectiveScale(Axes.DepthOf(Position), Perspective);
			Point = Space.Center + (Point - Space.Center) * Scale;
			Size *= Scale;
		}

		// The sprite's own axes in the plane: across its width, and down its height (its texture's top is "up").
		FVector2f Right(1.f, 0.f);
		FVector2f Down(0.f, 1.f);
		bool bAligned = false;
		if (bVelocityAligned || bCustomAligned)
		{
			const FVector3f Facing3 = bVelocityAligned ? Velocities.GetSafe(Index, FVector3f::ZeroVector) : Alignments.GetSafe(Index, FVector3f::ZeroVector);
			const FVector2f Facing = Axes.Project(Facing3);
			const float LengthSquared = Facing.SizeSquared();
			if (LengthSquared > UE_SMALL_NUMBER)
			{
				// Its top points the way it faces.
				Down = Facing * -FMath::InvSqrt(LengthSquared);
				Right = FVector2f(Down.Y, -Down.X);
				bAligned = true;
			}
		}
		if (!bAligned)
		{
			const float Degrees = Rotations.GetSafe(Index, 0.f);
			if (Degrees != 0.f)
			{
				float Sin = 0.f;
				float Cos = 1.f;
				FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(Degrees));
				Right = FVector2f(Cos, Sin);
				Down = FVector2f(-Sin, Cos);
			}
		}

		const FVector2f Across = Space.ToAbsoluteVector(Right * Size.X);
		const FVector2f Along = Space.ToAbsoluteVector(Down * Size.Y);
		const FVector2f TopLeft = Space.ToAbsolute(Point) - Across * Pivot.X - Along * Pivot.Y;
		const FVector2f TopRight = TopLeft + Across;
		const FVector2f BottomLeft = TopLeft + Along;
		const FVector2f BottomRight = TopRight + Along;

		const FVector2f Min = TopLeft.ComponentMin(TopRight).ComponentMin(BottomLeft.ComponentMin(BottomRight));
		const FVector2f Max = TopLeft.ComponentMax(TopRight).ComponentMax(BottomLeft.ComponentMax(BottomRight));
		if (IsOutside(Min, Max, Settings.CullRect))
		{
			continue;
		}

		FVector2f UVMin = Choice.UVMin;
		FVector2f UVSpan = Choice.UVSize;
		if (NumFrames > 1)
		{
			int32 Frame = FMath::FloorToInt(SubImages.GetSafe(Index, 0.f)) % NumFrames;
			if (Frame < 0)
			{
				Frame += NumFrames;
			}
			UVMin += FrameSize * FVector2f(static_cast<float>(Frame % Columns), static_cast<float>(Frame / Columns));
			UVSpan = FrameSize;
		}
		const FVector2f UVMax = UVMin + UVSpan;

		// The Dynamic Parameter's x and y reach a UI material as TexCoord1.
		const FVector4f Dynamic = Dynamics.GetSafe(Index, FVector4f(0.f, 0.f, 0.f, 0.f));
		const FVector2f DynamicXY(Dynamic.X, Dynamic.Y);
		const FColor Packed = Color.ToFColor(true);

		const SlateIndex First = static_cast<SlateIndex>(Batch.Vertices.Num());
		Batch.Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Identity, TopLeft, UVMin, DynamicXY, Packed));
		Batch.Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Identity, TopRight, FVector2f(UVMax.X, UVMin.Y), DynamicXY, Packed));
		Batch.Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Identity, BottomLeft, FVector2f(UVMin.X, UVMax.Y), DynamicXY, Packed));
		Batch.Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Identity, BottomRight, UVMax, DynamicXY, Packed));

		Batch.Indices.Add(First);
		Batch.Indices.Add(First + 1);
		Batch.Indices.Add(First + 2);
		Batch.Indices.Add(First + 2);
		Batch.Indices.Add(First + 1);
		Batch.Indices.Add(First + 3);

		++NumParticlesDrawn;
	}
}

void FNiagaraUIMesh::AddRibbons(const FDrawEntry& Entry, const FNiagaraUIBuildSettings& Settings, const FNiagaraUIPlaneAxes& Axes, const FBrushChoice& Choice, FBatch& Batch)
{
	using namespace NiagaraUIMeshPrivate;

	const UNiagaraRibbonRendererProperties& Props = *CastChecked<UNiagaraRibbonRendererProperties>(Entry.Renderer);
	const FNiagaraDataSet& DataSet = Entry.Emitter->GetParticleData();
	const FNiagaraUISpace& Space = *Entry.Space;
	const int32 Count = Entry.NumParticles;
	if (Count < 2)
	{
		return;
	}

	const auto Positions = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(DataSet, Props.PositionBinding.GetDataSetBindableVariable().GetName());
	if (!Positions.IsValid())
	{
		return;
	}
	const auto Colors = FNiagaraDataSetAccessor<FLinearColor>::CreateReader(DataSet, Props.ColorBinding.GetDataSetBindableVariable().GetName());
	const auto Widths = FNiagaraDataSetAccessor<float>::CreateReader(DataSet, Props.RibbonWidthBinding.GetDataSetBindableVariable().GetName());
	const auto LinkOrders = FNiagaraDataSetAccessor<float>::CreateReader(DataSet, Props.RibbonLinkOrderBinding.GetDataSetBindableVariable().GetName());
	const FName RibbonIdName = Props.RibbonIdBinding.GetDataSetBindableVariable().GetName();
	const auto FullIds = FNiagaraDataSetAccessor<FNiagaraID>::CreateReader(DataSet, RibbonIdName);
	const auto IntIds = FNiagaraDataSetAccessor<int32>::CreateReader(DataSet, RibbonIdName);
	const auto UniqueIds = FNiagaraDataSetAccessor<int32>::CreateReader(DataSet, FName(TEXT("UniqueID")));
	const auto Ages = FNiagaraDataSetAccessor<float>::CreateReader(DataSet, FName(TEXT("NormalizedAge")));

	// Each particle by its ribbon, then along it: its Link Order; else newest first by ID, or by age; else as stored.
	RibbonScratch.SetNumUninitialized(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FRibbonParticle& Particle = RibbonScratch[Index];
		Particle.Index = Index;
		if (FullIds.IsValid())
		{
			const FNiagaraID Id = FullIds.GetSafe(Index, FNiagaraID());
			Particle.Ribbon = (static_cast<uint64>(static_cast<uint32>(Id.AcquireTag)) << 32) | static_cast<uint32>(Id.Index);
		}
		else
		{
			Particle.Ribbon = IntIds.IsValid() ? static_cast<uint32>(IntIds.GetSafe(Index, 0)) : 0;
		}

		if (LinkOrders.IsValid())
		{
			Particle.Order = LinkOrders.GetSafe(Index, 0.f);
		}
		else if (UniqueIds.IsValid())
		{
			Particle.Order = -static_cast<double>(UniqueIds.GetSafe(Index, 0));
		}
		else if (Ages.IsValid())
		{
			Particle.Order = Ages.GetSafe(Index, 0.f);
		}
		else
		{
			Particle.Order = Index;
		}
	}
	RibbonScratch.Sort([](const FRibbonParticle& A, const FRibbonParticle& B)
	{
		if (A.Ribbon != B.Ribbon)
		{
			return A.Ribbon < B.Ribbon;
		}
		return A.Order != B.Order ? A.Order < B.Order : A.Index < B.Index;
	});

	const FLinearColor Tint = Settings.Tint * Choice.Tint;
	const float Perspective = Settings.PerspectiveDistance;

	int32 RunStart = 0;
	while (RunStart < Count)
	{
		int32 RunEnd = RunStart + 1;
		while (RunEnd < Count && RibbonScratch[RunEnd].Ribbon == RibbonScratch[RunStart].Ribbon)
		{
			++RunEnd;
		}

		RibbonPoints.Reset();
		for (int32 Step = RunStart; Step < RunEnd; ++Step)
		{
			const int32 Index = RibbonScratch[Step].Index;
			const FVector3f Position = Positions.GetSafe(Index, FVector3f::ZeroVector);
			FVector2f Point = Axes.Project(Position);
			float Scale = 1.f;
			if (Perspective > 0.f)
			{
				Scale = PerspectiveScale(Axes.DepthOf(Position), Perspective);
				Point = Space.Center + (Point - Space.Center) * Scale;
			}

			FRibbonPoint RibbonPoint;
			RibbonPoint.Position = Space.ToAbsolute(Point);
			RibbonPoint.Width = Widths.GetSafe(Index, 1.f) * Space.LengthScale * Scale;
			RibbonPoint.Color = Colors.GetSafe(Index, FLinearColor::White) * Tint;
			RibbonPoint.Param = static_cast<float>(Step - RunStart);

			// Particles on top of one another make no segment, and give it no way to face.
			if (RibbonPoints.Num() > 0 && FVector2f::DistSquared(RibbonPoints.Last().Position, RibbonPoint.Position) < 1.e-4f)
			{
				continue;
			}
			RibbonPoints.Add(RibbonPoint);
		}

		AddRibbonStrip(RunEnd - RunStart, Entry, Settings, Choice, Batch);
		RunStart = RunEnd;
	}
}

void FNiagaraUIMesh::AddRibbonStrip(int32 NumParticles, const FDrawEntry& Entry, const FNiagaraUIBuildSettings& Settings, const FBrushChoice& Choice, FBatch& Batch)
{
	using namespace NiagaraUIMeshPrivate;

	if (RibbonPoints.Num() < 2)
	{
		return;
	}

	const UNiagaraRibbonRendererProperties& Props = *CastChecked<UNiagaraRibbonRendererProperties>(Entry.Renderer);
	const FNiagaraUISpace& Space = *Entry.Space;

	// Smoothed: a Catmull-Rom curve through the particles, with more points where they lie far apart on screen.
	bool bSmoothed = false;
	if (Settings.bSmoothRibbons && RibbonPoints.Num() >= 3)
	{
		SmoothedPoints.Reset();
		const int32 Last = RibbonPoints.Num() - 1;
		for (int32 Segment = 0; Segment < Last; ++Segment)
		{
			const FRibbonPoint& From = RibbonPoints[Segment];
			const FRibbonPoint& To = RibbonPoints[Segment + 1];
			const FVector2f& Before = RibbonPoints[FMath::Max(Segment - 1, 0)].Position;
			const FVector2f& After = RibbonPoints[FMath::Min(Segment + 2, Last)].Position;

			SmoothedPoints.Add(From);
			const int32 Steps = FMath::Clamp(FMath::CeilToInt(FVector2f::Distance(From.Position, To.Position) / SmoothStepPixels), 1, MaxSmoothSteps);
			for (int32 Step = 1; Step < Steps; ++Step)
			{
				const float Alpha = static_cast<float>(Step) / Steps;
				FRibbonPoint& Between = SmoothedPoints.AddDefaulted_GetRef();
				Between.Position = CatmullRom(Before, From.Position, To.Position, After, Alpha);
				Between.Width = FMath::Lerp(From.Width, To.Width, Alpha);
				Between.Color = FMath::Lerp(From.Color, To.Color, Alpha);
				Between.Param = FMath::Lerp(From.Param, To.Param, Alpha);
			}
		}
		SmoothedPoints.Add(RibbonPoints.Last());
		bSmoothed = true;
	}
	TArray<FRibbonPoint>& Strip = bSmoothed ? SmoothedPoints : RibbonPoints;
	const int32 NumPoints = Strip.Num();

	// Its length so far at each point, and its bounds - to leave out a ribbon wholly off screen.
	FVector2f Min = Strip[0].Position;
	FVector2f Max = Strip[0].Position;
	float MaxWidth = Strip[0].Width;
	float TotalLength = 0.f;
	Strip[0].Distance = 0.f;
	for (int32 PointIndex = 1; PointIndex < NumPoints; ++PointIndex)
	{
		FRibbonPoint& Point = Strip[PointIndex];
		TotalLength += FVector2f::Distance(Strip[PointIndex - 1].Position, Point.Position);
		Point.Distance = TotalLength;
		Min = Min.ComponentMin(Point.Position);
		Max = Max.ComponentMax(Point.Position);
		MaxWidth = FMath::Max(MaxWidth, Point.Width);
	}
	const FVector2f Reach(MaxWidth * 0.5f * MaxMiter, MaxWidth * 0.5f * MaxMiter);
	if (IsOutside(Min - Reach, Max + Reach, Settings.CullRect))
	{
		return;
	}

	// U along the ribbon, as the renderer's UV settings lay it: evenly per particle, over its length, or tiled.
	const auto GetU = [&Space, TotalLength, NumParticles](const FNiagaraRibbonUVSettings& UVSettings, const FRibbonPoint& Point) -> float
	{
		if (UVSettings.DistributionMode == ENiagaraRibbonUVDistributionMode::ScaledUniformly)
		{
			return NumParticles > 1 ? Point.Param / static_cast<float>(NumParticles - 1) : 0.f;
		}
		const bool bTiled = UVSettings.DistributionMode == ENiagaraRibbonUVDistributionMode::TiledOverRibbonLength
			|| UVSettings.DistributionMode == ENiagaraRibbonUVDistributionMode::TiledFromStartOverRibbonLength;
		if (bTiled && UVSettings.TilingLength > 0.f)
		{
			return Point.Distance / (UVSettings.TilingLength * FMath::Max(Space.LengthScale, UE_SMALL_NUMBER));
		}
		return TotalLength > 0.f ? Point.Distance / TotalLength : 0.f;
	};

	const FSlateRenderTransform& Identity = GetIdentity();
	const SlateIndex First = static_cast<SlateIndex>(Batch.Vertices.Num());
	Batch.Vertices.Reserve(Batch.Vertices.Num() + NumPoints * 2);
	Batch.Indices.Reserve(Batch.Indices.Num() + (NumPoints - 1) * 6);

	for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
	{
		const FRibbonPoint& Point = Strip[PointIndex];

		// Which way the ribbon runs here: between the segments either side of the point.
		FVector2f Incoming = PointIndex > 0 ? (Point.Position - Strip[PointIndex - 1].Position).GetSafeNormal() : FVector2f::ZeroVector;
		FVector2f Outgoing = PointIndex < NumPoints - 1 ? (Strip[PointIndex + 1].Position - Point.Position).GetSafeNormal() : FVector2f::ZeroVector;
		if (PointIndex == 0)
		{
			Incoming = Outgoing;
		}
		if (PointIndex == NumPoints - 1)
		{
			Outgoing = Incoming;
		}
		FVector2f Tangent = (Incoming + Outgoing).GetSafeNormal();
		if (Tangent.IsNearlyZero())
		{
			Tangent = Incoming;
		}
		const FVector2f Normal(-Tangent.Y, Tangent.X);

		// Widened at a bend so it keeps its width along both segments - up to MaxMiter, past which the corner is cut.
		const float Cosine = FVector2f::DotProduct(Normal, FVector2f(-Incoming.Y, Incoming.X));
		const float Miter = FMath::Min(1.f / FMath::Max(Cosine, 1.f / MaxMiter), MaxMiter);
		const FVector2f Offset = Normal * (Point.Width * 0.5f * Miter);

		const float U0 = GetU(Props.UV0Settings, Point);
		const float U1 = GetU(Props.UV1Settings, Point);
		const FColor Packed = Point.Color.ToFColor(true);

		// UV0 is TexCoord0 (V across the ribbon), UV1 TexCoord1.
		Batch.Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Identity, Point.Position + Offset,
			Choice.UVMin + Choice.UVSize * FVector2f(U0, 0.f), FVector2f(U1, 0.f), Packed));
		Batch.Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Identity, Point.Position - Offset,
			Choice.UVMin + Choice.UVSize * FVector2f(U0, 1.f), FVector2f(U1, 1.f), Packed));
	}

	for (int32 Segment = 0; Segment < NumPoints - 1; ++Segment)
	{
		const SlateIndex Corner = First + static_cast<SlateIndex>(Segment * 2);
		Batch.Indices.Add(Corner);
		Batch.Indices.Add(Corner + 1);
		Batch.Indices.Add(Corner + 2);
		Batch.Indices.Add(Corner + 1);
		Batch.Indices.Add(Corner + 3);
		Batch.Indices.Add(Corner + 2);
	}

	NumParticlesDrawn += NumParticles;
}

// =====================================================================================================
// Helpers
// =====================================================================================================

int32 NiagaraUIMesh::CountParticles(UNiagaraComponent* Component)
{
	FNiagaraSystemInstance* SystemInstance = NiagaraUIMeshPrivate::GetSystemInstance(Component);
	if (!SystemInstance)
	{
		return 0;
	}

	int32 Total = 0;
	for (const auto& EmitterRef : SystemInstance->GetEmitters())
	{
		const FNiagaraEmitterInstance& Emitter = EmitterRef.Get();
		if (Emitter.IsDisabled())
		{
			continue;
		}
		const FVersionedNiagaraEmitterData* EmitterData = Emitter.GetVersionedEmitter().GetEmitterData();
		if (!EmitterData || EmitterData->SimTarget != ENiagaraSimTarget::CPUSim)
		{
			continue;
		}
		if (const FNiagaraDataBuffer* Buffer = Emitter.GetParticleData().GetCurrentData())
		{
			Total += static_cast<int32>(Buffer->GetNumInstances());
		}
	}
	return Total;
}
