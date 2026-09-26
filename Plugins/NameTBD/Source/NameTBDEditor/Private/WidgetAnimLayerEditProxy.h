// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"

#include "WidgetAnimLayerEditProxy.generated.h"

class SWidgetAnimTimeline;

/**
 * One selected layer, standing in for itself in the Selection panel while several layers are selected.
 *
 * The layers are elements of one array on one object, and a details view can only merge the same property
 * across several OBJECTS - that is what shows a shared value once and "Multiple Values" where they differ. So
 * each selected layer gets a proxy holding a copy of it, the Selection panel shows the proxies together, and an
 * edit made there is copied back onto the real layer - the one property that changed, nothing else - inside
 * the same undo step. See SWidgetAnimTimeline::OnProxyEdited.
 *
 * Transient: made when a multi-selection is, thrown away with it, never saved.
 */
UCLASS(Transient)
class UWidgetAnimLayerEditProxy : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Layer", meta = (ShowOnlyInnerProperties))
	FWidgetAnimLayer Layer;

	/** Which layer of the edited animation this stands in for. */
	int32 LayerIndex = INDEX_NONE;

	/** The panel that made it, and is told of every edit. */
	TWeakPtr<SWidgetAnimTimeline> Panel;

	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
};