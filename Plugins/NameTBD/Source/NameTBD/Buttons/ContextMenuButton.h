// Copyright AR GameSystems 2026. All rights reserved.

// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Engine/TimerHandle.h"
#include "Components/Widget.h"
#include "Slate/WidgetTransform.h"
#include "Widgets/Input/SButton.h"
#include "Sound/SoundBase.h"

#include "ContextMenuButton.generated.h"

class UInfoCardWidget;
class UContextMenuButton;
class UTexture2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class FWidgetPath;
class FWeakWidgetPath;

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
 * How one widget looks in one state. Each field belongs to a channel that the owning Design Target
 * switches on; fields whose channel is off are greyed out and never written to the widget.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuWidgetDesign
{
	GENERATED_BODY()

	/**
	 * Multiplied against the widget's own pixels, not a replacement: a white or grey image can be tinted any color,
	 * a pure black one stays black. Export icons white with the shape in the alpha channel, or drive a material instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bTintApplicable", HideEditConditionToggle))
	FLinearColor Tint = FLinearColor::White;

	/** The widget's render opacity. Multiplies with its parents' opacity, as usual. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bOpacityApplicable", HideEditConditionToggle, ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 1.0f;

	/** Swaps the widget's brush texture (Image and Border only). Switches at the state change rather than blending. Leave empty to keep the authored brush. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bBrushApplicable", HideEditConditionToggle, DisplayName = "Brush Texture"))
	TObjectPtr<UTexture2D> BrushTexture = nullptr;

	/** The widget's material for this state. Swaps exactly on state change rather than blending. Leave empty to keep the authored material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bMaterialApplicable", HideEditConditionToggle))
	TObjectPtr<UMaterialInterface> Material = nullptr;

	/** Scalar parameters pushed into the widget's material. The widget must already use a material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bMaterialApplicable", HideEditConditionToggle, EditFixedSize, TitleProperty = "ParameterName"))
	TArray<FContextMenuMaterialScalarValue> MaterialScalars;

	/** Vector parameters pushed into the widget's material. This is how a solid-black icon changes color: drive a Tint parameter here instead of the brush tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bMaterialApplicable", HideEditConditionToggle, EditFixedSize, TitleProperty = "ParameterName"))
	TArray<FContextMenuMaterialVectorValue> MaterialVectors;

	/** The sound to play when this widget switches into this state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "bSoundApplicable", HideEditConditionToggle))
	TObjectPtr<USoundBase> Sound = nullptr;

	// Details-panel mirrors of the owning target's channel switches; no runtime effect. Unreal can't reach a parent
	// struct's fields from an EditCondition, so UContextMenuButton copies them down in SyncEditConditionMirrors.
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
};

/**
 * One widget restyled per button state. Replaces the old Color Target: as well as a tint it can drive
 * opacity, a brush texture swap, and a material + parameters.
 *
 * Only the channels switched on below are ever written, so a target never fights with anything else
 * setting the same property on that widget.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuDesignTarget
{
	GENERATED_BODY()

	/** Any widget in this button's tree, including the button itself. Does not need Is Variable. Stored by name, so renaming the widget breaks the link. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Target Widget", GetOptions = "GetColorTargetWidgetOptions"))
	FName TargetWidgetName;

	// ---- Channels ----

	/** Drives the widget's tint (text color, image tint, border brush color, button background). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|Channels", meta = (DisplayName = "Drive Tint"))
	bool bDriveTint = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|Channels", meta = (DisplayName = "Drive Opacity"))
	bool bDriveOpacity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|Channels", meta = (DisplayName = "Drive Brush Texture"))
	bool bDriveBrush = false;

	/** Drives the material and named parameters on the widget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|Channels", meta = (DisplayName = "Drive Material"))
	bool bDriveMaterial = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|Channels", meta = (DisplayName = "Drive Sound"))
	bool bDriveSound = false;

	// ---- Source ----

	/** Use Project Settings: the design comes from a named Design Preset. Every other channel is always authored here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (DisplayName = "Design Source", EditCondition = "bDriveTint || bDriveOpacity || bDriveBrush || bDriveMaterial || bDriveSound", EditConditionHides))
	EContextMenuButtonSettingSource DesignSource = EContextMenuButtonSettingSource::Custom;

	/** Design Preset from Project Settings > Game > Info Card. Falls back to the states below if the preset is renamed or deleted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (EditCondition = "(bDriveTint || bDriveOpacity || bDriveBrush || bDriveMaterial || bDriveSound) && DesignSource == EContextMenuButtonSettingSource::UseProjectSettings", EditConditionHides, GetOptions = "InfoCardDeveloperSettings.GetContextMenuDesignPresetNameOptions"))
	FName PresetName = TEXT("Default");

	// ---- State designs ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States")
	FContextMenuWidgetDesign Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States")
	FContextMenuWidgetDesign Hovered;

	/** Not used by buttons with Button Can Be Enabled: a press on those blends straight from the current state to the state the click produces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States")
	FContextMenuWidgetDesign Pressed;

	/** Used while the button's Is Enabled is false. Not the same as the Enabled state below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States")
	FContextMenuWidgetDesign Disabled;

	/** Used while the button's enabled state is true. Greyed out unless Button Can Be Enabled is on, and while Enabled Matches Hover Design is on (Hovered is used instead). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design|States", meta = (EditCondition = "bEnabledDesignApplicable", HideEditConditionToggle))
	FContextMenuWidgetDesign Enabled;

	/** Details-panel mirror of the owning button; no runtime effect. */
	UPROPERTY()
	bool bEnabledDesignApplicable = true;

	/** How fast this target blends between states. 0 snaps instantly. Brush/Material swaps always snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float InterpolationSpeed = 15.0f;

	/** The design for one state, honoring the button's Enabled Matches Hover Design. */
	const FContextMenuWidgetDesign& GetDesignForState(EContextMenuState State, bool bEnabledMatchesHover) const;
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

USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuShapeStateSettings
{
	GENERATED_BODY()

	/** Transforms only the button itself (its background brush). Content and layout are not affected, but the mouse hit area follows the transform. Pivot is the center of the (extruded/inset) button. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "bIsApplicable", HideEditConditionToggle))
	FWidgetTransform ButtonTransform;

	/** Transforms only the button's content (its child widget). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "bIsApplicable", HideEditConditionToggle))
	FWidgetTransform ContentTransform;

	/** Grows only the button outward, in pixels. Content and layout are not affected, but the mouse hit area grows with it. +X extends the right edge, -X the left, +Y the top, -Y the bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "bIsApplicable", HideEditConditionToggle))
	FVector2D Extrude = FVector2D::ZeroVector;

	/** Pulls only the button inward, in pixels. Content and layout are not affected, but the mouse hit area shrinks with it. +X pulls in the right edge, -X the left, +Y the top, -Y the bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (EditCondition = "bIsApplicable", HideEditConditionToggle))
	FVector2D Inset = FVector2D::ZeroVector;

	/**
	 * Details-panel only; no runtime effect. When false, the fields above are greyed out.
	 * Unreal can't hide a property on one condition and grey it out on another, so UContextMenuButton
	 * hides EnabledShape by Shape Setting Source and mirrors "Button Can Be Enabled and not Enabled
	 * Matches Hover Shape" into this flag to grey it.
	 */
	UPROPERTY()
	bool bIsApplicable = true;
};

/**
 * A named, project-wide set of state shapes, authored in Project Settings > Game > Info Card.
 * A button with ShapeSettingSource = Use Project Settings picks one of these by name.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FContextMenuShapePreset
{
	GENERATED_BODY()

	/** Name shown in each button's Shape Preset dropdown. Keep unique; renaming breaks buttons pointed at the old name (they fall back to their own Custom shapes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	FName PresetName = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	FContextMenuShapeStateSettings NormalShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	FContextMenuShapeStateSettings HoveredShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	FContextMenuShapeStateSettings PressedShape;

	/** Used while the button's Is Enabled is false. Disabled is not the same as the Enabled state: disabling is explicit, and a disabled button's enabled state is also set to false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	FContextMenuShapeStateSettings DisabledShape;

	/**
	 * Used while the button's enabled state is true (buttons with Button Can Be Enabled only). Not the same as Disabled:
	 * a button can be enabled and un-enabled, and when un-enabled it simply returns to Normal.
	 * Hovered and Pressed still take priority while the pointer is over the button.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	FContextMenuShapeStateSettings EnabledShape;

	/** Blend speed for Button Transform and Content Transform. 0 snaps instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float TransformInterpolationSpeed = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	EContextMenuInterpolationCurve TransformInterpolationCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (EditCondition = "TransformInterpolationCurve == EContextMenuInterpolationCurve::Click", EditConditionHides))
	float TransformOvershoot = 1.5f;

	/** Blend speed for Extrude and Inset. 0 snaps instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float SizeInterpolationSpeed = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation")
	EContextMenuInterpolationCurve SizeInterpolationCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape Interpolation", meta = (EditCondition = "SizeInterpolationCurve == EContextMenuInterpolationCurve::Click", EditConditionHides))
	float SizeOvershoot = 1.5f;
};

/** Runtime-only resolved design target (not a USTRUCT: widgets are held weakly, nothing to serialize). */
struct FContextMenuRuntimeDesignTarget
{
	TWeakObjectPtr<UWidget> Widget;

	/** Index into UContextMenuButton::DesignTargets, so the authored values are read live every step. */
	int32 AuthoredIndex = INDEX_NONE;

	/** Where each driven channel is right now, so a state change blends on from here rather than restarting. */
	FLinearColor CurrentTint = FLinearColor::White;
	float CurrentOpacity = 1.0f;
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

	/** Sound plays are discrete, so they only trigger exactly on state change. */
	EContextMenuState LastSoundState = EContextMenuState::Normal;
	bool bHasAppliedSound = false;
};

/**
 * Slate button used by UContextMenuButton. Identical to SButton, except for two things.
 *
 * Shape: the extruded/inset bounds plus ButtonTransform are carried on this widget's render transform, so the
 * mouse hit area follows the shape - an extruded button is clickable (and hoverable) out to its painted edge, an
 * inset one stops responding where it stops being drawn. Layout is untouched, since a render transform never
 * reflows anything, and both the brush and the content are painted back through the inverse of the shape: the
 * brush resized in unshaped local space (so a Box/Border brush's margins are never stretched) and the content
 * left exactly where layout put it. That is what lets the shape affect only the button and never its content.
 *
 * OnPaint is the only thing that writes that render transform, and it writes it last, for the paint after this
 * one. It has to be that way round. A paint is handed geometry built from whatever transform was on the widget
 * when Slate last updated it, so the shape OnPaint cancels has to be one that cannot have moved since that
 * geometry was built - which is only guaranteed of the shape committed by the previous paint. Writing the
 * transform from anywhere else (Tick, or the owner pushing a new shape) lets the two drift apart by one frame's
 * worth of the blend, which shows up as the content fidgeting about while the shape animates: the faster the
 * interpolation, the bigger the frame delta, the further it moves.
 *
 * Brush: the style brush is picked from the state the owner resolved rather than from Slate's own hover/press
 * flags, so the button's own state settings win over the native button style instead of being painted on top of it.
 *
 * With no active button visuals and no state reported, it paints exactly like a stock SButton.
 */
class SContextMenuSlateButton : public SButton
{
public:
	void SetOwnerWidget(UContextMenuButton* InOwner);

	/**
	 * The shape to paint and hit test against. Takes effect on the next paint, which is where it gets composed onto
	 * the render transform.
	 *
	 * @param InEdgeOffsets  Per-edge offsets in local pixels. Positive grows that edge outward, negative pulls it inward.
	 * @param InTransform    Render transform applied to the button brush only, pivoting on the brush center.
	 */
	void SetButtonShape(const FMargin& InEdgeOffsets, const FWidgetTransform& InTransform);

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
	 * Writes the committed shape back onto the render transform. UWidget::SynchronizeProperties pushes the authored
	 * render transform straight onto this widget, which drops the shape composed into it; the owner calls this
	 * straight afterwards. It re-applies the shape the last paint committed, never a newly computed one, so the
	 * invariant the next paint relies on still holds.
	 */
	void RefreshShapeRenderTransform();

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** The extruded/inset size for a given layout size. Never quite zero, so the shape stays invertible. */
	FVector2D GetShapedSize(const FVector2D& LocalSize) const;

	/** Maps the layout box onto the extruded/inset box: a per-axis scale plus the top-left offset. */
	FSlateRenderTransform ComputeEdgeTransform(const FVector2D& LocalSize) const;

	/** The whole shape: the edge offsets, then ButtonTransform about the center of the shaped box. */
	FSlateRenderTransform ComputeShapeTransform(const FVector2D& LocalSize) const;

	/**
	 * Works out the shape for the current Extrude/Inset/ButtonTransform and commits it: stores what the next paint
	 * needs to cancel it, and composes it onto the render transform (which is what moves the hit area, since Slate
	 * hit tests against paint-space geometry). Called at the very end of OnPaint and nowhere else.
	 */
	void CommitShapeRenderTransform(const FVector2D& LocalSize) const;

	/** Composes a committed shape with the widget's own authored render transform and writes the result. */
	void WriteRenderTransform(const FSlateRenderTransform& Shape, bool bShapeActive, const FVector2D& LocalSize, const FWidgetTransform& AuthoredTransform, const FVector2D& AuthoredPivot);

	TWeakObjectPtr<UContextMenuButton> OwnerWidget;
	FMargin EdgeOffsets = FMargin(0.0f);
	FWidgetTransform BrushTransform;
	bool bHasButtonShape = false;

	EContextMenuState VisualState = EContextMenuState::Normal;
	bool bDesignOwnsBrush = false;

	/** False until the owner first calls SetVisualState; until then the stock SButton brush logic applies. */
	bool bHasVisualState = false;

	// The shape as committed by the last paint, i.e. the shape sitting on the render transform right now and so the
	// one baked into the geometry this paint was handed. Everything a paint draws comes from these. Mutable because
	// the commit happens at the end of OnPaint, which is const.
	mutable bool bHasCommittedShape = false;
	mutable bool bCommittedShapeActive = false;
	mutable FSlateRenderTransform CommittedShape;
	mutable FSlateRenderTransform CommittedShapeInverse;
	mutable FVector2D CommittedBrushSize = FVector2D::ZeroVector;
	mutable FVector2D CommittedEdgeScale = FVector2D(1.0, 1.0);

	// What that shape was committed from, so the transform is only rewritten (and the widget invalidated) when one
	// of its inputs actually moves.
	mutable FVector2D CommittedLocalSize = FVector2D::ZeroVector;
	mutable FMargin CommittedEdgeOffsets = FMargin(0.0f);
	mutable FWidgetTransform CommittedBrushTransform;
	mutable FWidgetTransform CommittedAuthoredTransform;
	mutable FVector2D CommittedAuthoredPivot = FVector2D(0.5, 0.5);

	/**
	 * Is Enabled as of the last paint. UWidget::SetIsEnabled isn't virtual and Slate has no enabled-changed
	 * callback, so paint is where a change first becomes visible; the owner is then notified on the next tick.
	 */
	mutable bool bLastPaintedSelfEnabled = true;
};

UCLASS()
class NAMETBD_API UContextMenuButton : public UButton
{
	GENERATED_BODY()

public:
	
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
	 * Set it back to None to restore the authored look.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button")
	EContextMenuDesignPreview ShowPreview = EContextMenuDesignPreview::None;

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
	bool bButtonCanBeEnabled = false;
	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "bButtonCanBeEnabled", HideEditConditionToggle))
	bool bDefaultEnabledState = false;

	/**
	 * If true, the Enabled state uses the Hovered shape (Button/Content Transform, Extrude, Inset, and the style's
	 * Hovered brush), so there's nothing to copy from Hovered to Enabled. Enabled Shape is greyed out while this is on.
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
	 * If false, hovering while this button is enabled does not apply the Hovered shape (Button/Content Transform, Extrude,
	 * Inset, and the style's Hovered brush); the Enabled shape stays.
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

	/** 
	 * Use the Context Menu Button getter function GetPressProgress for wiring to a progress bar.
	 * You may also use the event OnPressDurationCancelled if you would like to handle that case.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom && HoldAction == EExecuteHoldAction::AfterPressDuration", EditConditionHides, ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	float PressDuration = 1.0f;

	/** Delegate for the PressDuration property to enable UMG function binding */
	UPROPERTY()
	FGetFloat PressDurationDelegate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides, ClampMin = "0.01", UIMin = "0.01", UIMax = "0.5"))
	float ActionExecutionFrequency = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides, ClampMin = "0.0", UIMin = "0.0", UIMax = "0.025"))
	float ActionExecutionFrequencyAcceleration = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Settings", meta = (EditCondition = "ButtonType == EContextMenuButtonType::Hold && ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::Custom", EditConditionHides, ClampMin = "0.001", UIMin = "0.01", UIMax = "0.5"))
	float ActionExecutionFrequencyCap = 0.05f;

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

	/** Use Project Settings: pick a Shape Preset from Project Settings > Game > Info Card. Custom: author the state shapes and speeds below. Designs are configured separately and are always per-button. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape")
	EContextMenuButtonSettingSource ShapeSettingSource = EContextMenuButtonSettingSource::UseProjectSettings;

	/** Shape Preset from Project Settings > Game > Info Card. Falls back to this button's Custom shape if the preset is renamed or deleted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::UseProjectSettings", EditConditionHides, GetOptions = "InfoCardDeveloperSettings.GetContextMenuShapePresetNameOptions"))
	FName ShapePresetName = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	FContextMenuShapeStateSettings NormalShape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	FContextMenuShapeStateSettings HoveredShape;

	/** Not used by buttons with Button Can Be Enabled: a press on those blends straight from the current state to the state the click produces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	FContextMenuShapeStateSettings PressedShape;

	/** Used while Is Enabled is false. Disabled is NOT the same as the Enabled state: disabling is explicit, and disabling the button also sets its enabled state to false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	FContextMenuShapeStateSettings DisabledShape;

	/**
	 * Used while the enabled state is true. Greyed out unless Button Can Be Enabled is on, and while Enabled Matches Hover Shape is on.
	 * NOT the same as disabled: a button can be enabled and un-enabled, and when un-enabled it simply returns to Normal.
	 * Priority: Disabled > Pressed > Hovered > Enabled > Normal.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	FContextMenuShapeStateSettings EnabledShape;

	/** Blend speed for Button Transform and Content Transform. 0 snaps instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape Interpolation", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::Custom", EditConditionHides, ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float TransformInterpolationSpeed = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape Interpolation", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	EContextMenuInterpolationCurve TransformInterpolationCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape Interpolation", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::Custom && TransformInterpolationCurve == EContextMenuInterpolationCurve::Click", EditConditionHides))
	float TransformOvershoot = 1.5f;

	/** Blend speed for Extrude and Inset. 0 snaps instantly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape Interpolation", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::Custom", EditConditionHides, ClampMin = "0.0", UIMin = "0.0", UIMax = "50.0"))
	float SizeInterpolationSpeed = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape Interpolation", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::Custom", EditConditionHides))
	EContextMenuInterpolationCurve SizeInterpolationCurve = EContextMenuInterpolationCurve::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context Menu Button|Shape Interpolation", meta = (EditCondition = "ShapeSettingSource == EContextMenuButtonSettingSource::Custom && SizeInterpolationCurve == EContextMenuInterpolationCurve::Click", EditConditionHides))
	float SizeOvershoot = 1.5f;

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

	/**
	 * Re-resolves Design Targets and re-applies the current state's shape/design. Call after changing
	 * visual properties at runtime (e.g. ShapePresetName, DesignTargets).
	 * Not needed after Set Is Enabled or Set Enabled State; those update automatically.
	 */
	UFUNCTION(BlueprintCallable, Category = "Context Menu Button|State")
	void RefreshButtonState(bool bInstant = false);

	/** Backs the Target Widget dropdown on FContextMenuDesignTarget: this button plus every widget beneath it. Must stay a UFUNCTION for GetOptions. */
	UFUNCTION()
	TArray<FString> GetColorTargetWidgetOptions() const;

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
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnContextPressDurationCancelled);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnContextPressDurationProgress, float, Progress);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnContextProgressDurationCompleted);

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button")
	FOnContextClicked OnContextClicked;

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button")
	FOnContextEnabledStateChanged OnEnabledStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button")
	FOnContextPressDurationCancelled OnContextPressDurationCancelled;

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button")
	FOnContextPressDurationProgress OnContextPressDurationProgress;

	UPROPERTY(BlueprintAssignable, Category = "Context Menu Button")
	FOnContextProgressDurationCompleted OnContextProgressDurationCompleted;

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
	float GetPressProgress() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Context Menu Button|State")
	float GetElapsedHoldTime() const;

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

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	
	void SyncMaterialParametersEditor();
	UMaterialInterface* ExtractWidgetMaterialEditor(UWidget* Widget);
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

	/** Sends the current Extrude/Inset/ButtonTransform to the Slate button, which paints them on the brush and hit tests against them. */
	void PushButtonShapeToSlate();

	/** Sends the resolved state (and who owns the brush) to the Slate button, which picks the style brush from it. */
	void PushVisualStateToSlate(EContextMenuState ShapeState);

	/** The style brush a resolved shape state should paint. Enabled has no brush of its own, so it maps to Hovered or Normal. */
	EContextMenuState GetBrushStateForShapeState(EContextMenuState ShapeState) const;

	/**
	 * True while a Design Target drives this button's own Tint, Brush Texture or Material, i.e. the design owns the
	 * button's look and the style's per-state brushes should stay out of its way.
	 */
	bool DoesDesignDriveButtonBrush() const;

	/** The project preset in use, or nullptr if ShapeSettingSource is Custom or the named preset no longer exists. */
	const FContextMenuShapePreset* FindActiveShapePreset() const;
	const FContextMenuShapeStateSettings& GetShapeForState(EContextMenuState State, const FContextMenuShapePreset* Preset) const;
	
	void InitializeRuntimeDesignTargets();

	/** Moves one design target one step toward its state design (or snaps). Returns true once every driven channel has arrived. */
	bool ApplyDesignTarget(FContextMenuRuntimeDesignTarget& Target, EContextMenuState State, float DeltaTime, bool bInstant);

	static FLinearColor ExtractWidgetColor(UWidget* Widget);
	static void ApplyColorToWidget(UWidget* Widget, const FLinearColor& Color);

	/** Swaps the brush texture on an Image or Border, keeping its authored draw settings. Does nothing for other widget types. */
	static void ApplyBrushTextureToWidget(UWidget* Widget, UTexture2D* Texture);

	/** Swaps the material on a supported widget. */
	static void ApplyMaterialToWidget(UWidget* Widget, UMaterialInterface* Material);

	/** The widget's Dynamic Material Instance, created on demand. Null unless its brush already uses a material. */
	static UMaterialInstanceDynamic* ResolveWidgetMaterial(UWidget* Widget);

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
		float Opacity = 1.0f;
		TWeakObjectPtr<UTexture2D> BrushTexture;
		TWeakObjectPtr<UMaterialInterface> Material;
		bool bIsButton = false;
		FButtonStyle ButtonStyle;
	};

	TArray<FContextMenuDesignPreviewBaseline> DesignPreviewBaselines;
#endif

	/** Finds a widget in this button's tree by the name/path stored in FContextMenuColorTarget::TargetWidgetName. */
	UWidget* FindWidgetInButtonTree(FName WidgetPath);

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

	UPROPERTY(Transient)
	TWeakObjectPtr<UInfoCardWidget> OwningCard;

	UPROPERTY(Transient, DuplicateTransient)
	bool bHasBoundClickHandler = false;

	static constexpr int32 MaxOwnerSearchDepth = 64;

	FTimerHandle HoldFrequencyTimerHandle;
	FTimerHandle CooldownTimerHandle;
	FDelegateHandle SlateFocusChangingHandle;

	bool bIsPressDurationActive = false;
	double PressDurationStartTime = 0.0;
	uint32 PressDurationSerial = 0;

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

	EContextMenuState CurrentShapeState = EContextMenuState::Normal;

	/** Designs can resolve to a different state than the shape (Hover Design While Enabled vs Hover Shape While Enabled). */
	EContextMenuState CurrentDesignState = EContextMenuState::Normal;

	UPROPERTY(Transient)
	float TransformTransitionAlpha = 1.0f;

	UPROPERTY(Transient)
	float SizeTransitionAlpha = 1.0f;

	UPROPERTY(Transient)
	FWidgetTransform StartButtonTransform;

	UPROPERTY(Transient)
	FWidgetTransform StartContentTransform;

	UPROPERTY(Transient)
	FVector2D StartExtrude = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	FVector2D StartInset = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	FWidgetTransform CurrentButtonTransform;

	UPROPERTY(Transient)
	FWidgetTransform CurrentContentTransform;

	UPROPERTY(Transient)
	FVector2D CurrentExtrude = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	FVector2D CurrentInset = FVector2D::ZeroVector;

	TArray<FContextMenuRuntimeDesignTarget> RuntimeDesignTargets;
};