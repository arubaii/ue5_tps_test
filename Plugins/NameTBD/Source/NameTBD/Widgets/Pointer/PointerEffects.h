// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Extensions/UIComponent.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"
#include "NameTBD/Widgets/WidgetMotion.h"
#include "NameTBD/Widgets/ShapeBox.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"

#include "PointerEffects.generated.h"

class UMaterialInstanceDynamic;
class UShapeWidget;
class UWidget;

/*
 * Pointer Effects: what a widget does as the pointer moves over and near it - the interaction polish of a production UI,
 * on any widget, as a UI Component (Add Component on its Details):
 *   Tilt     - turns toward / away from the pointer in perspective (a trading card, a hero tile), lifting a little.
 *   Magnet   - leans toward the pointer as it nears (a magnetic button), springing back as it leaves.
 *   Press    - sinks while pressed and springs back.
 *   Lights   - drawn over the widget, shaped to its corners, following the pointer: a Spotlight (soft glow under the cursor),
 *              a Sheen (a shimmer band through the cursor - the shine on a button), a Border Glow (the rim lit nearest the
 *              cursor, even before it is reached - a grid of cards lighting up as it passes), a Holographic foil (colors
 *              shifting with the pointer's angle).
 *   Ripple   - a ring of light spreading from where it was pressed.
 * And it hands the pointer on, so anything else can follow it with no Blueprint: Materials (the widget's Image, Border or
 * shape material - and its children's - get Pointer (U, V, hover, pressed), Pointer Hover and Pointer Velocity), Shapes (a
 * Radial Gradient's center follows the pointer), Widget Animations (a value set every frame), state machines (inputs set),
 * and Blueprint (On Pointer Effects Updated, Get Pointer State).
 *
 * The pointer is the mouse, a touch, or - on a Diegetic Screen in the world - that screen's pointer. Keyboard / gamepad
 * focus counts as hover (React To Focus), with the pointer in the middle (or where Set Virtual Pointer puts it - a stick).
 * Everything is measured on the widget's laid-out box, before any transform, so a tilting card doesn't chase itself.
 */

/** Which way a Tilt turns. */
UENUM(BlueprintType)
enum class EPointerTiltDirection : uint8
{
	PressAway UMETA(DisplayName = "Press Away", ToolTip = "The part under the pointer goes away from you, as if pressed - a card resting on a soft surface."),
	RiseToward UMETA(DisplayName = "Rise Toward", ToolTip = "The part under the pointer comes toward you - a card leaning in to look at it.")
};

/** A tilt toward the pointer, in perspective. */
USTRUCT(BlueprintType)
struct NAMETBD_API FPointerTiltEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt", meta = (DisplayName = "Enabled"))
	bool bEnabled = false;

	/** How far it turns with the pointer at its edge, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt", meta = (EditCondition = "bEnabled", ClampMin = "0", ClampMax = "45", ForceUnits = "Degrees"))
	float MaxAngle = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt", meta = (EditCondition = "bEnabled"))
	EPointerTiltDirection Direction = EPointerTiltDirection::PressAway;

	/** How strongly the turn is foreshortened: 0 flat, 1 strong. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt", meta = (EditCondition = "bEnabled", ClampMin = "0", ClampMax = "1"))
	float Perspective = 0.4f;

	/** Scale while hovered (1.03 lifts it 3%). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt", meta = (EditCondition = "bEnabled", ClampMin = "0.5", ClampMax = "1.5"))
	float HoverScale = 1.03f;

	/** How it follows the pointer and settles back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt", meta = (EditCondition = "bEnabled"))
	FWidgetSpringParams Spring;

	FPointerTiltEffect();
};

/** A lean toward the pointer. */
USTRUCT(BlueprintType)
struct NAMETBD_API FPointerMagnetEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnet", meta = (DisplayName = "Enabled"))
	bool bEnabled = false;

	/** How much of the pointer's distance from the middle it moves: 0.3 leans a third of the way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnet", meta = (EditCondition = "bEnabled", ClampMin = "0", ClampMax = "1"))
	float Pull = 0.3f;

	/** The furthest it moves, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnet", meta = (EditCondition = "bEnabled", ClampMin = "0", UIMax = "64", ForceUnits = "px"))
	float MaxOffset = 10.f;

	/** It starts leaning when the pointer is this close to its edges, in Slate units (0: only once over it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnet", meta = (EditCondition = "bEnabled", ClampMin = "0", UIMax = "200", ForceUnits = "px"))
	float Range = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnet", meta = (EditCondition = "bEnabled"))
	FWidgetSpringParams Spring;

	FPointerMagnetEffect();
};

/** A sink while pressed. */
USTRUCT(BlueprintType)
struct NAMETBD_API FPointerPressEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Press", meta = (DisplayName = "Enabled"))
	bool bEnabled = false;

	/** Scale while pressed (0.96 sinks it 4%). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Press", meta = (EditCondition = "bEnabled", ClampMin = "0.5", ClampMax = "1.5"))
	float PressScale = 0.96f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Press", meta = (EditCondition = "bEnabled"))
	FWidgetSpringParams Spring;

	FPointerPressEffect();
};

/** What a pointer light draws. */
UENUM(BlueprintType)
enum class EPointerLightKind : uint8
{
	Spotlight UMETA(ToolTip = "A soft glow under the pointer, Radius across."),
	Sheen UMETA(ToolTip = "A shimmer: a band of light through the pointer at Sheen Angle, Sheen Width wide - the shine sliding over a button."),
	BorderGlow UMETA(DisplayName = "Border Glow", ToolTip = "The widget's rim lit nearest the pointer, Border Width wide, fading over Radius - lights up as the pointer nears, before it is over."),
	Holographic UMETA(ToolTip = "A holographic foil: bands of color shifting across the widget as the pointer moves over it, strongest round the pointer (Radius).")
};

/** When a pointer light shows. */
UENUM(BlueprintType)
enum class EPointerLightShow : uint8
{
	WhileHovered UMETA(DisplayName = "While Hovered", ToolTip = "Fades in while the pointer is over the widget (or it has focus)."),
	NearPointer UMETA(DisplayName = "Near Pointer", ToolTip = "Fades in as the pointer comes within Proximity Range of the widget's edges."),
	Always UMETA(ToolTip = "Always, where the pointer last was.")
};

/** One light following the pointer over a widget. */
USTRUCT(BlueprintType)
struct NAMETBD_API FPointerLight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (DisplayName = "Enabled"))
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	EPointerLightKind Kind = EPointerLightKind::Spotlight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	EPointerLightShow Show = EPointerLightShow::WhileHovered;

	/** Its color; alpha is its strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FLinearColor Color = FLinearColor(1.f, 1.f, 1.f, 0.22f);

	/** How far it spreads from the pointer, in Slate units (Spotlight, Border Glow, Holographic). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (EditCondition = "Kind != EPointerLightKind::Sheen", EditConditionHides, ClampMin = "4", UIMax = "600", ForceUnits = "px"))
	float Radius = 160.f;

	/** Sheen: the band's slant, degrees from upright. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (EditCondition = "Kind == EPointerLightKind::Sheen", EditConditionHides, UIMin = "-80", UIMax = "80", ForceUnits = "Degrees"))
	float SheenAngle = 25.f;

	/** Sheen: how wide the band is, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (EditCondition = "Kind == EPointerLightKind::Sheen", EditConditionHides, ClampMin = "2", UIMax = "300", ForceUnits = "px"))
	float SheenWidth = 70.f;

	/** Border Glow: how wide the lit rim is, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light", meta = (EditCondition = "Kind == EPointerLightKind::BorderGlow", EditConditionHides, ClampMin = "0.5", UIMax = "8", ForceUnits = "px"))
	float BorderWidth = 1.5f;
};

/** A ring of light from a press. */
USTRUCT(BlueprintType)
struct NAMETBD_API FPointerRippleEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ripple", meta = (DisplayName = "Enabled"))
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ripple", meta = (EditCondition = "bEnabled"))
	FLinearColor Color = FLinearColor(1.f, 1.f, 1.f, 0.25f);

	/** Seconds it takes to spread over the widget and fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ripple", meta = (EditCondition = "bEnabled", ClampMin = "0.05", UIMax = "2", Units = "s"))
	float Duration = 0.6f;
};

/** What the pointer is handed on to. */
USTRUCT(BlueprintType)
struct NAMETBD_API FPointerEffectsOutputs
{
	GENERATED_BODY()

	/**
	 * Sets parameters on the widget's material (an Image's or Border's brush, a shape painted with a material) - and its
	 * children's with Include Children: Pointer Parameter (vector: U, V, hover, pressed), Hover Parameter (scalar), Velocity
	 * Parameter (vector: X, Y, speed in Slate units a second, proximity). A material without them is left alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	bool bDriveMaterials = false;

	/** Also the widgets inside it (through its children and User Widgets, a few levels deep). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	bool bIncludeChildren = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs", meta = (EditCondition = "bDriveMaterials", EditConditionHides))
	FName PointerParameter = TEXT("Pointer");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs", meta = (EditCondition = "bDriveMaterials", EditConditionHides))
	FName HoverParameter = TEXT("PointerHover");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs", meta = (EditCondition = "bDriveMaterials", EditConditionHides))
	FName VelocityParameter = TEXT("PointerVelocity");

	/** Shapes (the widget, and its children with Include Children) filled or stroked with a Radial Gradient have its center follow the pointer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	bool bDriveShapeGradients = false;

	/** A Widget Animation value (Vector 2D, -1..1 across the widget) set to the pointer every frame - for value tracks. None: off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	FName AnimationPointerValue;

	/** A Widget Animation value (0..1) set to how hovered it is. None: off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	FName AnimationHoverValue;

	/** A state machine input (Vector 2D, -1..1) set to the pointer. None: off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	FName MachinePointerInput;

	/** A state machine Number input set to how hovered it is (0..1). None: off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	FName MachineHoverInput;

	/** A state machine Number input set to how near the pointer is (0..1 within Proximity Range). None: off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outputs")
	FName MachineProximityInput;
};

/** Where the pointer is over a widget, as Pointer Effects measures it. */
USTRUCT(BlueprintType)
struct NAMETBD_API FPointerEffectsState
{
	GENERATED_BODY()

	/** 0..1 across and down the widget (outside it, past 0 or 1). */
	UPROPERTY(BlueprintReadOnly, Category = "Pointer")
	FVector2D UV = FVector2D(0.5, 0.5);

	/** -1 at the left / top edge, 0 in the middle, 1 at the right / bottom. */
	UPROPERTY(BlueprintReadOnly, Category = "Pointer")
	FVector2D Centered = FVector2D::ZeroVector;

	/** How hovered (or focused) it is, eased 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Pointer")
	float Hover = 0.f;

	/** How near the pointer is, eased: 1 over it, 0 Proximity Range from its edges or further. */
	UPROPERTY(BlueprintReadOnly, Category = "Pointer")
	float Proximity = 0.f;

	/** How pressed it is, eased 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Pointer")
	float Pressed = 0.f;

	/** How fast the pointer moves, in Slate units a second. */
	UPROPERTY(BlueprintReadOnly, Category = "Pointer")
	FVector2D Velocity = FVector2D::ZeroVector;

	/** The pointer is over the widget now. */
	UPROPERTY(BlueprintReadOnly, Category = "Pointer")
	bool bOver = false;
};

/** A ready-made set of Pointer Effects. Picking one sets the effects; editing them makes it Custom. */
UENUM(BlueprintType)
enum class EPointerEffectsPreset : uint8
{
	Custom UMETA(ToolTip = "The effects as set."),
	TiltCard UMETA(DisplayName = "Tilt Card", ToolTip = "Tilts away from the pointer with a soft spotlight under it - a card, a tile."),
	HolographicCard UMETA(DisplayName = "Holographic Card", ToolTip = "A stronger tilt with a holographic foil and a spotlight - a rare trading card."),
	ShimmerButton UMETA(DisplayName = "Shimmer Button", ToolTip = "A shine band following the pointer across it, and a press."),
	MagneticButton UMETA(DisplayName = "Magnetic Button", ToolTip = "Leans toward the pointer as it nears, a spotlight, and a springy press."),
	SpotlightBorder UMETA(DisplayName = "Spotlight Border", ToolTip = "The rim lights up nearest the pointer as it passes near - grids of cards - with a faint spotlight inside."),
	Ripple UMETA(ToolTip = "A ring of light from each press, and a press."),
	Lift UMETA(ToolTip = "Grows a little on hover and sinks when pressed - no lights.")
};

/** A Pointer Effects value that can be animated (the Pointer Effects track) or set by name from Blueprint. */
UENUM(BlueprintType)
enum class EPointerEffectsChannel : uint8
{
	Intensity UMETA(ToolTip = "Every effect's strength at once."),
	TiltAngle UMETA(DisplayName = "Tilt Angle", ToolTip = "Tilt: Max Angle, degrees."),
	MagnetPull UMETA(DisplayName = "Magnet Pull", ToolTip = "Magnet: Pull."),
	LightStrength UMETA(DisplayName = "Light Strength", ToolTip = "Every light's alpha, 0 to 1 (as a multiplier of the first light's).")
};

/** The Slate side's settings. */
struct FPointerEffectsSettings
{
	FPointerTiltEffect Tilt;
	FPointerMagnetEffect Magnet;
	FPointerPressEffect Press;
	TArray<FPointerLight> Lights;
	FPointerRippleEffect Ripple;
	float Intensity = 1.f;
	float ProximityRange = 120.f;
	float HoverFade = 0.2f;
	bool bReactToFocus = true;
	bool bDesignTime = false;
	bool bPreviewInDesigner = true;
	FVector2D DesignerPointer = FVector2D(0.7, 0.3);
};

class SPointerEffectsSurface;

/**
 * The Slate side of Pointer Effects: measures the pointer over the widget's laid-out box, springs the tilt, lean, lift and press,
 * and arranges the widget through a Shape Box (the tilt, drawn offscreen only while turned) with its lean and scale; a surface
 * over it draws the lights and ripples, turned with it.
 */
class NAMETBD_API SPointerEffectsHost : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPointerEffectsHost) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The widget it is on: its pointer (a Diegetic Screen's too) and what is over it are read through it. */
	void SetOwner(UWidget* InOwner) { Owner = InOwner; }

	void SetSettings(const FPointerEffectsSettings& InSettings);

	/** Read every paint for the lights' shape: the widget's corners (X top left, Y top right, Z bottom right, W bottom left) and pill. */
	void SetCornerSource(TFunction<FVector4f(const FVector2f&)> InSource) { CornerSource = MoveTemp(InSource); }

	/** Called whenever what it measures changes. */
	void SetOnUpdated(TFunction<void(const FPointerEffectsState&)> InOnUpdated) { OnUpdated = MoveTemp(InOnUpdated); }

	/** A pointer of its own (0..1 across the widget) used instead of the real one - a stick tilting a focused card. Unset: back. */
	void SetVirtualPointer(TOptional<FVector2D> InUV);

	const FPointerEffectsState& GetState() const { return State; }
	const FPointerEffectsSettings& GetSettings() const { return Settings; }
	FVector4f GetCorners(const FVector2f& Size) const;

	struct FRipple
	{
		FVector2f UV = FVector2f::ZeroVector;
		float Age = 0.f;
	};
	const TArray<FRipple>& GetRipples() const { return Ripples; }

	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;

private:
	EActiveTimerReturnType OnActiveTick(double InCurrentTime, float InDeltaTime);
	void Step(float DeltaTime, bool bSnap);
	FShapeBoxShape GetTiltShape() const;
	void Wake();

	TWeakObjectPtr<UWidget> Owner;
	FPointerEffectsSettings Settings;
	TFunction<FVector4f(const FVector2f&)> CornerSource;
	TFunction<void(const FPointerEffectsState&)> OnUpdated;
	TOptional<FVector2D> VirtualPointer;

	TSharedPtr<SShapeBox> TiltBox;
	TSharedPtr<SPointerEffectsSurface> Surface;
	TSharedPtr<FActiveTimerHandle> TickTimer;

	FPointerEffectsState State;
	FWidgetMotionValue HoverValue;
	FWidgetMotionValue ProximityValue;
	FWidgetSpringState TiltX;
	FWidgetSpringState TiltY;
	FWidgetSpringState LeanX;
	FWidgetSpringState LeanY;
	FWidgetSpringState ScaleSpring;
	FWidgetSpringState PressSpring;
	FVector2f LastLocal = FVector2f::ZeroVector;
	bool bHasLastLocal = false;
	bool bWasDown = false;
	bool bPressStartedOver = false;
	TArray<FRipple> Ripples;
};

/** Draws Pointer Effects' lights and ripples over the widget, inside its tilt. Takes no input. */
class NAMETBD_API SPointerEffectsSurface : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPointerEffectsSurface) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SPointerEffectsHost>& InHost);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D::ZeroVector; }

private:
	TWeakPtr<SPointerEffectsHost> Host;

	/** The widget's outline filled and as a rim, cut into a grid - built again only when its size or corners change. */
	mutable uint32 MeshHash = 0;
	mutable FWidgetShapeMesh FillMesh;
	mutable FWidgetShapeMesh RimMesh;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPointerEffectsUpdated, const FPointerEffectsState&, PointerState);

/**
 * Pointer Effects: a UI Component that makes any widget answer the pointer - tilt, magnetic lean, press, lights that follow the
 * cursor (spotlight, shimmer sheen, border glow, holographic foil), press ripples - and hands the pointer on to its materials,
 * shapes, Widget Animations and state machines. See the note at the top of PointerEffects.h.
 */
UCLASS(DefaultToInstanced, EditInlineNew, BlueprintType, meta = (DisplayName = "Pointer Effects"))
class NAMETBD_API UPointerEffectsComponent : public UUIComponent
{
	GENERATED_BODY()

public:
	UPointerEffectsComponent();

	/** Ready-made effects: picking one sets them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer Effects")
	EPointerEffectsPreset Preset = EPointerEffectsPreset::TiltCard;

	/** Every effect's strength at once (0 none, 1 as set) - animatable (Pointer Effects track). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer Effects", meta = (ClampMin = "0", UIMax = "2"))
	float Intensity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FPointerTiltEffect Tilt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FPointerMagnetEffect Magnet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FPointerPressEffect Press;

	/** Lights drawn over the widget following the pointer, in order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TArray<FPointerLight> Lights;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FPointerRippleEffect Ripple;

	/** The lights' corners: read from the widget (a Rectangle, a rounded Border or Button, a Glass Panel, a pill), or Corner Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (DisplayName = "Match Widget Corners"))
	bool bMatchWidgetCorners = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (EditCondition = "!bMatchWidgetCorners", EditConditionHides, ClampMin = "0", UIMax = "64", ForceUnits = "px"))
	float CornerRadius = 12.f;

	/** How far from its edges the pointer counts as near (Near Pointer lights, Proximity), in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer", meta = (ClampMin = "1", UIMax = "600", ForceUnits = "px"))
	float ProximityRange = 120.f;

	/** Seconds the hover takes to fade in and out (lights, outputs). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer", meta = (ClampMin = "0", UIMax = "1", Units = "s"))
	float HoverFade = 0.2f;

	/** Keyboard / gamepad focus on it (or in it) counts as hover, the pointer in its middle (or Set Virtual Pointer's). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer")
	bool bReactToFocus = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outputs", meta = (ShowOnlyInnerProperties))
	FPointerEffectsOutputs Outputs;

	/** Shows the effects in the designer as if the pointer were at Designer Pointer, hovering. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Designer")
	bool bPreviewInDesigner = true;

	/** Where the designer's preview puts the pointer, 0..1 across and down the widget. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Designer", meta = (EditCondition = "bPreviewInDesigner", EditConditionHides))
	FVector2D DesignerPointer = FVector2D(0.7, 0.3);

	/** Every time what it measures changes: where the pointer is, how hovered, near and pressed. */
	UPROPERTY(BlueprintAssignable, Category = "Pointer Effects")
	FOnPointerEffectsUpdated OnPointerEffectsUpdated;

	UFUNCTION(BlueprintPure, Category = "Pointer Effects")
	FPointerEffectsState GetPointerState() const;

	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void SetIntensity(float InIntensity);

	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void ApplyPreset(EPointerEffectsPreset InPreset);

	/** Puts the pointer at UV (0..1 across the widget) whatever the real one does - a gamepad stick tilting a focused card. */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void SetVirtualPointer(FVector2D UV);

	/** Back to the real pointer. */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void ClearVirtualPointer();

	/** Sets an animatable value. */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void SetEffectValue(EPointerEffectsChannel Channel, float Value);

	UFUNCTION(BlueprintPure, Category = "Pointer Effects")
	float GetEffectValue(EPointerEffectsChannel Channel) const;

	/** Finds the materials and shapes it drives again (after the widget's brushes or children changed). */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void RefreshTargets();

	UFUNCTION(BlueprintPure, Category = "Pointer Effects")
	UWidget* GetAffectedWidget() const;

	/** The Pointer Effects component on a widget, or null. */
	UFUNCTION(BlueprintPure, Category = "Pointer Effects", meta = (DisplayName = "Get Pointer Effects"))
	static UPointerEffectsComponent* FindPointerEffects(const UWidget* Widget);

	/** The effects a preset sets. */
	static void MakePreset(EPointerEffectsPreset InPreset, FPointerTiltEffect& OutTilt, FPointerMagnetEffect& OutMagnet, FPointerPressEffect& OutPress,
		TArray<FPointerLight>& OutLights, FPointerRippleEffect& OutRipple);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	// UUIComponent
	virtual TSharedRef<SWidget> RebuildWidgetWithContent(const TSharedRef<SWidget> OwnerContent) override;

	// UObject
	virtual void BeginDestroy() override;

private:
	void Push();
	void HandleUpdated(const FPointerEffectsState& InState);
	void CollectTargets();

	TWeakPtr<SPointerEffectsHost> MyHost;
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
	TArray<TWeakObjectPtr<UShapeWidget>> Shapes;
	bool bTargetsCollected = false;
	FPointerEffectsState LastSent;
	bool bHasSent = false;
};
