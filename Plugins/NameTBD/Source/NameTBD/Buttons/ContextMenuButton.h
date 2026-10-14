// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Components/PanelSlot.h"
#include "Engine/TimerHandle.h"
#include "Components/Widget.h"
#include "Slate/WidgetTransform.h"
#include "Widgets/Input/SButton.h"
#include "Sound/SoundBase.h"
#include "GameplayTagContainer.h"
#include "Components/TextBlock.h"
#include "NameTBD/Sound/UISoundTypes.h"
#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimDesignerPreview.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"
#include "NameTBD/Widgets/ShapeBox.h"
#include "NameTBD/Widgets/Shapes/ShapeWidget.h"
#include "NameTBD/Widgets/WidgetSVG.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"
#include "StructUtils/PropertyBag.h"

#include "ContextMenuButton.generated.h"

class UInfoCardWidget;
class UContextMenuButton;
class UButtonAction; 
class UContextButtonPayload;
class UNameTBD_Payload;
struct FContextMenuButtonData;
class UTexture2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class FWidgetPath;
class FWeakWidgetPath;
class UUserWidget;
class UShapeBox;
class SShapeBox;
class SContextMenuButtonBrush;
class SWidgetShape;
class UShapeWidget;
class UWidgetTree;

UENUM(BlueprintType)
enum class EContextMenuButtonType : uint8
{
	Press,
	Hold
};

UENUM(BlueprintType)
enum class EContextMenuButtonSettingSource : uint8
{
	UseProjectSettings UMETA(DisplayName = "Use Project Settings"),
	Custom
};

UENUM(BlueprintType)
enum class EExecutePressAction : uint8
{
	Immediate,
	OnRelease 
};

UENUM(BlueprintType)
enum class EExecuteHoldAction : uint8
{
	Immediate,
	AfterPressDuration
};

UENUM(BlueprintType)
enum class EContextMenuState : uint8
{
	Normal,
	Hovered,
	Pressed,
	Disabled UMETA(ToolTip = "The button's Is Enabled is false. Not the same as the Enabled state: disabling is explicit, and a disabled button's enabled state is also set to false."),
	Enabled UMETA(ToolTip = "The button's enabled state is true (requires Button Can Be Enabled). Not the same as Disabled: an enabled button that gets un-enabled simply returns to Normal.")
};

/**
 * Which state the button previews in the UMG designer. Editor only; ignored at runtime.
 */
UENUM(BlueprintType)
enum class EContextMenuDesignPreview : uint8
{
	Normal,
	Hovered,
	Pressed,
	Disabled,
	Enabled,
	None UMETA(ToolTip = "No preview."),
};

UENUM(BlueprintType)
enum class EContextMenuInterpolationCurve : uint8
{
	Linear,
	EaseIn,
	EaseOut,
	EaseInOut,
	Click UMETA(ToolTip = "Satisfying magnetic-click with overshoot toward the end.")
};

/** One named scalar parameter on the target widget's Dynamic Material Instance. */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuMaterialScalarValue
{
	GENERATED_BODY()

	/** Scalar parameter name. Auto-populated from the material. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Design")
	FName ParameterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	float Value = 0.0f;
};

/** One named vector parameter on the target widget's Dynamic Material Instance. */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuMaterialVectorValue
{
	GENERATED_BODY()

	/** Vector parameter name. Auto-populated from the material. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Design")
	FName ParameterName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	FLinearColor Value = FLinearColor::White;
};

/**
 * How one widget looks in one state.
 *
 * Every channel carries its own toggle, right next to the value it controls. A channel that's off is simply
 * not written for this state, and the widget shows whatever it was authored as - captured once, the first
 * time the button resolves its targets, and blended back to exactly as if it were any other value. So a
 * target can tint on hover and leave the resting color alone, without a second place to go and switch the
 * channel on first.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuWidgetDesign
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (InlineEditConditionToggle))
	bool bDriveTint = true;

	/**
	 * Multiplied against the widget's own pixels, not a replacement: a white or gray image can be tinted any color,
	 * a pure black one stays black. Export icons white with the shape in the alpha channel, or drive a material instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bDriveTint"))
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (InlineEditConditionToggle))
	bool bDriveOutlineTint = false;

	/**
	 * A brush's outline (rounded-box brushes with an outline width) is drawn in its own color rather than
	 * being multiplied by Tint, so a widget with no fill and just an outline - a hollow rounded border, say -
	 * doesn't move at all when Tint changes. Drive this instead to color that outline directly. Image, Border
	 * and (for the button's own native style) this button itself; ignored on anything else.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bDriveOutlineTint", DisplayName = "Outline Tint"))
	FLinearColor OutlineTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (InlineEditConditionToggle))
	bool bDriveOpacity = false;

	/** The widget's render opacity. Multiplies with its parents' opacity, as usual. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bDriveOpacity", ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (InlineEditConditionToggle))
	bool bDriveBrushTexture = false;

	/**
	 * Swaps the widget's image - a texture (Brush Texture) or an SVG (Brush SVG) - at the state change rather than blending.
	 * An SVG is drawn sharp at the widget's size on screen: on a shape (its Image paint), an SVG Image (its SVG), and on an
	 * Image, Border or Button (their brush, redrawn when the widget's size changes).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bDriveBrushTexture", DisplayName = "Brush Image Source"))
	EWidgetImageSource BrushImageSource = EWidgetImageSource::Texture;

	/** The texture for this state (Brush Image Source Texture 2D). Image, Border, Button and shapes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bDriveBrushTexture && BrushImageSource == EWidgetImageSource::Texture", EditConditionHides, DisplayName = "Brush Texture"))
	TObjectPtr<UTexture2D> BrushTexture = nullptr;

	/** The SVG for this state (Brush Image Source SVG): a play icon for Normal and a pause icon for Enabled, say. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bDriveBrushTexture && BrushImageSource == EWidgetImageSource::SVG", EditConditionHides, DisplayName = "Brush SVG"))
	TObjectPtr<UWidgetSVG> BrushSVG = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (InlineEditConditionToggle))
	bool bDriveMaterial = false;

	/** The widget's material for this state. Swaps exactly on state change rather than blending. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bDriveMaterial"))
	TObjectPtr<UMaterialInterface> Material = nullptr;

	/** Scalar parameters pushed into the widget's material. Auto-populated from the material; you can edit the values but not the list. */
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bDriveMaterial", TitleProperty = "ParameterName"))
	TArray<FContextMenuMaterialScalarValue> MaterialScalars;

	/** Vector parameters pushed into the widget's material. This is how a solid-black icon changes color: drive a Tint parameter here instead of the brush tint. */
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bDriveMaterial", TitleProperty = "ParameterName"))
	TArray<FContextMenuMaterialVectorValue> MaterialVectors;

	/**
	 * Legacy, hidden. Sound is no longer a design channel - it has its own Sound Sets on the button, which
	 * don't need a target widget. UContextMenuButton::PostLoad moves anything stored here into a Sound Set.
	 */
	UPROPERTY()
	TObjectPtr<USoundBase> Sound = nullptr;

	// Legacy, hidden, unused. These were details-panel mirrors of the owning target's channel switches, back
	// when the switches lived one level up and a state's fields were grayed out from there. The switches are
	// now the toggles above, so there is nothing left to mirror.

	UPROPERTY()
	bool bTintApplicable = true;

	UPROPERTY()
	bool bOpacityApplicable = false;

	UPROPERTY()
	bool bBrushApplicable = false;

	UPROPERTY()
	bool bMaterialApplicable = false;

	UPROPERTY()
	bool bSoundApplicable = false;

	bool DrivesAnything() const { return bDriveTint || bDriveOutlineTint || bDriveOpacity || bDriveBrushTexture || bDriveMaterial; }
};

/**
 * One widget restyled per button state. Replaces the old Color Target: as well as a tint it can drive
 * opacity, a brush texture swap, and a material + parameters.
 *
 * Only the channels switched on below are ever written, so a target never fights with anything else
 * setting the same property on that widget.
 *
 * States are authored the same way the button's own Shape is: every state falls back to Base Design until
 * its own override is switched on, so a target that looks the same in every state is one field to fill in
 * rather than five.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuDesignTarget
{
	GENERATED_BODY()

	/** Any widget in this button's tree, including the button itself. Does not need Is Variable. Stored by name, so renaming the widget breaks the link. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Target Widget", GetOptions = "GetColorTargetWidgetOptions"))
	FName TargetWidgetName;

	/**
	 * Optional name for changing this target at runtime: Set Target Variant, Set Target Active, Set Design Target By ID, Set
	 * Design Target Image. Several targets may share one (all are changed). Give the targets that take turns on one widget each
	 * their own ID - a volume icon's Full, Half and Muted - and Set Target Variant picks one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Target ID"))
	FName TargetId;

	/** Applied while on. Off: skipped, as if it weren't there (the widget shows what the other targets give it). Switched at runtime by Set Target Active / Set Target Variant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Active"))
	bool bActive = true;

	// ---- Source ----

	/** Use Project Settings: the design comes from a named Design Preset. Every channel is then the preset's, and the states below are hidden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Design Source"))
	EContextMenuButtonSettingSource DesignSource = EContextMenuButtonSettingSource::Custom;

	/** Design Preset from Project Settings > Game > Info Card. Falls back to the states below if the preset is renamed or deleted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::UseProjectSettings", EditConditionHides, GetOptions = "InfoCardDeveloperSettings.GetContextMenuDesignPresetNameOptions"))
	FName PresetName = TEXT("Default");

	// ---- Legacy channels ----
	// The channel switches used to live here, one level above the values they controlled, so switching a
	// channel on meant finding it up here and then scrolling back down to five separate places to fill it in.
	// Each state now carries its own toggle next to each value instead. Kept hidden so old assets still load;
	// UContextMenuButton::PostLoad pushes them down into the states once and then leaves them alone.

	UPROPERTY()
	bool bDriveTint = true;

	UPROPERTY()
	bool bDriveOpacity = false;

	UPROPERTY()
	bool bDriveBrush = false;

	UPROPERTY()
	bool bDriveMaterial = false;

	UPROPERTY()
	bool bDriveSound = false;

	// ---- State designs ----

	/**
	 * The design every state uses unless that state's own override below is switched on. Author here first;
	 * turn on a state's override only once that state actually needs to look different from the rest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	FContextMenuWidgetDesign BaseDesign;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	bool bOverrideNormalDesign = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::Custom && bOverrideNormalDesign", EditConditionHides))
	FContextMenuWidgetDesign Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	bool bOverrideHoveredDesign = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::Custom && bOverrideHoveredDesign", EditConditionHides))
	FContextMenuWidgetDesign Hovered;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	bool bOverridePressedDesign = false;

	/** Not used by buttons with Button Can Be Enabled: a press on those blends straight from the current state to the state the click produces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::Custom && bOverridePressedDesign", EditConditionHides))
	FContextMenuWidgetDesign Pressed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	bool bOverrideDisabledDesign = false;

	/** Used while the button's Is Enabled is false. Not the same as the Enabled state below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::Custom && bOverrideDisabledDesign", EditConditionHides))
	FContextMenuWidgetDesign Disabled;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::Custom && bEnabledDesignApplicable", EditConditionHides))
	bool bOverrideEnabledDesign = false;

	/** Used while the button's enabled state is true. Grayed out unless Button Can Be Enabled is on, and while Enabled Matches Hover Design is on (Hovered is used instead). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "DesignSource == EContextMenuButtonSettingSource::Custom && bEnabledDesignApplicable && bOverrideEnabledDesign", EditConditionHides))
	FContextMenuWidgetDesign Enabled;

	/** Details-panel mirror of the owning button; no runtime effect. */
	UPROPERTY()
	bool bEnabledDesignApplicable = true;

	/** How fast this target blends between states. 0 snaps instantly. Brush/Material swaps always snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float InterpolationSpeed = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (InlineEditConditionToggle))
	bool bSeparateExitSpeed = false;

	/** How fast it blends back to Normal (unhovered, switched off) - its exit. Unticked: Interpolation Speed both ways. 0 snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Exit Interpolation Speed", EditCondition = "bSeparateExitSpeed", ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float ExitInterpolationSpeed = 15.0f;

	/** The design for one state, honoring the button's Enabled Matches Hover Design. */
	const FContextMenuWidgetDesign& GetDesignForState(EContextMenuState State, bool bEnabledMatchesHover) const;

	/** True if any state drives any channel, i.e. this target does something at all. */
	bool DrivesAnything() const;

	// A channel is "owned" by this target if ANY state drives it. That is the level the question has to be
	// asked at: a state that doesn't drive an owned channel writes the widget's authored value back rather
	// than leaving the previous state's value sitting there, and a channel no state drives is never touched.
	bool DrivesTintAnywhere() const;
	bool DrivesOutlineTintAnywhere() const;
	bool DrivesOpacityAnywhere() const;
	bool DrivesBrushTextureAnywhere() const;
	bool DrivesMaterialAnywhere() const;
};



/**
 * Legacy struct from before Design Targets. Hidden; kept only so previously 
 * saved assets still load their colors for the PostLoad conversion pipeline.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuColorTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals", meta = (DisplayName = "Target Widget", GetOptions = "GetColorTargetWidgetOptions"))
	FName TargetWidgetName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals")
	EContextMenuButtonSettingSource ColorSource = EContextMenuButtonSettingSource::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals", meta = (EditCondition = "ColorSource == EContextMenuButtonSettingSource::UseProjectSettings", EditConditionHides, GetOptions = "InfoCardDeveloperSettings.GetContextMenuDesignPresetNameOptions"))
	FName PaletteName = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals", meta = (EditCondition = "ColorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	FLinearColor NormalColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals", meta = (EditCondition = "ColorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	FLinearColor HoverColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals", meta = (EditCondition = "ColorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	FLinearColor PressedColor = FLinearColor::Black;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals", meta = (EditCondition = "ColorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	FLinearColor DisabledColor = FLinearColor::Gray;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals", meta = (EditCondition = "ColorSource == EContextMenuButtonSettingSource::Custom && bEnabledColorApplicable", EditConditionHides))
	FLinearColor EnabledColor = FLinearColor::Black;

	UPROPERTY()
	bool bEnabledColorApplicable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals", meta = (EditCondition = "ColorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides, ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float ColorInterpolationSpeed = 15.0f;

	UPROPERTY()
	TObjectPtr<UWidget> TargetWidget = nullptr;
};

/**
 * What one Shape Target looks like in one state: a Transform, plus a whole Shape Box shape.
 *
 * Shape is an FShapeBoxShape stored whole - Extrude, Inset, Corners, Bend, Edge Bow, Angle, Rotation and
 * anything added to the Shape Box later. Nothing in the button names the individual properties: states blend
 * with FShapeBoxShape::Lerp, compare with Equals, and a placement overrides the struct as one piece, so a new
 * Shape Box property shows up here, animates between states and can be overridden without the button changing.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuShapeStateSettings
{
	GENERATED_BODY()

	/**
	 * Transforms this shape's target widget. When the target IS the button, that means the button's background
	 * brush: content and layout are not affected, but the mouse hit area follows the transform, pivoting on the
	 * middle of the shaped brush. A Shape Box target (or a widget sitting alone inside a Shape Box) has the
	 * transform put on the Shape Box; any other target is transformed through its own Render Transform.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "bIsApplicable", HideEditConditionToggle))
	FWidgetTransform Transform;

	/**
	 * The Shape Box shape for this state, applied before Transform.
	 *
	 * On the button itself it shapes the background brush: Extrude and Inset resize the brush (margins and corners
	 * kept, not stretched), and the mouse hit area follows the drawn shape, chamfered corners and all. Content and
	 * layout are not affected.
	 *
	 * On any other target it needs a Shape Box to act through: target the Shape Box itself, or a widget that sits
	 * alone inside one. Anything else only gets Transform - Shape Status on the button says which targets those are.
	 *
	 * Not shown for a vector shape (an Ellipse, Rectangle, Star... or the button's own Background Shape): those take Look.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "bIsApplicable && !bIsVectorShape", EditConditionHides))
	FShapeBoxShape Shape;

	/**
	 * How a vector shape looks in this state - an Ellipse, Rectangle, Triangle, Star, N-gon or Custom Shape target, or the
	 * button's own Background Shape: its fill, its outline (materials and all), Trim Path, rounding, points, sweep... every
	 * setting it has, laid out for that kind of shape and started from how it is authored.
	 *
	 * Only what differs from the shape as authored is ever written, and it blends at the Shape speed: a hover that only
	 * brightens the outline leaves the fill alone, and editing the shape itself still shows wherever no state changes it.
	 *
	 * Only a state its target overrides has a Look. The Base Shape has none: the base is the shape itself, as authored in its
	 * own Details (Background Look, for Self), and a state with no Look shows it. That keeps a target to one Look per state it
	 * actually changes - each Look is every setting of the shape, so there are never more of them than are used.
	 */
	UPROPERTY(EditAnywhere, Category = "Shape", meta = (FixedLayout, EditCondition = "bIsApplicable && bHasLook", EditConditionHides))
	FInstancedPropertyBag Look;

	/** Details-panel mirror: this state has a Look (a vector shape's state the target overrides). Written by the button. */
	UPROPERTY()
	bool bHasLook = false;

	/** Details-panel mirror: the target is a vector shape, so the Shape Box Shape is hidden. Written by the button. */
	UPROPERTY()
	bool bIsVectorShape = false;

	/**
	 * Details-panel only; no runtime effect. When false, the fields above are grayed out.
	 * Unreal can't hide a property on one condition and gray it out on another, so UContextMenuButton
	 * hides EnabledShape by Shape Source and mirrors "Button Can Be Enabled and not Enabled
	 * Matches Hover Shape" into this flag to gray it.
	 */
	UPROPERTY()
	bool bIsApplicable = true;
};

/**
 * Speed, curve, overshoot and delay for both channels, resolved through a preset and through Separate Transform And Shape.
 * The "Size" channel is the whole Shape Box shape (the name predates it).
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuResolvedInterpolation
{
	GENERATED_BODY()

	UPROPERTY()
	float TransformSpeed = 5.0f;

	UPROPERTY()
	EContextMenuInterpolationCurve TransformCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY()
	float TransformOvershoot = 1.5f;

	UPROPERTY()
	float TransformDelay = 0.0f;

	UPROPERTY()
	float SizeSpeed = 5.0f;

	UPROPERTY()
	EContextMenuInterpolationCurve SizeCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY()
	float SizeOvershoot = 1.5f;

	UPROPERTY()
	float SizeDelay = 0.0f;
};

/**
 * A named, project-wide set of state shapes, authored in Project Settings > Game > Info Card.
 * A Shape Target with Shape Source = Use Project Settings picks one of these by name.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuShapePreset
{
	GENERATED_BODY()

	/** Name shown in each button's Shape Preset dropdown. Keep unique; renaming breaks buttons pointed at the old name (they fall back to their own Custom shapes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	FName PresetName = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	FContextMenuShapeStateSettings BaseShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	bool bOverrideNormalShape = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (EditCondition = "bOverrideNormalShape", EditConditionHides))
	FContextMenuShapeStateSettings NormalShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	bool bOverrideHoveredShape = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (EditCondition = "bOverrideHoveredShape", EditConditionHides))
	FContextMenuShapeStateSettings HoveredShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	bool bOverridePressedShape = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (EditCondition = "bOverridePressedShape", EditConditionHides))
	FContextMenuShapeStateSettings PressedShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	bool bOverrideDisabledShape = false;

	/** Used while the button's Is Enabled is false. Disabled is not the same as the Enabled state: disabling is explicit, and a disabled button's enabled state is also set to false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (EditCondition = "bOverrideDisabledShape", EditConditionHides))
	FContextMenuShapeStateSettings DisabledShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	bool bOverrideEnabledShape = false;

	/**
	 * Used while the button's enabled state is true (buttons with Button Can Be Enabled only). Not the same as Disabled:
	 * a button can be enabled and un-enabled, and when un-enabled it simply returns to Normal.
	 * Hovered and Pressed still take priority while the pointer is over the button.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (EditCondition = "bOverrideEnabledShape", EditConditionHides))
	FContextMenuShapeStateSettings EnabledShape;

	/** Blend speed for Transform. 0 snaps instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float TransformInterpolationSpeed = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	EContextMenuInterpolationCurve TransformInterpolationCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (EditCondition = "TransformInterpolationCurve == EContextMenuInterpolationCurve::Click", EditConditionHides))
	float TransformOvershoot = 1.5f;

	/** Seconds to wait after the state changes before the transform starts moving. The shape holds where it is until then. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", ForceUnits = "s"))
	float TransformDelay = 0.0f;

	/** When off, Shape reuses every Transform setting above, so the whole thing moves as one. Turn it on to give Shape its own speed, curve and delay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (DisplayName = "Separate Transform And Shape"))
	bool bSeparateTransformAndSize = false;

	/** Blend speed for Shape (every Shape Box property). 0 snaps instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (DisplayName = "Shape Interpolation Speed", EditCondition = "bSeparateTransformAndSize", ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float SizeInterpolationSpeed = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (DisplayName = "Shape Interpolation Curve", EditCondition = "bSeparateTransformAndSize"))
	EContextMenuInterpolationCurve SizeInterpolationCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (DisplayName = "Shape Overshoot", EditCondition = "bSeparateTransformAndSize && SizeInterpolationCurve == EContextMenuInterpolationCurve::Click"))
	float SizeOvershoot = 1.5f;

	/** Seconds to wait after the state changes before Shape starts moving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (DisplayName = "Shape Delay", EditCondition = "bSeparateTransformAndSize", ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", ForceUnits = "s"))
	float SizeDelay = 0.0f;

	/** This preset's own speed/curve/overshoot/delay, resolved through Separate Transform And Size. */
	FContextMenuResolvedInterpolation GetResolvedInterpolation() const;

	/** This preset's shape for a state, honoring the button's Enabled Matches Hover Shape. No instance patch here - see UContextMenuButton::ResolveShapeForState. */
	FContextMenuShapeStateSettings GetShapeForState(EContextMenuState State, bool bEnabledMatchesHoverShape) const;
};

/**
 * A set of sounds covering every button state, plus the click. Sound isn't a design channel: it has nothing
 * to say about a target widget, so it lives here instead of inside a Design Target.
 *
 * Only the state-to-slot mapping is button-specific. The slots themselves, the repeat limit and the playback
 * are FUISoundSlot / FUISoundSetSettings / FUISoundPlayer, shared with the info card system - see
 * UISoundTypes.h.
 *
 * Add one set for the whole button in the simple case. Several sets are allowed and all of them are heard, so
 * a shared "clicks" set can sit alongside a set that only fills in one unusual state.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuSoundSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ShowOnlyInnerProperties))
	FUISoundSetSettings Settings;

	/** Plays when the button enters Normal, i.e. on unhover and on being un-enabled. Usually left empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound|States")
	FUISoundSlot Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound|States")
	FUISoundSlot Hovered;

	/** Plays the moment the button goes down. Buttons with Button Can Be Enabled skip the Pressed state, so use Clicked for those. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound|States")
	FUISoundSlot Pressed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound|States")
	FUISoundSlot Disabled;

	/** Plays when the button becomes enabled. Grayed out unless Button Can Be Enabled is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound|States", meta = (EditCondition = "bEnabledSoundApplicable", HideEditConditionToggle))
	FUISoundSlot Enabled;

	/**
	 * Plays when the button's action actually executes, which is not the same as being pressed: a Hold button
	 * fires this on every repeat, a press-duration button only once the bar fills, and a button on cooldown
	 * doesn't fire it at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound|States")
	FUISoundSlot Clicked;

	/** Details-panel mirror of the owning button's Button Can Be Enabled; no runtime effect. */
	UPROPERTY()
	bool bEnabledSoundApplicable = true;

	/** The slot a state maps to. Clicked is not a state and is never returned here. */
	const FUISoundSlot& GetSoundForState(EContextMenuState State) const;
};

/**
 * A patch on one FContextMenuShapeStateSettings: only the halves switched on are replaced, everything else
 * keeps whatever the button (or its Shape Preset) authored. This is what lets a placement nudge a shape
 * without freezing a whole copy of the target.
 *
 * Shape is overridden as one piece: switched on, every Shape Box property in it is the placement's. That is
 * what keeps the Shape Box the single definition of a shape - a property added to it is overridable here
 * without this struct (or the button) needing a toggle of its own for it.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuShapeOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (InlineEditConditionToggle))
	bool bOverrideTransform = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "bOverrideTransform"))
	FWidgetTransform Transform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (InlineEditConditionToggle))
	bool bOverrideShape = false;

	/** The whole Shape Box shape for this slot. Untoggled, it shows (and follows) the button's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "bOverrideShape"))
	FShapeBoxShape Shape;

	bool HasAnything() const { return bOverrideTransform || bOverrideShape; }

	/** Writes the switched-on halves over Target. */
	void ApplyTo(FContextMenuShapeStateSettings& Target) const;
};

/**
 * One widget shaped per button state: its own Transform and Shape Box shape.
 *
 * Aimed at the button itself, the shape bends the button's background brush and the hit area follows it;
 * content and layout stay put. Aimed at a Shape Box (or at a widget that sits alone inside one), the Shape Box
 * is driven: its Shape and Render Transform follow the button's state. Aimed at anything else, only Transform
 * applies - wrap the widget in a Shape Box to give it the rest. Shape Status on the button lists which is which.
 *
 * Authored the same way a Design Target is: every state falls back to Base Shape until its own override is
 * switched on.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuShapeTarget
{
	GENERATED_BODY()

	/** Any widget in this button's tree, including the button itself. Stored by name, so renaming the widget breaks the link. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Target Widget", GetOptions = "GetColorTargetWidgetOptions"))
	FName TargetWidgetName;

	/**
	 * Optional name for changing this target at runtime: Set Target Variant, Set Target Active, Set Shape Target By ID, Set Shape
	 * Target Transform. Several targets may share one (all are changed). Two targets on one shape with their own IDs - Idle (a
	 * circle) and Recording (a square) - and Set Target Variant blends from one to the other.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Target ID"))
	FName TargetId;

	/** Applied while on. Off: skipped, as if it weren't there. Switched at runtime by Set Target Active / Set Target Variant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Active"))
	bool bActive = true;

	// ---- Source ----

	/** Use Project Settings: pick a Shape Preset from Project Settings > Game > Info Card. Custom: author the state shapes and speeds below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Shape Source"))
	EContextMenuButtonSettingSource ShapeSource = EContextMenuButtonSettingSource::UseProjectSettings;

	/** Shape Preset from Project Settings > Game > Info Card. Falls back to this target's Custom shape if the preset is renamed or deleted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::UseProjectSettings", EditConditionHides, GetOptions = "InfoCardDeveloperSettings.GetContextMenuShapePresetNameOptions"))
	FName PresetName = TEXT("Default");

	// ---- State shapes ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	FContextMenuShapeStateSettings BaseShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	bool bOverrideNormalShape = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::Custom && bOverrideNormalShape", EditConditionHides))
	FContextMenuShapeStateSettings NormalShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	bool bOverrideHoveredShape = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::Custom && bOverrideHoveredShape", EditConditionHides))
	FContextMenuShapeStateSettings HoveredShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	bool bOverridePressedShape = false;

	/** Not used by buttons with Button Can Be Enabled: a press on those blends straight from the current state to the state the click produces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::Custom && bOverridePressedShape", EditConditionHides))
	FContextMenuShapeStateSettings PressedShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	bool bOverrideDisabledShape = false;

	/** Used while the button's Is Enabled is false. Disabled is NOT the same as the Enabled state: disabling is explicit, and disabling the button also sets its enabled state to false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::Custom && bOverrideDisabledShape", EditConditionHides))
	FContextMenuShapeStateSettings DisabledShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	bool bOverrideEnabledShape = false;

	/**
	 * Used while the enabled state is true. Grayed out unless Button Can Be Enabled is on, and while Enabled Matches Hover Shape is on.
	 * NOT the same as disabled: a button can be enabled and un-enabled, and when un-enabled it simply returns to Normal.
	 * Priority: Disabled > Pressed > Hovered > Enabled > Normal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "ShapeSource == EContextMenuButtonSettingSource::Custom && bOverrideEnabledShape", EditConditionHides))
	FContextMenuShapeStateSettings EnabledShape;

	// ---- Interpolation ----
	// Per target, like Interpolation Speed already is on a Design Target - a shape aimed at a small icon can
	// snap while the button's own shape eases, without one speed having to serve both.

	/** Blend speed for Transform. 0 snaps instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float TransformInterpolationSpeed = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	EContextMenuInterpolationCurve TransformInterpolationCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (EditCondition = "TransformInterpolationCurve == EContextMenuInterpolationCurve::Click", EditConditionHides))
	float TransformOvershoot = 1.5f;

	/** Seconds to wait after the state changes before the transform starts moving. The shape holds where it is until then. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", ForceUnits = "s"))
	float TransformDelay = 0.0f;

	/** When off, Shape reuses every Transform setting above, so the whole thing moves as one. Turn it on to give Shape its own speed, curve and delay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (DisplayName = "Separate Transform And Shape"))
	bool bSeparateTransformAndSize = false;

	/** Blend speed for Shape (every Shape Box property). 0 snaps instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (DisplayName = "Shape Interpolation Speed", EditCondition = "bSeparateTransformAndSize", ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float SizeInterpolationSpeed = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (DisplayName = "Shape Interpolation Curve", EditCondition = "bSeparateTransformAndSize"))
	EContextMenuInterpolationCurve SizeInterpolationCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (DisplayName = "Shape Overshoot", EditCondition = "bSeparateTransformAndSize && SizeInterpolationCurve == EContextMenuInterpolationCurve::Click"))
	float SizeOvershoot = 1.5f;

	/** Seconds to wait after the state changes before Shape starts moving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (DisplayName = "Shape Delay", EditCondition = "bSeparateTransformAndSize", ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0", ForceUnits = "s"))
	float SizeDelay = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (InlineEditConditionToggle))
	bool bSeparateExitSpeed = false;

	/**
	 * How fast it blends back to Normal (unhovered, switched off) - its exit - for Transform and Shape alike. Unticked: the speeds
	 * above both ways. 0 snaps. Custom shapes only (a Shape Preset keeps its own speeds).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (DisplayName = "Exit Interpolation Speed", EditCondition = "bSeparateExitSpeed", ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float ExitInterpolationSpeed = 5.0f;

	/**
	 * The shape for a state, honoring the button's Enabled Matches Hover Shape. Preset substitution and any
	 * active instance patch are NOT applied here - see UContextMenuButton::ResolveShapeForState, the single
	 * place both the runtime and the designer preview resolve a target so they can't disagree about it.
	 */
	FContextMenuShapeStateSettings GetShapeForState(EContextMenuState State, bool bEnabledMatchesHoverShape) const;

	/** Speed/curve/overshoot/delay for this target, Custom-authored (a preset's own values are read directly by the caller when Shape Source is Use Project Settings). */
	FContextMenuResolvedInterpolation GetResolvedInterpolation() const;
};

/**
 * A patch on one of the button's Shape Targets, matched by the widget it points at - the shape equivalent of
 * a Design Target Override.
 *
 * Laid out slot for slot like the Shape Target it patches: a Base Shape, and one slot for each state the button
 * itself overrides (Enabled included - it is a state like any other). A state the button leaves on its own Base
 * Shape has no slot here; a slot the button has always shows, with a checkbox:
 *
 *  - Checked: this placement patches that slot. Inside it, each channel still has its own toggle.
 *  - Unchecked: the slot is grayed out and ignored, and its values are put back to the button's, exactly as if
 *    it had never been touched. Unchecking never hides the row.
 *
 * Which slots appear is decided by the "... Applicable" mirrors, which the button rewrites every time it seeds
 * the patch. The rows themselves are laid out by FContextMenuTargetOverrideCustomization (see the .cpp):
 * plain UPROPERTY metadata can't show a checkbox on a row while hiding that row on a different condition.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuShapeTargetOverride
{
	GENERATED_BODY()

	/** Which of the button's Shape Targets this patches. Filled in automatically from the button. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shape", meta = (DisplayName = "Target Widget"))
	FName TargetWidgetName;

	// ---- Slots, each with its own "this placement patches it" switch ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Override Base Shape"))
	bool bOverrideBaseShape = false;

	/** Patches every state the button leaves on its own Base Shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Base Shape"))
	FContextMenuShapeOverride BaseShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Override Normal"))
	bool bOverrideNormalShape = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Normal"))
	FContextMenuShapeOverride NormalShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Override Hovered"))
	bool bOverrideHoveredShape = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Hovered"))
	FContextMenuShapeOverride HoveredShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Override Pressed"))
	bool bOverridePressedShape = false;

	/** Not used by buttons with Button Can Be Enabled: a press on those blends straight from the current state to the state the click produces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Pressed"))
	FContextMenuShapeOverride PressedShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Override Disabled"))
	bool bOverrideDisabledShape = false;

	/** Used while the button's Is Enabled is false. Not the same as the Enabled state below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Disabled"))
	FContextMenuShapeOverride DisabledShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Override Enabled"))
	bool bOverrideEnabledShape = false;

	/** Used while the button's enabled state is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Enabled"))
	FContextMenuShapeOverride EnabledShape;

	// ---- Details-panel mirrors of the owning button; no runtime effect ----
	// Whether the button has each slot at all. Resolution never reads these (it asks the button directly).

	UPROPERTY()
	bool bBaseShapeApplicable = true;

	UPROPERTY()
	bool bNormalShapeApplicable = true;

	UPROPERTY()
	bool bHoveredShapeApplicable = true;

	UPROPERTY()
	bool bPressedShapeApplicable = true;

	UPROPERTY()
	bool bDisabledShapeApplicable = true;

	UPROPERTY()
	bool bEnabledShapeApplicable = true;

	/**
	 * What each slot looks like untouched - every channel off, every value the button's - in slot order
	 * (Base, Normal, Hovered, Pressed, Disabled, Enabled). Written by the button on every seed; unchecking a
	 * slot copies its entry back over it.
	 */
	UPROPERTY()
	TArray<FContextMenuShapeOverride> SlotDefaults;

	/** True if anything at all is actually being patched, in a slot the button has and this placement has switched on. */
	bool HasAnything() const;

	/** The patch for one state, honoring the button's Enabled Matches Hover Shape. Null when that state's slot is switched off. Uses the mirrors above. */
	const FContextMenuShapeOverride* GetForState(EContextMenuState State, bool bEnabledMatchesHoverShape) const;

	/** The state's own slot, regardless of any switch. Base Shape is not a state and is never returned here. */
	const FContextMenuShapeOverride& GetStateSlot(EContextMenuState State) const;
	FContextMenuShapeOverride& GetStateSlot(EContextMenuState State);

	/** Whether this placement has switched on the state's own slot. */
	bool IsStateSlotOverridden(EContextMenuState State) const;
};

/**
 * A patch on one widget's design in one state. Channels left off keep whatever the button authored.
 *
 * A placement can only patch what the button itself drives in the matching slot: a channel the button leaves
 * unticked there (Opacity, say) is hidden here outright rather than offered as something to edit that would
 * then do nothing. The "... Applicable" mirrors below are what hide them; the button fills them in every time
 * it seeds the patch, so they follow the button as its channels are switched on and off.
 *
 * Each channel is a checkbox row and a value row rather than one row with an inline checkbox. Unreal can only
 * draw the inline checkbox when a value's Edit Condition is that checkbox alone, and hiding a channel the
 * button doesn't drive needs a second term in the same condition - so the two rows are what make "hidden when
 * the button doesn't drive it" possible at all.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuDesignStateOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Override Tint", EditCondition = "bTintApplicable", EditConditionHides))
	bool bOverrideTint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bTintApplicable && bOverrideTint", EditConditionHides))
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Override Outline Tint", EditCondition = "bOutlineTintApplicable", EditConditionHides))
	bool bOverrideOutlineTint = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bOutlineTintApplicable && bOverrideOutlineTint", EditConditionHides, DisplayName = "Outline Tint"))
	FLinearColor OutlineTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Override Opacity", EditCondition = "bOpacityApplicable", EditConditionHides))
	bool bOverrideOpacity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bOpacityApplicable && bOverrideOpacity", EditConditionHides, ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Override Brush Texture", EditCondition = "bBrushTextureApplicable", EditConditionHides))
	bool bOverrideBrushTexture = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bBrushTextureApplicable && bOverrideBrushTexture", EditConditionHides, DisplayName = "Brush Image Source"))
	EWidgetImageSource BrushImageSource = EWidgetImageSource::Texture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bBrushTextureApplicable && bOverrideBrushTexture && BrushImageSource == EWidgetImageSource::Texture", EditConditionHides, DisplayName = "Brush Texture"))
	TObjectPtr<UTexture2D> BrushTexture = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bBrushTextureApplicable && bOverrideBrushTexture && BrushImageSource == EWidgetImageSource::SVG", EditConditionHides, DisplayName = "Brush SVG"))
	TObjectPtr<UWidgetSVG> BrushSVG = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Override Material", EditCondition = "bMaterialApplicable", EditConditionHides))
	bool bOverrideMaterial = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bMaterialApplicable && bOverrideMaterial", EditConditionHides))
	TObjectPtr<UMaterialInterface> Material = nullptr;

	/**
	 * Scalar parameters, patched along with the material. The list itself tracks the button - parameters
	 * appear and disappear as the material does, and are never editable as a list - but the values are yours
	 * while Material is switched on, so a placement can re-tune one parameter and leave the rest following.
	 */
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bMaterialApplicable && bOverrideMaterial", EditConditionHides, EditFixedOrder, NoResetToDefault, TitleProperty = "ParameterName"))
	TArray<FContextMenuMaterialScalarValue> MaterialScalars;

	/** Vector parameters, patched along with the material. Same rules as Material Scalars. */
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bMaterialApplicable && bOverrideMaterial", EditConditionHides, EditFixedOrder, NoResetToDefault, TitleProperty = "ParameterName"))
	TArray<FContextMenuMaterialVectorValue> MaterialVectors;

	// ---- Details-panel mirrors of the button ----
	// Whether the button drives each channel in the slot this patch sits over. No runtime effect: a patched
	// channel the button doesn't drive is never written anyway, because the button's own Drive toggle is what
	// the runtime reads. Default true so a struct that has never been seeded still shows everything.

	UPROPERTY()
	bool bTintApplicable = true;

	UPROPERTY()
	bool bOutlineTintApplicable = true;

	UPROPERTY()
	bool bOpacityApplicable = true;

	UPROPERTY()
	bool bBrushTextureApplicable = true;

	UPROPERTY()
	bool bMaterialApplicable = true;

	/** True if a channel is both switched on here and something the button actually drives in this slot. */
	bool HasAnything() const
	{
		return (bTintApplicable && bOverrideTint)
			|| (bOutlineTintApplicable && bOverrideOutlineTint)
			|| (bOpacityApplicable && bOverrideOpacity)
			|| (bBrushTextureApplicable && bOverrideBrushTexture)
			|| (bMaterialApplicable && bOverrideMaterial);
	}

	/** Writes the switched-on channels over Target. */
	void ApplyTo(FContextMenuWidgetDesign& Target) const;

	/** Copies the button's Drive toggles for this slot into the mirrors above. Null means the slot doesn't exist on the button. */
	void SyncApplicability(const FContextMenuWidgetDesign* ButtonSlot);
};

/**
 * A patch on one of the button's Design Targets, matched by the widget it points at. A target the button has
 * but this list doesn't mention is left completely alone, and a name that no longer matches any target is
 * quietly ignored rather than resurrecting a deleted target.
 *
 * Laid out slot for slot like the Design Target it patches: a Base Design, and one slot for each state the
 * button overrides (every state, for a target on a Design Preset, since a preset authors each state on its
 * own; Enabled is a state like any other). Each slot has a checkbox that works exactly like the one on a Shape
 * Target Override: unchecked grays the slot out, ignores it, and puts its values back to the button's. Inside a
 * slot, only the channels the button drives in that slot are shown.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuDesignTargetOverride
{
	GENERATED_BODY()

	/** Which of the button's Design Targets this patches. Filled in automatically from the button. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Design", meta = (DisplayName = "Target Widget"))
	FName TargetWidgetName;

	// ---- Slots, each with its own "this placement patches it" switch ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (DisplayName = "Override Base Design"))
	bool bOverrideBaseDesign = false;

	/** Patches every state the button leaves on its own Base Design. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States")
	FContextMenuDesignStateOverride BaseDesign;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (DisplayName = "Override Normal"))
	bool bOverrideNormalDesign = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States")
	FContextMenuDesignStateOverride Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (DisplayName = "Override Hovered"))
	bool bOverrideHoveredDesign = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States")
	FContextMenuDesignStateOverride Hovered;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (DisplayName = "Override Pressed"))
	bool bOverridePressedDesign = false;

	/** Not used by buttons with Button Can Be Enabled: a press on those blends straight from the current state to the state the click produces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States")
	FContextMenuDesignStateOverride Pressed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (DisplayName = "Override Disabled"))
	bool bOverrideDisabledDesign = false;

	/** Used while the button's Is Enabled is false. Not the same as the Enabled state below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States")
	FContextMenuDesignStateOverride Disabled;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (DisplayName = "Override Enabled"))
	bool bOverrideEnabledDesign = false;

	/** Used while the button's enabled state is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States")
	FContextMenuDesignStateOverride Enabled;

	// ---- Details-panel mirrors of the owning button; no runtime effect ----

	UPROPERTY()
	bool bBaseDesignApplicable = true;

	UPROPERTY()
	bool bNormalDesignApplicable = true;

	UPROPERTY()
	bool bHoveredDesignApplicable = true;

	UPROPERTY()
	bool bPressedDesignApplicable = true;

	UPROPERTY()
	bool bDisabledDesignApplicable = true;

	UPROPERTY()
	bool bEnabledDesignApplicable = true;

	/** Each slot untouched, in slot order (Base, Normal, Hovered, Pressed, Disabled, Enabled). See FContextMenuShapeTargetOverride::SlotDefaults. */
	UPROPERTY()
	TArray<FContextMenuDesignStateOverride> SlotDefaults;

	/** True if anything at all is actually being patched, in a slot the button has and this placement has switched on. */
	bool HasAnything() const;

	/** The patch for one state, honoring the button's Enabled Matches Hover Design. Null when that state's slot is switched off. Uses the mirrors above. */
	const FContextMenuDesignStateOverride* GetForState(EContextMenuState State, bool bEnabledMatchesHover) const;

	/** The state's own slot, regardless of any switch. Base Design is not a state and is never returned here. */
	const FContextMenuDesignStateOverride& GetStateSlot(EContextMenuState State) const;
	FContextMenuDesignStateOverride& GetStateSlot(EContextMenuState State);

	/** Whether this placement has switched on the state's own slot. */
	bool IsStateSlotOverridden(EContextMenuState State) const;

	// ---- Legacy ----

	UPROPERTY()
	bool bHasMigratedStateLayout = false;

	/**
	 * Guards the conversion to per-slot switches. For one version the state switches were mirrors of the
	 * button rather than the placement's own choice, so on load a slot is switched on only if something in
	 * it is actually patched.
	 */
	UPROPERTY()
	bool bHasMigratedSlotToggles = false;

	void PostSerialize(const FArchive& Ar);
};

template<>
struct TStructOpsTypeTraits<FContextMenuDesignTargetOverride> : public TStructOpsTypeTraitsBase2<FContextMenuDesignTargetOverride>
{
	enum { WithPostSerialize = true };
};

/**
 * Everything a placement can change about a Context Menu Button, as a PATCH rather than a copy.
 *
 * Nothing in here replaces the button wholesale. Each field has its own little toggle, and only the toggled
 * ones are written over the button's authored Shape/Design at the moment they're read. Everything else keeps
 * following the button, so editing the widget that owns it - adding a Design Target, retinting a state,
 * changing a preset - flows straight through to every placement instead of being clobbered by a frozen copy.
 * That is the whole point of the patch: an instance stores its differences, not a snapshot.
 *
 * You do not create or wire this up by hand. Tick Is Instance Editable on the button and it keeps its own
 * copy; if the widget that contains the button declares a variable of this type, the button finds it by
 * itself and uses that instead - no Apply Instance Overrides node needed.
 *
 * That optional variable is the only way to get PER-PLACEMENT values, because UMG never exposes a child
 * widget's internal widget tree to whatever places it: a struct stored on the button itself can only be
 * edited from inside the widget that authors the button. So declare the variable on the widget that directly
 * contains the button and mark it Instance Editable.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuButtonInstanceOverrides
{
	GENERATED_BODY()

	/**
	 * Which button in the containing widget this struct is for, by name.
	 *
	 * Filled in for you by "Add Overrides Variable" in the button's Details panel, and that is the only
	 * way it normally gets set. With it, a button finds its overrides by an exact match instead of by
	 * guessing from the variable's name - so the variable can be called anything, a widget can hold as
	 * many of them as it has buttons, and none of it depends on a naming convention anyone has to know.
	 *
	 * Left empty, the old behavior still applies: the button looks for a variable named after itself, or
	 * for a single unambiguous one. Existing setups keep working untouched.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (DisplayName = "Target Button Name"))
	FName TargetButtonName;

	/**
	 * Which state this placement previews in the designer - the same dropdown the button has, but for this
	 * placement only, so every copy of a widget can be judged in Hovered, Disabled and so on right where it is
	 * placed, with its own patch applied. Editor only; ignored at runtime.
	 *
	 * Only read when this struct is a variable on a widget that has itself been placed inside another widget.
	 * In the widget that authors the button, the button's own Show Preview stays in charge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Preview", meta = (DisplayName = "Show Preview"))
	EContextMenuDesignPreview ShowPreview = EContextMenuDesignPreview::Normal;

	/** When off, the button's Shape Targets are left entirely alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	bool bOverrideShape = false;

	/**
	 * One entry per Shape Target on the button, kept in step with it automatically. Entries can't be added,
	 * removed, reordered or reset from here - the list is the button's - only the fields inside them are
	 * editable. Untoggled channels follow the button.
	 */
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "Shape", meta = (DisplayName = "Shape Targets", EditCondition = "bOverrideShape", EditConditionHides, EditFixedOrder, NoResetToDefault, TitleProperty = "TargetWidgetName"))
	TArray<FContextMenuShapeTargetOverride> ShapePatch;

	/** When off, the button's Design Targets are left entirely alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	bool bOverrideDesign = false;

	/**
	 * One entry per Design Target on the button, kept in step with it automatically. Entries can't be added,
	 * removed, reordered or reset from here - the list is the button's - only the fields inside them are
	 * editable. Untoggled channels follow the button.
	 */
	UPROPERTY(EditAnywhere, EditFixedSize, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Design Targets", EditCondition = "bOverrideDesign", EditConditionHides, EditFixedOrder, NoResetToDefault, TitleProperty = "TargetWidgetName"))
	TArray<FContextMenuDesignTargetOverride> DesignPatch;

	/** True once anything is actually being patched, as opposed to the halves merely being switched on. */
	bool HasAnything() const;

	// ---- Legacy ----
	// The first version of this struct stored whole copies of the Design Target array, which is exactly the
	// behavior that made a placement stop tracking its parent. Kept hidden so old assets still load;
	// UContextMenuButton::PostLoad reads them once and clears them.

	UPROPERTY()
	TArray<FContextMenuDesignTarget> DesignOverride;

	UPROPERTY()
	bool bHasBeenSeeded = false;
};

/**
 * Runtime-only resolved shape target (not a USTRUCT: widgets are held weakly, nothing to serialize). One per
 * entry in UContextMenuButton::ShapeTargets, mirroring FContextMenuRuntimeDesignTarget.
 *
 * Unlike Design, Shape's own state always applies (there's no per-channel "isn't driven, leave it alone"), so
 * there's no baseline to blend back to - only a Start value to blend away from whenever the state changes,
 * captured the moment that happens rather than up front.
 */
struct FContextMenuRuntimeShapeTarget
{
	TWeakObjectPtr<UWidget> Widget;

	/** Index into UContextMenuButton::ShapeTargets, so the authored values are read live every step. */
	int32 AuthoredIndex = INDEX_NONE;

	/** Where each channel is right now. */
	FWidgetTransform CurrentTransform;
	FShapeBoxShape CurrentShape;

	/** Where the current blend started from, captured the moment the shape state last changed. */
	FWidgetTransform StartTransform;
	FShapeBoxShape StartShape;

	/** Blend progress since the last state change. Tracked per target now that each target has its own speed/curve/delay. */
	float TransformTransitionAlpha = 1.0f;
	float SizeTransitionAlpha = 1.0f;
	float TransformDelayRemaining = 0.0f;
	float SizeDelayRemaining = 0.0f;

	/** The shape state this target last blended toward, so ApplyShapeTarget can tell when it needs to restart the blend. */
	EContextMenuState LastShapeState = EContextMenuState::Normal;
	bool bHasInitializedState = false;

	/** A vector shape's Look: the shape it is written onto (the target, or the button's Background Shape), and the settings any state changes. */
	TWeakObjectPtr<UShapeWidget> LookWidget;
	TArray<const FProperty*> LookProperties;

	/** The Look as it stood when the state last changed - what the blend starts from. */
	FInstancedPropertyBag StartLook;

	/** The shape as authored: the value of a setting for a state that has no Look of its own. */
	FInstancedPropertyBag BaselineLook;

	/** The state's settings the blend heads for, resolved when the state last changed (or snapped). */
	FContextMenuShapeStateSettings ResolvedGoal;
	bool bHasResolvedGoal = false;
};

/** A vector shape as authored, before any Look was written onto it - what a setting no state changes is compared against. */
USTRUCT()
struct FContextMenuLookBaseline
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UWidget> Widget;

	UPROPERTY()
	FInstancedPropertyBag Look;
};

/**
 * An Image, Border or Button a Design Target shows an SVG on (not a USTRUCT: runtime only). Its brush (or the button's
 * every state brush) is the SVG drawn at the widget's size on screen, pushed in again whenever that drawing changes;
 * what it had before is kept to put back.
 */
struct FContextMenuTargetSVG
{
	TWeakObjectPtr<UWidgetSVG> SVG;
	FWidgetSVGBrush Drawing;
	FName PushedResource;
	TOptional<FSlateBrush> BrushBefore;
	TOptional<FButtonStyle> StyleBefore;
};

/** Runtime-only resolved design target (not a USTRUCT: widgets are held weakly, nothing to serialize). */
struct FContextMenuRuntimeDesignTarget
{
	TWeakObjectPtr<UWidget> Widget;

	/** Index into UContextMenuButton::DesignTargets, so the authored values are read live every step. */
	int32 AuthoredIndex = INDEX_NONE;

	/** Where each driven channel is right now, so a state change blends on from here rather than restarting. */
	FLinearColor CurrentTint = FLinearColor::White;
	FLinearColor CurrentOutlineTint = FLinearColor::White;
	float CurrentOpacity = 1.0f;

	// What the widget was authored as, captured once when the target is resolved. A state that doesn't drive
	// a channel blends back to these, which is what lets a channel be switched on for one state only: the
	// other states aren't "unwritten", they're written back to the widget's own look.
	FLinearColor BaselineTint = FLinearColor::White;
	FLinearColor BaselineOutlineTint = FLinearColor::White;
	float BaselineOpacity = 1.0f;
	TWeakObjectPtr<UTexture2D> BaselineBrushTexture;
	TWeakObjectPtr<UMaterialInterface> BaselineMaterial;
	/** The SVG a shape or SVG Image target was authored with (null if it shows a texture or nothing). */
	TWeakObjectPtr<UWidgetSVG> BaselineSVG;
	bool bHasBaseline = false;
	TMap<FName, float> CurrentScalars;
	TMap<FName, FLinearColor> CurrentVectors;

	/** Resolved once per target; null when the widget's brush isn't a material. */
	TWeakObjectPtr<UMaterialInstanceDynamic> CachedMaterial;
	bool bHasResolvedMaterial = false;

	/** Brush swaps are discrete, so they only happen when the state actually changes. */
	EContextMenuState LastBrushState = EContextMenuState::Normal;
	bool bHasAppliedBrush = false;

	/** Material swaps are discrete, so they only happen when the state actually changes. */
	EContextMenuState LastMaterialState = EContextMenuState::Normal;
	bool bHasAppliedMaterial = false;

};

/**
 * Slate button used by UContextMenuButton. Identical to SButton, except for two things.
 *
 * Shape: the button's background brush is drawn by a brush layer - an SShapeBox wrapping a leaf that paints the
 * brush - rather than by SBorder, so the brush takes any FShapeBoxShape plus the shape's Transform, while the
 * content is painted exactly where layout put it. The brush layer sits in the hit-test grid with this button as its
 * parent, so wherever the shape is drawn (an extruded edge, a skewed corner) is hoverable and clickable, and events
 * there reach this button as if the button itself had been hit. Where the shape is not drawn, nothing is hit: the
 * button's own hit rect is shrunk to a "core" that sits inside the shape, carried on this widget's render
 * transform (layout never moves, and the content is painted back through the inverse of it). The button itself
 * stays in the hit-test grid, which is what keeps it focusable and reachable by gamepad navigation.
 *
 * OnPaint is the only thing that writes that render transform, and it writes it last, for the paint after this
 * one. It has to be that way round. A paint is handed geometry built from whatever transform was on the widget
 * when Slate last updated it, so the transform OnPaint cancels has to be one that cannot have moved since that
 * geometry was built - which is only guaranteed of the one committed by the previous paint. Writing it from
 * anywhere else lets the two drift apart by one frame's worth of the blend, which shows up as the content
 * fidgeting while the shape animates.
 *
 * Brush: the style brush is picked from the state the owner resolved rather than from Slate's own hover/press
 * flags, so the button's own state settings win over the native button style instead of being painted on top of it.
 *
 * With no shape, no active button visuals and no state reported, it paints exactly like a stock SButton.
 */
class SContextMenuSlateButton : public SButton
{
public:
	/** Also builds the brush layer. Called once, straight after construction. */
	void SetOwnerWidget(UContextMenuButton* InOwner);

	/**
	 * The shape to paint the brush with and hit test against. The brush takes it on the next paint; the hit area
	 * of the button's own rect follows at the end of that paint.
	 *
	 * @param InShape      The brush's Shape Box shape, evaluated against the button's layout size.
	 * @param InTransform  Applied to the shaped brush, pivoting on its middle.
	 */
	void SetButtonShape(const FShapeBoxShape& InShape, const FWidgetTransform& InTransform);

	/**
	 * The state the owner's state machine resolved. From the first call on, this is what picks the style brush:
	 * Slate's raw hover/press flags no longer do. So the Pressed state's settings replace the style's native
	 * pressed brush (rather than tinting it), hover can be suppressed by Hover Shape While Enabled, and a button
	 * that skips Pressed entirely (Button Can Be Enabled) no longer flashes the native pressed brush when held.
	 *
	 * @param InVisualState       Normal, Hovered, Pressed or Disabled; the owner maps Enabled onto one of those.
	 * @param bInDesignOwnsBrush  True when a Design Target drives this button's own tint, brush or material. The
	 *                            design is then the entire look, so every state paints the brush slot the design
	 *                            writes into instead of the style's per-state brushes.
	 */
	void SetVisualState(EContextMenuState InVisualState, bool bInDesignOwnsBrush);

	/**
	 * Writes the committed core transform back onto the render transform. UWidget::SynchronizeProperties pushes the
	 * authored render transform straight onto this widget, which drops the core composed into it; the owner calls
	 * this straight afterwards. It re-applies what the last paint committed, never a newly computed one, so the
	 * invariant the next paint relies on still holds.
	 */
	void RefreshShapeRenderTransform();

	void ClearVisualState();

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/**
	 * Paints Shape - a vector shape the owner holds - as the button's background instead of the style brush, inside the same
	 * brush layer, so the shape's Transform, hit area and the rest work exactly as for the brush. Null goes back to the brush.
	 */
	void SetBackgroundShape(UShapeWidget* InShape);

	/** Where the last press landed, as a fraction of the button's layout box (0, 0 top left). (0.5, 0.5) before any. */
	FVector2D GetLastPressFraction() const { return LastPressFraction; }

	/** Where the last press landed on screen. Unset for a key or gamepad press, which has no position. */
	const TOptional<FVector2D>& GetLastPressScreenPosition() const { return LastPressScreenPosition; }

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override;

	/** A key or gamepad press has no position: it counts as the middle, so a Choice pressed from a gamepad steps forward. */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Gives the owner first say over gamepad and keyboard navigation: a Choice steps its value on left and right. */
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;

	/**
	 * A mouse release reaches the button wherever the shape is drawn, because the brush layer bubbles to it. A touch
	 * release is only counted as "over the button" by SButton if it lands inside the button's own geometry, though,
	 * so one lifted over an extruded edge is re-checked here against the drawn shape.
	 */
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	/** The shape's Transform, pivoting on the middle of the shaped brush, in the layout box's local space. */
	FSlateRenderTransform ComputeBrushTransform(const FVector2D& LocalSize) const;

	/**
	 * Maps the layout box onto the button's own hit rect. For a parallelogram shape that is the shape itself; for
	 * anything else (chamfers, bends, tapers...) it is the largest centered, scaled-down layout box the painted
	 * shape fully covers, so the button's rect never pokes out past the drawn outline.
	 */
	FSlateRenderTransform ComputeCoreTransform(const FVector2D& LocalSize) const;

	/**
	 * Works out the core transform for the current shape and commits it: stores what the next paint needs to cancel
	 * it, and composes it onto the render transform (which is what moves the button's own hit rect, since Slate hit
	 * tests against paint-space geometry). Called at the very end of OnPaint and nowhere else.
	 */
	void CommitShapeRenderTransform(const FVector2D& LocalSize) const;

	/** Composes a committed core transform with the widget's own authored render transform and writes the result. */
	void WriteRenderTransform(const FSlateRenderTransform& Core, bool bShapeActive, const FVector2D& LocalSize, const FWidgetTransform& AuthoredTransform, const FVector2D& AuthoredPivot);

	/** Whether a screen-space point lies on the brush as it was last painted. False if the brush layer wasn't painted. */
	bool IsScreenPointInBrushShape(const FVector2D& ScreenPoint) const;

	TWeakObjectPtr<UContextMenuButton> OwnerWidget;

	/** The shaped brush: an SShapeBox around BrushPainter. Not a layout child - painted by OnPaint, with this button as its parent. */
	TSharedPtr<SShapeBox> BrushLayer;
	TSharedPtr<SContextMenuButtonBrush> BrushPainter;

	/** The vector Background Shape, in the brush layer in place of BrushPainter. */
	TSharedPtr<SWidgetShape> BackgroundPainter;
	TWeakObjectPtr<UShapeWidget> BackgroundShape;

	/** Records where a press landed, in the layout box. */
	void RecordPress(const FGeometry& MyGeometry, const FPointerEvent& Event);
	FVector2D LastPressFraction = FVector2D(0.5, 0.5);
	TOptional<FVector2D> LastPressScreenPosition;

	FShapeBoxShape ButtonShape;
	FWidgetTransform BrushTransform;
	bool bHasButtonShape = false;

	EContextMenuState VisualState = EContextMenuState::Normal;
	bool bDesignOwnsBrush = false;

	/** False until the owner first calls SetVisualState; until then the stock SButton brush logic applies. */
	bool bHasVisualState = false;

	/** True when the brush layer was painted by the latest paint, i.e. its geometry and hit data are current. */
	mutable bool bBrushLayerPainted = false;

	// The core as committed by the last paint, i.e. the transform sitting on the render transform right now and so
	// the one baked into the geometry this paint was handed. Mutable because the commit happens at the end of
	// OnPaint, which is const.
	mutable bool bHasCommittedShape = false;
	mutable bool bCommittedShapeActive = false;
	mutable FSlateRenderTransform CommittedShape;
	mutable FSlateRenderTransform CommittedShapeInverse;

	// What that core was committed from, so the transform is only rewritten (and the widget invalidated) when one
	// of its inputs actually moves.
	mutable FVector2D CommittedLocalSize = FVector2D::ZeroVector;
	mutable FShapeBoxShape CommittedButtonShape;
	mutable FWidgetTransform CommittedBrushTransform;
	mutable FWidgetTransform CommittedAuthoredTransform;
	mutable FVector2D CommittedAuthoredPivot = FVector2D(0.5, 0.5);

	/**
	 * Is Enabled as of the last paint. UWidget::SetIsEnabled isn't virtual and Slate has no enabled-changed
	 * callback, so paint is where a change first becomes visible; the owner is then notified on the next tick.
	 */
	mutable bool bLastPaintedSelfEnabled = true;

	/** The layout size last told to an owner that wants it (UContextMenuButton::WantsLayoutSizeChanges). */
	mutable FVector2D LastReportedLayoutSize = FVector2D(-1.0, -1.0);
};

/** Which of the button's events an owning widget's Event Dispatcher is being matched against. */
UENUM()
enum class EContextMenuButtonEvent : uint8
{
	Clicked,
	EnabledStateChanged,
	HoverChanged,
	PressDurationCompleted
};

UCLASS()
class NAMETBD_API UContextMenuButton : public UButton, public IWidgetAnimHost, public IWidgetAnimDesignerPreviewHost
{
	GENERATED_BODY()

public:

	/** Zeroes out the native button style's default padding - see the constructor for why. */
	UContextMenuButton(const FObjectInitializer& ObjectInitializer);

	/** 
	 * For proper functionality with the settings below, use the events 'On Context...', otherwise this
	 * behaves just like a regular button. 
	 * 
	 * Enabled state semantics: Disabled <- Normal -> Enabled
	 * 'Unenabling' a button takes it a Normal state, not Disabled. 
	 * 
	 * For dynamic and complex widgets:
	 * You may use 'SetOwningCard' on Event Construct to explicitly link this button to a specific Info Card for better performance. 
	 * This completely bypasses the automatic hierarchy search used by 'GetOwningCard', which otherwise walks up the UI tree to find 
	 * the first ancestor widget that inherits from Uinfocardwidget.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Context Menu Button")
	bool bREADME = false;

	/**
	 * Shows the button's design and shape for one state in the UMG designer, without pressing Play. Editor only.
	 * Set it back to None to restore the authored look. Hidden from the Details panel: the Widget Blueprint editor's toolbar
	 * previews states (and values) with their transitions now, and this stays the state shown until the toolbar picks one.
	 *
	 * Placements have their own copy of this on their Instance Overrides variable, so a widget dropped into
	 * another one can be previewed per state from there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button")
	EContextMenuDesignPreview ShowPreview = EContextMenuDesignPreview::Normal;

	// ------------------------------------------------------------------
	// Context Menu Button Logic Configurations
	// ------------------------------------------------------------------
	
	/** 
	 * When the class widget is a child of InfoCardBase, setting this to true will destroy the entire widget after On Context Menu Button runs.
	 * 
	 * If the class widget is not a child of InfoCardBase and if there are no InfoCardBase children in the widget tree,
	 * this does nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button")
	bool bShouldDestroyCard = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings")
	EContextMenuButtonType ButtonType = EContextMenuButtonType::Press;

	/**
	 * If true, this button possesses an enabled state, in which a boolean is exposed on On Context Menu Button Clicked.
	 *
	 * The enabled state is not the same as disabled (Is Enabled). The button can be enabled and un-enabled by clicking it;
	 * when un-enabled it just goes back to the Normal state. Disabling is its own explicit state, and disabling the
	 * button also sets its enabled state to false.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings")
	bool bButtonCanBeEnabled = true;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "bButtonCanBeEnabled", HideEditConditionToggle))
	bool bDefaultEnabledState = false;

	/**
	 * If true, the Enabled state uses the Hovered shape (Transform, Shape, and the style's
	 * Hovered brush), so there's nothing to copy from Hovered to Enabled. Enabled Shape is grayed out while this is on.
	 * Applies to Shape Presets too.
	 * 
	 * Set to true if your hover shape and enabled shapes are identical (which they often are).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "bButtonCanBeEnabled", HideEditConditionToggle))
	bool bEnabledMatchesHoverShape = false;

	/**
	 * If true, the Enabled state uses the Hover design (Tint, Opacity, Brush, Material, Parameters). Each Design Target's 
	 * Enabled Design is hidden while this is on.
	 * 
	 * Set to true if your hover design and enabled design are identical (which they often are).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "bButtonCanBeEnabled", HideEditConditionToggle))
	bool bEnabledMatchesHoverDesign = false;

	/**
	 * If false, hovering while this button is enabled does not apply the Hovered shape (Transform, Shape,
	 * and the style's Hovered brush); the Enabled shape stays.
	 *
	 * Hidden while Enabled Matches Hover Shape is on: at that point Enabled already looks exactly like Hovered, so
	 * this has nothing left to switch off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "bButtonCanBeEnabled && !bEnabledMatchesHoverShape", EditConditionHides))
	bool bHoverShapeWhileEnabled = false;

	/**
	 * If false, hovering while this button is enabled does not apply the Hovered designs; the Enabled designs stay. Once the
	 * button is un-enabled, Hovered designs stay off until the cursor leaves the button and hovers it again. Pressed designs
	 * are unaffected.
	 *
	 * Hidden while Enabled Matches Hover Design is on: at that point Enabled already uses the Hover design, so this has
	 * nothing left to switch off.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "bButtonCanBeEnabled && !bEnabledMatchesHoverDesign", EditConditionHides))
	bool bHoverDesignWhileEnabled = true;

	/**
	 * If true, enabling this button un-enables every other button in the same container that can be enabled, so only
	 * one is enabled at a time (tabs, radio buttons, a filter row).
	 *
	 * It applies to the whole container: turn it on for ANY one button there and every button in that container
	 * behaves this way, including buttons added later or spawned at runtime. The others don't need it set.
	 *
	 * The container is found automatically: the closest widget holding this button and at least one other button that
	 * can be enabled. Wrappers are looked through, so buttons inside Size Boxes, or each inside its own User Widget
	 * (e.g. a Vertical Box of W_TabButton), still share the box they're laid out in. Buttons that can be enabled are
	 * ignored. To group buttons that don't share a container, give them the same Enable Group instead.
	 *
	 * Clicking the enabled button still un-enables it. If several start enabled, the first one built keeps it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "bButtonCanBeEnabled", HideEditConditionToggle))
	bool bUnenableAllOtherGroupButtonsOnEnabled = false;

	/**
	 * A click enables the button but never un-enables it - a radio button, a tab, a selected loadout slot: clicking the
	 * one already chosen keeps it chosen. Off, a click toggles (a checkbox, a switch). Set Enabled State still does either.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "bButtonCanBeEnabled", HideEditConditionToggle))
	bool bClickOnlyEnables = false;

	/**
	 * A ButtonGroup links buttons together to allow for management of other buttons from any one of them, e.g. setting
	 * a button in a group to enabled can unenable all other buttons in the group.
	 * 
	 * - Leaving the group name automatically links buttons that share the same immediate UI layout box, e.g. Vertical Box. 
	 * - (Recommended) Enroll the button into a group by naming it with the (exact) shared name that the other buttons use.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "bButtonCanBeEnabled", HideEditConditionToggle))
	FName ButtonGroup = NAME_None;

	/** 
	 * The automatically assigned index of this button within its enable group. 
	 * Useful for mapping to arrays, enums, or Widget Switchers.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Settings", meta = (EditCondition = "bButtonCanBeEnabled", HideEditConditionToggle))
	int32 GroupIndex = INDEX_NONE;

	/** UseProjectSettings: Edit > Project Settings > Game > Info Card */ 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings")
	EContextMenuButtonSettingSource ButtonTypeBehaviorSource = EContextMenuButtonSettingSource::UseProjectSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Press && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	EExecutePressAction PressAction = EExecutePressAction::OnRelease;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	EExecuteHoldAction HoldAction = EExecuteHoldAction::Immediate;

	/** Use GetPressDurationProgress to retrieve the current progress. 
	 * 
	 * Progress is handled internally, so just wire the progress pin from On Context Press Duration Progress Set Percent 
	 * on your progress bar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom && HoldAction == EExecuteHoldAction::AfterPressDuration", EditConditionHides, ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	float PressDuration = 1.0f;
	
	/** If true, set press duration progress to 0.0f on cancel. */ 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom && HoldAction == EExecuteHoldAction::AfterPressDuration", EditConditionHides))
	bool bPressDurationProgressResetsOnCancelled = true; 
	
	/** If true, set press duration progress to 0.0f on complete. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom && HoldAction == EExecuteHoldAction::AfterPressDuration", EditConditionHides))
	bool bPressDurationProgressResetsOnCompleted = true; 

	/** Delegate for the PressDuration property to enable UMG function binding */
	UPROPERTY()
	FGetFloat PressDurationDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides, ClampMin = "0.01", UIMin = "0.01", UIMax = "0.5"))
	float ActionExecutionFrequency = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides, ClampMin = "0.0", UIMin = "0.0", UIMax = "0.025"))
	float ActionExecutionFrequencyAcceleration = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides, ClampMin = "0.001", UIMin = "0.01", UIMax = "0.5"))
	float ActionExecutionFrequencyCap = 0.05f;

	/**
	 * Whether a Hold button keeps firing while it stays held (at Action Execution Frequency, speeding up). Off, it fires once
	 * per press - once its Press Duration fills, for a "hold to confirm" button.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold", EditConditionHides))
	bool bRepeatWhileHeld = true;

	/**
	 * A widget that shows the press duration filling, without any Blueprint: a shape (its Progress Value goes from Progress
	 * Range X to Y - a ring's Trim End 0 to 1, an Ellipse's Sweep 0 to 360) or a Progress Bar (its percent). Empty: none.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (DisplayName = "Progress Widget", EditCondition = "ButtonType == EContextMenuButtonType::Hold", EditConditionHides, GetOptions = "GetColorTargetWidgetOptions"))
	FName PressProgressWidgetName;

	/** Which of the shape's values the progress drives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (DisplayName = "Progress Value", EditCondition = "ButtonType == EContextMenuButtonType::Hold", EditConditionHides))
	EWidgetShapeValue PressProgressValue = EWidgetShapeValue::TrimEnd;

	/** The value at no progress (X) and at full (Y). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (DisplayName = "Progress Range", EditCondition = "ButtonType == EContextMenuButtonType::Hold", EditConditionHides))
	FVector2D PressProgressRange = FVector2D(0.0, 1.0);

	// ------------------------------------------------------------------
	// Cooldown Configurations
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Cooldown", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Press", EditConditionHides))
	bool bHasButtonCooldown = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Cooldown", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Press && bHasButtonCooldown", ClampMin = "0.01", UIMin = "0.01", UIMax = "3.0"))
	float CooldownDuration = 1.0f;

	/** If true, natively disables the button widget during cooldown by intercepting clicks without applying opacity fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Cooldown", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Press && bHasButtonCooldown"))
	bool bInvalidatesButton = true;

	// ------------------------------------------------------------------
	// Shape Configurations
	// ------------------------------------------------------------------

	/**
	 * Draws the button's background as a vector shape - a Rectangle (Fully Rounded for a pill), an Ellipse, a Star, any shape
	 * of the plugin - instead of the Style's brushes. Its fill, outline (a material laid along it included) and the rest are
	 * Background Look; a Shape Target aimed at Self changes them per state, and the hit area follows the shape's Transform
	 * as it does the brush's. None: the Style's brushes, as a stock button.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Shape", meta = (AllowAbstract = "false"))
	TSubclassOf<UShapeWidget> BackgroundShape;

	/** The Background Shape as authored: every setting that kind of shape has. States change it through a Shape Target aimed at Self. */
	UPROPERTY(EditAnywhere, Category = "Context Menu Button|Shape", meta = (FixedLayout, EditCondition = "bHasBackgroundShape", EditConditionHides))
	FInstancedPropertyBag BackgroundLook;

	/** Details-panel mirror of Background Shape being set. */
	UPROPERTY()
	bool bHasBackgroundShape = false;

	/**
	 * Parts hidden from the Hierarchy: the button's content kept in HiddenContentSlot instead of among its slots, the widgets
	 * themselves in PartsTree - a widget tree of the button's own, inside it, the way a User Widget keeps its widgets. The
	 * Widget Blueprint lists, compiles and gives GUIDs only to the widgets of its own tree, so these are none of its business;
	 * duplicating, copying, pasting and undoing the button carry them along as its subobjects. Built, drawn and targeted
	 * exactly as when listed. See SetPartsHidden.
	 */
	bool ArePartsHidden() const;

	/**
	 * Checks the hidden parts on load (and for the editor module): parts in PartsTree are this button's own and kept (their
	 * links put right); parts anywhere else - Buttons fix6-fix7 kept them in the Widget Blueprint's tree, or a pasted copy
	 * still names the original's - are put back as the button's content when they are its own, else let go of, and the
	 * button builds its parts again. True when it changed anything.
	 */
	bool RestoreHiddenParts();

	/** Whether the button holds a hidden content slot at all (its own or legacy - see RestoreHiddenParts). */
	bool HasHiddenParts() const { return HiddenContentSlot != nullptr; }

	/** The content hidden from the Hierarchy, or null. */
	UWidget* GetHiddenContent() const { return HiddenContentSlot ? HiddenContentSlot->Content.Get() : nullptr; }

	/** The content's slot, listed or hidden. */
	UPanelSlot* GetPartsSlot() const { return GetContentSlot() ? GetContentSlot() : HiddenContentSlot.Get(); }

	/** Whether the button has content, listed or hidden. */
	bool HasAnyContent() const { return GetChildrenCount() > 0 || GetHiddenContent() != nullptr; }

	/** The Background Shape being drawn - set its values from Blueprint like any shape's. Null without a Background Shape. */
	UFUNCTION(BlueprintPure, Category = "Context Menu Button|Shape")
	UShapeWidget* GetBackgroundShapeWidget() const { return BackgroundShapeWidget; }

	/**
	 * One entry per widget this button shapes: its own Transform and Shape Box shape across the button's states.
	 *
	 * Aim one at the button itself to shape its background brush (the hit area follows; content and layout don't
	 * move). Aim one at a Shape Box, or at a widget sitting alone inside a Shape Box, to drive that Shape Box.
	 * Any other widget only takes the Transform.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape", meta = (TitleProperty = "TargetWidgetName"))
	TArray<FContextMenuShapeTarget> ShapeTargets;

	/**
	 * What each Shape Target is actually shaping: the button's brush, a Shape Box, or - for a widget that is
	 * neither a Shape Box nor alone inside one - Transform only, with its Shape ignored. Rebuilt whenever the
	 * button is edited.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Context Menu Button|Shape", meta = (DisplayName = "Shape Status", MultiLine = true))
	FString ShapeTargetStatus;

	// ------------------------------------------------------------------
	// Design Configurations
	// ------------------------------------------------------------------

	/**
	 * One entry per widget this button restyles. Each drives that widget's tint, opacity, brush
	 * texture, material and material parameters across the button's states, and can preview any state in the designer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Design", meta = (TitleProperty = "TargetWidgetName"))
	TArray<FContextMenuDesignTarget> DesignTargets;

	/**
	 * Pre-Design Target entries. Hidden; kept only so previously saved assets still load their colors.
	 * UContextMenuButton::PostLoad converts each into a tint-only Design Target and empties this.
	 */
	UPROPERTY()
	TArray<FContextMenuColorTarget> ColorTargets;

	// ------------------------------------------------------------------
	// Sound Configurations
	// ------------------------------------------------------------------

	/**
	 * Sound Sets for this button. Each set covers every state plus the click, with its own volume, pitch,
	 * pitch variation, delay and repeat limit.
	 *
	 * Sound used to be a Design Target channel, which meant picking a target widget for something that has no
	 * widget to speak of and re-picking it for every set of sounds. It lives here instead. Several sets can be
	 * added and all of them are heard, so a shared click set can sit alongside a set that fills in one state.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Sound", meta = (TitleProperty = "Settings.SetName"))
	TArray<FContextMenuSoundSet> Sounds;

	/**
	 * Plays a one-off sound through the same path the Sound Sets use (2D, with the button's own repeat limit
	 * bypassed). Handy for a Blueprint that wants a variant without authoring a whole set.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Sound")
	void PlayButtonSound(USoundBase* Sound, float VolumeMultiplier = 1.0f, float PitchMultiplier = 1.0f);

	/**
	 * Re-resolves Shape Targets and Design Targets and re-applies the current state's shape/design. Call after
	 * changing visual properties at runtime (e.g. ShapeTargets, DesignTargets).
	 * Not needed after Set Is Enabled or Set Enabled State; those update automatically.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|State")
	void RefreshButtonState(bool bInstant = false);

	// ------------------------------------------------------------------
	// Targets at runtime (by Target ID)
	// ------------------------------------------------------------------

	/**
	 * Switches every Shape and Design Target whose Target ID is InTargetId on or off. A target switched on blends from where its
	 * widget is now to its state's look (bInstant: at once). A target switched off is skipped, the widget left as it is.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Targets", meta = (DisplayName = "Set Target Active"))
	void SetTargetActive(FName InTargetId, bool bMakeActive, bool bInstant = false);

	/**
	 * Switches on the targets with Target ID InTargetId and switches off every other target (with a Target ID) on the same widgets
	 * - one of several looks for one widget: a volume icon's Full / Half / Muted, a record button's Idle circle / Recording
	 * square. Targets without a Target ID are never switched off.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Targets", meta = (DisplayName = "Set Target Variant"))
	void SetTargetVariant(FName InTargetId, bool bInstant = false);

	/** Whether any target with Target ID InTargetId is on. */
	UFUNCTION(BlueprintPure, Category = "Context Menu Button|Targets", meta = (DisplayName = "Is Target Active"))
	bool IsTargetActive(FName InTargetId) const;

	/** Every Target ID the Shape and Design Targets use, once each. */
	UFUNCTION(BlueprintPure, Category = "Context Menu Button|Targets", meta = (DisplayName = "Get Target IDs"))
	TArray<FName> GetTargetIds() const;

	/** The first Shape Target with Target ID InTargetId. False when there is none. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Targets", meta = (DisplayName = "Get Shape Target By ID"))
	bool GetShapeTargetById(FName InTargetId, FContextMenuShapeTarget& OutShapeTarget) const;

	/**
	 * Replaces every Shape Target with Target ID InTargetId with NewShapeTarget (its Target ID kept) and blends to it (bInstant:
	 * at once). Change what Get Shape Target By ID gave and set it back. False when there is none.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Targets", meta = (DisplayName = "Set Shape Target By ID"))
	bool SetShapeTargetById(FName InTargetId, const FContextMenuShapeTarget& NewShapeTarget, bool bInstant = false);

	/** The first Design Target with Target ID InTargetId. False when there is none. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Targets", meta = (DisplayName = "Get Design Target By ID"))
	bool GetDesignTargetById(FName InTargetId, FContextMenuDesignTarget& OutDesignTarget) const;

	/** Replaces every Design Target with Target ID InTargetId with NewDesignTarget (its Target ID kept) and blends to it. False when there is none. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Targets", meta = (DisplayName = "Set Design Target By ID"))
	bool SetDesignTargetById(FName InTargetId, const FContextMenuDesignTarget& NewDesignTarget, bool bInstant = false);

	/**
	 * Shows NewSVG (else NewTexture) on the Design Targets with Target ID InTargetId, in every state: their Base Design and each
	 * state they override get the image. An icon that follows a value - a speaker for the volume. Both None: the image is no longer
	 * driven (the widget's own shows). False when there is no such target.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Targets", meta = (DisplayName = "Set Design Target Image"))
	bool SetDesignTargetImage(FName InTargetId, UWidgetSVG* NewSVG, UTexture2D* NewTexture, bool bInstant = false);

	/**
	 * Sets the Transform the Shape Targets with Target ID InTargetId have in ForState (their Base Shape when the state isn't
	 * overridden) and blends to it. False when there is no such target.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Targets", meta = (DisplayName = "Set Shape Target Transform"))
	bool SetShapeTargetTransform(FName InTargetId, EContextMenuState ForState, const FWidgetTransform& NewTransform, bool bInstant = false);

	// ------------------------------------------------------------------
	// Action
	// ------------------------------------------------------------------

	/**
	 * Which entry in Project Settings > Info Card > Button Actions this button IS.
	 *
	 * The tag is the whole identity of a button: it supplies the label, the UButtonAction classes that
	 * run when the button is clicked, and the payload classes those actions expect to be handed. Set it
	 * here for a button placed by hand, or call Initialize From Button Tag on a button built at runtime
	 * from a Context Menu Payload's list.
	 *
	 * Leave it unset and the button is an ordinary button: it still clicks, still broadcasts
	 * On Context Clicked, and simply has no actions of its own to run.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Context Menu Button|Action", meta = (DisplayName = "Button Tag", Categories = "UIContext.Button"))
	FGameplayTag ButtonTag;

	/**
	 * Which Text Block the tag's Button Text is written into.
	 *
	 * Leave it empty and the button finds the text block itself, as long as there is exactly one beneath
	 * it - which is the usual shape. Name one explicitly when there are several, or when the label is not
	 * the one that should change. Blank Button Text in the settings never overwrites authored text, so a
	 * tag that only supplies actions leaves whatever the designer typed alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Action", meta = (DisplayName = "Button Text Widget", GetOptions = "GetAnimTargetWidgetOptions"))
	FName ButtonTextWidgetName;

	/**
	 * The payload classes this button's actions are handed, in order, resolved from Button Tag.
	 *
	 * Filled in from the settings, not authored. Read it to know what a card carrying this button has to
	 * supply; Action Status below says the same thing in words, including which ones are actually present.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Context Menu Button|Action")
	TArray<TSubclassOf<UContextButtonPayload>> ExpectedPayloads;

	/**
	 * What Button Tag resolves to, in words: the label, the actions that will run, the payloads they
	 * expect, and - once the button is live - which of those the owning card actually carries.
	 *
	 * This exists so authoring a context menu does not mean keeping Project Settings open in a second
	 * window. Transient and editor-facing; nothing reads it back.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Context Menu Button|Action", meta = (DisplayName = "Action Status", MultiLine = true))
	FString ButtonActionStatus;

	/**
	 * Points this button at a Button Actions entry and resolves it: label, actions, expected payloads.
	 *
	 * What a card building its buttons from a Context Menu Payload's tag list calls on each one. Safe to
	 * call again with a different tag; the previous tag's action instances are dropped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Action" , meta = (Categories = "UIContext.Button"))
	void InitializeFromButtonTag(UPARAM(DisplayName = "Button Tag") FGameplayTag InButtonTag);

	/** Re-reads the current Button Tag from the settings. Call after editing Button Actions at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Action")
	void RefreshButtonAction();

	/**
	 * The payload instances this button's actions will be handed, index-for-index with Expected Payloads.
	 *
	 * Found on the owning Info Card by class, so a card that was given a UContextButtonPayloadText
	 * satisfies an expectation of that class. An expectation nothing on the card matches comes back null
	 * rather than being skipped, so an action can always trust that slot N is its Nth expected class.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Action")
	TArray<UContextButtonPayload*> ResolveActionPayloads() const;

	/**
	 * What a tag's Button Actions entry supplies, as readable text. Shared with UContextMenuPayload, which
	 * shows the same summary for every tag in its list - the point where the payloads are actually chosen.
	 */
	static FString DescribeButtonTag(const FGameplayTag& Tag);

	/** The settings entry for a tag, or null if the settings have none. */
	static const FContextMenuButtonData* FindButtonData(const FGameplayTag& Tag);

	// ------------------------------------------------------------------
	// Animation
	// ------------------------------------------------------------------

	/**
	 * The animations this button plays as it appears.
	 *
	 * Each entry points at a Widget Animation asset - which holds the layers, the timing and the curves -
	 * and decides the few things that are about THIS use of it: whether it loops, whether it also plays
	 * backwards as the exit, whether it runs inverted, and which widget it moves.
	 *
	 * Target Widget may name the button itself, anything beneath it, or nothing at all - leaving it empty
	 * animates the button, same as on an animatable User Widget. The one thing that cannot be left empty
	 * is the target of a Material Scalar / Material Vector layer, which needs an Image to find a material
	 * instance on; Animation Status below says when one was skipped for that.
	 *
	 * Give an entry an Anim ID to fire it from Blueprint later with Play Widget Animation.
	 *
	 * Animations compose over whatever the target was authored as and over whatever its Design Target has
	 * applied - opacity and scale multiply, translation and angle add - so Scale / Slide / Rotate / Pulse
	 * on a Design Target widget are fine. Fade is the one to watch: if that target's design also drives
	 * Opacity, both are writing the same channel and the animation wins while it plays.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Animation", meta = (DisplayName = "Anim In", TitleProperty = "Animation"))
	TArray<FWidgetAnimEntry> AnimIn;

	/** Played as the button closes. Anything in Anim In marked Mirror In Out is included automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Animation", meta = (DisplayName = "Anim Out", TitleProperty = "Animation"))
	TArray<FWidgetAnimEntry> AnimOut;

	/** Whether Anim In plays by itself the first time the button is built at runtime. Off if something else drives it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Animation", meta = (DisplayName = "Play Anim In On Construct"))
	bool bPlayAnimInOnConstruct = true;

	/**
	 * Runs the animations in the UMG designer so they can be judged without compiling and running. Set
	 * Mode to Loop In and Out and the entrance and exit cycle in the viewport.
	 *
	 * Editor only, and non-destructive: the preview composes over a baseline taken when the designer built
	 * it, and setting Mode back to Off restores every animated widget exactly.
	 *
	 * Show Only Inner Properties is what puts Mode, Playback Speed and the rest directly in the Animation
	 * category instead of behind an expander. FContextMenuButtonDetails used to hide this property and
	 * re-add its children by hand to get the same result, which meant the editor module had to know about
	 * a purely cosmetic decision belonging to the property itself - and meant nothing else could have a
	 * designer preview without its own customization.
	 */
	UPROPERTY(EditAnywhere, Category = "Context Menu Button|Animation", meta = (DisplayName = "Designer Preview", ShowOnlyInnerProperties))
	FWidgetAnimPreviewSettings DesignerAnimPreview;

	/**
	 * What the last animation rebuild found, written by the button itself: how many layers each stack
	 * ended up with and how many were dropped for having no Target Widget. Read this first when an
	 * animation authored here isn't doing anything. Transient and editor-only; nothing reads it back.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "Context Menu Button|Animation", meta = (DisplayName = "Animation Status", MultiLine = true))
	FString ButtonAnimationStatus;

	/** Plays Anim In now. Safe to call repeatedly; each call restarts it. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Animation")
	void PlayButtonAnimIn();

	/** Plays Anim Out now. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Animation")
	void PlayButtonAnimOut();

	/** Flattens Anim In / Anim Out into the stacks that actually play. Called for you on every rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Animation")
	void RebuildEffectiveButtonAnimations();

	/** Backs the Target Widget dropdown on every animation entry. Must stay a UFUNCTION for GetOptions. */
	UFUNCTION()
	TArray<FString> GetAnimTargetWidgetOptions() const;

	// IWidgetAnimHost
	virtual void PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying) override;
	virtual void StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart) override;
	virtual bool AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const override;
	virtual bool SetWidgetAnimationValues(const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying) override;
	virtual FVector4 GetWidgetAnimationValues(const FWidgetAnimValueId& Id, bool bTargetValue) const override;

	// IWidgetAnimDesignerPreviewHost
	virtual UWidget* GetPreviewOwnerWidget() override { return this; }
	virtual FWidgetAnimPreviewSettings& GetPreviewSettings() override { return DesignerAnimPreview; }
	virtual FWidgetAnimPreviewState& GetPreviewState() override { return DesignerAnimPreviewState; }
	virtual FWidgetAnimBaselineSet& GetPreviewBaselines() override { return ButtonAnimBaselines; }
	virtual const TArray<FWidgetAnimSettings>& GetPreviewAnimIn() const override { return FlattenedAnimIn; }
	virtual const TArray<FWidgetAnimSettings>& GetPreviewAnimOut() const override { return FlattenedAnimOut; }
	virtual void RebuildPreviewAnimations() override { RebuildEffectiveButtonAnimations(); }

	/**
	 * The button already runs one active timer for its runtime playback, and TickButtonAnimations
	 * branches to the preview at design time. Overridden so the harness reuses that timer rather than
	 * registering a second one that would tick the same preview twice a frame.
	 */
	virtual void EnsurePreviewTimer() override { StartButtonAnimTimer(); }

	// ------------------------------------------------------------------
	// Instance Override Configurations
	// ------------------------------------------------------------------

	/**
	 * Lets this button's Shape and Design be re-authored from outside, without touching the button itself.
	 *
	 * For PER-PLACEMENT values (the same widget dropped into several other widgets, each looking different),
	 * declare a variable of type Context Menu Button Instance Overrides on the widget that directly contains
	 * this button and mark it Instance Editable. The button finds it by itself - no node to wire up, no box
	 * to tick - and seeds it from this button's authored values. That variable then wins over Instance
	 * Overrides below. The variable has to live on the containing widget because UMG never exposes a child
	 * widget's internal widget tree to whatever places it.
	 *
	 * Declaring the variable IS the switch. There used to be an "Is Instance Editable" checkbox here as
	 * well, which meant a placement could have a perfectly good override variable sitting on it and still
	 * do nothing because a second, unrelated box on a widget two levels down was unticked. Kept only so
	 * existing assets load; it no longer gates anything.
	 */
	UPROPERTY()
	bool bIsInstanceEditable_DEPRECATED = false;

	/**
	 * What the last resolve actually found, written by the button itself. Read this first when a placement
	 * isn't picking up its override: it says which of the two sources won and what was in it, which is the
	 * difference between "the variable isn't being found" and "the variable is found but nothing is switched
	 * on in it". Transient and editor-only; nothing reads it back.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Context Menu Button|Instance Overrides")
	FString InstanceOverrideStatus;

	/**
	 * This button's own overrides, used only when no overrides variable is found on any widget above it.
	 *
	 * Not shown in the Details panel: editing it here would be editing the button's authored look twice,
	 * once in Shape and Design and once in a copy of them, and the two drift. Per-placement values belong
	 * on a variable on the containing widget, which is what Add Overrides Variable creates. This stays for
	 * Blueprint and for assets that already set it.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Context Menu Button|Instance Overrides")
	FContextMenuButtonInstanceOverrides InstanceOverrides;

	/**
	 * An overrides struct shaped to this button: both halves switched off, every channel untoggled, and one
	 * Design Target entry per target the button has. Use it to fill an overrides variable from Blueprint;
	 * you don't need it for the normal flow.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Menu Button|Instance Overrides")
	FContextMenuButtonInstanceOverrides GetAuthoredInstanceOverrides() const;

	/**
	 * Applies an overrides struct to this button immediately. Only needed for overrides built at runtime; the
	 * designer flow (Is Instance Editable, plus an optional variable on the containing widget) is applied
	 * automatically and needs no call. Nothing is copied over the button's authored values - the patch is
	 * kept to one side and merged in as the shape and design are read - so calling this repeatedly, or
	 * switching a channel back off, does exactly what you'd expect.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Instance Overrides")
	void ApplyInstanceOverrides(UPARAM(ref) FContextMenuButtonInstanceOverrides& Overrides);

	/** Drops any patch pushed in by Apply Instance Overrides and hands control back to the designer flow. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|Instance Overrides")
	void ClearInstanceOverrides();

	/** Backs the Target Widget dropdown on FContextMenuDesignTarget: Self (this button) plus every widget beneath it. Must stay a UFUNCTION for GetOptions. */
	UFUNCTION()
	TArray<FString> GetColorTargetWidgetOptions() const;

	/**
	 * The name a target stores to mean this button itself, whatever it is called - so renaming or copying a button never
	 * breaks a target aimed at it. A target holding the button's own name works too, as it always has.
	 */
	static const FName SelfTargetName;

	/** Whether Name means this button itself: Self, or its own name. */
	bool IsSelfTargetName(FName Name) const;

	/** Where the last press landed on the button, as a fraction of it: (0, 0) top left, (1, 1) bottom right. */
	UFUNCTION(BlueprintPure, Category = "Context Menu Button|State")
	FVector2D GetLastPressPosition() const;

	/** Where the last press landed on screen. False for a key or gamepad press, which has none. */
	bool GetLastPressScreenPosition(FVector2D& OutScreenPosition) const;

	/**
	 * Lays out every Shape Target's Look for the vector shape it aims at, and Background Look for Background Shape, and
	 * refreshes the mirrors that pick which rows show. Run on every edit; call it after changing targets from code.
	 */
	void RefreshShapeTargetLooks();

	// ------------------------------------------------------------------
	// Cursor Configurations
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Cursor")
	EContextMenuButtonSettingSource CursorSettingSource = EContextMenuButtonSettingSource::UseProjectSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Cursor", meta = (EditCondition = "CursorSettingSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	TEnumAsByte<EMouseCursor::Type> ButtonHoverCursor = EMouseCursor::Default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Cursor", meta = (EditCondition = "CursorSettingSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	TEnumAsByte<EMouseCursor::Type> ButtonPressCursor = EMouseCursor::Default;

	/////////////////////////////////////////////////////////////////////////////////////////////////////
	/// Native button events
	/////////////////////////////////////////////////////////////////////////////////////////////////////
	
	/**
	 * Fires when this button's action executes.
	 * @param bShouldDestroyCard  Whether the owning Info Card will be closed after this event.
	 * @param bEnabling  The enabled state this button is BECOMING because of this click, not the state it currently was in. Always false if Button Can Be Enabled is off. Hold buttons flip once per press; repeats while held report the same value. Not the same as disabled (Is Enabled).
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnContextClicked, bool, bShouldDestroyCard, bool, bEnabling);
	/**
	 * Fires whenever this button's enabled state actually changes, whatever caused it: a click, Set Enabled State,
	 * another button in its group being enabled, or the button being disabled.
	 * @param bIsEnabled  The enabled state the button now has.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnContextEnabledStateChanged, bool, bIsEnabled);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnContextPressDurationProgress, float, Progress);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnContextPressDurationCancelled);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnContextPressDurationCompleted);

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button")
	FOnContextClicked OnContextClicked;

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button")
	FOnContextEnabledStateChanged    OnContextEnabledStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button")
	FOnContextPressDurationProgress  OnContextPressDurationProgress;

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button")
	FOnContextPressDurationCancelled OnContextPressDurationCancelled;

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button")
	FOnContextPressDurationCompleted OnContextPressDurationCompleted;

	/**
	 * Whether this button broadcasts through matching Event Dispatchers on the widgets above it.
	 *
	 * THE PROBLEM THIS SOLVES
	 * A button's OnContextClicked shows up in the Details panel of the Widget Blueprint that CONTAINS the
	 * button, and nowhere else. Wrap the button in a reusable widget and every placement of that widget is
	 * stuck: UMG never exposes a child widget's internal tree to whatever drops it on a canvas, so the
	 * placement cannot see the button, let alone bind it. Normally the wrapper then has to declare its own
	 * dispatcher, bind the button in Construct and re-broadcast - boilerplate repeated in every wrapper.
	 *
	 * HOW TO USE IT
	 * Add an Event Dispatcher to the widget that contains the button, named On{ButtonName}{Event}. A button
	 * called PillButton gets "OnPillButtonClicked", "OnPillButtonHovered",
	 * "OnPillButtonEnabledStateChanged", "OnPillButtonPressCompleted". That is the whole setup. The button
	 * finds it by name and broadcasts through it, so selecting that widget in a parent designer shows the
	 * dispatcher under Events with a + that drops a node straight into the PARENT's graph.
	 *
	 * The order matters, not just the words: On, then the button's name, then the event. "OnPillButtonClicked"
	 * binds; "OnClickedPillButton" and "PillButtonClicked" do not. And if the containing widget has
	 * exactly one On...{Event} dispatcher and none of them names a button, that one is used - the same "one
	 * candidate is unambiguous" rule the instance overrides use.
	 *
	 * SIGNATURES
	 * Parameters are OPTIONAL. A dispatcher with none at all works for every event, which is what most
	 * placements want - they care that the button was clicked, not what it reported. Add them only if you
	 * want the values: Clicked offers (bool bShouldDestroyCard, bool bEnabling), Enabled State Changed and
	 * Hover Changed offer a single bool. Take all of them or none; one bool where an event offers two is
	 * rejected rather than guessed at. A dispatcher that is named correctly but shaped wrongly is reported
	 * in Event Dispatcher Bindings below rather than silently ignored.
	 *
	 * This never replaces OnContextClicked - both fire, so anything already bound keeps working. Always on,
	 * and always reaches every matching dispatcher up the chain: a reusable wrapper handling its own
	 * dispatcher for an event never shields the screen that placed it from also having one.
	 */
	/**
	 * What the last dispatcher resolve found. Not shown in the Details panel - a raw list of bindings is
	 * implementation detail nobody authoring a button needs to see; a mismatch is what matters, and that
	 * shows up through the Add Matching Dispatcher tooltip instead.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Context Menu Button|Events")
	FString EventDispatcherBindingStatus;

	/////////////////////////////////////////////////////////////////////////////////////////////////////
	///
	/////////////////////////////////////////////////////////////////////////////////////////////////////
	
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button")
	UInfoCardWidget* GetOwningCard();

	UFUNCTION(BlueprintCallable, Category = "Context Menu Button")
	void SetOwningCard(UInfoCardWidget* InCard);

	// ------------------------------------------------------------------
	// State Getters
	// ------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Menu Button|State")
	float GetElapsedPressTime() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Menu Button|State")
	float GetPressDurationProgress() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Menu Button|State")
	float GetElapsedHoldTime() const;

	/**
	 * How many times the button's action has gone off - every click, every repeat of a Hold button - since it was made. What a
	 * state machine's built-in Clicked input watches.
	 */
	uint32 GetActionCount() const { return ActionCount; }

	/** True while this button is enabled. Always false if Button Can Be Enabled is off or the button is disabled (Is Enabled false). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Menu Button|State")
	bool GetEnabledState() const;

	/**
	 * Sets the enabled state without firing On Context Menu Button Clicked. Ignored (stays false) while Button Can
	 * Be Enabled is off or the button is disabled. Not the same as Set Is Enabled.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|State")
	void SetEnabledState(bool bNewEnabledState);

	/**
	 * Every button in this button's enable group (see Unenable All Other Buttons In Container When Enabled and Enable
	 * Group), including this one, in no particular order. Just this button if it has no group-mates; empty if it can't be
	 * enabled. Use it to find which button in a tab row is enabled.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|State")
	TArray<UContextMenuButton*> GetEnableGroupButtons();

	/** Gets the auto-assigned index of this button within its enable group. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Menu Button|State")
	int32 GetGroupIndex() const;

	/** Returns all currently enabled buttons in this button's group. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Menu Button|State")
	TArray<UContextMenuButton*> GetEnabledGroupButtons();

	/** Returns the indices of all currently enabled buttons in this button's group. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Menu Button|State")
	TArray<int32> GetEnabledGroupIndices();

	/** Sets the enabled state of a specific button in this group by its group index. */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|State")
	void SetGroupButtonEnabledStateByIndex(int32 TargetIndex, bool bNewEnabledState);

	virtual void PostLoad() override;
	virtual void BeginDestroy() override;

	/**
	 * Resets the state-sound latches when the button comes back into view, so a set with Reassert On
	 * Repeat off sounds again after a menu collapse rather than staying latched on whatever state it was
	 * in when it disappeared.
	 */
	virtual void SetVisibility(ESlateVisibility InVisibility) override;

	/**
	 * Runs after the Slate widget exists, which is later than SynchronizeProperties. In a parent designer the
	 * placement's override values can land on the owning widget after this button's tree was first built, so
	 * the resolve is repeated here and the preview redrawn - otherwise the first pass wins with nothing in it
	 * and the designer keeps showing the authoring widget's look.
	 */
	virtual void OnWidgetRebuilt() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void OnCreationFromPalette() override;

	/** Whether this button was dropped from the Palette and still wants its default content built. */
	bool HasPendingDefaultContent() const { return bPendingDefaultContent; }

	/**
	 * Takes the pending request: true if there was one (it is cleared). Called before the build's transaction opens, so undoing
	 * the build leaves the button empty instead of asking for it again.
	 */
	bool ConsumePendingDefaultContent();

	/**
	 * Builds the default content and hands back every part made (for the Widget Blueprint's GUIDs). Called by the editor module
	 * once the button has really been placed - never during a drag. Nothing when the button already has content.
	 */
	void BuildDefaultContentForEditor(TArray<UWidget*>& OutParts);

	/**
	 * Hides the button's content from the Hierarchy (moved, with everything inside it, into PartsTree) or lists it again (moved
	 * back into the tree the button is in). The same widgets either way - renamed only if their name is taken where they go, which
	 * targets still follow (see FindWidgetInButtonTree). Hands back every widget moved. Call inside a transaction, then make the
	 * Widget Blueprint's GUIDs match (the editor module does both). True when it moved anything.
	 */
	bool SetPartsHidden(bool bHide, TArray<UWidget*>& OutMovedParts);

	/** Whether the Show / Hide Parts choice was made for this button (a fresh build hides; older buttons are hidden once). */
	bool IsPartsVisibilityDecided() const { return bPartsVisibilityDecided; }
	void MarkPartsVisibilityDecided() { bPartsVisibilityDecided = true; }

	// ---- Button preview toolbar (designer only) ----------------------------------------------------

	/**
	 * Shows State in the designer, blending there from what the preview shows now with the button's own speeds, curves and
	 * delays - exactly as the button changes state at runtime - or at once (bAnimate false). From then on the toolbar's state is
	 * the preview in force instead of Show Preview, until StopDesignerPreviewState. Designer preview copies only.
	 */
	void PlayDesignerPreviewState(EContextMenuDesignPreview State, bool bAnimate);

	/** Gives the preview back to Show Preview. */
	void StopDesignerPreviewState();

	/** The state the preview shows: the toolbar's, else Show Preview's (or the placement's). */
	EContextMenuDesignPreview GetEffectiveDesignPreview() const;

	/** Whether a toolbar transition is running. */
	bool IsPlayingDesignerPreviewState() const { return bIsDesignerStateTimerActive; }

	/**
	 * The values the toolbar's stepper shows for a button that steps through some (a Choice Button's options or numbers, a
	 * Segmented Choice's choices): how many, the one shown, its text, and showing another - animated as at runtime. A button
	 * with none returns 0 and the stepper hides. Designer preview copies only; the authored value is never changed.
	 */
	virtual int32 GetDesignerPreviewValueCount() const { return 0; }
	virtual int32 GetDesignerPreviewValueIndex() const { return 0; }
	virtual FText GetDesignerPreviewValueText() const { return FText::GetEmpty(); }
	virtual void SetDesignerPreviewValueIndex(int32 Index, bool bAnimate) {}

	void SyncMaterialParametersEditor();
	void SyncMaterialParameters(FContextMenuWidgetDesign& Design, UMaterialInterface* Material);
#endif

protected:
	friend class SContextMenuSlateButton;
	virtual void SynchronizeProperties() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

	void BindSlateFocusChanging();
	void UnbindSlateFocusChanging();
	void HandleSlateFocusChanging(const FFocusEvent& FocusEvent, const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget);

	UFUNCTION()
	void HandleHovered();

	UFUNCTION()
	void HandleUnhovered();

	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandlePressed();

	UFUNCTION()
	void HandleReleased();

	UFUNCTION()
	void ExecuteButtonAction();

	UFUNCTION()
	void StartHoldFrequencyTimer();

	UFUNCTION()
	void OnHoldTimerTick();

	UFUNCTION()
	void OnCooldownFinished();

	void BeginPressDuration();
	void StopPressDuration();
	bool TickPressDuration(uint32 Serial);
	UInfoCardWidget* ResolveOwningCard() const;

	// Visuals API
	void UpdateButtonState(bool bInstant = false);
	void StartVisualUpdateTimer();
	EActiveTimerReturnType TickVisuals(double InCurrentTime, float InDeltaTime);

	/** Moves every visual one step toward the current state's targets (or snaps if bInstant). Returns true once everything has arrived. */
	bool ApplyTransitions(float DeltaTime, bool bInstant);

	/** Sends a target's current Shape and Transform to the Slate button, which paints the brush with them and hit tests against them. Self-targeting only; see ApplyShapeToTargetWidget for the general case. */
	void PushButtonShapeToSlate(const FShapeBoxShape& Shape, const FWidgetTransform& Transform);

	/** Sends the resolved state (and who owns the brush) to the Slate button, which picks the style brush from it. */
	void PushVisualStateToSlate(EContextMenuState ShapeState);

	/** The style brush a resolved shape state should paint. Enabled has no brush of its own, so it maps to Hovered or Normal. */
	EContextMenuState GetBrushStateForShapeState(EContextMenuState ShapeState) const;

	/**
	 * True while a Design Target drives this button's own Tint, Outline Tint, Brush Texture or Material, i.e.
	 * the design owns the button's look and the style's per-state brushes should stay out of its way.
	 */
	bool DoesDesignDriveButtonBrush() const;

	/** The project preset a Shape Target is using, or nullptr if its Shape Source is Custom or the named preset no longer exists. */
	const FContextMenuShapePreset* FindActiveShapePreset(const FContextMenuShapeTarget& Target) const;

	/**
	 * The shape one target should show in a state: the target's own values, or its Shape Preset's, with any
	 * active instance patch merged over the top. The single place that resolution happens, so the runtime and
	 * the designer preview can't disagree about it. By value rather than by reference, because the patched
	 * result doesn't exist anywhere to point at - which is the whole reason a placement can nudge a shape
	 * without the button having to overwrite its own authored values.
	 */
	FContextMenuShapeStateSettings ResolveShapeForState(const FContextMenuShapeTarget& Authored, EContextMenuState State) const;

	/**
	 * The design one target should show in a state: the target's own values, or its Design Preset's, with any
	 * active instance patch merged over the top. The single place that resolution happens, so the runtime and
	 * the designer preview can't disagree about it.
	 */
	FContextMenuWidgetDesign ResolveDesignForState(const FContextMenuDesignTarget& Authored, EContextMenuState State) const;

	void InitializeRuntimeShapeTargets();
	void InitializeRuntimeDesignTargets();

	/**
	 * After targets were changed at runtime (Target ID API): the runtime entries of the changed ones are made to re-read their goal,
	 * those switched on start from where their widget is now, and the button blends (or snaps) to them. The designer repaints its preview.
	 */
	void ApplyChangedTargets(const TArray<int32>& ChangedShapes, const TArray<int32>& ChangedDesigns, const TArray<int32>& ActivatedShapes, const TArray<int32>& ActivatedDesigns, bool bInstant);

	// Sound
	/** Plays whatever the Sound Sets have for this state, honoring each set's delay, repeat limit and first-state suppression. */
	void HandleStateSounds(EContextMenuState State, bool bSuppressPlayback);

	/** Plays every set's Clicked slot. Fired from the action, not from the press, so cooldowns and press durations are respected. */
	void PlayClickSounds();

	/** One slot from one set: applies the repeat limit, then either plays now or arms the set's delay timer. */
	void PlaySoundSlot(int32 SetIndex, const FUISoundSlot& SoundSlot);

	void CancelPendingSounds();

	// Instance overrides
	/** Fills in whichever channels aren't toggled on with the button's own values, and keeps the Shape Target and Design Target lists in step with the button's. */
	void SeedInstanceOverrides(FContextMenuButtonInstanceOverrides& Overrides) const;

	/** Makes a patch's material parameter list match the button's, keeping already-edited values by name while the channel is switched on. */
	static void SyncOverrideParameters(TArray<FContextMenuMaterialScalarValue>& Patch, const TArray<FContextMenuMaterialScalarValue>& Source, bool bKeepEditedValues);
	static void SyncOverrideParameters(TArray<FContextMenuMaterialVectorValue>& Patch, const TArray<FContextMenuMaterialVectorValue>& Source, bool bKeepEditedValues);

	/**
	 * The single FContextMenuButtonInstanceOverrides variable declared on a User Widget above this button, or
	 * null if none declares one. Walks outwards from the widget that authors the button. A variable whose name
	 * mentions this button's name wins when a widget declares more than one; otherwise more than one is
	 * ambiguous and none is used. OutOwnerWidget receives the widget that declares the variable.
	 */
	FContextMenuButtonInstanceOverrides* FindOwnerInstanceOverrides(FString* OutVariableName = nullptr, FString* OutDiagnostic = nullptr, UUserWidget** OutOwnerWidget = nullptr) const;

	/**
	 * Works out which patch is in force and caches it. Nothing is written onto the button's authored Shape or
	 * Design - the patch is merged in at read time by ResolveShapeForState/ResolveDesignForState - so this is
	 * safe to run on every rebuild and a parent edit can never be overwritten by a stale instance copy.
	 *
	 * Seed Source fills the channels nobody has switched on with the button's own values, which is what
	 * keeps a grayed-out override showing what the button actually looks like. Pass false from anything
	 * that polls: seeding WRITES to whatever struct was found, and doing that on a timer means repeatedly
	 * mutating an object that may well be a real asset rather than a throwaway designer preview.
	 */
	void ResolveInstanceOverrides(bool bSeedSource = true);

	/** Moves one shape target one step toward its state shape (or snaps). Returns true once every channel has arrived. */
	bool ApplyShapeTarget(FContextMenuRuntimeShapeTarget& Target, EContextMenuState State, float DeltaTime, bool bInstant);

	/** Moves one design target one step toward its state design (or snaps). Returns true once every driven channel has arrived. */
	bool ApplyDesignTarget(FContextMenuRuntimeDesignTarget& Target, EContextMenuState State, float DeltaTime, bool bInstant);

	/**
	 * Applies a fully-resolved shape to whichever widget a Shape Target points at. Self-targeting (Widget ==
	 * this) goes through the button's own mouse-hit-aware Slate brush; a Shape Box found by FindDrivenShapeBox
	 * takes both the Shape and the Transform; anything else takes the Transform alone, through its own Render
	 * Transform.
	 */
	void ApplyShapeToTargetWidget(UWidget* Widget, const FShapeBoxShape& Shape, const FWidgetTransform& Transform);

	/** The Shape Box a Shape Target drives: the widget itself if it is one, else its parent if that is one (a Shape Box only ever has one child). Null otherwise. */
	static UShapeBox* FindDrivenShapeBox(UWidget* Widget);

	/** The vector shape a target's Look is written onto: the target if it is a shape widget, the Background Shape for Self. Null otherwise. */
	UShapeWidget* GetLookWidget(UWidget* Widget) const;

	/** The kind of shape a target's Look is laid out for, found from the target in this button's tree. Null when it has no Look. */
	UClass* GetLookClass(const FContextMenuShapeTarget& Target);

	/** Makes (or drops) the Background Shape's widget to match Background Shape, and writes Background Look onto it. */
	void SyncBackgroundShape();

	/** The shape as authored, captured the first time a Look touches it (Background Look, for the Background Shape). */
	const FInstancedPropertyBag* FindLookBaseline(UShapeWidget* Shape);

	/** The settings a target's states change on its shape: those whose Look in any state differs from the shape as authored. */
	void GatherLookProperties(const FContextMenuShapeTarget& Authored, UShapeWidget* Shape, TArray<const FProperty*>& OutProperties);

	/** Writes the settings Properties of Look onto Shape at once (a preview, a snap); a setting Look lacks is taken from Fallback. */
	void WriteLook(UShapeWidget* Shape, const FInstancedPropertyBag& Look, const TArray<const FProperty*>& Properties, const FInstancedPropertyBag* Fallback = nullptr);

	/** Repaints whatever draws Shape: its own widget, or this button for the Background Shape. */
	void InvalidateLookWidget(UShapeWidget* Shape);

	/** OnContextPressDurationProgress, and the Progress Widget. */
	void BroadcastPressProgress(float Progress);

	/**
	 * Gamepad or keyboard navigation while this button is focused. True takes it (the focus stays here): a Choice steps its
	 * value on left and right. The base takes none.
	 */
	virtual bool HandleNavigation(EUINavigation Direction) { return false; }

	/** Whether HandleLayoutSizeChanged is wanted: a button whose parts follow the size it is laid out at. Off by default. */
	virtual bool WantsLayoutSizeChanges() const { return false; }

	/** Laid out (and drawn) at a new size - the slot made it bigger or smaller than it asks to be. Called on the next tick. */
	virtual void HandleLayoutSizeChanged(const FVector2D& NewSize) {}

	/**
	 * The state the button's designs now show changed (hovered - navigation focus included -, pressed, enabled, disabled, back
	 * to normal), or was set outright (bInstant: a rebuild, a refresh, the designer toolbar's pick without its blend). For parts
	 * that animate themselves rather than through targets: a Glass Button's glass. The base does nothing.
	 */
	virtual void OnVisualStateChanged(EContextMenuState NewState, bool bInstant) {}

	/**
	 * Builds what a ready-made button is made of - its children, Background Look, Shape and Design Targets. Called when the
	 * button is dragged in from the Palette (and, for a ready-made button left empty, when it is built at runtime). The plain
	 * Context Menu Button builds nothing.
	 */
	virtual void BuildDefaultContent() {}

	/** Whether a button with no content builds its default content when it is built at runtime. */
	virtual bool ShouldBuildDefaultContentWhenEmpty() const { return false; }

	/** Rebuilds Shape Status from the current Shape Targets. */
	void RebuildShapeTargetStatus();

	/**
	 * The vector shape a Design Target's channels act on: a shape widget itself, or a button's Background Shape. On a shape,
	 * Tint is its fill's color, Outline Tint its stroke's, and Brush Texture / Material its outline's image when the outline is
	 * painted with one, otherwise its fill's.
	 */
	static UShapeWidget* GetDesignShape(UWidget* Widget);

	static FLinearColor ExtractWidgetColor(UWidget* Widget);
	static void ApplyColorToWidget(UWidget* Widget, const FLinearColor& Color);

	/** The color of a widget's brush outline (rounded-box brushes with an outline width). Used to capture a baseline. Transparent if the widget has no such brush. */
	static FLinearColor ExtractWidgetOutlineTint(UWidget* Widget);

	/** Colors the outline of a widget's brush directly, since it isn't multiplied by Tint the way the brush's fill is. Image, Border, and (uniformly across every native state brush) this button itself; does nothing on anything else. */
	static void ApplyOutlineTintToWidget(UWidget* Widget, const FLinearColor& Color);

	/** Swaps the brush texture on an Image or Border, keeping its authored draw settings. Does nothing for other widget types. */
	static void ApplyBrushTextureToWidget(UWidget* Widget, UTexture2D* Texture);

	/** Swaps the material on a supported widget. */
	static void ApplyMaterialToWidget(UWidget* Widget, UMaterialInterface* Material);

	/** The widget's Dynamic Material Instance, created on demand. Null unless its brush already uses a material. */
	static UMaterialInstanceDynamic* ResolveWidgetMaterial(UWidget* Widget);

	/** Whether a target touches the button's own brush at all, in any state. Coarse on purpose: the Slate button needs one answer, not a per-frame one. */
	static bool DoesTargetDriveBrush(const FContextMenuDesignTarget& Target);

	/** The texture currently on the widget's brush, or null if its brush isn't a texture. Used to capture a baseline. */
	static UTexture2D* ExtractWidgetBrushTexture(UWidget* Widget);

	/** The SVG a shape (its Image paint) or an SVG Image shows, or null. Used to capture a baseline. */
	static UWidgetSVG* ExtractWidgetSVG(UWidget* Widget);

	/**
	 * Shows SVG on a design target: a shape's Image paint (Image Source SVG), an SVG Image's SVG, or - on an Image, Border
	 * or Button - a brush of the SVG drawn at the widget's size, kept sharp by TickTargetSVGs. Nothing for other widgets.
	 */
	void ShowSVGOnWidget(UWidget* Widget, UWidgetSVG* SVG);

	/** An Image, Border or Button stops showing an SVG through a drawn brush: what it had before is put back. */
	void StopSVGOnWidget(UWidget* Widget);

	/** Pushes Entry's drawing for Widget's current size into it, when the drawing has changed. */
	static void PushTargetSVG(UWidget* Widget, FContextMenuTargetSVG& Entry);

	/** Keeps every drawn-brush SVG at its widget's size; runs while any is shown. */
	EActiveTimerReturnType TickTargetSVGs(double InCurrentTime, float InDeltaTime);
	void StartTargetSVGTimer();

	/** Image, Border and Button targets showing an SVG through a drawn brush, by widget. */
	TMap<TWeakObjectPtr<UWidget>, FContextMenuTargetSVG> TargetSVGs;
	bool bIsTargetSVGTimerActive = false;

	/** The material currently on the widget, or null if it isn't using one. Used to capture a baseline. */
	static UMaterialInterface* ExtractWidgetMaterial(UWidget* Widget);

#if WITH_EDITOR
	/** Applies every Design Target using the global ShowPreview property, and restores them when set back to None. Design time only. */
	void ApplyDesignPreviews();

	/** Puts every previewed widget back the way it was authored. */
	void ClearDesignPreviews();

	/** What a previewed widget looked like before the preview touched it, so it can be restored. */
	struct FContextMenuDesignPreviewBaseline
	{
		TWeakObjectPtr<UWidget> Widget;
		FLinearColor Tint = FLinearColor::White;
		FLinearColor OutlineTint = FLinearColor::White;
		float Opacity = 1.0f;
		TWeakObjectPtr<UTexture2D> BrushTexture;
		TWeakObjectPtr<UMaterialInterface> Material;
		TWeakObjectPtr<UWidgetSVG> SVG;
		bool bIsButton = false;
		FButtonStyle ButtonStyle;
	};

	TArray<FContextMenuDesignPreviewBaseline> DesignPreviewBaselines;

	/**
	 * Keeps the designer preview honest for a PLACEMENT of this button.
	 *
	 * The values a placement overrides live on the widget that contains the button, and they can be edited
	 * from a parent designer long after this button's tree was built - editing a nested widget's exposed
	 * variable does not reliably re-synchronize the widgets inside it. SynchronizeProperties and
	 * OnWidgetRebuilt both re-resolve, but neither is guaranteed to run again when only that variable
	 * changed, and when neither does the preview keeps showing the authoring widget's look while the game
	 * shows the placement's - the exact "overrides work in game but not in the designer" split.
	 *
	 * So the button watches instead: a cheap editor-only active timer re-resolves, compares the resolved
	 * patch against the one that was last painted, and repaints only when it actually differs. Costs
	 * nothing in a cooked build (the whole thing is compiled out) and nothing in the editor beyond a
	 * struct comparison a few times a second.
	 */
	void StartDesignPreviewWatcher();
	EActiveTimerReturnType TickDesignPreviewWatcher(double InCurrentTime, float InDeltaTime);

	/** True if the resolved patch, or the preview in force (the button's or the placement's), differs from what was last painted. */
	bool HasResolvedOverridesChanged() const;

	/** Records the resolved patch as painted, so the next comparison has something to compare against. */
	void CacheResolvedOverridesSnapshot();

	bool bIsDesignPreviewWatcherActive = false;

	/** Whether the designer animation preview should be running right now. */
	bool ShouldRunDesignerAnimPreview() const;

	/** The toolbar's state is in force (see PlayDesignerPreviewState), and which it is. */
	bool bDesignerPreviewDriven = false;
	EContextMenuDesignPreview DesignerPreviewState = EContextMenuDesignPreview::Normal;

	/** A toolbar transition is blending; each new one bumps the serial, so a stale timer stops itself. */
	bool bIsDesignerStateTimerActive = false;
	int32 DesignerStateSerial = 0;

	EActiveTimerReturnType TickDesignerPreviewState(double InCurrentTime, float InDeltaTime, int32 Serial);

	/** The transition has arrived: the preview handed back to ApplyDesignPreviews, at the state reached. */
	void FinishDesignerPreviewState();

	/** Puts every Design Target's widget back as it was before the runtime targets wrote to it. */
	void RestoreRuntimeDesignBaselines();
#endif

#if WITH_EDITORONLY_DATA
	/** Dropped from the Palette, its default content not built yet. See OnCreationFromPalette. */
	UPROPERTY()
	bool bPendingDefaultContent = false;

	/** See IsPartsVisibilityDecided. */
	UPROPERTY()
	bool bPartsVisibilityDecided = false;

	UPROPERTY(Transient, DuplicateTransient)
	bool bHasPaintedDesignPreviewOnce = false;

	UPROPERTY(Transient, DuplicateTransient)
	EContextMenuDesignPreview LastPaintedPreview = EContextMenuDesignPreview::None;

	UPROPERTY(Transient, DuplicateTransient)
	TArray<FContextMenuShapeTargetOverride> LastPaintedShapePatch;

	UPROPERTY(Transient, DuplicateTransient)
	TArray<FContextMenuDesignTargetOverride> LastPaintedDesignPatch;
#endif

	/**
	 * Outside the WITH_EDITOR block the rest of the design-preview members sit in, because
	 * GetPreviewState() is part of an interface that is compiled in every configuration. Transient and
	 * inert at runtime, so it costs a struct nobody reads.
	 */
	UPROPERTY(Transient, DuplicateTransient)
	FWidgetAnimPreviewState DesignerAnimPreviewState;

	// Animation
	/** Puts every animated widget back as authored, then re-reads the poses the animation composes over. */
	void RecaptureButtonAnimBaselines();

	/** Registers the Slate active timer that drives the animations, if it isn't already running. */
	void StartButtonAnimTimer();

	EActiveTimerReturnType TickButtonAnimations(double InCurrentTime, float InDeltaTime);

	/** The widget an entry's Target Widget name refers to. Null for an entry that named nothing. */
	UWidget* ResolveAnimTarget(FName TargetWidgetName) const;

	/** Anim In / Anim Out with Mirror In Out resolved: what actually plays. */
	UPROPERTY(Transient, DuplicateTransient)
	TArray<FWidgetAnimEntry> EffectiveInEntries;

	UPROPERTY(Transient, DuplicateTransient)
	TArray<FWidgetAnimEntry> EffectiveOutEntries;

	/** The same two, flattened to settings. Only the designer preview harness needs them. */
	UPROPERTY(Transient, DuplicateTransient)
	TArray<FWidgetAnimSettings> FlattenedAnimIn;

	UPROPERTY(Transient, DuplicateTransient)
	TArray<FWidgetAnimSettings> FlattenedAnimOut;

	UPROPERTY(Transient, DuplicateTransient)
	FWidgetAnimPlayer ButtonAnimPlayer;

	UPROPERTY(Transient, DuplicateTransient)
	FWidgetAnimBaselineSet ButtonAnimBaselines;

	UPROPERTY(Transient, DuplicateTransient)
	bool bHasPlayedAnimInOnce = false;

	bool bIsButtonAnimTimerActive = false;

	/**
	 * The button's tracks at rest with only Value Inputs to follow - a tilt toward the pointer that has caught up: the
	 * timer then only reads the inputs each frame, and works a frame out only when a read moves a value (the player's
	 * Revision goes up). bButtonAnimsHeldLastTick: they were at rest after the last tick too, so its pose is the one that
	 * stays. See UWidgetAnimationComponent::TickPlayer, which rests the same way.
	 */
	bool bButtonAnimsResting = false;
	bool bButtonAnimsHeldLastTick = false;
	uint32 ButtonAnimRestRevision = 0;

	/** Finds a widget in this button's tree by the name/path stored in FContextMenuColorTarget::TargetWidgetName. */
	UWidget* FindWidgetInButtonTree(FName WidgetPath);

	// Action
	/** Reads Button Tag out of the settings: label, action instances, expected payloads, status string. */
	void ApplyButtonData();

	/** Runs every resolved action, handing each the payloads Expected Payloads asked for. */
	void ExecuteTaggedActions();

	/** Writes the tag's Button Text into the resolved text block. Blank text leaves the widget alone. */
	void ApplyButtonTextFromData(const FText& ButtonText);

	/** The Text Block named by Button Text Widget, or the only one beneath this button when it is unset. */
	UTextBlock* ResolveButtonTextBlock();

	void RebuildButtonActionStatus();

	/**
	 * One instance per Actions entry on the tag, built once when the tag resolves rather than per click.
	 *
	 * An action is a UObject subclass a project writes, so it is allowed to hold state between presses -
	 * a confirm-twice action, a counter - and a Hold button firing ten times a second would otherwise
	 * churn ten objects a second for no reason.
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButtonAction>> ResolvedActions;

	/** Whether ApplyButtonData has run for the tag currently set. */
	bool bHasResolvedButtonData = false;

	/** Is Enabled as Slate sees it (includes a bound Is Enabled), falling back to the UObject flag before the widget is built. */
	bool IsWidgetEnabledForVisuals() const;

	/** State the shapes (bForDesign = false) or designs (true) should currently show, honoring Hover Shape/Design While Enabled. */
	EContextMenuState ResolveButtonState(bool bForDesign) const;

	/** Every enabled-state change goes through here: stores it, arms the re-hover latches on enabled -> un-enabled, refreshes visuals. */
	void CommitEnabledState(bool bNewEnabledState);

	/** Called (deferred) by SContextMenuSlateButton when Is Enabled changes. Clears the enabled state on disable and refreshes visuals. */
	void HandleSlateEnabledStateChanged();

	/** Copies the enabling settings into EnabledShape.bIsApplicable and each Design Target's bEnabledDesignApplicable for the details panel. */
	void SyncEditConditionMirrors();

	/**
	 * One Event Dispatcher on one widget above this button, already matched to an event and checked for a
	 * usable signature.
	 *
	 * Not a USTRUCT: it holds a raw FProperty, which is reflection data owned by the class rather than
	 * anything that can be serialized. The owner is weak because the chain above a button can be torn down
	 * without the button hearing about it.
	 */
	struct FContextMenuButtonDispatcherBinding
	{
		TWeakObjectPtr<UObject> Owner;
		FMulticastDelegateProperty* Property = nullptr;
		EContextMenuButtonEvent Event = EContextMenuButtonEvent::Clicked;

		/** 0, 1 or 2. Decided when the binding was matched, so broadcasting never has to re-inspect the signature. */
		int32 ParamCount = 0;
	};

	/**
	 * Walks the outer chain and matches Event Dispatchers by name. Cached: this is reflection over every
	 * widget above the button, which is not something to redo on every click.
	 */
	void ResolveEventDispatcherBindings();

	/** Broadcasts one event through every dispatcher matched to it, passing only the args its signature takes. */
	void BroadcastToOwnerDispatchers(EContextMenuButtonEvent Event, bool bArg0 = false, bool bArg1 = false);

	/** True if Signature is usable for an event taking MaxBoolParams: no parameters at all, or exactly that many bools. */
	static bool IsUsableDispatcherSignature(const UFunction* Signature, int32 MaxBoolParams, int32& OutParamCount);

	/** The word a dispatcher's name has to contain to be considered for this event. */
	static const TCHAR* GetDispatcherKeywordForEvent(EContextMenuButtonEvent Event);

	/** How many bool arguments this event offers. A dispatcher may always take fewer (zero). */
	static int32 GetDispatcherParamCountForEvent(EContextMenuButtonEvent Event);

	TArray<FContextMenuButtonDispatcherBinding> CachedDispatcherBindings;
	bool bHasResolvedDispatcherBindings = false;

	/** True when the button is in a state a player could see and interact with, for the sound latch reset. */
	static bool IsVisibilityInteractive(ESlateVisibility Visibility);

	/** What IsVisibilityInteractive said last time SetVisibility ran, so only the transition back resets. */
	bool bWasVisibilityInteractive = true;

	// Enable groups
	/** Adds this button to (or removes it from) EnableCapableButtons, depending on Button Can Be Enabled. Runtime only. Returns true if it was just added. */
	bool UpdateEnableGroupRegistration();
	void UnregisterFromEnableGroups();

	/** See GetEnableGroupButtons. */
	void GatherEnableGroupMembers(TArray<UContextMenuButton*>& OutMembers);

	/** True if any member (this button included) has Unenable All Other Buttons In Container When Enabled on. */
	static bool IsEnableGroupExclusive(const TArray<UContextMenuButton*>& Members);

	/** True if this button's group is exclusive and another member that has already been built is enabled. */
	bool IsAnotherExclusiveGroupMemberEnabled();

	/** Called when this button becomes enabled: un-enables its group-mates if the group is exclusive. */
	void EnforceEnableGroupExclusivity();

	/** Every live runtime button that can be enabled. Groups are resolved from this on demand, so nothing is stored per container. */
	static TArray<TWeakObjectPtr<UContextMenuButton>> EnableCapableButtons;

	/** Stops a Blueprint that re-enables a button from inside On Enabled State Changed from ping-ponging a group forever. */
	static bool bIsEnforcingEnableGroup;

	// The patch currently in force, resolved once per rebuild and merged in wherever the shape or design is
	// read. Nothing here is ever copied onto the button's own properties: an instance that stored a copy would
	// stop tracking the widget it came from, which is exactly what we're avoiding.
	UPROPERTY(Transient, DuplicateTransient)
	TArray<FContextMenuShapeTargetOverride> ActiveShapePatch;

	UPROPERTY(Transient, DuplicateTransient)
	TArray<FContextMenuDesignTargetOverride> ActiveDesignPatch;

	/** Set by an explicit Apply Instance Overrides call. The automatic pass then stands down, so a rebuild can't wipe what Blueprint pushed in. */
	UPROPERTY(Transient, DuplicateTransient)
	bool bInstanceOverridesAppliedManually = false;

	UPROPERTY(Transient, DuplicateTransient)
	FContextMenuButtonInstanceOverrides ManualInstanceOverrides;

	UPROPERTY(Transient)
	TWeakObjectPtr<UInfoCardWidget> OwningCard;

	/** The content's slot while the parts are hidden from the Hierarchy (see ArePartsHidden). */
	UPROPERTY(Instanced)
	TObjectPtr<UPanelSlot> HiddenContentSlot;

	/** The button's own tree the hidden parts live in (see ArePartsHidden). Made the first time they are hidden. */
	UPROPERTY(Instanced)
	TObjectPtr<UWidgetTree> PartsTree;

	/** What draws the Background Shape: a shape widget of that kind, in no tree, painted by the Slate button. */
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UShapeWidget> BackgroundShapeWidget;

	/** Each Look-driven shape as authored. See FindLookBaseline. */
	UPROPERTY(Transient, DuplicateTransient)
	TArray<FContextMenuLookBaseline> LookBaselines;

	/** The Progress Widget, found when a press starts. */
	TWeakObjectPtr<UWidget> CachedPressProgressWidget;

	UPROPERTY(Transient, DuplicateTransient)
	bool bHasBoundClickHandler = false;

	static constexpr int32 MaxOwnerSearchDepth = 64;

	FTimerHandle HoldFrequencyTimerHandle;
	FTimerHandle CooldownTimerHandle;
	FDelegateHandle SlateFocusChangingHandle;

	bool bIsPressDurationActive = false;
	double PressDurationStartTime = 0.0;
	uint32 PressDurationSerial = 0;

	/** See GetActionCount. */
	uint32 ActionCount = 0;

	UPROPERTY(Transient)
	float CurrentHoldFrequency = 0.0f;

	UPROPERTY(Transient)
	float ActivePressDuration = 0.0f;

	UPROPERTY(Transient)
	float HoldStartTime = 0.0f;

	UPROPERTY(Transient)
	bool bIsOnCooldown = false;

	// Visual Tracking State
	TWeakPtr<SContextMenuSlateButton> MyContextMenuSlateButton;

	bool bIsVisualTimerActive = false;

	/** True only while focus arrived by keyboard/gamepad navigation. Focus from a mouse click does not count as hover. */
	bool bHasNavigationFocus = false;

	/** The enabled state flips once per press; Hold repeats within the same press report the same value. */
	bool bHasToggledEnablingThisPress = false;

	/** Runtime enabled state. Seeded from bDefaultEnabledState the first time the button is built. */
	bool bRuntimeEnabledState = false;
	bool bHasInitializedEnabledState = false;

	/** The enabled state last reported through OnEnabledStateChanged (or seeded), so it only fires on real changes. */
	bool bLastReportedEnabledState = false;

	/** Set when the button is un-enabled while hovered and the matching While Enabled flag is off; cleared once the cursor leaves. */
	bool bHoverShapeLatched = false;
	bool bHoverDesignLatched = false;
	bool bHasInitializedRuntimeDesignTargets = false;
	bool bHasInitializedRuntimeShapeTargets = false;

	/**
	 * The designer preview actually in force: a placement's own Show Preview when this button is being
	 * previewed inside a widget that has been placed somewhere, otherwise the button's Show Preview. Worked
	 * out alongside the instance overrides, since it comes from the same variable.
	 */
	EContextMenuDesignPreview ActiveDesignPreview = EContextMenuDesignPreview::Normal;

	/** Which state every Shape Target currently resolves to. Shared across all of them, the same way CurrentDesignState is. */
	EContextMenuState CurrentShapeState = EContextMenuState::Normal;

	/** Designs can resolve to a different state than the shape (Hover Design While Enabled vs Hover Shape While Enabled). */
	EContextMenuState CurrentDesignState = EContextMenuState::Normal;

	TArray<FContextMenuRuntimeShapeTarget> RuntimeShapeTargets;
	TArray<FContextMenuRuntimeDesignTarget> RuntimeDesignTargets;

	/** One entry per Sound Set, rebuilt whenever the array changes size. See FUISoundPlayer. */
	TArray<FUISoundRuntime> RuntimeSounds;
};