// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "DesignerExtension.h"
#include "IHasDesignerExtensibility.h"
#include "WidgetReference.h"

class UWidgetAnimBuilder;

/**
 * Play / Pause and Restart, drawn on the UMG design surface above the widget being previewed.
 *
 * A designer extension is the only sanctioned way onto that surface: the designer asks every registered
 * extension whether it wants the current selection, and the ones that do hand back Slate widgets that it
 * positions relative to the selected widget's bounds. The toolbar row along the top of the viewport -
 * Zoom, Screen Size, Desired - is built inside SDesignerView and has no extension point.
 *
 * Shown for a Widget Anim Builder placed in another Widget Blueprint, and for anything inside the builder's
 * own Widget Blueprint - selecting PreviewImage there is what puts the controls on screen. Selecting the
 * root row in the Hierarchy is not a widget selection and shows nothing.
 *
 * Nothing about the preview instance is cached. FWidgetReference::GetPreview is documented as a transient
 * thing constantly destroyed and recreated, and a builder running a design-time animation is rebuilt more
 * than most, so the builder is looked up again every time it is needed. The reference itself is what
 * survives; it finds the current preview by name.
 */
class FWidgetAnimPreviewControls : public FDesignerExtension
{
public:

	FWidgetAnimPreviewControls();

	// FDesignerExtension
	virtual bool CanExtendSelection(const TArray<FWidgetReference>& Selection) const override;
	virtual void ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements) override;

private:

	/** The builder a selected widget belongs to: the widget itself when placed, or its owner when it is PreviewImage. */
	static UWidgetAnimBuilder* ResolveBuilder(const FWidgetReference& Reference);

	/** The builder as it stands right now. Never stored - see the class comment. */
	UWidgetAnimBuilder* GetBuilder() const;

	FReply OnPlayPauseClicked();
	FReply OnRestartClicked();

	/** Bound as attributes rather than set once, so the button follows the preview without anything pushing at it. */
	FText GetPlayPauseLabel() const;
	FText GetPlayPauseTooltip() const;
	EVisibility GetControlsVisibility() const;

	/** The selection this row was built for. A reference, not a pointer: it outlives the widget it names. */
	FWidgetReference ExtendedWidget;
};

/** Hands the designer a fresh extension per designer opened, which is what the extensibility manager wants. */
class FWidgetAnimPreviewControlsFactory : public IDesignerExtensionFactory, public TSharedFromThis<FWidgetAnimPreviewControlsFactory>
{
public:

	virtual ~FWidgetAnimPreviewControlsFactory() = default;

	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override;
};