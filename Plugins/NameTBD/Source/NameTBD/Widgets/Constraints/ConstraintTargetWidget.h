// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Widgets/SLeafWidget.h"

#include "ConstraintTargetWidget.generated.h"

/** The Slate side of a Constraint Target: a point with a size, drawn as a crosshair in the designer (or always, Show At Runtime). */
class NAMETBD_API SConstraintTargetMarker : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SConstraintTargetMarker) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetMarker(const FVector2D& InMarkerSize, const FLinearColor& InMarkerColor, bool bInDrawn);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return MarkerSize; }

private:
	FVector2D MarkerSize = FVector2D(16.0, 16.0);
	FLinearColor MarkerColor = FLinearColor(1.f, 0.35f, 0.85f, 0.9f);
	bool bDrawn = true;
};

/**
 * Constraint Target: an invisible point to tie constraints to and animate - the way Rive rigs use a target group, Blender an
 * Empty and Spine a target bone. Place it in the tree, point constraints at it (Copy Position, Look At, Distance, Follow Path's
 * Circle centre, Remap...) or Pointer Effects (Pointer Source: Widget), and key its Transform track: every key is a 2D place
 * with the timeline's easing, springs and value inputs, shown on its motion path and dragged with the gizmo.
 *
 * Eyes: on each pupil, Copy Position -> Widget LookTarget (Copy Amount 1) then Stay Inside the eye (Ellipse). Key LookTarget's
 * position and both pupils look there, kept in their sockets; their motion paths show the look inside each eye. Give
 * LookTarget its own Copy Position -> Pointer (While Animated: Let Go) and it follows the cursor between keyed glances.
 *
 * Draws nothing in a game (Show At Runtime off) and is never hit: it only has a place and a size. Its pivot (Render Transform
 * Pivot, the middle by default) is the point constraints read with Anchor (0.5, 0.5).
 */
UCLASS(meta = (DisplayName = "Constraint Target"))
class NAMETBD_API UConstraintTargetWidget : public UWidget
{
	GENERATED_BODY()

public:
	UConstraintTargetWidget();

	/** Its size in Slate units: what it asks its panel for, and the box drawn in the designer. Anchors on it are fractions of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint Target")
	FVector2D MarkerSize = FVector2D(16.0, 16.0);

	/** The crosshair's colour in the designer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint Target")
	FLinearColor MarkerColor = FLinearColor(1.f, 0.35f, 0.85f, 0.9f);

	/** Draws the crosshair in a game too (debugging a rig). Off: seen only in the designer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Constraint Target")
	bool bShowAtRuntime = false;

	/** Moves it so its middle is at Point, in its parent's units from where it is laid out (sets the Render Translation). */
	UFUNCTION(BlueprintCallable, Category = "Constraint Target")
	void SetTargetOffset(FVector2D Point);

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	TSharedPtr<SConstraintTargetMarker> MyMarker;
};
