// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

#include "NiagaraUIMesh.h"

class UNiagaraUIWidget;

/**
 * Draws a Niagara Particles widget. Reads the UMG widget live as it paints (as the shape widgets do): asks it to place its
 * simulations where this paint puts the widget, then builds the particle meshes - only when the particles, the widget's
 * place or its look changed - and draws one custom-vertex element per batch. Draws nothing and costs nothing beyond that
 * check when no simulation is live.
 */
class SNiagaraUIWidget : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraUIWidget) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UNiagaraUIWidget>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	TWeakObjectPtr<UNiagaraUIWidget> OwnerWidget;

	mutable FNiagaraUIMesh Mesh;
	mutable TArray<FNiagaraUISource> Sources;
};
