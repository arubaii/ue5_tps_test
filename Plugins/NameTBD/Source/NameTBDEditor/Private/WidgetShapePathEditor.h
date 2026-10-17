// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

#include "DesignerExtension.h"
#include "IHasDesignerExtensibility.h"
#include "WidgetReference.h"

class UObject;

/**
 * Editing a Custom Shape's outline on the UMG designer's canvas, the way a vector tool edits a path.
 *
 * A designer extension puts two things on the canvas for a selected Custom Shape: a toolbar above it (Edit Shape; while
 * editing, Done, the Select, Pen and Pencil tools, the selected vertices' kind - or Smooth / Corner on a Curve that works out
 * its own handles - and Corner Radius, Close / Open Path, Reverse, Delete), and - while editing - a layer over the whole
 * designer that takes the mouse and keyboard, so no other widget is picked (or the editing left) by a stray click. The
 * outline with its vertices and handles is drawn by the extension's Paint, over the designer's own selection outline. See
 * the class comment of SWidgetShapePathCanvas in the .cpp for every gesture.
 *
 * Edits go to the Widget Blueprint's own widget (the template, which is saved) and to the designer's preview of it (so they
 * show at once), each drag or key one undo step. The state of an edit - whether editing, which tool, which vertices are
 * selected - is kept here by the widget's name rather than in the canvas widgets, which the designer throws away and
 * builds again whenever it refreshes, so a refresh in the middle of editing goes unnoticed.
 */
namespace WidgetShapePathEditing
{
	enum class ETool : uint8
	{
		Select,
		Pen,
		Pencil
	};

	/** One vertex of a path: its contour, and its place in it. */
	struct FVertexId
	{
		int32 Contour = INDEX_NONE;
		int32 Vertex = INDEX_NONE;

		FVertexId() = default;
		FVertexId(int32 InContour, int32 InVertex) : Contour(InContour), Vertex(InVertex) {}

		bool IsValid() const { return Contour != INDEX_NONE && Vertex != INDEX_NONE; }
		bool operator==(const FVertexId& Other) const { return Contour == Other.Contour && Vertex == Other.Vertex; }
		bool operator!=(const FVertexId& Other) const { return !(*this == Other); }
		friend uint32 GetTypeHash(const FVertexId& Id) { return HashCombineFast(GetTypeHash(Id.Contour), GetTypeHash(Id.Vertex)); }
	};

	struct FState
	{
		bool bEditing = false;
		ETool Tool = ETool::Select;
		TArray<FVertexId> Selection;
	};

	/** The edit state of the shape widget named Name - in its Widget Blueprint's tree and in the designer's preview alike. */
	FState& GetState(FName Name);

	/** How closely the Pencil's line follows the hand, in local units (the toolbar's Fit): lower keeps more points. Shared by every shape. */
	float& PencilTolerance();

	bool IsEditing(const UObject* Shape);
	void SetEditing(const UObject* Shape, bool bEditing);

	/** Broadcast with the shape's name whenever an edit on the canvas finishes - so a Details panel showing it can refresh. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPathEdited, FName);
	FOnPathEdited& OnPathEdited();
}

/** Puts the path editor on the designer's canvas for a selected Custom Shape. */
class FWidgetShapePathEditorExtension : public FDesignerExtension
{
public:
	FWidgetShapePathEditorExtension();

	// FDesignerExtension
	virtual bool CanExtendSelection(const TArray<FWidgetReference>& Selection) const override;
	virtual void ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements) override;

	/** Draws the outline being edited over the designer - after its preview and its own selection outline. */
	virtual void Paint(const TSet<FWidgetReference>& Selection, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId) const override;

private:
	/** The editing layer made for the current selection (an SWidgetShapePathCanvas). */
	TWeakPtr<SWidget> ActiveCanvas;
};

/** Hands the designer a fresh path editor extension per designer opened. */
class FWidgetShapePathEditorFactory : public IDesignerExtensionFactory, public TSharedFromThis<FWidgetShapePathEditorFactory>
{
public:
	virtual ~FWidgetShapePathEditorFactory() = default;

	virtual TSharedRef<FDesignerExtension> CreateDesignerExtension() const override;
};
