// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Rendering/RenderingCommon.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeTypes.h"

#include "FlowListScrollBar.generated.h"

class UFlowList;
class UFlowListScrollBar;
struct FWidgetShapeFrame;
struct FWidgetShapeMesh;

/** How a Flow List Scroll Bar shows where its list is. */
UENUM(BlueprintType)
enum class EFlowListScrollBarStyle : uint8
{
	Bar UMETA(ToolTip = "A track with a thumb: its length how much of the list is in view, its place where the list is. Drag the thumb, or click the track to go there."),
	Dots UMETA(ToolTip = "Page dots, one per item: the current one grows and lights as the list reaches it, gliding between dots as it moves. Click a dot to go to its item. A long list shows Max Dots at a time, the ones at the edges smaller.")
};

/** The line a scroll bar - its track, thumb, fill, ticks or dots - runs along. */
UENUM(BlueprintType)
enum class EFlowListScrollBarPath : uint8
{
	Straight UMETA(ToolTip = "A straight line down the middle of the bar."),
	Curve UMETA(ToolTip = "Bowed into an arc - the edge of a dial, the curve of a wheel or arc list beside it: Curve Bow says how far its middle stands out, Curve Length how much of the bar it spans, Curve Rotation how it is turned."),
	Spline UMETA(ToolTip = "Through Spline Points, any shape - drawn on the designer's canvas: select the bar and click Edit Path above it, as for a Flow List's Spline path.")
};

/** How a scroll bar's thumb is drawn. */
UENUM(BlueprintType)
enum class EFlowListScrollBarThumbKind : uint8
{
	Bar UMETA(ToolTip = "A stretch of the bar along its path, as long as the share of the list in view."),
	Knob UMETA(ToolTip = "A knob sliding along the path - a brush (a circle, an icon, a material) at Knob Size, turned with the path if Align Knob To Path.")
};

/** A ready-made look for a scroll bar. Everything stays editable afterwards. */
UENUM(BlueprintType)
enum class EFlowListScrollBarPreset : uint8
{
	Custom UMETA(ToolTip = "Leaves the settings as they are."),
	Minimal UMETA(ToolTip = "A thin pill on a faint track that thickens under the pointer and hides while the list rests - a phone's scroll indicator."),
	Classic UMETA(ToolTip = "A solid track and a thicker thumb with an outline, always shown - a desktop scroll bar."),
	Segmented UMETA(ToolTip = "A dashed track, the part before the thumb filled in - a progress meter."),
	Arc UMETA(ToolTip = "Bowed into an arc, a pill thumb gliding round it - beside a wheel or arc list."),
	Dial UMETA(ToolTip = "An arc with a tick per item and a round knob - a dial's detents. The current item's tick lights."),
	PageDots UMETA(DisplayName = "Page Dots", ToolTip = "Page dots with a pill for the current item.")
};

/** One line of a scroll bar - its track, thumb or fill - drawn along its path. */
USTRUCT(BlueprintType)
struct NAMETBD_API FFlowListScrollBarLine
{
	GENERATED_BODY()

	/** Draws this line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (DisplayName = "Show"))
	bool bShow = true;

	/** A color, a gradient (across the whole bar - a linear one at 90 degrees runs top to bottom) or an image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "bShow"))
	FWidgetShapePaint Paint;

	/** How thick it is, across the path, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "bShow", ClampMin = "0", UIMax = "40"))
	float Thickness = 5.f;

	/** How thick it grows while the pointer is over the bar or the thumb is held - easier to grab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "bShow", ClampMin = "0", UIMax = "40"))
	float HoverThickness = 9.f;

	/** Multiplies its colors while the pointer is over the bar. White leaves them as they are. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "bShow"))
	FLinearColor HoverTint = FLinearColor::White;

	/** Multiplies its colors while the thumb is held. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "bShow"))
	FLinearColor PressedTint = FLinearColor::White;

	/** How its ends are drawn: Round makes a pill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "bShow"))
	EWidgetShapeLineCap Cap = EWidgetShapeLineCap::Round;

	/** Cuts it into dashes this long, in Slate units: a segmented bar. 0 draws it solid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "bShow", ClampMin = "0", UIMax = "40"))
	float DashLength = 0.f;

	/** The space between dashes. 0 leaves gaps as long as the dashes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "bShow && DashLength > 0", ClampMin = "0", UIMax = "40"))
	float DashGap = 0.f;

	/** An outline around it, this wide, in Slate units. 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "bShow", ClampMin = "0", UIMax = "8"))
	float OutlineWidth = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "bShow && OutlineWidth > 0"))
	FLinearColor OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

	FFlowListScrollBarLine() = default;
	FFlowListScrollBarLine(const FLinearColor& Color, float InThickness, float InHoverThickness);
};

/**
 * Draws a Flow List Scroll Bar: reads its UMG widget's settings and its list live on every paint (as SWidgetShape does), and
 * turns drags and clicks into scrolling. Every part is laid along one path - straight, an arc or a drawn curve - and drawn
 * as shapes are, anti-aliased, with colors, gradients or images.
 */
class NAMETBD_API SFlowListScrollBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFlowListScrollBar) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UFlowListScrollBar>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The list moved or changed: shown again if hidden, and repainted. */
	void NotifyActivity();

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

private:
	/** The bar's path in local space, flattened, and how far along it each point is. */
	struct FBarPath
	{
		TArray<FVector2f> Points;
		TArray<float> Distances;
		float Total = 0.f;

		/** The point Distance along, clamped to the path, and the way it runs there. */
		FVector2f At(float Distance, FVector2f* OutTangent = nullptr) const;

		/** How far along the path the point on it nearest Point is. */
		float Project(const FVector2f& Point) const;

		/** The part of the path from From to To along it. */
		void Slice(float From, float To, TArray<FVector2f>& OutPoints) const;
	};

	/** Where the thumb is along the path, in Slate units. */
	struct FThumbPlace
	{
		float Start = 0.f;
		float Length = 0.f;
		float Center = 0.f;

		/** How far along the path the thumb can travel: the path less the thumb. */
		float Room = 0.f;
	};

	bool IsVertical() const;
	void BuildPath(const FVector2f& Size, FBarPath& Out) const;
	FThumbPlace PlaceThumb(const FBarPath& Path) const;

	/** How far down the list the thumb shows: the list's own fraction, or - with Smooth Thumb - a spring following it. */
	float GetShownFraction() const;
	float GetListFraction() const;

	float GetShownOpacity() const;
	float FractionAt(const FGeometry& Geometry, const FVector2f& Absolute) const;
	void WakeUp();
	EActiveTimerReturnType OnAnimate(double InCurrentTime, float InDeltaTime);

	/** Strokes Points with Line's look, at Thickness, its colors multiplied by Tint. Its outline first, if it has one. */
	void PaintLine(const TArray<FVector2f>& Points, const FFlowListScrollBarLine& Line, float Thickness, const FLinearColor& Tint,
		const FGeometry& Geometry, const FWidgetShapeFrame& Frame, float PixelScale, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& WidgetStyle) const;

	/** Draws a mesh: each vertex colored by ColorAt (its index, where it is in the widget's box 0..1), faded by its coverage. */
	void PaintMesh(const FWidgetShapeMesh& Mesh, const FSlateBrush* ImageBrush, const FWidgetShapeFrame& Frame, const FGeometry& Geometry,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, TFunctionRef<FLinearColor(int32, const FVector2f&)> ColorAt) const;

	int32 PaintBar(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, float Opacity) const;
	int32 PaintDots(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, float Opacity) const;

	TWeakObjectPtr<UFlowListScrollBar> Owner;

	/** When the list last moved, for Auto Hide. */
	double LastActivityTime = -1.0e9;
	float HoverAmount = 0.f;
	bool bDragging = false;

	/** Where on the thumb a drag took hold of it, along the path. */
	float GrabOffset = 0.f;

	/** Smooth Thumb: where the thumb is shown, and how fast it is moving there. */
	float SmoothFraction = 0.f;
	float SmoothVelocity = 0.f;
	bool bSmoothPlaced = false;

	/** The dots as last painted - each one's item, middle and reach - for clicks. */
	struct FDotHit
	{
		int32 Index = INDEX_NONE;
		FVector2f Center = FVector2f::ZeroVector;
		float Radius = 0.f;
	};
	mutable TArray<FDotHit> DotHits;

	// Reused from paint to paint.
	mutable TArray<FSlateVertex> Vertices;
	mutable TArray<SlateIndex> Indices;

	TSharedPtr<FActiveTimerHandle> ActiveTimer;
};

/**
 * A scroll bar or page dots for a Flow List - as Rive's Scroll Bar constraint follows a scrolling layout: drop it next to a
 * list and it follows the nearest one, or the one named Flow List Name.
 *
 * Styled like the list itself. Start from a Style Preset, then shape it:
 *   Path   - straight, bowed into an arc, or bent along a curve you draw - so a bar beside a wheel curves with it.
 *   Track  - the line the thumb runs on: color, gradient or image, thickness, round or square ends, dashes, an outline.
 *   Thumb  - a stretch of the bar or a knob (any brush); thicker and tinted under the pointer and while held; it can glide
 *            after the list on a spring.
 *   Fill   - the track filled in up to the thumb, as a progress meter.
 *   Ticks  - a mark per item across the path - a dial's detents - the current one lit.
 *   Dots   - page dots instead of a bar, laid along the same path.
 * It can hide while the list rests.
 */
UCLASS(meta = (DisplayName = "Flow List Scroll Bar"))
class NAMETBD_API UFlowListScrollBar : public UWidget
{
	GENERATED_BODY()

public:
	UFlowListScrollBar();

	/** Fills the settings below with a look. Everything stays editable afterwards. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar")
	EFlowListScrollBarPreset StylePreset = EFlowListScrollBarPreset::Custom;

	/** The Flow List to follow, by its name in the Widget Blueprint. None: the nearest one (see UFlowList::FindNearestFlowList). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (DisplayName = "Flow List Name"))
	FName FlowListName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar")
	EFlowListScrollBarStyle Style = EFlowListScrollBarStyle::Bar;

	/** Runs the way its list does. Off: Orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (DisplayName = "Match List Orientation"))
	bool bMatchListOrientation = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (EditCondition = "!bMatchListOrientation"))
	TEnumAsByte<EOrientation> Orientation = Orient_Vertical;

	/** Dragging the thumb, clicking the track or a dot scrolls the list. Off: it only shows where the list is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (DisplayName = "Interactive"))
	bool bInteractive = true;

	/** Fades out once the list has rested for Hide Delay, and back in as soon as it moves or the pointer comes over it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (DisplayName = "Auto Hide"))
	bool bAutoHide = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (ClampMin = "0", UIMax = "5", Units = "s", EditCondition = "bAutoHide"))
	float HideDelay = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar", meta = (ClampMin = "0", UIMax = "1", Units = "s", EditCondition = "bAutoHide"))
	float FadeTime = 0.25f;

	// Path

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Path", meta = (DisplayName = "Path"))
	EFlowListScrollBarPath PathShape = EFlowListScrollBarPath::Straight;

	/**
	 * How far the middle of the curve stands out from the straight line between its ends, in Slate units. Positive bows toward
	 * the right of a vertical bar and the bottom of a horizontal one; negative the other way. Up to a half circle. Give the
	 * bar room across for it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Path", meta = (UIMin = "-100", UIMax = "100", EditCondition = "PathShape == EFlowListScrollBarPath::Curve", EditConditionHides))
	float CurveBow = 12.f;

	/** How much of the bar's length the curve spans, end to end: 1 is all of it, 0.5 the middle half. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Path", meta = (ClampMin = "0.05", ClampMax = "1", UIMin = "0.05", UIMax = "1", EditCondition = "PathShape == EFlowListScrollBarPath::Curve", EditConditionHides))
	float CurveLength = 1.f;

	/** Turns the curve about the bar's middle, in degrees, clockwise. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Path", meta = (UIMin = "-180", UIMax = "180", ForceUnits = "Degrees", EditCondition = "PathShape == EFlowListScrollBarPath::Curve", EditConditionHides))
	float CurveRotation = 0.f;

	/**
	 * The path's points, as fractions of the bar's size (0,0 its top left, 1,1 its bottom right). It passes through each, the
	 * first where the list starts. Easiest drawn on the designer's canvas: select the bar and click Edit Path above it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Path", meta = (EditCondition = "PathShape == EFlowListScrollBarPath::Spline", EditConditionHides))
	TArray<FVector2D> SplinePoints = { FVector2D(0.35, 0.04), FVector2D(0.7, 0.5), FVector2D(0.35, 0.96) };

	/** Room left at each end of a Straight or Curve path, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Path", meta = (ClampMin = "0", UIMax = "40"))
	float EndPadding = 0.f;

	// Bar

	/** The line the thumb runs along. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar", EditConditionHides))
	FFlowListScrollBarLine Track;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (DisplayName = "Thumb Kind", EditCondition = "Style == EFlowListScrollBarStyle::Bar", EditConditionHides))
	EFlowListScrollBarThumbKind ThumbKind = EFlowListScrollBarThumbKind::Bar;

	/** The thumb, as a stretch of the bar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar && ThumbKind == EFlowListScrollBarThumbKind::Bar", EditConditionHides))
	FFlowListScrollBarLine Thumb;

	/** The shortest the thumb gets however long the list, so it can always be grabbed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (ClampMin = "4", UIMax = "200", EditCondition = "Style == EFlowListScrollBarStyle::Bar && ThumbKind == EFlowListScrollBarThumbKind::Bar", EditConditionHides))
	float MinThumbLength = 28.f;

	/** The knob: a circle by default - an image, an icon or a material too. Its tint is multiplied by the hover and held tints. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar && ThumbKind == EFlowListScrollBarThumbKind::Knob", EditConditionHides))
	FSlateBrush KnobBrush;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar && ThumbKind == EFlowListScrollBarThumbKind::Knob", EditConditionHides))
	FVector2D KnobSize = FVector2D(14.0, 14.0);

	/** The knob's size while the pointer is over the bar or the knob is held. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar && ThumbKind == EFlowListScrollBarThumbKind::Knob", EditConditionHides))
	FVector2D KnobHoverSize = FVector2D(18.0, 18.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar && ThumbKind == EFlowListScrollBarThumbKind::Knob", EditConditionHides))
	FLinearColor KnobHoverTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar && ThumbKind == EFlowListScrollBarThumbKind::Knob", EditConditionHides))
	FLinearColor KnobPressedTint = FLinearColor::White;

	/** Turns the knob with the path, its X along it - an arrow or a notch that points the way the bar runs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (DisplayName = "Align Knob To Path", EditCondition = "Style == EFlowListScrollBarStyle::Bar && ThumbKind == EFlowListScrollBarThumbKind::Knob", EditConditionHides))
	bool bAlignKnobToPath = false;

	/** The track filled in from its start up to the thumb - a progress meter. Off by default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar", EditConditionHides))
	FFlowListScrollBarLine Fill;

	/** The thumb glides after the list on Thumb Spring rather than sitting exactly where the list is. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (DisplayName = "Smooth Thumb", EditCondition = "Style == EFlowListScrollBarStyle::Bar", EditConditionHides))
	bool bSmoothThumb = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Bar", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar && bSmoothThumb", EditConditionHides))
	FWidgetSpringParams ThumbSpring;

	// Ticks

	/** A mark across the path at each item - a dial's detents. A list with more items than Max Ticks marks every so many. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Ticks", meta = (DisplayName = "Show Ticks", EditCondition = "Style == EFlowListScrollBarStyle::Bar", EditConditionHides))
	bool bShowTicks = false;

	/** How long each tick is, across the path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Ticks", meta = (ClampMin = "0", UIMax = "40", EditCondition = "Style == EFlowListScrollBarStyle::Bar && bShowTicks", EditConditionHides))
	float TickLength = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Ticks", meta = (ClampMin = "0", UIMax = "8", EditCondition = "Style == EFlowListScrollBarStyle::Bar && bShowTicks", EditConditionHides))
	float TickWidth = 2.f;

	/** Moves the ticks across the path: 0 centers them on it; positive toward the side a positive Curve Bow bows to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Ticks", meta = (UIMin = "-40", UIMax = "40", EditCondition = "Style == EFlowListScrollBarStyle::Bar && bShowTicks", EditConditionHides))
	float TickOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Ticks", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar && bShowTicks", EditConditionHides))
	FLinearColor TickColor = FLinearColor(1.f, 1.f, 1.f, 0.3f);

	/** The current item's tick: ticks light up as the list reaches their items. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Ticks", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Bar && bShowTicks", EditConditionHides))
	FLinearColor ActiveTickColor = FLinearColor::White;

	/** How long the current item's tick grows to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Ticks", meta = (ClampMin = "0", UIMax = "40", EditCondition = "Style == EFlowListScrollBarStyle::Bar && bShowTicks", EditConditionHides))
	float ActiveTickLength = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Ticks", meta = (ClampMin = "2", UIMax = "100", EditCondition = "Style == EFlowListScrollBarStyle::Bar && bShowTicks", EditConditionHides))
	int32 MaxTicks = 40;

	// Dots

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	FSlateBrush DotBrush;

	/** A dot's size: X along the path, Y across it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	FVector2D DotSize = FVector2D(8.0, 8.0);

	/** The current item's dot: wider makes the pill most page indicators use. Dots grow into it as the list reaches them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	FVector2D ActiveDotSize = FVector2D(22.0, 8.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	FLinearColor DotColor = FLinearColor(1.f, 1.f, 1.f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	FLinearColor ActiveDotColor = FLinearColor::White;

	/** Multiplies the dots' colors while the pointer is over them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	FLinearColor DotHoverTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (ClampMin = "0", UIMax = "40", EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	float DotSpacing = 8.f;

	/** The most dots shown at once. A longer list shows the ones around the current item, shrinking those at the edges. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (ClampMin = "3", UIMax = "30", EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	int32 MaxDots = 9;

	/** Turns each dot with the path, so a pill lies along an arc. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll Bar|Dots", meta = (DisplayName = "Align Dots To Path", EditCondition = "Style == EFlowListScrollBarStyle::Dots", EditConditionHides))
	bool bAlignDotsToPath = true;

	/** Follows List instead of looking one up by name. Null goes back to looking. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void SetFlowList(UFlowList* List);

	/** The list followed, found if it hasn't been yet. */
	UFUNCTION(BlueprintPure, Category = "Scroll Bar")
	UFlowList* GetFlowList() const;

	/** Fills the settings with a preset's look. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Bar")
	void ApplyStylePreset(EFlowListScrollBarPreset Preset);

	/** Draws the bar again after its settings were changed directly - by the designer's path editor, say. */
	void RefreshBar();

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	void Bind(UFlowList* List) const;
	void Unbind() const;
	void HandleListUpdated() const;

	TSharedPtr<SFlowListScrollBar> MyBar;

	/** Set with Set Flow List. */
	TWeakObjectPtr<UFlowList> ExplicitList;

	/** The list followed and its OnListUpdated binding. Found lazily: a list placed after the bar isn't built yet when the bar is. */
	mutable TWeakObjectPtr<UFlowList> BoundList;
	mutable FDelegateHandle UpdatedHandle;
	mutable double LastSearchTime = -1.0e9;
};
