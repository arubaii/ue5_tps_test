// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "WidgetSVG.h"

#include "WidgetSVGTexture.generated.h"

class UTexture;
class UTexture2D;

/**
 * The SVG behind an SVG Texture: an ordinary Texture 2D drawn from an SVG at a fixed resolution, for engine widgets that
 * take textures (Image, Button styles...: picked as a Brush's Image, Set Brush From Texture, like any image). Made from an
 * SVG asset (right-click > Create SVG Texture), which puts one of these on the texture as Asset User Data. The texture's
 * Details (the Texture Editor) show its settings at the top, in an SVG category.
 *
 * The SVG is drawn into the texture at Resolution Scale x its own size (held to Max Resolution), with mips, so it stays
 * sharp in widgets drawn at or below that size. Editing anything here redraws it from the SVG kept in this object;
 * Reimport reads the file again and keeps the colours you set.
 *
 * Colours: every colour the SVG uses is listed under Colors and can be changed in place. Draw In White draws every colour
 * as white instead (keeping opacity), so the Brush's Tint colours the image - the fix for icons that come in black.
 *
 * Editor-only: it is stripped when cooking, so a game has only the texture.
 */
UCLASS(meta = (DisplayName = "SVG Source"))
class NAMETBD_API UWidgetSVGTextureData : public UAssetUserData
{
	GENERATED_BODY()

public:

#if WITH_EDITORONLY_DATA
	/** The size the SVG says it is, in pixels (its width / height, else its viewBox). */
	UPROPERTY(VisibleAnywhere, Category = "SVG", meta = (DisplayName = "SVG Size"))
	FVector2D SVGSize = FVector2D::ZeroVector;

	/**
	 * How many texture pixels per SVG pixel. The texture has mips, so it is sharp anywhere up to this many times the
	 * SVG's own size (a 24 px icon at 4 is a 96 px texture: sharp at 24, at 48 on a 2x screen, at 96).
	 */
	UPROPERTY(EditAnywhere, Category = "SVG", meta = (ClampMin = "0.1", UIMin = "1", UIMax = "8"))
	float ResolutionScale = 4.f;

	/** The texture's longest side is held to this, whatever Resolution Scale asks for. */
	UPROPERTY(EditAnywhere, Category = "SVG", meta = (ClampMin = "16", ClampMax = "8192"))
	int32 MaxResolution = 2048;

	/**
	 * Draw every colour as white, keeping its opacity, so the Brush's Tint (or Color and Opacity) gives the image its
	 * colour. Colors below is ignored while this is on.
	 */
	UPROPERTY(EditAnywhere, Category = "SVG", meta = (DisplayName = "Draw In White (Tint With Brush)"))
	bool bDrawInWhite = false;

	/**
	 * Every colour the SVG uses, in the order it first uses them. Change Draw As to recolour every fill, stroke and
	 * gradient stop of that colour. An SVG that gives a shape no fill draws it black: that is the black entry here.
	 */
	UPROPERTY(EditAnywhere, EditFixedSize, Category = "SVG", meta = (EditCondition = "!bDrawInWhite"))
	TArray<FWidgetSVGColor> Colors;

	/** The SVG file's text, as last imported. */
	UPROPERTY()
	FString SVGSource;
#endif

	// Stripped when cooking: only the texture is needed in a game.
	virtual bool IsEditorOnly() const override { return true; }

#if WITH_EDITOR
	/** The SVG data on a texture, or null if it wasn't imported from an SVG. */
	static UWidgetSVGTextureData* FindOn(const UTexture* InTexture);

	/** Sets a texture up as a UI texture drawn from an SVG (uncompressed, sRGB, UI group, never streamed, mips). */
	static void ApplyTextureSettings(UTexture2D* InTexture);

	/**
	 * Takes new SVG text (import, reimport) and draws it into the texture this is on, keeping the Draw As of every colour
	 * still in it. On failure nothing changes and OutError says why.
	 */
	bool SetSVGSource(const FString& InSVGText, FText& OutError);

	/**
	 * Draws SVGSource with the current settings into the texture this is on (its outer), refreshing Colors, and rebuilds
	 * the texture. False (and the texture untouched) if the SVG can't be read.
	 */
	bool RasterizeSVG(FText* OutError = nullptr);

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif
};
