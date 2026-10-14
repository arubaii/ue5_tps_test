// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetEffectImage.h"

#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"

// =====================================================================================================
// FWidgetEffectImage
// =====================================================================================================

FWidgetEffectImage::FWidgetEffectImage()
{
	// An empty brush: nothing is drawn until an image is picked.
	Image.SetResourceObject(nullptr);
	Image.DrawAs = ESlateBrushDrawType::Image;
}

bool FWidgetEffectImage::IsSet() const
{
	return ImageSource == EWidgetImageSource::SVG ? SVG != nullptr : Image.GetResourceObject() != nullptr;
}

UMaterialInterface* FWidgetEffectImage::GetMaterial() const
{
	return ImageSource == EWidgetImageSource::Texture ? Cast<UMaterialInterface>(Image.GetResourceObject()) : nullptr;
}

uint32 FWidgetEffectImage::GetHash() const
{
	uint32 Hash = GetTypeHash(static_cast<uint8>(ImageSource));
	Hash = HashCombineFast(Hash, PointerHash(Image.GetResourceObject()));
	Hash = HashCombineFast(Hash, PointerHash(SVG.Get()));
	return HashCombineFast(Hash, WidgetMaterialParameters::Hash(MaterialParameters));
}

// =====================================================================================================
// Parameters
// =====================================================================================================

namespace WidgetMaterialParameters
{
	void Apply(UMaterialInstanceDynamic* Dynamic, const TArray<FWidgetMaterialParameter>& Parameters)
	{
		if (!Dynamic)
		{
			return;
		}
		for (const FWidgetMaterialParameter& Parameter : Parameters)
		{
			if (Parameter.Name.IsNone())
			{
				continue;
			}
			switch (Parameter.Type)
			{
			case EWidgetMaterialParameterType::Scalar:
				Dynamic->SetScalarParameterValue(Parameter.Name, Parameter.Scalar);
				break;
			case EWidgetMaterialParameterType::Color:
				Dynamic->SetVectorParameterValue(Parameter.Name, Parameter.Color);
				break;
			case EWidgetMaterialParameterType::Texture:
				if (Parameter.Texture)
				{
					Dynamic->SetTextureParameterValue(Parameter.Name, Parameter.Texture);
				}
				break;
			}
		}
	}

	uint32 Hash(const TArray<FWidgetMaterialParameter>& Parameters)
	{
		uint32 Hash = GetTypeHash(Parameters.Num());
		for (const FWidgetMaterialParameter& Parameter : Parameters)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Parameter.Name));
			Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Parameter.Type)));
			Hash = HashCombineFast(Hash, GetTypeHash(Parameter.Scalar));
			Hash = HashCombineFast(Hash, GetTypeHash(Parameter.Color));
			Hash = HashCombineFast(Hash, PointerHash(Parameter.Texture.Get()));
		}
		return Hash;
	}
}

// =====================================================================================================
// FWidgetEffectMaterials
// =====================================================================================================

FWidgetEffectMaterials::FWidgetEffectMaterials() = default;
FWidgetEffectMaterials::~FWidgetEffectMaterials() = default;

const FSlateBrush* FWidgetEffectMaterials::GetBrush(uint64 Key, const FWidgetEffectImage& Image, const FWidgetEffectMaterialInputs* Inputs, const FGeometry& Geometry,
	const FVector2f& LocalSize, const SWidget& Widget)
{
	if (!Image.IsSet())
	{
		return nullptr;
	}

	if (Image.ImageSource == EWidgetImageSource::SVG)
	{
		FEntry& Entry = Entries.FindOrAdd(Key);
		if (!Entry.SVG.IsValid())
		{
			Entry.SVG = MakeShared<FWidgetSVGBrush>();
		}
		return Entry.SVG->Get(Image.SVG, nullptr, Geometry, FVector2f(FMath::Max(LocalSize.X, 1.f), FMath::Max(LocalSize.Y, 1.f)), Widget);
	}

	UMaterialInterface* Material = Image.GetMaterial();
	if (!Material)
	{
		// A texture: drawn with its own brush.
		Entries.Remove(Key);
		return &Image.Image;
	}

	// A material: an instance of its own, so live values can differ from one line or overlay to the next.
	FEntry& Entry = Entries.FindOrAdd(Key);
	const uint32 ParameterHash = WidgetMaterialParameters::Hash(Image.MaterialParameters);
	if (!Entry.Dynamic.IsValid() || Entry.Source.Get() != Material)
	{
		Entry.Dynamic.Reset(UMaterialInstanceDynamic::Create(Material, GetTransientPackage()));
		Entry.Source = Material;
		Entry.ParameterHash = ParameterHash ^ 1u;
		Entry.LastScalars.Reset();
		Entry.LastVectors.Reset();
	}
	UMaterialInstanceDynamic* Dynamic = Entry.Dynamic.Get();
	if (!Dynamic)
	{
		return &Image.Image;
	}
	if (Entry.ParameterHash != ParameterHash)
	{
		Entry.ParameterHash = ParameterHash;
		WidgetMaterialParameters::Apply(Dynamic, Image.MaterialParameters);
	}
	if (Inputs)
	{
		for (const TPair<FName, float>& Scalar : Inputs->Scalars)
		{
			float* Last = Entry.LastScalars.Find(Scalar.Key);
			if (!Last || *Last != Scalar.Value)
			{
				Dynamic->SetScalarParameterValue(Scalar.Key, Scalar.Value);
				Entry.LastScalars.Add(Scalar.Key, Scalar.Value);
			}
		}
		for (const TPair<FName, FLinearColor>& Vector : Inputs->Vectors)
		{
			FLinearColor* Last = Entry.LastVectors.Find(Vector.Key);
			if (!Last || *Last != Vector.Value)
			{
				Dynamic->SetVectorParameterValue(Vector.Key, Vector.Value);
				Entry.LastVectors.Add(Vector.Key, Vector.Value);
			}
		}
	}

	// The brush as authored (tint, margins), holding the instance - copied again only when the brush changes.
	uint32 BrushHash = GetTypeHash(Image.Image.TintColor.GetSpecifiedColor());
	BrushHash = HashCombineFast(BrushHash, HashCombineFast(GetTypeHash(static_cast<float>(Image.Image.ImageSize.X)), GetTypeHash(static_cast<float>(Image.Image.ImageSize.Y))));
	BrushHash = HashCombineFast(BrushHash, GetTypeHash(static_cast<uint8>(Image.Image.DrawAs.GetValue())));
	if (Entry.Brush.GetResourceObject() != Dynamic || Entry.BrushHash != BrushHash)
	{
		Entry.Brush = Image.Image;
		Entry.Brush.SetResourceObject(Dynamic);
		Entry.BrushHash = BrushHash;
	}
	return &Entry.Brush;
}

void FWidgetEffectMaterials::Reset()
{
	Entries.Reset();
}

void FWidgetEffectMaterials::Prune(const TSet<uint64>& Keep)
{
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		if (!Keep.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}
