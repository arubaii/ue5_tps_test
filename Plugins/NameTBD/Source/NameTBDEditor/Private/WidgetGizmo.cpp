// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetGizmo.h"

#include "DesignerCursorOverride.h"
#include "NameTBDColors.h"
#include "SWidgetAnimTimeline.h"
#include "WidgetAnimPanelSource.h"
#include "WidgetAnimScripting.h"
#include "WidgetGizmoSettings.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GenericPlatform/ICursor.h"
#include "Layout/WidgetPath.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "ScopedTransaction.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "WidgetGizmo"

namespace WidgetGizmoImpl
{
	namespace Palette = NameTBD::Colors::WidgetGizmo;

	/** How far the mouse must move, in the designer's pixels, before a press on a handle becomes a drag. */
	constexpr double DragThreshold = 2.0;

	/** The most selected widgets that get a gizmo at once. */
	constexpr int32 MaxTargets = 8;

	/** The most characters a typed value holds. */
	constexpr int32 MaxTypedLength = 12;

	/** What the gizmo's handles do - switched with the Grab / Scale / Rotate / Shear keys and the chips above the widget. */
	enum class EMode : uint8
	{
		Move,
		Scale,
		Rotate,
		Shear
	};

	/** One mode and one Hide for every designer, as the level editor's gizmo mode is one for every viewport. Not saved. */
	EMode& GizmoMode()
	{
		static EMode Mode = EMode::Move;
		return Mode;
	}

	bool& GizmoHidden()
	{
		static bool bHidden = false;
		return bHidden;
	}

	/** What part of the gizmo is under the mouse, or pressed. */
	enum class EHandle : uint8
	{
		None,
		Chip,
		Pivot,
		MoveX,
		MoveY,
		MoveXY,
		Rotate,
		Scale,
		Shear
	};

	/** The chips above the widget, left to right. Key is the Auto Key badge - shown, never clicked. */
	enum class EChip : uint8
	{
		Move,
		Scale,
		Rotate,
		Shear,
		Hide,
		Key
	};

	/** What a key typed in the designer asks of the gizmo. */
	enum class EGizmoKey : uint8
	{
		Grab,
		Scale,
		Rotate,
		Shear,
		Hide
	};

	/** What a typed session (see FGizmoLayer::FSession) sets. */
	enum class EOp : uint8
	{
		None,
		Grab,
		Scale,
		Rotate,
		Shear
	};

	/** What a right click on a handle offers to put back to its default. */
	enum class EReset : uint8
	{
		Pivot,
		Position,
		PositionX,
		PositionY,
		Scale,
		Rotation,
		Shear,
		All
	};

	struct FHit
	{
		EHandle Handle = EHandle::None;

		/** Which selected widget's gizmo - an index into the layer's targets. */
		int32 Target = INDEX_NONE;

		/** Scale: the box point (0-3 corners from the top left, clockwise; 4-7 edge middles: top, right, bottom, left). Shear: the
		 * edge (0 top, 1 right, 2 bottom, 3 left). Chip: its EChip. */
		int32 Index = INDEX_NONE;

		bool IsValid() const { return Handle != EHandle::None; }
		bool operator==(const FHit& Other) const { return Handle == Other.Handle && Target == Other.Target && Index == Other.Index; }
		bool operator!=(const FHit& Other) const { return !(*this == Other); }
	};

	/** Where box point Index (see FHit::Index) is in the widget's box, as a fraction of its size. */
	FVector2D BoxFraction(int32 Index)
	{
		static const FVector2D Fractions[8] =
		{
			FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(1.0, 1.0), FVector2D(0.0, 1.0),
			FVector2D(0.5, 0.0), FVector2D(1.0, 0.5), FVector2D(0.5, 1.0), FVector2D(0.0, 0.5)
		};
		return Fractions[FMath::Clamp(Index, 0, 7)];
	}

	double RoundTo(double Value, double Step)
	{
		return FMath::RoundToDouble(Value / Step) * Step;
	}

	/** How far along G the move D goes, in steps of G. Zero when G has no length. */
	double Project(const FVector2D& D, const FVector2D& G)
	{
		const double LengthSquared = G.SizeSquared();
		return LengthSquared > 1.e-12 ? FVector2D::DotProduct(D, G) / LengthSquared : 0.0;
	}

	/** The (a, b) for which a * ColumnA + b * ColumnB = Wanted; each projected on its own when the two are parallel. */
	FVector2D SolveTwo(const FVector2D& ColumnA, const FVector2D& ColumnB, const FVector2D& Wanted)
	{
		const double Determinant = ColumnA.X * ColumnB.Y - ColumnB.X * ColumnA.Y;
		if (FMath::Abs(Determinant) < 1.e-9)
		{
			return FVector2D(Project(Wanted, ColumnA), Project(Wanted, ColumnB));
		}
		return FVector2D(
			(ColumnB.Y * Wanted.X - ColumnB.X * Wanted.Y) / Determinant,
			(-ColumnA.Y * Wanted.X + ColumnA.X * Wanted.Y) / Determinant);
	}

	double DistanceToSegment(const FVector2D& Point, const FVector2D& From, const FVector2D& To)
	{
		const FVector2D Along = To - From;
		const double Fraction = FMath::Clamp(Project(Point - From, Along), 0.0, 1.0);
		return FVector2D::Distance(Point, From + Along * Fraction);
	}

	/** Whether Point is inside a rectangle of half extents Half about Center, turned by Angle (radians). */
	bool IsInTurnedRect(const FVector2D& Point, const FVector2D& Center, double Angle, const FVector2D& Half)
	{
		const FVector2D Offset = Point - Center;
		const double Cos = FMath::Cos(Angle);
		const double Sin = FMath::Sin(Angle);
		const double Along = Offset.X * Cos + Offset.Y * Sin;
		const double Across = -Offset.X * Sin + Offset.Y * Cos;
		return FMath::Abs(Along) <= Half.X && FMath::Abs(Across) <= Half.Y;
	}

	double AngleOf(const FVector2D& Direction)
	{
		return FMath::Atan2(Direction.Y, Direction.X);
	}

	/** A typed value: false while there is no number yet (nothing typed, or only a sign or a point). */
	bool ParseTyped(const FString& Text, double& OutValue)
	{
		if (Text.IsEmpty() || Text == TEXT("-") || Text == TEXT(".") || Text == TEXT("-."))
		{
			return false;
		}
		OutValue = FCString::Atod(*Text);
		return true;
	}

	/** The character a key types into a value - a digit, a point or a minus, from the main keys or the numpad - or 0. */
	TCHAR GetTypedCharacter(const FKey& Key)
	{
		const FKey Digits[10] = { EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
		const FKey PadDigits[10] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour,
			EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
		for (int32 Digit = 0; Digit < 10; ++Digit)
		{
			if (Key == Digits[Digit] || Key == PadDigits[Digit])
			{
				return static_cast<TCHAR>(TEXT('0') + Digit);
			}
		}
		if (Key == EKeys::Period || Key == EKeys::Decimal)
		{
			return TEXT('.');
		}
		if (Key == EKeys::Hyphen || Key == EKeys::Subtract)
		{
			return TEXT('-');
		}
		return 0;
	}

	// =================================================================================================
	// 2D affine maps
	// =================================================================================================

	/**
	 * A 2D affine map in doubles: Point' = (M00 x + M01 y + Tx, M10 x + M11 y + Ty). Slate's own transforms are read into it by
	 * where they put the origin and the two unit steps - so whatever order Slate composes scale, shear and turn in, the
	 * gizmo places and solves with exactly what Slate draws.
	 */
	struct FAffine
	{
		double M00 = 1.0;
		double M01 = 0.0;
		double M10 = 0.0;
		double M11 = 1.0;
		double Tx = 0.0;
		double Ty = 0.0;

		template <typename TransformType>
		static FAffine From(const TransformType& Transform)
		{
			const FVector2f Origin = Transform.TransformPoint(FVector2f(0.f, 0.f));
			const FVector2f ColumnX = Transform.TransformPoint(FVector2f(1.f, 0.f)) - Origin;
			const FVector2f ColumnY = Transform.TransformPoint(FVector2f(0.f, 1.f)) - Origin;
			FAffine Out;
			Out.M00 = ColumnX.X;
			Out.M10 = ColumnX.Y;
			Out.M01 = ColumnY.X;
			Out.M11 = ColumnY.Y;
			Out.Tx = Origin.X;
			Out.Ty = Origin.Y;
			return Out;
		}

		FVector2D Apply(const FVector2D& Point) const
		{
			return FVector2D(M00 * Point.X + M01 * Point.Y + Tx, M10 * Point.X + M11 * Point.Y + Ty);
		}

		FVector2D ApplyVector(const FVector2D& Vector) const
		{
			return FVector2D(M00 * Vector.X + M01 * Vector.Y, M10 * Vector.X + M11 * Vector.Y);
		}

		double Determinant() const { return M00 * M11 - M01 * M10; }

		/** The map back - the identity when this one flattens everything onto a line. */
		FAffine Inverse() const
		{
			FAffine Out;
			const double Det = Determinant();
			if (FMath::Abs(Det) < 1.e-12)
			{
				return Out;
			}
			const double InverseDet = 1.0 / Det;
			Out.M00 = M11 * InverseDet;
			Out.M01 = -M01 * InverseDet;
			Out.M10 = -M10 * InverseDet;
			Out.M11 = M00 * InverseDet;
			Out.Tx = -(Out.M00 * Tx + Out.M01 * Ty);
			Out.Ty = -(Out.M10 * Tx + Out.M11 * Ty);
			return Out;
		}

		/** This map, then Next. */
		FAffine Then(const FAffine& Next) const
		{
			FAffine Out;
			Out.M00 = Next.M00 * M00 + Next.M01 * M10;
			Out.M01 = Next.M00 * M01 + Next.M01 * M11;
			Out.M10 = Next.M10 * M00 + Next.M11 * M10;
			Out.M11 = Next.M10 * M01 + Next.M11 * M11;
			Out.Tx = Next.M00 * Tx + Next.M01 * Ty + Next.Tx;
			Out.Ty = Next.M10 * Tx + Next.M11 * Ty + Next.Ty;
			return Out;
		}
	};

	/** A widget's render transform about its pivot (PivotPx from its top left): what Slate draws its box with. */
	FAffine PivotedPose(const FWidgetTransform& Pose, const FVector2D& PivotPx)
	{
		const FVector2f Pivot(PivotPx);
		return FAffine::From(Concatenate(Concatenate(FSlateRenderTransform(-Pivot), Pose.ToSlateRenderTransform()), FSlateRenderTransform(Pivot)));
	}

	// =================================================================================================
	// Brushes
	// =================================================================================================

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

	/** White, and tinted where it is drawn: MakeBox colors a rounded box's fill only by its tint. */
	const FSlateBrush* RoundedBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 3.f);
		return &Brush;
	}

	// =================================================================================================
	// One selected widget
	// =================================================================================================

	struct FTarget
	{
		/** The Widget Blueprint's own widget (saved) and the designer's preview of it (drawn). */
		TWeakObjectPtr<UWidget> Template;
		TWeakObjectPtr<UWidget> Preview;

		/**
		 * While the Widget Animations panel poses its preview at the playhead (see SWidgetAnimTimeline::CanGizmoKey): the same
		 * widget on the panel's preview - what its tracks name, and what it is keyed through. Null otherwise.
		 */
		TWeakObjectPtr<UWidget> PanelWidget;

		/** Its Render Transform as shown: as authored (the template's), or as posed at the playhead while the panel poses it. */
		FWidgetTransform Pose;
		FVector2D Pivot = FVector2D(0.5, 0.5);

		/** The channels (WidgetGizmoChannel) a track of the open animation keys at the playhead - the Auto Key chip. */
		uint8 KeyableChannels = WidgetGizmoChannel::None;

		bool bLocked = false;

		/** Whether it could be placed on the designer this paint: it has a Slate widget with a size. */
		bool bPlaced = false;

		/** Its box's size, in its own units. */
		FVector2D Size = FVector2D::ZeroVector;

		/** Its box, before its own pose, onto the designer's canvas: its layout, then everything above it as drawn now. */
		FAffine BoxToCanvas;

		// Worked out each paint, on the designer's canvas.
		FVector2D PivotCanvas = FVector2D::ZeroVector;
		/** Its box's corners and edge middles as drawn (see BoxFraction) - what Scale and Shear solve for. */
		FVector2D Points[8];
		/** The same points FrameGap further out: where the Scale handles and Shear grips are drawn and grabbed. */
		FVector2D Frame[8];
		/** The directions Translation moves it in - its parent's X and Y (Y down; the Y arrow points up, along -AxisY). */
		FVector2D AxisX = FVector2D(1.0, 0.0);
		FVector2D AxisY = FVector2D(0.0, 1.0);
		/** The direction of its top edge as drawn, in radians. */
		double BoxAngle = 0.0;

		FVector2D GetPivotPx() const { return Pivot * Size; }

		/** Where BoxPoint (its own units, from its top left) is drawn with PoseToUse about PivotToUse (a fraction of its size). */
		FVector2D ToCanvas(const FWidgetTransform& PoseToUse, const FVector2D& PivotToUse, const FVector2D& BoxPoint) const
		{
			return PivotedPose(PoseToUse, PivotToUse * Size).Then(BoxToCanvas).Apply(BoxPoint);
		}

		bool IsSameWidget(const FTarget& Other) const { return Template == Other.Template && Preview == Other.Preview; }

		bool IsUsable() const { return bPlaced && !bLocked; }

		FString GetName() const
		{
			const UWidget* Named = Template.IsValid() ? Template.Get() : Preview.Get();
			return Named ? Named->GetName() : FString();
		}
	};

	/** A chip above the widget, as drawn this paint. */
	struct FChip
	{
		EChip Kind = EChip::Move;
		FVector2D TopLeft = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		FString Label;
		FString Key;
		float LabelWidth = 0.f;
	};

	/**
	 * A smart guide a moving or scaling widget's edges and middle catch on: a vertical line at X = Value (or a horizontal one at
	 * Y = Value) on the canvas, as long as the box it comes from - SpanMin to SpanMax along the line.
	 */
	struct FSnapLine
	{
		double Value = 0.0;
		double SpanMin = 0.0;
		double SpanMax = 0.0;
	};

	/** The pose that keeps a widget where it is drawn when its pivot goes from Start's to NewPivot - see the Pivot drag. */
	FWidgetTransform CompensatePivot(const FTarget& Start, const FVector2D& NewPivot)
	{
		// With A the pose's scale, shear and turn, moving the pivot from P to P' moves the box by (A - I)(P - P'); Translation
		// takes it back.
		FWidgetTransform Out = Start.Pose;
		const FVector2D Back = Start.GetPivotPx() - NewPivot * Start.Size;
		const FVector2D Turned = FAffine::From(Start.Pose.ToSlateRenderTransform()).ApplyVector(Back);
		Out.Translation = FVector2D(
			RoundTo(Start.Pose.Translation.X + Back.X - Turned.X, 0.01),
			RoundTo(Start.Pose.Translation.Y + Back.Y - Turned.Y, 0.01));
		return Out;
	}

	/** The channel a session's edit changes. */
	uint8 GetOpChannel(EOp Op)
	{
		switch (Op)
		{
		case EOp::Grab: return WidgetGizmoChannel::Translation;
		case EOp::Scale: return WidgetGizmoChannel::Scale;
		case EOp::Rotate: return WidgetGizmoChannel::Angle;
		case EOp::Shear: return WidgetGizmoChannel::Shear;
		default: return WidgetGizmoChannel::None;
		}
	}

	/** The channel a handle's drag changes - none for the pivot, which is never keyed. */
	uint8 GetHandleChannel(EHandle Handle)
	{
		switch (Handle)
		{
		case EHandle::MoveX:
		case EHandle::MoveY:
		case EHandle::MoveXY: return WidgetGizmoChannel::Translation;
		case EHandle::Scale: return WidgetGizmoChannel::Scale;
		case EHandle::Rotate: return WidgetGizmoChannel::Angle;
		case EHandle::Shear: return WidgetGizmoChannel::Shear;
		default: return WidgetGizmoChannel::None;
		}
	}

	/** A scale for a pose scale gone from PoseStart to PoseNew: times the ratio, as scales multiply. A pose at 0 has no ratio. */
	double Rescale(double ValueStart, double PoseStart, double PoseNew)
	{
		if (FMath::Abs(PoseStart) > 1.e-6)
		{
			return ValueStart * PoseNew / PoseStart;
		}
		return FMath::Abs(ValueStart) > 1.e-6 ? ValueStart : PoseNew;
	}

	class FGizmoLayer;

	/** Every designer's gizmo alive now - what FGizmoInput hit-tests. Each adds itself as it is made. */
	TArray<TWeakPtr<FGizmoLayer>>& GetDesignerLayers()
	{
		static TArray<TWeakPtr<FGizmoLayer>> Layers;
		return Layers;
	}

	/** Takes the mouse and the keys for the gizmos - registered while any exists. See FGizmoInput below. */
	void AcquireInput();
	void ReleaseInput();

	void RepaintAll();

	// =================================================================================================
	// The layer
	// =================================================================================================

	/**
	 * One designer's gizmo, drawn over it by its extension's Paint (see FWidgetGizmoExtension) every frame the designer is drawn.
	 * Not a widget: what it draws is part of the designer's own drawing, and its handles take the mouse through FGizmoInput.
	 */
	class FGizmoLayer : public TSharedFromThis<FGizmoLayer>
	{
	public:
		FGizmoLayer()
		{
			AcquireInput();
		}

		~FGizmoLayer()
		{
			EndSession(/*bCancel=*/true);
			CancelDrag();
			EndEdit(/*bCancel=*/true);
			ReleaseInput();
		}

		/** Listed for FGizmoInput - once it is owned by a shared pointer. */
		void Register()
		{
			GetDesignerLayers().RemoveAll([](const TWeakPtr<FGizmoLayer>& Weak) { return !Weak.IsValid(); });
			GetDesignerLayers().Add(AsShared());
		}

		/** Places the gizmo on every selected widget and draws it over the designer, in the designer's own space (DesignerGeometry). */
		void Paint(const TSet<FWidgetReference>& Selection, const FGeometry& DesignerGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId);

		// ---------------------------------------------------------------------------------------------
		// What FGizmoInput asks
		// ---------------------------------------------------------------------------------------------

		/** Whether a gizmo is drawn - enabled, not hidden, and a selected widget placed. */
		bool IsShowing() const
		{
			return UWidgetGizmoSettings::Get().bEnableGizmo && !GizmoHidden() && HasTargets();
		}

		/** Whether a widget is selected in this designer that the gizmo can act on (shown or hidden). */
		bool HasTargets() const
		{
			return Targets.ContainsByPredicate([](const FTarget& Target) { return Target.IsUsable(); });
		}

		/** Whether this designer has been drawn and Screen is over it as last drawn. Whether it is in front is IsDesignerUnder's. */
		bool IsOver(const FVector2D& Screen) const
		{
			return PaintedFrame > 0 && Designer.IsUnderLocation(Screen);
		}

		/** Whether Path - what is under the mouse - goes through this layer's designer: not a menu, drawer or window over it. */
		bool IsDesignerUnder(const FWidgetPath& Path)
		{
			if (!Path.IsValid())
			{
				return false;
			}
			const TSharedPtr<SWidget> OwnDesigner = DesignerWidget.Pin();
			for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
			{
				const FArrangedWidget& Arranged = Path.Widgets[Index];
				if (OwnDesigner.IsValid() && Arranged.Widget == OwnDesigner.ToSharedRef())
				{
					return true;
				}
				// The widget exactly where the gizmo is drawn - the one the designer painted it with, whatever its type.
				if (IsDesignerRect(Arranged.Geometry))
				{
					DesignerWidget = Arranged.Widget;
					return true;
				}
			}
			return false;
		}

		/**
		 * Whether the keyboard is with this layer's designer: Focused - the widget with keyboard focus - is the designer or
		 * inside it, or is the designer view around the canvas the gizmo is drawn over.
		 */
		bool IsKeyboardIn(const TSharedPtr<SWidget>& Focused) const
		{
			const TSharedPtr<SWidget> OwnDesigner = DesignerWidget.Pin();
			if (!Focused.IsValid() || !OwnDesigner.IsValid())
			{
				return false;
			}
			for (TSharedPtr<SWidget> Walk = Focused; Walk.IsValid(); Walk = Walk->GetParentWidget())
			{
				if (Walk == OwnDesigner)
				{
					return true;
				}
			}
			// The designer view takes the keyboard when a widget is clicked; the canvas the gizmo is drawn with may be inside it.
			int32 Depth = 0;
			for (TSharedPtr<SWidget> Walk = OwnDesigner; Walk.IsValid() && Depth < 8; Walk = Walk->GetParentWidget(), ++Depth)
			{
				if (Walk == Focused)
				{
					return Focused->GetType() == FName(TEXT("SDesignerView"));
				}
			}
			return false;
		}

		/** The handle drawn at Screen this paint, if any. */
		FHit HitTest(const FVector2D& Screen) const;

		void SetHover(const FHit& Hit)
		{
			if (Hovered != Hit)
			{
				Hovered = Hit;
				RequestRepaint();
			}
		}

		const FHit& GetHovered() const { return Hovered; }

		/** The cursor over (or pressing) Hit. */
		EMouseCursor::Type GetCursorFor(const FHit& Hit) const;

		/** A left press on what HitTest found. A chip acts at once; a handle is held until let go - a click, or past the threshold a drag. */
		void Press(const FHit& Hit, const FVector2D& Screen);

		bool IsPressed() const { return Pressing.bPressed; }
		bool IsDragging() const { return Pressing.bDragging; }
		const FHit& GetPressed() const { return Pressing.Hit; }

		/** The mouse moved while pressed: past the threshold the drag starts, and follows it. */
		void PressMove(const FVector2D& Screen, const FModifierKeysState& Modifiers);

		/** The modifiers changed during a drag: worked out again where the mouse last was (Shift snaps, Ctrl frees the pivot). */
		void ModifiersChanged(const FModifierKeysState& Modifiers)
		{
			if (Pressing.bDragging)
			{
				PressMove(Pressing.LastScreen, Modifiers);
			}
		}

		/** The press let go: a drag is kept as one undo step. */
		void Release();

		/** Escape or a right click mid-drag, or the gizmo going: everything put back as it was at the press, no undo step. */
		void CancelDrag();

		/** Right-click on a handle: what it can put back to its default, in a menu over the designer (OwnerPath). */
		void OpenResetMenu(const FHit& Hit, const FVector2D& ScreenPosition, const FWidgetPath& OwnerPath);

		// ---------------------------------------------------------------------------------------------
		// Keys and typed values (FSession)
		// ---------------------------------------------------------------------------------------------

		/** A gizmo key pressed with this designer's keyboard or mouse. Screen: where the mouse is. */
		void OnGizmoKey(EGizmoKey Which, const FVector2D& Screen);

		/** Whether a typed session (or a Grab) is going on. */
		bool HasSession() const { return Session.Op != EOp::None; }
		bool IsGrabbing() const { return Session.Op == EOp::Grab; }
		bool HasTyped() const { return !Session.Typed[0].IsEmpty() || !Session.Typed[1].IsEmpty(); }

		/** A key while a session is going on: what it types, its axis, Enter, Escape. False: not the session's - it is set, and the key passes on. */
		bool HandleSessionKey(const FKeyEvent& InKeyEvent);

		/** The mouse moved while grabbing: the widgets follow it - unless a value has been typed, which then sets them instead. */
		void SessionMouseMove(const FVector2D& Screen);

		/** Ends the session: what it set kept as one undo step, or with bCancel put back. */
		void EndSession(bool bCancel);

		/** Has the designer painted again - the hovered or pressed handle's look, or a drag, changed. */
		void RequestRepaint() const
		{
			// Never from inside the designer's own paint: it is being drawn already.
			if (bPainting)
			{
				return;
			}
			if (const TSharedPtr<SWidget> OwnDesigner = DesignerWidget.Pin())
			{
				OwnDesigner->Invalidate(EInvalidateWidgetReason::Paint);
			}
		}

	private:
		// ---------------------------------------------------------------------------------------------
		// Edits - one undo step each, onto the template, the preview, and the open animation's keys
		// ---------------------------------------------------------------------------------------------

		/** One widget of an edit: as it was at the begin, and what the edit sets now. */
		struct FEdited
		{
			/** The widget as shown at the begin: its place on the canvas, pose and pivot. Every change is worked out from it. */
			FTarget Start;

			/** The template's own Render Transform and pivot at the begin - what an unkeyed channel's change is put onto. */
			FWidgetTransform AuthoredStart;
			FVector2D AuthoredPivotStart = FVector2D(0.5, 0.5);

			/** The channels (WidgetGizmoChannel) keyed in the open animation rather than written onto the template. */
			uint8 Keyed = WidgetGizmoChannel::None;

			/** What the edit shows now. */
			FWidgetTransform Pose;
			FVector2D Pivot = FVector2D(0.5, 0.5);
		};

		/**
		 * An edit: a drag, a session, a reset. Each channel goes to a key of the open animation at the playhead when the Widget
		 * Animations panel poses its preview and a track takes it (see SWidgetAnimTimeline::BeginGizmoKeying), else onto the
		 * widget as authored - by the same change: Translation, Shear and Angle by the difference, Scale by the ratio, from the
		 * pose shown at the begin. The pivot is never keyed.
		 */
		struct FEdit
		{
			bool bActive = false;
			bool bChanged = false;
			bool bPivot = false;
			bool bKeying = false;
			TArray<FEdited> Widgets;
			TWeakPtr<SWidgetAnimTimeline> Panel;
			TUniquePtr<FScopedTransaction> Transaction;
		};

		/** Begins an edit of Targets[TargetIndices] changing Channels (and the pivot, with bPivot): one transaction until EndEdit. */
		bool BeginEdit(const TArray<int32>& TargetIndices, uint8 Channels, bool bPivot, const FText& Description);

		/** What edited widget EditedIndex shows now: its keys, template and preview set to it. */
		void SetPose(int32 EditedIndex, const FWidgetTransform& NewPose, const FVector2D& NewPivot);

		/** Ends the edit: kept as one undo step, or - cancelled, or nothing changed - everything put back, no undo step. */
		void EndEdit(bool bCancel);

		/** The press on a handle being held, and - once past the threshold - its drag (Edit's one widget). */
		struct FPress
		{
			bool bPressed = false;
			bool bDragging = false;
			FHit Hit;
			FVector2D PressCanvas = FVector2D::ZeroVector;
			/** From the press to the handle's box point - kept, so the box point moves exactly as the mouse does. */
			FVector2D GrabOffset = FVector2D::ZeroVector;
			FVector2D LastScreen = FVector2D::ZeroVector;

			/** Shear: which of Shear X (0) or Y (1) the grabbed edge slides. */
			int32 ShearComponent = 0;

			/** Rotate: the mouse's angle about the pivot last move, and how far it has turned since the press (radians). */
			double RotateLastAngle = 0.0;
			double RotateTurned = 0.0;
		};

		/**
		 * A typed session, as Blender's: started by the Grab / Scale / Rotate / Shear key, on every widget the gizmo acts on. A
		 * number typed sets the value - X or Y (or Tab) says which axis; Scale with no axis sets both; Rotate has one value. A Grab
		 * also follows the mouse (X / Y keep it to that axis) until something is typed. Enter or a click sets it, Escape or a
		 * right click puts it back; any other key sets it and goes on as usual.
		 */
		struct FSession
		{
			EOp Op = EOp::None;

			/** INDEX_NONE: no axis picked (Scale: both); 0 X; 1 Y. */
			int32 Axis = INDEX_NONE;

			/** What has been typed for X and Y. Rotate, and Scale with no axis, type into the first. */
			FString Typed[2];

			/** Grab: where the mouse was when it started, and where it is now. */
			FVector2D StartMouse = FVector2D::ZeroVector;
			FVector2D LastScreen = FVector2D::ZeroVector;

			/** Which of Typed the keys type into now. */
			int32 GetBufferIndex() const
			{
				return Op == EOp::Rotate || (Op == EOp::Scale && Axis == INDEX_NONE) ? 0 : FMath::Max(Axis, 0);
			}
		};

		/** Starts a session of Op on every widget the gizmo acts on - a Grab's edit at once, the others' when something is typed. */
		void StartSession(EOp Op, const FVector2D& Screen);

		/** Begins the session's edit if it hasn't begun. */
		bool BeginSessionEdit();

		/** What the session sets now, onto every widget of its edit. */
		void ApplySession();

		/** The axis the session's keys type into and a Grab keeps to - from no axis, Scale's both-ways value goes into both. */
		void SetSessionAxis(int32 NewAxis);

		/** Puts one channel of Targets[TargetIndex] back to its default - see EReset. One undo step. */
		void Reset(int32 TargetIndex, EReset What);

		/** Every selected widget this designer has, in a stable order (by name), with its pose as shown. */
		void GatherTargets(const TSet<FWidgetReference>& Selection);

		/** Where Target's box is on the canvas this paint, and its handles. False when it has no Slate widget with a size. */
		bool Place(FTarget& Target) const;

		/** Up the first selected widget's preview to the designer widget the gizmo is drawn over - see IsDesignerRect. */
		void FindDesignerWidget();

		/** Whether Geometry (desktop space) covers exactly the designer the gizmo was last drawn over - to the pixel. */
		bool IsDesignerRect(const FGeometry& Geometry) const
		{
			const FVector2D Position(Geometry.GetAbsolutePosition());
			const FVector2D AbsoluteSize(Geometry.GetAbsoluteSize());
			return FVector2D::Distance(Position, FVector2D(Designer.GetAbsolutePosition())) < 2.0
				&& FVector2D::Distance(AbsoluteSize, FVector2D(Designer.GetAbsoluteSize())) < 2.0;
		}

		FVector2D ScreenToCanvas(const FVector2D& Screen) const { return DesktopToCanvas.Apply(Screen); }

		/** The box point a handle moves (Scale, Shear), else the pivot - on the canvas, as drawn. */
		FVector2D GetHandleCanvas(const FTarget& Target, const FHit& Hit) const;

		/** The drag of the press: from Edit's one widget as it was at the press. */
		void UpdateDrag(const FVector2D& Mouse, const FModifierKeysState& Modifiers);

		void ResetPress()
		{
			Pressing = FPress();
			Readout.Reset();
		}

		// ---------------------------------------------------------------------------------------------
		// Smart guides
		// ---------------------------------------------------------------------------------------------

		/** Whether Target is drawn upright - no turn or shear on it from above - so its edges line up on the canvas. */
		static bool IsUpright(const FTarget& Target)
		{
			const FAffine& Map = Target.BoxToCanvas;
			const double Tolerance = 1.e-4 * FMath::Max(FMath::Abs(Map.M00), FMath::Abs(Map.M11));
			return Map.M00 > 0.0 && Map.M11 > 0.0 && FMath::Abs(Map.M01) <= Tolerance && FMath::Abs(Map.M10) <= Tolerance;
		}

		/**
		 * The guides for an edit of Start (and the other widgets of the edit, which are never guides): the edges and middle of the
		 * panel it is in, and of every visible widget beside it in that panel. None when it isn't upright or guides are off.
		 */
		void GatherSnapLines(const FTarget& Start);

		/** Whether guides apply to this move or scale now: on, gathered, and Ctrl not held. */
		bool UseGuides(const FModifierKeysState& Modifiers) const
		{
			return UWidgetGizmoSettings::Get().bSmartGuides && (SnapX.Num() > 0 || SnapY.Num() > 0) && !Modifiers.IsControlDown() && !Modifiers.IsCommandDown();
		}

		/**
		 * How far, on the canvas, Start at Pose has to move for its box's left, middle or right (bX) and top, middle or bottom (bY)
		 * to land on the nearest guide within Smart Guide Distance - zero on an axis with none. The guides used are drawn.
		 */
		FVector2D SnapBox(const FTarget& Start, const FWidgetTransform& Pose, bool bX, bool bY);

		/** Scale: NewScale changed so the edges the grabbed handle moves land on a guide - see the Scale drag. */
		void SnapScale(const FTarget& Start, int32 HandleIndex, bool bProportional, FVector2D& NewScale);

		/** Where Start's box is at Pose, on the canvas: its smallest and largest corner. */
		static void GetBoxBounds(const FTarget& Start, const FWidgetTransform& Pose, FVector2D& OutMin, FVector2D& OutMax)
		{
			OutMin = FVector2D(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
			OutMax = FVector2D(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const FVector2D Point = Start.ToCanvas(Pose, Start.Pivot, BoxFraction(Corner) * Start.Size);
				OutMin = FVector2D(FMath::Min(OutMin.X, Point.X), FMath::Min(OutMin.Y, Point.Y));
				OutMax = FVector2D(FMath::Max(OutMax.X, Point.X), FMath::Max(OutMax.Y, Point.Y));
			}
		}

		/** Lays the chips out above Target, inside the canvas - also where the typing bar goes when the chips are hidden. */
		void LayOutChips(const FTarget& Target);

		int32 PaintTarget(const FTarget& Target, int32 TargetIndex, const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
		int32 PaintChips(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
		int32 PaintTyping(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;
		int32 PaintReadout(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

		bool IsHot(int32 TargetIndex, EHandle Handle, int32 Index = INDEX_NONE) const
		{
			const FHit Asked{ Handle, TargetIndex, Index };
			return Pressing.bPressed ? Pressing.Hit == Asked : Hovered == Asked;
		}

		double GetGizmoScale() const { return FMath::Clamp(static_cast<double>(UWidgetGizmoSettings::Get().GizmoSize), 0.5, 3.0); }

		/** Whether the designer is painting the gizmo now - see RequestRepaint. */
		bool bPainting = false;

		/** The designer as last painted, moved onto the desktop (the mouse's space), and the frame it was painted. */
		FGeometry Designer;
		uint64 PaintedFrame = 0;
		FAffine DesktopToCanvas;
		FVector2D CanvasSize = FVector2D::ZeroVector;

		/** The designer widget itself - see FindDesignerWidget. */
		mutable TWeakPtr<SWidget> DesignerWidget;
		double NextDesignerSearch = 0.0;

		/** The Widget Animations panel posing this designer's preview at the playhead now - what edits are keyed through. */
		TWeakPtr<SWidgetAnimTimeline> KeyPanel;

		TArray<FTarget> Targets;
		TArray<FChip> Chips;
		FVector2D ChipRowMin = FVector2D::ZeroVector;
		FVector2D ChipRowMax = FVector2D::ZeroVector;

		FHit Hovered;
		FPress Pressing;
		FEdit Edit;
		FSession Session;

		/** The guides of the edit going on (see GatherSnapLines), and the ones lined up with now - drawn while it goes on. */
		TArray<FSnapLine> SnapX;
		TArray<FSnapLine> SnapY;
		TArray<TPair<FVector2D, FVector2D>> Guides;

		/** What a drag or a Grab sets, beside the mouse. */
		FString Readout;
		FVector2D ReadoutAt = FVector2D::ZeroVector;
	};

	// ---------------------------------------------------------------------------------------------
	// Placing
	// ---------------------------------------------------------------------------------------------

	void FGizmoLayer::GatherTargets(const TSet<FWidgetReference>& Selection)
	{
		Targets.Reset();
		for (const FWidgetReference& Reference : Selection)
		{
			UWidget* SelectedPreview = Reference.GetPreview();
			UWidget* SelectedTemplate = Reference.GetTemplate();
			if (!SelectedPreview)
			{
				continue;
			}
			FTarget& Target = Targets.AddDefaulted_GetRef();
			Target.Template = SelectedTemplate;
			Target.Preview = SelectedPreview;
			UWidget* Authored = SelectedTemplate ? SelectedTemplate : SelectedPreview;
			Target.Pose = Authored->GetRenderTransform();
			Target.Pivot = Authored->GetRenderTransformPivot();
			Target.bLocked = Authored->IsLockedInDesigner();
		}
		Targets.Sort([](const FTarget& A, const FTarget& B) { return A.GetName() < B.GetName(); });
		if (Targets.Num() > MaxTargets)
		{
			Targets.SetNum(MaxTargets);
		}

		// While the Widget Animations panel poses its preview at the playhead, the gizmo shows - and edits - the widgets as posed
		// there, and what it does is keyed (see SWidgetAnimTimeline::CanGizmoKey).
		const UWidget* FirstTemplate = Targets.Num() > 0 ? Targets[0].Template.Get() : nullptr;
		const UWidgetBlueprint* OwningBlueprint = FirstTemplate ? FirstTemplate->GetTypedOuter<UWidgetBlueprint>() : nullptr;
		const TSharedPtr<SWidgetAnimTimeline> Panel = OwningBlueprint ? WidgetAnimScripting::FindLastUsedPanelForBlueprint(OwningBlueprint) : nullptr;
		const bool bPosed = Panel.IsValid() && Panel->CanGizmoKey();
		KeyPanel = bPosed ? Panel : nullptr;
		UUserWidget* PanelRoot = bPosed && Panel->GetSource() ? Panel->GetSource()->GetPreviewRoot() : nullptr;
		if (PanelRoot)
		{
			for (FTarget& Target : Targets)
			{
				// A designer widget on the panel's preview, which may be another instance of the same tree: found there by name.
				UWidget* OnPanel = Target.Preview.Get();
				const UWidget* SelectedTemplate = Target.Template.Get();
				if (OnPanel && OnPanel != PanelRoot && OnPanel->GetTypedOuter<UUserWidget>() != PanelRoot)
				{
					OnPanel = PanelRoot->WidgetTree && SelectedTemplate ? PanelRoot->WidgetTree->FindWidget(SelectedTemplate->GetFName()) : nullptr;
				}
				if (OnPanel)
				{
					Target.PanelWidget = OnPanel;
					Target.Pose = OnPanel->GetRenderTransform();
					Target.KeyableChannels = Panel->GetGizmoKeyableChannels(OnPanel);
				}
			}
		}

		// What an edit going on shows - not read back off the widgets, which lag a frame behind (or are posed by the panel).
		if (Edit.bActive)
		{
			for (const FEdited& Edited : Edit.Widgets)
			{
				if (FTarget* Shown = Targets.FindByPredicate([&Edited](const FTarget& Target) { return Target.IsSameWidget(Edited.Start); }))
				{
					Shown->Pose = Edited.Pose;
					Shown->Pivot = Edited.Pivot;
				}
			}
		}
	}

	bool FGizmoLayer::Place(FTarget& Target) const
	{
		const UWidget* PlacedPreview = Target.Preview.Get();
		const TSharedPtr<SWidget> SlateWidget = PlacedPreview ? PlacedPreview->GetCachedWidget() : nullptr;
		if (!SlateWidget.IsValid())
		{
			return false;
		}
		const FGeometry& WidgetGeometry = SlateWidget->GetTickSpaceGeometry();
		const FVector2D LocalSize(WidgetGeometry.GetLocalSize());
		if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
		{
			return false;
		}
		Target.Size = LocalSize;

		// Through the widget's layout and its Slate parent as drawn - never the widget's own render transform as last drawn,
		// which lags a frame behind a drag. Without a parent (never in the designer): its own, its pose now taken off.
		FAffine BoxToDesktop;
		if (const TSharedPtr<SWidget> Above = SlateWidget->GetParentWidget())
		{
			const FGeometry& AboveGeometry = Above->GetTickSpaceGeometry();
			BoxToDesktop = FAffine::From(WidgetGeometry.GetAccumulatedLayoutTransform())
				.Then(FAffine::From(AboveGeometry.GetAccumulatedLayoutTransform().Inverse()))
				.Then(FAffine::From(AboveGeometry.GetAccumulatedRenderTransform()));
		}
		else
		{
			const FAffine Now = PivotedPose(PlacedPreview->GetRenderTransform(), PlacedPreview->GetRenderTransformPivot() * LocalSize);
			BoxToDesktop = Now.Inverse().Then(FAffine::From(WidgetGeometry.GetAccumulatedRenderTransform()));
		}
		Target.BoxToCanvas = BoxToDesktop.Then(DesktopToCanvas);

		const FAffine Posed = PivotedPose(Target.Pose, Target.GetPivotPx()).Then(Target.BoxToCanvas);
		Target.PivotCanvas = Posed.Apply(Target.GetPivotPx());
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Target.Points[Index] = Posed.Apply(BoxFraction(Index) * Target.Size);
		}

		const FVector2D StepX = Target.BoxToCanvas.ApplyVector(FVector2D(1.0, 0.0));
		const FVector2D StepY = Target.BoxToCanvas.ApplyVector(FVector2D(0.0, 1.0));
		Target.AxisX = StepX.SizeSquared() > 1.e-12 ? StepX.GetSafeNormal() : FVector2D(1.0, 0.0);
		Target.AxisY = StepY.SizeSquared() > 1.e-12 ? StepY.GetSafeNormal() : FVector2D(0.0, 1.0);

		const FVector2D TopEdge = Target.Points[1] - Target.Points[0];
		const FVector2D SideEdge = Target.Points[3] - Target.Points[0];
		Target.BoxAngle = TopEdge.SizeSquared() > 1.e-6 ? AngleOf(TopEdge) : AngleOf(Target.AxisX);

		// The frame the handles sit on: each edge pushed FrameGap out, so the corners move out along both edges by FrameGap over
		// the sine of the angle between them (a sheared box's frame stays FrameGap from every edge).
		const FVector2D AlongTop = TopEdge.SizeSquared() > 1.e-6 ? TopEdge.GetSafeNormal() : Target.AxisX;
		const FVector2D AlongSide = SideEdge.SizeSquared() > 1.e-6 ? SideEdge.GetSafeNormal() : Target.AxisY;
		const double Sine = FMath::Max(FMath::Abs(AlongTop.X * AlongSide.Y - AlongTop.Y * AlongSide.X), 0.25);
		const double Push = Palette::FrameGap * GetGizmoScale() / Sine;
		const double CornerSigns[4][2] = { { -1.0, -1.0 }, { 1.0, -1.0 }, { 1.0, 1.0 }, { -1.0, 1.0 } };
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Target.Frame[Corner] = Target.Points[Corner] + (AlongTop * CornerSigns[Corner][0] + AlongSide * CornerSigns[Corner][1]) * Push;
		}
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			Target.Frame[4 + Edge] = (Target.Frame[Edge] + Target.Frame[(Edge + 1) % 4]) * 0.5;
		}
		return true;
	}

	void FGizmoLayer::FindDesignerWidget()
	{
		if (DesignerWidget.IsValid() || Targets.Num() == 0)
		{
			return;
		}
		const double Now = FPlatformTime::Seconds();
		if (Now < NextDesignerSearch)
		{
			return;
		}
		NextDesignerSearch = Now + 1.0;

		// Up the selected widget's parents to the one exactly where the gizmo is drawn - matched by place, not by type: the
		// designer's own widget isn't always the one it hands its geometry from (see WidgetAnimMotionPath.cpp).
		const UWidget* FirstPreview = Targets[0].Preview.Get();
		TSharedPtr<SWidget> Walk = FirstPreview ? FirstPreview->GetCachedWidget() : nullptr;
		while (Walk.IsValid() && !IsDesignerRect(Walk->GetTickSpaceGeometry()))
		{
			Walk = Walk->GetParentWidget();
		}
		DesignerWidget = Walk;
	}

	void FGizmoLayer::LayOutChips(const FTarget& Target)
	{
		Chips.Reset();
		const UWidgetGizmoSettings& Settings = UWidgetGizmoSettings::Get();
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		auto KeyText = [](const FInputChord& Chord) { return Chord.IsValidChord() ? Chord.GetInputText().ToString() : FString(); };

		struct FChipText
		{
			EChip Kind;
			FText Label;
			FString Key;
		};
		TArray<FChipText> Entries =
		{
			{ EChip::Move, LOCTEXT("ChipMove", "Move"), KeyText(Settings.GrabKey) },
			{ EChip::Scale, LOCTEXT("ChipScale", "Scale"), KeyText(Settings.ScaleKey) },
			{ EChip::Rotate, LOCTEXT("ChipRotate", "Rotate"), KeyText(Settings.RotateKey) },
			{ EChip::Shear, LOCTEXT("ChipShear", "Shear"), KeyText(Settings.ShearKey) },
			{ EChip::Hide, LOCTEXT("ChipHide", "Hide"), KeyText(Settings.HideKey) },
		};

		// Auto Key: what the open animation keys at the playhead - the keys whose edits are keyed.
		if (Target.KeyableChannels != WidgetGizmoChannel::None)
		{
			TArray<FString> Keyed;
			if (Target.KeyableChannels & WidgetGizmoChannel::Translation)
			{
				Keyed.Add(KeyText(Settings.GrabKey));
			}
			if (Target.KeyableChannels & WidgetGizmoChannel::Scale)
			{
				Keyed.Add(KeyText(Settings.ScaleKey));
			}
			if (Target.KeyableChannels & WidgetGizmoChannel::Angle)
			{
				Keyed.Add(KeyText(Settings.RotateKey));
			}
			if (Target.KeyableChannels & WidgetGizmoChannel::Shear)
			{
				Keyed.Add(KeyText(Settings.ShearKey));
			}
			Entries.Add({ EChip::Key, LOCTEXT("ChipKey", "Auto Key"), FString::Join(Keyed, TEXT(" ")) });
		}

		constexpr float PaddingX = 6.f;
		constexpr float KeyGap = 4.f;
		constexpr float DotRoom = 10.f;
		double RowWidth = 0.0;
		for (const FChipText& Entry : Entries)
		{
			FChip& Chip = Chips.AddDefaulted_GetRef();
			Chip.Kind = Entry.Kind;
			Chip.Label = Entry.Label.ToString();
			Chip.Key = Entry.Key;
			Chip.LabelWidth = static_cast<float>(FVector2D(Measure->Measure(Chip.Label, Font)).X) + (Entry.Kind == EChip::Key ? DotRoom : 0.f);
			const double KeyWidth = Chip.Key.IsEmpty() ? 0.0 : FVector2D(Measure->Measure(Chip.Key, Font)).X + KeyGap;
			Chip.Size = FVector2D(Chip.LabelWidth + KeyWidth + PaddingX * 2.0, Palette::ChipHeight);
			RowWidth += Chip.Size.X + Palette::ChipGap;
		}
		RowWidth -= Palette::ChipGap;

		// Above the widget's frame as drawn, at its left; below it when there is no room above. Always inside the canvas.
		FVector2D BoxMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D BoxMax(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
		for (int32 Index = 0; Index < 4; ++Index)
		{
			BoxMin = FVector2D(FMath::Min(BoxMin.X, Target.Frame[Index].X), FMath::Min(BoxMin.Y, Target.Frame[Index].Y));
			BoxMax = FVector2D(FMath::Max(BoxMax.X, Target.Frame[Index].X), FMath::Max(BoxMax.Y, Target.Frame[Index].Y));
		}
		constexpr double Margin = 4.0;
		constexpr double Gap = 8.0;
		double Top = BoxMin.Y - Gap - Palette::ChipHeight;
		if (Top < Margin)
		{
			Top = BoxMax.Y + Gap;
		}
		Top = FMath::Clamp(Top, Margin, FMath::Max(Margin, CanvasSize.Y - Palette::ChipHeight - Margin));
		double Left = FMath::Clamp(BoxMin.X, Margin, FMath::Max(Margin, CanvasSize.X - RowWidth - Margin));

		ChipRowMin = FVector2D(Left, Top);
		for (FChip& Chip : Chips)
		{
			Chip.TopLeft = FVector2D(Left, Top);
			Left += Chip.Size.X + Palette::ChipGap;
		}
		ChipRowMax = FVector2D(Left - Palette::ChipGap, Top + Palette::ChipHeight);
	}

	FVector2D FGizmoLayer::GetHandleCanvas(const FTarget& Target, const FHit& Hit) const
	{
		switch (Hit.Handle)
		{
		case EHandle::Scale:
			return Target.Points[FMath::Clamp(Hit.Index, 0, 7)];
		case EHandle::Shear:
			return Target.Points[4 + FMath::Clamp(Hit.Index, 0, 3)];
		default:
			return Target.PivotCanvas;
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Smart guides
	// ---------------------------------------------------------------------------------------------

	void FGizmoLayer::GatherSnapLines(const FTarget& Start)
	{
		SnapX.Reset();
		SnapY.Reset();
		Guides.Reset();
		const UWidget* Moved = Start.Preview.Get();
		const TSharedPtr<SWidget> MovedSlate = Moved ? Moved->GetCachedWidget() : nullptr;
		if (!UWidgetGizmoSettings::Get().bSmartGuides || !MovedSlate.IsValid() || !IsUpright(Start))
		{
			return;
		}

		// A box on the desktop, as guides on the canvas: its two edges and its middle each way, as long as the box.
		auto AddBox = [this](const FSlateRect& Desktop)
		{
			const FVector2D A = DesktopToCanvas.Apply(FVector2D(Desktop.Left, Desktop.Top));
			const FVector2D B = DesktopToCanvas.Apply(FVector2D(Desktop.Right, Desktop.Bottom));
			const FVector2D Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
			const FVector2D Max(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
			if (Max.X - Min.X < 1.0 || Max.Y - Min.Y < 1.0)
			{
				return;
			}
			for (const double X : { Min.X, (Min.X + Max.X) * 0.5, Max.X })
			{
				SnapX.Add({ X, Min.Y, Max.Y });
			}
			for (const double Y : { Min.Y, (Min.Y + Max.Y) * 0.5, Max.Y })
			{
				SnapY.Add({ Y, Min.X, Max.X });
			}
		};

		// The panel it is in: what it is laid out in.
		if (const TSharedPtr<SWidget> Above = MovedSlate->GetParentWidget())
		{
			AddBox(Above->GetTickSpaceGeometry().GetRenderBoundingRect());
		}

		// Every visible widget beside it in that panel - none of the widgets being edited.
		const UPanelWidget* Parent = Moved->GetParent();
		for (int32 ChildIndex = 0; Parent && ChildIndex < Parent->GetChildrenCount(); ++ChildIndex)
		{
			const UWidget* Sibling = Parent->GetChildAt(ChildIndex);
			const bool bEdited = Edit.Widgets.ContainsByPredicate([Sibling](const FEdited& Edited) { return Edited.Start.Preview.Get() == Sibling; });
			const TSharedPtr<SWidget> SiblingSlate = Sibling ? Sibling->GetCachedWidget() : nullptr;
			if (!Sibling || Sibling == Moved || bEdited || !Sibling->IsVisible() || !SiblingSlate.IsValid())
			{
				continue;
			}
			AddBox(SiblingSlate->GetTickSpaceGeometry().GetRenderBoundingRect());
		}
	}

	FVector2D FGizmoLayer::SnapBox(const FTarget& Start, const FWidgetTransform& Pose, bool bX, bool bY)
	{
		FVector2D Min, Max;
		GetBoxBounds(Start, Pose, Min, Max);
		const double Reach = UWidgetGizmoSettings::Get().SmartGuideDistance;

		// The nearest guide to any of Values, within reach: how far to move, and the guide.
		auto Nearest = [Reach](const TArray<FSnapLine>& Lines, std::initializer_list<double> Values, double& OutMove, const FSnapLine*& OutLine)
		{
			double Best = Reach;
			OutLine = nullptr;
			OutMove = 0.0;
			for (const FSnapLine& Line : Lines)
			{
				for (const double Value : Values)
				{
					const double Distance = FMath::Abs(Line.Value - Value);
					if (Distance <= Best)
					{
						Best = Distance;
						OutMove = Line.Value - Value;
						OutLine = &Line;
					}
				}
			}
		};

		FVector2D Move = FVector2D::ZeroVector;
		const FSnapLine* LineX = nullptr;
		const FSnapLine* LineY = nullptr;
		if (bX)
		{
			Nearest(SnapX, { Min.X, (Min.X + Max.X) * 0.5, Max.X }, Move.X, LineX);
		}
		if (bY)
		{
			Nearest(SnapY, { Min.Y, (Min.Y + Max.Y) * 0.5, Max.Y }, Move.Y, LineY);
		}

		// Each guide drawn from the box it comes from to the widget where it lands.
		if (LineX)
		{
			Guides.Emplace(FVector2D(LineX->Value, FMath::Min(LineX->SpanMin, Min.Y + Move.Y)), FVector2D(LineX->Value, FMath::Max(LineX->SpanMax, Max.Y + Move.Y)));
		}
		if (LineY)
		{
			Guides.Emplace(FVector2D(FMath::Min(LineY->SpanMin, Min.X + Move.X), LineY->Value), FVector2D(FMath::Max(LineY->SpanMax, Max.X + Move.X), LineY->Value));
		}
		return Move;
	}

	void FGizmoLayer::SnapScale(const FTarget& Start, int32 HandleIndex, bool bProportional, FVector2D& NewScale)
	{
		// Only upright and unturned, unsheared: then each edge of the box sits at the pivot plus its distance from the pivot times
		// its scale, on its own axis - and the scale that puts it on a guide is one division.
		if (!IsUpright(Start) || !FMath::IsNearlyZero(Start.Pose.Angle, 1.e-3f) || !Start.Pose.Shear.IsNearlyZero(1.e-3))
		{
			return;
		}
		const FVector2D Fraction = BoxFraction(HandleIndex);
		const FVector2D PivotPx = Start.GetPivotPx();
		const double Reach = UWidgetGizmoSettings::Get().SmartGuideDistance;

		// Per axis: the scale that lands the moving edge on the nearest guide, and how far it moves to get there.
		struct FAxisSnap
		{
			bool bFound = false;
			double Scale = 1.0;
			double Distance = 0.0;
			const FSnapLine* Line = nullptr;
		};
		auto SnapAxis = [&](int32 Axis) -> FAxisSnap
		{
			FAxisSnap Out;
			const double Along = Axis == 0 ? Fraction.X : Fraction.Y;
			if (FMath::IsNearlyEqual(Along, 0.5))
			{
				return Out;
			}
			const double PerScale = (Along * (Axis == 0 ? Start.Size.X : Start.Size.Y) - (Axis == 0 ? PivotPx.X : PivotPx.Y)) * (Axis == 0 ? Start.BoxToCanvas.M00 : Start.BoxToCanvas.M11);
			if (FMath::Abs(PerScale) < 1.e-6)
			{
				return Out;
			}
			const double Pivot = Axis == 0 ? Start.PivotCanvas.X : Start.PivotCanvas.Y;
			const double Edge = Pivot + PerScale * (Axis == 0 ? NewScale.X : NewScale.Y);
			double Best = Reach;
			for (const FSnapLine& Line : (Axis == 0 ? SnapX : SnapY))
			{
				const double Distance = FMath::Abs(Line.Value - Edge);
				if (Distance <= Best)
				{
					Best = Distance;
					Out.bFound = true;
					Out.Scale = (Line.Value - Pivot) / PerScale;
					Out.Distance = Distance;
					Out.Line = &Line;
				}
			}
			return Out;
		};

		const FAxisSnap OnX = SnapAxis(0);
		const FAxisSnap OnY = SnapAxis(1);
		bool bUsedX = false;
		bool bUsedY = false;
		if (bProportional)
		{
			// One factor for both: whichever axis is nearer its guide sets it.
			bUsedX = OnX.bFound && (!OnY.bFound || OnX.Distance <= OnY.Distance) && !FMath::IsNearlyZero(NewScale.X);
			bUsedY = !bUsedX && OnY.bFound && !FMath::IsNearlyZero(NewScale.Y);
			if (bUsedX)
			{
				NewScale *= OnX.Scale / NewScale.X;
			}
			else if (bUsedY)
			{
				NewScale *= OnY.Scale / NewScale.Y;
			}
		}
		else
		{
			bUsedX = OnX.bFound;
			bUsedY = OnY.bFound;
			NewScale = FVector2D(bUsedX ? OnX.Scale : NewScale.X, bUsedY ? OnY.Scale : NewScale.Y);
		}
		if (!bUsedX && !bUsedY)
		{
			return;
		}

		// The guides lined up with, from the box they come from to the box as scaled.
		FWidgetTransform Scaled = Start.Pose;
		Scaled.Scale = NewScale;
		FVector2D Min, Max;
		GetBoxBounds(Start, Scaled, Min, Max);
		if (bUsedX && OnX.Line)
		{
			Guides.Emplace(FVector2D(OnX.Line->Value, FMath::Min(OnX.Line->SpanMin, Min.Y)), FVector2D(OnX.Line->Value, FMath::Max(OnX.Line->SpanMax, Max.Y)));
		}
		if (bUsedY && OnY.Line)
		{
			Guides.Emplace(FVector2D(FMath::Min(OnY.Line->SpanMin, Min.X), OnY.Line->Value), FVector2D(FMath::Max(OnY.Line->SpanMax, Max.X), OnY.Line->Value));
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Hit testing
	// ---------------------------------------------------------------------------------------------

	FHit FGizmoLayer::HitTest(const FVector2D& Screen) const
	{
		FHit Hit;
		if (!IsShowing())
		{
			return Hit;
		}
		const FVector2D Mouse = ScreenToCanvas(Screen);
		if (Mouse.X < 0.0 || Mouse.Y < 0.0 || Mouse.X > CanvasSize.X || Mouse.Y > CanvasSize.Y)
		{
			return Hit;
		}

		for (const FChip& Chip : Chips)
		{
			if (Chip.Kind != EChip::Key && Mouse.X >= Chip.TopLeft.X && Mouse.Y >= Chip.TopLeft.Y
				&& Mouse.X <= Chip.TopLeft.X + Chip.Size.X && Mouse.Y <= Chip.TopLeft.Y + Chip.Size.Y)
			{
				Hit.Handle = EHandle::Chip;
				Hit.Index = static_cast<int32>(Chip.Kind);
				return Hit;
			}
		}

		const double GizmoScale = GetGizmoScale();
		const double Slop = Palette::HitSlop;
		const EMode Mode = GizmoMode();
		for (int32 TargetIndex = Targets.Num() - 1; TargetIndex >= 0; --TargetIndex)
		{
			const FTarget& Target = Targets[TargetIndex];
			if (!Target.IsUsable())
			{
				continue;
			}
			Hit.Target = TargetIndex;

			// The pivot first: it is on top in every mode.
			if (FVector2D::Distance(Mouse, Target.PivotCanvas) <= Palette::PivotSize * 0.5 * GizmoScale + Slop * 0.5)
			{
				Hit.Handle = EHandle::Pivot;
				return Hit;
			}

			switch (Mode)
			{
			case EMode::Move:
			{
				const FVector2D AxisUp = -Target.AxisY;
				const FVector2D PlaneCenter = Target.PivotCanvas + (Target.AxisX + AxisUp) * (Palette::PlaneOffset * GizmoScale);
				const double PlaneHalf = Palette::PlaneSize * 0.5 * GizmoScale + 2.0;
				if (IsInTurnedRect(Mouse, PlaneCenter, AngleOf(Target.AxisX), FVector2D(PlaneHalf, PlaneHalf)))
				{
					Hit.Handle = EHandle::MoveXY;
					return Hit;
				}
				const double From = Palette::ArrowGap * GizmoScale;
				const double To = Palette::ArrowLength * GizmoScale;
				if (DistanceToSegment(Mouse, Target.PivotCanvas + Target.AxisX * From, Target.PivotCanvas + Target.AxisX * To) <= Slop)
				{
					Hit.Handle = EHandle::MoveX;
					return Hit;
				}
				if (DistanceToSegment(Mouse, Target.PivotCanvas + AxisUp * From, Target.PivotCanvas + AxisUp * To) <= Slop)
				{
					Hit.Handle = EHandle::MoveY;
					return Hit;
				}
				break;
			}
			case EMode::Rotate:
			{
				const double Radius = Palette::RingRadius * GizmoScale;
				if (FMath::Abs(FVector2D::Distance(Mouse, Target.PivotCanvas) - Radius) <= Slop)
				{
					Hit.Handle = EHandle::Rotate;
					return Hit;
				}
				break;
			}
			case EMode::Scale:
			{
				const double Half = Palette::GripSize * 0.5 * GizmoScale + Slop * 0.5;
				for (int32 Index = 0; Index < 8; ++Index)
				{
					if (IsInTurnedRect(Mouse, Target.Frame[Index], Target.BoxAngle, FVector2D(Half, Half)))
					{
						Hit.Handle = EHandle::Scale;
						Hit.Index = Index;
						return Hit;
					}
				}
				break;
			}
			case EMode::Shear:
			{
				const FVector2D Half(Palette::ShearGripLength * 0.5 * GizmoScale + Slop * 0.5, Palette::ShearGripThickness * 0.5 * GizmoScale + Slop * 0.5);
				for (int32 Edge = 0; Edge < 4; ++Edge)
				{
					const FVector2D EdgeDirection = (Edge % 2 == 0) ? Target.Points[1] - Target.Points[0] : Target.Points[3] - Target.Points[0];
					if (IsInTurnedRect(Mouse, Target.Frame[4 + Edge], AngleOf(EdgeDirection), Half))
					{
						Hit.Handle = EHandle::Shear;
						Hit.Index = Edge;
						return Hit;
					}
				}
				break;
			}
			}
		}
		return FHit();
	}

	EMouseCursor::Type FGizmoLayer::GetCursorFor(const FHit& Hit) const
	{
		const bool bHeld = Pressing.bPressed && Pressing.Hit == Hit;
		auto ResizeAlong = [](const FVector2D& Direction)
		{
			// Folded onto 0..180 degrees: a line has no way round.
			double Degrees = FMath::RadiansToDegrees(AngleOf(Direction));
			Degrees = FMath::Fmod(Degrees + 360.0, 180.0);
			if (Degrees < 22.5 || Degrees >= 157.5)
			{
				return EMouseCursor::ResizeLeftRight;
			}
			if (Degrees < 67.5)
			{
				return EMouseCursor::ResizeSouthEast;
			}
			if (Degrees < 112.5)
			{
				return EMouseCursor::ResizeUpDown;
			}
			return EMouseCursor::ResizeSouthWest;
		};

		const FTarget* Target = Targets.IsValidIndex(Hit.Target) ? &Targets[Hit.Target] : nullptr;
		switch (Hit.Handle)
		{
		case EHandle::Chip:
			return EMouseCursor::Hand;
		case EHandle::Pivot:
		case EHandle::Rotate:
			return bHeld ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand;
		case EHandle::MoveX:
		case EHandle::MoveY:
		case EHandle::MoveXY:
			return EMouseCursor::CardinalCross;
		case EHandle::Scale:
			if (Target)
			{
				const FVector2D Middle = (Target->Points[0] + Target->Points[2]) * 0.5;
				return ResizeAlong(Target->Points[FMath::Clamp(Hit.Index, 0, 7)] - Middle);
			}
			return EMouseCursor::ResizeSouthEast;
		case EHandle::Shear:
			if (Target)
			{
				return ResizeAlong(Hit.Index % 2 == 0 ? Target->Points[1] - Target->Points[0] : Target->Points[3] - Target->Points[0]);
			}
			return EMouseCursor::ResizeLeftRight;
		default:
			return EMouseCursor::Default;
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Edits
	// ---------------------------------------------------------------------------------------------

	bool FGizmoLayer::BeginEdit(const TArray<int32>& TargetIndices, uint8 Channels, bool bPivot, const FText& Description)
	{
		if (Edit.bActive)
		{
			return false;
		}
		TArray<FEdited> Widgets;
		for (const int32 TargetIndex : TargetIndices)
		{
			if (!Targets.IsValidIndex(TargetIndex) || !Targets[TargetIndex].IsUsable() || !Targets[TargetIndex].Preview.IsValid())
			{
				continue;
			}
			const FTarget& Target = Targets[TargetIndex];
			FEdited& Edited = Widgets.AddDefaulted_GetRef();
			Edited.Start = Target;
			const UWidget* Authored = Target.Template.IsValid() ? Target.Template.Get() : Target.Preview.Get();
			Edited.AuthoredStart = Authored->GetRenderTransform();
			Edited.AuthoredPivotStart = Authored->GetRenderTransformPivot();
			Edited.Pose = Target.Pose;
			Edited.Pivot = Target.Pivot;
		}
		if (Widgets.Num() == 0)
		{
			return false;
		}

		Edit.Transaction = MakeUnique<FScopedTransaction>(Description);
		for (const FEdited& Edited : Widgets)
		{
			if (UWidget* EditedTemplate = Edited.Start.Template.Get())
			{
				EditedTemplate->Modify();
			}
		}

		// The channels a track of the open animation takes, keyed at the playhead - never the pivot, which isn't animated.
		const TSharedPtr<SWidgetAnimTimeline> Panel = KeyPanel.Pin();
		if (!bPivot && Channels != WidgetGizmoChannel::None && Panel.IsValid() && Panel->CanGizmoKey())
		{
			TArray<UWidget*> PanelWidgets;
			TArray<uint8> Wanted;
			for (const FEdited& Edited : Widgets)
			{
				PanelWidgets.Add(Edited.Start.PanelWidget.Get());
				Wanted.Add(Edited.Start.PanelWidget.IsValid() ? Channels : static_cast<uint8>(WidgetGizmoChannel::None));
			}
			TArray<uint8> Keyed;
			if (Panel->BeginGizmoKeying(PanelWidgets, Wanted, Keyed))
			{
				Edit.bKeying = true;
				Edit.Panel = Panel;
				for (int32 Index = 0; Index < Widgets.Num(); ++Index)
				{
					Widgets[Index].Keyed = Keyed.IsValidIndex(Index) ? Keyed[Index] : static_cast<uint8>(WidgetGizmoChannel::None);
				}
			}
		}

		Edit.bActive = true;
		Edit.bChanged = false;
		Edit.bPivot = bPivot;
		Edit.Widgets = MoveTemp(Widgets);
		return true;
	}

	void FGizmoLayer::SetPose(int32 EditedIndex, const FWidgetTransform& NewPose, const FVector2D& NewPivot)
	{
		if (!Edit.bActive || !Edit.Widgets.IsValidIndex(EditedIndex))
		{
			return;
		}
		FEdited& Edited = Edit.Widgets[EditedIndex];
		Edited.Pose = NewPose;
		Edited.Pivot = NewPivot;
		Edit.bChanged = true;

		const FWidgetTransform& From = Edited.Start.Pose;

		// Keyed channels: their keys moved by the change.
		if (Edited.Keyed != WidgetGizmoChannel::None)
		{
			if (const TSharedPtr<SWidgetAnimTimeline> Panel = Edit.Panel.Pin())
			{
				Panel->UpdateGizmoKeying(EditedIndex, From, NewPose);
			}
		}

		// The rest onto the widget as authored, by the same change - the pose shown at the begin may be posed by the animation,
		// so the change, not the value shown, is what the template takes. Unposed, the two are the same.
		FWidgetTransform Authored = Edited.AuthoredStart;
		const FWidgetTransform& AuthoredFrom = Edited.AuthoredStart;
		if (!(Edited.Keyed & WidgetGizmoChannel::Translation))
		{
			Authored.Translation = AuthoredFrom.Translation + (NewPose.Translation - From.Translation);
		}
		if (!(Edited.Keyed & WidgetGizmoChannel::Scale))
		{
			Authored.Scale = FVector2D(
				RoundTo(Rescale(AuthoredFrom.Scale.X, From.Scale.X, NewPose.Scale.X), 0.0001),
				RoundTo(Rescale(AuthoredFrom.Scale.Y, From.Scale.Y, NewPose.Scale.Y), 0.0001));
		}
		if (!(Edited.Keyed & WidgetGizmoChannel::Shear))
		{
			Authored.Shear = AuthoredFrom.Shear + (NewPose.Shear - From.Shear);
		}
		if (!(Edited.Keyed & WidgetGizmoChannel::Angle))
		{
			Authored.Angle = AuthoredFrom.Angle + (NewPose.Angle - From.Angle);
		}

		UWidget* EditedTemplate = Edited.Start.Template.Get();
		UWidget* EditedPreview = Edited.Start.Preview.Get();
		if (EditedTemplate)
		{
			EditedTemplate->SetRenderTransform(Authored);
			if (Edit.bPivot)
			{
				EditedTemplate->SetRenderTransformPivot(NewPivot);
			}
		}
		// The preview shows the edit at once; a panel posing it poses it again next tick, from the keys and the template.
		if (EditedPreview && EditedPreview != EditedTemplate)
		{
			EditedPreview->SetRenderTransform(NewPose);
			if (Edit.bPivot)
			{
				EditedPreview->SetRenderTransformPivot(NewPivot);
			}
		}
	}

	void FGizmoLayer::EndEdit(bool bCancel)
	{
		if (!Edit.bActive)
		{
			return;
		}
		const TSharedPtr<SWidgetAnimTimeline> Panel = Edit.Panel.Pin();
		const bool bKeep = !bCancel && Edit.bChanged;

		if (Edit.bKeying && Panel.IsValid())
		{
			Panel->EndGizmoKeying(/*bCancel=*/!bKeep);
		}

		if (!bKeep)
		{
			// Everything as it was at the begin, and no undo step.
			for (const FEdited& Edited : Edit.Widgets)
			{
				UWidget* EditedTemplate = Edited.Start.Template.Get();
				if (EditedTemplate)
				{
					EditedTemplate->SetRenderTransform(Edited.AuthoredStart);
					EditedTemplate->SetRenderTransformPivot(Edited.AuthoredPivotStart);
				}
				UWidget* EditedPreview = Edited.Start.Preview.Get();
				if (EditedPreview && EditedPreview != EditedTemplate)
				{
					EditedPreview->SetRenderTransform(Edited.Start.Pose);
					EditedPreview->SetRenderTransformPivot(Edited.Start.Pivot);
				}
			}
			if (Edit.Transaction.IsValid())
			{
				Edit.Transaction->Cancel();
			}
		}
		else
		{
			// One undo step. The templates changed are told so, as a Details edit tells them: the Widget Animations panel puts its
			// poses back, takes the edit onto its preview and reads the widgets as authored again (FWidgetAnimAssetPanelSource::
			// OnObjectPropertyChanged) - without it a posed preview kept the old authored pose under the animation.
			Edit.Transaction.Reset();
			FProperty* TransformProperty = UWidget::StaticClass()->FindPropertyByName(TEXT("RenderTransform"));
			FProperty* PivotProperty = UWidget::StaticClass()->FindPropertyByName(TEXT("RenderTransformPivot"));
			for (const FEdited& Edited : Edit.Widgets)
			{
				UWidget* EditedTemplate = Edited.Start.Template.Get();
				if (!EditedTemplate)
				{
					continue;
				}
				const FWidgetTransform Now = EditedTemplate->GetRenderTransform();
				const bool bTransformChanged = !Now.Translation.Equals(Edited.AuthoredStart.Translation, 1.e-6)
					|| !Now.Scale.Equals(Edited.AuthoredStart.Scale, 1.e-6)
					|| !Now.Shear.Equals(Edited.AuthoredStart.Shear, 1.e-6)
					|| !FMath::IsNearlyEqual(Now.Angle, Edited.AuthoredStart.Angle, 1.e-6f);
				const bool bPivotChanged = Edit.bPivot && !EditedTemplate->GetRenderTransformPivot().Equals(Edited.AuthoredPivotStart, 1.e-6);
				if (!bTransformChanged && !bPivotChanged)
				{
					continue;
				}
				EditedTemplate->MarkPackageDirty();
				if (UWidgetBlueprint* OwningBlueprint = EditedTemplate->GetTypedOuter<UWidgetBlueprint>())
				{
					OwningBlueprint->Status = BS_Dirty;
				}
				if (TransformProperty && bTransformChanged)
				{
					FPropertyChangedEvent TransformEvent(TransformProperty, EPropertyChangeType::ValueSet);
					FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(EditedTemplate, TransformEvent);
				}
				if (PivotProperty && bPivotChanged)
				{
					FPropertyChangedEvent PivotEvent(PivotProperty, EPropertyChangeType::ValueSet);
					FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(EditedTemplate, PivotEvent);
				}
			}
		}

		Edit.Transaction.Reset();
		Edit.Widgets.Reset();
		Edit.Panel.Reset();
		SnapX.Reset();
		SnapY.Reset();
		Guides.Reset();
		Edit.bActive = false;
		Edit.bChanged = false;
		Edit.bPivot = false;
		Edit.bKeying = false;
		RequestRepaint();
	}

	void FGizmoLayer::Reset(int32 TargetIndex, EReset What)
	{
		if (!Targets.IsValidIndex(TargetIndex) || !Targets[TargetIndex].IsUsable() || Edit.bActive)
		{
			return;
		}
		const FTarget& Target = Targets[TargetIndex];
		FWidgetTransform NewPose = Target.Pose;
		FVector2D NewPivot = Target.Pivot;
		uint8 Channels = WidgetGizmoChannel::None;
		bool bPivot = false;
		FText Description;
		switch (What)
		{
		case EReset::Pivot:
			NewPivot = FVector2D(0.5, 0.5);
			NewPose = CompensatePivot(Target, NewPivot);
			bPivot = true;
			Description = LOCTEXT("ResetPivotTransaction", "Reset Widget Pivot");
			break;
		case EReset::Position:
			NewPose.Translation = FVector2D::ZeroVector;
			Channels = WidgetGizmoChannel::Translation;
			Description = LOCTEXT("ResetPositionTransaction", "Reset Widget Position");
			break;
		case EReset::PositionX:
			NewPose.Translation.X = 0.0;
			Channels = WidgetGizmoChannel::Translation;
			Description = LOCTEXT("ResetPositionXTransaction", "Reset Widget Position X");
			break;
		case EReset::PositionY:
			NewPose.Translation.Y = 0.0;
			Channels = WidgetGizmoChannel::Translation;
			Description = LOCTEXT("ResetPositionYTransaction", "Reset Widget Position Y");
			break;
		case EReset::Scale:
			NewPose.Scale = FVector2D(1.0, 1.0);
			Channels = WidgetGizmoChannel::Scale;
			Description = LOCTEXT("ResetScaleTransaction", "Reset Widget Scale");
			break;
		case EReset::Rotation:
			NewPose.Angle = 0.f;
			Channels = WidgetGizmoChannel::Angle;
			Description = LOCTEXT("ResetRotationTransaction", "Reset Widget Rotation");
			break;
		case EReset::Shear:
			NewPose.Shear = FVector2D::ZeroVector;
			Channels = WidgetGizmoChannel::Shear;
			Description = LOCTEXT("ResetShearTransaction", "Reset Widget Shear");
			break;
		case EReset::All:
			NewPose = FWidgetTransform();
			Channels = WidgetGizmoChannel::All;
			Description = LOCTEXT("ResetTransformTransaction", "Reset Widget Render Transform");
			break;
		}
		if (BeginEdit({ TargetIndex }, Channels, bPivot, Description))
		{
			SetPose(0, NewPose, NewPivot);
			EndEdit(/*bCancel=*/false);
		}
	}

	void FGizmoLayer::OpenResetMenu(const FHit& Hit, const FVector2D& ScreenPosition, const FWidgetPath& OwnerPath)
	{
		if (!Targets.IsValidIndex(Hit.Target) || !OwnerPath.IsValid())
		{
			return;
		}

		// Found again by its widgets when picked: the targets are gathered again every paint.
		const TWeakPtr<FGizmoLayer> WeakLayer = AsShared();
		const TWeakObjectPtr<UWidget> WeakTemplate = Targets[Hit.Target].Template;
		const TWeakObjectPtr<UWidget> WeakPreview = Targets[Hit.Target].Preview;
		const bool bKeyed = (Targets[Hit.Target].KeyableChannels & GetHandleChannel(Hit.Handle)) != 0;

		FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);
		auto Add = [&Menu, WeakLayer, WeakTemplate, WeakPreview](const FText& Label, const FText& Tip, EReset What)
		{
			Menu.AddMenuEntry(Label, Tip, FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakLayer, WeakTemplate, WeakPreview, What]()
			{
				if (const TSharedPtr<FGizmoLayer> Layer = WeakLayer.Pin())
				{
					const int32 Index = Layer->Targets.IndexOfByPredicate([&WeakTemplate, &WeakPreview](const FTarget& Target)
					{
						return Target.Template == WeakTemplate && Target.Preview == WeakPreview;
					});
					Layer->Reset(Index, What);
				}
			})));
		};

		Menu.BeginSection(NAME_None, bKeyed ? LOCTEXT("ResetKeyedSection", "Reset (keyed at the playhead)") : LOCTEXT("ResetSection", "Reset"));
		switch (Hit.Handle)
		{
		case EHandle::Pivot:
			Add(LOCTEXT("ResetPivot", "Reset Pivot to Center"), LOCTEXT("ResetPivotTip", "Render Transform Pivot back to (0.5, 0.5) - the widget stays where it is."), EReset::Pivot);
			break;
		case EHandle::MoveX:
			Add(LOCTEXT("ResetPositionX", "Reset Position X"), LOCTEXT("ResetPositionXTip", "Translation X back to 0."), EReset::PositionX);
			Add(LOCTEXT("ResetPositionBoth", "Reset Position"), LOCTEXT("ResetPositionTip", "Translation back to (0, 0)."), EReset::Position);
			break;
		case EHandle::MoveY:
			Add(LOCTEXT("ResetPositionY", "Reset Position Y"), LOCTEXT("ResetPositionYTip", "Translation Y back to 0."), EReset::PositionY);
			Add(LOCTEXT("ResetPositionBoth", "Reset Position"), LOCTEXT("ResetPositionTip", "Translation back to (0, 0)."), EReset::Position);
			break;
		case EHandle::MoveXY:
			Add(LOCTEXT("ResetPositionBoth", "Reset Position"), LOCTEXT("ResetPositionTip", "Translation back to (0, 0)."), EReset::Position);
			break;
		case EHandle::Scale:
			Add(LOCTEXT("ResetScale", "Reset Scale"), LOCTEXT("ResetScaleTip", "Scale back to (1, 1)."), EReset::Scale);
			break;
		case EHandle::Rotate:
			Add(LOCTEXT("ResetRotation", "Reset Rotation"), LOCTEXT("ResetRotationTip", "Angle back to 0."), EReset::Rotation);
			break;
		case EHandle::Shear:
			Add(LOCTEXT("ResetShear", "Reset Shear"), LOCTEXT("ResetShearTip", "Shear back to (0, 0)."), EReset::Shear);
			break;
		default:
			return;
		}
		Menu.EndSection();
		Menu.BeginSection(NAME_None);
		Add(LOCTEXT("ResetAll", "Reset Render Transform"), LOCTEXT("ResetAllTip", "Translation, Scale, Shear and Angle back to their defaults (the pivot is kept)."), EReset::All);
		Menu.EndSection();

		// Over the designer the handle was clicked in: the menu belongs to its window.
		FSlateApplication::Get().PushMenu(OwnerPath.GetLastWidget(), OwnerPath, Menu.MakeWidget(), ScreenPosition, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	}

	// ---------------------------------------------------------------------------------------------
	// Pressing and dragging
	// ---------------------------------------------------------------------------------------------

	void FGizmoLayer::Press(const FHit& Hit, const FVector2D& Screen)
	{
		if (Hit.Handle == EHandle::Chip)
		{
			switch (static_cast<EChip>(Hit.Index))
			{
			case EChip::Move: GizmoMode() = EMode::Move; break;
			case EChip::Scale: GizmoMode() = EMode::Scale; break;
			case EChip::Rotate: GizmoMode() = EMode::Rotate; break;
			case EChip::Shear: GizmoMode() = EMode::Shear; break;
			case EChip::Hide: GizmoHidden() = true; break;
			default: break;
			}
			RepaintAll();
			return;
		}
		if (!Targets.IsValidIndex(Hit.Target) || Edit.bActive)
		{
			return;
		}

		const FTarget& Target = Targets[Hit.Target];
		ResetPress();
		Pressing.bPressed = true;
		Pressing.Hit = Hit;
		Pressing.PressCanvas = ScreenToCanvas(Screen);
		Pressing.LastScreen = Screen;
		Pressing.GrabOffset = GetHandleCanvas(Target, Hit) - Pressing.PressCanvas;
		Pressing.RotateLastAngle = AngleOf(Pressing.PressCanvas - Target.PivotCanvas);
		Pressing.RotateTurned = 0.0;

		// Shear: the grabbed edge slides along itself - by whichever of Shear X or Y moves it along itself most (top and bottom
		// slide sideways, the sides up and down, whatever order Slate composes the shear in).
		if (Hit.Handle == EHandle::Shear)
		{
			const FVector2D EdgeDirection = (Hit.Index % 2 == 0 ? Target.Points[1] - Target.Points[0] : Target.Points[3] - Target.Points[0]).GetSafeNormal();
			const FVector2D BoxPoint = BoxFraction(4 + Hit.Index) * Target.Size;
			double BestScore = -1.0;
			for (int32 Component = 0; Component < 2; ++Component)
			{
				FWidgetTransform Nudged = Target.Pose;
				double& Angle = Component == 0 ? Nudged.Shear.X : Nudged.Shear.Y;
				Angle = FMath::Clamp(Angle + 1.0, -89.0, 89.0);
				const FVector2D Moved = Target.ToCanvas(Nudged, Target.Pivot, BoxPoint) - Target.ToCanvas(Target.Pose, Target.Pivot, BoxPoint);
				const double Score = FMath::Abs(FVector2D::DotProduct(Moved, EdgeDirection));
				if (Score > BestScore)
				{
					BestScore = Score;
					Pressing.ShearComponent = Component;
				}
			}
		}
		RequestRepaint();
	}

	void FGizmoLayer::PressMove(const FVector2D& Screen, const FModifierKeysState& Modifiers)
	{
		if (!Pressing.bPressed)
		{
			return;
		}
		Pressing.LastScreen = Screen;
		const FVector2D Mouse = ScreenToCanvas(Screen);
		if (!Pressing.bDragging)
		{
			if (FVector2D::Distance(Mouse, Pressing.PressCanvas) < DragThreshold)
			{
				return;
			}

			FText Description;
			switch (Pressing.Hit.Handle)
			{
			case EHandle::Pivot: Description = LOCTEXT("MovePivot", "Move Widget Pivot"); break;
			case EHandle::Rotate: Description = LOCTEXT("RotateWidget", "Rotate Widget"); break;
			case EHandle::Scale: Description = LOCTEXT("ScaleWidget", "Scale Widget"); break;
			case EHandle::Shear: Description = LOCTEXT("ShearWidget", "Shear Widget"); break;
			default: Description = LOCTEXT("MoveWidget", "Move Widget"); break;
			}
			if (!BeginEdit({ Pressing.Hit.Target }, GetHandleChannel(Pressing.Hit.Handle), Pressing.Hit.Handle == EHandle::Pivot, Description))
			{
				ResetPress();
				RequestRepaint();
				return;
			}
			Pressing.bDragging = true;
			if (Pressing.Hit.Handle != EHandle::Pivot && Pressing.Hit.Handle != EHandle::Rotate && Pressing.Hit.Handle != EHandle::Shear)
			{
				GatherSnapLines(Edit.Widgets[0].Start);
			}
		}
		UpdateDrag(Mouse, Modifiers);
		RequestRepaint();
	}

	void FGizmoLayer::UpdateDrag(const FVector2D& Mouse, const FModifierKeysState& Modifiers)
	{
		if (!Edit.bActive || Edit.Widgets.Num() == 0)
		{
			return;
		}
		const UWidgetGizmoSettings& Settings = UWidgetGizmoSettings::Get();
		const FTarget& Start = Edit.Widgets[0].Start;
		const bool bShift = Modifiers.IsShiftDown();
		const bool bCtrl = Modifiers.IsControlDown() || Modifiers.IsCommandDown();
		const FVector2D Wanted = Mouse + Pressing.GrabOffset;

		FWidgetTransform NewPose = Start.Pose;
		FVector2D NewPivot = Start.Pivot;

		switch (Pressing.Hit.Handle)
		{
		case EHandle::MoveX:
		case EHandle::MoveY:
		case EHandle::MoveXY:
		{
			// Translation is in the parent's units: one unit moves the widget by these on the canvas.
			const FVector2D Delta = Mouse - Pressing.PressCanvas;
			const FVector2D PerX = Start.BoxToCanvas.ApplyVector(FVector2D(1.0, 0.0));
			const FVector2D PerY = Start.BoxToCanvas.ApplyVector(FVector2D(0.0, 1.0));
			FVector2D Units = FVector2D::ZeroVector;
			if (Pressing.Hit.Handle == EHandle::MoveX)
			{
				Units.X = Project(Delta, PerX);
			}
			else if (Pressing.Hit.Handle == EHandle::MoveY)
			{
				Units.Y = Project(Delta, PerY);
			}
			else
			{
				Units = SolveTwo(PerX, PerY, Delta);
				if (bShift)
				{
					// Kept on the axis it has moved along most, on screen.
					if (FMath::Abs(Units.X) * PerX.Size() >= FMath::Abs(Units.Y) * PerY.Size())
					{
						Units.Y = 0.0;
					}
					else
					{
						Units.X = 0.0;
					}
				}
			}
			if (Units.X != 0.0)
			{
				NewPose.Translation.X = RoundTo(Start.Pose.Translation.X + Units.X, 0.1);
			}
			if (Units.Y != 0.0)
			{
				NewPose.Translation.Y = RoundTo(Start.Pose.Translation.Y + Units.Y, 0.1);
			}

			// Smart guides: the box's edges and middle onto the panel's and its neighbors' (upright, so one canvas pixel is 1 / PerX.X).
			Guides.Reset();
			if (UseGuides(Modifiers))
			{
				const bool bMovesX = Pressing.Hit.Handle == EHandle::MoveX || (Pressing.Hit.Handle == EHandle::MoveXY && Units.X != 0.0);
				const bool bMovesY = Pressing.Hit.Handle == EHandle::MoveY || (Pressing.Hit.Handle == EHandle::MoveXY && Units.Y != 0.0);
				const FVector2D Snap = SnapBox(Start, NewPose, bMovesX, bMovesY);
				NewPose.Translation.X = RoundTo(NewPose.Translation.X + Snap.X / PerX.X, 0.01);
				NewPose.Translation.Y = RoundTo(NewPose.Translation.Y + Snap.Y / PerY.Y, 0.01);
			}
			Readout = FString::Printf(TEXT("X %.1f   Y %.1f"), NewPose.Translation.X, NewPose.Translation.Y);
			break;
		}

		case EHandle::Scale:
		{
			// The grabbed point follows the mouse, the box scaling about the pivot (as the Render Transform scales). Its place is
			// linear in Scale X and Y, so one step of each says exactly how far to go. Corners keep the proportions (Shift: free);
			// edges scale their one axis (Shift: both, in proportion).
			const int32 Index = FMath::Clamp(Pressing.Hit.Index, 0, 7);
			const FVector2D BoxPoint = BoxFraction(Index) * Start.Size;
			const FVector2D StartScale = Start.Pose.Scale;
			auto At = [&Start, &BoxPoint](double ScaleX, double ScaleY)
			{
				FWidgetTransform Scaled = Start.Pose;
				Scaled.Scale = FVector2D(ScaleX, ScaleY);
				return Start.ToCanvas(Scaled, Start.Pivot, BoxPoint);
			};
			const FVector2D From = At(StartScale.X, StartScale.Y);
			const FVector2D PerX = At(StartScale.X + 1.0, StartScale.Y) - From;
			const FVector2D PerY = At(StartScale.X, StartScale.Y + 1.0) - From;
			const FVector2D Delta = Wanted - From;
			const bool bCorner = Index < 4;
			const bool bSides = Index == 5 || Index == 7;
			const bool bProportional = bCorner != bShift;

			FVector2D NewScale = StartScale;
			if (bProportional)
			{
				// Both scales grow by one factor - the grabbed point moves straight out from the pivot.
				const FVector2D Along = bCorner ? PerX * StartScale.X + PerY * StartScale.Y : (bSides ? PerX * StartScale.X : PerY * StartScale.Y);
				if (Along.SizeSquared() > 1.e-12)
				{
					NewScale = StartScale * (1.0 + Project(Delta, Along));
				}
			}
			else if (bCorner)
			{
				NewScale = StartScale + SolveTwo(PerX, PerY, Delta);
			}
			else if (bSides)
			{
				NewScale.X = StartScale.X + Project(Delta, PerX);
			}
			else
			{
				NewScale.Y = StartScale.Y + Project(Delta, PerY);
			}

			// Smart guides: the edges the handle moves onto the panel's and its neighbors'.
			Guides.Reset();
			if (UseGuides(Modifiers))
			{
				SnapScale(Start, Index, bProportional, NewScale);
			}
			NewPose.Scale = FVector2D(RoundTo(NewScale.X, 0.001), RoundTo(NewScale.Y, 0.001));
			Readout = FString::Printf(TEXT("Scale %.3f x %.3f"), NewPose.Scale.X, NewPose.Scale.Y);
			break;
		}

		case EHandle::Rotate:
		{
			// Turned by how far the mouse has gone round the pivot since the press - every turn counted.
			const FVector2D FromCenter = Mouse - Start.PivotCanvas;
			if (FromCenter.SizeSquared() > 16.0)
			{
				const double MouseAngle = AngleOf(FromCenter);
				double Step = MouseAngle - Pressing.RotateLastAngle;
				while (Step > UE_DOUBLE_PI)
				{
					Step -= 2.0 * UE_DOUBLE_PI;
				}
				while (Step < -UE_DOUBLE_PI)
				{
					Step += 2.0 * UE_DOUBLE_PI;
				}
				Pressing.RotateTurned += Step;
				Pressing.RotateLastAngle = MouseAngle;
			}
			// A positive Angle turns clockwise on screen, as the mouse going round clockwise does - unless the widget is drawn mirrored.
			const double Direction = Start.BoxToCanvas.Determinant() < 0.0 ? -1.0 : 1.0;
			double NewAngle = Start.Pose.Angle + Direction * FMath::RadiansToDegrees(Pressing.RotateTurned);
			const double Snap = FMath::Max(0.1, static_cast<double>(Settings.RotationSnap));
			const double Magnet = Settings.RotationMagnetAngle;
			if (bShift)
			{
				NewAngle = RoundTo(NewAngle, Snap);
			}
			else if (Magnet > 0.0 && FMath::Abs(NewAngle - RoundTo(NewAngle, Magnet)) <= Settings.RotationMagnetRange)
			{
				// Caught on a Rotation Magnet angle (every 45 degrees by default) while near it.
				NewAngle = RoundTo(NewAngle, Magnet);
			}
			else
			{
				NewAngle = RoundTo(NewAngle, 0.01);
			}
			NewPose.Angle = static_cast<float>(NewAngle);
			Readout = FString::Printf(TEXT("%.2f\u00B0"), NewAngle);
			break;
		}

		case EHandle::Shear:
		{
			// The grabbed edge's middle follows the mouse along the edge. Slate shears by the tangent of the angle, and the
			// point moves linearly with that tangent - so it is solved for the tangent, then turned back into degrees.
			const int32 Edge = FMath::Clamp(Pressing.Hit.Index, 0, 3);
			const FVector2D BoxPoint = BoxFraction(4 + Edge) * Start.Size;
			const int32 Component = Pressing.ShearComponent;
			auto At = [&Start, &BoxPoint, Component](double Slope)
			{
				FWidgetTransform Sheared = Start.Pose;
				const double Degrees = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(Slope)), -89.0, 89.0);
				(Component == 0 ? Sheared.Shear.X : Sheared.Shear.Y) = Degrees;
				return Start.ToCanvas(Sheared, Start.Pivot, BoxPoint);
			};
			const double StartDegrees = Component == 0 ? Start.Pose.Shear.X : Start.Pose.Shear.Y;
			const double StartSlope = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(StartDegrees, -89.0, 89.0)));
			constexpr double SlopeStep = 0.01;
			const FVector2D From = At(StartSlope);
			const FVector2D PerSlope = (At(StartSlope + SlopeStep) - From) / SlopeStep;
			const double NewSlope = StartSlope + Project(Wanted - From, PerSlope);
			double NewDegrees = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan(NewSlope)), -89.0, 89.0);
			const double Snap = FMath::Max(0.1, static_cast<double>(Settings.ShearSnap));
			NewDegrees = bShift ? RoundTo(NewDegrees, Snap) : RoundTo(NewDegrees, 0.01);
			(Component == 0 ? NewPose.Shear.X : NewPose.Shear.Y) = NewDegrees;
			Readout = FString::Printf(TEXT("Shear %.2f\u00B0, %.2f\u00B0"), NewPose.Shear.X, NewPose.Shear.Y);
			break;
		}

		case EHandle::Pivot:
		{
			// The pivot goes to the point of the box under the mouse; Translation makes up for it (CompensatePivot), so the widget
			// stays exactly where it is drawn.
			const FAffine CanvasToBox = PivotedPose(Start.Pose, Start.GetPivotPx()).Then(Start.BoxToCanvas).Inverse();
			FVector2D NewPivotPx = CanvasToBox.Apply(Wanted);

			// Snapped to the box's corners, edge middles and middle when the mouse comes close to one (Ctrl: free).
			const double SnapDistance = Settings.PivotSnapDistance;
			if (!bCtrl && SnapDistance > 0.0)
			{
				double Nearest = SnapDistance;
				for (int32 Row = 0; Row < 3; ++Row)
				{
					for (int32 Column = 0; Column < 3; ++Column)
					{
						const FVector2D Fraction(Column * 0.5, Row * 0.5);
						const double Distance = FVector2D::Distance(Start.ToCanvas(Start.Pose, Start.Pivot, Fraction * Start.Size), Wanted);
						if (Distance <= Nearest)
						{
							Nearest = Distance;
							NewPivotPx = Fraction * Start.Size;
						}
					}
				}
			}

			NewPivot = FVector2D(
				Start.Size.X > 0.0 ? RoundTo(NewPivotPx.X / Start.Size.X, 0.0001) : Start.Pivot.X,
				Start.Size.Y > 0.0 ? RoundTo(NewPivotPx.Y / Start.Size.Y, 0.0001) : Start.Pivot.Y);
			NewPose = CompensatePivot(Start, NewPivot);
			Readout = FString::Printf(TEXT("Pivot %.3f, %.3f"), NewPivot.X, NewPivot.Y);
			break;
		}

		default:
			return;
		}

		ReadoutAt = Mouse;
		SetPose(0, NewPose, NewPivot);
	}

	void FGizmoLayer::Release()
	{
		if (Pressing.bDragging)
		{
			EndEdit(/*bCancel=*/false);
		}
		ResetPress();
		RequestRepaint();
	}

	void FGizmoLayer::CancelDrag()
	{
		if (Pressing.bDragging)
		{
			EndEdit(/*bCancel=*/true);
		}
		ResetPress();
		RequestRepaint();
	}

	// ---------------------------------------------------------------------------------------------
	// Keys and typed values
	// ---------------------------------------------------------------------------------------------

	void FGizmoLayer::OnGizmoKey(EGizmoKey Which, const FVector2D& Screen)
	{
		if (Pressing.bPressed)
		{
			return;
		}
		if (Which == EGizmoKey::Hide)
		{
			EndSession(/*bCancel=*/false);
			GizmoHidden() = !GizmoHidden();
			RepaintAll();
			return;
		}

		EOp Op = EOp::Grab;
		EMode Mode = EMode::Move;
		switch (Which)
		{
		case EGizmoKey::Scale: Op = EOp::Scale; Mode = EMode::Scale; break;
		case EGizmoKey::Rotate: Op = EOp::Rotate; Mode = EMode::Rotate; break;
		case EGizmoKey::Shear: Op = EOp::Shear; Mode = EMode::Shear; break;
		default: break;
		}

		// A mode's key again, with nothing typed: back to Move.
		if (Op != EOp::Grab && Session.Op == Op && !HasTyped())
		{
			EndSession(/*bCancel=*/false);
			GizmoMode() = EMode::Move;
			GizmoHidden() = false;
			RepaintAll();
			return;
		}

		EndSession(/*bCancel=*/false);
		GizmoHidden() = false;
		GizmoMode() = Mode;
		StartSession(Op, Screen);
		RepaintAll();
	}

	void FGizmoLayer::StartSession(EOp Op, const FVector2D& Screen)
	{
		Session = FSession();
		Session.Op = Op;
		Session.StartMouse = ScreenToCanvas(Screen);
		Session.LastScreen = Screen;
		if (Op == EOp::Grab && !BeginSessionEdit())
		{
			Session = FSession();
		}
		RequestRepaint();
	}

	bool FGizmoLayer::BeginSessionEdit()
	{
		if (Edit.bActive)
		{
			return true;
		}
		TArray<int32> Indices;
		for (int32 TargetIndex = 0; TargetIndex < Targets.Num(); ++TargetIndex)
		{
			if (Targets[TargetIndex].IsUsable())
			{
				Indices.Add(TargetIndex);
			}
		}
		FText Description;
		switch (Session.Op)
		{
		case EOp::Scale: Description = LOCTEXT("ScaleWidget", "Scale Widget"); break;
		case EOp::Rotate: Description = LOCTEXT("RotateWidget", "Rotate Widget"); break;
		case EOp::Shear: Description = LOCTEXT("ShearWidget", "Shear Widget"); break;
		default: Description = LOCTEXT("MoveWidget", "Move Widget"); break;
		}
		if (!BeginEdit(Indices, GetOpChannel(Session.Op), /*bPivot=*/false, Description))
		{
			return false;
		}
		if (Session.Op == EOp::Grab)
		{
			GatherSnapLines(Edit.Widgets[0].Start);
		}
		return true;
	}

	void FGizmoLayer::ApplySession()
	{
		if (!Edit.bActive || Session.Op == EOp::None)
		{
			return;
		}
		double Typed[2] = { 0.0, 0.0 };
		const bool bHasTyped[2] = { ParseTyped(Session.Typed[0], Typed[0]), ParseTyped(Session.Typed[1], Typed[1]) };
		const bool bAnyTyped = HasTyped();
		const FVector2D Mouse = ScreenToCanvas(Session.LastScreen);

		// A Grab following the mouse: the first widget's box catches on the smart guides, and every widget moves by as much.
		FVector2D GrabDelta = Mouse - Session.StartMouse;
		Guides.Reset();
		if (Session.Op == EOp::Grab && !bAnyTyped && Edit.Widgets.Num() > 0 && UseGuides(FSlateApplication::Get().GetModifierKeys()))
		{
			const FTarget& Lead = Edit.Widgets[0].Start;
			const FVector2D PerX = Lead.BoxToCanvas.ApplyVector(FVector2D(1.0, 0.0));
			const FVector2D PerY = Lead.BoxToCanvas.ApplyVector(FVector2D(0.0, 1.0));
			FVector2D Units = SolveTwo(PerX, PerY, GrabDelta);
			if (Session.Axis == 0)
			{
				Units = FVector2D(Project(GrabDelta, PerX), 0.0);
			}
			else if (Session.Axis == 1)
			{
				Units = FVector2D(0.0, Project(GrabDelta, PerY));
			}
			FWidgetTransform LeadPose = Lead.Pose;
			LeadPose.Translation = FVector2D(RoundTo(Lead.Pose.Translation.X + Units.X, 0.1), RoundTo(Lead.Pose.Translation.Y + Units.Y, 0.1));
			GrabDelta += SnapBox(Lead, LeadPose, Session.Axis != 1, Session.Axis != 0);
		}

		for (int32 EditedIndex = 0; EditedIndex < Edit.Widgets.Num(); ++EditedIndex)
		{
			const FTarget& Start = Edit.Widgets[EditedIndex].Start;
			FWidgetTransform NewPose = Start.Pose;
			switch (Session.Op)
			{
			case EOp::Grab:
				if (bAnyTyped)
				{
					// Typed: the position itself, axis by axis; an axis not typed stays where it was.
					if (bHasTyped[0])
					{
						NewPose.Translation.X = Typed[0];
					}
					if (bHasTyped[1])
					{
						NewPose.Translation.Y = Typed[1];
					}
				}
				else
				{
					// Following the mouse, kept to the axis picked - and caught on a guide (see above).
					const FVector2D Delta = GrabDelta;
					const FVector2D PerX = Start.BoxToCanvas.ApplyVector(FVector2D(1.0, 0.0));
					const FVector2D PerY = Start.BoxToCanvas.ApplyVector(FVector2D(0.0, 1.0));
					FVector2D Units = SolveTwo(PerX, PerY, Delta);
					if (Session.Axis == 0)
					{
						Units = FVector2D(Project(Delta, PerX), 0.0);
					}
					else if (Session.Axis == 1)
					{
						Units = FVector2D(0.0, Project(Delta, PerY));
					}
					// Tenths, as a drag's - hundredths when caught on a guide, so it lands on it.
					const double Step = Guides.Num() > 0 ? 0.01 : 0.1;
					NewPose.Translation = FVector2D(RoundTo(Start.Pose.Translation.X + Units.X, Step), RoundTo(Start.Pose.Translation.Y + Units.Y, Step));
				}
				if (EditedIndex == 0)
				{
					Readout = FString::Printf(TEXT("X %.1f   Y %.1f"), NewPose.Translation.X, NewPose.Translation.Y);
					ReadoutAt = Mouse;
				}
				break;

			case EOp::Scale:
				if (Session.Axis == INDEX_NONE)
				{
					if (bHasTyped[0])
					{
						NewPose.Scale = FVector2D(Typed[0], Typed[0]);
					}
				}
				else
				{
					if (bHasTyped[0])
					{
						NewPose.Scale.X = Typed[0];
					}
					if (bHasTyped[1])
					{
						NewPose.Scale.Y = Typed[1];
					}
				}
				break;

			case EOp::Rotate:
				if (bHasTyped[0])
				{
					NewPose.Angle = static_cast<float>(Typed[0]);
				}
				break;

			case EOp::Shear:
				if (bHasTyped[0])
				{
					NewPose.Shear.X = FMath::Clamp(Typed[0], -89.0, 89.0);
				}
				if (bHasTyped[1])
				{
					NewPose.Shear.Y = FMath::Clamp(Typed[1], -89.0, 89.0);
				}
				break;

			default:
				break;
			}
			SetPose(EditedIndex, NewPose, Start.Pivot);
		}
		RequestRepaint();
	}

	void FGizmoLayer::SetSessionAxis(int32 NewAxis)
	{
		if (Session.Op == EOp::Rotate || Session.Axis == NewAxis)
		{
			return;
		}
		// Scale typed both ways first: that value goes on in both axes, so nothing changes until the next one is typed.
		if (Session.Op == EOp::Scale && Session.Axis == INDEX_NONE && !Session.Typed[0].IsEmpty())
		{
			Session.Typed[1] = Session.Typed[0];
		}
		Session.Axis = NewAxis;
		ApplySession();
		RequestRepaint();
	}

	bool FGizmoLayer::HandleSessionKey(const FKeyEvent& InKeyEvent)
	{
		if (Session.Op == EOp::None)
		{
			return false;
		}
		const FKey Key = InKeyEvent.GetKey();
		const bool bPlain = !InKeyEvent.IsControlDown() && !InKeyEvent.IsAltDown() && !InKeyEvent.IsCommandDown();

		if (Key == EKeys::Escape)
		{
			EndSession(/*bCancel=*/true);
			return true;
		}
		if (Key == EKeys::Enter)
		{
			EndSession(/*bCancel=*/false);
			return true;
		}
		if (bPlain && (Key == EKeys::X || Key == EKeys::Y))
		{
			SetSessionAxis(Key == EKeys::X ? 0 : 1);
			return true;
		}
		if (bPlain && Key == EKeys::Tab)
		{
			SetSessionAxis(Session.Axis == 0 ? 1 : 0);
			return true;
		}
		if (bPlain && Key == EKeys::BackSpace)
		{
			FString& Buffer = Session.Typed[Session.GetBufferIndex()];
			if (!Buffer.IsEmpty())
			{
				Buffer.LeftChopInline(1);
				ApplySession();
			}
			RequestRepaint();
			return true;
		}

		const TCHAR Character = bPlain ? GetTypedCharacter(Key) : 0;
		if (Character != 0)
		{
			// Move and Shear type into X until an axis is picked; the mouse stops driving a Grab once anything is typed.
			if ((Session.Op == EOp::Grab || Session.Op == EOp::Shear) && Session.Axis == INDEX_NONE)
			{
				Session.Axis = 0;
			}
			if (!BeginSessionEdit())
			{
				EndSession(/*bCancel=*/true);
				return true;
			}
			FString& Buffer = Session.Typed[Session.GetBufferIndex()];
			if (Character == TEXT('-'))
			{
				// Minus flips the sign, wherever it is typed.
				if (Buffer.StartsWith(TEXT("-")))
				{
					Buffer.RightChopInline(1);
				}
				else
				{
					Buffer.InsertAt(0, TEXT('-'));
				}
			}
			else if (Buffer.Len() < MaxTypedLength && (Character != TEXT('.') || !Buffer.Contains(TEXT("."))))
			{
				Buffer.AppendChar(Character);
			}
			ApplySession();
			return true;
		}

		// Any other key: what was set is kept, and the key goes on as usual.
		EndSession(/*bCancel=*/false);
		return false;
	}

	void FGizmoLayer::SessionMouseMove(const FVector2D& Screen)
	{
		if (Session.Op != EOp::Grab)
		{
			return;
		}
		Session.LastScreen = Screen;
		if (!HasTyped())
		{
			ApplySession();
		}
	}

	void FGizmoLayer::EndSession(bool bCancel)
	{
		if (Session.Op == EOp::None)
		{
			return;
		}
		Session = FSession();
		Readout.Reset();
		if (Edit.bActive && !Pressing.bPressed)
		{
			EndEdit(bCancel);
		}
		RequestRepaint();
	}

	// ---------------------------------------------------------------------------------------------
	// Drawing
	// ---------------------------------------------------------------------------------------------

	void FGizmoLayer::Paint(const TSet<FWidgetReference>& Selection, const FGeometry& DesignerGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId)
	{
		// The designer as it is painted now: its own local space is the canvas. DesignerGeometry is in its window's space, while
		// the mouse and the widgets' tick geometry are in desktop space - so the gizmo is drawn through DesignerGeometry and
		// placed and hit-tested through Designer, the same geometry moved onto the desktop (as the motion paths are).
		Designer = DesignerGeometry;
		if (const SWindow* Window = OutDrawElements.GetPaintWindow())
		{
			Designer.AppendTransform(FSlateLayoutTransform(FVector2f(FVector2D(Window->GetPositionInScreen()))));
		}
		PaintedFrame = GFrameCounter;
		TGuardValue<bool> PaintingGuard(bPainting, true);
		DesktopToCanvas = FAffine::From(Designer.GetAccumulatedRenderTransform()).Inverse();
		CanvasSize = FVector2D(DesignerGeometry.GetLocalSize());
		Chips.Reset();

		const UWidgetGizmoSettings& Settings = UWidgetGizmoSettings::Get();
		if (!Settings.bEnableGizmo)
		{
			EndSession(/*bCancel=*/true);
			if (Pressing.bPressed)
			{
				CancelDrag();
			}
			Targets.Reset();
			Hovered = FHit();
			return;
		}

		GatherTargets(Selection);
		for (FTarget& Target : Targets)
		{
			Target.bPlaced = Place(Target);
		}
		FindDesignerWidget();

		// A widget being edited no longer selected (an undo, a compile, another pick): the edit is kept where it is.
		if (Edit.bActive)
		{
			const bool bAllThere = !Edit.Widgets.ContainsByPredicate([this](const FEdited& Edited)
			{
				return !Targets.ContainsByPredicate([&Edited](const FTarget& Target) { return Target.IsSameWidget(Edited.Start); });
			});
			if (!bAllThere)
			{
				if (Pressing.bPressed)
				{
					Release();
				}
				EndSession(/*bCancel=*/false);
				EndEdit(/*bCancel=*/false);
			}
		}
		if (Session.Op != EOp::None && !HasTargets())
		{
			EndSession(/*bCancel=*/false);
		}
		if (Hovered.IsValid() && Hovered.Handle != EHandle::Chip && !Targets.IsValidIndex(Hovered.Target))
		{
			Hovered = FHit();
		}

		if (GizmoHidden() || !HasTargets())
		{
			return;
		}

		OutDrawElements.PushClip(FSlateClippingZone(DesignerGeometry));
		int32 MaxLayer = LayerId;
		for (int32 TargetIndex = 0; TargetIndex < Targets.Num(); ++TargetIndex)
		{
			const FTarget& Target = Targets[TargetIndex];
			if (Target.IsUsable())
			{
				MaxLayer = FMath::Max(MaxLayer, PaintTarget(Target, TargetIndex, DesignerGeometry, OutDrawElements, LayerId));
			}
		}

		// Smart guides lined up with, over the widgets and under the chips.
		for (const TPair<FVector2D, FVector2D>& Guide : Guides)
		{
			FSlateDrawElement::MakeLines(OutDrawElements, MaxLayer + 1, DesignerGeometry.ToPaintGeometry(),
				TArray<FVector2f>{ FVector2f(Guide.Key), FVector2f(Guide.Value) }, ESlateDrawEffect::None, Palette::Guide, /*bAntialias=*/true, 1.f);
		}

		// The chips over the first widget the gizmo acts on - one row, whatever is selected - and under them what is being typed.
		// With Show Mode Chips off, the row is only laid out: the typing bar goes where it would be.
		if (const FTarget* First = Targets.FindByPredicate([](const FTarget& Target) { return Target.IsUsable(); }))
		{
			LayOutChips(*First);
			if (Settings.bShowChips)
			{
				MaxLayer = PaintChips(DesignerGeometry, OutDrawElements, MaxLayer + 1);
			}
			else
			{
				Chips.Reset();
				ChipRowMax.Y = ChipRowMin.Y - 6.0;
				if (Hovered.Handle == EHandle::Chip)
				{
					Hovered = FHit();
				}
			}
			MaxLayer = PaintTyping(DesignerGeometry, OutDrawElements, MaxLayer + 1);
		}
		PaintReadout(DesignerGeometry, OutDrawElements, MaxLayer + 1);
		OutDrawElements.PopClip();
	}

	int32 FGizmoLayer::PaintTarget(const FTarget& Target, int32 TargetIndex, const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
	{
		const double GizmoScale = GetGizmoScale();
		const float Thickness = Palette::LineThickness * static_cast<float>(GizmoScale);
		const int32 UnderLayer = LayerId;
		const int32 LineLayer = LayerId + 1;
		const int32 FillLayer = LayerId + 2;
		const int32 TopLayer = LayerId + 3;

		auto ToPoint = [](const FVector2D& Point) { return FVector2f(Point); };

		// A line drawn twice: dark and wider under, colored over - so it reads on any background.
		auto Polyline = [&](const TArray<FVector2f>& Points, const FLinearColor& LineColor, float LineThickness)
		{
			if (Points.Num() < 2)
			{
				return;
			}
			FSlateDrawElement::MakeLines(OutDrawElements, UnderLayer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Palette::Underlay, /*bAntialias=*/true, LineThickness + 2.f);
			FSlateDrawElement::MakeLines(OutDrawElements, LineLayer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, LineColor, /*bAntialias=*/true, LineThickness);
		};
		auto Segment = [&](const FVector2D& From, const FVector2D& To, const FLinearColor& LineColor, float LineThickness)
		{
			Polyline(TArray<FVector2f>{ ToPoint(From), ToPoint(To) }, LineColor, LineThickness);
		};
		auto TurnedBox = [&](const FVector2D& Center, const FVector2D& BoxSize, double Angle, const FSlateBrush* Brush, const FLinearColor& BoxColor, int32 Layer)
		{
			const FVector2f Size2f(BoxSize);
			FSlateDrawElement::MakeBox(OutDrawElements, Layer,
				Geometry.ToPaintGeometry(Size2f, FSlateLayoutTransform(ToPoint(Center) - Size2f * 0.5f),
					FSlateRenderTransform(FQuat2f(static_cast<float>(Angle))), FVector2f(0.5f, 0.5f)),
				Brush, ESlateDrawEffect::None, BoxColor);
		};
		auto Disc = [&](const FVector2D& Center, double Diameter, const FLinearColor& DiscColor, int32 Layer)
		{
			TurnedBox(Center, FVector2D(Diameter, Diameter), 0.0, CircleBrush(), DiscColor, Layer);
		};
		auto Circle = [&](const FVector2D& Center, double Radius, double FromAngle, double ToAngle, int32 Steps)
		{
			TArray<FVector2f> Points;
			Points.Reserve(Steps + 1);
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const double Angle = FMath::Lerp(FromAngle, ToAngle, static_cast<double>(Step) / Steps);
				Points.Add(ToPoint(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius));
			}
			return Points;
		};

		const EMode Mode = GizmoMode();
		const FVector2D& Pivot = Target.PivotCanvas;
		const FVector2D AxisUp = -Target.AxisY;

		// A Grab kept to one axis: that axis, right across the designer.
		if (Session.Op == EOp::Grab && Session.Axis != INDEX_NONE)
		{
			const FVector2D Along = Session.Axis == 0 ? Target.AxisX : Target.AxisY;
			const double Reach = CanvasSize.Size() + 1.0;
			Segment(Pivot - Along * Reach, Pivot + Along * Reach, Session.Axis == 0 ? Palette::AxisX : Palette::AxisY, 1.f);
		}

		// The frame the Scale and Shear handles sit on, a little outside the widget's box - clear of the designer's own outline.
		if (Mode == EMode::Scale || Mode == EMode::Shear)
		{
			TArray<FVector2f> Outline;
			for (int32 Index = 0; Index <= 4; ++Index)
			{
				Outline.Add(ToPoint(Target.Frame[Index % 4]));
			}
			Polyline(Outline, Palette::Frame, 1.f);
		}

		switch (Mode)
		{
		case EMode::Move:
		{
			auto Arrow = [&](const FVector2D& Axis, const FLinearColor& ArrowColor)
			{
				const FVector2D Tip = Pivot + Axis * (Palette::ArrowLength * GizmoScale);
				const FVector2D HeadBase = Tip - Axis * (Palette::ArrowHeadLength * GizmoScale);
				const FVector2D Across(-Axis.Y, Axis.X);
				const double HalfWidth = Palette::ArrowHeadWidth * 0.5 * GizmoScale;
				Segment(Pivot + Axis * (Palette::ArrowGap * GizmoScale), HeadBase, ArrowColor, Thickness);

				// The head: its outline under, then filled with a fan of lines from the tip.
				const FVector2D Left = HeadBase + Across * HalfWidth;
				const FVector2D Right = HeadBase - Across * HalfWidth;
				FSlateDrawElement::MakeLines(OutDrawElements, UnderLayer, Geometry.ToPaintGeometry(),
					TArray<FVector2f>{ ToPoint(Tip), ToPoint(Left), ToPoint(Right), ToPoint(Tip) }, ESlateDrawEffect::None, Palette::Underlay, true, 3.f);
				constexpr int32 FanLines = 7;
				for (int32 Line = 0; Line < FanLines; ++Line)
				{
					const FVector2D Along = FMath::Lerp(Left, Right, static_cast<double>(Line) / (FanLines - 1));
					FSlateDrawElement::MakeLines(OutDrawElements, FillLayer, Geometry.ToPaintGeometry(),
						TArray<FVector2f>{ ToPoint(Tip), ToPoint(Along) }, ESlateDrawEffect::None, ArrowColor, true, 2.f);
				}
			};
			Arrow(Target.AxisX, IsHot(TargetIndex, EHandle::MoveX) ? Palette::Hot : Palette::AxisX);
			Arrow(AxisUp, IsHot(TargetIndex, EHandle::MoveY) ? Palette::Hot : Palette::AxisY);

			// The square between them: moves both ways.
			const bool bPlaneHot = IsHot(TargetIndex, EHandle::MoveXY) || Session.Op == EOp::Grab;
			const FVector2D PlaneCenter = Pivot + (Target.AxisX + AxisUp) * (Palette::PlaneOffset * GizmoScale);
			const double PlaneSize = Palette::PlaneSize * GizmoScale;
			const double PlaneAngle = AngleOf(Target.AxisX);
			TurnedBox(PlaneCenter, FVector2D(PlaneSize + 2.0, PlaneSize + 2.0), PlaneAngle, SquareBrush(), Palette::Underlay, UnderLayer);
			TurnedBox(PlaneCenter, FVector2D(PlaneSize, PlaneSize), PlaneAngle, SquareBrush(), bPlaneHot ? Palette::Hot : Palette::PlaneFill, FillLayer);
			break;
		}

		case EMode::Rotate:
		{
			const double Radius = Palette::RingRadius * GizmoScale;
			const bool bRingHot = IsHot(TargetIndex, EHandle::Rotate);
			Polyline(Circle(Pivot, Radius, 0.0, 2.0 * UE_DOUBLE_PI, 72), bRingHot ? Palette::Hot : Palette::Ring, bRingHot ? Thickness + 1.f : Thickness);

			// While turning: where it started, where it is, and the sweep between.
			if (Pressing.bDragging && Pressing.Hit.Handle == EHandle::Rotate && Pressing.Hit.Target == TargetIndex && Edit.Widgets.Num() > 0)
			{
				const double StartAngle = AngleOf(Pressing.PressCanvas - Edit.Widgets[0].Start.PivotCanvas);
				const double EndAngle = StartAngle + Pressing.RotateTurned;
				const FVector2D StartPoint = Pivot + FVector2D(FMath::Cos(StartAngle), FMath::Sin(StartAngle)) * Radius;
				const FVector2D EndPoint = Pivot + FVector2D(FMath::Cos(EndAngle), FMath::Sin(EndAngle)) * Radius;
				Segment(Pivot, StartPoint, Palette::RingSweep, 1.f);
				Segment(Pivot, EndPoint, Palette::Hot, 1.f);
				const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::Abs(Pressing.RotateTurned) / (2.0 * UE_DOUBLE_PI) * 72.0), 2, 720);
				FSlateDrawElement::MakeLines(OutDrawElements, FillLayer, Geometry.ToPaintGeometry(), Circle(Pivot, Radius, StartAngle, EndAngle, Steps),
					ESlateDrawEffect::None, Palette::RingSweep, true, Thickness + 3.f);
			}
			break;
		}

		case EMode::Scale:
		{
			const double GripSize = Palette::GripSize * GizmoScale;
			for (int32 Index = 0; Index < 8; ++Index)
			{
				const bool bGripHot = IsHot(TargetIndex, EHandle::Scale, Index);
				const double Size = bGripHot ? GripSize * 1.25 : GripSize;
				TurnedBox(Target.Frame[Index], FVector2D(Size + 2.0, Size + 2.0), Target.BoxAngle, SquareBrush(), Palette::GripOutline, FillLayer);
				TurnedBox(Target.Frame[Index], FVector2D(Size, Size), Target.BoxAngle, SquareBrush(), bGripHot ? Palette::Hot : Palette::GripFill, TopLayer);
			}
			break;
		}

		case EMode::Shear:
		{
			for (int32 Edge = 0; Edge < 4; ++Edge)
			{
				const bool bGripHot = IsHot(TargetIndex, EHandle::Shear, Edge);
				const FVector2D EdgeDirection = Edge % 2 == 0 ? Target.Points[1] - Target.Points[0] : Target.Points[3] - Target.Points[0];
				const double Angle = AngleOf(EdgeDirection);
				const double Grow = bGripHot ? 1.2 : 1.0;
				const FVector2D GripSize(Palette::ShearGripLength * GizmoScale * Grow, Palette::ShearGripThickness * GizmoScale * Grow);
				TurnedBox(Target.Frame[4 + Edge], GripSize + FVector2D(2.0, 2.0), Angle, RoundedBrush(), Palette::GripOutline, FillLayer);
				TurnedBox(Target.Frame[4 + Edge], GripSize, Angle, RoundedBrush(), bGripHot ? Palette::Hot : Palette::ShearGrip, TopLayer);
			}
			break;
		}
		}

		// The pivot - the gizmo's middle, dragged to move it: a ring with a cross through it, on top of everything.
		{
			const bool bPivotHot = IsHot(TargetIndex, EHandle::Pivot);
			const double PivotSize = Palette::PivotSize * GizmoScale;
			const double Radius = PivotSize * 0.5;
			const double CrossLength = Palette::PivotCross * GizmoScale;
			const FLinearColor RingColor = bPivotHot ? Palette::Hot : Palette::PivotRing;
			const FVector2D Directions[4] = { FVector2D(1.0, 0.0), FVector2D(-1.0, 0.0), FVector2D(0.0, 1.0), FVector2D(0.0, -1.0) };
			for (const FVector2D& Direction : Directions)
			{
				Segment(Pivot + Direction * (Radius + 1.0), Pivot + Direction * (Radius + CrossLength), RingColor, 1.5f);
			}
			Disc(Pivot, PivotSize + 2.0, Palette::Underlay, FillLayer);
			Disc(Pivot, PivotSize, RingColor, TopLayer);
			Disc(Pivot, FMath::Max(2.0, PivotSize - 5.0), Palette::PivotFill, TopLayer + 1);
		}
		return TopLayer + 1;
	}

	int32 FGizmoLayer::PaintChips(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
	{
		if (Chips.Num() == 0)
		{
			return LayerId;
		}
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		constexpr float PaddingX = 6.f;
		constexpr float KeyGap = 4.f;
		const EMode Mode = GizmoMode();

		// The row's background, then each chip on it.
		const FVector2D RowMin = ChipRowMin - FVector2D(2.0, 2.0);
		const FVector2D RowMax = ChipRowMax + FVector2D(2.0, 2.0);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			Geometry.ToPaintGeometry(FVector2f(RowMax - RowMin), FSlateLayoutTransform(FVector2f(RowMin))),
			RoundedBrush(), ESlateDrawEffect::None, Palette::ChipBackground);

		for (const FChip& Chip : Chips)
		{
			const bool bKeyChip = Chip.Kind == EChip::Key;
			const bool bActive = (Chip.Kind == EChip::Move && Mode == EMode::Move) || (Chip.Kind == EChip::Scale && Mode == EMode::Scale)
				|| (Chip.Kind == EChip::Rotate && Mode == EMode::Rotate) || (Chip.Kind == EChip::Shear && Mode == EMode::Shear);
			const bool bChipHovered = Hovered.Handle == EHandle::Chip && Hovered.Index == static_cast<int32>(Chip.Kind);
			const FLinearColor Fill = bKeyChip ? Palette::KeyChip
				: bActive ? (bChipHovered ? Palette::ChipActiveHovered : Palette::ChipActive) : (bChipHovered ? Palette::ChipIdleHovered : Palette::ChipIdle);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				Geometry.ToPaintGeometry(FVector2f(Chip.Size), FSlateLayoutTransform(FVector2f(Chip.TopLeft))),
				RoundedBrush(), ESlateDrawEffect::None, Fill);

			const float TextHeight = 12.f;
			FVector2f TextAt = FVector2f(Chip.TopLeft) + FVector2f(PaddingX, (Palette::ChipHeight - TextHeight) * 0.5f - 1.f);
			float LabelWidth = Chip.LabelWidth;
			if (bKeyChip)
			{
				// The record dot, as a keying toggle has.
				constexpr float DotSize = 6.f;
				const FVector2f DotAt = FVector2f(Chip.TopLeft) + FVector2f(PaddingX, (Palette::ChipHeight - DotSize) * 0.5f);
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, Geometry.ToPaintGeometry(FVector2f(DotSize, DotSize), FSlateLayoutTransform(DotAt)),
					CircleBrush(), ESlateDrawEffect::None, Palette::KeyChipDot);
				TextAt.X += 10.f;
				LabelWidth -= 10.f;
			}
			const FLinearColor TextColor = bKeyChip ? Palette::KeyChipText : (bActive ? Palette::ChipTextActive : Palette::ChipText);
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2,
				Geometry.ToPaintGeometry(FVector2f(LabelWidth + 2.f, TextHeight + 2.f), FSlateLayoutTransform(TextAt)),
				Chip.Label, Font, ESlateDrawEffect::None, TextColor);
			if (!Chip.Key.IsEmpty())
			{
				const FVector2f KeyAt = TextAt + FVector2f(LabelWidth + KeyGap, 0.f);
				const FLinearColor KeyColor = bKeyChip ? Palette::KeyChipText.CopyWithNewOpacity(0.7f)
					: (bActive ? Palette::ChipTextActive.CopyWithNewOpacity(0.6f) : Palette::ChipKeyText);
				FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2,
					Geometry.ToPaintGeometry(FVector2f(static_cast<float>(Chip.Size.X) - (KeyAt.X - static_cast<float>(Chip.TopLeft.X)), TextHeight + 2.f), FSlateLayoutTransform(KeyAt)),
					Chip.Key, Font, ESlateDrawEffect::None, KeyColor);
			}
		}
		return LayerId + 2;
	}

	int32 FGizmoLayer::PaintTyping(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
	{
		if (Session.Op == EOp::None)
		{
			return LayerId;
		}
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const FSlateFontInfo BoldFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		// What each field shows: what has been typed (with a caret where the keys type), else the value now, dimmed.
		FWidgetTransform Shown;
		if (Edit.bActive && Edit.Widgets.Num() > 0)
		{
			Shown = Edit.Widgets[0].Pose;
		}
		else if (const FTarget* First = Targets.FindByPredicate([](const FTarget& Target) { return Target.IsUsable(); }))
		{
			Shown = First->Pose;
		}

		struct FPart
		{
			FString Text;
			FLinearColor Color;
			bool bField = false;
			bool bActive = false;
			bool bBold = false;
		};
		TArray<FPart> Parts;
		auto Label = [&Parts](const FText& Text, const FLinearColor& Color, bool bBold)
		{
			FPart& Part = Parts.AddDefaulted_GetRef();
			Part.Text = Text.ToString();
			Part.Color = Color;
			Part.bBold = bBold;
		};
		auto Field = [this, &Parts](const FString& Name, int32 BufferIndex, double Now, bool bActiveField)
		{
			FPart& NamePart = Parts.AddDefaulted_GetRef();
			NamePart.Text = Name;
			NamePart.Color = Palette::TypingLabel;

			const FString& Typed = Session.Typed[BufferIndex];
			FPart& ValuePart = Parts.AddDefaulted_GetRef();
			const FString Value = Typed.IsEmpty() ? FString::SanitizeFloat(RoundTo(Now, 0.001)) : Typed;
			ValuePart.Text = bActiveField ? Value + TEXT("|") : Value;
			ValuePart.Color = Typed.IsEmpty() ? Palette::TypingHint : Palette::TypingValue;
			ValuePart.bField = true;
			ValuePart.bActive = bActiveField;
		};

		const int32 BufferIndex = Session.GetBufferIndex();
		switch (Session.Op)
		{
		case EOp::Grab:
			Label(LOCTEXT("TypingMove", "Move"), Palette::TypingLabel, true);
			Field(TEXT("X"), 0, Shown.Translation.X, HasTyped() && BufferIndex == 0);
			Field(TEXT("Y"), 1, Shown.Translation.Y, HasTyped() && BufferIndex == 1);
			Label(HasTyped() ? LOCTEXT("TypingGrabTyped", "X / Y / Tab axis   Enter set   Esc cancel")
				: (Session.Axis == INDEX_NONE ? LOCTEXT("TypingGrabFree", "Following the mouse   X / Y keep to an axis   type to set   Click / Enter set   Esc cancel")
					: LOCTEXT("TypingGrabAxis", "Kept to the axis   type to set   Click / Enter set   Esc cancel")), Palette::TypingHint, false);
			break;
		case EOp::Scale:
			Label(LOCTEXT("TypingScale", "Scale"), Palette::TypingLabel, true);
			if (Session.Axis == INDEX_NONE)
			{
				Field(TEXT("X & Y"), 0, Shown.Scale.X, true);
			}
			else
			{
				Field(TEXT("X"), 0, Shown.Scale.X, BufferIndex == 0);
				Field(TEXT("Y"), 1, Shown.Scale.Y, BufferIndex == 1);
			}
			Label(LOCTEXT("TypingScaleHint", "X / Y / Tab axis   Enter set   Esc cancel"), Palette::TypingHint, false);
			break;
		case EOp::Rotate:
			Label(LOCTEXT("TypingRotate", "Rotate"), Palette::TypingLabel, true);
			Field(TEXT("Angle"), 0, Shown.Angle, true);
			Label(LOCTEXT("TypingRotateHint", "Enter set   Esc cancel"), Palette::TypingHint, false);
			break;
		case EOp::Shear:
			Label(LOCTEXT("TypingShear", "Shear"), Palette::TypingLabel, true);
			Field(TEXT("X"), 0, Shown.Shear.X, BufferIndex == 0);
			Field(TEXT("Y"), 1, Shown.Shear.Y, BufferIndex == 1);
			Label(LOCTEXT("TypingShearHint", "X / Y / Tab axis   Enter set   Esc cancel"), Palette::TypingHint, false);
			break;
		default:
			return LayerId;
		}
		if (Edit.bKeying)
		{
			Label(LOCTEXT("TypingKeying", "Keying at the playhead"), Palette::KeyChipDot, false);
		}

		// One bar, under the chips (over them when there is no room below).
		constexpr double Gap = 6.0;
		constexpr double PaddingX = 6.0;
		constexpr double Height = 20.0;
		double Width = PaddingX;
		TArray<double> Widths;
		for (const FPart& Part : Parts)
		{
			const double PartWidth = FVector2D(Measure->Measure(Part.Text, Part.bBold ? BoldFont : Font)).X + (Part.bField ? 8.0 : 0.0);
			Widths.Add(PartWidth);
			Width += PartWidth + Gap;
		}
		Width += PaddingX - Gap;
		double Top = ChipRowMax.Y + 6.0;
		if (Top + Height > CanvasSize.Y - 4.0)
		{
			Top = ChipRowMin.Y - 6.0 - Height;
		}
		const double Left = FMath::Clamp(ChipRowMin.X, 4.0, FMath::Max(4.0, CanvasSize.X - Width - 4.0));
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(FVector2f(Width, Height), FSlateLayoutTransform(FVector2f(Left, Top))),
			RoundedBrush(), ESlateDrawEffect::None, Palette::TypingBackground);

		double X = Left + PaddingX;
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			const FPart& Part = Parts[Index];
			if (Part.bField)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(FVector2f(Widths[Index], Height - 6.0), FSlateLayoutTransform(FVector2f(X, Top + 3.0))),
					RoundedBrush(), ESlateDrawEffect::None, Part.bActive ? Palette::TypingFieldActive : Palette::TypingField);
			}
			const double TextX = X + (Part.bField ? 4.0 : 0.0);
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2, Geometry.ToPaintGeometry(FVector2f(Widths[Index] + 2.0, 14.0), FSlateLayoutTransform(FVector2f(TextX, Top + 3.0))),
				Part.Text, Part.bBold ? BoldFont : Font, ESlateDrawEffect::None, Part.Color);
			X += Widths[Index] + Gap;
		}
		return LayerId + 2;
	}

	int32 FGizmoLayer::PaintReadout(const FGeometry& Geometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
	{
		if (Readout.IsEmpty() || !(Pressing.bDragging || Session.Op == EOp::Grab))
		{
			return LayerId;
		}
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2D Padding(5.0, 2.0);
		const FVector2D TextSize(Measure->Measure(Readout, Font));
		const FVector2D BoxSize = TextSize + Padding * 2.0;

		// Beside the mouse, below and to the right of it; kept inside the canvas.
		FVector2D TopLeft = ReadoutAt + FVector2D(16.0, 18.0);
		TopLeft.X = FMath::Clamp(TopLeft.X, 2.0, FMath::Max(2.0, CanvasSize.X - BoxSize.X - 2.0));
		TopLeft.Y = FMath::Clamp(TopLeft.Y, 2.0, FMath::Max(2.0, CanvasSize.Y - BoxSize.Y - 2.0));

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			Geometry.ToPaintGeometry(FVector2f(BoxSize), FSlateLayoutTransform(FVector2f(TopLeft))),
			RoundedBrush(), ESlateDrawEffect::None, Palette::ReadoutBackground);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
			Geometry.ToPaintGeometry(FVector2f(TextSize + FVector2D(2.0, 2.0)), FSlateLayoutTransform(FVector2f(TopLeft + Padding))),
			Readout, Font, ESlateDrawEffect::None, Palette::ReadoutText);
		return LayerId + 1;
	}

	void RepaintAll()
	{
		for (const TWeakPtr<FGizmoLayer>& Weak : GetDesignerLayers())
		{
			if (const TSharedPtr<FGizmoLayer> Layer = Weak.Pin())
			{
				Layer->RequestRepaint();
			}
		}
	}

	// =================================================================================================
	// Input - the handles take the mouse, the gizmo's keys switch its mode and take typed values
	// =================================================================================================

	/**
	 * Takes the mouse for the gizmos' handles and chips before the designer sees it: a left press on one - where the designer
	 * itself is under the mouse, not a menu or drawer over it - is consumed, so the designer neither selects nor marquees, and
	 * the moves and the release that follow are the gizmo's until the button is let go. A right click on a handle opens its
	 * Reset menu. Moving over a handle lights it up and shows its cursor. The gizmo's keys (UWidgetGizmoSettings) are taken
	 * while the designer has the keyboard, or the mouse is over it and no text is being typed - and only while a widget the
	 * gizmo can act on is selected; while a session goes on (see FGizmoLayer::FSession) its keys are its own. Everything else
	 * passes through untouched. Registered first, ahead of the motion paths' input, so a handle wins over a diamond under it.
	 */
	class FGizmoInput : public IInputProcessor
	{
	public:
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
		{
			TickInput(SlateApp, Cursor);
			UpdateDesignerCursor();
		}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			const bool bHandled = HandleButtonDown(SlateApp, MouseEvent);
			UpdateDesignerCursor();
			return bHandled;
		}

		/** The second press of a double click comes as a double click: on a handle it is a press, as the first was. */
		virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			return MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && HandleMouseButtonDownEvent(SlateApp, MouseEvent);
		}

		virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			const bool bHandled = HandleMove(SlateApp, MouseEvent);
			UpdateDesignerCursor();
			return bHandled;
		}

		virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			const bool bHandled = HandleButtonUp(SlateApp, MouseEvent);
			UpdateDesignerCursor();
			return bHandled;
		}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			const bool bHandled = HandleKeyDown(SlateApp, InKeyEvent);
			UpdateDesignerCursor();
			return bHandled;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("WidgetGizmo"); }

	private:
		/**
		 * The cursor the designer answers Slate with while the gizmo has the mouse - a drag's, a Grab's, a lit handle's - or none
		 * (its own). Asked of the designer whenever Slate likes, so it never flips back to the arrow. See DesignerCursorOverride.h.
		 */
		void UpdateDesignerCursor() const
		{
			TOptional<EMouseCursor::Type> Wanted;
			if (const TSharedPtr<FGizmoLayer> Held = Pressed.Pin())
			{
				Wanted = Held->GetCursorFor(Held->GetPressed());
			}
			else if (FindSessionLayer(/*bGrabOnly=*/true).IsValid())
			{
				Wanted = EMouseCursor::CardinalCross;
			}
			else
			{
				for (const TWeakPtr<FGizmoLayer>& Weak : GetDesignerLayers())
				{
					const TSharedPtr<FGizmoLayer> Lit = Weak.Pin();
					if (Lit.IsValid() && Lit->GetHovered().IsValid())
					{
						Wanted = Lit->GetCursorFor(Lit->GetHovered());
						break;
					}
				}
			}
			NameTBDDesignerCursor::Override() = Wanted;
		}

		void TickInput(FSlateApplication& SlateApp, const TSharedRef<ICursor>& Cursor)
		{
			if (bSwallowLeft && !SlateApp.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
			{
				bSwallowLeft = false;
			}
			if (bSwallowRight && !SlateApp.GetPressedMouseButtons().Contains(EKeys::RightMouseButton))
			{
				bSwallowRight = false;
			}

			if (const TSharedPtr<FGizmoLayer> Layer = Pressed.Pin())
			{
				// A release that never came here - the window lost focus mid-drag: the drag is kept where it is.
				if (!SlateApp.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
				{
					Pressed.Reset();
					Layer->Release();
					GiveBackCursor(*Cursor);
					return;
				}

				// Shift or Ctrl pressed or let go mid-drag: worked out again at once, without waiting for the mouse to move.
				const FModifierKeysState Modifiers = SlateApp.GetModifierKeys();
				const bool bShift = Modifiers.IsShiftDown();
				const bool bCtrl = Modifiers.IsControlDown() || Modifiers.IsCommandDown();
				if (bShift != bLastShift || bCtrl != bLastCtrl)
				{
					bLastShift = bShift;
					bLastCtrl = bCtrl;
					Layer->ModifiersChanged(Modifiers);
				}

				Cursor->SetType(Layer->GetCursorFor(Layer->GetPressed()));
				bCursorOwned = true;
				return;
			}

			// Grabbing: the move cursor, the whole time.
			if (FindSessionLayer(/*bGrabOnly=*/true).IsValid())
			{
				Cursor->SetType(EMouseCursor::CardinalCross);
				bCursorOwned = true;
				return;
			}

			// Kept up every frame while a handle is lit: Slate asks the designer for its own cursor whenever it likes.
			for (const TWeakPtr<FGizmoLayer>& Weak : GetDesignerLayers())
			{
				const TSharedPtr<FGizmoLayer> Layer = Weak.Pin();
				if (Layer.IsValid() && Layer->GetHovered().IsValid())
				{
					Cursor->SetType(Layer->GetCursorFor(Layer->GetHovered()));
					bCursorOwned = true;
					return;
				}
			}
			GiveBackCursor(*Cursor);
		}

		bool HandleButtonDown(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
		{
			const FKey Button = MouseEvent.GetEffectingButton();
			const FVector2D Screen = MouseEvent.GetScreenSpacePosition();

			if (Button == EKeys::RightMouseButton)
			{
				// Mid-drag: puts it back, as Escape does; the release is kept from the designer too.
				if (const TSharedPtr<FGizmoLayer> Layer = Pressed.Pin())
				{
					Pressed.Reset();
					Layer->CancelDrag();
					bSwallowRight = true;
					bSwallowLeft = true;
					return true;
				}
				// A session: a Grab or anything typed is put back; an empty session simply ends.
				if (const TSharedPtr<FGizmoLayer> Layer = FindSessionLayer(/*bGrabOnly=*/false))
				{
					const bool bTook = Layer->IsGrabbing() || Layer->HasTyped();
					Layer->EndSession(/*bCancel=*/true);
					if (bTook)
					{
						bSwallowRight = true;
						return true;
					}
				}
				// On a handle: its Reset menu, opened as the button comes up over it.
				FHit Hit;
				const TSharedPtr<FGizmoLayer> HitLayer = FindHit(SlateApp, Screen, Hit, nullptr);
				if (HitLayer.IsValid() && Hit.Handle != EHandle::Chip)
				{
					RightPressed = HitLayer;
					RightHit = Hit;
					return true;
				}
				return false;
			}
			if (Button != EKeys::LeftMouseButton)
			{
				return false;
			}

			// A session: a Grab is set where it is (and the click is its); anything else typed is set, and the click goes on.
			if (const TSharedPtr<FGizmoLayer> Layer = FindSessionLayer(/*bGrabOnly=*/false))
			{
				const bool bGrab = Layer->IsGrabbing();
				Layer->EndSession(/*bCancel=*/false);
				if (bGrab)
				{
					bSwallowLeft = true;
					return true;
				}
			}

			FHit Hit;
			const TSharedPtr<FGizmoLayer> Layer = FindHit(SlateApp, Screen, Hit, nullptr);
			if (!Layer.IsValid())
			{
				return false;
			}
			Layer->Press(Hit, Screen);
			if (Layer->IsPressed())
			{
				Pressed = Layer;
				bLastShift = MouseEvent.IsShiftDown();
				bLastCtrl = MouseEvent.IsControlDown() || MouseEvent.IsCommandDown();
				SetCursor(SlateApp, Layer->GetCursorFor(Hit));
				bCursorOwned = true;
			}
			else
			{
				// A chip: it acted; its release is the designer's no more than its press was.
				bSwallowLeft = true;
			}
			return true;
		}

		bool HandleMove(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
		{
			const FVector2D Screen = MouseEvent.GetScreenSpacePosition();
			if (const TSharedPtr<FGizmoLayer> Layer = Pressed.Pin())
			{
				Layer->PressMove(Screen, MouseEvent.GetModifierKeys());
				SetCursor(SlateApp, Layer->GetCursorFor(Layer->GetPressed()));
				bCursorOwned = true;
				return true;
			}

			// Grabbing: the widgets follow the mouse; the designer doesn't see the move.
			if (const TSharedPtr<FGizmoLayer> Layer = FindSessionLayer(/*bGrabOnly=*/true))
			{
				Layer->SessionMouseMove(Screen);
				SetCursor(SlateApp, EMouseCursor::CardinalCross);
				bCursorOwned = true;
				return true;
			}

			// Lit up under the mouse - and its cursor. The move is kept from the designer, which would otherwise have Slate ask it
			// for its own cursor straight after.
			FHit Hit;
			const TSharedPtr<FGizmoLayer> HitLayer = FindHit(SlateApp, Screen, Hit, nullptr);
			for (const TWeakPtr<FGizmoLayer>& Weak : GetDesignerLayers())
			{
				if (const TSharedPtr<FGizmoLayer> Layer = Weak.Pin())
				{
					Layer->SetHover(Layer == HitLayer ? Hit : FHit());
				}
			}
			if (HitLayer.IsValid())
			{
				SetCursor(SlateApp, HitLayer->GetCursorFor(Hit));
				bCursorOwned = true;
				return true;
			}
			if (bCursorOwned)
			{
				bCursorOwned = false;
				SetCursor(SlateApp, EMouseCursor::Default);
			}
			return false;
		}

		bool HandleButtonUp(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
		{
			const FKey Button = MouseEvent.GetEffectingButton();
			if (Button == EKeys::RightMouseButton)
			{
				if (const TSharedPtr<FGizmoLayer> Layer = RightPressed.Pin())
				{
					const FHit PressedHit = RightHit;
					RightPressed.Reset();
					RightHit = FHit();

					// Let go over the handle it was pressed on: its Reset menu.
					FHit Hit;
					FWidgetPath Path;
					if (FindHit(SlateApp, MouseEvent.GetScreenSpacePosition(), Hit, &Path) == Layer && Hit == PressedHit)
					{
						Layer->OpenResetMenu(Hit, MouseEvent.GetScreenSpacePosition(), Path);
					}
					return true;
				}
				if (bSwallowRight)
				{
					bSwallowRight = false;
					return true;
				}
				return false;
			}
			if (Button != EKeys::LeftMouseButton)
			{
				return false;
			}
			if (const TSharedPtr<FGizmoLayer> Layer = Pressed.Pin())
			{
				Pressed.Reset();
				Layer->Release();
				return true;
			}
			if (bSwallowLeft)
			{
				bSwallowLeft = false;
				return true;
			}
			return false;
		}

		bool HandleKeyDown(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
		{
			const FKey Key = InKeyEvent.GetKey();

			// Mid-drag: Escape puts everything back; the gizmo's keys wait for the drag to end.
			if (const TSharedPtr<FGizmoLayer> Layer = Pressed.Pin())
			{
				if (Key == EKeys::Escape)
				{
					Pressed.Reset();
					Layer->CancelDrag();
					// The left button is still held: its release was the gizmo's press, and is kept from the designer too.
					bSwallowLeft = true;
					return true;
				}
				return GetGizmoKey(InKeyEvent).IsSet();
			}

			const UWidgetGizmoSettings& Settings = UWidgetGizmoSettings::Get();
			if (!Settings.bEnableGizmo)
			{
				return false;
			}
			const TSharedPtr<SWidget> Focused = SlateApp.GetKeyboardFocusedWidget();
			const bool bTypingText = Focused.IsValid() && Focused->GetType().ToString().Contains(TEXT("EditableText"));
			const TOptional<EGizmoKey> GizmoKey = GetGizmoKey(InKeyEvent);

			// A session: its keys are its own - the gizmo's keys switch to another (or end it); any other key sets it and goes on.
			if (const TSharedPtr<FGizmoLayer> Layer = FindSessionLayer(/*bGrabOnly=*/false))
			{
				if (bTypingText)
				{
					Layer->EndSession(/*bCancel=*/false);
					return false;
				}
				if (GizmoKey.IsSet())
				{
					if (!InKeyEvent.IsRepeat())
					{
						Layer->OnGizmoKey(GizmoKey.GetValue(), SlateApp.GetCursorPos());
						ClearHover();
					}
					return true;
				}
				return Layer->HandleSessionKey(InKeyEvent);
			}

			if (!GizmoKey.IsSet() || bTypingText)
			{
				return false;
			}
			const TSharedPtr<FGizmoLayer> Layer = FindKeyLayer(SlateApp, Focused);
			if (!Layer.IsValid())
			{
				return false;
			}
			if (!InKeyEvent.IsRepeat())
			{
				Layer->OnGizmoKey(GizmoKey.GetValue(), SlateApp.GetCursorPos());
				ClearHover();
			}
			return true;
		}

		static bool Matches(const FInputChord& Chord, const FKeyEvent& InKeyEvent)
		{
			return Chord.Key.IsValid() && Chord.Key == InKeyEvent.GetKey()
				&& Chord.NeedsShift() == InKeyEvent.IsShiftDown()
				&& Chord.NeedsControl() == InKeyEvent.IsControlDown()
				&& Chord.NeedsAlt() == InKeyEvent.IsAltDown()
				&& Chord.NeedsCommand() == InKeyEvent.IsCommandDown();
		}

		/** Which of the gizmo's keys (UWidgetGizmoSettings) the event is, if any. */
		static TOptional<EGizmoKey> GetGizmoKey(const FKeyEvent& InKeyEvent)
		{
			const UWidgetGizmoSettings& Settings = UWidgetGizmoSettings::Get();
			if (Matches(Settings.GrabKey, InKeyEvent))
			{
				return EGizmoKey::Grab;
			}
			if (Matches(Settings.ScaleKey, InKeyEvent))
			{
				return EGizmoKey::Scale;
			}
			if (Matches(Settings.RotateKey, InKeyEvent))
			{
				return EGizmoKey::Rotate;
			}
			if (Matches(Settings.ShearKey, InKeyEvent))
			{
				return EGizmoKey::Shear;
			}
			if (Matches(Settings.HideKey, InKeyEvent))
			{
				return EGizmoKey::Hide;
			}
			return TOptional<EGizmoKey>();
		}

		static void ClearHover()
		{
			for (const TWeakPtr<FGizmoLayer>& Weak : GetDesignerLayers())
			{
				if (const TSharedPtr<FGizmoLayer> Layer = Weak.Pin())
				{
					Layer->SetHover(FHit());
				}
			}
		}

		/** The layer with a session going on (bGrabOnly: a Grab), if any. */
		static TSharedPtr<FGizmoLayer> FindSessionLayer(bool bGrabOnly)
		{
			for (const TWeakPtr<FGizmoLayer>& Weak : GetDesignerLayers())
			{
				const TSharedPtr<FGizmoLayer> Layer = Weak.Pin();
				if (Layer.IsValid() && (bGrabOnly ? Layer->IsGrabbing() : Layer->HasSession()))
				{
					return Layer;
				}
			}
			return nullptr;
		}

		/** The layer whose handle or chip is at Screen - checked against what was drawn, then that its designer is on top. */
		static TSharedPtr<FGizmoLayer> FindHit(FSlateApplication& SlateApp, const FVector2D& Screen, FHit& OutHit, FWidgetPath* OutPath)
		{
			for (const TWeakPtr<FGizmoLayer>& Weak : GetDesignerLayers())
			{
				const TSharedPtr<FGizmoLayer> Layer = Weak.Pin();
				if (!Layer.IsValid() || !Layer->IsShowing() || !Layer->IsOver(Screen))
				{
					continue;
				}
				const FHit Hit = Layer->HitTest(Screen);
				if (!Hit.IsValid())
				{
					continue;
				}
				// Only where the designer itself is under the mouse - not a menu, a drawer or another window over it.
				FWidgetPath Path = SlateApp.LocateWindowUnderMouse(Screen, SlateApp.GetInteractiveTopLevelWindows());
				if (!Layer->IsDesignerUnder(Path))
				{
					continue;
				}
				OutHit = Hit;
				if (OutPath)
				{
					*OutPath = MoveTemp(Path);
				}
				return Layer;
			}
			OutHit = FHit();
			return nullptr;
		}

		/**
		 * The layer the gizmo's keys are for: the one whose designer has the keyboard, else the one under the mouse - and only
		 * with a widget it can act on.
		 */
		static TSharedPtr<FGizmoLayer> FindKeyLayer(FSlateApplication& SlateApp, const TSharedPtr<SWidget>& Focused)
		{
			for (const TWeakPtr<FGizmoLayer>& Weak : GetDesignerLayers())
			{
				const TSharedPtr<FGizmoLayer> Layer = Weak.Pin();
				if (Layer.IsValid() && Layer->HasTargets() && Layer->IsKeyboardIn(Focused))
				{
					return Layer;
				}
			}
			const FVector2D Screen = SlateApp.GetCursorPos();
			for (const TWeakPtr<FGizmoLayer>& Weak : GetDesignerLayers())
			{
				const TSharedPtr<FGizmoLayer> Layer = Weak.Pin();
				if (Layer.IsValid() && Layer->HasTargets() && Layer->IsOver(Screen)
					&& Layer->IsDesignerUnder(SlateApp.LocateWindowUnderMouse(Screen, SlateApp.GetInteractiveTopLevelWindows())))
				{
					return Layer;
				}
			}
			return nullptr;
		}

		void GiveBackCursor(ICursor& Cursor)
		{
			if (bCursorOwned)
			{
				bCursorOwned = false;
				Cursor.SetType(EMouseCursor::Default);
			}
		}

		static void SetCursor(FSlateApplication& SlateApp, EMouseCursor::Type CursorType)
		{
			if (const TSharedPtr<ICursor> PlatformCursor = SlateApp.GetPlatformCursor())
			{
				PlatformCursor->SetType(CursorType);
			}
		}

		TWeakPtr<FGizmoLayer> Pressed;
		TWeakPtr<FGizmoLayer> RightPressed;
		FHit RightHit;
		bool bSwallowLeft = false;
		bool bSwallowRight = false;
		bool bLastShift = false;
		bool bLastCtrl = false;

		/** Whether the cursor showing is one set here - given back once the mouse leaves every handle. */
		bool bCursorOwned = false;
	};

	namespace
	{
		TSharedPtr<FGizmoInput>& GetInput()
		{
			static TSharedPtr<FGizmoInput> Input;
			return Input;
		}

		int32& GetInputUsers()
		{
			static int32 Users = 0;
			return Users;
		}
	}

	void AcquireInput()
	{
		if (++GetInputUsers() == 1 && FSlateApplication::IsInitialized())
		{
			GetInput() = MakeShared<FGizmoInput>();
			// First of every preprocessor: a handle wins over a motion path diamond drawn under it.
			FSlateApplication::Get().RegisterInputPreProcessor(GetInput(), 0);
		}
	}

	void ReleaseInput()
	{
		if (--GetInputUsers() > 0)
		{
			return;
		}
		GetInputUsers() = 0;
		NameTBDDesignerCursor::Override().Reset();
		if (GetInput().IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(GetInput());
		}
		GetInput().Reset();
	}
}

// =====================================================================================================
// Extension - the designer draws the gizmo through it, every frame it is drawn
// =====================================================================================================

FWidgetGizmoExtension::FWidgetGizmoExtension()
{
	ExtensionId = FName(TEXT("WidgetGizmo"));
}

FWidgetGizmoExtension::~FWidgetGizmoExtension()
{
	Layer.Reset();
}

void FWidgetGizmoExtension::Paint(const TSet<FWidgetReference>& Selection, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	using namespace WidgetGizmoImpl;

	if (!Layer.IsValid())
	{
		Layer = MakeShared<FGizmoLayer>();
		Layer->Register();
	}
	Layer->Paint(Selection, AllottedGeometry, OutDrawElements, LayerId);
}

TSharedRef<FDesignerExtension> FWidgetGizmoFactory::CreateDesignerExtension() const
{
	return MakeShared<FWidgetGizmoExtension>();
}

#undef LOCTEXT_NAMESPACE
