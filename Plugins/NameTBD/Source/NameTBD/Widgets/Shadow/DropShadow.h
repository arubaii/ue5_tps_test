// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Extensions/UIComponent.h"
#include "Widgets/SCompoundWidget.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"
#include "NameTBD/Widgets/WidgetMotion.h"

#include "DropShadow.generated.h"

class UWidget;

/** What one layer of a Drop Shadow is. */
UENUM(BlueprintType)
enum class EDropShadowKind : uint8
{
	Outer UMETA(DisplayName = "Drop Shadow", ToolTip = "Cast outward from the widget's shape, as CSS box-shadow: moved by Offset, grown by Spread, softened by Blur."),
	Inner UMETA(DisplayName = "Inner Shadow", ToolTip = "Inside the widget's shape, from its edges in (CSS inset): a pressed or recessed look, or an inner glow in a light color. Drawn over the widget."),
	Floor UMETA(DisplayName = "Floor Shadow", ToolTip = "A soft oval on the floor under the widget, as if it hovered above it: Floor Width across, Floor Height tall, Offset below the widget's bottom edge. Lifting the widget (Elevation) spreads and fades it.")
};

/** One shadow. A Drop Shadow draws its layers in order, each over the last - several soft layers make one natural shadow. */
USTRUCT(BlueprintType)
struct NAMETBD_API FDropShadowLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow", meta = (EditCondition = "bEnabled"))
	EDropShadowKind Kind = EDropShadowKind::Outer;

	/**
	 * Where the shadow falls, in Slate units: +Y down, +X right. On a Floor Shadow, Y is the gap between the widget's bottom
	 * edge and the oval's middle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow", meta = (EditCondition = "bEnabled"))
	FVector2D Offset = FVector2D(0.0, 4.0);

	/** How soft, in Slate units: the blur radius as CSS gives it (a Gaussian of half this). 0 is a hard shadow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow", meta = (EditCondition = "bEnabled", ClampMin = "0", UIMax = "96", ForceUnits = "px"))
	float Blur = 12.f;

	/** Grows (or, negative, shrinks) the shadow's shape before it is blurred, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow", meta = (EditCondition = "bEnabled", UIMin = "-32", UIMax = "32", ForceUnits = "px"))
	float Spread = 0.f;

	/** Its color; alpha is how dark (or, for a light color, how bright). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow", meta = (EditCondition = "bEnabled"))
	FLinearColor Color = FLinearColor(0.f, 0.f, 0.f, 0.25f);

	/** Floor Shadow: how wide the oval is, as a fraction of the widget's width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow", meta = (EditCondition = "bEnabled && Kind == EDropShadowKind::Floor", EditConditionHides, ClampMin = "0.05", ClampMax = "2"))
	float FloorWidth = 0.8f;

	/** Floor Shadow: how tall the oval is, in Slate units, before Blur. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shadow", meta = (EditCondition = "bEnabled && Kind == EDropShadowKind::Floor", EditConditionHides, ClampMin = "1", UIMax = "64", ForceUnits = "px"))
	float FloorHeight = 10.f;

	FDropShadowLayer() = default;
	FDropShadowLayer(EDropShadowKind InKind, const FVector2D& InOffset, float InBlur, float InSpread, const FLinearColor& InColor)
		: Kind(InKind), Offset(InOffset), Blur(InBlur), Spread(InSpread), Color(InColor) {}
};

/** A ready-made shadow: picking one sets the Layers (from Shadow Color where it uses one). Editing the layers makes it Custom. */
UENUM(BlueprintType)
enum class EDropShadowPreset : uint8
{
	Subtle UMETA(ToolTip = "A tight, faint shadow just under the edge: list rows, inputs, flat cards."),
	Soft UMETA(ToolTip = "The modern card: a wide, soft shadow pulled in at the sides (negative spread) and a faint close one."),
	Elevated UMETA(ToolTip = "Raised well off the page: a key shadow below and an ambient one around it, as Material's higher elevations."),
	Layered UMETA(DisplayName = "Sleek Layered", ToolTip = "Six stacked shadows each twice the last (1, 2, 4 ... 32): a crisp contact shadow that fades into a long, very soft tail - the smoothest, most natural falloff."),
	Dramatic UMETA(DisplayName = "Long & Dramatic", ToolTip = "Deep and long, pulled in hard at the sides: a hero card, a modal floating far above the page."),
	Hard UMETA(DisplayName = "Hard Offset", ToolTip = "A solid, unblurred shadow down and to the right - the bold neo-brutalist / retro look."),
	Glow UMETA(ToolTip = "A colored glow all round (Shadow Color - blue unless one is set): neon, a focused or selected state."),
	Tinted UMETA(DisplayName = "Colored Shadow", ToolTip = "A soft shadow in Shadow Color rather than black - a blue button casting a blue shadow."),
	Floating UMETA(ToolTip = "Hovering above the floor: a soft oval shadow detached below the widget, plus a faint one at its edge."),
	Contact UMETA(DisplayName = "Contact (Floor)", ToolTip = "Resting on the floor: a tight, dark oval right under the bottom edge, where it touches."),
	SoftUIRaised UMETA(DisplayName = "Soft UI Raised", ToolTip = "Neumorphism: a light shadow up and left, a dark one down and right - the widget pushed out of a mid-grey surface of its own color."),
	SoftUIPressed UMETA(DisplayName = "Soft UI Pressed", ToolTip = "Neumorphism, pressed in: the same two shadows inside the widget."),
	Inset UMETA(DisplayName = "Inner Shadow", ToolTip = "A soft inner shadow along the top edge: a recessed field or well."),
	Custom UMETA(ToolTip = "The Layers as set.")
};

/** Where a Drop Shadow's corners come from. */
UENUM(BlueprintType)
enum class EDropShadowCorners : uint8
{
	MatchWidget UMETA(DisplayName = "Match Widget", ToolTip = "Read from the widget: a Rectangle's or Glass Panel's rounding, an Ellipse (a pill), a rounded Image, Border or Button brush, a Background Blur's corners, a ready-made button's background. Square for anything else."),
	Custom UMETA(ToolTip = "Corner Radii as set here.")
};

/** Whether a Drop Shadow also draws under its widget. */
UENUM(BlueprintType)
enum class EDropShadowUnderWidget : uint8
{
	Auto UMETA(ToolTip = "Left clear under see-through widgets - a Glass Panel, a Background Blur, a Glass Button - and drawn under everything else."),
	Show UMETA(ToolTip = "Drawn under the widget too (it is hidden by an opaque widget anyway)."),
	Hide UMETA(ToolTip = "Never drawn under the widget: only what falls outside it shows, as CSS box-shadow - for see-through widgets.")
};

/** The shape a shadow is cast by: the widget's corners (a pill's are half its height, from its size). */
struct FDropShadowCaster
{
	FVector4f Radii = FVector4f(0.f, 0.f, 0.f, 0.f);
	bool bPill = false;
	bool bSeeThrough = false;
};

/**
 * The Slate side of a Drop Shadow: wraps the widget's own Slate widget, drawing the shadows under it (inner ones over it) from
 * the geometry it is laid out and transformed at, so they follow its render transform and opacity. Lifts it on hover.
 */
class NAMETBD_API SDropShadow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDropShadow) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetContent(const TSharedRef<SWidget>& InContent);
	void SetLayers(const TArray<FDropShadowLayer>& InLayers, float InOpacity, EDropShadowUnderWidget InUnder);

	/** Read every paint: the widget's corners can change (a Shape Target rounding them on hover). */
	void SetCasterSource(TFunction<FDropShadowCaster()> InSource);

	/** Hover response: elevation at full hover and while pressed, the lift (Slate units up), and how it moves. */
	void SetResponse(bool bInReact, float InHoverElevation, float InPressedElevation, float InHoverLift, EWidgetMotion InMotion, float InEnterDuration, float InExitDuration);

	/** Pressed, polled while hovered (a Button's). Hover itself is the pointer over the widget, or keyboard / gamepad focus in it. */
	void SetPressedSource(TFunction<bool()> InSource);

	void SetElevation(float InElevation, bool bAnimate);
	float GetElevation() const { return Elevation.Value; }

	/** Forces the hovered look on or off (a selected card); unset follows the pointer again. */
	void SetForcedHover(TOptional<bool> InForced);

	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent) override;

private:
	struct FLayerMesh
	{
		uint32 Hash = 0;
		bool bBuilt = false;
		FWidgetShapeMesh Mesh;
	};

	void BuildLayer(const FDropShadowLayer& Layer, float ElevationScale, const FVector2f& Size, const FVector4f& Corners, bool bHideUnder, float PixelScale, FWidgetShapeMesh& Out) const;
	float GetHoverGoal() const;
	void Wake();
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);
	float GetLift() const;

	TArray<FDropShadowLayer> Layers;
	float Opacity = 1.f;
	EDropShadowUnderWidget Under = EDropShadowUnderWidget::Auto;
	TFunction<FDropShadowCaster()> CasterSource;
	TFunction<bool()> PressedSource;

	bool bReact = false;
	float HoverElevation = 1.6f;
	float PressedElevation = 0.6f;
	float HoverLift = 0.f;
	EWidgetMotion Motion = EWidgetMotion::Smooth;
	float EnterDuration = 0.f;
	float ExitDuration = 0.f;
	TOptional<bool> ForcedHover;

	float BaseElevation = 1.f;
	FWidgetMotionValue Elevation;
	FWidgetMotionValue HoverAmount;
	TSharedPtr<FActiveTimerHandle> AnimateTimer;

	mutable TArray<FLayerMesh> Meshes;
};

/**
 * Drop Shadow: a UI Component that gives any widget a shadow - add it with the designer's Add Component button on the
 * widget's Details (or Add Component in Blueprint). From a faint hairline shadow to a long, layered, sleek one, colored
 * glows, hard retro offsets, an oval floor shadow under a floating card, soft-UI (neumorphic) light and dark pairs and inner
 * shadows. Every shadow is a true Gaussian blur of the widget's rounded shape (its corners read from the widget), drawn as
 * vector geometry - no texture, no material, any platform.
 *
 * It follows the widget's render transform and opacity, so it scales, turns and fades with a Widget Animation. React To Hover
 * raises it while hovered (and lowers it while pressed), lifting the widget a little - cards and buttons that rise to the
 * pointer.
 */
UCLASS(DefaultToInstanced, EditInlineNew, BlueprintType, meta = (DisplayName = "Drop Shadow"))
class NAMETBD_API UDropShadowComponent : public UUIComponent
{
	GENERATED_BODY()

public:
	UDropShadowComponent();

	/** A ready-made shadow: picking one sets the Layers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drop Shadow")
	EDropShadowPreset Preset = EDropShadowPreset::Soft;

	/** The color presets cast in: black for shadows, a color for Glow and Colored Shadow (and Soft UI's dark side). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drop Shadow")
	FLinearColor ShadowColor = FLinearColor::Black;

	/** The shadows, drawn in order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drop Shadow")
	TArray<FDropShadowLayer> Layers;

	/** Every layer's strength at once. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drop Shadow", meta = (ClampMin = "0", UIMax = "2"))
	float Opacity = 1.f;

	/** How high the widget sits: scales every Drop and Floor shadow's offset, blur and spread (2 is twice as high). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Drop Shadow", meta = (ClampMin = "0", UIMax = "4"))
	float Elevation = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape")
	EDropShadowCorners Corners = EDropShadowCorners::MatchWidget;

	/** X top left, Y top right, Z bottom right, W bottom left, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (EditCondition = "Corners == EDropShadowCorners::Custom && !bFullyRounded", EditConditionHides))
	FVector4 CornerRadii = FVector4(8.0, 8.0, 8.0, 8.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (DisplayName = "Fully Rounded", EditCondition = "Corners == EDropShadowCorners::Custom", EditConditionHides))
	bool bFullyRounded = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape")
	EDropShadowUnderWidget UnderWidget = EDropShadowUnderWidget::Auto;

	/** Rises while hovered (or focused), sinks while pressed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover")
	bool bReactToHover = false;

	/** Elevation while hovered, as a multiple of Elevation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (EditCondition = "bReactToHover", EditConditionHides, ClampMin = "0", UIMax = "4"))
	float HoverElevation = 1.8f;

	/** Elevation while pressed (a Button's press), as a multiple of Elevation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (EditCondition = "bReactToHover", EditConditionHides, ClampMin = "0", UIMax = "4"))
	float PressedElevation = 0.6f;

	/** How far the widget rises while hovered, in Slate units (it moves; its layout doesn't). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (EditCondition = "bReactToHover", EditConditionHides, ClampMin = "0", UIMax = "16", ForceUnits = "px"))
	float HoverLift = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (EditCondition = "bReactToHover", EditConditionHides))
	EWidgetMotion Motion = EWidgetMotion::Smooth;

	/** Seconds to rise. 0: Motion's own pace. Also how Set Elevation animates up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (EditCondition = "Motion != EWidgetMotion::Instant", ClampMin = "0", UIMax = "2", Units = "s"))
	float EnterDuration = 0.f;

	/** Seconds to settle back. 0: Motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (EditCondition = "Motion != EWidgetMotion::Instant", ClampMin = "0", UIMax = "2", Units = "s"))
	float ExitDuration = 0.f;

	/** The layers a preset casts, in InColor where it uses a color. */
	UFUNCTION(BlueprintPure, Category = "Drop Shadow")
	static TArray<FDropShadowLayer> MakePresetLayers(EDropShadowPreset InPreset, FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "Drop Shadow")
	void SetPreset(EDropShadowPreset InPreset);

	UFUNCTION(BlueprintCallable, Category = "Drop Shadow")
	void SetShadowColor(FLinearColor InColor);

	/** Sets every layer; the preset becomes Custom. */
	UFUNCTION(BlueprintCallable, Category = "Drop Shadow")
	void SetLayers(const TArray<FDropShadowLayer>& InLayers);

	UFUNCTION(BlueprintCallable, Category = "Drop Shadow")
	void SetOpacity(float InOpacity);

	/** Raises or lowers the widget (shadow offset, blur and spread scale with it), moving over Enter / Exit Duration when bAnimate. */
	UFUNCTION(BlueprintCallable, Category = "Drop Shadow")
	void SetElevation(float InElevation, bool bAnimate = true);

	/** Shows the hovered look whatever the pointer does (a selected card) - or, off, the resting look. */
	UFUNCTION(BlueprintCallable, Category = "Drop Shadow")
	void SetForcedHover(bool bHovered);

	/** Follows the pointer again after Set Forced Hover. */
	UFUNCTION(BlueprintCallable, Category = "Drop Shadow")
	void ClearForcedHover();

	/** The widget the shadow is on. */
	UFUNCTION(BlueprintPure, Category = "Drop Shadow")
	UWidget* GetShadowedWidget() const;

	/** The shape a widget casts: its corners as Match Widget reads them. */
	static FDropShadowCaster ReadCaster(const UWidget* Widget);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	// UUIComponent
	virtual TSharedRef<SWidget> RebuildWidgetWithContent(const TSharedRef<SWidget> OwnerContent) override;

private:
	void ApplyPreset();
	void Push();
	FDropShadowCaster ResolveCaster() const;

	TWeakPtr<SDropShadow> MyShadow;
};
