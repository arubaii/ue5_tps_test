// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "DesignerExtension.h"
#include "IHasDesignerExtensibility.h"
#include "WidgetReference.h"

/**
 * Connecting widgets with a Widget Path Layer from the UMG designer's canvas: select two or more widgets in a Widget
 * Blueprint that has a Widget Path Layer, and a toolbar over them offers Connect (adds a connection between each widget and
 * the next, ordered along the way they are spread - left to right or top to bottom - so selecting a row of skill nodes and
 * clicking Connect chains them) and Disconnect (removes the connections among them). One undo step each. Edits go to the
 * Widget Blueprint's layer (saved) and to the designer's preview of it, so the lines appear at once.
 */
class FWidgetPathConnectExtension : public FDesignerExtension
{
public:
	FWidgetPathConnectExtension();

	// FDesignerExtension
	virtual bool CanExtendSelection(const TArray<FWidgetReference>& Selection) const override;
	virtual void ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements) override;
};

/** Hands the designer a fresh Widget Path connect extension per designer opened. */
class FWidgetPathConnectFactory : public IDesignerExtensionFactory, public TSharedFromThis<FWidgetPathConnectFactory>
{
public:
	virtual ~FWidgetPathConnectFactory() = default;

	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override;
};
