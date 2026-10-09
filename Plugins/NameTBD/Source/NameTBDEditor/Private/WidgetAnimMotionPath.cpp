// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimMotionPath.h"

#include "NameTBDColors.h"
#include "SWidgetAnimTimeline.h"
#include "WidgetAnimEditorSettings.h"
#include "WidgetAnimPanelSource.h"
#include "WidgetAnimScripting.h"
#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/PanelWidget.h"
#include "Editor.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/IInputProcessor.h"
#include "GenericPlatform/ICursor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/IConsoleManager.h"
#include "Layout/WidgetPath.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "WidgetBlueprint.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "WidgetAnimMotionPath"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimMotionPath, Log, All);

namespace WidgetAnimMotionPathImpl
{
	namespace Palette = NameTBD::Colors::MotionPath;
	namespace KeyPalette = NameTBD::Colors::WidgetAnimEditor;

	/** How finely the line is sampled between dots, at most: this many samples per second of the animation. */
	constexpr float LineSamplesPerSecond = 240.f;

	/** The most samples one path is worked out at - a long animation is sampled more coarsely rather than slowly. */
	constexpr int32 MaxSamples = 6000;

	/** The most widgets that show a path at once. */
	constexpr int32 MaxPaths = 8;

	/** How far the mouse must move, in the designer's units, before a press on a diamond or a handle becomes a drag. */
	constexpr float DragThreshold = 3.f;

	/** A step between two samples is a jump - a Hold, a step - when it is longer than this, in canvas units... */
	constexpr float JumpMinDistance = 20.f;

	/** ...and this many times longer than the steps on either side of it. */
	constexpr float JumpRatio = 6.f;

	/** A path whose points all lie within this many canvas units of each other doesn't move, and isn't drawn. */
	constexpr float StillExtent = 1.5f;

	static TAutoConsoleVariable<int32> CVarLogMotionPaths(
		TEXT("WidgetAnim.LogMotionPaths"),
		0,
		TEXT("Says why the designer's motion paths show or not: written over the designer and to LogWidgetAnimMotionPath. 1: each change - what is missing, what the animation moves; 2: also where each path lands on screen."),
		ECVF_Default);

	int32 LogLevel()
	{
		return CVarLogMotionPaths.GetValueOnGameThread();
	}

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

	/** White, and tinted where it is drawn: a brush's own color is only applied by widgets that draw it, never by MakeBox. */
	const FSlateBrush* LabelBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 3.f);
		return &Brush;
	}

	/** A widget's render transform about its pivot, PivotPx from its top left: what Slate draws it with. */
	FSlateRenderTransform PivotedPose(const FWidgetTransform& Pose, const FVector2f& PivotPx)
	{
		return Concatenate(Concatenate(FSlateRenderTransform(-PivotPx), Pose.ToSlateRenderTransform()), FSlateRenderTransform(PivotPx));
	}

	/** The first widget of a designer's preview - the user widget a widget is in. */
	UUserWidget* GetOwningPreview(const UWidget* Widget)
	{
		return Widget ? Widget->GetTypedOuter<UUserWidget>() : nullptr;
	}

	// =================================================================================================
	// Where a widget is on screen, without its render transform
	// =================================================================================================

	/**
	 * One widget of a path's chain - the widget, then each parent up - by its layout alone: where its box is laid out, never how
	 * its render transform moves it. Layout doesn't change as the animation plays, so nothing read here moves with it - which
	 * is what keeps the path and its keys still while the preview plays or a key is dragged. Reading a widget's geometry with
	 * its pose taken back off couldn't: the geometry is the last frame's, the pose this one's.
	 */
	struct FChainFrame
	{
		FSlateLayoutTransform Layout;
		FSlateLayoutTransform LayoutInverse;
		FVector2f PivotPx = FVector2f::ZeroVector;

		/** Its pose now - what the "now" ring and a drag's units are worked out at. */
		FWidgetTransform Now;
	};

	/** A chain, ready to map poses onto the screen: each widget's layout, and where the top one's parent is on screen. */
	struct FChainMapping
	{
		TArray<FChainFrame> Frames;

		/** The top widget's Slate parent: on screen (render transforms and all), and its layout - what the top is laid out in. */
		FSlateRenderTransform AboveRender;
		FSlateLayoutTransform AboveLayoutInverse;
		bool bHasAbove = false;

		/** With no parent to read: the top widget's own place on screen, its pose now taken off. Only a fallback. */
		FSlateRenderTransform TopRender;
		FSlateRenderTransform TopNowInverse;
	};

	bool ReadChain(const TArray<TWeakObjectPtr<UWidget>>& Chain, FChainMapping& Out)
	{
		Out.Frames.Reset();
		Out.bHasAbove = false;
		for (const TWeakObjectPtr<UWidget>& Link : Chain)
		{
			const UWidget* Linked = Link.Get();
			const TSharedPtr<SWidget> SlateWidget = Linked ? Linked->GetCachedWidget() : nullptr;
			if (!SlateWidget.IsValid())
			{
				return false;
			}
			const FGeometry& LinkGeometry = SlateWidget->GetTickSpaceGeometry();
			const FVector2D LocalSize = LinkGeometry.GetLocalSize();
			if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
			{
				return false;
			}

			FChainFrame& Frame = Out.Frames.AddDefaulted_GetRef();
			Frame.Layout = LinkGeometry.GetAccumulatedLayoutTransform();
			Frame.LayoutInverse = Frame.Layout.Inverse();
			Frame.PivotPx = FVector2f(Linked->GetRenderTransformPivot() * LocalSize);
			Frame.Now = Linked->GetRenderTransform();

			if (&Link == &Chain.Last())
			{
				Out.TopRender = LinkGeometry.GetAccumulatedRenderTransform();
				Out.TopNowInverse = PivotedPose(Frame.Now, Frame.PivotPx).Inverse();
				if (const TSharedPtr<SWidget> Above = SlateWidget->GetParentWidget())
				{
					const FGeometry& AboveGeometry = Above->GetTickSpaceGeometry();
					Out.AboveRender = AboveGeometry.GetAccumulatedRenderTransform();
					Out.AboveLayoutInverse = AboveGeometry.GetAccumulatedLayoutTransform().Inverse();
					Out.bHasAbove = true;
				}
			}
		}
		return Out.Frames.Num() > 0;
	}

	/**
	 * A point in chain widget Level's box - its pose already applied - on the screen: through its layout, then each widget above
	 * at its pose in Poses (null: as it is now), then the top one's parent.
	 */
	FVector2f LevelToAbsolute(const FChainMapping& Mapping, int32 Level, const FVector2f& BoxPoint, const FWidgetTransform* Poses)
	{
		FVector2f Point = Mapping.Frames[Level].Layout.TransformPoint(BoxPoint);
		for (int32 Up = Level + 1; Up < Mapping.Frames.Num(); ++Up)
		{
			const FChainFrame& Frame = Mapping.Frames[Up];
			const FVector2f Local = Frame.LayoutInverse.TransformPoint(Point);
			const FWidgetTransform& Pose = Poses ? Poses[Up] : Frame.Now;
			Point = Frame.Layout.TransformPoint(PivotedPose(Pose, Frame.PivotPx).TransformPoint(Local));
		}
		if (Mapping.bHasAbove)
		{
			return Mapping.AboveRender.TransformPoint(Mapping.AboveLayoutInverse.TransformPoint(Point));
		}
		const FVector2f TopLocal = Mapping.Frames.Last().LayoutInverse.TransformPoint(Point);
		return Mapping.TopRender.TransformPoint(Mapping.TopNowInverse.TransformPoint(TopLocal));
	}

	/** Where the chain's first widget's pivot is on screen with each chain widget at Poses[Level]. */
	FVector2f MapChain(const FChainMapping& Mapping, const FWidgetTransform* Poses)
	{
		const FChainFrame& First = Mapping.Frames[0];
		return LevelToAbsolute(Mapping, 0, PivotedPose(Poses[0], First.PivotPx).TransformPoint(First.PivotPx), Poses);
	}

	/**
	 * How the screen moves per unit of chain widget Level's Translation - its pose moves its box in its own units, after its
	 * scale and turn - as a 2x2 matrix, columns X and Y, with the widgets above at Poses (a key's own, so a key's handles
	 * never move as the animation plays). Affine all the way up, so the same everywhere.
	 */
	void UnitsToScreen(const FChainMapping& Mapping, int32 Level, const FWidgetTransform* Poses, FVector2f& OutX, FVector2f& OutY)
	{
		const FVector2f Origin = LevelToAbsolute(Mapping, Level, FVector2f::ZeroVector, Poses);
		OutX = LevelToAbsolute(Mapping, Level, FVector2f(1.f, 0.f), Poses) - Origin;
		OutY = LevelToAbsolute(Mapping, Level, FVector2f(0.f, 1.f), Poses) - Origin;
	}

	/** The other way: a screen movement in Level's Translation units. Zero when the widget has no size on screen. */
	FVector2D ScreenToUnits(const FVector2f& ColumnX, const FVector2f& ColumnY, const FVector2D& ScreenDelta)
	{
		const double Determinant = static_cast<double>(ColumnX.X) * ColumnY.Y - static_cast<double>(ColumnY.X) * ColumnX.Y;
		if (FMath::Abs(Determinant) < 1.e-9)
		{
			return FVector2D::ZeroVector;
		}
		return FVector2D(
			(ColumnY.Y * ScreenDelta.X - ColumnY.X * ScreenDelta.Y) / Determinant,
			(-ColumnX.Y * ScreenDelta.X + ColumnX.X * ScreenDelta.Y) / Determinant);
	}

	/**
	 * The first moment the eased track reaches Fraction of its run - where a key of a track eased across its whole run (or timed
	 * by a spring) is passed. Scanned, then narrowed; an easing that never reaches it leaves Fraction as it is.
	 */
	float FindEasedPassing(const FWidgetAnimSettings& Layer, float Fraction)
	{
		constexpr int32 Scan = 64;
		float PreviousAlpha = 0.f;
		float PreviousValue = UWidgetAnimLibrary::EvaluateEasedAlpha(Layer, 0.f) - Fraction;
		if (FMath::IsNearlyZero(PreviousValue, 1.e-5f))
		{
			return 0.f;
		}
		for (int32 Step = 1; Step <= Scan; ++Step)
		{
			const float Alpha = static_cast<float>(Step) / Scan;
			const float Value = UWidgetAnimLibrary::EvaluateEasedAlpha(Layer, Alpha) - Fraction;
			if (Value * PreviousValue <= 0.f)
			{
				float Low = PreviousAlpha;
				float High = Alpha;
				for (int32 Narrow = 0; Narrow < 24; ++Narrow)
				{
					const float Middle = (Low + High) * 0.5f;
					const float MiddleValue = UWidgetAnimLibrary::EvaluateEasedAlpha(Layer, Middle) - Fraction;
					if (MiddleValue * PreviousValue <= 0.f)
					{
						High = Middle;
					}
					else
					{
						Low = Middle;
						PreviousValue = MiddleValue;
					}
				}
				return (Low + High) * 0.5f;
			}
			PreviousAlpha = Alpha;
			PreviousValue = Value;
		}
		return Fraction;
	}

	// =================================================================================================
	// The paths
	// =================================================================================================

	/** One key's diamond on a path. */
	struct FMarker
	{
		/** The panel's layer and key - FKeyRef counting. For a track with no keys between its start and end, its start or end. */
		int32 Layer = INDEX_NONE;
		int32 Key = INDEX_NONE;

		/** A track's start or end rather than a key: picking it picks the track and moves the playhead there. */
		bool bEdge = false;

		/** A Transform key: its diamond drags its Translation, and it has a Path with handles. */
		bool bDraggable = false;

		/** Whether its track can be edited at all - the Track Editor draws keys of one that can't dimmer. */
		bool bEditable = false;

		/** Which widget of its path's chain the track moves - 0 the path's own widget, then each parent up. */
		int32 ChainLevel = 0;

		/** When the widget passes it, as the panel shows time - and in seconds into one pass of the preview. */
		float PanelSeconds = 0.f;
		float StackTime = 0.f;

		/** Which path it is on, the sample nearest it (for the path's direction there), and every chain widget's pose at it. */
		int32 Path = INDEX_NONE;
		int32 NearSample = 0;
		TArray<FWidgetTransform> Poses;

		/** Worked out every frame: where it is on screen and on the canvas, whether it is drawn, and its handles when shown. */
		FVector2f Absolute = FVector2f::ZeroVector;
		FVector2f Canvas = FVector2f::ZeroVector;
		bool bShown = false;
		bool bShowHandles = false;
		FVector2f HandleInCanvas = FVector2f::ZeroVector;
		FVector2f HandleOutCanvas = FVector2f::ZeroVector;
	};

	/** One widget's path: its poses through the animation, worked out when the animation changes - mapped onto the canvas every frame. */
	struct FPath
	{
		/** The widget, then each parent up to the highest one the animation moves - every widget whose pose moves this one. */
		TArray<TWeakObjectPtr<UWidget>> Chain;

		/** When each sample is, in seconds into one pass, and each chain widget's pose then: Times.Num() * Chain.Num() poses. */
		TArray<float> Times;
		TArray<FWidgetTransform> Poses;

		/** Every DotEvery-th sample is a frame's dot. */
		int32 DotEvery = 1;

		/** Worked out every frame: each sample on the canvas, and whether the path moves at all. */
		TArray<FVector2f> Canvas;
		bool bShown = false;

		/** Where the widget is right now, on the canvas. */
		FVector2f Here = FVector2f::ZeroVector;

		/** Each chain widget's layout this frame - see FChainFrame. */
		FChainMapping Mapping;
		bool bMapped = false;

		/** For WidgetAnim.LogMotionPaths: the widget's name, and why the path is drawn or not this frame. */
		FString Name;
		FString MapNote;
		FString MapDetail;
	};

	class FMotionPathLayer;

	/** Every designer's layer alive now - what FMotionPathInput hit-tests. Each adds itself as it is made. */
	TArray<TWeakPtr<FMotionPathLayer>>& GetDesignerLayers()
	{
		static TArray<TWeakPtr<FMotionPathLayer>> Layers;
		return Layers;
	}

	/** Takes the mouse for the layers' diamonds and handles - registered while any layer exists. See FMotionPathInput below. */
	void AcquireInput();
	void ReleaseInput();

	// =================================================================================================
	// The layer
	// =================================================================================================

	/**
	 * One designer's paths, drawn over it by its extension's Paint (see FWidgetAnimMotionPathExtension) every frame the designer
	 * is drawn, whatever is selected in it. Not a widget: what it draws is part of the designer's own drawing, in the designer's
	 * own space, and its diamonds and handles take the mouse through FMotionPathInput, which hit-tests what was drawn that frame.
	 * See WidgetAnimMotionPath.h.
	 */
	class FMotionPathLayer : public TSharedFromThis<FMotionPathLayer>
	{
	public:
		explicit FMotionPathLayer(const TWeakObjectPtr<UWidgetBlueprint>& InBlueprint)
			: BlueprintWeak(InBlueprint)
		{
			AcquireInput();
			if (LogLevel() >= 1)
			{
				const UWidgetBlueprint* Blueprint = BlueprintWeak.Get();
				UE_LOG(LogWidgetAnimMotionPath, Log, TEXT("Motion path layer made for the designer of %s."), Blueprint ? *Blueprint->GetName() : TEXT("(none)"));
			}
		}

		~FMotionPathLayer()
		{
			EndDrag();
			ReleaseInput();
		}

		/** Listed for FMotionPathInput - once it is owned by a shared pointer. */
		void Register()
		{
			GetDesignerLayers().RemoveAll([](const TWeakPtr<FMotionPathLayer>& Weak) { return !Weak.IsValid(); });
			GetDesignerLayers().Add(AsShared());
		}

		/**
		 * Works this frame's paths out and draws them over the designer: DesignerGeometry is the designer's own, as it paints, and
		 * everything is drawn in its space and clipped to it.
		 */
		void Paint(const TSet<FWidgetReference>& Selection, const FGeometry& DesignerGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId);

		// ---------------------------------------------------------------------------------------------
		// What FMotionPathInput asks
		// ---------------------------------------------------------------------------------------------

		/**
		 * Whether this layer has been drawn and Screen is over its designer as last drawn. Not "drawn this frame": an idle designer
		 * isn't painted again every frame - what it drew is kept and shown - so its paths are on screen without a fresh paint.
		 * Whether the designer is really there, in front, is IsDesignerUnder's question.
		 */
		bool IsOver(const FVector2D& Screen) const
		{
			return PaintedFrame > 0 && Designer.IsUnderLocation(Screen);
		}

		/**
		 * Whether Path - what is under the mouse - goes through this layer's own designer: so not a menu, drawer or window over
		 * it, nor another Widget Blueprint's designer tabbed in front of it in the same place.
		 */
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
				// The widget the designer handed its geometry to the paths with - whatever its type - is the one exactly where the
				// paths are drawn: a menu or a drawer over the designer is not, and neither are the widgets around the designer.
				if (IsDesignerRect(Arranged.Geometry))
				{
					if (DesignerWidget != Arranged.Widget)
					{
						DesignerWidget = Arranged.Widget;
						if (LogLevel() >= 1)
						{
							UE_LOG(LogWidgetAnimMotionPath, Log, TEXT("Motion path layer: its designer is %s, found under the mouse."), *Arranged.Widget->GetTypeAsString());
						}
					}
					return true;
				}
			}
			return false;
		}

		/** Whether Geometry (desktop space) covers exactly the designer the paths were last drawn over - to the pixel. */
		bool IsDesignerRect(const FGeometry& Geometry) const
		{
			const FVector2D Position(Geometry.GetAbsolutePosition());
			const FVector2D Size(Geometry.GetAbsoluteSize());
			return FVector2D::Distance(Position, FVector2D(Designer.GetAbsolutePosition())) < 2.0
				&& FVector2D::Distance(Size, FVector2D(Designer.GetAbsoluteSize())) < 2.0;
		}

		/** For WidgetAnim.LogMotionPaths: the widgets under the mouse, innermost last, and where the designer is. */
		FString DescribePath(const FWidgetPath& Path) const
		{
			TArray<FString> Parts;
			const int32 First = FMath::Max(0, Path.Widgets.Num() - 10);
			for (int32 Index = First; Index < Path.Widgets.Num(); ++Index)
			{
				const FArrangedWidget& Arranged = Path.Widgets[Index];
				const FVector2D Position(Arranged.Geometry.GetAbsolutePosition());
				const FVector2D Size(Arranged.Geometry.GetAbsoluteSize());
				Parts.Add(FString::Printf(TEXT("%s [%.0f,%.0f %.0fx%.0f]"), *Arranged.Widget->GetTypeAsString(), Position.X, Position.Y, Size.X, Size.Y));
			}
			const FVector2D DesignerPosition(Designer.GetAbsolutePosition());
			const FVector2D DesignerSize(Designer.GetAbsoluteSize());
			return FString::Printf(TEXT("Under the mouse: %s. Paths drawn over [%.0f,%.0f %.0fx%.0f]."),
				*FString::Join(Parts, TEXT(" > ")), DesignerPosition.X, DesignerPosition.Y, DesignerSize.X, DesignerSize.Y);
		}

		/** The cursor over diamond Marker (or its Handle): grab for what drags, a hand for what only picks. */
		EMouseCursor::Type GetCursorFor(int32 Marker, int32 Handle) const
		{
			if (bPressDragging)
			{
				return EMouseCursor::GrabHandClosed;
			}
			const FMarker* Found = GetMarker(Marker);
			return Handle != INDEX_NONE || (Found && Found->bDraggable) ? EMouseCursor::GrabHand : EMouseCursor::Hand;
		}

		/** Has the designer painted again - the hovered or pressed diamond's look, or a drag's path, changed. */
		void RequestRepaint() const
		{
			if (const TSharedPtr<SWidget> OwnDesigner = DesignerWidget.Pin())
			{
				OwnDesigner->Invalidate(EInvalidateWidgetReason::Paint);
			}
		}

		/** The diamond or handle drawn at Screen this frame: its marker, and its handle (0 in, 1 out) or INDEX_NONE for the diamond. */
		bool HitTest(const FVector2D& Screen, int32& OutMarker, int32& OutHandle) const
		{
			const FVector2f Local = FVector2f(FVector2D(Designer.AbsoluteToLocal(Screen)));
			// Handles first - they sit over the diamonds - then the diamonds drawn last, which are on top.
			for (int32 Index = Markers.Num() - 1; Index >= 0; --Index)
			{
				const FMarker& Marker = Markers[Index];
				if (!Marker.bShown || !Marker.bShowHandles)
				{
					continue;
				}
				for (int32 Handle = 0; Handle < 2; ++Handle)
				{
					const FVector2f Center = Handle == 0 ? Marker.HandleInCanvas : Marker.HandleOutCanvas;
					if (IsInDesigner(Center) && FVector2f::Distance(Center, Local) <= Palette::HandleHitSize * 0.5f)
					{
						OutMarker = Index;
						OutHandle = Handle;
						return true;
					}
				}
			}
			for (int32 Index = Markers.Num() - 1; Index >= 0; --Index)
			{
				const FMarker& Marker = Markers[Index];
				const float Half = Palette::KeyHitSize * 0.5f;
				if (Marker.bShown && FMath::Abs(Marker.Canvas.X - Local.X) <= Half && FMath::Abs(Marker.Canvas.Y - Local.Y) <= Half)
				{
					OutMarker = Index;
					OutHandle = INDEX_NONE;
					return true;
				}
			}
			return false;
		}

		/** For WidgetAnim.LogMotionPaths: where Screen is on the designer, and the nearest diamond drawn. */
		void DescribeNearest(const FVector2D& Screen, FString& OutText) const
		{
			const FVector2f Local = FVector2f(FVector2D(Designer.AbsoluteToLocal(Screen)));
			int32 NearestIndex = INDEX_NONE;
			float NearestDistance = TNumericLimits<float>::Max();
			int32 NumShown = 0;
			for (int32 Index = 0; Index < Markers.Num(); ++Index)
			{
				if (!Markers[Index].bShown)
				{
					continue;
				}
				++NumShown;
				const float Distance = FVector2f::Distance(Markers[Index].Canvas, Local);
				if (Distance < NearestDistance)
				{
					NearestDistance = Distance;
					NearestIndex = Index;
				}
			}
			OutText = NearestIndex == INDEX_NONE
				? FString::Printf(TEXT("Pressed at (%.0f, %.0f) on the designer; no diamond drawn (%d markers)."), Local.X, Local.Y, Markers.Num())
				: FString::Printf(TEXT("Pressed at (%.0f, %.0f) on the designer; nearest of %d diamonds at (%.0f, %.0f), %.1f away (hit within %.1f)."),
					Local.X, Local.Y, NumShown, Markers[NearestIndex].Canvas.X, Markers[NearestIndex].Canvas.Y, NearestDistance, Palette::KeyHitSize * 0.5f);
		}

		void SetHover(int32 Marker, int32 Handle)
		{
			if (HoveredMarker != Marker || HoveredHandle != Handle)
			{
				HoveredMarker = Marker;
				HoveredHandle = Handle;
				RequestRepaint();
			}
		}

		/** A left press on what HitTest found, held until released: a click, or once past the threshold a drag. */
		void Press(int32 Marker, int32 Handle, const FVector2D& Screen, bool bAlt)
		{
			PressedMarker = Marker;
			PressedHandle = Handle;
			PressScreen = Screen;
			bPressAlt = bAlt;
			bPressDragging = false;
			RequestRepaint();
		}

		bool IsPressed() const { return PressedMarker != INDEX_NONE; }

		/** The mouse moved while pressed: past the threshold the drag starts - moving the key, or its handle - and follows it. */
		void PressMove(const FVector2D& Screen, const FModifierKeysState& Modifiers)
		{
			if (!IsPressed())
			{
				return;
			}
			if (!bPressDragging)
			{
				// Measured on the designer, so the threshold is the same at any DPI.
				const FVector2D Moved = FVector2D(Designer.AbsoluteToLocal(Screen)) - FVector2D(Designer.AbsoluteToLocal(PressScreen));
				if (Moved.Size() < DragThreshold)
				{
					return;
				}
				if (PressedHandle == INDEX_NONE)
				{
					// Ctrl picks: a drag only starts from a plain press, or Alt to pull the key's handles out.
					if (Modifiers.IsControlDown() || Modifiers.IsCommandDown() || !BeginKeyDrag(PressedMarker, bPressAlt))
					{
						return;
					}
				}
				else if (!BeginHandleDrag(PressedMarker, PressedHandle == 1))
				{
					return;
				}
				bPressDragging = true;
			}
			const bool bHandle = PressedHandle != INDEX_NONE;
			UpdateDrag(Screen - PressScreen, !bHandle && Modifiers.IsShiftDown(), bHandle && Modifiers.IsAltDown());
			RequestRepaint();
		}

		/** The press let go: a click on a diamond picks its key; a drag ends. bCancelled: let go elsewhere - no click. */
		void Release(const FModifierKeysState& Modifiers, bool bCancelled)
		{
			const int32 Marker = PressedMarker;
			const int32 Handle = PressedHandle;
			const bool bWasDragging = bPressDragging;
			PressedMarker = INDEX_NONE;
			PressedHandle = INDEX_NONE;
			bPressDragging = false;
			if (bWasDragging)
			{
				EndDrag();
			}
			else if (!bCancelled && Handle == INDEX_NONE && Marker != INDEX_NONE)
			{
				ClickMarker(Marker, Modifiers);
			}
			RequestRepaint();
		}

		/** Right-click on a diamond: how the path runs through it, and its track's easing - a menu over the designer (OwnerPath). */
		void OpenMarkerMenu(int32 Index, const FVector2D& ScreenPosition, const FWidgetPath& OwnerPath);

		// ---------------------------------------------------------------------------------------------
		// Keys and drags
		// ---------------------------------------------------------------------------------------------

		const FMarker* GetMarker(int32 Index) const { return Markers.IsValidIndex(Index) ? &Markers[Index] : nullptr; }

		bool IsMarkerSelected(const FMarker& Marker) const
		{
			const TSharedPtr<SWidgetAnimTimeline> Panel = PanelWeak.Pin();
			if (!Panel.IsValid())
			{
				return false;
			}
			return Marker.bEdge ? Panel->GetSelectedLayers().Contains(Marker.Layer) && Panel->GetSelectedKeys().Num() == 0 : Panel->IsKeySelected(Marker.Layer, Marker.Key);
		}

		/** Under the mouse, or the key being dragged - found by its key, as the markers are worked out again while it moves. */
		bool IsMarkerHot(int32 Index) const
		{
			const FMarker* Marker = GetMarker(Index);
			return Marker && ((HoveredMarker == Index && HoveredHandle == INDEX_NONE) || IsMarkerDragged(*Marker));
		}

		bool IsMarkerDragged(const FMarker& Marker) const { return bDragActive && Marker.Layer == DragLayer && Marker.Key == DragKey && !Marker.bEdge; }

		/** A click on a diamond with no drag: its key picked - Ctrl toggles it among the picked keys, Shift adds it. */
		void ClickMarker(int32 Index, const FModifierKeysState& Modifiers)
		{
			const TSharedPtr<SWidgetAnimTimeline> Panel = PanelWeak.Pin();
			const FMarker* Marker = GetMarker(Index);
			if (!Panel.IsValid() || !Marker)
			{
				return;
			}

			if (Marker->bEdge)
			{
				// A track's start or end isn't a key the Track Editor picks: the track is picked, and the playhead goes there.
				Panel->SelectLayer(Marker->Layer);
				Panel->ScrubToSeconds(Marker->PanelSeconds);
				return;
			}

			const bool bToggle = Modifiers.IsControlDown() || Modifiers.IsCommandDown();
			if (bToggle || Modifiers.IsShiftDown())
			{
				Panel->SelectKeyWithModifiers(Marker->Layer, Marker->Key, bToggle);
			}
			else
			{
				Panel->SelectKey(Marker->Layer, Marker->Key);
			}
		}

		/**
		 * A press on a diamond moved past the threshold: a draggable key's drag starts - moving it, or with bHandles pulling its
		 * handles out of it - its widget's units worked out now, so the drag never drifts.
		 */
		bool BeginKeyDrag(int32 Index, bool bHandles)
		{
			const TSharedPtr<SWidgetAnimTimeline> Panel = PanelWeak.Pin();
			const FMarker* Marker = GetMarker(Index);
			if (!Panel.IsValid() || !Marker || !Marker->bDraggable || !ReadDragUnits(*Marker))
			{
				return false;
			}

			// Read before the drag starts: starting it picks the key, which may change which paths are shown.
			const int32 PressedLayer = Marker->Layer;
			const int32 PressedKey = Marker->Key;
			if (!(bHandles ? Panel->BeginKeyHandleDrag(PressedLayer, PressedKey) : Panel->BeginKeyPositionDrag(PressedLayer, PressedKey)))
			{
				return false;
			}
			bDragActive = true;
			bDragHandles = bHandles;
			bDragOutHandle = true;
			DragLayer = PressedLayer;
			DragKey = PressedKey;
			DragHandleIn = FVector2D::ZeroVector;
			DragHandleOut = FVector2D::ZeroVector;
			return true;
		}

		/** A press on a handle moved past the threshold: the key's handles are dragged. */
		bool BeginHandleDrag(int32 Index, bool bOut)
		{
			const TSharedPtr<SWidgetAnimTimeline> Panel = PanelWeak.Pin();
			const FMarker* Marker = GetMarker(Index);
			const TArray<FWidgetAnimLayer>* Layers = Panel.IsValid() ? Panel->GetLayers() : nullptr;
			if (!Marker || !Marker->bDraggable || !Layers || !Layers->IsValidIndex(Marker->Layer) || !ReadDragUnits(*Marker))
			{
				return false;
			}

			FVector2D HandleIn, HandleOut;
			(*Layers)[Marker->Layer].Transform.GetPathHandles(Marker->Key, HandleIn, HandleOut);
			const int32 PressedLayer = Marker->Layer;
			const int32 PressedKey = Marker->Key;
			if (!Panel->BeginKeyHandleDrag(PressedLayer, PressedKey))
			{
				return false;
			}
			bDragActive = true;
			bDragHandles = true;
			bDragOutHandle = bOut;
			DragLayer = PressedLayer;
			DragKey = PressedKey;
			DragHandleIn = HandleIn;
			DragHandleOut = HandleOut;
			return true;
		}

		/**
		 * The mouse moved during a drag. A key moves by it (Shift: along one axis). A handle follows it, and the other handle
		 * turns to stay opposite it, keeping its own length - unless Alt breaks them apart. Pulled out of a key, the two handles
		 * start opposite and equal.
		 */
		void UpdateDrag(const FVector2D& ScreenDelta, bool bShift, bool bAlt)
		{
			const TSharedPtr<SWidgetAnimTimeline> Panel = PanelWeak.Pin();
			if (!Panel.IsValid() || !bDragActive)
			{
				return;
			}

			FVector2D ScreenMove = ScreenDelta;
			if (bShift && !bDragHandles)
			{
				// Kept on the axis it has moved along most, on screen.
				if (FMath::Abs(ScreenMove.X) >= FMath::Abs(ScreenMove.Y))
				{
					ScreenMove.Y = 0.0;
				}
				else
				{
					ScreenMove.X = 0.0;
				}
			}
			const FVector2D Units = ScreenToUnits(DragUnitsX, DragUnitsY, ScreenMove);

			if (!bDragHandles)
			{
				Panel->UpdateKeyPositionDrag(Units);
				return;
			}

			const FVector2D Dragged = (bDragOutHandle ? DragHandleOut : DragHandleIn) + Units;
			const FVector2D Other = bDragOutHandle ? DragHandleIn : DragHandleOut;
			const double OtherLength = Other.Size();
			const FVector2D Opposite = OtherLength > UE_KINDA_SMALL_NUMBER && !Dragged.IsNearlyZero()
				? -Dragged.GetSafeNormal() * OtherLength
				: -Dragged;
			const FVector2D NewOther = bAlt ? Other : Opposite;
			Panel->UpdateKeyHandleDrag(bDragOutHandle ? NewOther : Dragged, bDragOutHandle ? Dragged : NewOther);
		}

		void EndDrag()
		{
			if (!bDragActive)
			{
				return;
			}
			bDragActive = false;
			DragLayer = INDEX_NONE;
			DragKey = INDEX_NONE;
			if (const TSharedPtr<SWidgetAnimTimeline> Panel = PanelWeak.Pin())
			{
				Panel->EndKeyPositionDrag();
			}
		}

		bool IsDragging() const { return bDragActive; }

	private:
		UWidgetBlueprint* GetBlueprint() const { return BlueprintWeak.Get(); }

		/**
		 * The designer widget this layer draws over: up the parents of its Widget Blueprint's designer preview to the
		 * SDesignerView. Looked for again only while it isn't known (once a second at most - finding the preview walks every
		 * User Widget); the designer outlives its preview, which is remade on every compile.
		 */
		void FindDesignerWidget()
		{
			if (DesignerWidget.IsValid())
			{
				return;
			}
			const double Now = FPlatformTime::Seconds();
			if (Now < NextDesignerSearch)
			{
				return;
			}
			NextDesignerSearch = Now + 1.0;

			// Up the preview's parents to the one exactly where the paths are drawn - the widget the designer painted them with.
			// Matched by place, not by type: the designer's own widget isn't always the one it hands its geometry from.
			UUserWidget* Preview = FindWidgetAnimDesignerPreview(GetBlueprint());
			TSharedPtr<SWidget> Walk = Preview ? Preview->GetCachedWidget() : nullptr;
			while (Walk.IsValid() && !IsDesignerRect(Walk->GetTickSpaceGeometry()))
			{
				Walk = Walk->GetParentWidget();
			}
			DesignerWidget = Walk;
			if (LogLevel() >= 1 && (Walk.IsValid() || !bReportedDesignerSearch))
			{
				bReportedDesignerSearch = true;
				UE_LOG(LogWidgetAnimMotionPath, Log, TEXT("Motion path layer: %s."), Walk.IsValid()
					? *FString::Printf(TEXT("its designer is %s, found up the preview"), *Walk->GetTypeAsString())
					: TEXT("its designer isn't found up the preview yet - it is found under the mouse instead, on the first press or hover over it"));
			}
		}

		/** Whether a point in the designer's space is inside it. */
		bool IsInDesigner(const FVector2f& Point) const
		{
			return Point.X >= DesignerMin.X && Point.Y >= DesignerMin.Y && Point.X <= DesignerMax.X && Point.Y <= DesignerMax.Y;
		}

		/** The screen per unit of the dragged key's widget's Translation, read now for the whole drag. */
		bool ReadDragUnits(const FMarker& Marker)
		{
			if (!Paths.IsValidIndex(Marker.Path) || !Paths[Marker.Path].bMapped || !Paths[Marker.Path].Mapping.Frames.IsValidIndex(Marker.ChainLevel)
				|| Marker.Poses.Num() != Paths[Marker.Path].Mapping.Frames.Num())
			{
				return false;
			}
			UnitsToScreen(Paths[Marker.Path].Mapping, Marker.ChainLevel, Marker.Poses.GetData(), DragUnitsX, DragUnitsY);
			return true;
		}

		/** Finds the panel and what it poses, works the paths out again when the animation changed, and maps them onto the designer. */
		void Update(const FGeometry& AllottedGeometry);

		/** The paths' poses and the keys' places, from the animation as posed: run only when that changed. */
		void Rebuild(SWidgetAnimTimeline& Panel, const IWidgetAnimPanelSource::FPosedAnimation& Posed, const TArray<UWidget*>& Widgets);

		/** Every path, diamond and handle onto the designer, through the widgets' layout this frame. */
		void MapToCanvas(const FGeometry& AllottedGeometry);

		/** The paths, diamonds and labels, in the designer's space. */
		int32 PaintPaths(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

		/** The handles of picked keys, and every key's diamond - in the Track Editor's key colors. */
		int32 PaintKeys(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const;

		void Clear()
		{
			Paths.Reset();
			Markers.Reset();
			BuiltVersion = MAX_uint32;
		}

		/**
		 * The widgets showing a path, as the panel's preview has them: the ones selected in the designer, the selected tracks', then
		 * every other widget the animation moves.
		 */
		void GatherWidgets(SWidgetAnimTimeline& Panel, const IWidgetAnimPanelSource::FPosedAnimation& Posed, TArray<UWidget*>& OutWidgets) const;

		/** Logs what Status and the paths' notes say now - to the Output Log when it changed - and keeps it for drawing. */
		void ReportStatus();

		TWeakObjectPtr<UWidgetBlueprint> BlueprintWeak;

		/** The designer as it was last painted: its geometry, its window, the frame, and what was selected in it. */
		FGeometry Designer;
		uint64 PaintedFrame = 0;

		/** The designer widget itself (its SDesignerView), found up the preview's parents - see FindDesignerWidget. */
		TWeakPtr<SWidget> DesignerWidget;
		double NextDesignerSearch = 0.0;
		bool bReportedDesignerSearch = false;
		TArray<FWidgetReference> DesignerSelection;
		FVector2f DesignerMin = FVector2f::ZeroVector;
		FVector2f DesignerMax = FVector2f::ZeroVector;

		TWeakPtr<SWidgetAnimTimeline> PanelWeak;

		TArray<FPath> Paths;
		TArray<FMarker> Markers;

		/** What the paths were worked out from - see Update. */
		uint32 BuiltVersion = MAX_uint32;
		uint32 BuiltSignature = 0;

		/** Where the playhead is, in seconds into one pass of the preview, and how long one pass is. */
		float PlayheadTime = 0.f;
		float PassTime = 0.f;

		/** Whether the preview is posed at the playhead - the panel on screen. Off screen it is as authored, and has no "now" to ring. */
		bool bPreviewPosed = false;

		// The mouse, from FMotionPathInput.
		int32 HoveredMarker = INDEX_NONE;
		int32 HoveredHandle = INDEX_NONE;
		int32 PressedMarker = INDEX_NONE;
		int32 PressedHandle = INDEX_NONE;
		FVector2D PressScreen = FVector2D::ZeroVector;
		bool bPressAlt = false;
		bool bPressDragging = false;

		bool bDragActive = false;
		bool bDragHandles = false;
		bool bDragOutHandle = true;
		int32 DragLayer = INDEX_NONE;
		int32 DragKey = INDEX_NONE;
		FVector2f DragUnitsX = FVector2f::ZeroVector;
		FVector2f DragUnitsY = FVector2f::ZeroVector;
		FVector2D DragHandleIn = FVector2D::ZeroVector;
		FVector2D DragHandleOut = FVector2D::ZeroVector;

		// WidgetAnim.LogMotionPaths: why the paths show or not.
		FString Status;
		FString RebuildSummary;
		FString LoggedStatus;
		FString LoggedDetail;
		TArray<FString> StatusLines;
		FVector2f StatusAnchor = FVector2f::ZeroVector;
	};

	// ---------------------------------------------------------------------------------------------
	// Working the paths out
	// ---------------------------------------------------------------------------------------------

	void FMotionPathLayer::GatherWidgets(SWidgetAnimTimeline& Panel, const IWidgetAnimPanelSource::FPosedAnimation& Posed, TArray<UWidget*>& OutWidgets) const
	{
		IWidgetAnimPanelSource* PanelSource = Panel.GetSource();
		UWidget* FallbackWidget = Posed.FallbackWidget;
		UUserWidget* Root = PanelSource ? PanelSource->GetPreviewRoot() : nullptr;

		// A designer widget on the panel's preview, which may be another instance of the same tree: found there by name.
		auto OnPanelPreview = [Root](UWidget* DesignerWidget, const UWidget* Template) -> UWidget*
		{
			if (DesignerWidget && Root && DesignerWidget != Root && GetOwningPreview(DesignerWidget) != Root)
			{
				return Root->WidgetTree && Template ? Root->WidgetTree->FindWidget(Template->GetFName()) : nullptr;
			}
			return DesignerWidget;
		};

		// What is selected in the designer - its paths through moving parents too. Not needed for anything to show.
		for (const FWidgetReference& Selected : DesignerSelection)
		{
			if (UWidget* OnPreview = OnPanelPreview(Selected.GetPreview(), Selected.GetTemplate()))
			{
				OutWidgets.AddUnique(OnPreview);
			}
		}

		// The tracks picked in the panel - or whose keys are.
		if (PanelSource)
		{
			TArray<int32> Picked = Panel.GetSelectedLayers();
			for (const SWidgetAnimTimeline::FKeyRef& Ref : Panel.GetSelectedKeys())
			{
				Picked.AddUnique(Ref.Layer);
			}
			for (const int32 LayerIndex : Picked)
			{
				if (UWidget* LayerWidget = PanelSource->GetLayerPreviewWidget(LayerIndex))
				{
					OutWidgets.AddUnique(LayerWidget);
				}
			}
		}

		// Then every other widget the animation moves - each layer group's Target Widget with a track that changes where it is -
		// so its path shows whatever is selected, or nothing.
		if (Posed.Settings)
		{
			for (const FWidgetAnimSettings& Layer : *Posed.Settings)
			{
				const bool bMovesPosition = Layer.AnimType == EWidgetAnimType::Slide
					|| Layer.AnimType == EWidgetAnimType::Shake
					|| (Layer.AnimType == EWidgetAnimType::Transform && Layer.Transform.IsAnimatingTransform());
				UWidget* Target = Layer.TargetWidget ? Layer.TargetWidget.Get() : FallbackWidget;
				if (bMovesPosition && !Layer.bMuted && Target)
				{
					OutWidgets.AddUnique(Target);
				}
			}
		}

		if (OutWidgets.Num() > MaxPaths)
		{
			OutWidgets.SetNum(MaxPaths);
		}
	}

	void FMotionPathLayer::Update(const FGeometry& AllottedGeometry)
	{
		const UWidgetAnimEditorSettings& Settings = UWidgetAnimEditorSettings::Get();
		const UWidgetBlueprint* Blueprint = GetBlueprint();

		const TSharedPtr<SWidgetAnimTimeline> Panel = Settings.bShowMotionPaths && Blueprint ? WidgetAnimScripting::FindLastUsedPanelForBlueprint(Blueprint) : nullptr;
		IWidgetAnimPanelSource* PanelSource = Panel.IsValid() ? Panel->GetSource() : nullptr;

		IWidgetAnimPanelSource::FPosedAnimation Posed;
		const bool bHasAnimation = PanelSource && PanelSource->GetPosedAnimation(Posed) && Posed.Settings && Posed.FallbackWidget;
		if (!Settings.bShowMotionPaths)
		{
			Status = TEXT("Off: Show Motion Paths is off (the Motion Path toggle in the panel's Track Controls).");
		}
		else if (!Blueprint)
		{
			Status = TEXT("Nothing to draw: no Widget Blueprint.");
		}
		else if (!Panel.IsValid())
		{
			Status = FString::Printf(TEXT("Nothing to draw: no Widget Animations panel of %s has been opened yet - open its Widget Animations drawer or tab once."), *Blueprint->GetName());
		}
		else if (!bHasAnimation)
		{
			Status = FString::Printf(TEXT("Nothing to draw: the Widget Animations panel of %s has no animation open (Animation Timeline or Edit Existing Asset), or the designer's preview of it wasn't found."), *Blueprint->GetName());
		}
		if (!bHasAnimation || !Settings.bShowMotionPaths)
		{
			// A drag can't outlive what it was dragging.
			EndDrag();
			if (Paths.Num() > 0 || Markers.Num() > 0)
			{
				Clear();
			}
			PanelWeak.Reset();
			StatusAnchor = DesignerMin + FVector2f(12.f, 40.f);
			return;
		}
		if (PanelWeak.Pin() != Panel)
		{
			EndDrag();
		}
		PanelWeak = Panel;

		TArray<UWidget*> Widgets;
		GatherWidgets(*Panel, Posed, Widgets);
		Status.Reset();
		if (Widgets.Num() == 0)
		{
			Status = TEXT("Nothing to draw: the open animation moves no widget (no Transform, Slide or Shake track), and none is selected.");
		}

		// Worked out again only when what it is worked out from changed: the animation as posed (an edit, another preview,
		// the source's own refresh), the widgets shown, the dot rate, or the panel's timeline (where its keys are).
		const float DotRate = Settings.bShowTimeInFrames ? Settings.PreviewFrameRate : Settings.MotionPathDotsPerSecond;
		uint32 Signature = GetTypeHash(DotRate);
		for (const UWidget* Shown : Widgets)
		{
			Signature = HashCombine(Signature, PointerHash(Shown));
		}
		Signature = HashCombine(Signature, GetTypeHash(Panel->GetPassSeconds()));
		Signature = HashCombine(Signature, GetTypeHash(PanelSource->GetInvert()));
		if (Posed.Version != BuiltVersion || Signature != BuiltSignature)
		{
			Rebuild(*Panel, Posed, Widgets);
			BuiltVersion = Posed.Version;
			BuiltSignature = Signature;
		}

		bPreviewPosed = Posed.bPosed;
		PlayheadTime = FMath::Clamp(PanelSource->GetPlayheadAlpha(), 0.f, 1.f) * PassTime;
		MapToCanvas(AllottedGeometry);

		// Where the status is written: under the designer's toolbar.
		StatusAnchor = DesignerMin + FVector2f(12.f, 40.f);
	}

	void FMotionPathLayer::Rebuild(SWidgetAnimTimeline& Panel, const IWidgetAnimPanelSource::FPosedAnimation& Posed, const TArray<UWidget*>& Widgets)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimMotionPathRebuild);

		Paths.Reset();
		Markers.Reset();
		RebuildSummary.Reset();

		static const FWidgetAnimBaselineSet NoBaselines;
		const FWidgetAnimBaselineSet& Baselines = Posed.Baselines ? *Posed.Baselines : NoBaselines;
		UWidget* FallbackWidget = Posed.FallbackWidget;
		IWidgetAnimPanelSource* PanelSource = Panel.GetSource();

		// Fanned out once, as the preview fans it out to pose it - its pass is the one the playhead's alpha is along.
		TArray<FWidgetAnimSettings> Expanded;
		const TArray<FWidgetAnimSettings>& Stack = UWidgetAnimLibrary::ExpandSettings(FallbackWidget, *Posed.Settings, Expanded) ? Expanded : *Posed.Settings;
		PassTime = UWidgetAnimLibrary::GetStackPassDuration(Stack);
		RebuildSummary = FString::Printf(TEXT("Animation: %d layer(s) (%d once staggers fan out), one pass %.2f s, preview %s."),
			Posed.Settings->Num(), Stack.Num(), PassTime, Posed.bPosed ? TEXT("posed by the panel") : TEXT("as authored - the panel is closed"));
		if (PassTime <= KINDA_SMALL_NUMBER)
		{
			RebuildSummary += TEXT(" Nothing to draw: the animation has no length.");
			return;
		}

		// The rate dots are at, and samples in between them for a smooth line.
		const UWidgetAnimEditorSettings& Settings = UWidgetAnimEditorSettings::Get();
		const float DotRate = FMath::Max(Settings.bShowTimeInFrames ? Settings.PreviewFrameRate : Settings.MotionPathDotsPerSecond, 1.f);
		const int32 SubSteps = FMath::Clamp(FMath::CeilToInt(LineSamplesPerSecond / DotRate), 1, 16);
		float Step = 1.f / (DotRate * SubSteps);
		int32 DotEvery = SubSteps;
		if (PassTime / Step > MaxSamples)
		{
			Step = PassTime / MaxSamples;
			DotEvery = FMath::Max(1, FMath::RoundToInt((1.f / DotRate) / Step));
		}

		// Where the panel shows its keys, for the diamonds.
		const TArray<FWidgetAnimLayer>* Layers = Panel.GetLayers();
		TArray<FVector2f> Spans;
		Panel.GetLayerSpans(Spans);
		const float PanelPass = Panel.GetPassSeconds();
		const bool bInvert = PanelSource && PanelSource->GetInvert();

		// What each chain widget is at rest: the baselines the preview poses over, and - for a widget of the chain that no layer
		// moves - the widget as authored in the Widget Blueprint. Never the preview's render transform as it stands, which
		// whatever is playing on the preview changes: a path read from it would shift as the animation plays.
		const UWidgetBlueprint* AuthoredBlueprint = PanelSource && PanelSource->GetWidgetBlueprint() ? PanelSource->GetWidgetBlueprint() : GetBlueprint();
		FWidgetAnimBaselineSet ChainBaselines;

		FWidgetAnimResult Result;
		auto PoseChainAt = [&](const TArray<FWidgetAnimSettings>& LayersToPose, const TArray<TWeakObjectPtr<UWidget>>& PosedChain, float StackTime, TArray<FWidgetTransform>& OutPoses)
		{
			Result.ResetKeepingCapacity();
			UWidgetAnimLibrary::EvaluateExpandedStackAtTime(LayersToPose, FMath::Min(StackTime, PassTime), /*bAtEnd=*/StackTime >= PassTime, Posed.bIsOutAnimation, Baselines, Result);
			for (const TWeakObjectPtr<UWidget>& Link : PosedChain)
			{
				OutPoses.Add(UWidgetAnimLibrary::GetResultPose(Result, Link.Get(), FallbackWidget, ChainBaselines));
			}
		};

		for (UWidget* Shown : Widgets)
		{
			if (!Shown || Paths.Num() >= MaxPaths)
			{
				continue;
			}

			// The widget and every parent up to the preview it is in - each one's pose moves it. The fallback widget is not
			// necessarily that preview: in a Widget Animation Builder it is PreviewImage.
			TArray<UWidget*> FullChain;
			for (UWidget* Link = Shown; Link; Link = Link->GetParent())
			{
				FullChain.Add(Link);
			}
			if (UUserWidget* OwningPreview = GetOwningPreview(Shown))
			{
				FullChain.AddUnique(OwningPreview);
			}

			// Only the layers that move one of them - what the rest do doesn't move this widget - and the highest one they move.
			TArray<FWidgetAnimSettings> Filtered;
			TSet<const UWidget*> Targeted;
			int32 Highest = INDEX_NONE;
			for (const FWidgetAnimSettings& Layer : Stack)
			{
				UWidget* Target = Layer.TargetWidget ? Layer.TargetWidget.Get() : FallbackWidget;
				const int32 Level = FullChain.IndexOfByKey(Target);
				if (Level != INDEX_NONE)
				{
					Filtered.Add(Layer);
					Targeted.Add(Target);
					Highest = FMath::Max(Highest, Level);
				}
			}
			if (Highest == INDEX_NONE)
			{
				continue;
			}

			const int32 PathIndex = Paths.AddDefaulted();
			FPath& Path = Paths[PathIndex];
			Path.Name = Shown->GetName();
			for (int32 Level = 0; Level <= Highest; ++Level)
			{
				Path.Chain.Add(FullChain[Level]);
			}
			Path.DotEvery = DotEvery;

			// The chain's rest poses: the preview's baselines as they are (so a moved widget composes exactly as the preview
			// poses it), and the authored pose for each widget of the chain that no layer moves and that has none.
			ChainBaselines.Baselines.Reset();
			if (Baselines.bCaptured)
			{
				ChainBaselines.Baselines = Baselines.Baselines;
			}
			ChainBaselines.bCaptured = true;
			ChainBaselines.bComposeTransforms = Baselines.bCaptured && Baselines.bComposeTransforms;
			for (const TWeakObjectPtr<UWidget>& Link : Path.Chain)
			{
				const UWidget* Linked = Link.Get();
				const bool bHasBaseline = ChainBaselines.Baselines.ContainsByPredicate([Linked](const FWidgetAnimBaseline& Candidate) { return Candidate.Widget.Get() == Linked; });
				if (!Linked || bHasBaseline || Targeted.Contains(Linked))
				{
					continue;
				}
				float AuthoredOpacity = 1.f;
				const FWidgetTransform Authored = GetWidgetAnimAuthoredTransform(Linked, AuthoredBlueprint, &AuthoredOpacity);
				FWidgetAnimBaseline& Rest = ChainBaselines.Baselines.AddDefaulted_GetRef();
				Rest.Widget = const_cast<UWidget*>(Linked);
				Rest.Opacity = AuthoredOpacity;
				Rest.Scale = Authored.Scale;
				Rest.Translation = Authored.Translation;
				Rest.Shear = Authored.Shear;
				Rest.Angle = Authored.Angle;
			}

			// The samples: every Step through the pass, and its very end.
			const int32 Count = FMath::FloorToInt(PassTime / Step) + 1;
			Path.Times.Reserve(Count + 1);
			Path.Poses.Reserve((Count + 1) * Path.Chain.Num());
			for (int32 Sample = 0; Sample < Count; ++Sample)
			{
				const float Time = Sample * Step;
				Path.Times.Add(Time);
				PoseChainAt(Filtered, Path.Chain, Time, Path.Poses);
			}
			if (Path.Times.Last() < PassTime - KINDA_SMALL_NUMBER)
			{
				Path.Times.Add(PassTime);
				PoseChainAt(Filtered, Path.Chain, PassTime, Path.Poses);
			}

			RebuildSummary += FString::Printf(TEXT(" %s: %d layer(s) play on it or its parents (up to %s), %d samples."),
				*Path.Name, Filtered.Num(), *FullChain[Highest]->GetName(), Path.Times.Num());

			// The diamonds: every key of every track moving it - its own position tracks, and its parents' moves and turns.
			if (!Layers || !PanelSource)
			{
				continue;
			}
			for (int32 LayerIndex = 0; LayerIndex < Layers->Num() && Spans.IsValidIndex(LayerIndex); ++LayerIndex)
			{
				const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
				if (Layer.IsAnimationTrack() || Panel.IsLayerHidden(LayerIndex))
				{
					continue;
				}
				const int32 Level = FullChain.IndexOfByKey(PanelSource->GetLayerPreviewWidget(LayerIndex));
				if (Level == INDEX_NONE || Level > Highest)
				{
					continue;
				}
				const bool bKeyframed = Layer.AnimType == EWidgetAnimType::Transform && Layer.Transform.IsAnimatingTransform();
				const bool bMovesPosition = bKeyframed
					|| Layer.AnimType == EWidgetAnimType::Slide
					// A parent's scale and turn swing the widget about the parent's pivot; its own don't move its pivot.
					|| (Level > 0 && (Layer.AnimType == EWidgetAnimType::Scale || Layer.AnimType == EWidgetAnimType::Rotate));
				if (!bMovesPosition)
				{
					continue;
				}

				TArray<float> KeyTimes;
				Panel.GetKeyTimes(LayerIndex, KeyTimes);
				TArray<int32> KeyIndices;
				bool bEdges = false;
				if (KeyTimes.Num() == 0 && WidgetAnimTypeSupportsValueKeys(Layer.AnimType))
				{
					// A track going from its start to its end: those two, as the track's own edges.
					KeyTimes = { 0.f, Spans[LayerIndex].Y };
					KeyIndices = { 0, Layer.ValueKeys.NumPoints() - 1 };
					bEdges = true;
				}
				else
				{
					for (int32 Key = 0; Key < KeyTimes.Num(); ++Key)
					{
						KeyIndices.Add(Key);
					}
				}

				// A keyframed track eased across its whole run - or timed by a spring - passes its keys when its eased progress
				// reaches them, not at their own times.
				const FWidgetAnimSettings* Played = Posed.Settings->IsValidIndex(LayerIndex) && (*Posed.Settings)[LayerIndex].AnimType == Layer.AnimType ? &(*Posed.Settings)[LayerIndex] : nullptr;
				const bool bEasedAcross = bKeyframed && Played && (Layer.IsSpringTimed() || Layer.Transform.EasingSpan == EWidgetKeyEasingSpan::WholeTrack);
				const float Run = Spans[LayerIndex].Y;

				for (int32 Point = 0; Point < KeyTimes.Num(); ++Point)
				{
					FMarker& Marker = Markers.AddDefaulted_GetRef();
					Marker.Layer = LayerIndex;
					Marker.Key = KeyIndices[Point];
					Marker.bEdge = bEdges;
					Marker.bDraggable = !bEdges && Panel.CanDragKeyPosition(LayerIndex, Marker.Key);
					Marker.bEditable = Panel.CanEditLayer(LayerIndex);
					Marker.ChainLevel = Level;
					Marker.Path = PathIndex;

					float KeySeconds = KeyTimes[Point];
					if (bEasedAcross && Run > KINDA_SMALL_NUMBER)
					{
						KeySeconds = FindEasedPassing(*Played, FMath::Clamp(KeySeconds / Run, 0.f, 1.f)) * Run;
					}
					Marker.PanelSeconds = Spans[LayerIndex].X + KeySeconds;

					// Where the preview shows the key - mirrored when the preview plays inverted, as it focuses a key then.
					float Alpha = PanelPass > KINDA_SMALL_NUMBER ? FMath::Clamp(Marker.PanelSeconds / PanelPass, 0.f, 1.f) : 0.f;
					if (bInvert)
					{
						Alpha = 1.f - Alpha;
					}
					Marker.StackTime = Alpha * PassTime;
					Marker.NearSample = FMath::Clamp(FMath::RoundToInt(Marker.StackTime / Step), 0, Path.Times.Num() - 1);
					PoseChainAt(Filtered, Path.Chain, Marker.StackTime, Marker.Poses);
				}
			}
		}
	}

	void FMotionPathLayer::MapToCanvas(const FGeometry& AllottedGeometry)
	{
		const FSlateRenderTransform AbsoluteToCanvas = AllottedGeometry.GetAccumulatedRenderTransform().Inverse();

		for (FPath& Path : Paths)
		{
			Path.bShown = false;
			Path.Canvas.Reset();
			Path.MapNote.Reset();
			Path.MapDetail.Reset();

			Path.bMapped = ReadChain(Path.Chain, Path.Mapping);
			const int32 ChainCount = Path.Chain.Num();
			const int32 SampleCount = Path.Times.Num();
			if (!Path.bMapped || Path.Mapping.Frames.Num() != ChainCount || SampleCount == 0 || Path.Poses.Num() != SampleCount * ChainCount)
			{
				Path.bMapped = false;
				Path.MapNote = FString::Printf(TEXT("%s: not drawn - it, or a parent of it, isn't on screen (no Slate widget, or no size)."), *Path.Name);
				continue;
			}

			Path.Canvas.Reserve(SampleCount);
			FBox2f Bounds(ForceInit);
			for (int32 Sample = 0; Sample < SampleCount; ++Sample)
			{
				const FVector2f Point = AbsoluteToCanvas.TransformPoint(MapChain(Path.Mapping, &Path.Poses[Sample * ChainCount]));
				Path.Canvas.Add(Point);
				Bounds += Point;
			}

			// Now, through the same layout: its pose now on each widget.
			TArray<FWidgetTransform> NowPoses;
			for (const FChainFrame& Frame : Path.Mapping.Frames)
			{
				NowPoses.Add(Frame.Now);
			}
			Path.Here = AbsoluteToCanvas.TransformPoint(MapChain(Path.Mapping, NowPoses.GetData()));

			const FVector2f Extent = Bounds.GetSize();
			Path.bShown = FMath::Max(Extent.X, Extent.Y) > StillExtent;
			Path.MapNote = Path.bShown
				? FString::Printf(TEXT("%s: drawn."), *Path.Name)
				: FString::Printf(TEXT("%s: not drawn - it doesn't move on screen (its layers don't change where it is: only fades, its own turns or scales...)."), *Path.Name);
			Path.MapDetail = FString::Printf(TEXT("%s: path from (%.0f, %.0f) to (%.0f, %.0f) on this layer, %.0f x %.0f; the layer is %.0f x %.0f; the widget is now at (%.0f, %.0f)."),
				*Path.Name, Path.Canvas[0].X, Path.Canvas[0].Y, Path.Canvas.Last().X, Path.Canvas.Last().Y, Extent.X, Extent.Y,
				AllottedGeometry.GetLocalSize().X, AllottedGeometry.GetLocalSize().Y, Path.Here.X, Path.Here.Y);
		}

		// The diamonds, on paths that are drawn - and, around the playhead, only those in the stretch drawn - and the handles of
		// the picked keys whose path isn't a corner.
		const UWidgetAnimEditorSettings& Settings = UWidgetAnimEditorSettings::Get();
		const bool bAround = Settings.MotionPathRange == EWidgetAnimMotionPathRange::AroundPlayhead;
		const TSharedPtr<SWidgetAnimTimeline> Panel = PanelWeak.Pin();
		const TArray<FWidgetAnimLayer>* Layers = Panel.IsValid() ? Panel->GetLayers() : nullptr;
		for (FMarker& Marker : Markers)
		{
			Marker.bShown = false;
			Marker.bShowHandles = false;
			if (!Paths.IsValidIndex(Marker.Path) || !Paths[Marker.Path].bShown)
			{
				continue;
			}
			const FPath& Path = Paths[Marker.Path];
			if (Marker.Poses.Num() != Path.Mapping.Frames.Num())
			{
				continue;
			}
			if (bAround && !IsMarkerDragged(Marker) && FMath::Abs(Marker.StackTime - PlayheadTime) > Settings.MotionPathAroundSeconds + KINDA_SMALL_NUMBER)
			{
				continue;
			}
			Marker.Absolute = MapChain(Path.Mapping, Marker.Poses.GetData());
			Marker.Canvas = AbsoluteToCanvas.TransformPoint(Marker.Absolute);
			Marker.bShown = IsInDesigner(Marker.Canvas);
			if (!Marker.bShown)
			{
				continue;
			}

			if (!Marker.bDraggable || !Layers || !Layers->IsValidIndex(Marker.Layer) || !IsMarkerSelected(Marker))
			{
				continue;
			}
			const FWidgetTransformAnim& Anim = (*Layers)[Marker.Layer].Transform;
			if (!Anim.Keys.IsValidIndex(Marker.Key) || Anim.Keys[Marker.Key].PathShape == EWidgetKeyPathShape::Linear)
			{
				continue;
			}
			FVector2D HandleIn, HandleOut;
			Anim.GetPathHandles(Marker.Key, HandleIn, HandleOut);
			FVector2f UnitsX, UnitsY;
			UnitsToScreen(Path.Mapping, Marker.ChainLevel, Marker.Poses.GetData(), UnitsX, UnitsY);
			auto HandleAt = [&](const FVector2D& Handle)
			{
				return AbsoluteToCanvas.TransformPoint(Marker.Absolute + UnitsX * static_cast<float>(Handle.X) + UnitsY * static_cast<float>(Handle.Y));
			};
			Marker.HandleInCanvas = HandleAt(HandleIn);
			Marker.HandleOutCanvas = HandleAt(HandleOut);
			// The first key has nothing before it to arrive from, the last nothing after it to leave toward.
			Marker.bShowHandles = true;
		}
	}

	void FMotionPathLayer::OpenMarkerMenu(int32 Index, const FVector2D& ScreenPosition, const FWidgetPath& OwnerPath)
	{
		const TSharedPtr<SWidgetAnimTimeline> Panel = PanelWeak.Pin();
		const FMarker* Marker = GetMarker(Index);
		const TArray<FWidgetAnimLayer>* Layers = Panel.IsValid() ? Panel->GetLayers() : nullptr;
		if (!Marker || !Marker->bDraggable || !Layers || !Layers->IsValidIndex(Marker->Layer))
		{
			return;
		}

		const TWeakPtr<SWidgetAnimTimeline> WeakPanel = Panel;
		const int32 LayerIndex = Marker->Layer;
		const int32 KeyIndex = Marker->Key;

		// The key right-clicked - and the other picked keys of its track with it, when it is one of them.
		TArray<int32> KeyIndices = { KeyIndex };
		if (Panel->IsKeySelected(LayerIndex, KeyIndex))
		{
			for (const SWidgetAnimTimeline::FKeyRef& Ref : Panel->GetSelectedKeys())
			{
				if (Ref.Layer == LayerIndex)
				{
					KeyIndices.AddUnique(Ref.Key);
				}
			}
		}
		TArray<int32> AllKeys;
		for (int32 Key = 0; Key < (*Layers)[LayerIndex].Transform.Keys.Num(); ++Key)
		{
			AllKeys.Add(Key);
		}

		auto IsShape = [WeakPanel, LayerIndex, KeyIndex](EWidgetKeyPathShape Shape)
		{
			const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakPanel.Pin();
			const TArray<FWidgetAnimLayer>* PinnedLayers = Pinned.IsValid() ? Pinned->GetLayers() : nullptr;
			return PinnedLayers && PinnedLayers->IsValidIndex(LayerIndex) && (*PinnedLayers)[LayerIndex].Transform.Keys.IsValidIndex(KeyIndex)
				&& (*PinnedLayers)[LayerIndex].Transform.Keys[KeyIndex].PathShape == Shape;
		};
		auto SetShape = [WeakPanel, LayerIndex](const TArray<int32>& Keys, EWidgetKeyPathShape Shape)
		{
			if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakPanel.Pin())
			{
				Pinned->SetKeyPathShape(LayerIndex, Keys, Shape);
			}
		};
		auto IsSpan = [WeakPanel, LayerIndex](EWidgetKeyEasingSpan Span)
		{
			const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakPanel.Pin();
			const TArray<FWidgetAnimLayer>* PinnedLayers = Pinned.IsValid() ? Pinned->GetLayers() : nullptr;
			return PinnedLayers && PinnedLayers->IsValidIndex(LayerIndex) && (*PinnedLayers)[LayerIndex].Transform.EasingSpan == Span;
		};
		auto SetSpan = [WeakPanel, LayerIndex](EWidgetKeyEasingSpan Span)
		{
			if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakPanel.Pin())
			{
				Pinned->SetLayerEasingSpan(LayerIndex, Span);
			}
		};

		FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

		Menu.BeginSection("Path", LOCTEXT("PathSection", "Path Through This Key"));
		struct FShapeEntry { EWidgetKeyPathShape Shape; FText Label; FText Tip; };
		const FShapeEntry ShapeEntries[] =
		{
			{ EWidgetKeyPathShape::Linear, LOCTEXT("PathLinear", "Corner"), LOCTEXT("PathLinearTip", "Straight in from the key before, straight on to the key after.") },
			{ EWidgetKeyPathShape::Smooth, LOCTEXT("PathSmooth", "Smooth"), LOCTEXT("PathSmoothTip", "A smooth curve through the key, shaped by the keys either side - an arc through three keys.") },
			{ EWidgetKeyPathShape::Bezier, LOCTEXT("PathBezier", "Bezier Handles"), LOCTEXT("PathBezierTip", "A curve shaped by the key's own handles, dragged on the path. Alt-drag a key to pull them out.") },
		};
		for (const FShapeEntry& Entry : ShapeEntries)
		{
			const EWidgetKeyPathShape Shape = Entry.Shape;
			Menu.AddMenuEntry(Entry.Label, Entry.Tip, FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([SetShape, KeyIndices, Shape]() { SetShape(KeyIndices, Shape); }),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([IsShape, Shape]() { return IsShape(Shape); })),
				NAME_None, EUserInterfaceActionType::RadioButton);
		}
		Menu.EndSection();

		Menu.BeginSection("Track", LOCTEXT("TrackSection", "This Track"));
		Menu.AddMenuEntry(LOCTEXT("SmoothEveryKey", "Smooth Path Through Every Key"), LOCTEXT("SmoothEveryKeyTip", "Every key of this track Smooth: one flowing curve through all of them."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([SetShape, AllKeys]() { SetShape(AllKeys, EWidgetKeyPathShape::Smooth); })));
		Menu.AddMenuEntry(LOCTEXT("StraightEveryKey", "Straight Path Through Every Key"), LOCTEXT("StraightEveryKeyTip", "Every key of this track a Corner: straight lines from key to key."), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([SetShape, AllKeys]() { SetShape(AllKeys, EWidgetKeyPathShape::Linear); })));
		Menu.AddMenuSeparator();
		Menu.AddMenuEntry(LOCTEXT("EaseEachSegment", "Ease Each Segment"), LOCTEXT("EaseEachSegmentTip", "The track's Easing shapes each stretch between two keys: it eases into and out of every key."), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([SetSpan]() { SetSpan(EWidgetKeyEasingSpan::EachSegment); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([IsSpan]() { return IsSpan(EWidgetKeyEasingSpan::EachSegment); })),
			NAME_None, EUserInterfaceActionType::RadioButton);
		Menu.AddMenuEntry(LOCTEXT("EaseWholeTrack", "Ease Whole Track"), LOCTEXT("EaseWholeTrackTip", "The track's Easing shapes the whole run: it eases out of its start and into its end, and passes through the keys between without slowing."), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([SetSpan]() { SetSpan(EWidgetKeyEasingSpan::WholeTrack); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([IsSpan]() { return IsSpan(EWidgetKeyEasingSpan::WholeTrack); })),
			NAME_None, EUserInterfaceActionType::RadioButton);
		Menu.EndSection();

		// Over the designer the diamond was clicked in: the menu belongs to its window.
		if (!OwnerPath.IsValid())
		{
			return;
		}
		FSlateApplication::Get().PushMenu(OwnerPath.GetLastWidget(), OwnerPath, Menu.MakeWidget(), ScreenPosition, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	}

	// ---------------------------------------------------------------------------------------------
	// WidgetAnim.LogMotionPaths
	// ---------------------------------------------------------------------------------------------

	void FMotionPathLayer::ReportStatus()
	{
		const int32 Level = LogLevel();
		StatusLines.Reset();
		if (Level <= 0)
		{
			LoggedStatus.Reset();
			LoggedDetail.Reset();
			return;
		}

		// What stopped it, else what the rebuild found and how each path did.
		if (!Status.IsEmpty())
		{
			StatusLines.Add(Status);
		}
		else
		{
			StatusLines.Add(RebuildSummary);
			for (const FPath& Path : Paths)
			{
				StatusLines.Add(Path.MapNote);
			}
			if (Paths.Num() > 0)
			{
				StatusLines.Add(FString::Printf(TEXT("%d key diamond(s)."), Markers.Num()));
			}
		}

		const FString Joined = FString::Join(StatusLines, TEXT(" "));
		if (Joined != LoggedStatus)
		{
			LoggedStatus = Joined;
			const UWidgetBlueprint* Blueprint = GetBlueprint();
			UE_LOG(LogWidgetAnimMotionPath, Log, TEXT("Motion path (%s): %s"), Blueprint ? *Blueprint->GetName() : TEXT("?"), *Joined);
		}

		if (Level >= 2)
		{
			TArray<FString> Details;
			for (const FPath& Path : Paths)
			{
				if (!Path.MapDetail.IsEmpty())
				{
					Details.Add(Path.MapDetail);
				}
			}
			const FString JoinedDetail = FString::Join(Details, TEXT(" "));
			if (JoinedDetail != LoggedDetail)
			{
				LoggedDetail = JoinedDetail;
				UE_LOG(LogWidgetAnimMotionPath, Log, TEXT("Motion path on screen: %s"), *JoinedDetail);
			}
		}
	}

	// ---------------------------------------------------------------------------------------------
	// Drawing
	// ---------------------------------------------------------------------------------------------

	void FMotionPathLayer::Paint(const TSet<FWidgetReference>& Selection, const FGeometry& DesignerGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId)
	{
		// The designer as it is painted now, after its preview: its own local space is the canvas. DesignerGeometry is in its
		// window's space - Slate paints each window from its own top left - while the mouse and the widgets' tick geometry, which
		// the paths are worked out from, are in desktop space: the two differ by where the window is on the desktop. Drawn
		// through DesignerGeometry; mapped and hit-tested through Designer, the same geometry moved onto the desktop.
		Designer = DesignerGeometry;
		if (const SWindow* Window = OutDrawElements.GetPaintWindow())
		{
			Designer.AppendTransform(FSlateLayoutTransform(FVector2f(FVector2D(Window->GetPositionInScreen()))));
		}
		PaintedFrame = GFrameCounter;
		FindDesignerWidget();
		DesignerMin = FVector2f::ZeroVector;
		DesignerMax = FVector2f(DesignerGeometry.GetLocalSize());
		DesignerSelection = Selection.Array();

		Update(Designer);
		ReportStatus();

		OutDrawElements.PushClip(FSlateClippingZone(DesignerGeometry));
		PaintPaths(DesignerGeometry, OutDrawElements, LayerId);
		OutDrawElements.PopClip();
	}

	int32 FMotionPathLayer::PaintPaths(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
	{
		const UWidgetAnimEditorSettings& Settings = UWidgetAnimEditorSettings::Get();
		const bool bAround = Settings.MotionPathRange == EWidgetAnimMotionPathRange::AroundPlayhead;
		const float RangeStart = bAround ? PlayheadTime - Settings.MotionPathAroundSeconds : -BIG_NUMBER;
		const float RangeEnd = bAround ? PlayheadTime + Settings.MotionPathAroundSeconds : BIG_NUMBER;

		auto DrawLine = [&](const TArray<FVector2f>& Line, const FLinearColor& LineColor, float Thickness, int32 Layer)
		{
			if (Line.Num() >= 2)
			{
				FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Line, ESlateDrawEffect::None, LineColor, /*bAntialias=*/true, Thickness);
			}
		};
		auto DrawDot = [&](const FVector2f& Center, float DotSize, const FSlateBrush* Brush, const FLinearColor& DotColor, int32 Layer)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, Layer,
				AllottedGeometry.ToPaintGeometry(FVector2f(DotSize, DotSize), FSlateLayoutTransform(Center - FVector2f(DotSize, DotSize) * 0.5f)),
				Brush, ESlateDrawEffect::None, DotColor);
		};

		for (const FPath& Path : Paths)
		{
			const int32 SampleCount = FMath::Min(Path.Canvas.Num(), Path.Times.Num());
			if (!Path.bShown || SampleCount < 2)
			{
				continue;
			}

			// Which steps are jumps: far longer than the steps either side of them.
			TArray<float> StepLengths;
			StepLengths.SetNumUninitialized(SampleCount - 1);
			for (int32 Sample = 0; Sample + 1 < SampleCount; ++Sample)
			{
				StepLengths[Sample] = FVector2f::Distance(Path.Canvas[Sample], Path.Canvas[Sample + 1]);
			}
			auto IsJump = [&](int32 StepIndex)
			{
				const float StepLength = StepLengths[StepIndex];
				if (StepLength < JumpMinDistance)
				{
					return false;
				}
				const float Before = StepIndex > 0 ? StepLengths[StepIndex - 1] : 0.f;
				const float After = StepIndex + 2 < SampleCount ? StepLengths[StepIndex + 1] : 0.f;
				return StepLength > JumpRatio * FMath::Max(Before, After);
			};

			// The line, in runs: broken at jumps (drawn dashed), and at the playhead (what has played drawn fainter).
			const FLinearColor Ahead = Palette::PathLine;
			const FLinearColor Behind = Palette::PathLine.CopyWithNewOpacity(Palette::PathLine.A * Palette::PathPastOpacity);
			TArray<FVector2f> Run;
			bool bRunPast = false;
			auto FlushRun = [&]()
			{
				DrawLine(Run, Palette::PathUnderlay.CopyWithNewOpacity(Palette::PathUnderlay.A * (bRunPast ? Palette::PathPastOpacity : 1.f)), Palette::PathThickness + 2.f, LayerId);
				DrawLine(Run, bRunPast ? Behind : Ahead, Palette::PathThickness, LayerId + 1);
				Run.Reset();
			};

			for (int32 Sample = 0; Sample + 1 < SampleCount; ++Sample)
			{
				const float From = Path.Times[Sample];
				const float To = Path.Times[Sample + 1];
				if (To < RangeStart || From > RangeEnd)
				{
					if (Run.Num() > 0)
					{
						FlushRun();
					}
					continue;
				}

				const bool bPast = To <= PlayheadTime + KINDA_SMALL_NUMBER;
				if (Run.Num() > 0 && bPast != bRunPast)
				{
					const FVector2f Joint = Run.Last();
					FlushRun();
					Run.Add(Joint);
				}
				bRunPast = bPast;

				if (IsJump(Sample))
				{
					if (Run.Num() > 0)
					{
						FlushRun();
					}

					// Dashed, from where it was to where it jumps.
					const FVector2f JumpStart = Path.Canvas[Sample];
					const FVector2f JumpEnd = Path.Canvas[Sample + 1];
					const float JumpLength = StepLengths[Sample];
					const FVector2f Direction = (JumpEnd - JumpStart) / JumpLength;
					for (float Along = 0.f; Along < JumpLength; Along += Palette::JumpDash * 2.f)
					{
						DrawLine({ JumpStart + Direction * Along, JumpStart + Direction * FMath::Min(Along + Palette::JumpDash, JumpLength) },
							Palette::Jump.CopyWithNewOpacity(Palette::Jump.A * (bPast ? Palette::PathPastOpacity : 1.f)), 1.f, LayerId + 1);
					}
					continue;
				}

				if (Run.Num() == 0)
				{
					Run.Add(Path.Canvas[Sample]);
				}
				Run.Add(Path.Canvas[Sample + 1]);
			}
			if (Run.Num() > 0)
			{
				FlushRun();
			}

			// A dot at every frame: their spacing is the speed.
			const int32 DotEvery = FMath::Max(Path.DotEvery, 1);
			for (int32 Sample = 0; Sample < SampleCount; Sample += DotEvery)
			{
				const float Time = Path.Times[Sample];
				if (Time < RangeStart || Time > RangeEnd)
				{
					continue;
				}
				const float Opacity = Time <= PlayheadTime + KINDA_SMALL_NUMBER ? Palette::PathPastOpacity : 1.f;
				DrawDot(Path.Canvas[Sample], Palette::DotSize + 2.f, CircleBrush(), Palette::DotOutline.CopyWithNewOpacity(Palette::DotOutline.A * Opacity), LayerId + 2);
				DrawDot(Path.Canvas[Sample], Palette::DotSize, CircleBrush(), Palette::Dot.CopyWithNewOpacity(Palette::Dot.A * Opacity), LayerId + 3);
			}

			// Where the widget is now, at the playhead: a ring.
			if (bPreviewPosed)
			{
				DrawDot(Path.Here, Palette::HereSize + 2.f, CircleBrush(), Palette::DotOutline, LayerId + 4);
				DrawDot(Path.Here, Palette::HereSize, CircleBrush(), Palette::Here, LayerId + 5);
				DrawDot(Path.Here, Palette::HereSize - 4.f, CircleBrush(), Palette::DotOutline, LayerId + 6);
			}
		}

		// The lines from each picked key to its handles, under the handles and the diamonds.
		for (const FMarker& Marker : Markers)
		{
			if (!Marker.bShown || !Marker.bShowHandles)
			{
				continue;
			}
			for (const FVector2f& Handle : { Marker.HandleInCanvas, Marker.HandleOutCanvas })
			{
				if (FVector2f::Distance(Handle, Marker.Canvas) > 0.5f)
				{
					DrawLine({ Marker.Canvas, Handle }, Palette::HandleLine, 1.f, LayerId + 6);
				}
			}
		}

		// The diamonds and handles over the line - FMotionPathInput takes the mouse for them.
		const int32 MaxLayer = PaintKeys(AllottedGeometry, OutDrawElements, LayerId + 7);

		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		auto DrawLabel = [&](const FString& Text, const FVector2f& TopLeft, int32 Layer)
		{
			const FVector2f TextSize(Measure->Measure(Text, Font));
			const FVector2f Padding(4.f, 2.f);
			FSlateDrawElement::MakeBox(OutDrawElements, Layer,
				AllottedGeometry.ToPaintGeometry(TextSize + Padding * 2.f, FSlateLayoutTransform(TopLeft)),
				LabelBrush(), ESlateDrawEffect::None, Palette::LabelBackground);
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 1,
				AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TopLeft + Padding)),
				Text, Font, ESlateDrawEffect::None, Palette::LabelText);
			return TextSize + Padding * 2.f;
		};

		// WidgetAnim.LogMotionPaths: why the paths show or not.
		{
			FVector2f LineAt = StatusAnchor + FVector2f(4.f, 4.f);
			for (const FString& Line : StatusLines)
			{
				LineAt.Y += DrawLabel(Line, LineAt, MaxLayer + 1).Y + 2.f;
			}
		}

		// The key labels, over everything: every key's time with Show Key Times, else only the key under the mouse or dragged -
		// and a Transform key's position while it is. Beside the diamond, off the path: on the side of the path facing up, at
		// right angles to it there, so the label never lies along it.
		const TSharedPtr<SWidgetAnimTimeline> Panel = PanelWeak.Pin();
		if (!Panel.IsValid())
		{
			return MaxLayer + 2;
		}
		const TArray<FWidgetAnimLayer>* Layers = Panel->GetLayers();
		const int32 LabelLayer = MaxLayer + 3;
		for (int32 Index = 0; Index < Markers.Num(); ++Index)
		{
			const FMarker& Marker = Markers[Index];
			const bool bHot = IsMarkerHot(Index);
			if (!Marker.bShown || (!bHot && !Settings.bMotionPathKeyTimes) || !Paths.IsValidIndex(Marker.Path))
			{
				continue;
			}

			FString Label = Panel->FormatTime(Marker.PanelSeconds);
			if (bHot && Marker.bDraggable && Layers && Layers->IsValidIndex(Marker.Layer) && (*Layers)[Marker.Layer].Transform.Keys.IsValidIndex(Marker.Key))
			{
				const FVector2D Translation = (*Layers)[Marker.Layer].Transform.GetKeyTransform(Marker.Key).Translation;
				Label += FString::Printf(TEXT("   %s, %s"), *FString::SanitizeFloat(FMath::RoundToDouble(Translation.X * 10.0) / 10.0, 0), *FString::SanitizeFloat(FMath::RoundToDouble(Translation.Y * 10.0) / 10.0, 0));
			}

			// The path's direction at the key, from the samples either side of it.
			const FPath& Path = Paths[Marker.Path];
			FVector2f Along(1.f, 0.f);
			if (Path.Canvas.Num() >= 2)
			{
				const int32 Before = FMath::Clamp(Marker.NearSample - 2, 0, Path.Canvas.Num() - 1);
				const int32 After = FMath::Clamp(Marker.NearSample + 2, 0, Path.Canvas.Num() - 1);
				const FVector2f Tangent = Path.Canvas[After] - Path.Canvas[Before];
				if (Tangent.SizeSquared() > 1.e-4f)
				{
					Along = Tangent.GetSafeNormal();
				}
			}
			FVector2f Side(-Along.Y, Along.X);
			if (Side.Y > 0.f || (FMath::IsNearlyZero(Side.Y) && Side.X < 0.f))
			{
				Side = -Side;
			}

			// The label's box pushed out along that side until its nearest edge clears the diamond.
			const FVector2f LabelSize = FVector2f(Measure->Measure(Label, Font)) + FVector2f(8.f, 4.f);
			const float Reach = Palette::KeyHitSize * 0.5f + Palette::LabelGap
				+ FMath::Abs(Side.X) * LabelSize.X * 0.5f + FMath::Abs(Side.Y) * LabelSize.Y * 0.5f;
			const FVector2f Center = Marker.Canvas + Side * Reach;
			DrawLabel(Label, Center - LabelSize * 0.5f, LabelLayer);
		}
		return LabelLayer + 1;
	}

	// ---------------------------------------------------------------------------------------------
	// The diamonds and handles
	// ---------------------------------------------------------------------------------------------

	int32 FMotionPathLayer::PaintKeys(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
	{
		auto Grip = [&](const FVector2f& Center, float Diameter, const FLinearColor& GripColor, int32 Layer)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, Layer,
				AllottedGeometry.ToPaintGeometry(FVector2f(Diameter, Diameter), FSlateLayoutTransform(Center - FVector2f(Diameter, Diameter) * 0.5f)),
				CircleBrush(), ESlateDrawEffect::None, GripColor);
		};
		auto Diamond = [&](const FVector2f& Center, float DiamondSize, const FLinearColor& DiamondColor, int32 Layer)
		{
			const float Half = DiamondSize * 0.5f;
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				Layer,
				AllottedGeometry.ToPaintGeometry(
					FVector2f(DiamondSize, DiamondSize),
					FSlateLayoutTransform(Center - FVector2f(Half, Half)),
					FSlateRenderTransform(FQuat2f(FMath::DegreesToRadians(45.f))),
					FVector2f(0.5f, 0.5f)),
				SquareBrush(),
				ESlateDrawEffect::None,
				DiamondColor);
		};

		for (int32 Index = 0; Index < Markers.Num(); ++Index)
		{
			const FMarker& Marker = Markers[Index];
			if (!Marker.bShown)
			{
				continue;
			}

			// A picked key's handles, under the diamonds.
			if (Marker.bShowHandles)
			{
				for (int32 Handle = 0; Handle < 2; ++Handle)
				{
					const FVector2f Center = Handle == 0 ? Marker.HandleInCanvas : Marker.HandleOutCanvas;
					if (!IsInDesigner(Center))
					{
						continue;
					}
					const bool bHot = (HoveredMarker == Index && HoveredHandle == Handle) || (PressedMarker == Index && PressedHandle == Handle);
					const float GripSize = Palette::HandleSize * (bHot ? 1.25f : 1.f);
					Grip(Center, GripSize + 2.f, Palette::HandleOutline, LayerId);
					Grip(Center, GripSize, Palette::HandleFill, LayerId + 1);
				}
			}

			// The Track Editor's keys exactly: its colors, and its glow on a picked key.
			const bool bSelected = IsMarkerSelected(Marker);
			const bool bHovered = IsMarkerHot(Index);
			const bool bPressed = PressedMarker == Index && PressedHandle == INDEX_NONE;
			FLinearColor Fill = Marker.bEditable ? KeyPalette::Key : KeyPalette::KeyReadOnly;
			float Scale = 1.f;
			if (bSelected)
			{
				Fill = KeyPalette::KeySelected;
			}
			if (bHovered)
			{
				Fill = bSelected ? KeyPalette::KeySelected * 1.2f : (Marker.bEditable ? KeyPalette::KeyHovered : KeyPalette::Key);
				Scale = 1.15f;
			}
			if (bPressed)
			{
				Fill = (bSelected ? KeyPalette::KeySelected : KeyPalette::Key) * 0.7f;
				Scale = 0.95f;
			}
			Fill.A = 1.f;

			// A track's start or end is a little smaller than a key.
			const float KeySize = Palette::KeySize * (Marker.bEdge ? 0.8f : 1.f) * Scale;
			if (bSelected)
			{
				Diamond(Marker.Canvas, KeySize + 5.f, KeyPalette::KeySelectedGlow, LayerId + 2);
			}
			Diamond(Marker.Canvas, KeySize + 2.f, KeyPalette::KeyOutline, LayerId + 3);
			Diamond(Marker.Canvas, KeySize, Fill, LayerId + 4);
		}
		return LayerId + 4;
	}

	// =================================================================================================
	// The mouse, for the diamonds and handles
	// =================================================================================================

	/**
	 * Sees the mouse before any widget does. A press on a diamond or handle a layer drew this frame - where its designer itself is
	 * under the mouse, not a menu or drawer over it - is the layer's: it is consumed, so the designer neither selects nor marquees,
	 * and the moves and the release that follow are the layer's too until the button is let go. Everything else passes through
	 * untouched; moving over the designer only lights up the diamond or handle under the mouse.
	 */
	class FMotionPathInput : public IInputProcessor
	{
	public:
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
		{
			// A release that never came here - the window lost focus mid-drag: the drag ends where it is.
			if (const TSharedPtr<FMotionPathLayer> Layer = Pressed.Pin())
			{
				if (!SlateApp.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
				{
					Layer->Release(SlateApp.GetModifierKeys(), /*bCancelled=*/true);
					Pressed.Reset();
				}
			}
		}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			const FKey Button = MouseEvent.GetEffectingButton();
			if (Button != EKeys::LeftMouseButton && Button != EKeys::RightMouseButton)
			{
				return false;
			}

			int32 Marker = INDEX_NONE;
			int32 Handle = INDEX_NONE;
			const TSharedPtr<FMotionPathLayer> Layer = FindHit(SlateApp, MouseEvent.GetScreenSpacePosition(), Marker, Handle, nullptr, /*bLog=*/true);
			if (!Layer.IsValid())
			{
				return false;
			}

			if (Button == EKeys::LeftMouseButton)
			{
				Layer->Press(Marker, Handle, MouseEvent.GetScreenSpacePosition(), MouseEvent.IsAltDown());
				Pressed = Layer;
				LeftMarker = Marker;
				LeftHandle = Handle;
				SetCursor(SlateApp, Handle != INDEX_NONE || Layer->GetCursorFor(Marker, Handle) == EMouseCursor::GrabHand ? EMouseCursor::GrabHandClosed : EMouseCursor::Hand);
				return true;
			}

			// Right: a diamond's menu, opened as the button comes up.
			if (Handle != INDEX_NONE)
			{
				return false;
			}
			RightPressed = Layer;
			RightMarker = Marker;
			return true;
		}

		virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			const FVector2D Screen = MouseEvent.GetScreenSpacePosition();
			if (const TSharedPtr<FMotionPathLayer> Layer = Pressed.Pin())
			{
				Layer->PressMove(Screen, MouseEvent.GetModifierKeys());
				SetCursor(SlateApp, Layer->GetCursorFor(LeftMarker, LeftHandle));
				return true;
			}

			// Lit up under the mouse.
			int32 Marker = INDEX_NONE;
			int32 Handle = INDEX_NONE;
			const TSharedPtr<FMotionPathLayer> Hit = FindHit(SlateApp, Screen, Marker, Handle, nullptr);
			for (const TWeakPtr<FMotionPathLayer>& Weak : GetDesignerLayers())
			{
				if (const TSharedPtr<FMotionPathLayer> Layer = Weak.Pin())
				{
					Layer->SetHover(Layer == Hit ? Marker : INDEX_NONE, Layer == Hit ? Handle : INDEX_NONE);
				}
			}

			// Over a diamond or handle: its cursor. The move is kept from the designer, which would otherwise have Slate ask it
			// for its own cursor straight after and put that back. Anywhere else the designer has the move, and its cursor.
			if (Hit.IsValid())
			{
				SetCursor(SlateApp, Hit->GetCursorFor(Marker, Handle));
				return true;
			}
			return false;
		}

		virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			const FKey Button = MouseEvent.GetEffectingButton();
			if (Button == EKeys::LeftMouseButton)
			{
				if (const TSharedPtr<FMotionPathLayer> Layer = Pressed.Pin())
				{
					Pressed.Reset();
					Layer->Release(MouseEvent.GetModifierKeys(), /*bCancelled=*/false);
					return true;
				}
				return false;
			}

			if (Button == EKeys::RightMouseButton)
			{
				const TSharedPtr<FMotionPathLayer> PressedLayer = RightPressed.Pin();
				const int32 PressedMarker = RightMarker;
				RightPressed.Reset();
				RightMarker = INDEX_NONE;
				if (!PressedLayer.IsValid())
				{
					return false;
				}

				// Released on the diamond it was pressed on: its menu.
				int32 Marker = INDEX_NONE;
				int32 Handle = INDEX_NONE;
				FWidgetPath Path;
				if (FindHit(SlateApp, MouseEvent.GetScreenSpacePosition(), Marker, Handle, &Path) == PressedLayer && Marker == PressedMarker && Handle == INDEX_NONE)
				{
					PressedLayer->OpenMarkerMenu(Marker, MouseEvent.GetScreenSpacePosition(), Path);
				}
				return true;
			}
			return false;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("WidgetAnimMotionPath"); }

	private:
		/** The layer whose diamond or handle is at Screen - checked cheaply against what was drawn, then that its designer is on top. */
		static TSharedPtr<FMotionPathLayer> FindHit(FSlateApplication& SlateApp, const FVector2D& Screen, int32& OutMarker, int32& OutHandle, FWidgetPath* OutPath, bool bLog = false)
		{
			// WidgetAnim.LogMotionPaths 1: why a press over a designer was or wasn't taken.
			const bool bExplain = bLog && LogLevel() >= 1;
			for (const TWeakPtr<FMotionPathLayer>& Weak : GetDesignerLayers())
			{
				const TSharedPtr<FMotionPathLayer> Layer = Weak.Pin();
				if (!Layer.IsValid() || !Layer->IsOver(Screen))
				{
					continue;
				}
				if (!Layer->HitTest(Screen, OutMarker, OutHandle))
				{
					if (bExplain)
					{
						FString Nearest;
						Layer->DescribeNearest(Screen, Nearest);
						UE_LOG(LogWidgetAnimMotionPath, Log, TEXT("Motion path press: over the designer, but on no diamond or handle - left to the designer. %s"), *Nearest);
					}
					continue;
				}

				// Only where the designer itself is under the mouse - not a menu, a drawer or another window over it.
				FWidgetPath Path = SlateApp.LocateWindowUnderMouse(Screen, SlateApp.GetInteractiveTopLevelWindows());
				if (!Layer->IsDesignerUnder(Path))
				{
					if (bExplain)
					{
						UE_LOG(LogWidgetAnimMotionPath, Log, TEXT("Motion path press: on diamond %d, but the layer's designer isn't what is under the mouse - left alone. %s"),
							OutMarker, *Layer->DescribePath(Path));
					}
					continue;
				}
				if (bExplain)
				{
					UE_LOG(LogWidgetAnimMotionPath, Log, TEXT("Motion path press: taken - diamond %d%s."), OutMarker, OutHandle == INDEX_NONE ? TEXT("") : TEXT(", a handle"));
				}
				if (OutPath)
				{
					*OutPath = MoveTemp(Path);
				}
				return Layer;
			}
			OutMarker = INDEX_NONE;
			OutHandle = INDEX_NONE;
			return nullptr;
		}

		static void SetCursor(FSlateApplication& SlateApp, EMouseCursor::Type CursorType)
		{
			if (const TSharedPtr<ICursor> PlatformCursor = SlateApp.GetPlatformCursor())
			{
				PlatformCursor->SetType(CursorType);
			}
		}

		TWeakPtr<FMotionPathLayer> Pressed;
		int32 LeftMarker = INDEX_NONE;
		int32 LeftHandle = INDEX_NONE;
		TWeakPtr<FMotionPathLayer> RightPressed;
		int32 RightMarker = INDEX_NONE;
	};

	namespace
	{
		TSharedPtr<FMotionPathInput>& GetInput()
		{
			static TSharedPtr<FMotionPathInput> Input;
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
			GetInput() = MakeShared<FMotionPathInput>();
			FSlateApplication::Get().RegisterInputPreProcessor(GetInput());
		}
	}

	void ReleaseInput()
	{
		if (--GetInputUsers() > 0)
		{
			return;
		}
		GetInputUsers() = 0;
		if (GetInput().IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(GetInput());
		}
		GetInput().Reset();
	}
}

// =====================================================================================================
// Extension - the designer draws the paths through it, every frame it is drawn
// =====================================================================================================

namespace WidgetAnimMotionPathImpl
{
	/** FDesignerExtension::Blueprint, whether it is kept weak or not. */
	UWidgetBlueprint* ResolveBlueprint(const TWeakObjectPtr<UWidgetBlueprint>& Weak) { return Weak.Get(); }
	UWidgetBlueprint* ResolveBlueprint(UWidgetBlueprint* Raw) { return Raw; }
}

FWidgetAnimMotionPathExtension::FWidgetAnimMotionPathExtension()
{
	ExtensionId = FName(TEXT("WidgetAnimMotionPath"));
}

FWidgetAnimMotionPathExtension::~FWidgetAnimMotionPathExtension()
{
	Layer.Reset();
}

void FWidgetAnimMotionPathExtension::Paint(const TSet<FWidgetReference>& Selection, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	using namespace WidgetAnimMotionPathImpl;

	if (!Layer.IsValid())
	{
		UWidgetBlueprint* OwnBlueprint = ResolveBlueprint(Blueprint);
		if (!OwnBlueprint)
		{
			return;
		}
		Layer = MakeShared<FMotionPathLayer>(TWeakObjectPtr<UWidgetBlueprint>(OwnBlueprint));
		Layer->Register();
	}
	Layer->Paint(Selection, AllottedGeometry, OutDrawElements, LayerId);
}

TSharedRef<FDesignerExtension> FWidgetAnimMotionPathFactory::CreateDesignerExtension() const
{
	return MakeShared<FWidgetAnimMotionPathExtension>();
}

#undef LOCTEXT_NAMESPACE
