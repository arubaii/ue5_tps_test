// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeTypes.h"
#include "NameTBD/Widgets/WidgetMotion.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"

#include "GlassPanel.generated.h"

class SBackgroundBlur;
class SBox;
class SOverlay;
class UButton;

/**
 * A ready-made glass. Picking one sets the Glass values below (the panel's corners, padding and hover response are kept);
 * Custom leaves them as set, and editing any of them makes the glass Custom.
 */
UENUM(BlueprintType)
enum class EGlassPreset : uint8
{
	Frosted UMETA(ToolTip = "Glassmorphism on a light or colorful background: a bright, soft blur, white film fading from the light, fine grain, a lit hairline edge."),
	FrostedDark UMETA(DisplayName = "Frosted Dark", ToolTip = "The dark-mode frosted panel: deeper blur under a charcoal film, faint grain, a dim edge lit along its top."),
	Clear UMETA(ToolTip = "Thin clear glass: barely blurred, almost no film, a strong sheen and crisp lit edges on both sides."),
	Liquid UMETA(ToolTip = "Liquid glass: a light blur, a bright specular rim where the light hits and a softer one opposite, a glowing inner edge that reads as thickness, a wide sheen."),
	Acrylic UMETA(ToolTip = "Acrylic, as Windows draws it: a heavy blur under a dense tint and a little noise - the background only as color."),
	Smoked UMETA(ToolTip = "Smoked glass: darkened and lightly blurred, a faint sheen - for HUD panels over bright scenes."),
	Tinted UMETA(ToolTip = "Colored glass: the Tint's color (blue by default) over a frosted blur, lit edge and inner glow in the same color."),
	ProgressiveTop UMETA(DisplayName = "Progressive (Top)", ToolTip = "Variable blur: strongest at the top edge and fading out downward, with a darkening that fades with it - a header or status bar over scrolling content."),
	ProgressiveBottom UMETA(DisplayName = "Progressive (Bottom)", ToolTip = "Variable blur strongest at the bottom edge, fading out upward - a caption over a picture, a bottom sheet, a toolbar."),
	Custom UMETA(ToolTip = "The Glass values as set.")
};

/** How the blur is spread over the panel. */
UENUM(BlueprintType)
enum class EGlassBlurMode : uint8
{
	Uniform UMETA(ToolTip = "The same blur everywhere under the panel, its corners rounded with it."),
	Progressive UMETA(ToolTip = "Variable (progressive) blur: full at the Strong Edge and falling off across the panel - drawn as Fade Steps bands of falling strength.")
};

/** One edge of a panel. */
UENUM(BlueprintType)
enum class EGlassEdge : uint8
{
	Top,
	Bottom,
	Left,
	Right
};

/** What lights a Glass Panel up (its Hover Response). */
UENUM(BlueprintType)
enum class EGlassReaction : uint8
{
	None UMETA(DisplayName = "Nothing", ToolTip = "The glass stays as it is."),
	NearestButton UMETA(DisplayName = "Nearest Button", ToolTip = "The button the panel is in (the nearest Button above it): hovered, focused or pressed - a glass button made from a Button and a Glass Panel."),
	OwnHover UMETA(DisplayName = "Own Hover", ToolTip = "The pointer over the panel itself (it must be Visible to the pointer)."),
	Manual UMETA(DisplayName = "Blueprint", ToolTip = "Only Set Interaction State - and a Glass Button, which drives the glass it is made of.")
};

/**
 * What a glass is made of, drawn in this order over whatever is behind the panel: the blur, the tint, grain, a sheen from the
 * light, an inner glow, and a lit edge. The panel's content is drawn over all of it, so text stays crisp. The blur is Slate's
 * own background blur (any renderer and platform Slate blurs on); everything else is plain vector drawing.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FGlassLook
{
	GENERATED_BODY()

	/** The same blur everywhere, or a variable blur falling off from one edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur")
	EGlassBlurMode BlurMode = EGlassBlurMode::Uniform;

	/** How strongly what is behind is blurred (Slate's Blur Strength: a few pixels of spread per unit). 0 leaves it sharp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (ClampMin = "0", UIMin = "0", UIMax = "60"))
	float BlurStrength = 14.f;

	/** Progressive: the edge the blur is strongest at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (EditCondition = "BlurMode == EGlassBlurMode::Progressive", EditConditionHides))
	EGlassEdge StrongEdge = EGlassEdge::Top;

	/** Progressive: how far across the panel (0 at the Strong Edge, 1 at the far one) the blur stays full before it starts to fall off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (EditCondition = "BlurMode == EGlassBlurMode::Progressive", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float FadeStart = 0.25f;

	/** Progressive: how far across the panel the blur is gone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (EditCondition = "BlurMode == EGlassBlurMode::Progressive", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float FadeEnd = 1.f;

	/**
	 * Progressive: how many bands the fall-off is drawn in, each its own blur pass a little weaker than the last. More is smoother
	 * and costs more (each band blurs its own strip of the screen).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur", meta = (EditCondition = "BlurMode == EGlassBlurMode::Progressive", EditConditionHides, ClampMin = "2", ClampMax = "16"))
	int32 FadeSteps = 8;

	/**
	 * The film over the blur: a color (its alpha how much it covers), a gradient across the panel, or an image or SVG. Keep it
	 * light (10 to 30% for a frosted look) so what is behind still shows.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tint")
	FWidgetShapePaint Tint;

	/** Film grain over the glass, as frosted glass has: 0 none, 0.05 subtle, 0.15 strong. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grain", meta = (ClampMin = "0", ClampMax = "0.5", UIMax = "0.25"))
	float Grain = 0.05f;

	/** How big one speck of grain is, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grain", meta = (ClampMin = "0.25", ClampMax = "8", UIMax = "4", EditCondition = "Grain > 0"))
	float GrainSize = 1.f;

	/** Where the light comes from, in degrees: 0 from the top, 90 from the right, -45 from the top left. Lights the sheen and the edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (UIMin = "-180", UIMax = "180", ForceUnits = "Degrees"))
	float LightAngle = -45.f;

	/** A wash of light across the glass from the lit side: how bright at its brightest (0 none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0", ClampMax = "1"))
	float Sheen = 0.1f;

	/** How far across the panel the sheen reaches before it is gone, as a fraction of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (ClampMin = "0.05", ClampMax = "1", EditCondition = "Sheen > 0"))
	float SheenReach = 0.6f;

	/** The edge's width, in Slate units, inside the panel. 0: no edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge", meta = (ClampMin = "0", ClampMax = "8", UIMax = "4", ForceUnits = "px"))
	float EdgeWidth = 1.f;

	/** The edge all the way round (its alpha how strong). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge", meta = (EditCondition = "EdgeWidth > 0"))
	FLinearColor EdgeColor = FLinearColor(1.f, 1.f, 1.f, 0.18f);

	/** Added to the edge where it faces the light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge", meta = (EditCondition = "EdgeWidth > 0"))
	FLinearColor EdgeHighlight = FLinearColor(1.f, 1.f, 1.f, 0.5f);

	/** Added to the edge facing away from the light - the far side of thick glass catching it (liquid glass). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge", meta = (EditCondition = "EdgeWidth > 0"))
	FLinearColor EdgeBacklight = FLinearColor(1.f, 1.f, 1.f, 0.f);

	/** How tightly the highlight and backlight gather where they face the light: 1 broad, 4 a narrow glint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Edge", meta = (ClampMin = "0.5", ClampMax = "8", EditCondition = "EdgeWidth > 0"))
	float EdgeFocus = 1.5f;

	/** A soft glow inside the edge, as thick glass shows: how strong (0 none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow", meta = (ClampMin = "0", ClampMax = "1"))
	float InnerGlow = 0.f;

	/** How far in the glow reaches, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow", meta = (ClampMin = "1", UIMax = "48", ForceUnits = "px", EditCondition = "InnerGlow > 0"))
	float InnerGlowSize = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inner Glow", meta = (EditCondition = "InnerGlow > 0"))
	FLinearColor InnerGlowColor = FLinearColor::White;

	/** Drawn instead of the blur where it is turned off: low scalability, or a platform without it. Rounded like the panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blur")
	FLinearColor FallbackColor = FLinearColor(0.1f, 0.11f, 0.14f, 0.85f);

	FGlassLook();
};

/** How a Glass Panel lights up while its button (or it) is hovered or pressed. Added on top of the Glass at full hover. */
USTRUCT(BlueprintType)
struct NAMETBD_API FGlassHoverResponse
{
	GENERATED_BODY()

	/** Added to Blur Strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover", meta = (UIMin = "-20", UIMax = "20"))
	float BlurBoost = 4.f;

	/** A white wash over the glass (alpha). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover", meta = (ClampMin = "0", ClampMax = "1"))
	float Brighten = 0.06f;

	/** Added to the edge highlight's alpha. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover", meta = (ClampMin = "0", ClampMax = "1"))
	float EdgeBoost = 0.3f;

	/** Added to Sheen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover", meta = (ClampMin = "0", ClampMax = "1"))
	float SheenBoost = 0.06f;

	/** A dark wash while pressed (alpha). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover", meta = (ClampMin = "0", ClampMax = "1"))
	float PressDarken = 0.08f;
};

/** The glass's decoration (everything but the blur and the content), painted between them. */
class NAMETBD_API SGlassSurface : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SGlassSurface) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetLook(const FGlassLook& InLook, const FVector4f& InRadii, bool bInPill);
	void SetResponse(const FGlassHoverResponse& InResponse) { Response = InResponse; }
	void SetAmounts(float InHover, float InPress);

	/** The corner radii at Size: the pill's when fully rounded. */
	FVector4f ResolveRadii(const FVector2f& Size) const;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void Rebuild(const FVector2f& Size, float PixelScale) const;

	FGlassLook Look;
	FGlassHoverResponse Response;
	FVector4f Radii = FVector4f(16.f, 16.f, 16.f, 16.f);
	bool bPill = false;
	float Hover = 0.f;
	float Press = 0.f;

	mutable uint32 BuiltHash = 0;
	mutable bool bBuilt = false;
	mutable FWidgetShapeMesh TintMesh;
	mutable FWidgetShapeMesh WashMesh;
	mutable FWidgetShapeMesh SheenMesh;
	mutable FWidgetShapeMesh GrainMesh;
	mutable FWidgetShapeMesh EdgeMesh;
	mutable FWidgetShapeMesh GlowMesh;
	mutable FWidgetSVGBrush TintSVG;
};

/**
 * The Slate side of a Glass Panel: the blur (one Background Blur, or a band of them for a progressive blur), the glass's
 * decoration, then the content - each over the last.
 */
class NAMETBD_API SGlassPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGlassPanel) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetContent(const TSharedRef<SWidget>& InContent);
	void SetContentLayout(const FMargin& InPadding, EHorizontalAlignment InHAlign, EVerticalAlignment InVAlign);
	void SetLook(const FGlassLook& InLook, const FVector4f& InRadii, bool bInPill);

	/** How it lights up and how fast. */
	void SetResponse(const FGlassHoverResponse& InResponse, EWidgetMotion InMotion, float InEnterDuration, float InExitDuration);

	/** Polled (a few times a second, every frame while it changes) for hovered and pressed. Unbound: only SetInteractionState. */
	void SetStateSource(TFunction<void(bool& bOutHovered, bool& bOutPressed)> InSource);

	/** Hovered and pressed, from outside (a Glass Button). */
	void SetInteractionState(bool bHovered, bool bPressed, bool bInstant);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	struct FBlurBand
	{
		TSharedPtr<SBackgroundBlur> Blur;
		float Weight = 1.f;
		bool bTouchesStart = false;
		bool bTouchesEnd = false;
	};

	void RebuildBlur();
	void UpdateBlurStrength();
	FVector4 GetBandRadius(int32 BandIndex) const;
	uint32 GetBlurLayoutHash() const;

	EActiveTimerReturnType Poll(double InCurrentTime, float InDeltaTime);
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);
	void StartAnimating();

	TSharedPtr<SOverlay> Layers;
	TSharedPtr<SBox> BlurHost;
	TSharedPtr<SGlassSurface> Surface;
	TSharedPtr<SBox> ContentBox;
	TArray<FBlurBand> Bands;
	FSlateBrush FallbackBrush;

	FGlassLook Look;
	FVector4f Radii = FVector4f(16.f, 16.f, 16.f, 16.f);
	bool bPill = false;
	uint32 BlurLayoutHash = 0;
	mutable FVector2f LastSize = FVector2f::ZeroVector;

	FGlassHoverResponse Response;
	EWidgetMotion Motion = EWidgetMotion::Smooth;
	float EnterDuration = 0.f;
	float ExitDuration = 0.f;
	TFunction<void(bool&, bool&)> StateSource;
	FWidgetMotionValue HoverAmount;
	FWidgetMotionValue PressAmount;
	TSharedPtr<FActiveTimerHandle> PollTimer;
	TSharedPtr<FActiveTimerHandle> AnimateTimer;
};

/**
 * Glass: a panel that blurs whatever is behind it and draws a pane of glass over it - tint, grain, sheen, lit edges, inner glow
 * - with its content on top. Uniform or progressive (variable) blur; presets from frosted glassmorphism and acrylic to liquid
 * glass and a fading header blur. It can light up while the button it is in is hovered or pressed, so a Button holding a
 * Glass Panel is a glass button (or use the ready-made Glass Button).
 *
 * The blur is Slate's own Background Blur, so it works wherever Background Blur does - every renderer and platform Slate
 * blurs on; with blur turned off (low scalability) the Fallback Color shows instead. Everything else is vector drawing.
 * Add a Drop Shadow component to give it a shadow: it leaves the area under the glass clear.
 */
UCLASS(meta = (DisplayName = "Glass Panel"))
class NAMETBD_API UGlassPanel : public UContentWidget
{
	GENERATED_BODY()

public:
	UGlassPanel(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** A ready-made glass: picking one sets the Glass values. Editing one makes it Custom. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass")
	EGlassPreset Preset = EGlassPreset::Frosted;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Glass")
	FGlassLook Glass;

	/** Rounds every corner, in Slate units, held to what fits. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (EditCondition = "!bFullyRounded && !bIndividualCorners", ClampMin = "0", UIMax = "64", ForceUnits = "px"))
	float CornerRadius = 16.f;

	/** Ends as round as they go: a pill, or a circle in a square panel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (DisplayName = "Fully Rounded"))
	bool bFullyRounded = false;

	/** Each corner its own rounding: X top left, Y top right, Z bottom right, W bottom left. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (EditCondition = "!bFullyRounded"))
	bool bIndividualCorners = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (EditCondition = "!bFullyRounded && bIndividualCorners", EditConditionHides))
	FVector4 CornerRadii = FVector4(16.0, 16.0, 16.0, 16.0);

	/** Space between the glass's edge and its content. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	FMargin ContentPadding = FMargin(16.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TEnumAsByte<EHorizontalAlignment> HorizontalAlignment = HAlign_Fill;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TEnumAsByte<EVerticalAlignment> VerticalAlignment = VAlign_Fill;

	/** What lights the glass up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover")
	EGlassReaction Reaction = EGlassReaction::NearestButton;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (EditCondition = "Reaction != EGlassReaction::None", EditConditionHides, ShowOnlyInnerProperties))
	FGlassHoverResponse HoverResponse;

	/** How it lights up and dims again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (EditCondition = "Reaction != EGlassReaction::None", EditConditionHides))
	EWidgetMotion Motion = EWidgetMotion::Smooth;

	/** Seconds to light up (hovered, pressed). 0: Motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (EditCondition = "Reaction != EGlassReaction::None && Motion != EWidgetMotion::Instant", EditConditionHides, ClampMin = "0", UIMax = "2", Units = "s"))
	float EnterDuration = 0.f;

	/** Seconds to dim again. 0: Motion's own pace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hover", meta = (EditCondition = "Reaction != EGlassReaction::None && Motion != EWidgetMotion::Instant", EditConditionHides, ClampMin = "0", UIMax = "2", Units = "s"))
	float ExitDuration = 0.f;

	/** The values a preset gives the glass. */
	UFUNCTION(BlueprintPure, Category = "Glass Panel")
	static FGlassLook MakePresetLook(EGlassPreset InPreset);

	/** Picks a ready-made glass (Custom keeps the values as they are). */
	UFUNCTION(BlueprintCallable, Category = "Glass Panel")
	void SetPreset(EGlassPreset InPreset);

	/** Sets every Glass value at once; the preset becomes Custom. */
	UFUNCTION(BlueprintCallable, Category = "Glass Panel")
	void SetLook(const FGlassLook& InLook);

	UFUNCTION(BlueprintPure, Category = "Glass Panel")
	FGlassLook GetLook() const { return Glass; }

	UFUNCTION(BlueprintCallable, Category = "Glass Panel")
	void SetBlurStrength(float InStrength);

	/** The tint's color (its alpha how much it covers). */
	UFUNCTION(BlueprintCallable, Category = "Glass Panel")
	void SetTintColor(FLinearColor InColor);

	/** The same rounding on every corner. */
	UFUNCTION(BlueprintCallable, Category = "Glass Panel")
	void SetCornerRadius(float InRadius);

	/** Lights the glass up as if hovered / pressed (Reaction Blueprint; any reaction takes it over again at its next change). */
	UFUNCTION(BlueprintCallable, Category = "Glass Panel")
	void SetInteractionState(bool bHovered, bool bPressed, bool bInstant = false);

	/** The corners as drawn at Size (a pill's are half its height). */
	FVector4f GetCornerRadiiAt(const FVector2f& Size) const;

	/** Corner radii as set (X top left, Y top right, Z bottom right, W bottom left), ignoring Fully Rounded. */
	FVector4f GetCornerRadiiSetting() const;

	// UWidget
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void OnSlotAdded(UPanelSlot* InSlot) override;
	virtual void OnSlotRemoved(UPanelSlot* InSlot) override;

private:
	void PushLook();
	void PushReaction();

	/** The nearest Button above this panel. */
	UButton* FindButton() const;

	TSharedPtr<SGlassPanel> MyGlass;
};
