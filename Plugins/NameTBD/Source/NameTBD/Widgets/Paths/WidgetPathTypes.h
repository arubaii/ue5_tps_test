// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "Styling/SlateBrush.h"
#include "NameTBD/Widgets/WidgetEffectImage.h"
#include "NameTBD/Widgets/WidgetSVG.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeTypes.h"

#include "WidgetPathTypes.generated.h"

struct FWidgetShapeMesh;

/*
 * Widget Paths: lines drawn between widgets - the links of a skill tree, a talent web, a quest or tech tree, a level map, a
 * flowchart, a circuit, a constellation, a subway map. A Widget Path Layer (WidgetPathLayer.h) draws every connection between
 * the widgets it names (its nodes); these are the settings it is made of:
 *
 *   Routing  - how a line gets from one node to the other: straight, a smooth curve, right-angled elbows (Manhattan), a single
 *              step, 45-degree diagonals (metro / circuit), a bowed arc, round an orbit (radial trees), or through waypoints;
 *              corners round, sharp or chamfered; which side of each node it leaves and enters; how far short of the node it
 *              stops; the node's shape (box, rounded box, circle), so a line ends exactly at its edge.
 *   Looks    - what a line looks like: width (tapering, or along a curve), a color, a gradient along it or an image / material
 *              / SVG laid along it (U along, V across), dashes that can march, an outline, a soft glow that can breathe (or
 *              an image / material glow), things flowing along it (a pulse of light, dots, arrows - or pictures), markers at
 *              its ends (arrows, dots, diamonds, bars - or pictures), more strokes under or over it (offset, tapered, dashed,
 *              each with its own paint or material), and a distortion of the path itself (waves, lightning). A material
 *              anywhere in a look gets its own instance per line and live values (see FWidgetPathLook).
 *   Styles   - named looks: Locked, Available, Unlocked, Unavailable, Highlighted...
 *   Rules    - which style a line takes from the states of the nodes at its ends (Locked, Unlocked, Available, Unavailable,
 *              or a Custom State the game names), whether it is on a highlighted route, and whether an end is hovered or
 *              selected.
 *   Presets  - ready-made routing, styles and rules (EWidgetPathPreset), or a Widget Path Preset asset of your own.
 *
 * Positions are in the layer's own Slate units (its local space). Everything is drawn as vector geometry (the shapes' stroke
 * builder: anti-aliased at any scale, real caps and joins and dashes), so no texture is needed for any of it.
 */

/** How a line runs from one node to the other. */
UENUM(BlueprintType)
enum class EWidgetPathRoute : uint8
{
	Straight UMETA(ToolTip = "A straight line between the nodes, ending at their edges. Constellations, webs, star maps."),
	Curve UMETA(ToolTip = "A smooth S-curve leaving and entering each node along the route's direction (Curvature sets how far it swings out). Node editors, flowcharts, organic trees."),
	Elbow UMETA(DisplayName = "Elbow (Manhattan)", ToolTip = "Right angles only: out along the route's direction, across, and in again (Elbow Position sets where it crosses), the corners shaped by Corner. Skill trees, org charts, tech trees. Goes round the back when the target is behind."),
	Step UMETA(ToolTip = "One right-angle bend: along the route's direction first, then straight into the target. Tree diagrams, file trees."),
	Diagonal UMETA(DisplayName = "Diagonal (45 Degree)", ToolTip = "Straight runs joined by 45-degree diagonals, as a subway map or a circuit board draws them (Elbow Position sets where the diagonal sits)."),
	Arc UMETA(ToolTip = "A bowed circular arc (Bend sets how far it bows, and which way)."),
	Orbit UMETA(ToolTip = "Round a center (Orbit Center): along the circle both nodes sit on, spiralling between them if they are at different distances. Radial and orbital skill trees."),
	Waypoints UMETA(ToolTip = "Through the Waypoints, in order, cornered by Corner (or smoothed through them with Smooth Waypoints).")
};

/** How a route's corners are shaped (Elbow, Step, Diagonal and Waypoints routes). */
UENUM(BlueprintType)
enum class EWidgetPathCorner : uint8
{
	Round UMETA(ToolTip = "A circular arc of Corner Radius - the soft Manhattan look."),
	Sharp UMETA(ToolTip = "A crisp point (mitered)."),
	Chamfer UMETA(ToolTip = "Cut off straight, Corner Radius back from the corner - the circuit-board look.")
};

/** Which way a route leaves its first node. */
UENUM(BlueprintType)
enum class EWidgetPathAxis : uint8
{
	Auto UMETA(ToolTip = "Along whichever way the nodes are further apart: across for a tree that grows sideways, down for one that grows downward."),
	Horizontal UMETA(ToolTip = "Leaves and enters the nodes sideways - a tree growing left to right."),
	Vertical UMETA(ToolTip = "Leaves and enters the nodes through their top and bottom - a tree growing downward or upward.")
};

/** Where on a node a line attaches. */
UENUM(BlueprintType)
enum class EWidgetPathSide : uint8
{
	Auto UMETA(ToolTip = "Picked from the route and where the other node is: aimed at the other node's middle for Straight, Arc and Orbit routes; the side facing it along the route's direction for the others."),
	Center UMETA(ToolTip = "Aimed at the node's middle, ending at its edge."),
	Left,
	Right,
	Top,
	Bottom
};

/** The outline a line ends at, so it stops exactly at a node's edge (plus the gap) - not at the corner of its box. */
UENUM(BlueprintType)
enum class EWidgetPathNodeShape : uint8
{
	Auto UMETA(ToolTip = "Read from the node: an Ellipse is a circle, a Rectangle, Glass Panel or rounded brush its rounded box, a pill a pill; anything else its box."),
	Box UMETA(ToolTip = "The node's box, square corners."),
	Ellipse UMETA(ToolTip = "The ellipse filling the node's box - round nodes."),
	Point UMETA(ToolTip = "Its middle: lines run to the middle, Gap short of it.")
};

/** What a line is painted with. */
UENUM(BlueprintType)
enum class EWidgetPathPaint : uint8
{
	Color UMETA(ToolTip = "One color."),
	Gradient UMETA(DisplayName = "Gradient Along Path", ToolTip = "Colors blended along the line from its first node (0) to its second (1) - power flowing from an unlocked node into the next."),
	Image UMETA(ToolTip = "A texture, material or SVG laid along the line: U along it (once, or every Image Tile Length), V across it (0 one edge, 1 the other). A material can flow, sparkle or glow along the line with Time; Image Scroll Speed moves a plain texture along it.")
};

/** What flows along a line, over its paint. */
UENUM(BlueprintType)
enum class EWidgetPathFlow : uint8
{
	None,
	Pulse UMETA(ToolTip = "Bands of light (Flow Color) travelling along the line, Flow Length long, every Flow Spacing - energy flowing into a node."),
	Dots UMETA(ToolTip = "Dots (Flow Size across) travelling along the line every Flow Spacing."),
	Arrows UMETA(ToolTip = "Small chevrons travelling along the line, pointing the way it flows.")
};

/** A mark at an end of a line. */
UENUM(BlueprintType)
enum class EWidgetPathMarker : uint8
{
	None,
	Arrow UMETA(ToolTip = "A filled arrowhead, its tip at the line's end."),
	OpenArrow UMETA(DisplayName = "Open Arrow", ToolTip = "A chevron arrowhead (two strokes)."),
	Dot UMETA(ToolTip = "A round dot on the end - a socket."),
	Diamond UMETA(ToolTip = "A diamond on the end."),
	Bar UMETA(ToolTip = "A short bar across the end - a stop.")
};

/** How a line changes look when its style changes (a node unlocked, a route highlighted). */
UENUM(BlueprintType)
enum class EWidgetPathTransition : uint8
{
	DrawOn UMETA(DisplayName = "Draw On", ToolTip = "The new look runs along the line from the node that was already reached (unlocked) toward the one that changed, over the old look - the classic skill tree unlock."),
	Fade UMETA(ToolTip = "The new look fades in over the old one."),
	Instant UMETA(ToolTip = "Changes at once.")
};

/** How the lines appear when the layer is first shown. */
UENUM(BlueprintType)
enum class EWidgetPathIntro : uint8
{
	None UMETA(ToolTip = "Shown at once."),
	DrawOn UMETA(DisplayName = "Draw On", ToolTip = "Each line draws itself from its first node to its second, the lines further from the tree's roots later (Intro Stagger per step)."),
	Fade UMETA(ToolTip = "Each line fades in, the lines further from the tree's roots later.")
};

/** A yes / no condition a style rule can ask for, or not care about. */
UENUM(BlueprintType)
enum class EWidgetPathMatch : uint8
{
	Any UMETA(ToolTip = "Either way."),
	Yes,
	No
};

/** Which connections of a node. */
UENUM(BlueprintType)
enum class EWidgetPathDirection : uint8
{
	Outgoing UMETA(ToolTip = "Connections from it (it is their From node)."),
	Incoming UMETA(ToolTip = "Connections to it (it is their To node)."),
	Both UMETA(ToolTip = "Every connection it is on.")
};

/** What hovering a node highlights. */
UENUM(BlueprintType)
enum class EWidgetPathHoverHighlight : uint8
{
	None UMETA(ToolTip = "Nothing."),
	Connections UMETA(ToolTip = "The lines touching the hovered node."),
	RouteTo UMETA(DisplayName = "Route To It", ToolTip = "The shortest route to the hovered node from the nearest node in Route From State (Unlocked): what it would take to get there - as Path of Exile previews a passive's path.")
};

/**
 * A node's state. Locked, Unlocked, Available and Unavailable are built in - the presets, the rules and Derive Available use
 * them, picked from this list rather than typed. A state of the game's own is named by a Custom State beside it, which wins
 * over the one picked here.
 */
UENUM(BlueprintType)
enum class EWidgetPathState : uint8
{
	Locked UMETA(ToolTip = "Not reached yet: what a node nothing has set is (Default Node State)."),
	Unlocked UMETA(ToolTip = "Reached - bought, allocated, learned. Routes start from it, and the Locked nodes next to it read Available (Derive Available)."),
	Available UMETA(ToolTip = "Can be unlocked now: worked out on its own next to an Unlocked node (Derive Available), or set."),
	Unavailable UMETA(ToolTip = "Can't be unlocked - a requirement not met, the other side of a choice taken, a level too low. Routes don't pass through it (Routes Avoid Unavailable)."),
	Custom UMETA(ToolTip = "A state of the game's own, named by Custom State ('Maxed', 'Upgraded', 'Disabled'...): what Get Node State reports for one. Set with no Custom State, it clears what was set (the node goes back to what it reports, was authored, or Default Node State).")
};

/** Which node state a style rule asks for (a Custom State beside it wins). */
UENUM(BlueprintType)
enum class EWidgetPathStateFilter : uint8
{
	Any UMETA(ToolTip = "Any state - unless a Custom State is named beside it."),
	Locked,
	Unlocked,
	Available,
	Unavailable
};

/** A ready-made set of routing, styles and rules. Picking one replaces them; editing them makes it Custom. */
UENUM(BlueprintType)
enum class EWidgetPathPreset : uint8
{
	SkillTree UMETA(DisplayName = "Skill Tree", ToolTip = "Rounded elbows. Locked: thin, dim and dashed. Available: a softly breathing glow. Unlocked: bright with a glow and light flowing along. Highlighted (a route previewed on hover or selection): gold, marching."),
	Constellation UMETA(ToolTip = "Straight hairlines between round nodes. Locked: faint dotted. Unlocked: bright with a glow - a star map, a talent web."),
	Circuit UMETA(ToolTip = "45-degree diagonals with chamfered corners and an outline; unlocked lines carry dots of light - a circuit board, a sci-fi tech tree."),
	Flowchart UMETA(ToolTip = "Smooth curves with arrowheads - node graphs, flowcharts, dialogue maps."),
	Metro UMETA(ToolTip = "Thick lines on 45-degree diagonals with round corners and a white casing, markers at the stations - a subway map, a level route."),
	Organic UMETA(ToolTip = "Soft curves that taper in color from node to node, glowing as they unlock - nature and magic trees."),
	Radial UMETA(ToolTip = "Lines along orbits round a center (set Orbit Center), as Path of Exile's passive tree draws them."),
	Custom UMETA(ToolTip = "The Routing, Styles and Rules as set.")
};

/** How lines get from node to node. Set on the layer for every line; a connection can override it. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetPathRouting
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing")
	EWidgetPathRoute Route = EWidgetPathRoute::Elbow;

	/** Which way the route leaves the first node (Curve, Elbow, Step, Diagonal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "Route == EWidgetPathRoute::Curve || Route == EWidgetPathRoute::Elbow || Route == EWidgetPathRoute::Step || Route == EWidgetPathRoute::Diagonal", EditConditionHides))
	EWidgetPathAxis Orientation = EWidgetPathAxis::Auto;

	/** Where the line leaves its first (From) node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing")
	EWidgetPathSide FromSide = EWidgetPathSide::Auto;

	/** Where the line enters its second (To) node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing")
	EWidgetPathSide ToSide = EWidgetPathSide::Auto;

	/** How corners are shaped (Elbow, Step, Diagonal, Waypoints). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "Route == EWidgetPathRoute::Elbow || Route == EWidgetPathRoute::Step || Route == EWidgetPathRoute::Diagonal || Route == EWidgetPathRoute::Waypoints", EditConditionHides))
	EWidgetPathCorner Corner = EWidgetPathCorner::Round;

	/** How round (or how far back a chamfer cuts), in Slate units - held to what fits between corners. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "Corner != EWidgetPathCorner::Sharp && (Route == EWidgetPathRoute::Elbow || Route == EWidgetPathRoute::Step || Route == EWidgetPathRoute::Diagonal || Route == EWidgetPathRoute::Waypoints)", EditConditionHides, ClampMin = "0", UIMin = "0", UIMax = "64", ForceUnits = "px"))
	float CornerRadius = 12.f;

	/** How far a Curve swings out of each node before turning toward the other: 0 nearly straight, 1 a wide S. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "Route == EWidgetPathRoute::Curve", EditConditionHides, ClampMin = "0", ClampMax = "2", UIMin = "0", UIMax = "1"))
	float Curvature = 0.5f;

	/** Where an Elbow crosses over (or a Diagonal's slant sits), from the first node (0) to the second (1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "Route == EWidgetPathRoute::Elbow || Route == EWidgetPathRoute::Diagonal", EditConditionHides, ClampMin = "0", ClampMax = "1"))
	float ElbowPosition = 0.5f;

	/** How far an Arc bows, as a fraction of half the distance between the nodes: 1 a half circle, negative the other way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "Route == EWidgetPathRoute::Arc", EditConditionHides, ClampMin = "-1", ClampMax = "1"))
	float Bend = 0.3f;

	/** The widget an Orbit goes round (its middle). None: Orbit Point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "Route == EWidgetPathRoute::Orbit", EditConditionHides, GetOptions = "GetWidgetOptions"))
	FName OrbitCenter;

	/** The point an Orbit goes round with no Orbit Center widget, in the layer's Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "Route == EWidgetPathRoute::Orbit", EditConditionHides))
	FVector2D OrbitPoint = FVector2D::ZeroVector;

	/** Goes the long way round an Orbit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "Route == EWidgetPathRoute::Orbit", EditConditionHides))
	bool bOrbitLongWay = false;

	/** The points a Waypoints route passes through, in order, in the layer's Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "Route == EWidgetPathRoute::Waypoints", EditConditionHides))
	TArray<FVector2D> Waypoints;

	/** A smooth curve through the Waypoints rather than straight runs with corners. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "Route == EWidgetPathRoute::Waypoints", EditConditionHides))
	bool bSmoothWaypoints = false;

	/** The outline lines stop at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing")
	EWidgetPathNodeShape NodeShape = EWidgetPathNodeShape::Auto;

	/** How far short of the first node's edge the line starts, in Slate units (negative: inside it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (UIMin = "-16", UIMax = "32", ForceUnits = "px"))
	float StartGap = 4.f;

	/** How far short of the second node's edge the line ends, in Slate units (negative: inside it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (UIMin = "-16", UIMax = "32", ForceUnits = "px"))
	float EndGap = 4.f;

	/**
	 * Lines leaving or entering the same side of a node are spread along it, Port Spacing apart, rather than all meeting in
	 * its middle - a node with many prerequisites. Sides picked by a route (not Center).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing")
	bool bSpreadPorts = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing", meta = (EditCondition = "bSpreadPorts", EditConditionHides, ClampMin = "0", UIMax = "40", ForceUnits = "px"))
	float PortSpacing = 10.f;

	/** A hash of everything here, for caches. */
	uint32 GetHash() const;
};

/**
 * Moves a line's path sideways as it runs - a wave, a wobble, a crackle of lightning - for one look (a highlighted route can
 * crackle while the locked web lies still). The ends fade back onto the route, so the line still meets its nodes.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetPathDistortion
{
	GENERATED_BODY()

	/** How far a wave swings to each side, in Slate units. 0: no wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (ClampMin = "0", UIMax = "24", ForceUnits = "px"))
	float WaveAmplitude = 0.f;

	/** How long one wave is, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (EditCondition = "WaveAmplitude > 0", ClampMin = "6", UIMax = "200", ForceUnits = "px"))
	float Wavelength = 40.f;

	/** How fast the waves travel along the line, in Slate units a second (negative: back toward the From node). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (EditCondition = "WaveAmplitude > 0", UIMin = "-200", UIMax = "200"))
	float WaveSpeed = 0.f;

	/** Random sideways kinks, in Slate units - lightning, a hand-drawn wobble. 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (ClampMin = "0", UIMax = "24", ForceUnits = "px"))
	float JitterAmplitude = 0.f;

	/** How far apart the kinks are, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (EditCondition = "JitterAmplitude > 0", ClampMin = "4", UIMax = "100", ForceUnits = "px"))
	float JitterSpacing = 18.f;

	/** How many times a second the kinks change (0: they stay put). About 12 for lightning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (EditCondition = "JitterAmplitude > 0", ClampMin = "0", UIMax = "30"))
	float JitterRate = 0.f;

	/** Smooth kinks (a wobble) rather than sharp ones (lightning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (EditCondition = "JitterAmplitude > 0"))
	bool bSmoothJitter = false;

	/** How far from each end the distortion fades in, in Slate units, so the line still meets its nodes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion", meta = (ClampMin = "0", UIMax = "100", ForceUnits = "px"))
	float EndFade = 16.f;

	bool IsActive() const { return WaveAmplitude > 0.f || JitterAmplitude > 0.f; }
	bool IsAnimated() const { return (WaveAmplitude > 0.f && WaveSpeed != 0.f) || (JitterAmplitude > 0.f && JitterRate > 0.f); }
	uint32 GetHash() const;
};

/**
 * One more stroke along a line, under or over it: a dark casing under a light core, rails either side (two strokes offset +-4),
 * a tapering shadow, a material's energy beam over a plain line, a dashed edge. Each is offset, tapered, dashed and painted as
 * it says - a color, a gradient, or a texture / material / SVG laid along it (U along, V across).
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetPathStroke
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (DisplayName = "Enabled"))
	bool bEnabled = true;

	/** Drawn over the line and what flows along it, rather than under them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (DisplayName = "Above Line"))
	bool bAboveLine = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (ClampMin = "0", UIMin = "0.5", UIMax = "40", ForceUnits = "px"))
	float Width = 8.f;

	/** Sideways from the line, in Slate units: positive to the right of the way it runs (From to To), negative to the left. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (UIMin = "-24", UIMax = "24", ForceUnits = "px"))
	float Offset = 0.f;

	/** Its width at the From end, as a multiple of Width (in between, a straight blend to End Width Scale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (ClampMin = "0", UIMax = "2"))
	float StartWidthScale = 1.f;

	/** Its width at the To end, as a multiple of Width: 0 tapers it to a point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (ClampMin = "0", UIMax = "2"))
	float EndWidthScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke")
	EWidgetPathPaint Paint = EWidgetPathPaint::Color;

	/** Its color - and with a gradient or an image, a tint over it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke")
	FLinearColor Color = FLinearColor(0.f, 0.f, 0.f, 0.5f);

	/** Colors along it, 0 at the From node and 1 at the To node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (EditCondition = "Paint == EWidgetPathPaint::Gradient", EditConditionHides))
	TArray<FWidgetShapeGradientStop> GradientStops;

	/**
	 * Laid along it: U along (once, or every Image Tile Length), V across. A material gets its own instance per line and the
	 * values listed under the look's Material Inputs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (EditCondition = "Paint == EWidgetPathPaint::Image", EditConditionHides))
	FWidgetEffectImage Image;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (EditCondition = "Paint == EWidgetPathPaint::Image", EditConditionHides, ClampMin = "0", UIMax = "200", ForceUnits = "px"))
	float ImageTileLength = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (EditCondition = "Paint == EWidgetPathPaint::Image", EditConditionHides, UIMin = "-200", UIMax = "200"))
	float ImageScrollSpeed = 0.f;

	/** The length of each dash, in Slate units. 0: solid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (ClampMin = "0", UIMax = "40", ForceUnits = "px"))
	float DashLength = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (EditCondition = "DashLength > 0", ClampMin = "0", UIMax = "40", ForceUnits = "px"))
	float DashGap = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (EditCondition = "DashLength > 0", UIMin = "-100", UIMax = "100"))
	float DashSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 1.f;

	/** Breathes with the look's Breathe, as the glow and the flow do. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stroke", meta = (DisplayName = "Breathe With Look"))
	bool bBreathe = false;

	FWidgetPathStroke();

	FLinearColor ColorAt(float Fraction) const;
	bool IsAnimated() const;
	uint32 GetHash() const;
};

/**
 * What one style of line looks like. Everything a look draws can use a material: the line (Paint Image with a material), the
 * glow (Glow Image), the flowing marks (Flow Image), the markers (Start / End Marker Image) and every Stroke. Each material gets
 * its own instance per line and these live values as scalar parameters, beside its own Material Parameters - so a technical
 * artist can build any line: PathLength (the line's length, Slate units), PathWidth (the look's Width), PathRandom (0..1, the
 * same for a line every frame - offset noise per line), PathRevealStart / PathRevealEnd (the part of the line drawn, 0..1 -
 * the draw-on transition), PathProgress (the connection's Progress), PathHighlighted, PathHovered, PathSelected (0 or 1), and
 * PathTileLength (Image Tile Length, or the whole length). Its UVs: U along (0..1 per tile, or once over the line), V across
 * (0 to 1); its vertex color the tint and opacity.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetPathLook
{
	GENERATED_BODY()

	/** In Slate units. Under a pixel on screen it fades rather than thins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (ClampMin = "0", UIMin = "0.5", UIMax = "24", ForceUnits = "px"))
	float Width = 3.f;

	/** The width at the From end, as a multiple of Width (in between, a straight blend to End Width Scale) - a line tapering from its source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (ClampMin = "0", UIMax = "3"))
	float StartWidthScale = 1.f;

	/** The width at the To end, as a multiple of Width: 0 tapers the line to a point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (ClampMin = "0", UIMax = "3"))
	float EndWidthScale = 1.f;

	/** Shapes the width along the line with a curve (X 0 at the From node, 1 at the To node; Y multiplies Width) - a brush stroke, a bulge in the middle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (DisplayName = "Use Width Curve"))
	bool bUseWidthCurve = false;

	UPROPERTY(EditAnywhere, Category = "Line", meta = (EditCondition = "bUseWidthCurve", EditConditionHides))
	FRuntimeFloatCurve WidthCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line")
	EWidgetPathPaint Paint = EWidgetPathPaint::Color;

	/** The line's color - and with a gradient or an image, a tint over it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line")
	FLinearColor Color = FLinearColor::White;

	/** Colors along the line, 0 at its first node and 1 at its second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "Paint == EWidgetPathPaint::Gradient", EditConditionHides))
	TArray<FWidgetShapeGradientStop> GradientStops;

	/** Where Image's picture comes from: the Image brush (a texture or material), or an SVG drawn at the size it is shown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "Paint == EWidgetPathPaint::Image", EditConditionHides))
	EWidgetImageSource ImageSource = EWidgetImageSource::Texture;

	/** The texture or material laid along the line (U along, V across), tinted by its Tint and by Color. A material gets the live values listed on the look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "Paint == EWidgetPathPaint::Image && ImageSource == EWidgetImageSource::Texture", EditConditionHides))
	FSlateBrush Image;

	/** The SVG laid along the line, drawn at one tile's size on screen, tinted by Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "Paint == EWidgetPathPaint::Image && ImageSource == EWidgetImageSource::SVG", EditConditionHides, DisplayName = "SVG"))
	TObjectPtr<UWidgetSVG> SVG = nullptr;

	/** Set on Image's material (when it is one), beside the live values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "Paint == EWidgetPathPaint::Image && ImageSource == EWidgetImageSource::Texture", EditConditionHides, TitleProperty = "Name"))
	TArray<FWidgetMaterialParameter> MaterialParameters;

	/** How long one repeat of the image is along the line, in Slate units. 0: once along the whole line (U 0 to 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "Paint == EWidgetPathPaint::Image", EditConditionHides, ClampMin = "0", UIMax = "200", ForceUnits = "px"))
	float ImageTileLength = 0.f;

	/** Moves the image along the line, in Slate units a second (negative: back toward the first node). Needs a tiling image. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (EditCondition = "Paint == EWidgetPathPaint::Image", EditConditionHides, UIMin = "-200", UIMax = "200"))
	float ImageScrollSpeed = 0.f;

	/** How the line ends - at its nodes and at each dash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line")
	EWidgetShapeLineCap Cap = EWidgetShapeLineCap::Round;

	/** The length of each dash, in Slate units. 0: a solid line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dashes", meta = (ClampMin = "0", UIMax = "40", ForceUnits = "px"))
	float DashLength = 0.f;

	/** The space between dashes. 0 with a Dash Length: as long as the dashes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dashes", meta = (EditCondition = "DashLength > 0", ClampMin = "0", UIMax = "40", ForceUnits = "px"))
	float DashGap = 0.f;

	/** Marches the dashes along the line, in Slate units a second (negative: backward). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dashes", meta = (EditCondition = "DashLength > 0", UIMin = "-100", UIMax = "100"))
	float DashSpeed = 0.f;

	/** A casing drawn under the line, Outline Width wider each side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline")
	bool bOutline = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (EditCondition = "bOutline", EditConditionHides, ClampMin = "0", UIMax = "8", ForceUnits = "px"))
	float OutlineWidth = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (EditCondition = "bOutline", EditConditionHides))
	FLinearColor OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);

	/** A soft glow under the line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow")
	bool bGlow = false;

	/** How far the glow reaches beyond the line's edge, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (EditCondition = "bGlow", EditConditionHides, ClampMin = "0", UIMax = "40", ForceUnits = "px"))
	float GlowSize = 8.f;

	/** The glow's color; alpha is its strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (EditCondition = "bGlow", EditConditionHides))
	FLinearColor GlowColor = FLinearColor(0.4f, 0.75f, 1.f, 0.5f);

	/**
	 * Draws the glow with a picture or material laid along it (U along over the whole line, V across the glow) instead of a
	 * plain soft fade, tinted by Glow Color - a heat haze, sparks, a custom falloff. Empty: the soft fade.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (EditCondition = "bGlow", EditConditionHides))
	FWidgetEffectImage GlowImage;

	/** Breathes the glow and the flow (0 steady, 1 fading fully out and in) - a node waiting to be unlocked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (ClampMin = "0", ClampMax = "1"))
	float Breathe = 0.f;

	/** Breaths a second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glow", meta = (EditCondition = "Breathe > 0", EditConditionHides, ClampMin = "0.05", UIMax = "4"))
	float BreatheRate = 0.6f;

	/** What flows along the line, over it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow")
	EWidgetPathFlow Flow = EWidgetPathFlow::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow", meta = (EditCondition = "Flow != EWidgetPathFlow::None", EditConditionHides))
	FLinearColor FlowColor = FLinearColor(1.f, 1.f, 1.f, 0.85f);

	/** In Slate units a second, from the first node toward the second (negative: the other way). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow", meta = (EditCondition = "Flow != EWidgetPathFlow::None", EditConditionHides, UIMin = "-300", UIMax = "300"))
	float FlowSpeed = 60.f;

	/** How far apart the pulses, dots or arrows are, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow", meta = (EditCondition = "Flow != EWidgetPathFlow::None", EditConditionHides, ClampMin = "4", UIMax = "300", ForceUnits = "px"))
	float FlowSpacing = 80.f;

	/** How big the dots or arrows are, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow", meta = (EditCondition = "Flow == EWidgetPathFlow::Dots || Flow == EWidgetPathFlow::Arrows", EditConditionHides, ClampMin = "1", UIMax = "48", ForceUnits = "px"))
	float FlowSize = 5.f;

	/** How long each pulse of light is, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow", meta = (EditCondition = "Flow == EWidgetPathFlow::Pulse", EditConditionHides, ClampMin = "2", UIMax = "200", ForceUnits = "px"))
	float FlowLength = 36.f;

	/**
	 * Dots / Arrows: a picture (texture, material or SVG) drawn for each mark instead - a spark, a rune, a particle sprite -
	 * Flow Size across, turned to the way the line runs, tinted by Flow Color. A material also gets PathFlowIndex (which mark).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow", meta = (EditCondition = "Flow == EWidgetPathFlow::Dots || Flow == EWidgetPathFlow::Arrows", EditConditionHides))
	FWidgetEffectImage FlowImage;

	/** A mark at the first node's end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers")
	EWidgetPathMarker StartMarker = EWidgetPathMarker::None;

	/** A mark at the second node's end - an arrowhead shows which way a connection goes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers")
	EWidgetPathMarker EndMarker = EWidgetPathMarker::None;

	/** How big the markers are, in Slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers", meta = (ClampMin = "1", UIMax = "64", ForceUnits = "px"))
	float MarkerSize = 9.f;

	/** A picture (texture, material or SVG) at the first node's end instead of Start Marker's shape, Marker Size across, pointing back along the line (its top toward the node). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers")
	FWidgetEffectImage StartMarkerImage;

	/** A picture at the second node's end instead of End Marker's shape, Marker Size across, pointing the way the line runs (its top toward the node). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers")
	FWidgetEffectImage EndMarkerImage;

	/** More strokes along the line, under or over it - casings, rails, cores, a material beam. Drawn in order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strokes", meta = (TitleProperty = "Paint"))
	TArray<FWidgetPathStroke> Strokes;

	/** Waves or a jitter on the path itself (every part of the look follows it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FWidgetPathDistortion Distortion;

	/** The whole line's opacity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 1.f;

	/** Lines with a higher Draw Order are drawn over the others - a highlighted route over the locked web. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Line")
	int32 DrawOrder = 0;

	FWidgetPathLook();

	/** Whether anything about it moves on its own (marching dashes, flow, breathing, a scrolling image, a moving distortion, a material). */
	bool IsAnimated() const;

	/** The color at a fraction along the line (0 its first node, 1 its second), Color included. */
	FLinearColor ColorAt(float Fraction) const;

	/** The width multiplier at a fraction along the line (taper and width curve). */
	float WidthScaleAt(float Fraction) const;

	/** Whether the width changes along the line. */
	bool HasVaryingWidth() const;

	/** The line's picture as an effect image (Image or SVG, with Material Parameters). */
	FWidgetEffectImage GetLineImage() const;
};

/** Colors along a line from gradient stops (any order), times Tint. No stops: Tint. */
namespace WidgetPathColors
{
	NAMETBD_API FLinearColor Sample(const TArray<FWidgetShapeGradientStop>& Stops, float Fraction, const FLinearColor& Tint);
}

/** A named look lines can take. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetPathStyle
{
	GENERATED_BODY()

	/** What rules and connections call it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FName Name;

	/** What lines of this style look like. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FWidgetPathLook Look;
};

/**
 * Which style a line takes. Rules are tried in order; the first whose every condition holds gives the line its style.
 * States are picked from the built-in ones (Locked, Unlocked, Available, Unavailable; Any matches any) - or named by a Custom
 * State, which wins over the one picked.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetPathStyleRule
{
	GENERATED_BODY()

	/** The state of the line's first (From) node. Any: any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule", meta = (DisplayName = "From State"))
	EWidgetPathStateFilter FromNodeState = EWidgetPathStateFilter::Any;

	/** A state of the game's own for the From node - wins over From State. None: From State. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	FName FromCustomState;

	/** The state of the line's second (To) node. Any: any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule", meta = (DisplayName = "To State"))
	EWidgetPathStateFilter ToNodeState = EWidgetPathStateFilter::Any;

	/** A state of the game's own for the To node - wins over To State. None: To State. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	FName ToCustomState;

	/** Also matches with the two states the other way round - most links in a tree don't care which end is which. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	bool bEitherWay = true;

	/** The line is on a highlighted route (hovered or selected node's route, or Highlight Route). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	EWidgetPathMatch Highlighted = EWidgetPathMatch::Any;

	/** One of its nodes is hovered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	EWidgetPathMatch Hovered = EWidgetPathMatch::Any;

	/** One of its nodes is the selected node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule")
	EWidgetPathMatch Selected = EWidgetPathMatch::Any;

	/** The style it gives (a name from Styles). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rule", meta = (GetOptions = "GetStyleOptions"))
	FName Style;

	FWidgetPathStyleRule() = default;
	FWidgetPathStyleRule(EWidgetPathStateFilter InFrom, EWidgetPathStateFilter InTo, FName InStyle, EWidgetPathMatch InHighlighted = EWidgetPathMatch::Any)
		: FromNodeState(InFrom), ToNodeState(InTo), Highlighted(InHighlighted), Style(InStyle) {}

	/** The From node's state as matched (None: any). */
	FName GetFromState() const;

	/** The To node's state as matched (None: any). */
	FName GetToState() const;
};

/** One line between two widgets, as authored on the layer. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetPathConnection
{
	GENERATED_BODY()

	/** The first node: in a tree, the prerequisite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection", meta = (GetOptions = "GetWidgetOptions"))
	FName From;

	/** The second node: in a tree, what it leads to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection", meta = (GetOptions = "GetWidgetOptions"))
	FName To;

	/** A style for this line whatever the rules say. None: by the rules. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection", meta = (GetOptions = "GetStyleOptions"))
	FName Style;

	/** Routes this line its own way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection")
	bool bOverrideRouting = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection", meta = (EditCondition = "bOverrideRouting", EditConditionHides))
	FWidgetPathRouting Routing;

	/**
	 * How much of the line is filled with Progress Style, from its first node: progress toward the next node (points
	 * spent, a quest step half done). 0: none - the line shows its own style.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection", meta = (ClampMin = "0", ClampMax = "1"))
	float Progress = 0.f;

	/** The style the filled part shows. None: Unlocked State's style name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Connection", meta = (GetOptions = "GetStyleOptions"))
	FName ProgressStyle;
};

/** A node's state, as authored (and shown in the designer). */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetPathNodeState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node", meta = (GetOptions = "GetWidgetOptions"))
	FName Widget;

	/** Locked, Unlocked, Available or Unavailable (Custom: Custom State's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node", meta = (DisplayName = "State"))
	EWidgetPathState NodeState = EWidgetPathState::Locked;

	/** A state of the game's own (the rules match it by name) - wins over State. None: State. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node")
	FName CustomState;

	/** The state as the rules see it (None: Custom with no name). */
	FName GetState() const;
};

namespace WidgetPathStates
{
	/** A built-in state's name ("Locked", "Unlocked", "Available", "Unavailable"); None for Custom. */
	NAMETBD_API FName GetName(EWidgetPathState State);

	/** A state as the rules match it: Custom State when given, else the built-in state's name (None: Custom with no name). */
	NAMETBD_API FName Resolve(EWidgetPathState State, FName CustomState);

	/** A rule's state as matched: Custom State when given, else the built-in state's name (None: Any). */
	NAMETBD_API FName ResolveFilter(EWidgetPathStateFilter Filter, FName CustomState);

	/** A state name back as a built-in state - or Custom, with the name in OutCustomState. None reads Locked. */
	NAMETBD_API EWidgetPathState FromName(FName Name, FName& OutCustomState);

	/** A custom name that is a built-in state's (as assets from before the built-in states saved them) moved into the enum. */
	NAMETBD_API void Normalize(EWidgetPathState& State, FName& CustomState);
	NAMETBD_API void NormalizeFilter(EWidgetPathStateFilter& Filter, FName& CustomState);
}

// =====================================================================================================
// Geometry
// =====================================================================================================

/** A node as routing sees it: its box in the layer's space and the outline lines stop at. */
struct FWidgetPathNodeBox
{
	FVector2f Center = FVector2f::ZeroVector;
	FVector2f HalfSize = FVector2f::ZeroVector;
	FVector4f Radii = FVector4f(0.f, 0.f, 0.f, 0.f);
	bool bEllipse = false;
	bool bPoint = false;
	bool bValid = false;

	/** Signed distance from the node's outline (negative inside), approximately for an ellipse. */
	float DistanceTo(const FVector2f& Point) const;
};

/** What a route needs besides the two nodes. */
struct FWidgetPathRouteInput
{
	FWidgetPathNodeBox From;
	FWidgetPathNodeBox To;
	FWidgetPathRouting Routing;

	/** Orbit: the center, in layer space. */
	FVector2f OrbitCenter = FVector2f::ZeroVector;

	/** Spread ports: offsets along the side each end attaches to (Slate units). */
	float FromPortOffset = 0.f;
	float ToPortOffset = 0.f;
};

namespace WidgetPathGeometry
{
	/** The side a route attaches to at each end, once Auto is decided. */
	NAMETBD_API void ResolveSides(const FWidgetPathRouteInput& Input, EWidgetPathSide& OutFrom, EWidgetPathSide& OutTo);

	/**
	 * The route from one node to the other, flattened to points in layer space (first at the From end), stopped at both nodes'
	 * outlines plus their gaps. Tolerance as WidgetShapeGeometry::GetTolerance. Fewer than two points: nothing to draw (the
	 * nodes overlap).
	 */
	NAMETBD_API void BuildRoute(const FWidgetPathRouteInput& Input, float Tolerance, TArray<FVector2f>& OutPoints);

	/** Points no further than MaxStep apart (the line's per-vertex colors - gradients, pulses - then read smoothly). */
	NAMETBD_API void Resample(TArray<FVector2f>& Points, float MaxStep);

	/** The polyline cut to the part between StartDistance and EndDistance along it. */
	NAMETBD_API void CutBetween(const TArray<FVector2f>& Points, float StartDistance, float EndDistance, TArray<FVector2f>& Out);

	/** Where along a polyline Distance is, and which way it runs there. False for an empty line. */
	NAMETBD_API bool SampleAt(const TArray<FVector2f>& Points, float Distance, FVector2f& OutPoint, FVector2f& OutDirection);

	/** A side's outward direction (Center and Auto: zero). */
	NAMETBD_API FVector2f SideNormal(EWidgetPathSide Side);

	/**
	 * A stroke along Points whose width (WidthAt) and sideways offset (OffsetAt, positive to the right of the way it runs)
	 * change with the distance along it - tapers, rails. Flat ends, anti-aliased sides. Both functions take the distance from
	 * Points' first point. Each vertex's Along: X AlongStart + that distance, Y 0..1 across (left to right); OutlineLength
	 * TotalLength.
	 */
	NAMETBD_API void BuildRibbon(const TArray<FVector2f>& Points, TFunctionRef<float(float)> WidthAt, TFunctionRef<float(float)> OffsetAt, float AntiAlias,
		float AlongStart, float TotalLength, FWidgetShapeMesh& Out);

	/** How far a marker of Size pulls its end of a line Width wide back, so the line doesn't poke through an arrowhead's tip. */
	NAMETBD_API float GetMarkerInset(EWidgetPathMarker Marker, float Size, float Width);

	/**
	 * A marker at Tip, pointing Direction (outward, away from the line): what a Widget Path draws at a line's ends, and a Curve at
	 * its own. Its vertices' Along are left at zero.
	 */
	NAMETBD_API void BuildEndMarker(EWidgetPathMarker Marker, const FVector2f& Tip, const FVector2f& Direction, float Size, float LineWidth, float AntiAlias,
		float Tolerance, FWidgetShapeMesh& Out);

	/**
	 * Ribbons (BuildRibbon) over the part of Route between From and To (distances along it), dashed when DashLength > 0 - each
	 * dash its own ribbon, DashOffset sliding them. WidthAt and OffsetAt take the distance along the whole route.
	 */
	NAMETBD_API void BuildDashedRibbons(const TArray<FVector2f>& Route, float From, float To, float DashLength, float DashGap, float DashOffset,
		TFunctionRef<float(float)> WidthAt, TFunctionRef<float(float)> OffsetAt, float AntiAlias, float TotalLength, FWidgetShapeMesh& Out);

	/** Points moved sideways as Distortion says at Time (seconds); Seed gives each line a jitter of its own. */
	NAMETBD_API void Distort(const TArray<FVector2f>& Points, const FWidgetPathDistortion& Distortion, double Time, uint32 Seed, TArray<FVector2f>& Out);
}

// =====================================================================================================
// Presets
// =====================================================================================================

namespace WidgetPathPresets
{
	/** The state names the presets, the rules and Derive Available use. */
	NAMETBD_API FName Locked();
	NAMETBD_API FName Available();
	NAMETBD_API FName Unlocked();
	NAMETBD_API FName Unavailable();

	/** The style names the presets make. */
	NAMETBD_API FName LockedStyle();
	NAMETBD_API FName AvailableStyle();
	NAMETBD_API FName UnlockedStyle();
	NAMETBD_API FName UnavailableStyle();
	NAMETBD_API FName HighlightedStyle();

	/** Fills routing, styles and rules for Preset (nothing for Custom). */
	NAMETBD_API void Apply(EWidgetPathPreset Preset, FWidgetPathRouting& OutRouting, TArray<FWidgetPathStyle>& OutStyles, TArray<FWidgetPathStyleRule>& OutRules);
}

/**
 * A Widget Path look of your own, kept as an asset and picked on any Widget Path Layer (its Preset Asset), so a game's
 * trees all share one look: the routing, the styles and the rules. Made from a layer with Save As Preset (its Details), or
 * from the Content Browser (Miscellaneous > Data Asset > Widget Path Preset) and filled in here.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Widget Path Preset"))
class NAMETBD_API UWidgetPathPresetAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UWidgetPathPresetAsset();

	/** Puts its Routing on the layer too (off: only the styles and rules, the layer keeps how its lines run). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Widget Path Preset", meta = (DisplayName = "Apply Routing"))
	bool bApplyRouting = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Routing", meta = (EditCondition = "bApplyRouting"))
	FWidgetPathRouting Routing;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Styles", meta = (TitleProperty = "Name"))
	TArray<FWidgetPathStyle> Styles;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Styles", meta = (TitleProperty = "Style"))
	TArray<FWidgetPathStyleRule> Rules;

	/** The style names offered in the rules' dropdowns. */
	UFUNCTION()
	TArray<FString> GetStyleOptions() const;

	/** Widget names have no tree here: only None. */
	UFUNCTION()
	TArray<FString> GetWidgetOptions() const;

	// UObject
	virtual void PostLoad() override;
};
