// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "NameTBD/Widgets/WidgetSVG.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"

#include "WidgetEffectImage.generated.h"

class SWidget;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture;
struct FGeometry;

/*
 * Images and materials for the effect widgets' looks (a Widget Path's lines, glows, markers and flowing marks, Pointer
 * Effects' overlays): a texture, a material or an SVG, and for a material, parameters to set on it. A material is given a
 * dynamic instance of its own per thing drawn (per line, per overlay), so the widget can hand it live values - how long the
 * line is, where the pointer is - besides the ones set here. What each look hands its material is in its own tooltip.
 */

/** What kind of value a material parameter is. */
UENUM(BlueprintType)
enum class EWidgetMaterialParameterType : uint8
{
	Scalar UMETA(ToolTip = "A number."),
	Color UMETA(ToolTip = "A color (vector) parameter."),
	Texture UMETA(ToolTip = "A texture parameter.")
};

/** One parameter set on an effect's material. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetMaterialParameter
{
	GENERATED_BODY()

	/** The parameter's name in the material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (DisplayName = "Parameter Name"))
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
	EWidgetMaterialParameterType Type = EWidgetMaterialParameterType::Scalar;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (EditCondition = "Type == EWidgetMaterialParameterType::Scalar", EditConditionHides))
	float Scalar = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (EditCondition = "Type == EWidgetMaterialParameterType::Color", EditConditionHides))
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (EditCondition = "Type == EWidgetMaterialParameterType::Texture", EditConditionHides))
	TObjectPtr<UTexture> Texture = nullptr;
};

/** A picture an effect draws: a texture or material (the brush), or an SVG drawn at the size it is shown. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetEffectImage
{
	GENERATED_BODY()

	/** Where the picture comes from: the Image brush (a texture or a material), or an SVG drawn at the size it is shown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image")
	EWidgetImageSource ImageSource = EWidgetImageSource::Texture;

	/** A texture or a material. A material gets its own dynamic instance, with Material Parameters and the live values its effect hands it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image", meta = (EditCondition = "ImageSource == EWidgetImageSource::Texture", EditConditionHides))
	FSlateBrush Image;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image", meta = (EditCondition = "ImageSource == EWidgetImageSource::SVG", EditConditionHides, DisplayName = "SVG"))
	TObjectPtr<UWidgetSVG> SVG = nullptr;

	/** Set on the Image's material (when it is one). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Image", meta = (EditCondition = "ImageSource == EWidgetImageSource::Texture", EditConditionHides, TitleProperty = "Name"))
	TArray<FWidgetMaterialParameter> MaterialParameters;

	FWidgetEffectImage();

	/** Whether there is anything to draw. */
	bool IsSet() const;

	/** The Image brush's material, when it holds one (and Image Source is the brush). */
	UMaterialInterface* GetMaterial() const;

	/** A hash of what it draws, for caches. */
	uint32 GetHash() const;
};

/** Live values handed to an effect's material each time it is drawn (beside its Material Parameters). */
struct FWidgetEffectMaterialInputs
{
	TArray<TPair<FName, float>, TInlineAllocator<8>> Scalars;
	TArray<TPair<FName, FLinearColor>, TInlineAllocator<4>> Vectors;

	void Add(FName Name, float Value) { Scalars.Emplace(Name, Value); }
	void Add(FName Name, const FLinearColor& Value) { Vectors.Emplace(Name, Value); }
};

/**
 * The brushes an effect draws its images with: the image's own brush for a texture, a copy holding a dynamic instance of the
 * material (one per Key - per line, per overlay) for a material, the drawing of an SVG. Owns those instances (kept alive while
 * it lives); parameters are only set again when they change. Game thread.
 */
class NAMETBD_API FWidgetEffectMaterials
{
public:
	FWidgetEffectMaterials();
	~FWidgetEffectMaterials();

	/**
	 * The brush to draw Image with: null when there is nothing to draw (or an SVG's first drawing isn't ready - Widget is
	 * repainted when it is). LocalSize: what an SVG is drawn at.
	 */
	const FSlateBrush* GetBrush(uint64 Key, const FWidgetEffectImage& Image, const FWidgetEffectMaterialInputs* Inputs, const FGeometry& Geometry,
		const FVector2f& LocalSize, const SWidget& Widget);

	/**
	 * The brush for a material with Parameters (and live Inputs) set on a dynamic instance of its own, one per Key: Template's
	 * look (tint, margins) when given, else a plain image brush. Null with no material.
	 */
	const FSlateBrush* GetMaterialBrush(uint64 Key, UMaterialInterface* Material, const TArray<FWidgetMaterialParameter>& Parameters,
		const FWidgetEffectMaterialInputs* Inputs, const FSlateBrush* Template = nullptr);

	/** The dynamic instance drawn for Key, once it has been drawn. */
	UMaterialInstanceDynamic* FindDynamic(uint64 Key) const;

	/** Lets go of every instance and drawing. */
	void Reset();

	/** Lets go of those whose keys aren't in Keep. */
	void Prune(const TSet<uint64>& Keep);

private:
	struct FEntry
	{
		TStrongObjectPtr<UMaterialInstanceDynamic> Dynamic;
		TWeakObjectPtr<UMaterialInterface> Source;
		uint32 ParameterHash = 0;
		uint32 BrushHash = 0;
		FSlateBrush Brush;
		TMap<FName, float> LastScalars;
		TMap<FName, FLinearColor> LastVectors;
		TSharedPtr<FWidgetSVGBrush> SVG;
	};

	TMap<uint64, FEntry> Entries;
};

/** Writes Parameters onto a dynamic material instance. */
namespace WidgetMaterialParameters
{
	NAMETBD_API void Apply(UMaterialInstanceDynamic* Dynamic, const TArray<FWidgetMaterialParameter>& Parameters);
	NAMETBD_API uint32 Hash(const TArray<FWidgetMaterialParameter>& Parameters);

	/**
	 * Lists Material's scalar, vector and texture parameters in Parameters, in the material's order: each keeps the value it had
	 * (by name and kind), a new one takes the material's default. Null empties it. True when the list (names, kinds) changed.
	 */
	NAMETBD_API bool SyncFromMaterial(TArray<FWidgetMaterialParameter>& Parameters, UMaterialInterface* Material);
}
