// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetShapePathEditor.h"

#include "NameTBDColors.h"
#include "NameTBDStyle.h"
#include "NameTBDTooltips.h"
#include "WidgetShapeEditorUI.h"
#include "NameTBD/Widgets/Shapes/ShapeWidgets.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/ICursor.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Styling/CoreStyle.h"
#include "WidgetBlueprint.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetShapePathEditor"

// =====================================================================================================
// Edit state
// =====================================================================================================

namespace WidgetShapePathEditing
{
	FState& GetState(FName Name)
	{
		static TMap<FName, FState> States;
		return States.FindOrAdd(Name);
	}

	bool IsEditing(const UObject* Shape)
	{
		return Shape && GetState(Shape->GetFName()).bEditing;
	}

	void SetEditing(const UObject* Shape, bool bEditing)
	{
		if (!Shape)
		{
			return;
		}
		FState& State = GetState(Shape->GetFName());
		State.bEditing = bEditing;
		if (!bEditing)
		{
			State.Tool = ETool::Select;
			State.Selection.Reset();
		}
	}

	FOnPathEdited& OnPathEdited()
	{
		static FOnPathEdited Delegate;
		return Delegate;
	}
}

// =====================================================================================================
// The canvas layer
// =====================================================================================================

namespace WidgetShapePathEditorImpl
{
	namespace Palette = NameTBD::Colors::ShapeEditor;
	namespace Tips = NameTBD::Tooltips::ShapeEditor;
	using WidgetShapePathEditing::ETool;
	using WidgetShapePathEditing::FState;
	using WidgetShapePathEditing::FVertexId;

	/** How far the layer reaches past the widget on every side, in canvas units: room to drag vertices and handles outside it. */
	constexpr float OverlayMargin = 160.f;

	/** How far above the widget the toolbar's bottom edge sits. */
	constexpr float ToolbarLift = 6.f;

	/** How far the mouse must move, in canvas units, before a press becomes a drag. */
	constexpr float DragThreshold = 3.f;

	/** How close, in canvas units, a dragged vertex must come to another's line to snap to it. */
	constexpr float SnapDistance = 6.f;

	/** Points each curved segment is drawn and hit tested with on the canvas. */
	constexpr int32 CurveSamples = 24;

	FVector2f ToF(const FVector2D& V) { return FVector2f(static_cast<float>(V.X), static_cast<float>(V.Y)); }
	FVector2D ToD(const FVector2f& V) { return FVector2D(V.X, V.Y); }

	const FSlateBrush* CircleBrush()
	{
		// Rounded at half its height: a circle in a square box.
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White);
		return &Brush;
	}

	const FSlateBrush* SquareBrush()
	{
		static const FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	const FSlateBrush* ToolbarBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Palette::ToolbarBackground, 5.f, Palette::ToolbarOutline, 1.f);
		return &Brush;
	}

	/** Snaps a vector's direction to the nearest 45 degrees, measured in local pixels (box units are stretched by the widget). */
	FVector2D SnapTo45(const FVector2D& BoxVector, const FVector2D& PixelsPerBox)
	{
		const FVector2D Pixels = BoxVector * PixelsPerBox;
		const double Length = Pixels.Size();
		if (Length <= 1e-9)
		{
			return BoxVector;
		}
		const double Angle = FMath::RoundToDouble(FMath::Atan2(Pixels.Y, Pixels.X) / (UE_DOUBLE_PI / 4.0)) * (UE_DOUBLE_PI / 4.0);
		const FVector2D Snapped(FMath::Cos(Angle) * Length, FMath::Sin(Angle) * Length);
		return FVector2D(PixelsPerBox.X > 0.0 ? Snapped.X / PixelsPerBox.X : 0.0, PixelsPerBox.Y > 0.0 ? Snapped.Y / PixelsPerBox.Y : 0.0);
	}

	/**
	 * Handles that make a vertex smooth where it had none: along the line between its neighbors, a third of the way to each
	 * (as the vector tools do when a corner is turned into a curve). Worked out in local pixels so a stretched widget still
	 * gets a smooth curve.
	 */
	void MakeSmoothHandles(const FWidgetShapeContour& Contour, int32 Index, const FVector2D& PixelsPerBox, FVector2D& OutIn, FVector2D& OutOut)
	{
		OutIn = FVector2D::ZeroVector;
		OutOut = FVector2D::ZeroVector;
		const int32 Count = Contour.Vertices.Num();
		const FVector2D Point = Contour.Vertices[Index].Position * PixelsPerBox;
		const bool bHasPrevious = Contour.bClosed || Index > 0;
		const bool bHasNext = Contour.bClosed || Index < Count - 1;
		if (Count < 2 || (!bHasPrevious && !bHasNext))
		{
			return;
		}
		const FVector2D Previous = bHasPrevious ? Contour.Vertices[(Index + Count - 1) % Count].Position * PixelsPerBox : Point;
		const FVector2D Next = bHasNext ? Contour.Vertices[(Index + 1) % Count].Position * PixelsPerBox : Point;
		FVector2D Direction = (Next - Previous).GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return;
		}
		const double LengthIn = bHasPrevious ? (Point - Previous).Size() / 3.0 : (Next - Point).Size() / 3.0;
		const double LengthOut = bHasNext ? (Next - Point).Size() / 3.0 : LengthIn;
		auto ToBox = [&](const FVector2D& Pixels) { return FVector2D(PixelsPerBox.X > 0.0 ? Pixels.X / PixelsPerBox.X : 0.0, PixelsPerBox.Y > 0.0 ? Pixels.Y / PixelsPerBox.Y : 0.0); };
		OutOut = ToBox(Direction * LengthOut);
		OutIn = ToBox(-Direction * LengthIn);
	}

	/** Gives a vertex a kind, making up handles for one that had none and squaring them up for Mirrored and Asymmetric. */
	void SetVertexType(FWidgetShapeContour& Contour, int32 Index, EWidgetShapeVertexType Type, const FVector2D& PixelsPerBox)
	{
		FWidgetShapeVertex& Vertex = Contour.Vertices[Index];
		if (Type == EWidgetShapeVertexType::Straight)
		{
			Vertex.Type = EWidgetShapeVertexType::Straight;
			Vertex.InHandle = FVector2D::ZeroVector;
			Vertex.OutHandle = FVector2D::ZeroVector;
			return;
		}

		if (!Vertex.HasHandles() || (Vertex.InHandle.IsNearlyZero(1e-9) && Vertex.OutHandle.IsNearlyZero(1e-9)))
		{
			MakeSmoothHandles(Contour, Index, PixelsPerBox, Vertex.InHandle, Vertex.OutHandle);
		}
		Vertex.Type = Type;

		const FVector2D InPixels = Vertex.InHandle * PixelsPerBox;
		const FVector2D OutPixels = Vertex.OutHandle * PixelsPerBox;
		const FVector2D Direction = (OutPixels - InPixels).GetSafeNormal();
		if (Direction.IsNearlyZero() || Type == EWidgetShapeVertexType::Disconnected)
		{
			return;
		}
		auto ToBox = [&](const FVector2D& Pixels) { return FVector2D(PixelsPerBox.X > 0.0 ? Pixels.X / PixelsPerBox.X : 0.0, PixelsPerBox.Y > 0.0 ? Pixels.Y / PixelsPerBox.Y : 0.0); };
		if (Type == EWidgetShapeVertexType::Mirrored)
		{
			const double Length = (InPixels.Size() + OutPixels.Size()) * 0.5;
			Vertex.OutHandle = ToBox(Direction * Length);
			Vertex.InHandle = ToBox(-Direction * Length);
		}
		else
		{
			Vertex.OutHandle = ToBox(Direction * OutPixels.Size());
			Vertex.InHandle = ToBox(-Direction * InPixels.Size());
		}
	}

	enum class EHit : uint8
	{
		None,
		Vertex,
		InHandle,
		OutHandle,
		Segment
	};

	struct FHit
	{
		EHit Kind = EHit::None;
		FVertexId Id;
		float T = 0.f;
	};

	enum class EDrag : uint8
	{
		None,
		Vertices,
		Handle,
		PullHandles,
		Marquee
	};

	/**
	 * The layer over a Custom Shape being edited on the designer's canvas. It draws the outline, its vertices (squares for
	 * Straight, circles for the curved kinds) and the selected vertices' handles, and takes the mouse and keyboard:
	 *
	 *   Select tool (V)
	 *     click a vertex, an edge (its two vertices) or a handle; Shift adds or removes; drag on empty space to box-select
	 *     drag to move what is selected - snapping to other vertices' lines and the widget's edges and middle (hold Ctrl to
	 *       turn snapping off); Shift keeps the move straight or at 45 degrees
	 *     drag a handle to bend its curve; Alt breaks it from its partner (Disconnected); Shift keeps it to 45 degrees;
	 *       Ctrl-click a handle to remove it
	 *     Ctrl-drag a vertex to pull handles out of it, as the vector tools' bend does
	 *     double-click a vertex to switch it between Straight and Mirrored; double-click an edge to add a vertex on it,
	 *       without changing the curve; double-click empty space to finish
	 *   Pen tool (P)
	 *     click to add a vertex after the selected end of an open contour, or to start a new contour; drag as you click to
	 *       pull out its handles; Shift keeps the new edge to 45 degrees
	 *     click the contour's other end to close it; click an edge to add a vertex on it; click a vertex to select it
	 *   Keys: Delete / Backspace removes the selected vertices, arrows nudge them (Shift: 10), Ctrl+A selects all,
	 *     Escape lets go of the selection then finishes, Enter finishes.
	 *
	 * Every press that changes the path is one undo step; the change goes to the Widget Blueprint's widget and the
	 * designer's preview at once.
	 */
	class SWidgetShapePathCanvas : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetShapePathCanvas) {}
			SLATE_ARGUMENT(FWidgetReference, Widget)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Reference = InArgs._Widget;
			if (const UWidget* Template = Reference.GetTemplate())
			{
				ShapeName = Template->GetFName();
			}
			SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() { return IsEditing() ? EVisibility::Visible : EVisibility::Collapsed; }));
		}

		virtual ~SWidgetShapePathCanvas() override
		{
			Transaction.Reset();
		}

		/** The toolbar the designer shows above the shape: Edit Shape, or the tools while editing. */
		TSharedRef<SWidget> MakeToolbar();

		// SWidget
		virtual FVector2D ComputeDesiredSize(float) const override
		{
			// Worked out here rather than in Tick: a layer of no size may never be painted, and so never ticked, to grow.
			return IsEditing() ? GetCanvasSize() : FVector2D::ZeroVector;
		}

		virtual bool SupportsKeyboardFocus() const override { return true; }

		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
			UpdateTarget();
			const FVector2D Desired = GetCanvasSize();
			if (!Desired.Equals(LastCanvasSize, 0.5))
			{
				LastCanvasSize = Desired;
				Invalidate(EInvalidateWidgetReason::Layout);
			}
		}

		/** As large as the widget plus the margin all round, in canvas units, so the layer covers it wherever the designer zooms. */
		FVector2D GetCanvasSize() const
		{
			const UCustomShape* Preview = GetPreview();
			const TSharedPtr<SWidget> SlateWidget = Preview ? Preview->GetCachedWidget() : nullptr;
			const FVector2D Margin(OverlayMargin * 2.f, OverlayMargin * 2.f);
			if (!SlateWidget.IsValid())
			{
				return Margin;
			}
			const FGeometry& Target = SlateWidget->GetTickSpaceGeometry();
			const FVector2D Size = Target.GetLocalSize();
			FBox2D Corners(ForceInit);
			for (const FVector2D& Corner : { FVector2D(0.0, 0.0), FVector2D(Size.X, 0.0), FVector2D(0.0, Size.Y), Size })
			{
				Corners += Target.LocalToAbsolute(Corner);
			}
			const float OwnScale = GetTickSpaceGeometry().GetAccumulatedLayoutTransform().GetScale();
			const float CanvasScale = OwnScale > 0.01f ? OwnScale : 1.f;
			return Corners.GetSize() / CanvasScale + Margin;
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
		virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
		virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
		virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
		virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
		virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override { bMouseInside = false; Hover = FHit(); }
		virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
		virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	private:
		// ---------------------------------------------------------------------------------------------
		// The shape
		// ---------------------------------------------------------------------------------------------

		UCustomShape* GetPreview() const { return Cast<UCustomShape>(Reference.GetPreview()); }
		UCustomShape* GetTemplate() const { return Cast<UCustomShape>(Reference.GetTemplate()); }
		FState& State() const { return WidgetShapePathEditing::GetState(ShapeName); }
		bool IsEditing() const { return !ShapeName.IsNone() && State().bEditing; }

		const FWidgetShapePath* GetPath() const
		{
			const UCustomShape* Preview = GetPreview();
			if (Preview)
			{
				return &Preview->Path;
			}
			const UCustomShape* Template = GetTemplate();
			return Template ? &Template->Path : nullptr;
		}

		bool IsValidId(const FWidgetShapePath& Path, const FVertexId& Id) const
		{
			return Path.Contours.IsValidIndex(Id.Contour) && Path.Contours[Id.Contour].Vertices.IsValidIndex(Id.Vertex);
		}

		/** The selection, without anything an undo or a delete has left pointing nowhere. */
		void PruneSelection() const
		{
			if (const FWidgetShapePath* Path = GetPath())
			{
				State().Selection.RemoveAll([this, Path](const FVertexId& Id) { return !IsValidId(*Path, Id); });
			}
		}

		bool IsSelected(const FVertexId& Id) const { return State().Selection.Contains(Id); }

		/** The selected vertex at an end of an open contour - where the Pen carries on from. bOutFirst: it is the first. */
		FVertexId GetActiveEnd(bool& bOutFirst) const
		{
			bOutFirst = false;
			const FWidgetShapePath* Path = GetPath();
			if (!Path || State().Selection.Num() != 1)
			{
				return FVertexId();
			}
			const FVertexId Id = State().Selection[0];
			if (!IsValidId(*Path, Id) || Path->Contours[Id.Contour].bClosed)
			{
				return FVertexId();
			}
			const int32 Count = Path->Contours[Id.Contour].Vertices.Num();
			if (Id.Vertex == Count - 1)
			{
				return Id;
			}
			if (Id.Vertex == 0 && Count > 1)
			{
				bOutFirst = true;
				return Id;
			}
			return FVertexId();
		}

		// ---------------------------------------------------------------------------------------------
		// Where things are: box fractions, the widget's local space, the screen, and this layer
		// ---------------------------------------------------------------------------------------------

		bool UpdateTarget()
		{
			bHasTarget = false;
			UCustomShape* Preview = GetPreview();
			if (!Preview)
			{
				return false;
			}
			const TSharedPtr<SWidget> SlateWidget = Preview->GetCachedWidget();
			if (!SlateWidget.IsValid())
			{
				return false;
			}
			TargetGeometry = SlateWidget->GetTickSpaceGeometry();
			const FVector2D Size = TargetGeometry.GetLocalSize();
			if (Size.X <= 0.0 || Size.Y <= 0.0)
			{
				return false;
			}
			Frame = Preview->MakeFrame(ToF(Size));
			bHasTarget = true;
			return true;
		}

		FVector2D BoxToScreen(const FVector2D& Box) const
		{
			return TargetGeometry.LocalToAbsolute(ToD(Frame.ToLocal(ToF(Box))));
		}

		FVector2D BoxToCanvas(const FVector2D& Box) const
		{
			return GetTickSpaceGeometry().AbsoluteToLocal(BoxToScreen(Box));
		}

		FVector2D ScreenToBox(const FVector2D& Screen) const
		{
			return ToD(Frame.ToBox(ToF(TargetGeometry.AbsoluteToLocal(Screen))));
		}

		/** Local pixels per box unit, each way - what keeps angles and lengths true on a stretched widget. */
		FVector2D PixelsPerBox() const
		{
			return FVector2D(FMath::Abs(Frame.Size.X), FMath::Abs(Frame.Size.Y));
		}

		/** Canvas units per box unit, each way - for snapping distances measured on screen. */
		FVector2D CanvasPerBox() const
		{
			const FVector2D Origin = BoxToCanvas(FVector2D::ZeroVector);
			return FVector2D((BoxToCanvas(FVector2D(1.0, 0.0)) - Origin).Size(), (BoxToCanvas(FVector2D(0.0, 1.0)) - Origin).Size());
		}

		/** A segment's points on this layer, its start and end included. */
		void SegmentPoints(const FWidgetShapeContour& Contour, int32 Segment, TArray<FVector2f>& Out) const
		{
			const int32 Steps = Contour.IsSegmentStraight(Segment) ? 1 : CurveSamples;
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				Out.Add(ToF(BoxToCanvas(Contour.EvaluateSegment(Segment, static_cast<float>(Step) / Steps))));
			}
		}

		FHit HitTest(const FVector2D& Canvas) const;

		// ---------------------------------------------------------------------------------------------
		// Editing
		// ---------------------------------------------------------------------------------------------

		void BeginEdit(const FText& Description)
		{
			if (Transaction.IsValid())
			{
				return;
			}
			Transaction = MakeUnique<FScopedTransaction>(Description);
			if (UCustomShape* Template = GetTemplate())
			{
				Template->Modify();
			}
		}

		void Commit(const FWidgetShapePath& Path)
		{
			bChanged = true;
			UCustomShape* Template = GetTemplate();
			UCustomShape* Preview = GetPreview();
			if (Template)
			{
				Template->Path = Path;
			}
			if (Preview && Preview != Template)
			{
				Preview->Path = Path;
			}
			if (Preview)
			{
				Preview->InvalidateShape();
			}
		}

		void EndEdit()
		{
			if (!Transaction.IsValid())
			{
				return;
			}
			Transaction.Reset();
			if (bChanged)
			{
				// Marked changed without a broadcast: a broadcast rebuilds the designer and every Details panel, which only
				// the panel showing this shape needs - it is told through OnPathEdited.
				if (UCustomShape* Template = GetTemplate())
				{
					Template->MarkPackageDirty();
					if (UWidgetBlueprint* Blueprint = Template->GetTypedOuter<UWidgetBlueprint>())
					{
						Blueprint->Status = BS_Dirty;
					}
				}
				WidgetShapePathEditing::OnPathEdited().Broadcast(ShapeName);
			}
			bChanged = false;
		}

		/** One change, one undo step: Edit changes a copy of the path and says whether it changed anything. */
		void EditOnce(const FText& Description, TFunctionRef<bool(FWidgetShapePath&)> Edit)
		{
			const FWidgetShapePath* Current = GetPath();
			if (!Current)
			{
				return;
			}
			FWidgetShapePath Path = *Current;
			if (!Edit(Path))
			{
				return;
			}
			BeginEdit(Description);
			Commit(Path);
			EndEdit();
		}

		void FinishEditing()
		{
			EndEdit();
			DragKind = EDrag::None;
			WidgetShapePathEditing::SetEditing(GetTemplate(), false);
			FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
		}

		void SelectOnly(const FVertexId& Id)
		{
			State().Selection.Reset();
			if (Id.IsValid())
			{
				State().Selection.Add(Id);
			}
		}

		bool InsertOnSegment(const FHit& Hit)
		{
			FVertexId NewId;
			EditOnce(LOCTEXT("AddVertex", "Add Vertex"), [&](FWidgetShapePath& Path)
			{
				if (!Path.Contours.IsValidIndex(Hit.Id.Contour))
				{
					return false;
				}
				FWidgetShapeContour& Contour = Path.Contours[Hit.Id.Contour];
				if (Hit.Id.Vertex < 0 || Hit.Id.Vertex >= Contour.GetSegmentCount())
				{
					return false;
				}
				NewId = FVertexId(Hit.Id.Contour, Contour.SplitSegment(Hit.Id.Vertex, FMath::Clamp(Hit.T, 0.01f, 0.99f)));
				return true;
			});
			if (NewId.IsValid())
			{
				SelectOnly(NewId);
			}
			return NewId.IsValid();
		}

		void ToggleSmooth(const FVertexId& Id)
		{
			const FVector2D Pixels = PixelsPerBox();
			EditOnce(LOCTEXT("ToggleSmooth", "Change Vertex Kind"), [&](FWidgetShapePath& Path)
			{
				if (!IsValidId(Path, Id))
				{
					return false;
				}
				FWidgetShapeContour& Contour = Path.Contours[Id.Contour];
				const bool bStraight = Contour.Vertices[Id.Vertex].Type == EWidgetShapeVertexType::Straight;
				SetVertexType(Contour, Id.Vertex, bStraight ? EWidgetShapeVertexType::Mirrored : EWidgetShapeVertexType::Straight, Pixels);
				return true;
			});
		}

		void DeleteSelection()
		{
			PruneSelection();
			TArray<FVertexId> Doomed = State().Selection;
			if (Doomed.Num() == 0)
			{
				return;
			}
			// Back to front, so each removal leaves the indices still to come where they were.
			Doomed.Sort([](const FVertexId& A, const FVertexId& B) { return A.Contour != B.Contour ? A.Contour > B.Contour : A.Vertex > B.Vertex; });
			EditOnce(LOCTEXT("DeleteVertices", "Delete Vertices"), [&](FWidgetShapePath& Path)
			{
				for (const FVertexId& Id : Doomed)
				{
					if (IsValidId(Path, Id))
					{
						Path.Contours[Id.Contour].Vertices.RemoveAt(Id.Vertex);
					}
				}
				Path.Contours.RemoveAll([](const FWidgetShapeContour& Contour) { return Contour.Vertices.Num() == 0; });
				return true;
			});
			State().Selection.Reset();
		}

		void Nudge(const FVector2D& Pixels)
		{
			PruneSelection();
			if (State().Selection.Num() == 0)
			{
				return;
			}
			const FVector2D PerBox = PixelsPerBox();
			const FVector2D Delta(PerBox.X > 0.0 ? Pixels.X / PerBox.X : 0.0, PerBox.Y > 0.0 ? Pixels.Y / PerBox.Y : 0.0);
			const TArray<FVertexId> Selection = State().Selection;
			EditOnce(LOCTEXT("NudgeVertices", "Nudge Vertices"), [&](FWidgetShapePath& Path)
			{
				for (const FVertexId& Id : Selection)
				{
					if (IsValidId(Path, Id))
					{
						Path.Contours[Id.Contour].Vertices[Id.Vertex].Position += Delta;
					}
				}
				return true;
			});
		}

		// ---------------------------------------------------------------------------------------------
		// Dragging
		// ---------------------------------------------------------------------------------------------

		void DragVertices(const FVector2D& Screen, bool bConstrain, bool bNoSnap);
		void DragHandle(const FVector2D& Screen, bool bConstrain);
		void DragPull(const FVector2D& Screen, bool bConstrain);
		void DragMarquee(const FVector2D& Canvas);
		FReply PenDown(const FHit& Hit, const FVector2D& Screen, bool bConstrain, FReply Reply);

		// Toolbar actions
		void SetTool(ETool Tool) { State().Tool = Tool; }
		bool HasSelection() const { PruneSelection(); return State().Selection.Num() > 0; }
		void SetSelectedType(EWidgetShapeVertexType Type);
		TOptional<float> GetSelectedRadius() const;
		void SetSelectedRadius(float Radius);
		bool IsSelectedContourClosed() const;
		void ToggleSelectedContourClosed();
		void ReverseSelectedContours();

		FWidgetReference Reference;
		FName ShapeName;

		bool bHasTarget = false;
		FGeometry TargetGeometry;
		FWidgetShapeFrame Frame;
		FVector2D LastCanvasSize = FVector2D::ZeroVector;

		FHit Hover;
		bool bMouseInside = false;
		FVector2D LastMouseCanvas = FVector2D::ZeroVector;

		EDrag DragKind = EDrag::None;
		bool bDragStarted = false;
		bool bBreakHandle = false;
		bool bDragOutHandle = false;
		bool bPullPrepend = false;
		bool bClickedEmpty = false;
		FVector2D MouseDownScreen = FVector2D::ZeroVector;
		FVector2D MarqueeStart = FVector2D::ZeroVector;
		FVertexId DragVertex;
		FWidgetShapePath DragStartPath;
		TArray<FVertexId> MarqueeBase;
		TOptional<double> GuideX;
		TOptional<double> GuideY;

		TUniquePtr<FScopedTransaction> Transaction;
		bool bChanged = false;
	};

	// ---------------------------------------------------------------------------------------------
	// Hit testing
	// ---------------------------------------------------------------------------------------------

	FHit SWidgetShapePathCanvas::HitTest(const FVector2D& Canvas) const
	{
		FHit Hit;
		const FWidgetShapePath* Path = GetPath();
		if (!Path || !bHasTarget)
		{
			return Hit;
		}
		const float Radius = Palette::HitRadius;
		float Best = Radius;

		// The selected vertices' handles come first: they sit on top.
		PruneSelection();
		for (const FVertexId& Id : State().Selection)
		{
			const FWidgetShapeVertex& Vertex = Path->Contours[Id.Contour].Vertices[Id.Vertex];
			if (!Vertex.HasHandles())
			{
				continue;
			}
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FVector2D Handle = Side == 0 ? Vertex.InHandle : Vertex.OutHandle;
				if (Handle.IsNearlyZero(1e-9))
				{
					continue;
				}
				const float Distance = static_cast<float>((BoxToCanvas(Vertex.Position + Handle) - Canvas).Size());
				if (Distance < Best)
				{
					Best = Distance;
					Hit.Kind = Side == 0 ? EHit::InHandle : EHit::OutHandle;
					Hit.Id = Id;
				}
			}
		}
		if (Hit.Kind != EHit::None)
		{
			return Hit;
		}

		for (int32 ContourIndex = 0; ContourIndex < Path->Contours.Num(); ++ContourIndex)
		{
			const FWidgetShapeContour& Contour = Path->Contours[ContourIndex];
			for (int32 VertexIndex = 0; VertexIndex < Contour.Vertices.Num(); ++VertexIndex)
			{
				const float Distance = static_cast<float>((BoxToCanvas(Contour.Vertices[VertexIndex].Position) - Canvas).Size());
				if (Distance < Best)
				{
					Best = Distance;
					Hit.Kind = EHit::Vertex;
					Hit.Id = FVertexId(ContourIndex, VertexIndex);
				}
			}
		}
		if (Hit.Kind != EHit::None)
		{
			return Hit;
		}

		// Edges: the nearest point along each, and where along it that is.
		Best = Radius * 0.8f;
		TArray<FVector2f> Points;
		for (int32 ContourIndex = 0; ContourIndex < Path->Contours.Num(); ++ContourIndex)
		{
			const FWidgetShapeContour& Contour = Path->Contours[ContourIndex];
			for (int32 Segment = 0; Segment < Contour.GetSegmentCount(); ++Segment)
			{
				Points.Reset();
				SegmentPoints(Contour, Segment, Points);
				const int32 Steps = Points.Num() - 1;
				for (int32 Step = 0; Step < Steps; ++Step)
				{
					const FVector2D A = ToD(Points[Step]);
					const FVector2D B = ToD(Points[Step + 1]);
					const FVector2D AB = B - A;
					const double Along = AB.SizeSquared() > 1e-12 ? FMath::Clamp(FVector2D::DotProduct(Canvas - A, AB) / AB.SizeSquared(), 0.0, 1.0) : 0.0;
					const float Distance = static_cast<float>((A + AB * Along - Canvas).Size());
					if (Distance < Best)
					{
						Best = Distance;
						Hit.Kind = EHit::Segment;
						Hit.Id = FVertexId(ContourIndex, Segment);
						Hit.T = (static_cast<float>(Step) + static_cast<float>(Along)) / Steps;
					}
				}
			}
		}
		return Hit;
	}

	// ---------------------------------------------------------------------------------------------
	// Painting
	// ---------------------------------------------------------------------------------------------

	int32 SWidgetShapePathCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
	{
		const FWidgetShapePath* Path = GetPath();
		if (!IsEditing() || !bHasTarget || !Path)
		{
			return LayerId;
		}
		PruneSelection();

		auto DrawLine = [&](const TArray<FVector2f>& Points, const FLinearColor& Color, float Thickness, int32 Layer)
		{
			if (Points.Num() >= 2)
			{
				FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, /*bAntialias=*/true, Thickness);
			}
		};
		auto DrawDot = [&](const FVector2D& Center, float Size, const FSlateBrush* Brush, const FLinearColor& Color, int32 Layer)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, Layer,
				AllottedGeometry.ToPaintGeometry(FVector2f(Size, Size), FSlateLayoutTransform(ToF(Center) - FVector2f(Size, Size) * 0.5f)),
				Brush, ESlateDrawEffect::None, Color);
		};

		// The outline: a dark line under a light one, so it reads on any background.
		TArray<FVector2f> Points;
		for (const FWidgetShapeContour& Contour : Path->Contours)
		{
			Points.Reset();
			for (int32 Segment = 0; Segment < Contour.GetSegmentCount(); ++Segment)
			{
				TArray<FVector2f> SegmentLine;
				SegmentPoints(Contour, Segment, SegmentLine);
				if (Points.Num() > 0)
				{
					SegmentLine.RemoveAt(0);
				}
				Points.Append(SegmentLine);
			}
			DrawLine(Points, Palette::PathUnderlay, Palette::PathThickness + 2.f, LayerId);
			DrawLine(Points, Palette::PathLine, Palette::PathThickness, LayerId + 1);
		}

		// The edge under the mouse, where a double-click (or the Pen) adds a vertex.
		if (Hover.Kind == EHit::Segment && DragKind == EDrag::None && Path->Contours.IsValidIndex(Hover.Id.Contour))
		{
			const FWidgetShapeContour& Contour = Path->Contours[Hover.Id.Contour];
			if (Hover.Id.Vertex < Contour.GetSegmentCount())
			{
				Points.Reset();
				SegmentPoints(Contour, Hover.Id.Vertex, Points);
				DrawLine(Points, Palette::PathHovered, Palette::PathThickness + 1.f, LayerId + 1);
				DrawDot(BoxToCanvas(Contour.EvaluateSegment(Hover.Id.Vertex, Hover.T)), Palette::HandleSize, CircleBrush(), Palette::PathHovered, LayerId + 2);
			}
		}

		// Snap guides, across the widget and a little past it.
		if (GuideX.IsSet())
		{
			DrawLine({ ToF(BoxToCanvas(FVector2D(GuideX.GetValue(), -0.2))), ToF(BoxToCanvas(FVector2D(GuideX.GetValue(), 1.2))) }, Palette::SnapGuide, 1.f, LayerId + 1);
		}
		if (GuideY.IsSet())
		{
			DrawLine({ ToF(BoxToCanvas(FVector2D(-0.2, GuideY.GetValue()))), ToF(BoxToCanvas(FVector2D(1.2, GuideY.GetValue()))) }, Palette::SnapGuide, 1.f, LayerId + 1);
		}

		// The Pen's next edge, from the end it carries on from to the mouse.
		bool bActiveFirst = false;
		const FVertexId ActiveEnd = GetActiveEnd(bActiveFirst);
		if (State().Tool == ETool::Pen && bMouseInside && DragKind == EDrag::None && ActiveEnd.IsValid())
		{
			const FVector2D From = BoxToCanvas(Path->Contours[ActiveEnd.Contour].Vertices[ActiveEnd.Vertex].Position);
			DrawLine({ ToF(From), ToF(LastMouseCanvas) }, Palette::PenPreview, 1.f, LayerId + 1);
		}

		// Handles of the selected vertices.
		for (const FVertexId& Id : State().Selection)
		{
			const FWidgetShapeVertex& Vertex = Path->Contours[Id.Contour].Vertices[Id.Vertex];
			if (!Vertex.HasHandles())
			{
				continue;
			}
			const FVector2D Center = BoxToCanvas(Vertex.Position);
			for (const FVector2D& Handle : { Vertex.InHandle, Vertex.OutHandle })
			{
				if (Handle.IsNearlyZero(1e-9))
				{
					continue;
				}
				const FVector2D End = BoxToCanvas(Vertex.Position + Handle);
				DrawLine({ ToF(Center), ToF(End) }, Palette::HandleLine, 1.f, LayerId + 2);
				DrawDot(End, Palette::HandleSize + 2.f, CircleBrush(), Palette::VertexOutline, LayerId + 3);
				DrawDot(End, Palette::HandleSize, CircleBrush(), Palette::HandleFill, LayerId + 4);
			}
		}

		// Vertices: squares for Straight, circles for curves; filled orange when selected.
		for (int32 ContourIndex = 0; ContourIndex < Path->Contours.Num(); ++ContourIndex)
		{
			const FWidgetShapeContour& Contour = Path->Contours[ContourIndex];
			for (int32 VertexIndex = 0; VertexIndex < Contour.Vertices.Num(); ++VertexIndex)
			{
				const FVertexId Id(ContourIndex, VertexIndex);
				const FWidgetShapeVertex& Vertex = Contour.Vertices[VertexIndex];
				const FVector2D Center = BoxToCanvas(Vertex.Position);
				const bool bHovered = Hover.Kind == EHit::Vertex && Hover.Id == Id;
				const float Size = Palette::VertexSize + (bHovered ? 2.f : 0.f);
				const FSlateBrush* Brush = Vertex.HasHandles() ? CircleBrush() : SquareBrush();
				DrawDot(Center, Size + 2.f, Brush, Palette::VertexOutline, LayerId + 5);
				DrawDot(Center, Size, Brush, IsSelected(Id) ? Palette::Selected : Palette::VertexFill, LayerId + 6);

				// The Pen closes a contour by clicking its other end: ringed while it would.
				const bool bClosesHere = State().Tool == ETool::Pen && ActiveEnd.IsValid() && ActiveEnd.Contour == ContourIndex
					&& Contour.Vertices.Num() > 1 && VertexIndex == (bActiveFirst ? Contour.Vertices.Num() - 1 : 0) && bHovered;
				if (bClosesHere)
				{
					DrawDot(Center, Size + 8.f, CircleBrush(), Palette::CloseRing.CopyWithNewOpacity(0.5f), LayerId + 4);
				}
			}
		}

		// The selection box.
		if (DragKind == EDrag::Marquee && bDragStarted)
		{
			const FVector2D Min(FMath::Min(MarqueeStart.X, LastMouseCanvas.X), FMath::Min(MarqueeStart.Y, LastMouseCanvas.Y));
			const FVector2D Max(FMath::Max(MarqueeStart.X, LastMouseCanvas.X), FMath::Max(MarqueeStart.Y, LastMouseCanvas.Y));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 7,
				AllottedGeometry.ToPaintGeometry(ToF(Max - Min), FSlateLayoutTransform(ToF(Min))), SquareBrush(), ESlateDrawEffect::None, Palette::MarqueeFill);
			DrawLine({ ToF(Min), ToF(FVector2D(Max.X, Min.Y)), ToF(Max), ToF(FVector2D(Min.X, Max.Y)), ToF(Min) }, Palette::MarqueeOutline, 1.f, LayerId + 8);
		}

		return LayerId + 8;
	}

	// ---------------------------------------------------------------------------------------------
	// Mouse
	// ---------------------------------------------------------------------------------------------

	FReply SWidgetShapePathCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (!IsEditing() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !UpdateTarget() || !GetPath())
		{
			return FReply::Unhandled();
		}

		const FVector2D Screen = MouseEvent.GetScreenSpacePosition();
		const FVector2D Canvas = MyGeometry.AbsoluteToLocal(Screen);
		LastMouseCanvas = Canvas;
		MouseDownScreen = Screen;
		bDragStarted = false;
		bClickedEmpty = false;
		DragStartPath = *GetPath();
		GuideX.Reset();
		GuideY.Reset();
		PruneSelection();

		FReply Reply = FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
		const FHit Hit = HitTest(Canvas);
		const bool bShift = MouseEvent.IsShiftDown();
		const bool bCtrl = MouseEvent.IsControlDown();

		if (State().Tool == ETool::Pen && Hit.Kind != EHit::InHandle && Hit.Kind != EHit::OutHandle)
		{
			return PenDown(Hit, Screen, bShift, Reply);
		}

		switch (Hit.Kind)
		{
		case EHit::InHandle:
		case EHit::OutHandle:
			if (bCtrl)
			{
				// Ctrl-click a handle: take it away. A vertex left with none is Straight; with one, Disconnected.
				const bool bOut = Hit.Kind == EHit::OutHandle;
				EditOnce(LOCTEXT("RemoveHandle", "Remove Handle"), [&](FWidgetShapePath& Path)
				{
					if (!IsValidId(Path, Hit.Id))
					{
						return false;
					}
					FWidgetShapeVertex& Vertex = Path.Contours[Hit.Id.Contour].Vertices[Hit.Id.Vertex];
					(bOut ? Vertex.OutHandle : Vertex.InHandle) = FVector2D::ZeroVector;
					const bool bNone = Vertex.InHandle.IsNearlyZero(1e-9) && Vertex.OutHandle.IsNearlyZero(1e-9);
					Vertex.Type = bNone ? EWidgetShapeVertexType::Straight : EWidgetShapeVertexType::Disconnected;
					return true;
				});
				DragKind = EDrag::None;
				return Reply;
			}
			DragKind = EDrag::Handle;
			DragVertex = Hit.Id;
			bDragOutHandle = Hit.Kind == EHit::OutHandle;
			bBreakHandle = MouseEvent.IsAltDown();
			return Reply;

		case EHit::Vertex:
			if (bCtrl)
			{
				// Ctrl-drag: pull handles out of the vertex.
				SelectOnly(Hit.Id);
				DragKind = EDrag::PullHandles;
				DragVertex = Hit.Id;
				bPullPrepend = false;
				return Reply;
			}
			if (bShift)
			{
				if (IsSelected(Hit.Id))
				{
					State().Selection.Remove(Hit.Id);
					DragKind = EDrag::None;
					return Reply;
				}
				State().Selection.Add(Hit.Id);
			}
			else if (!IsSelected(Hit.Id))
			{
				SelectOnly(Hit.Id);
			}
			DragKind = EDrag::Vertices;
			DragVertex = Hit.Id;
			return Reply;

		case EHit::Segment:
		{
			// An edge: its two vertices, to move together.
			const FWidgetShapeContour& Contour = DragStartPath.Contours[Hit.Id.Contour];
			const FVertexId From(Hit.Id.Contour, Hit.Id.Vertex);
			const FVertexId To(Hit.Id.Contour, (Hit.Id.Vertex + 1) % Contour.Vertices.Num());
			if (!bShift)
			{
				State().Selection.Reset();
			}
			State().Selection.AddUnique(From);
			State().Selection.AddUnique(To);
			DragKind = EDrag::Vertices;
			DragVertex = From;
			return Reply;
		}

		default:
			// Empty space: a selection box, adding to the selection with Shift.
			if (!bShift)
			{
				State().Selection.Reset();
			}
			MarqueeBase = State().Selection;
			MarqueeStart = Canvas;
			DragKind = EDrag::Marquee;
			bClickedEmpty = true;
			return Reply;
		}
	}

	FReply SWidgetShapePathCanvas::PenDown(const FHit& Hit, const FVector2D& Screen, bool bConstrain, FReply Reply)
	{
		bool bActiveFirst = false;
		const FVertexId ActiveEnd = GetActiveEnd(bActiveFirst);
		const FWidgetShapePath& Start = DragStartPath;

		if (Hit.Kind == EHit::Vertex)
		{
			const FWidgetShapeContour& Contour = Start.Contours[Hit.Id.Contour];
			const int32 OtherEnd = bActiveFirst ? Contour.Vertices.Num() - 1 : 0;
			if (ActiveEnd.IsValid() && Hit.Id.Contour == ActiveEnd.Contour && Hit.Id.Vertex == OtherEnd && Hit.Id != ActiveEnd && Contour.Vertices.Num() > 1)
			{
				const int32 ContourIndex = Hit.Id.Contour;
				EditOnce(LOCTEXT("ClosePath", "Close Path"), [ContourIndex](FWidgetShapePath& Path)
				{
					Path.Contours[ContourIndex].bClosed = true;
					return true;
				});
				SelectOnly(Hit.Id);
				DragKind = EDrag::None;
				return Reply;
			}
			SelectOnly(Hit.Id);
			DragKind = EDrag::None;
			return Reply;
		}

		if (Hit.Kind == EHit::Segment)
		{
			InsertOnSegment(Hit);
			DragKind = EDrag::None;
			return Reply;
		}

		// Empty space: a new vertex, after the active end or starting a contour of its own.
		FVector2D Position = ScreenToBox(Screen);
		if (bConstrain && ActiveEnd.IsValid())
		{
			const FVector2D From = Start.Contours[ActiveEnd.Contour].Vertices[ActiveEnd.Vertex].Position;
			Position = From + SnapTo45(Position - From, PixelsPerBox());
		}

		FWidgetShapePath Path = Start;
		FVertexId NewId;
		bPullPrepend = false;
		if (ActiveEnd.IsValid())
		{
			FWidgetShapeContour& Contour = Path.Contours[ActiveEnd.Contour];
			if (bActiveFirst)
			{
				Contour.Vertices.Insert(FWidgetShapeVertex(Position), 0);
				NewId = FVertexId(ActiveEnd.Contour, 0);
				bPullPrepend = true;
			}
			else
			{
				NewId = FVertexId(ActiveEnd.Contour, Contour.Vertices.Add(FWidgetShapeVertex(Position)));
			}
		}
		else
		{
			FWidgetShapeContour& Contour = Path.Contours.AddDefaulted_GetRef();
			Contour.bClosed = false;
			Contour.Vertices.Add(FWidgetShapeVertex(Position));
			NewId = FVertexId(Path.Contours.Num() - 1, 0);
		}

		BeginEdit(LOCTEXT("AddVertexPen", "Add Vertex"));
		Commit(Path);
		SelectOnly(NewId);

		// Dragging on from here pulls the new vertex's handles out; the edit is one undo step with it.
		DragStartPath = Path;
		DragKind = EDrag::PullHandles;
		DragVertex = NewId;
		return Reply;
	}

	FReply SWidgetShapePathCanvas::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (!IsEditing() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !UpdateTarget())
		{
			return FReply::Unhandled();
		}
		EndEdit();
		DragKind = EDrag::None;

		const FHit Hit = HitTest(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
		switch (Hit.Kind)
		{
		case EHit::Vertex:
			ToggleSmooth(Hit.Id);
			SelectOnly(Hit.Id);
			break;
		case EHit::Segment:
			InsertOnSegment(Hit);
			break;
		case EHit::None:
			if (State().Tool == ETool::Select)
			{
				FinishEditing();
			}
			break;
		default:
			break;
		}
		return FReply::Handled();
	}

	FReply SWidgetShapePathCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (!IsEditing())
		{
			return FReply::Unhandled();
		}
		const FVector2D Screen = MouseEvent.GetScreenSpacePosition();
		LastMouseCanvas = MyGeometry.AbsoluteToLocal(Screen);
		bMouseInside = true;

		if (!HasMouseCapture())
		{
			if (UpdateTarget())
			{
				Hover = HitTest(LastMouseCanvas);
			}
			return FReply::Unhandled();
		}

		if (!bDragStarted)
		{
			const FVector2D MovedOnCanvas = LastMouseCanvas - MyGeometry.AbsoluteToLocal(MouseDownScreen);
			if (MovedOnCanvas.Size() < DragThreshold)
			{
				return FReply::Handled();
			}
			bDragStarted = true;
		}
		UpdateTarget();
		Hover = FHit();

		switch (DragKind)
		{
		case EDrag::Vertices: DragVertices(Screen, MouseEvent.IsShiftDown(), MouseEvent.IsControlDown()); break;
		case EDrag::Handle: DragHandle(Screen, MouseEvent.IsShiftDown()); break;
		case EDrag::PullHandles: DragPull(Screen, MouseEvent.IsShiftDown()); break;
		case EDrag::Marquee: DragMarquee(LastMouseCanvas); break;
		default: break;
		}
		return FReply::Handled();
	}

	FReply SWidgetShapePathCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (!HasMouseCapture() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}

		// A plain click on empty space well away from the shape finishes, as clicking off a vector does in the vector tools.
		if (bClickedEmpty && !bDragStarted && State().Tool == ETool::Select && bHasTarget)
		{
			const FVector2D Box = ScreenToBox(MouseEvent.GetScreenSpacePosition());
			const FVector2D Slack = FVector2D(Palette::HitRadius, Palette::HitRadius) / CanvasPerBox().ComponentMax(FVector2D(1e-6, 1e-6));
			const FBox2D Bounds = GetPath()->GetBounds(/*bIncludeHandles=*/false).ExpandBy(FVector2D(Slack.X, Slack.Y));
			if (!Bounds.IsInside(Box))
			{
				FinishEditing();
				return FReply::Handled().ReleaseMouseCapture();
			}
		}

		EndEdit();
		DragKind = EDrag::None;
		bDragStarted = false;
		GuideX.Reset();
		GuideY.Reset();
		return FReply::Handled().ReleaseMouseCapture();
	}

	void SWidgetShapePathCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
	{
		EndEdit();
		DragKind = EDrag::None;
		bDragStarted = false;
		GuideX.Reset();
		GuideY.Reset();
	}

	FCursorReply SWidgetShapePathCanvas::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
	{
		if (!IsEditing())
		{
			return FCursorReply::Unhandled();
		}
		if (State().Tool == ETool::Pen && Hover.Kind == EHit::None)
		{
			return FCursorReply::Cursor(EMouseCursor::Crosshairs);
		}
		if (Hover.Kind != EHit::None || DragKind == EDrag::Vertices || DragKind == EDrag::Handle)
		{
			return FCursorReply::Cursor(EMouseCursor::Hand);
		}
		return FCursorReply::Cursor(EMouseCursor::Default);
	}

	// ---------------------------------------------------------------------------------------------
	// Drags
	// ---------------------------------------------------------------------------------------------

	void SWidgetShapePathCanvas::DragVertices(const FVector2D& Screen, bool bConstrain, bool bNoSnap)
	{
		if (!IsValidId(DragStartPath, DragVertex))
		{
			return;
		}
		FVector2D Delta = ScreenToBox(Screen) - ScreenToBox(MouseDownScreen);
		if (bConstrain)
		{
			Delta = SnapTo45(Delta, PixelsPerBox());
		}

		GuideX.Reset();
		GuideY.Reset();
		const TArray<FVertexId>& Selection = State().Selection;
		if (!bNoSnap && !bConstrain)
		{
			// The vertex under the mouse snaps to the lines of every vertex that isn't moving, and to the widget's edges and
			// middle; everything selected moves with it.
			const FVector2D Lead = DragStartPath.Contours[DragVertex.Contour].Vertices[DragVertex.Vertex].Position + Delta;
			const FVector2D PerBox = CanvasPerBox();
			const FVector2D Reach(PerBox.X > 1e-6 ? SnapDistance / PerBox.X : 0.0, PerBox.Y > 1e-6 ? SnapDistance / PerBox.Y : 0.0);
			double BestX = Reach.X;
			double BestY = Reach.Y;
			auto Consider = [&](const FVector2D& Candidate)
			{
				const double DX = FMath::Abs(Candidate.X - Lead.X);
				if (DX < BestX) { BestX = DX; GuideX = Candidate.X; }
				const double DY = FMath::Abs(Candidate.Y - Lead.Y);
				if (DY < BestY) { BestY = DY; GuideY = Candidate.Y; }
			};
			for (const double Edge : { 0.0, 0.5, 1.0 })
			{
				Consider(FVector2D(Edge, Edge));
			}
			for (int32 ContourIndex = 0; ContourIndex < DragStartPath.Contours.Num(); ++ContourIndex)
			{
				const FWidgetShapeContour& Contour = DragStartPath.Contours[ContourIndex];
				for (int32 VertexIndex = 0; VertexIndex < Contour.Vertices.Num(); ++VertexIndex)
				{
					if (!Selection.Contains(FVertexId(ContourIndex, VertexIndex)))
					{
						Consider(Contour.Vertices[VertexIndex].Position);
					}
				}
			}
			if (GuideX.IsSet())
			{
				Delta.X += GuideX.GetValue() - Lead.X;
			}
			if (GuideY.IsSet())
			{
				Delta.Y += GuideY.GetValue() - Lead.Y;
			}
		}

		FWidgetShapePath Path = DragStartPath;
		for (const FVertexId& Id : Selection)
		{
			if (IsValidId(Path, Id))
			{
				Path.Contours[Id.Contour].Vertices[Id.Vertex].Position += Delta;
			}
		}
		BeginEdit(LOCTEXT("MoveVertices", "Move Vertices"));
		Commit(Path);
	}

	void SWidgetShapePathCanvas::DragHandle(const FVector2D& Screen, bool bConstrain)
	{
		if (!IsValidId(DragStartPath, DragVertex))
		{
			return;
		}
		FWidgetShapePath Path = DragStartPath;
		FWidgetShapeVertex& Vertex = Path.Contours[DragVertex.Contour].Vertices[DragVertex.Vertex];
		FVector2D Handle = ScreenToBox(Screen) - Vertex.Position;
		if (bConstrain)
		{
			Handle = SnapTo45(Handle, PixelsPerBox());
		}
		if (bBreakHandle)
		{
			Vertex.Type = EWidgetShapeVertexType::Disconnected;
		}
		(bDragOutHandle ? Vertex.OutHandle : Vertex.InHandle) = Handle;
		Vertex.EnforceType(bDragOutHandle);
		BeginEdit(LOCTEXT("MoveHandle", "Move Handle"));
		Commit(Path);
	}

	void SWidgetShapePathCanvas::DragPull(const FVector2D& Screen, bool bConstrain)
	{
		if (!IsValidId(DragStartPath, DragVertex))
		{
			return;
		}
		FWidgetShapePath Path = DragStartPath;
		FWidgetShapeVertex& Vertex = Path.Contours[DragVertex.Contour].Vertices[DragVertex.Vertex];
		FVector2D Pull = ScreenToBox(Screen) - Vertex.Position;
		if (bConstrain)
		{
			Pull = SnapTo45(Pull, PixelsPerBox());
		}
		// Pulled the way the contour is being drawn: forward for a vertex added at the end, backward at the start.
		Vertex.Type = EWidgetShapeVertexType::Mirrored;
		Vertex.OutHandle = bPullPrepend ? -Pull : Pull;
		Vertex.InHandle = -Vertex.OutHandle;
		BeginEdit(LOCTEXT("PullHandles", "Pull Handles"));
		Commit(Path);
	}

	void SWidgetShapePathCanvas::DragMarquee(const FVector2D& Canvas)
	{
		const FWidgetShapePath* Path = GetPath();
		if (!Path)
		{
			return;
		}
		const FBox2D Box(FVector2D(FMath::Min(MarqueeStart.X, Canvas.X), FMath::Min(MarqueeStart.Y, Canvas.Y)),
			FVector2D(FMath::Max(MarqueeStart.X, Canvas.X), FMath::Max(MarqueeStart.Y, Canvas.Y)));
		TArray<FVertexId>& Selection = State().Selection;
		Selection = MarqueeBase;
		for (int32 ContourIndex = 0; ContourIndex < Path->Contours.Num(); ++ContourIndex)
		{
			const FWidgetShapeContour& Contour = Path->Contours[ContourIndex];
			for (int32 VertexIndex = 0; VertexIndex < Contour.Vertices.Num(); ++VertexIndex)
			{
				if (Box.IsInside(BoxToCanvas(Contour.Vertices[VertexIndex].Position)))
				{
					Selection.AddUnique(FVertexId(ContourIndex, VertexIndex));
				}
			}
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Keys
	// ---------------------------------------------------------------------------------------------

	FReply SWidgetShapePathCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
	{
		if (!IsEditing())
		{
			return FReply::Unhandled();
		}
		UpdateTarget();
		const FKey Key = InKeyEvent.GetKey();
		const bool bShift = InKeyEvent.IsShiftDown();
		const bool bCtrl = InKeyEvent.IsControlDown();

		if (Key == EKeys::Delete || Key == EKeys::BackSpace)
		{
			DeleteSelection();
			return FReply::Handled();
		}
		if (Key == EKeys::Escape)
		{
			if (HasMouseCapture())
			{
				// Mid-drag: put everything back as it was when the drag started.
				Commit(DragStartPath);
				EndEdit();
				DragKind = EDrag::None;
				return FReply::Handled().ReleaseMouseCapture();
			}
			if (State().Selection.Num() > 0)
			{
				State().Selection.Reset();
				return FReply::Handled();
			}
			FinishEditing();
			return FReply::Handled();
		}
		if (Key == EKeys::Enter)
		{
			FinishEditing();
			return FReply::Handled();
		}
		if (Key == EKeys::V && !bCtrl)
		{
			SetTool(ETool::Select);
			return FReply::Handled();
		}
		if (Key == EKeys::P && !bCtrl)
		{
			SetTool(ETool::Pen);
			return FReply::Handled();
		}
		if (Key == EKeys::A && bCtrl)
		{
			State().Selection.Reset();
			if (const FWidgetShapePath* Path = GetPath())
			{
				for (int32 ContourIndex = 0; ContourIndex < Path->Contours.Num(); ++ContourIndex)
				{
					for (int32 VertexIndex = 0; VertexIndex < Path->Contours[ContourIndex].Vertices.Num(); ++VertexIndex)
					{
						State().Selection.Add(FVertexId(ContourIndex, VertexIndex));
					}
				}
			}
			return FReply::Handled();
		}

		const double Step = bShift ? 10.0 : 1.0;
		if (Key == EKeys::Left) { Nudge(FVector2D(-Step, 0.0)); return FReply::Handled(); }
		if (Key == EKeys::Right) { Nudge(FVector2D(Step, 0.0)); return FReply::Handled(); }
		if (Key == EKeys::Up) { Nudge(FVector2D(0.0, -Step)); return FReply::Handled(); }
		if (Key == EKeys::Down) { Nudge(FVector2D(0.0, Step)); return FReply::Handled(); }

		// Anything else - Ctrl+Z among them - goes on up to the editor.
		return FReply::Unhandled();
	}

	// ---------------------------------------------------------------------------------------------
	// Toolbar actions
	// ---------------------------------------------------------------------------------------------

	void SWidgetShapePathCanvas::SetSelectedType(EWidgetShapeVertexType Type)
	{
		PruneSelection();
		UpdateTarget();
		const TArray<FVertexId> Selection = State().Selection;
		const FVector2D Pixels = PixelsPerBox();
		EditOnce(LOCTEXT("SetVertexKind", "Change Vertex Kind"), [&](FWidgetShapePath& Path)
		{
			bool bAny = false;
			for (const FVertexId& Id : Selection)
			{
				if (IsValidId(Path, Id))
				{
					SetVertexType(Path.Contours[Id.Contour], Id.Vertex, Type, Pixels);
					bAny = true;
				}
			}
			return bAny;
		});
	}

	TOptional<float> SWidgetShapePathCanvas::GetSelectedRadius() const
	{
		PruneSelection();
		const FWidgetShapePath* Path = GetPath();
		if (!Path)
		{
			return TOptional<float>();
		}
		for (const FVertexId& Id : State().Selection)
		{
			const FWidgetShapeVertex& Vertex = Path->Contours[Id.Contour].Vertices[Id.Vertex];
			if (!Vertex.HasHandles())
			{
				return Vertex.CornerRadius;
			}
		}
		return TOptional<float>();
	}

	void SWidgetShapePathCanvas::SetSelectedRadius(float Radius)
	{
		PruneSelection();
		const TArray<FVertexId> Selection = State().Selection;
		EditOnce(LOCTEXT("SetCornerRadius", "Set Corner Radius"), [&](FWidgetShapePath& Path)
		{
			bool bAny = false;
			for (const FVertexId& Id : Selection)
			{
				FWidgetShapeVertex& Vertex = Path.Contours[Id.Contour].Vertices[Id.Vertex];
				if (!Vertex.HasHandles())
				{
					Vertex.CornerRadius = FMath::Max(Radius, 0.f);
					bAny = true;
				}
			}
			return bAny;
		});
	}

	bool SWidgetShapePathCanvas::IsSelectedContourClosed() const
	{
		PruneSelection();
		const FWidgetShapePath* Path = GetPath();
		if (!Path || State().Selection.Num() == 0)
		{
			return true;
		}
		return Path->Contours[State().Selection[0].Contour].bClosed;
	}

	void SWidgetShapePathCanvas::ToggleSelectedContourClosed()
	{
		PruneSelection();
		if (State().Selection.Num() == 0)
		{
			return;
		}
		const int32 ContourIndex = State().Selection[0].Contour;
		const bool bClose = !IsSelectedContourClosed();
		EditOnce(bClose ? LOCTEXT("ClosePathButton", "Close Path") : LOCTEXT("OpenPath", "Open Path"), [&](FWidgetShapePath& Path)
		{
			Path.Contours[ContourIndex].bClosed = bClose;
			return true;
		});
	}

	void SWidgetShapePathCanvas::ReverseSelectedContours()
	{
		PruneSelection();
		TSet<int32> Contours;
		for (const FVertexId& Id : State().Selection)
		{
			Contours.Add(Id.Contour);
		}
		EditOnce(LOCTEXT("ReverseDirection", "Reverse Direction"), [&](FWidgetShapePath& Path)
		{
			for (int32 ContourIndex = 0; ContourIndex < Path.Contours.Num(); ++ContourIndex)
			{
				if (Contours.Num() == 0 || Contours.Contains(ContourIndex))
				{
					Path.Contours[ContourIndex].Reverse();
				}
			}
			return Path.Contours.Num() > 0;
		});
		State().Selection.Reset();
	}

	// ---------------------------------------------------------------------------------------------
	// Toolbar
	// ---------------------------------------------------------------------------------------------

	TSharedRef<SWidget> SWidgetShapePathCanvas::MakeToolbar()
	{
		using WidgetShapeEditorUI::FChip;
		using WidgetShapeEditorUI::MakeChip;

		const TWeakPtr<SWidgetShapePathCanvas> WeakCanvas = StaticCastSharedRef<SWidgetShapePathCanvas>(AsShared());
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 9);

		auto Ask = [WeakCanvas](TFunction<bool(const SWidgetShapePathCanvas&)> Query) -> TFunction<bool()>
		{
			return [WeakCanvas, Query]()
			{
				const TSharedPtr<SWidgetShapePathCanvas> Canvas = WeakCanvas.Pin();
				return Canvas.IsValid() && Query(*Canvas);
			};
		};
		auto Do = [WeakCanvas](TFunction<void(SWidgetShapePathCanvas&)> Action) -> TFunction<void()>
		{
			return [WeakCanvas, Action]()
			{
				if (const TSharedPtr<SWidgetShapePathCanvas> Canvas = WeakCanvas.Pin())
				{
					Action(*Canvas);
				}
			};
		};
		const TFunction<bool()> Editing = Ask([](const SWidgetShapePathCanvas& Canvas) { return Canvas.IsEditing(); });
		const TFunction<bool()> Selected = Ask([](const SWidgetShapePathCanvas& Canvas) { return Canvas.HasSelection(); });

		auto TypeChip = [&](EWidgetShapeVertexType Type, FName Icon, const FText& ToolTip)
		{
			FChip Chip;
			Chip.Icon = Icon;
			Chip.ToolTip = ToolTip;
			Chip.IsEnabled = Selected;
			Chip.IsActive = Ask([Type](const SWidgetShapePathCanvas& Canvas)
			{
				const FWidgetShapePath* Path = Canvas.GetPath();
				if (!Path || !Canvas.HasSelection())
				{
					return false;
				}
				for (const FVertexId& Id : Canvas.State().Selection)
				{
					if (Path->Contours[Id.Contour].Vertices[Id.Vertex].Type != Type)
					{
						return false;
					}
				}
				return true;
			});
			Chip.OnClick = Do([Type](SWidgetShapePathCanvas& Canvas) { Canvas.SetSelectedType(Type); });
			return MakeChip(Chip);
		};

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		auto Add = [&Row](TSharedRef<SWidget> Widget, TFunction<bool()> Shown, float Gap = 2.f)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(Gap, 0.f, 0.f, 0.f))[Widget];
			Widget->SetVisibility(TAttribute<EVisibility>::CreateLambda([Shown]() { return Shown() ? EVisibility::Visible : EVisibility::Collapsed; }));
		};
		auto Divider = []()
		{
			return SNew(SBox).WidthOverride(1.f).HeightOverride(16.f)[SNew(SBorder).BorderImage(SquareBrush()).BorderBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.15f))];
		};
		const TFunction<bool()> NotEditing = [Editing]() { return !Editing(); };

		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapeEditIcon;
			Chip.Label = Tips::EditOnCanvas();
			Chip.ToolTip = Tips::EditShapeTooltip();
			Chip.OnClick = Do([](SWidgetShapePathCanvas& Canvas) { WidgetShapePathEditing::SetEditing(Canvas.GetTemplate(), true); });
			Add(MakeChip(Chip), NotEditing, 0.f);
		}
		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapeDoneIcon;
			Chip.Label = LOCTEXT("Done", "Done");
			Chip.ToolTip = Tips::DoneTooltip();
			Chip.IsActive = []() { return true; };
			Chip.OnClick = Do([](SWidgetShapePathCanvas& Canvas) { Canvas.FinishEditing(); });
			Add(MakeChip(Chip), Editing, 0.f);
		}
		Add(Divider(), Editing, 6.f);
		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapeSelectIcon;
			Chip.ToolTip = Tips::SelectToolTooltip();
			Chip.IsActive = Ask([](const SWidgetShapePathCanvas& Canvas) { return Canvas.State().Tool == ETool::Select; });
			Chip.OnClick = Do([](SWidgetShapePathCanvas& Canvas) { Canvas.SetTool(ETool::Select); });
			Add(MakeChip(Chip), Editing, 6.f);
		}
		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapePenIcon;
			Chip.ToolTip = Tips::PenToolTooltip();
			Chip.IsActive = Ask([](const SWidgetShapePathCanvas& Canvas) { return Canvas.State().Tool == ETool::Pen; });
			Chip.OnClick = Do([](SWidgetShapePathCanvas& Canvas) { Canvas.SetTool(ETool::Pen); });
			Add(MakeChip(Chip), Editing);
		}
		Add(Divider(), Editing, 6.f);
		Add(TypeChip(EWidgetShapeVertexType::Straight, FNameTBDStyle::ShapeStraightIcon, Tips::StraightTooltip()), Editing, 6.f);
		Add(TypeChip(EWidgetShapeVertexType::Mirrored, FNameTBDStyle::ShapeMirroredIcon, Tips::MirroredTooltip()), Editing);
		Add(TypeChip(EWidgetShapeVertexType::Asymmetric, FNameTBDStyle::ShapeAsymmetricIcon, Tips::AsymmetricTooltip()), Editing);
		Add(TypeChip(EWidgetShapeVertexType::Disconnected, FNameTBDStyle::ShapeDisconnectedIcon, Tips::DisconnectedTooltip()), Editing);
		Add(Divider(), Editing, 6.f);

		Add(SNew(SHorizontalBox)
			.ToolTipText(Tips::CornerRadiusTooltip())
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				SNew(STextBlock).Font(Font).ColorAndOpacity(FSlateColor(Palette::ToolText)).Text(LOCTEXT("Radius", "Radius"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(50.f)
				[
					SNew(SNumericEntryBox<float>)
					.Font(Font)
					.AllowSpin(true)
					.MinValue(0.f)
					.MinSliderValue(0.f)
					.MaxSliderValue(64.f)
					.IsEnabled_Lambda([WeakCanvas]() { const TSharedPtr<SWidgetShapePathCanvas> Canvas = WeakCanvas.Pin(); return Canvas.IsValid() && Canvas->GetSelectedRadius().IsSet(); })
					.Value_Lambda([WeakCanvas]() { const TSharedPtr<SWidgetShapePathCanvas> Canvas = WeakCanvas.Pin(); return Canvas.IsValid() ? Canvas->GetSelectedRadius() : TOptional<float>(); })
					.OnValueCommitted_Lambda([WeakCanvas](float Value, ETextCommit::Type) { if (const TSharedPtr<SWidgetShapePathCanvas> Canvas = WeakCanvas.Pin()) { Canvas->SetSelectedRadius(Value); } })
				]
			], Editing, 6.f);

		Add(Divider(), Editing, 6.f);
		{
			FChip Chip;
			const TFunction<bool()> Closed = Ask([](const SWidgetShapePathCanvas& Canvas) { return Canvas.IsSelectedContourClosed(); });
			Chip.GetIcon = [Closed]() { return Closed() ? FNameTBDStyle::ShapeOpenPathIcon : FNameTBDStyle::ShapeClosePathIcon; };
			Chip.GetToolTip = [Closed]() { return Closed() ? Tips::OpenContourTooltip() : Tips::CloseContourTooltip(); };
			Chip.IsEnabled = Selected;
			Chip.OnClick = Do([](SWidgetShapePathCanvas& Canvas) { Canvas.ToggleSelectedContourClosed(); });
			Add(MakeChip(Chip), Editing, 6.f);
		}
		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapeReverseIcon;
			Chip.ToolTip = Tips::ReverseTooltip();
			Chip.OnClick = Do([](SWidgetShapePathCanvas& Canvas) { Canvas.ReverseSelectedContours(); });
			Add(MakeChip(Chip), Editing);
		}
		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapeDeleteIcon;
			Chip.ToolTip = Tips::DeleteVerticesTooltip();
			Chip.IsEnabled = Selected;
			Chip.OnClick = Do([](SWidgetShapePathCanvas& Canvas) { Canvas.DeleteSelection(); });
			Add(MakeChip(Chip), Editing);
		}

		return SNew(SBorder)
			.BorderImage(ToolbarBrush())
			.Padding(FMargin(4.f, 3.f))
			[
				Row
			];
	}
}

// =====================================================================================================
// Extension
// =====================================================================================================

FWidgetShapePathEditorExtension::FWidgetShapePathEditorExtension()
{
	ExtensionId = FName(TEXT("WidgetShapePathEditor"));
}

bool FWidgetShapePathEditorExtension::CanExtendSelection(const TArray<FWidgetReference>& Selection) const
{
	return Selection.Num() == 1 && Cast<UCustomShape>(Selection[0].GetTemplate()) != nullptr;
}

void FWidgetShapePathEditorExtension::ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements)
{
	using namespace WidgetShapePathEditorImpl;

	if (Selection.Num() != 1)
	{
		return;
	}

	// The layer first and the toolbar after it, so the toolbar is drawn - and clicked - on top where they overlap.
	const TSharedRef<SWidgetShapePathCanvas> Canvas = SNew(SWidgetShapePathCanvas).Widget(Selection[0]);
	SurfaceElements.Add(MakeShared<FDesignerSurfaceElement>(Canvas, EExtensionLayoutLocation::TopLeft, FVector2D(-OverlayMargin, -OverlayMargin), FVector2D(0.0, 0.0)));
	// Centered above the shape: its bottom middle (alignment 0.5, 1) a few units over the shape's top middle.
	SurfaceElements.Add(MakeShared<FDesignerSurfaceElement>(Canvas->MakeToolbar(), EExtensionLayoutLocation::TopCenter, FVector2D(0.0, -ToolbarLift), FVector2D(0.5, 1.0)));
}

TSharedRef<FDesignerExtension> FWidgetShapePathEditorFactory::CreateDesignerExtension() const
{
	return MakeShared<FWidgetShapePathEditorExtension>();
}

#undef LOCTEXT_NAMESPACE
