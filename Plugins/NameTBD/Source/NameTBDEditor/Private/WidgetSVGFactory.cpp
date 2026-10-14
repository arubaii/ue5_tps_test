// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetSVGFactory.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Texture2D.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IAssetTools.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Misc/FileHelper.h"
#include "NameTBD/Widgets/WidgetSVG.h"
#include "NameTBD/Widgets/WidgetSVGRaster.h"
#include "NameTBD/Widgets/WidgetSVGTexture.h"
#include "NameTBDColors.h"
#include "NameTBDTooltips.h"
#include "Styling/AppStyle.h"
#include "Subsystems/ImportSubsystem.h"
#include "TextureCompiler.h"
#include "ToolMenuSection.h"
#include "UObject/Package.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "WidgetSVGFactory"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetSVG, Log, All);

namespace WidgetSVGFactoryPrivate
{
	/** Where an SVG asset or an SVG Texture was imported from, or null if Obj is neither. */
	static UAssetImportData* GetImportData(UObject* Obj)
	{
		if (UWidgetSVG* SVG = Cast<UWidgetSVG>(Obj))
		{
			return SVG->AssetImportData;
		}
		UTexture2D* SVGTexture = Cast<UTexture2D>(Obj);
		if (SVGTexture && UWidgetSVGTextureData::FindOn(SVGTexture))
		{
			return SVGTexture->AssetImportData;
		}
		return nullptr;
	}
}

// ---- UWidgetSVGFactory ------------------------------------------------------------------------------------------

UWidgetSVGFactory::UWidgetSVGFactory()
{
	SupportedClass = UWidgetSVG::StaticClass();
	Formats.Add(TEXT("svg;Scalable Vector Graphics"));
	bCreateNew = false;
	bEditorImport = true;
	bText = true;
	ImportPriority = DefaultImportPriority + 10;
}

FText UWidgetSVGFactory::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "SVG");
}

UObject* UWidgetSVGFactory::FactoryCreateText(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn)
{
	UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>();
	ImportSubsystem->BroadcastAssetPreImport(this, InClass, InParent, InName, Type);

	UWidgetSVG* SVG = NewObject<UWidgetSVG>(InParent, InName, Flags | RF_Transactional);
	FText Error;
	if (!SVG->SetSVGSource(FString(UE_PTRDIFF_TO_INT32(BufferEnd - Buffer), Buffer), Error))
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("%s: %s"), *CurrentFilename, *Error.ToString());
		SVG->MarkAsGarbage();
		ImportSubsystem->BroadcastAssetPostImport(this, nullptr);
		return nullptr;
	}
	SVG->AssetImportData->Update(CurrentFilename);

	ImportSubsystem->BroadcastAssetPostImport(this, SVG);
	return SVG;
}

bool UWidgetSVGFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	if (const UAssetImportData* ImportData = WidgetSVGFactoryPrivate::GetImportData(Obj))
	{
		ImportData->ExtractFilenames(OutFilenames);
		return true;
	}
	return false;
}

void UWidgetSVGFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UAssetImportData* ImportData = WidgetSVGFactoryPrivate::GetImportData(Obj);
	if (ImportData && ensure(NewReimportPaths.Num() == 1))
	{
		ImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UWidgetSVGFactory::Reimport(UObject* Obj)
{
	UAssetImportData* ImportData = WidgetSVGFactoryPrivate::GetImportData(Obj);
	if (!ImportData)
	{
		return EReimportResult::Failed;
	}

	const FString Filename = ImportData->GetFirstFilename();
	FString SVGText;
	if (Filename.IsEmpty() || !FFileHelper::LoadFileToString(SVGText, *Filename))
	{
		UE_LOG(LogWidgetSVG, Warning, TEXT("Reimport of %s: can't read '%s'."), *Obj->GetPathName(), *Filename);
		return EReimportResult::Failed;
	}

	FText Error;
	bool bTaken = false;
	if (UWidgetSVG* SVG = Cast<UWidgetSVG>(Obj))
	{
		bTaken = SVG->SetSVGSource(SVGText, Error);
		if (bTaken)
		{
			SVG->PostEditChange();
		}
	}
	else if (UWidgetSVGTextureData* SVGData = UWidgetSVGTextureData::FindOn(Cast<UTexture2D>(Obj)))
	{
		bTaken = SVGData->SetSVGSource(SVGText, Error);
	}
	if (!bTaken)
	{
		UE_LOG(LogWidgetSVG, Warning, TEXT("Reimport of %s: %s"), *Obj->GetPathName(), *Error.ToString());
		return EReimportResult::Failed;
	}

	ImportData->Update(Filename);
	Obj->MarkPackageDirty();
	return EReimportResult::Succeeded;
}

int32 UWidgetSVGFactory::GetPriority() const
{
	return ImportPriority;
}

void UWidgetSVGFactory::CreateSVGTextures(const TArray<UWidgetSVG*>& SVGs)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UObject*> Created;

	for (UWidgetSVG* SVG : SVGs)
	{
		if (!SVG)
		{
			continue;
		}

		FString PackageName;
		FString AssetName;
		AssetTools.CreateUniqueAssetName(SVG->GetOutermost()->GetName(), TEXT("_Texture"), PackageName, AssetName);
		UPackage* Package = CreatePackage(*PackageName);
		UTexture2D* SVGTexture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		UWidgetSVGTextureData::ApplyTextureSettings(SVGTexture);

		// The SVG rides on the texture as Asset User Data, added through the interface (a virtual call: UTexture's own
		// functions are not all exported to plugins). It starts with the SVG's colours.
		IInterface_AssetUserData* UserDataOwner = Cast<IInterface_AssetUserData>(SVGTexture);
		UWidgetSVGTextureData* SVGData = UserDataOwner ? NewObject<UWidgetSVGTextureData>(SVGTexture, NAME_None, RF_Transactional) : nullptr;
		FText Error = LOCTEXT("NoUserData", "This engine's textures can't hold Asset User Data.");
		if (SVGData)
		{
			UserDataOwner->AddAssetUserData(SVGData);
			SVGData->Colors = SVG->Colors;
			SVGData->bDrawInWhite = SVG->bDrawInWhite;
		}
		if (!SVGData || !SVGData->SetSVGSource(SVG->SVGSource, Error))
		{
			SVGTexture->MarkAsGarbage();
			FNotificationInfo Info(FText::Format(NameTBD::Tooltips::SVG::CreateSVGTextureFailed(), FText::FromString(SVG->GetName()), Error));
			Info.ExpireDuration = 6.f;
			FSlateNotificationManager::Get().AddNotification(Info);
			continue;
		}

		// Reimport reads the SVG's own file.
		const FString SourceFile = SVG->AssetImportData ? SVG->AssetImportData->GetFirstFilename() : FString();
		if (!SourceFile.IsEmpty())
		{
			SVGTexture->AssetImportData->Update(SourceFile);
		}

		FAssetRegistryModule::AssetCreated(SVGTexture);
		Package->MarkPackageDirty();
		Created.Add(SVGTexture);
	}

	if (Created.Num() > 0 && GEditor)
	{
		GEditor->SyncBrowserToObjects(Created);
	}
}

// ---- FWidgetSVGAssetTypeActions ---------------------------------------------------------------------------------

FText FWidgetSVGAssetTypeActions::GetName() const
{
	return LOCTEXT("AssetTypeName", "SVG");
}

FColor FWidgetSVGAssetTypeActions::GetTypeColor() const
{
	return NameTBD::Colors::SVG::AssetType;
}

UClass* FWidgetSVGAssetTypeActions::GetSupportedClass() const
{
	return UWidgetSVG::StaticClass();
}

uint32 FWidgetSVGAssetTypeActions::GetCategories()
{
	return EAssetTypeCategories::UI;
}

void FWidgetSVGAssetTypeActions::GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const
{
	for (UObject* Asset : TypeAssets)
	{
		const UWidgetSVG* SVG = Cast<UWidgetSVG>(Asset);
		if (SVG && SVG->AssetImportData)
		{
			SVG->AssetImportData->ExtractFilenames(OutSourceFilePaths);
		}
	}
}

void FWidgetSVGAssetTypeActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	TArray<TWeakObjectPtr<UWidgetSVG>> WeakSVGs;
	for (UObject* Object : InObjects)
	{
		if (UWidgetSVG* SVG = Cast<UWidgetSVG>(Object))
		{
			WeakSVGs.Add(SVG);
		}
	}

	Section.AddMenuEntry(
		TEXT("WidgetSVG_CreateSVGTexture"),
		NameTBD::Tooltips::SVG::CreateSVGTextureLabel(),
		NameTBD::Tooltips::SVG::CreateSVGTextureTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Texture2D")),
		FUIAction(FExecuteAction::CreateLambda([WeakSVGs]()
		{
			TArray<UWidgetSVG*> SVGs;
			for (const TWeakObjectPtr<UWidgetSVG>& WeakSVG : WeakSVGs)
			{
				if (UWidgetSVG* SVG = WeakSVG.Get())
				{
					SVGs.Add(SVG);
				}
			}
			UWidgetSVGFactory::CreateSVGTextures(SVGs);
		})));
}

// ---- UWidgetSVGThumbnailRenderer --------------------------------------------------------------------------------

void UWidgetSVGThumbnailRenderer::GetThumbnailSize(UObject* Object, float Zoom, uint32& OutWidth, uint32& OutHeight) const
{
	OutWidth = FMath::TruncToInt32(128 * Zoom);
	OutHeight = FMath::TruncToInt32(128 * Zoom);
}

void UWidgetSVGThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	const UWidgetSVG* SVG = Cast<UWidgetSVG>(Object);
	const double LongestSide = SVG ? FMath::Max(SVG->SVGSize.X, SVG->SVGSize.Y) : 0.0;
	if (!SVG || SVG->SVGSource.IsEmpty() || LongestSide <= 0.0 || Width == 0 || Height == 0)
	{
		return;
	}

	FCanvasTileItem BackgroundTile(FVector2D(X, Y), FVector2D(Width, Height), NameTBD::Colors::SVG::ThumbnailBackground);
	BackgroundTile.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(BackgroundTile);

	// The SVG drawn to fit the thumbnail, into a throwaway texture (thumbnails are drawn once and kept).
	const float FitScale = static_cast<float>(FMath::Min(Width, Height) / LongestSide);
	TArray<uint8> Pixels;
	FIntPoint PixelSize;
	if (!WidgetSVGRaster::Draw(SVG->SVGSource, SVG->Colors, SVG->bDrawInWhite, FitScale, static_cast<int32>(FMath::Max(Width, Height)), Pixels, PixelSize))
	{
		return;
	}
	UTexture2D* Preview = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Preview->SRGB = true;
	Preview->CompressionSettings = TC_EditorIcon;
	Preview->MipGenSettings = TMGS_NoMipmaps;
	Preview->NeverStream = true;
	Preview->Source.Init(PixelSize.X, PixelSize.Y, /*NumSlices*/ 1, /*NumMips*/ 1, TSF_BGRA8, Pixels.GetData());
	Preview->PostEditChange();
	const TArray<UTexture*> ToFinish = { Preview };
	FTextureCompilingManager::Get().FinishCompilation(ToFinish);
	if (!Preview->GetResource())
	{
		return;
	}

	const FVector2D TileSize(PixelSize.X, PixelSize.Y);
	const FVector2D TilePosition(X + (Width - TileSize.X) * 0.5, Y + (Height - TileSize.Y) * 0.5);
	FCanvasTileItem ImageTile(TilePosition, Preview->GetResource(), TileSize, FLinearColor::White);
	ImageTile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(ImageTile);
}

// ---- FWidgetSVGTextureDetails -----------------------------------------------------------------------------------

TSharedRef<IDetailCustomization> FWidgetSVGTextureDetails::MakeInstance()
{
	return MakeShared<FWidgetSVGTextureDetails>();
}

void FWidgetSVGTextureDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);
	if (CustomizedObjects.Num() != 1)
	{
		return;
	}
	UWidgetSVGTextureData* SVGData = UWidgetSVGTextureData::FindOn(Cast<UTexture>(CustomizedObjects[0].Get()));
	if (!SVGData)
	{
		return;
	}

	// The SVG data's own properties, edited on it (so its PostEditChangeProperty redraws the texture, and undo works).
	IDetailCategoryBuilder& SVGCategory = DetailBuilder.EditCategory(TEXT("SVG"), LOCTEXT("SVGCategory", "SVG"), ECategoryPriority::Important);
	const TArray<UObject*> SVGObjects = { SVGData };
	SVGCategory.AddExternalObjectProperty(SVGObjects, GET_MEMBER_NAME_CHECKED(UWidgetSVGTextureData, bDrawInWhite));
	SVGCategory.AddExternalObjectProperty(SVGObjects, GET_MEMBER_NAME_CHECKED(UWidgetSVGTextureData, Colors));
	SVGCategory.AddExternalObjectProperty(SVGObjects, GET_MEMBER_NAME_CHECKED(UWidgetSVGTextureData, ResolutionScale));
	SVGCategory.AddExternalObjectProperty(SVGObjects, GET_MEMBER_NAME_CHECKED(UWidgetSVGTextureData, MaxResolution));
	SVGCategory.AddExternalObjectProperty(SVGObjects, GET_MEMBER_NAME_CHECKED(UWidgetSVGTextureData, SVGSize));
}

#undef LOCTEXT_NAMESPACE
