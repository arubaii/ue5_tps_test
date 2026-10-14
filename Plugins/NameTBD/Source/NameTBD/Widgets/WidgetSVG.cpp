// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetSVG.h"

#include <atomic>

#include "WidgetSVGRaster.h"
#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

namespace WidgetSVGPrivate
{
	/** Shared by every SVG, so a revision never repeats (an SVG's drawings are found by its revision). */
	static std::atomic<uint32> NextRevision{ 1 };
}

void UWidgetSVG::BumpRevision()
{
	Revision = WidgetSVGPrivate::NextRevision.fetch_add(1);
}

#if WITH_EDITORONLY_DATA
void UWidgetSVG::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
	Super::PostInitProperties();
}
#endif

#if WITH_EDITOR

bool UWidgetSVG::SetSVGSource(const FString& InSVGText, FText& OutError)
{
	FVector2D ReadSize;
	TArray<FColor> FoundColors;
	if (!WidgetSVGRaster::Read(InSVGText, ReadSize, FoundColors, &OutError))
	{
		return false;
	}
	SVGSource = InSVGText;
	SVGSize = ReadSize;
	Colors = WidgetSVGRaster::MergeColors(FoundColors, Colors);
	BumpRevision();
	return true;
}

void UWidgetSVG::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Any change (Colors, Draw In White) redraws: widgets showing it draw it again on their next paint.
	BumpRevision();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UWidgetSVG::PostEditUndo()
{
	BumpRevision();
	Super::PostEditUndo();
}

#endif // WITH_EDITOR
