// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimTextGlow.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Slate/SRetainerWidget.h"
#include "SWidgetAnimText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

namespace WidgetAnimTextGlow
{
	/** The parameters every effect material is given - see UWidgetAnimRuntimeSettings::TextGlowMaterial. */
	const FName TextureParameter(TEXT("Texture"));
	const FName RadiusParameter(TEXT("Radius"));
	const FName TagStrengthParameter(TEXT("TagStrength"));
}

void SWidgetAnimTextGlow::Construct(const FArguments& InArgs)
{
	Text = InArgs._Text;
	bOver = InArgs._Over;
	StyleSource = InArgs._StyleSource;
	Effects = InArgs._Effects;

	ChildSlot
	[
		SAssignNew(Overlay, SOverlay)
	];

	SetCanTick(true);
}

void SWidgetAnimTextGlow::ForEachLayerText(TFunctionRef<void(SWidgetAnimText&)> Visit) const
{
	for (const TSharedRef<FLayer>& Layer : Layers)
	{
		if (Layer->Text.IsValid())
		{
			Visit(*Layer->Text);
		}
	}
}

void SWidgetAnimTextGlow::AddLayer(const FWidgetAnimTextEffectLayer& Wanted)
{
	const TSharedRef<FLayer> Layer = MakeShared<FLayer>();
	Layer->Key = Wanted.Key;
	const TWeakPtr<FLayer> WeakLayer = Layer;

	Layer->Text = SNew(SWidgetAnimText)
		.StyleSource(StyleSource)
		.Effects(Effects)
		.GlowOnly(true)
		.EffectKey(Wanted.Key);

	// Its material, loaded once as the layer is made. One that can't be loaded leaves the layer undrawn.
	UMaterialInterface* Material = Cast<UMaterialInterface>(Wanted.Material.TryLoad());
	Layer->bHasMaterial = Material != nullptr;

	// Reaching out past the text by its room on every side, then padded back in, so the copy lines up with the text.
	Layer->Root = SAssignNew(Layer->Retainer, SRetainerWidget)
		.RenderOnPhase(true)
		.RenderOnInvalidation(false)
		.Phase(0)
		.PhaseCount(1)
		.Visibility(Layer->bHasMaterial ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
		[
			SNew(SBox)
			.Padding(TAttribute<FMargin>::CreateLambda([WeakLayer]()
			{
				const TSharedPtr<FLayer> Pinned = WeakLayer.Pin();
				return FMargin(Pinned.IsValid() ? Pinned->Pad : 0.f);
			}))
			[
				Layer->Text.ToSharedRef()
			]
		];

	if (Material)
	{
		Layer->Retainer->SetEffectMaterial(Material);
		Layer->Retainer->SetTextureParameter(WidgetAnimTextGlow::TextureParameter);
	}

	Overlay->AddSlot()
	.Padding(TAttribute<FMargin>::CreateLambda([WeakLayer]()
	{
		const TSharedPtr<FLayer> Pinned = WeakLayer.Pin();
		return FMargin(Pinned.IsValid() ? -Pinned->Pad : 0.f);
	}))
	[
		Layer->Root.ToSharedRef()
	];

	Layers.Add(Layer);
}

void SWidgetAnimTextGlow::UpdateLayer(FLayer& Layer, const FWidgetAnimTextEffectLayer& Wanted, float Scale)
{
	// Twice the radius each way, so the effect fades out well inside the layer rather than being cut off at its edge.
	const float NewPad = FMath::CeilToFloat(Wanted.Radius * 2.f) + 2.f;
	if (NewPad != Layer.Pad)
	{
		Layer.Pad = NewPad;
		Invalidate(EInvalidateWidgetReason::Layout);
	}

	UMaterialInstanceDynamic* Material = Layer.bHasMaterial && Layer.Retainer.IsValid() ? Layer.Retainer->GetEffectMaterial() : nullptr;
	if (!Material)
	{
		return;
	}

	// The tags' own parameters are part of what makes the layer, so they never change for it: set once.
	if (!Layer.bParametersSet)
	{
		Layer.bParametersSet = true;
		for (const TPair<FName, float>& Parameter : Wanted.ScalarParameters)
		{
			Material->SetScalarParameterValue(Parameter.Key, Parameter.Value);
		}
		for (const TPair<FName, FLinearColor>& Parameter : Wanted.VectorParameters)
		{
			Material->SetVectorParameterValue(Parameter.Key, Parameter.Value);
		}
	}

	// The radius in the render target's pixels, which follow the text's scale on screen.
	const float PixelRadius = Wanted.Radius * Scale;
	if (PixelRadius != Layer.AppliedRadius)
	{
		Layer.AppliedRadius = PixelRadius;
		Material->SetScalarParameterValue(WidgetAnimTextGlow::RadiusParameter, PixelRadius);
	}
	if (Wanted.Strength != Layer.AppliedStrength)
	{
		Layer.AppliedStrength = Wanted.Strength;
		Material->SetScalarParameterValue(WidgetAnimTextGlow::TagStrengthParameter, Wanted.Strength);
	}
}

void SWidgetAnimTextGlow::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	const TSharedPtr<SWidgetAnimText> Shown = Text.Pin();
	if (!Shown.IsValid() || !Overlay.IsValid())
	{
		return;
	}

	// The layers the text's tags ask for on this side. Worked out once per text, so this is cheap every frame.
	const TArray<FWidgetAnimTextEffectLayer>& Wanted = Shown->GetEffectLayers();

	// Layers no longer asked for go.
	for (int32 Index = Layers.Num() - 1; Index >= 0; --Index)
	{
		const uint32 Key = Layers[Index]->Key;
		const bool bStillWanted = Wanted.ContainsByPredicate([this, Key](const FWidgetAnimTextEffectLayer& Layer) { return Layer.Key == Key && Layer.bOver == bOver; });
		if (!bStillWanted)
		{
			if (Layers[Index]->Root.IsValid())
			{
				Overlay->RemoveSlot(Layers[Index]->Root.ToSharedRef());
			}
			Layers.RemoveAt(Index);
		}
	}

	// New ones come; every one is kept up to date.
	for (const FWidgetAnimTextEffectLayer& Layer : Wanted)
	{
		if (Layer.bOver != bOver)
		{
			continue;
		}
		const TSharedRef<FLayer>* Existing = Layers.FindByPredicate([&Layer](const TSharedRef<FLayer>& Candidate) { return Candidate->Key == Layer.Key; });
		if (!Existing)
		{
			AddLayer(Layer);
			Existing = &Layers.Last();
		}
		UpdateLayer(**Existing, Layer, AllottedGeometry.Scale);
	}
}
