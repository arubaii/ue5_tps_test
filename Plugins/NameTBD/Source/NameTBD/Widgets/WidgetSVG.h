// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "WidgetSVG.generated.h"

/**
 * Where a plugin widget's image comes from. Texture 2D (the default everywhere) is the brush as before: a texture or a
 * material. SVG draws an SVG asset instead, from its vectors, at the size it is shown on screen - sharp at any size.
 */
UENUM(BlueprintType)
enum class EWidgetImageSource : uint8
{
	Texture UMETA(DisplayName = "Texture 2D", ToolTip = "The brush: a texture or a material, as before."),

	SVG UMETA(DisplayName = "SVG", ToolTip = "An SVG asset, drawn from its vectors at the size it is shown on screen (redrawn when that size changes), so it is sharp at any size. The brush's Tint, Draw As and Margin still apply.")
};

/** One colour used in an SVG (in a fill, a stroke or a gradient stop), and the colour to draw it in. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetSVGColor
{
	GENERATED_BODY()

	/** The colour as the SVG writes it. Opacity is not part of it: the SVG's own opacity is always kept. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SVG")
	FColor Original = FColor::Black;

	/** What to draw it in. Its alpha multiplies the SVG's own opacity (255 keeps it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SVG")
	FColor DrawAs = FColor::Black;
};

/**
 * An SVG file, imported (drag a .svg into the Content Browser). Drawn from its vectors at the size it is shown, so it is
 * sharp at any size: by the SVG Image widget, and by every plugin widget with an image (a shape's Image paint, a Flow
 * List scroll bar's knob and dots, a Media Box's Error Image, Niagara Particles' Particle Image) when its Image Source is
 * SVG.
 *
 * For engine widgets (Image, Button styles...), which take textures: right-click > Create SVG Texture makes a Texture 2D
 * drawn from it at a fixed resolution.
 *
 * Colours: every colour the SVG uses is listed under Colors and can be changed in place. Draw In White draws every colour
 * as white instead (keeping opacity), so a widget's tint colours it - the fix for icons that come in black.
 */
UCLASS(BlueprintType, hidecategories = Object, meta = (DisplayName = "SVG"))
class NAMETBD_API UWidgetSVG : public UObject
{
	GENERATED_BODY()

public:

	/** The size the SVG says it is, in pixels (its width / height, else its viewBox). An SVG Image's size by default. */
	UPROPERTY(VisibleAnywhere, Category = "SVG", meta = (DisplayName = "SVG Size"))
	FVector2D SVGSize = FVector2D::ZeroVector;

	/**
	 * Draw every colour as white, keeping its opacity, so the widget's tint (an SVG Image's Color And Opacity, a brush's
	 * Tint, a shape paint's Color) gives the image its colour. Colors below is ignored while this is on.
	 */
	UPROPERTY(EditAnywhere, Category = "SVG", meta = (DisplayName = "Draw In White (Tint With Widget)"))
	bool bDrawInWhite = false;

	/**
	 * Every colour the SVG uses, in the order it first uses them. Change Draw As to recolour every fill, stroke and
	 * gradient stop of that colour. An SVG that gives a shape no fill draws it black: that is the black entry here.
	 */
	UPROPERTY(EditAnywhere, EditFixedSize, Category = "SVG", meta = (EditCondition = "!bDrawInWhite"))
	TArray<FWidgetSVGColor> Colors;

	/** The SVG file's text, as last imported. Cooked: the game draws from it. */
	UPROPERTY()
	FString SVGSource;

#if WITH_EDITORONLY_DATA
	/** Where the SVG was imported from, for Reimport. */
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Import Settings")
	TObjectPtr<class UAssetImportData> AssetImportData;
#endif

	/** The size the SVG says it is, in pixels. */
	UFUNCTION(BlueprintPure, Category = "SVG")
	FVector2D GetSVGSize() const { return SVGSize; }

	/** Changes whenever what the SVG draws changes (an edit, a reimport): drawings made before are then out of date. */
	uint32 GetRevision() const { return Revision; }

#if WITH_EDITOR
	/**
	 * Takes new SVG text (import, reimport), keeping the Draw As of every colour still in it. On failure nothing changes
	 * and OutError says why.
	 */
	bool SetSVGSource(const FString& InSVGText, FText& OutError);

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
#endif

#if WITH_EDITORONLY_DATA
	virtual void PostInitProperties() override;
#endif

private:

	/** Bumped on every change (never saved: drawings are not either). */
	uint32 Revision = 0;

	void BumpRevision();
};
