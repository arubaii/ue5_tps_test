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

	bool SyncFromMaterial(TArray<FWidgetMaterialParameter>& Parameters, UMaterialInterface* Material)
	{
		if (!Material)
		{
			const bool bHad = Parameters.Num() > 0;
			Parameters.Reset();
			return bHad;
		}

		TArray<FWidgetMaterialParameter> Listed;
		auto Add = [&Parameters, &Listed](const FMaterialParameterInfo& Info, EWidgetMaterialParameterType ParameterType) -> FWidgetMaterialParameter&
		{
			FWidgetMaterialParameter& Parameter = Listed.AddDefaulted_GetRef();
			const FWidgetMaterialParameter* Had = Parameters.FindByPredicate([&Info, ParameterType](const FWidgetMaterialParameter& Existing)
			{
				return Existing.Name == Info.Name && Existing.Type == ParameterType;
			});
			if (Had)
			{
				Parameter = *Had;
			}
			Parameter.Name = Info.Name;
			Parameter.Type = ParameterType;
			return Parameter;
		};

		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		Material->GetAllScalarParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			const bool bNew = !Parameters.ContainsByPredicate([&Info](const FWidgetMaterialParameter& Existing) { return Existing.Name == Info.Name && Existing.Type == EWidgetMaterialParameterType::Scalar; });
			FWidgetMaterialParameter& Parameter = Add(Info, EWidgetMaterialParameterType::Scalar);
			float Default = 0.f;
			if (bNew && Material->GetScalarParameterDefaultValue(Info, Default))
			{
				Parameter.Scalar = Default;
			}
		}
		Infos.Reset();
		Ids.Reset();
		Material->GetAllVectorParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			const bool bNew = !Parameters.ContainsByPredicate([&Info](const FWidgetMaterialParameter& Existing) { return Existing.Name == Info.Name && Existing.Type == EWidgetMaterialParameterType::Color; });
			FWidgetMaterialParameter& Parameter = Add(Info, EWidgetMaterialParameterType::Color);
			FLinearColor Default = FLinearColor::White;
			if (bNew && Material->GetVectorParameterDefaultValue(Info, Default))
			{
				Parameter.Color = Default;
			}
		}
		Infos.Reset();
		Ids.Reset();
		Material->GetAllTextureParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			const bool bNew = !Parameters.ContainsByPredicate([&Info](const FWidgetMaterialParameter& Existing) { return Existing.Name == Info.Name && Existing.Type == EWidgetMaterialParameterType::Texture; });
			FWidgetMaterialParameter& Parameter = Add(Info, EWidgetMaterialParameterType::Texture);
			UTexture* Default = nullptr;
			if (bNew && Material->GetTextureParameterDefaultValue(Info, Default))
			{
				Parameter.Texture = Default;
			}
		}

		// Changed only when the names or kinds did: the values are kept either way (a Details panel isn't rebuilt for nothing).
		bool bChanged = Listed.Num() != Parameters.Num();
		for (int32 Index = 0; !bChanged && Index < Listed.Num(); ++Index)
		{
			bChanged = Listed[Index].Name != Parameters[Index].Name || Listed[Index].Type != Parameters[Index].Type;
		}
		if (bChanged)
		{
			Parameters = MoveTemp(Listed);
		}
		return bChanged;
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
	const FSlateBrush* Brush = GetMaterialBrush(Key, Material, Image.MaterialParameters, Inputs, &Image.Image);
	return Brush ? Brush : &Image.Image;
}

const FSlateBrush* FWidgetEffectMaterials::GetMaterialBrush(uint64 Key, UMaterialInterface* Material, const TArray<FWidgetMaterialParameter>& Parameters,
	const FWidgetEffectMaterialInputs* Inputs, const FSlateBrush* Template)
{
	if (!Material)
	{
		Entries.Remove(Key);
		return nullptr;
	}
	FEntry& Entry = Entries.FindOrAdd(Key);
	const uint32 ParameterHash = WidgetMaterialParameters::Hash(Parameters);
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
		return nullptr;
	}
	if (Entry.ParameterHash != ParameterHash)
	{
		Entry.ParameterHash = ParameterHash;
		WidgetMaterialParameters::Apply(Dynamic, Parameters);
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
	static const FSlateBrush PlainImage;
	const FSlateBrush& Look = Template ? *Template : PlainImage;
	uint32 BrushHash = GetTypeHash(Look.TintColor.GetSpecifiedColor());
	BrushHash = HashCombineFast(BrushHash, HashCombineFast(GetTypeHash(static_cast<float>(Look.ImageSize.X)), GetTypeHash(static_cast<float>(Look.ImageSize.Y))));
	BrushHash = HashCombineFast(BrushHash, GetTypeHash(static_cast<uint8>(Look.DrawAs.GetValue())));
	if (Entry.Brush.GetResourceObject() != Dynamic || Entry.BrushHash != BrushHash)
	{
		Entry.Brush = Look;
		Entry.Brush.SetResourceObject(Dynamic);
		Entry.BrushHash = BrushHash;
	}
	return &Entry.Brush;
}

UMaterialInstanceDynamic* FWidgetEffectMaterials::FindDynamic(uint64 Key) const
{
	const FEntry* Entry = Entries.Find(Key);
	return Entry ? Entry->Dynamic.Get() : nullptr;
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
