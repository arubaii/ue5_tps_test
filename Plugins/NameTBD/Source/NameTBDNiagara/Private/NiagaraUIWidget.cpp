// Copyright AR GameSystems 2026. All rights reserved.

#include "NiagaraUIWidget.h"

#include "NiagaraUILog.h"
#include "NiagaraUIMesh.h"
#include "NiagaraUIParameters.h"
#include "NiagaraUIRuntime.h"
#include "SNiagaraUIWidget.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Containers/Ticker.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UObject/ObjectSaveContext.h"

#if WITH_EDITOR
#include "Editor/WidgetCompilerLog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#endif

#define LOCTEXT_NAMESPACE "NiagaraUIWidget"

namespace NiagaraUIWidgetPrivate
{
	/** The step Prewarm Time is simulated in. */
	constexpr float PrewarmStep = 1.f / 30.f;

	/** Frames a widget may go undrawn before Pause When Hidden pauses it - one frame of slack for a skipped paint. */
	constexpr uint64 HiddenAfterFrames = 2;
}

template <typename FunctionType>
void UNiagaraUIWidget::ForEachComponent(FunctionType&& Function) const
{
	if (IsValid(Main.Component))
	{
		Function(Main.Component.Get());
	}
	for (const FNiagaraUISimulation& Burst : Bursts)
	{
		if (IsValid(Burst.Component))
		{
			Function(Burst.Component.Get());
		}
	}
}

UNiagaraUIWidget::UNiagaraUIWidget()
{
	// Particles are decoration: a click goes through them to whatever is underneath.
	SetVisibilityInternal(ESlateVisibility::HitTestInvisible);
}

// ---------------------------------------------------------------------------------------------
// UWidget
// ---------------------------------------------------------------------------------------------

TSharedRef<SWidget> UNiagaraUIWidget::RebuildWidget()
{
	MyParticles = SNew(SNiagaraUIWidget).Owner(this);
	bSlateVolatile = false;
	return MyParticles.ToSharedRef();
}

void UNiagaraUIWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

#if WITH_EDITOR
	// The designer remakes its preview from the Widget Blueprint's widget whenever anything changes; laid out again here, the
	// preview - which is what the Details panel shows - lists the system's User parameters as soon as the system is set, and
	// any added in the Niagara editor since.
	if (IsDesignTime() && LayOutUserParameters())
	{
		RequestDetailsRefresh();
	}
#endif

	// Another system set on a simulation already made: it starts again with that one, if it was playing.
	if (IsValid(Main.Component) && Main.Component->GetAsset() != NiagaraSystem)
	{
		const bool bWasPlaying = Main.Component->IsActive() || Main.bPendingPlay;
		DropOverrides(Main);
		Main.Component->OnSystemFinished.RemoveAll(this);
		Main.Component->DeactivateImmediate();
		Main.Component->SetAsset(NiagaraSystem);
		Main.Component->OnSystemFinished.AddUniqueDynamic(this, &UNiagaraUIWidget::HandleSystemFinished);
		Main.bPendingPlay = NiagaraSystem && (bWasPlaying || bAutoPlay);
		Main.bPendingRestart = true;
	}

	ForEachComponent([this](UNiagaraComponent* SimComponent)
	{
		ApplyRuntimeSettings(SimComponent);
		ApplyParameters(SimComponent);
	});

	if (MyParticles.IsValid())
	{
		// UWidget::SynchronizeProperties sets volatility from Is Volatile alone; live particles keep it repainting too.
		MyParticles->ForceVolatile(bSlateVolatile || bIsVolatile);
		MyParticles->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void UNiagaraUIWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyParticles.Reset();
	bSlateVolatile = false;

	// Not while being garbage collected: the simulations are this widget's alone and go with it, and touching other
	// objects from here then isn't safe.
	if (!IsUnreachable())
	{
		ReleaseSimulations();
	}
}

void UNiagaraUIWidget::PostLoad()
{
	Super::PostLoad();

	const bool bHasLegacy = FloatParameters.Num() > 0 || ColorParameters.Num() > 0 || VectorParameters.Num() > 0;
#if WITH_EDITOR
	// In the editor the system's User parameters are read again as the widget loads: any added, removed or changed in the
	// Niagara editor since it was saved show at once. A cooked game plays what was saved.
	LayOutUserParameters();
#else
	if (bHasLegacy)
	{
		LayOutUserParameters();
	}
#endif
	if (bHasLegacy)
	{
		MigrateLegacyParameters();
	}
}

#if WITH_EDITOR
void UNiagaraUIWidget::PreSave(FObjectPreSaveContext SaveContext)
{
	// Saved laid out for the system as it is now: the designer edits its preview, whose changed values reach this widget one by
	// one, without the layout they came with.
	LayOutUserParameters();
	Super::PreSave(SaveContext);
}

void UNiagaraUIWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UNiagaraUIWidget, NiagaraSystem) && LayOutUserParameters())
	{
		RequestDetailsRefresh();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UNiagaraUIWidget::RequestDetailsRefresh() const
{
	// Next tick, once the edit that changed the rows is over: every Details panel lays itself out again.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
	{
		if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
		{
			PropertyEditor->NotifyCustomizationModuleChanged();
		}
		return false;
	}));
}

const FText UNiagaraUIWidget::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}

void UNiagaraUIWidget::ValidateCompiledDefaults(IWidgetCompilerLog& CompileLog) const
{
	Super::ValidateCompiledDefaults(CompileLog);

	if (!NiagaraSystem)
	{
		return;
	}

	// What of the system won't show in UI, said where the widget is compiled rather than discovered by a blank screen.
	const FText WidgetName = FText::FromString(GetName());
	for (const FNiagaraEmitterHandle& Handle : NiagaraSystem->GetEmitterHandles())
	{
		if (!Handle.GetIsEnabled())
		{
			continue;
		}

		const FText EmitterName = FText::FromName(Handle.GetName());
		const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (!EmitterData)
		{
			CompileLog.Note(FText::Format(LOCTEXT("IrregularEmitter", "{0}: emitter {1} isn't a regular emitter (a lightweight emitter?) and won't be drawn in UI."), WidgetName, EmitterName));
			continue;
		}

		if (EmitterData->SimTarget != ENiagaraSimTarget::CPUSim)
		{
			CompileLog.Warning(FText::Format(LOCTEXT("GPUEmitter", "{0}: emitter {1} simulates on the GPU and won't be drawn - UI draws CPU particles only. Set its Sim Target to CPUSim."), WidgetName, EmitterName));
			continue;
		}

		for (const UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
		{
			if (!Renderer || !Renderer->GetIsEnabled())
			{
				continue;
			}

			UMaterialInterface* Material = nullptr;
			if (const UNiagaraSpriteRendererProperties* SpriteRenderer = Cast<UNiagaraSpriteRendererProperties>(Renderer))
			{
				Material = SpriteRenderer->Material;
			}
			else if (const UNiagaraRibbonRendererProperties* RibbonRenderer = Cast<UNiagaraRibbonRendererProperties>(Renderer))
			{
				Material = RibbonRenderer->Material;
			}
			else
			{
				CompileLog.Note(FText::Format(LOCTEXT("UnsupportedRenderer", "{0}: emitter {1}'s {2} won't be drawn - UI draws Sprite and Ribbon renderers."),
					WidgetName, EmitterName, Renderer->GetClass()->GetDisplayNameText()));
				continue;
			}

			if (const TObjectPtr<UMaterialInterface>* Remapped = Material ? MaterialRemap.Find(Material) : nullptr)
			{
				if (*Remapped)
				{
					Material = Remapped->Get();
				}
			}
			if (Material && !NiagaraUIRuntime::IsUIMaterial(Material))
			{
				CompileLog.Note(FText::Format(LOCTEXT("SurfaceMaterial", "{0}: emitter {1} draws with {2}, which isn't a User Interface material - its particles show the Particle Image instead. Map it to a UI material in Material Remap to draw it as authored."),
					WidgetName, EmitterName, FText::FromString(Material->GetName())));
			}
		}
	}
}
#endif

// ---------------------------------------------------------------------------------------------
// Blueprint
// ---------------------------------------------------------------------------------------------

void UNiagaraUIWidget::Play(bool bRestart)
{
	bAutoPlayDone = true;
	if (IsValid(Main.Component) && Main.bPlaced)
	{
		StartSimulation(Main, bRestart);
	}
	else
	{
		// Not drawn yet: it starts on its first paint, once it is where the widget is.
		Main.bPendingPlay = true;
		Main.bPendingRestart = bRestart;
	}
	SetAnimating(!bUserPaused);
}

void UNiagaraUIWidget::StopEmitting()
{
	bAutoPlayDone = true;
	Main.bPendingPlay = false;
	for (FNiagaraUISimulation& Burst : Bursts)
	{
		Burst.bPendingPlay = false;
	}
	ForEachComponent([](UNiagaraComponent* SimComponent)
	{
		if (SimComponent->IsActive())
		{
			SimComponent->Deactivate();
		}
	});
}

void UNiagaraUIWidget::Clear()
{
	bAutoPlayDone = true;
	Main.bPendingPlay = false;
	for (FNiagaraUISimulation& Burst : Bursts)
	{
		Burst.bPendingPlay = false;
	}
	ForEachComponent([](UNiagaraComponent* SimComponent)
	{
		SimComponent->DeactivateImmediate();
	});
	SetAnimating(false);
	if (MyParticles.IsValid())
	{
		MyParticles->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UNiagaraUIWidget::PlayAt(FVector2D LocalPosition, UNiagaraSystem* BurstSystem)
{
	StartBurst(BurstSystem, FVector2f(LocalPosition), /*bAtOrigin=*/false);
}

void UNiagaraUIWidget::BurstAtOrigin(UNiagaraSystem* BurstSystem)
{
	StartBurst(BurstSystem, FVector2f::ZeroVector, /*bAtOrigin=*/true);
}

void UNiagaraUIWidget::StartBurst(UNiagaraSystem* System, const FVector2f& LocalPosition, bool bAtOrigin, const FNiagaraUIParameterOverrides* Overrides)
{
	UNiagaraSystem* PlayedSystem = System ? System : NiagaraSystem.Get();
	if (!PlayedSystem || MaxBursts <= 0 || !CanSimulate())
	{
		if (!PlayedSystem && NiagaraUIRuntime::IsLogging())
		{
			UE_LOG(LogNiagaraUI, Log, TEXT("%s: a burst with no system - neither the burst nor the widget names one."), *GetPathName());
		}
		return;
	}

	UWorld* World = FindSimulationWorld();
	FNiagaraUISimulation* Burst = World ? TakeBurst(PlayedSystem, World) : nullptr;
	if (!Burst)
	{
		return;
	}

	// Placed and started on the next paint, so a world-space emitter's first particles are born at the point - the widget's
	// Origin worked out then, wherever the widget is by that time.
	Burst->Position = LocalPosition;
	Burst->bAtOrigin = bAtOrigin;
	if (Overrides)
	{
		Burst->Overrides = *Overrides;
	}
	else
	{
		Burst->Overrides.Reset();
	}
	Burst->StartedAt = FPlatformTime::Seconds();
	Burst->bPlaced = false;
	Burst->bPendingPlay = true;
	Burst->bPendingRestart = true;
	SetAnimating(!bUserPaused);
}

void UNiagaraUIWidget::PlayAtAbsolutePosition(FVector2D AbsolutePosition, UNiagaraSystem* BurstSystem)
{
	const FGeometry& Geometry = GetCachedGeometry();
	PlayAt(FVector2D(Geometry.AbsoluteToLocal(AbsolutePosition)), BurstSystem);
}

void UNiagaraUIWidget::SetNiagaraSystem(UNiagaraSystem* NewSystem, bool bPlay)
{
	NiagaraSystem = NewSystem;
	LayOutUserParameters();
	if (IsValid(Main.Component))
	{
		// Quietly: On Finished is for the system ending, not for being swapped out. A track's parameter values go with the old one.
		DropOverrides(Main);
		Main.Component->OnSystemFinished.RemoveAll(this);
		Main.Component->DeactivateImmediate();
		Main.Component->SetAsset(NewSystem);
		Main.Component->OnSystemFinished.AddUniqueDynamic(this, &UNiagaraUIWidget::HandleSystemFinished);
		ApplyParameters(Main.Component);
	}
	Main.bPendingPlay = false;
	Main.Overrides.Reset();

	if (bPlay && NewSystem)
	{
		Play(true);
	}
	else
	{
		bAutoPlayDone = true;
	}
}

void UNiagaraUIWidget::PlayWithOverrides(UNiagaraSystem* System, const FNiagaraUIParameterOverrides* Overrides)
{
	if (System && System != NiagaraSystem)
	{
		SetNiagaraSystem(System, /*bPlay=*/false);
	}

	// Put on as it starts - now, or on its first paint - and the last track's taken off, if it had others.
	if (Overrides)
	{
		Main.Overrides = *Overrides;
	}
	else
	{
		Main.Overrides.Reset();
	}
	Play(/*bRestart=*/true);
}

void UNiagaraUIWidget::PlayNothing()
{
	bAutoPlayDone = true;
	Main.bPendingPlay = false;
	if (IsValid(Main.Component))
	{
		// Quietly, as when Set Niagara System swaps it: On Finished is for the system ending, not for being replaced.
		DropOverrides(Main);
		Main.Component->OnSystemFinished.RemoveAll(this);
		Main.Component->DeactivateImmediate();
		Main.Component->OnSystemFinished.AddUniqueDynamic(this, &UNiagaraUIWidget::HandleSystemFinished);
	}
	Main.Overrides.Reset();
	if (MyParticles.IsValid())
	{
		MyParticles->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UNiagaraUIWidget::SetPaused(bool bPaused)
{
	bUserPaused = bPaused;
	ApplyPause();

	bool bAnyActive = false;
	ForEachComponent([&bAnyActive](UNiagaraComponent* SimComponent)
	{
		bAnyActive |= SimComponent->IsActive();
	});
	SetAnimating(bAnyActive && !bPaused);
}

bool UNiagaraUIWidget::IsPlaying() const
{
	return Main.bPendingPlay || (IsValid(Main.Component) && Main.Component->IsActive());
}

int32 UNiagaraUIWidget::GetLiveParticleCount() const
{
	int32 Total = 0;
	ForEachComponent([&Total](UNiagaraComponent* SimComponent)
	{
		Total += NiagaraUIMesh::CountParticles(SimComponent);
	});
	return Total;
}

UNiagaraComponent* UNiagaraUIWidget::GetNiagaraComponent() const
{
	return IsValid(Main.Component) ? Main.Component.Get() : nullptr;
}

void UNiagaraUIWidget::SetFloatParameter(FName ParameterName, float Value)
{
	const FName Name = NiagaraUIParameters::GetShortName(ParameterName);
	UserParameters.SetValueFloat(Name, Value);
	ForEachComponent([Name, Value](UNiagaraComponent* SimComponent) { SimComponent->SetVariableFloat(Name, Value); });
}

void UNiagaraUIWidget::SetIntParameter(FName ParameterName, int32 Value)
{
	const FName Name = NiagaraUIParameters::GetShortName(ParameterName);
	if (const FPropertyBagPropertyDesc* Desc = UserParameters.FindPropertyDescByName(Name); Desc && Desc->ValueType == EPropertyBagPropertyType::Enum)
	{
		UserParameters.SetValueEnum(Name, static_cast<uint8>(Value), Cast<UEnum>(Desc->ValueTypeObject));
	}
	else
	{
		UserParameters.SetValueInt32(Name, Value);
	}
	ForEachComponent([Name, Value](UNiagaraComponent* SimComponent) { SimComponent->SetVariableInt(Name, Value); });
}

void UNiagaraUIWidget::SetBoolParameter(FName ParameterName, bool Value)
{
	const FName Name = NiagaraUIParameters::GetShortName(ParameterName);
	UserParameters.SetValueBool(Name, Value);
	ForEachComponent([Name, Value](UNiagaraComponent* SimComponent) { SimComponent->SetVariableBool(Name, Value); });
}

void UNiagaraUIWidget::SetColorParameter(FName ParameterName, FLinearColor Value)
{
	const FName Name = NiagaraUIParameters::GetShortName(ParameterName);
	UserParameters.SetValueStruct(Name, FConstStructView::Make(Value));
	ForEachComponent([Name, Value](UNiagaraComponent* SimComponent) { SimComponent->SetVariableLinearColor(Name, Value); });
}

void UNiagaraUIWidget::SetVector2DParameter(FName ParameterName, FVector2D Value)
{
	const FName Name = NiagaraUIParameters::GetShortName(ParameterName);
	UserParameters.SetValueStruct(Name, FConstStructView::Make(Value));
	ForEachComponent([Name, Value](UNiagaraComponent* SimComponent) { SimComponent->SetVariableVec2(Name, Value); });
}

void UNiagaraUIWidget::SetVectorParameter(FName ParameterName, FVector Value)
{
	const FName Name = NiagaraUIParameters::GetShortName(ParameterName);
	UserParameters.SetValueStruct(Name, FConstStructView::Make(Value));
	const bool bPosition = PositionParameters.Contains(Name);
	ForEachComponent([Name, Value, bPosition](UNiagaraComponent* SimComponent)
	{
		if (bPosition)
		{
			SimComponent->SetVariablePosition(Name, Value);
		}
		else
		{
			SimComponent->SetVariableVec3(Name, Value);
		}
	});
}

void UNiagaraUIWidget::SetVector4Parameter(FName ParameterName, FVector4 Value)
{
	const FName Name = NiagaraUIParameters::GetShortName(ParameterName);
	UserParameters.SetValueStruct(Name, FConstStructView::Make(Value));
	ForEachComponent([Name, Value](UNiagaraComponent* SimComponent) { SimComponent->SetVariableVec4(Name, Value); });
}

void UNiagaraUIWidget::SetObjectParameter(FName ParameterName, UObject* Value)
{
	const FName Name = NiagaraUIParameters::GetShortName(ParameterName);
	UserParameters.SetValueObject(Name, Value);
	ForEachComponent([Name, Value](UNiagaraComponent* SimComponent) { NiagaraUIParameters::ApplyObject(SimComponent, Name, Value); });
}

void UNiagaraUIWidget::ResetParameters()
{
	UserParameters = UserParameterDefaults;
	const UPropertyBag* BagStruct = UserParameterDefaults.GetPropertyBagStruct();
	if (!BagStruct)
	{
		return;
	}
	ForEachComponent([this, BagStruct](UNiagaraComponent* SimComponent)
	{
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			NiagaraUIParameters::Apply(SimComponent, UserParameterDefaults, Desc, PositionParameters);
		}
	});
}

void UNiagaraUIWidget::SetColorAndOpacity(FLinearColor InColorAndOpacity)
{
	ColorAndOpacity = InColorAndOpacity;
	if (MyParticles.IsValid())
	{
		MyParticles->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UNiagaraUIWidget::SetParticleScale(float InParticleScale)
{
	ParticleScale = FMath::Max(InParticleScale, 0.001f);
	if (MyParticles.IsValid())
	{
		MyParticles->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void UNiagaraUIWidget::SetDesiredSize(FVector2D InDesiredSize)
{
	DesiredSize = InDesiredSize;
	if (MyParticles.IsValid())
	{
		MyParticles->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

// ---------------------------------------------------------------------------------------------
// Painting and ticking
// ---------------------------------------------------------------------------------------------

void UNiagaraUIWidget::PrepareForPaint(const FGeometry& Geometry, TArray<FNiagaraUISource>& OutSources)
{
	if (!CanSimulate())
	{
		return;
	}
	LastPaintFrame = GFrameCounter;

	// The first paint makes the main simulation, and Auto Play starts it - once per simulation, not on every paint.
	if (!IsValid(Main.Component))
	{
		if (bAutoPlay && !bAutoPlayDone && NiagaraSystem)
		{
			Main.bPendingPlay = true;
			Main.bPendingRestart = true;
		}
		if (Main.bPendingPlay && EnsureMainSimulation())
		{
			bAutoPlayDone = true;
		}
	}

	// Drawn again: Pause When Hidden lets go.
	if (bHiddenPaused)
	{
		bHiddenPaused = false;
		ApplyPause();
		if (NiagaraUIRuntime::IsLogging())
		{
			UE_LOG(LogNiagaraUI, Log, TEXT("%s: shown again - resumed."), *GetPathName());
		}
	}

	// Where this paint puts the widget. Local-space particles go through its whole render transform (DPI scale, layout,
	// Render Transform, a Widget Animation's turn or squash); world-space ones through its scale alone, in units of
	// WorldUnit absolute pixels, so the simulation's world is the screen and a moving widget leaves them behind.
	const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
	const float UnitScale = FMath::Max(ParticleScale, 0.001f);
	const float WorldUnit = FMath::Max(Geometry.Scale, UE_KINDA_SMALL_NUMBER) * UnitScale;
	const FVector2f LocalAxisX = Transform.TransformVector(FVector2f(UnitScale, 0.f));
	const FVector2f LocalAxisY = Transform.TransformVector(FVector2f(0.f, UnitScale));
	const float LocalUnit = FMath::Sqrt(FMath::Abs(LocalAxisX.X * LocalAxisY.Y - LocalAxisX.Y * LocalAxisY.X));
	const float Angle = FMath::Atan2(LocalAxisX.Y, LocalAxisX.X);
	const float SimulationScale = FMath::Max(LocalUnit / WorldUnit, UE_KINDA_SMALL_NUMBER);
	const FVector2f LocalSize = FVector2f(FVector2D(Geometry.GetLocalSize()));
	const FNiagaraUIPlaneAxes Axes = FNiagaraUIPlaneAxes::Get(Plane);

	auto PrepareSimulation = [&](FNiagaraUISimulation& Simulation, const FVector2f& LocalPoint)
	{
		UNiagaraComponent* SimComponent = Simulation.Component;
		if (!IsValid(SimComponent))
		{
			return;
		}

		// The component goes where the widget puts the system's origin, turned and scaled as the widget is - what a
		// world-space emitter spawns from. Only moved when the widget has moved.
		const FVector2f Anchor = Transform.TransformPoint(LocalPoint);
		const FVector2f WorldPoint = Anchor / WorldUnit;
		const bool bMoved = !Simulation.bPlaced
			|| !WorldPoint.Equals(Simulation.PlacedAt, 0.01f)
			|| !FMath::IsNearlyEqual(Angle, Simulation.PlacedAngle, 1.e-4f)
			|| !FMath::IsNearlyEqual(SimulationScale, Simulation.PlacedScale, 1.e-4f);
		if (bMoved)
		{
			const FVector Location(Axes.Right * WorldPoint.X + Axes.Down * WorldPoint.Y);
			const FQuat Rotation(FVector(Axes.Depth), Angle);
			SimComponent->SetWorldTransform(FTransform(Rotation, Location, FVector(SimulationScale)));
			Simulation.bPlaced = true;
			Simulation.PlacedAt = WorldPoint;
			Simulation.PlacedAngle = Angle;
			Simulation.PlacedScale = SimulationScale;
		}

		if (Simulation.bPendingPlay)
		{
			if (NiagaraUIRuntime::IsLogging())
			{
				const FVector2D WidgetPosition(Geometry.GetAbsolutePosition());
				const FVector2D WidgetSize(Geometry.GetLocalSize());
				const UObject* PlayedSystem = SimComponent->GetAsset();
				UE_LOG(LogNiagaraUI, Log, TEXT("%s: plays %s at (%.0f, %.0f) in the widget - (%.0f, %.0f) as drawn; the widget is %.0f x %.0f at (%.0f, %.0f)."),
					*GetPathName(), PlayedSystem ? *PlayedSystem->GetName() : TEXT("no system"), LocalPoint.X, LocalPoint.Y, Anchor.X, Anchor.Y,
					WidgetSize.X, WidgetSize.Y, WidgetPosition.X, WidgetPosition.Y);
			}
			StartSimulation(Simulation, Simulation.bPendingRestart);
		}
		if (!SimComponent->IsActive())
		{
			return;
		}

		FNiagaraUISource& Source = OutSources.AddDefaulted_GetRef();
		Source.Component = SimComponent;

		Source.LocalSpace.Origin = Anchor;
		Source.LocalSpace.AxisX = LocalAxisX;
		Source.LocalSpace.AxisY = LocalAxisY;
		Source.LocalSpace.LengthScale = LocalUnit;
		Source.LocalSpace.Center = FVector2f::ZeroVector;

		Source.WorldSpace.Origin = FVector2f::ZeroVector;
		Source.WorldSpace.AxisX = FVector2f(WorldUnit, 0.f);
		Source.WorldSpace.AxisY = FVector2f(0.f, WorldUnit);
		Source.WorldSpace.LengthScale = WorldUnit;
		Source.WorldSpace.Center = WorldPoint;
	};

	PrepareSimulation(Main, LocalSize * FVector2f(Origin));
	for (FNiagaraUISimulation& Burst : Bursts)
	{
		PrepareSimulation(Burst, Burst.bAtOrigin ? LocalSize * FVector2f(Origin) : Burst.Position);
	}
}

void UNiagaraUIWidget::TickRuntime(uint64 Frame)
{
	using namespace NiagaraUIWidgetPrivate;

	bool bAnyActive = Main.bPendingPlay;
	for (const FNiagaraUISimulation& Burst : Bursts)
	{
		bAnyActive |= Burst.bPendingPlay;
	}
	ForEachComponent([&bAnyActive](UNiagaraComponent* SimComponent)
	{
		bAnyActive |= SimComponent->IsActive();
	});

	// Not drawn for a couple of frames: collapsed, hidden, scrolled out of view, off the screen.
	const bool bHidden = bPauseWhenHidden && bAnyActive && LastPaintFrame + HiddenAfterFrames < Frame;
	if (bHidden != bHiddenPaused)
	{
		bHiddenPaused = bHidden;
		ApplyPause();
		if (NiagaraUIRuntime::IsLogging())
		{
			UE_LOG(LogNiagaraUI, Log, TEXT("%s: %s."), *GetPathName(), bHidden ? TEXT("not drawn - paused") : TEXT("resumed"));
		}
	}

	SetAnimating(bAnyActive && !bUserPaused);
}

void UNiagaraUIWidget::ReleaseSimulationsIn(const UWorld* World)
{
	bool bInWorld = false;
	ForEachComponent([World, &bInWorld](UNiagaraComponent* SimComponent)
	{
		bInWorld |= SimComponent->GetWorld() == World;
	});
	if (bInWorld)
	{
		ReleaseSimulations();
	}
}

// ---------------------------------------------------------------------------------------------
// Simulations
// ---------------------------------------------------------------------------------------------

UWorld* UNiagaraUIWidget::FindSimulationWorld() const
{
	UWorld* World = GetWorld();

#if WITH_EDITOR
	// A designer preview may have no world of its own: it plays in the editor's.
	if (!World && IsDesignTime() && GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Editor)
			{
				World = Context.World();
				break;
			}
		}
	}
#endif

	if (!World || World->bIsTearingDown || !World->bIsWorldInitialized)
	{
		return nullptr;
	}
	return World;
}

bool UNiagaraUIWidget::CanSimulate() const
{
	if (!NiagaraUIRuntime::IsEnabled())
	{
		return false;
	}
	return !IsDesignTime() || bPreviewInDesigner;
}

UNiagaraComponent* UNiagaraUIWidget::MakeComponent(UWorld* World, UNiagaraSystem* System)
{
	// Ownerless, as Niagara's own Spawn System At Location makes them, and never saved, copied or exported.
	UNiagaraComponent* SimComponent = NewObject<UNiagaraComponent>(World, NAME_None, RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
	SimComponent->SetAutoActivate(false);
	SimComponent->SetAutoDestroy(false);

	// Drawn by this widget wherever the camera is: never culled for distance, visibility or an effect budget.
	SimComponent->SetAllowScalability(false);

	// Nothing for the scene renderer - no proxy, no render data sent each frame. The widget reads the particles itself.
	SimComponent->SetRenderingEnabled(false);
	SimComponent->SetHiddenInGame(true);
	SimComponent->SetCastShadow(false);
	SimComponent->SetCanEverAffectNavigation(false);

	ApplyRuntimeSettings(SimComponent);
	SimComponent->SetAsset(System);
	SimComponent->RegisterComponentWithWorld(World);
	ApplyParameters(SimComponent);

	if (!bTracked)
	{
		NiagaraUIRuntime::Track(this);
		bTracked = true;
	}

	if (NiagaraUIRuntime::IsLogging())
	{
		UE_LOG(LogNiagaraUI, Log, TEXT("%s: simulation of %s made in %s."), *GetPathName(), System ? *System->GetName() : TEXT("no system"), *World->GetName());
	}
	return SimComponent;
}

void UNiagaraUIWidget::ApplyRuntimeSettings(UNiagaraComponent* SimComponent) const
{
	// Ticking itself, not batched with the world's systems, is what lets it tick while the game is paused.
	SimComponent->SetTickableWhenPaused(bPlayWhilePaused);
	SimComponent->SetForceSolo(bPlayWhilePaused);
}

void UNiagaraUIWidget::ApplyParameters(UNiagaraComponent* SimComponent) const
{
	// Only what this widget changed: the rest the system already has, at its own - current - values.
	NiagaraUIParameters::ApplyChanged(SimComponent, UserParameters, UserParameterDefaults, PositionParameters);
}

void UNiagaraUIWidget::RefreshUserParameters()
{
	LayOutUserParameters();
}

bool UNiagaraUIWidget::LayOutUserParameters()
{
	return NiagaraUIParameters::LayOut(NiagaraSystem, UserParameters, UserParameterDefaults, PositionParameters);
}

void UNiagaraUIWidget::MigrateLegacyParameters()
{
	for (const TPair<FName, float>& Pair : FloatParameters)
	{
		UserParameters.SetValueFloat(NiagaraUIParameters::GetShortName(Pair.Key), Pair.Value);
	}
	for (const TPair<FName, FLinearColor>& Pair : ColorParameters)
	{
		UserParameters.SetValueStruct(NiagaraUIParameters::GetShortName(Pair.Key), FConstStructView::Make(Pair.Value));
	}
	for (const TPair<FName, FVector>& Pair : VectorParameters)
	{
		UserParameters.SetValueStruct(NiagaraUIParameters::GetShortName(Pair.Key), FConstStructView::Make(Pair.Value));
	}
	FloatParameters.Empty();
	ColorParameters.Empty();
	VectorParameters.Empty();
}

bool UNiagaraUIWidget::EnsureMainSimulation()
{
	if (IsValid(Main.Component))
	{
		return true;
	}

	Main.Component = nullptr;
	Main.bPlaced = false;
	if (!NiagaraSystem || !CanSimulate())
	{
		return false;
	}

	UWorld* World = FindSimulationWorld();
	if (!World)
	{
		return false;
	}

	Main.Component = MakeComponent(World, NiagaraSystem);
	Main.Component->OnSystemFinished.AddUniqueDynamic(this, &UNiagaraUIWidget::HandleSystemFinished);
	return true;
}

void UNiagaraUIWidget::StartSimulation(FNiagaraUISimulation& Simulation, bool bRestart)
{
	Simulation.bPendingPlay = false;

	UNiagaraComponent* SimComponent = Simulation.Component;
	if (!IsValid(SimComponent) || !SimComponent->GetAsset())
	{
		return;
	}

	ApplyOverrides(Simulation);

	if (bRestart || !SimComponent->IsActive())
	{
		SimComponent->Activate(/*bReset=*/true);

		// Only the main system opens already full; a burst plays from its first frame.
		if (&Simulation == &Main && PrewarmTime > 0.f)
		{
			SimComponent->AdvanceSimulationByTime(PrewarmTime, NiagaraUIWidgetPrivate::PrewarmStep);
		}
	}

	const bool bPause = bUserPaused || bHiddenPaused;
	if (SimComponent->IsPaused() != bPause)
	{
		SimComponent->SetPaused(bPause);
	}
	SetAnimating(!bUserPaused);
}

void UNiagaraUIWidget::ApplyOverrides(FNiagaraUISimulation& Simulation)
{
	const bool bHad = Simulation.AppliedOverrides.HasChanges();
	const bool bHas = Simulation.Overrides.HasChanges();
	if (!bHad && !bHas)
	{
		return;
	}

	// The last track's values back to the system's - and this widget's own over those it had covered - then this one's.
	UNiagaraComponent* SimComponent = Simulation.Component;
	if (bHad)
	{
		Simulation.AppliedOverrides.RestoreChanged(SimComponent);
		ApplyParameters(SimComponent);
	}
	if (bHas)
	{
		Simulation.Overrides.ApplyChanged(SimComponent);
	}
	Simulation.AppliedOverrides = Simulation.Overrides;
}

void UNiagaraUIWidget::DropOverrides(FNiagaraUISimulation& Simulation)
{
	// Taken off while the component still has the system they were for.
	if (IsValid(Simulation.Component) && Simulation.AppliedOverrides.HasChanges())
	{
		Simulation.AppliedOverrides.RestoreChanged(Simulation.Component);
		ApplyParameters(Simulation.Component);
	}
	Simulation.Overrides.Reset();
	Simulation.AppliedOverrides.Reset();
}

FNiagaraUISimulation* UNiagaraUIWidget::TakeBurst(UNiagaraSystem* System, UWorld* World)
{
	Bursts.RemoveAll([](const FNiagaraUISimulation& Burst)
	{
		return !IsValid(Burst.Component);
	});

	// A finished burst of the same system first (nothing to swap), then any finished one, then a new one, then the oldest.
	FNiagaraUISimulation* SameIdle = nullptr;
	FNiagaraUISimulation* AnyIdle = nullptr;
	FNiagaraUISimulation* Oldest = nullptr;
	for (FNiagaraUISimulation& Burst : Bursts)
	{
		const bool bBusy = Burst.bPendingPlay || Burst.Component->IsActive();
		if (!bBusy)
		{
			if (Burst.Component->GetAsset() == System)
			{
				SameIdle = &Burst;
				break;
			}
			AnyIdle = AnyIdle ? AnyIdle : &Burst;
		}
		if (!Oldest || Burst.StartedAt < Oldest->StartedAt)
		{
			Oldest = &Burst;
		}
	}

	FNiagaraUISimulation* Chosen = SameIdle ? SameIdle : AnyIdle;
	if (!Chosen && Bursts.Num() < MaxBursts)
	{
		FNiagaraUISimulation& Added = Bursts.AddDefaulted_GetRef();
		Added.Component = MakeComponent(World, System);
		return &Added;
	}

	Chosen = Chosen ? Chosen : Oldest;
	if (!Chosen)
	{
		return nullptr;
	}

	// A busy one taken over is cleared first, so nothing of it jumps to the new point.
	Chosen->Component->DeactivateImmediate();
	if (Chosen->Component->GetAsset() != System)
	{
		DropOverrides(*Chosen);
		Chosen->Component->SetAsset(System);
		ApplyParameters(Chosen->Component);
	}
	return Chosen;
}

void UNiagaraUIWidget::ReleaseSimulations()
{
	const bool bHadAny = IsValid(Main.Component) || Bursts.Num() > 0;

	ForEachComponent([this](UNiagaraComponent* SimComponent)
	{
		SimComponent->OnSystemFinished.RemoveAll(this);
		SimComponent->DestroyComponent();
	});
	Main = FNiagaraUISimulation();
	Bursts.Reset();
	bAutoPlayDone = false;
	bHiddenPaused = false;

	if (bTracked)
	{
		NiagaraUIRuntime::Untrack(this);
		bTracked = false;
	}
	SetAnimating(false);

	if (bHadAny && NiagaraUIRuntime::IsLogging())
	{
		UE_LOG(LogNiagaraUI, Log, TEXT("%s: simulations let go."), *GetPathName());
	}
}

void UNiagaraUIWidget::ApplyPause()
{
	const bool bPause = bUserPaused || bHiddenPaused;
	ForEachComponent([bPause](UNiagaraComponent* SimComponent)
	{
		if (SimComponent->IsPaused() != bPause)
		{
			SimComponent->SetPaused(bPause);
		}
	});
}

void UNiagaraUIWidget::SetAnimating(bool bAnimating)
{
	if (!MyParticles.IsValid())
	{
		bSlateVolatile = false;
		return;
	}
	if (bAnimating == bSlateVolatile)
	{
		return;
	}

	// Repainted every frame only while particles move; once still, painted once more and then cached like any widget.
	bSlateVolatile = bAnimating;
	MyParticles->ForceVolatile(bAnimating || bIsVolatile);
	MyParticles->Invalidate(EInvalidateWidgetReason::Paint);
}

void UNiagaraUIWidget::HandleSystemFinished(UNiagaraComponent* FinishedComponent)
{
	if (FinishedComponent && FinishedComponent == Main.Component)
	{
		OnFinished.Broadcast(this);
	}
}

#undef LOCTEXT_NAMESPACE
