// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Rendering/SlateRenderTransform.h"
#include "UObject/Object.h"
#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"
#include "NameTBD/Widgets/WidgetMotion.h"

#include "WidgetConstraints.generated.h"

class UWidget;

/*
 * Widget constraints: one widget's place, turn, size or opacity tied to something else - another widget, the pointer, a
 * path, an area - and kept so every frame, whatever moves it. A ball going round a ring, eyes following the cursor and
 * staying in their sockets, a label pinned to a moving marker, a needle aiming at a value, a magnetic button leaning toward
 * the pointer, a knob that slides round a dial under the finger, parallax layers, a chain of dots trailing the cursor.
 *
 * Modelled on the production animation tools' constraints (Rive's Translation, Rotation, Scale, Transform, Distance and
 * Follow Path; Unity's Aim / Look At and Position constraints; Blender's Limit and Transformation (remap) constraints; Spine's
 * path constraint), short of 2D rigging (bones, IK):
 *   - A widget's constraints are a stack, applied in order, each starting from where the last left it.
 *   - Every constraint has a Strength (0 none, 1 full, animatable), blending from where the widget was to where it says.
 *   - Targets are another widget (any point of it - Anchor), the pointer, the widget's parent, or a fixed point.
 *   - Source Space: Screen (where the target is drawn) or Local (the target's own animated offset, turn or scale - mirror
 *     another widget's animation).
 *   - Limits (min / max) relative to where the widget rests.
 *   - Beyond Rive: an Activation (always, while hovered or pressed, or near the pointer, fading in and out), and Smoothing (a
 *     spring - Duration and Bounce - on what the constraint adds, so a follower lags, swings and settles; the widget's own
 *     animation underneath is never lagged).
 *
 * Constraints never write the widget's Render Transform: the Constraints component (WidgetConstraintComponent.h) wraps the
 * widget and draws (and hit-tests) it with an extra transform on top of its own, so Widget Animations, the designer and
 * gameplay keep owning the Render Transform and constraints layer over them.
 *
 * Space: everything is measured in the constrained widget's layout space - Slate units, (0, 0) the top left of the box it is
 * laid out in, before any transform. A pose's Position is where the widget's Render Transform Pivot sits in that space.
 */

/** Where a constrained widget is: its pivot's place, its turn (degrees, clockwise), its scale and an opacity multiplier. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetConstraintPose
{
	GENERATED_BODY()

	/** Where the widget's pivot is, in its layout space (Slate units from the top left of its laid-out box). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FVector2D Position = FVector2D::ZeroVector;

	/** Degrees, clockwise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float Angle = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	FVector2D Scale = FVector2D(1.0, 1.0);

	/** Multiplies the widget's opacity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pose")
	float Opacity = 1.f;

	/** Blends two poses: positions, scales and opacity linearly, angles the short way round. */
	static FWidgetConstraintPose Lerp(const FWidgetConstraintPose& A, const FWidgetConstraintPose& B, float Alpha);
};

/** What a constraint is tied to. */
UENUM(BlueprintType)
enum class EWidgetConstraintTargetKind : uint8
{
	Widget UMETA(ToolTip = "Another widget in this Widget Blueprint (or set from Blueprint with Set Target Widget): the point of it Anchor names, as it is drawn."),
	Pointer UMETA(ToolTip = "The mouse pointer (or touch, or a Diegetic Screen's pointer in the world). Not there - a gamepad, the designer - the constraint lets go."),
	Parent UMETA(ToolTip = "The widget's parent (the panel it is in): the point of it Anchor names."),
	Point UMETA(ToolTip = "A fixed point: Point, in Slate units from where the widget's pivot rests.")
};

/** What a constraint does while the pointer is away from a Pointer target. */
UENUM(BlueprintType)
enum class EWidgetConstraintPointerAway : uint8
{
	Release UMETA(ToolTip = "Lets go: the widget returns to rest (smoothly, with Smoothing or the activation fade)."),
	HoldLast UMETA(DisplayName = "Hold Last", ToolTip = "Keeps the last place the pointer was.")
};

/** Where a constraint reads its target's value. */
UENUM(BlueprintType)
enum class EWidgetConstraintSpace : uint8
{
	Screen UMETA(ToolTip = "Where the target is drawn - its place, turn and size as seen, whatever moves it."),
	Local UMETA(ToolTip = "The target's own Render Transform - its animated offset, angle or scale - put on this widget the same way: one widget mirrors another's animation.")
};

/** When a constraint acts. */
UENUM(BlueprintType)
enum class EWidgetConstraintActivation : uint8
{
	Always,
	WhileHovered UMETA(DisplayName = "While Hovered", ToolTip = "While the pointer is over the widget."),
	WhilePressed UMETA(DisplayName = "While Pressed", ToolTip = "While the left button (or a touch) is held, having pressed on the widget."),
	NearPointer UMETA(DisplayName = "Near Pointer", ToolTip = "While the pointer is within Activation Radius of the widget's edges, fading in as it nears - a magnetic button that starts leaning before it is reached.")
};

/**
 * What a constraint does while a Widget Animation moves the widget it is on - its Render Transform keyed away from where it
 * was authored, the motion path the designer draws.
 */
UENUM(BlueprintType)
enum class EWidgetConstraintWhileAnimated : uint8
{
	ActOver UMETA(DisplayName = "Act Over the Animation", ToolTip = "As always: the constraint acts on wherever the animation puts the widget (a Stay Inside keeps a keyed pupil in its eye)."),
	LetGo UMETA(DisplayName = "Let Go", ToolTip = "Fades out (over Activation Fade) while the widget is animated, so the keys alone place it, and back in as it comes home: a pupil that follows the cursor until a keyed look plays."),
	FollowKeys UMETA(DisplayName = "Follow the Keys", ToolTip = "The animated place becomes this constraint's target, in place of the pointer, widget or point: the widget is carried to each key with the constraint's own smoothing and limits (a pupil looks where the keys say, springing there, staying in its eye), and the keyed offset isn't added on top. As the animation ends the target goes back to the pointer - smoothly, with Smooth on.")
};

/** A value of a constraint that can be animated (the Constraint track) or set by name from Blueprint. */
UENUM(BlueprintType)
enum class EWidgetConstraintChannel : uint8
{
	Strength UMETA(ToolTip = "How much the constraint acts, 0 to 1."),
	PathPosition UMETA(DisplayName = "Path Position", ToolTip = "Follow Path: how far along the path."),
	OffsetX UMETA(DisplayName = "Offset X", ToolTip = "Copy Position / Copy Transform: Offset X."),
	OffsetY UMETA(DisplayName = "Offset Y", ToolTip = "Copy Position / Copy Transform: Offset Y."),
	AngleOffset UMETA(DisplayName = "Angle Offset", ToolTip = "Copy Rotation / Look At / Follow Path (orient): the added angle."),
	Distance UMETA(ToolTip = "Distance: the distance kept. Pointer Parallax: Depth (both axes).")
};

/** A constraint's target. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetConstraintTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	EWidgetConstraintTargetKind Kind = EWidgetConstraintTargetKind::Widget;

	/**
	 * The widget, for Widget - any widget of this Widget Blueprint, a parent of this one included. Always shown: picking one
	 * makes the Kind Widget. Set Target Widget overrides it at runtime (a widget made by code).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (GetOptions = "GetWidgetOptions"))
	FName Widget;

	/** Which point of the widget (or parent): (0, 0) its top left, (0.5, 0.5) its middle, (1, 1) its bottom right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (EditCondition = "Kind == EWidgetConstraintTargetKind::Widget || Kind == EWidgetConstraintTargetKind::Parent", EditConditionHides))
	FVector2D Anchor = FVector2D(0.5, 0.5);

	/** For Point: Slate units from where the widget's pivot rests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (EditCondition = "Kind == EWidgetConstraintTargetKind::Point", EditConditionHides))
	FVector2D Point = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target", meta = (EditCondition = "Kind == EWidgetConstraintTargetKind::Pointer", EditConditionHides))
	EWidgetConstraintPointerAway WhenPointerAway = EWidgetConstraintPointerAway::Release;
};

/**
 * What a constraint is given when it is solved: the widget, where it rests, and how to find its targets - all in the widget's
 * layout space ("host" space: Slate units from the top left of its laid-out box).
 */
struct NAMETBD_API FWidgetConstraintContext
{
	UWidget* Owner = nullptr;

	/** The widget's laid-out size, and where its pivot is (Slate units) when it rests untransformed. */
	FVector2f Size = FVector2f::ZeroVector;
	FVector2f PivotPoint = FVector2f::ZeroVector;

	/** The widget as it is before any constraint: laid out, with its own Render Transform. */
	FWidgetConstraintPose Rest;

	float DeltaTime = 0.f;
	bool bDesignTime = false;

	/** Desktop space (where the pointer is measured) to and from the widget's layout space. */
	FSlateRenderTransform HostToDesktop;
	FSlateRenderTransform DesktopToHost;

	/** The pointer, in layout space. Only when bHasPointer (never at design time). */
	bool bHasPointer = false;
	FVector2f Pointer = FVector2f::ZeroVector;
	bool bPointerOver = false;
	bool bPressed = false;

	/** Whether a Widget Animation has the widget away from where it was authored, and the place it puts the pivot (layout space). */
	bool bAnimated = false;
	FVector2f AnimatedPoint = FVector2f::ZeroVector;

	/**
	 * Set for the constraint being applied when it follows the animation (Follow the Keys, while animated): its target is
	 * AnimatedPoint, whatever it is set to.
	 */
	bool bFollowAnimation = false;

	/** A preview pose worked out with no lasting effect (the designer's motion paths): no smoothing, fades or remembered offsets. */
	bool bStateless = false;

	/** Where another widget is: its local space to this widget's layout space, and its size. Its constraints already solved. */
	TFunction<bool(const UWidget*, FSlateRenderTransform&, FVector2f&)> ResolveWidget;

	/** A widget in the same tree, by name. */
	UWidget* FindWidget(FName Name) const;

	/** ResolveWidget, checked. */
	bool GetWidgetFrame(const UWidget* Widget, FSlateRenderTransform& OutLocalToHost, FVector2f& OutSize) const;

	/** The widget's laid-out box at rest, in layout space (its own transform's translation included). */
	FBox2f GetRestBox() const;
};

/**
 * One constraint on a widget. Add them to a widget's Constraints component (the designer's Add Component on the widget's
 * Details, then + under Constraints); they are applied in order. The base is never used itself.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, BlueprintType, CollapseCategories)
class NAMETBD_API UWidgetConstraint : public UObject
{
	GENERATED_BODY()

public:
	/** What Blueprint and the Constraint track find it by. None: by its place in the list. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint", meta = (DisplayName = "Name"))
	FName ConstraintName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint", meta = (DisplayName = "Enabled"))
	bool bEnabled = true;

	/** How much it acts: 0 not at all, 1 fully, between - partway from where the widget would be. Animatable (Constraint track). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint", meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 1.f;

	/** When it acts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint")
	EWidgetConstraintActivation Activation = EWidgetConstraintActivation::Always;

	/**
	 * While a Widget Animation moves the widget (keys on its motion path): act over it, let go so the keys place it, or follow
	 * the keys - the keyed place becomes the target (a pupil that follows the cursor looks where the keys say instead, springing
	 * there with Smooth, still kept in its eye).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint")
	EWidgetConstraintWhileAnimated WhileAnimated = EWidgetConstraintWhileAnimated::ActOver;

	/** Near Pointer: how far from the widget's edges the pointer starts to pull, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint", meta = (EditCondition = "Activation == EWidgetConstraintActivation::NearPointer", EditConditionHides, ClampMin = "0", UIMax = "400", ForceUnits = "px"))
	float ActivationRadius = 80.f;

	/** Seconds to fade in and out as it starts and stops acting (and as its target comes and goes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint", meta = (ClampMin = "0", UIMax = "2", Units = "s"))
	float ActivationFade = 0.2f;

	/**
	 * Springs what the constraint adds rather than snapping it: a follower that lags, swings past and settles - eyes darting,
	 * a magnetic button, a tail of dots. Only the constraint's own change is smoothed; the widget's animation underneath isn't.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Smoothing", meta = (DisplayName = "Smooth"))
	bool bSmooth = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Smoothing", meta = (EditCondition = "bSmooth", EditConditionHides))
	FWidgetSpringParams Smoothing;

	UWidgetConstraint();

	UFUNCTION(BlueprintCallable, Category = "Widget Constraint")
	void SetStrength(float InStrength) { Strength = FMath::Clamp(InStrength, 0.f, 1.f); }

	UFUNCTION(BlueprintCallable, Category = "Widget Constraint")
	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

	/** The widget a Widget target is tied to, whatever its Target names - for widgets made at runtime. Null: back to the name. */
	UFUNCTION(BlueprintCallable, Category = "Widget Constraint")
	void SetTargetWidget(UWidget* InWidget) { TargetOverride = InWidget; }

	/** Snaps its smoothing and fades to where they are going (after a jump, a reopened menu). */
	UFUNCTION(BlueprintCallable, Category = "Widget Constraint")
	virtual void ResetMotion();

	/** Sets an animatable value. False when this kind of constraint has no such value. */
	UFUNCTION(BlueprintCallable, Category = "Widget Constraint")
	virtual bool SetChannel(EWidgetConstraintChannel Channel, float Value);

	/** An animatable value. False when this kind of constraint has no such value. */
	UFUNCTION(BlueprintPure, Category = "Widget Constraint")
	virtual bool GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const;

	/** The widget names offered in its dropdowns. */
	UFUNCTION()
	TArray<FString> GetWidgetOptions() const;

	/**
	 * Applies the constraint: Pose is where the widget is so far (rest, then each earlier constraint), Activation and Strength
	 * already handled by the caller. Returns the pose it would put the widget in at full strength - false when it can't act
	 * this frame (its target is missing), which lets the widget go.
	 */
	virtual bool Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose) { return false; }

	/** The widgets it reads, which must be solved before it. */
	virtual void GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const {}

	/** Lines (pairs of points, layout space) drawn over the widget in the designer: what it is tied to. */
	virtual void GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const;

	/** Runs the whole step - activation, Solve, Strength, smoothing - from Pose. What the Constraints component calls. */
	FWidgetConstraintPose Apply(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, float MasterStrength);

	/**
	 * The step with no lasting effect - Solve at Strength, no activation fade, smoothing or remembered state: a pose for a
	 * preview (the designer's motion paths). Context.bStateless must be set.
	 */
	FWidgetConstraintPose ApplyStateless(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, float MasterStrength);

	/** The name Blueprint and the Constraint track can find it by: its Name, else its kind's ("Follow Path"). */
	FName GetDisplayName() const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** The widget named by a Widget target, or the runtime override. */
	UWidget* ResolveTargetWidget(const FWidgetConstraintContext& Context, const FWidgetConstraintTarget& Target) const;

	/** The target's place (and turn and scale, for a widget) in layout space. False when it isn't there this frame. */
	bool GetTargetPose(const FWidgetConstraintContext& Context, const FWidgetConstraintTarget& Target, FWidgetConstraintPose& OutPose);

protected:
	/** Remembers where the target was this frame - what the designer's line to it is drawn to. */
	void NoteTarget(const FWidgetConstraintPose& TargetPose);

	/** The target as it is set (what GetTargetPose gives when the constraint isn't following the animation). */
	bool GetSetTargetPose(const FWidgetConstraintContext& Context, const FWidgetConstraintTarget& Target, FWidgetConstraintPose& OutPose);

	TWeakObjectPtr<UWidget> TargetOverride;
	FVector2f LastTarget = FVector2f::ZeroVector;
	bool bHasLastTarget = false;

private:
	/** The change it made last frame it could act: kept while it fades out after its target went. */
	float LastChange[6] = { 0.f, 0.f, 0.f, 1.f, 1.f, 1.f };

	/** How much it acts now, faded. */
	FWidgetMotionValue ActivationValue;
	bool bActivationStarted = false;

	/** The smoothed change it adds: position x/y, angle, scale x/y, opacity. */
	FWidgetSpringState SmoothState[6];
	bool bSmoothStarted = false;

	/** The last place a Pointer target was seen (Hold Last). */
	FVector2f LastPointer = FVector2f::ZeroVector;
	bool bHasLastPointer = false;

	float ComputeActivation(const FWidgetConstraintContext& Context) const;
};

// =====================================================================================================
// Copy constraints
// =====================================================================================================

/** Moves the widget to its target (a widget, the pointer, its parent, a point) - all the way, or part of it. */
UCLASS(meta = (DisplayName = "Copy Position"))
class NAMETBD_API UWidgetCopyPositionConstraint : public UWidgetConstraint
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	FWidgetConstraintTarget Target;

	/** Screen: goes to where the target is. Local: takes on the target's own animated offset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	EWidgetConstraintSpace SourceSpace = EWidgetConstraintSpace::Screen;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy", meta = (DisplayName = "Copy X"))
	bool bCopyX = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy", meta = (DisplayName = "Copy Y"))
	bool bCopyY = true;

	/**
	 * How much of the way to the target, per axis (Screen), or how much of its offset (Local): 1 all of it, 0.25 a quarter - a
	 * magnetic lean toward the pointer, a parallax layer moving less than the one it follows. Negative goes the other way.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy")
	FVector2D CopyAmount = FVector2D(1.0, 1.0);

	/** Added after copying, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy")
	FVector2D Offset = FVector2D::ZeroVector;

	/** Keeps the distance there was between the widget and its target when it started (Screen): the two move together. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy")
	bool bKeepStartingOffset = false;

	/** Holds the result within Min / Max of where the widget rests, per axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (DisplayName = "Limit"))
	bool bLimit = false;

	/** The furthest left / up it may go from rest (negative), in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (EditCondition = "bLimit", EditConditionHides))
	FVector2D Min = FVector2D(-20.0, -20.0);

	/** The furthest right / down it may go from rest, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (EditCondition = "bLimit", EditConditionHides))
	FVector2D Max = FVector2D(20.0, 20.0);

	virtual bool Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose) override;
	virtual void GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const override;
	virtual void GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const override;
	virtual bool SetChannel(EWidgetConstraintChannel Channel, float Value) override;
	virtual bool GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const override;
	virtual void ResetMotion() override;

private:
	FVector2f StartingOffset = FVector2f::ZeroVector;
	bool bHasStartingOffset = false;
};

/** Turns the widget as its target is turned. */
UCLASS(meta = (DisplayName = "Copy Rotation"))
class NAMETBD_API UWidgetCopyRotationConstraint : public UWidgetConstraint
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	FWidgetConstraintTarget Target;

	/** Screen: the target's turn as drawn. Local: the target's own Render Transform angle (gears turning together). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	EWidgetConstraintSpace SourceSpace = EWidgetConstraintSpace::Local;

	/** Multiplies the copied angle: -1 turns the other way (a meshing gear), 0.5 half as far. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy")
	float CopyAmount = 1.f;

	/** Degrees added. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy", meta = (ForceUnits = "Degrees"))
	float AngleOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (DisplayName = "Limit"))
	bool bLimit = false;

	/** Degrees from its resting angle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (EditCondition = "bLimit", EditConditionHides, ForceUnits = "Degrees"))
	float MinAngle = -45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (EditCondition = "bLimit", EditConditionHides, ForceUnits = "Degrees"))
	float MaxAngle = 45.f;

	virtual bool Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose) override;
	virtual void GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const override;
	virtual bool SetChannel(EWidgetConstraintChannel Channel, float Value) override;
	virtual bool GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const override;
};

/** Scales the widget as its target is scaled. */
UCLASS(meta = (DisplayName = "Copy Scale"))
class NAMETBD_API UWidgetCopyScaleConstraint : public UWidgetConstraint
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	FWidgetConstraintTarget Target;

	/** Screen: the target's size as drawn. Local: the target's own Render Transform scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	EWidgetConstraintSpace SourceSpace = EWidgetConstraintSpace::Local;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy", meta = (DisplayName = "Copy X"))
	bool bCopyX = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy", meta = (DisplayName = "Copy Y"))
	bool bCopyY = true;

	/** How much of the target's change in scale is taken: 1 all, 0.5 half (1.4 becomes 1.2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy")
	float CopyAmount = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (DisplayName = "Limit"))
	bool bLimit = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (EditCondition = "bLimit", EditConditionHides))
	FVector2D MinScale = FVector2D(0.5, 0.5);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (EditCondition = "bLimit", EditConditionHides))
	FVector2D MaxScale = FVector2D(2.0, 2.0);

	virtual bool Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose) override;
	virtual void GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const override;
};

/** Puts the widget where its target is, turned and scaled as it is - pinned to it, as a child of it would be. */
UCLASS(meta = (DisplayName = "Copy Transform"))
class NAMETBD_API UWidgetCopyTransformConstraint : public UWidgetConstraint
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	FWidgetConstraintTarget Target;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy", meta = (DisplayName = "Copy Position"))
	bool bCopyPosition = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy", meta = (DisplayName = "Copy Rotation"))
	bool bCopyRotation = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy", meta = (DisplayName = "Copy Scale"))
	bool bCopyScale = true;

	/** Where the widget sits relative to the target, in the target's own (turned, scaled) units - a badge on a card's corner. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy")
	FVector2D Offset = FVector2D::ZeroVector;

	/** Degrees added to the target's turn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy", meta = (ForceUnits = "Degrees"))
	float AngleOffset = 0.f;

	/** Keeps how it sat relative to the target when it started: it then moves with it as if attached. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Copy")
	bool bKeepStartingOffset = false;

	virtual bool Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose) override;
	virtual void GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const override;
	virtual void GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const override;
	virtual bool SetChannel(EWidgetConstraintChannel Channel, float Value) override;
	virtual bool GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const override;
	virtual void ResetMotion() override;

private:
	FVector2f StartingLocal = FVector2f::ZeroVector;
	float StartingAngle = 0.f;
	bool bHasStarting = false;
};

// =====================================================================================================
// Shaping constraints
// =====================================================================================================

/** How a Distance constraint holds the widget. */
UENUM(BlueprintType)
enum class EWidgetConstraintDistanceMode : uint8
{
	Within UMETA(DisplayName = "Within (Closer)", ToolTip = "No further than Distance from the target: a pupil that can't leave its eye, a leash."),
	Outside UMETA(DisplayName = "Outside (Further)", ToolTip = "No nearer than Distance: icons pushed away from the pointer."),
	Exact UMETA(ToolTip = "Always exactly Distance away, round the target: an orbit at a fixed radius, a needle tip.")
};

/** Keeps the widget within, beyond or at a distance from its target. */
UCLASS(meta = (DisplayName = "Distance"))
class NAMETBD_API UWidgetDistanceConstraint : public UWidgetConstraint
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	FWidgetConstraintTarget Target;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance")
	EWidgetConstraintDistanceMode Mode = EWidgetConstraintDistanceMode::Within;

	/** In Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distance", meta = (ClampMin = "0", UIMax = "400", ForceUnits = "px"))
	float Distance = 40.f;

	virtual bool Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose) override;
	virtual void GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const override;
	virtual void GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const override;
	virtual bool SetChannel(EWidgetConstraintChannel Channel, float Value) override;
	virtual bool GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const override;
};

/** The path a Follow Path constraint runs along. */
UENUM(BlueprintType)
enum class EWidgetConstraintPathSource : uint8
{
	ShapeOutline UMETA(DisplayName = "Shape Outline", ToolTip = "The outline of a shape widget (Path Widget): an Ellipse - a ring - a Rectangle, a Star, a Custom Shape's path, as it is drawn (its Shape Scale and Rotation, and anything moving the shape, included)."),
	WidgetBox UMETA(DisplayName = "Widget Edge", ToolTip = "The edge of any widget (Path Widget): its box, rounded as it is drawn (a rounded Border or Button), or the ellipse in it (Edge Shape)."),
	Circle UMETA(ToolTip = "A circle or ellipse of Radius round the Target (a widget's middle, the parent's, a point)."),
	WidgetPathConnection UMETA(DisplayName = "Widget Path Connection", ToolTip = "A line of a Widget Path Layer (Path Widget), between Connection From and Connection To, as it is routed: an orb travelling along a skill tree's links.")
};

/** How a Follow Path constraint picks its place along the path. */
UENUM(BlueprintType)
enum class EWidgetConstraintPathMode : uint8
{
	Distance UMETA(ToolTip = "Path Position along it (animate it, or let Speed carry it round)."),
	ClosestToTarget UMETA(DisplayName = "Closest To Target", ToolTip = "The point of the path nearest the Target - with the pointer, a knob that slides round a dial under the finger, a bead on a wire.")
};

/** What the edge of a Widget Edge path is. */
UENUM(BlueprintType)
enum class EWidgetConstraintEdgeShape : uint8
{
	Auto UMETA(ToolTip = "As the widget is drawn: rounded corners or a pill read from it, an Ellipse round."),
	Box UMETA(ToolTip = "Its box, square corners."),
	Ellipse UMETA(ToolTip = "The ellipse filling its box.")
};

/** Puts the widget on a path - a ring, a shape's outline, a widget's edge, a Widget Path line - at a place along it. */
UCLASS(meta = (DisplayName = "Follow Path"))
class NAMETBD_API UWidgetFollowPathConstraint : public UWidgetConstraint
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Path")
	EWidgetConstraintPathSource PathSource = EWidgetConstraintPathSource::ShapeOutline;

	/** The shape, widget or Widget Path Layer whose path it follows (any widget of this Widget Blueprint; picking one with Path Source Circle picks the source that fits it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Path", meta = (GetOptions = "GetWidgetOptions"))
	FName PathWidget;

	/** Widget Edge: the edge's shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Path", meta = (EditCondition = "PathSource == EWidgetConstraintPathSource::WidgetBox", EditConditionHides))
	EWidgetConstraintEdgeShape EdgeShape = EWidgetConstraintEdgeShape::Auto;

	/** Widget Path Connection: its first node (picking one makes the Path Source Widget Path Connection). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Path", meta = (GetOptions = "GetWidgetOptions"))
	FName ConnectionFrom;

	/** Widget Path Connection: its second node (picking one makes the Path Source Widget Path Connection). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Path", meta = (GetOptions = "GetWidgetOptions"))
	FName ConnectionTo;

	/** Circle: its middle - and, Closest To Target, what it slides toward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Path")
	FWidgetConstraintTarget Target;

	/** Circle: its radius across and down, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Path", meta = (EditCondition = "PathSource == EWidgetConstraintPathSource::Circle", EditConditionHides))
	FVector2D Radius = FVector2D(60.0, 60.0);

	/** Circle: where it starts, in degrees from the top, clockwise. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Path", meta = (EditCondition = "PathSource == EWidgetConstraintPathSource::Circle", EditConditionHides, ForceUnits = "Degrees"))
	float StartAngle = 0.f;

	/** Moves the path outward (positive) or inward from the outline, in Slate units - a ball riding on a ring's outside. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Path", meta = (UIMin = "-40", UIMax = "40", ForceUnits = "px"))
	float PathOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	EWidgetConstraintPathMode Mode = EWidgetConstraintPathMode::Distance;

	/** How far along the path: 0 its start, 1 its end (Units Fraction), or Slate units (Pixels). Animatable (Constraint track). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (DisplayName = "Path Position"))
	float PathPosition = 0.f;

	/** Path Position in Pixels rather than as a fraction of the path's length. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (DisplayName = "Position In Pixels"))
	bool bPositionInPixels = false;

	/** Carries it along on its own: fractions of the path a second (or Slate units a second, in Pixels). Negative goes back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (EditCondition = "Mode == EWidgetConstraintPathMode::Distance", EditConditionHides))
	float Speed = 0.f;

	/** Past the end comes back in at the start (a closed path always loops); off, it stops at the ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bLoop = true;

	/** Turns the widget to run along the path (as Rive's Orient). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bOrient = false;

	/** Degrees added to the path's direction when Orient is on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (EditCondition = "bOrient", EditConditionHides, ForceUnits = "Degrees"))
	float OrientOffset = 0.f;

	/** Where along the path it is now, 0..1 - what Closest To Target found, or Path Position plus Speed. */
	UFUNCTION(BlueprintPure, Category = "Widget Constraint")
	float GetCurrentFraction() const { return CurrentFraction; }

	UFUNCTION(BlueprintCallable, Category = "Widget Constraint")
	void SetPathPosition(float InPosition) { PathPosition = InPosition; }

	virtual bool Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose) override;
	virtual void GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const override;
	virtual void GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const override;
	virtual bool SetChannel(EWidgetConstraintChannel Channel, float Value) override;
	virtual bool GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const override;
	virtual void ResetMotion() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	bool BuildPath(const FWidgetConstraintContext& Context, TArray<FVector2f>& OutPoints, bool& bOutClosed);

	float Travelled = 0.f;
	float CurrentFraction = 0.f;
	mutable TArray<FVector2f> DebugPath;
	mutable bool bDebugClosed = false;
};

/** Turns the widget to face its target - eyes, a turret, a compass needle, an arrow pointing at the cursor. */
UCLASS(meta = (DisplayName = "Look At"))
class NAMETBD_API UWidgetLookAtConstraint : public UWidgetConstraint
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	FWidgetConstraintTarget Target;

	/** Which way the widget points at rest, in degrees clockwise from right: 0 right, -90 up, 90 down, 180 left. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ForceUnits = "Degrees"))
	float ForwardAngle = 0.f;

	/** Degrees added. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim", meta = (ForceUnits = "Degrees"))
	float AngleOffset = 0.f;

	/** Flips it upside down while it points left, so text or a character stays upright. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim")
	bool bKeepUpright = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (DisplayName = "Limit"))
	bool bLimit = false;

	/** Degrees from its resting angle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (EditCondition = "bLimit", EditConditionHides, ForceUnits = "Degrees"))
	float MinAngle = -60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Limits", meta = (EditCondition = "bLimit", EditConditionHides, ForceUnits = "Degrees"))
	float MaxAngle = 60.f;

	virtual bool Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose) override;
	virtual void GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const override;
	virtual void GetDebugLines(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, TArray<FVector2f>& OutSegments) const override;
	virtual bool SetChannel(EWidgetConstraintChannel Channel, float Value) override;
	virtual bool GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const override;
};

/** The area a Stay Inside constraint keeps the widget in. */
UENUM(BlueprintType)
enum class EWidgetConstraintArea : uint8
{
	Parent UMETA(ToolTip = "The panel the widget is in."),
	Widget UMETA(ToolTip = "Another widget (Area Widget): an eye's white, a slider's track, a play area.")
};

/** Keeps the widget inside an area - a pupil in its eye, a draggable inside its frame - with an optional rubber-band edge. */
UCLASS(meta = (DisplayName = "Stay Inside"))
class NAMETBD_API UWidgetStayInsideConstraint : public UWidgetConstraint
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area")
	EWidgetConstraintArea Area = EWidgetConstraintArea::Parent;

	/** The area, for Widget - any widget of this Widget Blueprint (picking one makes the Area Widget). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area", meta = (GetOptions = "GetWidgetOptions"))
	FName AreaWidget;

	/** The area's shape: its box, the ellipse in it (round eyes), or as drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area")
	EWidgetConstraintEdgeShape AreaShape = EWidgetConstraintEdgeShape::Auto;

	/** Kept this far inside the area's edge, in Slate units (negative lets it out). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area", meta = (UIMin = "-20", UIMax = "40", ForceUnits = "px"))
	float Margin = 0.f;

	/** The whole widget stays in, not just its pivot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area")
	bool bKeepWholeWidgetInside = true;

	/** How far past the edge it may still be pulled, as a fraction of how far it is asked to go: 0 a hard edge, 0.3 a rubber band. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Area", meta = (ClampMin = "0", ClampMax = "1"))
	float Elasticity = 0.f;

	virtual bool Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose) override;
	virtual void GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	const UWidget* GetAreaWidget(const FWidgetConstraintContext& Context) const;
};

// =====================================================================================================
// Driving constraints
// =====================================================================================================

/** What a Remap constraint measures. */
UENUM(BlueprintType)
enum class EWidgetConstraintMeasure : uint8
{
	OffsetX UMETA(DisplayName = "Offset X", ToolTip = "How far right of the widget (where it rests) the target is, in Slate units."),
	OffsetY UMETA(DisplayName = "Offset Y", ToolTip = "How far below the widget the target is, in Slate units."),
	Distance UMETA(ToolTip = "How far the target is from the widget, in Slate units (from its edges with Measure From Edges)."),
	Direction UMETA(ToolTip = "Which way the target is from the widget, in degrees clockwise from right (-180 to 180)."),
	TargetAngle UMETA(DisplayName = "Target Angle", ToolTip = "The target's own turn, degrees (Render Transform angle)."),
	TargetScale UMETA(DisplayName = "Target Scale", ToolTip = "The target's own scale (Render Transform, X)."),
	TargetOpacity UMETA(DisplayName = "Target Opacity", ToolTip = "The target's Render Opacity.")
};

/** What a Remap constraint drives. */
UENUM(BlueprintType)
enum class EWidgetConstraintDrive : uint8
{
	OffsetX UMETA(DisplayName = "Offset X", ToolTip = "Moves it right (Slate units)."),
	OffsetY UMETA(DisplayName = "Offset Y", ToolTip = "Moves it down (Slate units)."),
	Angle UMETA(ToolTip = "Turns it (degrees added)."),
	Scale UMETA(ToolTip = "Scales it (multiplier)."),
	ScaleX UMETA(DisplayName = "Scale X"),
	ScaleY UMETA(DisplayName = "Scale Y"),
	Opacity UMETA(ToolTip = "Multiplies its opacity.")
};

/**
 * Drives one value of the widget from one measurement of its target, mapped from a range to a range (Blender's
 * Transformation constraint, Framer's useTransform): an icon growing as the pointer nears (a dock), a hint fading in as it
 * approaches, a dial's needle turning as a slider moves, a shadow sliding as the light moves.
 */
UCLASS(meta = (DisplayName = "Remap (Drive)"))
class NAMETBD_API UWidgetRemapConstraint : public UWidgetConstraint
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	FWidgetConstraintTarget Target;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	EWidgetConstraintMeasure Measure = EWidgetConstraintMeasure::Distance;

	/** Distance: measured from the widget's edges (0 while over it) rather than its pivot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map", meta = (EditCondition = "Measure == EWidgetConstraintMeasure::Distance", EditConditionHides))
	bool bMeasureFromEdges = true;

	/** The measured range: From X maps to To X, From Y to To Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map", meta = (DisplayName = "From Range"))
	FVector2D FromRange = FVector2D(0.0, 200.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	EWidgetConstraintDrive Drive = EWidgetConstraintDrive::Scale;

	/** The driven range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map", meta = (DisplayName = "To Range"))
	FVector2D ToRange = FVector2D(1.4, 1.0);

	/** Holds the result inside To Range when the measurement is past From Range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	bool bClamp = true;

	/** A curve over 0..1 (the place in From Range) shaping the mapping. Off: straight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map", meta = (DisplayName = "Use Curve"))
	bool bUseCurve = false;

	UPROPERTY(EditAnywhere, Category = "Map", meta = (EditCondition = "bUseCurve", EditConditionHides))
	FRuntimeFloatCurve Curve;

	virtual bool Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose) override;
	virtual void GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const override;
};

/** Where Pointer Parallax measures the pointer. */
UENUM(BlueprintType)
enum class EWidgetConstraintParallaxArea : uint8
{
	Screen UMETA(ToolTip = "The whole User Widget the layer is in (its root): the pointer anywhere on it moves the layers."),
	Widget UMETA(ToolTip = "Another widget (Area Widget): a card whose layers shift as the pointer moves over it.")
};

/**
 * Shifts the widget with the pointer by Depth - layers at different depths make a scene parallax under the cursor (a hero
 * banner, a title screen, a card's art). Also tilts with Tilt.
 */
UCLASS(meta = (DisplayName = "Pointer Parallax"))
class NAMETBD_API UWidgetPointerParallaxConstraint : public UWidgetConstraint
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parallax")
	EWidgetConstraintParallaxArea Area = EWidgetConstraintParallaxArea::Screen;

	/** The area, for Widget - any widget of this Widget Blueprint (picking one makes the Area Widget). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parallax", meta = (GetOptions = "GetWidgetOptions"))
	FName AreaWidget;

	/** How far it moves with the pointer at the area's edge, in Slate units; negative moves against it (the far background). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parallax")
	FVector2D Depth = FVector2D(12.0, 12.0);

	/** Degrees it turns with the pointer across the area (a slight swing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parallax", meta = (ForceUnits = "Degrees"))
	float Tilt = 0.f;

	virtual bool Solve(const FWidgetConstraintContext& Context, const FWidgetConstraintPose& Pose, FWidgetConstraintPose& OutPose) override;
	virtual void GetDependencies(const FWidgetConstraintContext& Context, TArray<const UWidget*>& OutWidgets) const override;
	virtual bool SetChannel(EWidgetConstraintChannel Channel, float Value) override;
	virtual bool GetChannel(EWidgetConstraintChannel Channel, float& OutValue) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	const UWidget* GetAreaWidget(const FWidgetConstraintContext& Context) const;
};

namespace WidgetConstraintMath
{
	/** A pose from a widget's local-to-layout transform: Anchor's place, the turn of its X axis, its axes' lengths. */
	NAMETBD_API FWidgetConstraintPose PoseFromTransform(const FSlateRenderTransform& LocalToHost, const FVector2f& Size, const FVector2f& Anchor);

	/** The shortest signed difference B - A between two angles in degrees (-180..180]. */
	NAMETBD_API float AngleDelta(float A, float B);

	/** The point of a polyline nearest Point: its distance along the line. bClosed joins the last point to the first. */
	NAMETBD_API float ClosestDistance(const TArray<FVector2f>& Points, bool bClosed, const FVector2f& Point);

	/** Point and direction at Distance along a polyline (wrapped on a closed one). */
	NAMETBD_API bool Sample(const TArray<FVector2f>& Points, bool bClosed, float Distance, FVector2f& OutPoint, FVector2f& OutDirection);

	/** A polyline moved Offset along its normals (outward for a closed one, to the left of an open one). */
	NAMETBD_API void OffsetLine(TArray<FVector2f>& Points, bool bClosed, float Offset);
}

/**
 * Any number of a constraint, by name - what the Constraint track keys (as Rive keys any property of a constraint): every
 * number and switch its Details show, named as they show it, a Vector 2D's and a Target's parts after it - "Path Position",
 * "Copy Amount X", "Offset Y", "Radius X", "Target Point Y", "Target Anchor X", "Elasticity", "Orient" (a switch: on above 0.5).
 * Names are worked out from the properties themselves (FName::NameToDisplayString), so they hold in a cooked game too.
 */
namespace WidgetConstraintValues
{
	/** Every name a constraint of ConstraintClass can be animated by, in the order its Details list them. */
	NAMETBD_API void GetNames(const UClass* ConstraintClass, TArray<FName>& OutNames);

	/** Every name any kind of constraint can be animated by, each once - what the Constraint track's Value dropdown lists. */
	NAMETBD_API void GetAllNames(TArray<FName>& OutNames);

	NAMETBD_API bool Get(const UWidgetConstraint* Constraint, FName ValueName, float& OutValue);

	/** Writes it (a switch: on above 0.5, a whole number: rounded). False when the constraint has no such value. */
	NAMETBD_API bool Set(UWidgetConstraint* Constraint, FName ValueName, float Value);
}
