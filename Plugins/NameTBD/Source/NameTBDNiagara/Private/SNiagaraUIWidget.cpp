// Copyright AR GameSystems 2026. All rights reserved.

#include "SNiagaraUIWidget.h"

#include "NiagaraUIWidget.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void SNiagaraUIWidget::Construct(const FArguments& InArgs)
{
	OwnerWidget = InArgs._Owner;
}

FVector2D SNiagaraUIWidget::ComputeDesiredSize(float /*LayoutScaleMultiplier*/) const
{
	const UNiagaraUIWidget* Particles = OwnerWidget.Get();
	return Particles ? Particles->DesiredSize : FVector2D(128.0, 128.0);
}

int32 SNiagaraUIWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SNiagaraUIWidget::OnPaint);

	UNiagaraUIWidget* Particles = OwnerWidget.Get();
	if (!Particles)
	{
		return LayerId;
	}

	// The widget places its simulations where this paint puts it (a world-space emitter's next particles spawn there) and
	// says which have particles to draw.
	Sources.Reset();
	Particles->PrepareForPaint(AllottedGeometry, Sources);
	if (Sources.Num() == 0)
	{
		Mesh.Reset();
		return LayerId;
	}

	FNiagaraUIBuildSettings Settings;
	Settings.Plane = Particles->Plane;
	Settings.PerspectiveDistance = FMath::Max(Particles->PerspectiveDistance, 0.f);
	Settings.bSmoothRibbons = Particles->bSmoothRibbons;
	Settings.Tint = InWidgetStyle.GetColorAndOpacityTint() * Particles->ColorAndOpacity;
	Settings.CullRect = MyCullingRect;
	Settings.MaterialRemap = &Particles->MaterialRemap;
	if (Particles->ParticleImageSource == EWidgetImageSource::SVG && Particles->ParticleSVG)
	{
		// One drawing for every particle, its longest side Particle SVG Resolution (a soft dot until it is ready).
		const FVector2D SVGSize = Particles->ParticleSVG->SVGSize;
		const double Side = FMath::Clamp(Particles->ParticleSVGResolution, 8, 1024);
		const double SVGScale = Side / FMath::Max(FMath::Max(SVGSize.X, SVGSize.Y), 1.0);
		const FIntPoint PixelSize(FMath::Max(FMath::CeilToInt32(SVGSize.X * SVGScale), 1), FMath::Max(FMath::CeilToInt32(SVGSize.Y * SVGScale), 1));
		Settings.ParticleImage = ParticleSVGBrush.GetAtPixelSize(Particles->ParticleSVG, &Particles->ParticleImage, PixelSize, this);
	}
	else
	{
		ParticleSVGBrush.Reset();
		Settings.ParticleImage = Particles->ParticleImage.GetResourceObject() ? &Particles->ParticleImage : nullptr;
	}
	Settings.Owner = Particles;

	Mesh.Build(Sources, Settings);
	Mesh.Paint(OutDrawElements, LayerId);
	return LayerId;
}
