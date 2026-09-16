#include "NameTBDThumbnailRenderer.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Texture2D.h"

void UNameTBDThumbnailRenderer::GetThumbnailSize(UObject* Object, float Zoom, uint32& OutWidth, uint32& OutHeight) const
{
	OutWidth = 128;
	OutHeight = 128;
}

void UNameTBDThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	if (!SharedIcon && !bHasAttemptedLoad)
	{
		bHasAttemptedLoad = true;
		SharedIcon = LoadObject<UTexture2D>(nullptr, TEXT("/NameTBD/NameTBD/Icons/Internal/smooth-shooting-star-large.smooth-shooting-star-large"));
        
		if (!SharedIcon)
		{
			UE_LOG(LogTemp, Warning, TEXT("NameTBDThumbnailRenderer: Failed to load custom icon."));
		}
	}

	if (SharedIcon)
	{
		// Draw the UE default grey background first
		FCanvasTileItem BackgroundTile(FVector2D(X, Y), FVector2D(Width, Height), FColor(30, 30, 30));
		BackgroundTile.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(BackgroundTile);

		// Draw the transparent icon on top
		FCanvasTileItem CanvasTile(FVector2D(X, Y), SharedIcon->GetResource(), FVector2D(Width, Height), FLinearColor::White);
		CanvasTile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(CanvasTile);
	}
}