// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"

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

	/**
	 * Every layer an edit made here is copied onto, when this one proxy stands in for a whole large selection - see
	 * SWidgetAnimTimeline::RebuildLayerProxies. Empty when it stands in for its own layer alone.
	 */
	TArray<int32> RepresentedLayers;

	/** The panel that made it, and is told of every edit. */
	TWeakPtr<SWidgetAnimTimeline> Panel;

	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
};

/**
 * An animation asset standing in for itself in the Selection panel: its own settings and Layer Groups, and of its tracks
 * only the one selected - at Layers[0].
 *
 * A details view makes a property node for every property of whatever it shows, checks them all again every frame, and
 * makes them all again on every refresh. Shown the real asset, an animation of hundreds of tracks gave the Selection panel
 * hundreds of thousands of them - walked every frame, made again on every click - for the one track it was showing. An
 * edit made here goes onto the real asset - the one property, at the real track's place - in the same undo step, and the
 * asset is told of it as a Details edit to it would tell it. See SWidgetAnimTimeline::OnShadowEdited.
 *
 * Transient: made with the panel, never saved, never offered anywhere.
 */
UCLASS(Transient, NotBlueprintable, HideDropdown)
class UWidgetAnimPresetSelectionShadow : public UWidgetAnimPresetAsset
{
	GENERATED_BODY()

public:

	/** The asset this stands in for. */
	TWeakObjectPtr<UWidgetAnimPresetAsset> ShadowOf;

	/** The real track shown at Layers[0], or INDEX_NONE when none is. */
	int32 LayerIndex = INDEX_NONE;

	/** The panel that made it, and is told of every edit. */
	TWeakPtr<SWidgetAnimTimeline> Panel;

	virtual void PreEditChange(FEditPropertyChain& PropertyAboutToChange) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
};