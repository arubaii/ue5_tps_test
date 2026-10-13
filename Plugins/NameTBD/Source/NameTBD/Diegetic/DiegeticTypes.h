// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Interface.h"
#include "DiegeticTypes.generated.h"

class APlayerController;
class UDiegeticScreenComponent;
class USoundAttenuation;

// =====================================================================================================
// Diegetic UI
// =====================================================================================================
//
// A Diegetic Screen (UDiegeticScreenComponent) is a Widget Component made for UI that lives in the world: a terminal, a
// wrist display, a shop sign, a hologram. It is laid out in centimetres, draws as often as it is worth drawing (by distance,
// by whether anything on it moves, inside a per-frame budget shared by every screen), wakes and sleeps as the player comes
// and goes, and is pointed at and clicked through a Diegetic Interactor on the player's pawn - added on its own unless the
// project turns that off. The widget on it is an ordinary Widget Blueprint: the plugin's buttons, Widget Animations, state
// machines and sounds work on it as on the HUD, and its sounds play from where it is in the world.
//
// Not every diegetic UI is a flat panel on a wall. A screen is shown on its own Panel (flat or curved) or on a Mesh Surface (the
// material slot of any mesh: a monitor model, a wrist device, a gun's ammo counter), placed As Placed, facing the camera, or
// floating In Front Of The Player (a projected menu), and looked through a Screen Look (clean UMG, terminal, hologram, frosted
// glass, LCD) drawn by the plugin's own screen materials. Quick Setup sets all of that for the common kinds of diegetic UI at
// once. Screens on one actor can share a Focus Group (a control desk of several monitors) and a Context object (the data they
// show: the weapon, the door, the room).
//
// See DiegeticScreenComponent.h, DiegeticInteractorComponent.h and DiegeticUISubsystem.h, and Docs/Design/DiegeticUI.md.

/**
 * How often a Diegetic Screen redraws its widget. A redraw renders the whole widget into the screen's texture and ticks
 * it - its timers, its animations - so a screen that redraws less costs less, and its widget's tick runs less often.
 * Widget Animations keep their own clock either way; they are only seen as often as the screen redraws.
 */
UENUM(BlueprintType)
enum class EDiegeticUpdateMode : uint8
{
	Realtime UMETA(DisplayName = "Every Frame", ToolTip = "Redraws every frame it is seen, whatever its distance, outside the per-frame budget: a hero screen, a screen showing a live feed. What a plain Widget Component does."),

	Throttled UMETA(DisplayName = "By Distance", ToolTip = "Redraws at the rate its distance from the camera asks for (Near / Mid / Far), within the per-frame budget; every frame while a player points at it or uses it."),

	OnDemand UMETA(DisplayName = "When Changed", ToolTip = "Redraws only when something on it moves: a Widget Animation or a UMG animation playing, a player pointing at it or using it (and Settle Time after), Mark Content Changed called, and Heartbeat Rate, if set. For screens that are still most of the time: signs, menus, readouts that change now and then.")
};

/** What a Diegetic Screen does while it is asleep - see FDiegeticPresenceSettings. */
UENUM(BlueprintType)
enum class EDiegeticSleepBehavior : uint8
{
	FreezeLastFrame UMETA(DisplayName = "Freeze Last Frame", ToolTip = "Stops redrawing once its widget has finished playing out: the screen keeps showing its last frame, and costs nothing."),

	Hide UMETA(ToolTip = "Stops redrawing once its widget has finished playing out, and hides the screen. It is shown again as it wakes, before its widget plays in."),

	KeepUpdating UMETA(DisplayName = "Keep Updating", ToolTip = "Keeps redrawing as its Update Mode says: asleep is only a state its widget is told about.")
};

/** How a player uses a Diegetic Screen. */
UENUM(BlueprintType)
enum class EDiegeticUseMode : uint8
{
	PointAndClick UMETA(DisplayName = "Point And Click", ToolTip = "The player points at it - with the camera's aim, or the cursor - and clicks what is under the point. Nothing else changes: the player keeps moving and looking."),

	FocusView UMETA(DisplayName = "Focus View", ToolTip = "A click on it takes the player into it: the camera moves in front of it, the player stops moving, the mouse cursor shows (or, with a gamepad, focus goes to its widget and the D-pad moves it) until Back is pressed. A terminal, a computer, an arcade cabinet."),

	None UMETA(DisplayName = "Not Usable", ToolTip = "Shown only: pointing at it does nothing, and nothing on it can be clicked.")
};

/** How a Diegetic Screen's size is set. */
UENUM(BlueprintType)
enum class EDiegeticScreenSizing : uint8
{
	DrawSize UMETA(DisplayName = "Draw Size", ToolTip = "As a Widget Component: the widget is laid out at Draw Size, and one pixel of it is one centimetre at a scale of 1."),

	PhysicalSize UMETA(DisplayName = "Physical Size", ToolTip = "The screen is Physical Size centimetres wide and high, with Pixel Density pixels to the centimetre: Draw Size and the component's scale (Y and Z) are worked out from them. Change the density to make the text crisper or cheaper without changing the screen's size.")
};

/**
 * The common kinds of diegetic UI. Picking one sets the screen's Look, Display, Placement, Use, Presence and Updates for that
 * kind at once (Apply Quick Setup); every setting can be changed afterwards. Custom changes nothing.
 */
UENUM(BlueprintType)
enum class EDiegeticScreenKind : uint8
{
	Custom UMETA(ToolTip = "Changes nothing: the settings below are as set."),

	WallTerminal UMETA(DisplayName = "Wall Terminal", ToolTip = "A computer terminal the player walks up to and uses (Fallout): green phosphor CRT look, Focus View, wakes as the player comes near."),

	Monitor UMETA(ToolTip = "A screen on a modelled monitor, TV or console (Alien: Isolation): shown on the mesh's screen material slot, clean look, Point And Click."),

	Hologram UMETA(ToolTip = "A projected hologram in the world (Dead Space): additive, scan lines, glitch, faces the camera's direction (yaw), wakes as the player comes near."),

	HologramMenu UMETA(DisplayName = "Hologram Menu", ToolTip = "A menu projected in front of the player that keeps the game running (Dead Space inventory): floats in front of the camera, hologram look, used without moving the camera, hidden until shown (Set Screen Shown)."),

	DeviceDisplay UMETA(DisplayName = "Device Display", ToolTip = "A small display on equipment - a wrist device, an ammo counter, a suit readout (Pip-Boy, Dead Space RIG): attach the screen (or the mesh it shows on) to a socket. LCD look, not clickable, redraws only when it changes."),

	Sign UMETA(ToolTip = "A sign, kiosk or room label: clean look, not clickable, redraws only when it changes, freezes far away."),

	GlassPanel UMETA(DisplayName = "Glass Panel", ToolTip = "A sci-fi glass panel: frosted glass that blurs the world behind it, Point And Click.")
};

/** Where a Diegetic Screen's picture is shown. */
UENUM(BlueprintType)
enum class EDiegeticDisplay : uint8
{
	Panel UMETA(ToolTip = "On its own panel - flat, or curved (Geometry Mode Cylinder) - the Widget Component's."),

	MeshSurface UMETA(DisplayName = "Mesh Surface", ToolTip = "On a material slot of a mesh (a monitor model, a wrist device, a gun): the slot shows the screen, in the slot's UV space (UV Region of UV channel 0). Its own panel isn't drawn. Clicking it needs Project Settings > Physics > Support UV From Hit Results, and complex collision on the mesh. Point this component's X axis out of the display face: Focus View and facing tests use it.")
};

/** Where a Diegetic Screen puts itself each frame. */
UENUM(BlueprintType)
enum class EDiegeticPlacement : uint8
{
	AsPlaced UMETA(DisplayName = "As Placed", ToolTip = "Where it is put (or attached: to a socket, to a moving actor)."),

	FaceCamera UMETA(DisplayName = "Face Camera", ToolTip = "Where it is put, turned to face the player's camera."),

	FaceCameraYaw UMETA(DisplayName = "Face Camera (Upright)", ToolTip = "Where it is put, turned about its up axis only to face the player's camera: it stays upright."),

	InFrontOfPlayer UMETA(DisplayName = "In Front Of Player", ToolTip = "Floats in front of the first local player's camera, Follow Distance away and Follow Offset aside, catching up with Follow Lag: a menu projected in front of the player.")
};

/** Which material a Diegetic Screen is drawn with. */
UENUM(BlueprintType)
enum class EDiegeticMaterialMode : uint8
{
	Automatic UMETA(ToolTip = "The plugin's own screen material for its Screen Look: matches the widget as UMG draws it (premultiplied alpha, scene exposure undone, no fog, no motion blur smear), and draws every look - terminal, hologram, frosted glass with background blur. Made in /NameTBD/Diegetic the first time the editor needs it."),

	EngineDefault UMETA(DisplayName = "Engine Default", ToolTip = "The Widget Component's own materials, chosen by Blend Mode and Two Sided. No Screen Look."),

	Custom UMETA(ToolTip = "Element 0 of Materials, as a Widget Component uses it. Screen Look's parameters are still handed to it: a material with parameters of the same names follows them (copy the plugin's with Copy Screen Material To Project).")
};

/** A ready-made Screen Look. Picking one sets the look's values; Custom leaves them as set. */
UENUM(BlueprintType)
enum class EDiegeticLookPreset : uint8
{
	Clean UMETA(ToolTip = "Exactly as UMG draws it: no effects."),

	TerminalGreen UMETA(DisplayName = "Terminal (Green)", ToolTip = "A green phosphor CRT terminal: monochrome, glow, scan lines, a rolling bar, curvature, vignette, flicker and grain."),

	TerminalAmber UMETA(DisplayName = "Terminal (Amber)", ToolTip = "An amber phosphor CRT terminal."),

	Hologram UMETA(ToolTip = "A projected hologram: additive light, cyan tint, moving scan lines, glitches, fading at grazing angles."),

	FrostedGlass UMETA(DisplayName = "Frosted Glass", ToolTip = "Glass that blurs and tints the world behind the screen's transparent parts."),

	LCDPanel UMETA(DisplayName = "LCD Panel", ToolTip = "A flat LCD: a fine pixel grid and a little glow."),

	Custom UMETA(ToolTip = "The look's values as set.")
};

/** How often a Diegetic Screen redraws. See EDiegeticUpdateMode. */
USTRUCT(BlueprintType)
struct NAMETBD_API FDiegeticUpdateSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates")
	EDiegeticUpdateMode UpdateMode = EDiegeticUpdateMode::Throttled;

	/** Within this distance of the camera (cm), Near Rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates", meta = (ClampMin = "0", UIMin = "0", UIMax = "5000", ForceUnits = "cm", EditCondition = "UpdateMode == EDiegeticUpdateMode::Throttled", EditConditionHides))
	float NearDistance = 500.f;

	/** Redraws a second within Near Distance. 0: every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates", meta = (ClampMin = "0", UIMin = "0", UIMax = "120", EditCondition = "UpdateMode == EDiegeticUpdateMode::Throttled", EditConditionHides))
	float NearRate = 0.f;

	/** Within this distance (cm), and past Near Distance, Mid Rate. Past it, Far Rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates", meta = (ClampMin = "0", UIMin = "0", UIMax = "10000", ForceUnits = "cm", EditCondition = "UpdateMode == EDiegeticUpdateMode::Throttled", EditConditionHides))
	float MidDistance = 2000.f;

	/** Redraws a second between Near Distance and Mid Distance. 0: every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates", meta = (ClampMin = "0", UIMin = "0", UIMax = "120", EditCondition = "UpdateMode == EDiegeticUpdateMode::Throttled", EditConditionHides))
	float MidRate = 20.f;

	/** Redraws a second past Mid Distance. 0: every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates", meta = (ClampMin = "0", UIMin = "0", UIMax = "120", EditCondition = "UpdateMode == EDiegeticUpdateMode::Throttled", EditConditionHides))
	float FarRate = 4.f;

	/**
	 * Past this distance from the camera (cm) the screen stops redrawing and keeps its last frame. 0: no limit. The screen is
	 * still drawn in the world; Cull Distance hides it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates", meta = (ClampMin = "0", UIMin = "0", UIMax = "20000", ForceUnits = "cm", EditCondition = "UpdateMode != EDiegeticUpdateMode::Realtime"))
	float MaxUpdateDistance = 5000.f;

	/** Past this distance from the camera (cm) the screen isn't drawn at all, nor redrawn. 0: never culled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates", meta = (ClampMin = "0", UIMin = "0", UIMax = "50000", ForceUnits = "cm"))
	float CullDistance = 0.f;

	/** When Changed: redraws a second whatever happens, to catch a change nothing reported. 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates", meta = (ClampMin = "0", UIMin = "0", UIMax = "10", EditCondition = "UpdateMode == EDiegeticUpdateMode::OnDemand", EditConditionHides))
	float HeartbeatRate = 1.f;

	/**
	 * When Changed: seconds it keeps redrawing after the last change - a pointer leaving, an animation ending - so a hover's
	 * fade or a button's blend back is seen to the end.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates", meta = (ClampMin = "0", UIMin = "0", UIMax = "3", ForceUnits = "s", EditCondition = "UpdateMode == EDiegeticUpdateMode::OnDemand", EditConditionHides))
	float SettleTime = 0.6f;

	/** Redraws every frame while a player points at it or uses it, whatever its distance or mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates")
	bool bFullRateWhileInteracting = true;

	/** A one-sided screen skips its redraws while every camera is behind it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates")
	bool bSkipWhenFacingAway = true;

	/** Which screens redraw first when more want to than the per-frame budget allows (Project Settings > Diegetic UI). Higher first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Updates", meta = (UIMin = "-10", UIMax = "10"))
	int32 BudgetPriority = 0;
};

/**
 * Whether a Diegetic Screen is awake: the player is near. A screen waking plays its widget in - the widget's Widget
 * Animation Component's Anim In - and tells it (Diegetic Screen Widget interface, the ScreenAwake state machine input); one
 * falling asleep plays it out, then does what While Asleep says.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FDiegeticPresenceSettings
{
	GENERATED_BODY()

	/** Within this distance of a player's pawn (cm), awake. 0: always awake. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presence", meta = (ClampMin = "0", UIMin = "0", UIMax = "5000", ForceUnits = "cm"))
	float WakeDistance = 0.f;

	/** How much farther than Wake Distance the player has to go before it sleeps again (cm), so it doesn't flicker on the edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presence", meta = (ClampMin = "0", UIMin = "0", UIMax = "1000", ForceUnits = "cm", EditCondition = "WakeDistance > 0"))
	float SleepMargin = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presence", meta = (DisplayName = "While Asleep", EditCondition = "WakeDistance > 0"))
	EDiegeticSleepBehavior SleepBehavior = EDiegeticSleepBehavior::FreezeLastFrame;

	/** Plays the widget's Anim In as it wakes and its Anim Out as it sleeps (its Widget Animation Component's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Presence", meta = (DisplayName = "Play Anim In / Out", EditCondition = "WakeDistance > 0"))
	bool bPlayAnimInOut = true;
};

/** How a player uses a Diegetic Screen. See EDiegeticUseMode. */
USTRUCT(BlueprintType)
struct NAMETBD_API FDiegeticUseSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Use")
	EDiegeticUseMode UseMode = EDiegeticUseMode::PointAndClick;

	/** How far from the player's pawn it can be pointed at and clicked (cm). 0: the Diegetic Interactor's Interaction Distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Use", meta = (ClampMin = "0", UIMin = "0", UIMax = "2000", ForceUnits = "cm", EditCondition = "UseMode != EDiegeticUseMode::None"))
	float ReachDistance = 0.f;

	/** Focus View: how much of the view the screen fills, across or up whichever is tighter (0.5 to 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Use", meta = (ClampMin = "0.2", ClampMax = "1.0", UIMin = "0.5", UIMax = "1.0", EditCondition = "UseMode == EDiegeticUseMode::FocusView", EditConditionHides))
	float ViewFill = 0.85f;

	/** Focus View: seconds the camera takes to move in, and out again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Use", meta = (ClampMin = "0", UIMin = "0", UIMax = "2", ForceUnits = "s", EditCondition = "UseMode == EDiegeticUseMode::FocusView", EditConditionHides))
	float CameraBlendTime = 0.45f;

	/** Focus View: moves the camera from where it is worked out to be, in the screen's own space (X out of its face, Y left, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Use", meta = (EditCondition = "UseMode == EDiegeticUseMode::FocusView", EditConditionHides))
	FVector CameraOffset = FVector::ZeroVector;

	/** Focus View: the player's pawn doesn't move or turn while the screen is in use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Use", meta = (EditCondition = "UseMode == EDiegeticUseMode::FocusView", EditConditionHides))
	bool bBlockPawnInput = true;

	/** Focus View: the mouse cursor shows while the screen is in use, and points at it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Use", meta = (EditCondition = "UseMode == EDiegeticUseMode::FocusView", EditConditionHides))
	bool bShowMouseCursor = true;

	/**
	 * Focus View: the camera moves in front of the screen. Off: the camera stays where it is and only the input goes to the screen
	 * - a menu projected in front of the player (Dead Space), a wrist device raised into view.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Use", meta = (EditCondition = "UseMode == EDiegeticUseMode::FocusView", EditConditionHides))
	bool bMoveCamera = true;

	/**
	 * Screens on the same actor with the same Focus Group are used together: Focus View frames all of them, and while one is in use
	 * the others can be pointed at and clicked too - a desk of several monitors, a cockpit. None: on its own.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Use", meta = (EditCondition = "UseMode == EDiegeticUseMode::FocusView", EditConditionHides))
	FName FocusGroup;
};

/** Where a Diegetic Screen's picture is shown. See EDiegeticDisplay. */
USTRUCT(BlueprintType)
struct NAMETBD_API FDiegeticDisplaySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display")
	EDiegeticDisplay Display = EDiegeticDisplay::Panel;

	/** Mesh Surface: the mesh it is shown on - on this actor, or another. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display", meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.MeshComponent", EditCondition = "Display == EDiegeticDisplay::MeshSurface", EditConditionHides))
	FComponentReference Mesh;

	/** Mesh Surface: the material slot that is the screen, by name. None: the first slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display", meta = (EditCondition = "Display == EDiegeticDisplay::MeshSurface", EditConditionHides))
	FName MaterialSlot;

	/** Mesh Surface: the part of the slot's UV space (channel 0) the picture fills, from its top left to its bottom right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display", meta = (EditCondition = "Display == EDiegeticDisplay::MeshSurface", EditConditionHides))
	FVector2D UVMin = FVector2D(0.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Display", meta = (EditCondition = "Display == EDiegeticDisplay::MeshSurface", EditConditionHides))
	FVector2D UVMax = FVector2D(1.0, 1.0);
};

/** Where a Diegetic Screen puts itself each frame. See EDiegeticPlacement. */
USTRUCT(BlueprintType)
struct NAMETBD_API FDiegeticPlacementSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	EDiegeticPlacement Placement = EDiegeticPlacement::AsPlaced;

	/** In Front Of Player: how far in front of the camera (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "10", UIMin = "30", UIMax = "400", ForceUnits = "cm", EditCondition = "Placement == EDiegeticPlacement::InFrontOfPlayer", EditConditionHides))
	float FollowDistance = 120.f;

	/** In Front Of Player: how far right (X) and up (Y) of the view's middle (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "Placement == EDiegeticPlacement::InFrontOfPlayer", EditConditionHides))
	FVector2D FollowOffset = FVector2D(20.0, -5.0);

	/** In Front Of Player: seconds it takes to catch up with the camera - a projection that trails a turn a little. 0: fixed to the view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0", UIMin = "0", UIMax = "1", ForceUnits = "s", EditCondition = "Placement == EDiegeticPlacement::InFrontOfPlayer", EditConditionHides))
	float FollowLag = 0.12f;

	/** In Front Of Player: stays upright (turns with the camera's yaw and pitch, never its roll). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (EditCondition = "Placement == EDiegeticPlacement::InFrontOfPlayer", EditConditionHides))
	bool bKeepUpright = true;
};

/**
 * How the screen looks: parameters of the plugin's screen material (Material Mode Automatic), handed to any material with
 * parameters of the same names. Preset fills them for a ready-made look; every value can be changed afterwards. The defaults
 * (Clean) draw the widget exactly as UMG does.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FDiegeticScreenLook
{
	GENERATED_BODY()

	/** A ready-made look: picking one sets the values below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look")
	EDiegeticLookPreset Preset = EDiegeticLookPreset::Clean;

	/** Multiplies the screen's light. Above 1 it glows (with bloom). Material parameter Brightness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", UIMin = "0", UIMax = "10"))
	float Brightness = 1.f;

	/**
	 * How much of the scene's exposure is undone, 0 to 1. At 1 the screen is as bright as authored whatever the camera's auto
	 * exposure does - as UMG draws it; at 0 it brightens in the dark and dims in the light like the world. Material parameter
	 * IgnoreSceneExposure.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float IgnoreSceneExposure = 1.f;

	/** The phosphor's (or projector's) colour: the picture is tinted toward it by Monochrome. Material parameter PhosphorColor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (HideAlphaChannel))
	FLinearColor PhosphorColor = FLinearColor::White;

	/** How much the picture becomes one colour (Phosphor Color by brightness), 0 to 1. Material parameter Monochrome. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Monochrome = 0.f;

	/** A soft halo around bright parts, as phosphor glows, 0 to 2. Material parameter Glow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", UIMin = "0", UIMax = "2"))
	float Glow = 0.f;

	/** How far the glow spreads, in the widget's pixels. Material parameter GlowRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0.5", UIMin = "1", UIMax = "16"))
	float GlowRadius = 4.f;

	/** How dark the scan lines are, 0 to 1. Material parameter ScanlineIntensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float ScanlineIntensity = 0.f;

	/** Scan lines from top to bottom. Material parameter ScanlineCount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "1", UIMin = "50", UIMax = "1200"))
	float ScanlineCount = 360.f;

	/** How fast the scan lines crawl down, in lines a second (a hologram's). Material parameter ScanlineSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (UIMin = "-60", UIMax = "60"))
	float ScanlineSpeed = 0.f;

	/** A brighter band rolling down the screen, as on an old CRT, 0 to 1. Material parameter RollingBar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "0.5"))
	float RollingBar = 0.f;

	/** How much the picture bulges, as on a CRT, 0 to 0.5. Material parameter Curvature. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "0.5", UIMin = "0", UIMax = "0.3"))
	float Curvature = 0.f;

	/** Red and blue pulled apart toward the edges, 0 to 0.02. Material parameter ChromaticAberration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "0.05", UIMin = "0", UIMax = "0.02"))
	float ChromaticAberration = 0.f;

	/** How dark the corners are, 0 to 1. Material parameter Vignette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Vignette = 0.f;

	/** How much the brightness flickers, 0 to 1. Material parameter Flicker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "0.3"))
	float Flicker = 0.f;

	/** Grain, 0 to 1. Material parameter Noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "0.3"))
	float Noise = 0.f;

	/** An LCD's grid of red, green and blue sub-pixels, one cell per widget pixel, 0 to 1. Material parameter PixelGrid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float PixelGrid = 0.f;

	/** Hologram: light added to the world rather than covering it, 0 to 1 (1: dark parts are see-through). Material parameter Additive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look|Hologram", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float Additive = 0.f;

	/** Hologram: fades the picture out as it is seen edge-on, 0 to 1. Material parameter EdgeFade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look|Hologram", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "1"))
	float EdgeFade = 0.f;

	/** Hologram: rows of the picture jumping sideways now and then, 0 to 1. Material parameter Glitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look|Hologram", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0", UIMax = "0.5"))
	float Glitch = 0.f;

	/**
	 * Frosted glass: the world behind the screen's transparent parts is blurred by this much, in the widget's pixels. 0: not
	 * blurred. A Background Blur widget on the screen blurs its own area by its own Blur Strength either way (see Blur Background
	 * Blur Widgets). Needs Material Mode Automatic (the glass variant is picked on its own). Material parameter BackgroundBlur.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look|Glass", meta = (ClampMin = "0", UIMin = "0", UIMax = "64"))
	float BackgroundBlur = 0.f;

	/** Frosted glass: the colour the blurred world behind is tinted toward, by its alpha. Material parameter GlassTint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look|Glass")
	FLinearColor GlassTint = FLinearColor(1.f, 1.f, 1.f, 0.f);

	/** Frosted glass: how bright the blurred world behind is, 0 to 2. Material parameter BlurBrightness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look|Glass", meta = (ClampMin = "0", UIMin = "0", UIMax = "2"))
	float BlurBrightness = 1.f;

	/**
	 * Background Blur widgets on the screen blur the world behind their area, as they blur what is behind them on the HUD (by their
	 * Blur Strength and corner radius; up to 8 on a screen). A Background Blur can't do that itself on a screen (the engine draws
	 * it opaque - UE-41001): its own drawing is turned off there, and the screen material does the blur.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Look|Glass")
	bool bBlurBackgroundBlurWidgets = true;

	/** The values of a ready-made look. Custom: the defaults (Clean). */
	static FDiegeticScreenLook MakePreset(EDiegeticLookPreset InPreset);

	/** Whether drawing it needs the glass material (it blurs the world behind). */
	bool NeedsBackgroundBlur() const { return BackgroundBlur > 0.f; }
};

/** Where a Diegetic Screen's UI sounds play from. */
USTRUCT(BlueprintType)
struct NAMETBD_API FDiegeticSoundSettings
{
	GENERATED_BODY()

	/**
	 * The UI sounds its widgets play - buttons, cards, lists, Sound cues - play from where they are on the screen, in the
	 * world, instead of flat in the player's ears.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (DisplayName = "Play Sounds In The World"))
	bool bSpatialize = true;

	/** How they fade with distance. Unset: Inner Radius and Falloff Distance below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (EditCondition = "bSpatialize"))
	TObjectPtr<USoundAttenuation> Attenuation = nullptr;

	/** Without an Attenuation asset: full volume within this distance (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = "0", UIMin = "0", UIMax = "1000", ForceUnits = "cm", EditCondition = "bSpatialize && Attenuation == nullptr"))
	float InnerRadius = 150.f;

	/** Without an Attenuation asset: how far past Inner Radius it fades to silence (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = "1", UIMin = "100", UIMax = "5000", ForceUnits = "cm", EditCondition = "bSpatialize && Attenuation == nullptr"))
	float FalloffDistance = 1500.f;
};

// =====================================================================================================
// What the widget on a screen is told
// =====================================================================================================

UINTERFACE(MinimalAPI, Blueprintable, meta = (DisplayName = "Diegetic Screen Widget"))
class UDiegeticScreenWidget : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by a widget shown on a Diegetic Screen - its root User Widget - to hear what happens to the screen. Every event
 * is optional. The same is also set as state machine inputs on the widget's state machines, when it has inputs of these names:
 * ScreenAwake, ScreenAimedAt, ScreenInUse (Bool) and ScreenDistance (Number, cm) - see UDiegeticScreenComponent.
 */
class NAMETBD_API IDiegeticScreenWidget
{
	GENERATED_BODY()

public:

	/** The screen woke: a player came within its Wake Distance (and once as it starts, when it starts awake). */
	UFUNCTION(BlueprintNativeEvent, Category = "Diegetic UI")
	void OnScreenWake(UDiegeticScreenComponent* Screen);

	/** The screen fell asleep: every player went past its Wake Distance and Sleep Margin. */
	UFUNCTION(BlueprintNativeEvent, Category = "Diegetic UI")
	void OnScreenSleep(UDiegeticScreenComponent* Screen);

	/** A player started (bAimedAt) or stopped pointing at the screen. */
	UFUNCTION(BlueprintNativeEvent, Category = "Diegetic UI")
	void OnScreenAimChanged(UDiegeticScreenComponent* Screen, bool bAimedAt);

	/** A player started (bInUse) or stopped using the screen in Focus View. */
	UFUNCTION(BlueprintNativeEvent, Category = "Diegetic UI")
	void OnScreenUseChanged(UDiegeticScreenComponent* Screen, bool bInUse, APlayerController* User);

	/**
	 * Back was pressed while the screen is in use. Return true to keep using it - the widget went back a page itself - or false
	 * (the default) to leave it.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Diegetic UI")
	bool OnScreenBack(UDiegeticScreenComponent* Screen);

	/**
	 * The screen's Context changed - the object whose data it shows (Set Screen Context: a weapon, a door, a room; its own actor
	 * when none is set) - and once as the widget comes onto the screen.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Diegetic UI")
	void OnScreenContextChanged(UDiegeticScreenComponent* Screen, UObject* Context);
};
