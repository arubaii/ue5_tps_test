// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Extensions/UIComponent.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/Interface.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"
#include "NameTBD/Widgets/WidgetEffectImage.h"
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
 *   Overlays - pictures of your own over (or behind) the widget: a texture, an SVG or a material - filling the widget or
 *              following the pointer - and a material is handed the pointer every frame (Pointer, PointerHover,
 *              PointerVelocity, PointerProximity, PointerPressed, EffectStrength, WidgetSize), so a designer can build any
 *              effect in a material and have it follow the cursor with no Blueprint.
 * Every effect says when it shows (Show When): its own trigger, always, while hovered, near the pointer, while pressed,
 * while focused - and only while a named condition holds, set from Blueprint or C++ (Set Effect Condition), answered by the
 * widget itself (the Pointer Effects Condition interface), or built in (On / Off - a Context Menu Button's toggle -, Usable,
 * Disabled). Looks of your own are kept as Pointer Effects Preset assets (Save As Preset).
 * And it hands the pointer on, so anything else can follow it with no Blueprint: Materials (the widget's Image, Border or
 * shape material - and its children's - get Pointer (U, V, hover, pressed), Pointer Hover and Pointer Velocity), Shapes (a
 * Radial Gradient's center follows the pointer), Widget Animations (a value set every frame), state machines (inputs set),
 * and Blueprint (On Pointer Effects Updated, Get Pointer State).
 *
 * The pointer is the mouse, a touch, or - on a Diegetic Screen in the world - that screen's pointer. Keyboard / gamepad
 * focus counts as hover (React To Focus), with the pointer in the middle (or where Set Virtual Pointer puts it - a stick) -
 * focus a click left behind does not (a clicked button keeps focus; it isn't hovered once the pointer leaves). A disabled
 * widget doesn't react (React While Disabled).
 * Everything is measured on the widget's laid-out box, before any transform, so a tilting card doesn't chase itself.
 */

/** What makes a Pointer Effect show. */
UENUM(BlueprintType)
enum class EPointerEffectTrigger : uint8
{
	Default UMETA(ToolTip = "The effect's own: Tilt and Overlays while hovered, Magnet as the pointer nears (its Range), Press while pressed, Ripple on each press, a Light as its Show says."),
	Always UMETA(ToolTip = "Always on, following the pointer wherever it is (resting in the middle without one)."),
	WhileHovered UMETA(DisplayName = "While Hovered", ToolTip = "While the pointer is over the widget (or navigation focus is on it, with React To Focus)."),
	NearPointer UMETA(DisplayName = "Near Pointer", ToolTip = "Fading in as the pointer comes within Proximity Range of the widget's edges."),
	WhilePressed UMETA(DisplayName = "While Pressed", ToolTip = "While a press that started on the widget is held."),
	WhileFocused UMETA(DisplayName = "While Focused", ToolTip = "While keyboard or gamepad navigation has focus on the widget (or inside it)."),
	ConditionOnly UMETA(DisplayName = "Condition Only", ToolTip = "Only while its Condition holds - nothing about the pointer.")
};

/**
 * When a Pointer Effect shows: its trigger, and - optionally - a named condition that must hold too. A condition is set from
 * Blueprint or C++ (Set Effect Condition), answered by the widget or the User Widget it is in (the Pointer Effects Condition
 * interface, asked every frame), or built in: On / Off (a Context Menu Button's Enabled toggle), Usable / Disabled (the
 * widget's Is Enabled).
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FPointerEffectCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Show When")
	EPointerEffectTrigger Trigger = EPointerEffectTrigger::Default;

	/** Also only while this named condition holds ("Selected", "Affordable", "On"...). None: no condition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Show When")
	FName Condition;

	/** While Condition does not hold, instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Show When", meta = (DisplayName = "Invert Condition"))
	bool bInvertCondition = false;
};

UINTERFACE(BlueprintType, meta = (DisplayName = "Pointer Effects Condition"))
class NAMETBD_API UPointerEffectsConditionSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Optional: implement it on a widget with Pointer Effects - or on the Widget Blueprint it is in (Class Settings > Interfaces) -
 * and its effects' named conditions are answered by it, every frame: "show the holographic foil only while this card is
 * owned", in the game's own code.
 */
class NAMETBD_API IPointerEffectsConditionSource
{
	GENERATED_BODY()

public:
	/** Whether Condition holds (in Met). Return false for a condition this widget doesn't answer - the next source is asked. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Pointer Effects")
	bool GetPointerEffectCondition(FName Condition, bool& bMet) const;
};

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

	/** When it shows (Default: while hovered). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tilt", meta = (EditCondition = "bEnabled"))
	FPointerEffectCondition ShowWhen;

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

	/** When it shows (Default: as the pointer nears). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Magnet", meta = (EditCondition = "bEnabled"))
	FPointerEffectCondition ShowWhen;

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

	/** When it shows (Default: while pressed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Press", meta = (EditCondition = "bEnabled"))
	FPointerEffectCondition ShowWhen;

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

	/** When it shows, beyond Show: another trigger, or a condition too (Default: as Show says). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FPointerEffectCondition ShowWhen;

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

	/** When it shows (Default: on each press). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ripple", meta = (EditCondition = "bEnabled"))
	FPointerEffectCondition ShowWhen;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ripple", meta = (EditCondition = "bEnabled"))
	FLinearColor Color = FLinearColor(1.f, 1.f, 1.f, 0.25f);

	/** Seconds it takes to spread over the widget and fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ripple", meta = (EditCondition = "bEnabled", ClampMin = "0.05", UIMax = "2", Units = "s"))
	float Duration = 0.6f;
};

/** Where a Pointer Effects overlay is drawn. */
UENUM(BlueprintType)
enum class EPointerOverlayPlacement : uint8
{
	Fill UMETA(ToolTip = "Over the whole widget (UVs 0..1 across it), cut to its corners - a foil, a frost, a scan line, anything a material draws."),
	FollowPointer UMETA(DisplayName = "Follow Pointer", ToolTip = "Size across, centered on the pointer, cut to the widget's corners - a custom spotlight, a lens, a cursor glow."),
	FollowPointerUncut UMETA(DisplayName = "Follow Pointer (Uncut)", ToolTip = "As Follow Pointer, spilling past the widget's edges - a halo round the cursor.")
};

/**
 * A picture of your own drawn with the widget, following the pointer or filling the widget: a texture, an SVG, or a material
 * that gets the pointer every frame as parameters - Pointer (U, V, hover, pressed; names as in Outputs), PointerHover,
 * PointerVelocity (x, y, speed, proximity), PointerProximity, PointerPressed, EffectStrength (how much it shows now, 0..1,
 * Intensity included) and WidgetSize (x, y in Slate units) - so any effect can be built in a material.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FPointerOverlay
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay", meta = (DisplayName = "Enabled"))
	bool bEnabled = true;

	/** When it shows (Default: while hovered). It fades in and out over Hover Fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	FPointerEffectCondition ShowWhen;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	FWidgetEffectImage Image;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	EPointerOverlayPlacement Placement = EPointerOverlayPlacement::Fill;

	/** Follow Pointer: how big it is, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay", meta = (EditCondition = "Placement != EPointerOverlayPlacement::Fill", EditConditionHides, ClampMin = "1", UIMax = "800", ForceUnits = "px"))
	float Size = 200.f;

	/** Its color over the picture; alpha is its strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay")
	FLinearColor Tint = FLinearColor::White;

	/** Drawn behind the widget's own content (over what is behind it) rather than over it - a glow under a card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Overlay", meta = (DisplayName = "Behind Widget"))
	bool bBehindContent = false;
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

/**
 * A ready-made set of Pointer Effects, named by what sets it off - the pointer hovering, or a click. Picking one sets the
 * effects; editing them makes it Custom. Quick starts: every effect can be set by hand, or kept as a Pointer Effects Preset
 * asset of your own (Preset Asset).
 */
UENUM(BlueprintType)
enum class EPointerEffectsPreset : uint8
{
	Custom UMETA(ToolTip = "The effects as set."),
	TiltCard UMETA(DisplayName = "Tilt Card Hover", ToolTip = "On hover: tilts away from the pointer with a soft spotlight under it - a card, a tile."),
	HolographicCard UMETA(DisplayName = "Holographic Card Hover", ToolTip = "On hover: a stronger tilt with a holographic foil and a spotlight - a rare trading card."),
	ShimmerButton UMETA(DisplayName = "Shimmer Hover", ToolTip = "On hover: a shine band following the pointer across it."),
	MagneticButton UMETA(DisplayName = "Magnetic Hover", ToolTip = "As the pointer nears: leans toward it, with a soft spotlight once over it."),
	SpotlightBorder UMETA(Hidden),
	Ripple UMETA(DisplayName = "Ripple Click", ToolTip = "On click: a ring of light spreading from where it was pressed, and a press."),
	Lift UMETA(Hidden)
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

/** Part of the pointer Pointer Effects see that a Widget Animation (the Pointer Effects track) or Blueprint can set. */
UENUM(BlueprintType)
enum class EPointerEffectsInput : uint8
{
	PointerX UMETA(DisplayName = "Pointer X", ToolTip = "Across the widget: 0 its left edge, 1 its right. Set, the effects see the pointer there, hovering; the other axis, unset, is the middle."),
	PointerY UMETA(DisplayName = "Pointer Y", ToolTip = "Down the widget: 0 its top edge, 1 its bottom."),
	Hover UMETA(ToolTip = "How hovered it is, 0 to 1, whatever the pointer does."),
	Press UMETA(ToolTip = "Pressed above 0.5; rising past 0.5 is a click - a ripple from the pointer.")
};

/** Where Pointer Effects read the pointer. */
UENUM(BlueprintType)
enum class EPointerEffectsPointerSource : uint8
{
	Pointer UMETA(ToolTip = "The mouse, touch or a Diegetic Screen's pointer (and navigation focus, React To Focus)."),
	Widget UMETA(ToolTip = "Another widget's middle (Pointer Widget) - a Constraint Target whose Transform track is keyed: the effects follow its keys, its motion path showing the sweep. Over the widget when inside its box; near by its distance.")
};

/** Parts of the pointer set by an animation or Blueprint, over what is measured. */
struct FPointerEffectsAnimatedInputs
{
	TOptional<float> PointerX;
	TOptional<float> PointerY;
	TOptional<float> Hover;
	TOptional<float> Press;

	bool IsEmpty() const { return !PointerX.IsSet() && !PointerY.IsSet() && !Hover.IsSet() && !Press.IsSet(); }
	TOptional<float>& Get(EPointerEffectsInput Input);
	const TOptional<float>& Get(EPointerEffectsInput Input) const;
};

/** The Slate side's settings. */
struct FPointerEffectsSettings
{
	FPointerTiltEffect Tilt;
	FPointerMagnetEffect Magnet;
	FPointerPressEffect Press;
	TArray<FPointerLight> Lights;
	FPointerRippleEffect Ripple;
	TArray<FPointerOverlay> Overlays;
	float Intensity = 1.f;
	float ProximityRange = 120.f;
	float HoverFade = 0.2f;
	bool bReactToFocus = true;
	bool bReactWhileDisabled = false;
	FPointerEffectCondition ActiveWhen;
	bool bDesignTime = false;
	bool bPreviewInDesigner = true;
	bool bPreviewConditionsAsMet = true;
	FVector2D DesignerPointer = FVector2D(0.7, 0.3);

	/** The parameter names overlays' materials get the pointer in (Outputs'). */
	FName PointerParameter = TEXT("Pointer");
	FName HoverParameter = TEXT("PointerHover");
	FName VelocityParameter = TEXT("PointerVelocity");
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

	/** Answers a named condition (Show When's Condition). */
	void SetConditionSource(TFunction<bool(FName)> InSource) { ConditionSource = MoveTemp(InSource); }

	/** Takes over another host's motion (the widget rebuilt: its tilt, lean, hover carry on rather than snapping to rest). */
	void CopyMotionFrom(const SPointerEffectsHost& Other);

	/** How much an overlay shows now, 0..1 (eased; Intensity not included). */
	float GetOverlayStrength(int32 Index) const { return OverlayGates.IsValidIndex(Index) ? FMath::Clamp(OverlayGates[Index].Value, 0.f, 1.f) : 0.f; }

	/** How much a light shows now, 0..1 (eased; its Show included). */
	float GetLightStrength(int32 Index) const { return LightGates.IsValidIndex(Index) ? FMath::Clamp(LightGates[Index].Value, 0.f, 1.f) : 0.f; }

	/** A pointer of its own (0..1 across the widget) used instead of the real one - a stick tilting a focused card. Unset: back. */
	void SetVirtualPointer(TOptional<FVector2D> InUV);

	/** Parts of the pointer an animation sets (Pointer X / Y, Hover, Press), over whatever else it reads. */
	void SetAnimatedInputs(const FPointerEffectsAnimatedInputs& InInputs);

	/** The pointer read off another widget instead (Pointer Source: Widget): its middle, in desktop space. Unset: the real pointer. */
	void SetPointerWidget(TWeakObjectPtr<UWidget> InPointerWidget) { PointerWidget = InPointerWidget; Wake(); }

	/** Both, kept without stepping - for SetSettings, which steps, to follow. */
	void SetPointerInputs(TWeakObjectPtr<UWidget> InPointerWidget, const FPointerEffectsAnimatedInputs& InInputs) { PointerWidget = InPointerWidget; AnimatedInputs = InInputs; }

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

	/** A press caught as it happens (before the widget under it handles it), so a click quicker than a frame still presses and ripples. */
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	EActiveTimerReturnType OnActiveTick(double InCurrentTime, float InDeltaTime);
	void Step(float DeltaTime, bool bSnap);
	FShapeBoxShape GetTiltShape() const;
	void Wake();

	/** Whether a condition holds, asked once a step per name. */
	bool IsConditionMet(FName Condition, TMap<FName, bool>& Asked) const;

	/** How much an effect shows (0..1), its trigger read from what the pointer is doing; DefaultTrigger stands for Default. */
	float GateTarget(const FPointerEffectCondition& Condition, EPointerEffectTrigger DefaultTrigger, float Hover, float Near, bool bPressedNow, bool bFocused,
		TMap<FName, bool>& Asked) const;

	TWeakObjectPtr<UWidget> Owner;
	FPointerEffectsSettings Settings;
	TFunction<FVector4f(const FVector2f&)> CornerSource;
	TFunction<void(const FPointerEffectsState&)> OnUpdated;
	TFunction<bool(FName)> ConditionSource;
	TOptional<FVector2D> VirtualPointer;
	FPointerEffectsAnimatedInputs AnimatedInputs;
	TWeakObjectPtr<UWidget> PointerWidget;

	/** Animated Press was above 0.5 last step (rising past it is a click). */
	bool bWasAnimatedPress = false;

	/** Whether it steps on a timer at design time too: following a widget, which an animation may be moving. */
	bool StepsInDesigner() const { return PointerWidget.IsValid(); }

	TSharedPtr<SShapeBox> TiltBox;
	TSharedPtr<SPointerEffectsSurface> Surface;
	TSharedPtr<SPointerEffectsSurface> UnderSurface;
	TSharedPtr<FActiveTimerHandle> TickTimer;

	/** How much each effect shows, eased over Hover Fade (the magnet and press are eased by their springs). */
	FWidgetMotionValue TiltGate;
	FWidgetMotionValue ActiveGate;
	TArray<FWidgetMotionValue> LightGates;
	TArray<FWidgetMotionValue> OverlayGates;

	/** Hovered as last measured: kept while the pointer stays inside the laid-out box, so a tilted or sunk widget doesn't flicker at its edge. */
	bool bWasOver = false;

	/** A press caught by OnPreviewMouseButtonDown, not yet seen by a step; held a moment so the quickest click still shows. */
	bool bPressCaught = false;
	FVector2f CaughtPressLocal = FVector2f::ZeroVector;
	float PressHold = 0.f;

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
	SLATE_BEGIN_ARGS(SPointerEffectsSurface)
		: _Behind(false)
	{}
		/** Behind the widget's content: draws only the overlays set Behind Widget (no lights or ripples). */
		SLATE_ARGUMENT(bool, Behind)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SPointerEffectsHost>& InHost);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D::ZeroVector; }

private:
	/** The overlays on this side of the content. */
	int32 PaintOverlays(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
		const FVector2f& Size, const FVector4f& Corners, float Tolerance) const;

	TWeakPtr<SPointerEffectsHost> Host;
	bool bBehind = false;

	/** The widget's outline filled and as a rim, cut into a grid - built again only when its size or corners change. */
	mutable uint32 MeshHash = 0;
	mutable FWidgetShapeMesh FillMesh;
	mutable FWidgetShapeMesh RimMesh;
	mutable TArray<FVector2f> OutlinePoints;

	/** The overlays' materials (an instance per overlay) and SVG drawings. */
	mutable FWidgetEffectMaterials Materials;
};

/**
 * A Pointer Effects look of your own, kept as an asset and picked on any widget's Pointer Effects (Preset Asset): the tilt,
 * magnet, press, lights, ripple and overlays. Made from a widget with Save As Preset, or from the Content Browser
 * (Miscellaneous > Data Asset > Pointer Effects Preset).
 */
UCLASS(BlueprintType, meta = (DisplayName = "Pointer Effects Preset"))
class NAMETBD_API UPointerEffectsPresetAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FPointerTiltEffect Tilt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FPointerMagnetEffect Magnet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FPointerPressEffect Press;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TArray<FPointerLight> Lights;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	FPointerRippleEffect Ripple;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TArray<FPointerOverlay> Overlays;
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

	/** Ready-made effects, named for what sets them off (hover or click): picking one sets them - a quick start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer Effects")
	EPointerEffectsPreset Preset = EPointerEffectsPreset::TiltCard;

	/** Effects of your own kept as an asset (Save As Preset makes one): picking one sets them, as a Preset does. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer Effects")
	TObjectPtr<UPointerEffectsPresetAsset> PresetAsset = nullptr;

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

	/**
	 * Pictures of your own over (or behind) the widget - textures, SVGs, or materials handed the pointer every frame - filling it
	 * or following the pointer, each shown when it says.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effects")
	TArray<FPointerOverlay> Overlays;

	/** When any effect shows at all - every effect's own Show When applies within it (Default: always). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer Effects")
	FPointerEffectCondition ActiveWhen;

	/** Reacts while the widget is disabled (Is Enabled off) too. Off: a disabled widget lies still. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer")
	bool bReactWhileDisabled = false;

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

	/**
	 * Keyboard / gamepad navigation focus on it (or in it) counts as hover, the pointer in its middle (or Set Virtual
	 * Pointer's). The focus a mouse click leaves on a button doesn't.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer")
	bool bReactToFocus = true;

	/**
	 * Where the pointer is read: the real one, or another widget's middle - a Constraint Target keyed on its Transform track, so
	 * an animation sweeps the tilt and lights across with 2D keys, eased and shown on a motion path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer")
	EPointerEffectsPointerSource PointerSource = EPointerEffectsPointerSource::Pointer;

	/** The widget read as the pointer (Pointer Source: Widget) - always shown: picking one makes Pointer Source Widget. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pointer", meta = (GetOptions = "GetWidgetOptions"))
	FName PointerWidget;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outputs")
	FPointerEffectsOutputs Outputs;

	/** Shows the effects in the designer as if the pointer were at Designer Pointer, hovering. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Designer")
	bool bPreviewInDesigner = true;

	/** Where the designer's preview puts the pointer, 0..1 across and down the widget. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Designer", meta = (EditCondition = "bPreviewInDesigner", EditConditionHides))
	FVector2D DesignerPointer = FVector2D(0.7, 0.3);

	/** In the designer, every named condition counts as met (nothing sets them there), so effects that wait on one are seen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Designer", meta = (EditCondition = "bPreviewInDesigner", EditConditionHides))
	bool bPreviewConditionsAsMet = true;

	/** Every time what it measures changes: where the pointer is, how hovered, near and pressed. */
	UPROPERTY(BlueprintAssignable, Category = "Pointer Effects")
	FOnPointerEffectsUpdated OnPointerEffectsUpdated;

	UFUNCTION(BlueprintPure, Category = "Pointer Effects")
	FPointerEffectsState GetPointerState() const;

	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void SetIntensity(float InIntensity);

	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void ApplyPreset(EPointerEffectsPreset InPreset);

	/** Sets the effects from a Pointer Effects Preset asset; it becomes the Preset Asset. */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void ApplyPresetAsset(UPointerEffectsPresetAsset* InPresetAsset);

	/** Writes these effects into a Pointer Effects Preset asset. */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void CopyToPresetAsset(UPointerEffectsPresetAsset* Target) const;

	/** Editor: saves these effects as a new Pointer Effects Preset asset (the Save Asset As window), and picks it. */
	UFUNCTION(CallInEditor, Category = "Pointer Effects")
	void SaveAsPreset();

	/**
	 * Sets a named condition that effects' Show When can ask for ("Selected", "Owned", "Affordable") - from Blueprint or C++,
	 * whenever it changes. Wins over the widget's own answer (the Pointer Effects Condition interface) and the built-in ones.
	 */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void SetEffectCondition(FName Condition, bool bMet);

	/** Forgets a condition set with Set Effect Condition (the widget's answer, or a built-in one, is used again). */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void ClearEffectCondition(FName Condition);

	/** Whether a named condition holds now, from wherever it is answered. */
	UFUNCTION(BlueprintPure, Category = "Pointer Effects")
	bool IsEffectConditionMet(FName Condition) const;

	/** Puts the pointer at UV (0..1 across the widget) whatever the real one does - a gamepad stick tilting a focused card. */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void SetVirtualPointer(FVector2D UV);

	/** Back to the real pointer. */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void ClearVirtualPointer();

	/**
	 * Sets part of the pointer the effects see - Pointer X / Y (0..1 across the widget), Hover, Press - over whatever they read,
	 * until cleared: what the Pointer Effects track animates. An intro sweeping a card's sheen, a selection shown hovered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void SetAnimatedInput(EPointerEffectsInput Input, float Value);

	/** Back to what is measured for that part. */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void ClearAnimatedInput(EPointerEffectsInput Input);

	/** That part as set, or false while it isn't. */
	UFUNCTION(BlueprintPure, Category = "Pointer Effects")
	bool GetAnimatedInput(EPointerEffectsInput Input, float& OutValue) const;

	/** Reads the pointer off another widget (Pointer Source: Widget) - one made at runtime included. Null: back to Pointer Widget's name. */
	UFUNCTION(BlueprintCallable, Category = "Pointer Effects")
	void SetPointerSourceWidget(UWidget* InWidget);

	/** The widget names offered in its dropdowns. */
	UFUNCTION()
	TArray<FString> GetWidgetOptions() const;

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

	// UObject
	virtual void PostLoad() override;

protected:
	// UUIComponent
	virtual TSharedRef<SWidget> RebuildWidgetWithContent(const TSharedRef<SWidget> OwnerContent) override;
#if UE_VERSION_OLDER_THAN(5, 6, 0)
	virtual bool OnInitialize() override;
#else
	virtual void OnInitialize() override;
	virtual void OnPreConstruct(bool bIsDesignTime) override;
#endif

	// UObject
	virtual void BeginDestroy() override;

private:
	void Push();

	/** Registers it so Get Pointer Effects and the Pointer Effects track find it - from when it is initialized. */
	void Register();

	/** Conditions set from Blueprint or C++. */
	TMap<FName, bool> Conditions;
	void HandleUpdated(const FPointerEffectsState& InState);
	void CollectTargets();

	TWeakPtr<SPointerEffectsHost> MyHost;

	/** Parts of the pointer set by the Pointer Effects track or Blueprint (kept here so a rebuilt widget keeps them). */
	FPointerEffectsAnimatedInputs AnimatedInputs;

	/** Set Pointer Source Widget's widget, over Pointer Widget's name. */
	TWeakObjectPtr<UWidget> PointerWidgetOverride;

	/** The widget read as the pointer now, or null (Pointer Source: Pointer, or none found). */
	UWidget* ResolvePointerWidget() const;
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
	TArray<TWeakObjectPtr<UShapeWidget>> Shapes;
	bool bTargetsCollected = false;
	FPointerEffectsState LastSent;
	bool bHasSent = false;
};
