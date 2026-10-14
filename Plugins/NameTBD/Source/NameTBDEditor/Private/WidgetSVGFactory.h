// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "EditorReimportHandler.h"
#include "Factories/Factory.h"
#include "IDetailCustomization.h"
#include "ThumbnailRendering/ThumbnailRenderer.h"

#include "WidgetSVGFactory.generated.h"

class UWidgetSVG;

/**
 * Imports a .svg file as an SVG asset (UWidgetSVG): drag it into the Content Browser, or Import. Reimport reads the file
 * again and keeps the colours set on the asset - for SVG assets, and for SVG Textures (Texture 2Ds made from one, the
 * SVG kept on them as UWidgetSVGTextureData).
 *
 * Its import priority is above the default, so it is the one used for .svg when another plugin (Epic's SVG Importer)
 * also takes them; its reimport priority is above the engine's texture reimporter, which would otherwise take SVG
 * Textures and fail on an .svg.
 */
UCLASS()
class UWidgetSVGFactory : public UFactory, public FReimportHandler
{
	GENERATED_BODY()

public:

	UWidgetSVGFactory();

	virtual UObject* FactoryCreateText(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context,
		const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;

	// FReimportHandler
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
	virtual EReimportResult::Type Reimport(UObject* Obj) override;
	virtual int32 GetPriority() const override;

	/** Makes an SVG Texture from each SVG, next to it, and selects them in the Content Browser. */
	static void CreateSVGTextures(const TArray<UWidgetSVG*>& SVGs);
};

/**
 * How the Content Browser shows an SVG: under User Interface, Reimport and Create SVG Texture in its menu, and the generic
 * asset editor on a double-click (its Details: the colours, Draw In White).
 */
class FWidgetSVGAssetTypeActions : public FAssetTypeActions_Base
{
public:

	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
	virtual bool IsImportedAsset() const override { return true; }
	virtual void GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const override;
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override { return true; }
	virtual void GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section) override;
};

/** An SVG's thumbnail: drawn to fit, on the Content Browser's dark background. */
UCLASS()
class UWidgetSVGThumbnailRenderer : public UThumbnailRenderer
{
	GENERATED_BODY()

public:

	virtual void GetThumbnailSize(UObject* Object, float Zoom, uint32& OutWidth, uint32& OutHeight) const override;
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;
	virtual bool AllowsRealtimeThumbnails(UObject* Object) const override { return false; }
};

/**
 * A Texture 2D's Details (the Texture Editor, a Details panel): for an SVG Texture, an SVG category at the top with its
 * colours, Draw In White and resolution, edited in place. Other textures are left as they are.
 */
class FWidgetSVGTextureDetails : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
