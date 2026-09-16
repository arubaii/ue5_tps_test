#pragma once
#include "CoreMinimal.h"
#include "ThumbnailRendering/ThumbnailRenderer.h"
#include "NameTBDThumbnailRenderer.generated.h"

class UTexture2D;

UCLASS()
class UNameTBDThumbnailRenderer : public UThumbnailRenderer
{
	GENERATED_BODY()

public:
	virtual void GetThumbnailSize(UObject* Object, float Zoom, uint32& OutWidth, uint32& OutHeight) const override;
	virtual void Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily) override;

	// Failsafe flag to prevent infinite load attempts
	bool bHasAttemptedLoad = false;
	
private:
	UPROPERTY()
	TObjectPtr<UTexture2D> SharedIcon = nullptr;
};