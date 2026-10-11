// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Framework/Commands/InputChord.h"

#include "WidgetGizmoSettings.generated.h"

/**
 * The Widget Gizmo's per-user preferences: Editor Preferences > Plugins > Widget Gizmo.
 *
 * The gizmo moves, scales, rotates and shears the widgets selected in the UMG designer through their Render Transform, about
 * their Render Transform Pivot - which is moved by dragging the gizmo itself. See WidgetGizmo.h.
 *
 * Per user and per project (EditorPerProjectUserSettings), like the Widget Animations Editor's preferences: one person's keys
 * never land in anyone else's editor.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "Widget Gizmo"))
class UWidgetGizmoSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UWidgetGizmoSettings();

	/**
	 * Shows the gizmo over every widget selected in the UMG designer. Off: no gizmo, and its keys are left to the editor.
	 * Hide Gizmo (H) only hides it until it is pressed again; this turns it off for good.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Gizmo", meta = (DisplayName = "Enable Widget Gizmo"))
	bool bEnableGizmo = true;

	/**
	 * Shows the row of chips above the selected widget - Move, Scale, Rotate, Shear and Hide with their keys, and Auto Key while
	 * the open animation is keyed. Off: only the gizmo itself; the keys still work, and what is typed still shows.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Gizmo", meta = (DisplayName = "Show Mode Chips"))
	bool bShowChips = true;

	/** How big the gizmo is drawn, whatever the designer's zoom: 1 is the default size. */
	UPROPERTY(config, EditAnywhere, Category = "Gizmo", meta = (DisplayName = "Gizmo Size", ClampMin = "0.5", ClampMax = "3", UIMin = "0.5", UIMax = "3"))
	float GizmoSize = 1.f;

	/**
	 * Grab: the selected widgets follow the mouse without a button held, until a click or Enter sets them (Escape or a right
	 * click puts them back). X or Y keeps them to that axis; typing a number sets the position instead (X / Y / Tab pick which).
	 * Only while the designer has the keyboard or the mouse, and a widget is selected.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Keys", meta = (DisplayName = "Grab"))
	FInputChord GrabKey;

	/**
	 * Switches the gizmo to Scale - handles round the widget - and takes a typed scale: a number sets both (S 1.5), or X / Y
	 * then a number one of them (S X 1.5 Y 1.25); Enter sets it. Pressed again with nothing typed: back to Move.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Keys", meta = (DisplayName = "Scale"))
	FInputChord ScaleKey;

	/** Switches the gizmo to Rotate - a ring about the pivot - and takes a typed angle (R 90, Enter). Pressed again with nothing typed: back to Move. */
	UPROPERTY(config, EditAnywhere, Category = "Keys", meta = (DisplayName = "Rotate"))
	FInputChord RotateKey;

	/** Switches the gizmo to Shear - a grip on each edge - and takes a typed shear (Z X 10 Y 5, Enter). Pressed again with nothing typed: back to Move. */
	UPROPERTY(config, EditAnywhere, Category = "Keys", meta = (DisplayName = "Shear"))
	FInputChord ShearKey;

	/** The key that hides the gizmo, and shows it again. Hidden, the designer gets every press as if there were no gizmo. */
	UPROPERTY(config, EditAnywhere, Category = "Keys", meta = (DisplayName = "Hide Gizmo"))
	FInputChord HideKey;

	/**
	 * While rotating, the angle catches on every multiple of this many degrees - 45: 0, 45, 90... - when it comes within Rotation
	 * Magnet Range of one, so square angles are easy to hit without holding anything. 90 catches only the quarter turns. 0: off.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Snapping", meta = (DisplayName = "Rotation Magnet", ClampMin = "0", ClampMax = "180", UIMin = "0", UIMax = "90", Units = "Degrees"))
	float RotationMagnetAngle = 45.f;

	/** How close, in degrees, the angle has to come to a Rotation Magnet angle to catch on it. */
	UPROPERTY(config, EditAnywhere, Category = "Snapping", meta = (DisplayName = "Rotation Magnet Range", ClampMin = "0.1", ClampMax = "15", UIMin = "0.5", UIMax = "10", Units = "Degrees", EditCondition = "RotationMagnetAngle > 0"))
	float RotationMagnetRange = 4.f;

	/** While rotating with Shift held, the angle goes in steps of this many degrees. */
	UPROPERTY(config, EditAnywhere, Category = "Snapping", meta = (DisplayName = "Rotation Snap (Shift)", ClampMin = "0.1", ClampMax = "90", UIMin = "1", UIMax = "90", Units = "Degrees"))
	float RotationSnap = 15.f;

	/**
	 * Smart guides, as design tools have them: while moving or scaling a widget, its edges and middle catch on the edges and middle
	 * of the panel it is in and of the widgets beside it in that panel, and a line shows what it lines up with. Ctrl held: free.
	 * Only for a widget drawn upright (no rotation or shear on it or above it), where edges line up on screen.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Snapping", meta = (DisplayName = "Smart Guides"))
	bool bSmartGuides = true;

	/** How close, in pixels on screen, an edge or middle has to come to a guide to catch on it. */
	UPROPERTY(config, EditAnywhere, Category = "Snapping", meta = (DisplayName = "Smart Guide Distance", ClampMin = "1", ClampMax = "24", UIMin = "2", UIMax = "16", EditCondition = "bSmartGuides"))
	float SmartGuideDistance = 6.f;

	/** While shearing with Shift held, the shear goes in steps of this many degrees. */
	UPROPERTY(config, EditAnywhere, Category = "Snapping", meta = (DisplayName = "Shear Snap (Shift)", ClampMin = "0.1", ClampMax = "45", UIMin = "1", UIMax = "45", Units = "Degrees"))
	float ShearSnap = 5.f;

	/**
	 * While moving the pivot, it snaps to the widget's corners, edge middles and middle when it comes within this many pixels
	 * of one (Ctrl held: no snapping). 0: never.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Snapping", meta = (DisplayName = "Pivot Snap Distance", ClampMin = "0", ClampMax = "32", UIMin = "0", UIMax = "32"))
	float PivotSnapDistance = 8.f;

	static const UWidgetGizmoSettings& Get() { return *GetDefault<UWidgetGizmoSettings>(); }

	// UDeveloperSettings
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("WidgetGizmo"); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif
};
