// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "DesignerExtension.h"
#include "IHasDesignerExtensibility.h"
#include "WidgetReference.h"

/**
 * Editing a Spline path on the UMG designer's canvas, as a Custom Shape's outline is edited (see WidgetShapePathEditor.h) -
 * instead of typing Spline Points into the Details panel. The same editor serves a Flow List (Layout > Type Spline) and a
 * Flow List Scroll Bar (Path Spline).
 *
 * For a selected one with a Spline path, a designer extension puts a toolbar above it (Edit Path; while editing, Done, the
 * Select and Pen tools, Reverse, Delete) and - while editing - a layer over it that draws the curve, its points, and for a
 * list the focus marker, and takes the mouse and keyboard. See the class comment of SFlowListPathCanvas in the .cpp for
 * every gesture. A selected Flow List's toolbar also always has Enter and Exit, which play its items' entrances and exits
 * in the preview.
 *
 * Edits go to the Widget Blueprint's own Flow List (the template, which is saved) and to the designer's preview of it, so
 * the items move along the new curve at once; each drag or key is one undo step. Whether it is editing, which tool and
 * which points are selected are kept here by the list's name, so the designer rebuilding its canvas widgets mid-edit goes
 * unnoticed.
 */
namespace FlowListPathEditing
{
	enum class ETool : uint8
	{
		Select,
		Pen
	};

	struct FState
	{
		bool bEditing = false;
		ETool Tool = ETool::Select;
		TArray<int32> Selection;
	};

	/** The edit state of the Flow List named Name - in its Widget Blueprint's tree and in the designer's preview alike. */
	FState& GetState(FName Name);

	void SetEditing(const UObject* List, bool bEditing);
}

/** Puts the path editor (and a list's Enter / Exit previews) on the designer's canvas for a selected Flow List or Flow List Scroll Bar. */
class FFlowListPathEditorExtension : public FDesignerExtension
{
public:
	FFlowListPathEditorExtension();

	// FDesignerExtension
	virtual bool CanExtendSelection(const TArray<FWidgetReference>& Selection) const override;
	virtual void ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements) override;
};

/** Hands the designer a fresh Flow List path editor extension per designer opened. */
class FFlowListPathEditorFactory : public IDesignerExtensionFactory, public TSharedFromThis<FFlowListPathEditorFactory>
{
public:
	virtual ~FFlowListPathEditorFactory() = default;

	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override;
};
