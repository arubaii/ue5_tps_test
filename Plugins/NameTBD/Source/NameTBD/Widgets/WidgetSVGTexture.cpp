// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetSVGTexture.h"

#include "Engine/Texture2D.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "WidgetSVGRaster.h"

#define LOCTEXT_NAMESPACE "WidgetSVGTexture"

#if WITH_EDITOR

UWidgetSVGTextureData* UWidgetSVGTextureData::FindOn(const UTexture* InTexture)
{
	// Through the interface (a virtual call): UTexture's own functions are not all exported to plugins.
	IInterface_AssetUserData* UserDataOwner = Cast<IInterface_AssetUserData>(const_cast<UTexture*>(InTexture));
	return UserDataOwner ? Cast<UWidgetSVGTextureData>(UserDataOwner->GetAssetUserDataOfClass(UWidgetSVGTextureData::StaticClass())) : nullptr;
}

void UWidgetSVGTextureData::ApplyTextureSettings(UTexture2D* InTexture)
{
	// What a UI texture wants: uncompressed (UserInterface2D), sRGB, never streamed (no blurry first frames), and mips,
	// so it stays sharp when a widget draws it smaller than it was drawn.
	InTexture->SRGB = true;
	InTexture->CompressionSettings = TC_EditorIcon;
	InTexture->LODGroup = TEXTUREGROUP_UI;
	InTexture->NeverStream = true;
	InTexture->MipGenSettings = TMGS_SimpleAverage;
}

bool UWidgetSVGTextureData::SetSVGSource(const FString& InSVGText, FText& OutError)
{
	const FString PreviousSource = SVGSource;
	SVGSource = InSVGText;
	if (!RasterizeSVG(&OutError))
	{
		SVGSource = PreviousSource;
		return false;
	}
	return true;
}

bool UWidgetSVGTextureData::RasterizeSVG(FText* OutError)
{
	UTexture2D* OwnerTexture = Cast<UTexture2D>(GetOuter());
	if (!OwnerTexture)
	{
		if (OutError)
		{
			*OutError = LOCTEXT("NoTexture", "The SVG data is not on a Texture 2D.");
		}
		return false;
	}

	// Colors: the SVG's colours in order of first use, each keeping the Draw As it had (a new colour draws as itself).
	FVector2D ReadSize;
	TArray<FColor> FoundColors;
	if (!WidgetSVGRaster::Read(SVGSource, ReadSize, FoundColors, OutError))
	{
		return false;
	}
	TArray<FWidgetSVGColor> MergedColors = WidgetSVGRaster::MergeColors(FoundColors, Colors);
	bool bColorsChanged = MergedColors.Num() != Colors.Num();
	for (int32 EntryIndex = 0; !bColorsChanged && EntryIndex < MergedColors.Num(); ++EntryIndex)
	{
		bColorsChanged = MergedColors[EntryIndex].Original != Colors[EntryIndex].Original || MergedColors[EntryIndex].DrawAs != Colors[EntryIndex].DrawAs;
	}
	if (bColorsChanged)
	{
		Colors = MoveTemp(MergedColors);
	}
	SVGSize = ReadSize;

	TArray<uint8> Pixels;
	FIntPoint PixelSize;
	if (!WidgetSVGRaster::Draw(SVGSource, Colors, bDrawInWhite, ResolutionScale, MaxResolution, Pixels, PixelSize, OutError))
	{
		return false;
	}

	// Waits for a build of the texture still running, then replaces its pixels and rebuilds it.
	OwnerTexture->PreEditChange(nullptr);
	OwnerTexture->Source.Init(PixelSize.X, PixelSize.Y, /*NumSlices*/ 1, /*NumMips*/ 1, TSF_BGRA8, Pixels.GetData());
	OwnerTexture->Source.UseHashAsGuid();
	OwnerTexture->PostEditChange();
	return true;
}

void UWidgetSVGTextureData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName ChangedMember = PropertyChangedEvent.GetMemberPropertyName();
	const bool bSVGSetting = ChangedMember == GET_MEMBER_NAME_CHECKED(UWidgetSVGTextureData, ResolutionScale)
		|| ChangedMember == GET_MEMBER_NAME_CHECKED(UWidgetSVGTextureData, MaxResolution)
		|| ChangedMember == GET_MEMBER_NAME_CHECKED(UWidgetSVGTextureData, bDrawInWhite)
		|| ChangedMember == GET_MEMBER_NAME_CHECKED(UWidgetSVGTextureData, Colors);

	// Not while a colour picker or a spin box is being dragged: each redraw rebuilds the texture.
	if (bSVGSetting && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		RasterizeSVG();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UWidgetSVGTextureData::PostEditUndo()
{
	// Undo restores the settings and colours; the texture is drawn again from them.
	if (!SVGSource.IsEmpty())
	{
		RasterizeSVG();
	}
	Super::PostEditUndo();
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
