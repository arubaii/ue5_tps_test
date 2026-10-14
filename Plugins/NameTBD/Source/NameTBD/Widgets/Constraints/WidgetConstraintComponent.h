// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Extensions/UIComponent.h"
#include "Misc/EngineVersionComparison.h"
#include "Rendering/SlateRenderTransform.h"
#include "Slate/WidgetTransform.h"
#include "Templates/SubclassOf.h"
#include "Widgets/SCompoundWidget.h"
#include "NameTBD/Widgets/Constraints/WidgetConstraints.h"

#include "WidgetConstraintComponent.generated.h"

class UWidget;

/**
 * The Slate side of a widget's Constraints: wraps the widget's own Slate widget and arranges it with the solved extra transform
 * over its own Render Transform (so it is drawn and hit-tested where its constraints put it), and with the solved opacity. In
 * the designer it draws what each constraint is tied to.
 */
class NAMETBD_API SWidgetConstraintHost : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWidgetConstraintHost) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The extra transform (about the widget's pivot, over its own) and opacity this frame. bInActive false: none. */
	void SetSolved(const FSlateRenderTransform& InExtra, bool bInActive, float InOpacity);

	/** Line segments (pairs of points, local space) drawn over the widget - the designer's view of its constraints. */
	void SetDebugSegments(TArray<FVector2f>&& InSegments);

	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FSlateRenderTransform Extra;
	bool bActive = false;
	float SolvedOpacity = 1.f;
	TArray<FVector2f> DebugSegments;
};

/**
 * Constraints: a UI Component that ties a widget's place, turn, size and opacity to other widgets, the pointer, paths and
 * areas - add it with the designer's Add Component on the widget's Details, then add constraints to its list (Copy Position,
 * Copy Rotation, Copy Scale, Copy Transform, Distance, Follow Path, Look At, Stay Inside, Remap, Pointer Parallax). They are
 * applied in order every frame, each from where the last left the widget, and blended by Strength.
 *
 * The widget's Render Transform is never written: the component draws the widget with an extra transform over it, so Widget
 * Animations, the designer and Blueprint keep owning it. Widgets constrained to other constrained widgets are solved after
 * them each frame, so chains (a tail of dots following the cursor) don't lag a frame per link. Solved in the designer too
 * (Solve In Designer), where the pointer isn't there and the lines each constraint is tied to are drawn (Show In Designer).
 *
 * Constraints and Widget Animations work together: each constraint says what it does while an animation moves the widget
 * (While Animated - act over it, let go, or follow the keys), and the designer's motion paths draw where the constraints put
 * the widget, so keys can be placed where it will really be.
 *
 * Recipes: a ball round a ring - Follow Path, Shape Outline of an Ellipse, Speed 0.25 (or a Constraint track on Path
 * Position). Eyes following the cursor - on each pupil: Copy Position to the Pointer (Copy Amount 0.15, Smooth, While
 * Animated Follow the Keys) then Stay Inside the eye (Ellipse): keyed looks then play through the same spring and socket. A
 * magnetic button - Copy Position to the Pointer, Copy Amount 0.3, Activation Near Pointer, Limit 12, Smooth with Bounce. A
 * knob dragged round a dial - Follow Path, Closest To Target, target the Pointer, Activation While Pressed (Hold Last).
 */
UCLASS(DefaultToInstanced, EditInlineNew, BlueprintType, meta = (DisplayName = "Constraints"))
class NAMETBD_API UWidgetConstraintComponent : public UUIComponent
{
	GENERATED_BODY()

public:
	UWidgetConstraintComponent();

	/** Applied in order, each from where the last left the widget. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Constraints")
	TArray<TObjectPtr<UWidgetConstraint>> Constraints;

	/** Every constraint's strength at once: 0 lets the widget go entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraints", meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 1.f;

	/** Solves the constraints in the designer's preview too (the pointer is never there). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraints")
	bool bSolveInDesigner = true;

	/** In the designer, draws a line to what each constraint is tied to, and the paths and limits it keeps to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraints")
	bool bShowInDesigner = true;

	/** The constraint named InName - its Name, else its kind's name ("Follow Path") - or the first when None. */
	UFUNCTION(BlueprintPure, Category = "Widget Constraints")
	UWidgetConstraint* FindConstraint(FName InName) const;

	/** The names the constraints can be found by (their Name, else their kind's) - what a Constraint track's dropdown lists. */
	UFUNCTION(BlueprintPure, Category = "Widget Constraints")
	TArray<FName> GetConstraintNames() const;

	UFUNCTION(BlueprintPure, Category = "Widget Constraints")
	UWidgetConstraint* GetConstraintAt(int32 Index) const;

	/** Adds a constraint of a kind at the end of the list, at runtime. Set it up from what this returns. */
	UFUNCTION(BlueprintCallable, Category = "Widget Constraints", meta = (DeterminesOutputType = "ConstraintClass"))
	UWidgetConstraint* AddConstraint(TSubclassOf<UWidgetConstraint> ConstraintClass, FName InName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Widget Constraints")
	bool RemoveConstraint(FName InName);

	UFUNCTION(BlueprintCallable, Category = "Widget Constraints")
	bool SetConstraintStrength(FName InName, float InStrength);

	UFUNCTION(BlueprintCallable, Category = "Widget Constraints")
	bool SetConstraintEnabled(FName InName, bool bInEnabled);

	/** Sets a constraint's animatable value (Strength, Path Position, Offset, Angle Offset, Distance). */
	UFUNCTION(BlueprintCallable, Category = "Widget Constraints")
	bool SetConstraintValue(FName InName, EWidgetConstraintChannel Channel, float Value);

	UFUNCTION(BlueprintPure, Category = "Widget Constraints")
	bool GetConstraintValue(FName InName, EWidgetConstraintChannel Channel, float& OutValue) const;

	/** Every constraint's strength at once. */
	UFUNCTION(BlueprintCallable, Category = "Widget Constraints")
	void SetStrength(float InStrength) { Strength = FMath::Clamp(InStrength, 0.f, 1.f); }

	/** Snaps every constraint's smoothing and fades to where they are going. */
	UFUNCTION(BlueprintCallable, Category = "Widget Constraints")
	void ResetConstraintMotion();

	/** Where the constraints put the widget last frame, in its layout space (its pivot's place, turn, scale, opacity). */
	UFUNCTION(BlueprintPure, Category = "Widget Constraints")
	FWidgetConstraintPose GetSolvedPose() const { return SolvedPose; }

	UFUNCTION(BlueprintPure, Category = "Widget Constraints")
	UWidget* GetConstrainedWidget() const;

	/** The Constraints component on a widget, or null. */
	UFUNCTION(BlueprintPure, Category = "Widget Constraints", meta = (DisplayName = "Get Widget Constraints"))
	static UWidgetConstraintComponent* FindConstraintComponent(const UWidget* Widget);

	/** Stops solving (the module shutting down). */
	static void ShutdownSolver();

	/**
	 * Where the constraints would draw the widget if its own Render Transform were OwnTransform, as a Render Transform -
	 * worked out with no lasting effect (no smoothing, fades or remembered offsets; no pointer at design time). What the
	 * designer's motion paths draw for a constrained widget. False when it can't be worked out (not laid out yet).
	 */
	bool PreviewTransform(const FWidgetTransform& OwnTransform, FWidgetTransform& OutTransform);

	/** The widget's Render Transform as authored: its Widget Blueprint's own widget's (in the designer), else its archetype's. */
	static FWidgetTransform GetAuthoredTransform(const UWidget* Widget);

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
	static void SolveAll(float DeltaTime);
	void GatherDependencies(TArray<const UWidget*>& OutWidgets) const;
	void SolveFrame(float DeltaTime, const TMap<const UWidget*, UWidgetConstraintComponent*>& ByOwner);

	/** Registers it so it is solved and found (Find Constraint Component, the Constraint track) - from when it is initialized. */
	void Register();

	/** The context for solving the widget with its own Render Transform at Own: false when it isn't laid out. */
	bool MakeContext(const FWidgetTransform& Own, float DeltaTime, const TMap<const UWidget*, UWidgetConstraintComponent*>* ByOwner, FWidgetConstraintContext& OutContext) const;

	/** Whether any enabled constraint follows the animation (While Animated: Follow the Keys). */
	bool AnyFollowsAnimation() const;

	/** Seconds since the widget was last seen animated - it stays "animated" a moment, so a key passing through rest doesn't flick the target. */
	float SinceAnimated = 1000.f;

	/** The widget's authored Render Transform (see GetAuthoredTransform), its template looked up once. */
	FWidgetTransform ReadAuthoredTransform() const;
	mutable TWeakObjectPtr<const UWidget> AuthoredSource;
	mutable TWeakObjectPtr<const UWidget> AuthoredFor;

	TWeakPtr<SWidgetConstraintHost> MyHost;

	/** What it solved: the widget's local space to desktop space as drawn, its size, and its pose - read by what depends on it. */
	FSlateRenderTransform SolvedLocalToDesktop;
	FVector2f SolvedSize = FVector2f::ZeroVector;
	FWidgetConstraintPose SolvedPose;
	bool bHasSolved = false;
	uint64 SolvedFrame = 0;

	/** A left press that started over the widget, still held (While Pressed). */
	bool bPressStartedOver = false;
	bool bWasDown = false;
};
