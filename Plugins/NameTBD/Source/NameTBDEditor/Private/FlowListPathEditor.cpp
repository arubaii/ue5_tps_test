// Copyright AR GameSystems 2026. All rights reserved.

#include "FlowListPathEditor.h"

#include "NameTBDColors.h"
#include "NameTBDStyle.h"
#include "NameTBDTooltips.h"
#include "WidgetShapeEditorUI.h"
#include "NameTBD/Widgets/FlowList/FlowList.h"
#include "NameTBD/Widgets/FlowList/FlowListScrollBar.h"

#include "Algo/Reverse.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/ICursor.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"

#define LOCTEXT_NAMESPACE "FlowListPathEditor"

// =====================================================================================================
// Edit state
// =====================================================================================================

namespace FlowListPathEditing
{
	FState& GetState(FName Name)
	{
		static TMap<FName, FState> States;
		return States.FindOrAdd(Name);
	}

	void SetEditing(const UObject* List, bool bEditing)
	{
		if (!List)
		{
			return;
		}
		FState& State = GetState(List->GetFName());
		State.bEditing = bEditing;
		if (!bEditing)
		{
			State.Tool = ETool::Select;
			State.Selection.Reset();
		}
	}
}

// =====================================================================================================
// The canvas layer
// =====================================================================================================

namespace FlowListPathEditorImpl
{
	namespace Palette = NameTBD::Colors::ShapeEditor;
	namespace Tips = NameTBD::Tooltips::FlowListPathEditor;
	using FlowListPathEditing::ETool;
	using FlowListPathEditing::FState;

	/** How far the layer reaches past the list on every side, in canvas units: room to drag points outside it. */
	constexpr float OverlayMargin = 160.f;

	/** How far above the list the toolbar's bottom edge sits. */
	constexpr float ToolbarLift = 6.f;

	/** How far the mouse must move, in canvas units, before a press becomes a drag. */
	constexpr float DragThreshold = 3.f;

	/** How close, in canvas units, a dragged point must come to another's line to snap to it. */
	constexpr float SnapDistance = 6.f;

	/** The focus marker's size on the canvas. */
	constexpr float FocusMarkerSize = 15.f;

	/** A path keeps at least this many points. */
	constexpr int32 MinPoints = 2;

	FVector2f ToF(const FVector2D& V) { return FVector2f(static_cast<float>(V.X), static_cast<float>(V.Y)); }
	FVector2D ToD(const FVector2f& V) { return FVector2D(V.X, V.Y); }

	const FSlateBrush* CircleBrush()
	{
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

	/** Snaps a vector's direction to the nearest 45 degrees, measured in local pixels (fractions are stretched by the list). */
	FVector2D SnapTo45(const FVector2D& FractionVector, const FVector2D& PixelsPerFraction)
	{
		const FVector2D Pixels = FractionVector * PixelsPerFraction;
		const double Length = Pixels.Size();
		if (Length <= 1e-9)
		{
			return FractionVector;
		}
		const double Angle = FMath::RoundToDouble(FMath::Atan2(Pixels.Y, Pixels.X) / (UE_DOUBLE_PI / 4.0)) * (UE_DOUBLE_PI / 4.0);
		const FVector2D Snapped(FMath::Cos(Angle) * Length, FMath::Sin(Angle) * Length);
		return FVector2D(PixelsPerFraction.X > 0.0 ? Snapped.X / PixelsPerFraction.X : 0.0, PixelsPerFraction.Y > 0.0 ? Snapped.Y / PixelsPerFraction.Y : 0.0);
	}

	/** The point on a line of points nearest Point: how far along it that is, which piece it is on, and how far off it Point is. */
	struct FProjection
	{
		float Along = 0.f;
		int32 Piece = INDEX_NONE;
		float Distance = TNumericLimits<float>::Max();
		FVector2f Position = FVector2f::ZeroVector;
	};

	FProjection Project(const TArray<FVector2f>& Line, const FVector2f& Point)
	{
		FProjection Best;
		float Walked = 0.f;
		for (int32 Piece = 0; Piece + 1 < Line.Num(); ++Piece)
		{
			const FVector2f A = Line[Piece];
			const FVector2f Delta = Line[Piece + 1] - A;
			const float Length = Delta.Size();
			const float Alpha = Length > 1e-6f ? FMath::Clamp(FVector2f::DotProduct(Point - A, Delta) / (Length * Length), 0.f, 1.f) : 0.f;
			const FVector2f OnLine = A + Delta * Alpha;
			const float Distance = FVector2f::Distance(OnLine, Point);
			if (Distance < Best.Distance)
			{
				Best.Distance = Distance;
				Best.Along = Walked + Length * Alpha;
				Best.Piece = Piece;
				Best.Position = OnLine;
			}
			Walked += Length;
		}
		return Best;
	}

	/** The point Along a line of points, clamped to it. */
	FVector2f PointAlong(const TArray<FVector2f>& Line, float Along)
	{
		if (Line.Num() == 0)
		{
			return FVector2f::ZeroVector;
		}
		float Walked = 0.f;
		for (int32 Piece = 0; Piece + 1 < Line.Num(); ++Piece)
		{
			const float Length = FVector2f::Distance(Line[Piece], Line[Piece + 1]);
			if (Walked + Length >= Along)
			{
				return Length > 1e-6f ? FMath::Lerp(Line[Piece], Line[Piece + 1], FMath::Clamp((Along - Walked) / Length, 0.f, 1.f)) : Line[Piece];
			}
			Walked += Length;
		}
		return Line.Last();
	}

	/**
	 * What a spline is edited on: a Flow List's Spline path (Layout > Spline Points, with Spline Focus and Focus Offset) or a
	 * Flow List Scroll Bar's (Spline Points, with no focus). Everything else about the editing is the same.
	 */
	namespace SplineTarget
	{
		bool IsSupported(const UWidget* Widget)
		{
			return Widget && (Widget->IsA<UFlowList>() || Widget->IsA<UFlowListScrollBar>());
		}

		bool IsSplineMode(const UWidget* Widget)
		{
			if (const UFlowList* List = Cast<UFlowList>(Widget))
			{
				return List->ListLayout.Type == EFlowListPathType::Spline;
			}
			const UFlowListScrollBar* Bar = Cast<UFlowListScrollBar>(Widget);
			return Bar && Bar->PathShape == EFlowListScrollBarPath::Spline;
		}

		const TArray<FVector2D>* GetPoints(const UWidget* Widget)
		{
			if (const UFlowList* List = Cast<UFlowList>(Widget))
			{
				return &List->ListLayout.SplinePoints;
			}
			const UFlowListScrollBar* Bar = Cast<UFlowListScrollBar>(Widget);
			return Bar ? &Bar->SplinePoints : nullptr;
		}

		/** Whether it has a focus to show and move along the curve: a list does, a scroll bar doesn't. */
		bool HasFocus(const UWidget* Widget) { return Widget && Widget->IsA<UFlowList>(); }

		float GetFocus(const UWidget* Widget)
		{
			const UFlowList* List = Cast<UFlowList>(Widget);
			return List ? FMath::Clamp(List->ListLayout.SplineFocus, 0.f, 1.f) : 0.f;
		}

		/** Where the curve is moved to in the widget: a list's Focus Offset. */
		FVector2D GetOffset(const UWidget* Widget)
		{
			const UFlowList* List = Cast<UFlowList>(Widget);
			return List ? List->ListLayout.FocusOffset : FVector2D::ZeroVector;
		}

		/** The property the points are saved under, for telling a Details panel. */
		FProperty* GetProperty(const UWidget* Widget)
		{
			if (Widget && Widget->IsA<UFlowList>())
			{
				return FindFProperty<FProperty>(UFlowList::StaticClass(), GET_MEMBER_NAME_CHECKED(UFlowList, ListLayout));
			}
			return FindFProperty<FProperty>(UFlowListScrollBar::StaticClass(), GET_MEMBER_NAME_CHECKED(UFlowListScrollBar, SplinePoints));
		}

		/** Writes the points (and a list's focus). A preview is drawn again at once. */
		void Write(UWidget* Widget, const TArray<FVector2D>& Points, float Focus, bool bPreview)
		{
			if (UFlowList* List = Cast<UFlowList>(Widget))
			{
				if (bPreview)
				{
					// Through the setter: the preview's Slate list lays its items along the new curve at once.
					FFlowListLayout Layout = List->ListLayout;
					Layout.SplinePoints = Points;
					Layout.SplineFocus = Focus;
					List->SetListLayout(Layout);
				}
				else
				{
					List->ListLayout.SplinePoints = Points;
					List->ListLayout.SplineFocus = Focus;
				}
			}
			else if (UFlowListScrollBar* Bar = Cast<UFlowListScrollBar>(Widget))
			{
				Bar->SplinePoints = Points;
				if (bPreview)
				{
					Bar->RefreshBar();
				}
			}
		}
	}

	enum class EHit : uint8
	{
		None,
		Focus,
		Point,
		Curve
	};

	struct FHit
	{
		EHit Kind = EHit::None;

		/** The point, or - on the curve - the point the piece under the mouse starts from. */
		int32 Index = INDEX_NONE;

		/** On the curve: where it was hit, as a fraction of the list. */
		FVector2D Fraction = FVector2D::ZeroVector;
	};

	enum class EDrag : uint8
	{
		None,
		Points,
		Focus,
		Marquee
	};

	/**
	 * The layer over a Flow List whose Spline path is being edited on the designer's canvas. It draws the curve items are
	 * laid along (a dark line under a light one), its points (the first a square, so the way the path runs shows), and the
	 * focus marker - the ring where the focused item sits - and takes the mouse and keyboard:
	 *
	 *   Select tool (V)
	 *     click a point; Shift adds or removes; drag on empty space to box-select
	 *     drag to move what is selected - snapping to the other points' lines and the list's edges and middle (hold Ctrl to
	 *       turn snapping off); Shift keeps the move straight or at 45 degrees
	 *     drag the focus marker along the curve to move the focus (Spline Focus); Shift snaps it to quarters
	 *     double-click the curve to add a point there, without changing its shape; double-click empty space to finish
	 *   Pen tool (P)
	 *     click to add a point after the selected end of the path - or the end nearer the click - and drag as you click to
	 *       place it; Shift keeps the new piece to 45 degrees
	 *     click the curve to add a point on it; click a point to select it
	 *   Keys: Delete / Backspace removes the selected points (two always stay), arrows nudge them (Shift: 10), Ctrl+A selects
	 *     all, Escape lets go of the selection then finishes, Enter finishes.
	 *
	 * Every press that changes the path is one undo step; the change goes to the Widget Blueprint's list and the designer's
	 * preview at once, so the items follow the new curve as it is dragged.
	 */
	class SFlowListPathCanvas : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFlowListPathCanvas) {}
			SLATE_ARGUMENT(FWidgetReference, Widget)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Reference = InArgs._Widget;
			if (const UWidget* Template = Reference.GetTemplate())
			{
				ListName = Template->GetFName();
			}
			SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() { return IsEditing() ? EVisibility::Visible : EVisibility::Collapsed; }));
		}

		virtual ~SFlowListPathCanvas() override
		{
			Transaction.Reset();
		}

		/** The toolbar the designer shows above the list: Edit Path, or the tools while editing. */
		TSharedRef<SWidget> MakeToolbar();

		// SWidget
		virtual FVector2D ComputeDesiredSize(float) const override
		{
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
		// The list
		// ---------------------------------------------------------------------------------------------

		UWidget* GetPreview() const { return SplineTarget::IsSupported(Reference.GetPreview()) ? Reference.GetPreview() : nullptr; }
		UWidget* GetTemplate() const { return SplineTarget::IsSupported(Reference.GetTemplate()) ? Reference.GetTemplate() : nullptr; }
		FState& State() const { return FlowListPathEditing::GetState(ListName); }

		const UWidget* GetShown() const
		{
			const UWidget* Preview = GetPreview();
			return Preview ? Preview : GetTemplate();
		}

		bool IsSpline() const { return SplineTarget::IsSplineMode(GetTemplate()); }
		bool IsFlowList() const { return Cast<UFlowList>(GetTemplate()) != nullptr; }
		bool HasFocus() const { return SplineTarget::HasFocus(GetTemplate()); }

		bool IsEditing() const { return !ListName.IsNone() && State().bEditing && IsSpline(); }

		/** The path's points - or, while it has fewer than two, the line across the middle the list lays items along then. */
		TArray<FVector2D> GetPoints() const
		{
			const TArray<FVector2D>* Points = SplineTarget::GetPoints(GetShown());
			if (Points && Points->Num() >= MinPoints)
			{
				return *Points;
			}
			return { FVector2D(0.0, 0.5), FVector2D(1.0, 0.5) };
		}

		float GetFocus() const
		{
			return SplineTarget::GetFocus(GetShown());
		}

		/** The selection, without anything an undo or a delete has left pointing nowhere. */
		void PruneSelection() const
		{
			const int32 Count = GetPoints().Num();
			State().Selection.RemoveAll([Count](int32 Index) { return Index < 0 || Index >= Count; });
		}

		bool IsSelected(int32 Index) const { return State().Selection.Contains(Index); }

		void SelectOnly(int32 Index)
		{
			State().Selection.Reset();
			if (Index != INDEX_NONE)
			{
				State().Selection.Add(Index);
			}
		}

		// ---------------------------------------------------------------------------------------------
		// Where things are: fractions of the list, its local space, the screen, and this layer
		// ---------------------------------------------------------------------------------------------

		bool UpdateTarget()
		{
			bHasTarget = false;
			UWidget* Preview = GetPreview();
			const TSharedPtr<SWidget> SlateWidget = Preview ? Preview->GetCachedWidget() : nullptr;
			if (!SlateWidget.IsValid())
			{
				return false;
			}
			TargetGeometry = SlateWidget->GetTickSpaceGeometry();
			TargetSize = TargetGeometry.GetLocalSize();
			if (TargetSize.X <= 0.0 || TargetSize.Y <= 0.0)
			{
				return false;
			}
			FocusOffset = SplineTarget::GetOffset(Preview);
			bHasTarget = true;
			return true;
		}

		FVector2D GetCanvasSize() const
		{
			const UWidget* Preview = GetPreview();
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

		/** The list's local space: where the list lays items for a point at a fraction of its size - Focus Offset added. */
		FVector2D FractionToLocal(const FVector2D& Fraction) const { return Fraction * TargetSize + FocusOffset; }

		FVector2D LocalToCanvas(const FVector2D& Local) const
		{
			return GetTickSpaceGeometry().AbsoluteToLocal(TargetGeometry.LocalToAbsolute(Local));
		}

		FVector2D FractionToCanvas(const FVector2D& Fraction) const { return LocalToCanvas(FractionToLocal(Fraction)); }

		FVector2D ScreenToFraction(const FVector2D& Screen) const
		{
			const FVector2D Local = TargetGeometry.AbsoluteToLocal(Screen) - FocusOffset;
			return FVector2D(TargetSize.X > 0.0 ? Local.X / TargetSize.X : 0.0, TargetSize.Y > 0.0 ? Local.Y / TargetSize.Y : 0.0);
		}

		/** Canvas units per fraction, each way - for snapping distances measured on screen. */
		FVector2D CanvasPerFraction() const
		{
			const FVector2D Origin = FractionToCanvas(FVector2D::ZeroVector);
			return FVector2D((FractionToCanvas(FVector2D(1.0, 0.0)) - Origin).Size(), (FractionToCanvas(FVector2D(0.0, 1.0)) - Origin).Size());
		}

		/** The curve the list lays items along, in its local space (Focus Offset added). */
		void BuildLocalCurve(const TArray<FVector2D>& Points, TArray<FVector2f>& Out) const
		{
			FlowListTypes::BuildSplineCurve(Points, ToF(TargetSize), Out);
			for (FVector2f& Point : Out)
			{
				Point += ToF(FocusOffset);
			}
		}

		/** Where the focus sits on the curve, in its local space. */
		FVector2D GetFocusLocal(const TArray<FVector2f>& LocalCurve) const
		{
			float Total = 0.f;
			for (int32 Index = 1; Index < LocalCurve.Num(); ++Index)
			{
				Total += FVector2f::Distance(LocalCurve[Index - 1], LocalCurve[Index]);
			}
			return ToD(PointAlong(LocalCurve, GetFocus() * Total));
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
			if (UWidget* Template = GetTemplate())
			{
				Template->Modify();
			}
		}

		void Commit(const TArray<FVector2D>& Points, float Focus)
		{
			bChanged = true;
			UWidget* Template = GetTemplate();
			UWidget* Preview = GetPreview();
			if (Template)
			{
				SplineTarget::Write(Template, Points, Focus, /*bPreview=*/false);
			}
			if (Preview && Preview != Template)
			{
				SplineTarget::Write(Preview, Points, Focus, /*bPreview=*/true);
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
				if (UWidget* Template = GetTemplate())
				{
					Template->MarkPackageDirty();
					if (UWidgetBlueprint* Blueprint = Template->GetTypedOuter<UWidgetBlueprint>())
					{
						Blueprint->Status = BS_Dirty;
					}

					// So a Details panel showing it shows the new points - added and removed ones too.
					if (FProperty* PointsProperty = SplineTarget::GetProperty(Template))
					{
						FPropertyChangedEvent Event(PointsProperty, EPropertyChangeType::ValueSet);
						FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Template, Event);
					}
				}
			}
			bChanged = false;
		}

		/** One change, one undo step: Edit changes a copy of the points and focus and says whether it changed anything. */
		void EditOnce(const FText& Description, TFunctionRef<bool(TArray<FVector2D>&, float&)> Edit)
		{
			TArray<FVector2D> Points = GetPoints();
			float Focus = GetFocus();
			if (!Edit(Points, Focus))
			{
				return;
			}
			BeginEdit(Description);
			Commit(Points, Focus);
			EndEdit();
		}

		void FinishEditing()
		{
			EndEdit();
			DragKind = EDrag::None;
			FlowListPathEditing::SetEditing(GetTemplate(), false);
			FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
		}

		/** A point where the curve was hit, after the point its piece starts from: the curve keeps its shape. */
		int32 InsertOnCurve(const FHit& Hit)
		{
			int32 NewIndex = INDEX_NONE;
			EditOnce(LOCTEXT("AddPoint", "Add Path Point"), [&](TArray<FVector2D>& Points, float&)
			{
				if (Hit.Index < 0 || Hit.Index >= Points.Num() - 1)
				{
					return false;
				}
				NewIndex = Hit.Index + 1;
				Points.Insert(Hit.Fraction, NewIndex);
				return true;
			});
			if (NewIndex != INDEX_NONE)
			{
				SelectOnly(NewIndex);
			}
			return NewIndex;
		}

		void DeleteSelection()
		{
			PruneSelection();
			TArray<int32> Doomed = State().Selection;
			if (Doomed.Num() == 0)
			{
				return;
			}
			// Back to front, so each removal leaves the indices still to come where they were.
			Doomed.Sort([](int32 A, int32 B) { return A > B; });
			EditOnce(LOCTEXT("DeletePoints", "Delete Path Points"), [&](TArray<FVector2D>& Points, float&)
			{
				bool bAny = false;
				for (const int32 Index : Doomed)
				{
					if (Points.Num() <= MinPoints)
					{
						break;
					}
					if (Points.IsValidIndex(Index))
					{
						Points.RemoveAt(Index);
						bAny = true;
					}
				}
				return bAny;
			});
			State().Selection.Reset();
		}

		void Nudge(const FVector2D& Pixels)
		{
			PruneSelection();
			if (State().Selection.Num() == 0 || !bHasTarget)
			{
				return;
			}
			const FVector2D Delta(Pixels.X / TargetSize.X, Pixels.Y / TargetSize.Y);
			const TArray<int32> Selection = State().Selection;
			EditOnce(LOCTEXT("NudgePoints", "Nudge Path Points"), [&](TArray<FVector2D>& Points, float&)
			{
				for (const int32 Index : Selection)
				{
					if (Points.IsValidIndex(Index))
					{
						Points[Index] += Delta;
					}
				}
				return true;
			});
		}

		void Reverse()
		{
			EditOnce(LOCTEXT("ReversePath", "Reverse Path"), [](TArray<FVector2D>& Points, float& Focus)
			{
				Algo::Reverse(Points);

				// The focus stays where it is on the curve, now measured from the other end.
				Focus = 1.f - Focus;
				return true;
			});
			State().Selection.Reset();
		}

		// ---------------------------------------------------------------------------------------------
		// Dragging
		// ---------------------------------------------------------------------------------------------

		void DragPoints(const FVector2D& Screen, bool bConstrain, bool bNoSnap);
		void DragFocus(const FVector2D& Screen, bool bSnapToQuarters);
		void DragMarquee(const FVector2D& Canvas);
		FReply PenDown(const FHit& Hit, const FVector2D& Screen, bool bConstrain, FReply Reply);

		FWidgetReference Reference;
		FName ListName;

		bool bHasTarget = false;
		FGeometry TargetGeometry;
		FVector2D TargetSize = FVector2D(1.0, 1.0);
		FVector2D FocusOffset = FVector2D::ZeroVector;
		FVector2D LastCanvasSize = FVector2D::ZeroVector;

		FHit Hover;
		bool bMouseInside = false;
		FVector2D LastMouseCanvas = FVector2D::ZeroVector;

		EDrag DragKind = EDrag::None;
		bool bDragStarted = false;
		bool bClickedEmpty = false;
		FVector2D MouseDownScreen = FVector2D::ZeroVector;
		FVector2D MarqueeStart = FVector2D::ZeroVector;
		int32 DragPoint = INDEX_NONE;
		TArray<FVector2D> DragStartPoints;
		float DragStartFocus = 0.5f;
		TArray<int32> MarqueeBase;
		TOptional<double> GuideX;
		TOptional<double> GuideY;

		TUniquePtr<FScopedTransaction> Transaction;
		bool bChanged = false;
	};

	// ---------------------------------------------------------------------------------------------
	// Hit testing
	// ---------------------------------------------------------------------------------------------

	FHit SFlowListPathCanvas::HitTest(const FVector2D& Canvas) const
	{
		FHit Hit;
		if (!bHasTarget)
		{
			return Hit;
		}
		const TArray<FVector2D> Points = GetPoints();
		TArray<FVector2f> LocalCurve;
		BuildLocalCurve(Points, LocalCurve);

		// The focus marker sits on the curve, over its points: it comes first.
		if (HasFocus() && (LocalToCanvas(GetFocusLocal(LocalCurve)) - Canvas).Size() <= FocusMarkerSize * 0.5f + 2.f)
		{
			Hit.Kind = EHit::Focus;
			return Hit;
		}

		float Best = Palette::HitRadius;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const float Distance = static_cast<float>((FractionToCanvas(Points[Index]) - Canvas).Size());
			if (Distance < Best)
			{
				Best = Distance;
				Hit.Kind = EHit::Point;
				Hit.Index = Index;
			}
		}
		if (Hit.Kind != EHit::None)
		{
			return Hit;
		}

		// The curve, measured on the canvas: which piece between two points it is, and where.
		TArray<FVector2f> CanvasCurve;
		CanvasCurve.Reserve(LocalCurve.Num());
		for (const FVector2f& Local : LocalCurve)
		{
			CanvasCurve.Add(ToF(LocalToCanvas(ToD(Local))));
		}
		const FProjection OnCurve = Project(CanvasCurve, ToF(Canvas));
		if (OnCurve.Piece != INDEX_NONE && OnCurve.Distance <= Palette::HitRadius * 0.8f)
		{
			Hit.Kind = EHit::Curve;
			Hit.Index = FMath::Clamp(OnCurve.Piece / FlowListTypes::SplineStepsPerSpan, 0, FMath::Max(Points.Num() - 2, 0));
			const FVector2f Local = FMath::Lerp(LocalCurve[OnCurve.Piece], LocalCurve[FMath::Min(OnCurve.Piece + 1, LocalCurve.Num() - 1)],
				(OnCurve.Position - CanvasCurve[OnCurve.Piece]).Size() / FMath::Max(FVector2f::Distance(CanvasCurve[OnCurve.Piece], CanvasCurve[FMath::Min(OnCurve.Piece + 1, CanvasCurve.Num() - 1)]), 1e-6f));
			const FVector2D Fraction = ToD(Local) - FocusOffset;
			Hit.Fraction = FVector2D(Fraction.X / TargetSize.X, Fraction.Y / TargetSize.Y);
		}
		return Hit;
	}

	// ---------------------------------------------------------------------------------------------
	// Painting
	// ---------------------------------------------------------------------------------------------

	int32 SFlowListPathCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
	{
		if (!IsEditing() || !bHasTarget)
		{
			return LayerId;
		}
		PruneSelection();
		const TArray<FVector2D> Points = GetPoints();

		auto DrawLine = [&](const TArray<FVector2f>& Line, const FLinearColor& Color, float Thickness, int32 Layer)
		{
			if (Line.Num() >= 2)
			{
				FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, Color, /*bAntialias=*/true, Thickness);
			}
		};
		auto DrawDot = [&](const FVector2D& Center, float Size, const FSlateBrush* Brush, const FLinearColor& Color, int32 Layer)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, Layer,
				AllottedGeometry.ToPaintGeometry(FVector2f(Size, Size), FSlateLayoutTransform(ToF(Center) - FVector2f(Size, Size) * 0.5f)),
				Brush, ESlateDrawEffect::None, Color);
		};

		// The curve: a dark line under a light one, so it reads over anything.
		TArray<FVector2f> LocalCurve;
		BuildLocalCurve(Points, LocalCurve);
		TArray<FVector2f> CanvasCurve;
		CanvasCurve.Reserve(LocalCurve.Num());
		for (const FVector2f& Local : LocalCurve)
		{
			CanvasCurve.Add(ToF(LocalToCanvas(ToD(Local))));
		}
		DrawLine(CanvasCurve, Palette::PathUnderlay, Palette::PathThickness + 2.f, LayerId);
		DrawLine(CanvasCurve, Palette::PathLine, Palette::PathThickness, LayerId + 1);

		// The piece under the mouse, where a double-click (or the Pen) adds a point.
		if (Hover.Kind == EHit::Curve && DragKind == EDrag::None)
		{
			const int32 First = Hover.Index * FlowListTypes::SplineStepsPerSpan;
			TArray<FVector2f> Piece;
			for (int32 Index = First; Index <= First + FlowListTypes::SplineStepsPerSpan && Index < CanvasCurve.Num(); ++Index)
			{
				Piece.Add(CanvasCurve[Index]);
			}
			DrawLine(Piece, Palette::PathHovered, Palette::PathThickness + 1.f, LayerId + 1);
			DrawDot(FractionToCanvas(Hover.Fraction), Palette::HandleSize, CircleBrush(), Palette::PathHovered, LayerId + 2);
		}

		// Snap guides, across the list and a little past it.
		if (GuideX.IsSet())
		{
			DrawLine({ ToF(FractionToCanvas(FVector2D(GuideX.GetValue(), -0.2))), ToF(FractionToCanvas(FVector2D(GuideX.GetValue(), 1.2))) }, Palette::SnapGuide, 1.f, LayerId + 1);
		}
		if (GuideY.IsSet())
		{
			DrawLine({ ToF(FractionToCanvas(FVector2D(-0.2, GuideY.GetValue()))), ToF(FractionToCanvas(FVector2D(1.2, GuideY.GetValue()))) }, Palette::SnapGuide, 1.f, LayerId + 1);
		}

		// The Pen's next piece, from the end it carries on from to the mouse.
		if (State().Tool == ETool::Pen && bMouseInside && DragKind == EDrag::None && Hover.Kind == EHit::None && Points.Num() > 0)
		{
			const FVector2D Mouse = ScreenToFraction(GetTickSpaceGeometry().LocalToAbsolute(LastMouseCanvas));
			bool bPrepend = false;
			if (State().Selection.Num() == 1 && State().Selection[0] == 0)
			{
				bPrepend = true;
			}
			else if (State().Selection.Num() != 1 || State().Selection[0] != Points.Num() - 1)
			{
				bPrepend = FVector2D::DistSquared(Mouse, Points[0]) < FVector2D::DistSquared(Mouse, Points.Last());
			}
			DrawLine({ ToF(FractionToCanvas(bPrepend ? Points[0] : Points.Last())), ToF(LastMouseCanvas) }, Palette::PenPreview, 1.f, LayerId + 1);
		}

		// Points: the first a square, the rest circles; filled orange when selected.
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FVector2D Center = FractionToCanvas(Points[Index]);
			const bool bHovered = Hover.Kind == EHit::Point && Hover.Index == Index;
			const float Size = Palette::VertexSize + (bHovered ? 2.f : 0.f);
			const FSlateBrush* Brush = Index == 0 ? SquareBrush() : CircleBrush();
			DrawDot(Center, Size + 2.f, Brush, Palette::VertexOutline, LayerId + 3);
			DrawDot(Center, Size, Brush, IsSelected(Index) ? Palette::Selected : Palette::VertexFill, LayerId + 4);
		}

		// The focus marker: a ring where the focused item sits.
		if (HasFocus())
		{
			const FVector2D Focus = LocalToCanvas(GetFocusLocal(LocalCurve));
			const bool bHot = (Hover.Kind == EHit::Focus && DragKind == EDrag::None) || DragKind == EDrag::Focus;
			const float Size = FocusMarkerSize + (bHot ? 3.f : 0.f);
			DrawDot(Focus, Size + 2.f, CircleBrush(), Palette::VertexOutline, LayerId + 5);
			DrawDot(Focus, Size, CircleBrush(), Palette::CloseRing, LayerId + 6);
			DrawDot(Focus, Size * 0.45f, CircleBrush(), Palette::VertexOutline, LayerId + 7);
		}

		// The selection box.
		if (DragKind == EDrag::Marquee && bDragStarted)
		{
			const FVector2D Min(FMath::Min(MarqueeStart.X, LastMouseCanvas.X), FMath::Min(MarqueeStart.Y, LastMouseCanvas.Y));
			const FVector2D Max(FMath::Max(MarqueeStart.X, LastMouseCanvas.X), FMath::Max(MarqueeStart.Y, LastMouseCanvas.Y));
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 8,
				AllottedGeometry.ToPaintGeometry(ToF(Max - Min), FSlateLayoutTransform(ToF(Min))), SquareBrush(), ESlateDrawEffect::None, Palette::MarqueeFill);
			DrawLine({ ToF(Min), ToF(FVector2D(Max.X, Min.Y)), ToF(Max), ToF(FVector2D(Min.X, Max.Y)), ToF(Min) }, Palette::MarqueeOutline, 1.f, LayerId + 9);
		}
		return LayerId + 9;
	}

	// ---------------------------------------------------------------------------------------------
	// Mouse
	// ---------------------------------------------------------------------------------------------

	FReply SFlowListPathCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (!IsEditing() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !UpdateTarget())
		{
			return FReply::Unhandled();
		}

		const FVector2D Screen = MouseEvent.GetScreenSpacePosition();
		const FVector2D Canvas = MyGeometry.AbsoluteToLocal(Screen);
		LastMouseCanvas = Canvas;
		MouseDownScreen = Screen;
		bDragStarted = false;
		bClickedEmpty = false;
		DragStartPoints = GetPoints();
		DragStartFocus = GetFocus();
		GuideX.Reset();
		GuideY.Reset();
		PruneSelection();

		FReply Reply = FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
		const FHit Hit = HitTest(Canvas);
		const bool bShift = MouseEvent.IsShiftDown();

		if (Hit.Kind == EHit::Focus)
		{
			DragKind = EDrag::Focus;
			return Reply;
		}
		if (State().Tool == ETool::Pen)
		{
			return PenDown(Hit, Screen, bShift, Reply);
		}

		switch (Hit.Kind)
		{
		case EHit::Point:
			if (bShift)
			{
				if (IsSelected(Hit.Index))
				{
					State().Selection.Remove(Hit.Index);
					DragKind = EDrag::None;
					return Reply;
				}
				State().Selection.Add(Hit.Index);
			}
			else if (!IsSelected(Hit.Index))
			{
				SelectOnly(Hit.Index);
			}
			DragKind = EDrag::Points;
			DragPoint = Hit.Index;
			return Reply;

		case EHit::Curve:
			// A piece of the curve: the two points either side, to move together.
			if (!bShift)
			{
				State().Selection.Reset();
			}
			State().Selection.AddUnique(Hit.Index);
			State().Selection.AddUnique(Hit.Index + 1);
			DragKind = EDrag::Points;
			DragPoint = Hit.Index;
			return Reply;

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

	FReply SFlowListPathCanvas::PenDown(const FHit& Hit, const FVector2D& Screen, bool bConstrain, FReply Reply)
	{
		if (Hit.Kind == EHit::Point)
		{
			SelectOnly(Hit.Index);
			DragKind = EDrag::None;
			return Reply;
		}
		if (Hit.Kind == EHit::Curve)
		{
			InsertOnCurve(Hit);
			DragKind = EDrag::None;
			return Reply;
		}

		// Empty space: a point after the selected end - or the end nearer the click.
		TArray<FVector2D> Points = DragStartPoints;
		FVector2D Position = ScreenToFraction(Screen);
		bool bPrepend = false;
		if (State().Selection.Num() == 1 && State().Selection[0] == 0)
		{
			bPrepend = true;
		}
		else if (State().Selection.Num() != 1 || State().Selection[0] != Points.Num() - 1)
		{
			bPrepend = FVector2D::DistSquared(Position, Points[0]) < FVector2D::DistSquared(Position, Points.Last());
		}
		if (bConstrain)
		{
			const FVector2D From = bPrepend ? Points[0] : Points.Last();
			Position = From + SnapTo45(Position - From, TargetSize);
		}
		const int32 NewIndex = bPrepend ? 0 : Points.Num();
		Points.Insert(Position, NewIndex);

		BeginEdit(LOCTEXT("AddPointPen", "Add Path Point"));
		Commit(Points, DragStartFocus);
		SelectOnly(NewIndex);

		// Dragging on from here places the new point; the edit is one undo step with it.
		DragStartPoints = Points;
		DragKind = EDrag::Points;
		DragPoint = NewIndex;
		return Reply;
	}

	FReply SFlowListPathCanvas::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (!IsEditing() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !UpdateTarget())
		{
			return FReply::Unhandled();
		}
		EndEdit();
		DragKind = EDrag::None;

		const FHit Hit = HitTest(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
		if (Hit.Kind == EHit::Curve)
		{
			InsertOnCurve(Hit);
		}
		else if (Hit.Kind == EHit::None && State().Tool == ETool::Select)
		{
			FinishEditing();
		}
		return FReply::Handled();
	}

	FReply SFlowListPathCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
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
		case EDrag::Points: DragPoints(Screen, MouseEvent.IsShiftDown(), MouseEvent.IsControlDown()); break;
		case EDrag::Focus: DragFocus(Screen, MouseEvent.IsShiftDown()); break;
		case EDrag::Marquee: DragMarquee(LastMouseCanvas); break;
		default: break;
		}
		return FReply::Handled();
	}

	FReply SFlowListPathCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
	{
		if (!HasMouseCapture() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}

		// A plain click on empty space well outside the list finishes, as clicking off a path does in the vector tools.
		if (bClickedEmpty && !bDragStarted && State().Tool == ETool::Select && bHasTarget)
		{
			const FVector2D Fraction = ScreenToFraction(MouseEvent.GetScreenSpacePosition());
			const FVector2D Slack = FVector2D(Palette::HitRadius, Palette::HitRadius) / CanvasPerFraction().ComponentMax(FVector2D(1e-6, 1e-6));
			FBox2D Bounds(ForceInit);
			Bounds += FVector2D(0.0, 0.0);
			Bounds += FVector2D(1.0, 1.0);
			for (const FVector2D& Point : GetPoints())
			{
				Bounds += Point;
			}
			if (!Bounds.ExpandBy(Slack).IsInside(Fraction))
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

	void SFlowListPathCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
	{
		EndEdit();
		DragKind = EDrag::None;
		bDragStarted = false;
		GuideX.Reset();
		GuideY.Reset();
	}

	FCursorReply SFlowListPathCanvas::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
	{
		if (!IsEditing())
		{
			return FCursorReply::Unhandled();
		}
		if (State().Tool == ETool::Pen && Hover.Kind == EHit::None)
		{
			return FCursorReply::Cursor(EMouseCursor::Crosshairs);
		}
		if (Hover.Kind != EHit::None || DragKind == EDrag::Points || DragKind == EDrag::Focus)
		{
			return FCursorReply::Cursor(EMouseCursor::Hand);
		}
		return FCursorReply::Cursor(EMouseCursor::Default);
	}

	// ---------------------------------------------------------------------------------------------
	// Drags
	// ---------------------------------------------------------------------------------------------

	void SFlowListPathCanvas::DragPoints(const FVector2D& Screen, bool bConstrain, bool bNoSnap)
	{
		if (!DragStartPoints.IsValidIndex(DragPoint))
		{
			return;
		}
		FVector2D Delta = ScreenToFraction(Screen) - ScreenToFraction(MouseDownScreen);
		if (bConstrain)
		{
			Delta = SnapTo45(Delta, TargetSize);
		}

		GuideX.Reset();
		GuideY.Reset();
		const TArray<int32>& Selection = State().Selection;
		if (!bNoSnap && !bConstrain)
		{
			// The point under the mouse snaps to the lines of every point that isn't moving, and to the list's edges and middle;
			// everything selected moves with it.
			const FVector2D Lead = DragStartPoints[DragPoint] + Delta;
			const FVector2D PerFraction = CanvasPerFraction();
			const FVector2D Reach(PerFraction.X > 1e-6 ? SnapDistance / PerFraction.X : 0.0, PerFraction.Y > 1e-6 ? SnapDistance / PerFraction.Y : 0.0);
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
			for (int32 Index = 0; Index < DragStartPoints.Num(); ++Index)
			{
				if (!Selection.Contains(Index))
				{
					Consider(DragStartPoints[Index]);
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

		TArray<FVector2D> Points = DragStartPoints;
		for (const int32 Index : Selection)
		{
			if (Points.IsValidIndex(Index))
			{
				Points[Index] += Delta;
			}
		}
		BeginEdit(LOCTEXT("MovePoints", "Move Path Points"));
		Commit(Points, DragStartFocus);
	}

	void SFlowListPathCanvas::DragFocus(const FVector2D& Screen, bool bSnapToQuarters)
	{
		TArray<FVector2f> LocalCurve;
		BuildLocalCurve(DragStartPoints, LocalCurve);
		float Total = 0.f;
		for (int32 Index = 1; Index < LocalCurve.Num(); ++Index)
		{
			Total += FVector2f::Distance(LocalCurve[Index - 1], LocalCurve[Index]);
		}
		if (Total <= 1e-3f)
		{
			return;
		}
		const FProjection OnCurve = Project(LocalCurve, ToF(TargetGeometry.AbsoluteToLocal(Screen)));
		float Focus = FMath::Clamp(OnCurve.Along / Total, 0.f, 1.f);
		if (bSnapToQuarters)
		{
			Focus = FMath::RoundToFloat(Focus * 4.f) / 4.f;
		}
		BeginEdit(LOCTEXT("MoveFocus", "Move Spline Focus"));
		Commit(DragStartPoints, Focus);
	}

	void SFlowListPathCanvas::DragMarquee(const FVector2D& Canvas)
	{
		const FBox2D Box(FVector2D(FMath::Min(MarqueeStart.X, Canvas.X), FMath::Min(MarqueeStart.Y, Canvas.Y)),
			FVector2D(FMath::Max(MarqueeStart.X, Canvas.X), FMath::Max(MarqueeStart.Y, Canvas.Y)));
		TArray<int32>& Selection = State().Selection;
		Selection = MarqueeBase;
		const TArray<FVector2D> Points = GetPoints();
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			if (Box.IsInside(FractionToCanvas(Points[Index])))
			{
				Selection.AddUnique(Index);
			}
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Keys
	// ---------------------------------------------------------------------------------------------

	FReply SFlowListPathCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
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
				Commit(DragStartPoints, DragStartFocus);
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
			State().Tool = ETool::Select;
			return FReply::Handled();
		}
		if (Key == EKeys::P && !bCtrl)
		{
			State().Tool = ETool::Pen;
			return FReply::Handled();
		}
		if (Key == EKeys::A && bCtrl)
		{
			State().Selection.Reset();
			for (int32 Index = 0; Index < GetPoints().Num(); ++Index)
			{
				State().Selection.Add(Index);
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
	// Toolbar
	// ---------------------------------------------------------------------------------------------

	TSharedRef<SWidget> SFlowListPathCanvas::MakeToolbar()
	{
		using WidgetShapeEditorUI::FChip;
		using WidgetShapeEditorUI::MakeChip;

		const TWeakPtr<SFlowListPathCanvas> WeakCanvas = StaticCastSharedRef<SFlowListPathCanvas>(AsShared());

		auto Ask = [WeakCanvas](TFunction<bool(const SFlowListPathCanvas&)> Query) -> TFunction<bool()>
		{
			return [WeakCanvas, Query]()
			{
				const TSharedPtr<SFlowListPathCanvas> Canvas = WeakCanvas.Pin();
				return Canvas.IsValid() && Query(*Canvas);
			};
		};
		auto Do = [WeakCanvas](TFunction<void(SFlowListPathCanvas&)> Action) -> TFunction<void()>
		{
			return [WeakCanvas, Action]()
			{
				if (const TSharedPtr<SFlowListPathCanvas> Canvas = WeakCanvas.Pin())
				{
					Action(*Canvas);
				}
			};
		};
		const TFunction<bool()> Editing = Ask([](const SFlowListPathCanvas& Canvas) { return Canvas.IsEditing(); });
		const TFunction<bool()> CanEdit = Ask([](const SFlowListPathCanvas& Canvas) { return Canvas.IsSpline() && !Canvas.IsEditing(); });
		const TFunction<bool()> Previewable = Ask([](const SFlowListPathCanvas& Canvas) { return Canvas.IsFlowList() && !Canvas.IsEditing(); });
		const TFunction<bool()> Selected = Ask([](const SFlowListPathCanvas& Canvas)
		{
			Canvas.PruneSelection();
			return Canvas.State().Selection.Num() > 0 && Canvas.GetPoints().Num() > MinPoints;
		});

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

		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapeEditIcon;
			Chip.Label = LOCTEXT("EditPath", "Edit Path");
			Chip.ToolTip = Tips::EditPathTooltip();
			Chip.OnClick = Do([](SFlowListPathCanvas& Canvas) { FlowListPathEditing::SetEditing(Canvas.GetTemplate(), true); });
			Add(MakeChip(Chip), CanEdit, 0.f);
		}
		Add(Divider(), Ask([](const SFlowListPathCanvas& Canvas) { return Canvas.IsSpline() && Canvas.IsFlowList() && !Canvas.IsEditing(); }), 6.f);

		// A Flow List's entrances and exits, played in the preview - Enter and Exit, or Anim Enter Item and Anim Exit Item.
		{
			FChip Chip;
			Chip.Label = LOCTEXT("PreviewEnter", "Enter");
			Chip.ToolTip = Tips::PreviewEnterTooltip();
			Chip.OnClick = Do([](SFlowListPathCanvas& Canvas) { if (UFlowList* List = Cast<UFlowList>(Canvas.GetPreview())) { List->PreviewEntrances(); } });
			Add(MakeChip(Chip), Previewable, 0.f);
		}
		{
			FChip Chip;
			Chip.Label = LOCTEXT("PreviewExit", "Exit");
			Chip.ToolTip = Tips::PreviewExitTooltip();
			Chip.OnClick = Do([](SFlowListPathCanvas& Canvas) { if (UFlowList* List = Cast<UFlowList>(Canvas.GetPreview())) { List->PreviewExits(); } });
			Add(MakeChip(Chip), Previewable);
		}
		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapeDoneIcon;
			Chip.Label = LOCTEXT("Done", "Done");
			Chip.ToolTip = Tips::DoneTooltip();
			Chip.IsActive = []() { return true; };
			Chip.OnClick = Do([](SFlowListPathCanvas& Canvas) { Canvas.FinishEditing(); });
			Add(MakeChip(Chip), Editing, 0.f);
		}
		Add(Divider(), Editing, 6.f);
		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapeSelectIcon;
			Chip.ToolTip = Tips::SelectToolTooltip();
			Chip.IsActive = Ask([](const SFlowListPathCanvas& Canvas) { return Canvas.State().Tool == ETool::Select; });
			Chip.OnClick = Do([](SFlowListPathCanvas& Canvas) { Canvas.State().Tool = ETool::Select; });
			Add(MakeChip(Chip), Editing, 6.f);
		}
		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapePenIcon;
			Chip.ToolTip = Tips::PenToolTooltip();
			Chip.IsActive = Ask([](const SFlowListPathCanvas& Canvas) { return Canvas.State().Tool == ETool::Pen; });
			Chip.OnClick = Do([](SFlowListPathCanvas& Canvas) { Canvas.State().Tool = ETool::Pen; });
			Add(MakeChip(Chip), Editing);
		}
		Add(Divider(), Editing, 6.f);
		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapeReverseIcon;
			Chip.ToolTip = Tips::ReverseTooltip();
			Chip.OnClick = Do([](SFlowListPathCanvas& Canvas) { Canvas.UpdateTarget(); Canvas.Reverse(); });
			Add(MakeChip(Chip), Editing, 6.f);
		}
		{
			FChip Chip;
			Chip.Icon = FNameTBDStyle::ShapeDeleteIcon;
			Chip.ToolTip = Tips::DeletePointsTooltip();
			Chip.IsEnabled = Selected;
			Chip.OnClick = Do([](SFlowListPathCanvas& Canvas) { Canvas.DeleteSelection(); });
			Add(MakeChip(Chip), Editing);
		}

		// A list always has its previews; a scroll bar shows the toolbar only with a Spline path. It comes and goes with the
		// Type, without reselecting.
		TSharedRef<SWidget> Toolbar = SNew(SBorder)
			.BorderImage(ToolbarBrush())
			.Padding(FMargin(4.f, 3.f))
			[
				Row
			];
		const TFunction<bool()> Shown = Ask([](const SFlowListPathCanvas& Canvas) { return Canvas.IsSpline() || Canvas.IsFlowList(); });
		Toolbar->SetVisibility(TAttribute<EVisibility>::CreateLambda([Shown]() { return Shown() ? EVisibility::Visible : EVisibility::Collapsed; }));
		return Toolbar;
	}
}

namespace FlowListPathEditorImpl
{
	/** A widget placed in a Flow List - one of its items. */
	bool IsFlowListItem(const UWidget* Widget)
	{
		return Widget && Cast<UFlowList>(Widget->GetParent()) != nullptr;
	}

	/** Above a selected item of a Flow List: Enter and Exit, playing just that item's entrance and exit in the preview. */
	TSharedRef<SWidget> MakeItemPreviewToolbar(const FWidgetReference& Reference)
	{
		using WidgetShapeEditorUI::FChip;
		using WidgetShapeEditorUI::MakeChip;

		auto Preview = [Reference](bool bExit)
		{
			UWidget* Item = Reference.GetPreview();
			if (UFlowList* List = Item ? Cast<UFlowList>(Item->GetParent()) : nullptr)
			{
				List->PreviewItem(Item, bExit);
			}
		};

		FChip Enter;
		Enter.Label = LOCTEXT("PreviewItemEnter", "Enter");
		Enter.ToolTip = Tips::PreviewItemEnterTooltip();
		Enter.OnClick = [Preview]() { Preview(false); };

		FChip Exit;
		Exit.Label = LOCTEXT("PreviewItemExit", "Exit");
		Exit.ToolTip = Tips::PreviewItemExitTooltip();
		Exit.OnClick = [Preview]() { Preview(true); };

		return SNew(SBorder)
			.BorderImage(ToolbarBrush())
			.Padding(FMargin(4.f, 3.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeChip(Enter)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(2.f, 0.f, 0.f, 0.f))
				[
					MakeChip(Exit)
				]
			];
	}
}

// =====================================================================================================
// Extension
// =====================================================================================================

FFlowListPathEditorExtension::FFlowListPathEditorExtension()
{
	ExtensionId = FName(TEXT("FlowListPathEditor"));
}

bool FFlowListPathEditorExtension::CanExtendSelection(const TArray<FWidgetReference>& Selection) const
{
	return Selection.Num() == 1 && (FlowListPathEditorImpl::SplineTarget::IsSupported(Selection[0].GetTemplate())
		|| FlowListPathEditorImpl::IsFlowListItem(Selection[0].GetTemplate()));
}

void FFlowListPathEditorExtension::ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements)
{
	using namespace FlowListPathEditorImpl;

	if (Selection.Num() != 1)
	{
		return;
	}

	// An item of a Flow List: just its own Enter and Exit.
	if (!SplineTarget::IsSupported(Selection[0].GetTemplate()))
	{
		if (IsFlowListItem(Selection[0].GetTemplate()))
		{
			// Over its top right corner: an item that is a Custom Shape has its own toolbar centered above it.
			SurfaceElements.Add(MakeShared<FDesignerSurfaceElement>(MakeItemPreviewToolbar(Selection[0]), EExtensionLayoutLocation::TopRight, FVector2D(0.0, -ToolbarLift), FVector2D(1.0, 1.0)));
		}
		return;
	}

	// The layer first and the toolbar after it, so the toolbar is drawn - and clicked - on top where they overlap.
	const TSharedRef<SFlowListPathCanvas> Canvas = SNew(SFlowListPathCanvas).Widget(Selection[0]);
	SurfaceElements.Add(MakeShared<FDesignerSurfaceElement>(Canvas, EExtensionLayoutLocation::TopLeft, FVector2D(-OverlayMargin, -OverlayMargin), FVector2D(0.0, 0.0)));
	SurfaceElements.Add(MakeShared<FDesignerSurfaceElement>(Canvas->MakeToolbar(), EExtensionLayoutLocation::TopCenter, FVector2D(0.0, -ToolbarLift), FVector2D(0.5, 1.0)));
}

TSharedRef<FDesignerExtension> FFlowListPathEditorFactory::CreateDesignerExtension() const
{
	return MakeShared<FFlowListPathEditorExtension>();
}

#undef LOCTEXT_NAMESPACE
