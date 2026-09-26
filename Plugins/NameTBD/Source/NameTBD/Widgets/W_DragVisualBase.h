// Copyright AR Game Systems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "W_DragVisualBase.generated.h"

UCLASS()
class NAMETBD_API UW_DragVisualBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Drag Visual")
	void SetDragVisualPosition(FVector2D IconSize);

	// Call this right before you add the visual to the viewport to prevent the widget from flying in from 0,0
	UFUNCTION(BlueprintCallable, Category = "Drag Visual")
	void InitializeDragPosition(FVector2D IconSize);

protected:
	// State variables for physics calculation
	FVector2D CurrentTranslation;
	FVector2D CurrentVelocity;
	bool bHasInitializedPosition = false;
};